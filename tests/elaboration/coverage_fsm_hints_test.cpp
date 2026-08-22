// SPDX-License-Identifier: Apache-2.0
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/coverage_fsm_hints.hpp"
#include "fsim/elaboration/coverage_fsm_inference.hpp"
#include "fsim/frontend/coverage_source_identity.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/project/project.hpp"

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

constexpr std::string_view kVhdlSource = R"(
entity controller is
end entity;

architecture rtl of controller is
  type state_t is (idle, run, done);
  signal state : state_t;
  signal next_state : state_t;
  attribute vendor_encoding : string;
  attribute vendor_encoding of state : signal is "onehot";
  attribute fsm_current_state : string;
  attribute fsm_next_state : string;
  attribute fsm_legal_states : string;
  attribute fsm_current_state of state : signal is "true";
  attribute fsm_next_state of state : signal is "next_state";
  attribute fsm_legal_states of state : signal is "idle, run";
begin
end architecture;
)";

constexpr std::string_view kSystemVerilogSource = R"(
module controller;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state;
  state_t next_state;
endmodule
)";

constexpr std::string_view kSystemVerilogPragmaSource = R"(
(* fsm_current_state = "state", fsm_next_state = "next_state",
   fsm_legal_states = "IDLE, RUN" *)
module controller;
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state;
  state_t next_state;
endmodule
)";

constexpr std::string_view kManifest = R"(
schema = 3

[project]
top = "controller"

[coverage]
enabled = true

[[coverage.fsm]]
instance = "root"
current_state = "state"
next_state = "next_state"
legal_states = ["idle", "run"]

[[coverage.fsm]]
instance = "other"
current_state = "state"
legal_states = ["idle", "done"]

[[library_map]]
library = "vendor"
path = "vendor.fsimlib"
)";

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root(const std::string_view leaf)
{
#if defined(_WIN32)
    return std::filesystem::path { "C:/fsim-fsm-hints" } / leaf;
#else
    return std::filesystem::path { "/fsim-fsm-hints" } / leaf;
#endif
}

fsim::elaboration::VerilogCoverageSource source_for(
    const std::filesystem::path& root, const std::string_view text,
    const std::string_view leaf)
{
    const auto path = root / leaf;
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, path, bytes(text));
    require(identity.ok(), "FSM hint source identity must be valid");
    return { path.generic_string(), std::move(*identity.identity) };
}

const fsim::frontend::DesignUnit& architecture(
    const fsim::frontend::ParseResult& parsed)
{
    const auto found = std::ranges::find(parsed.design.units,
        fsim::frontend::UnitKind::VhdlArchitecture,
        &fsim::frontend::DesignUnit::kind);
    require(found != parsed.design.units.end(),
        "parsed VHDL hint fixture must retain one architecture");
    return *found;
}

std::optional<fsim::project::Config> parse_manifest(
    const std::string_view text, fsim::diagnostic::Engine& diagnostics)
{
    return fsim::project::parse(text, "fsim.toml",
        std::filesystem::path { "/fsim-manifest" }, diagnostics);
}

fsim::elaboration::CoverageInventoryOwner vhdl_owner(
    const fsim::elaboration::VerilogCoverageSource& source,
    const std::string_view instance = "root")
{
    return { 3U, instance, fsim::frontend::Language::Vhdl2008,
        source.source_name, { }, "work", "vhdl:work.controller(rtl)", { } };
}

fsim::elaboration::CoverageInventoryOwner sv_owner(
    const fsim::elaboration::VerilogCoverageSource& source,
    const std::string_view instance = "root")
{
    return { 3U, instance,
        fsim::frontend::Language::SystemVerilog2017, source.source_name,
        { }, "work", "sv:work.controller", { } };
}

const fsim::elaboration::CoverageFsmCurrentStateObject& current_state(
    const fsim::elaboration::CoverageFsmInference& inference)
{
    require(inference.current_state_objects.size() == 1U,
        "one current-state object must be inferred");
    return inference.current_state_objects.front();
}

const fsim::elaboration::CoverageFsmDescriptionDiagnostic&
description_conflict(
    const fsim::elaboration::CoverageFsmInference& inference)
{
    const auto found = std::ranges::find(
        inference.description_diagnostics,
        fsim::elaboration::CoverageFsmDescriptionIssueKind::Conflicting,
        &fsim::elaboration::CoverageFsmDescriptionDiagnostic::kind);
    require(found != inference.description_diagnostics.end()
            && found->subject
                == fsim::elaboration::CoverageFsmDescriptionSubject::LegalStates
            && found->object == "state",
        "expected stable legal-state conflict diagnostic must exist");
    return *found;
}

void test_manifest_surface_and_hint_construction()
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const auto config = parse_manifest(kManifest, diagnostics);
    require(config.has_value() && !diagnostics.has_error()
            && config->coverage.enabled
            && config->coverage.fsm_hints.size() == 2U
            && config->coverage.fsm_hints[0].instance == "root"
            && config->coverage.fsm_hints[0].current_state == "state"
            && config->coverage.fsm_hints[0].next_state
                == std::optional<std::string> { "next_state" }
            && config->coverage.fsm_hints[0].legal_states
                == std::optional<std::vector<std::string>> {
                    { "idle", "run" } },
        "[[coverage.fsm]] must retain one exact language-neutral description");

    std::optional<std::vector<elaboration::CoverageFsmHint>> reference;
    for (const auto standard : { frontend::VhdlStandard::Vhdl1987,
             frontend::VhdlStandard::Vhdl1993,
             frontend::VhdlStandard::Vhdl2000,
             frontend::VhdlStandard::Vhdl2002,
             frontend::VhdlStandard::Vhdl2008 }) {
        const auto parsed = frontend::parse_text("rtl/controller.vhd",
            kVhdlSource, frontend::Language::Vhdl2008, standard);
        require(parsed.ok(),
            "independently authored VHDL FSM source hints must parse");
        const auto built = elaboration::make_coverage_fsm_hints(
            architecture(parsed), config->coverage.fsm_hints);
        require(built.ok() && built.hints->size() == 5U,
            "three VHDL source contributions and two manifest descriptions must be retained");
        require((*built.hints)[0].origin
                    == elaboration::CoverageFsmHintOrigin::VhdlSource
                && (*built.hints)[0].marks_current_state
                && (*built.hints)[0].current_state == "state"
                && (*built.hints)[1].next_state
                    == std::optional<std::string> { "next_state" }
                && (*built.hints)[2].legal_states
                    == std::optional<std::vector<std::string>> {
                        { "idle", "run" } }
                && (*built.hints)[3].origin == elaboration::CoverageFsmHintOrigin::Manifest && (*built.hints)[3].instance == "root",
            "source and manifest hints must share one canonical model without vendor attributes");
        if (!reference) {
            reference = *built.hints;
        } else {
            require(*built.hints == *reference,
                "all retained VHDL profiles must construct identical FSM hints");
        }
    }
}

void test_vhdl_and_manifest_inference()
{
    using namespace fsim;
    diagnostic::Engine diagnostics;
    const auto config = parse_manifest(kManifest, diagnostics);
    require(config.has_value(), "FSM hint manifest must parse for inference");
    const auto source_a = source_for(
        checkout_root("vhdl-a"), kVhdlSource, "rtl/controller.vhd");
    const auto source_b = source_for(
        checkout_root("vhdl-b"), kVhdlSource, "rtl/controller.vhd");
    std::optional<elaboration::CoverageFsmInference> reference;
    for (const auto standard : { frontend::VhdlStandard::Vhdl1987,
             frontend::VhdlStandard::Vhdl1993,
             frontend::VhdlStandard::Vhdl2000,
             frontend::VhdlStandard::Vhdl2002,
             frontend::VhdlStandard::Vhdl2008 }) {
        const auto parsed = frontend::parse_text(source_a.source_name,
            kVhdlSource, frontend::Language::Vhdl2008, standard);
        require(parsed.ok(), "VHDL source-hint inference fixture must parse");
        const auto& unit = architecture(parsed);
        const auto hints = elaboration::make_coverage_fsm_hints(
            unit, config->coverage.fsm_hints);
        require(hints.ok(), "VHDL and manifest hints must validate together");
        const auto built = elaboration::make_coverage_fsm_inference(unit,
            unit.ports, vhdl_owner(source_a), std::span { &source_a, 1U },
            *hints.hints);
        require(built.ok()
                && built.inference->next_state_objects.size() == 1U
                && built.inference->legal_state_sets.size() == 1U,
            "matching source and manifest descriptions must infer one complete FSM");
        const auto& current = current_state(*built.inference);
        const auto& next = built.inference->next_state_objects.front();
        const auto& legal = built.inference->legal_state_sets.front();
        require(current.hierarchy_path == "root.state"
                && current.enum_evidence && !current.pragma_evidence
                && current.vhdl_source_hint_evidence
                && current.manifest_hint_evidence
                && next.hierarchy_path == "root.next_state"
                && !next.assignment_evidence && !next.pragma_evidence
                && next.vhdl_source_hint_evidence
                && next.manifest_hint_evidence
                && legal.states.size() == 2U
                && legal.vhdl_source_hint_evidence
                && legal.manifest_hint_evidence
                && legal.states[0] == current.states[0].id
                && legal.states[1] == current.states[1].id,
            "matching source and manifest provenance must select existing current/next/legal identities");
        if (!reference) {
            reference = *built.inference;
        } else {
            require(*built.inference == *reference,
                "VHDL profile selection must not perturb hint-driven identities");
        }
    }

    const auto relocated = frontend::parse_text(source_b.source_name,
        kVhdlSource, frontend::Language::Vhdl2008);
    const auto relocated_hints = elaboration::make_coverage_fsm_hints(
        architecture(relocated), config->coverage.fsm_hints);
    const auto relocated_inference = elaboration::make_coverage_fsm_inference(
        architecture(relocated), architecture(relocated).ports,
        vhdl_owner(source_b), std::span { &source_b, 1U },
        *relocated_hints.hints);
    require(relocated_inference.ok()
            && relocated_inference.inference == reference,
        "VHDL/manifest hint inference must be checkout-location independent");
}

void test_manifest_hint_is_language_neutral()
{
    using namespace fsim;
    const auto source = source_for(checkout_root("sv"),
        kSystemVerilogSource, "rtl/controller.sv");
    const auto parsed = frontend::parse_text(source.source_name,
        kSystemVerilogSource, frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.size() == 1U,
        "SystemVerilog manifest-hint fixture must parse");
    const std::array manifest_hints {
        project::CoverageFsmHintEntry { "root", "state", "next_state",
            std::vector<std::string> { "IDLE", "RUN" } }
    };
    const auto hints = elaboration::make_coverage_fsm_hints(
        parsed.design.units.front(), manifest_hints);
    const auto& unit = parsed.design.units.front();
    const auto built = elaboration::make_coverage_fsm_inference(unit,
        unit.ports, sv_owner(source), std::span { &source, 1U },
        *hints.hints);
    require(built.ok()
            && current_state(*built.inference).manifest_hint_evidence
            && !current_state(*built.inference).vhdl_source_hint_evidence
            && built.inference->next_state_objects.size() == 1U
            && built.inference->next_state_objects.front()
                .manifest_hint_evidence
            && built.inference->legal_state_sets.size() == 1U
            && built.inference->legal_state_sets.front()
                .manifest_hint_evidence,
        "the same manifest model must describe a SystemVerilog FSM without source semantics");
}

void test_systemverilog_pragma_and_manifest_composition()
{
    using namespace fsim;
    const auto source = source_for(checkout_root("sv-composed"),
        kSystemVerilogPragmaSource, "rtl/controller.sv");
    const auto parsed = frontend::parse_text(source.source_name,
        kSystemVerilogPragmaSource,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok(), "SystemVerilog pragma/manifest fixture must parse");
    const auto& unit = parsed.design.units.front();
    const std::array manifest_hints {
        project::CoverageFsmHintEntry { "root", "state", "next_state",
            std::vector<std::string> { "IDLE", "RUN" } }
    };
    const auto hints = elaboration::make_coverage_fsm_hints(
        unit, manifest_hints);
    const auto matching = elaboration::make_coverage_fsm_inference(unit,
        unit.ports, sv_owner(source), std::span { &source, 1U },
        *hints.hints);
    require(matching.ok()
            && current_state(*matching.inference).pragma_evidence
            && current_state(*matching.inference).manifest_hint_evidence
            && matching.inference->next_state_objects.front().pragma_evidence
            && matching.inference->next_state_objects.front()
                .manifest_hint_evidence
            && matching.inference->legal_state_sets.front().pragma_evidence
            && matching.inference->legal_state_sets.front()
                .manifest_hint_evidence,
        "matching SystemVerilog pragma and manifest descriptions must retain both provenance sources");

    auto conflicting_hints = *hints.hints;
    conflicting_hints.front().legal_states
        = std::vector<std::string> { "IDLE", "DONE" };
    const auto conflicting = elaboration::make_coverage_fsm_inference(unit,
        unit.ports, sv_owner(source), std::span { &source, 1U },
        conflicting_hints);
    require(conflicting.ok()
            && !current_state(*conflicting.inference).pragma_evidence
            && !current_state(*conflicting.inference).manifest_hint_evidence
            && conflicting.inference->next_state_objects.empty()
            && conflicting.inference->legal_state_sets.front().states.size()
                == 3U,
        "conflicting pragma and manifest descriptions must retain only independent enum evidence");
    require(description_conflict(*conflicting.inference).origins
            == std::vector {
                elaboration::CoverageFsmDescriptionOrigin::SystemVerilogPragma,
                elaboration::CoverageFsmDescriptionOrigin::Manifest },
        "pragma/manifest conflicts must retain both canonical origins");
}

void test_hint_validation_and_ambiguity()
{
    using namespace fsim;
    using HintError = elaboration::CoverageFsmHintError;
    using InferenceError = elaboration::CoverageFsmInferenceError;
    const auto source = source_for(checkout_root("negative"),
        kVhdlSource, "rtl/controller.vhd");
    const auto parsed = frontend::parse_text(source.source_name,
        kVhdlSource, frontend::Language::Vhdl2008);
    require(parsed.ok(), "negative VHDL hint fixture must parse");
    auto unit = architecture(parsed);
    const std::array valid_manifest {
        project::CoverageFsmHintEntry { "root", "state", "next_state",
            std::vector<std::string> { "idle", "run" } }
    };

    auto bad_declaration = unit;
    const auto declaration = std::ranges::find_if(
        bad_declaration.vhdl_attributes, [](const auto& attribute) {
            return !attribute.specification
                && attribute.name == "fsm_current_state";
        });
    require(declaration != bad_declaration.vhdl_attributes.end(),
        "recognized VHDL attribute declaration must exist");
    declaration->type.domain = frontend::ValueDomain::Integer;
    require(elaboration::make_coverage_fsm_hints(bad_declaration).error
            == HintError::InvalidAttributeDeclaration,
        "recognized VHDL FSM attributes must be declared as strings");

    auto bad_specification = unit;
    const auto current_attribute = std::ranges::find_if(
        bad_specification.vhdl_attributes, [](const auto& attribute) {
            return attribute.specification
                && attribute.name == "fsm_current_state";
        });
    require(current_attribute != bad_specification.vhdl_attributes.end(),
        "recognized VHDL current-state specification must exist");
    current_attribute->value.decoded_string = "maybe";
    require(elaboration::make_coverage_fsm_hints(bad_specification).error
            == HintError::InvalidAttributeSpecification,
        "VHDL current-state markers must be exact true or false strings");

    auto bad_manifest = valid_manifest;
    bad_manifest[0].legal_states
        = std::vector<std::string> { "idle", "idle" };
    require(elaboration::make_coverage_fsm_hints(unit, bad_manifest).error
            == HintError::InvalidManifestHint,
        "manifest legal-state names must be unique");

    auto limits = elaboration::CoverageFsmHintLimits { };
    limits.maximum_attributes = 0U;
    require(elaboration::make_coverage_fsm_hints(unit, { }, limits).error
            == HintError::ResourceLimit,
        "VHDL FSM attribute ceiling must be enforced");
    limits = { };
    limits.maximum_manifest_hints = 0U;
    require(elaboration::make_coverage_fsm_hints(
                unit, valid_manifest, limits)
                .error
            == HintError::ResourceLimit,
        "manifest FSM hint ceiling must be enforced");
    limits = { };
    limits.maximum_hints = 0U;
    require(elaboration::make_coverage_fsm_hints(unit, { }, limits).error
            == HintError::ResourceLimit,
        "combined FSM hint ceiling must be enforced");
    limits = { };
    limits.maximum_value_bytes = 1U;
    require(elaboration::make_coverage_fsm_hints(unit, { }, limits).error
            == HintError::ResourceLimit,
        "VHDL FSM attribute value ceiling must be enforced transactionally");
    limits = { };
    limits.maximum_attribute_entity_names = 0U;
    require(elaboration::make_coverage_fsm_hints(unit, { }, limits).error
            == HintError::ResourceLimit,
        "VHDL FSM attribute entity-name ceiling must be enforced");
    limits = { };
    limits.maximum_legal_states = 1U;
    require(elaboration::make_coverage_fsm_hints(unit, { }, limits).error
            == HintError::ResourceLimit,
        "aggregate FSM hint legal-state ceiling must be enforced");
    limits = { };
    limits.maximum_name_bytes = 2U;
    require(elaboration::make_coverage_fsm_hints(unit, { }, limits).error
            == HintError::ResourceLimit,
        "FSM hint object-name byte ceiling must be enforced");
    limits = { };
    limits.maximum_instance_bytes = 1U;
    require(elaboration::make_coverage_fsm_hints(
                frontend::DesignUnit { }, valid_manifest, limits)
                .error
            == HintError::ResourceLimit,
        "manifest FSM instance byte ceiling must be enforced transactionally");

    const auto built_hints = elaboration::make_coverage_fsm_hints(
        unit, valid_manifest);
    require(built_hints.ok(), "valid negative-base hints must construct");
    const auto invoke = [&](const std::span<const elaboration::CoverageFsmHint> hints,
                            const elaboration::CoverageFsmInferenceLimits inference_limits = { }) {
        return elaboration::make_coverage_fsm_inference(unit, unit.ports,
            vhdl_owner(source), std::span { &source, 1U }, hints,
            inference_limits);
    };

    auto wrong_origin = *built_hints.hints;
    wrong_origin.front().origin
        = elaboration::CoverageFsmHintOrigin::Manifest;
    require(invoke(wrong_origin).error == InferenceError::InvalidFsmHint,
        "an untrusted manifest hint must carry an exact instance");

    auto unknown = *built_hints.hints;
    unknown.back().current_state = "missing";
    require(invoke(unknown).error == InferenceError::InvalidFsmHint,
        "an applicable hint must resolve its current-state object uniquely");

    auto unknown_legal = *built_hints.hints;
    std::erase_if(unknown_legal, [](const auto& hint) {
        return hint.origin
            == elaboration::CoverageFsmHintOrigin::VhdlSource;
    });
    unknown_legal.back().legal_states
        = std::vector<std::string> { "idle", "missing" };
    require(invoke(unknown_legal).error == InferenceError::InvalidFsmHint,
        "a hint legal set must not manufacture an unknown enum state");

    auto conflicting = *built_hints.hints;
    conflicting.back().legal_states
        = std::vector<std::string> { "idle", "done" };
    const auto conflict = invoke(conflicting);
    require(conflict.ok()
            && !current_state(*conflict.inference)
                .vhdl_source_hint_evidence
            && !current_state(*conflict.inference).manifest_hint_evidence
            && conflict.inference->next_state_objects.empty()
            && conflict.inference->legal_state_sets.front().states.size()
                == 3U,
        "conflicting source and manifest descriptions must suppress explicit evidence without losing enum inference");
    require(description_conflict(*conflict.inference).origins
            == std::vector {
                elaboration::CoverageFsmDescriptionOrigin::VhdlSource,
                elaboration::CoverageFsmDescriptionOrigin::Manifest },
        "VHDL-source/manifest conflicts must retain both canonical origins");

    auto other_instance = *built_hints.hints;
    for (auto& hint : other_instance) {
        if (hint.origin == elaboration::CoverageFsmHintOrigin::Manifest) {
            hint.instance = "other";
        }
    }
    const auto ignored = invoke(other_instance);
    require(ignored.ok()
            && current_state(*ignored.inference)
                .vhdl_source_hint_evidence
            && !current_state(*ignored.inference).manifest_hint_evidence,
        "a valid manifest hint for another instance must not affect this instance");

    auto inference_limits = elaboration::CoverageFsmInferenceLimits { };
    inference_limits.maximum_hints = 0U;
    require(invoke(*built_hints.hints, inference_limits).error
            == InferenceError::ResourceLimit,
        "inference must revalidate its untrusted hint-count ceiling");
    inference_limits = { };
    inference_limits.maximum_hint_legal_states = 1U;
    require(invoke(*built_hints.hints, inference_limits).error
            == InferenceError::ResourceLimit,
        "inference must revalidate its aggregate hint-state ceiling");
}

void test_manifest_schema_failures()
{
    using namespace fsim;
    const auto base = R"(
schema = 3
[project]
top = "controller"
[[library_map]]
library = "vendor"
path = "vendor.fsimlib"
)";
    diagnostic::Engine diagnostics;
    const auto missing = parse_manifest(
        std::string { base }
            + "[[coverage.fsm]]\ninstance = \"root\"\n",
        diagnostics);
    require(!missing && diagnostics.has_error(),
        "a manifest FSM entry must require current_state");

    diagnostic::Engine unknown_diagnostics;
    const auto unknown = parse_manifest(
        std::string { base }
            + "[[coverage.fsm]]\ninstance = \"root\"\n"
              "current_state = \"state\"\nvendor_mode = \"onehot\"\n",
        unknown_diagnostics);
    require(!unknown && unknown_diagnostics.has_error(),
        "unknown vendor keys must not become manifest FSM aliases");
}

} // namespace

int main()
{
    test_manifest_surface_and_hint_construction();
    test_vhdl_and_manifest_inference();
    test_manifest_hint_is_language_neutral();
    test_systemverilog_pragma_and_manifest_composition();
    test_hint_validation_and_ambiguity();
    test_manifest_schema_failures();
    std::cout << "Coverage FSM hint tests passed\n";
}
