// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel runtime (see simir_static_kernel.hpp).
//
// One scheduler activation of the host evaluates a complete kernel step:
//
// 1. Changed boundary inputs mark their combinational readers dirty and select
//    edge-triggered members.
// 2. Combinational members settle in level order. IEEE 1800 permits any order
//    of Active events (§4.7), and there is no normative delta per net hop.
// 3. Triggered sequential members run against the settled pre-edge state.
//    Their nonblocking writes commit together afterwards, as in the NBA
//    region, followed by another settle and derived-edge detection.
// 4. Changed boundary outputs are published to the scheduler under their
//    original driver identity. After any nonblocking commit they use the NBA
//    domain, so observers never see combinational results ahead of the
//    registers they derive from.
//
// Members whose registers have statically known widths of at most 64 bits run
// in the compiled narrow tier (simir_static_kernel_compiled.cpp); the others
// use the generic evaluator below, built on the reference value functions.
#include "simir_static_kernel_state.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <unordered_set>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <tuple>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;

namespace {

[[nodiscard]] std::uint32_t words_for(const std::uint32_t width) noexcept
{
    return width == 0U ? 1U : (width + 63U) / 64U;
}

/// Whether any plane of a slot differs between `before` and `after` (each
/// `planes * words` words) in the bit range, or anywhere when width is 0.
[[nodiscard]] bool word_range_changed(const std::uint64_t* before,
    const std::uint64_t* after, const std::uint32_t words,
    const std::uint32_t planes, const std::uint32_t offset,
    const std::uint32_t width)
{
    if (width == 0U) {
        return !std::equal(before, before + planes * words, after);
    }
    for (std::uint32_t bit = offset; bit < offset + width; ++bit) {
        const auto word = bit / 64U;
        if (word >= words) {
            break;
        }
        const auto one = std::uint64_t { 1 } << (bit % 64U);
        for (std::uint32_t plane = 0U; plane < planes; ++plane) {
            if ((before[plane * words + word] ^ after[plane * words + word])
                & one) {
                return true;
            }
        }
    }
    return false;
}

/// Plane `plane` of a value: aval, bval, then the upper Logic9 code planes.
[[nodiscard]] std::span<const std::uint64_t> value_plane(
    const PackedLogic4& value, const std::uint32_t plane)
{
    if (plane == 0U) {
        return value.aval_words();
    }
    if (plane == 1U) {
        return value.bval_words();
    }
    return value.logic9_plane_words(plane);
}

/// Copies `width` bits of `source` (from bit 0) into `target` at `offset`.
void copy_bits(std::uint64_t* target, const std::uint32_t offset,
    const std::span<const std::uint64_t> source, const std::uint32_t width)
{
    for (std::uint32_t done = 0U; done < width;) {
        const auto bit = offset + done;
        const auto shift = bit % 64U;
        const auto chunk = std::min({ 64U - shift, width - done,
            64U - done % 64U });
        const auto word = done / 64U;
        const auto bits = word < source.size()
            ? source[word] >> (done % 64U) : std::uint64_t { 0 };
        const auto m = kernel_word::mask(chunk);
        auto& out = target[bit / 64U];
        out = (out & ~(m << shift)) | ((bits & m) << shift);
        done += chunk;
    }
}

/// `width` (at most 64) bits of a plane at bit `bit`.
[[nodiscard]] std::uint64_t read_plane_bits(const std::uint64_t* plane,
    const std::uint32_t words, const std::uint32_t bit, const std::uint32_t width)
{
    const auto word = bit / 64U;
    const auto shift = bit % 64U;
    auto value = plane[word] >> shift;
    if (shift != 0U && shift + width > 64U && word + 1U < words) {
        value |= plane[word + 1U] << (64U - shift);
    }
    return value & kernel_word::mask(width);
}

[[nodiscard]] Logic4 edge_bit(const PackedLogic4& value)
{
    return value.width() == 0U ? Logic4::x : value.get(0U);
}

} // namespace

Interpreter::Impl::StaticKernel::StaticKernel(
    Impl& impl, StaticKernelRuntimeSpec spec)
    : impl_(impl)
    , vhdl_(spec.vhdl)
    , profile_(std::getenv("FSIM_PROFILE_KERNEL") != nullptr)
    , trace_(std::getenv("FSIM_KERNEL_TRACE") != nullptr)
{
    verify_ = std::getenv("FSIM_KERNEL_VERIFY") != nullptr;
    check_inputs_ = std::getenv("FSIM_KERNEL_CHECK_INPUTS") != nullptr;
    host_process_ = spec.host;
    mixed_ = spec.mixed;
    debug_points_ = spec.debug_points;
    if (debug_points_
        && std::ranges::any_of(spec.members, [&](const auto& member) {
               return member.process == spec.host;
           })) {
        host_original_ = spec.host_original;
    }
    const auto signal_count = impl_.signals.size();
    slot_of_signal_.assign(signal_count, no_slot);
    slots_.reserve(spec.owned_signals.size());
    for (const auto signal : spec.owned_signals) {
        if (signal >= signal_count || slot_of_signal_[signal] != no_slot) {
            throw std::invalid_argument("static kernel owns an invalid signal");
        }
        const bool logic9 = spec.vhdl
            && impl_.get_signal(signal).value_kind == ValueKind::logic9;
        const auto initial = Impl::coerce_value_kind(
            impl_.get_signal(signal).initial_value,
            logic9 ? ValueKind::logic9 : ValueKind::logic4);
        slot_of_signal_[signal] = static_cast<std::uint32_t>(slots_.size());
        Slot slot;
        slot.signal = signal;
        slot.width = static_cast<std::uint32_t>(initial.width());
        slot.words = words_for(slot.width);
        slot.planes = logic9 ? 4U : 2U;
        slot.offset = static_cast<std::uint32_t>(arena_.size());
        arena_.resize(arena_.size() + slot.planes * slot.words, 0U);
        for (std::uint32_t plane = 0U; plane < slot.planes; ++plane) {
            const auto words = value_plane(initial, plane);
            for (std::uint32_t word = 0U; word < slot.words; ++word) {
                arena_[slot.offset + plane * slot.words + word]
                    = word < words.size() ? words[word] : 0U;
            }
        }
        slot.edge_seen = edge_bit(initial);
        slots_.push_back(std::move(slot));
    }
    for (const auto signal : spec.boundary_outputs) {
        if (signal >= signal_count || slot_of_signal_[signal] == no_slot) {
            throw std::invalid_argument(
                "static kernel output is not kernel-owned");
        }
        auto& slot = slots_[slot_of_signal_[signal]];
        slot.output = true;
        slot.published = static_cast<std::uint32_t>(arena_.size());
        arena_.resize(arena_.size() + slot.planes * slot.words, 0U);
        std::copy_n(arena_.begin() + slot.offset, slot.planes * slot.words,
            arena_.begin() + slot.published);
    }
    const auto owned_slot = [&](const SignalId signal) {
        if (signal >= signal_count || slot_of_signal_[signal] == no_slot) {
            throw std::invalid_argument(
                "static kernel alias storage is not kernel-owned");
        }
        return slot_of_signal_[signal];
    };
    family_of_signal_.assign(signal_count, no_slot);
    for (const auto& family_spec : spec.families) {
        if (family_spec.proxy >= signal_count
            || slot_of_signal_[family_spec.proxy] != no_slot
            || family_of_signal_[family_spec.proxy] != no_slot) {
            throw std::invalid_argument("static kernel proxy is invalid");
        }
        family_of_signal_[family_spec.proxy]
            = static_cast<std::uint32_t>(families_.size());
        Family family;
        family.proxy = family_spec.proxy;
        family.width = family_spec.width;
        for (const auto& leaf : family_spec.leaves) {
            family.leaves.push_back(
                { owned_slot(leaf.signal), leaf.offset, leaf.width });
        }
        families_.push_back(std::move(family));
    }
    container_of_object_.assign(impl_.container_objects.size(), no_container);
    for (const auto& container_spec : spec.containers) {
        const auto object = container_spec.object;
        if (object >= container_of_object_.size()
            || container_of_object_[object] != no_container) {
            throw std::invalid_argument(
                "static kernel owns an invalid container");
        }
        container_of_object_[object]
            = static_cast<std::uint32_t>(containers_.size());
        const auto& initial = impl_.get_container_object(object).initial_value;
        Container container;
        container.object = object;
        container.type = initial.type;
        container.count = static_cast<std::uint32_t>(initial.elements.size());
        container.storage = container_spec.storage;
        container.element_words = words_for(
            static_cast<std::uint32_t>(initial.type.element_width));
        if (container.storage == StaticKernelContainerStorage::element_signals) {
            if (container_spec.element_signals.size() != container.count) {
                throw std::invalid_argument(
                    "static kernel element aliases are incomplete");
            }
            for (const auto signal : container_spec.element_signals) {
                container.element_slots.push_back(owned_slot(signal));
            }
        } else if (container.storage
            == StaticKernelContainerStorage::packed_signal) {
            container.packed_slot = owned_slot(container_spec.packed_signal);
            if (slots_[container.packed_slot].width
                != container.count * container.type.element_width) {
                throw std::invalid_argument(
                    "static kernel packed memory width is inconsistent");
            }
        } else {
            place_elements(container, initial);
        }
        containers_.push_back(std::move(container));
    }
    // Behavioral members own the fixed arrays they use directly (ContainerRead
    // and ContainerWrite without a design-memory binding; see the planner).
    std::vector<std::vector<std::pair<ContainerRegisterId, std::uint32_t>>>
        private_containers(spec.members.size());
    for (std::size_t index = 0U; index < spec.members.size(); ++index) {
        const auto& member_spec = spec.members[index];
        if (member_spec.kind != StaticKernelMemberKind::behavioral) {
            continue;
        }
        // The host stub takes the slot of the process it is shaped from;
        // that member runs the original program.
        const ProcessProgramView program = member_spec.process == spec.host
            ? ProcessProgramView { spec.host_original }
            : impl_.processes.program_view(member_spec.process);
        const auto& operations = program.operations();
        std::vector<std::uint8_t> bound(program.container_register_count(), 0U);
        std::vector<std::uint8_t> used(program.container_register_count(), 0U);
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            const auto operation = operations.expanded(op);
            if (const auto* read = operation_get_if<ReadContainerObject>(&operation)) {
                if (read->destination < bound.size()) {
                    bound[read->destination] = 1U;
                }
            } else if (const auto* load = operation_get_if<ContainerRead>(&operation)) {
                if (load->source < used.size()) {
                    used[load->source] = 1U;
                }
            } else if (const auto* store = operation_get_if<ContainerWrite>(&operation)) {
                if (store->target < used.size()) {
                    used[store->target] = 1U;
                }
            } else if (const auto* copy
                = operation_get_if<CopyContainerRegister>(&operation)) {
                if (copy->destination < used.size()) {
                    used[copy->destination] = 1U;
                }
                if (copy->source < used.size()) {
                    used[copy->source] = 1U;
                }
            }
        }
        const auto types = process_layout_detail::ProcessLayoutAccess::view(
            program.container_register_types());
        for (std::size_t id = 0U; id < used.size(); ++id) {
            if (used[id] == 0U || bound[id] != 0U || id >= types.size()) {
                continue;
            }
            const auto initial = impl_.default_container_register(types[id]);
            Container container;
            container.object = no_container_object;
            container.type = initial->type;
            container.count = static_cast<std::uint32_t>(initial->elements.size());
            container.element_words = words_for(
                static_cast<std::uint32_t>(initial->type.element_width));
            place_elements(container, *initial);
            private_containers[index].emplace_back(
                static_cast<ContainerRegisterId>(id),
                static_cast<std::uint32_t>(containers_.size()));
            containers_.push_back(std::move(container));
        }
    }
    // Native memory access (SystemVerilog): element slots or one packed
    // slot, one-word Logic4 elements.
    container_info_.reserve(containers_.size());
    std::vector<std::size_t> element_begin;
    for (const auto& container : containers_) {
        const auto& type = container.type;
        StaticKernelContainerInfo info;
        const bool shape = (!vhdl_ || mixed_) && type.fixed && !type.associative
            && container.element_words == 1U
            && (type.element_kind == ContainerElementKind::Packed
                || type.element_kind == ContainerElementKind::Scalar);
        element_begin.push_back(container_elements_.size());
        if (shape
            && container.storage == StaticKernelContainerStorage::element_signals
            && std::ranges::all_of(container.element_slots,
                [&](const std::uint32_t slot) {
                    return slots_[slot].words == 1U && slots_[slot].planes == 2U;
                })) {
            info.storage = 1U;
            for (const auto slot : container.element_slots) {
                container_elements_.push_back(slots_[slot].offset);
                container_elements_.push_back(slot);
            }
        } else if (shape
            && container.storage == StaticKernelContainerStorage::packed_signal
            && slots_[container.packed_slot].planes == 2U) {
            const auto& slot = slots_[container.packed_slot];
            info.storage = 2U;
            info.slot = container.packed_slot;
            info.packed_offset = slot.offset;
            info.packed_words = slot.words;
            info.packed_width = slot.width;
        } else if (shape
            && container.storage == StaticKernelContainerStorage::elements) {
            // Kernel-held elements: nothing observes them, so native code
            // also writes them in place.
            info.storage = 3U;
            info.packed_offset = container.element_offset;
            info.packed_words = 1U;
        }
        info.count = container.count;
        info.left = type.index_left;
        info.low = std::min(type.index_left, type.index_right);
        info.high = std::max(type.index_left, type.index_right);
        info.element_width = static_cast<std::uint32_t>(type.element_width);
        const Word unknown = type.two_state
            ? Word { }
            : kernel_word::all_x(info.element_width);
        info.unknown_a = unknown.a;
        info.unknown_b = unknown.b;
        info.flags = (type.two_state ? 1U : 0U)
            | (type.index_left >= type.index_right ? 2U : 0U);
        container_info_.push_back(info);
    }
    for (std::size_t index = 0U; index < container_info_.size(); ++index) {
        if (container_info_[index].storage == 1U) {
            container_info_[index].elements
                = container_elements_.data() + element_begin[index];
        }
    }

    input_of_signal_.assign(signal_count, no_slot);
    const auto input_for = [&](const SignalId signal) -> Input& {
        if (input_of_signal_[signal] == no_slot) {
            input_of_signal_[signal] = static_cast<std::uint32_t>(inputs_.size());
            Input input;
            input.signal = signal;
            inputs_.push_back(std::move(input));
        }
        return inputs_[input_of_signal_[signal]];
    };

    const bool stage_profile = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    auto stage_started = std::chrono::steady_clock::now();
    const auto stage = [&](const char* name) {
        if (stage_profile) {
            const auto now = std::chrono::steady_clock::now();
            std::cerr << "fsim-profile: static-kernel-stage " << name << "_ms="
                      << std::chrono::duration<double, std::milli>(now - stage_started).count()
                      << '\n';
            stage_started = now;
        }
    };
    stage("slots");
    double expand_seconds = 0.0;
    std::size_t expanded_ops = 0U;
    std::unordered_set<const void*> distinct_bodies;
    members_.reserve(spec.members.size());
    const bool keep_names = profile_ || trace_
        || std::getenv("FSIM_KERNEL_DUMP") != nullptr;
    for (const auto& member_spec : spec.members) {
        const ProcessProgramView program = member_spec.process == spec.host
            ? ProcessProgramView { spec.host_original }
            : impl_.processes.program_view(member_spec.process);
        Member member;
        member.process = member_spec.process;
        member.kind = member_spec.kind;
        member.run_at_start = member_spec.run_at_start;
        member.start_late = member_spec.start_late;
        member.after_procedures = program.startup_phase()
            == ProcessStartupPhase::after_procedures;
        member.partition_key = member_spec.partition;
        member.body_begin = member_spec.body_begin;
        member.prologue_end = member_spec.body_begin;
        member.body_end = member_spec.body_end;
        member.exit_alt = member_spec.exit_alt;
        member.vhdl = vhdl_ && (!mixed_ || member_spec.vhdl);
        member.fresh = member.vhdl && member_spec.run_at_start;
        if (member.vhdl) {
            const auto kinds = program.register_value_kinds();
            member.register_kinds.assign(kinds.begin(), kinds.end());
            member.register_kinds.resize(program.register_count(),
                ValueKind::logic4);
        }
        const auto& operations = program.operations();
        const auto expand_started = stage_profile ? std::chrono::steady_clock::now()
                                                  : std::chrono::steady_clock::time_point { };
        member.operations.reserve(operations.size());
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            member.operations.push_back(operations.expanded(op));
        }
        member.body_identity = operations.body_identity();
        if (stage_profile) {
            expand_seconds += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - expand_started).count();
            distinct_bodies.insert(operations.body_identity());
            expanded_ops += operations.size();
        }
        if (member.body_end > member.operations.size()
            || member.body_begin > (member.vhdl ? member.operations.size()
                                                : member.body_end)) {
            throw std::invalid_argument("static kernel member body is invalid");
        }
        if (keep_names) {
            profile_names_.push_back(program.name());
        }
        member.registers.assign(program.register_count(), PackedLogic4 { });
        member.container_registers.assign(
            program.container_register_count(), no_container);
        for (const auto& [id, container] : private_containers[members_.size()]) {
            member.container_registers[id] = container;
        }
        for (const auto& operation : member.operations) {
            const auto* push = operation_get_if<CallableFramePush>(&operation);
            if (push != nullptr
                && std::ranges::any_of(push->containers,
                    [&](const ContainerRegisterId id) {
                        return id < member.container_registers.size()
                            && member.container_registers[id] != no_container;
                    })) {
                member.private_frame_identities.push_back(push->identity);
            }
        }
        std::ranges::sort(member.private_frame_identities);
        member.private_frame_identities.erase(
            std::ranges::unique(member.private_frame_identities).begin(),
            member.private_frame_identities.end());
        const auto index = static_cast<std::uint32_t>(members_.size());
        const bool behavioral
            = member.kind == StaticKernelMemberKind::behavioral;
        if (behavioral) {
            // Threads wait on these signals dynamically: kernel slots stay
            // observable (never silent), host signals become inputs.
            behavioral_ = true;
            vhdl_threads_ = vhdl_threads_ || member.vhdl;
            member.strings.assign(program.string_register_count(), std::string { });
            member.wait_sensitivity = member_spec.sensitivity;
            const auto waitable = [&](const SignalId signal) {
                if (signal >= signal_count || family_of_signal_[signal] != no_slot) {
                    throw std::invalid_argument(
                        "static kernel behavioral wait targets an unsupported signal");
                }
                if (const auto slot = slot_of_signal_[signal]; slot != no_slot) {
                    slot_waitable_.resize(slots_.size(), 0U);
                    slot_waitable_[slot] = 1U;
                } else {
                    (void)input_for(signal);
                }
            };
            for (const auto& entry : member.wait_sensitivity) {
                waitable(entry.signal);
            }
            for (const auto& operation : member.operations) {
                if (const auto* wait = operation_get_if<WaitOn>(&operation)) {
                    for (const auto signal : wait->signals) {
                        waitable(signal);
                    }
                }
            }
        }
        for (const auto& sensitivity : behavioral
                 ? std::span<const Sensitivity> { }
                 : std::span<const Sensitivity> { member_spec.sensitivity }) {
            if (sensitivity.signal >= signal_count) {
                throw std::invalid_argument(
                    "static kernel sensitivity is invalid");
            }
            const auto slot = slot_of_signal_[sensitivity.signal];
            if (const auto family = family_of_signal_[sensitivity.signal];
                family != no_slot) {
                if (sensitivity.edge != EdgeKind::any) {
                    throw std::invalid_argument(
                        "static kernel proxy edge sensitivity is unsupported");
                }
                const auto begin = sensitivity.offset;
                const auto end = sensitivity.width == 0U
                    ? families_[family].width
                    : sensitivity.offset + sensitivity.width;
                for (const auto& leaf : families_[family].leaves) {
                    const auto low = std::max(begin, leaf.offset);
                    const auto high = std::min(end, leaf.offset + leaf.width);
                    if (low < high) {
                        slots_[leaf.slot].readers.push_back({ index,
                            low - leaf.offset,
                            low == leaf.offset && high == leaf.offset + leaf.width
                                ? 0U : high - low });
                    }
                }
                continue;
            }
            if (sensitivity.edge == EdgeKind::posedge
                || sensitivity.edge == EdgeKind::negedge) {
                const EdgeReader reader { index, sensitivity.edge };
                if (slot != no_slot) {
                    slots_[slot].edges.push_back(reader);
                } else {
                    input_for(sensitivity.signal).edges.push_back(reader);
                }
            } else if (sensitivity.edge == EdgeKind::any) {
                const Reader reader { index, sensitivity.offset,
                    sensitivity.width };
                if (slot != no_slot) {
                    slots_[slot].readers.push_back(reader);
                } else {
                    input_for(sensitivity.signal).readers.push_back(reader);
                }
            } else {
                throw std::invalid_argument(
                    "static kernel sensitivity edge is unsupported");
            }
        }
        for (const auto& region : program.driver_regions()) {
            if (member.vhdl || region.signal >= signal_count) {
                continue;
            }
            const auto slot = slot_of_signal_[region.signal];
            if (slot == no_slot) {
                continue;
            }
            const auto width = slots_[slot].width;
            slots_[slot].writers.push_back({ member.process,
                region.whole ? 0U : static_cast<std::uint32_t>(region.offset),
                region.whole ? width : static_cast<std::uint32_t>(region.width) });
        }
        members_.push_back(std::move(member));
    }
    member_process_.reserve(members_.size());
    for (const auto& member : members_) {
        member_process_.push_back(member.process);
    }
    {
        // A partition runs all of its members on any trigger of the
        // partition. That is unobservable only for a member that is the sole
        // writer of what it writes: running it again writes its own values
        // again. A member sharing written bits with another process (two
        // always_comb blocks assigning one variable, a host signal with
        // several drivers) would overwrite the other's later value although
        // none of its own events occurred (IEEE 1800-2023 9.4.2), so it runs
        // alone. Processes writing disjoint bit ranges of one vector (one
        // continuous assignment per bit) each rewrite only their own bits.
        struct Region {
            std::uint64_t begin { };
            std::uint64_t end { };
            ProcessId process { };
        };
        std::vector<std::vector<Region>> regions(signal_count);
        const auto note = [&](const ProcessProgramView& program,
                              const ProcessId process) {
            if (!program.valid()) {
                return;
            }
            for (const auto& region : program.driver_regions()) {
                if (region.signal >= signal_count) {
                    continue;
                }
                regions[region.signal].push_back(region.whole
                        ? Region { 0U, std::numeric_limits<std::uint64_t>::max(),
                              process }
                        : Region { region.offset,
                              std::uint64_t { region.offset } + region.width,
                              process });
            }
        };
        for (ProcessId process = 0U; process < impl_.processes.size(); ++process) {
            note(process == spec.host ? ProcessProgramView { spec.host_original }
                                      : impl_.processes.program_view(process),
                process);
        }
        // A sweep in offset order: a region starting below the furthest end
        // reached so far overlaps an earlier region, shared when the region
        // reaching furthest belongs to another process (one that overlaps
        // only a same-process region was already compared to the others).
        std::vector<std::uint8_t> shared(signal_count, 0U);
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            auto& list = regions[signal];
            if (list.size() < 2U) {
                continue;
            }
            std::ranges::sort(list, { }, &Region::begin);
            auto reach = list.front().end;
            auto reach_process = list.front().process;
            for (std::size_t at = 1U; at < list.size(); ++at) {
                const auto& region = list[at];
                if (region.begin < reach && region.process != reach_process) {
                    shared[signal] = 1U;
                    break;
                }
                if (region.end > reach) {
                    reach = region.end;
                    reach_process = region.process;
                }
            }
        }
        for (auto& member : members_) {
            if (member.partition_key == no_slot) {
                continue;
            }
            const ProcessProgramView program = member.process == spec.host
                ? ProcessProgramView { spec.host_original }
                : impl_.processes.program_view(member.process);
            if (std::ranges::any_of(program.driver_regions(),
                    [&](const auto& region) {
                        return region.signal < signal_count
                            && shared[region.signal] != 0U;
                    })) {
                member.partition_key = no_slot;
            }
        }
    }
    fast_runs_.assign(members_.size(), FastRun { });
    for (const auto& region : spec.writer_regions) {
        if (region.signal < signal_count
            && slot_of_signal_[region.signal] != no_slot) {
            auto& slot = slots_[slot_of_signal_[region.signal]];
            slot.writers.push_back({ region.process, region.offset,
                region.width == 0U ? slot.width : region.width });
        }
    }
    {
        // A slot written by VHDL members publishes in the generic domain.
        std::vector<std::uint8_t> vhdl_process(impl_.processes.size(), 0U);
        for (const auto& member : members_) {
            if (member.vhdl && member.process < vhdl_process.size()) {
                vhdl_process[member.process] = 1U;
            }
        }
        for (auto& slot : slots_) {
            slot.vhdl_written = vhdl_ && !mixed_;
            for (const auto& writer : slot.writers) {
                if (writer.process < vhdl_process.size()
                    && vhdl_process[writer.process] != 0U) {
                    slot.vhdl_written = true;
                }
            }
        }
    }
    for (auto& slot : slots_) {
        if (slot.writers.size() == 1U) {
            slot.last_writer = slot.writers.front().process;
            slot.single_writer = true;
            continue;
        }
        auto writers = slot.writers;
        std::ranges::sort(writers, [](const auto& left, const auto& right) {
            return left.offset < right.offset;
        });
        for (std::size_t index = 1U; index < writers.size(); ++index) {
            if (writers[index - 1U].offset + writers[index - 1U].width
                > writers[index].offset) {
                slot.disjoint_writers = false;
            }
        }
        if (!slot.writers.empty()) {
            slot.last_writer = slot.writers.front().process;
            slot.single_writer = std::ranges::all_of(slot.writers,
                [&](const auto& writer) {
                    return writer.process == slot.last_writer;
                });
        }
    }

    // Mirrors: narrow host signals members read become arena slots that
    // scan_inputs refreshes, so compiled reads are plain loads. A signal a
    // member writes immediately on the host is not mirrored.
    mirror_of_signal_.assign(signal_count, no_slot);
    if (std::getenv("FSIM_KERNEL_NO_MIRRORS") == nullptr) {
        std::vector<std::uint8_t> read(signal_count, 0U);
        std::vector<std::uint8_t> blocking(signal_count, 0U);
        for (const auto& member : members_) {
            for (const auto& operation : member.operations) {
                visit_operation([&](const auto& op) {
                    using T = std::decay_t<decltype(op)>;
                    if constexpr (std::is_same_v<T, ReadSignal>) {
                        if (op.signal < signal_count) {
                            read[op.signal] = 1U;
                        }
                    } else if constexpr (std::is_same_v<T, WriteBlocking>
                        || std::is_same_v<T, WriteBlockingSlice>
                        || std::is_same_v<T, WriteBlockingDynamicSlice>
                        || std::is_same_v<T, WriteBlockingDynamicPartSlice>) {
                        if (op.signal < signal_count) {
                            blocking[op.signal] = 1U;
                        }
                    }
                }, operation);
            }
        }
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (read[signal] == 0U || blocking[signal] != 0U || owned(signal)) {
                continue;
            }
            const auto& info = impl_.get_signal(signal);
            const auto width = static_cast<std::uint32_t>(info.initial_value.width());
            if (width == 0U
                || info.systemverilog_scalar != SystemVerilogScalarKind::None) {
                continue;
            }
            // Wide mirrors are read in place by narrow selections.
            Slot slot;
            slot.signal = signal;
            slot.width = width;
            slot.words = (width + 63U) / 64U;
            slot.planes = vhdl_ && info.value_kind == ValueKind::logic9 ? 4U : 2U;
            slot.offset = static_cast<std::uint32_t>(arena_.size());
            slot.mirror = true;
            arena_.resize(arena_.size() + slot.planes * slot.words, 0U);
            mirror_of_signal_[signal] = static_cast<std::uint32_t>(slots_.size());
            input_for(signal).mirror = mirror_of_signal_[signal];
            slots_.push_back(std::move(slot));
        }
    }

    // Wired formals: an owned signal whose host resolver is a pure-routing
    // program over an owned source (static_kernel_wiring) is driven by the
    // kernel from now on; the host does not resolve it again.
    for (const auto signal : spec.owned_signals) {
        const auto resolver = impl_.vhdl_resolvers.find(signal);
        if (resolver == impl_.vhdl_resolvers.end()
            || resolver->second.sources.size() != 1U
            || !resolver->second.drivers.empty()
            || !resolver->second.variants.empty()
            || resolver->second.driving_mirror
            || resolver->second.process >= impl_.processes.size()) {
            continue;
        }
        const auto source = resolver->second.sources.front();
        const auto wire = static_kernel_wiring(
            impl_.processes.program_view(resolver->second.process).operations(),
            resolver->second.output, [&](const SignalId id) {
                return static_cast<std::uint64_t>(
                    impl_.get_signal(id).initial_value.width());
            });
        if (!wire || wire->source != source || source >= signal_count
            || slot_of_signal_[source] == no_slot) {
            continue;
        }
        const auto& from = slots_[slot_of_signal_[source]];
        const auto& to = slots_[slot_of_signal_[signal]];
        if (from.planes != to.planes || to.width != wire->width
            || wire->offset + wire->width > from.width) {
            throw std::invalid_argument(
                "static kernel wiring does not fit its signals");
        }
        wires_.push_back({ slot_of_signal_[source], slot_of_signal_[signal],
            wire->offset, wire->width, 0U });
        auto& dependents = impl_.vhdl_conversion_dependents[source];
        std::erase(dependents, signal);
        impl_.vhdl_resolvers.erase(resolver);
    }
    // Wired actual parts: an owned signal whose conversion drivers are
    // pure-routing programs over owned output formals (the planner owns it
    // only then) is driven part by part by the kernel; the host runs none of
    // them again.
    for (const auto signal : spec.owned_signals) {
        const auto found = impl_.vhdl_conversion_drivers.find(signal);
        if (found == impl_.vhdl_conversion_drivers.end()) {
            continue;
        }
        for (const auto& driver : found->second) {
            const auto wire = driver.sources.size() == 1U
                    && driver.program < impl_.processes.size()
                ? static_kernel_wiring(
                      impl_.processes.program_view(driver.program).operations(),
                      driver.output, [&](const SignalId id) {
                          return static_cast<std::uint64_t>(
                              impl_.get_signal(id).initial_value.width());
                      })
                : std::nullopt;
            const auto source = wire ? wire->source : SignalId { };
            const auto target_offset = driver.offset.value_or(0U);
            if (!wire || source != driver.sources.front()
                || source >= signal_count || slot_of_signal_[source] == no_slot
                || impl_.vhdl_resolvers.contains(signal)) {
                throw std::invalid_argument(
                    "static kernel wiring does not fit its signals");
            }
            const auto& from = slots_[slot_of_signal_[source]];
            const auto& to = slots_[slot_of_signal_[signal]];
            if (from.planes != to.planes || wire->offset + wire->width > from.width
                || target_offset + wire->width > to.width
                || (!driver.offset && wire->width != to.width)) {
                throw std::invalid_argument(
                    "static kernel wiring does not fit its signals");
            }
            wires_.push_back({ slot_of_signal_[source], slot_of_signal_[signal],
                wire->offset, wire->width,
                static_cast<std::uint32_t>(target_offset) });
            std::erase(impl_.vhdl_conversion_dependents[source], signal);
        }
        impl_.vhdl_conversion_drivers.erase(found);
    }
    std::ranges::stable_sort(wires_, {}, &Wire::source);
    slot_wires_.assign(slots_.size(), { 0U, 0U });
    for (std::uint32_t index = 0U; index < wires_.size();) {
        auto end = index;
        while (end < wires_.size() && wires_[end].source == wires_[index].source) {
            ++end;
        }
        slot_wires_[wires_[index].source] = { index, end };
        index = end;
    }
    // A wired formal starts at its source's initial value (IEEE 1076-2008
    // 14.7.5.2); wires chain, so repeat until the values settle. This runs
    // before any activation, so a value deposited on the formal before the
    // first cycle (a VHPI start-of-simulation callback) is kept.
    for (std::size_t pass = 0U; pass <= wires_.size(); ++pass) {
        bool changed = false;
        for (const auto& wire : wires_) {
            changed = copy_wire(wire) || changed;
        }
        if (!changed) {
            break;
        }
    }

    // Level combinational members: writer before reader, cycles last.
    const auto count = members_.size();
    std::vector<std::vector<std::uint32_t>> successors(count);
    std::vector<std::uint32_t> indegree(count, 0U);
    std::unordered_map<SignalId, std::vector<std::uint32_t>> signal_writers;
    // A combinational member may read more than it is sensitive to (`@(d iff
    // enable)`, an incomplete event list): only its sensitivity wakes it, but
    // it runs after the members that settle everything it reads.
    std::vector<std::vector<std::uint32_t>> slot_readers(slots_.size());
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (members_[index].kind != StaticKernelMemberKind::combinational) {
            continue;
        }
        for (const auto& operation : members_[index].operations) {
            const auto* read = operation_get_if<ReadSignal>(&operation);
            if (read == nullptr || read->signal >= signal_count) {
                continue;
            }
            const auto add = [&](const std::uint32_t slot) {
                auto& readers = slot_readers[slot];
                if (readers.empty() || readers.back() != index) {
                    readers.push_back(index);
                }
            };
            if (slot_of_signal_[read->signal] != no_slot) {
                add(slot_of_signal_[read->signal]);
            } else if (family_of_signal_[read->signal] != no_slot) {
                for (const auto& leaf :
                    families_[family_of_signal_[read->signal]].leaves) {
                    add(leaf.slot);
                }
            }
        }
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto& member = members_[index];
        if (member.kind != StaticKernelMemberKind::combinational) {
            continue;
        }
        std::vector<std::uint32_t> written;
        std::vector<SignalId> assigned;
        for (const auto& operation : member.operations) {
            // A write in the NBA region lands after the Active region has
            // drained, so it orders nothing within a pass: its readers wake
            // when the commit changes the slot.
            bool deferred = false;
            const auto signal = visit_operation([&deferred](const auto& op)
                -> std::optional<SignalId> {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>) {
                    return op.signal;
                } else if constexpr (std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    deferred = op.domain == SignalUpdateDomain::systemverilog_nba;
                    return op.signal;
                } else {
                    return std::nullopt;
                }
            }, operation);
            if (signal && *signal < signal_count) {
                if (!deferred) {
                    assigned.push_back(*signal);
                    auto& writers = signal_writers[*signal];
                    if (writers.empty() || writers.back() != index) {
                        writers.push_back(index);
                    }
                }
                if (slot_of_signal_[*signal] != no_slot) {
                    if (deferred) {
                        slots_[slot_of_signal_[*signal]].deferred_write = true;
                    } else {
                        written.push_back(slot_of_signal_[*signal]);
                    }
                } else if (family_of_signal_[*signal] != no_slot) {
                    for (const auto& leaf :
                        families_[family_of_signal_[*signal]].leaves) {
                        if (deferred) {
                            slots_[leaf.slot].deferred_write = true;
                        } else {
                            written.push_back(leaf.slot);
                        }
                    }
                }
            }
        }
        std::ranges::sort(assigned);
        members_[index].rewrites_signal
            = std::ranges::adjacent_find(assigned) != assigned.end();
        std::ranges::sort(written);
        written.erase(std::unique(written.begin(), written.end()),
            written.end());
        for (const auto slot : written) {
            for (const auto& reader : slots_[slot].readers) {
                if (reader.member != index
                    && members_[reader.member].kind
                        == StaticKernelMemberKind::combinational) {
                    successors[index].push_back(reader.member);
                    ++indegree[reader.member];
                }
            }
            for (const auto reader : slot_readers[slot]) {
                if (reader != index) {
                    successors[index].push_back(reader);
                    ++indegree[reader];
                }
            }
        }
    }
    // Members assigning the same variable run in process order, the order
    // the host scheduler gives processes woken together, so the last writer
    // is the reference's (IEEE 1800-2023 4.7 leaves it unspecified).
    // Host signals the kernel writes and reads back: writer before reader.
    std::unordered_map<SignalId, std::vector<std::uint32_t>> host_readers;
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (members_[index].kind != StaticKernelMemberKind::combinational) {
            continue;
        }
        for (const auto& operation : members_[index].operations) {
            const auto* read = operation_get_if<ReadSignal>(&operation);
            if (read != nullptr && read->signal < signal_count
                && slot_of_signal_[read->signal] == no_slot
                && family_of_signal_[read->signal] == no_slot
                && signal_writers.contains(read->signal)) {
                auto& readers = host_readers[read->signal];
                if (readers.empty() || readers.back() != index) {
                    readers.push_back(index);
                }
            }
        }
    }
    for (const auto& [signal, readers] : host_readers) {
        for (const auto writer : signal_writers[signal]) {
            for (const auto reader : readers) {
                if (reader != writer) {
                    successors[writer].push_back(reader);
                    ++indegree[reader];
                }
            }
        }
    }
    for (auto& [signal, writers] : signal_writers) {
        std::ranges::sort(writers, { }, [&](const std::uint32_t member) {
            return members_[member].process;
        });
        for (std::size_t position = 1U; position < writers.size(); ++position) {
            successors[writers[position - 1U]].push_back(writers[position]);
            ++indegree[writers[position]];
        }
    }
    std::vector<std::uint32_t> ready;
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (indegree[index] == 0U) {
            ready.push_back(index);
        }
    }
    std::uint32_t level = 0U;
    std::vector<std::uint8_t> placed(count, 0U);
    while (!ready.empty()) {
        std::vector<std::uint32_t> next;
        for (const auto index : ready) {
            members_[index].level = level;
            placed[index] = 1U;
            for (const auto successor : successors[index]) {
                if (--indegree[successor] == 0U) {
                    next.push_back(successor);
                }
            }
        }
        ready = std::move(next);
        ++level;
    }
    for (std::uint32_t index = 0U; index < count; ++index) {
        if (placed[index] == 0U) {
            members_[index].level = level++;
        }
    }
    if (stage_profile) {
        std::cerr << "fsim-profile: static-kernel-members expand_ms=" << expand_seconds * 1000.0
                  << " ops=" << expanded_ops << " distinct_bodies=" << distinct_bodies.size()
                  << " members=" << spec.members.size() << '\n';
    }
    stage("members");
    if (std::getenv("FSIM_STATIC_KERNEL_GENERIC") == nullptr) {
        restored_specializations_ = std::move(spec.specializations);
        recorded_specializations_ = std::move(spec.record_specializations);
        restored_bodies_ = std::move(spec.compiled_bodies);
        recorded_bodies_ = std::move(spec.record_compiled_bodies);
        restored_native_ = std::move(spec.native_image);
        recorded_native_ = std::move(spec.record_native_image);
        compile_members();
        stage("compile");
        // VHDL members defer their writes and are not partitioned.
        if ((!vhdl_ || mixed_)
            && std::getenv("FSIM_STATIC_KERNEL_NO_PARTITIONS") == nullptr) {
            build_partitions(successors);
        }
        stage("partitions");
        if (spec.codegen) {
            codegen_ = spec.codegen;
            build_native(*codegen_);
        }
        stage("native");
    }
    build_schedule_targets();
    stage("schedule");
    for (const auto signal : spec.unwritten_inputs) {
        if (signal < input_of_signal_.size() && input_of_signal_[signal] != no_slot) {
            inputs_[input_of_signal_[signal]].unwritten = true;
        }
    }
    for (const auto signal : spec.sole_writer_inputs) {
        if (signal < input_of_signal_.size() && input_of_signal_[signal] != no_slot) {
            inputs_[input_of_signal_[signal]].sole_writer = !vhdl_;
        }
    }
    for (std::uint32_t index = 0U; index < inputs_.size(); ++index) {
        if (!inputs_[index].unwritten) {
            written_inputs_.push_back(index);
        }
    }
    // Hosted processes (simir_static_kernel_hosted.cpp): SystemVerilog ones
    // in a SystemVerilog kernel, VHDL ones in a VHDL or mixed kernel, where
    // they run in the delta rounds (S25a).
    if (!spec.hosted.empty()) {
        adopt_hosted(spec.hosted);
    }
    // A hosted VHDL process writes through the host's update phase, so every
    // round is a host delta.
    host_boundary_ = has_hosted_
        || std::ranges::any_of(inputs_,
            [](const Input& input) { return !input.unwritten; })
        || std::ranges::any_of(slots_, [](const Slot& slot) { return slot.output; });
    // Every process is a member and nothing crosses the host boundary.
    closed_ = !host_boundary_ && !has_hosted_
        && impl_.processes.size() == members_.size();
    if (std::getenv("FSIM_PROFILE_PHASES") != nullptr) {
        report_interpreted_sites();
    }
}

Interpreter::Impl::StaticKernel::~StaticKernel()
{
    if (!profile_) {
        return;
    }
    for (const auto& lazy : lazy_templates_) {
        if (lazy.code.body != nullptr && !lazy.code.body->resume_entries.empty()) {
            std::cerr << "fsim-kernel: lazy-behavioral size="
                      << lazy.code.body->code.size() << " work=" << lazy.work
                      << " compiled=" << (lazy.units.empty()
                                 || lazy.units.front()->entry != nullptr)
                      << '\n';
        }
    }
    std::cerr << "fsim-kernel: deopts=" << profile_deopts_
              << " generic_runs=" << profile_generic_runs_ << '\n';
    if (vhdl_) {
        // Members that deoptimize or run generically most often.
        std::vector<std::pair<std::uint64_t, std::string>> generic;
        std::map<std::string, std::uint64_t> grouped;
        for (std::size_t index = 0U; index < members_.size()
             && index < profile_member_deopts_.size(); ++index) {
            if (profile_member_deopts_[index] == 0U
                || index >= profile_names_.size()) {
                continue;
            }
            std::string name;
            for (const auto character : profile_names_[index]) {
                if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
                    name += character;
                } else if (name.empty() || name.back() != 'N') {
                    name += 'N';
                }
            }
            grouped[name] += profile_member_deopts_[index];
        }
        for (const auto& [name, count] : grouped) {
            generic.emplace_back(count, name);
        }
        std::ranges::sort(generic, std::greater<> { });
        for (std::size_t rank = 0U; rank < generic.size() && rank < 12U; ++rank) {
            std::cerr << "fsim-kernel: deopt group count=" << generic[rank].first
                      << ' ' << generic[rank].second << '\n';
        }
    }
    std::cerr << "fsim-kernel: activation_ms="
              << static_cast<double>(profile_activation_ns_) / 1e6
              << " native_runs=" << profile_native_calls_[4]
              << " evaluate=" << profile_native_calls_[0]
              << " effect=" << profile_native_calls_[1]
              << " notify=" << profile_native_calls_[2]
              << " generic=" << profile_native_calls_[3]
              << " slot_changes=" << profile_slot_changes_
              << " schedules=" << profile_schedules_ << '\n';
    std::cerr << "fsim-kernel: activations=" << profile_activations_
              << " combinational_runs=" << profile_runs_[0]
              << " sequential_runs=" << profile_runs_[1]
              << " once_runs=" << profile_runs_[2]
              << " compiled_runs=" << profile_compiled_runs_
              << " partitions=" << partitions_.size()
              << " partition_runs=" << profile_partition_runs_
              << " native_units=" << native_units_
              << " templates=" << templates_.size()
              << " compiled_members=" << compiled_members_ << '/'
              << members_.size()
              << " specialized=" << specialized_members_
              << " warps=" << profile_warps_
              << " specialize_ms=" << specialize_seconds_ * 1000.0
              << " specialize_hits=" << specialize_cache_hits_
              << " operations=" << profile_operations_
              << " inputs=" << inputs_.size()
              << " input_changes=" << profile_input_changes_
              << " publishes=" << profile_publishes_ << '\n';
    {
        // Run distribution over templates: the code generation each
        // threshold of runs would need.
        std::vector<std::pair<std::uint64_t, std::size_t>> runs;
        for (const auto& program : templates_) {
            const auto found = profile_template_runs_.find(program.get());
            runs.emplace_back(found == profile_template_runs_.end() ? 0U : found->second,
                program->code.size());
        }
        for (const std::uint64_t threshold : { 1ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL }) {
            std::size_t count = 0U;
            std::size_t size = 0U;
            for (const auto& [count_runs, code] : runs) {
                if (count_runs >= threshold) {
                    ++count;
                    size += code;
                }
            }
            std::cerr << "fsim-kernel: templates_run_at_least_" << threshold << "="
                      << count << " insts=" << size << '\n';
        }
    }
    {
        std::vector<std::pair<std::uint64_t, std::string>> generic;
        for (const auto& [key, count] : profile_generic_ops_) {
            generic.emplace_back(count, key);
        }
        std::ranges::sort(generic, std::greater<> { });
        for (std::size_t rank = 0U; rank < generic.size() && rank < 40U; ++rank) {
            std::cerr << "fsim-kernel: generic " << generic[rank].first << ' '
                      << generic[rank].second << '\n';
        }
    }
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> groups;
    for (std::uint32_t index = 0U; index < profile_member_runs_.size(); ++index) {
        if (profile_member_runs_[index] == 0U) {
            continue;
        }
        const auto& name = profile_names_[index];
        std::string pattern;
        for (std::size_t at = 0U; at < name.size(); ++at) {
            if (std::isdigit(static_cast<unsigned char>(name[at])) != 0) {
                pattern += 'N';
                while (at + 1U < name.size()
                    && std::isdigit(static_cast<unsigned char>(name[at + 1U])) != 0) {
                    ++at;
                }
            } else {
                pattern += name[at];
            }
        }
        const auto& member = members_[index];
        auto& group = groups[std::to_string(static_cast<int>(member.kind))
            + (member.compiled ? " c " : " g ") + pattern];
        group.first += profile_member_runs_[index];
        group.second += profile_member_runs_[index]
            * (member.body_end - member.body_begin);
    }
    for (std::uint32_t partition = 0U;
         partition < profile_partition_counts_.size(); ++partition) {
        if (profile_partition_counts_[partition] == 0U) {
            continue;
        }
        const auto& name = profile_names_[partitions_[partition].members.front()];
        std::string pattern = "P ";
        for (std::size_t at = 0U; at < name.size(); ++at) {
            if (std::isdigit(static_cast<unsigned char>(name[at])) != 0) {
                pattern += 'N';
                while (at + 1U < name.size()
                    && std::isdigit(static_cast<unsigned char>(name[at + 1U])) != 0) {
                    ++at;
                }
            } else {
                pattern += name[at];
            }
        }
        pattern += " size=" + std::to_string(partitions_[partition].program.code.size());
        auto& group = groups[pattern];
        group.first += profile_partition_counts_[partition];
        group.second += profile_partition_operations_[partition];
    }
    std::vector<std::pair<std::uint64_t, std::string>> ordered;
    for (const auto& [pattern, group] : groups) {
        ordered.emplace_back(group.second, pattern);
    }
    std::ranges::sort(ordered, std::greater<> { });
    for (std::size_t rank = 0U; rank < ordered.size() && rank < 30U; ++rank) {
        std::cerr << "fsim-kernel: group ops~" << ordered[rank].first
                  << " runs=" << groups[ordered[rank].second].first << ' '
                  << ordered[rank].second << '\n';
    }
}

void Interpreter::Impl::StaticKernel::fail(const std::uint32_t member,
    const InstructionIndex instruction, const std::string& message) const
{
    const auto& origin = members_[member].origin;
    throw InterpreterError { members_[member].process,
        instruction < origin.size() ? origin[instruction] : instruction, message };
}

PackedLogic4 Interpreter::Impl::StaticKernel::slot_value(
    const std::uint32_t slot) const
{
    const auto& state = slots_[slot];
    const auto plane = [&](const std::uint32_t index) {
        return std::span<const std::uint64_t> {
            arena_.data() + state.offset + index * state.words, state.words
        };
    };
    if (state.planes == 4U) {
        return PackedLogic4::from_logic9_word_planes(state.width, plane(0U),
            plane(1U), plane(2U), plane(3U));
    }
    return PackedLogic4::from_word_planes(state.width, plane(0U), plane(1U));
}

Logic4 Interpreter::Impl::StaticKernel::slot_edge_bit(
    const std::uint32_t slot_index) const noexcept
{
    const auto& slot = slots_[slot_index];
    if (slot.planes != 4U) {
        return kernel_word::bit(slot_word(slot_index), 0U);
    }
    std::uint32_t code = 0U;
    for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
        code |= static_cast<std::uint32_t>(
                    arena_[slot.offset + plane * slot.words] & 1U)
            << plane;
    }
    return code <= static_cast<std::uint32_t>(Logic9::dont_care)
        ? to_logic4(static_cast<Logic9>(code)) : Logic4::x;
}

PackedLogic4 Interpreter::Impl::StaticKernel::read_signal(const SignalId signal)
{
    const auto slot = slot_of_signal_[signal];
    if (slot != no_slot) {
        return slot_value(slot);
    }
    if (const auto family = family_of_signal_[signal]; family != no_slot) {
        return assemble(families_[family]);
    }
    return impl_.logical_signal_value(signal);
}

PackedLogic4 Interpreter::Impl::StaticKernel::assemble(
    const Family& family) const
{
    PackedLogic4 value(family.width, Logic4::x);
    for (const auto& leaf : family.leaves) {
        value = insert_value(std::move(value), slot_value(leaf.slot),
            leaf.offset);
    }
    return value;
}

Interpreter::Impl::StaticKernel::Word
Interpreter::Impl::StaticKernel::read_field(const std::uint32_t slot_index,
    const std::uint32_t offset, const std::uint32_t width) const noexcept
{
    const auto& slot = slots_[slot_index];
    const auto* a = arena_.data() + slot.offset;
    const auto* b = a + slot.words;
    const auto word = offset / 64U;
    const auto shift = offset % 64U;
    auto value_a = a[word] >> shift;
    auto value_b = b[word] >> shift;
    if (shift != 0U && shift + width > 64U && word + 1U < slot.words) {
        value_a |= a[word + 1U] << (64U - shift);
        value_b |= b[word + 1U] << (64U - shift);
    }
    const auto m = kernel_word::mask(width);
    return { value_a & m, value_b & m };
}

void Interpreter::Impl::StaticKernel::slot_changed(const std::uint32_t slot_index,
    const std::uint64_t changed_mask, const std::uint64_t* before,
    const std::uint32_t field_offset)
{
    profile_slot_changes_ += profile_ ? 1U : 0U;
    const auto notify = slot_notify_[slot_index];
    for (auto index = notify.begin; index < notify.end; ++index) {
        schedule_target(notify_targets_[index]);
    }
    if (notify.general) {
        slot_changed_general(slot_index, changed_mask, before, field_offset);
    }
}

bool Interpreter::Impl::StaticKernel::waiter_range_changed(
    const Waiter& waiter, const WaitChange& change) const
{
    if (change.input_after != nullptr) {
        const auto& before = *change.input_before;
        const auto& after = *change.input_after;
        if (change.event || before.width() != after.width()
            || waiter.offset + waiter.width > after.width()) {
            return true;
        }
        for (std::uint32_t bit = waiter.offset;
             bit < waiter.offset + waiter.width; ++bit) {
            if (vhdl_ ? before.get_logic9(bit) != after.get_logic9(bit)
                      : before.get(bit) != after.get(bit)) {
                return true;
            }
        }
        return false;
    }
    if (change.slot_before != nullptr) {
        const auto& slot = slots_[change.slot];
        return word_range_changed(change.slot_before,
            arena_.data() + slot.offset, slot.words, slot.planes,
            waiter.offset, waiter.width);
    }
    const auto low = std::max(waiter.offset, change.field_offset);
    const auto high = std::min(waiter.offset + waiter.width,
        change.field_offset + 64U);
    return low < high
        && ((change.changed_mask >> (low - change.field_offset))
               & kernel_word::mask(high - low))
            != 0U;
}

void Interpreter::Impl::StaticKernel::slot_changed_general(
    const std::uint32_t slot_index, const std::uint64_t changed_mask,
    const std::uint64_t* before, const std::uint32_t field_offset)
{
    auto& slot = slots_[slot_index];
    for (const auto& group : slot.ranged) {
        // Scheduling a target that is already pending changes nothing.
        if (target_pending(group.id)) {
            continue;
        }
        for (auto index = group.begin; index < group.end; ++index) {
            const auto& reader = slot.readers[index];
            bool differs = true;
            if (before == nullptr) {
                const auto low = std::max(reader.offset, field_offset);
                const auto high = std::min(reader.offset + reader.width,
                    field_offset + 64U);
                differs = low < high
                    && ((changed_mask >> (low - field_offset))
                           & kernel_word::mask(high - low))
                        != 0U;
            } else {
                differs = word_range_changed(before,
                    arena_.data() + slot.offset, slot.words, slot.planes,
                    reader.offset, reader.width);
            }
            if (differs) {
                schedule(reader.member);
                if (target_pending(group.id)) {
                    break;
                }
            }
        }
    }
    if (slot.output) {
        if (!slot.output_pending) {
            slot.output_pending = true;
            slot.publish_nba = false;
            output_queue_.push_back(slot_index);
        }
        // Inside the host's NBA region (the deferred commit) an Active update
        // is the next region.
        slot.publish_nba = slot.publish_nba
            || (nonblocking_committed_ && !in_host_nonblocking_region_);
    }
    if (slot_index < slot_wait_seen_.size() && slot_waitable_[slot_index] != 0U) {
        const auto seen = slot_wait_seen_[slot_index];
        const auto current = slot_edge_bit(slot_index);
        slot_wait_seen_[slot_index] = current;
        if (!slot_waiters_[slot_index].empty()) {
            WaitChange change;
            change.before = seen;
            change.after = current;
            change.slot_before = before;
            change.slot = slot_index;
            change.field_offset = field_offset;
            change.changed_mask = changed_mask;
            wake_waiters(slot_waiters_[slot_index], change);
        }
    }
    if (!slot.edges.empty() && !slot.trigger_pending) {
        slot.trigger_pending = true;
        trigger_queue_.push_back(slot_index);
    }
}

// Native code's change notifications live beside slot_changed so the
// notify-target loop inlines into them.
void Interpreter::Impl::StaticKernel::native_notify(
    StaticKernelNativeFrame* frame, const std::uint32_t slot,
    const std::uint64_t changed)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[2] += kernel.profile_ ? 1U : 0U;
    try {
        kernel.running_position_ = frame->position;
        // Only a general slot can have several writers.
        if (kernel.slot_notify_[slot].general) {
            if (auto& state = kernel.slots_[slot]; !state.single_writer) {
                state.last_writer = kernel.member_process_[frame->member];
            }
        }
        kernel.slot_changed(slot, changed, nullptr);
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::native_notify_field(
    StaticKernelNativeFrame* frame, const std::uint32_t slot,
    const std::uint64_t changed, const std::uint32_t field_offset)
{
    auto& kernel = *static_cast<StaticKernel*>(frame->kernel);
    kernel.profile_native_calls_[2] += kernel.profile_ ? 1U : 0U;
    try {
        kernel.running_position_ = frame->position;
        if (kernel.slot_notify_[slot].general) {
            if (auto& state = kernel.slots_[slot]; !state.single_writer) {
                state.last_writer = kernel.member_process_[frame->member];
            }
        }
        kernel.slot_changed(slot, changed, nullptr, field_offset);
    } catch (...) {
        kernel.native_exception_ = std::current_exception();
        frame->status = 1U;
    }
}

void Interpreter::Impl::StaticKernel::write_slot_word(
    const std::uint32_t slot_index, const Word value, const std::uint32_t offset,
    const std::uint32_t width, const std::uint32_t member)
{
    auto& slot = slots_[slot_index];
    if (!slot.single_writer) {
        slot.last_writer = member_process_[member];
    }
    if (slot.words != 1U) {
        auto* a = arena_.data() + slot.offset;
        auto* b = a + slot.words;
        const auto old = read_field(slot_index, offset, width);
        const auto m = kernel_word::mask(width);
        const Word next { value.a & m, value.b & m };
        const auto changed = (old.a ^ next.a) | (old.b ^ next.b);
        if (changed == 0U) {
            return;
        }
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        a[word] = (a[word] & ~(m << shift)) | (next.a << shift);
        b[word] = (b[word] & ~(m << shift)) | (next.b << shift);
        if (shift != 0U && shift + width > 64U && word + 1U < slot.words) {
            const auto spill = 64U - shift;
            a[word + 1U] = (a[word + 1U] & ~(m >> spill)) | (next.a >> spill);
            b[word + 1U] = (b[word + 1U] & ~(m >> spill)) | (next.b >> spill);
        }
        if (!slot.silent) {
            slot_changed(slot_index, changed, nullptr, offset);
        }
        return;
    }
    if (slot.silent) {
        auto& a = arena_[slot.offset];
        auto& b = arena_[slot.offset + 1U];
        if (offset == 0U && width == slot.width) {
            a = value.a;
            b = value.b;
        } else {
            const auto next = kernel_word::insert({ a, b }, value, offset, width);
            a = next.a;
            b = next.b;
        }
        return;
    }
    const auto old = slot_word(slot_index);
    const auto next = offset == 0U && width == slot.width
        ? value
        : kernel_word::insert(old, value, offset, width);
    const auto changed = (old.a ^ next.a) | (old.b ^ next.b);
    if (changed == 0U) {
        return;
    }
    arena_[slot.offset] = next.a;
    arena_[slot.offset + 1U] = next.b;
    slot_changed(slot_index, changed, nullptr);
}

void Interpreter::Impl::StaticKernel::write_slot_value(
    const std::uint32_t slot_index, const PackedLogic4& value,
    const std::optional<std::uint32_t> offset, const std::uint32_t member,
    const InstructionIndex instruction)
{
    auto& slot = slots_[slot_index];
    const auto width = static_cast<std::uint32_t>(value.width());
    if (offset) {
        if (*offset > slot.width || width > slot.width - *offset) {
            fail(member, instruction,
                "static kernel write is outside its signal");
        }
    } else if (width != slot.width) {
        fail(member, instruction,
            "static kernel write width does not match its signal");
    }
    if (slot.words == 1U) {
        const auto aval = value.aval_words();
        const auto bval = value.bval_words();
        write_slot_word(slot_index,
            { aval.empty() ? 0U : aval[0], bval.empty() ? 0U : bval[0] },
            offset.value_or(0U), width, member);
        return;
    }
    slot.last_writer = member_process_[member];
    auto next = offset ? insert_value(slot_value(slot_index), value, *offset)
                       : value;
    std::vector<std::uint64_t> before(arena_.begin() + slot.offset,
        arena_.begin() + slot.offset + 2U * slot.words);
    const auto aval = next.aval_words();
    const auto bval = next.bval_words();
    bool changed = false;
    for (std::uint32_t word = 0U; word < slot.words; ++word) {
        const auto a = word < aval.size() ? aval[word] : 0U;
        const auto b = word < bval.size() ? bval[word] : 0U;
        changed = changed || a != arena_[slot.offset + word]
            || b != arena_[slot.offset + slot.words + word];
        arena_[slot.offset + word] = a;
        arena_[slot.offset + slot.words + word] = b;
    }
    if (changed) {
        slot_changed(slot_index, 0U, before.data());
    }
}

void Interpreter::Impl::StaticKernel::store_slot_bits(
    const std::uint32_t slot_index, const PackedLogic4& value,
    const std::uint32_t offset)
{
    const auto& slot = slots_[slot_index];
    const auto width = static_cast<std::uint32_t>(value.width());
    for (std::uint32_t plane = 0U; plane < slot.planes; ++plane) {
        copy_bits(arena_.data() + slot.offset + plane * slot.words, offset,
            value_plane(value, plane), width);
    }
}

void Interpreter::Impl::StaticKernel::write_slot_immediate(
    const std::uint32_t slot_index, PackedLogic4 value,
    const std::optional<std::uint32_t> offset, const std::uint32_t member,
    const InstructionIndex instruction)
{
    auto& slot = slots_[slot_index];
    value = Impl::coerce_value_kind(std::move(value),
        slot.planes == 4U ? ValueKind::logic9 : ValueKind::logic4);
    const auto width = static_cast<std::uint32_t>(value.width());
    if (offset) {
        if (*offset > slot.width || width > slot.width - *offset
            || width == 0U) {
            fail(member, instruction,
                "static kernel write is outside its signal");
        }
    } else if (width != slot.width) {
        fail(member, instruction,
            "static kernel write width does not match its signal");
    }
    slot.last_writer = member_process_[member];
    std::vector<std::uint64_t> before(arena_.begin() + slot.offset,
        arena_.begin() + slot.offset + slot.planes * slot.words);
    store_slot_bits(slot_index, value, offset.value_or(0U));
    if (!std::equal(before.begin(), before.end(),
            arena_.begin() + slot.offset)) {
        slot_changed(slot_index, 0U, before.data());
    }
}

void Interpreter::Impl::StaticKernel::store_slot_word(
    const std::uint32_t slot_index, const Word value, const std::uint32_t offset,
    const std::uint32_t width, const std::uint64_t unknown)
{
    const auto& slot = slots_[slot_index];
    store_planes_word(arena_.data() + slot.offset, slot.words, slot.planes,
        value, offset, width, unknown);
}

void Interpreter::Impl::StaticKernel::store_planes_word(std::uint64_t* const base,
    const std::uint32_t words, const std::uint32_t planes, const Word value,
    const std::uint32_t offset, const std::uint32_t width,
    const std::uint64_t unknown)
{
    const bool narrow = offset + width <= 64U;
    const auto field = kernel_word::mask(width) << offset;
    const auto put = [&](std::uint64_t* plane, const std::uint64_t bits) {
        if (narrow) {
            *plane = (*plane & ~field) | ((bits << offset) & field);
            return;
        }
        const std::uint64_t words[1] = { bits };
        copy_bits(plane, offset, std::span<const std::uint64_t> { words, 1U },
            width);
    };
    if (planes == 4U) {
        const auto codes = kernel_word::logic9_planes(value, width, unknown);
        put(base, codes.p0);
        put(base + words, codes.p1);
        put(base + 2U * words, codes.p2);
        put(base + 3U * words, codes.p3);
        return;
    }
    put(base, value.a);
    put(base + words, value.b);
}

bool Interpreter::Impl::StaticKernel::copy_wire(const Wire& wire)
{
    const auto& from = slots_[wire.source];
    const auto& to = slots_[wire.target];
    bool changed = false;
    for (std::uint32_t plane = 0U; plane < to.planes; ++plane) {
        const auto* source = arena_.data() + from.offset + plane * from.words;
        auto* target = arena_.data() + to.offset + plane * to.words;
        for (std::uint32_t done = 0U; done < wire.width;) {
            // At most to the end of the target word.
            const auto bit = wire.target_offset + done;
            const auto chunk = std::min(64U - bit % 64U, wire.width - done);
            const auto bits = read_plane_bits(
                source, from.words, wire.offset + done, chunk);
            auto& out = target[bit / 64U];
            const auto mask = kernel_word::mask(chunk) << (bit % 64U);
            const auto next = (out & ~mask) | (bits << (bit % 64U));
            changed = changed || next != out;
            out = next;
            done += chunk;
        }
    }
    return changed;
}

void Interpreter::Impl::StaticKernel::commit_round()
{
    // VHDL update phase: apply this round's assignments in program order
    // (the last one to an element wins), then report each signal whose
    // value differs from the start of the round exactly once.
    if (commit_slots_.size() != slots_.size()) {
        commit_slots_.assign(slots_.size(), CommitSlot { });
        for (std::size_t index = 0U; index < slots_.size(); ++index) {
            auto& commit = commit_slots_[index];
            commit.offset = slots_[index].offset;
            commit.words = slots_[index].words;
            commit.planes = static_cast<std::uint8_t>(slots_[index].planes);
            commit.single_writer = slots_[index].single_writer;
        }
    }
    const auto touch = [&](const std::uint32_t slot_index) -> CommitSlot& {
        auto& slot = commit_slots_[slot_index];
        if (!slot.touched) {
            slot.touched = true;
            const auto words = static_cast<std::size_t>(slot.planes) * slot.words;
            const auto at = round_before_.size();
            slot.before = static_cast<std::uint32_t>(at);
            const auto* current = arena_.data() + slot.offset;
            // A few words: a loop, not a library call.
            for (std::size_t word = 0U; word < words; ++word) {
                round_before_.push_back(current[word]);
            }
            touched_.push_back(slot_index);
        }
        return slot;
    };
    const auto count = writes_.count;
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto& item = writes_.data[index];
        if (item.kind == 0U) {
            auto& pending = commit_slots_[item.slot];
            if (!pending.single_writer) {
                slots_[item.slot].last_writer = member_process_[item.member];
            }
            if (!pending.touched && item.offset + item.width <= 64U) {
                // Most assignments rewrite the value a signal already
                // holds; one that leaves an untouched slot as it is needs
                // neither its prior value saved nor a change check. (The
                // planes compared are the ones store_planes_word writes.)
                const auto* current = arena_.data() + pending.offset;
                const auto words = pending.words;
                const auto field = kernel_word::mask(item.width) << item.offset;
                const auto same = [&](const std::uint32_t plane,
                                      const std::uint64_t bits) {
                    return ((current[plane * words] ^ (bits << item.offset)) & field)
                        == 0U;
                };
                if (pending.planes == 2U) {
                    if (same(0U, item.a) && same(1U, item.b)) {
                        continue;
                    }
                } else if (pending.planes == 4U) {
                    const auto codes = kernel_word::logic9_planes(
                        { item.a, item.b }, item.width, item.unknown);
                    if (same(0U, codes.p0) && same(1U, codes.p1)
                        && same(2U, codes.p2) && same(3U, codes.p3)) {
                        continue;
                    }
                }
            }
            const auto& slot = touch(item.slot);
            store_planes_word(arena_.data() + slot.offset, slot.words,
                slot.planes, { item.a, item.b }, item.offset, item.width,
                item.unknown);
            continue;
        }
        auto& entry = pending_generic_[item.member];
        const auto slot_index = slot_of_signal_[entry.target];
        auto& slot = slots_[slot_index];
        auto value = Impl::coerce_value_kind(std::move(entry.value),
            slot.planes == 4U ? ValueKind::logic9 : ValueKind::logic4);
        const auto width = static_cast<std::uint32_t>(value.width());
        if (entry.kind == PendingKind::slice) {
            if (entry.offset > slot.width || width > slot.width - entry.offset
                || width == 0U) {
                fail(entry.member, entry.instruction,
                    "partial update range is outside its target signal");
            }
        } else if (width != slot.width) {
            fail(entry.member, entry.instruction,
                "SimIR signal assignment width mismatch (signal width "
                    + std::to_string(slot.width) + ", value width "
                    + std::to_string(width) + ")");
        }
        touch(slot_index);
        slot.last_writer = member_process_[entry.member];
        store_slot_bits(slot_index, value,
            entry.kind == PendingKind::slice ? entry.offset : 0U);
    }
    writes_.count = 0U;
    pending_generic_.clear();
    // Wired formals follow their sources' new values in this update phase.
    // A wire's target joins the touched list, so wires chain.
    for (std::size_t at = 0U; !wires_.empty() && at < touched_.size(); ++at) {
        const auto [begin, end] = slot_wires_[touched_[at]];
        for (auto index = begin; index < end; ++index) {
            touch(wires_[index].target);
            copy_wire(wires_[index]);
        }
    }
    for (const auto slot_index : touched_) {
        auto& slot = commit_slots_[slot_index];
        slot.touched = false;
        const auto words = static_cast<std::size_t>(slot.planes) * slot.words;
        const auto* before = round_before_.data() + slot.before;
        const auto* current = arena_.data() + slot.offset;
        bool same = true;
        for (std::size_t word = 0U; word < words && same; ++word) {
            same = before[word] == current[word];
        }
        if (!same) {
            if (trace_) {
                std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                          << " commit "
                          << impl_.get_signal_cold(slots_[slot_index].signal).name
                          << " = " << slot_value(slot_index).to_msb_string()
                          << '\n';
            }
            slot_changed(slot_index, 0U, before);
        }
    }
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                  << " end-of-round\n";
    }
    touched_.clear();
    round_before_.clear();
}

void Interpreter::Impl::StaticKernel::write_signal_owned(const SignalId signal,
    PackedLogic4 value, const std::optional<std::uint32_t> offset,
    const std::uint32_t member, const InstructionIndex instruction)
{
    if (const auto slot = slot_of_signal_[signal]; slot != no_slot) {
        write_slot_value(slot, value, offset, member, instruction);
        return;
    }
    const auto& family = families_[family_of_signal_[signal]];
    PackedLogic4 next;
    try {
        if (offset) {
            next = insert_value(assemble(family), value, *offset);
        } else {
            if (value.width() != family.width) {
                fail(member, instruction,
                    "static kernel write width does not match its signal");
            }
            next = std::move(value);
        }
    } catch (const std::invalid_argument& error) {
        fail(member, instruction, error.what());
    }
    for (const auto& leaf : family.leaves) {
        write_slot_value(leaf.slot, extract_value(next, leaf.offset, leaf.width),
            std::nullopt, member, instruction);
    }
}

void Interpreter::Impl::StaticKernel::write_host(const std::uint32_t member,
    const SignalId signal, PackedLogic4 value,
    const std::optional<std::uint32_t> offset, const bool blocking,
    const SignalUpdateDomain domain)
{
    const auto process = members_[member].process;
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                  << " host-write " << impl_.get_signal_cold(signal).name
                  << (blocking ? " blocking " : " staged ")
                  << value.to_msb_string() << '\n';
    }
    if (!blocking && domain == SignalUpdateDomain::systemverilog_active) {
        // An Active event behind this activation: #0 threads (Inactive
        // region) wait for it (IEEE 1800-2023 4.5).
        host_active_staged_ = true;
    }
    if (blocking) {
        if (offset) {
            impl_.commit_driver_slice(process, signal, std::move(value), *offset);
        } else {
            impl_.commit_driver(process, signal, std::move(value));
        }
        // A blocking write is visible to the statements and members that
        // follow it (IEEE 1800-2023 10.4.1), including kernel readers of the
        // host signal.
        if (const auto input = input_of_signal_[signal]; input != no_slot) {
            (void)refresh_input(inputs_[input]);
        }
        return;
    }
    if (const auto input = input_of_signal_[signal];
        input != no_slot
        && (!inputs_[input].readers.empty() || !inputs_[input].edges.empty())) {
        // A process does not wake on its own updates; run again once they
        // commit. (Mirror-only inputs have no readers to wake.)
        yield_requested_ = true;
    }
    if (const auto input = input_of_signal_[signal];
        input != no_slot && inputs_[input].sole_writer
        && domain == SignalUpdateDomain::systemverilog_active) {
        // The host applies this update unchanged; members that run after
        // this write read it (see sole_writer_inputs).
        auto& mirrored = inputs_[input];
        auto next = mirrored.cached;
        if (offset) {
            if (*offset <= next.width() && value.width() <= next.width() - *offset) {
                for (std::uint32_t bit = 0U; bit < value.width(); ++bit) {
                    next.set(*offset + bit, value.get(bit));
                }
                apply_input(mirrored, Impl::coerce_value_kind(
                    impl_.normalize_signal_value(signal, std::move(next)),
                    ValueKind::logic4));
            }
        } else if (value.width() == next.width()) {
            apply_input(mirrored, Impl::coerce_value_kind(
                impl_.normalize_signal_value(signal, value), ValueKind::logic4));
        }
    }
    if (offset) {
        impl_.stage_update_slice(process, signal, std::move(value), *offset,
            domain);
    } else {
        impl_.stage_update(process, signal, std::move(value), domain);
    }
}

Interpreter::Impl::StaticKernel::Word
Interpreter::Impl::StaticKernel::container_element_word(
    const Container& container, const std::size_t ordinal) const
{
    const auto width = static_cast<std::uint32_t>(container.type.element_width);
    switch (container.storage) {
    case StaticKernelContainerStorage::element_signals:
        return slot_word(container.element_slots[ordinal]);
    case StaticKernelContainerStorage::packed_signal: {
        const auto& slot = slots_[container.packed_slot];
        const auto offset = static_cast<std::uint32_t>(
            slot.width - (ordinal + 1U) * width);
        const auto word = offset / 64U;
        const auto shift = offset % 64U;
        const auto* a = arena_.data() + slot.offset;
        const auto* b = a + slot.words;
        auto value_a = a[word] >> shift;
        auto value_b = b[word] >> shift;
        if (shift != 0U && word + 1U < slot.words) {
            value_a |= a[word + 1U] << (64U - shift);
            value_b |= b[word + 1U] << (64U - shift);
        }
        const auto m = kernel_word::mask(width);
        return { value_a & m, value_b & m };
    }
    case StaticKernelContainerStorage::elements:
        break;
    }
    const auto base = container.element_offset
        + 2U * container.element_words * static_cast<std::uint32_t>(ordinal);
    return { arena_[base], arena_[base + container.element_words] };
}

PackedLogic4 Interpreter::Impl::StaticKernel::container_element(
    const Container& container, const std::size_t ordinal) const
{
    const auto width = container.type.element_width;
    switch (container.storage) {
    case StaticKernelContainerStorage::element_signals:
        return slot_value(container.element_slots[ordinal]);
    case StaticKernelContainerStorage::packed_signal:
        if (width <= 64U) {
            const auto value = container_element_word(container, ordinal);
            return PackedLogic4::from_aval_bval(width, value.a, value.b);
        }
        return extract_value(slot_value(container.packed_slot),
            slots_[container.packed_slot].width - (ordinal + 1U) * width, width);
    case StaticKernelContainerStorage::elements:
        break;
    }
    const auto base = container.element_offset
        + 2U * container.element_words * static_cast<std::uint32_t>(ordinal);
    return PackedLogic4::from_word_planes(width,
        std::span<const std::uint64_t> { arena_.data() + base,
            container.element_words },
        std::span<const std::uint64_t> {
            arena_.data() + base + container.element_words,
            container.element_words });
}

void Interpreter::Impl::StaticKernel::place_elements(
    Container& container, const ContainerValue& initial)
{
    container.element_offset = static_cast<std::uint32_t>(arena_.size());
    arena_.resize(arena_.size()
            + 2U * container.element_words * container.count,
        0U);
    for (std::uint32_t index = 0U; index < container.count; ++index) {
        const auto& element = initial.elements[index];
        const auto aval = element.aval_words();
        const auto bval = element.bval_words();
        const auto base = container.element_offset
            + 2U * container.element_words * index;
        for (std::uint32_t word = 0U; word < container.element_words; ++word) {
            arena_[base + word] = word < aval.size() ? aval[word] : 0U;
            arena_[base + container.element_words + word]
                = word < bval.size() ? bval[word] : 0U;
        }
    }
}

void Interpreter::Impl::StaticKernel::write_container_element(
    Container& container, const std::size_t ordinal, const PackedLogic4& value,
    const std::uint32_t member, const InstructionIndex instruction)
{
    const auto width = container.type.element_width;
    switch (container.storage) {
    case StaticKernelContainerStorage::element_signals:
        write_slot_value(container.element_slots[ordinal], value, std::nullopt,
            member, instruction);
        return;
    case StaticKernelContainerStorage::packed_signal:
        write_slot_value(container.packed_slot, value,
            static_cast<std::uint32_t>(
                slots_[container.packed_slot].width - (ordinal + 1U) * width),
            member, instruction);
        return;
    case StaticKernelContainerStorage::elements:
        break;
    }
    const auto base = container.element_offset
        + 2U * container.element_words * static_cast<std::uint32_t>(ordinal);
    const auto aval = value.aval_words();
    const auto bval = value.bval_words();
    for (std::uint32_t word = 0U; word < container.element_words; ++word) {
        arena_[base + word] = word < aval.size() ? aval[word] : 0U;
        arena_[base + container.element_words + word]
            = word < bval.size() ? bval[word] : 0U;
    }
}

void Interpreter::Impl::StaticKernel::copy_container(
    const std::uint32_t destination, const std::uint32_t source,
    const std::uint32_t member, const InstructionIndex instruction)
{
    const auto& to = containers_[destination];
    const auto& from = containers_[source];
    if (to.object != no_container_object || from.object != no_container_object
        || to.storage != StaticKernelContainerStorage::elements
        || from.storage != StaticKernelContainerStorage::elements
        || !(to.type == from.type) || to.count != from.count
        || to.element_words != from.element_words) {
        fail(member, instruction,
            "static kernel container copy needs private arrays of one type");
    }
    if (destination == source) {
        return;
    }
    const auto words = static_cast<std::size_t>(2U) * to.element_words * to.count;
    std::copy_n(arena_.begin() + from.element_offset, words,
        arena_.begin() + to.element_offset);
}

void Interpreter::Impl::StaticKernel::write_container_element_word(
    Container& container, const std::size_t ordinal, const Word value,
    const std::uint32_t member)
{
    const auto width = static_cast<std::uint32_t>(container.type.element_width);
    switch (container.storage) {
    case StaticKernelContainerStorage::element_signals:
        write_slot_word(container.element_slots[ordinal], value, 0U, width,
            member);
        return;
    case StaticKernelContainerStorage::packed_signal:
        write_slot_word(container.packed_slot, value,
            static_cast<std::uint32_t>(
                slots_[container.packed_slot].width - (ordinal + 1U) * width),
            width, member);
        return;
    case StaticKernelContainerStorage::elements:
        break;
    }
    const auto base = container.element_offset
        + 2U * container.element_words * static_cast<std::uint32_t>(ordinal);
    arena_[base] = value.a;
    arena_[base + container.element_words] = value.b;
}

bool Interpreter::Impl::StaticKernel::write_element_narrow(const NbaEntry& entry)
{
    auto& container = containers_[entry.target];
    const auto& type = container.type;
    const auto element_width = static_cast<std::uint32_t>(type.element_width);
    if (element_width == 0U || element_width > 64U
        || (type.element_kind != ContainerElementKind::Packed
            && type.element_kind != ContainerElementKind::Scalar)
        || !kernel_word::known(entry.index)) {
        return false;
    }
    const auto index = kernel_word::sign_extend(
        entry.index.a & kernel_word::mask(entry.index_width), entry.index_width);
    std::size_t ordinal = 0U;
    if (entry.linear_index) {
        if (index < 0 || static_cast<std::uint64_t>(index) >= container.count) {
            return false;
        }
        ordinal = static_cast<std::size_t>(index);
    } else {
        const auto low = std::min(type.index_left, type.index_right);
        const auto high = std::max(type.index_left, type.index_right);
        if (index < low || index > high) {
            return false;
        }
        ordinal = fixed_offset(type, static_cast<std::int32_t>(index));
        if (ordinal >= container.count) {
            return false;
        }
    }
    auto value = entry.value;
    if (entry.kind == NbaKind::element_part) {
        const auto& selection = *entry.part;
        if (selection.width == 0U || entry.width != selection.width
            || (type.two_state && !kernel_word::known(value))) {
            return false;
        }
        const auto write
            = kernel_word::dynamic_part_write(value, entry.base, selection);
        if (!write.valid) {
            return false;
        }
        if (!write.write) {
            return true;
        }
        if (write.write->offset > element_width
            || write.write->width > element_width - write.write->offset) {
            return false;
        }
        value = kernel_word::insert(container_element_word(container, ordinal),
            write.write->value, write.write->offset, write.write->width);
    } else if (entry.width != element_width) {
        return false;
    }
    if (type.two_state && !kernel_word::known(value)) {
        return false;
    }
    write_container_element_word(container, ordinal, value, entry.member);
    return true;
}

void Interpreter::Impl::StaticKernel::write_element_entry(const NbaEntry& entry)
{
    if (write_element_narrow(entry)) {
        return;
    }
    // Error and corner cases take the reference path with exact messages.
    Pending slow;
    slow.kind = entry.kind == NbaKind::element_part ? PendingKind::element_part
                                                    : PendingKind::element;
    slow.member = entry.member;
    slow.instruction = entry.instruction;
    slow.target = entry.target;
    slow.linear_index = entry.linear_index;
    slow.value = PackedLogic4::from_aval_bval(entry.width, entry.value.a,
        entry.value.b);
    slow.index = PackedLogic4::from_aval_bval(entry.index_width, entry.index.a,
        entry.index.b);
    if (entry.kind == NbaKind::element_part) {
        slow.base = PackedLogic4::from_aval_bval(32U, entry.base.a, entry.base.b);
        slow.part = *entry.part;
    }
    write_element(slow);
}

void Interpreter::Impl::StaticKernel::push_write(const StaticKernelWrite& write)
{
    if (writes_.count == writes_.capacity) {
        write_storage_.resize(std::max<std::size_t>(256U,
            2U * write_storage_.size()));
        writes_.data = write_storage_.data();
        writes_.capacity = static_cast<std::uint32_t>(write_storage_.size());
    }
    writes_.data[writes_.count++] = write;
}

void Interpreter::Impl::StaticKernel::push_generic_pending(Pending pending)
{
    StaticKernelWrite write;
    write.kind = 1U;
    write.member = static_cast<std::uint32_t>(pending_generic_.size());
    pending_generic_.push_back(std::move(pending));
    push_write(write);
}

void Interpreter::Impl::StaticKernel::push_element_write(const NbaEntry& entry)
{
    StaticKernelWrite write;
    write.kind = 2U;
    write.member = static_cast<std::uint32_t>(nba_.size());
    nba_.push_back(entry);
    push_write(write);
}

void Interpreter::Impl::StaticKernel::write_element(const Pending& pending)
{
    auto& container = containers_[pending.target];
    const auto& type = container.type;
    const auto member = pending.member;
    const auto instruction = pending.instruction;
    const auto process = members_[member].process;
    auto value = pending.value;
    if (pending.kind == PendingKind::element_part) {
        const auto& selection = pending.part;
        if (selection.width == 0U || value.width() != selection.width
            || pending.base.width() != 32U || type.element_width == 0U
            || (type.two_state && has_unknown(value))) {
            fail(member, instruction,
                "dynamic part-select container element write type mismatch");
        }
        const auto selected = pending.linear_index
            ? known_index(process, instruction, pending.index, true,
                  "multidimensional linear index")
            : fixed_offset(process, instruction, type, pending.index);
        if (selected >= container.count) {
            fail(member, instruction, "container index is out of range");
        }
        try {
            value = dynamic_part_insert_value(
                container_element(container, selected), value, pending.base,
                selection);
        } catch (const std::invalid_argument&) {
            fail(member, instruction,
                "invalid dynamic part-select container element write");
        }
    }
    if ((type.element_kind != ContainerElementKind::Packed
            && type.element_kind != ContainerElementKind::Scalar)
        || value.width() != type.element_width || value.is_logic9()
        || (type.two_state && has_unknown(value))) {
        fail(member, instruction, "container element write type mismatch");
    }
    const auto selected = pending.linear_index
        ? known_index(process, instruction, pending.index, true,
              "multidimensional linear index")
        : fixed_offset(process, instruction, type, pending.index);
    if (selected >= container.count) {
        fail(member, instruction, "container index is out of range");
    }
    write_container_element(container, selected, value, member, instruction);
}

void Interpreter::Impl::StaticKernel::enqueue(const std::uint32_t level,
    const std::uint32_t entry)
{
    if (level >= buckets_.size()) {
        buckets_.resize(static_cast<std::size_t>(level) + 1U);
    }
    buckets_[level].push_back(entry);
    if (queued_count_++ == 0U || level < scan_level_) {
        scan_level_ = level;
    }
}

void Interpreter::Impl::StaticKernel::schedule_target(const ScheduleTarget& target)
{
    profile_schedules_ += profile_ ? 1U : 0U;
    if ((target.id & partition_tag) != 0U) {
        if (committing_round_) {
            deferred_targets_.push_back(target);
            return;
        }
        const auto partition = target.id & ~partition_tag;
        if (partition == running_partition_
            && target.min_position >= running_position_) {
            // Later readers run in this pass; the running member itself does
            // not wake on its own writes (it is not waiting while it runs).
            return;
        }
        if (partition_queued_[partition] == 0U) {
            partition_queued_[partition] = 1U;
            enqueue(partition_level_[partition], target.id);
        }
        return;
    }
    schedule(target.id);
}

void Interpreter::Impl::StaticKernel::build_schedule_targets()
{
    for (auto& slot : slots_) {
        std::vector<Reader> ranged;
        std::vector<ScheduleTarget> targets;
        for (const auto& reader : slot.readers) {
            if (reader.width != 0U) {
                ranged.push_back(reader);
                continue;
            }
            const auto& member = members_[reader.member];
            ScheduleTarget target { reader.member, 0U };
            if (member.kind == StaticKernelMemberKind::combinational
                && member.partition != no_slot) {
                target = { member.partition | partition_tag, member.position };
            }
            const auto found = std::ranges::find_if(targets,
                [&](const ScheduleTarget& existing) {
                    return existing.id == target.id;
                });
            if (found == targets.end()) {
                targets.push_back(target);
            } else {
                found->min_position
                    = std::min(found->min_position, target.min_position);
            }
        }
        // Group ranged readers by the target that schedule() would queue.
        const auto group_of = [&](const Reader& reader) {
            const auto& member = members_[reader.member];
            return member.kind == StaticKernelMemberKind::combinational
                    && member.partition != no_slot
                ? member.partition | partition_tag
                : reader.member;
        };
        std::ranges::stable_sort(ranged, {}, group_of);
        slot.ranged.clear();
        for (std::uint32_t index = 0U; index < ranged.size(); ++index) {
            const auto id = group_of(ranged[index]);
            if (slot.ranged.empty() || slot.ranged.back().id != id) {
                slot.ranged.push_back({ id, index, index });
            }
            slot.ranged.back().end = index + 1U;
        }
        slot.readers = std::move(ranged);
        slot.targets = std::move(targets);
    }
    member_schedule_.resize(members_.size());
    for (std::size_t index = 0U; index < members_.size(); ++index) {
        const auto& member = members_[index];
        auto& state = member_schedule_[index];
        state.partition = member.partition;
        state.position = member.position;
        state.level = member.level;
        state.kind = member.kind;
        state.vhdl = member.vhdl;
    }
    partition_queued_.assign(partitions_.size(), 0U);
    partition_level_.resize(partitions_.size());
    for (std::size_t index = 0U; index < partitions_.size(); ++index) {
        partition_level_[index] = partitions_[index].level;
    }
    slot_notify_.resize(slots_.size());
    notify_targets_.clear();
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        auto& notify = slot_notify_[index];
        notify.begin = static_cast<std::uint32_t>(notify_targets_.size());
        notify_targets_.insert(
            notify_targets_.end(), slot.targets.begin(), slot.targets.end());
        notify.end = static_cast<std::uint32_t>(notify_targets_.size());
        notify.general = !slot.ranged.empty() || slot.output
            || !slot.edges.empty() || !slot.single_writer
            || (index < slot_waitable_.size() && slot_waitable_[index] != 0U);
    }
}

bool Interpreter::Impl::StaticKernel::target_pending(const std::uint32_t id) const
{
    if ((id & partition_tag) != 0U) {
        return partition_queued_[id & ~partition_tag] != 0U;
    }
    const auto& member = member_schedule_[id];
    switch (member.kind) {
    case StaticKernelMemberKind::combinational:
        return member.queued;
    case StaticKernelMemberKind::sequential:
        return member.triggered;
    default:
        return false;
    }
}

void Interpreter::Impl::StaticKernel::schedule(const std::uint32_t member)
{
    profile_schedules_ += profile_ ? 1U : 0U;
    auto& state = member_schedule_[member];
    if (mixed_) {
        // A VHDL member runs in the next round; a SystemVerilog member woken
        // by a VHDL commit runs in the next delta.
        if (state.vhdl) {
            if (!state.round) {
                state.round = true;
                next_round_.push_back(member);
            }
            return;
        }
        if (committing_round_) {
            if (!state.deferred) {
                state.deferred = true;
                deferred_targets_.push_back({ member, 0U });
            }
            return;
        }
    }
    if (state.kind == StaticKernelMemberKind::combinational) {
        if (state.partition != no_slot) {
            if (state.partition == running_partition_
                && state.position >= running_position_) {
                // Runs later in the current partition pass.
                return;
            }
            if (partition_queued_[state.partition] == 0U) {
                partition_queued_[state.partition] = 1U;
                enqueue(partition_level_[state.partition],
                    state.partition | partition_tag);
            }
            return;
        }
        if (!state.queued && member != running_member_) {
            state.queued = true;
            enqueue(state.level, member);
        }
    } else if (state.kind == StaticKernelMemberKind::sequential) {
        if (!state.triggered) {
            state.triggered = true;
            triggered_.push_back(member);
        }
    }
}

void Interpreter::Impl::StaticKernel::run(const std::uint32_t member_index)
{
    if (const auto& fast = fast_runs_[member_index]; fast.entry != nullptr) {
        StaticKernelNativeFrame frame;
        frame.arena = arena_.data();
        frame.bindings = fast.bindings;
        frame.kernel = this;
        frame.program = fast.program;
        frame.instance = fast.body;
        frame.writes = &writes_;
        frame.containers = container_info_.data();
        frame.member = member_index;
        frame.position = 0U;
        frame.status = 0U;
        frame.reserved = 0U;
        two_state_running_ = fast.two_state;
        const auto status = fast.entry(&frame, fast.registers);
        two_state_running_ = false;
        if (fast.two_state) {
            ++fast_runs_[member_index].two_state_runs;
        }
        if (status == 0U && frame.status == 0U) {
            return;
        }
        auto& member = members_[member_index];
        finish_native(member.native, *member.compiled, member_index, frame,
            status, false);
        refresh_fast_run(member_index);
        return;
    }
    run_member(member_index);
    refresh_fast_run(member_index);
}

bool Interpreter::Impl::StaticKernel::first_run_is_activation(const Member& member)
{
    if (member.prologue_end >= member.operations.size()) {
        return false;
    }
    const auto* jump = operation_get_if<Jump>(&member.operations[member.prologue_end]);
    return member.prologue_end == 0U || (jump != nullptr && jump->target == 0U);
}

bool Interpreter::Impl::StaticKernel::native_prologue(const Member& member)
{
    return member.vhdl && member.body_begin != 0U && member.prologue_end != 0U
        && !first_run_is_activation(member);
}

void Interpreter::Impl::StaticKernel::run_member(const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                  << " d=" << impl_.scheduler.delta() << " run "
                  << profile_names_[member_index]
                  << (member.fresh ? " fresh" : "")
                  << (member.compiled ? " compiled" : "")
                  << (member.generic_mode ? " generic" : "") << '\n';
    }
    if (profile_) {
        ++profile_runs_[static_cast<int>(member.kind)];
        if (profile_member_runs_.empty()) {
            profile_member_runs_.assign(members_.size(), 0U);
        }
        ++profile_member_runs_[member_index];
    }
    if (member.vhdl && member.compiled && member.fresh && !member.generic_mode
        && !verify_
        && (first_run_is_activation(member)
            || has_prologue_entry(*member.compiled, member.vhdl))) {
        // Either the prologue is the body itself (the activation's entry
        // jumps to operation 0), so the first run is an ordinary activation,
        // or the code enters at operation 0 and runs the prologue into the
        // activation (IEEE 1076-2019 14.7.5.2: each process runs until it
        // suspends).
        member.fresh = false;
        profile_compiled_runs_ += profile_ ? 1U : 0U;
        run_compiled(member_index, first_run_is_activation(member)
                ? 0U : static_kernel_prologue_entry);
        return;
    }
    if (member.vhdl && member.compiled && member.fresh && !member.generic_mode
        && !verify_) {
        // The first run is the process prologue followed by an ordinary
        // activation: the prologue runs on the reference evaluator, the body
        // from its start (prologue_end) in compiled code.
        member.fresh = false;
        if (const auto stopped = run_operation_tier(member_index, 0U, false)) {
            // The whole first run in operation-tier code.
            if (*stopped != member.body_end) {
                const InterpretingScope scope { *this, InterpretedCause::prologue };
                run_generic_from(member_index, *stopped);
            }
            sync_compiled_registers(member_index);
            return;
        }
        const auto pc = [&] {
            const InterpretingScope scope { *this, InterpretedCause::prologue };
            return run_generic_from(member_index, 0U, member.prologue_end);
        }();
        sync_compiled_registers(member_index);
        if (pc == member.body_end) {
            return;
        }
        if (member.generic_mode) {
            const InterpretingScope scope { *this,
                InterpretedCause::generic_mode };
            run_generic_from(member_index, pc);
            sync_compiled_registers(member_index);
            return;
        }
        profile_compiled_runs_ += profile_ ? 1U : 0U;
        run_compiled(member_index);
        return;
    }
    if (member.vhdl && member.compiled && (member.fresh || member.generic_mode)) {
        if (profile_) {
            ++profile_generic_runs_;
            profile_member_deopts_.resize(members_.size(), 0U);
            ++profile_member_deopts_[member_index];
        }
        const InterpretingScope scope { *this, member.fresh
                ? InterpretedCause::prologue : InterpretedCause::generic_mode };
        run_generic(member_index);
        sync_compiled_registers(member_index);
        return;
    }
    if (member.compiled) {
        profile_compiled_runs_ += profile_ ? 1U : 0U;
        if (verify_ && member.vhdl) {
            verify_run(member_index);
            return;
        }
        run_compiled(member_index);
    } else {
        const InterpretingScope scope { *this, InterpretedCause::uncompiled };
        run_generic(member_index);
    }
}

void Interpreter::Impl::StaticKernel::verify_run(const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    auto& body = *member.compiled;
    const auto registers = body.registers;
    const auto wide = body.wide_registers;
    const auto stack = member.call_stack;
    const auto nba_size = writes_.count;
    const auto generic_size = pending_generic_.size();
    const auto arena = arena_;
    // Writes appended by a run, as (slot, offset, Logic9-exact value).
    const auto writes = [&] {
        std::vector<std::tuple<std::uint32_t, std::uint32_t, std::string>> out;
        for (std::size_t index = nba_size; index < writes_.count; ++index) {
            const auto& entry = writes_.data[index];
            if (entry.kind == 0U) {
                out.emplace_back(entry.slot, entry.offset,
                    uword_value({ entry.a, entry.b }, entry.unknown, entry.width,
                        ValueKind::logic9)
                        .to_msb_string());
            } else {
                const auto& pending = pending_generic_[entry.member];
                out.emplace_back(slot_of_signal_[pending.target], pending.offset,
                    pending.value.promoted_to_logic9().to_msb_string());
            }
        }
        return out;
    };
    run_compiled(member_index);
    const auto compiled_writes = writes();
    std::vector<std::string> compiled_registers;
    for (std::size_t reg = 0U; reg < body.registers.size(); ++reg) {
        const auto width = body.register_widths[reg];
        const auto unknown = body.shadow_base != 0U && reg < body.tracked.size()
                && body.tracked[reg] != 0U
            ? body.registers[body.shadow_base + reg].a : 0U;
        compiled_registers.push_back(width == 0U ? std::string { }
                : width > 64U ? (reg < body.wide_registers.size()
                                       ? Impl::coerce_value_kind(
                                             body.wide_registers[reg],
                                             ValueKind::logic9)
                                             .to_msb_string()
                                       : std::string { })
                              : uword_value(body.registers[reg], unknown, width,
                                    ValueKind::logic9)
                                    .to_msb_string());
    }
    if (member.generic_mode) {
        return;
    }
    // Replay from the same state on the reference evaluator.
    writes_.count = static_cast<std::uint32_t>(nba_size);
    pending_generic_.resize(generic_size);
    arena_ = arena;
    member.call_stack = stack;
    body.registers = registers;
    body.wide_registers = wide;
    for (std::size_t reg = 0U; reg < member.registers.size(); ++reg) {
        const auto width = body.register_widths[reg];
        if (width == 0U) {
            continue;
        }
        if (width > 64U) {
            if (reg < body.wide_registers.size()) {
                member.registers[reg] = body.wide_registers[reg];
            }
            continue;
        }
        const auto kind = reg < member.register_kinds.size()
            ? member.register_kinds[reg] : ValueKind::logic4;
        member.registers[reg] = Impl::coerce_value_kind(
            PackedLogic4::from_aval_bval(width, body.registers[reg].a,
                body.registers[reg].b),
            kind);
    }
    {
        const InterpretingScope scope { *this, InterpretedCause::verify };
        run_generic_from(member_index, member.body_begin);
    }
    const auto reference_writes = writes();
    std::string mismatch;
    if (compiled_writes != reference_writes) {
        mismatch = "writes";
        for (std::size_t index = 0U;
             index < std::max(compiled_writes.size(), reference_writes.size());
             ++index) {
            const auto describe = [&](const auto& list) {
                if (index >= list.size()) {
                    return std::string { "<none>" };
                }
                const auto& [slot, offset, value] = list[index];
                return impl_.get_signal_cold(slots_[slot].signal).name + "["
                    + std::to_string(offset) + "]=" + value;
            };
            if (index >= compiled_writes.size() || index >= reference_writes.size()
                || compiled_writes[index] != reference_writes[index]) {
                mismatch += " #" + std::to_string(index) + " compiled "
                    + describe(compiled_writes) + " reference "
                    + describe(reference_writes);
                break;
            }
        }
    }
    for (std::size_t reg = 0U; reg < member.registers.size() && mismatch.empty();
         ++reg) {
        const auto width = body.register_widths[reg];
        const bool live = body.live_at_entry.empty()
            || (reg / 64U < body.live_at_entry.size()
                && (body.live_at_entry[reg / 64U] >> (reg % 64U) & 1U) != 0U);
        if (width == 0U || compiled_registers[reg].empty() || !live) {
            continue;
        }
        const auto reference = Impl::coerce_value_kind(member.registers[reg],
            ValueKind::logic9).to_msb_string();
        if (compiled_registers[reg] != reference) {
            mismatch = "register " + std::to_string(reg) + " compiled "
                + compiled_registers[reg] + " reference " + reference;
        }
    }
    if (!mismatch.empty() && verify_mismatches_++ < 20U) {
        std::cerr << "fsim-kernel-verify: t=" << impl_.scheduler.now() << ' '
                  << impl_.processes.program_view(member.process).name() << ' '
                  << mismatch << '\n';
        if (verify_mismatches_ == 1U) {
            for (const auto* list : { &compiled_writes, &reference_writes }) {
                std::cerr << (list == &compiled_writes ? "  compiled:" : "  reference:");
                for (const auto& [slot, offset, value] : *list) {
                    std::cerr << ' ' << impl_.get_signal_cold(slots_[slot].signal).name
                              << '[' << offset << "]=" << value;
                }
                std::cerr << '\n';
            }
            for (std::size_t reg = 0U; reg < registers.size(); ++reg) {
                if (body.register_widths[reg] != 0U && body.register_widths[reg] <= 64U) {
                    std::cerr << "  start r" << reg << '='
                              << PackedLogic4::from_aval_bval(body.register_widths[reg],
                                     registers[reg].a, registers[reg].b).to_msb_string()
                              << '\n';
                }
            }
        }
    }
    sync_compiled_registers(member_index);
}

void Interpreter::Impl::StaticKernel::sync_compiled_registers(
    const std::uint32_t member_index)
{
    auto& member = members_[member_index];
    auto& body = *member.compiled;
    for (std::size_t reg = 0U; reg < member.registers.size(); ++reg) {
        const auto& value = member.registers[reg];
        const auto width = body.register_widths[reg];
        if (value.width() == 0U || width == 0U) {
            continue;
        }
        if (width > 64U) {
            if (reg < body.wide_registers.size()) {
                body.wide_registers[reg] = value;
            }
            continue;
        }
        const auto uword = value.width() == width ? exact_uword(value)
                                                  : std::nullopt;
        const bool tracked = body.shadow_base != 0U && reg < body.tracked.size()
            && body.tracked[reg] != 0U;
        if (uword && (uword->unknown == 0U || tracked)) {
            body.registers[reg] = uword->value;
            if (tracked) {
                body.registers[body.shadow_base + reg] = { uword->unknown, 0U };
            }
            continue;
        }
        // Only registers read before written in the next activation need
        // exact values.
        const bool live = body.live_at_entry.empty()
            || (reg / 64U < body.live_at_entry.size()
                && (body.live_at_entry[reg / 64U] >> (reg % 64U) & 1U) != 0U);
        if (live) {
            member.generic_mode = true;
            return;
        }
    }
    member.generic_mode = false;
    if (member.native.two_state) {
        member.registers_known = registers_known(body);
    }
}

void Interpreter::Impl::StaticKernel::run_generic(const std::uint32_t member_index)
{
    const auto& member = members_[member_index];
    std::uint32_t pc = member.body_begin;
    std::size_t steps = 0U;
    if (member.vhdl) {
        // The first run starts at operation 0; later ones resume after the
        // wait.
        if (std::exchange(members_[member_index].fresh, false)) {
            pc = 0U;
        }
        run_generic_from(member_index, pc);
        return;
    }
    if (const auto stopped = run_operation_tier(member_index, pc, false)) {
        pc = *stopped;
    }
    while (pc >= member.body_begin && pc < member.body_end) {
        if (++steps > run_step_limit) {
            fail(member_index, pc, "static kernel member did not terminate");
        }
        pc = step_generic(member_index, pc);
    }
}

std::uint32_t Interpreter::Impl::StaticKernel::run_generic_from(
    const std::uint32_t member_index, std::uint32_t pc, const std::uint32_t until)
{
    // Subprogram bodies may lie anywhere; the run ends at the wait (or the
    // halt of a process without sensitivity), or at `until`.
    const auto& member = members_[member_index];
    const auto size = member.operations.size();
    if (until == no_slot) {
        // Native code from `pc`; the reference loop below only finishes a
        // run the operation tier could not (no code, an unexpected stop).
        if (const auto stopped = run_operation_tier(member_index, pc, false)) {
            pc = *stopped;
        }
    }
    std::size_t steps = 0U;
    while (pc != member.body_end && pc != until) {
        if (++steps > run_step_limit) {
            fail(member_index, pc, "static kernel member did not terminate");
        }
        if (pc >= size) {
            fail(member_index, pc,
                "static kernel member left its operation stream");
        }
        pc = step_generic(member_index, pc);
    }
    return pc;
}

std::uint32_t Interpreter::Impl::StaticKernel::step_generic(
    const std::uint32_t member_index, const std::uint32_t pc)
{
    profile_operations_ += profile_ ? 1U : 0U;
    ++interpreted_operations_;
    ++interpreted_by_cause_[static_cast<std::size_t>(interpreting_)];
    return visit_operation([&](const auto& op) {
        return step_operation(member_index, pc, op);
    }, members_[member_index].operations[pc]);
}

template <typename Op>
std::uint32_t Interpreter::Impl::StaticKernel::step_operation_at(
    const std::uint32_t member_index, const std::uint32_t pc)
{
    return step_operation(member_index, pc,
        operation_get<Op>(members_[member_index].operations[pc]));
}

const std::array<Interpreter::Impl::StaticKernel::OperationStep,
    operation_kind_count>&
Interpreter::Impl::StaticKernel::operation_steps()
{
    static constexpr auto steps = []<std::size_t... K>(
                                      std::index_sequence<K...>) {
        return std::array<OperationStep, operation_kind_count> {
            &StaticKernel::step_operation_at<OperationKindAt<K>>...
        };
    }(std::make_index_sequence<operation_kind_count>());
    return steps;
}

template <typename Op>
std::uint32_t Interpreter::Impl::StaticKernel::step_operation(
    const std::uint32_t member_index, const std::uint32_t pc, const Op& op)
{
    auto& member = members_[member_index];
    auto& registers = member.registers;
    const auto reg = [&](const RegisterId id,
                         const InstructionIndex at) -> PackedLogic4& {
        if (id >= registers.size()) {
            fail(member_index, at, "invalid register ID");
        }
        return registers[id];
    };
    const auto kind_of = [&](const RegisterId id) {
        return id < member.register_kinds.size() ? member.register_kinds[id]
                                                 : ValueKind::logic4;
    };
    {
        const auto& operation = member.operations[pc];
        (void)operation;
        std::uint32_t next = pc + 1U;
        const auto signal_write = [&](const SignalId signal, PackedLogic4 value,
                                      const std::optional<std::uint32_t> offset,
                                      const bool blocking,
                                      const SignalUpdateDomain domain) {
            if (!owned(signal)) {
                write_host(member_index, signal, std::move(value), offset,
                    blocking, domain);
                return;
            }
            if (member.vhdl) {
                if (slot_of_signal_[signal] == no_slot) {
                    fail(member_index, pc,
                        "static kernel VHDL write targets an aggregate proxy");
                }
                if (blocking) {
                    write_slot_immediate(slot_of_signal_[signal],
                        std::move(value), offset, member_index, pc);
                    return;
                }
                // Applied in the round's update phase (commit_round).
                Pending pending;
                pending.kind = offset ? PendingKind::slice : PendingKind::whole;
                pending.member = member_index;
                pending.instruction = pc;
                pending.target = signal;
                pending.offset = offset.value_or(0U);
                pending.value = std::move(value);
                push_generic_pending(std::move(pending));
                return;
            }
            if (!blocking && domain == SignalUpdateDomain::systemverilog_nba) {
                Pending pending;
                pending.kind = offset ? PendingKind::slice : PendingKind::whole;
                pending.member = member_index;
                pending.instruction = pc;
                pending.target = signal;
                pending.offset = offset.value_or(0U);
                pending.value = std::move(value);
                push_generic_pending(std::move(pending));
                return;
            }
            write_signal_owned(signal, std::move(value), offset, member_index,
                pc);
        };
        const auto dynamic_offset = [&](const DynamicIndex& selection) {
            try {
                return dynamic_index_offset(
                    reg(selection.index, pc), selection);
            } catch (const std::invalid_argument& error) {
                fail(member_index, pc, error.what());
            }
        };
        // A SystemVerilog write through an unknown or out-of-range index
        // does nothing (IEEE 1800-2017 11.5.1); a VHDL index is checked.
        const auto written_offset = [&](const DynamicIndex& selection)
            -> std::optional<std::uint32_t> {
            try {
                return dynamic_index_offset(
                    reg(selection.index, pc), selection);
            } catch (const std::invalid_argument& error) {
                if (selection.strict) {
                    fail(member_index, pc, error.what());
                }
                return std::nullopt;
            }
        };
        const auto container_at = [&](const ContainerRegisterId id)
            -> const Container& {
            if (id >= member.container_registers.size()
                || member.container_registers[id] == no_container) {
                fail(member_index, pc,
                    "static kernel container register is unbound");
            }
            return containers_[member.container_registers[id]];
        };
        [&] {
            using T = Op;
            if constexpr (std::is_same_v<T, DebugPoint>) {
            } else if constexpr (std::is_same_v<T, LoadConstant>) {
                reg(op.destination, pc) = Impl::coerce_value_kind(
                    op.value, kind_of(op.destination));
            } else if constexpr (std::is_same_v<T, CopyRegister>) {
                reg(op.destination, pc) = Impl::coerce_value_kind(
                    reg(op.source, pc), kind_of(op.destination));
            } else if constexpr (std::is_same_v<T, ConvertToTwoState>) {
                const auto& source = reg(op.source, pc);
                auto converted = PackedLogic4(source.width(), Logic4::zero);
                for (std::size_t bit = 0; bit < source.width(); ++bit) {
                    if (to_logic4(source.get_logic9(bit)) == Logic4::one) {
                        converted.set(bit, Logic4::one);
                    }
                }
                reg(op.destination, pc) = std::move(converted);
            } else if constexpr (std::is_same_v<T, ReadSignal>) {
                reg(op.destination, pc) = Impl::coerce_value_kind(
                    read_signal(op.signal), kind_of(op.destination));
            } else if constexpr (std::is_same_v<T, UnaryNot>) {
                reg(op.destination, pc) = unary_not(reg(op.source, pc));
            } else if constexpr (std::is_same_v<T, LogicalNot>) {
                reg(op.destination, pc) = logical_not(reg(op.source, pc));
            } else if constexpr (std::is_same_v<T, LogicalBinary>) {
                reg(op.destination, pc) = logical_binary(
                    op.operation, reg(op.lhs, pc), reg(op.rhs, pc));
            } else if constexpr (std::is_same_v<T, Reduction>) {
                reg(op.destination, pc)
                    = reduce_value(op.operation, reg(op.source, pc));
            } else if constexpr (std::is_same_v<T, Shift>) {
                reg(op.destination, pc) = shift_value(op.operation,
                    reg(op.value, pc), reg(op.amount, pc), op.signed_amount);
            } else if constexpr (std::is_same_v<T, Extract>) {
                try {
                    reg(op.destination, pc)
                        = extract_value(reg(op.source, pc), op.offset, op.width);
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, DynamicExtract>) {
                const auto& source = reg(op.source, pc);
                try {
                    reg(op.destination, pc) = extract_value(source,
                        dynamic_index_offset(
                            reg(op.selection.index, pc), op.selection),
                        1);
                } catch (const std::invalid_argument& error) {
                    if (op.selection.strict) {
                        fail(member_index, pc, error.what());
                    }
                    auto invalid = PackedLogic4(1, Logic4::x);
                    if (source.is_logic9()) {
                        invalid = invalid.promoted_to_logic9();
                    }
                    reg(op.destination, pc) = std::move(invalid);
                }
            } else if constexpr (std::is_same_v<T, DynamicPartSelect>) {
                // Word-level fast path for a known base selecting an in-range
                // run whose source offsets increase with the result bit; the
                // reference function handles every other case.
                const auto& source = reg(op.source, pc);
                const auto& base = reg(op.base, pc);
                if (!source.is_logic9() && base.width() == 32U
                    && op.width != 0U) {
                    if (const auto known = base.known_signed_value()) {
                        const auto signed_base = static_cast<std::int64_t>(
                            static_cast<std::int32_t>(*known));
                        const auto edge = static_cast<std::int64_t>(op.width - 1U);
                        const auto selected_right = op.increasing
                            ? signed_base + (op.source_descending ? 0 : edge)
                            : signed_base - (op.source_descending ? edge : 0);
                        const auto selected_last = selected_right
                            + (op.source_descending ? edge : -edge);
                        const auto low = std::min(selected_right, selected_last);
                        const auto high = std::max(selected_right, selected_last);
                        const bool in_range = low >= std::min(op.left, op.right)
                            && high <= std::max(op.left, op.right);
                        const bool increasing = op.source_descending
                            ? low >= op.right : high <= op.right;
                        if (in_range && increasing) {
                            const auto first = selected_right >= op.right
                                ? selected_right - op.right
                                : op.right - selected_right;
                            const auto offset = static_cast<std::uint64_t>(first)
                                + op.base_offset;
                            if (offset + op.width <= source.width()) {
                                reg(op.destination, pc) = extract_value(source,
                                    static_cast<std::size_t>(offset), op.width);
                                return;
                            }
                        }
                    }
                }
                try {
                    reg(op.destination, pc) = dynamic_part_select_value(
                        reg(op.source, pc), reg(op.base, pc), op.left, op.right,
                        op.base_offset, op.width, op.increasing,
                        op.source_descending, op.two_state);
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, Insert>) {
                try {
                    // An element write to a register updates it in place.
                    if (op.destination == op.target) {
                        reg(op.target, pc).insert_bits(
                            reg(op.source, pc), op.offset);
                    } else {
                        reg(op.destination, pc) = insert_value(
                            reg(op.target, pc), reg(op.source, pc),
                            op.offset);
                    }
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, DynamicInsert>) {
                try {
                    const auto offset = dynamic_index_offset(
                        reg(op.selection.index, pc), op.selection);
                    if (op.destination == op.target) {
                        reg(op.target, pc).insert_bits(
                            reg(op.source, pc), offset);
                    } else {
                        reg(op.destination, pc) = insert_value(
                            reg(op.target, pc), reg(op.source, pc), offset);
                    }
                } catch (const std::invalid_argument& error) {
                    if (op.selection.strict) {
                        fail(member_index, pc, error.what());
                    }
                    reg(op.destination, pc) = reg(op.target, pc);
                }
            } else if constexpr (std::is_same_v<T, DynamicPartInsert>) {
                try {
                    if (op.destination == op.target) {
                        const auto write = dynamic_part_write_value(
                            reg(op.source, pc), reg(op.selection.base, pc),
                            op.selection);
                        if (write) {
                            reg(op.target, pc).insert_bits(
                                write->value, write->offset);
                        }
                    } else {
                        reg(op.destination, pc) = dynamic_part_insert_value(
                            reg(op.target, pc), reg(op.source, pc),
                            reg(op.selection.base, pc), op.selection);
                    }
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, Concatenate>) {
                std::vector<PackedLogic4> operands;
                operands.reserve(op.operands.size());
                for (const auto operand : op.operands) {
                    operands.push_back(reg(operand, pc));
                }
                try {
                    reg(op.destination, pc)
                        = concatenate_values(operands, op.width);
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, Binary>) {
                try {
                    reg(op.destination, pc) = binary_value(
                        op.operation, reg(op.lhs, pc), reg(op.rhs, pc));
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
                try {
                    reg(op.destination, pc) = conditional_value(
                        reg(op.condition, pc), reg(op.when_true, pc),
                        reg(op.when_false, pc));
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, Jump>) {
                next = op.target;
            } else if constexpr (std::is_same_v<T, Branch>) {
                const auto& condition = reg(op.condition, pc);
                if (condition.width() != 1U) {
                    fail(member_index, pc, "branch condition must be scalar");
                }
                const auto value = condition.get(0);
                if (value == Logic4::one) {
                    next = op.when_true;
                } else if (value == Logic4::zero
                    || op.unknown_policy == UnknownBranchPolicy::when_false) {
                    next = op.when_false;
                } else {
                    fail(member_index, pc,
                        "branch condition is unknown or high impedance");
                }
            } else if constexpr (std::is_same_v<T, WriteBlocking>) {
                signal_write(op.signal, reg(op.source, pc), std::nullopt, true,
                    SignalUpdateDomain::systemverilog_active);
            } else if constexpr (std::is_same_v<T, WriteBlockingSlice>) {
                signal_write(op.signal, reg(op.source, pc), op.offset, true,
                    SignalUpdateDomain::systemverilog_active);
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicSlice>) {
                if (const auto offset = written_offset(op.selection)) {
                    auto value = reg(op.source, pc);
                    signal_write(op.signal, std::move(value), *offset, true,
                        SignalUpdateDomain::systemverilog_active);
                }
            } else if constexpr (std::is_same_v<T, WriteBlockingDynamicPartSlice>) {
                std::optional<DynamicPartWrite> write;
                try {
                    write = dynamic_part_write_value(reg(op.source, pc),
                        reg(op.selection.base, pc), op.selection);
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
                if (write) {
                    signal_write(op.signal, std::move(write->value),
                        static_cast<std::uint32_t>(write->offset), true,
                        SignalUpdateDomain::systemverilog_active);
                }
            } else if constexpr (std::is_same_v<T, WriteUpdate>) {
                signal_write(op.signal, reg(op.source, pc), std::nullopt, false,
                    op.domain);
            } else if constexpr (std::is_same_v<T, WriteUpdateSlice>) {
                signal_write(op.signal, reg(op.source, pc), op.offset, false,
                    op.domain);
            } else if constexpr (std::is_same_v<T, WriteUpdateDynamicSlice>) {
                if (const auto offset = written_offset(op.selection)) {
                    auto value = reg(op.source, pc);
                    signal_write(op.signal, std::move(value), *offset, false,
                        op.domain);
                }
            } else if constexpr (std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                std::optional<DynamicPartWrite> write;
                try {
                    write = dynamic_part_write_value(reg(op.source, pc),
                        reg(op.selection.base, pc), op.selection);
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
                if (write) {
                    signal_write(op.signal, std::move(write->value),
                        static_cast<std::uint32_t>(write->offset), false,
                        op.domain);
                }
            } else if constexpr (std::is_same_v<T, WriteProjected>) {
                signal_write(op.signal, reg(op.source, pc), std::nullopt, false,
                    SignalUpdateDomain::generic);
            } else if constexpr (std::is_same_v<T, WriteProjectedSlice>) {
                signal_write(op.signal, reg(op.source, pc), op.offset, false,
                    SignalUpdateDomain::generic);
            } else if constexpr (std::is_same_v<T, WriteProjectedDynamicSlice>) {
                auto value = reg(op.source, pc);
                signal_write(op.signal, std::move(value),
                    dynamic_offset(op.selection), false,
                    SignalUpdateDomain::generic);
            } else if constexpr (std::is_same_v<T, IntegerUnary>) {
                try {
                    reg(op.destination, pc)
                        = integer_unary_value(op.operation, reg(op.source, pc));
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, IntegerBinary>) {
                try {
                    reg(op.destination, pc) = integer_binary_value(
                        op.operation, reg(op.lhs, pc), reg(op.rhs, pc));
                } catch (const std::invalid_argument& error) {
                    fail(member_index, pc, error.what());
                }
            } else if constexpr (std::is_same_v<T, IntegerCheck>) {
                try {
                    check_integer_range(reg(op.source, pc), op.lower, op.upper);
                } catch (const std::invalid_argument& error) {
                    auto message = std::string { error.what() };
                    if (message
                        == "VHDL integer operand contains an unknown or high-impedance value") {
                        const auto& value = reg(op.source, pc);
                        auto bits = value.to_msb_string();
                        constexpr std::size_t maximum_bits { 128U };
                        if (bits.size() > maximum_bits) {
                            bits = bits.substr(0U, maximum_bits) + "...";
                        }
                        message += " [process="
                            + impl_.processes.program_view(member.process).name()
                            + ", register=" + std::to_string(op.source)
                            + ", width=" + std::to_string(value.width())
                            + ", value=" + bits + ", range="
                            + std::to_string(op.lower) + ".."
                            + std::to_string(op.upper) + "]";
                    }
                    fail(member_index, pc, message);
                }
            } else if constexpr (std::is_same_v<T, Assert>) {
                const auto& condition = reg(op.condition, pc);
                if (condition.width() != 1U || condition.get(0) != Logic4::one) {
                    const auto message = op.message.empty()
                        ? std::string_view { "assertion failed" }
                        : std::string_view { op.message };
                    if (op.severity != AssertionSeverity::failure
                        && impl_.report_hook) {
                        impl_.report_hook(member.process, message, op.severity,
                            op.source, kernel_now(),
                            warp_time_ ? 0U : impl_.scheduler.delta());
                    }
                    if (op.severity == AssertionSeverity::failure) {
                        throw AssertionError(member.process,
                            pc < member.origin.size() ? member.origin[pc] : pc,
                            std::string { message }, op.severity, op.source);
                    }
                }
            } else if constexpr (std::is_same_v<T, Report>) {
                // IEEE 1076-2019 10.9 (the planner keeps 2019 mode on the
                // host): the message goes through the assertion hook.
                if (impl_.report_hook) {
                    impl_.report_hook(member.process, op.message.str(),
                        op.severity, op.source, kernel_now(),
                        warp_time_ ? 0U : impl_.scheduler.delta());
                }
                if (op.severity == AssertionSeverity::failure) {
                    throw AssertionError(member.process,
                        pc < member.origin.size() ? member.origin[pc] : pc,
                        op.message.empty() ? std::string { "report failure" }
                                           : op.message.str(),
                        op.severity, op.source, true);
                }
            } else if constexpr (std::is_same_v<T, Call>) {
                const auto size = member.operations.size();
                if (op.stack.capacity == 0U) {
                    if (op.stack.pointer != 0U || op.stack.entries != 0U) {
                        fail(member_index, pc,
                            "dynamic call stack has fixed-register metadata");
                    }
                    if (member.call_stack.size() >= maximum_container_storage_bytes
                            / sizeof(InstructionIndex)) {
                        fail(member_index, pc,
                            "dynamic call stack exceeds its owning-storage budget");
                    }
                    if (op.target >= size || op.return_target >= size) {
                        fail(member_index, pc,
                            "call target is outside the operation stream");
                    }
                    member.call_stack.push_back(op.return_target);
                    next = op.target;
                    return;
                }
                const auto pointer = reg(op.stack.pointer, pc).low_word();
                if (pointer.bval != 0U) {
                    fail(member_index, pc, "call-stack pointer is unknown");
                }
                if (pointer.aval >= op.stack.capacity) {
                    fail(member_index, pc, "call-stack capacity is exhausted");
                }
                if (op.target >= size || op.return_target >= size) {
                    fail(member_index, pc,
                        "call target is outside the operation stream");
                }
                reg(static_cast<RegisterId>(op.stack.entries + pointer.aval), pc)
                    = PackedLogic4::from_aval_bval(32, op.return_target, 0);
                reg(op.stack.pointer, pc)
                    = PackedLogic4::from_aval_bval(32, pointer.aval + 1U, 0);
                next = op.target;
            } else if constexpr (std::is_same_v<T, Return>) {
                const auto size = member.operations.size();
                if (op.stack.capacity == 0U) {
                    if (op.stack.pointer != 0U || op.stack.entries != 0U) {
                        fail(member_index, pc,
                            "dynamic call stack has fixed-register metadata");
                    }
                    if (member.call_stack.empty()) {
                        fail(member_index, pc, "call-stack underflow");
                    }
                    const auto target = member.call_stack.back();
                    member.call_stack.pop_back();
                    if (target >= size) {
                        fail(member_index, pc,
                            "call-stack return target is invalid");
                    }
                    next = target;
                    return;
                }
                const auto pointer = reg(op.stack.pointer, pc).low_word();
                if (pointer.bval != 0U) {
                    fail(member_index, pc, "call-stack pointer is unknown");
                }
                if (pointer.aval == 0U || pointer.aval > op.stack.capacity) {
                    fail(member_index, pc, "call-stack underflow");
                }
                const auto next_pointer = pointer.aval - 1U;
                const auto target = reg(
                    static_cast<RegisterId>(op.stack.entries + next_pointer), pc)
                                        .low_word();
                if (target.bval != 0U || target.aval >= size) {
                    fail(member_index, pc, "call-stack return target is invalid");
                }
                reg(op.stack.pointer, pc)
                    = PackedLogic4::from_aval_bval(32, next_pointer, 0);
                next = static_cast<std::uint32_t>(target.aval);
            } else if constexpr (std::is_same_v<T, CallableFramePush>) {
                if (member.frames_elided) {
                    return;
                }
                if (op.identity == 0U) {
                    fail(member_index, pc,
                        "automatic callable frame identity is zero");
                }
                Member::Frame frame;
                frame.identity = op.identity;
                frame.ids.assign(op.packed.begin(), op.packed.end());
                frame.values.reserve(frame.ids.size());
                for (const auto id : frame.ids) {
                    const auto& value = reg(id, pc);
                    frame.bytes += sizeof(PackedLogic4)
                        + value.aval_words().size_bytes()
                        + value.bval_words().size_bytes()
                            * (value.is_logic9() ? 3U : 1U);
                    frame.values.push_back(value);
                }
                if (frame.bytes > maximum_container_storage_bytes
                        - std::min(member.frame_bytes,
                            maximum_container_storage_bytes)) {
                    fail(member_index, pc,
                        "automatic callable frames exceed their owning-storage budget");
                }
                member.frame_bytes += frame.bytes;
                member.frames.push_back(std::move(frame));
            } else if constexpr (std::is_same_v<T, CallableFramePop>) {
                if (member.frames_elided) {
                    return;
                }
                if (member.frames.empty()
                    || member.frames.back().identity != op.identity) {
                    fail(member_index, pc,
                        "automatic callable frame stack mismatch");
                }
                std::vector<PackedLogic4> preserved;
                preserved.reserve(op.preserve_packed.size());
                for (const auto id : op.preserve_packed) {
                    preserved.push_back(reg(id, pc));
                }
                auto frame = std::move(member.frames.back());
                member.frames.pop_back();
                for (std::size_t index = 0U; index < frame.ids.size(); ++index) {
                    reg(frame.ids[index], pc) = std::move(frame.values[index]);
                }
                member.frame_bytes -= frame.bytes;
                std::size_t index = 0U;
                for (const auto id : op.preserve_packed) {
                    reg(id, pc) = std::move(preserved[index++]);
                }
            } else if constexpr (std::is_same_v<T, ReadContainerObject>) {
                if (op.destination >= member.container_registers.size()
                    || op.object >= container_of_object_.size()
                    || container_of_object_[op.object] == no_container) {
                    fail(member_index, pc,
                        "static kernel container is not kernel-owned");
                }
                member.container_registers[op.destination]
                    = container_of_object_[op.object];
            } else if constexpr (std::is_same_v<T, ContainerRead>) {
                const auto& container = container_at(op.source);
                const auto& type = container.type;
                const auto& index_value = reg(op.index, pc);
                const auto unknown = [&] {
                    return PackedLogic4(type.element_width,
                        type.two_state ? Logic4::zero : Logic4::x);
                };
                const auto index = index_value.known_signed_value();
                if (op.linear_index) {
                    reg(op.destination, pc) = !index || *index < 0
                            || static_cast<std::uint64_t>(*index)
                                >= container.count
                        ? unknown()
                        : container_element(container,
                              static_cast<std::size_t>(*index));
                } else {
                    const auto low = std::min(type.index_left, type.index_right);
                    const auto high = std::max(type.index_left, type.index_right);
                    reg(op.destination, pc) = !index || *index < low
                            || *index > high
                        ? unknown()
                        : container_element(container,
                              fixed_offset(type,
                                  static_cast<std::int32_t>(*index)));
                }
            } else if constexpr (std::is_same_v<T, ContainerWrite>) {
                // A blocking element write of a private array.
                if (op.target >= member.container_registers.size()
                    || member.container_registers[op.target] == no_container
                    || containers_[member.container_registers[op.target]].object
                        != no_container_object
                    || op.string_index) {
                    fail(member_index, pc,
                        "static kernel container register is not private");
                }
                Pending pending;
                pending.kind = PendingKind::element;
                pending.member = member_index;
                pending.instruction = pc;
                pending.target = member.container_registers[op.target];
                pending.value = reg(op.source, pc);
                pending.index = reg(op.index, pc);
                pending.signed_index = op.signed_index;
                pending.linear_index = op.linear_index;
                write_element(pending);
            } else if constexpr (std::is_same_v<T, CopyContainerRegister>) {
                // A whole-array copy between private registers of one shape.
                const auto destination = op.destination
                        < member.container_registers.size()
                    ? member.container_registers[op.destination] : no_container;
                const auto source = op.source < member.container_registers.size()
                    ? member.container_registers[op.source] : no_container;
                if (destination == no_container || source == no_container) {
                    fail(member_index, pc,
                        "static kernel container register is not private");
                }
                copy_container(destination, source, member_index, pc);
            } else if constexpr (std::is_same_v<T, WriteContainerObjectElement>) {
                if (op.object >= container_of_object_.size()
                    || container_of_object_[op.object] == no_container) {
                    fail(member_index, pc,
                        "static kernel container is not kernel-owned");
                }
                if (!op.levels.empty()) {
                    fail(member_index, pc,
                        "static kernel cannot write nested container elements");
                }
                Pending pending;
                pending.kind = op.dynamic_part ? PendingKind::element_part
                                               : PendingKind::element;
                pending.member = member_index;
                pending.instruction = pc;
                pending.target = container_of_object_[op.object];
                pending.value = reg(op.source, pc);
                pending.index = reg(op.index, pc);
                pending.signed_index = op.signed_index;
                pending.linear_index = op.linear_index;
                if (op.dynamic_part) {
                    pending.base = reg(op.dynamic_part->base, pc);
                    pending.part = *op.dynamic_part;
                }
                if (op.nonblocking) {
                    push_generic_pending(std::move(pending));
                } else {
                    write_element(pending);
                }
            } else {
                fail(member_index, pc,
                    "static kernel member contains an unsupported operation");
            }
        }();
        return next;
    }
}

void Interpreter::Impl::StaticKernel::settle()
{
    std::size_t evaluations = 0U;
    const auto limit = members_.size() * 64U + 1'000'000U;
    while (queued_count_ != 0U) {
        while (buckets_[scan_level_].empty()) {
            ++scan_level_;
        }
        auto& bucket = buckets_[scan_level_];
        const auto entry = bucket.back();
        bucket.pop_back();
        --queued_count_;
        if ((entry & partition_tag) != 0U) {
            const auto partition = entry & ~partition_tag;
            partition_queued_[partition] = 0U;
            if (++evaluations > limit) {
                const auto member = partitions_[partition].members.front();
                fail(member, members_[member].body_begin,
                    "static kernel combinational logic did not settle");
            }
            run_partition(partition);
            continue;
        }
        member_schedule_[entry].queued = false;
        if (++evaluations > limit) {
            fail(entry, members_[entry].body_begin,
                "static kernel combinational logic did not settle");
        }
        // A combinational member does not wake on its own writes.
        running_member_ = entry;
        run(entry);
        running_member_ = no_slot;
    }
}

void Interpreter::Impl::StaticKernel::check_owned_edges()
{
    for (const auto slot_index : trigger_queue_) {
        auto& slot = slots_[slot_index];
        slot.trigger_pending = false;
        const auto before = slot.edge_seen;
        const auto after = slot_edge_bit(slot_index);
        slot.edge_seen = after;
        for (const auto& reader : slot.edges) {
            if (edge_matches(reader.edge, before, after)) {
                schedule(reader.member);
            }
        }
    }
    trigger_queue_.clear();
}

void Interpreter::Impl::StaticKernel::commit_pending()
{
    // NBA region: apply the queued writes in execution order, each one
    // notifying its readers as it lands.
    const auto count = writes_.count;
    writes_.count = 0U;
    auto elements = std::move(nba_);
    nba_.clear();
    auto generic = std::move(pending_generic_);
    pending_generic_.clear();
    for (std::uint32_t index = 0U; index < count; ++index) {
        const auto item = writes_.data[index];
        switch (item.kind) {
        case 0U:
            write_slot_word(item.slot, { item.a, item.b }, item.offset,
                item.width, item.member);
            continue;
        case 2U:
            write_element_entry(elements[item.member]);
            continue;
        default:
            break;
        }
        auto& entry = generic[item.member];
        switch (entry.kind) {
        case PendingKind::whole:
            write_signal_owned(entry.target, std::move(entry.value),
                std::nullopt, entry.member, entry.instruction);
            break;
        case PendingKind::slice:
            write_signal_owned(entry.target, std::move(entry.value),
                entry.offset, entry.member, entry.instruction);
            break;
        case PendingKind::element:
        case PendingKind::element_part:
            write_element(entry);
            break;
        }
    }
}

void Interpreter::Impl::StaticKernel::publish(
    const bool immediate, const bool ending)
{
    // The commits below are the kernel's own: the host must not import them
    // back (import_static_kernel_write). Staged updates commit later with the
    // slot's value, which an import finds equal.
    struct PublishingScope {
        bool& flag;
        bool saved;
        explicit PublishingScope(bool& value) noexcept
            : flag(value)
            , saved(value)
        {
            flag = true;
        }
        ~PublishingScope() { flag = saved; }
        PublishingScope(const PublishingScope&) = delete;
        PublishingScope& operator=(const PublishingScope&) = delete;
    } publishing { impl_.static_kernel_publishing };
    if (ending) {
        for (std::uint32_t index = 0U; index < slots_.size(); ++index) {
            if (slots_[index].output && !slots_[index].output_pending) {
                slots_[index].output_pending = true;
                output_queue_.push_back(index);
            }
        }
    }
    auto queue = std::move(output_queue_);
    output_queue_.clear();
    for (const auto slot_index : queue) {
        auto& slot = slots_[slot_index];
        slot.output_pending = false;
        // A value changed before this activation's NBA commit (a blocking
        // write, such as a clock edge) belongs to the Active region; one
        // changed by or after the commit to the NBA region.
        const auto domain = slot.vhdl_written ? SignalUpdateDomain::generic
            : slot.publish_nba ? SignalUpdateDomain::systemverilog_nba
                               : SignalUpdateDomain::systemverilog_active;
        if (!ending
            && std::equal(arena_.begin() + slot.offset,
                arena_.begin() + slot.offset + slot.planes * slot.words,
                arena_.begin() + slot.published)) {
            continue;
        }
        profile_publishes_ += profile_ ? 1U : 0U;
        const auto value = slot_value(slot_index);
        // A blocking Active-region write of an initializing member commits
        // at once, as the reference process does, so that the host
        // processes that start after it observe the value (IEEE 1800-2023
        // 6.8 and 10.5: declaration initializers take effect before any
        // initial or always procedure starts).
        const bool commit_now = immediate
            && (ending || domain == SignalUpdateDomain::systemverilog_active);
        if (slot.disjoint_writers && !slot.writers.empty()) {
            for (const auto& writer : slot.writers) {
                if (writer.offset == 0U && writer.width == slot.width) {
                    if (commit_now) {
                        impl_.commit_driver(writer.process, slot.signal, value);
                    } else {
                        impl_.stage_update(writer.process, slot.signal, value,
                            domain);
                    }
                } else if (commit_now) {
                    impl_.commit_driver_slice(writer.process, slot.signal,
                        extract_value(value, writer.offset, writer.width),
                        writer.offset);
                } else {
                    impl_.stage_update_slice(writer.process, slot.signal,
                        extract_value(value, writer.offset, writer.width),
                        writer.offset, domain);
                }
            }
        } else if (commit_now) {
            impl_.commit_driver(slot.last_writer, slot.signal, value);
        } else {
            impl_.stage_update(slot.last_writer, slot.signal, value, domain);
        }
        std::copy_n(arena_.begin() + slot.offset, slot.planes * slot.words,
            arena_.begin() + slot.published);
    }
}

void Interpreter::Impl::StaticKernel::initialize()
{
    initialized_ = true;
    // From now on the host lists the inputs whose revision changes.
    impl_.static_kernel_input_marks.assign(
        impl_.signal_value_revisions.size(), 0U);
    impl_.static_kernel_dirty_inputs.clear();
    for (auto& input : inputs_) {
        impl_.static_kernel_input_marks[input.signal] = 1U;
        input.revision = impl_.signal_value_revisions[input.signal];
        input.event_seen = impl_.signal_events[input.signal];
        input.cached = vhdl_ ? read_signal(input.signal)
                             : Impl::coerce_value_kind(read_signal(input.signal),
                                   ValueKind::logic4);
        write_mirror(input);
    }
    for (std::uint32_t index = 0U; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (!slot.output) {
            continue;
        }
        // Publish whatever differs from the scheduler's initial value.
        const auto host = Impl::coerce_value_kind(
            impl_.logical_signal_value(slot.signal),
            slot.planes == 4U ? ValueKind::logic9 : ValueKind::logic4);
        for (std::uint32_t plane = 0U; plane < slot.planes; ++plane) {
            const auto words = value_plane(host, plane);
            for (std::uint32_t word = 0U; word < slot.words; ++word) {
                arena_[slot.published + plane * slot.words + word]
                    = word < words.size() ? words[word] : 0U;
            }
        }
        if (!slot.output_pending) {
            slot.output_pending = true;
            output_queue_.push_back(index);
        }
    }
    // An always_comb or always_latch starts after every initial and always
    // procedure has started (IEEE 1800-2023 9.2.2.2.2); such members run
    // once the threads have (start_after_procedures).
    const auto after_procedures = [&](const Member& member) {
        return member.after_procedures && !member.vhdl && behavioral_
            && ((member.kind == StaticKernelMemberKind::combinational
                    && member.run_at_start)
                || member.kind == StaticKernelMemberKind::once);
    };
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        const auto& member = members_[index];
        if (after_procedures(member)) {
            after_procedure_members_.push_back(index);
            continue;
        }
        if (member.run_at_start
            && (member.kind == StaticKernelMemberKind::combinational
                || (member.vhdl && member.kind == StaticKernelMemberKind::sequential))) {
            schedule(index);
        }
    }
    if (behavioral_) {
        prepare_behavioral();
    }
    for (std::uint32_t index = 0U; index < members_.size(); ++index) {
        if (members_[index].kind == StaticKernelMemberKind::once) {
            if (after_procedures(members_[index])) {
                continue;
            }
            if (mixed_ && members_[index].vhdl) {
                // Its assignments belong to the first VHDL round.
                auto& state = member_schedule_[index];
                if (!state.round) {
                    state.round = true;
                    next_round_.push_back(index);
                }
                continue;
            }
            run(index);
        }
    }
    // Initializing members have run to completion; later host processes
    // read what they wrote.
    publish(true);
    if (behavioral_) {
        start_behavioral();
    }
}

void Interpreter::Impl::StaticKernel::write_mirror(const Input& input)
{
    if (input.mirror == no_slot) {
        return;
    }
    const auto& slot = slots_[input.mirror];
    const auto value = Impl::coerce_value_kind(input.cached,
        slot.planes == 4U ? ValueKind::logic9 : ValueKind::logic4);
    for (std::uint32_t plane = 0U; plane < slot.planes; ++plane) {
        const auto words = value_plane(value, plane);
        for (std::uint32_t word = 0U; word < slot.words; ++word) {
            arena_[slot.offset + plane * slot.words + word]
                = word < words.size() ? words[word] : 0U;
        }
    }
}

bool Interpreter::Impl::StaticKernel::scan_inputs(const bool all)
{
    bool changed = false;
    // Inputs no process writes change only between activations; within one
    // only the others are scanned.
    if (check_inputs_ || !initialized_) {
        const auto count = all ? inputs_.size() : written_inputs_.size();
        for (std::size_t position = 0U; position < count; ++position) {
            changed = refresh_input(
                          inputs_[all ? position : written_inputs_[position]])
                || changed;
        }
        return changed;
    }
    // Only the inputs whose revision changed (Impl::note_signal_revision),
    // in input order as a full scan visits them.
    auto& dirty = impl_.static_kernel_dirty_inputs;
    if (dirty.empty()) {
        return false;
    }
    std::vector<std::uint32_t> order;
    order.swap(scan_order_);
    order.clear();
    std::size_t kept = 0U;
    for (const auto signal : dirty) {
        const auto input = input_of_signal_[signal];
        if (!all && inputs_[input].unwritten) {
            dirty[kept++] = signal;
            continue;
        }
        impl_.static_kernel_input_marks[signal] = 1U;
        order.push_back(input);
    }
    dirty.resize(kept);
    std::ranges::sort(order);
    for (const auto input : order) {
        changed = refresh_input(inputs_[input]) || changed;
    }
    order.swap(scan_order_);
    return changed;
}

bool Interpreter::Impl::StaticKernel::refresh_input(Input& input)
{
    // Every committed host value change bumps the signal's revision.
    const auto revision = impl_.signal_value_revisions[input.signal];
    if (revision == input.revision && !check_inputs_) {
        return false;
    }
    const auto& current = impl_.logical_signal_value(input.signal);
    if (check_inputs_ && revision == input.revision
        && !(current == input.cached)
        && !(Impl::coerce_value_kind(current, ValueKind::logic4)
            == input.cached)) {
        std::cerr << "fsim-kernel-check: input "
                  << impl_.get_signal_cold(input.signal).name
                  << " changed without a revision bump\n";
    }
    input.revision = revision;
    const auto seen = std::exchange(
        input.event_seen, impl_.signal_events[input.signal]);
    if (current == input.cached) {
        // A VHDL signal can change and return to the scanned value inside one
        // update phase (two force assignments of one cycle apply one after
        // the other). Each applied change is an event (IEEE 1076-2019
        // 14.7.3.1, 14.7.5.3: processes sensitive to the signal resume), so
        // the readers wake although the net change is empty.
        // Only a signal no member writes: the host's publication of a
        // member's own write is already in the cached value and is no new
        // event for the members.
        if (!vhdl_ || !input.event_seen || input.event_seen == seen
            || (input.signal < slot_of_signal_.size()
                && slot_of_signal_[input.signal] != no_slot)) {
            return false;
        }
        if (trace_) {
            std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                      << " input " << impl_.get_signal_cold(input.signal).name
                      << " event without net change\n";
        }
        auto same = current;
        apply_input(input, std::move(same), true);
        return true;
    }
    auto value = vhdl_ ? current
                       : Impl::coerce_value_kind(current, ValueKind::logic4);
    profile_input_changes_ += profile_ ? 1U : 0U;
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                  << " input " << impl_.get_signal_cold(input.signal).name
                  << " " << input.cached.to_msb_string() << " -> "
                  << value.to_msb_string() << '\n';
    }
    apply_input(input, std::move(value));
    return true;
}

void Interpreter::Impl::StaticKernel::apply_input(
    Input& input, PackedLogic4 value, const bool event)
{
    const auto before = std::exchange(input.cached, std::move(value));
    write_mirror(input);
    if (behavioral_) {
        const auto input_index = static_cast<std::size_t>(&input - inputs_.data());
        if (input_index < input_waiters_.size()
            && !input_waiters_[input_index].empty()) {
            WaitChange change;
            change.before = edge_bit(before);
            change.after = edge_bit(input.cached);
            change.input_before = &before;
            change.input_after = &input.cached;
            change.event = event;
            wake_waiters(input_waiters_[input_index], change);
        }
    }
    for (const auto& reader : input.readers) {
        bool differs = event || reader.width == 0U
            || before.width() != input.cached.width()
            || reader.offset + reader.width > input.cached.width();
        for (std::uint32_t bit = reader.offset;
             !differs && bit < reader.offset + reader.width; ++bit) {
            differs = vhdl_
                ? before.get_logic9(bit) != input.cached.get_logic9(bit)
                : before.get(bit) != input.cached.get(bit);
        }
        if (differs) {
            schedule(reader.member);
        }
    }
    if (!input.edges.empty()) {
        const auto old_bit = edge_bit(before);
        const auto new_bit = edge_bit(input.cached);
        for (const auto& reader : input.edges) {
            if (edge_matches(reader.edge, old_bit, new_bit)) {
                schedule(reader.member);
            }
        }
    }
}

bool Interpreter::Impl::StaticKernel::start_after_procedures()
{
    if (after_procedure_members_.empty()) {
        return false;
    }
    auto members = std::move(after_procedure_members_);
    after_procedure_members_.clear();
    for (const auto index : members) {
        if (members_[index].kind == StaticKernelMemberKind::once) {
            run(index);
        } else {
            schedule(index);
        }
    }
    return true;
}

bool Interpreter::Impl::StaticKernel::activate()
{
    host_active_staged_ = false;
    const auto activation_started = profile_ ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point { };
    struct ActivationTimer {
        StaticKernel& kernel;
        std::chrono::steady_clock::time_point started;
        ~ActivationTimer()
        {
            if (kernel.profile_) {
                kernel.profile_activation_ns_ += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count());
            }
        }
    } timer { *this, activation_started };
    warp_time_.reset();
    nonblocking_committed_ = false;
    yield_requested_ = false;
    profile_activations_ += profile_ ? 1U : 0U;
    // Hosted wakes during this activation wait for its drain loop instead of
    // waking the host process again.
    struct HostedActivation {
        StaticKernel& kernel;
        explicit HostedActivation(StaticKernel& owner) : kernel(owner)
        {
            kernel.in_activation_ = kernel.has_hosted_;
        }
        ~HostedActivation()
        {
            kernel.in_activation_ = false;
            kernel.hosted_inactive_task_ = false;
            kernel.hosted_inactive_region_ = false;
        }
    } hosted_activation { *this };
    if (std::exchange(debug_stopped_, false)) {
        // The run continues after a debugger stop.
        stopped_ = false;
    }
    if (paused_thread_ != no_slot) {
        // The run continues after a $stop: the pausing thread resumes first.
        stopped_ = false;
        const auto paused = std::exchange(paused_thread_, no_slot);
        if (threads_[paused].alive) {
            ready_thread(paused);
            // It resumes ahead of the threads the stop left ready.
            auto& queue = members_[threads_[paused].member].vhdl
                ? vhdl_ready_threads_ : ready_threads_;
            if (queue.size() > 1U && queue.back() == paused) {
                std::rotate(queue.begin(), queue.end() - 1, queue.end());
            }
        }
    }
    const bool first_activation = !initialized_;
    if (first_activation) {
        if (has_hosted_) {
            run_hosted_initializers();
        }
        initialize();
    } else {
        (void)scan_inputs();
    }
    if (late_start_armed_ && !first_activation) {
        // The delta after time zero: continuous assignments and event
        // controls of time zero have settled (see start_behavioral).
        late_start_armed_ = false;
        for (const auto index : std::exchange(late_members_,
                 std::vector<std::uint32_t> { })) {
            ready_thread(spawn_thread(index, 0U, no_slot, no_slot));
        }
    }
    activate_step();
    // A closed kernel (no host process, output or host-written input) whose
    // next event is its own timer, with nothing else pending on the host up
    // to that time, runs its next time steps itself instead of handing each
    // one to the host scheduler. Nothing outside the kernel can observe the
    // steps in between; time and $finish are kept at their exact values.
    const bool observed = static_cast<bool>(impl_.signal_change_hook)
        || static_cast<bool>(impl_.stored_signal_change_hook)
        || static_cast<bool>(impl_.scalar_signal_change_hook)
        || static_cast<bool>(impl_.driver_change_hook)
        || static_cast<bool>(impl_.container_object_change_hook)
        || static_cast<bool>(impl_.container_element_change_hook);
    for (std::size_t step = 0U; time_warp_ && closed_ && !has_hosted_
         && !observed && behavioral_
         && !vhdl_threads_ && !stopped_ && !nonblocking_deferred_
         && !yield_requested_ && step < max_warp_steps; ++step) {
        const auto next = next_timer();
        if (!next) {
            break;
        }
        const auto host_next = impl_.scheduler.next_pending_time();
        if (host_next && *host_next <= *next) {
            break;
        }
        warp_time_ = *next;
        nonblocking_committed_ = false;
        profile_warps_ += profile_ ? 1U : 0U;
        activate_step();
    }
    if (behavioral_) {
        arm_timer();
        if (vhdl_threads_) {
            arm_vhdl_timer();
        }
    }
    // The initializing activation runs the time-zero Active work of the
    // processes it replaces (declaration initializers, continuous
    // assignments), whose blocking writes host processes that start after
    // it see (IEEE 1800-2023 6.8, 10.3, 4.7): commit them at once.
    // In the host's NBA region (the deferred commit) the kernel's updates
    // belong to that region and apply with the host's own (IEEE 1800-2023
    // 4.5), before the processes they wake run.
    publish((first_activation && !warp_time_) || in_host_nonblocking_region_);
    if (has_hosted_) {
        // After the publication: its commits may wake hosted processes.
        finish_hosted();
    }
    return yield_requested_;
}

void Interpreter::Impl::StaticKernel::activate_step()
{
    if (behavioral_) {
        collect_timers();
    }
    if (has_hosted_) {
        collect_hosted();
    }
    if (mixed_) {
        activate_mixed();
        return;
    }
    const auto run_triggered = [&] {
        auto triggered = std::move(triggered_);
        triggered_.clear();
        std::ranges::sort(triggered);
        for (const auto member : triggered) {
            member_schedule_[member].triggered = false;
            run(member);
        }
    };
    for (std::size_t iteration = 0U;; ++iteration) {
        if (iteration > edge_iteration_limit) {
            fail(0U, 0U, "static kernel edge iteration limit exceeded");
        }
        settle();
        check_owned_edges();
        if (vhdl_) {
            if (!triggered_.empty() || writes_.count != 0U
                || !vhdl_ready_threads_.empty() || !hosted_active_.empty()) {
                run_triggered();
                // Behavioral processes resumed for this delta run with the
                // members (1076-2019 14.7.5.3); their assignments commit
                // with the round.
                (void)run_vhdl_threads();
                if (has_hosted_) {
                    // Hosted VHDL processes resumed for this delta run with
                    // the members; their assignments are the host's staged
                    // updates, applied in this delta's update phase (S25a).
                    (void)run_hosted_active();
                    if (impl_.scheduler.stop_requested()) {
                        break;
                    }
                }
                if (stopped_) {
                    break;
                }
                commit_round();
                if (queued_count_ != 0U || !triggered_.empty()
                    || !trigger_queue_.empty()
                    || !vhdl_ready_threads_.empty()
                    || !hosted_active_.empty()) {
                    // One round is one VHDL delta. Resume in the host's next
                    // delta so host processes see and drive each delta at its
                    // reference position.
                    yield_requested_ = true;
                    break;
                }
                continue;
            }
        } else {
            bool active = false;
            if (!triggered_.empty()) {
                run_triggered();
                active = true;
            }
            if (has_hosted_) {
                // Hosted processes run in the Active region with the members
                // and threads; what they wake runs in the next iteration.
                // They read the host's copy of the kernel's outputs, so the
                // blocking and continuous updates the members made before
                // them commit first (IEEE 1800-2023 4.7, 6.8: declaration
                // initializers before any procedure); NBA-region values
                // stay staged.
                if (!hosted_active_.empty() && !output_queue_.empty()) {
                    publish(true);
                }
                if (run_hosted_active()) {
                    active = true;
                    // Their blocking writes are events now: the threads and
                    // members that run next, or wait next, see the new
                    // values (IEEE 1800-2023 4.7, 9.4.2), not a later edge.
                    (void)scan_inputs(false);
                }
                if (impl_.scheduler.stop_requested()) {
                    break;
                }
            }
            if (behavioral_) {
                active = run_ready_threads() || active;
                if (stopped_) {
                    break;
                }
                active = start_after_procedures() || active;
            }
            // The Active region drains (combinational settling included)
            // before #0 resumptions (Inactive) and before the NBA region.
            if (active) {
                continue;
            }
            if (!inactive_threads_.empty() || !hosted_inactive_.empty()) {
                // The Inactive region follows every Active event, including
                // the host's, and precedes the NBA region (IEEE 1800-2023
                // 4.4, 4.5). While the host still has Active events (staged
                // updates, the processes changed outputs wake, queued
                // processes) a host task of the Inactive region activates
                // the kernel again to resume the #0 threads and hosted
                // processes; otherwise the Inactive region starts now.
                if (!hosted_inactive_region_) {
                    if (scan_inputs(false)) {
                        continue;
                    }
                    if (has_hosted_ || host_active_staged_
                        || !output_queue_.empty()
                        || impl_.scheduler.systemverilog_active_work_pending()) {
                        defer_hosted_inactive();
                        break;
                    }
                }
                hosted_inactive_region_ = false;
                for (const auto thread : inactive_threads_) {
                    ready_thread(thread);
                }
                inactive_threads_.clear();
                if (!hosted_inactive_.empty() && !output_queue_.empty()) {
                    publish(true);
                }
                if (run_hosted_inactive()) {
                    (void)scan_inputs(false);
                }
                if (impl_.scheduler.stop_requested()) {
                    break;
                }
                continue;
            }
            if (writes_.count != 0U) {
                if (defer_nonblocking()) {
                    break;
                }
                nonblocking_committed_ = true;
                commit_pending();
                continue;
            }
            if (active) {
                continue;
            }
        }
        // Blocking commits to host-owned signals the kernel also reads are
        // visible immediately; a process never wakes on its own writes.
        if (!scan_inputs(false)) {
            break;
        }
    }
}

bool Interpreter::Impl::StaticKernel::defer_nonblocking()
{
    // The kernel runs the Active, NBA and following Active iterations of an
    // activation in one host callback. Its NBA region begins only after the
    // host's same-time Active and Inactive events, including the processes
    // this activation's outputs and staged updates wake (IEEE 1800-2023 4.5),
    // or host processes would observe the writes that follow the commit.
    if (std::exchange(nonblocking_region_, false)) {
        return false;
    }
    static const bool enabled = [] {
        const char* value = std::getenv("FSIM_STATIC_KERNEL_NBA_DEFER");
        return value == nullptr || std::string_view { value } != "0";
    }();
    if (!enabled || closed_ || warp_time_
        || (!host_active_staged_ && output_queue_.empty()
            && hosted_active_.empty()
            && !impl_.scheduler.systemverilog_active_work_pending())) {
        return false;
    }
    publish();
    if (nonblocking_deferred_) {
        return true;
    }
    nonblocking_deferred_ = true;
    struct KernelNonblockingTask {
        Interpreter::Impl* owner { };
        ProcessId host { };
    };
    const auto task = fsim::runtime::detail::make_scheduler_task_descriptor<
        KernelNonblockingTask,
        +[](Scheduler&, const KernelNonblockingTask& scheduled) {
            auto& kernel = *scheduled.owner->static_kernel;
            kernel.nonblocking_deferred_ = false;
            kernel.nonblocking_region_ = true;
            kernel.in_host_nonblocking_region_ = true;
            bool yielded { };
            try {
                yielded = kernel.activate();
            } catch (...) {
                kernel.nonblocking_region_ = false;
                kernel.in_host_nonblocking_region_ = false;
                throw;
            }
            kernel.nonblocking_region_ = false;
            kernel.in_host_nonblocking_region_ = false;
            if (yielded) {
                scheduled.owner->queue_next_delta(scheduled.host);
            }
        }>(KernelNonblockingTask { &impl_, host_process_ });
    impl_.scheduler.schedule_internal_systemverilog_at(impl_.scheduler.now(),
        SchedulerPhase::update, host_process_, task);
    return true;
}

void Interpreter::Impl::StaticKernel::swap_write_queues()
{
    std::swap(writes_, parked_writes_);
    std::swap(write_storage_, parked_storage_);
    std::swap(pending_generic_, parked_pending_);
}

void Interpreter::Impl::StaticKernel::sv_drain()
{
    // SystemVerilog regions of one delta: Active (combinational settling,
    // edge-triggered members, ready threads), Inactive (#0), NBA.
    for (std::size_t iteration = 0U;; ++iteration) {
        if (iteration > edge_iteration_limit) {
            fail(0U, 0U, "static kernel edge iteration limit exceeded");
        }
        settle();
        check_owned_edges();
        bool active = false;
        if (!triggered_.empty()) {
            auto triggered = std::move(triggered_);
            triggered_.clear();
            std::ranges::sort(triggered);
            for (const auto member : triggered) {
                member_schedule_[member].triggered = false;
                run(member);
            }
            active = true;
        }
        if (behavioral_) {
            active = run_ready_threads() || active;
            if (stopped_) {
                return;
            }
            active = start_after_procedures() || active;
            if (active) {
                continue;
            }
            if (!inactive_threads_.empty()) {
                if (host_active_staged_ || !output_queue_.empty()) {
                    publish();
                    yield_requested_ = true;
                    return;
                }
                for (const auto thread : inactive_threads_) {
                    ready_thread(thread);
                }
                inactive_threads_.clear();
                continue;
            }
        }
        if (writes_.count != 0U) {
            nonblocking_committed_ = true;
            commit_pending();
            continue;
        }
        if (active) {
            continue;
        }
        return;
    }
}

void Interpreter::Impl::StaticKernel::activate_mixed()
{
    // One iteration is one delta, as the reference scheduler interleaves
    // them: VHDL members of the round run (their assignments deferred),
    // SystemVerilog regions drain, then the round commits. SystemVerilog work
    // woken by a commit and VHDL members woken by anything run in the next
    // delta. With host processes at the boundary each delta is a host delta;
    // without, the host still sees a delta every few hundred rounds, so its
    // delta limit stops a design that never settles.
    constexpr std::size_t rounds_per_host_delta = 256U;
    std::size_t rounds = 0U;
    sv_drain_outside_round();
    while (!stopped_) {
        if (next_round_.empty() && deferred_targets_.empty()
            && deferred_threads_.empty() && vhdl_ready_threads_.empty()
            && hosted_active_.empty()) {
            if (!scan_inputs(false)) {
                break;
            }
            sv_drain_outside_round();
            continue;
        }
        for (const auto thread : deferred_threads_) {
            ready_threads_.push_back(thread);
        }
        deferred_threads_.clear();
        deferred_buffer_.swap(deferred_targets_);
        deferred_targets_.clear();
        for (const auto& target : deferred_buffer_) {
            if ((target.id & partition_tag) == 0U) {
                member_schedule_[target.id].deferred = false;
                schedule(target.id);
            } else {
                schedule_target(target);
            }
        }
        round_.swap(next_round_);
        next_round_.clear();
        sort_round();
        swap_write_queues();
        for (const auto member : round_) {
            member_schedule_[member].round = false;
            run(member);
        }
        // VHDL behavioral processes resumed for this delta run with the
        // round's members, their assignments in the round's queue.
        (void)run_vhdl_threads();
        if (has_hosted_) {
            // Hosted VHDL processes resumed for this delta (S25a).
            (void)run_hosted_active();
            if (impl_.scheduler.stop_requested()) {
                swap_write_queues();
                break;
            }
        }
        swap_write_queues();
        sv_drain();
        if (stopped_) {
            break;
        }
        swap_write_queues();
        committing_round_ = true;
        commit_round();
        check_owned_edges();
        committing_round_ = false;
        swap_write_queues();
        if ((host_boundary_ || ++rounds == rounds_per_host_delta)
            && (!next_round_.empty() || !deferred_targets_.empty()
                || !deferred_threads_.empty()
                || !vhdl_ready_threads_.empty() || !hosted_active_.empty())) {
            yield_requested_ = true;
            break;
        }
    }
    if (!held_round_.empty() || !held_threads_.empty()) {
        next_round_.insert(next_round_.end(), held_round_.begin(), held_round_.end());
        vhdl_ready_threads_.insert(vhdl_ready_threads_.end(),
            held_threads_.begin(), held_threads_.end());
        held_round_.clear();
        held_threads_.clear();
        yield_requested_ = yield_requested_ || !stopped_;
    }
}

void Interpreter::Impl::StaticKernel::sv_drain_outside_round()
{
    // SystemVerilog work outside a round (pending at activation, or woken by
    // inputs after a round settled) runs in the current host delta, and a
    // VHDL process woken by its writes runs in the next delta (1076-2019
    // 14.7.5.3, S16). Without a host boundary the next loop iteration is
    // that delta. With one, every round is a host delta: the hosted VHDL
    // processes the same writes wake resume in the next host delta
    // (queue_next_delta), so the members and threads woken here wait for it
    // too, not for the next iteration of this activation.
    if (!host_boundary_) {
        sv_drain();
        return;
    }
    const auto members = next_round_.size();
    const auto threads = vhdl_ready_threads_.size();
    sv_drain();
    held_round_.insert(held_round_.end(),
        next_round_.begin() + static_cast<std::ptrdiff_t>(members), next_round_.end());
    next_round_.resize(members);
    held_threads_.insert(held_threads_.end(),
        vhdl_ready_threads_.begin() + static_cast<std::ptrdiff_t>(threads),
        vhdl_ready_threads_.end());
    vhdl_ready_threads_.resize(threads);
}

void Interpreter::Impl::StaticKernel::sort_round()
{
    // Members run in index order. A round holds each member once, so a large
    // one sorts through a bitmap of member indices instead of comparisons.
    if (round_.size() <= 32U) {
        std::ranges::sort(round_);
        return;
    }
    round_bits_.resize((members_.size() + 63U) / 64U);
    std::uint32_t first = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t last = 0U;
    for (const auto member : round_) {
        round_bits_[member / 64U] |= std::uint64_t { 1 } << (member % 64U);
        first = std::min(first, member / 64U);
        last = std::max(last, member / 64U);
    }
    round_.clear();
    for (auto word = first; word <= last; ++word) {
        for (auto bits = std::exchange(round_bits_[word], 0U); bits != 0U;
             bits &= bits - 1U) {
            round_.push_back(word * 64U
                + static_cast<std::uint32_t>(std::countr_zero(bits)));
        }
    }
}

void Interpreter::Impl::StaticKernel::materialize(const SignalId signal)
{
    if (const auto family = family_of_signal_[signal]; family != no_slot) {
        for (const auto& leaf : families_[family].leaves) {
            materialize(slots_[leaf.slot].signal);
        }
        return;
    }
    const auto slot_index = slot_of_signal_[signal];
    if (slot_index == no_slot) {
        return;
    }
    const auto& slot = slots_[slot_index];
    if (slot.output) {
        return;
    }
    const auto value = slot_value(slot_index);
    if (impl_.logical_signal_value(signal) == value) {
        return;
    }
    if (slot.disjoint_writers && !slot.writers.empty()) {
        for (const auto& writer : slot.writers) {
            if (writer.offset == 0U && writer.width == slot.width) {
                impl_.commit_driver(writer.process, signal, value);
            } else {
                impl_.commit_driver_slice(writer.process, signal,
                    extract_value(value, writer.offset, writer.width),
                    writer.offset);
            }
        }
    } else {
        impl_.commit_driver(slot.last_writer, signal, value);
    }
}

void Interpreter::Impl::StaticKernel::import_host_write(
    const SignalId signal, const SignalChangeOrigin origin)
{
    if (signal >= slot_of_signal_.size()) {
        return;
    }
    // Aggregate proxies have no slot: their leaves are separate owned signals
    // that the host commits (and this hook imports) on their own.
    const auto slot_index = slot_of_signal_[signal];
    if (slot_index == no_slot) {
        return;
    }
    auto& slot = slots_[slot_index];
    const auto value = Impl::coerce_value_kind(impl_.logical_signal_value(signal),
        slot.planes == 4U ? ValueKind::logic9 : ValueKind::logic4);
    if (value.width() != slot.width) {
        return;
    }
    const auto size = slot.planes * slot.words;
    import_before_.assign(arena_.begin() + slot.offset,
        arena_.begin() + slot.offset + size);
    for (std::uint32_t plane = 0U; plane < slot.planes; ++plane) {
        const auto words = value_plane(value, plane);
        for (std::uint32_t word = 0U; word < slot.words; ++word) {
            arena_[slot.offset + plane * slot.words + word]
                = word < words.size() ? words[word] : 0U;
        }
    }
    if (std::equal(import_before_.begin(), import_before_.end(),
            arena_.begin() + slot.offset)) {
        // The kernel's own publication arriving (or a write of the value the
        // slot already holds): nothing changed.
        return;
    }
    if (slot.output) {
        // The host holds this value now; do not publish it back.
        std::copy_n(arena_.begin() + slot.offset, size,
            arena_.begin() + slot.published);
    }
    if (!initialized_) {
        // Before the first activation every member is evaluated from the
        // slots anyway (initialize()); nothing is waiting to be woken.
        return;
    }
    if (trace_) {
        std::cerr << "fsim-kernel-trace: t=" << impl_.scheduler.now()
                  << " import " << impl_.get_signal_cold(signal).name << " "
                  << value.to_msb_string() << '\n';
    }
    slot_changed(slot_index, 0U, import_before_.data());
    if (in_activation_) {
        // A hosted process wrote it inside an activation: the running loop
        // settles the members it wakes.
        return;
    }
    // The change is an update event; the members it wakes are evaluation
    // events of the same time step's Active region (IEEE 1800-2023 4.4.2.2,
    // 4.5), so the host process that runs the kernel is queued like any
    // process a signal change wakes. It is a no-op if already queued.
    impl_.queue_next_delta(host_process_, origin);
}

namespace {

class StaticKernelExecutor final : public ProcessExecutor {
public:
    explicit StaticKernelExecutor(std::function<bool()> activate)
        : activate_(std::move(activate))
    {
    }

    ProcessResumeResult resume(
        ProcessExecutionContext&, InstructionIndex) override
    {
        // The host stub is [WaitSensitivity, Jump 0, Yield, Jump 0].
        return activate_() ? ProcessResumeResult { 2U, 3U }
                           : ProcessResumeResult { 0U, 1U };
    }

private:
    std::function<bool()> activate_;
};

} // namespace

void Interpreter::Impl::materialize_static_kernel_signal(const SignalId signal)
{
    if (!static_kernel) {
        return;
    }
    static_kernel_materializing = true;
    try {
        static_kernel->materialize(signal);
    } catch (...) {
        static_kernel_materializing = false;
        throw;
    }
    static_kernel_materializing = false;
}

void Interpreter::Impl::import_static_kernel_write(
    const SignalId signal, const SignalChangeOrigin origin)
{
    if (!static_kernel || static_kernel_materializing || static_kernel_publishing
        || signal >= static_kernel_owned_signal.size()
        || static_kernel_owned_signal[signal] == 0U) {
        return;
    }
    static_kernel->import_host_write(signal, origin);
}

void InterpreterProgramAccess::set_static_kernel_time_warp(
    Interpreter& interpreter, const bool allowed)
{
    auto& impl = *interpreter.impl_;
    if (impl.static_kernel) {
        impl.static_kernel->set_time_warp(allowed);
    }
}

InterpretationCounts InterpreterProgramAccess::interpretation_counts(
    const Interpreter& interpreter)
{
    const auto& impl = *interpreter.impl_;
    auto counts = impl.static_kernel ? impl.static_kernel->interpretation_counts()
                                     : InterpretationCounts { };
    counts.executes = impl.interpreted_executes;
    counts.constant_drivers = impl.constant_driver_executes;
    for (const auto& [process, count] : impl.interpreted_programs) {
        counts.executed_programs.emplace_back(
            std::string { impl.processes.program_view(process).name() }, count);
    }
    return counts;
}

void InterpreterProgramAccess::install_static_kernel(
    Interpreter& interpreter, StaticKernelRuntimeSpec spec)
{
    auto& impl = *interpreter.impl_;
    if (impl.started) {
        throw std::logic_error(
            "the static kernel must be installed before simulation starts");
    }
    if (impl.static_kernel) {
        throw std::logic_error("a static kernel is already installed");
    }
    if (impl.fusion_dormant_process.size() != impl.processes.size()) {
        impl.fusion_dormant_process.assign(impl.processes.size(), 0U);
    }
    for (const auto& member : spec.members) {
        if (member.process >= impl.processes.size()) {
            throw std::out_of_range("static kernel member is not installed");
        }
        // Members never run on the scheduler; the host is driven by the
        // kernel executor rather than a native process executor.
        // 1 marks an inert member (no sensitivity, never initialized or
        // final on the host); 2 the host, which runs the kernel.
        impl.fusion_dormant_process[member.process]
            = member.process == spec.host ? 2U : 1U;
        // Installed without their sensitivity, members still drive the
        // signals the host's time-zero start treats as settling.
        if (!member.sensitivity.empty()) {
            if (impl.static_kernel_event_driven_process.size()
                != impl.processes.size()) {
                impl.static_kernel_event_driven_process.assign(
                    impl.processes.size(), 0U);
            }
            impl.static_kernel_event_driven_process[member.process] = 1U;
        }
    }
    impl.static_kernel_import_enabled = spec.import_host_writes;
    impl.static_kernel_owned_signal.assign(impl.signals.size(), 0U);
    for (const auto signal : spec.owned_signals) {
        if (signal >= impl.signals.size()) {
            throw std::out_of_range("static kernel signal is not declared");
        }
        impl.static_kernel_owned_signal[signal] = 1U;
    }
    for (const auto& family : spec.families) {
        if (family.proxy >= impl.signals.size()) {
            throw std::out_of_range("static kernel proxy is not declared");
        }
        impl.static_kernel_owned_signal[family.proxy] = 1U;
    }
    const auto host = spec.host;
    auto kernel = std::make_shared<Interpreter::Impl::StaticKernel>(
        impl, std::move(spec));
    impl.static_kernel = kernel;
    interpreter.set_process_executor(host,
        std::make_unique<StaticKernelExecutor>(
            [kernel = std::move(kernel)] { return kernel->activate(); }));
}

} // namespace fsim::runtime::simir
