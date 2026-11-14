// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsim::elaboration {

// The resolver program of a net of a user-defined nettype (IEEE 1800-2023
// 6.6.7): the runtime writes the values of the net's drivers to register
// `input`, the first in the most significant position, and runs one pass of
// the program, which calls the nettype's resolution function on a dynamic
// array of those values and leaves the net's value in register `output`.
std::optional<Lowerer::HirVhdlResolverProgram>
Lowerer::lower_hir_systemverilog_resolver_process(const SignalId signal,
    const semantic::DeclarationId function, const std::size_t driver_count,
    const std::string_view hierarchy,
    std::vector<std::vector<std::size_t>> elements)
{
    const auto declaration = specialized_hir_unit_ != nullptr
        ? specialized_hir_unit_->find_declaration(function)
        : std::nullopt;
    if (!declaration || declaration->systemverilog == nullptr
        || signal >= design_.signal_info_.size()) {
        return std::nullopt;
    }
    HirProcessDescription description;
    description.source = declaration->systemverilog->source;
    description.scope = declaration->systemverilog->scope;
    const auto& name = design_.signal_info_[signal].name;
    description.name = "resolver_"
        + name.substr(name.find_last_of('.') + 1U);
    description.kind = frontend::ProcessKind::Initial;
    description.systemverilog_resolver
        = HirProcessDescription::SystemVerilogResolver {
              signal, function, driver_count, std::move(elements) };
    hir_vhdl_resolver_registers_.reset();
    auto process = lower_hir_process_body(
        description, frontend::Language::SystemVerilog2017, hierarchy);
    if (!process || !hir_vhdl_resolver_registers_) {
        return std::nullopt;
    }
    // The runtime runs the program once per resolution; it is never
    // scheduled itself.
    process->initialize = false;
    return HirVhdlResolverProgram { std::move(*process),
        hir_vhdl_resolver_registers_->first,
        hir_vhdl_resolver_registers_->second };
}

bool Lowerer::lower_hir_systemverilog_resolver(
    const HirProcessDescription::SystemVerilogResolver& resolver)
{
    const auto& signal = design_.signal_info_[resolver.signal];
    // One net, or each element of an array of them, resolves alone.
    const auto element_count = resolver.elements.empty()
        ? std::size_t { 1 }
        : resolver.elements.size();
    if (signal.width == 0U || signal.width % element_count != 0U
        || (resolver.driver_count != 0U
            && signal.width > std::numeric_limits<std::size_t>::max()
                / resolver.driver_count)) {
        return false;
    }
    const auto element_width = signal.width / element_count;
    // With no driver the function receives an empty array (6.7.3); the
    // input register then keeps one unused element.
    const auto input = allocate_register(
        signal.width * std::max<std::size_t>(resolver.driver_count, 1U),
        signal.source_domain);
    const auto output = allocate_register(signal.width, signal.source_domain);
    const auto declaration
        = specialized_hir_unit_->find_declaration(resolver.function);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->callable
        || declaration->systemverilog->callable->formals.size() != 1U) {
        return false;
    }
    const auto formal = specialized_hir_unit_->find_declaration(
        declaration->systemverilog->callable->formals.front());
    const auto formal_type = formal && formal->systemverilog != nullptr
            && formal->systemverilog->type
        ? hir_systemverilog_container_type(*formal->systemverilog->type)
        : std::nullopt;
    if (!formal_type) {
        return false;
    }
    // The driver values, element 0 the first driver, become the dynamic
    // array argument the way a bit-stream cast fills one (6.24.3).
    ContainerType bit_queue;
    bit_queue.element_kind = ContainerElementKind::Packed;
    bit_queue.element_width = 1U;
    bit_queue.queue = true;
    const auto bits = allocate_container_register(bit_queue);
    const auto drivers = allocate_container_register(*formal_type);
    // The function's value for the drivers held in `driving` (none when
    // empty), as one packed element.
    const auto resolve = [&](const std::optional<RegisterId> driving)
        -> std::optional<RegisterId> {
        ContainerBitstream clear;
        clear.operation = ContainerBitstreamOperation::clear;
        clear.bits = bits;
        process_.operations.emplace_back(clear);
        if (driving) {
            ContainerBitstream append;
            append.operation = ContainerBitstreamOperation::append_value;
            append.bits = bits;
            append.value = *driving;
            process_.operations.emplace_back(append);
        }
        ContainerBitstream unpack;
        unpack.operation = ContainerBitstreamOperation::to_container;
        unpack.bits = bits;
        unpack.container = drivers;
        process_.operations.emplace_back(unpack);
        auto result = lower_hir_systemverilog_resolution_call(
            resolver.function, HirStaticContainerValue { drivers, *formal_type });
        if (!result) {
            return std::nullopt;
        }
        auto value = result->packed;
        if (result->container) {
            // An unpacked result (a structure, an array) becomes the net's
            // packed value in the same order.
            ContainerBitstream restart;
            restart.operation = ContainerBitstreamOperation::clear;
            restart.bits = bits;
            process_.operations.emplace_back(restart);
            ContainerBitstream pack;
            pack.operation = ContainerBitstreamOperation::append_container;
            pack.bits = bits;
            pack.container = result->container->value;
            process_.operations.emplace_back(pack);
            const auto packed = allocate_register(
                element_width, signal.source_domain);
            ContainerBitstream extract;
            extract.operation = ContainerBitstreamOperation::to_value;
            extract.bits = bits;
            extract.value = packed;
            extract.slice = static_cast<std::uint32_t>(element_width);
            process_.operations.emplace_back(extract);
            value = packed;
        }
        if (!value || register_width(*value) != element_width) {
            return std::nullopt;
        }
        return value;
    };
    if (resolver.elements.empty()) {
        const auto value = resolve(resolver.driver_count != 0U
                ? std::optional { input }
                : std::nullopt);
        if (!value) {
            return false;
        }
        process_.operations.emplace_back(CopyRegister { output, *value });
        hir_vhdl_resolver_registers_ = std::pair { input, output };
        return true;
    }
    // Each element from the drivers that drive it, the first driver's value
    // in the most significant position of `input`, and its leftmost element
    // in the most significant bits of each value.
    std::vector<RegisterId> results;
    results.reserve(element_count);
    for (std::size_t element = 0U; element < element_count; ++element) {
        std::vector<RegisterId> parts;
        for (const auto driver : resolver.elements[element]) {
            if (driver >= resolver.driver_count) {
                return false;
            }
            const auto part
                = allocate_register(element_width, signal.source_domain);
            process_.operations.emplace_back(Extract { part, input,
                static_cast<std::uint32_t>(
                    (resolver.driver_count - 1U - driver) * signal.width
                    + (element_count - 1U - element) * element_width),
                static_cast<std::uint32_t>(element_width) });
            parts.push_back(part);
        }
        std::optional<RegisterId> driving;
        if (parts.size() == 1U) {
            driving = parts.front();
        } else if (!parts.empty()) {
            const auto joined = allocate_register(
                element_width * parts.size(), signal.source_domain);
            process_.operations.emplace_back(Concatenate { joined,
                std::move(parts),
                static_cast<std::uint32_t>(element_width * resolver.elements[element].size()) });
            driving = joined;
        }
        const auto value = resolve(driving);
        if (!value) {
            return false;
        }
        results.push_back(*value);
    }
    if (results.size() == 1U) {
        process_.operations.emplace_back(CopyRegister { output, results.front() });
    } else {
        process_.operations.emplace_back(Concatenate { output,
            std::move(results), static_cast<std::uint32_t>(signal.width) });
    }
    hir_vhdl_resolver_registers_ = std::pair { input, output };
    return true;
}

// A call of resolution function `function` with `drivers` as its one
// dynamic array argument, framed as any call of it (13.4).
std::optional<Lowerer::HirFunctionCallValue>
Lowerer::lower_hir_systemverilog_resolution_call(
    const semantic::DeclarationId function,
    const HirStaticContainerValue& drivers)
{
    const auto declaration = specialized_hir_unit_->find_declaration(function);
    if (!declaration || declaration->systemverilog == nullptr
        || !declaration->systemverilog->callable
        || !declaration->systemverilog->callable->function
        || declaration->systemverilog->callable->formals.size() != 1U
        || !declaration->systemverilog->nested_scope) {
        return std::nullopt;
    }
    const auto type = hir_callable_type(function);
    if (!type || type->string) {
        return std::nullopt;
    }
    const auto formal = declaration->systemverilog->callable->formals.front();
    const std::string callable_key
        = "sv-resolver:" + std::to_string(function.value());
    auto frame_index = hir_callable_frames_.size();
    CallableKeyReservation key_reservation;
    if (const auto found = hir_callable_indices_.find(callable_key);
        found != hir_callable_indices_.end()) {
        frame_index = found->second;
    } else {
        hir_callable_indices_.emplace(callable_key, frame_index);
        key_reservation.reserve(hir_callable_indices_, callable_key);
        HirCallableFrame frame;
        frame.declaration = function;
        frame.scope = *declaration->systemverilog->nested_scope;
        frame.type = *type;
        frame.formals = { formal };
        frame.memo_key = callable_key;
        frame.debug_name = declaration->systemverilog->name;
        frame.function = true;
        bind_hir_instance_parameters(frame);
        if (type->container) {
            frame.container_result = allocate_container_register(
                *type->container);
            frame.container_result_default = allocate_container_register(
                *type->container);
            frame.invocation_containers.push_back(*frame.container_result);
        } else {
            frame.result = allocate_register(type->width, type->domain);
            frame.invocation_registers.push_back(frame.result);
        }
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (frame.invocation_identity == 0U) {
            frame.invocation_identity = next_callable_invocation_identity_++;
        }
        frame.argument_is_string.push_back(false);
        frame.argument_is_container.push_back(true);
        frame.arguments.push_back({ });
        frame.string_arguments.push_back({ });
        frame.container_arguments.push_back(
            allocate_container_register(drivers.type));
        frame.container_output_defaults.push_back(std::nullopt);
        frame.invocation_containers.push_back(
            frame.container_arguments.back());
        frame.directions.push_back(frontend::PortDirection::Input);
        frame.allocated = true;
        inherit_enclosing_hir_frame_facts(frame);
        hir_callable_frames_.push_back(std::move(frame));
        key_reservation.commit();
        if (!allocate_hir_static_callable_declarations(frame_index)) {
            return std::nullopt;
        }
    }
    auto& frame = hir_callable_frames_[frame_index];
    if (frame.type.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_registers,
            frame.invocation_strings,
            frame.invocation_containers,
            true,
        });
        if (!frame.snapshot_complete) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }
    if (frame.type.container) {
        if (!frame.container_result || !frame.container_result_default) {
            return std::nullopt;
        }
        if (frame.type.automatic) {
            process_.operations.emplace_back(CopyContainerRegister {
                *frame.container_result, *frame.container_result_default });
        }
    } else {
        process_.operations.emplace_back(LoadConstant {
            frame.result,
            callable_default_value(frame.type.width, frame.type.domain),
        });
    }
    process_.operations.emplace_back(CopyContainerRegister {
        frame.container_arguments.front(), drivers.value });
    transfer_hir_static_formals(frame, true);
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0U),
        static_cast<InstructionIndex>(call_site + 1U),
        { },
    });
    transfer_hir_static_formals(frame, false);
    if (!frame.target) {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_hir_callables_.push_back(frame_index);
    }
    HirFunctionCallValue result;
    std::vector<RegisterId> preserve_packed;
    std::vector<ContainerRegisterId> preserve_containers;
    if (frame.type.container) {
        const auto destination = allocate_container_register(
            *frame.type.container);
        process_.operations.emplace_back(CopyContainerRegister {
            destination, *frame.container_result });
        preserve_containers.push_back(destination);
        result.container
            = HirStaticContainerValue { destination, *frame.type.container };
    } else {
        const auto destination = allocate_register(
            frame.type.width, frame.type.domain);
        process_.operations.emplace_back(CopyRegister {
            destination, frame.result });
        preserve_packed.push_back(destination);
        result.packed = destination;
    }
    if (frame.type.automatic) {
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::move(preserve_packed),
            { },
            std::move(preserve_containers),
        });
    }
    return result;
}

} // namespace fsim::elaboration
