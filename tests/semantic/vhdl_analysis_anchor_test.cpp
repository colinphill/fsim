// SPDX-License-Identifier: Apache-2.0
// Compile-time VHDL analysis kept through link (gate BD D1): a stamped
// unit's resolution results survive the projection into its own library
// object, its references into another library are rebound by position from
// their anchors, a link against a target unit analyzed again differently
// is refused with a diagnostic (IEEE 1076-2019 13.5), and
// the test-only verifier finds a stored result that differs from a fresh
// resolution.
#include "fsim/app/built_project.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_vhdl_analysis_stamp.hpp"
#include "fsim/semantic/compiled_design_vhdl_resolution.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include "../support/test_helpers.hpp"
#include "../../src/app/application_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsim::test::require;
namespace semantic = fsim::semantic;
namespace vhdl = fsim::semantic::vhdl;

constexpr std::string_view package_a = R"(
package pa is
  type t is (red, green, blue);
  function f(x : integer) return integer;
  function f(x : real) return integer;
  procedure p(x : integer);
end package;

package body pa is
  function f(x : integer) return integer is
  begin
    return x + 1;
  end function;
  function f(x : real) return integer is
  begin
    return 0;
  end function;
  procedure p(x : integer) is
  begin
  end procedure;
end package body;
)";

// The same package with its overloads declared in the other order (IEEE
// 1076-2019 13.5: units that depend on it are obsolete).
constexpr std::string_view package_a_reordered = R"(
package pa is
  type t is (red, green, blue);
  function f(x : real) return integer;
  function f(x : integer) return integer;
  procedure p(x : integer);
end package;

package body pa is
  function f(x : real) return integer is
  begin
    return 0;
  end function;
  function f(x : integer) return integer is
  begin
    return x + 1;
  end function;
  procedure p(x : integer) is
  begin
  end procedure;
end package body;
)";

constexpr std::string_view entity_b = R"(
library liba;
use liba.pa.all;

entity eb is
end entity;

architecture rtl of eb is
  subtype my_time is time range 0 ns to 10 ns;
  signal s : integer := f(3);
  signal c : t := green;
  constant d : time := time'value("5 ns");
  constant q : time := time'(2 ns);
begin
  process
  begin
    p(1);
    wait;
  end process;
end architecture;
)";

[[nodiscard]] fsim::project::Config two_library_config(
    const std::filesystem::path& directory)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-analysis-anchors";
    config.project.tops = { { "vhdl:libb.eb(rtl)", "dut" } };
    config.build.cache_path = directory / "cache";
    fsim::project::SourceSet a;
    a.language = fsim::project::Language::vhdl;
    a.standard = "2008";
    a.library = "liba";
    a.files = { directory / "a.vhd" };
    config.source_sets.push_back(std::move(a));
    fsim::project::SourceSet b;
    b.language = fsim::project::Language::vhdl;
    b.standard = "2008";
    b.library = "libb";
    b.files = { directory / "b.vhd" };
    config.source_sets.push_back(std::move(b));
    return config;
}

[[nodiscard]] semantic::CompiledDesign check(
    const std::filesystem::path& directory, const std::string_view a)
{
    fsim::test::write_text(directory / "a.vhd", a);
    fsim::test::write_text(directory / "b.vhd", entity_b);
    fsim::diagnostic::Engine diagnostics;
    auto checked
        = fsim::app::check_project(two_library_config(directory), diagnostics);
    if (!checked || diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    require(checked.has_value() && !diagnostics.has_error(),
        "the two-library design is checked");
    // As a workspace stores objects: source names relative to the base
    // directory (an object never holds a producer-absolute path).
    const auto mappings = fsim::app::application_detail::compiled_cache_source_mappings(
        *checked, directory, diagnostics);
    require(mappings.has_value()
            && fsim::app::application_detail::relocate_compiled_design_sources(
                *checked, *mappings, diagnostics),
        "the design's sources are relocated");
    return std::move(static_cast<semantic::CompiledDesign&>(*checked));
}

// A library object as a workspace stores it: the projection of one library
// (with the compiler libraries for the provider), written and read back.
[[nodiscard]] semantic::CompiledDesign library_object(
    const semantic::CompiledDesign& design,
    const std::vector<std::string>& libraries)
{
    auto projected = semantic::extract_compiled_libraries(design, libraries);
    require(projected.ok(), "library projection: " + projected.error);
    fsim::diagnostic::Engine diagnostics;
    const auto bytes = fsim::app::serialize_compiled_hir_bundle(
        *projected.design, diagnostics);
    if (!bytes || diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    require(bytes.has_value() && !diagnostics.has_error(),
        "the library object is written");
    auto read = fsim::app::deserialize_compiled_hir_bundle(
        *bytes, "vhdl-analysis-anchor-object", diagnostics);
    require(read.has_value() && !diagnostics.has_error(),
        "the library object is read back");
    return std::move(*read);
}

[[nodiscard]] semantic::CompiledDesign link(
    semantic::CompiledDesign first, semantic::CompiledDesign second)
{
    std::vector<semantic::CompiledDesign> inputs;
    inputs.push_back(std::move(first));
    inputs.push_back(std::move(second));
    auto linked = semantic::link_compiled_designs(std::move(inputs));
    require(linked.ok(), "link: " + linked.error);
    return std::move(*linked.design);
}

[[nodiscard]] bool in_library(
    const vhdl::Unit& unit, const std::string_view library)
{
    return fsim::support::equal_ignoring_ascii_case(unit.library, library);
}

[[nodiscard]] const vhdl::Unit& unit_of(const semantic::CompiledDesign& design,
    const std::string_view library, const vhdl::UnitKind kind,
    const bool secondary)
{
    for (const auto& unit : design.vhdl_hir.units()) {
        if (unit.kind == kind && in_library(unit, library)
            && unit.primary_name.empty() != secondary) {
            return unit;
        }
    }
    throw std::runtime_error { "missing unit in library "
        + std::string { library } };
}

[[nodiscard]] const vhdl::Unit& architecture_b(
    const semantic::CompiledDesign& design)
{
    return unit_of(design, "libb", vhdl::UnitKind::architecture, true);
}

[[nodiscard]] const semantic::CompiledVhdlUnitItems& items_of(
    semantic::CompiledDesign& design, const vhdl::Unit& unit)
{
    if (design.vhdl_unit_items(unit.id) == nullptr) {
        design.refresh_lookup_indexes();
    }
    const auto* items = design.vhdl_unit_items(unit.id);
    require(items != nullptr, "unit items");
    return *items;
}

// A declaration's place: the unit that owns it and its ordinal there.
struct Place {
    const vhdl::Unit* unit { };
    std::uint32_t ordinal { };
    const vhdl::Declaration* declaration { };
};

[[nodiscard]] Place place_of(
    semantic::CompiledDesign& design, const semantic::DeclarationId id)
{
    const auto view = design.find_declaration(id);
    require(view && view->vhdl != nullptr, "the target declaration exists");
    const auto* scope = design.find_scope(view->vhdl->scope);
    require(scope != nullptr, "the target scope exists");
    const auto unit = design.find_unit(scope->unit);
    require(unit && unit->vhdl != nullptr, "the target unit exists");
    const auto& items = items_of(design, *unit->vhdl);
    const auto position = static_cast<std::uint32_t>(
        view->vhdl - design.vhdl_hir.declarations().data());
    const auto found = std::ranges::find(items.declarations, position);
    require(found != items.declarations.end(), "the target is a unit item");
    return { unit->vhdl,
        static_cast<std::uint32_t>(found - items.declarations.begin()),
        view->vhdl };
}

// Whether a declaration is the first function `f` its unit declares, which
// the source declares with an INTEGER formal (before the REAL one).
[[nodiscard]] bool first_f(semantic::CompiledDesign& design, const Place& place)
{
    const auto& items = items_of(design, *place.unit);
    for (std::uint32_t ordinal { }; ordinal < items.declarations.size();
         ++ordinal) {
        const auto& declaration
            = design.vhdl_hir.declarations()[items.declarations[ordinal]];
        if (declaration.form == vhdl::DeclarationForm::function
            && declaration.name == "f") {
            return ordinal == place.ordinal;
        }
    }
    return false;
}

// The ordinal in B's items of the expression whose stored call names `f`.
[[nodiscard]] std::uint32_t call_of_f(semantic::CompiledDesign& design)
{
    const auto& unit = architecture_b(design);
    const auto& items = items_of(design, unit);
    for (std::uint32_t ordinal { }; ordinal < items.expressions.size();
         ++ordinal) {
        const auto& expression
            = design.vhdl_hir.expressions()[items.expressions[ordinal]];
        if (!expression.resolved_declaration) {
            continue;
        }
        const auto view = design.find_declaration(*expression.resolved_declaration);
        if (view && view->vhdl != nullptr && view->vhdl->name == "f") {
            return ordinal;
        }
    }
    throw std::runtime_error { "no call of f is resolved in libb" };
}

[[nodiscard]] std::uint32_t call_of_p(semantic::CompiledDesign& design)
{
    const auto& unit = architecture_b(design);
    const auto& items = items_of(design, unit);
    for (std::uint32_t ordinal { }; ordinal < items.statements.size();
         ++ordinal) {
        const auto& statement
            = design.vhdl_hir.statements()[items.statements[ordinal]];
        if (!statement.resolved_procedure) {
            continue;
        }
        const auto view = design.find_declaration(*statement.resolved_procedure);
        if (view && view->vhdl != nullptr && view->vhdl->name == "p") {
            return ordinal;
        }
    }
    throw std::runtime_error { "no call of p is resolved in libb" };
}

[[nodiscard]] const vhdl::Expression& expression_at(
    semantic::CompiledDesign& design, const std::uint32_t ordinal)
{
    const auto& items = items_of(design, architecture_b(design));
    return design.vhdl_hir.expressions().at(items.expressions.at(ordinal));
}

[[nodiscard]] const vhdl::Statement& statement_at(
    semantic::CompiledDesign& design, const std::uint32_t ordinal)
{
    const auto& items = items_of(design, architecture_b(design));
    return design.vhdl_hir.statements().at(items.statements.at(ordinal));
}

// The three places gate BD's TIME work reads Expression::denoted_provided_type
// and SubtypeIndication::provided_type from.
void check_time_marks(semantic::CompiledDesign& design)
{
    const auto& unit = architecture_b(design);
    const auto& items = items_of(design, unit);
    bool prefix { };
    bool qualified { };
    for (const auto position : items.expressions) {
        const auto& expression = design.vhdl_hir.expressions()[position];
        if (expression.denoted_provided_type != vhdl::ProvidedType::time) {
            continue;
        }
        if (expression.kind == vhdl::ExpressionKind::name
            && expression.marker == fsim::support::ExpressionMarker::none) {
            prefix = true;
        }
        if (expression.kind == vhdl::ExpressionKind::call
            && expression.marker
                == fsim::support::ExpressionMarker::vhdl_qualified) {
            qualified = true;
        }
    }
    require(prefix, "the prefix TIME of TIME'VALUE denotes TIME");
    require(qualified, "TIME'(2 ns) denotes TIME on the qualified expression");
    bool subtype { };
    for (const auto position : items.declarations) {
        const auto& declaration = design.vhdl_hir.declarations()[position];
        if (declaration.name != "my_time") {
            continue;
        }
        if (declaration.subtype
            && declaration.subtype->provided_type == vhdl::ProvidedType::time) {
            subtype = true;
        }
        if (declaration.declared_type) {
            const auto type = design.find_type(*declaration.declared_type);
            if (type && type->vhdl != nullptr
                && type->vhdl->base.provided_type == vhdl::ProvidedType::time) {
                subtype = true;
            }
        }
    }
    require(subtype, "the subtype MY_TIME's indication is of TIME");
}

void cross_library(const std::filesystem::path& directory)
{
    auto checked = check(directory, package_a);
    const auto& b = architecture_b(checked);
    require(b.analyzed, "libb's architecture is stamped");
    require(!b.anchors.empty(), "libb's architecture anchors into liba");
    for (const auto& anchor : b.anchors) {
        require(fsim::support::equal_ignoring_ascii_case(anchor.library, "liba")
                && anchor.primary == "pa" && !anchor.target_digest.empty(),
            "every anchor names a unit of package liba.pa");
    }
    require(unit_of(checked, "liba", vhdl::UnitKind::package, false).analyzed,
        "liba's package is stamped");
    check_time_marks(checked);

    const auto f_call = call_of_f(checked);
    const auto p_call = call_of_p(checked);
    const auto f_target = place_of(
        checked, *expression_at(checked, f_call).resolved_declaration);
    require(in_library(*f_target.unit, "liba") && first_f(checked, f_target),
        "analysis selects liba's f(integer)");
    const auto p_target = place_of(
        checked, *statement_at(checked, p_call).resolved_procedure);

    // Projected into its own library, B keeps its stamp and anchors, but not
    // the IDs of liba's declarations: nothing resolves the slot again.
    auto object_b = library_object(checked, { "libb" });
    require(architecture_b(object_b).analyzed
            && architecture_b(object_b).anchors == b.anchors,
        "the libb object keeps the stamp and the anchors");
    require(!expression_at(object_b, f_call).resolved_declaration,
        "the libb object holds no ID of liba's f");
    auto object_a = library_object(checked, { "liba", "std", "ieee" });

    auto linked = link(std::move(object_b), std::move(object_a));
    const auto rebound = place_of(
        linked, *expression_at(linked, f_call).resolved_declaration);
    require(in_library(*rebound.unit, "liba")
            && rebound.unit->kind == f_target.unit->kind
            && rebound.unit->primary_name == f_target.unit->primary_name
            && rebound.ordinal == f_target.ordinal && first_f(linked, rebound),
        "the link rebinds the call to liba's f(integer)");
    const auto procedure = place_of(
        linked, *statement_at(linked, p_call).resolved_procedure);
    require(procedure.ordinal == p_target.ordinal
            && procedure.declaration->name == "p",
        "the link rebinds the procedure call to liba's p");
    check_time_marks(linked);
    const auto difference = semantic::verify_stamped_vhdl_analysis(linked);
    if (!difference.empty()) {
        std::cerr << difference << '\n';
    }
    require(difference.empty(), "the verifier agrees after the link");
}

void stale(const std::filesystem::path& directory)
{
    const auto first = directory / "first";
    const auto second = directory / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    auto checked = check(first, package_a);
    auto object_b = library_object(checked, { "libb" });
    // liba analyzed again with its overloads reordered: the libb object was
    // analyzed against the old unit. A workspace leaves such an object out
    // of a compile environment and refuses to elaborate it (FSIM-WS-002,
    // IEEE 1076-2019 13.5); a link that is handed both anyway reports the
    // libb unit obsolete (FSIM-SEM-0003) instead of rebinding its anchors
    // to other declarations.
    auto reordered = check(second, package_a_reordered);
    auto object_a = library_object(reordered, { "liba", "std", "ieee" });
    std::vector<semantic::CompiledDesign> inputs;
    inputs.push_back(std::move(object_b));
    inputs.push_back(std::move(object_a));
    const auto linked = semantic::link_compiled_designs(std::move(inputs));
    require(!linked.ok(), "a link against a changed unit is refused");
    require(linked.diagnostic_code == "FSIM-SEM-0003",
        "the refusal is FSIM-SEM-0003, not '" + linked.diagnostic_code + "'");
    require(linked.error.find("13.5") != std::string::npos
            && linked.error.find("obsolete") != std::string::npos
            && linked.error.find("libb") != std::string::npos
            && linked.error.find("liba.pa") != std::string::npos,
        "the refusal names the obsolete unit and its target: "
            + linked.error);
}

void verifier_detects(const std::filesystem::path& directory)
{
    auto checked = check(directory, package_a);
    const auto f_call = call_of_f(checked);
    auto linked = link(library_object(checked, { "libb" }),
        library_object(checked, { "liba", "std", "ieee" }));
    require(semantic::verify_stamped_vhdl_analysis(linked).empty(),
        "the verifier agrees before the corruption");
    // Store the other overload of f for the call.
    const auto stored = *expression_at(linked, f_call).resolved_declaration;
    const auto target = place_of(linked, stored);
    const auto& items = items_of(linked, *target.unit);
    std::optional<semantic::DeclarationId> other;
    for (const auto position : items.declarations) {
        const auto& declaration = linked.vhdl_hir.declarations()[position];
        if (declaration.form == vhdl::DeclarationForm::function
            && declaration.name == "f" && declaration.id != stored) {
            other = declaration.id;
            break;
        }
    }
    require(other.has_value(), "liba declares a second f");
    const auto position = items_of(linked, architecture_b(linked))
                              .expressions.at(f_call);
    linked.vhdl_hir.mutable_expressions().at(position).resolved_declaration
        = *other;
    linked.refresh_lookup_indexes();
    const auto difference = semantic::verify_stamped_vhdl_analysis(linked);
    require(difference.find("resolved_declaration") != std::string::npos,
        "the verifier reports the corrupted stored call: " + difference);
}

// One object linked alone keeps its anchors unbound; the design is not
// elaborated while a target is missing (IEEE 1076-2019 13.5).
void unbound_at_elaboration(const std::filesystem::path& directory)
{
    auto checked = check(directory, package_a);
    std::vector<semantic::CompiledDesign> inputs;
    inputs.push_back(library_object(checked, { "libb" }));
    auto linked = semantic::link_compiled_designs(std::move(inputs));
    require(linked.ok(), "one object links alone: " + linked.error);
    const auto unbound = semantic::unbound_vhdl_anchor_error(*linked.design);
    require(unbound.find("liba.pa") != std::string::npos
            && unbound.find("13.5") != std::string::npos,
        "the missing target is reported: " + unbound);
    const auto elaborated = fsim::elaboration::elaborate(*linked.design, "eb");
    require(!elaborated.design
            && std::ranges::any_of(elaborated.diagnostics,
                [](const auto& diagnostic) {
                    return diagnostic.code == "FSIM-SEM-0003";
                }),
        "elaboration refuses the design with FSIM-SEM-0003");
    auto whole = link(library_object(checked, { "libb" }),
        library_object(checked, { "liba", "std", "ieee" }));
    require(semantic::unbound_vhdl_anchor_error(whole).empty(),
        "every anchor is bound once liba is linked");
}

} // namespace

int main()
{
    try {
        const fsim::test::TemporaryDirectory directory {
            "fsim-vhdl-analysis-anchors"
        };
        const auto cross = directory.path / "cross";
        const auto changed = directory.path / "stale";
        const auto corrupt = directory.path / "verifier";
        const auto missing = directory.path / "unbound";
        for (const auto& path : { cross, changed, corrupt, missing }) {
            std::filesystem::create_directories(path);
        }
        cross_library(cross);
        stale(changed);
        verifier_detects(corrupt);
        unbound_at_elaboration(missing);
    } catch (const std::exception& error) {
        std::cerr << "vhdl_analysis_anchor_test: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
