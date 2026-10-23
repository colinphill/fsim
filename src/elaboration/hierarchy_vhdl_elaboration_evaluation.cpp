// SPDX-License-Identifier: Apache-2.0
// Elaboration-time evaluation of VHDL expressions that do not fold
// statically, such as a signal's initial value given by a function with
// loops (IEEE 1076-2008 14.4.2.5, 6.4.2.3): the expression is lowered into
// a one-shot program and run by the SimIR interpreter against the objects
// elaborated so far.
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <string_view>

namespace fsim::elaboration {
namespace {

void trace_evaluation(const std::string_view stage, const std::string_view detail)
{
    if (std::getenv("FSIM_HIR_LOWER_TRACE_FAILURES") != nullptr) {
        std::cerr << "[fsim-hir-lower] vhdl-elaboration-evaluation " << stage
                  << ": " << detail << '\n';
    }
}

} // namespace

std::optional<PackedLogic4> HierarchyBuilder::evaluate_compiled_vhdl_value(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    const SignalId destination,
    const std::optional<semantic::vhdl::SubtypeIndication>& subtype,
    const std::string& path,
    SignalMap& signals,
    ReadOnlySignalSet& read_only_signals,
    ContainerMap& container_objects,
    const semantic::CompiledBindingFrame* const generic_bindings)
{
    if (destination >= design_.signals_.size()) {
        return std::nullopt;
    }
    // A failed attempt leaves no diagnostics or generated processes.
    const auto diagnostics_before = diagnostics_.size();
    const auto processes_before = design_.process_count();
    StringMap strings;
    ReadOnlyStringSet read_only_strings;
    ReadOnlyContainerSet read_only_containers;
    Lowerer lowerer {
        design_,
        signals,
        read_only_signals,
        strings,
        read_only_strings,
        container_objects,
        read_only_containers,
        diagnostics_,
    };
    lowerer.set_specialized_hir_unit(&specialization);
    lowerer.set_package_constant_signal(
        [&](const semantic::DeclarationId constant) {
            return compiled_vhdl_package_constant_signal(
                specialization, constant);
        });
    if (generic_bindings != nullptr) {
        lowerer.set_base_generic_binding_frame(*generic_bindings);
    }
    auto process = lowerer.lower_hir_input_actual(expression, destination,
        frontend::Language::Vhdl2008, path, 0U, subtype);
    if (!process || lowerer.has_generated_processes()
        || design_.process_count() != processes_before) {
        for (auto index = diagnostics_before; index < diagnostics_.size();
             ++index) {
            trace_evaluation("lowering", diagnostics_[index].message);
        }
        if (!process) {
            trace_evaluation("lowering", "no process");
        }
        diagnostics_.resize(diagnostics_before);
        return std::nullopt;
    }
    try {
        runtime::simir::Interpreter interpreter;
        for (const auto& signal : design_.signals_) {
            static_cast<void>(interpreter.add_signal(signal));
        }
        for (const auto& object : design_.string_objects_) {
            static_cast<void>(interpreter.add_string_object(object));
        }
        for (const auto& object : design_.container_objects_) {
            static_cast<void>(interpreter.add_container_object(object));
        }
        process->id = 0U;
        process->initialize = true;
        static_cast<void>(interpreter.add_process(std::move(*process)));
        interpreter.start();
        static_cast<void>(interpreter.run());
        auto value = interpreter.signal_value_snapshot(destination);
        diagnostics_.resize(diagnostics_before);
        return value;
    } catch (const std::exception& error) {
        trace_evaluation("run", error.what());
        diagnostics_.resize(diagnostics_before);
        return std::nullopt;
    }
}

std::optional<std::int64_t> HierarchyBuilder::evaluate_compiled_vhdl_integer(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    const std::size_t width,
    const std::string& path,
    SignalMap& signals,
    ReadOnlySignalSet& read_only_signals,
    ContainerMap& container_objects,
    const frontend::ValueDomain domain,
    const semantic::CompiledBindingFrame* const generic_bindings)
{
    if (generic_bindings == nullptr) {
        if (const auto folded
            = specialization.evaluate_integral_expression(expression)) {
            return folded;
        }
    }
    const bool boolean = domain == frontend::ValueDomain::Boolean;
    if (width == 0U || width > 64U) {
        return std::nullopt;
    }
    // The value runs into a scratch INTEGER signal that is removed again;
    // nothing else refers to it.
    const auto scratch = static_cast<SignalId>(design_.signals_.size());
    SignalInfo info;
    info.id = scratch;
    info.name = path + ".@elaboration-integer";
    info.width = width;
    info.type_name = boolean ? "boolean" : "integer";
    info.source_domain = domain;
    info.is_signed = !boolean;
    design_.signal_info_.push_back(std::move(info));
    design_.signals_.push_back(runtime::simir::Signal {
        path + ".@elaboration-integer",
        PackedLogic4 { width },
        ResolutionKind::none,
        value_kind(domain),
        std::nullopt,
        { StrengthRank::pull, StrengthRank::pull },
        std::nullopt,
        std::nullopt,
        frontend::SystemVerilogScalarKind::None,
    });
    const auto value = evaluate_compiled_vhdl_value(specialization,
        expression, scratch, std::nullopt, path, signals, read_only_signals,
        container_objects, generic_bindings);
    if (design_.signals_.size() != scratch + 1U
        || design_.signal_info_.size() != scratch + 1U) {
        throw std::logic_error(
            "VHDL elaboration-time evaluation created signals");
    }
    design_.signals_.pop_back();
    design_.signal_info_.pop_back();
    if (!value || value->width() != width) {
        return std::nullopt;
    }
    const auto bits = boolean ? value->known_unsigned_value()
                              : std::optional<std::uint64_t> { };
    const auto result = !boolean ? value->known_signed_value()
        : bits ? std::optional { static_cast<std::int64_t>(*bits) }
               : std::nullopt;
    if (!result) {
        trace_evaluation("run", "integer value has unknown bits");
    }
    return result;
}

std::optional<semantic::vhdl::SubtypeIndication>
HierarchyBuilder::compiled_vhdl_initialized_object_subtype(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId initializer,
    const semantic::vhdl::SubtypeIndication& declared,
    const semantic::ScopeId scope,
    SignalMap& signals,
    ReadOnlySignalSet& read_only_signals,
    ContainerMap& container_objects)
{
    const auto diagnostics_before = diagnostics_.size();
    StringMap strings;
    ReadOnlyStringSet read_only_strings;
    ReadOnlyContainerSet read_only_containers;
    Lowerer lowerer {
        design_,
        signals,
        read_only_signals,
        strings,
        read_only_strings,
        container_objects,
        read_only_containers,
        diagnostics_,
    };
    lowerer.set_specialized_hir_unit(&specialization);
    lowerer.set_package_constant_signal(
        [&](const semantic::DeclarationId constant) {
            return compiled_vhdl_package_constant_signal(
                specialization, constant);
        });
    auto subtype = lowerer.hir_vhdl_initialized_object_subtype(
        initializer, declared, scope);
    diagnostics_.resize(diagnostics_before);
    if (!subtype) {
        trace_evaluation("shape", "initial value has no known shape");
    }
    return subtype;
}
} // namespace fsim::elaboration
