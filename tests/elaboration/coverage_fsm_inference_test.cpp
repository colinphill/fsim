// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_fsm_inference.hpp"
#include "fsim/frontend/coverage_source_identity.hpp"
#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::string_view kSystemVerilogSource = R"(
module controller;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state;
  logic [1:0] phase;
  logic [1:0] phase_next;
  logic observed;
  always_comb begin
    phase = phase_next;
    case (state)
      IDLE: observed = 1'b0;
      RUN:  observed = 1'b1;
      DONE: observed = 1'b0;
    endcase
    case (phase)
      2'b00: observed = 1'b0;
      2'b01: observed = 1'b1;
      2'b10: observed = 1'b0;
    endcase
  end
endmodule
)";

constexpr std::string_view kNextStateSystemVerilogSource = R"(
module next_controller;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  logic clk;
  state_t state;
  state_t next_state;
  always_comb begin
    case (state)
      IDLE: next_state = RUN;
      RUN:  next_state = DONE;
      DONE: next_state = IDLE;
    endcase
  end
  always_ff @(posedge clk) begin
    state <= next_state;
  end
endmodule
)";

constexpr std::string_view kPragmaSystemVerilogSource = R"(
(* vendor_fsm_encoding = "onehot" *)
(* fsm_current_state = "state", fsm_next_state = "next_state",
   fsm_legal_states = "IDLE, RUN" *)
(* fsm_current_state = "mode", fsm_legal_states = "OFF, ON" *)
module pragma_controller;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state;
  state_t next_state;
  logic mode;
endmodule
)";

constexpr std::string_view kVerilogAttributeSource = R"(
(* fsm_current_state = "state", fsm_legal_states = "OFF, ON" *)
module verilog_attributes;
  reg state;
endmodule
)";

constexpr std::string_view kVhdlSource = R"(architecture rtl of controller is
state_decl
next_state_decl
begin
case_state
idle_state
run_state
done_state
end
)";

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root(const std::string_view leaf)
{
#if defined(_WIN32)
    return std::filesystem::path { "C:/fsim-fsm-inference" } / leaf;
#else
    return std::filesystem::path { "/fsim-fsm-inference" } / leaf;
#endif
}

fsim::elaboration::VerilogCoverageSource source_for(
    const std::filesystem::path& root, const std::string_view text,
    const std::string_view leaf)
{
    const auto path = root / leaf;
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, path, bytes(text));
    require(identity.ok(), "FSM inference source identity must be valid");
    return { path.generic_string(), std::move(*identity.identity) };
}

fsim::elaboration::CoverageInventoryOwner sv_owner(
    const fsim::elaboration::VerilogCoverageSource& source,
    const std::string_view instance = "top.u",
    const std::string_view unit = "sv:work.controller")
{
    return { 7U, instance, fsim::frontend::Language::SystemVerilog2017,
        source.source_name, { }, "work", unit, { } };
}

const fsim::elaboration::CoverageFsmCurrentStateObject& find_object(
    const fsim::elaboration::CoverageFsmInference& inference,
    const std::string_view path)
{
    const auto found = std::ranges::find(
        inference.current_state_objects, path,
        &fsim::elaboration::CoverageFsmCurrentStateObject::hierarchy_path);
    require(found != inference.current_state_objects.end(),
        "expected inferred current-state path must exist");
    return *found;
}

const fsim::elaboration::CoverageFsmLegalStateSet& find_legal_set(
    const fsim::elaboration::CoverageFsmInference& inference,
    const fsim::runtime::CodeCoveragePointId current)
{
    const auto found = std::ranges::find(inference.legal_state_sets, current,
        &fsim::elaboration::CoverageFsmLegalStateSet::current_state_object);
    require(found != inference.legal_state_sets.end(),
        "expected inferred legal-state set must exist");
    return *found;
}

const fsim::elaboration::CoverageFsmDescriptionDiagnostic& find_diagnostic(
    const fsim::elaboration::CoverageFsmInference& inference,
    const fsim::elaboration::CoverageFsmDescriptionIssueKind kind,
    const fsim::elaboration::CoverageFsmDescriptionSubject subject,
    const std::string_view object)
{
    const auto found = std::ranges::find_if(
        inference.description_diagnostics, [&](const auto& diagnostic) {
            return diagnostic.kind == kind
                && diagnostic.subject == subject
                && diagnostic.object == object;
        });
    require(found != inference.description_diagnostics.end(),
        "expected FSM description diagnostic must exist");
    return *found;
}

fsim::frontend::SystemVerilogFsmPragmaSpecification& find_pragma(
    fsim::frontend::DesignUnit& unit,
    const fsim::frontend::SystemVerilogFsmPragmaKind kind,
    const std::size_t group = 0U)
{
    require(group < unit.systemverilog_fsm_pragmas.size(),
        "expected parsed FSM pragma group must exist");
    auto& specifications
        = unit.systemverilog_fsm_pragmas[group].specifications;
    const auto found = std::ranges::find(specifications, kind,
        &fsim::frontend::SystemVerilogFsmPragmaSpecification::kind);
    require(found != specifications.end(),
        "expected parsed FSM pragma specification must exist");
    return *found;
}

void set_pragma_string(
    fsim::frontend::SystemVerilogFsmPragmaSpecification& specification,
    std::string value)
{
    require(specification.value.has_value(),
        "expected pragma value expression must exist");
    specification.value->kind
        = fsim::frontend::ExpressionKind::StringLiteral;
    specification.value->decoded_string = std::move(value);
}

fsim::frontend::Statement& parsed_case(
    fsim::frontend::DesignUnit& unit, const std::size_t index)
{
    return unit.processes.front().statements.at(index);
}

fsim::frontend::Statement direct_assignment(
    const std::string_view target, const std::string_view value,
    const fsim::frontend::SourceSpan& span)
{
    using namespace fsim::frontend;
    Statement statement;
    statement.kind = StatementKind::Assignment;
    statement.target = { ExpressionKind::Identifier, std::string { target },
        { }, span };
    statement.value = { ExpressionKind::Identifier, std::string { value },
        { }, span };
    statement.span = span;
    return statement;
}

void test_parsed_enum_and_case_inference()
{
    using namespace fsim;
    const auto source = source_for(checkout_root("parsed"),
        kSystemVerilogSource, "rtl/controller.sv");
    const auto parsed = frontend::parse_text(source.source_name,
        kSystemVerilogSource, frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.size() == 1U,
        "independently authored FSM source must parse into one semantic unit");
    const auto& unit = parsed.design.units.front();
    const auto built = elaboration::make_coverage_fsm_inference(unit,
        unit.ports, sv_owner(source), std::span { &source, 1U });
    require(built.ok()
            && built.inference->current_state_objects.size() == 2U,
        "enum- and exact-case-driven retained objects must be inferred");
    const auto& state = find_object(*built.inference, "top.u.state");
    const auto& phase = find_object(*built.inference, "top.u.phase");
    require(state.enum_evidence && state.case_evidence
            && state.states.size() == 3U
            && state.states[0].name == "IDLE"
            && state.states[1].name == "RUN"
            && state.states[2].name == "DONE",
        "enum evidence must retain declaration-order state identities");
    require(!phase.enum_evidence && phase.case_evidence
            && phase.states.size() == 3U
            && phase.states[0].name == "2'b00"
            && phase.states[1].name == "2'b01"
            && phase.states[2].name == "2'b10",
        "case-only inference must retain a canonical explicit state set");
    require(built.inference->next_state_objects.size() == 1U
            && built.inference->next_state_objects.front().hierarchy_path
                == "top.u.phase_next"
            && built.inference->next_state_objects.front().current_state_object
                == phase.point,
        "compatible scalar assignment must infer an optional next-state object");
    require(built.inference->legal_state_sets.size() == 2U
            && std::ranges::any_of(built.inference->legal_state_sets,
                [&](const auto& legal) {
                    return legal.current_state_object == phase.point
                        && !legal.enum_evidence && legal.case_evidence;
                }),
        "case-only inference must publish an explicit legal-state set");
    for (const auto& object : built.inference->current_state_objects) {
        require(runtime::is_code_coverage_identity_valid(object.source_point)
                && runtime::is_code_coverage_identity_valid(object.point)
                && std::ranges::all_of(object.states,
                    [](const auto& inferred) {
                        return runtime::is_code_coverage_identity_valid(
                            inferred.id);
                    }),
            "every inferred object and state must own stable valid identities");
    }
}

void test_next_state_and_legal_state_inference()
{
    using namespace fsim;
    const auto source_a = source_for(checkout_root("next-a"),
        kNextStateSystemVerilogSource, "rtl/next_controller.sv");
    const auto source_b = source_for(checkout_root("next-b"),
        kNextStateSystemVerilogSource, "rtl/next_controller.sv");
    const auto parsed_a = frontend::parse_text(source_a.source_name,
        kNextStateSystemVerilogSource,
        frontend::Language::SystemVerilog2017);
    const auto parsed_b = frontend::parse_text(source_b.source_name,
        kNextStateSystemVerilogSource,
        frontend::Language::SystemVerilog2017);
    require(parsed_a.ok() && parsed_b.ok(),
        "independently authored next-state source must parse");
    const auto& unit_a = parsed_a.design.units.front();
    const auto& unit_b = parsed_b.design.units.front();
    const auto built = elaboration::make_coverage_fsm_inference(unit_a,
        unit_a.ports, sv_owner(source_a, "top.u", "sv:work.next_controller"),
        std::span { &source_a, 1U });
    const auto relocated = elaboration::make_coverage_fsm_inference(unit_b,
        unit_b.ports, sv_owner(source_b, "top.u", "sv:work.next_controller"),
        std::span { &source_b, 1U });
    require(built.ok() && relocated.ok()
            && built.inference == relocated.inference,
        "next-state and legal-state inference must be relocation independent");
    require(built.inference->current_state_objects.size() == 1U
            && built.inference->next_state_objects.size() == 1U
            && built.inference->legal_state_sets.size() == 1U,
        "one current object must own its optional next object and legal set");
    const auto& current = built.inference->current_state_objects.front();
    const auto& next = built.inference->next_state_objects.front();
    const auto& legal = built.inference->legal_state_sets.front();
    require(current.hierarchy_path == "top.u.state"
            && !current.pragma_evidence
            && next.hierarchy_path == "top.u.next_state"
            && next.current_state_object == current.point
            && next.assignment_evidence && !next.pragma_evidence
            && runtime::is_code_coverage_identity_valid(next.source_point)
            && runtime::is_code_coverage_identity_valid(next.point),
        "direct compatible assignment must bind stable current/next identities");
    require(legal.current_state_object == current.point
            && legal.enum_evidence && legal.case_evidence
            && !legal.pragma_evidence
            && runtime::is_code_coverage_identity_valid(legal.point)
            && legal.states.size() == current.states.size(),
        "enum and matching exact-case evidence must publish one legal-state set");
    for (std::size_t index = 0U; index < legal.states.size(); ++index) {
        require(legal.states[index] == current.states[index].id,
            "legal-state sets must reference existing stable state identities");
    }
}

void test_systemverilog_fsm_pragmas()
{
    using namespace fsim;
    const auto source_a = source_for(checkout_root("pragma-a"),
        kPragmaSystemVerilogSource, "rtl/pragma_controller.sv");
    const auto source_b = source_for(checkout_root("pragma-b"),
        kPragmaSystemVerilogSource, "rtl/pragma_controller.sv");
    const auto parsed_a = frontend::parse_text(source_a.source_name,
        kPragmaSystemVerilogSource,
        frontend::Language::SystemVerilog2017);
    const auto parsed_b = frontend::parse_text(source_b.source_name,
        kPragmaSystemVerilogSource,
        frontend::Language::SystemVerilog2017);
    require(parsed_a.ok() && parsed_b.ok()
            && parsed_a.design.units.size() == 1U
            && parsed_a.design.units.front().systemverilog_fsm_pragmas.size()
                == 2U
            && parsed_a.design.units.front()
                    .systemverilog_fsm_pragmas[0]
                    .specifications.size()
                == 3U
            && parsed_a.design.units.front()
                    .systemverilog_fsm_pragmas[1]
                    .specifications.size()
                == 2U,
        "only exact vendor-neutral FSM pragma keys must be retained by group");

    std::optional<elaboration::CoverageFsmInference> reference;
    for (const auto standard : {
             frontend::StandardRevision::SystemVerilog2005,
             frontend::StandardRevision::SystemVerilog2009,
             frontend::StandardRevision::SystemVerilog2012,
             frontend::StandardRevision::SystemVerilog2017 }) {
        auto unit = parsed_a.design.units.front();
        unit.standard_revision = standard;
        const auto built = elaboration::make_coverage_fsm_inference(unit,
            unit.ports,
            sv_owner(source_a, "top.u", "sv:work.pragma_controller"),
            std::span { &source_a, 1U });
        require(built.ok()
                && built.inference->current_state_objects.size() == 2U
                && built.inference->next_state_objects.size() == 1U
                && built.inference->legal_state_sets.size() == 2U,
            "every retained SystemVerilog profile must honor exact FSM pragmas");
        const auto& state = find_object(*built.inference, "top.u.state");
        const auto& mode = find_object(*built.inference, "top.u.mode");
        const auto& next = built.inference->next_state_objects.front();
        const auto& state_legal
            = find_legal_set(*built.inference, state.point);
        const auto& mode_legal
            = find_legal_set(*built.inference, mode.point);
        require(state.enum_evidence && !state.case_evidence
                && state.pragma_evidence && state.states.size() == 3U
                && state.states[0].name == "IDLE"
                && state.states[1].name == "RUN"
                && state.states[2].name == "DONE",
            "a pragma must augment an enum current-state description without replacing its state universe");
        require(next.hierarchy_path == "top.u.next_state"
                && next.current_state_object == state.point
                && !next.assignment_evidence && next.pragma_evidence,
            "an exact next-state pragma must bind one compatible retained object");
        require(state_legal.pragma_evidence
                && state_legal.states.size() == 2U
                && state_legal.states[0] == state.states[0].id
                && state_legal.states[1] == state.states[1].id,
            "an enum legal-state pragma must select existing ordered state identities");
        require(!mode.enum_evidence && !mode.case_evidence
                && mode.pragma_evidence && mode.states.size() == 2U
                && mode.states[0].name == "OFF"
                && mode.states[1].name == "ON"
                && mode_legal.pragma_evidence
                && mode_legal.states[0] == mode.states[0].id
                && mode_legal.states[1] == mode.states[1].id,
            "a legal-state pragma must describe a retained scalar FSM without enum or case evidence");
        if (!reference) {
            reference = *built.inference;
        } else {
            require(*built.inference == *reference,
                "SystemVerilog revision spelling must not perturb stable pragma identities");
        }
    }

    auto relocated_unit = parsed_b.design.units.front();
    relocated_unit.standard_revision
        = frontend::StandardRevision::SystemVerilog2005;
    const auto relocated = elaboration::make_coverage_fsm_inference(
        relocated_unit, relocated_unit.ports,
        sv_owner(source_b, "top.u", "sv:work.pragma_controller"),
        std::span { &source_b, 1U });
    require(relocated.ok() && relocated.inference == reference,
        "pragma-driven FSM identities must be checkout-location independent");

    const auto verilog = frontend::parse_text("rtl/verilog_attributes.v",
        kVerilogAttributeSource, frontend::Language::Verilog2005);
    require(verilog.ok() && verilog.design.units.size() == 1U
            && verilog.design.units.front()
                .systemverilog_fsm_pragmas.empty(),
        "SystemVerilog FSM pragma semantics must not leak into Verilog profiles");
}

void test_systemverilog_fsm_pragma_failures()
{
    using namespace fsim;
    using Error = elaboration::CoverageFsmInferenceError;
    const auto source = source_for(checkout_root("pragma-negative"),
        kPragmaSystemVerilogSource, "rtl/pragma_controller.sv");
    const auto parsed = frontend::parse_text(source.source_name,
        kPragmaSystemVerilogSource,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok(), "negative FSM pragma fixture must parse");
    const auto invoke = [&](const frontend::DesignUnit& unit,
                            const elaboration::CoverageFsmInferenceLimits limits = { }) {
        return elaboration::make_coverage_fsm_inference(unit, unit.ports,
            sv_owner(source, "top.u", "sv:work.pragma_controller"),
            std::span { &source, 1U }, limits);
    };

    auto non_string = parsed.design.units.front();
    find_pragma(non_string,
        frontend::SystemVerilogFsmPragmaKind::CurrentState)
        .value->kind = frontend::ExpressionKind::Identifier;
    require(invoke(non_string).error == Error::InvalidSystemVerilogPragma,
        "FSM pragma values must be decoded string literals");

    auto missing_current = parsed.design.units.front();
    auto& missing_specifications
        = missing_current.systemverilog_fsm_pragmas.front().specifications;
    std::erase_if(missing_specifications, [](const auto& specification) {
        return specification.kind
            == frontend::SystemVerilogFsmPragmaKind::CurrentState;
    });
    require(invoke(missing_current).error
            == Error::InvalidSystemVerilogPragma,
        "each FSM pragma group must name exactly one current-state object");

    auto unknown_current = parsed.design.units.front();
    set_pragma_string(find_pragma(unknown_current,
                          frontend::SystemVerilogFsmPragmaKind::CurrentState),
        "missing_state");
    require(invoke(unknown_current).error
            == Error::InvalidSystemVerilogPragma,
        "an FSM pragma must resolve its current-state object uniquely");

    auto incompatible_next = parsed.design.units.front();
    set_pragma_string(find_pragma(incompatible_next,
                          frontend::SystemVerilogFsmPragmaKind::NextState),
        "mode");
    require(invoke(incompatible_next).error
            == Error::InvalidSystemVerilogPragma,
        "an FSM pragma must reject an incompatible next-state object");

    auto unknown_legal = parsed.design.units.front();
    set_pragma_string(find_pragma(unknown_legal,
                          frontend::SystemVerilogFsmPragmaKind::LegalStates),
        "IDLE, MISSING");
    require(invoke(unknown_legal).error
            == Error::InvalidSystemVerilogPragma,
        "an enum legal-state pragma must not manufacture unknown states");

    auto duplicate_current = parsed.design.units.front();
    duplicate_current.systemverilog_fsm_pragmas.front()
        .specifications.push_back(find_pragma(duplicate_current,
            frontend::SystemVerilogFsmPragmaKind::CurrentState));
    require(invoke(duplicate_current).error
            == Error::InvalidSystemVerilogPragma,
        "one FSM pragma group must not repeat a current-state specification");

    auto conflicting = parsed.design.units.front();
    auto conflict_group = conflicting.systemverilog_fsm_pragmas.front();
    conflicting.systemverilog_fsm_pragmas.push_back(
        std::move(conflict_group));
    set_pragma_string(find_pragma(conflicting,
                          frontend::SystemVerilogFsmPragmaKind::LegalStates,
                          2U),
        "IDLE, DONE");
    const auto conflict = invoke(conflicting);
    require(conflict.ok()
            && !find_object(*conflict.inference, "top.u.state").pragma_evidence
            && conflict.inference->next_state_objects.empty()
            && !find_legal_set(*conflict.inference,
                find_object(*conflict.inference, "top.u.state").point)
                .pragma_evidence,
        "conflicting repeated pragma descriptions must suppress their evidence transactionally");
    const auto& legal_conflict = find_diagnostic(*conflict.inference,
        elaboration::CoverageFsmDescriptionIssueKind::Conflicting,
        elaboration::CoverageFsmDescriptionSubject::LegalStates, "state");
    require(legal_conflict.origins
            == std::vector {
                elaboration::CoverageFsmDescriptionOrigin::SystemVerilogPragma },
        "conflicting repeated pragma legal sets must retain their exact origin");

    auto competing_next = parsed.design.units.front();
    auto alternate = competing_next.signals[1];
    alternate.name = "alternate_state";
    competing_next.signals.push_back(std::move(alternate));
    auto competing_group
        = competing_next.systemverilog_fsm_pragmas.front();
    competing_next.systemverilog_fsm_pragmas.push_back(
        std::move(competing_group));
    set_pragma_string(find_pragma(competing_next,
                          frontend::SystemVerilogFsmPragmaKind::NextState,
                          2U),
        "alternate_state");
    const auto competition = invoke(competing_next);
    require(competition.ok()
            && find_object(*competition.inference, "top.u.state")
                .pragma_evidence
            && competition.inference->next_state_objects.empty(),
        "multiple compatible pragma next-state candidates must suppress only the optional relation");
    find_diagnostic(*competition.inference,
        elaboration::CoverageFsmDescriptionIssueKind::Conflicting,
        elaboration::CoverageFsmDescriptionSubject::NextState, "state");

    auto incomplete = parsed.design.units.front();
    auto& incomplete_specifications
        = incomplete.systemverilog_fsm_pragmas[1].specifications;
    std::erase_if(incomplete_specifications, [](const auto& specification) {
        return specification.kind
            == frontend::SystemVerilogFsmPragmaKind::LegalStates;
    });
    const auto incompleteness = invoke(incomplete);
    require(incompleteness.ok()
            && incompleteness.inference->current_state_objects.size() == 1U
            && find_diagnostic(*incompleteness.inference,
                   elaboration::CoverageFsmDescriptionIssueKind::Incomplete,
                   elaboration::CoverageFsmDescriptionSubject::LegalStates,
                   "mode")
                    .origins
                == std::vector {
                    elaboration::CoverageFsmDescriptionOrigin::SystemVerilogPragma },
        "a scalar current-state description without a state universe must publish an incomplete diagnostic");

    auto wrong_profile = parsed.design.units.front();
    wrong_profile.standard_revision
        = frontend::StandardRevision::Verilog2005;
    require(invoke(wrong_profile).error
            == Error::InvalidSystemVerilogPragma,
        "retained FSM pragmas must reject a non-SystemVerilog standard identity");

    auto limits = elaboration::CoverageFsmInferenceLimits { };
    limits.maximum_pragmas = 1U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "FSM pragma-group ceiling must be enforced");
    limits = { };
    limits.maximum_pragma_specifications = 1U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "FSM pragma-specification ceiling must be enforced");
    limits = { };
    limits.maximum_pragma_value_bytes = 4U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "FSM pragma-value byte ceiling must be enforced");
}

void test_relocation_order_and_instance_identity()
{
    using namespace fsim;
    const auto source_a = source_for(checkout_root("relocated-a"),
        kSystemVerilogSource, "rtl/controller.sv");
    const auto source_b = source_for(checkout_root("relocated-b"),
        kSystemVerilogSource, "rtl/controller.sv");
    auto parsed_a = frontend::parse_text(source_a.source_name,
        kSystemVerilogSource, frontend::Language::SystemVerilog2017);
    auto parsed_b = frontend::parse_text(source_b.source_name,
        kSystemVerilogSource, frontend::Language::SystemVerilog2017);
    require(parsed_a.ok() && parsed_b.ok(),
        "relocated FSM fixtures must parse");
    auto& unit_a = parsed_a.design.units.front();
    auto& unit_b = parsed_b.design.units.front();
    const auto first = elaboration::make_coverage_fsm_inference(unit_a,
        unit_a.ports, sv_owner(source_a), std::span { &source_a, 1U });
    const auto relocated = elaboration::make_coverage_fsm_inference(unit_b,
        unit_b.ports, sv_owner(source_b), std::span { &source_b, 1U });
    require(first.ok() && relocated.ok()
            && first.inference == relocated.inference,
        "FSM inference must be checkout-location independent");
    std::ranges::reverse(unit_a.variables);
    const auto reordered = elaboration::make_coverage_fsm_inference(unit_a,
        unit_a.ports, sv_owner(source_a), std::span { &source_a, 1U });
    require(reordered.ok() && reordered.inference == first.inference,
        "candidate declaration-container order must not affect inference");
    const auto sibling = elaboration::make_coverage_fsm_inference(unit_a,
        unit_a.ports, sv_owner(source_a, "top.v"),
        std::span { &source_a, 1U });
    require(sibling.ok()
            && sibling.inference->current_state_objects.front().source_point
                == first.inference->current_state_objects.front().source_point
            && sibling.inference->current_state_objects.front().point
                != first.inference->current_state_objects.front().point
            && sibling.inference->current_state_objects.front().states.front().id
                != first.inference->current_state_objects.front().states.front().id,
        "source ownership may be shared while instance object/state identities remain distinct");
}

fsim::frontend::SourceSpan vhdl_span(
    const std::string& source, const std::string_view token)
{
    const auto begin = kVhdlSource.find(token);
    require(begin != std::string_view::npos, "VHDL FSM token must exist");
    const auto line = static_cast<std::size_t>(
        1U + std::count(kVhdlSource.begin(), kVhdlSource.begin() + begin, '\n'));
    return { source, { begin, line, 1U },
        { begin + token.size(), line, token.size() + 1U }, { }, { } };
}

fsim::frontend::Expression identifier(
    std::string text, const fsim::frontend::SourceSpan& span)
{
    return { fsim::frontend::ExpressionKind::Identifier,
        std::move(text), { }, span };
}

fsim::frontend::Statement vhdl_case(const std::string& source)
{
    using namespace fsim::frontend;
    Statement statement;
    statement.kind = StatementKind::Case;
    statement.condition = identifier("state", vhdl_span(source, "case_state"));
    statement.span = statement.condition.span;
    for (const auto token : { "idle_state", "run_state", "done_state" }) {
        CaseAlternative alternative;
        alternative.span = vhdl_span(source, token);
        alternative.choices.push_back(identifier(
            std::string { token }.substr(0U,
                std::string_view { token }.find('_')),
            alternative.span));
        statement.case_alternatives.push_back(std::move(alternative));
    }
    return statement;
}

fsim::frontend::Statement vhdl_next_state_assignment(
    const std::string& source)
{
    using namespace fsim::frontend;
    Statement statement;
    statement.kind = StatementKind::Assignment;
    statement.target = identifier("state", vhdl_span(source, "case_state"));
    statement.value = identifier(
        "next_state", vhdl_span(source, "next_state_decl"));
    statement.span = statement.target.span;
    return statement;
}

fsim::frontend::DesignUnit vhdl_unit(
    const fsim::elaboration::VerilogCoverageSource& source,
    const fsim::frontend::VhdlStandard standard)
{
    using namespace fsim::frontend;
    DesignUnit unit;
    unit.kind = UnitKind::VhdlArchitecture;
    unit.language = Language::Vhdl2008;
    unit.vhdl_standard = standard;
    unit.library = "work";
    unit.name = "rtl";
    unit.primary_name = "controller";
    unit.span = vhdl_span(source.source_name, "architecture rtl");
    Type state_type;
    state_type.domain = ValueDomain::Bit2;
    state_type.spelling = "state_t";
    state_type.nominal_type = "work.state_t";
    state_type.enumeration_literals = { "idle", "run", "done" };
    state_type.enumeration_range = EnumerationRange { 0, 2, false };
    SignalDeclaration state;
    state.name = "state";
    state.type = std::move(state_type);
    state.span = vhdl_span(source.source_name, "state_decl");
    SignalDeclaration next_state;
    next_state.name = "next_state";
    next_state.type = state.type;
    next_state.span = vhdl_span(source.source_name, "next_state_decl");
    unit.signals.push_back(std::move(state));
    unit.signals.push_back(std::move(next_state));
    Process process;
    process.kind = ProcessKind::VhdlProcess;
    process.span = vhdl_span(source.source_name, "case_state");
    process.statements.push_back(vhdl_next_state_assignment(
        source.source_name));
    process.statements.push_back(vhdl_case(source.source_name));
    unit.processes.push_back(std::move(process));
    return unit;
}

void test_vhdl_profiles_and_case_evidence()
{
    using namespace fsim;
    const auto source = source_for(checkout_root("vhdl"),
        kVhdlSource, "rtl/controller.vhd");
    std::optional<elaboration::CoverageFsmInference> reference;
    for (const auto standard : { frontend::VhdlStandard::Vhdl1987,
             frontend::VhdlStandard::Vhdl1993,
             frontend::VhdlStandard::Vhdl2000,
             frontend::VhdlStandard::Vhdl2002,
             frontend::VhdlStandard::Vhdl2008 }) {
        const auto unit = vhdl_unit(source, standard);
        const elaboration::CoverageInventoryOwner owner { 2U, "root",
            frontend::Language::Vhdl2008, source.source_name, { }, "work",
            "vhdl:work.controller(rtl)", { } };
        const auto built = elaboration::make_coverage_fsm_inference(unit, { },
            owner, std::span { &source, 1U });
        require(built.ok()
                && built.inference->current_state_objects.size() == 1U
                && built.inference->current_state_objects.front().enum_evidence
                && built.inference->current_state_objects.front().case_evidence
                && built.inference->next_state_objects.size() == 1U
                && built.inference->next_state_objects.front().hierarchy_path
                    == "root.next_state"
                && built.inference->legal_state_sets.size() == 1U
                && built.inference->legal_state_sets.front().enum_evidence
                && built.inference->legal_state_sets.front().case_evidence,
            "every retained VHDL profile must infer enum/case current state equivalently");
        if (!reference) {
            reference = *built.inference;
        } else {
            require(*built.inference == *reference,
                "VHDL revision spelling must not perturb stable FSM identities");
        }
    }
}

void test_next_state_ambiguity_shadowing_and_resource_failures()
{
    using namespace fsim;
    using Error = elaboration::CoverageFsmInferenceError;
    const auto source = source_for(checkout_root("next-negative"),
        kNextStateSystemVerilogSource, "rtl/next_controller.sv");
    const auto parsed = frontend::parse_text(source.source_name,
        kNextStateSystemVerilogSource,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok(), "negative next-state fixture must parse");
    const auto invoke = [&](const frontend::DesignUnit& unit,
                            const elaboration::CoverageFsmInferenceLimits limits = { }) {
        return elaboration::make_coverage_fsm_inference(unit, unit.ports,
            sv_owner(source, "top.u", "sv:work.next_controller"),
            std::span { &source, 1U }, limits);
    };

    auto ambiguous = parsed.design.units.front();
    const auto retained_next = std::ranges::find(ambiguous.signals,
        "next_state", &frontend::SignalDeclaration::name);
    require(retained_next != ambiguous.signals.end(),
        "parsed retained next-state declaration must exist");
    const auto next_span = retained_next->span;
    const auto next_type = retained_next->type;
    auto alternate = *retained_next;
    alternate.name = "alternate_state";
    ambiguous.signals.push_back(std::move(alternate));
    frontend::Process competing;
    competing.kind = frontend::ProcessKind::VerilogAlways;
    competing.span = next_span;
    competing.statements.push_back(direct_assignment(
        "state", "alternate_state", next_span));
    ambiguous.processes.push_back(std::move(competing));
    const auto ambiguity = invoke(ambiguous);
    require(ambiguity.ok()
            && ambiguity.inference->current_state_objects.size() == 1U
            && ambiguity.inference->next_state_objects.empty()
            && ambiguity.inference->legal_state_sets.size() == 1U,
        "multiple compatible next-state candidates must suppress the optional relation only");
    find_diagnostic(*ambiguity.inference,
        elaboration::CoverageFsmDescriptionIssueKind::Ambiguous,
        elaboration::CoverageFsmDescriptionSubject::NextState, "state");

    auto shadowed = parsed.design.units.front();
    for (auto& process : shadowed.processes) {
        frontend::VariableDeclaration local;
        local.name = "next_state";
        local.type = next_type;
        local.span = next_span;
        process.variables.push_back(std::move(local));
    }
    const auto shadow = invoke(shadowed);
    require(shadow.ok() && shadow.inference->next_state_objects.empty(),
        "a process-local shadow must prevent retained next-state inference");

    auto limits = elaboration::CoverageFsmInferenceLimits { };
    limits.maximum_assignments = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "assignment traversal ceiling must be enforced");
    limits = { };
    limits.maximum_next_state_objects = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "next-state object ceiling must be enforced");
    limits = { };
    limits.maximum_legal_state_sets = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "legal-state-set ceiling must be enforced");
    limits = { };
    limits.description_validation.maximum_candidates = 0U;
    require(invoke(ambiguous, limits).error == Error::ResourceLimit,
        "inference must enforce its diagnostic-candidate ceiling before publication");
    limits = { };
    limits.description_validation.maximum_diagnostics = 0U;
    require(invoke(ambiguous, limits).error == Error::ResourceLimit,
        "inference must enforce its published-diagnostic ceiling transactionally");
}

void test_ambiguity_shadowing_and_resource_failures()
{
    using namespace fsim;
    using Error = elaboration::CoverageFsmInferenceError;
    const auto source = source_for(checkout_root("negative"),
        kSystemVerilogSource, "rtl/controller.sv");
    auto parsed = frontend::parse_text(source.source_name,
        kSystemVerilogSource, frontend::Language::SystemVerilog2017);
    require(parsed.ok(), "negative FSM fixture must parse");
    const auto invoke = [&](const frontend::DesignUnit& unit,
                            const elaboration::CoverageFsmInferenceLimits limits = { }) {
        return elaboration::make_coverage_fsm_inference(unit, unit.ports,
            sv_owner(source), std::span { &source, 1U }, limits);
    };

    auto ambiguous = parsed.design.units.front();
    auto conflicting = parsed_case(ambiguous, 2U);
    conflicting.case_alternatives[1].choices.front().text = "2'b11";
    ambiguous.processes.front().statements.push_back(std::move(conflicting));
    const auto ambiguity = invoke(ambiguous);
    require(ambiguity.ok()
            && ambiguity.inference->current_state_objects.size() == 1U
            && ambiguity.inference->current_state_objects.front().hierarchy_path
                == "top.u.state",
        "conflicting case-only descriptions must not infer an ambiguous object");
    const auto& case_conflict = find_diagnostic(*ambiguity.inference,
        elaboration::CoverageFsmDescriptionIssueKind::Conflicting,
        elaboration::CoverageFsmDescriptionSubject::LegalStates, "phase");
    require(case_conflict.origins
            == std::vector {
                elaboration::CoverageFsmDescriptionOrigin::Case },
        "conflicting case descriptions must publish their stable source origin");

    auto incomplete_enum_case = parsed.design.units.front();
    parsed_case(incomplete_enum_case, 1U).case_alternatives.pop_back();
    const auto incomplete = invoke(incomplete_enum_case);
    require(incomplete.ok()
            && find_object(*incomplete.inference, "top.u.state")
                .enum_evidence
            && find_diagnostic(*incomplete.inference,
                   elaboration::CoverageFsmDescriptionIssueKind::Incomplete,
                   elaboration::CoverageFsmDescriptionSubject::LegalStates,
                   "state")
                    .origins
                == std::vector {
                    elaboration::CoverageFsmDescriptionOrigin::Enum,
                    elaboration::CoverageFsmDescriptionOrigin::Case },
        "an exact case that covers only an enum subset must be diagnosed as incomplete without losing enum inference");

    auto conflicting_enum_case = parsed.design.units.front();
    parsed_case(conflicting_enum_case, 1U)
        .case_alternatives.back()
        .choices.front()
        .text = "2'b11";
    const auto enum_conflict = invoke(conflicting_enum_case);
    require(enum_conflict.ok()
            && find_diagnostic(*enum_conflict.inference,
                   elaboration::CoverageFsmDescriptionIssueKind::Conflicting,
                   elaboration::CoverageFsmDescriptionSubject::LegalStates,
                   "state")
                    .origins
                == std::vector {
                    elaboration::CoverageFsmDescriptionOrigin::Enum,
                    elaboration::CoverageFsmDescriptionOrigin::Case },
        "an exact case outside its enum universe must publish a conflict diagnostic");

    auto shadowed = parsed.design.units.front();
    frontend::VariableDeclaration local;
    local.name = "phase";
    local.type.domain = frontend::ValueDomain::Logic4;
    local.span = parsed_case(shadowed, 2U).span;
    shadowed.processes.front().variables.push_back(std::move(local));
    const auto shadow = invoke(shadowed);
    require(shadow.ok()
            && shadow.inference->current_state_objects.size() == 1U
            && shadow.inference->current_state_objects.front().hierarchy_path
                == "top.u.state",
        "a process-local shadow must prevent unit-object case inference");

    auto duplicate = parsed.design.units.front();
    const auto state = std::ranges::find(duplicate.signals, "state",
        &frontend::SignalDeclaration::name);
    require(state != duplicate.signals.end(),
        "parsed enum state declaration must remain available");
    duplicate.signals.push_back(*state);
    const auto duplicated = invoke(duplicate);
    require(duplicated.ok()
            && duplicated.inference->current_state_objects.size() == 1U
            && duplicated.inference->current_state_objects.front().hierarchy_path
                == "top.u.phase",
        "duplicate retained names must not produce ambiguous current-state ownership");
    find_diagnostic(*duplicated.inference,
        elaboration::CoverageFsmDescriptionIssueKind::Ambiguous,
        elaboration::CoverageFsmDescriptionSubject::CurrentState, "state");

    auto limits = elaboration::CoverageFsmInferenceLimits { };
    limits.maximum_objects = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "candidate-object ceiling must be enforced");
    limits = { };
    limits.maximum_cases = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "case traversal ceiling must be enforced");
    limits = { };
    limits.maximum_case_choices = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "case-choice ceiling must be enforced");
    limits = { };
    limits.maximum_states = 2U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "aggregate inferred-state ceiling must be enforced");
    limits = { };
    limits.maximum_statement_depth = 0U;
    require(invoke(parsed.design.units.front(), limits).error
            == Error::ResourceLimit,
        "statement-depth ceiling must be enforced");

    auto unknown = parsed.design.units.front();
    auto unknown_state = std::ranges::find(unknown.signals, "state",
        &frontend::SignalDeclaration::name);
    require(unknown_state != unknown.signals.end(),
        "negative enum state declaration must remain available");
    unknown_state->span.source_name = "unknown.sv";
    unknown_state->span.physical_source_name = "unknown.sv";
    require(invoke(unknown).error == Error::UnknownObjectSource,
        "an inferred object with an unmapped source must fail transactionally");
}

} // namespace

int main()
{
    test_parsed_enum_and_case_inference();
    test_next_state_and_legal_state_inference();
    test_systemverilog_fsm_pragmas();
    test_systemverilog_fsm_pragma_failures();
    test_relocation_order_and_instance_identity();
    test_vhdl_profiles_and_case_evidence();
    test_ambiguity_shadowing_and_resource_failures();
    test_next_state_ambiguity_shadowing_and_resource_failures();
    std::cout << "Coverage FSM inference tests passed\n";
}
