// SPDX-License-Identifier: Apache-2.0
// Elaboration-time evaluation of VHDL expressions that do not fold
// statically, such as a signal's initial value given by a function with
// loops (IEEE 1076-2008 14.4.2.5, 6.4.2.3): the expression is lowered into
// a one-shot program and run by the SimIR interpreter against the objects
// elaborated so far.
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

void trace_evaluation(const std::string_view stage, const std::string_view detail)
{
    if (std::getenv("FSIM_HIR_LOWER_TRACE_FAILURES") != nullptr) {
        std::cerr << "[fsim-hir-lower] vhdl-elaboration-evaluation " << stage
                  << ": " << detail << '\n';
    }
}

// The one-shot program reads the values elaborated so far. A resolved or
// converted signal's resolver and conversion programs run in the design's
// own processes, which this interpreter does not have.
void add_elaboration_signals(runtime::simir::Interpreter& interpreter,
    const std::vector<runtime::simir::Signal>& signals)
{
    for (const auto& signal : signals) {
        if (!signal.vhdl_resolver && signal.vhdl_conversion_drivers.empty()) {
            static_cast<void>(interpreter.add_signal(signal));
            continue;
        }
        auto value = signal;
        value.vhdl_resolver.reset();
        value.vhdl_conversion_drivers.clear();
        static_cast<void>(interpreter.add_signal(std::move(value)));
    }
}

// Whether a one-shot program's value depends only on its operations and
// the values of the signals it names: it covers nothing and reads no
// container object or string, which the run takes from the whole design.
// An assertion or report changes only the run's own counters, as the run
// has no report hook; a failure throws, and a failed run is not kept.
bool elaboration_program_memoizable(const runtime::simir::Process& process)
{
    using namespace runtime::simir;
    if (process.string_register_count != 0U) {
        trace_evaluation("memo", "string registers");
        return false;
    }
    if (!process_operations_shareable(process)) {
        trace_evaluation("memo", "operations without a structural comparison");
        return false;
    }
    if (std::ranges::any_of(process.operations,
            [](const Operation& operation) {
                return operation_get_if<ReadContainerObject>(&operation)
                    != nullptr
                    || operation_get_if<CodeCoverageHit>(&operation) != nullptr
                    || operation_get_if<CoverageControl>(&operation) != nullptr
                    || operation_get_if<CoverageAccess>(&operation) != nullptr;
            })) {
        trace_evaluation("memo", "container object or coverage operations");
        return false;
    }
    return true;
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
    lowerer.set_vhdl_lowering_index(
        &vhdl_lowering_index_);
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
        std::optional<PackedLogic4> value;
        static_cast<void>(run_compiled_vhdl_elaboration_program(
            interpreter, std::move(*process), destination, &value));
        diagnostics_.resize(diagnostics_before);
        return value;
    } catch (const std::exception& error) {
        trace_evaluation("run", error.what());
        diagnostics_.resize(diagnostics_before);
        return std::nullopt;
    }
}

std::optional<SignalId> HierarchyBuilder::run_compiled_vhdl_elaboration_program(
    runtime::simir::Interpreter& interpreter,
    runtime::simir::Process process,
    const std::optional<SignalId> result,
    std::optional<PackedLogic4>* const value)
{
    process.id = 0U;
    process.initialize = true;
    // The program usually names a few of the elaborated signals: number
    // those densely so the interpreter holds only them. An operation that
    // can name signals in ways the renumbering does not cover keeps every
    // signal under its own id.
    std::map<SignalId, SignalId> images;
    std::vector<SignalId> sources;
    std::vector<SignalId> sequence;
    const auto rename = [&](SignalId& signal) {
        if (signal >= design_.signals_.size()) {
            return false;
        }
        const auto [found, inserted] = images.try_emplace(
            signal, static_cast<SignalId>(sources.size()));
        if (inserted) {
            sources.push_back(signal);
        }
        signal = found->second;
        sequence.push_back(signal);
        return true;
    };
    auto compacted = process;
    auto containers = design_.container_objects_;
    bool compact = !compacted.switch_source && !compacted.switch_target
        && !compacted.switch_control;
    for (std::size_t index = 0;
         compact && index < compacted.operations.size(); ++index) {
        auto operation = compacted.operations.expanded(index);
        bool changed { };
        compact = remap_operation_signals(
            operation, rename, std::nullopt, &changed);
        if (compact && changed) {
            compacted.operations.replace(index, std::move(operation));
        }
    }
    for (auto& sensitivity : compacted.static_sensitivity) {
        compact = compact && rename(sensitivity.signal);
    }
    for (auto& driver : compacted.driver_regions) {
        compact = compact && rename(driver.signal);
    }
    // The signals the program itself names; the container objects' change
    // signals follow.
    const auto named_signals = sources.size();
    const auto named_sequence = sequence.size();
    for (auto& object : containers) {
        compact = compact
            && (!object.change_signal || rename(*object.change_signal));
    }
    auto result_image = result;
    compact = compact && (!result_image || rename(*result_image));
    // A value-only program whose result is a signal it names, reading no
    // shared variable, is looked up among the programs already run.
    const bool memoize = value != nullptr && compact && result_image
        && *result_image < named_signals
        && elaboration_program_memoizable(compacted)
        && std::ranges::none_of(
            std::span { sources }.first(named_signals),
            [&](const SignalId signal) {
                return vhdl_unprotected_shared_signals_.contains(signal);
            });
    sequence.resize(named_sequence);
    const auto memo_key = std::tuple { compacted.operations.size(),
        named_signals, compacted.register_count };
    const auto same_inputs
        = [&](const VhdlElaborationProgramMemo& entry) {
              for (std::size_t index = 0; index < named_signals; ++index) {
                  const auto& signal = design_.signals_[sources[index]];
                  if (entry.inputs[index].second != signal.value_kind
                      || !(entry.inputs[index].first
                          == signal.initial_value)) {
                      return false;
                  }
              }
              return true;
          };
    std::vector<runtime::simir::Signal> signals;
    if (compact) {
        signals.reserve(sources.size());
        for (const auto source : sources) {
            signals.push_back(design_.signals_[source]);
        }
    }
    if (memoize) {
        if (const auto bucket = vhdl_elaboration_program_memo_.find(memo_key);
            bucket != vhdl_elaboration_program_memo_.end()) {
            for (const auto& entry : bucket->second) {
                if (entry.result != *result_image
                    || entry.signal_sequence != sequence
                    || !same_inputs(entry)) {
                    continue;
                }
                // The signal sequences match, so equal operations name the
                // same signals.
                auto candidate = compacted;
                if (runtime::simir::share_process_operations(
                        entry.program, candidate, signals)) {
                    trace_evaluation("memo", "an earlier run's value");
                    *value = entry.value;
                    return result_image;
                }
            }
        }
    }
    std::optional<VhdlElaborationProgramMemo> memo_entry;
    if (memoize) {
        memo_entry = VhdlElaborationProgramMemo {
            compacted, sequence, { }, *result_image, PackedLogic4 { } };
        memo_entry->inputs.reserve(named_signals);
        for (std::size_t index = 0; index < named_signals; ++index) {
            memo_entry->inputs.emplace_back(
                signals[index].initial_value, signals[index].value_kind);
        }
    }
    if (compact) {
        runtime::simir::normalize_sensitivities(
            compacted.static_sensitivity);
        // A hint indexed by the sorted sensitivities; the interpreter
        // ignores it.
        compacted.static_trigger_regions = { };
        add_elaboration_signals(interpreter, signals);
        process = std::move(compacted);
    } else {
        add_elaboration_signals(interpreter, design_.signals_);
        containers = design_.container_objects_;
        result_image = result;
    }
    for (const auto& object : design_.string_objects_) {
        static_cast<void>(interpreter.add_string_object(object));
    }
    for (auto& object : containers) {
        static_cast<void>(interpreter.add_container_object(std::move(object)));
    }
    static_cast<void>(interpreter.add_process(std::move(process)));
    interpreter.start();
    static_cast<void>(interpreter.run());
    keep_compiled_vhdl_elaboration_state(
        interpreter, compact ? &images : nullptr);
    if (value != nullptr && result_image) {
        *value = interpreter.signal_value_snapshot(*result_image);
        constexpr std::size_t maximum_memo_entries { 4096U };
        if (memo_entry
            && vhdl_elaboration_program_memo_entries_
                < maximum_memo_entries) {
            memo_entry->value = **value;
            vhdl_elaboration_program_memo_[memo_key].push_back(
                std::move(*memo_entry));
            ++vhdl_elaboration_program_memo_entries_;
        }
    }
    return result_image;
}

void HierarchyBuilder::keep_compiled_vhdl_elaboration_state(
    const runtime::simir::Interpreter& interpreter,
    const std::map<SignalId, SignalId>* const images)
{
    // Elaboration calls a function once for each evaluation; the state an
    // impure function leaves in a shared variable is seen by later
    // elaboration and by simulation (IEEE 1076-2008 14.4.2.1, 14.4.2.3).
    for (const auto& object : design_.vhdl_protected_object_info_) {
        for (const auto& member : object.members) {
            if (member.storage < design_.container_objects_.size()) {
                design_.container_objects_[member.storage].initial_value
                    = interpreter.container_object_value_snapshot(
                        member.storage);
            }
        }
    }
    for (const auto signal : vhdl_unprotected_shared_signals_) {
        if (signal >= design_.signals_.size()) {
            continue;
        }
        // A signal the program does not name keeps its value.
        const auto image = images == nullptr
            ? std::optional { signal }
            : images->contains(signal)
            ? std::optional { images->at(signal) }
            : std::nullopt;
        if (image) {
            design_.signals_[signal].initial_value
                = interpreter.signal_value_snapshot(*image);
        }
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

std::optional<std::string> HierarchyBuilder::evaluate_compiled_vhdl_string(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression,
    const std::string& path,
    SignalMap& signals,
    ReadOnlySignalSet& read_only_signals,
    ContainerMap& container_objects)
{
    const auto diagnostics_before = diagnostics_.size();
    const auto processes_before = design_.process_count();
    // The value runs into a scratch string object that is removed again.
    const auto scratch = static_cast<StringObjectId>(
        design_.string_objects_.size());
    design_.string_objects_.push_back(
        StringObject { path + ".@elaboration-string", std::string { } });
    const auto remove_scratch = [&] {
        if (design_.string_objects_.size() != scratch + 1U) {
            throw std::logic_error(
                "VHDL elaboration-time evaluation created string objects");
        }
        design_.string_objects_.pop_back();
    };
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
    lowerer.set_vhdl_lowering_index(
        &vhdl_lowering_index_);
    lowerer.set_package_constant_signal(
        [&](const semantic::DeclarationId constant) {
            return compiled_vhdl_package_constant_signal(
                specialization, constant);
        });
    auto process = lowerer.lower_hir_elaboration_string(
        expression, scratch, path);
    if (!process || lowerer.has_generated_processes()
        || design_.process_count() != processes_before) {
        for (auto index = diagnostics_before; index < diagnostics_.size();
             ++index) {
            trace_evaluation("string lowering", diagnostics_[index].message);
        }
        diagnostics_.resize(diagnostics_before);
        remove_scratch();
        return std::nullopt;
    }
    std::optional<std::string> value;
    try {
        runtime::simir::Interpreter interpreter;
        static_cast<void>(run_compiled_vhdl_elaboration_program(
            interpreter, std::move(*process), std::nullopt));
        value = interpreter.string_object_value(scratch);
    } catch (const std::exception& error) {
        trace_evaluation("string run", error.what());
    }
    diagnostics_.resize(diagnostics_before);
    remove_scratch();
    return value;
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
    lowerer.set_vhdl_lowering_index(
        &vhdl_lowering_index_);
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
