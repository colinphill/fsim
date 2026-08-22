// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vhdl_toggle_inventory.hpp"
#include "fsim/frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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
    return std::filesystem::path { "C:/fsim-vhdl-toggle" } / leaf;
#else
    return std::filesystem::path { "/fsim-vhdl-toggle" } / leaf;
#endif
}

constexpr std::string_view kSource = R"(entity leaf is
  port (
    clock : in bit;
    data : in std_logic_vector(7 downto 0);
    ready : out boolean);
end entity leaf;

architecture rtl of leaf is
  signal state : std_logic_vector(3 downto 0);
  signal flag : bit;
  signal count : integer;
begin
  worker : process
    variable automatic_local : integer;
  begin
    wait;
  end process worker;
end architecture rtl;
)";

fsim::elaboration::VerilogCoverageSource make_source(
    const std::filesystem::path& root)
{
    const auto physical = root / "rtl/leaf.vhd";
    auto identity = fsim::frontend::make_code_coverage_source_identity(
        root, physical, bytes(kSource));
    require(identity.ok(), "VHDL toggle source identity must be valid");
    return { physical.generic_string(), std::move(*identity.identity) };
}

fsim::frontend::SourceSpan declaration_span(
    const std::string& source_name, const std::string_view token)
{
    const auto begin = kSource.find(token);
    require(begin != std::string_view::npos,
        "VHDL toggle declaration token must exist");
    const auto line = static_cast<std::size_t>(
        1U + std::count(kSource.begin(), kSource.begin() + begin, '\n'));
    return { source_name,
        { begin, line, 1U }, { begin + token.size(), line, 1U }, { }, { } };
}

fsim::elaboration::CoverageInventoryOwner owner_for(
    const fsim::elaboration::VerilogCoverageSource& source,
    const std::string_view instance = "top.u",
    const std::uint32_t specialization = 7U)
{
    return { specialization, instance, fsim::frontend::Language::Vhdl2008,
        source.source_name, { }, "work", "vhdl:work.leaf(rtl)", { } };
}

std::pair<fsim::frontend::DesignUnit, std::vector<fsim::frontend::SignalDeclaration>>
parsed_units(const fsim::elaboration::VerilogCoverageSource& source,
    const fsim::frontend::VhdlStandard standard
    = fsim::frontend::VhdlStandard::Vhdl2008)
{
    const auto parsed = fsim::frontend::parse_text(source.source_name, kSource,
        fsim::frontend::Language::Vhdl2008, standard);
    require(parsed.ok() && parsed.design.units.size() == 2U,
        "independently authored VHDL toggle corpus must parse");
    require(parsed.design.units.front().kind
                == fsim::frontend::UnitKind::VhdlEntity
            && parsed.design.units.back().kind
                == fsim::frontend::UnitKind::VhdlArchitecture,
        "VHDL toggle corpus must retain separate entity and architecture units");
    return { parsed.design.units.back(), parsed.design.units.front().ports };
}

fsim::frontend::VariableDeclaration shared_integer(
    const fsim::elaboration::VerilogCoverageSource& source)
{
    fsim::frontend::Type type;
    type.domain = fsim::frontend::ValueDomain::Integer;
    type.spelling = "integer";
    fsim::frontend::VariableDeclaration variable;
    variable.name = "shared_count";
    variable.type = std::move(type);
    variable.span = declaration_span(source.source_name, "count : integer");
    variable.vhdl_shared = true;
    return variable;
}

void test_selection_profiles_and_dense_outcomes()
{
    using namespace fsim;
    using Kind = elaboration::VhdlToggleObjectKind;
    constexpr std::array standards {
        frontend::VhdlStandard::Vhdl1987,
        frontend::VhdlStandard::Vhdl1993,
        frontend::VhdlStandard::Vhdl2000,
        frontend::VhdlStandard::Vhdl2002,
        frontend::VhdlStandard::Vhdl2008,
    };
    const auto source = make_source(checkout_root("profiles"));
    std::vector<runtime::CodeCoveragePointId> baseline_points;
    for (const auto standard : standards) {
        auto [architecture, ports] = parsed_units(source, standard);
        architecture.variables.push_back(shared_integer(source));
        require(architecture.variables.size() == 1U
                && architecture.processes.front().variables.size() == 1U,
            "only the shared architecture variable may enter retained-object selection");
        const auto built = elaboration::make_vhdl_toggle_inventory(
            architecture, ports, owner_for(source),
            std::span { &source, 1U });
        require(built.ok() && built.inventory->objects.size() == 7U
                && built.inventory->outcomes.size() == 79U
                && built.inventory->standard == standard,
            "every retained VHDL profile must inventory ports, signals, and shared retained variables equivalently");
        const std::vector<std::string> paths { "top.u.clock", "top.u.count",
            "top.u.data", "top.u.flag", "top.u.ready",
            "top.u.shared_count", "top.u.state" };
        const std::vector kinds { Kind::Port, Kind::Signal, Kind::Port,
            Kind::Signal, Kind::Port, Kind::RetainedVariable, Kind::Signal };
        require(std::ranges::equal(built.inventory->objects, paths, { },
                    &elaboration::VhdlToggleObject::hierarchy_path,
                    std::identity { })
                && std::ranges::equal(built.inventory->objects, kinds, { },
                    &elaboration::VhdlToggleObject::kind,
                    std::identity { }),
            "VHDL object order and semantic kinds must be canonical");
        std::size_t next = 0U;
        std::vector<runtime::CodeCoveragePointId> points;
        for (const auto& object : built.inventory->objects) {
            require(object.first_outcome == next
                    && object.instance_identity
                        == built.inventory->instance_identity
                    && object.specialization == 7U
                    && runtime::is_code_coverage_identity_valid(
                        object.source_point)
                    && runtime::is_code_coverage_identity_valid(object.point),
                "every VHDL object must retain exact source, instance, and dense outcome ownership");
            for (std::size_t bit = 0U; bit < object.width; ++bit) {
                const auto& outcome
                    = built.inventory->outcomes[object.first_outcome + bit];
                require(outcome.point == object.point
                        && outcome.bit_index == bit,
                    "every VHDL packed bit must own one empty directional outcome pair");
            }
            next += object.width;
            points.push_back(object.source_point);
        }
        if (baseline_points.empty()) {
            baseline_points = std::move(points);
        } else {
            require(points == baseline_points,
                "VHDL source point identity must not vary by retained revision");
        }
    }
}

void test_hierarchy_relocation_and_order()
{
    using namespace fsim;
    const auto source_a = make_source(checkout_root("relocated-a"));
    const auto source_b = make_source(checkout_root("relocated-b"));
    auto [architecture_a, ports_a] = parsed_units(source_a);
    auto [architecture_b, ports_b] = parsed_units(source_b);
    architecture_a.variables.push_back(shared_integer(source_a));
    architecture_b.variables.push_back(shared_integer(source_b));
    const auto first = elaboration::make_vhdl_toggle_inventory(architecture_a,
        ports_a, owner_for(source_a), std::span { &source_a, 1U });
    const auto relocated = elaboration::make_vhdl_toggle_inventory(
        architecture_b, ports_b, owner_for(source_b),
        std::span { &source_b, 1U });
    require(first.ok() && relocated.ok()
            && first.inventory->objects == relocated.inventory->objects
            && first.inventory->outcomes == relocated.inventory->outcomes,
        "VHDL toggle inventories must be checkout-location independent");

    std::ranges::reverse(ports_a);
    std::ranges::reverse(architecture_a.signals);
    std::ranges::reverse(architecture_a.variables);
    const auto reordered = elaboration::make_vhdl_toggle_inventory(
        architecture_a, ports_a, owner_for(source_a),
        std::span { &source_a, 1U });
    require(reordered.ok() && reordered.inventory == first.inventory,
        "VHDL declaration-container order must not change the inventory");

    const auto sibling = elaboration::make_vhdl_toggle_inventory(
        architecture_a, ports_a, owner_for(source_a, "top.v", 8U),
        std::span { &source_a, 1U });
    require(sibling.ok()
            && sibling.inventory->objects.front().source_point
                == first.inventory->objects.front().source_point
            && sibling.inventory->objects.front().point
                != first.inventory->objects.front().point,
        "one VHDL declaration point must remain shared while concrete hierarchy points differ");
}

void test_entity_source_ownership()
{
    using namespace fsim;
    const auto root = checkout_root("split-source");
    const auto architecture_source = make_source(root);
    const auto entity_path = root / "rtl/leaf_entity.vhd";
    auto entity_identity = frontend::make_code_coverage_source_identity(
        root, entity_path, bytes(kSource));
    require(entity_identity.ok(),
        "split entity source identity must be valid");
    const elaboration::VerilogCoverageSource entity_source {
        entity_path.generic_string(), std::move(*entity_identity.identity)
    };
    auto [architecture, ports] = parsed_units(architecture_source);
    for (auto& port : ports) {
        port.span.physical_source_name = entity_source.source_name;
    }
    const std::array sources { architecture_source, entity_source };
    auto owner = owner_for(architecture_source);
    const auto rejected = elaboration::make_vhdl_toggle_inventory(
        architecture, ports, owner, sources);
    const std::array dependencies { entity_source.source_name };
    owner.source_dependencies = dependencies;
    const auto accepted = elaboration::make_vhdl_toggle_inventory(
        architecture, ports, owner, sources);
    require(!rejected.ok()
            && rejected.error
                == elaboration::VhdlToggleInventoryError::ObjectSourceOwnershipMismatch
            && accepted.ok(),
        "entity ports must be authenticated through the architecture owner's exact source dependency");
}

void test_type_and_retention_selection()
{
    using namespace fsim;
    const auto source = make_source(checkout_root("selection"));
    auto [architecture, ports] = parsed_units(source);
    const auto scalar = architecture.signals.back().type;

    auto ordinary = shared_integer(source);
    ordinary.name = "ordinary";
    ordinary.vhdl_shared = false;
    architecture.variables.push_back(ordinary);
    auto file = shared_integer(source);
    file.name = "file_object";
    file.vhdl_file = true;
    architecture.variables.push_back(file);
    auto protected_object = shared_integer(source);
    protected_object.name = "protected_object";
    protected_object.type.vhdl_protected
        = std::make_shared<frontend::VhdlProtectedInfo>();
    architecture.variables.push_back(protected_object);
    auto physical = shared_integer(source);
    physical.name = "physical_object";
    physical.type.vhdl_physical.emplace();
    architecture.variables.push_back(physical);
    architecture.variables.push_back(shared_integer(source));

    require(elaboration::is_vhdl_toggle_type(scalar)
            && !elaboration::is_vhdl_toggle_type(protected_object.type)
            && !elaboration::is_vhdl_toggle_type(physical.type),
        "only directly packed VHDL value domains may manufacture binary toggle bins");
    const auto built = elaboration::make_vhdl_toggle_inventory(architecture,
        ports, owner_for(source), std::span { &source, 1U });
    require(built.ok()
            && std::ranges::none_of(built.inventory->objects,
                [](const auto& object) {
                    return object.hierarchy_path.ends_with("ordinary")
                        || object.hierarchy_path.ends_with("file_object")
                        || object.hierarchy_path.ends_with("protected_object")
                        || object.hierarchy_path.ends_with("physical_object")
                        || object.hierarchy_path.ends_with("automatic_local");
                }),
        "ordinary locals, files, protected/physical objects, and process locals must remain outside default VHDL toggle selection");
}

void test_rejections_and_limits()
{
    using namespace fsim;
    using Error = elaboration::VhdlToggleInventoryError;
    const auto source = make_source(checkout_root("negative"));
    auto [architecture, ports] = parsed_units(source);
    architecture.variables.push_back(shared_integer(source));
    const auto build = [&](const frontend::DesignUnit& unit,
                           std::span<const frontend::SignalDeclaration> input_ports,
                           const elaboration::CoverageInventoryOwner& owner,
                           std::span<const elaboration::VerilogCoverageSource> input_sources,
                           const elaboration::VhdlToggleInventoryLimits limits = { }) {
        return elaboration::make_vhdl_toggle_inventory(
            unit, input_ports, owner, input_sources, limits);
    };
    const auto good_owner = owner_for(source);

    auto wrong_language = architecture;
    wrong_language.language = frontend::Language::SystemVerilog2017;
    require(build(wrong_language, ports, good_owner,
                std::span { &source, 1U })
                .error
            == Error::InvalidLanguage,
        "non-VHDL semantic input must be rejected transactionally");
    auto wrong_standard = architecture;
    wrong_standard.vhdl_standard = static_cast<frontend::VhdlStandard>(255U);
    require(build(wrong_standard, ports, good_owner,
                std::span { &source, 1U })
                .error
            == Error::InvalidStandard,
        "unknown VHDL revisions must be rejected");
    auto wrong_kind = architecture;
    wrong_kind.kind = frontend::UnitKind::VhdlEntity;
    require(build(wrong_kind, ports, good_owner,
                std::span { &source, 1U })
                .error
            == Error::InvalidUnitKind,
        "only a specialized VHDL architecture may own the inventory");
    auto wrong_owner = good_owner;
    wrong_owner.unit = "vhdl:work.leaf(other)";
    require(build(architecture, ports, wrong_owner,
                std::span { &source, 1U })
                .error
            == Error::InstanceOwnerMismatch,
        "noncanonical VHDL owner identity must be rejected");

    auto invalid_source = source;
    ++invalid_source.identity.content_bytes;
    require(build(architecture, ports, good_owner,
                std::span { &invalid_source, 1U })
                .error
            == Error::InvalidSourceIdentity,
        "unauthenticated VHDL sources must be rejected");
    const std::array duplicates { source, source };
    require(build(architecture, ports, good_owner, duplicates).error
            == Error::DuplicateSourceName,
        "duplicate VHDL source mappings must be rejected");

    auto unknown_ports = ports;
    unknown_ports.front().span.source_name = "unknown.vhd";
    unknown_ports.front().span.physical_source_name = "unknown.vhd";
    require(build(architecture, unknown_ports, good_owner,
                std::span { &source, 1U })
                .error
            == Error::UnknownObjectSource,
        "an unmapped entity-port source must be rejected");
    auto bad_span = ports;
    bad_span.front().span.end.offset = kSource.size() + 1U;
    require(build(architecture, bad_span, good_owner,
                std::span { &source, 1U })
                .error
            == Error::InvalidObjectSpan,
        "out-of-source VHDL declaration spans must be rejected");
    auto duplicate_path = architecture;
    duplicate_path.signals.push_back(duplicate_path.signals.front());
    require(build(duplicate_path, ports, good_owner,
                std::span { &source, 1U })
                .error
            == Error::DuplicateObjectPath,
        "duplicate VHDL hierarchical objects must not publish partial output");

    auto source_limit = elaboration::VhdlToggleInventoryLimits { };
    source_limit.maximum_sources = 0U;
    auto object_limit = elaboration::VhdlToggleInventoryLimits { };
    object_limit.maximum_objects = 0U;
    auto bit_limit = elaboration::VhdlToggleInventoryLimits { };
    bit_limit.maximum_bits = 1U;
    auto width_limit = elaboration::VhdlToggleInventoryLimits { };
    width_limit.maximum_object_width = 1U;
    require(build(architecture, ports, good_owner,
                std::span { &source, 1U }, source_limit)
                    .error
                == Error::ResourceLimit
            && build(architecture, ports, good_owner,
                   std::span { &source, 1U }, object_limit)
                    .error
                == Error::ResourceLimit
            && build(architecture, ports, good_owner,
                   std::span { &source, 1U }, bit_limit)
                    .error
                == Error::ResourceLimit
            && build(architecture, ports, good_owner,
                   std::span { &source, 1U }, width_limit)
                    .error
                == Error::ResourceLimit,
        "source, semantic-object, aggregate-bit, and per-object width ceilings must be enforced");

    auto empty_library = architecture;
    empty_library.library.clear();
    require(build(empty_library, ports, good_owner,
                std::span { &source, 1U })
                .ok(),
        "empty semantic libraries must normalize to the elaborated work owner");
}

} // namespace

int main()
{
    test_selection_profiles_and_dense_outcomes();
    test_hierarchy_relocation_and_order();
    test_entity_source_ownership();
    test_type_and_retention_selection();
    test_rejections_and_limits();
    std::cout << "VHDL toggle inventory tests passed\n";
}
