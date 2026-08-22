// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_instance_identity.hpp"
#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/parser.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

using fsim::elaboration::CoverageInstanceIdentityError;
using fsim::elaboration::CoverageInstanceIdentityInput;
using fsim::elaboration::CoverageInstanceIdentityLimits;
using fsim::frontend::Language;

const std::array<std::pair<std::string, std::string>, 2U> kParameters {
    std::pair { "WIDTH", "u32:8" },
    std::pair { "SIGNED", "bit:0" },
};

CoverageInstanceIdentityInput input(
    const std::string_view path = "top.lanes[3].decoder")
{
    return { path, Language::SystemVerilog2017, "work", "decoder",
        kParameters };
}

void expect_error(const CoverageInstanceIdentityInput& candidate,
    const CoverageInstanceIdentityError error,
    const CoverageInstanceIdentityLimits limits = { })
{
    const auto result = fsim::elaboration::make_coverage_instance_identity(
        candidate, limits);
    assert(!result.ok());
    assert(!result.identity);
    assert(result.error == error);
}

void test_determinism_and_hierarchy()
{
    const auto first = fsim::elaboration::make_coverage_instance_identity(
        input());
    const auto repeated = fsim::elaboration::make_coverage_instance_identity(
        input());
    assert(first.ok() && repeated.ok());
    assert(*first.identity == *repeated.identity);
    assert(fsim::elaboration::is_coverage_instance_identity_valid(
        *first.identity));
    assert(fsim::elaboration::coverage_instance_identity_hex(*first.identity)
               .size()
        == 32U);

    const std::array reversed_parameters {
        kParameters[1], kParameters[0]
    };
    auto reordered = input();
    reordered.parameter_identities = reversed_parameters;
    const auto reordered_result
        = fsim::elaboration::make_coverage_instance_identity(reordered);
    assert(reordered_result.ok());
    assert(*reordered_result.identity == *first.identity);

    const auto different_generate
        = fsim::elaboration::make_coverage_instance_identity(
            input("top.lanes[4].decoder"));
    const auto different_parent
        = fsim::elaboration::make_coverage_instance_identity(
            input("other.lanes[3].decoder"));
    assert(different_generate.ok() && different_parent.ok());
    assert(*different_generate.identity != *first.identity);
    assert(*different_parent.identity != *first.identity);
}

void test_semantic_distinctions()
{
    const auto base
        = fsim::elaboration::make_coverage_instance_identity(input());
    assert(base.ok());
    for (std::size_t selection = 0U; selection < 4U; ++selection) {
        auto changed = input();
        std::vector<std::pair<std::string, std::string>> parameters;
        switch (selection) {
        case 0U:
            changed.language = Language::Vhdl2008;
            break;
        case 1U:
            changed.library = "vendor";
            break;
        case 2U:
            changed.unit = "decoder_alt";
            break;
        case 3U:
            parameters.assign(kParameters.begin(), kParameters.end());
            parameters.front().second = "u32:16";
            changed.parameter_identities = parameters;
            break;
        }
        const auto result
            = fsim::elaboration::make_coverage_instance_identity(changed);
        assert(result.ok());
        assert(*result.identity != *base.identity);
    }
}

void test_rejections_and_limits()
{
    auto candidate = input();
    candidate.hierarchy_path = { };
    expect_error(candidate, CoverageInstanceIdentityError::EmptyHierarchyPath);
    candidate = input(std::string_view { "bad\0path", 8U });
    expect_error(candidate, CoverageInstanceIdentityError::InvalidHierarchyPath);
    candidate = input();
    candidate.language = static_cast<Language>(99);
    expect_error(candidate, CoverageInstanceIdentityError::InvalidLanguage);
    candidate = input();
    candidate.library = { };
    expect_error(candidate, CoverageInstanceIdentityError::EmptyLibrary);
    candidate = input();
    candidate.unit = { };
    expect_error(candidate, CoverageInstanceIdentityError::EmptyUnit);

    const std::array duplicate_parameters {
        std::pair { std::string { "WIDTH" }, std::string { "u32:8" } },
        std::pair { std::string { "WIDTH" }, std::string { "u32:16" } },
    };
    candidate = input();
    candidate.parameter_identities = duplicate_parameters;
    expect_error(candidate,
        CoverageInstanceIdentityError::DuplicateParameterName);
    const std::array empty_parameter {
        std::pair { std::string { }, std::string { "u32:8" } }
    };
    candidate.parameter_identities = empty_parameter;
    expect_error(candidate,
        CoverageInstanceIdentityError::InvalidParameterIdentity);

    auto limits = CoverageInstanceIdentityLimits { };
    limits.maximum_hierarchy_bytes = 3U;
    expect_error(input(), CoverageInstanceIdentityError::ResourceLimit, limits);
    limits = { };
    limits.maximum_parameter_count = 1U;
    expect_error(input(), CoverageInstanceIdentityError::ResourceLimit, limits);
    limits = { };
    limits.maximum_parameter_bytes = 4U;
    expect_error(input(), CoverageInstanceIdentityError::ResourceLimit, limits);
}

std::vector<fsim::elaboration::CoverageInstanceIdentity>
elaborated_generate_identities()
{
    const auto parsed = fsim::frontend::parse_text(
        "generated.sv",
        R"(
module leaf #(parameter int WIDTH = 1) ();
endmodule
module top;
  for (genvar lane = 0; lane < 2; ++lane) begin : lanes
    leaf #(.WIDTH(lane + 1)) decoder();
  end
endmodule
)",
        Language::SystemVerilog2017);
    assert(parsed.ok());
    auto elaborated = fsim::elaboration::elaborate(parsed.design, "top");
    assert(elaborated.ok());
    std::vector<fsim::elaboration::CoverageInstanceInventoryDraft> drafts;
    drafts.reserve(elaborated.design->specializations().size());
    for (const auto& specialization : elaborated.design->specializations()) {
        drafts.push_back({ specialization.id, { } });
    }
    const std::array<fsim::elaboration::CoverageInventorySource, 0U> sources;
    assert(elaborated.design
            ->attach_code_coverage_inventory(sources, drafts)
            .ok());
    std::vector<fsim::elaboration::CoverageInstanceIdentity> identities;
    for (const auto& instance :
        elaborated.design->code_coverage_inventory()->instances) {
        identities.push_back(instance.identity);
    }
    assert(identities.size() == 3U);
    assert(identities[0] != identities[1]);
    assert(identities[1] != identities[2]);
    return identities;
}

void test_real_elaboration_assignment()
{
    assert(elaborated_generate_identities()
        == elaborated_generate_identities());
}

} // namespace

int main()
{
    static_assert(
        fsim::elaboration::kCoverageInstanceIdentityDiagnostic
        == "FSIM-COV-012");
    test_determinism_and_hierarchy();
    test_semantic_distinctions();
    test_rejections_and_limits();
    test_real_elaboration_assignment();
}
