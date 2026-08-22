// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/parser.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsim::elaboration::CodeCoverageInventory;
using fsim::elaboration::CoverageInstanceInventoryDraft;
using fsim::elaboration::CoverageInventoryError;
using fsim::elaboration::CoverageInventoryOwner;
using fsim::elaboration::CoverageInventoryPointDraft;
using fsim::elaboration::CoverageInventorySource;
using fsim::frontend::CodeCoverageConstructKind;
using fsim::frontend::CodeCoverageLanguage;
using fsim::frontend::CodeCoverageSourceSpan;
using fsim::frontend::Language;
using fsim::runtime::CodeCoverageMetric;

constexpr std::string_view kSource = R"(
module leaf;
  initial begin
    $display("leaf");
    if (1) $display("taken");
  end
endmodule
module top;
  leaf a();
  leaf b();
endmodule
)";

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

CoverageInventorySource source(
    const std::string_view source_name = "design.sv",
    const std::string_view logical_name = "rtl/design.sv",
    const std::string_view contents = kSource)
{
    const auto identity
        = fsim::frontend::make_code_coverage_source_identity(
            std::filesystem::path { "/checkout" },
            std::filesystem::path { "/checkout" } / logical_name,
            bytes(contents));
    assert(identity.ok());
    return { std::string { source_name }, *identity.identity };
}

CoverageInventoryPointDraft point(
    const CoverageInventorySource& owned_source,
    const CodeCoverageConstructKind construct,
    const CodeCoverageMetric metric,
    const CodeCoverageSourceSpan span,
    const std::uint64_t line)
{
    const auto identity = fsim::frontend::make_code_coverage_point_identity(
        owned_source.identity, CodeCoverageLanguage::SystemVerilog,
        construct, span);
    assert(identity.ok());
    return { *identity.identity, metric, 0U, span, line };
}

struct Corpus {
    std::vector<CoverageInventorySource> sources;
    std::array<CoverageInventoryOwner, 3U> owners;
    std::vector<CoverageInstanceInventoryDraft> drafts;
};

Corpus corpus()
{
    Corpus result;
    result.sources.push_back(source());
    result.owners = { CoverageInventoryOwner {
                          0U, "top", Language::SystemVerilog2017,
                          "design.sv", { }, "work", "top", { } },
        CoverageInventoryOwner { 1U, "top.a",
            Language::SystemVerilog2017, "design.sv", { }, "work", "leaf",
            { } },
        CoverageInventoryOwner { 2U, "top.b",
            Language::SystemVerilog2017, "design.sv", { }, "work", "leaf",
            { } } };
    const auto statement = point(result.sources.front(),
        CodeCoverageConstructKind::Statement,
        CodeCoverageMetric::Statement, { 20U, 28U }, 3U);
    const auto branch = point(result.sources.front(),
        CodeCoverageConstructKind::BranchTrueArm,
        CodeCoverageMetric::Branch, { 50U, 60U }, 4U);
    // Deliberately reverse both instance and point order. The builder owns
    // deterministic normalization and global counter assignment.
    result.drafts = {
        { 2U, { branch, statement } },
        { 0U, { } },
        { 1U, { branch, statement } },
    };
    return result;
}

void expect_error(
    const fsim::elaboration::CoverageInventoryBuildResult& result,
    const CoverageInventoryError error)
{
    assert(!result.ok());
    assert(result.error == error);
    assert(!result.inventory);
}

void test_builder_and_validation()
{
    auto input = corpus();
    const auto built = fsim::elaboration::make_code_coverage_inventory(
        input.sources, input.drafts, input.owners);
    assert(built.ok());
    assert(built.inventory->instances.size() == 3U);
    assert(built.inventory->instances[0].instance == "top");
    assert(fsim::elaboration::is_coverage_instance_identity_valid(
        built.inventory->instances[0].identity));
    assert(built.inventory->instances[0].points.empty());
    assert(built.inventory->instances[1].instance == "top.a");
    assert(built.inventory->instances[2].instance == "top.b");
    assert(built.inventory->instances[1].identity
        != built.inventory->instances[2].identity);
    assert(built.inventory->total_points == 4U);
    std::uint32_t counter = 0U;
    for (const auto& instance : built.inventory->instances) {
        for (const auto& owned_point : instance.points) {
            assert(owned_point.point.counter.value == counter++);
        }
    }
    assert(fsim::elaboration::validate_code_coverage_inventory(
        *built.inventory, input.owners)
            .ok());

    auto reordered = input;
    std::swap(reordered.drafts.front(), reordered.drafts.back());
    const auto rebuilt = fsim::elaboration::make_code_coverage_inventory(
        reordered.sources, reordered.drafts, reordered.owners);
    assert(rebuilt.ok());
    assert(*rebuilt.inventory == *built.inventory);

    // The generic ownership seam accepts every retained language family;
    // profile-specific legality remains frozen by Changes 5 and 6.
    for (const auto language : { Language::Vhdl2008,
             Language::Verilog2005, Language::SystemVerilog2017 }) {
        const std::array owners { CoverageInventoryOwner {
            0U, "root", language, "design.sv", { }, "work", "root",
            { } } };
        const std::array drafts { CoverageInstanceInventoryDraft { 0U, { } } };
        const auto family = fsim::elaboration::make_code_coverage_inventory(
            input.sources, drafts, owners);
        assert(family.ok());
        assert(family.inventory->instances.front().language == language);
    }
}

void test_rejections()
{
    auto input = corpus();
    auto sources = input.sources;
    sources.front().source_name.clear();
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     sources, input.drafts, input.owners),
        CoverageInventoryError::EmptySourceName);

    sources = input.sources;
    sources.push_back(sources.front());
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     sources, input.drafts, input.owners),
        CoverageInventoryError::DuplicateSourceName);
    sources.back().source_name = "alias.sv";
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     sources, input.drafts, input.owners),
        CoverageInventoryError::DuplicateSourceIdentity);
    sources = input.sources;
    sources.front().identity.digest = { };
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     sources, input.drafts, input.owners),
        CoverageInventoryError::InvalidSourceIdentity);

    auto owners = input.owners;
    owners[1].specialization = 7U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, owners),
        CoverageInventoryError::InvalidOwner);
    owners = input.owners;
    owners[1].source = "other.sv";
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, owners),
        CoverageInventoryError::PointSourceOwnershipMismatch);
    owners = input.owners;
    owners[1].library = { };
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, owners),
        CoverageInventoryError::InvalidInstanceIdentity);
    owners = input.owners;
    owners[2].instance = owners[1].instance;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, owners),
        CoverageInventoryError::DuplicateInstanceIdentity);

    auto drafts = input.drafts;
    drafts.pop_back();
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::MissingInstance);
    drafts = input.drafts;
    drafts[0].specialization = 1U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::DuplicateInstance);
    drafts = input.drafts;
    drafts[0].specialization = 9U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::UnknownInstance);

    drafts = input.drafts;
    drafts[0].points.front().id = { };
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::InvalidPointIdentity);
    drafts = input.drafts;
    drafts[0].points.front().metric = CodeCoverageMetric::Line;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::InvalidMetric);
    drafts = input.drafts;
    drafts[0].points.front().source_index = 1U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::UnknownPointSource);
    drafts = input.drafts;
    drafts[0].points.front().span = { 4U, 4U };
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::InvalidPointSpan);
    drafts = input.drafts;
    drafts[0].points.front().line = 0U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::InvalidLine);
    drafts = input.drafts;
    drafts[0].points.push_back(drafts[0].points.front());
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, drafts, input.owners),
        CoverageInventoryError::DuplicatePoint);

    auto limits = fsim::elaboration::CoverageInventoryLimits { };
    limits.maximum_sources = 0U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, input.owners, limits),
        CoverageInventoryError::ResourceLimit);
    limits = { };
    limits.maximum_instances = 2U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, input.owners, limits),
        CoverageInventoryError::ResourceLimit);
    limits = { };
    limits.maximum_points = 3U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, input.owners, limits),
        CoverageInventoryError::ResourceLimit);
    limits = { };
    limits.maximum_line_number = 2U;
    expect_error(fsim::elaboration::make_code_coverage_inventory(
                     input.sources, input.drafts, input.owners, limits),
        CoverageInventoryError::InvalidLine);
}

void test_elaborated_design_attachment()
{
    const auto parsed = fsim::frontend::parse_text(
        "design.sv", kSource, Language::SystemVerilog2017);
    assert(parsed.ok());
    auto elaborated = fsim::elaboration::elaborate(parsed.design, "top");
    assert(elaborated.ok());
    auto design = std::move(*elaborated.design);
    assert(design.specializations().size() == 3U);
    assert(!design.code_coverage_inventory());

    const std::array sources { source() };
    const auto statement = point(sources.front(),
        CodeCoverageConstructKind::Statement,
        CodeCoverageMetric::Statement, { 20U, 28U }, 3U);
    const auto branch = point(sources.front(),
        CodeCoverageConstructKind::BranchTrueArm,
        CodeCoverageMetric::Branch, { 50U, 60U }, 4U);
    std::vector<CoverageInstanceInventoryDraft> drafts;
    for (const auto& specialization : design.specializations()) {
        drafts.push_back(CoverageInstanceInventoryDraft {
            specialization.id,
            specialization.instance != design.top()
                ? std::vector<CoverageInventoryPointDraft> {
                      branch, statement }
                : std::vector<CoverageInventoryPointDraft> { } });
    }
    assert(design.attach_code_coverage_inventory(sources, drafts).ok());
    assert(design.code_coverage_inventory());
    assert(design.code_coverage_inventory()->instances.size() == 3U);
    assert(design.code_coverage_inventory()->total_points == 4U);
    const CodeCoverageInventory retained = *design.code_coverage_inventory();

    drafts.pop_back();
    const auto rejected
        = design.attach_code_coverage_inventory(sources, drafts);
    assert(rejected.error == CoverageInventoryError::MissingInstance);
    assert(*design.code_coverage_inventory() == retained);

    auto state = design.state();
    assert(state.code_coverage_inventory == retained);
    auto restored
        = fsim::elaboration::ElaboratedDesign::from_state(std::move(state));
    assert(restored);
    assert(restored->code_coverage_inventory());
    assert(*restored->code_coverage_inventory() == retained);

    auto invalid_state = restored->state();
    invalid_state.code_coverage_inventory->instances[1]
        .points.front()
        .point.counter.value = 99U;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(invalid_state)));
}

void test_stored_validation_rejections()
{
    auto input = corpus();
    const auto built = fsim::elaboration::make_code_coverage_inventory(
        input.sources, input.drafts, input.owners);
    assert(built.ok());

    auto inventory = *built.inventory;
    inventory.instances[1].instance = "wrong";
    assert(fsim::elaboration::validate_code_coverage_inventory(
               inventory, input.owners)
               .error
        == CoverageInventoryError::InstanceOwnerMismatch);
    inventory = *built.inventory;
    inventory.instances[1].identity = { };
    assert(fsim::elaboration::validate_code_coverage_inventory(
               inventory, input.owners)
               .error
        == CoverageInventoryError::InvalidInstanceIdentity);
    inventory = *built.inventory;
    std::swap(inventory.instances[1], inventory.instances[2]);
    assert(fsim::elaboration::validate_code_coverage_inventory(
               inventory, input.owners)
               .error
        == CoverageInventoryError::NonCanonicalOrder);
    inventory = *built.inventory;
    inventory.instances[1].points.front().point.counter.value = 9U;
    assert(fsim::elaboration::validate_code_coverage_inventory(
               inventory, input.owners)
               .error
        == CoverageInventoryError::CounterOwnershipMismatch);
    inventory = *built.inventory;
    ++inventory.total_points;
    assert(fsim::elaboration::validate_code_coverage_inventory(
               inventory, input.owners)
               .error
        == CoverageInventoryError::TotalPointMismatch);
}

} // namespace

int main()
{
    test_builder_and_validation();
    test_rejections();
    test_elaborated_design_attachment();
    test_stored_validation_rejections();
}
