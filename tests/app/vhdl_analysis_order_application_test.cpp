// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct Source {
    std::string name;
    std::string library;
    std::string text;
    fsim::project::Language language { fsim::project::Language::vhdl };
    std::string standard { "2008" };
};

struct CheckResult {
    bool accepted { };
    std::vector<std::string> codes;
    std::vector<std::string> units;
    std::vector<std::string> unit_profiles;
    std::vector<std::string> source_profiles;
    bool deferred_constant_hir { };
    bool attribute_group_hir { };
    bool nested_attribute_group_hir { };
    bool psl_hir { };
};

CheckResult check(
    const std::filesystem::path& directory,
    const std::string_view case_name,
    const std::vector<Source>& sources)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = std::string { case_name };
    config.build.jobs = 8;

    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto path = directory
            / (std::string { case_name } + "-" + std::to_string(index)
                + "-" + sources[index].name);
        std::ofstream output(path, std::ios::binary);
        output << sources[index].text;
        assert(output.good());

        fsim::project::SourceSet source_set;
        source_set.language = sources[index].language;
        source_set.standard = sources[index].standard;
        source_set.library = sources[index].library;
        source_set.compilation_unit = "file";
        source_set.files.push_back(path);
        config.source_sets.push_back(std::move(source_set));
    }

    fsim::diagnostic::Engine diagnostics;
    auto checked = fsim::app::check_project(config, diagnostics);
    CheckResult result;
    result.accepted = checked.has_value();
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        result.codes.push_back(diagnostic.code);
        if (!checked && case_name == "valid") {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    if (checked) {
        for (const auto& source : checked->hdl_sources) {
            result.source_profiles.push_back(
                source.language + ":" + source.standard);
        }
        for (const auto& unit : checked->parsed.units) {
            result.units.push_back(unit.library + ":" + unit.name);
            if (unit.language == fsim::frontend::Language::Vhdl2008) {
                result.unit_profiles.push_back(
                    "vhdl:" + std::string { fsim::frontend::to_string(unit.vhdl_standard) }
                    + ":" + unit.library + ":" + unit.name);
            }
        }
        bool attribute_seen = false;
        bool attribute_specification_seen = false;
        bool group_template_seen = false;
        bool group_instance_seen = false;
        bool nested_attribute_seen = false;
        bool nested_group_seen = false;
        bool entity_psl_seen = false;
        bool architecture_psl_seen = false;
        bool verification_unit_psl_seen = false;
        for (const auto& unit : checked->vhdl_hir.units()) {
            const auto repeated = std::ranges::find(
                unit.psl_declarations, std::string_view { "repeated" },
                &fsim::semantic::vhdl::PslDeclaration::name);
            entity_psl_seen |= unit.name == "psl_target" && unit.psl_directives.size() == 1
                && unit.psl_directives.front().label == "entity_assert"
                && unit.psl_directives.front().comment_embedded;
            architecture_psl_seen |= unit.name == "psl_arch" && unit.psl_declarations.size() == 2
                && unit.psl_declarations.front().kind
                    == fsim::semantic::vhdl::PslDeclarationKind::default_clock
                && repeated != unit.psl_declarations.end()
                && repeated->formals.size() == 1
                && repeated->formals.front().expression_class
                    == fsim::semantic::vhdl::PslExpressionClass::static_integer
                && repeated->formals.front().static_default == 3U
                && repeated->analyzed_expression
                && repeated->analyzed_expression->temporal_operators.size() == 1
                && repeated->analyzed_expression->temporal_operators.front().range
                && repeated->analyzed_expression->temporal_operators.front()
                        .range->minimum
                    == 3U
                && repeated->analyzed_expression->temporal_operators.front()
                        .range->minimum_formal
                    == "count"
                && unit.psl_directives.size() == 1
                && unit.psl_directives.front().kind
                    == fsim::semantic::vhdl::PslDirectiveKind::assert_directive
                && unit.psl_directives.front().analyzed_property
                && unit.psl_directives.front().analyzed_property->clock
                && unit.psl_directives.front().analyzed_property->expression_class
                    == fsim::semantic::vhdl::PslExpressionClass::property
                && !unit.psl_directives.front().comment_embedded;
            verification_unit_psl_seen |= unit.name == "psl_monitor" && unit.psl_verification_unit
                && unit.psl_verification_unit->kind
                    == fsim::semantic::vhdl::PslVerificationUnitKind::unit
                && unit.psl_verification_unit->target_tokens.size() == 1
                && unit.psl_declarations.size() == 1
                && unit.psl_declarations.front().name == "ready"
                && unit.psl_declarations.front().analyzed_expression
                && unit.psl_declarations.front().analyzed_expression->expression_class
                    == fsim::semantic::vhdl::PslExpressionClass::boolean
                && unit.psl_directives.size() == 1
                && unit.psl_directives.front().analyzed_property
                && unit.psl_directives.front().analyzed_property->clock;
        }
        for (const auto& declaration : checked->vhdl_hir.declarations()) {
            if (declaration.name == "amount"
                && declaration.form
                    == fsim::semantic::vhdl::DeclarationForm::constant
                && declaration.deferred && declaration.completion
                && declaration.completion_source && !declaration.initializer) {
                result.deferred_constant_hir = true;
            }
            attribute_seen |= declaration.form
                    == fsim::semantic::vhdl::DeclarationForm::attribute_declaration
                && declaration.attribute && declaration.attribute->subtype;
            attribute_specification_seen |= declaration.form
                    == fsim::semantic::vhdl::DeclarationForm::attribute_specification
                && declaration.attribute
                && declaration.attribute->entity_class == "constant"
                && declaration.attribute->value;
            group_template_seen |= declaration.form
                    == fsim::semantic::vhdl::DeclarationForm::group_template
                && declaration.group && declaration.group->template_declaration;
            group_instance_seen |= declaration.form
                    == fsim::semantic::vhdl::DeclarationForm::group_instance
                && declaration.group && declaration.group->template_name
                && declaration.group->entries.size() == 1;
            nested_attribute_seen |= declaration.name == "local_mark" && declaration.attribute;
            nested_group_seen |= declaration.name == "local_group" && declaration.group;
        }
        result.attribute_group_hir = attribute_seen
            && attribute_specification_seen && group_template_seen
            && group_instance_seen;
        result.nested_attribute_group_hir = nested_attribute_seen && nested_group_seen;
        result.psl_hir = entity_psl_seen && architecture_psl_seen
            && verification_unit_psl_seen;
    }
    return result;
}

void expect_code(
    const std::filesystem::path& directory,
    const std::string_view case_name,
    const std::vector<Source>& sources,
    const std::string_view code)
{
    const auto first = check(directory, case_name, sources);
    const auto second = check(directory, case_name, sources);
    assert(!first.accepted);
    assert(first.codes == second.codes);
    if (std::ranges::find(first.codes, code) == first.codes.end()) {
        std::cerr << "missing " << code << " for " << case_name << ":";
        for (const auto& observed : first.codes) {
            std::cerr << ' ' << observed;
        }
        std::cerr << '\n';
    }
    assert(std::ranges::find(first.codes, code) != first.codes.end());
}

const Source package_declaration {
    // FSIM-CONFORMANCE CF-VHDL-LIB-001 source=SRC-IEEE-P1076 expectation=accept
    // FSIM-CONFORMANCE CF-VHDL-DECL-001 source=SRC-IEEE-P1076 expectation=accept
    // FSIM-CONFORMANCE CF-VHDL-TYPE-001 source=SRC-UVVM expectation=accept
    "package.vhd", "liba", R"(
package values is
  constant amount : integer;
  attribute marking : integer;
  attribute marking of amount : constant is 1;
  group related is (constant <>);
  group constants : related (amount);
end package;
)"
};

const Source package_body {
    "package_body.vhd", "liba", R"(
package body values is
  constant amount : integer := 7;
end package body;
)"
};

const Source context_declaration {
    // FSIM-CONFORMANCE CF-VHDL-CONTEXT-001 source=SRC-IEEE-P1076 expectation=accept
    "context.vhd", "libb", R"(
library liba;
use liba.values.all;
context analysis_context is
  library liba;
  use liba.values.all;
end context;
)"
};

const Source entity_declaration {
    // FSIM-CONFORMANCE CF-VHDL-GENERIC-001 source=SRC-UVVM expectation=accept
    "entity.vhd", "libb", R"(
context work.analysis_context;
entity leaf is
end entity;
)"
};

const Source architecture_declaration {
    "architecture.vhd", "libb", R"(
architecture rtl of leaf is
  function local_value return integer is
    constant nested_value : integer := 1;
    attribute local_mark : integer;
    attribute local_mark of nested_value : constant is 1;
    group local_template is (constant <>);
    group local_group : local_template (nested_value);
  begin
    return nested_value;
  end function;
begin
end architecture;
)"
};

const Source psl_declaration {
    "psl.vhd", "libb", R"(
entity psl_target is
  port (clk : in bit);
begin
  -- psl ENTITY_ASSERT: assert true;
end entity;

architecture psl_arch of psl_target is
  -- psl default clock is rising_edge(clk);
  -- psl sequence repeated(count : natural := 3) is {true[*count]};
begin
  NATIVE_ASSERT: assert property true;
end architecture;

-- psl vunit psl_monitor (psl_target) {
-- psl   boolean ready is true;
-- psl   CHECK_READY: cover ready;
-- psl }
)"
};

const Source configuration_declaration {
    // FSIM-CONFORMANCE CF-VHDL-CONFIG-001 source=SRC-IEEE-P1076 expectation=accept
    "configuration.vhd", "libb", R"(
configuration leaf_configuration of leaf is
  for rtl
  end for;
end configuration;
)"
};

void test_structural_conformance()
{
    // This compact fixture is independently authored from the bounded
    // structural expectations identified by SRC-IEEE-P1076 and SRC-UVVM.
    // FSIM-CONFORMANCE CF-VHDL-COMPONENT-001 source=SRC-UVVM expectation=accept
    // FSIM-CONFORMANCE CF-VHDL-GENERATE-001 source=SRC-IEEE-P1076 expectation=accept
    const auto parsed = fsim::frontend::parse_text(
        "structural-conformance.vhd",
        R"(package structural_types is
  subtype element_t is integer range 0 to 15;
  constant lane_count : integer := 2;
end package structural_types;

context structural_context is
  library work;
  use work.structural_types.all;
end context structural_context;

context work.structural_context;
entity structural_leaf is
  generic (offset : element_t := 1);
  port (
    value : in integer;
    observed : out integer);
end entity structural_leaf;

context work.structural_context;
architecture rtl of structural_leaf is
begin
  observed <= value + offset;
end architecture rtl;

context work.structural_context;
entity structural_top is
  generic (lanes : integer := lane_count);
end entity structural_top;

context work.structural_context;
architecture structure of structural_top is
  component structural_leaf is
    generic (offset : element_t := 1);
    port (
      value : in integer;
      observed : out integer);
  end component structural_leaf;
  for all : structural_leaf
    use entity work.structural_leaf(rtl);
begin
  generated: for lane in 0 to lanes - 1 generate
    signal observed : integer;
  begin
    child: structural_leaf
      generic map (offset => lane + 1)
      port map (value => lane, observed => observed);
  end generate generated;
end architecture structure;

configuration structural_selected of structural_top is
  for structure
    for generated(0)
      for all : structural_leaf
        use entity work.structural_leaf(rtl);
      end for;
    end for;
  end for;
end configuration structural_selected;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    assert(parsed.design.units.size() == 7);
    assert(parsed.design.find(
        fsim::frontend::UnitKind::VhdlPackage,
        "structural_types"));
    assert(parsed.design.find(
        fsim::frontend::UnitKind::VhdlContext,
        "structural_context"));
    assert(parsed.design.find(
        fsim::frontend::UnitKind::VhdlConfiguration,
        "structural_selected"));

    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "vhdl:work.structural_top(structure)");
    assert(elaborated.ok());
    assert(elaborated.design->find_signal(
        "structural_top.generated[0].observed"));
    assert(elaborated.design->find_signal(
        "structural_top.generated[1].observed"));
}

} // namespace

int main()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl-analysis-order-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);

    const std::vector<Source> valid {
        package_declaration,
        package_body,
        context_declaration,
        entity_declaration,
        architecture_declaration,
        configuration_declaration,
    };
    // FSIM-CONFORMANCE CF-VHDL-ORDER-P01 source=SRC-IEEE-P1076 expectation=accept
    const auto accepted = check(directory.path, "valid", valid);
    if (!accepted.accepted) {
        std::cerr << "unexpected diagnostics for valid VHDL analysis order:";
        for (const auto& code : accepted.codes) {
            std::cerr << ' ' << code;
        }
        std::cerr << '\n';
    }
    assert(accepted.accepted);
    assert(accepted.codes.empty());
    assert(accepted.deferred_constant_hir);
    assert(accepted.attribute_group_hir);
    assert(accepted.nested_attribute_group_hir);
    assert((accepted.units == std::vector<std::string> { "liba:values", "liba:values", "libb:analysis_context", "libb:leaf", "libb:rtl", "libb:leaf_configuration" }));

    const Source revision_package {
        "revision_package.vhd", "profiles", R"(
package revision_values is
  constant amount : integer := 7;
end package;
)",
        fsim::project::Language::vhdl, "1993"
    };
    const Source revision_consumer {
        "revision_consumer.vhd", "profiles", R"(
use work.revision_values.all;
entity revision_consumer is
end entity;
)",
        fsim::project::Language::vhdl, "1993"
    };
    const Source mixed_systemverilog {
        "mixed_profile.sv", "profiles", R"(
module mixed_profile;
endmodule
)",
        fsim::project::Language::system_verilog, "2017"
    };
    const auto revision_profiles = check(
        directory.path, "revision-profiles",
        { revision_package, revision_consumer, mixed_systemverilog });
    assert(revision_profiles.accepted);
    assert(revision_profiles.codes.empty());
    assert((revision_profiles.source_profiles
        == std::vector<std::string> {
            "vhdl:1993", "vhdl:1993", "systemverilog:2017" }));
    assert(std::ranges::find(
               revision_profiles.unit_profiles,
               "vhdl:1993:profiles:revision_values")
        != revision_profiles.unit_profiles.end());
    assert(std::ranges::find(
               revision_profiles.unit_profiles,
               "vhdl:1993:profiles:revision_consumer")
        != revision_profiles.unit_profiles.end());

    auto incompatible_consumer = revision_consumer;
    incompatible_consumer.standard = "2002";
    expect_code(
        directory.path, "incompatible-package-dependency",
        { revision_package, incompatible_consumer },
        "FSIM-FE-VHORDER-011");

    auto incompatible_reanalysis = revision_package;
    incompatible_reanalysis.standard = "1987";
    expect_code(
        directory.path, "incompatible-reanalysis",
        { revision_package, incompatible_reanalysis },
        "FSIM-FE-VHORDER-011");

    const auto psl_accepted = check(
        directory.path, "psl-ownership", { psl_declaration });
    assert(psl_accepted.accepted);
    assert(psl_accepted.codes.empty());
    assert(psl_accepted.psl_hir);
    assert((psl_accepted.units == std::vector<std::string> { "libb:psl_target", "libb:psl_arch", "libb:psl_monitor" }));
    expect_code(
        directory.path,
        "duplicate-psl-verification-unit",
        { psl_declaration, psl_declaration },
        "FSIM-FE-VHORDER-009");

    // FSIM-CONFORMANCE CF-VHDL-ORDER-N01 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-001
    expect_code(
        directory.path,
        "architecture-before-entity",
        { architecture_declaration, entity_declaration },
        "FSIM-FE-VHORDER-001");
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N02 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-002
    expect_code(
        directory.path,
        "body-before-package",
        { package_body, package_declaration },
        "FSIM-FE-VHORDER-002");
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N03 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-003
    expect_code(
        directory.path,
        "reference-before-context",
        { entity_declaration, context_declaration, package_declaration },
        "FSIM-FE-VHORDER-003");
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N04 source=SRC-UVVM expectation=FSIM-FE-VHORDER-004
    expect_code(
        directory.path,
        "use-before-package",
        { context_declaration, package_declaration },
        "FSIM-FE-VHORDER-004");
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N05 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-005
    expect_code(
        directory.path,
        "configuration-before-entity",
        { configuration_declaration, entity_declaration,
            architecture_declaration },
        "FSIM-FE-VHORDER-005");
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N06 source=SRC-IEEE-P1076 expectation=FSIM-FE-VHORDER-006
    expect_code(
        directory.path,
        "configuration-before-architecture",
        { entity_declaration, configuration_declaration,
            architecture_declaration, context_declaration,
            package_declaration },
        "FSIM-FE-VHORDER-006");

    const Source entity_binding {
        "entity_binding.vhd", "libb", R"(
entity host is
end entity;
architecture rtl of host is
  component leaf is
  end component;
  for all : leaf use entity work.leaf(rtl);
begin
end architecture;
)"
    };
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N07 source=SRC-UVVM expectation=FSIM-FE-VHORDER-007
    expect_code(
        directory.path,
        "binding-before-entity",
        { entity_binding, entity_declaration, architecture_declaration,
            context_declaration, package_declaration },
        "FSIM-FE-VHORDER-007");

    const Source configuration_binding {
        "configuration_binding.vhd", "libb", R"(
entity host is
end entity;
architecture rtl of host is
  component leaf is
  end component;
  for all : leaf use configuration work.leaf_configuration;
begin
end architecture;
)"
    };
    // FSIM-CONFORMANCE CF-VHDL-ORDER-N08 source=SRC-UVVM expectation=FSIM-FE-VHORDER-008
    expect_code(
        directory.path,
        "binding-before-configuration",
        { package_declaration, context_declaration, entity_declaration,
            architecture_declaration, configuration_binding,
            configuration_declaration },
        "FSIM-FE-VHORDER-008");

    const Source direct_configuration_instance {
        "direct_configuration_instance.vhd", "libb", R"(
entity configuration_host is
end entity;
architecture rtl of configuration_host is
begin
  child: configuration work.leaf_configuration;
end architecture;
)"
    };
    expect_code(
        directory.path,
        "instance-before-configuration",
        { direct_configuration_instance, configuration_declaration,
            entity_declaration, architecture_declaration,
            context_declaration, package_declaration },
        "FSIM-FE-VHORDER-008");

    expect_code(
        directory.path,
        "duplicate-primary-unit",
        { entity_declaration, entity_declaration },
        "FSIM-FE-VHORDER-009");
    const Source colliding_package {
        "colliding_package.vhd", "libb", R"(
package leaf is
end package;
)"
    };
    expect_code(
        directory.path,
        "cross-kind-primary-identity",
        { package_declaration, context_declaration, entity_declaration,
            colliding_package },
        "FSIM-FE-VHORDER-009");
    expect_code(
        directory.path,
        "duplicate-architecture",
        { entity_declaration, architecture_declaration,
            architecture_declaration },
        "FSIM-FE-VHORDER-010");

    const Source deferred_missing {
        "deferred_missing.vhd", "liba", R"(
package deferred_missing is
  constant value : integer;
end package;
)"
    };
    expect_code(
        directory.path,
        "deferred-constant-missing",
        { deferred_missing },
        "FSIM-FE-VHDECL-001");

    const Source deferred_mismatch_declaration {
        "deferred_mismatch.vhd", "liba", R"(
package deferred_mismatch is
  constant value : integer;
end package;
)"
    };
    const Source deferred_mismatch_body {
        "deferred_mismatch_body.vhd", "liba", R"(
package body deferred_mismatch is
  constant value : boolean := true;
end package body;
)"
    };
    expect_code(
        directory.path,
        "deferred-constant-mismatch",
        { deferred_mismatch_declaration, deferred_mismatch_body },
        "FSIM-FE-VHDECL-002");

    const Source nondeferred_declaration {
        "nondeferred.vhd", "liba", R"(
package nondeferred is
  constant value : integer := 1;
end package;
)"
    };
    const Source nondeferred_body {
        "nondeferred_body.vhd", "liba", R"(
package body nondeferred is
  constant value : integer := 2;
end package body;
)"
    };
    expect_code(
        directory.path,
        "nondeferred-constant-redeclaration",
        { nondeferred_declaration, nondeferred_body },
        "FSIM-FE-VHDECL-003");
    expect_code(
        directory.path,
        "duplicate-package-body",
        { package_declaration, package_body, package_body },
        "FSIM-FE-VHORDER-010");

    const auto extended_identity = check(
        directory.path,
        "case-sensitive-extended-identities",
        {
            { "upper_extended.vhd", "libb", R"(
entity \Leaf\ is end entity;
)" },
            { "lower_extended.vhd", "libb", R"(
entity \leaf\ is end entity;
)" },
        });
    assert(extended_identity.accepted);
    assert(
        std::ranges::find(extended_identity.units, "libb:Leaf")
            != extended_identity.units.end()
        && std::ranges::find(extended_identity.units, "libb:leaf")
            != extended_identity.units.end());

    test_structural_conformance();

    return 0;
}
