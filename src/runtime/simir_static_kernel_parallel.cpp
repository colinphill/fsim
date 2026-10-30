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

/// FSIM_KERNEL_MT_TRACE's clock: TSC ticks where there is one.
std::uint64_t trace_ticks() noexcept
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    return __rdtsc();
#else
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

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
    , done_(std::make_unique<LaneDone[]>(std::max(lanes, 1U)))
{
    trace_ = std::getenv("FSIM_KERNEL_MT_TRACE") != nullptr;
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
    pinned_.store(false, std::memory_order_relaxed);
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

void KernelPool::run_lanes(const Body& body)
{
    pinned_.store(true, std::memory_order_relaxed);
    body_.store(&body, std::memory_order_relaxed);
    completed_.store(0U, std::memory_order_relaxed);
    const auto epoch = ((state_.load(std::memory_order_relaxed) >> epoch_shift) + 1U)
        & epoch_mask;
    const auto word = (epoch << epoch_shift)
        | (static_cast<std::uint64_t>(lanes_) << count_shift);
    const auto dispatched = trace_ ? trace_ticks() : 0U;
    state_.store(word, std::memory_order_release);
    state_.notify_all();
    take(0U, word);
    // One pass reads every lane's line (the misses overlap) until all are
    // done.
    for (std::uint32_t first = 1U; first < lanes_;) {
        bool done = true;
        for (auto lane = first; lane < lanes_; ++lane) {
            done = (done_[lane].epoch.load(std::memory_order_relaxed) == epoch) && done;
        }
        if (done) {
            std::atomic_thread_fence(std::memory_order_acquire);
            break;
        }
        while (first < lanes_ && done_[first].epoch.load(std::memory_order_relaxed) == epoch) {
            ++first;
        }
        cpu_relax();
    }
    if (trace_) {
        trace_wait_sum_ += trace_ticks() - dispatched;
        ++trace_dispatches_;
        std::uint64_t latest = 0U;
        std::uint64_t longest = 0U;
        for (std::uint32_t lane = 0U; lane < lanes_; ++lane) {
            done_[lane].start_sum += done_[lane].start - dispatched;
            done_[lane].end_sum += done_[lane].end - dispatched;
            latest = std::max(latest, done_[lane].end - dispatched);
            longest = std::max(longest, done_[lane].end - done_[lane].start);
        }
        trace_latest_sum_ += latest;
        trace_longest_sum_ += longest;
        if ((trace_dispatches_ & ((1U << 20U) - 1U)) == 0U) {
            std::cerr << "fsim-kernel: mt-trace dispatches=" << trace_dispatches_
                      << " leader_done_tsc=" << trace_wait_sum_ / trace_dispatches_
                      << " latest_end=" << trace_latest_sum_ / trace_dispatches_
                      << " longest_run=" << trace_longest_sum_ / trace_dispatches_;
            for (std::uint32_t lane = 0U; lane < lanes_; ++lane) {
                std::cerr << " lane" << lane << "=" << done_[lane].start_sum / trace_dispatches_
                          << ".." << done_[lane].end_sum / trace_dispatches_;
            }
            std::cerr << '\n';
        }
    }
}

void KernelPool::take(const std::uint32_t lane, std::uint64_t word)
{
    const auto epoch = word >> epoch_shift;
    if (pinned_.load(std::memory_order_relaxed)) {
        // The batch word's release published pinned_ and body_; a word of an
        // earlier batch (a late worker) takes nothing.
        if ((state_.load(std::memory_order_acquire) >> epoch_shift) != epoch) {
            return;
        }
        if (trace_) {
            done_[lane].start = trace_ticks();
        }
        (*body_.load(std::memory_order_relaxed))(lane, lane);
        if (trace_) {
            done_[lane].end = trace_ticks();
        }
        done_[lane].epoch.store(epoch, std::memory_order_release);
        return;
    }
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
    // The units queuing nonblocking writes to a slot or container.
    std::vector<std::vector<std::uint32_t>> nba_touches(slot_count + containers_.size());
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
        const auto touch_nba = [&](const std::uint32_t resource) {
            if (resource < nba_touches.size()) {
                nba_touches[resource].push_back(unit);
            }
        };
        // An instruction the reference evaluator runs (generic, step): its
        // operation's signal accesses, or nothing known.
        const auto touch_operation = [&](const std::uint32_t member, const std::uint32_t at) {
            if (member >= member_count || at >= members_[member].operations().size()) {
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
                for (const auto& other : state.operations()) {
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
                if constexpr (std::is_same_v<T, Report>
                    || std::is_same_v<T, Display>
                    || std::is_same_v<T, FormatDisplay>
                    || std::is_same_v<T, TimeDisplay>
                    || std::is_same_v<T, SystemVerilogMath>
                    || std::is_same_v<T, SystemVerilogScalarBinary>) {
                    // Output only (a worker logs it for the replay,
                    // LaneOutput), or a register written from the time or
                    // from a real-number computation.
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
            }, members_[member].operations()[at]);
            if (!known) {
                if (opaque_level[group[unit]] == 0U && std::getenv("FSIM_KERNEL_MT_STATS") != nullptr) {
                    std::cerr << "fsim-kernel: mt-plan opaque group=" << group[unit]
                              << " operation=" << visit_operation([](const auto& op) {
                                     return typeid(op).name();
                                 }, members_[member].operations()[at])
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
            case KOp::store_slot_nba:
                touch_nba(inst.d);
                break;
            case KOp::store_slot_dynamic:
            case KOp::store_slot_part:
                if (!nba) {
                    touch_slot(inst.d, true);
                } else {
                    touch_nba(inst.d);
                }
                break;
            case KOp::wide_move:
                touch_slot(inst.x, false);
                if (!nba) {
                    touch_slot(inst.d, true);
                } else {
                    touch_nba(inst.d);
                }
                break;
            case KOp::wide_store:
                if (inst.sub == 0U) {
                    touch_slot(inst.d, true);
                } else if (inst.sub == 1U) {
                    touch_nba(inst.d);
                } else {
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
                if (nba) {
                    touch_nba(slot_count + inst.d);
                }
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
                touch_operation(current, inst.x);
                // A sequential member's output and severity messages wait
                // in the lane for the replay; every other generic operation
                // runs on the leader.
                ok = ok && group[unit] == sequential_group && current < member_count
                    && inst.x < members_[current].operations().size()
                    && static_kernel_sequential_operation_supported(
                        members_[current].operations()[inst.x]);
                break;
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
        std::vector<std::uint32_t> component_of(unit_total, no_slot);
        for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
            if (comb(unit) && serial_root[find(unit)] == 0U) {
                component_of[unit] = find(unit);
            }
        }
        const auto clusters = cluster_components(component_of);
        const auto& cluster_of = clusters.of_root;
        unit_domain_.assign(unit_total, no_slot);
        unit_bucket_.assign(unit_total, no_slot);
        unit_bit_.assign(unit_total, no_slot);
        std::unordered_map<std::uint32_t, std::uint32_t> domain_of_root;
        std::vector<std::vector<std::uint32_t>> domain_units;
        for (std::uint32_t unit = 0U; unit < unit_total; ++unit) {
            if (!comb(unit) || serial_root[find(unit)] != 0U) {
                continue;
            }
            const auto [found, added] = domain_of_root.emplace(cluster_of[find(unit)],
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
            // Domain time steps: a slot or container whose combinational
            // readers and writers are all in one domain has its queued
            // writes committed by that domain's lane, which then settles.
            // One no combinational unit touches goes to the domain of the
            // sequential members queuing writes to it. A container and its
            // element slots are one resource.
            std::vector<std::uint32_t> seq_domain(member_count, no_slot);
            for (std::uint32_t member = 0U; member < member_count; ++member) {
                const auto cluster = clusters.of_member[member];
                if (cluster != no_slot) {
                    if (const auto found = domain_of_root.find(cluster);
                        found != domain_of_root.end()) {
                        seq_domain[member] = found->second;
                    }
                }
            }
            member_domain_ = seq_domain;
            const auto join = [](std::uint32_t& into, const std::uint32_t domain) {
                into = into == no_slot || into == domain ? domain : domain_many;
            };
            const auto resources = slot_count + static_cast<std::uint32_t>(containers_.size());
            std::vector<std::uint32_t> readers(resources, no_slot);
            std::vector<std::uint32_t> owner(resources, no_slot);
            for (std::uint32_t resource = 0U; resource < resources; ++resource) {
                for (const auto& touch : touches[resource]) {
                    if (comb(touch.unit)) {
                        const auto domain = unit_domain_[touch.unit];
                        join(readers[resource], domain == no_slot ? domain_many : domain);
                    }
                }
                if (resource < slot_count) {
                    const auto notify = slot_notify_[resource];
                    for (auto index = notify.begin; index < notify.end; ++index) {
                        const auto id = notify_targets_[index].id;
                        const auto unit = (id & partition_tag) != 0U
                            ? member_count + (id & ~partition_tag) : unit_of_member(id);
                        // Other targets hear of the change from the log.
                        if (comb(unit)) {
                            join(readers[resource],
                                unit_domain_[unit] == no_slot ? domain_many : unit_domain_[unit]);
                        }
                    }
                    for (const auto& reader : slots_[resource].readers) {
                        const auto unit = unit_of_member(reader.member);
                        if (comb(unit)) {
                            join(readers[resource],
                                unit_domain_[unit] == no_slot ? domain_many : unit_domain_[unit]);
                        }
                    }
                }
                if (readers[resource] != no_slot) {
                    owner[resource] = readers[resource];
                    continue;
                }
                for (const auto unit : nba_touches[resource]) {
                    join(owner[resource], unit < member_count && seq_domain[unit] != no_slot
                            ? seq_domain[unit] : domain_many);
                }
            }
            for (std::uint32_t container = 0U; container < containers_.size(); ++container) {
                auto slots = containers_[container].element_slots;
                if (containers_[container].packed_slot < slot_count) {
                    slots.push_back(containers_[container].packed_slot);
                }
                const auto at = slot_count + container;
                for (const auto slot : slots) {
                    if (slot < slot_count) {
                        if (readers[slot] != no_slot) {
                            join(readers[at], readers[slot]);
                        }
                        if (owner[slot] != no_slot) {
                            join(owner[at], owner[slot]);
                        }
                    }
                }
                if (readers[at] != no_slot) {
                    owner[at] = readers[at];
                }
                for (const auto slot : slots) {
                    if (slot < slot_count) {
                        readers[slot] = readers[at];
                        owner[slot] = owner[at];
                    }
                }
            }
            const auto committer = [](const std::uint32_t domain) {
                return domain == domain_many ? no_slot : domain;
            };
            slot_commit_domain_.assign(slot_count, no_slot);
            slot_reader_domain_.assign(readers.begin(), readers.begin() + slot_count);
            container_commit_domain_.assign(containers_.size(), no_slot);
            container_reader_domain_.assign(readers.begin() + slot_count, readers.end());
            for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
                slot_commit_domain_[slot] = committer(owner[slot]);
            }
            for (std::uint32_t container = 0U; container < containers_.size(); ++container) {
                container_commit_domain_[container] = committer(owner[slot_count + container]);
            }
            // A member queuing to its domain's slots, and to a few others
            // (taken back out after its run), runs with the domain's queue.
            member_private_.assign(member_count, 0U);
            member_foreign_.assign(member_count, { });
            for (std::uint32_t member = 0U; member < member_count; ++member) {
                member_private_[member] = seq_domain[member] != no_slot && eligible[member] != 0U
                    ? 1U : 0U;
            }
            for (std::uint32_t resource = 0U; resource < resources; ++resource) {
                const auto commit = resource < slot_count
                    ? slot_commit_domain_[resource]
                    : container_commit_domain_[resource - slot_count];
                for (const auto unit : nba_touches[resource]) {
                    if (unit >= member_count || commit == seq_domain[unit]) {
                        continue;
                    }
                    auto& foreign = member_foreign_[unit];
                    if (resource < slot_count) {
                        foreign.push_back(resource);
                        continue;
                    }
                    // A container's writes may queue as writes to its packed
                    // or element slots.
                    const auto& container = containers_[resource - slot_count];
                    foreign.push_back((resource - slot_count) | private_tag);
                    if (container.packed_slot < slot_count) {
                        foreign.push_back(container.packed_slot);
                    }
                    for (const auto slot : container.element_slots) {
                        if (slot < slot_count) {
                            foreign.push_back(slot);
                        }
                    }
                }
            }
            for (std::uint32_t member = 0U; member < member_count; ++member) {
                auto& foreign = member_foreign_[member];
                std::ranges::sort(foreign);
                foreign.erase(std::ranges::unique(foreign).begin(), foreign.end());
                if (foreign.size() > 4U) {
                    member_private_[member] = 0U;
                }
            }
            if (diagnose) {
                // NBA targets by how their writers' domains and the
                // committing domain agree.
                std::array<std::size_t, 5> kinds { };
                for (std::uint32_t resource = 0U; resource < resources; ++resource) {
                    for (const auto unit : nba_touches[resource]) {
                        const auto mine = unit < member_count ? seq_domain[unit] : no_slot;
                        const auto commit = resource < slot_count
                            ? slot_commit_domain_[resource]
                            : container_commit_domain_[resource - slot_count];
                        ++kinds[mine == no_slot ? 0U
                                : commit == mine ? 1U
                                : commit == no_slot ? (readers[resource] == domain_many ? 2U : 3U)
                                : 4U];
                    }
                }
                std::cerr << "fsim-kernel: mt-plan nba writer_no_domain=" << kinds[0]
                          << " private=" << kinds[1] << " shared_readers=" << kinds[2]
                          << " shared_writers=" << kinds[3] << " other_domain=" << kinds[4] << '\n';
            }
        } else {
            unit_domain_.clear();
            member_domain_.clear();
            member_private_.clear();
            member_foreign_.clear();
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
        run_batch_item(lane_index, item);
    };
    // Domain time steps: a lane runs the members of the domains it owns, so
    // a domain's queue of private writes has one writer.
    batch_pinned_body_ = [this](const std::uint32_t lane_index, std::uint32_t) {
        const auto count = static_cast<std::uint32_t>(batch_.size());
        const auto lane_count = static_cast<std::uint32_t>(lanes_.size());
        for (std::uint32_t order = 0U; order < count; ++order) {
            const auto item = mt_shuffle_ ? shuffle_order_[order] : order;
            const auto entry = batch_[item];
            const auto domain = (entry & partition_tag) == 0U ? member_domain_[entry] : no_slot;
            if ((domain != no_slot ? domain_lane_[domain] : item % lane_count) == lane_index) {
                run_batch_item(lane_index, item);
            }
        }
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
        prepare_lane(lane);
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
        summarize_domain(domains_[index]);
        lane.domain = no_slot;
        worker_lane_ = nullptr;
    };
    commit_body_ = [this](const std::uint32_t lane_index, std::uint32_t item) {
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
        auto& domain = domains_[index];
        auto& lane = lanes_[lane_index];
        prepare_lane(lane);
        domain.lane = lane_index;
        domain.passes.clear();
        domain.records.clear();
        domain.snapshot.clear();
        domain.next = 0U;
        domain.evaluations = 0U;
        domain.stop = domain_running;
        domain.pass = 0U;
        domain.commit_done = 0U;
        domain.commit_error = nullptr;
        domain.logged.clear();
        domain.run_ns = 0U;
        worker_lane_ = &lane;
        lane.domain = index;
        const auto domain_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                                      : std::chrono::steady_clock::time_point { };
        commit_in_domain(index);
        if (!domain.commit_error && domain.commit_done == domain.commit_total) {
            run_domain(index, domain_limit_);
            domain.nba.count = 0U;
            domain.nba_elements.clear();
            domain.segments.clear();
        }
        summarize_domain(domain);
        if (mt_stats_.enabled) {
            domain.run_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - domain_started).count());
        }
        lane.domain = no_slot;
        worker_lane_ = nullptr;
    };
    pinned_body_ = [this](const std::uint32_t lane_index, std::uint32_t) {
        for (std::uint32_t item = 0U; item < pinned_items_; ++item) {
            const auto index = active_domains_[mt_shuffle_ ? shuffle_order_[item] : item];
            if (domain_lane_[index] == lane_index) {
                (*pinned_target_)(lane_index, item);
            }
        }
    };
    // FSIM_KERNEL_MT_FUSED=0: triggered members log every change for the
    // leader instead of scheduling their domains' units.
    const char* fused_switch = std::getenv("FSIM_KERNEL_MT_FUSED");
    mt_fused_ = fused_switch == nullptr || std::string_view { fused_switch } != "0";
    const char* affinity_switch = std::getenv("FSIM_KERNEL_MT_AFFINITY");
    mt_affinity_ = affinity_switch == nullptr || std::string_view { affinity_switch } != "0";
    // Largest domains first, each to the least loaded lane.
    domain_lane_.assign(domains_.size(), 0U);
    {
        std::vector<std::uint32_t> order(domains_.size());
        for (std::uint32_t index = 0U; index < order.size(); ++index) {
            order[index] = index;
        }
        std::ranges::stable_sort(order, [&](const std::uint32_t left, const std::uint32_t right) {
            return domains_[left].cost > domains_[right].cost;
        });
        std::vector<std::uint64_t> load(mt_threads_, 0U);
        for (const auto index : order) {
            const auto lane = static_cast<std::uint32_t>(
                std::ranges::min_element(load) - load.begin());
            domain_lane_[index] = lane;
            load[lane] += std::max<std::uint64_t>(domains_[index].cost, 1U);
        }
    }
    // FSIM_KERNEL_MT_COMMIT=0: commits stay on the leader.
    const char* commit_switch = std::getenv("FSIM_KERNEL_MT_COMMIT");
    mt_commit_ = commit_switch == nullptr || std::string_view { commit_switch } != "0";
    mt_commit_min_ = 64U;
    if (const char* minimum = std::getenv("FSIM_KERNEL_MT_COMMIT_MIN")) {
        mt_commit_min_ = std::strtoull(minimum, nullptr, 10);
    }
    pool_ = std::make_unique<KernelPool>(mt_threads_, spin);
    // Lanes queue writes without marks: every assignment is queued.
    write_marks_.clear();
}

Interpreter::Impl::StaticKernel::Clusters Interpreter::Impl::StaticKernel::cluster_components(
    const std::vector<std::uint32_t>& component_of) const
{
    // Proposal §2.4 steps 1 and 4: a component is often one unit (a core's
    // combinational blocks rarely feed one another), and every domain costs
    // a pass setup and a lane hand-off whatever its size. Components are
    // grouped by instance instead: from the root scope down, the costliest
    // scope splits into its child scopes (its own components stay one
    // cluster) while there are fewer clusters than lanes or one costs more
    // than a lane's share. Any grouping is exact: a domain is settled in
    // the serial order and merged back. FSIM_KERNEL_MT_CLUSTER=0 keeps one
    // domain per component. A sequential member joins the cluster of its
    // scope's own components or of the closest scope cluster above it.
    Clusters clusters;
    auto& cluster_of = clusters.of_root;
    cluster_of.assign(component_of.size(), no_slot);
    clusters.of_member.assign(members_.size(), no_slot);
    for (const auto root : component_of) {
        if (root != no_slot) {
            cluster_of[root] = root;
        }
    }
    const char* cluster_switch = std::getenv("FSIM_KERNEL_MT_CLUSTER");
    if (cluster_switch != nullptr && std::string_view { cluster_switch } == "0") {
        return clusters;
    }
    const auto member_count = static_cast<std::uint32_t>(members_.size());
    struct Scope {
        std::uint32_t parent { no_slot };
        std::uint32_t depth { };
        std::vector<std::uint32_t> children;
        std::vector<std::uint32_t> atoms;
        std::uint64_t own_cost { };
        std::uint64_t cost { };
    };
    std::vector<Scope> scopes(1U);
    std::unordered_map<std::string, std::uint32_t> scope_index;
    // The scope of a process path is its prefix before the last '.'.
    const auto intern = [&](const std::string_view path) {
        std::uint32_t node = 0U;
        const auto end = path.rfind('.');
        for (std::size_t at = 0U; end != std::string_view::npos && at <= end;) {
            const auto dot = std::min(path.find('.', at), end);
            const auto [found, added] = scope_index.try_emplace(std::string { path.substr(0U, dot) },
                static_cast<std::uint32_t>(scopes.size()));
            if (added) {
                scopes.emplace_back();
                scopes.back().parent = node;
                scopes.back().depth = scopes[node].depth + 1U;
                scopes[node].children.push_back(found->second);
            }
            node = found->second;
            at = dot + 1U;
        }
        return node;
    };
    const auto common = [&](std::uint32_t left, std::uint32_t right) {
        while (left != right) {
            if (scopes[left].depth >= scopes[right].depth) {
                left = scopes[left].parent;
            } else {
                right = scopes[right].parent;
            }
        }
        return left;
    };
    // A component sits at the common scope of its units.
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint64_t>> atoms;
    for (std::uint32_t unit = 0U; unit < component_of.size(); ++unit) {
        const auto root = component_of[unit];
        if (root == no_slot) {
            continue;
        }
        const auto member = unit < member_count ? unit
                                                : partitions_[unit - member_count].members.front();
        const auto scope = intern(impl_.processes.program_view(members_[member].process).name());
        const auto [found, added] = atoms.try_emplace(root, scope, 0U);
        if (!added) {
            found->second.first = common(found->second.first, scope);
        }
        found->second.second += unit_cost_[unit];
    }
    for (const auto& [root, atom] : atoms) {
        scopes[atom.first].atoms.push_back(root);
        scopes[atom.first].own_cost += atom.second;
    }
    // Children are interned after their parents.
    std::vector<std::uint32_t> atom_count(scopes.size(), 0U);
    for (auto node = static_cast<std::uint32_t>(scopes.size()); node-- > 0U;) {
        scopes[node].cost += scopes[node].own_cost;
        atom_count[node] += static_cast<std::uint32_t>(scopes[node].atoms.size());
        if (node != 0U) {
            scopes[scopes[node].parent].cost += scopes[node].cost;
            atom_count[scopes[node].parent] += atom_count[node];
        }
    }
    // A frontier entry is a scope's subtree, its own components, or one
    // component (own components split one by one, as in a flat design).
    enum class Part : std::uint8_t { subtree, own, atom };
    struct Entry {
        Part part;
        std::uint32_t at;
        std::uint64_t cost;
    };
    const auto splittable = [&](const Entry& entry) {
        return entry.part == Part::subtree ? atom_count[entry.at] > 1U
            : entry.part == Part::own      ? scopes[entry.at].atoms.size() > 1U
                                           : false;
    };
    std::vector<Entry> frontier { { Part::subtree, 0U, scopes[0].cost } };
    const auto share = scopes[0].cost / std::max<std::uint32_t>(mt_threads_, 1U);
    for (;;) {
        std::size_t best = frontier.size();
        for (std::size_t index = 0U; index < frontier.size(); ++index) {
            if (splittable(frontier[index])
                && (best == frontier.size() || frontier[index].cost > frontier[best].cost)) {
                best = index;
            }
        }
        if (best == frontier.size()
            || (frontier.size() >= mt_threads_ && frontier[best].cost <= share)) {
            break;
        }
        const auto split = frontier[best];
        frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(best));
        const auto& scope = scopes[split.at];
        if (split.part == Part::own) {
            for (const auto root : scope.atoms) {
                frontier.push_back({ Part::atom, root, atoms.at(root).second });
            }
            continue;
        }
        if (!scope.atoms.empty()) {
            frontier.push_back({ Part::own, split.at, scope.own_cost });
        }
        for (const auto child : scope.children) {
            if (atom_count[child] != 0U) {
                frontier.push_back({ Part::subtree, child, scopes[child].cost });
            }
        }
    }
    std::vector<std::uint32_t> subtree_cluster(scopes.size(), no_slot);
    std::vector<std::uint32_t> own_cluster(scopes.size(), no_slot);
    for (const auto& entry : frontier) {
        std::vector<std::uint32_t> roots;
        if (entry.part == Part::atom) {
            roots.push_back(entry.at);
        }
        std::vector<std::uint32_t> pending;
        if (entry.part != Part::atom) {
            pending.push_back(entry.at);
        }
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            roots.insert(roots.end(), scopes[node].atoms.begin(), scopes[node].atoms.end());
            if (entry.part == Part::subtree) {
                pending.insert(pending.end(), scopes[node].children.begin(),
                    scopes[node].children.end());
            }
        }
        if (roots.empty()) {
            continue;
        }
        const auto representative = *std::ranges::min_element(roots);
        for (const auto root : roots) {
            cluster_of[root] = representative;
        }
        if (entry.part == Part::subtree) {
            subtree_cluster[entry.at] = representative;
        } else if (entry.part == Part::own) {
            own_cluster[entry.at] = representative;
        }
    }
    for (std::uint32_t member = 0U; member < members_.size(); ++member) {
        const auto& state = members_[member];
        if (state.kind != StaticKernelMemberKind::sequential || state.vhdl
            || state.partition != no_slot) {
            continue;
        }
        // The deepest interned scope on the member's path.
        const std::string_view path = impl_.processes.program_view(state.process).name();
        const auto end = path.rfind('.');
        std::uint32_t node = 0U;
        bool exact = end == std::string_view::npos;
        for (std::size_t at = 0U; end != std::string_view::npos && at <= end;) {
            const auto dot = std::min(path.find('.', at), end);
            const auto found = scope_index.find(std::string { path.substr(0U, dot) });
            if (found == scope_index.end()) {
                break;
            }
            node = found->second;
            exact = dot == end;
            at = dot + 1U;
        }
        auto cluster = exact ? own_cluster[node] : no_slot;
        for (auto at = node; cluster == no_slot && at != no_slot; at = scopes[at].parent) {
            cluster = subtree_cluster[at];
        }
        clusters.of_member[member] = cluster;
    }
    return clusters;
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
    // Each lane clears its logs when it first runs in this dispatch.
    ++lane_epoch_;
    if (mt_shuffle_) {
        shuffle_items(count);
    }
    // FSIM_KERNEL_MT_INLINE: the leader runs every item itself, in order
    // (the log and replay without concurrency).
    static const bool inline_items = std::getenv("FSIM_KERNEL_MT_INLINE") != nullptr;
    // Domain time steps: a triggered member's writes to slots its domain
    // commits go to the domain's queue, placed at the item's place in the
    // step's order (step_item_base_).
    // Only the SystemVerilog step commits through commit_domains.
    batch_private_ = phase != BatchPhase::settle && !member_domain_.empty() && mt_commit_
        && !vhdl_ && !mixed_ && (inline_items || mt_affinity_);
    step_item_offset_ = static_cast<std::uint32_t>(step_item_base_.size());
    if (batch_private_) {
        step_item_base_.resize(step_item_offset_ + count, 0U);
    }
    if (inline_items) {
        batch_dispatch_ = BatchDispatch::inline_items;
        for (std::uint32_t item = 0U; item < count; ++item) {
            parallel_body_(0U, item);
        }
    } else if (batch_private_) {
        batch_dispatch_ = BatchDispatch::pinned;
        pool_->run_lanes(batch_pinned_body_);
    } else {
        batch_dispatch_ = BatchDispatch::dynamic;
        pool_->run(count, parallel_body_);
    }
    if (batch_private_) {
        for (const auto& lane : lanes_) {
            if (lane.epoch == lane_epoch_) {
                private_writes_ += lane.private_count;
                private_domains_ |= lane.private_domains;
                private_more_ = private_more_ || lane.private_more;
            }
        }
    }
    const auto replay_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                                  : std::chrono::steady_clock::time_point { };
    struct DrainScope {
        StaticKernel& kernel;
        bool armed;
        ~DrainScope()
        {
            if (armed && std::uncaught_exceptions() != 0) {
                kernel.drain_domain_queues();
            }
        }
    } drain_scope { *this, batch_private_ && mt_fused_ };
    for (std::uint32_t item = 0U; item < count; ++item) {
        const auto entry = batch_[item];
        auto& lane_run = lane_runs_[item];
        if (phase == BatchPhase::settle) {
            if (lanes_[item_lane(item)].item_state[item] == 1U) {
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
        if (batch_private_) {
            step_item_base_[step_item_offset_ + item] = writes_.count;
        }
        const auto state = lanes_[item_lane(item)].item_state[item];
        if (state == 1U) {
            run(entry);
        } else if (state == 2U) {
            replay(entry, lane_run, false);
        }
    }
    if (batch_private_ && mt_fused_) {
        drain_domain_queues();
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
    std::cerr << "fsim-kernel: mt-stats commits offered=" << mt_stats_.commit_offered
              << " small=" << mt_stats_.commit_small
              << " read=" << mt_stats_.commit_read
              << " parallel=" << mt_stats_.commit_parallel
              << " domain_entries=" << mt_stats_.commit_domain_entries
              << " leader_entries=" << mt_stats_.commit_leader_entries
              << " private_entries=" << mt_stats_.commit_private_entries
              << " merge_ms=" << static_cast<double>(mt_stats_.commit_merge_ns) / 1e6
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
    run.status = 0U;
    run.frame.status = 0U;
    run.deopt.reset();
    run.exception = nullptr;
    return run;
}

void Interpreter::Impl::StaticKernel::summarize_domain(Domain& domain) noexcept
{
    domain.evaluated = 0U;
    domain.effects = false;
    for (std::size_t record = 0U; record < domain.records.size(); ++record) {
        domain.evaluated += domain.records[record].continued ? 0U : 1U;
        const auto& run = domain.runs[record];
        domain.effects = domain.effects || run.changes_begin != run.changes_end
            || run.writes_begin != run.writes_end;
    }
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

void Interpreter::Impl::StaticKernel::drain_domain_queues() noexcept
{
    // The units a domain time step's members woke in their domains' queues
    // go to the global queue, as a serial run would have queued them.
    for (auto& domain : domains_) {
        if (domain.queued == 0U) {
            continue;
        }
        for (auto& queue : domain.buckets) {
            for (auto word_index = queue.cursor; queue.count != 0U
                 && word_index < queue.bits.size(); ++word_index) {
                for (auto word = std::exchange(queue.bits[word_index], 0U); word != 0U;
                     word &= word - 1U) {
                    --queue.count;
                    const auto entry = queue.entries[word_index * 64U
                        + static_cast<std::uint32_t>(std::countr_zero(word))];
                    if ((entry & partition_tag) != 0U) {
                        const auto partition = entry & ~partition_tag;
                        if (partition_queued_[partition] == 0U) {
                            partition_queued_[partition] = 1U;
                            enqueue(partition_level_[partition], entry);
                        }
                    } else if (auto& state = member_schedule_[entry]; !state.queued) {
                        state.queued = true;
                        enqueue(state.level, entry);
                    }
                }
            }
            queue.count = 0U;
            queue.cursor = static_cast<std::uint32_t>(queue.bits.size());
        }
        domain.queued = 0U;
        domain.scan = static_cast<std::uint32_t>(domain.buckets.size());
    }
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
    // Each lane clears its logs when it first runs in this dispatch.
    ++lane_epoch_;
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
        run_domain_items(domain_body_, count);
        finish_domains(evaluations, limit, run_started);
    } catch (...) {
        worker_lane_ = nullptr;
        abandon_domains();
        throw;
    }
    active_domains_.clear();
    return true;
}

void Interpreter::Impl::StaticKernel::finish_domains(std::size_t& evaluations,
    const std::size_t limit, const std::chrono::steady_clock::time_point run_started)
{
    for (const auto index : active_domains_) {
        if (domains_[index].stop == domain_finish || domains_[index].stop == domain_unit) {
            resume_domain(index, limit);
            summarize_domain(domains_[index]);
        }
    }
    const auto merge_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                                 : std::chrono::steady_clock::time_point { };
    bool effects = queued_count_ != 0U;
    std::size_t records = 0U;
    for (const auto index : active_domains_) {
        const auto& domain = domains_[index];
        effects = effects || domain.stop != domain_running || domain.effects;
        records += domain.evaluated;
    }
    if (!effects) {
        // Nothing for the leader to deliver or run: the merge would only
        // count the evaluations.
        evaluations += records;
        if (evaluations > limit) {
            const auto busy = std::ranges::find_if(active_domains_, [&](const std::uint32_t index) {
                return !domains_[index].records.empty();
            });
            const auto entry = domains_[*busy].records.back().entry;
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
        mt_stats_.domain_active += active_domains_.size();
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
}

void Interpreter::Impl::StaticKernel::run_domain_items(
    const KernelPool::Body& body, const std::uint32_t count)
{
    // FSIM_KERNEL_MT_INLINE: the leader runs every item itself, in order.
    static const bool inline_items = std::getenv("FSIM_KERNEL_MT_INLINE") != nullptr;
    if (inline_items) {
        for (std::uint32_t item = 0U; item < count; ++item) {
            body(0U, item);
        }
    } else if (mt_affinity_) {
        pinned_items_ = count;
        pinned_target_ = &body;
        pool_->run_lanes(pinned_body_);
    } else {
        pool_->run(count, body);
    }
}

bool Interpreter::Impl::StaticKernel::commit_domains()
{
    // Domain time steps: the NBA commit and the settle after it, per
    // domain. A domain's lane commits the queued writes of the slots and
    // containers only its combinational units read, in serial queue order
    // (the last write wins as in commit_pending): its private queue and its
    // entries of writes_. It schedules inside the domain and logs what the
    // leader delivers, then settles the domain. The leader commits the
    // other writes in queue order, delivering each logged domain write's
    // changes at its serial place, so waiters, outputs and edges hear of
    // the writes in the serial order; then it merges the settle.
    // Combinational units run in canonical order whatever the order of the
    // writes that woke them, so the settle is the serial one.
    if (!mt_commit_ || domains_.empty() || pool_ == nullptr || queued_count_ != 0U
        || !pending_generic_.empty()) {
        return false;
    }
    const auto count = writes_.count;
    mt_stats_.commit_offered += mt_stats_.enabled ? 1U : 0U;
    if (domain_seen_.size() != domains_.size()) {
        domain_seen_.assign(domains_.size(), 0U);
    }
    ++domain_epoch_;
    active_domains_.clear();
    commit_owner_.resize(count);
    const std::size_t private_entries = private_writes_;
    std::size_t domain_entries = private_writes_;
    std::size_t leader_entries = 0U;
    const auto activate = [&](const std::uint32_t domain) {
        if (domain_seen_[domain] != domain_epoch_) {
            domain_seen_[domain] = domain_epoch_;
            active_domains_.push_back(domain);
            // The line stays shared with the lane unless it changes.
            if (!domains_[domain].commit.empty()) {
                domains_[domain].commit.clear();
            }
        }
    };
    if (private_writes_ != 0U) {
        for (auto bits = private_domains_; bits != 0U; bits &= bits - 1U) {
            activate(static_cast<std::uint32_t>(std::countr_zero(bits)));
        }
        for (auto index = static_cast<std::uint32_t>(std::min<std::size_t>(64U, domains_.size()));
             private_more_ && index < domains_.size(); ++index) {
            if (domains_[index].nba.count != 0U) {
                activate(index);
            }
        }
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto& item = writes_.data[index];
        const auto domain = item.kind == 0U ? slot_commit_domain_[item.slot]
            : item.kind == 2U               ? container_commit_domain_[nba_[item.member].target]
                                            : no_slot;
        commit_owner_[index] = domain;
        if (domain == no_slot) {
            ++leader_entries;
            continue;
        }
        activate(domain);
        domains_[domain].commit.push_back(index);
        ++domain_entries;
    }
    const auto decline = [&](std::uint64_t& counter) {
        counter += mt_stats_.enabled ? 1U : 0U;
        active_domains_.clear();
        ++domain_epoch_;
        materialize_private_writes();
        return false;
    };
    if (active_domains_.size() < 2U || (!mt_force_ && domain_entries < mt_commit_min_)) {
        return decline(mt_stats_.commit_small);
    }
    // A write the leader commits must not reach a committing domain: its
    // settle would miss it.
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (commit_owner_[index] != no_slot) {
            continue;
        }
        const auto& item = writes_.data[index];
        const auto readers = item.kind == 0U ? slot_reader_domain_[item.slot]
            : container_reader_domain_[nba_[item.member].target];
        if (readers == domain_many
            || (readers != no_slot && domain_seen_[readers] == domain_epoch_)) {
            return decline(mt_stats_.commit_read);
        }
    }
    const auto run_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point { };
    // Each lane resets its domains (commit_body_).
    // Each lane clears its logs when it first runs in this dispatch.
    ++lane_epoch_;
    const auto items = static_cast<std::uint32_t>(active_domains_.size());
    if (mt_shuffle_) {
        shuffle_items(items);
    }
    std::size_t evaluations = 0U;
    const auto limit = members_.size() * 64U + 1'000'000U;
    domain_limit_ = limit;
    try {
        run_domain_items(commit_body_, items);
        for (const auto index : active_domains_) {
            auto& domain = domains_[index];
            if (!domain.commit_error && domain.commit_done < domain.commit_total) {
                resume_commit(index, limit);
                domain.nba.count = 0U;
                domain.nba_elements.clear();
                domain.segments.clear();
            }
        }
        // commit_pending's order: each write lands, or its logged changes
        // are delivered, at its place in the serial queue.
        const auto merge_started = mt_stats_.enabled ? std::chrono::steady_clock::now()
                                                     : std::chrono::steady_clock::time_point { };
        writes_.count = 0U;
        host_nba_staged_ = false;
        static_kernel_detail::QueueDrain<NbaEntry> drained_elements(
            nba_, nba_spare_);
        auto& elements = drained_elements.items();
        merge_items_.clear();
        for (const auto index : active_domains_) {
            const auto& logged = domains_[index].logged;
            for (std::uint32_t at = 0U; at < logged.size(); ++at) {
                merge_items_.push_back({ 0U, index, at });
            }
        }
        const auto key = [&](const MergeItem& item) {
            const auto& entry = domains_[item.domain].logged[item.item];
            return std::tuple { entry.position, entry.queued, entry.item, entry.before,
                entry.order };
        };
        if (merge_items_.size() > 1U) {
            std::ranges::sort(merge_items_, [&](const MergeItem& left, const MergeItem& right) {
                return key(left) < key(right);
            });
        }
        std::size_t next = 0U;
        const auto deliver_until = [&](const std::uint32_t position, const std::uint32_t queued) {
            for (; next < merge_items_.size()
                 && std::pair { domains_[merge_items_[next].domain].logged[merge_items_[next].item].position,
                                domains_[merge_items_[next].domain].logged[merge_items_[next].item].queued }
                     <= std::pair { position, queued };
                 ++next) {
                const auto& item = merge_items_[next];
                const auto& entry = domains_[item.domain].logged[item.item];
                const auto& lane = lanes_[domains_[item.domain].lane];
                replay_external_ = true;
                try {
                    for (auto change = entry.begin; change < entry.end; ++change) {
                        replay_change(lane, lane.changes[change]);
                    }
                } catch (...) {
                    replay_external_ = false;
                    throw;
                }
                replay_external_ = false;
                if (entry.error) {
                    std::rethrow_exception(domains_[item.domain].commit_error);
                }
            }
        };
        for (std::uint32_t index = 0U; index < count; ++index) {
            if (commit_owner_[index] != no_slot) {
                deliver_until(index, 1U);
                continue;
            }
            deliver_until(index, 0U);
            const auto& item = writes_.data[index];
            if (item.kind == 0U) {
                write_slot_word(item.slot, { item.a, item.b }, item.offset,
                    item.width, item.member);
            } else {
                write_element_entry(elements[item.member]);
            }
        }
        deliver_until(no_slot, 1U);
        private_writes_ = 0U;
        private_domains_ = 0U;
        private_more_ = false;
        step_item_base_.clear();
        if (mt_stats_.enabled) {
            ++mt_stats_.commit_parallel;
            mt_stats_.commit_domain_entries += domain_entries;
            mt_stats_.commit_leader_entries += leader_entries;
            mt_stats_.commit_private_entries += private_entries;
            mt_stats_.commit_merge_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - merge_started).count());
        }
        finish_domains(evaluations, limit, run_started);
    } catch (...) {
        worker_lane_ = nullptr;
        replay_external_ = false;
        for (auto& domain : domains_) {
            domain.nba.count = 0U;
            domain.nba_elements.clear();
            domain.segments.clear();
        }
        private_writes_ = 0U;
        private_domains_ = 0U;
        private_more_ = false;
        step_item_base_.clear();
        abandon_domains();
        throw;
    }
    active_domains_.clear();
    return true;
}

void Interpreter::Impl::StaticKernel::push_private(Domain& domain, const std::uint32_t item,
    const std::uint32_t before, const StaticKernelWrite& write)
{
    auto& queue = domain.nba;
    if (queue.count == queue.capacity) {
        domain.nba_storage.resize(std::max<std::size_t>(256U, 2U * domain.nba_storage.size()));
        queue.data = domain.nba_storage.data();
        queue.capacity = static_cast<std::uint32_t>(domain.nba_storage.size());
    }
    if (domain.segments.empty() || domain.segments.back().item != item
        || domain.segments.back().before != before
        || domain.segments.back().end != queue.count) {
        domain.segments.push_back({ item, before, queue.count, queue.count });
    }
    queue.data[queue.count++] = write;
    ++domain.segments.back().end;
}

void Interpreter::Impl::StaticKernel::build_commit_sequence(Domain& domain)
{
    // A private write goes before the writes_ entry at its place.
    domain.sequence.clear();
    std::size_t queued = 0U;
    for (const auto& segment : domain.segments) {
        const auto position = step_item_base_[segment.item] + segment.before;
        for (; queued < domain.commit.size() && domain.commit[queued] < position; ++queued) {
            domain.sequence.push_back(domain.commit[queued]);
        }
        for (auto at = segment.begin; at < segment.end; ++at) {
            domain.sequence.push_back(at | private_tag);
        }
    }
    for (; queued < domain.commit.size(); ++queued) {
        domain.sequence.push_back(domain.commit[queued]);
    }
}

void Interpreter::Impl::StaticKernel::commit_in_domain(const std::uint32_t index)
{
    auto& domain = domains_[index];
    auto& lane = lanes_[domain.lane];
    lane.scratch.running_position = 0U;
    // The private writes in run order (a shuffled batch queues them out of
    // order), merged with the domain's writes_ entries by serial place.
    const auto earlier = [](const PrivateSegment& left, const PrivateSegment& right) {
        return std::pair { left.item, left.before } < std::pair { right.item, right.before };
    };
    if (!std::ranges::is_sorted(domain.segments, earlier)) {
        std::ranges::stable_sort(domain.segments, earlier);
    }
    domain.commit_total = domain.nba.count + static_cast<std::uint32_t>(domain.commit.size());
    domain.sequence.clear();
    // commit_pending's loop over the domain's writes in serial order: a
    // private write goes before the writes_ entry at its place.
    std::uint32_t current = 0U;
    std::uint32_t begin = 0U;
    const auto apply = [&](const std::uint32_t at) {
        const bool is_private = (at & private_tag) != 0U;
        const auto& write = is_private ? domain.nba.data[at & ~private_tag] : writes_.data[at];
        current = at;
        begin = static_cast<std::uint32_t>(lane.changes.size());
        if (write.kind == 0U) {
            write_slot_word(write.slot, { write.a, write.b }, write.offset, write.width,
                write.member);
        } else if (!write_element_narrow(is_private ? domain.nba_elements[write.member]
                                                    : nba_[write.member])) {
            // An element write the reference path makes, with its exact
            // errors: the leader's (resume_commit).
            build_commit_sequence(domain);
            return false;
        }
        ++domain.commit_done;
        if (lane.changes.size() != begin) {
            log_commit(domain, at, begin, false);
        }
        return true;
    };
    try {
        std::size_t queued = 0U;
        for (const auto& segment : domain.segments) {
            const auto position = step_item_base_[segment.item] + segment.before;
            for (; queued < domain.commit.size() && domain.commit[queued] < position; ++queued) {
                if (!apply(domain.commit[queued])) {
                    return;
                }
            }
            for (auto at = segment.begin; at < segment.end; ++at) {
                if (!apply(at | private_tag)) {
                    return;
                }
            }
        }
        for (; queued < domain.commit.size(); ++queued) {
            if (!apply(domain.commit[queued])) {
                return;
            }
        }
    } catch (...) {
        log_commit(domain, current, begin, true);
        domain.commit_error = std::current_exception();
    }
}

bool Interpreter::Impl::StaticKernel::commit_write(Domain& domain, const std::uint32_t at,
    const bool reference)
{
    auto& lane = lanes_[domain.lane];
    const auto begin = static_cast<std::uint32_t>(lane.changes.size());
    const bool is_private = (at & private_tag) != 0U;
    const auto& write = is_private ? domain.nba.data[at & ~private_tag] : writes_.data[at];
    const auto log = [&](const bool error) {
        if (lane.changes.size() != begin || error) {
            log_commit(domain, at, begin, error);
        }
    };
    try {
        if (write.kind == 0U) {
            write_slot_word(write.slot, { write.a, write.b }, write.offset, write.width,
                write.member);
        } else {
            const auto& entry = is_private ? domain.nba_elements[write.member]
                                           : nba_[write.member];
            if (reference) {
                write_element_entry(entry);
            } else if (!write_element_narrow(entry)) {
                // Nothing written yet.
                return false;
            }
        }
    } catch (...) {
        log(true);
        throw;
    }
    log(false);
    return true;
}

void Interpreter::Impl::StaticKernel::log_commit(Domain& domain, const std::uint32_t at,
    const std::uint32_t begin, const bool error)
{
    LoggedCommit logged;
    if ((at & private_tag) != 0U) {
        const auto index = at & ~private_tag;
        const auto& segment = *std::ranges::find_if(domain.segments,
            [&](const PrivateSegment& candidate) {
                return candidate.begin <= index && index < candidate.end;
            });
        logged.position = step_item_base_[segment.item] + segment.before;
        logged.item = segment.item;
        logged.before = segment.before;
        // Ties keep queue order.
        logged.order = index;
    } else {
        logged.position = at;
        logged.queued = 1U;
    }
    logged.begin = begin;
    logged.end = static_cast<std::uint32_t>(lanes_[domain.lane].changes.size());
    logged.error = error;
    domain.logged.push_back(logged);
}

void Interpreter::Impl::StaticKernel::resume_commit(const std::uint32_t index,
    const std::size_t limit)
{
    auto& domain = domains_[index];
    auto& lane = lanes_[domain.lane];
    worker_lane_ = &lane;
    lane.domain = index;
    lane.scratch.running_position = 0U;
    for (; domain.commit_done < domain.sequence.size(); ++domain.commit_done) {
        try {
            (void)commit_write(domain, domain.sequence[domain.commit_done], true);
        } catch (...) {
            // Rethrown at its place in the leader's commit, after the
            // changes it made; the domain commits and settles nothing more.
            domain.commit_error = std::current_exception();
            break;
        }
    }
    if (!domain.commit_error) {
        run_domain(index, limit);
    }
    summarize_domain(domain);
    lane.domain = no_slot;
    worker_lane_ = nullptr;
}

void Interpreter::Impl::StaticKernel::materialize_private_writes()
{
    // A commit the domains do not take: the private queues' writes join
    // writes_ at their serial places (before the entry at the place, in
    // run order).
    if (private_writes_ == 0U) {
        step_item_base_.clear();
        return;
    }
    struct Placed {
        std::uint32_t position;
        std::uint32_t item;
        std::uint32_t before;
        std::uint32_t domain;
        std::uint32_t segment;
    };
    std::vector<Placed> placed;
    for (std::uint32_t index = 0U; index < domains_.size(); ++index) {
        const auto& segments = domains_[index].segments;
        for (std::uint32_t at = 0U; at < segments.size(); ++at) {
            placed.push_back({ step_item_base_[segments[at].item] + segments[at].before,
                segments[at].item, segments[at].before, index, at });
        }
    }
    std::ranges::stable_sort(placed, [](const Placed& left, const Placed& right) {
        return std::tuple { left.position, left.item, left.before }
            < std::tuple { right.position, right.item, right.before };
    });
    std::vector<StaticKernelWrite> merged;
    merged.reserve(writes_.count + private_writes_);
    std::size_t next = 0U;
    const auto place_until = [&](const std::uint32_t position) {
        for (; next < placed.size() && placed[next].position <= position; ++next) {
            auto& domain = domains_[placed[next].domain];
            const auto& segment = domain.segments[placed[next].segment];
            for (auto at = segment.begin; at < segment.end; ++at) {
                auto write = domain.nba.data[at];
                if (write.kind == 2U) {
                    const auto element = static_cast<std::uint32_t>(nba_.size());
                    nba_.push_back(domain.nba_elements[write.member]);
                    write.member = element;
                }
                merged.push_back(write);
            }
        }
    };
    for (std::uint32_t index = 0U; index < writes_.count; ++index) {
        place_until(index);
        merged.push_back(writes_.data[index]);
    }
    place_until(no_slot);
    if (write_storage_.size() < merged.size()) {
        write_storage_.resize(merged.size());
    }
    std::ranges::copy(merged, write_storage_.begin());
    writes_.data = write_storage_.data();
    writes_.capacity = static_cast<std::uint32_t>(write_storage_.size());
    writes_.count = static_cast<std::uint32_t>(merged.size());
    for (auto& domain : domains_) {
        domain.nba.count = 0U;
        domain.nba_elements.clear();
        domain.segments.clear();
    }
    private_writes_ = 0U;
    private_domains_ = 0U;
    private_more_ = false;
    step_item_base_.clear();
}

void Interpreter::Impl::StaticKernel::replay_change(const Lane& lane,
    const LaneChange& change)
{
    struct EdgeScope {
        std::uint8_t& edge;
        ~EdgeScope() { edge = 0U; }
    } edge_scope { replay_edge_ };
    scratch_.running_position = change.position;
    replay_edge_ = change.edge;
    if (change.words == no_slot) {
        slot_changed(change.slot, change.changed, nullptr, change.field_offset);
        return;
    }
    // The arena holds the slot's last words of the batch; the change is
    // replayed against the words its own write left.
    const auto& state = slots_[change.slot];
    const auto size = static_cast<std::size_t>(state.planes) * state.words;
    auto* const words = arena_.data() + state.offset;
    const auto* const before = lane.words.data() + change.words;
    replay_words_.assign(words, words + size);
    std::copy(before + size, before + 2U * size, words);
    slot_changed(change.slot, change.changed, before, change.field_offset);
    std::copy(replay_words_.begin(), replay_words_.end(), words);
}

std::uint32_t Interpreter::Impl::StaticKernel::item_lane(const std::uint32_t item) const noexcept
{
    // A pinned batch's lanes are known: the leader reads no line of the
    // item's run unless it has something to replay.
    if (batch_dispatch_ == BatchDispatch::inline_items) {
        return 0U;
    }
    if (batch_dispatch_ == BatchDispatch::pinned) {
        const auto entry = batch_[item];
        const auto domain = (entry & partition_tag) == 0U ? member_domain_[entry] : no_slot;
        return domain != no_slot ? domain_lane_[domain]
                                 : item % static_cast<std::uint32_t>(lanes_.size());
    }
    return lane_runs_[item].lane;
}

void Interpreter::Impl::StaticKernel::run_batch_item(const std::uint32_t lane_index,
    const std::uint32_t item)
{
    auto& run = lane_runs_[item];
    const auto entry = batch_[item];
    auto& lane = lanes_[lane_index];
    prepare_lane(lane);
    if (lane.item_state.size() < batch_.size()) {
        lane.item_state.resize(batch_.size());
    }
    run.lane = lane_index;
    run.serial = !parallel_now(entry);
    if (run.serial) {
        lane.item_state[item] = 1U;
        return;
    }
    worker_lane_ = &lane;
    const auto domain_index = batch_private_ && (entry & partition_tag) == 0U
        ? member_domain_[entry] : no_slot;
    if (domain_index == no_slot) {
        run_in_lane(lane, entry, run);
        run.effects = run.changes_begin != run.changes_end || run.writes_begin != run.writes_end
            || run.outputs_begin != run.outputs_end || run.status != 0U
            || run.frame.status != 0U;
        lane.item_state[item] = run.effects ? 2U : 0U;
        worker_lane_ = nullptr;
        return;
    }
    // Domain time steps: the member's changes wake its domain's units in
    // the domain's queue; the leader hears of the rest from the log.
    lane.domain = mt_fused_ ? domain_index : no_slot;
    auto& domain = domains_[domain_index];
    const auto private_before = lane.private_count;
    const auto step_item = step_item_offset_ + item;
    if (member_private_[entry] != 0U) {
        // Every write it queues is the domain's: the domain's queue is the
        // lane's while it runs.
        std::swap(lane.writes, domain.nba);
        std::swap(lane.write_storage, domain.nba_storage);
        std::swap(lane.elements, domain.nba_elements);
        const auto begin = lane.writes.count;
        run_in_lane(lane, entry, run);
        const auto end = lane.writes.count;
        std::swap(lane.writes, domain.nba);
        std::swap(lane.write_storage, domain.nba_storage);
        std::swap(lane.elements, domain.nba_elements);
        run.writes_begin = lane.writes.count;
        const auto& foreign = member_foreign_[entry];
        if (mt_shuffle_ || !foreign.empty()) {
            // The writes to other slots go to the lane's queue (and the
            // leader's replay); the domain's writes after one are placed
            // after it.
            auto kept = begin;
            auto segment_begin = begin;
            std::uint32_t before = 0U;
            for (auto at = begin; at < end; ++at) {
                auto write = domain.nba.data[at];
                const auto key = write.kind == 2U
                    ? domain.nba_elements[write.member].target | private_tag : write.slot;
                const bool own = write.kind != 1U
                    && std::ranges::find(foreign, key) == foreign.end();
                if (mt_shuffle_ && own
                    && (write.kind == 0U ? slot_commit_domain_[write.slot]
                                         : container_commit_domain_[key & ~private_tag])
                        != domain_index) {
                    throw std::logic_error(
                        "static kernel: a private write's target is not its domain's (member "
                        + std::string { program_of(members_[entry]).name() } + ", kind "
                        + std::to_string(write.kind) + ", target "
                        + std::to_string(key & ~private_tag) + ", offset "
                        + std::to_string(write.offset) + ", width "
                        + std::to_string(write.width) + ")");
                }
                if (own) {
                    domain.nba.data[kept++] = write;
                    continue;
                }
                if (kept != segment_begin) {
                    domain.segments.push_back({ step_item, before, segment_begin, kept });
                }
                segment_begin = kept;
                ++before;
                if (write.kind == 2U) {
                    const auto element = write.member;
                    write.member = static_cast<std::uint32_t>(lane.elements.size());
                    lane.elements.push_back(domain.nba_elements[element]);
                }
                push_write(write);
            }
            if (kept != segment_begin) {
                domain.segments.push_back({ step_item, before, segment_begin, kept });
            }
            domain.nba.count = kept;
            lane.private_count += kept - begin;
        } else if (end != begin) {
            domain.segments.push_back({ step_item, 0U, begin, end });
            lane.private_count += end - begin;
        }
        run.writes_end = lane.writes.count;
    } else {
        run_in_lane(lane, entry, run);
        // The writes the domain commits leave the lane's queue for the
        // domain's, each placed after the lane writes before it.
        auto kept = run.writes_begin;
        for (auto index = run.writes_begin; index < run.writes_end; ++index) {
            auto write = lane.writes.data[index];
            const bool own = write.kind == 0U
                ? slot_commit_domain_[write.slot] == domain_index
                : write.kind == 2U
                    && container_commit_domain_[lane.elements[write.member].target]
                        == domain_index;
            if (!own) {
                lane.writes.data[kept++] = write;
                continue;
            }
            if (write.kind == 2U) {
                const auto element = write.member;
                write.member = static_cast<std::uint32_t>(domain.nba_elements.size());
                domain.nba_elements.push_back(lane.elements[element]);
            }
            push_private(domain, step_item, kept - run.writes_begin, write);
            ++lane.private_count;
        }
        run.writes_end = kept;
        lane.writes.count = kept;
    }
    run.effects = run.changes_begin != run.changes_end || run.writes_begin != run.writes_end
        || run.outputs_begin != run.outputs_end || run.status != 0U
        || run.frame.status != 0U;
    lane.item_state[item] = run.effects ? 2U : 0U;
    if (lane.private_count != private_before) {
        if (domain_index < 64U) {
            lane.private_domains |= std::uint64_t { 1 } << domain_index;
        } else {
            lane.private_more = true;
        }
    }
    lane.domain = no_slot;
    worker_lane_ = nullptr;
}

void Interpreter::Impl::StaticKernel::run_in_lane(Lane& lane,
    const std::uint32_t entry, LaneRun& run)
{
    run.changes_begin = static_cast<std::uint32_t>(lane.changes.size());
    run.writes_begin = lane.writes.count;
    run.outputs_begin = static_cast<std::uint32_t>(lane.outputs.size());
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
    // A unit runs on one lane at a time: its counter is the lane's.
    if (two_state) {
        ++fast_runs_[frame.member].two_state_runs;
    }
    if (run.status != 0U || frame.status != 0U) {
        run.deopt = std::exchange(scratch.pending_deopt, std::nullopt);
        run.exception = std::exchange(scratch.native_exception, nullptr);
    }
    run.changes_end = static_cast<std::uint32_t>(lane.changes.size());
    run.writes_end = lane.writes.count;
    run.outputs_end = static_cast<std::uint32_t>(lane.outputs.size());
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
        // The unit's output first: it precedes any error the run raised.
        for (auto index = run.outputs_begin; index < run.outputs_end; ++index) {
            emit_lane_output(lane.outputs[index]);
        }
        for (auto index = run.changes_begin; index < run.changes_end; ++index) {
            replay_change(lane, lane.changes[index]);
        }
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
