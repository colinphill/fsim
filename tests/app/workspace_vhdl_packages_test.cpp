// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_compiled_environment_vhdl.hpp"
#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_workspace_internal.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace detail = fsim::app::application_detail;
namespace semantic = fsim::semantic;
namespace vh = semantic::vhdl;
namespace workspace = fsim::app::workspace;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator { path, error };
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            const auto status = iterator->symlink_status(error);
            if (!error && !std::filesystem::is_symlink(status)) {
                std::filesystem::permissions(iterator->path(),
                    std::filesystem::perms::owner_all,
                    std::filesystem::perm_options::add, error);
            }
            if (!error) {
                iterator.increment(error);
            }
        }
        error.clear();
        std::filesystem::remove_all(path, error);
    }
};

bool has_code(const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(diagnostics.diagnostics(),
        [&](const auto& diagnostic) { return diagnostic.code == code; });
}

std::optional<semantic::CompiledDesign> compile(const std::filesystem::path& directory,
    const std::string_view name, const std::string_view source,
    fsim::diagnostic::Engine& diagnostics,
    const semantic::CompiledDesign* imported = nullptr,
    const std::string_view library = "support",
    const std::string_view standard = "2008")
{
    const auto path = directory / (std::string { name } + ".vhd");
    {
        std::ofstream output(path, std::ios::binary);
        output << source;
        assert(output.good());
    }
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "workspace-vhdl-packages";
    config.build.jobs = 8;
    fsim::project::SourceSet input;
    input.language = fsim::project::Language::vhdl;
    input.library = library;
    input.standard = standard;
    input.compilation_unit = "file";
    input.files.push_back(path);
    config.source_sets.push_back(std::move(input));
    auto checked = detail::check_project_for_object(config, diagnostics, imported);
    std::filesystem::remove(path);
    if (!checked) {
        return std::nullopt;
    }
    const auto mappings = detail::compiled_cache_source_mappings(
        *checked, config.base_directory, diagnostics);
    if (!mappings || !detail::relocate_compiled_design_sources(*checked, *mappings, diagnostics)) {
        return std::nullopt;
    }
    const auto bytes = fsim::app::serialize_compiled_hir_bundle(*checked, diagnostics);
    checked.reset();
    if (!bytes) {
        return std::nullopt;
    }
    auto decoded = fsim::app::deserialize_compiled_hir_bundle(*bytes,
        path.generic_string(), diagnostics);
    std::vector<fsim::library::SourceNameMapping> restored_sources;
    for (const auto& mapping : *mappings) {
        restored_sources.push_back({ mapping.logical_name, mapping.producer_name });
    }
    if (decoded && !detail::relocate_compiled_design_sources(*decoded, restored_sources, diagnostics)) {
        return std::nullopt;
    }
    return decoded;
}

semantic::CompiledDesign accepted(const std::filesystem::path& directory,
    const std::string_view name, const std::string_view source,
    const semantic::CompiledDesign* imported = nullptr,
    const std::string_view library = "support")
{
    fsim::diagnostic::Engine diagnostics;
    auto result = compile(directory, name, source, diagnostics, imported, library);
    if (!result) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(result);
    assert(!diagnostics.has_error());
    return std::move(*result);
}

const vh::Declaration& named_declaration(const semantic::CompiledDesign& design,
    const vh::Unit& unit, const std::string_view name)
{
    for (const auto id : unit.declarations) {
        const auto declaration = design.find_declaration(id);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->name == name) {
            return *declaration->vhdl;
        }
    }
    assert(false);
    std::abort();
}

bool has_reference(const semantic::CompiledDesign& design,
    const semantic::UnitId owner, const semantic::CompiledReferenceKind kind,
    const semantic::UnitId target)
{
    return std::ranges::any_of(design.references(), [&](const auto& reference) {
        return reference.owner == owner && reference.kind == kind
            && reference.target == target;
    });
}

constexpr std::string_view package_specification = R"(
package shared_values is
  subtype word is integer range 0 to 255;
  constant base : integer := 17;
  constant late : integer;
  constant pattern : bit_vector(3 downto 0);
  constant label_text : string(1 to 2);
  function plus_base(value : integer) return integer;
end package;
)";

constexpr std::string_view package_body = R"(
package body shared_values is
  constant late : integer := 25;
  constant pattern : bit_vector(3 downto 0) := "1010";
  constant label_text : string(1 to 2) := "OK";
  function plus_base(value : integer) return integer is
  begin
    return value + base;
  end function;
end package body;
)";

void test_context_dependency_fingerprints(const semantic::CompiledDesign& design,
    const semantic::UnitId context_id, const semantic::UnitId architecture_id)
{
    detail::WorkspaceCatalogs catalogs;
    for (const std::string_view library : { "support", "work" }) {
        workspace::LibraryCatalog catalog;
        catalog.location.name = library;
        for (auto& entry : detail::compiled_object_unit_entries(design, library)) {
            auto& unit = entry.second;
            workspace::ArtifactRecord artifact;
            const auto kind = unit.kind == "package" && !unit.primary_name.empty()
                ? "package_body"
                : unit.kind;
            artifact.id = kind + "-" + unit.name;
            artifact.fingerprint = artifact.id + "-v1";
            artifact.units.push_back({ std::move(unit), { } });
            catalog.artifacts.push_back(std::move(artifact));
        }
        catalogs.push_back(std::move(catalog));
    }
    const auto artifact_for = [&](const std::string_view library,
                                  const std::string_view kind) -> workspace::ArtifactRecord& {
        for (auto& catalog : catalogs) {
            if (catalog.location.name != library) {
                continue;
            }
            for (auto& artifact : catalog.artifacts) {
                const auto& unit = artifact.units.front().unit;
                const auto unit_kind = unit.kind == "package" && !unit.primary_name.empty()
                    ? "package_body"
                    : unit.kind;
                if (unit_kind == kind) {
                    return artifact;
                }
            }
        }
        assert(false);
        std::abort();
    };
    const auto has_dependency = [](const auto& dependencies, const auto& artifact,
                                    const std::string_view fingerprint) {
        return std::ranges::any_of(dependencies, [&](const auto& dependency) {
            return dependency.library == "support"
                && workspace::unit_identity(dependency.unit)
                == workspace::unit_identity(artifact.units.front().unit)
                && dependency.fingerprint == fingerprint;
        });
    };
    auto& specification = artifact_for("support", "package");
    auto& body = artifact_for("support", "package_body");
    auto& context = artifact_for("support", "context");
    auto& architecture = artifact_for("work", "architecture");
    const std::array context_owner { context_id };
    const std::array architecture_owner { architecture_id };
    context.dependencies = detail::workspace_dependencies(design, context_owner, catalogs);
    architecture.dependencies = detail::workspace_dependencies(design, architecture_owner, catalogs);
    assert(has_dependency(architecture.dependencies, context, context.fingerprint));
    assert(has_dependency(architecture.dependencies, specification, specification.fingerprint));
    assert(has_dependency(architecture.dependencies, body, body.fingerprint));
    fsim::diagnostic::Engine consistent;
    assert(detail::validate_workspace_dependencies(catalogs, consistent));
    assert(!consistent.has_error());

    const auto old_body_fingerprint = body.fingerprint;
    const auto context_fingerprint = context.fingerprint;
    body.fingerprint = body.id + "-v2";
    context.dependencies = detail::workspace_dependencies(design, context_owner, catalogs);
    assert(context.fingerprint == context_fingerprint);
    assert(has_dependency(context.dependencies, body, body.fingerprint));
    assert(has_dependency(architecture.dependencies, body, old_body_fingerprint));
    fsim::diagnostic::Engine stale;
    assert(!detail::validate_workspace_dependencies(catalogs, stale));
    assert(has_code(stale, "FSIM-WS-002"));
    assert(std::ranges::any_of(stale.diagnostics(), [&](const auto& diagnostic) {
        return diagnostic.message.find("object '" + architecture.id + "'")
            != std::string::npos;
    }));
}

void test_separate_package_compilation(const std::filesystem::path& directory)
{
    auto specification = accepted(directory, "specification", package_specification);
    assert(!std::filesystem::exists(directory / "specification.vhd"));
    {
        auto incomplete = specification;
        fsim::diagnostic::Engine diagnostics;
        assert(!detail::validate_and_link_vhdl_compiled_environment(
            incomplete, diagnostics, false));
        assert(has_code(diagnostics, "FSIM-FE-VHDECL-001"));
    }
    auto body = accepted(directory, "body", package_body, &specification);
    assert(!std::filesystem::exists(directory / "body.vhd"));
    fsim::diagnostic::Engine body_diagnostics;
    assert(detail::validate_and_link_vhdl_compiled_environment(
        body, body_diagnostics, false));
    const auto package = body.find_unit(semantic::UnitKind::vhdl_package,
        "support", "shared_values");
    assert(package && package->vhdl != nullptr);
    const auto package_body_unit = std::ranges::find_if(body.vhdl_units(),
        [](const auto& unit) {
            return unit.library == "support" && unit.name == "shared_values"
                && unit.kind == vh::UnitKind::package && !unit.primary_name.empty();
        });
    assert(package_body_unit != body.vhdl_units().end());
    assert(has_reference(body, package_body_unit->id,
        semantic::CompiledReferenceKind::package, package->identity->id));
    const auto& late = named_declaration(body, *package->vhdl, "late");
    assert(late.deferred && late.completion);
    const auto completion = body.find_declaration(*late.completion);
    assert(completion && completion->vhdl != nullptr);
    assert(completion->vhdl->initializer);

    auto context = accepted(directory, "context", R"(
context shared_context is
  library support;
  use support.shared_values.all;
end context;
)",
        &body);
    auto entity = accepted(directory, "entity", R"(
library support;
context support.shared_context;
entity consumer is
end entity;
)",
        &context, "work");
    auto design = accepted(directory, "architecture", R"(
library support;
context support.shared_context;
architecture rtl of consumer is
  constant total : word := plus_base(base) + late;
begin
end architecture;
)",
        &entity, "work");
    assert(!std::filesystem::exists(directory / "context.vhd"));
    assert(!std::filesystem::exists(directory / "entity.vhd"));
    const auto architecture = design.find_unit(semantic::UnitKind::vhdl_architecture,
        "work", "consumer", "rtl");
    assert(architecture && architecture->vhdl != nullptr);
    const auto compiled_context = design.find_unit(semantic::UnitKind::vhdl_context,
        "support", "shared_context");
    const auto compiled_specification = design.find_unit(semantic::UnitKind::vhdl_package,
        "support", "shared_values");
    assert(compiled_context && compiled_specification);
    assert(has_reference(design, architecture->identity->id,
        semantic::CompiledReferenceKind::context, compiled_context->identity->id));
    assert(has_reference(design, compiled_context->identity->id,
        semantic::CompiledReferenceKind::package, compiled_specification->identity->id));
    const auto& total = named_declaration(design, *architecture->vhdl, "total");
    assert(total.subtype && total.initializer);
    const semantic::CompiledDesignResolver resolver { design, architecture->identity->id };
    const auto resolved = resolver.effective_vhdl_subtype(*total.subtype, total.scope);
    assert(resolved && resolved->domain == vh::ValueDomain::integer);
    const auto specialized = semantic::make_specialized_hir_unit(
        design, architecture->identity->id,
        std::span<const semantic::SpecializedHirActualIdentity> { });
    assert(specialized);
    assert(specialized->evaluate_integral_expression(*total.initializer) == 59);
    const auto compiled_package = design.find_unit(semantic::UnitKind::vhdl_package,
        "support", "shared_values");
    assert(compiled_package && compiled_package->vhdl != nullptr);
    const auto& pattern = named_declaration(design, *compiled_package->vhdl, "pattern");
    const auto packed = specialized->evaluate_vhdl_packed_value_declaration(pattern.id);
    assert(packed && packed->bits == "1010");
    const auto& label = named_declaration(design, *compiled_package->vhdl, "label_text");
    assert(specialized->evaluate_string_declaration(label.id) == "OK");
    test_context_dependency_fingerprints(design, compiled_context->identity->id,
        architecture->identity->id);

    fsim::diagnostic::Engine mismatch;
    const auto incompatible = compile(directory, "standard_mismatch", R"(
library support;
context support.shared_context;
entity newer_consumer is end entity;
)",
        mismatch, &context, "work", "2019");
    assert(!incompatible);
    assert(has_code(mismatch, "FSIM-FE-VHORDER-011"));
}

void test_compiled_body_conformance(const std::filesystem::path& directory)
{
    auto specification = accepted(directory, "conformance_specification", R"(
package declarations is
  constant completed : integer := 1;
  constant deferred : integer;
end package;
)");
    fsim::diagnostic::Engine wrong_subtype;
    assert(!compile(directory, "wrong_subtype", R"(
package body declarations is
  constant deferred : boolean := true;
end package body;
)",
        wrong_subtype, &specification));
    assert(has_code(wrong_subtype, "FSIM-FE-VHDECL-002"));

    fsim::diagnostic::Engine missing_constant;
    assert(!compile(directory, "missing_constant", R"(
package body declarations is
end package body;
)",
        missing_constant, &specification));
    assert(has_code(missing_constant, "FSIM-FE-VHDECL-001"));

    fsim::diagnostic::Engine redeclaration;
    assert(!compile(directory, "redeclaration", R"(
package body declarations is
  constant completed : integer := 2;
  constant deferred : integer := 3;
end package body;
)",
        redeclaration, &specification));
    assert(has_code(redeclaration, "FSIM-FE-VHDECL-003"));

    auto constrained = accepted(directory, "constraint_specification", R"(
package constrained is
  constant value : integer range 0 to 3;
end package;
)");
    fsim::diagnostic::Engine wrong_constraint;
    assert(!compile(directory, "wrong_constraint", R"(
package body constrained is
  constant value : integer range 0 to 4 := 2;
end package body;
)",
        wrong_constraint, &constrained));
    assert(has_code(wrong_constraint, "FSIM-FE-VHDECL-002"));

    auto function_specification = accepted(directory, "function_specification", R"(
package subprograms is
  function value(argument : integer) return integer;
end package;
)");
    fsim::diagnostic::Engine wrong_formal;
    assert(!compile(directory, "wrong_formal", R"(
package body subprograms is
  function value(different : integer) return integer is
  begin
    return different;
  end function;
end package body;
)",
        wrong_formal, &function_specification));
    assert(has_code(wrong_formal, "FSIM-FE-VHDECL-004"));
}

void test_missing_workspace_entity(const std::filesystem::path& directory)
{
    const semantic::CompiledDesign empty_environment;
    fsim::diagnostic::Engine diagnostics;
    assert(!compile(directory, "missing_entity", R"(
architecture rtl of absent is
begin
end architecture;
)",
        diagnostics, &empty_environment, "work"));
    assert(has_code(diagnostics, "FSIM-FE-VHORDER-001"));
}

void test_workspace_compile_environment_isolation(const std::filesystem::path& directory)
{
    struct RestoreDirectory {
        std::filesystem::path previous { std::filesystem::current_path() };
        ~RestoreDirectory()
        {
            std::error_code error;
            std::filesystem::current_path(previous, error);
        }
    } restore;
    const auto root = directory / "environment-isolation";
    std::filesystem::create_directory(root);
    std::filesystem::current_path(root);
    const auto compile_source = [](const std::string& name, const std::string& language,
                                    const std::string& standard, const std::string_view source,
                                    const bool success = true) {
        {
            std::ofstream output(name);
            output << source;
            assert(output.good());
        }
        const std::vector<std::string> arguments { "fsim", "compile", "--quiet",
            "--lang", language, "--standard", standard, "--jobs", "8", name };
        std::vector<const char*> raw;
        for (const auto& argument : arguments) {
            raw.push_back(argument.c_str());
        }
        std::istringstream input;
        std::ostringstream output;
        std::ostringstream error;
        const auto status = fsim::cli::run(static_cast<int>(raw.size()), raw.data(),
            fsim::app::make_cli_services(input), output, error);
        if ((status == 0) != success) {
            std::cerr << name << ": " << output.str() << error.str();
        }
        assert((status == 0) == success);
        std::filesystem::remove(name);
        return error.str();
    };
    compile_source("legacy.vhd", "vhdl", "2008", R"(
package legacy_values is
  constant base : integer := 17;
end package;
context legacy_context is
  library work;
  use work.legacy_values.all;
end context;
entity legacy_entity is end entity;
)");
    compile_source("shared_values.sv", "systemverilog", "2017", R"(
package shared_values;
  parameter int base = 5;
endpackage
)");
    compile_source("modern.vhd", "vhdl", "2019", R"(
package modern_values is
  constant base : integer := 19;
end package;
)");
    compile_source("native.v", "verilog", "2005", R"(
module native_module;
endmodule
)");
    compile_source("consumer.sv", "systemverilog", "2017", R"(
module consumer;
  import shared_values::*;
  localparam int result = base;
  native_module implementation();
endmodule
)");
    compile_source("older_consumer.vhd", "vhdl", "2008", R"(
use work.legacy_values.all;
entity older_consumer is
  generic (value : integer := base);
end entity;
)");
    compile_source("newer_consumer.vhd", "vhdl", "2019", R"(
use work.modern_values.all;
entity newer_consumer is
  generic (value : integer := base);
end entity;
)");
    const auto incompatible = compile_source("incompatible.vhd", "vhdl", "2019", R"(
use work.legacy_values.all;
entity incompatible_package is end entity;
context work.legacy_context;
entity incompatible_context is end entity;
architecture incompatible_architecture of legacy_entity is
begin
end architecture;
)",
        false);
    assert(incompatible.find("FSIM-FE-VHORDER-011") != std::string::npos);
    assert(incompatible.find("VHDL package") != std::string::npos);
    assert(incompatible.find("VHDL context") != std::string::npos);
    assert(incompatible.find("VHDL entity") != std::string::npos);
    const auto replaced = compile_source("legacy.vhd", "vhdl", "2019", R"(
use work.legacy_values.all;
entity replacement is end entity;
)",
        false);
    assert(replaced.find("FSIM-FE-VHORDER-004") != std::string::npos);
    assert(replaced.find("FSIM-FE-VHORDER-011") == std::string::npos);

    std::string error;
    const workspace::Store store { root };
    const auto catalog = store.read_library("work", error);
    assert(catalog);
    for (const std::string_view name : { "legacy_values", "modern_values", "shared_values", "native_module" }) {
        assert(std::ranges::any_of(catalog->artifacts, [&](const auto& artifact) {
            return std::ranges::any_of(artifact.units, [&](const auto& owned) {
                return owned.unit.name == name;
            });
        }));
    }
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-workspace-vhdl-packages-" + std::to_string(nonce)) };
    std::filesystem::create_directories(directory.path);
    test_separate_package_compilation(directory.path);
    test_compiled_body_conformance(directory.path);
    test_missing_workspace_entity(directory.path);
    test_workspace_compile_environment_isolation(directory.path);
}
