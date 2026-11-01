// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include "fsim/elaboration/coverage_hir_points.hpp"
#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/frontend/coverage_source_control.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    using runtime::simir::Binary;
    using runtime::simir::BinaryOperator;
    using runtime::simir::Branch;
    using runtime::simir::CodeCoverageHit;
    using runtime::simir::CodeCoverageToggle;
    using runtime::simir::CopyRegister;
    using runtime::simir::InstructionIndex;
    using runtime::simir::Jump;
    using runtime::simir::LoadConstant;
    using runtime::simir::Process;
    using runtime::simir::ReadSignal;
    using runtime::simir::UnknownBranchPolicy;
    using runtime::simir::WaitSensitivity;

    // The coverage source that holds a declaration, and its span there.
    struct MonitoredDeclaration {
        const CoverageHirSource* source { };
        frontend::SourceSpan span;
        frontend::CodeCoverageSourceSpan offsets;
    };

    std::optional<MonitoredDeclaration> monitored_declaration(
        const semantic::CompiledDesign& compiled,
        const CoverageHirContext& coverage,
        const semantic::SourceSpanId declaration)
    {
        if (!declaration.valid()) {
            return std::nullopt;
        }
        const auto span = compiled_source_span(compiled, declaration);
        const auto physical = frontend::physical_source(span);
        const auto source = std::ranges::find_if(coverage.sources,
            [&](const CoverageHirSource& item) {
                return item.inventory.source_name == physical;
            });
        if (source == coverage.sources.end()) {
            return std::nullopt;
        }
        return MonitoredDeclaration { &*source, span,
            { span.begin.offset, span.end.offset } };
    }

    bool monitor_excluded(const MonitoredDeclaration& declaration,
        const frontend::CoverageSourceMetric metric)
    {
        return frontend::coverage_source_exclusion_at(
                   declaration.source->exclusions, metric,
                   declaration.span.begin.offset)
            != nullptr;
    }

    // A monitor process of an instance, sensitive to any change of one
    // signal.
    Process monitor_process(const std::size_t id, std::string name,
        const frontend::CodeCoverageLanguage language, const SignalId signal,
        const std::size_t registers,
        const runtime::simir::ValueKind value_kind)
    {
        Process monitor;
        monitor.id = static_cast<runtime::simir::ProcessId>(id);
        monitor.name = std::move(name);
        monitor.scheduling_domain
            = language == frontend::CodeCoverageLanguage::Vhdl
            ? runtime::simir::ProcessSchedulingDomain::generic
            : runtime::simir::ProcessSchedulingDomain::systemverilog;
        monitor.static_sensitivity.push_back(
            { signal, runtime::simir::EdgeKind::any });
        monitor.register_count = registers;
        monitor.register_value_kinds.assign(registers, value_kind);
        return monitor;
    }

    // A toggle-covered object: one bit-valued scalar or vector. A memory
    // (an array of words) is reported as excluded by default.
    struct ToggleCandidate {
        std::string name;
        semantic::SourceSpanId source;
        std::optional<SignalId> signal;
        bool memory { };
    };

    bool toggle_domain(const frontend::ValueDomain domain) noexcept
    {
        return domain == frontend::ValueDomain::Bit2
            || domain == frontend::ValueDomain::Logic4
            || domain == frontend::ValueDomain::Logic9
            || domain == frontend::ValueDomain::Boolean;
    }

    std::string lower_ascii(std::string text)
    {
        for (auto& character : text) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return text;
    }

    // A VHDL enumeration other than a character type or BOOLEAN has an
    // encoding the language does not show, so its bits do not toggle.
    bool vhdl_hidden_encoding(const SignalInfo& info)
    {
        if (info.enumeration_literals.empty()) {
            return false;
        }
        const bool characters = std::ranges::all_of(info.enumeration_literals,
            [](const std::string& literal) {
                return literal.starts_with('\'');
            });
        if (characters) {
            return false;
        }
        std::vector<std::string> spellings;
        for (const auto& literal : info.enumeration_literals) {
            spellings.push_back(lower_ascii(literal));
        }
        std::ranges::sort(spellings);
        return spellings != std::vector<std::string> { "false", "true" };
    }

    // A VHDL array of words (an element wider than one bit), or of more
    // than one dimension, or of elements without bits, is a memory.
    bool vhdl_memory(const SignalInfo& info)
    {
        if (!info.vhdl_array) {
            return false;
        }
        const auto& array = *info.vhdl_array;
        if (array.dimensions.size() > 1U || !toggle_domain(array.element_domain)) {
            return true;
        }
        if (array.dimensions.empty() || !array.dimensions.front().range) {
            return false;
        }
        const auto& range = *array.dimensions.front().range;
        const auto length = range.left > range.right
            ? static_cast<std::uint64_t>(range.left - range.right) + 1U
            : static_cast<std::uint64_t>(range.right - range.left) + 1U;
        return length != info.width;
    }

    bool toggle_signal(const SignalInfo& info, const bool vhdl)
    {
        if (info.width == 0U || info.width > kCoverageToggleMaximumWidth
            || info.systemverilog_scalar
                != frontend::SystemVerilogScalarKind::None
            || info.vhdl_access || info.vhdl_physical
            || (vhdl && vhdl_hidden_encoding(info))) {
            return false;
        }
        if (info.vhdl_array) {
            return !vhdl_memory(info);
        }
        return toggle_domain(info.source_domain);
    }

    // The parsed ports and signals of a VHDL architecture's instance.
    template <typename Visit>
    void visit_vhdl_toggle_declarations(const semantic::CompiledDesign& compiled,
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture, Visit&& visit)
    {
        const auto& origins = compiled.semantics.origins();
        for (const auto* unit : { &entity, &architecture }) {
            for (const auto id : unit->declarations) {
                const auto found = compiled.find_declaration(id);
                if (!found || found->vhdl == nullptr) {
                    continue;
                }
                const auto& declaration = *found->vhdl;
                using Form = semantic::vhdl::DeclarationForm;
                const auto origin = declaration.origin;
                if ((declaration.form == Form::port
                        || declaration.form == Form::signal)
                    && origin.valid() && origin.value() < origins.size()
                    && origins[origin.value()].kind
                        == semantic::OriginKind::parsed) {
                    visit(declaration);
                }
            }
        }
    }

} // namespace

void HierarchyBuilder::record_vhdl_toggle_coverage_signals(
    const std::size_t specialization, const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture, const SignalMap& signals)
{
    if (coverage_ == nullptr) {
        return;
    }
    auto& recorded = vhdl_toggle_coverage_signals_[specialization];
    visit_vhdl_toggle_declarations(*compiled_, entity, architecture,
        [&](const semantic::vhdl::Declaration& declaration) {
            const auto name = lower_ascii(declaration.name);
            const auto found = signals.find(name);
            if (found != signals.end()) {
                recorded.emplace(name, found->second);
            }
        });
}

void HierarchyBuilder::append_toggle_coverage_monitors()
{
    if (coverage_ == nullptr) {
        return;
    }
    const auto& origins = compiled_->semantics.origins();
    const auto parsed = [&](const semantic::OriginId origin) {
        return origin.valid() && origin.value() < origins.size()
            && origins[origin.value()].kind == semantic::OriginKind::parsed;
    };
    for (std::size_t index = 0U; index < design_.specializations_.size();
        ++index) {
        auto& specialization = design_.specializations_[index];
        if (!specialization.source_unit) {
            continue;
        }
        const auto unit = compiled_->find_unit(*specialization.source_unit);
        if (!unit) {
            continue;
        }
        std::vector<ToggleCandidate> candidates;
        frontend::CodeCoverageLanguage language {
            frontend::CodeCoverageLanguage::SystemVerilog
        };
        if (specialization.language == frontend::Language::SystemVerilog2017
            && unit->systemverilog != nullptr
            && covered_systemverilog_unit(unit->systemverilog->kind)) {
            for (const auto id : unit->systemverilog->declarations) {
                const auto found = compiled_->find_declaration(id);
                if (!found || found->systemverilog == nullptr) {
                    continue;
                }
                const auto& declaration = *found->systemverilog;
                using Form = semantic::sv::DeclarationForm;
                if ((declaration.form != Form::port
                        && declaration.form != Form::net
                        && declaration.form != Form::variable)
                    || !parsed(declaration.origin)) {
                    continue;
                }
                candidates.push_back({ declaration.name, declaration.source,
                    std::nullopt,
                    declaration.type
                        && !declaration.type->unpacked_dimensions.empty() });
            }
        } else if (specialization.language == frontend::Language::Vhdl2008
            && unit->vhdl != nullptr
            && unit->vhdl->kind == semantic::vhdl::UnitKind::architecture) {
            language = frontend::CodeCoverageLanguage::Vhdl;
            const auto entity = compiled_->vhdl_primary_unit(*unit->vhdl);
            const auto recorded = vhdl_toggle_coverage_signals_.find(index);
            if (!entity || recorded == vhdl_toggle_coverage_signals_.end()) {
                continue;
            }
            // A port bound directly to its actual has no name of its own
            // in the design's signal index; the instance's bindings name it.
            visit_vhdl_toggle_declarations(*compiled_, **entity, *unit->vhdl,
                [&](const semantic::vhdl::Declaration& declaration) {
                    auto name = lower_ascii(declaration.name);
                    const auto signal = recorded->second.find(name);
                    if (signal != recorded->second.end()) {
                        candidates.push_back({ std::move(name),
                            declaration.source, signal->second, false });
                    }
                });
        } else {
            continue;
        }

        // Objects declared together share a span; each takes its position.
        std::map<std::uint32_t, std::uint32_t> declared_together;
        for (const auto& candidate : candidates) {
            if (!candidate.source.valid()) {
                continue;
            }
            const auto position
                = declared_together[candidate.source.value()]++;
            const auto declaration = monitored_declaration(
                *compiled_, *coverage_, candidate.source);
            if (!declaration) {
                continue;
            }
            const auto excluded = [&](const std::string_view reason) {
                specialization.coverage_toggle_objects.push_back({
                    candidate.source, 0U, position, std::string { reason },
                    candidate.name });
            };
            if (candidate.memory) {
                excluded(kCoverageToggleMemoryReason);
                continue;
            }
            auto signal = candidate.signal;
            if (!signal) {
                const auto named = design_.signal_by_name_.find(
                    specialization.instance + "." + candidate.name);
                if (named != design_.signal_by_name_.end()) {
                    signal = named->second;
                }
            }
            if (!signal || *signal >= design_.signal_info_.size()) {
                continue;
            }
            const auto& info = design_.signal_info_[*signal];
            if (!toggle_signal(
                    info, language == frontend::CodeCoverageLanguage::Vhdl)) {
                if (vhdl_memory(info)) {
                    excluded(kCoverageToggleMemoryReason);
                }
                continue;
            }
            if (monitor_excluded(
                    *declaration, frontend::CoverageSourceMetric::Toggle)) {
                excluded("excluded by a source comment");
                continue;
            }
            CodeCoverageToggle toggle { 0U, 1U, { }, { } };
            toggle.points.reserve(2U * info.width);
            bool valid = true;
            for (std::size_t bit = 0U; bit < info.width && valid; ++bit) {
                for (const auto kind : {
                         frontend::CodeCoverageConstructKind::ToggleRise,
                         frontend::CodeCoverageConstructKind::ToggleFall }) {
                    const auto identity
                        = frontend::make_code_coverage_point_identity(
                            declaration->source->inventory.identity,
                            language, kind, declaration->offsets,
                            coverage_object_ordinal_base(position) + bit
                                + 1U);
                    if (!identity.ok()) {
                        valid = false;
                        break;
                    }
                    toggle.points.push_back(*identity.identity);
                }
            }
            if (!valid) {
                continue;
            }

            // Sample the value at initialization, then count each change.
            auto monitor = monitor_process(design_.process_count(),
                specialization.instance + "." + candidate.name
                    + ".$toggle_coverage",
                language, *signal, 2U, design_.signals_[*signal].value_kind);
            monitor.operations.emplace_back(ReadSignal { 0U, *signal });
            monitor.operations.emplace_back(WaitSensitivity { });
            monitor.operations.emplace_back(ReadSignal { 1U, *signal });
            monitor.operations.emplace_back(std::move(toggle));
            monitor.operations.emplace_back(CopyRegister { 0U, 1U });
            monitor.operations.emplace_back(Jump { 1U });
            canonicalize_process_operations(monitor);
            specialization.processes.push_back(monitor.id);
            specialization.coverage_toggle_objects.push_back({ candidate.source,
                static_cast<std::uint32_t>(info.width), position,
                std::string { }, candidate.name });
            design_.append_process_record(std::move(monitor));
        }
    }
}

void HierarchyBuilder::append_fsm_coverage_monitors()
{
    if (coverage_ == nullptr) {
        return;
    }
    for (auto& specialization : design_.specializations_) {
        std::vector<CoverageFsmMachine> machines;
        for (const auto process : specialization.processes) {
            const auto drafts = coverage_fsm_drafts_.find(process);
            if (drafts == coverage_fsm_drafts_.end()) {
                continue;
            }
            for (const auto& draft : drafts->second) {
                merge_coverage_fsm_machine(machines, draft);
            }
        }
        const auto language
            = specialization.language == frontend::Language::Vhdl2008
            ? frontend::CodeCoverageLanguage::Vhdl
            : frontend::CodeCoverageLanguage::SystemVerilog;
        std::map<std::uint32_t, std::uint32_t> declared_together;
        for (auto& machine : machines) {
            machine.declared_together
                = declared_together[machine.declaration.value()]++;
            const auto base
                = coverage_object_ordinal_base(machine.declared_together);
            // A case over an enumeration-typed value that no arm assigns
            // is a decoder, not a state machine.
            if (machine.transitions.empty()
                || machine.signal >= design_.signal_info_.size()) {
                continue;
            }
            const auto width = design_.signal_info_[machine.signal].width;
            const auto value_kind = design_.signals_[machine.signal].value_kind;
            if (width == 0U || width > 64U) {
                continue;
            }
            const bool logic9 = value_kind == runtime::simir::ValueKind::logic9;
            const auto declaration = monitored_declaration(
                *compiled_, *coverage_, machine.declaration);
            if (!declaration) {
                continue;
            }
            const bool count_states = !monitor_excluded(
                *declaration, frontend::CoverageSourceMetric::FsmState);
            const bool count_transitions = !monitor_excluded(
                *declaration, frontend::CoverageSourceMetric::FsmTransition);
            if (!count_states && !count_transitions) {
                // Discovery reports its points as excluded.
                specialization.coverage_fsm_machines.push_back(machine);
                continue;
            }
            const auto states = machine.values.size();
            const auto point = [&](const frontend::CodeCoverageConstructKind kind,
                                   const std::uint64_t ordinal)
                -> std::optional<runtime::CodeCoveragePointId> {
                const auto identity = frontend::make_code_coverage_point_identity(
                    declaration->source->inventory.identity, language, kind,
                    declaration->offsets, ordinal);
                return identity.ok() ? std::optional { *identity.identity }
                                     : std::nullopt;
            };

            // Registers: the previous state, the current state, a state's
            // value, a comparison, and whether the current value is a state.
            auto monitor = monitor_process(design_.process_count(),
                specialization.instance + ".$fsm_coverage."
                    + std::to_string(machine.signal),
                language, machine.signal, 5U, value_kind);
            // The comparison and the flag are single bits of four values.
            monitor.register_value_kinds[3] = runtime::simir::ValueKind::logic4;
            monitor.register_value_kinds[4] = runtime::simir::ValueKind::logic4;
            // A state's value; a nine-valued state's elements are '0' or '1'.
            const auto state_constant = [&](const std::uint64_t value) {
                if (!logic9) {
                    return runtime::PackedLogic4::from_aval_bval(
                        width, value, 0U);
                }
                std::string elements(width, '0');
                for (std::size_t bit = 0U; bit < width; ++bit) {
                    if (((value >> bit) & 1U) != 0U) {
                        elements[width - 1U - bit] = '1';
                    }
                }
                return runtime::PackedLogic4::from_logic9_msb_string(elements);
            };
            auto& operations = monitor.operations;
            const auto here = [&] {
                return static_cast<InstructionIndex>(operations.size());
            };
            // A branch whose false target is set once that code is placed.
            using PendingBranch = std::pair<InstructionIndex, Branch>;
            // Branches to the next instruction when `reg` holds `value`.
            const auto when_equal = [&](const runtime::simir::RegisterId reg,
                                        const std::uint64_t value) {
                operations.emplace_back(
                    LoadConstant { 2U, state_constant(value) });
                operations.emplace_back(Binary {
                    BinaryOperator::case_equal, 3U, reg, 2U });
                const auto index = here();
                const Branch branch { 3U,
                    static_cast<InstructionIndex>(index + 1U), 0U,
                    UnknownBranchPolicy::when_false };
                operations.emplace_back(branch);
                return PendingBranch { index, branch };
            };
            const auto patch = [&](PendingBranch pending) {
                pending.second.when_false = here();
                operations[pending.first] = pending.second;
            };
            bool valid = true;
            const auto hit = [&](const frontend::CodeCoverageConstructKind kind,
                                 const runtime::CodeCoverageMetric metric,
                                 const std::uint64_t ordinal) {
                const auto identity = point(kind, ordinal);
                valid = valid && identity.has_value();
                operations.emplace_back(CodeCoverageHit {
                    identity.value_or(runtime::CodeCoveragePointId { }),
                    metric, runtime::CodeCoverageCounterId { 0U } });
            };
            const auto flag = [&](const bool value) {
                operations.emplace_back(LoadConstant { 4U,
                    runtime::PackedLogic4::from_aval_bval(
                        1U, value ? 1U : 0U, 0U) });
            };
            // Counts the state `reg` holds; with `mark`, also records in
            // register 4 whether it holds one.
            const auto count_state = [&](const runtime::simir::RegisterId reg,
                                         const bool mark) {
                if (!count_states && !mark) {
                    return;
                }
                if (mark) {
                    flag(false);
                }
                for (std::size_t state = 0U; state < states; ++state) {
                    const auto branch = when_equal(reg, machine.values[state]);
                    if (mark) {
                        flag(true);
                    }
                    if (count_states) {
                        hit(frontend::CodeCoverageConstructKind::FsmState,
                            runtime::CodeCoverageMetric::FsmState,
                            base + state + 1U);
                    }
                    patch(branch);
                }
            };

            // The state at initialization is visited; afterwards each change
            // visits a state and takes a transition. A change to or from a
            // value that is no state counts in no bin.
            operations.emplace_back(ReadSignal { 0U, machine.signal });
            count_state(0U, false);
            const auto waiting = here();
            operations.emplace_back(runtime::simir::WaitSensitivity { });
            operations.emplace_back(ReadSignal { 1U, machine.signal });
            operations.emplace_back(Binary {
                BinaryOperator::case_equal, 3U, 0U, 1U });
            PendingBranch unchanged { here(), Branch { 3U, 0U,
                static_cast<InstructionIndex>(here() + 1U),
                UnknownBranchPolicy::when_false } };
            operations.emplace_back(unchanged.second);
            count_state(1U, count_transitions);
            // From each state: its legal transitions, else the informational
            // other-transition bin when the new value is a state.
            std::vector<InstructionIndex> done;
            const auto finish = [&] {
                done.push_back(here());
                operations.emplace_back(Jump { 0U });
            };
            if (count_transitions) {
                std::size_t index = 0U;
                for (std::uint32_t from = 0U; from < states; ++from) {
                    const auto source = when_equal(0U, machine.values[from]);
                    for (; index < machine.transitions.size()
                         && machine.transitions[index].from == from;
                         ++index) {
                        const auto to = machine.transitions[index].to;
                        const auto target = when_equal(1U, machine.values[to]);
                        hit(frontend::CodeCoverageConstructKind::FsmTransition,
                            runtime::CodeCoverageMetric::FsmTransition,
                            base + std::uint64_t { from } * states + to
                                + 1U);
                        finish();
                        patch(target);
                    }
                    const auto index_of_state = here();
                    const Branch is_state { 4U,
                        static_cast<InstructionIndex>(index_of_state + 1U), 0U,
                        UnknownBranchPolicy::when_false };
                    operations.emplace_back(is_state);
                    hit(frontend::CodeCoverageConstructKind::FsmOtherTransition,
                        runtime::CodeCoverageMetric::FsmTransition,
                        base + std::uint64_t { states } * states + 1U);
                    patch({ index_of_state, is_state });
                    finish();
                    patch(source);
                }
            }
            for (const auto jump : done) {
                operations[jump] = Jump { here() };
            }
            unchanged.second.when_true = here();
            operations[unchanged.first] = unchanged.second;
            operations.emplace_back(CopyRegister { 0U, 1U });
            operations.emplace_back(Jump { waiting });
            if (!valid) {
                continue;
            }
            canonicalize_process_operations(monitor);
            specialization.processes.push_back(monitor.id);
            specialization.coverage_fsm_machines.push_back(machine);
            design_.append_process_record(std::move(monitor));
        }
    }
}

bool HierarchyBuilder::covered_concurrent_assignment(
    const semantic::SpecializedHirUnit& specialized,
    const semantic::StatementId statement) const
{
    if (coverage_ == nullptr) {
        return false;
    }
    const auto source = specialized.find_statement(statement);
    if (!source) {
        return false;
    }
    const auto& origins = compiled_->semantics.origins();
    const auto parsed = [&](const semantic::OriginId origin) {
        return origin.valid() && origin.value() < origins.size()
            && origins[origin.value()].kind == semantic::OriginKind::parsed;
    };
    if (source->systemverilog != nullptr) {
        const auto& assignment = *source->systemverilog;
        return assignment.kind == semantic::sv::StatementKind::assignment
            && assignment.assignment_kind
                == semantic::sv::AssignmentKind::continuous
            && parsed(assignment.origin);
    }
    if (source->vhdl != nullptr) {
        // Conditional and selected assignments are if and case statements.
        using Kind = semantic::vhdl::StatementKind;
        const auto& assignment = *source->vhdl;
        return (assignment.kind == Kind::signal_assignment
                   || (assignment.kind == Kind::conditional
                       && assignment.conditional_assignment)
                   || assignment.kind == Kind::selection)
            && parsed(assignment.origin);
    }
    return false;
}

void HierarchyBuilder::record_coverage_concurrent_statements()
{
    if (coverage_ == nullptr) {
        return;
    }
    for (auto& specialization : design_.specializations_) {
        for (const auto process : specialization.processes) {
            const auto found = coverage_concurrent_statements_.find(process);
            if (found != coverage_concurrent_statements_.end()) {
                specialization.coverage_concurrent_statements.push_back(
                    found->second);
            }
        }
    }
}

} // namespace fsim::elaboration
