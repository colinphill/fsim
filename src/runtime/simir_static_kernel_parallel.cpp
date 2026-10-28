// SPDX-License-Identifier: Apache-2.0
//
// Parallel level batches of the static kernel (multithread proposal,
// .local-artifacts/v4-migration/design-opt/multithread-proposal.md, §2.3).
//
// settle() runs each level as a snapshot: the units queued at the level when
// the pass starts, in canonical order. Units of one level touch no common
// slot (build_parallel_plan checks it), so the eligible units of a snapshot
// may run at the same time. Workers run them with their effects logged
// instead of applied: change notifications (slot_changed), queued nonblocking
// writes and element writes go to the worker's lane. The leader then walks
// the snapshot in order. It replays each unit's log as the serial kernel
// would have made it and runs the units that are not eligible in place, so
// the queues, the wakes and every later order are the serial kernel's
// whatever the thread count.
#include "simir_static_kernel_state.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <immintrin.h>
#endif

namespace fsim::runtime::simir {

using namespace static_kernel_detail;

FSIM_STATIC_KERNEL_TLS thread_local Interpreter::Impl::StaticKernel::Lane*
    Interpreter::Impl::StaticKernel::worker_lane_ = nullptr;

namespace static_kernel_detail {

namespace {

void cpu_relax() noexcept
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

} // namespace

KernelPool::KernelPool(const std::uint32_t lanes, const std::uint32_t spin)
    : lanes_(std::max(lanes, 1U))
    , spin_(spin)
{
    threads_.reserve(lanes_ - 1U);
    for (std::uint32_t lane = 1U; lane < lanes_; ++lane) {
        threads_.emplace_back([this, lane] { work(lane); });
    }
}

KernelPool::~KernelPool()
{
    stop_.store(true, std::memory_order_release);
    const auto epoch = ((state_.load(std::memory_order_relaxed) >> epoch_shift) + 1U)
        & epoch_mask;
    state_.store(epoch << epoch_shift, std::memory_order_release);
    state_.notify_all();
    for (auto& thread : threads_) {
        thread.join();
    }
}

void KernelPool::run(const std::uint32_t count, const Body& body)
{
    body_.store(&body, std::memory_order_relaxed);
    // Claims of a few items each: units are often tens of nanoseconds, the
    // claim itself a contended cache line.
    chunk_.store(std::max(1U, count / (lanes_ * 4U)), std::memory_order_relaxed);
    completed_.store(0U, std::memory_order_relaxed);
    const auto epoch = ((state_.load(std::memory_order_relaxed) >> epoch_shift) + 1U)
        & epoch_mask;
    const auto word = (epoch << epoch_shift)
        | (static_cast<std::uint64_t>(count) << count_shift);
    state_.store(word, std::memory_order_release);
    state_.notify_all();
    take(0U, word);
    while (completed_.load(std::memory_order_acquire) != count) {
        cpu_relax();
    }
}

void KernelPool::take(const std::uint32_t lane, std::uint64_t word)
{
    const auto epoch = word >> epoch_shift;
    for (;;) {
        // The batch, its size and the next item come from one word, so a
        // claim can only take items of the batch it saw.
        const auto item = static_cast<std::uint32_t>(word & item_mask);
        const auto count = static_cast<std::uint32_t>((word >> count_shift) & item_mask);
        if ((word >> epoch_shift) != epoch || item >= count) {
            return;
        }
        const auto end = std::min(count, item + chunk_.load(std::memory_order_relaxed));
        if (state_.compare_exchange_weak(word, (word & ~item_mask) | end,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            const auto* const body = body_.load(std::memory_order_relaxed);
            for (auto at = item; at < end; ++at) {
                (*body)(lane, at);
            }
            completed_.fetch_add(end - item, std::memory_order_release);
            word = state_.load(std::memory_order_acquire);
        }
    }
}

void KernelPool::work(const std::uint32_t lane)
{
    std::uint64_t seen = 0U;
    for (;;) {
        auto word = state_.load(std::memory_order_acquire);
        for (std::uint32_t spin = 0U; (word >> epoch_shift) == seen; ++spin) {
            if (spin < spin_) {
                cpu_relax();
            } else {
                state_.wait(word, std::memory_order_acquire);
            }
            word = state_.load(std::memory_order_acquire);
        }
        seen = word >> epoch_shift;
        if (stop_.load(std::memory_order_acquire)) {
            return;
        }
        take(lane, word);
    }
}

} // namespace static_kernel_detail

bool Interpreter::Impl::StaticKernel::parallel_now(const std::uint32_t entry) const noexcept
{
    if ((entry & partition_tag) != 0U) {
        const auto partition = entry & ~partition_tag;
        return unit_parallel_[members_.size() + partition] != 0U
            && partitions_[partition].native.entry != nullptr;
    }
    return unit_parallel_[entry] != 0U && fast_runs_[entry].entry != nullptr;
}

void Interpreter::Impl::StaticKernel::build_parallel_plan()
{
    parallel_planned_ = true;
    // The serial kernel only: profiles, traces, the verifier and the
    // debugger observe every run in order, and VHDL rounds are not batched
    // yet.
    if (mt_threads_ < 2U || profile_ || trace_ || verify_ || vhdl_ || mixed_
        || debug_points_) {
        return;
    }
    const auto member_count = static_cast<std::uint32_t>(members_.size());
    const auto unit_total = member_count + static_cast<std::uint32_t>(partitions_.size());
    unit_parallel_.assign(unit_total, 0U);
    unit_cost_.assign(unit_total, 0U);
    // The units that may share a batch form a group: the combinational
    // units of one level (settle snapshots), and all sequential members
    // (triggered rounds), after the levels.
    std::vector<std::uint32_t> group(unit_total, no_slot);
    std::vector<const CompiledBody*> body(unit_total, nullptr);
    std::uint32_t sequential_group = 0U;
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
        if (member.kind == StaticKernelMemberKind::combinational
            && member.partition == no_slot && !member.vhdl) {
            group[index] = member_schedule_[index].level;
            sequential_group = std::max(sequential_group, group[index] + 1U);
        }
    }
    for (std::uint32_t partition = 0U; partition < partitions_.size(); ++partition) {
        group[member_count + partition] = partition_level_[partition];
        body[member_count + partition] = &partitions_[partition].program;
        sequential_group = std::max(sequential_group, partition_level_[partition] + 1U);
    }
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
        if (member.vhdl || member.partition != no_slot) {
            continue;
        }
        if (member.kind == StaticKernelMemberKind::sequential) {
            group[index] = sequential_group;
        }
        if (group[index] != no_slot) {
            body[index] = member.compiled ? &*member.compiled : nullptr;
        }
    }
    // Resources a unit touches in its phase: slots, containers (after the
    // slots; an element slot is also its container) and host signals.
    std::unordered_map<std::uint32_t, std::uint32_t> slot_of_offset;
    for (std::uint32_t slot = 0U; slot < slots_.size(); ++slot) {
        slot_of_offset.emplace(slots_[slot].offset, slot);
    }
    const auto slot_count = static_cast<std::uint32_t>(slots_.size());
    std::vector<std::uint32_t> container_of_slot(slot_count, no_slot);
    for (std::uint32_t container = 0U; container < containers_.size(); ++container) {
        for (const auto slot : containers_[container].element_slots) {
            if (slot < slot_count) {
                container_of_slot[slot] = container;
            }
        }
        if (containers_[container].packed_slot < slot_count) {
            container_of_slot[containers_[container].packed_slot] = container;
        }
    }
    struct Touch {
        std::uint32_t level;
        std::uint32_t unit;
        bool write;
    };
    std::vector<std::vector<Touch>> touches(slot_count + containers_.size());
    std::unordered_map<std::uint32_t, std::vector<Touch>> host_touches;
    std::vector<std::uint8_t> opaque_level(sequential_group + 1U, 0U);
    std::vector<std::uint8_t> eligible(unit_total, 0U);
    const auto signal_count = static_cast<SignalId>(slot_of_signal_.size());
    for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
        if (group[unit] == no_slot) {
            continue;
        }
        if (body[unit] == nullptr) {
            // Interpreted: what it touches is not in compiled code.
            if (opaque_level[group[unit]] == 0U && std::getenv("FSIM_KERNEL_MT_STATS") != nullptr) {
                std::cerr << "fsim-kernel: mt-plan opaque group=" << group[unit] << " uncompiled\n";
            }
            opaque_level[group[unit]] = 1U;
            continue;
        }
        unit_cost_[unit] = static_cast<std::uint32_t>(body[unit]->code.size());
        bool ok = true;
        const auto touch_slot = [&](const std::uint32_t slot, const bool write) {
            if (slot >= slot_count) {
                return;
            }
            touches[slot].push_back({ group[unit], unit, write });
            if (container_of_slot[slot] != no_slot) {
                touches[slot_count + container_of_slot[slot]].push_back(
                    { group[unit], unit, write });
            }
        };
        const auto touch_signal = [&](const SignalId signal, const bool write) {
            if (signal >= signal_count) {
                return;
            }
            if (slot_of_signal_[signal] != no_slot) {
                touch_slot(slot_of_signal_[signal], write);
            } else if (family_of_signal_[signal] != no_slot) {
                for (const auto& leaf : families_[family_of_signal_[signal]].leaves) {
                    touch_slot(leaf.slot, write);
                }
            } else {
                host_touches[static_cast<std::uint32_t>(signal)].push_back(
                    { group[unit], unit, write });
            }
        };
        const auto touch_container = [&](const std::uint32_t container, const bool write) {
            if (container < containers_.size()) {
                touches[slot_count + container].push_back({ group[unit], unit, write });
            }
        };
        // An instruction the reference evaluator runs (generic, step): its
        // operation's signal accesses, or nothing known.
        const auto touch_operation = [&](const std::uint32_t member, const std::uint32_t at) {
            if (member >= member_count || at >= members_[member].operations.size()) {
                opaque_level[group[unit]] = 1U;
                return;
            }
            const auto& state = members_[member];
            const auto object_container = [&](const ContainerObjectId object) {
                return object < container_of_object_.size()
                    ? container_of_object_[object] : no_container;
            };
            // A register is private storage or names the object a
            // ReadContainerObject of this member binds to it.
            const auto touch_register = [&](const ContainerRegisterId reg, const bool write) {
                bool found = false;
                if (reg < state.container_registers.size()
                    && state.container_registers[reg] != no_container) {
                    touch_container(state.container_registers[reg], write);
                    found = true;
                }
                for (const auto& other : state.operations) {
                    const auto* const bind = operation_get_if<ReadContainerObject>(&other);
                    if (bind != nullptr && bind->destination == reg) {
                        const auto container = object_container(bind->object);
                        if (container == no_container) {
                            return false;
                        }
                        touch_container(container, write);
                        found = true;
                    }
                }
                return found;
            };
            const auto known = visit_operation([&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, Report>) {
                    return true;
                } else if constexpr (std::is_same_v<T, IntegerBinary>
                    || std::is_same_v<T, IntegerUnary>
                    || std::is_same_v<T, IntegerCheck>
                    || std::is_same_v<T, Assert>
                    || std::is_same_v<T, CodeCoverageHit>
                    || std::is_same_v<T, ReadContainerObject>) {
                    // Registers only (a binding's object is touched by the
                    // reads and writes through it).
                    return true;
                } else if constexpr (std::is_same_v<T, ContainerRead>) {
                    return touch_register(op.source, false);
                } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                    return touch_register(op.target, true);
                } else if constexpr (std::is_same_v<T, CopyContainerRegister>) {
                    return touch_register(op.source, false)
                        && touch_register(op.destination, true);
                } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
                    const auto container = object_container(op.object);
                    if (container == no_container) {
                        return false;
                    }
                    touch_container(container, !op.nonblocking);
                    return true;
                } else if constexpr (std::is_same_v<T, ReadSignal>) {
                    touch_signal(op.signal, false);
                    return true;
                } else if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>) {
                    touch_signal(op.signal, true);
                    return true;
                } else if constexpr (std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    if (op.domain != SignalUpdateDomain::systemverilog_nba) {
                        touch_signal(op.signal, true);
                    }
                    return true;
                } else {
                    return false;
                }
            }, members_[member].operations[at]);
            if (!known) {
                if (opaque_level[group[unit]] == 0U && std::getenv("FSIM_KERNEL_MT_STATS") != nullptr) {
                    std::cerr << "fsim-kernel: mt-plan opaque group=" << group[unit]
                              << " operation=" << visit_operation([](const auto& op) {
                                     return typeid(op).name();
                                 }, members_[member].operations[at])
                              << " in " << program_of(members_[member]).name() << '\n';
                }
                opaque_level[group[unit]] = 1U;
            }
        };
        std::uint32_t current = unit < member_count ? unit : no_slot;
        for (const auto& inst : body[unit]->code) {
            const bool nba = (inst.flags & flag_nba) != 0U;
            switch (inst.op) {
            case KOp::mark:
                current = inst.x;
                break;
            case KOp::load_slot:
                if (const auto found = slot_of_offset.find(inst.x);
                    found != slot_of_offset.end()) {
                    touch_slot(found->second, false);
                }
                break;
            case KOp::load_field:
            case KOp::load_slot9:
            case KOp::load_field9:
            case KOp::wide_load:
                touch_slot(inst.x, false);
                break;
            case KOp::dynamic_extract:
            case KOp::dynamic_part_select:
                if (inst.sub == 1U) {
                    touch_slot(inst.x, false);
                }
                break;
            case KOp::store_slot:
                touch_slot(inst.d, true);
                break;
            case KOp::store_slot_dynamic:
            case KOp::store_slot_part:
                if (!nba) {
                    touch_slot(inst.d, true);
                }
                break;
            case KOp::wide_move:
                touch_slot(inst.x, false);
                if (!nba) {
                    touch_slot(inst.d, true);
                }
                break;
            case KOp::wide_store:
                if (inst.sub == 0U) {
                    touch_slot(inst.d, true);
                } else if (inst.sub != 1U) {
                    ok = false;
                }
                break;
            case KOp::load_host:
                host_touches[inst.x].push_back({ group[unit], unit, false });
                break;
            case KOp::mem_read:
                touch_container(inst.x, false);
                break;
            case KOp::mem_write:
                // A blocking element write's corner cases take the reference
                // path, which writes the element's slot or kernel storage
                // and notifies through slot_changed: lane-safe.
                touch_container(inst.d, !nba);
                break;
            case KOp::store_host:
                host_touches[inst.d].push_back({ group[unit], unit, true });
                ok = false;
                break;
            case KOp::mem_copy:
                touch_container(inst.x, false);
                touch_container(inst.d, true);
                ok = false;
                break;
            case KOp::generic:
            case KOp::step:
                touch_operation(current, inst.x);
                ok = false;
                break;
            case KOp::suspend:
                if (opaque_level[group[unit]] == 0U && std::getenv("FSIM_KERNEL_MT_STATS") != nullptr) {
                    std::cerr << "fsim-kernel: mt-plan opaque group=" << group[unit] << " suspend\n";
                }
                opaque_level[group[unit]] = 1U;
                ok = false;
                break;
            case KOp::deopt:
            case KOp::store_vhdl:
            case KOp::assert_check:
            case KOp::integer_check:
                ok = false;
                break;
            default:
                break;
            }
        }
        eligible[unit] = ok ? 1U : 0U;
    }
    const bool diagnose = std::getenv("FSIM_KERNEL_MT_STATS") != nullptr;
    // Domains (proposal §2.4): the combinational units joined by a resource
    // they share with a write, and by the notifications between them. A
    // component with a unit a lane cannot run, or any combinational unit
    // whose accesses are unknown, stays in the global queue (FSIM_KERNEL_MT_
    // DOMAINS=0: no domains).
    const char* domains_switch = std::getenv("FSIM_KERNEL_MT_DOMAINS");
    bool comb_opaque = false;
    for (std::uint32_t level = 0U; level < sequential_group; ++level) {
        comb_opaque = comb_opaque || opaque_level[level] != 0U;
    }
    if ((domains_switch == nullptr || std::string_view { domains_switch } != "0")
        && !comb_opaque) {
        const auto comb = [&](const std::uint32_t unit) {
            return group[unit] != no_slot && group[unit] < sequential_group;
        };
        const auto unit_of_member = [&](const std::uint32_t member) {
            return members_[member].partition != no_slot
                ? member_count + members_[member].partition : member;
        };
        std::vector<std::uint32_t> parent(unit_total);
        for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
            parent[unit] = unit;
        }
        const auto find = [&](std::uint32_t at) {
            while (parent[at] != at) {
                parent[at] = parent[parent[at]];
                at = parent[at];
            }
            return at;
        };
        const auto unite = [&](const std::uint32_t left, const std::uint32_t right) {
            const auto a = find(left);
            const auto b = find(right);
            if (a != b) {
                parent[std::max(a, b)] = std::min(a, b);
            }
        };
        const auto join_shared = [&](const std::vector<Touch>& list) {
            bool write = false;
            for (const auto& touch : list) {
                write = write || (touch.write && comb(touch.unit));
            }
            if (!write) {
                return;
            }
            std::uint32_t first = no_slot;
            for (const auto& touch : list) {
                if (!comb(touch.unit)) {
                    continue;
                }
                if (first == no_slot) {
                    first = touch.unit;
                } else {
                    unite(first, touch.unit);
                }
            }
        };
        for (const auto& list : touches) {
            join_shared(list);
        }
        for (const auto& [signal, list] : host_touches) {
            join_shared(list);
        }
        // A combinational writer's notifications reach its whole-slot
        // targets and ranged readers: a combinational one joins it.
        for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
            std::uint32_t writer = no_slot;
            for (const auto& touch : touches[slot]) {
                if (touch.write && comb(touch.unit)) {
                    writer = touch.unit;
                    break;
                }
            }
            if (writer == no_slot) {
                continue;
            }
            const auto notify = slot_notify_[slot];
            for (auto index = notify.begin; index < notify.end; ++index) {
                const auto id = notify_targets_[index].id;
                const auto unit = (id & partition_tag) != 0U
                    ? member_count + (id & ~partition_tag) : unit_of_member(id);
                if (comb(unit)) {
                    unite(writer, unit);
                }
            }
            for (const auto& reader : slots_[slot].readers) {
                const auto unit = unit_of_member(reader.member);
                if (comb(unit)) {
                    unite(writer, unit);
                }
            }
        }
        std::vector<std::uint8_t> serial_root(unit_total, 0U);
        for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
            if (comb(unit) && (eligible[unit] == 0U || body[unit] == nullptr)) {
                serial_root[find(unit)] = 1U;
            }
        }
        unit_domain_.assign(unit_total, no_slot);
        unit_bucket_.assign(unit_total, no_slot);
        unit_bit_.assign(unit_total, no_slot);
        std::unordered_map<std::uint32_t, std::uint32_t> domain_of_root;
        std::vector<std::vector<std::uint32_t>> domain_units;
        for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
            if (!comb(unit) || serial_root[find(unit)] != 0U) {
                continue;
            }
            const auto [found, added] = domain_of_root.emplace(find(unit),
                static_cast<std::uint32_t>(domain_units.size()));
            if (added) {
                domain_units.emplace_back();
            }
            unit_domain_[unit] = found->second;
            domain_units[found->second].push_back(unit);
        }
        if (domain_units.size() >= 2U) {
            domains_.resize(domain_units.size());
            for (std::uint32_t index = 0U; index < domain_units.size(); ++index) {
                auto& domain = domains_[index];
                auto& units = domain_units[index];
                const auto entry_of = [&](const std::uint32_t unit) {
                    return unit < member_count ? unit
                                               : (unit - member_count) | partition_tag;
                };
                std::ranges::sort(units, [&](const std::uint32_t left, const std::uint32_t right) {
                    return std::pair { group[left], queue_position(entry_of(left)) }
                        < std::pair { group[right], queue_position(entry_of(right)) };
                });
                for (const auto unit : units) {
                    if (domain.levels.empty() || domain.levels.back() != group[unit]) {
                        domain.levels.push_back(group[unit]);
                        domain.buckets.emplace_back();
                    }
                    auto& bucket = domain.buckets.back();
                    unit_bucket_[unit] = static_cast<std::uint32_t>(domain.buckets.size() - 1U);
                    unit_bit_[unit] = static_cast<std::uint32_t>(bucket.entries.size());
                    bucket.entries.push_back(entry_of(unit));
                }
                for (const auto unit : units) {
                    domain.cost += unit_cost_[unit];
                }
                // A cache line or more apiece: lanes write them.
                domain.snapshot.reserve(16U);
                for (auto& bucket : domain.buckets) {
                    bucket.bits.reserve(std::max<std::size_t>(8U,
                        (bucket.entries.size() + 63U) / 64U));
                    bucket.bits.assign((bucket.entries.size() + 63U) / 64U, 0U);
                    bucket.cursor = static_cast<std::uint32_t>(bucket.bits.size());
                }
                domain.scan = static_cast<std::uint32_t>(domain.buckets.size());
            }
            // A change goes to the leader when a reader is not a domain unit
            // or it has an output, edges or waiters.
            slot_logged_.assign(slot_count, 0U);
            for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
                const auto& state = slots_[slot];
                const auto notify = slot_notify_[slot];
                bool logged = state.output || !state.edges.empty()
                    || (slot < slot_waitable_.size() && slot_waitable_[slot] != 0U);
                for (auto index = notify.begin; !logged && index < notify.end; ++index) {
                    const auto id = notify_targets_[index].id;
                    const auto unit = (id & partition_tag) != 0U
                        ? member_count + (id & ~partition_tag) : unit_of_member(id);
                    logged = unit_domain_[unit] == no_slot;
                }
                for (const auto& reader : state.readers) {
                    logged = logged || unit_domain_[unit_of_member(reader.member)] == no_slot;
                }
                slot_logged_[slot] = logged ? 1U : 0U;
            }
        } else {
            unit_domain_.clear();
        }
        if (diagnose) {
            std::size_t domain_unit_count = 0U;
            for (const auto& units : domain_units) {
                domain_unit_count += units.size();
            }
            std::cerr << "fsim-kernel: mt-plan domains=" << domains_.size()
                      << " units=" << domain_unit_count << '\n';
            // Components by size: parallel or serial, and a serial one's
            // first unit a lane cannot run.
            std::unordered_map<std::uint32_t, std::tuple<std::uint32_t, std::uint64_t, std::uint32_t>> components;
            for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
                if (!comb(unit)) {
                    continue;
                }
                auto& [units, cost, blocker] = components.try_emplace(find(unit), 0U, 0U, no_slot).first->second;
                ++units;
                cost += unit_cost_[unit];
                if (blocker == no_slot && (eligible[unit] == 0U || body[unit] == nullptr)) {
                    blocker = unit;
                }
            }
            std::vector<std::tuple<std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t>> ranked;
            std::uint32_t serial_components = 0U;
            std::uint32_t serial_units = 0U;
            for (const auto& [root, info] : components) {
                const auto& [units, cost, blocker] = info;
                ranked.emplace_back(cost, units, blocker, root);
                if (blocker != no_slot) {
                    ++serial_components;
                    serial_units += units;
                }
            }
            std::ranges::sort(ranked, std::greater<> { });
            std::cerr << "fsim-kernel: mt-plan components=" << components.size()
                      << " serial=" << serial_components << " serial_units=" << serial_units << '\n';
            for (std::size_t rank = 0U; rank < ranked.size() && rank < 12U; ++rank) {
                const auto [cost, units, blocker, root] = ranked[rank];
                const auto member = [&](const std::uint32_t unit) {
                    return unit < member_count ? unit : partitions_[unit - member_count].members.front();
                };
                std::cerr << "fsim-kernel: mt-plan component cost=" << cost << " units=" << units
                          << " first=" << program_of(members_[member(root)]).name();
                if (blocker != no_slot) {
                    std::cerr << " serial_by=" << program_of(members_[member(blocker)]).name();
                }
                std::cerr << '\n';
            }
        }
    }
    // A resource one unit writes and another touches in the same group would
    // make the batch's result depend on its order: both stay serial (they
    // run on the leader, in order).
    const auto before_conflicts = eligible;
    // Diagnostics: per resource that serializes units, how many it took and
    // one writer.
    std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> serializing;
    const auto check = [&](std::vector<Touch>& list) {
        std::ranges::sort(list, [](const Touch& left, const Touch& right) {
            return std::tie(left.level, left.unit) < std::tie(right.level, right.unit);
        });
        for (std::size_t first = 0U; first < list.size();) {
            auto last = first;
            bool write = false;
            bool shared = false;
            while (last < list.size() && list[last].level == list[first].level) {
                write = write || list[last].write;
                shared = shared || list[last].unit != list[first].unit;
                ++last;
            }
            if (write && shared) {
                std::uint32_t taken = 0U;
                std::uint32_t writer = no_slot;
                for (auto at = first; at < last; ++at) {
                    taken += eligible[list[at].unit];
                    eligible[list[at].unit] = 0U;
                    if (list[at].write && writer == no_slot) {
                        writer = list[at].unit;
                    }
                }
                if (diagnose && taken != 0U) {
                    serializing.emplace_back(taken, writer, list[first].level);
                }
            }
            first = last;
        }
    };
    for (auto& list : touches) {
        check(list);
    }
    for (auto& [signal, list] : host_touches) {
        check(list);
    }
    std::uint32_t parallel_units = 0U;
    for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
        if (eligible[unit] != 0U && opaque_level[group[unit]] == 0U) {
            unit_parallel_[unit] = 1U;
            ++parallel_units;
        }
    }
    if (diagnose) {
        const auto name = [&](const std::uint32_t unit) {
            const auto member = unit < member_count
                ? unit : partitions_[unit - member_count].members.front();
            return std::string { program_of(members_[member]).name() }
                + (unit < member_count ? "" : " (partition)");
        };
        for (const bool sequential : { false, true }) {
            std::uint32_t units = 0U;
            std::uint32_t compiled_ok = 0U;
            std::uint32_t after = 0U;
            std::uint32_t opaque = 0U;
            for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
                if (group[unit] == no_slot
                    || (group[unit] == sequential_group) != sequential) {
                    continue;
                }
                ++units;
                compiled_ok += before_conflicts[unit];
                after += unit_parallel_[unit];
                opaque += opaque_level[group[unit]];
            }
            std::cerr << "fsim-kernel: mt-plan " << (sequential ? "seq" : "comb")
                      << " units=" << units << " ops_ok=" << compiled_ok
                      << " parallel=" << after << " in_opaque_groups=" << opaque << '\n';
        }
        std::ranges::sort(serializing, std::greater<> { });
        for (std::size_t rank = 0U; rank < serializing.size() && rank < 6U; ++rank) {
            const auto [taken, writer, level] = serializing[rank];
            std::cerr << "fsim-kernel: mt-plan conflict took=" << taken << " group=" << level
                      << " writer=" << (writer == no_slot ? std::string { "?" } : name(writer))
                      << '\n';
        }
    }
    if (parallel_units < 2U && domains_.empty()) {
        return;
    }
    mt_force_ = std::getenv("FSIM_KERNEL_MT_FORCE") != nullptr;
    mt_stats_.enabled = std::getenv("FSIM_KERNEL_MT_STATS") != nullptr;
    mt_shuffle_ = std::getenv("FSIM_KERNEL_MT_SHUFFLE") != nullptr;
    mt_min_cost_ = 2048U;
    if (const char* minimum = std::getenv("FSIM_KERNEL_MT_MIN")) {
        mt_min_cost_ = std::strtoull(minimum, nullptr, 10);
    }
    std::uint32_t spin = 20000U;
    if (const char* text = std::getenv("FSIM_KERNEL_MT_SPIN")) {
        spin = static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10));
    }
    lanes_.resize(mt_threads_);
    parallel_body_ = [this](const std::uint32_t lane_index, std::uint32_t item) {
        if (mt_shuffle_) {
            item = shuffle_order_[item];
            // A pause of up to about 20 microseconds on one item in four.
            thread_local std::uint64_t pause = 0x2545f4914f6cdd1dULL + lane_index;
            pause ^= pause << 13U;
            pause ^= pause >> 7U;
            pause ^= pause << 17U;
            if ((pause & 3U) == 0U) {
                std::this_thread::sleep_for(std::chrono::microseconds { (pause >> 8U) % 20U });
            }
        }
        auto& run = lane_runs_[item];
        const auto entry = batch_[item];
        run.lane = lane_index;
        run.serial = !parallel_now(entry);
        if (run.serial) {
            return;
        }
        auto& lane = lanes_[lane_index];
        worker_lane_ = &lane;
        run_in_lane(lane, entry, run);
        worker_lane_ = nullptr;
    };
    domain_body_ = [this](const std::uint32_t lane_index, std::uint32_t item) {
        if (mt_shuffle_) {
            item = shuffle_order_[item];
            thread_local std::uint64_t pause = 0x2545f4914f6cdd1dULL + lane_index;
            pause ^= pause << 13U;
            pause ^= pause >> 7U;
            pause ^= pause << 17U;
            if ((pause & 3U) == 0U) {
                std::this_thread::sleep_for(std::chrono::microseconds { (pause >> 8U) % 20U });
            }
        }
        const auto index = active_domains_[item];
        auto& lane = lanes_[lane_index];
        domains_[index].lane = lane_index;
        worker_lane_ = &lane;
        lane.domain = index;
        if (mt_stats_.enabled) {
            const auto domain_started = std::chrono::steady_clock::now();
            run_domain(index, domain_limit_);
            domains_[index].run_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - domain_started).count());
        } else {
            run_domain(index, domain_limit_);
        }
        lane.domain = no_slot;
        worker_lane_ = nullptr;
    };
    pool_ = std::make_unique<KernelPool>(mt_threads_, spin);
}

bool Interpreter::Impl::StaticKernel::run_parallel_batch(const BatchPhase phase,
    std::size_t& evaluations, const std::size_t limit)
{
    const auto count = static_cast<std::uint32_t>(batch_.size());
    // FSIM_KERNEL_MT_PHASES: a mask of the batch phases that may run in
    // parallel (1 settle, 2 merged, 4 flagged; default all).
    static const auto phases = [] {
        const char* text = std::getenv("FSIM_KERNEL_MT_PHASES");
        return text != nullptr ? std::strtoul(text, nullptr, 10) : 7UL;
    }();
    if (count < 2U || (phases & (1UL << static_cast<unsigned>(phase))) == 0U) {
        return false;
    }
    const auto member_count = static_cast<std::uint32_t>(members_.size());
    std::uint32_t eligible = 0U;
    std::uint64_t cost = 0U;
    for (const auto entry : batch_) {
        if (parallel_now(entry)) {
            ++eligible;
            cost += unit_cost_[(entry & partition_tag) != 0U
                    ? member_count + (entry & ~partition_tag) : entry];
        }
    }
    const auto phase_index = static_cast<std::size_t>(phase);
    mt_stats_.offered[phase_index] += mt_stats_.enabled ? 1U : 0U;
    if (eligible < 2U || (!mt_force_ && cost < mt_min_cost_)) {
        if (mt_stats_.enabled) {
            mt_stats_.declined_units[phase_index] += count;
            mt_stats_.declined_eligible[phase_index] += eligible;
            mt_stats_.declined_cost[phase_index] += cost;
        }
        return false;
    }
    const auto batch_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point { };
    if (lane_runs_.size() < count) {
        lane_runs_.resize(count);
    }
    for (auto& lane : lanes_) {
        lane.changes.clear();
        lane.words.clear();
        lane.elements.clear();
        lane.generic.clear();
        lane.writes.count = 0U;
    }
    if (mt_shuffle_) {
        shuffle_items(count);
    }
    // FSIM_KERNEL_MT_INLINE: the leader runs every item itself, in order
    // (the log and replay without concurrency).
    static const bool inline_items = std::getenv("FSIM_KERNEL_MT_INLINE") != nullptr;
    if (inline_items) {
        for (std::uint32_t item = 0U; item < count; ++item) {
            parallel_body_(0U, item);
        }
    } else {
        pool_->run(count, parallel_body_);
    }
    const auto replay_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                                  : std::chrono::steady_clock::time_point { };
    for (std::uint32_t item = 0U; item < count; ++item) {
        const auto entry = batch_[item];
        auto& lane_run = lane_runs_[item];
        if (phase == BatchPhase::settle) {
            if (lane_run.serial) {
                run_settle_entry(entry, evaluations, limit);
                continue;
            }
            if (++evaluations > limit) {
                const auto member = (entry & partition_tag) != 0U
                    ? partitions_[entry & ~partition_tag].members.front() : entry;
                fail(member, members_[member].body_begin,
                    "static kernel combinational logic did not settle");
            }
            replay(entry, lane_run, true);
            continue;
        }
        // run_triggered_round's bookkeeping for the member it runs next.
        if (phase == BatchPhase::merged) {
            ++batch_groups_[item]->next;
        } else {
            member_schedule_[entry].triggered = false;
        }
        if (lane_run.serial) {
            run(entry);
        } else {
            replay(entry, lane_run, false);
        }
    }
    if (mt_stats_.enabled) {
        const auto now = std::chrono::steady_clock::now();
        const auto nanoseconds = [](const auto elapsed) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        };
        ++mt_stats_.batches[phase_index];
        mt_stats_.lane_units.resize(lanes_.size());
        for (std::uint32_t item = 0U; item < count; ++item) {
            if (!lane_runs_[item].serial) {
                ++mt_stats_.lane_units[lane_runs_[item].lane];
            }
        }
        mt_stats_.units[phase_index] += count;
        mt_stats_.eligible[phase_index] += eligible;
        mt_stats_.run_ns[phase_index] += nanoseconds(replay_started - batch_started);
        mt_stats_.replay_ns[phase_index] += nanoseconds(now - replay_started);
    }
    return true;
}

void Interpreter::Impl::StaticKernel::report_mt_stats() const
{
    static constexpr std::array names { "settle", "merged", "flagged" };
    for (std::size_t phase = 0U; phase < names.size(); ++phase) {
        std::cerr << "fsim-kernel: mt-stats " << names[phase]
                  << " offered=" << mt_stats_.offered[phase]
                  << " parallel=" << mt_stats_.batches[phase]
                  << " units=" << mt_stats_.units[phase]
                  << " eligible=" << mt_stats_.eligible[phase]
                  << " run_ms=" << static_cast<double>(mt_stats_.run_ns[phase]) / 1e6
                  << " replay_ms=" << static_cast<double>(mt_stats_.replay_ns[phase]) / 1e6
                  << " declined_units=" << mt_stats_.declined_units[phase]
                  << " declined_eligible=" << mt_stats_.declined_eligible[phase]
                  << " declined_cost=" << mt_stats_.declined_cost[phase]
                  << '\n';
    }
    std::cerr << "fsim-kernel: mt-stats domains settles=" << mt_stats_.domain_settles
              << " declined=" << mt_stats_.domain_declined
              << " active=" << mt_stats_.domain_active
              << " records=" << mt_stats_.domain_records
              << " changes=" << mt_stats_.domain_changes
              << " run_ms=" << static_cast<double>(mt_stats_.domain_run_ns) / 1e6
              << " merge_ms=" << static_cast<double>(mt_stats_.domain_merge_ns) / 1e6
              << " merges=" << mt_stats_.domain_merges
              << " work_ms=" << static_cast<double>(mt_stats_.domain_work_ns) / 1e6
              << " critical_ms=" << static_cast<double>(mt_stats_.domain_critical_ns) / 1e6
              << '\n';
    std::cerr << "fsim-kernel: mt-stats leader settle_ms="
              << static_cast<double>(mt_stats_.phase_ns[0]) / 1e6
              << " triggered_ms=" << static_cast<double>(mt_stats_.phase_ns[1]) / 1e6
              << " activations_ms=" << static_cast<double>(mt_stats_.phase_ns[2]) / 1e6
              << '\n';
    std::cerr << "fsim-kernel: mt-stats lane_units=";
    for (const auto units : mt_stats_.lane_units) {
        std::cerr << ' ' << units;
    }
    std::cerr << '\n';
}

std::uint32_t Interpreter::Impl::StaticKernel::queue_position(
    const std::uint32_t entry) const noexcept
{
    return (entry & partition_tag) != 0U
        ? partition_queue_slot_[entry & ~partition_tag]
        : member_schedule_[entry].queue_slot;
}

bool Interpreter::Impl::StaticKernel::native_ready(const std::uint32_t entry) const noexcept
{
    return (entry & partition_tag) != 0U
        ? partitions_[entry & ~partition_tag].native.entry != nullptr
        : fast_runs_[entry].entry != nullptr;
}

void Interpreter::Impl::StaticKernel::enqueue_domain(Domain& domain,
    const std::uint32_t entry)
{
    const auto unit = unit_of_entry(entry);
    const auto bucket = unit_bucket_[unit];
    const auto bit = unit_bit_[unit];
    auto& queue = domain.buckets[bucket];
    queue.bits[bit / 64U] |= std::uint64_t { 1 } << (bit % 64U);
    ++queue.count;
    queue.cursor = std::min(queue.cursor, bit / 64U);
    ++domain.queued;
    domain.scan = std::min(domain.scan, bucket);
}

void Interpreter::Impl::StaticKernel::schedule_in_domain(Lane& lane,
    const std::uint32_t entry)
{
    // The plan joins every combinational reader of a slot to its writers'
    // domain; anything else is a unit the lane does not schedule. The
    // domain's bit is the unit's queued flag.
    const auto unit = unit_of_entry(entry);
    if (unit_domain_[unit] != lane.domain) {
        return;
    }
    auto& domain = domains_[lane.domain];
    const auto bucket = unit_bucket_[unit];
    const auto bit = unit_bit_[unit];
    auto& queue = domain.buckets[bucket];
    const auto mask = std::uint64_t { 1 } << (bit % 64U);
    if ((queue.bits[bit / 64U] & mask) != 0U) {
        return;
    }
    queue.bits[bit / 64U] |= mask;
    ++queue.count;
    queue.cursor = std::min(queue.cursor, bit / 64U);
    ++domain.queued;
    domain.scan = std::min(domain.scan, bucket);
}

Interpreter::Impl::StaticKernel::LaneRun& Interpreter::Impl::StaticKernel::add_domain_record(
    Domain& domain, const std::uint32_t entry, const bool continued)
{
    const auto record = domain.records.size();
    domain.records.push_back({ entry, continued });
    if (domain.runs.size() <= record) {
        domain.runs.resize(std::max<std::size_t>(16U, 2U * domain.runs.size()));
    }
    auto& run = domain.runs[record];
    run.lane = domain.lane;
    run.two_state = false;
    run.status = 0U;
    run.frame.status = 0U;
    run.deopt.reset();
    run.exception = nullptr;
    return run;
}

void Interpreter::Impl::StaticKernel::run_domain(const std::uint32_t index,
    const std::size_t limit)
{
    auto& domain = domains_[index];
    auto& lane = *worker_lane_;
    auto& scratch = lane.scratch;
    domain.stop = domain_running;
    for (;;) {
        if (domain.next == domain.snapshot.size()) {
            if (domain.queued == 0U) {
                return;
            }
            // The domain's share of settle()'s snapshot pass at its lowest
            // queued level.
            while (domain.buckets[domain.scan].count == 0U) {
                ++domain.scan;
            }
            auto& queue = domain.buckets[domain.scan];
            domain.snapshot.clear();
            auto remaining = queue.count;
            domain.queued -= remaining;
            queue.count = 0U;
            for (auto word_index = queue.cursor; remaining != 0U; ++word_index) {
                auto word = queue.bits[word_index];
                if (word == 0U) {
                    continue;
                }
                queue.bits[word_index] = 0U;
                remaining -= static_cast<std::uint32_t>(std::popcount(word));
                for (; word != 0U; word &= word - 1U) {
                    domain.snapshot.push_back(queue.entries[word_index * 64U
                        + static_cast<std::uint32_t>(std::countr_zero(word))]);
                }
            }
            queue.cursor = static_cast<std::uint32_t>(queue.bits.size());
            domain.next = 0U;
            domain.passes.emplace_back(domain.levels[domain.scan],
                static_cast<std::uint32_t>(domain.records.size()));
        }
        const auto entry = domain.snapshot[domain.next];
        if (domain.evaluations >= limit) {
            domain.stop = domain_limit;
            return;
        }
        if (!native_ready(entry)) {
            domain.stop = domain_unit;
            return;
        }
        ++domain.evaluations;
        auto& run = add_domain_record(domain, entry, false);
        if ((entry & partition_tag) != 0U) {
            scratch.running_partition = entry & ~partition_tag;
        } else {
            scratch.running_member = entry;
        }
        run_in_lane(lane, entry, run);
        scratch.running_partition = no_slot;
        scratch.running_member = no_slot;
        ++domain.next;
        if (run.status != 0U || run.frame.status != 0U) {
            domain.stop = domain_finish;
            return;
        }
    }
}

void Interpreter::Impl::StaticKernel::resume_domain(const std::uint32_t index,
    const std::size_t limit)
{
    // The leader, between the pool's run and the merge: what a lane cannot
    // do (finish a deoptimized or failed run, run a unit without native
    // code) in lane mode on the domain's lane, then the domain goes on.
    auto& domain = domains_[index];
    auto& lane = lanes_[domain.lane];
    worker_lane_ = &lane;
    lane.domain = index;
    auto& scratch = lane.scratch;
    while (domain.stop == domain_finish || domain.stop == domain_unit) {
        const bool finish = domain.stop == domain_finish;
        const auto entry = finish ? domain.records.back().entry : domain.snapshot[domain.next];
        if (!finish) {
            if (domain.evaluations >= limit) {
                domain.stop = domain_limit;
                break;
            }
            ++domain.evaluations;
            ++domain.next;
        }
        const auto stopped = static_cast<std::uint32_t>(domain.records.size() - 1U);
        auto& record = add_domain_record(domain, entry, finish);
        record.changes_begin = static_cast<std::uint32_t>(lane.changes.size());
        record.writes_begin = lane.writes.count;
        const bool partition = (entry & partition_tag) != 0U;
        try {
            if (finish) {
                auto& first = domain.runs[stopped];
                first.frame.kernel = this;
                first.frame.writes = &lane.writes;
                scratch.running_position = first.frame.position;
                scratch.pending_deopt = std::move(first.deopt);
                scratch.native_exception = std::move(first.exception);
                const auto status = std::exchange(first.status, 0U);
                if (partition) {
                    auto& state = partitions_[entry & ~partition_tag];
                    scratch.running_partition = entry & ~partition_tag;
                    finish_native(state.native, state.program, state.members.front(),
                        first.frame, status, false);
                } else {
                    auto& member = members_[entry];
                    scratch.running_member = entry;
                    finish_native(member.native, *member.compiled, entry, first.frame,
                        status, false);
                    refresh_fast_run(entry);
                }
                first.frame.status = 0U;
            } else if (partition) {
                run_partition(entry & ~partition_tag);
            } else {
                scratch.running_member = entry;
                run(entry);
            }
        } catch (...) {
            record.status = 1U;
            record.exception = std::current_exception();
            domain.stop = domain_error;
        }
        scratch.running_partition = no_slot;
        scratch.running_member = no_slot;
        scratch.pending_deopt.reset();
        scratch.native_exception = nullptr;
        record.changes_end = static_cast<std::uint32_t>(lane.changes.size());
        record.writes_end = lane.writes.count;
        if (domain.stop == domain_error) {
            break;
        }
        run_domain(index, limit);
    }
    lane.domain = no_slot;
    worker_lane_ = nullptr;
}

void Interpreter::Impl::StaticKernel::replay_domain_record(Domain& domain,
    const std::uint32_t record, std::size_t& evaluations, const std::size_t limit)
{
    const auto entry = domain.records[record].entry;
    if (++evaluations > limit) {
        const auto member = (entry & partition_tag) != 0U
            ? partitions_[entry & ~partition_tag].members.front() : entry;
        fail(member, members_[member].body_begin,
            "static kernel combinational logic did not settle");
    }
    const auto count = static_cast<std::uint32_t>(domain.records.size());
    for (auto at = record; at < count && (at == record || domain.records[at].continued); ++at) {
        auto& run = domain.runs[at];
        auto error = std::exchange(run.exception, nullptr);
        run.status = 0U;
        run.frame.status = 0U;
        replay_external_ = true;
        try {
            replay(entry, run, true);
        } catch (...) {
            replay_external_ = false;
            throw;
        }
        replay_external_ = false;
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

void Interpreter::Impl::StaticKernel::abandon_domains() noexcept
{
    replay_external_ = false;
    reconstructing_ = false;
    for (const auto index : active_domains_) {
        auto& domain = domains_[index];
        for (auto& queue : domain.buckets) {
            // The shared queued flags of domain units are clear.
            std::ranges::fill(queue.bits, 0U);
            queue.count = 0U;
            queue.cursor = static_cast<std::uint32_t>(queue.bits.size());
        }
        domain.queued = 0U;
        domain.scan = static_cast<std::uint32_t>(domain.buckets.size());
        domain.snapshot.clear();
        domain.next = 0U;
    }
    active_domains_.clear();
}

void Interpreter::Impl::StaticKernel::shuffle_items(const std::uint32_t count)
{
    shuffle_order_.resize(count);
    for (std::uint32_t item = 0U; item < count; ++item) {
        shuffle_order_[item] = item;
    }
    for (auto item = count - 1U; item > 0U; --item) {
        shuffle_state_ ^= shuffle_state_ << 13U;
        shuffle_state_ ^= shuffle_state_ >> 7U;
        shuffle_state_ ^= shuffle_state_ << 17U;
        std::swap(shuffle_order_[item],
            shuffle_order_[static_cast<std::uint32_t>(shuffle_state_ % (item + 1U))]);
    }
}

bool Interpreter::Impl::StaticKernel::settle_domains(std::size_t& evaluations,
    const std::size_t limit)
{
    // The queued domain units, and how many domains they wake.
    if (domain_seen_.size() != domains_.size()) {
        domain_seen_.assign(domains_.size(), 0U);
    }
    ++domain_epoch_;
    domain_moves_.clear();
    active_domains_.clear();
    std::uint64_t cost = 0U;
    std::size_t seen = 0U;
    for (auto level = scan_level_; seen < queued_count_ && level < buckets_.size(); ++level) {
        const auto& queue = buckets_[level];
        if (queue.count == 0U) {
            continue;
        }
        seen += queue.count;
        for (auto word_index = queue.cursor; word_index < queue.bits.size(); ++word_index) {
            for (auto word = queue.bits[word_index]; word != 0U; word &= word - 1U) {
                const auto entry = queue.entries[word_index * 64U
                    + static_cast<std::uint32_t>(std::countr_zero(word))];
                const auto unit = unit_of_entry(entry);
                const auto domain = unit_domain_[unit];
                if (domain == no_slot) {
                    continue;
                }
                domain_moves_.emplace_back(level, entry);
                cost += unit_cost_[unit];
                if (domain_seen_[domain] != domain_epoch_) {
                    domain_seen_[domain] = domain_epoch_;
                    active_domains_.push_back(domain);
                }
            }
        }
    }
    if (active_domains_.size() < 2U || (!mt_force_ && cost < mt_min_cost_)) {
        mt_stats_.domain_declined += mt_stats_.enabled ? 1U : 0U;
        active_domains_.clear();
        return false;
    }
    const auto run_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point { };
    for (const auto& [level, entry] : domain_moves_) {
        auto& queue = buckets_[level];
        const auto slot = queue_position(entry);
        queue.bits[slot / 64U] &= ~(std::uint64_t { 1 } << (slot % 64U));
        --queue.count;
        --queued_count_;
        if ((entry & partition_tag) != 0U) {
            partition_queued_[entry & ~partition_tag] = 0U;
        } else {
            member_schedule_[entry].queued = false;
        }
        enqueue_domain(domains_[unit_domain_[unit_of_entry(entry)]], entry);
    }
    for (const auto index : active_domains_) {
        auto& domain = domains_[index];
        domain.passes.clear();
        domain.records.clear();
        domain.snapshot.clear();
        domain.next = 0U;
        domain.evaluations = 0U;
        domain.stop = domain_running;
        domain.pass = 0U;
    }
    for (auto& lane : lanes_) {
        lane.changes.clear();
        lane.words.clear();
        lane.elements.clear();
        lane.generic.clear();
        lane.writes.count = 0U;
    }
    const auto count = static_cast<std::uint32_t>(active_domains_.size());
    // The largest first, so the lanes finish together.
    std::ranges::sort(active_domains_, [&](const std::uint32_t left, const std::uint32_t right) {
        return domains_[left].cost > domains_[right].cost;
    });
    if (mt_shuffle_) {
        shuffle_items(count);
    }
    domain_limit_ = limit;
    try {
        static const bool inline_items = std::getenv("FSIM_KERNEL_MT_INLINE") != nullptr;
        if (inline_items) {
            for (std::uint32_t item = 0U; item < count; ++item) {
                domain_body_(0U, item);
            }
        } else {
            pool_->run(count, domain_body_);
        }
        for (const auto index : active_domains_) {
            if (domains_[index].stop == domain_finish || domains_[index].stop == domain_unit) {
                resume_domain(index, limit);
            }
        }
        const auto merge_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                                     : std::chrono::steady_clock::time_point { };
        bool effects = queued_count_ != 0U;
        std::size_t records = 0U;
        for (const auto index : active_domains_) {
            const auto& domain = domains_[index];
            effects = effects || domain.stop != domain_running;
            for (std::size_t record = 0U; record < domain.records.size(); ++record) {
                if (domain.runs[record].two_state) {
                    const auto entry = domain.records[record].entry;
                    ++fast_runs_[(entry & partition_tag) != 0U
                            ? partitions_[entry & ~partition_tag].members.front() : entry]
                          .two_state_runs;
                }
                records += domain.records[record].continued ? 0U : 1U;
            }
        }
        for (const auto& lane : lanes_) {
            effects = effects || !lane.changes.empty() || lane.writes.count != 0U;
        }
        if (!effects) {
            // Nothing for the leader to deliver or run: the merge would only
            // count the evaluations.
            evaluations += records;
            if (evaluations > limit) {
                const auto entry = domains_[active_domains_.front()].records.back().entry;
                const auto member = (entry & partition_tag) != 0U
                    ? partitions_[entry & ~partition_tag].members.front() : entry;
                fail(member, members_[member].body_begin,
                    "static kernel combinational logic did not settle");
            }
        } else {
            ++mt_stats_.domain_merges;
        }
        // The merge: settle()'s passes in order, each the serial units'
        // snapshot at the lowest level plus every domain's pass there.
        reconstructing_ = effects;
        for (; effects;) {
            auto level = no_slot;
            if (queued_count_ != 0U) {
                while (buckets_[scan_level_].count == 0U) {
                    ++scan_level_;
                }
                level = scan_level_;
            }
            for (const auto index : active_domains_) {
                const auto& domain = domains_[index];
                if (domain.pass < domain.passes.size()) {
                    level = std::min(level, domain.passes[domain.pass].first);
                }
            }
            if (level == no_slot) {
                break;
            }
            merge_items_.clear();
            if (queued_count_ != 0U && scan_level_ == level) {
                auto& queue = buckets_[level];
                auto remaining = queue.count;
                queued_count_ -= remaining;
                queue.count = 0U;
                for (auto word_index = queue.cursor; remaining != 0U; ++word_index) {
                    auto word = queue.bits[word_index];
                    if (word == 0U) {
                        continue;
                    }
                    queue.bits[word_index] = 0U;
                    remaining -= static_cast<std::uint32_t>(std::popcount(word));
                    for (; word != 0U; word &= word - 1U) {
                        const auto bit = word_index * 64U
                            + static_cast<std::uint32_t>(std::countr_zero(word));
                        merge_items_.push_back({ bit, no_slot, queue.entries[bit] });
                    }
                }
                queue.cursor = static_cast<std::uint32_t>(queue.bits.size());
                for (const auto& item : merge_items_) {
                    if ((item.item & partition_tag) != 0U) {
                        partition_queued_[item.item & ~partition_tag] = 0U;
                    } else {
                        member_schedule_[item.item].queued = false;
                    }
                }
            }
            const bool global = !merge_items_.empty();
            for (const auto index : active_domains_) {
                auto& domain = domains_[index];
                if (domain.pass >= domain.passes.size()
                    || domain.passes[domain.pass].first != level) {
                    continue;
                }
                const auto begin = domain.passes[domain.pass].second;
                ++domain.pass;
                const auto end = domain.pass < domain.passes.size()
                    ? domain.passes[domain.pass].second
                    : static_cast<std::uint32_t>(domain.records.size());
                for (auto record = begin; record < end; ++record) {
                    if (!domain.records[record].continued) {
                        merge_items_.push_back({ queue_position(domain.records[record].entry),
                            index, record });
                    }
                }
            }
            if (global || merge_items_.size() > 1U) {
                std::ranges::sort(merge_items_, [](const MergeItem& left, const MergeItem& right) {
                    return left.position < right.position;
                });
            }
            for (const auto& item : merge_items_) {
                if (item.domain == no_slot) {
                    run_settle_entry(item.item, evaluations, limit);
                } else {
                    replay_domain_record(domains_[item.domain], item.item, evaluations, limit);
                }
            }
        }
        reconstructing_ = false;
        for (const auto index : active_domains_) {
            const auto& domain = domains_[index];
            if (domain.stop == domain_limit) {
                // The domain alone went past the limit.
                const auto entry = domain.snapshot[domain.next];
                const auto member = (entry & partition_tag) != 0U
                    ? partitions_[entry & ~partition_tag].members.front() : entry;
                fail(member, members_[member].body_begin,
                    "static kernel combinational logic did not settle");
            }
        }
        if (mt_stats_.enabled) {
            const auto now = std::chrono::steady_clock::now();
            const auto nanoseconds = [](const auto elapsed) {
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
            };
            ++mt_stats_.domain_settles;
            mt_stats_.domain_active += count;
            std::uint64_t critical = 0U;
            for (const auto index : active_domains_) {
                mt_stats_.domain_records += domains_[index].records.size();
                critical = std::max(critical, domains_[index].run_ns);
                mt_stats_.domain_work_ns += domains_[index].run_ns;
            }
            mt_stats_.domain_critical_ns += critical;
            for (const auto& lane : lanes_) {
                mt_stats_.domain_changes += lane.changes.size();
            }
            mt_stats_.domain_run_ns += nanoseconds(merge_started - run_started);
            mt_stats_.domain_merge_ns += nanoseconds(now - merge_started);
        }
    } catch (...) {
        worker_lane_ = nullptr;
        abandon_domains();
        throw;
    }
    active_domains_.clear();
    return true;
}

void Interpreter::Impl::StaticKernel::run_in_lane(Lane& lane,
    const std::uint32_t entry, LaneRun& run)
{
    run.changes_begin = static_cast<std::uint32_t>(lane.changes.size());
    run.writes_begin = lane.writes.count;
    auto& frame = run.frame;
    frame = StaticKernelNativeFrame { };
    frame.arena = arena_.data();
    frame.kernel = this;
    frame.writes = &lane.writes;
    frame.containers = container_info_.data();
    StaticKernelNativeEntry code { };
    kernel_word::Word* registers { };
    bool two_state { };
    if ((entry & partition_tag) != 0U) {
        auto& state = partitions_[entry & ~partition_tag];
        frame.bindings = state.native.bindings.data();
        frame.program = state.native.program;
        frame.instance = &state.program;
        frame.member = state.members.front();
        code = state.native.entry;
        registers = state.program.registers.data();
        two_state = state.native.two_state;
    } else {
        const auto& fast = fast_runs_[entry];
        frame.bindings = fast.bindings;
        frame.program = fast.program;
        frame.instance = fast.body;
        frame.member = entry;
        code = fast.entry;
        registers = fast.registers;
        two_state = fast.two_state;
    }
    auto& scratch = lane.scratch;
    scratch.running_position = 0U;
    scratch.two_state_running = two_state;
    run.status = code(&frame, registers);
    scratch.two_state_running = false;
    // A domain lane leaves the shared counter to the leader.
    run.two_state = two_state && lane.domain != no_slot;
    if (two_state && lane.domain == no_slot) {
        ++fast_runs_[frame.member].two_state_runs;
    }
    if (run.status != 0U || frame.status != 0U) {
        run.deopt = std::exchange(scratch.pending_deopt, std::nullopt);
        run.exception = std::exchange(scratch.native_exception, nullptr);
    }
    run.changes_end = static_cast<std::uint32_t>(lane.changes.size());
    run.writes_end = lane.writes.count;
}

void Interpreter::Impl::StaticKernel::log_change(Lane& lane,
    const std::uint32_t slot, const std::uint64_t changed,
    const std::uint64_t* before, const std::uint32_t field_offset)
{
    LaneChange change;
    change.changed = changed;
    change.slot = slot;
    change.field_offset = field_offset;
    change.position = lane.scratch.running_position;
    if (slot < slot_wait_seen_.size() && slot_waitable_[slot] != 0U) {
        change.edge = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(slot_edge_bit(slot)) + 1U);
    }
    if (before != nullptr) {
        const auto& state = slots_[slot];
        const auto size = static_cast<std::size_t>(state.planes) * state.words;
        const auto* after = arena_.data() + state.offset;
        change.words = static_cast<std::uint32_t>(lane.words.size());
        lane.words.insert(lane.words.end(), before, before + size);
        lane.words.insert(lane.words.end(), after, after + size);
    }
    lane.changes.push_back(change);
}

void Interpreter::Impl::StaticKernel::replay(const std::uint32_t entry, LaneRun& run,
    const bool settle)
{
    auto& lane = lanes_[run.lane];
    const bool partition = (entry & partition_tag) != 0U;
    if (partition) {
        scratch().running_partition = entry & ~partition_tag;
    } else if (settle) {
        scratch().running_member = entry;
    }
    try {
        for (auto index = run.changes_begin; index < run.changes_end; ++index) {
            const auto& change = lane.changes[index];
            scratch_.running_position = change.position;
            replay_edge_ = change.edge;
            if (change.words == no_slot) {
                slot_changed(change.slot, change.changed, nullptr, change.field_offset);
                continue;
            }
            // The arena holds the slot's last words of the batch; the
            // change is replayed against the words its own write left.
            const auto& state = slots_[change.slot];
            const auto size = static_cast<std::size_t>(state.planes) * state.words;
            auto* const words = arena_.data() + state.offset;
            const auto* const before = lane.words.data() + change.words;
            replay_words_.assign(words, words + size);
            std::copy(before + size, before + 2U * size, words);
            slot_changed(change.slot, change.changed, before, change.field_offset);
            std::copy(replay_words_.begin(), replay_words_.end(), words);
        }
        replay_edge_ = 0U;
        for (auto index = run.writes_begin; index < run.writes_end; ++index) {
            const auto& write = lane.writes.data[index];
            if (write.kind == 2U) {
                push_element_write(lane.elements[write.member]);
            } else if (write.kind == 1U) {
                push_generic_pending(std::move(lane.generic[write.member]));
            } else {
                push_write(write);
            }
        }
        if (run.status != 0U || run.frame.status != 0U) {
            // A deoptimization or an error: the rest of the run is the
            // leader's, at the unit's place in the order.
            run.frame.kernel = this;
            run.frame.writes = &writes_;
            scratch_.running_position = run.frame.position;
            scratch_.pending_deopt = std::move(run.deopt);
            scratch_.native_exception = std::move(run.exception);
            if (partition) {
                auto& state = partitions_[entry & ~partition_tag];
                finish_native(state.native, state.program, state.members.front(),
                    run.frame, run.status, false);
            } else {
                auto& member = members_[entry];
                finish_native(member.native, *member.compiled, entry, run.frame,
                    run.status, false);
                refresh_fast_run(entry);
            }
        }
    } catch (...) {
        replay_edge_ = 0U;
        scratch().running_partition = no_slot;
        scratch().running_member = no_slot;
        throw;
    }
    scratch().running_partition = no_slot;
    scratch().running_member = no_slot;
}

} // namespace fsim::runtime::simir
