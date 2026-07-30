// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

namespace {

void append_design(
    fsim::frontend::ParsedDesign& destination,
    fsim::frontend::ParsedDesign source) {
    for (auto& unit : source.units) {
        destination.units.push_back(std::move(unit));
    }
}

const fsim::elaboration::SpecializationInfo&
specialization(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view path) {
    const auto found = std::ranges::find_if(
        result.design->specializations(),
        [&](const auto& candidate) {
          return candidate.instance == path;
        });
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

void test_vhdl_configurations() {
    auto leaf = fsim::frontend::parse_text(
        "configured_leaf.vhd",
        R"(
entity configured_leaf is
  generic (amount : integer := 1);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of configured_leaf is
begin
  output_value <= input_value + amount;
end architecture;
architecture fast of configured_leaf is
begin
  output_value <= input_value + amount + 100;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto hierarchy = fsim::frontend::parse_text(
        "configured_hierarchy.vhd",
        R"(
entity configured_wrapper is
end entity;
architecture rtl of configured_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  component configured_leaf is
    generic (amount : integer := 1);
    port (
      input_value : in integer;
      output_value : out integer);
  end component;
  for all : configured_leaf
    use entity work.configured_leaf(fast);
begin
  nested: configured_leaf
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity configured_top is
end entity;
architecture rtl of configured_top is
  signal exact_input : integer;
  signal exact_output : integer;
  signal remaining_input : integer;
  signal remaining_output : integer;
  signal direct_input : integer;
  signal direct_output : integer;
  component configured_leaf is
    generic (component_amount : integer := 1);
    port (
      component_input : in integer;
      component_output : out integer);
  end component;
  for exact_child : configured_leaf
    use entity work.configured_leaf(fast)
      generic map (amount => component_amount)
      port map (
        input_value => component_input,
        output_value => component_output);
  for others : configured_leaf
    use entity work.configured_leaf(rtl);
begin
  exact_child: configured_leaf
    generic map (3)
    port map (exact_input, exact_output);
  remaining_child: configured_leaf
    generic map (component_amount => 4)
    port map (
      component_input => remaining_input,
      component_output => remaining_output);
  direct_child: entity work.configured_leaf(rtl)
    generic map (amount => 5)
    port map (
      input_value => direct_input,
      output_value => direct_output);
  wrapper_child: entity work.configured_wrapper(rtl)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto configuration = fsim::frontend::parse_text(
        "selected_configuration.vhd",
        R"(
configuration selected_configuration of configured_top is
  for rtl
    for exact_child : configured_leaf
      use entity work.configured_leaf(rtl)
        generic map (amount => component_amount)
        port map (
          input_value => component_input,
          output_value => component_output);
    end for;
    for others : configured_leaf
      use entity work.configured_leaf(fast);
    end for;
  end for;
end configuration;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(leaf.ok() && hierarchy.ok() && configuration.ok());
    append_design(leaf.design, std::move(hierarchy.design));
    append_design(leaf.design, std::move(configuration.design));

    const auto architecture_result =
        fsim::elaboration::elaborate(
            leaf.design,
            "vhdl:work.configured_top(rtl)");
    assert(architecture_result.ok());
    assert(
        specialization(
            architecture_result,
            "configured_top.exact_child").unit
        == "vhdl:work.configured_leaf(fast)");
    assert(
        specialization(
            architecture_result,
            "configured_top.remaining_child").unit
        == "vhdl:work.configured_leaf(rtl)");
    assert(
        specialization(
            architecture_result,
            "configured_top.direct_child").unit
        == "vhdl:work.configured_leaf(rtl)");
    assert(
        specialization(
            architecture_result,
            "configured_top.wrapper_child.nested").unit
        == "vhdl:work.configured_leaf(fast)");
    assert(has_parameter(
        specialization(
            architecture_result,
            "configured_top.exact_child"),
        "amount",
        "3"));

    const auto configured_result =
        fsim::elaboration::elaborate(
            leaf.design,
            "vhdl:work.selected_configuration");
    if (!configured_result.ok()) {
        for (const auto& diagnostic :
             configured_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(configured_result.ok());
    assert(configured_result.design->top()
           == "selected_configuration");
    assert(
        specialization(
            configured_result,
            "selected_configuration.exact_child").unit
        == "vhdl:work.configured_leaf(rtl)");
    assert(
        specialization(
            configured_result,
            "selected_configuration.remaining_child").unit
        == "vhdl:work.configured_leaf(fast)");
    assert(
        specialization(
            configured_result,
            "selected_configuration.direct_child").unit
        == "vhdl:work.configured_leaf(rtl)");
    assert(
        specialization(
            configured_result,
            "selected_configuration.wrapper_child.nested").unit
        == "vhdl:work.configured_leaf(fast)");
    const auto& root =
        specialization(
            configured_result, "selected_configuration");
    assert(std::ranges::any_of(
        root.parameter_identity_values,
        [](const auto& item) {
          return item.first == "__configuration"
              && item.second.find(
                     "vhdl-configuration-v2")
                  != std::string::npos;
        }));
    assert(std::ranges::find(
        root.source_dependencies,
        "selected_configuration.vhd")
        != root.source_dependencies.end());
    for (const auto child : {
             "selected_configuration.exact_child",
             "selected_configuration.remaining_child"}) {
        assert(std::ranges::find(
            specialization(configured_result, child)
                .source_dependencies,
            "selected_configuration.vhd")
            != specialization(configured_result, child)
                   .source_dependencies.end());
    }
    assert(std::ranges::find(
        specialization(
            configured_result,
            "selected_configuration.direct_child")
            .source_dependencies,
        "selected_configuration.vhd")
        == specialization(
               configured_result,
               "selected_configuration.direct_child")
               .source_dependencies.end());
    assert(has_parameter(
        specialization(
            configured_result,
            "selected_configuration.exact_child"),
        "amount",
        "3"));

    auto nested = fsim::frontend::parse_text(
        "nested_configuration.vhd",
        R"(
entity nested_leaf is
  generic (amount : integer := 1);
end entity;
architecture rtl of nested_leaf is
begin
end architecture;
architecture fast of nested_leaf is
begin
end architecture;

entity stable_leaf is
end entity;
architecture rtl of stable_leaf is
begin
end architecture;

entity referenced_wrapper is
  generic (bias : integer := 1);
end entity;
architecture rtl of referenced_wrapper is
  component nested_leaf is
    generic (selected_amount : integer := 1);
  end component;
  for all : nested_leaf
    use entity work.nested_leaf(rtl)
      generic map (amount => selected_amount);
begin
  nested: nested_leaf
    generic map (selected_amount => bias)
    port map ();
end architecture;

entity nested_configuration_top is
end entity;
architecture rtl of nested_configuration_top is
  component nested_leaf is
    generic (selected_amount : integer := 1);
  end component;
  component stable_leaf is
  end component;
  component referenced_wrapper is
    generic (wrapper_bias : integer := 1);
  end component;
begin
  outer_block: block
  begin
    loop_gen: for index in 0 to 1 generate
      generated: nested_leaf
        generic map (selected_amount => index)
        port map ();
    end generate;
    open_child: stable_leaf
      port map ();
  end block;
  if_gen: if 1 = 1 generate
    if_child: nested_leaf
      generic map (selected_amount => 8)
      port map ();
  end generate;
  case_gen: case 1 generate
    selected_case: when 1 =>
      case_child: nested_leaf
        generic map (selected_amount => 9)
        port map ();
  end generate;
  referenced_child: referenced_wrapper
    generic map (wrapper_bias => 7)
    port map ();
end architecture;

configuration nested_configuration of nested_configuration_top is
  for rtl
    for all : nested_leaf
      use entity work.nested_leaf(rtl)
        generic map (amount => selected_amount);
    end for;
    for outer_block
      for loop_gen(1)
        for all : nested_leaf
          use entity work.nested_leaf(fast)
            generic map (amount => selected_amount);
        end for;
      end for;
      for open_child : stable_leaf
        use open;
      end for;
    end for;
    for if_gen
      for all : nested_leaf
        use entity work.nested_leaf(fast)
          generic map (amount => selected_amount);
      end for;
    end for;
    for selected_case
      for all : nested_leaf
        use entity work.nested_leaf(fast)
          generic map (amount => selected_amount);
      end for;
    end for;
    for referenced_child : referenced_wrapper
      use configuration work.referenced_wrapper_configuration
        generic map (bias => wrapper_bias);
    end for;
  end for;
end configuration;
)",
        fsim::frontend::Language::Vhdl2008);
    auto referenced_configuration =
        fsim::frontend::parse_text(
            "referenced_wrapper_configuration.vhd",
            R"(
configuration referenced_wrapper_configuration
  of referenced_wrapper is
  for rtl
    for all : nested_leaf
      use entity work.nested_leaf(fast)
        generic map (amount => selected_amount);
    end for;
  end for;
end configuration;
)",
            fsim::frontend::Language::Vhdl2008);
    if (!nested.ok()) {
        for (const auto& diagnostic : nested.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(nested.ok() && referenced_configuration.ok());
    append_design(
        nested.design,
        std::move(referenced_configuration.design));
    const auto nested_result =
        fsim::elaboration::elaborate(
            nested.design,
            "vhdl:work.nested_configuration");
    if (!nested_result.ok()) {
        for (const auto& diagnostic :
             nested_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(nested_result.ok());
    assert(
        specialization(
            nested_result,
            "nested_configuration.outer_block.loop_gen[0].generated")
            .unit
        == "vhdl:work.nested_leaf(rtl)");
    assert(
        specialization(
            nested_result,
            "nested_configuration.outer_block.loop_gen[1].generated")
            .unit
        == "vhdl:work.nested_leaf(fast)");
    assert(
        specialization(
            nested_result,
            "nested_configuration.outer_block.open_child")
            .unit
        == "vhdl:work.stable_leaf(rtl)");
    assert(
        specialization(
            nested_result,
            "nested_configuration.if_gen.if_child")
            .unit
        == "vhdl:work.nested_leaf(fast)");
    assert(
        specialization(
            nested_result,
            "nested_configuration.selected_case.case_child")
            .unit
        == "vhdl:work.nested_leaf(fast)");
    assert(
        specialization(
            nested_result,
            "nested_configuration.referenced_child.nested")
            .unit
        == "vhdl:work.nested_leaf(fast)");
    assert(has_parameter(
        specialization(
            nested_result,
            "nested_configuration.referenced_child"),
        "bias",
        "7"));
    assert(has_parameter(
        specialization(
            nested_result,
            "nested_configuration.referenced_child.nested"),
        "amount",
        "7"));
    const auto& referenced_child =
        specialization(
            nested_result,
            "nested_configuration.referenced_child");
    assert(std::ranges::find(
        referenced_child.source_dependencies,
        "referenced_wrapper_configuration.vhd")
        != referenced_child.source_dependencies.end());
    assert(std::ranges::any_of(
        referenced_child.parameter_identity_values,
        [](const auto& item) {
          return item.first == "__component"
              && item.second.find(
                     "vhdl-configuration-binding-v2")
                  != std::string::npos;
        }));

    const auto find_configuration =
        [](fsim::frontend::ParsedDesign& design,
            const std::string_view name)
            -> fsim::frontend::DesignUnit& {
          const auto found = std::ranges::find_if(
              design.units,
              [&](const auto& unit) {
                return unit.kind
                        == fsim::frontend::UnitKind::
                            VhdlConfiguration
                    && unit.name == name;
              });
          assert(found != design.units.end());
          return *found;
        };

    auto invalid_scopes = nested.design;
    auto& invalid_scope_root =
        find_configuration(
            invalid_scopes,
            "nested_configuration")
            .vhdl_configuration->block;
    auto duplicate_outer =
        invalid_scope_root.block_configurations.front();
    invalid_scope_root.block_configurations.push_back(
        duplicate_outer);
    fsim::frontend::VhdlBlockConfiguration missing_scope;
    missing_scope.block_name = "absent_block";
    missing_scope.span = invalid_scope_root.span;
    invalid_scope_root.block_configurations.push_back(
        missing_scope);
    invalid_scope_root.block_configurations.front()
        .block_configurations.front()
        .generate_index =
        fsim::frontend::Expression{
            fsim::frontend::ExpressionKind::Identifier,
            "dynamic_index",
            {},
            invalid_scope_root.span};
    const auto invalid_scope_result =
        fsim::elaboration::elaborate(
            invalid_scopes,
            "vhdl:work.nested_configuration");
    assert(!invalid_scope_result.ok());
    assert(has_diagnostic(
        invalid_scope_result,
        "FSIM-ELAB-VHCONFIG-010"));
    assert(has_diagnostic(
        invalid_scope_result,
        "FSIM-ELAB-VHCONFIG-015"));

    auto missing_reference = nested.design;
    auto& missing_reference_rules =
        find_configuration(
            missing_reference,
            "nested_configuration")
            .vhdl_configuration->block
            .component_configurations;
    const auto missing_reference_rule =
        std::ranges::find_if(
            missing_reference_rules,
            [](const auto& rule) {
              return rule.binding.kind
                  == fsim::frontend::
                      VhdlBindingAspectKind::Configuration;
            });
    assert(
        missing_reference_rule
        != missing_reference_rules.end());
    missing_reference_rule->binding.configuration_name =
        "work.absent_configuration";
    const auto missing_reference_result =
        fsim::elaboration::elaborate(
            missing_reference,
            "vhdl:work.nested_configuration");
    assert(!missing_reference_result.ok());
    assert(has_diagnostic(
        missing_reference_result,
        "FSIM-ELAB-VHCONFIG-013"));

    auto ambiguous_reference = nested.design;
    const auto referenced_unit =
        std::ranges::find_if(
            ambiguous_reference.units,
            [](const auto& unit) {
              return unit.name
                  == "referenced_wrapper_configuration";
            });
    assert(referenced_unit != ambiguous_reference.units.end());
    const auto duplicate_reference = *referenced_unit;
    ambiguous_reference.units.push_back(
        duplicate_reference);
    const auto ambiguous_reference_result =
        fsim::elaboration::elaborate(
            ambiguous_reference,
            "vhdl:work.nested_configuration");
    assert(!ambiguous_reference_result.ok());
    assert(has_diagnostic(
        ambiguous_reference_result,
        "FSIM-ELAB-VHCONFIG-014"));

    auto invalid_open = nested.design;
    auto& open_rules =
        find_configuration(
            invalid_open,
            "nested_configuration")
            .vhdl_configuration->block
            .component_configurations;
    const auto open_leaf =
        std::ranges::find_if(
            open_rules,
            [](const auto& rule) {
              return rule.component_name == "nested_leaf";
            });
    assert(open_leaf != open_rules.end());
    open_leaf->binding.kind =
        fsim::frontend::VhdlBindingAspectKind::Open;
    open_leaf->binding.entity_name.clear();
    open_leaf->binding.architecture_name.clear();
    const auto nested_leaf_entity =
        std::ranges::find_if(
            invalid_open.units,
            [](const auto& unit) {
              return unit.kind
                      == fsim::frontend::UnitKind::VhdlEntity
                  && unit.name == "nested_leaf";
            });
    assert(nested_leaf_entity != invalid_open.units.end());
    const auto duplicate_open_entity = *nested_leaf_entity;
    invalid_open.units.push_back(duplicate_open_entity);
    const auto invalid_open_result =
        fsim::elaboration::elaborate(
            invalid_open,
            "vhdl:work.nested_configuration");
    assert(!invalid_open_result.ok());
    assert(has_diagnostic(
        invalid_open_result,
        "FSIM-ELAB-VHCOMP-005"));

    auto invalid = fsim::frontend::parse_text(
        "invalid_configuration_bindings.vhd",
        R"(
entity invalid_leaf is
  generic (amount : integer := 1);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of invalid_leaf is
begin
  output_value <= input_value + amount;
end architecture;
entity invalid_configuration_top is
end entity;
architecture rtl of invalid_configuration_top is
  signal input_value : integer;
  signal output_value : integer;
  component invalid_leaf is
    generic (component_amount : integer := 1);
    port (
      component_input : in integer;
      component_output : out integer);
  end component;
  component absent_component is
  end component;
  for missing_label : invalid_leaf
    use entity work.invalid_leaf(rtl);
  for all : invalid_leaf
    use entity work.invalid_leaf(rtl);
  for actual : invalid_leaf
    use entity work.missing_leaf(rtl);
  for actual : absent_component
    use entity work.invalid_leaf(rtl);
  for actual : invalid_leaf
    use entity work.invalid_leaf(rtl)
      generic map (amount => component_amount)
      port map (
        input_value => missing_component_port,
        output_value => component_output);
begin
  actual: invalid_leaf
    generic map (2)
    port map (input_value, output_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto invalid_result =
        fsim::elaboration::elaborate(
            invalid.design,
            "vhdl:work.invalid_configuration_top(rtl)");
    assert(!invalid_result.ok());
    for (const auto code : {
             "FSIM-ELAB-VHCONFIG-005",
             "FSIM-ELAB-VHCONFIG-006",
             "FSIM-ELAB-VHCONFIG-007",
             "FSIM-ELAB-VHCONFIG-008"}) {
        assert(has_diagnostic(invalid_result, code));
    }

    auto invalid_map = fsim::frontend::parse_text(
        "invalid_configuration_map.vhd",
        R"(
entity invalid_map_leaf is
  generic (amount : integer := 1);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of invalid_map_leaf is
begin
  output_value <= input_value + amount;
end architecture;
entity invalid_configuration_map_top is
end entity;
architecture rtl of invalid_configuration_map_top is
  signal input_value : integer;
  signal output_value : integer;
  component invalid_map_leaf is
    generic (component_amount : integer := 1);
    port (
      component_input : in integer;
      component_output : out integer);
  end component;
  for actual : invalid_map_leaf
    use entity work.invalid_map_leaf(rtl)
      generic map (amount => component_amount)
      port map (
        input_value => component_input,
        output_value => component_output);
begin
  actual: invalid_map_leaf
    generic map (unknown_amount => 2)
    port map (
      unknown_input => input_value,
      component_output => output_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid_map.ok());
    const auto invalid_map_result =
        fsim::elaboration::elaborate(
            invalid_map.design,
            "vhdl:work.invalid_configuration_map_top(rtl)");
    assert(!invalid_map_result.ok());
    assert(has_diagnostic(
        invalid_map_result,
        "FSIM-ELAB-VHCOMP-008"));
    assert(has_diagnostic(
        invalid_map_result,
        "FSIM-ELAB-VHCOMP-009"));

    auto missing_entity = fsim::frontend::parse_text(
        "missing_configuration_entity.vhd",
        R"(
configuration missing_entity_configuration of absent is
  for rtl
  end for;
end configuration;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(missing_entity.ok());
    const auto missing_entity_result =
        fsim::elaboration::elaborate(
            missing_entity.design,
            "vhdl:work.missing_entity_configuration");
    assert(!missing_entity_result.ok());
    assert(has_diagnostic(
        missing_entity_result,
        "FSIM-ELAB-VHCONFIG-002"));

    auto missing_architecture =
        fsim::frontend::parse_text(
            "missing_configuration_architecture.vhd",
            R"(
entity missing_architecture_top is
end entity;
architecture rtl of missing_architecture_top is
begin
end architecture;
configuration missing_architecture_configuration
  of missing_architecture_top is
  for absent
  end for;
end configuration;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(missing_architecture.ok());
    const auto missing_architecture_result =
        fsim::elaboration::elaborate(
            missing_architecture.design,
            "vhdl:work.missing_architecture_configuration");
    assert(!missing_architecture_result.ok());
    assert(has_diagnostic(
        missing_architecture_result,
        "FSIM-ELAB-VHCONFIG-003"));

    auto missing_hir = leaf.design;
    const auto configuration_unit =
        std::ranges::find_if(
            missing_hir.units,
            [](const auto& unit) {
              return unit.kind
                  == fsim::frontend::UnitKind::
                      VhdlConfiguration;
            });
    assert(configuration_unit != missing_hir.units.end());
    configuration_unit->vhdl_configuration.reset();
    const auto missing_hir_result =
        fsim::elaboration::elaborate(
            missing_hir,
            "vhdl:work.selected_configuration");
    assert(!missing_hir_result.ok());
    assert(has_diagnostic(
        missing_hir_result,
        "FSIM-ELAB-VHCONFIG-001"));

    auto ambiguous_binding = leaf.design;
    const auto fast_architecture =
        std::ranges::find_if(
            ambiguous_binding.units,
            [](const auto& unit) {
              return unit.kind
                         == fsim::frontend::UnitKind::
                             VhdlArchitecture
                  && unit.primary_name
                      == "configured_leaf"
                  && unit.name == "fast";
            });
    assert(fast_architecture != ambiguous_binding.units.end());
    ambiguous_binding.units.push_back(*fast_architecture);
    const auto ambiguous_binding_result =
        fsim::elaboration::elaborate(
            ambiguous_binding,
            "vhdl:work.selected_configuration");
    assert(!ambiguous_binding_result.ok());
    assert(has_diagnostic(
        ambiguous_binding_result,
        "FSIM-ELAB-VHCONFIG-009"));

    auto ambiguous_root = leaf.design;
    const auto top_architecture =
        std::ranges::find_if(
            ambiguous_root.units,
            [](const auto& unit) {
              return unit.kind
                         == fsim::frontend::UnitKind::
                             VhdlArchitecture
                  && unit.primary_name == "configured_top"
                  && unit.name == "rtl";
            });
    assert(top_architecture != ambiguous_root.units.end());
    ambiguous_root.units.push_back(*top_architecture);
    const auto ambiguous_root_result =
        fsim::elaboration::elaborate(
            ambiguous_root,
            "vhdl:work.selected_configuration");
    assert(!ambiguous_root_result.ok());
    assert(has_diagnostic(
        ambiguous_root_result,
        "FSIM-ELAB-VHCONFIG-004"));

    auto cross_language = fsim::frontend::parse_text(
        "cross_language_configuration.sv",
        R"(
module cross_language_configuration;
  configured_child child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(cross_language.ok());
    append_design(
        cross_language.design, leaf.design);
    const std::vector<fsim::elaboration::Binding>
        cross_language_binding{{
            "cross_language_configuration.child",
            "vhdl:work.selected_configuration",
            std::nullopt}};
    const auto cross_language_result =
        fsim::elaboration::elaborate(
            cross_language.design,
            "sv:work.cross_language_configuration",
            cross_language_binding);
    assert(!cross_language_result.ok());
    assert(has_diagnostic(
        cross_language_result,
        "FSIM-ELAB-BIND-016"));

    auto recursive = fsim::frontend::parse_text(
        "recursive_configuration.vhd",
        R"(
entity recursive_configuration_top is
end entity;
architecture rtl of recursive_configuration_top is
  component recursive_configuration_top is
  end component;
begin
  nested: recursive_configuration_top
    port map ();
end architecture;
configuration recursive_configuration
  of recursive_configuration_top is
  for rtl
    for all : recursive_configuration_top
      use configuration work.recursive_configuration;
    end for;
  end for;
end configuration;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(recursive.ok());
    const auto recursive_result =
        fsim::elaboration::elaborate(
            recursive.design,
            "vhdl:work.recursive_configuration");
    assert(!recursive_result.ok());
    assert(has_diagnostic(
        recursive_result, "FSIM-ELAB-HIER-002"));
}

}  // namespace fsim::tests::elaboration
