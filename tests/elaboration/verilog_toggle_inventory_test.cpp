// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_toggle_inventory.hpp"
#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span { text.data(), text.size() });
}

std::filesystem::path checkout_root(const std::string_view leaf)
{
#if defined(_WIN32)
    return std::filesystem::path { "C:/fsim-toggle" } / leaf;
#else
    return std::filesystem::path { "/fsim-toggle" } / leaf;
#endif
}

constexpr std::string_view kSource = R"(
module leaf(
  input logic in,
  input logic [7:0] data
);
  wire [3:0] n;
  reg [1:0] q;
  integer counter;
  logic [1:0] lane;
  real analog_value;
  string message;
endmodule
)";

fsim::elaboration::VerilogCoverageSource make_source(
    const std::filesystem::path& root)
{
    const auto physical = root / "rtl/leaf.sv";
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, physical, bytes(kSource));
    require(identity.ok(), "toggle fixture source identity must be valid");
    return { physical.generic_string(), std::move(*identity.identity) };
}

fsim::frontend::SourceSpan declaration_span(
    const std::string& source_name, const std::string_view token)
{
    const auto begin = kSource.find(token);
    require(begin != std::string_view::npos,
        "toggle fixture declaration token must exist");
    const auto line = static_cast<std::size_t>(
        1U + std::count(kSource.begin(), kSource.begin() + begin, '\n'));
    return { source_name,
        { begin, line, 1U }, { begin + token.size(), line, 1U }, { }, { } };
}

fsim::frontend::Type packed_type(const fsim::frontend::ValueDomain domain,
    const std::uint64_t width, const std::string_view net = { })
{
    fsim::frontend::Type type;
    type.domain = domain;
    type.spelling = domain == fsim::frontend::ValueDomain::Integer
        ? "integer"
        : "logic";
    type.systemverilog_net_type = net;
    if (width != 1U) {
        type.packed_range = fsim::frontend::PackedRange {
            static_cast<std::int64_t>(width - 1U), 0, true
        };
    }
    return type;
}

fsim::frontend::SignalDeclaration signal(std::string name,
    fsim::frontend::Type type, const fsim::frontend::SourceSpan& span,
    const bool port = false)
{
    fsim::frontend::SignalDeclaration declaration;
    declaration.name = std::move(name);
    declaration.type = std::move(type);
    declaration.is_port = port;
    declaration.span = span;
    return declaration;
}

fsim::frontend::VariableDeclaration variable(std::string name,
    fsim::frontend::Type type, const fsim::frontend::SourceSpan& span)
{
    fsim::frontend::VariableDeclaration declaration;
    declaration.name = std::move(name);
    declaration.type = std::move(type);
    declaration.span = span;
    return declaration;
}

fsim::frontend::DesignUnit unit_for(
    const fsim::elaboration::VerilogCoverageSource& source,
    const fsim::frontend::Language language
    = fsim::frontend::Language::SystemVerilog2017,
    const fsim::frontend::UnitKind kind
    = fsim::frontend::UnitKind::VerilogModule)
{
    using fsim::frontend::ValueDomain;
    fsim::frontend::DesignUnit unit;
    unit.kind = kind;
    unit.language = language;
    unit.library = "work";
    unit.name = "leaf";
    unit.ports.push_back(signal("in", packed_type(ValueDomain::Logic4, 1U),
        declaration_span(source.source_name, "in,"), true));
    unit.ports.back().span.end.offset--;
    unit.ports.push_back(signal("data", packed_type(ValueDomain::Logic4, 8U),
        declaration_span(source.source_name, "data\n"), true));
    unit.ports.back().span.end.offset--;
    unit.signals.push_back(signal("n",
        packed_type(ValueDomain::Logic4, 4U, "wire"),
        declaration_span(source.source_name, "n;")));
    unit.signals.back().span.end.offset--;
    unit.signals.push_back(signal("q", packed_type(ValueDomain::Logic4, 2U),
        declaration_span(source.source_name, "q;")));
    unit.signals.back().span.end.offset--;
    unit.variables.push_back(variable("counter",
        packed_type(ValueDomain::Integer, 32U),
        declaration_span(source.source_name, "counter;")));
    unit.variables.back().span.end.offset--;
    unit.variables.push_back(variable("g[0].lane",
        packed_type(ValueDomain::Logic4, 2U),
        declaration_span(source.source_name, "lane;")));
    unit.variables.back().span.end.offset--;

    auto real_type = packed_type(ValueDomain::Unknown, 1U);
    real_type.systemverilog_scalar
        = fsim::frontend::SystemVerilogScalarKind::Real;
    unit.variables.push_back(variable("analog_value", std::move(real_type),
        declaration_span(source.source_name, "analog_value;")));
    unit.variables.back().span.end.offset--;
    auto string_type = packed_type(ValueDomain::String, 1U);
    unit.variables.push_back(variable("message", std::move(string_type),
        declaration_span(source.source_name, "message;")));
    unit.variables.back().span.end.offset--;
    auto memory_type = packed_type(ValueDomain::Logic4, 8U);
    memory_type.systemverilog_container.emplace();
    unit.variables.push_back(variable("memory", std::move(memory_type),
        declaration_span(source.source_name, "message;")));
    unit.variables.back().span.end.offset--;
    return unit;
}

fsim::elaboration::CoverageInventoryOwner owner_for(
    const fsim::elaboration::VerilogCoverageSource& source,
    const std::string_view instance,
    const fsim::frontend::Language language
    = fsim::frontend::Language::SystemVerilog2017,
    const std::uint32_t specialization = 0U,
    const fsim::frontend::UnitKind kind
    = fsim::frontend::UnitKind::VerilogModule)
{
    const auto unit = kind == fsim::frontend::UnitKind::SystemVerilogInterface
        ? std::string_view { "sv:work.interface(leaf)" }
        : kind == fsim::frontend::UnitKind::SystemVerilogProgram
        ? std::string_view { "sv:work.program(leaf)" }
        : std::string_view { "sv:work.leaf" };
    return { specialization, instance, language, source.source_name, { },
        "work", unit, { } };
}

void test_object_selection_and_bit_inventory()
{
    using namespace fsim;
    using Kind = elaboration::VerilogToggleObjectKind;
    const auto source = make_source(checkout_root("selection"));
    const auto unit = unit_for(source);
    const auto owner = owner_for(source, "top.u",
        frontend::Language::SystemVerilog2017, 7U);
    const auto built = elaboration::make_verilog_toggle_inventory(
        unit, owner, std::span { &source, 1U });
    require(built.ok() && built.inventory->objects.size() == 6U
            && built.inventory->outcomes.size() == 49U,
        "ports, nets, signals, and retained packed variables must receive one bounded bit inventory");
    require(built.inventory->specialization == 7U
            && built.inventory->instance == "top.u"
            && elaboration::is_coverage_instance_identity_valid(
                built.inventory->instance_identity),
        "the inventory must retain its exact elaborated owner");

    const std::vector<std::string> expected_paths {
        "top.u.counter", "top.u.data", "top.u.g[0].lane", "top.u.in",
        "top.u.n", "top.u.q"
    };
    const std::vector expected_kinds { Kind::RetainedVariable, Kind::Port,
        Kind::RetainedVariable, Kind::Port, Kind::Net,
        Kind::RetainedVariable };
    require(std::ranges::equal(built.inventory->objects, expected_paths, { },
                &elaboration::VerilogToggleObject::hierarchy_path,
                std::identity { })
            && std::ranges::equal(built.inventory->objects, expected_kinds, { },
                &elaboration::VerilogToggleObject::kind,
                std::identity { }),
        "object inventory order and semantic kind must be canonical and exact across generated hierarchy");

    std::size_t next_outcome = 0U;
    for (const auto& object : built.inventory->objects) {
        require(object.instance_identity == built.inventory->instance_identity
                && object.specialization == 7U
                && object.first_outcome == next_outcome
                && runtime::is_code_coverage_identity_valid(object.source_point)
                && runtime::is_code_coverage_identity_valid(object.point),
            "each object must authenticate source and concrete instance ownership before dense outcomes");
        for (std::size_t bit = 0U; bit < object.width; ++bit) {
            const auto& outcome
                = built.inventory->outcomes[object.first_outcome + bit];
            require(outcome.point == object.point
                    && outcome.bit_index == bit
                    && outcome.zero_to_one_hits == 0U
                    && outcome.one_to_zero_hits == 0U,
                "every packed bit must map contiguously into an empty Change 7 outcome");
        }
        next_outcome += object.width;
    }
    require(next_outcome == built.inventory->outcomes.size()
            && std::ranges::none_of(built.inventory->objects,
                [](const auto& object) {
                    return object.hierarchy_path.ends_with("analog_value")
                        || object.hierarchy_path.ends_with("message")
                        || object.hierarchy_path.ends_with("memory");
                }),
        "real, string, and whole-container objects must not manufacture binary bins");
}

void test_parsed_semantic_surface()
{
    using namespace fsim;
    using Kind = elaboration::VerilogToggleObjectKind;
    const auto source = make_source(checkout_root("parsed"));
    const auto parsed = frontend::parse_text(source.source_name, kSource,
        frontend::Language::SystemVerilog2017);
    require(parsed.ok() && parsed.design.units.size() == 1U,
        "independently authored toggle corpus must parse into one semantic unit");
    const auto& unit = parsed.design.units.front();
    const auto owner = owner_for(source, "root");
    const auto built = elaboration::make_verilog_toggle_inventory(
        unit, owner, std::span { &source, 1U });
    require(built.ok() && built.inventory->outcomes.size() == 49U,
        "the real parser semantic surface must expose every packed port, net, signal, and retained variable bit");
    const auto find = [&](const std::string_view path) {
        return std::ranges::find(
            built.inventory->objects, path,
            &elaboration::VerilogToggleObject::hierarchy_path);
    };
    const auto input = find("root.in");
    const auto data = find("root.data");
    const auto net = find("root.n");
    const auto signal = find("root.q");
    const auto counter = find("root.counter");
    const auto lane = find("root.lane");
    const bool mapped = input != built.inventory->objects.end()
        && data != built.inventory->objects.end()
        && input->kind == Kind::Port && data->kind == Kind::Port
        && net != built.inventory->objects.end()
        && net->kind == Kind::Net
        && signal != built.inventory->objects.end()
        && counter != built.inventory->objects.end()
        && lane != built.inventory->objects.end()
        && signal->kind == Kind::RetainedVariable
        && counter->kind == Kind::RetainedVariable
        && lane->kind == Kind::RetainedVariable;
    if (!mapped) {
        for (const auto& object : built.inventory->objects) {
            std::cerr << "OBJECT " << object.hierarchy_path << " kind="
                      << static_cast<unsigned>(object.kind) << " width="
                      << object.width << '\n';
        }
    }
    require(mapped,
        "parser categories must map to exact port, net, signal, and retained-variable inventory ownership");
}

void test_instance_identity_relocation_and_order()
{
    using namespace fsim;
    const auto source_a = make_source(checkout_root("relocated-a"));
    const auto source_b = make_source(checkout_root("relocated-b"));
    auto unit_a = unit_for(source_a);
    auto unit_b = unit_for(source_b);
    const auto owner_a = owner_for(source_a, "top.u");
    const auto owner_b = owner_for(source_b, "top.u");
    const auto first = elaboration::make_verilog_toggle_inventory(
        unit_a, owner_a, std::span { &source_a, 1U });
    const auto relocated = elaboration::make_verilog_toggle_inventory(
        unit_b, owner_b, std::span { &source_b, 1U });
    require(first.ok() && relocated.ok()
            && first.inventory == relocated.inventory,
        "toggle inventories must be stable across checkout relocation");

    std::ranges::reverse(unit_a.ports);
    std::ranges::reverse(unit_a.signals);
    std::ranges::reverse(unit_a.variables);
    const auto reordered = elaboration::make_verilog_toggle_inventory(
        unit_a, owner_a, std::span { &source_a, 1U });
    require(reordered.ok() && reordered.inventory == first.inventory,
        "semantic declaration-container order must not affect canonical inventory order");

    const auto other_owner = owner_for(source_a, "top.v",
        frontend::Language::SystemVerilog2017, 1U);
    const auto other = elaboration::make_verilog_toggle_inventory(
        unit_for(source_a), other_owner, std::span { &source_a, 1U });
    require(other.ok()
            && other.inventory->objects.front().source_point
                == first.inventory->objects.front().source_point
            && other.inventory->objects.front().point
                != first.inventory->objects.front().point
            && other.inventory->instance_identity
                != first.inventory->instance_identity,
        "one source declaration must retain its source point but own distinct instance-qualified identities across hierarchy");
}

void test_retained_verilog_profiles_and_unit_kinds()
{
    using namespace fsim;
    const auto source = make_source(checkout_root("profiles"));
    const std::array profiles {
        std::pair { frontend::Language::Verilog2005,
            frontend::UnitKind::VerilogModule },
        std::pair { frontend::Language::SystemVerilog2017,
            frontend::UnitKind::VerilogModule },
        std::pair { frontend::Language::SystemVerilog2017,
            frontend::UnitKind::SystemVerilogInterface },
        std::pair { frontend::Language::SystemVerilog2017,
            frontend::UnitKind::SystemVerilogProgram },
    };
    for (const auto& [language, kind] : profiles) {
        const auto unit = unit_for(source, language, kind);
        const auto owner = owner_for(source, "root", language, 0U, kind);
        const auto built = elaboration::make_verilog_toggle_inventory(
            unit, owner, std::span { &source, 1U });
        require(built.ok() && built.inventory->objects.size() == 6U,
            "every retained Verilog/SystemVerilog design-unit family must share bounded toggle selection");
        const auto expected = language == frontend::Language::Verilog2005
            ? frontend::CodeCoverageLanguage::Verilog
            : frontend::CodeCoverageLanguage::SystemVerilog;
        require(std::ranges::all_of(built.inventory->objects,
                    [&](const auto& object) {
                        return object.language == expected;
                    }),
            "profile families must retain distinct typed source identities");
    }
}

void expect_error(const fsim::elaboration::VerilogToggleInventoryResult& result,
    const fsim::elaboration::VerilogToggleInventoryError error,
    const std::string_view message)
{
    require(!result.ok() && !result.inventory && result.error == error,
        message);
}

void test_transactional_rejections_and_limits()
{
    using namespace fsim;
    using Error = elaboration::VerilogToggleInventoryError;
    const auto source = make_source(checkout_root("negative"));
    auto unit = unit_for(source);
    auto owner = owner_for(source, "top.u");
    const auto build = [&](const auto& test_unit, const auto& test_owner,
                           const auto& test_sources,
                           const elaboration::VerilogToggleInventoryLimits limits
                           = { }) {
        return elaboration::make_verilog_toggle_inventory(
            test_unit, test_owner, test_sources, limits);
    };

    auto wrong_owner = owner;
    wrong_owner.unit = "sv:work.other";
    expect_error(build(unit, wrong_owner, std::span { &source, 1U }),
        Error::InstanceOwnerMismatch,
        "unit/instance owner mismatch must fail transactionally");
    wrong_owner = owner;
    wrong_owner.language = frontend::Language::Vhdl2008;
    expect_error(build(unit, wrong_owner, std::span { &source, 1U }),
        Error::InvalidLanguage,
        "cross-language owners must not enter Verilog toggle discovery");
    auto package = unit;
    package.kind = frontend::UnitKind::SystemVerilogPackage;
    expect_error(build(package, owner, std::span { &source, 1U }),
        Error::InvalidUnitKind,
        "non-instantiated packages must not receive hierarchy-owned toggle objects");
    auto implicit_work = unit;
    implicit_work.library.clear();
    const auto normalized = build(
        implicit_work, owner, std::span { &source, 1U });
    require(normalized.ok(),
        "empty semantic libraries must normalize to the elaborated work owner");

    auto unnamed_source = source;
    unnamed_source.source_name.clear();
    expect_error(build(unit, owner, std::span { &unnamed_source, 1U }),
        Error::EmptySourceName,
        "empty physical source mappings must be rejected");
    const std::array duplicate_sources { source, source };
    expect_error(build(unit, owner, duplicate_sources),
        Error::DuplicateSourceName,
        "duplicate source mapping names must be rejected");
    auto alias_source = source;
    alias_source.source_name += ".alias";
    const std::array aliased_sources { source, alias_source };
    expect_error(build(unit, owner, aliased_sources),
        Error::DuplicateSourceIdentity,
        "duplicate authenticated sources must not alias inventory ownership");
    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    expect_error(build(unit, owner, std::span { &invalid_source, 1U }),
        Error::InvalidSourceIdentity,
        "tampered source identity must fail before object publication");

    auto invalid = unit;
    invalid.ports.front().name.clear();
    expect_error(build(invalid, owner, std::span { &source, 1U }),
        Error::EmptyObjectName,
        "eligible objects require a nonempty semantic name");
    invalid = unit;
    invalid.ports.front().span.source_name = "unknown.sv";
    expect_error(build(invalid, owner, std::span { &source, 1U }),
        Error::UnknownObjectSource,
        "objects require an authenticated physical source mapping");
    invalid = unit;
    invalid.ports.front().span.end.offset
        = source.identity.content_bytes + 1U;
    expect_error(build(invalid, owner, std::span { &source, 1U }),
        Error::InvalidObjectSpan,
        "object spans must remain inside authenticated source bytes");
    invalid = unit;
    invalid.ports.front().type.packed_range.reset();
    invalid.ports.front().type.packed_range_expression.emplace();
    expect_error(build(invalid, owner, std::span { &source, 1U }),
        Error::UnspecializedObjectWidth,
        "toggle discovery requires post-specialization packed widths");
    invalid = unit;
    invalid.signals.front().name = invalid.ports.front().name;
    expect_error(build(invalid, owner, std::span { &source, 1U }),
        Error::DuplicateObjectPath,
        "one concrete hierarchy path may own only one toggle object");

    auto limits = elaboration::VerilogToggleInventoryLimits { };
    limits.maximum_sources = 0U;
    expect_error(build(unit, owner, std::span { &source, 1U }, limits),
        Error::ResourceLimit, "source count ceiling must be enforced");
    limits = { };
    limits.maximum_objects = 2U;
    expect_error(build(unit, owner, std::span { &source, 1U }, limits),
        Error::ResourceLimit, "semantic object ceiling must be enforced");
    limits = { };
    limits.maximum_bits = 48U;
    expect_error(build(unit, owner, std::span { &source, 1U }, limits),
        Error::ResourceLimit, "aggregate bit ceiling must be enforced");
    limits = { };
    limits.maximum_object_width = 31U;
    expect_error(build(unit, owner, std::span { &source, 1U }, limits),
        Error::ResourceLimit, "per-object width ceiling must be enforced");
    limits = { };
    limits.maximum_hierarchy_bytes = 4U;
    expect_error(build(unit, owner, std::span { &source, 1U }, limits),
        Error::ResourceLimit, "hierarchy byte ceiling must be enforced");
    require(elaboration::kVerilogToggleInventorySchema
                == "fsim-verilog-toggle-inventory-v3"
            && elaboration::kVerilogToggleInventoryDiagnostic
                == "FSIM-COV-021",
        "toggle inventory schema and diagnostic identities must remain stable");
}

} // namespace

int main()
{
    test_object_selection_and_bit_inventory();
    test_parsed_semantic_surface();
    test_instance_identity_relocation_and_order();
    test_retained_verilog_profiles_and_unit_kinds();
    test_transactional_rejections_and_limits();
    std::cout << "Verilog toggle inventory tests passed\n";
    return 0;
}
