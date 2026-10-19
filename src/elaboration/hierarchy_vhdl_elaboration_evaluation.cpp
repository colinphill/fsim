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
    ContainerMap& container_objects)
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

} // namespace fsim::elaboration
