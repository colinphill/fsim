// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {
namespace {

frontend::Instance& association_instance(
    frontend::ParsedDesign& design) {
    const auto architecture = std::ranges::find_if(
        design.units,
        [](const auto& unit) {
          return unit.kind == frontend::UnitKind::VhdlArchitecture
              && unit.primary_name == "association_top";
        });
    assert(architecture != design.units.end());
    assert(architecture->instances.size() == 1);
    return architecture->instances.front();
}

const fsim::elaboration::SpecializationInfo& specialization(
    const fsim::elaboration::ElaborationResult& result) {
    const auto found = std::ranges::find(
        result.design->specializations(),
        "association_top.child",
        &fsim::elaboration::SpecializationInfo::instance);
    assert(found != result.design->specializations().end());
    return *found;
}

bool has_parameter(
    const fsim::elaboration::SpecializationInfo& selected,
    const std::string_view name,
    const std::string_view value) {
    return std::ranges::any_of(
        selected.parameter_values,
        [&](const auto& item) {
          return item.first == name && item.second == value;
        });
}

}  // namespace

void test_vhdl_generic_associations() {
    auto fixture = frontend::parse_text(
        "generic_association_matrix.vhd",
        R"(
entity association_leaf is
  generic (
    first_value : integer;
    second_value : integer := 2);
end entity;
architecture rtl of association_leaf is
begin
end architecture;
entity association_top is
end entity;
architecture rtl of association_top is
begin
  child: entity work.association_leaf(rtl)
    generic map (1);
end architecture;
)",
        frontend::Language::Vhdl2008);
    assert(fixture.ok());

    const auto positive = fsim::elaboration::elaborate(
        fixture.design, "vhdl:work.association_top(rtl)");
    assert(positive.ok());
    assert(has_parameter(
        specialization(positive), "first_value", "1"));
    assert(has_parameter(
        specialization(positive), "second_value", "2"));

    auto unknown = fixture.design;
    association_instance(unknown)
        .parameter_overrides.front().name = "unknown_value";
    const auto unknown_result = fsim::elaboration::elaborate(
        unknown, "vhdl:work.association_top(rtl)");
    assert(!unknown_result.ok());
    assert(has_diagnostic(
        unknown_result, "FSIM-ELAB-GENERIC-001"));

    auto excessive = fixture.design;
    auto& excessive_actuals =
        association_instance(excessive).parameter_overrides;
    excessive_actuals.push_back(excessive_actuals.front());
    excessive_actuals.push_back(excessive_actuals.front());
    const auto excessive_result = fsim::elaboration::elaborate(
        excessive, "vhdl:work.association_top(rtl)");
    assert(!excessive_result.ok());
    assert(has_diagnostic(
        excessive_result, "FSIM-ELAB-GENERIC-001"));

    auto duplicate = fixture.design;
    auto& duplicate_actuals =
        association_instance(duplicate).parameter_overrides;
    duplicate_actuals.front().name = "first_value";
    duplicate_actuals.push_back(duplicate_actuals.front());
    const auto duplicate_result = fsim::elaboration::elaborate(
        duplicate, "vhdl:work.association_top(rtl)");
    assert(!duplicate_result.ok());
    assert(has_diagnostic(
        duplicate_result, "FSIM-ELAB-GENERIC-002"));

    auto ordered = fixture.design;
    auto& ordered_actuals =
        association_instance(ordered).parameter_overrides;
    ordered_actuals.front().name = "first_value";
    ordered_actuals.push_back(ordered_actuals.front());
    ordered_actuals.back().name.reset();
    const auto ordered_result = fsim::elaboration::elaborate(
        ordered, "vhdl:work.association_top(rtl)");
    assert(!ordered_result.ok());
    assert(has_diagnostic(
        ordered_result, "FSIM-ELAB-GENERIC-003"));

    auto dynamic = fixture.design;
    auto& dynamic_actual = association_instance(dynamic)
        .parameter_overrides.front().value;
    dynamic_actual.kind = frontend::ExpressionKind::Identifier;
    dynamic_actual.text = "runtime_signal";
    const auto dynamic_result = fsim::elaboration::elaborate(
        dynamic, "vhdl:work.association_top(rtl)");
    assert(!dynamic_result.ok());
    assert(has_diagnostic(
        dynamic_result, "FSIM-ELAB-GENERIC-004"));

    const auto invalid_boundaries = frontend::parse_text(
        "invalid_dependent_boundaries.vhd",
        R"(
entity dependent_boundary_leaf is
  generic (width : positive := 4);
  port (input_value : in bit_vector(width - 1 downto 0));
end entity;
architecture rtl of dependent_boundary_leaf is
begin
end architecture;
entity dependent_boundary_top is
end entity;
architecture rtl of dependent_boundary_top is
  signal ascending_value : bit_vector(0 to 3);
  signal narrow_value : bit_vector(2 downto 0);
begin
  direction_child: entity work.dependent_boundary_leaf(rtl)
    generic map (width => 4)
    port map (input_value => ascending_value);
  width_child: entity work.dependent_boundary_leaf(rtl)
    generic map (width => 4)
    port map (input_value => narrow_value);
end architecture;
)",
        frontend::Language::Vhdl2008);
    assert(invalid_boundaries.ok());
    const auto invalid_boundary_result =
        fsim::elaboration::elaborate(
            invalid_boundaries.design,
            "vhdl:work.dependent_boundary_top(rtl)");
    assert(!invalid_boundary_result.ok());
    assert(has_diagnostic(
        invalid_boundary_result, "FSIM-ELAB-BIND-020"));
    assert(has_diagnostic(
        invalid_boundary_result, "FSIM-ELAB-BIND-031"));
}

}  // namespace fsim::tests::elaboration
