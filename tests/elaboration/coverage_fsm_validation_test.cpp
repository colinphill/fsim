// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_fsm_validation.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

fsim::elaboration::CoverageInstanceIdentity instance(
    const std::size_t index = 0U)
{
    return { 0x1790000000000012ULL,
        static_cast<std::uint64_t>(index + 1U) };
}

fsim::elaboration::CoverageFsmDescriptionIssueCandidate candidate(
    const fsim::elaboration::CoverageFsmDescriptionIssueKind kind,
    const fsim::elaboration::CoverageFsmDescriptionSubject subject,
    std::vector<fsim::elaboration::CoverageFsmDescriptionOrigin> origins,
    const std::size_t instance_index = 0U)
{
    return { kind, subject, instance(instance_index), "top.codec", "state",
        std::move(origins) };
}

void test_kinds_codes_coalescing_and_stability()
{
    using namespace fsim::elaboration;
    using Kind = CoverageFsmDescriptionIssueKind;
    using Origin = CoverageFsmDescriptionOrigin;
    using Subject = CoverageFsmDescriptionSubject;
    std::vector inputs {
        candidate(Kind::Conflicting, Subject::LegalStates,
            { Origin::Manifest, Origin::Case }),
        candidate(Kind::Ambiguous, Subject::NextState,
            { Origin::Assignment }),
        candidate(Kind::Incomplete, Subject::LegalStates,
            { Origin::SystemVerilogPragma }),
        candidate(Kind::Conflicting, Subject::LegalStates,
            { Origin::VhdlSource, Origin::Case }),
    };
    const auto first = make_coverage_fsm_description_diagnostics(inputs);
    std::ranges::reverse(inputs);
    const auto second = make_coverage_fsm_description_diagnostics(inputs);
    require(first.ok() && second.ok()
            && *first.diagnostics == *second.diagnostics
            && first.diagnostics->size() == 3U,
        "diagnostic input order must not change stable coalesced results");
    const auto conflict = std::ranges::find(
        *first.diagnostics, Kind::Conflicting,
        &CoverageFsmDescriptionDiagnostic::kind);
    require(conflict != first.diagnostics->end()
            && conflict->origins
                == std::vector { Origin::Case, Origin::VhdlSource,
                    Origin::Manifest },
        "equal conflict subjects must union and canonicalize their origins");
    require(coverage_fsm_description_diagnostic_code(Kind::Ambiguous)
                == "FSIM-COV-028"
            && coverage_fsm_description_diagnostic_code(Kind::Incomplete)
                == "FSIM-COV-029"
            && coverage_fsm_description_diagnostic_code(Kind::Conflicting)
                == "FSIM-COV-030",
        "each description issue kind must own one stable diagnostic code");

    auto sibling_input = inputs;
    for (auto& entry : sibling_input) {
        entry.instance_identity = instance(1U);
        entry.instance = "top.other";
    }
    const auto sibling
        = make_coverage_fsm_description_diagnostics(sibling_input);
    require(sibling.ok()
            && sibling.diagnostics->front().id
                != first.diagnostics->front().id,
        "sibling instance identity and path must qualify diagnostic identity");
}

void test_invalid_inputs_are_transactional()
{
    using namespace fsim::elaboration;
    using Error = CoverageFsmDescriptionValidationError;
    using Kind = CoverageFsmDescriptionIssueKind;
    using Origin = CoverageFsmDescriptionOrigin;
    using Subject = CoverageFsmDescriptionSubject;
    const auto rejects = [](CoverageFsmDescriptionIssueCandidate input,
                             const Error error,
                             const std::string_view message,
                             CoverageFsmDescriptionValidationLimits limits
                             = { }) {
        const std::array inputs { std::move(input) };
        const auto result
            = make_coverage_fsm_description_diagnostics(inputs, limits);
        require(!result.ok() && !result.diagnostics
                && result.error == error,
            message);
    };
    auto input = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    input.kind = static_cast<Kind>(0xffU);
    rejects(input, Error::InvalidKind, "unknown issue kind must be rejected");
    input = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    input.subject = static_cast<Subject>(0xffU);
    rejects(input, Error::InvalidSubject,
        "unknown issue subject must be rejected");
    input = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    input.instance_identity = { };
    rejects(input, Error::InvalidInstanceIdentity,
        "invalid instance identity must be rejected");
    input = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    input.instance.clear();
    rejects(input, Error::InvalidInstance,
        "empty instance path must be rejected");
    input = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    input.object.clear();
    rejects(input, Error::InvalidObject, "empty object must be rejected");
    input = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    input.origins.clear();
    rejects(input, Error::MissingOrigin,
        "diagnostics without evidence origins must be rejected");
    input = candidate(Kind::Ambiguous, Subject::NextState,
        { static_cast<Origin>(0xffU) });
    rejects(input, Error::InvalidOrigin,
        "unknown evidence origin must be rejected");
}

void test_resource_ceilings()
{
    using namespace fsim::elaboration;
    using Error = CoverageFsmDescriptionValidationError;
    using Kind = CoverageFsmDescriptionIssueKind;
    using Origin = CoverageFsmDescriptionOrigin;
    using Subject = CoverageFsmDescriptionSubject;
    const auto base = candidate(
        Kind::Ambiguous, Subject::NextState, { Origin::Assignment });
    const auto rejects = [&](CoverageFsmDescriptionValidationLimits limits,
                             const std::string_view message) {
        const std::array inputs { base };
        const auto result
            = make_coverage_fsm_description_diagnostics(inputs, limits);
        require(result.error == Error::ResourceLimit && !result.diagnostics,
            message);
    };
    CoverageFsmDescriptionValidationLimits limits;
    limits.maximum_candidates = 0U;
    rejects(limits, "candidate ceiling must be enforced");
    limits = { };
    limits.maximum_diagnostics = 0U;
    rejects(limits, "diagnostic ceiling must be enforced");
    limits = { };
    limits.maximum_origins = 0U;
    rejects(limits, "origin ceiling must be enforced");
    limits = { };
    limits.maximum_origins = 1U;
    const std::array merged_origins {
        candidate(Kind::Conflicting, Subject::LegalStates,
            { Origin::Case }),
        candidate(Kind::Conflicting, Subject::LegalStates,
            { Origin::Manifest }),
    };
    const auto merged
        = make_coverage_fsm_description_diagnostics(merged_origins, limits);
    require(merged.error == Error::ResourceLimit && !merged.diagnostics,
        "coalesced-origin ceiling must apply before unbounded accumulation");
    limits = { };
    limits.maximum_instance_bytes = 4U;
    rejects(limits, "instance path ceiling must be enforced");
    limits = { };
    limits.maximum_object_bytes = 4U;
    rejects(limits, "object name ceiling must be enforced");
}

} // namespace

int main()
{
    test_kinds_codes_coalescing_and_stability();
    test_invalid_inputs_are_transactional();
    test_resource_ceilings();
    std::cout << "coverage FSM description validation tests passed\n";
    return 0;
}
