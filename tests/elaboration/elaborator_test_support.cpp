// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "../support/test_helpers.hpp"
#include "../../src/app/application_internal.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_normalization.hpp"
#include "fsim/frontend/class_resolution.hpp"
#include "fsim/frontend/class_specialization.hpp"
#include "fsim/frontend/coverage_resolution.hpp"
#include "fsim/frontend/design_core.hpp"
#include "fsim/frontend/diagnostic.hpp"
#include "fsim/diagnostic/diagnostic.hpp"

#include <cstdlib>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace fsim::tests::elaboration {

semantic::CompiledDesign compile_test_design(frontend::ParsedDesign parsed)
{
    // STD and IEEE come from the precompiled standard libraries, loaded and
    // linked as the check step does (gate BD D2).
    app::application_detail::CompilationWorkspace workspace;
    workspace.parsed = std::move(parsed);
    diagnostic::Engine standard_diagnostics;
    app::application_detail::inject_vhdl_standard_libraries(workspace,
        standard_diagnostics,
        app::application_detail::load_precompiled_standard_libraries);
    if (standard_diagnostics.has_error()) {
        throw std::runtime_error {
            "test design: the standard libraries could not be loaded: "
            + standard_diagnostics.diagnostics().front().message
        };
    }
    parsed = std::move(workspace.parsed);
    std::vector<frontend::Diagnostic> class_diagnostics;
    (void)frontend::resolve_systemverilog_classes(
        parsed, class_diagnostics);
    class_diagnostics.clear();
    (void)frontend::resolve_systemverilog_covergroups(
        parsed, class_diagnostics);
    auto class_specializations
        = frontend::specialize_systemverilog_classes(parsed);

    auto semantics = app::application_detail::build_semantic_model(
        parsed, { }, { }, { });
    auto vhdl = app::application_detail::build_vhdl_hir(
        parsed, semantics);
    auto systemverilog = app::application_detail::build_systemverilog_hir(
        parsed, semantics, class_specializations.specializations);
    semantic::CompiledDesign compiled {
        std::move(semantics),
        std::move(systemverilog),
        std::move(vhdl)
    };
    semantic::refresh_compiled_design_metadata(compiled);
    if (workspace.mapped_compiled_designs.empty()) {
        return compiled;
    }
    std::vector<semantic::CompiledDesign> inputs;
    for (auto& mapped : workspace.mapped_compiled_designs) {
        inputs.push_back(std::move(mapped));
    }
    inputs.push_back(std::move(compiled));
    const auto linked = app::application_detail::install_linked_compiled_design(
        workspace, std::move(inputs));
    if (!linked.ok()) {
        throw std::runtime_error {
            "test design: the standard libraries could not be linked: "
            + linked.error
        };
    }
    return std::move(static_cast<semantic::CompiledDesign&>(workspace));
}

void set_merge_constant_drivers(const bool enabled)
{
    const char* value = enabled ? "1" : "0";
#if defined(_WIN32)
    const auto status = ::_putenv_s("FSIM_MERGE_CONSTANT_DRIVERS", value);
#else
    const auto status = ::setenv("FSIM_MERGE_CONSTANT_DRIVERS", value, 1);
#endif
    if (status != 0) {
        throw std::runtime_error {
            "failed to set FSIM_MERGE_CONSTANT_DRIVERS"
        };
    }
}

fsim::elaboration::ElaborationResult compile_and_elaborate(
    frontend::ParsedDesign parsed,
    const std::span<const fsim::elaboration::Root> roots,
    const std::span<const fsim::elaboration::Binding> bindings,
    const std::span<const fsim::elaboration::SystemCInstanceDescription>
        systemc_instances,
    fsim::elaboration::SystemCFactoryProvider* const systemc_provider,
    const std::span<const std::string> search_libraries)
{
    auto compiled = compile_test_design(std::move(parsed));
    if (!semantic::normalize_compiled_design(compiled)) {
        fsim::elaboration::ElaborationResult result;
        result.diagnostics.push_back({
            "FSIM-ELAB-HIR-001",
            "test syntax could not be normalized into compiled HIR",
            { }
        });
        return result;
    }
    return fsim::elaboration::elaborate(
        compiled, roots, bindings, systemc_instances,
        systemc_provider, search_libraries);
}

bool has_diagnostic(const fsim::elaboration::ElaborationResult& result,
    const std::string_view code)
{
    return fsim::test::find_diagnostic(result.diagnostics, code) != nullptr;
}

std::vector<fsim::elaboration::SystemCFactoryCandidate>
TestSystemCFactoryProvider::candidates() const
{
    return factory_candidates;
}

std::vector<std::string> TestSystemCFactoryProvider::libraries() const
{
    auto result = available_libraries;
    for (const auto& candidate : factory_candidates) {
        if (std::ranges::find(result, candidate.library) == result.end()) {
            result.push_back(candidate.library);
        }
    }
    return result;
}

std::optional<std::vector<
    fsim::elaboration::SystemCConstructionParameter>>
TestSystemCFactoryProvider::schema(std::string_view, std::string& error)
{
    if (!schema_failure.empty()) {
        error = schema_failure;
        return std::nullopt;
    }
    error.clear();
    return parameters;
}

std::optional<fsim::elaboration::SystemCInstanceDescription>
TestSystemCFactoryProvider::instantiate(const std::string_view path,
    const std::string_view target,
    const std::span<const std::pair<std::string, std::int64_t>> values,
    std::string& error)
{
    if (!construction_failure.empty()) {
        error = construction_failure;
        return std::nullopt;
    }
    error.clear();
    auto result = prototype;
    result.path = path;
    result.target = target;
    result.handle = next_handle++;
    result.construction_values.assign(values.begin(), values.end());
    last_values = result.construction_values;
    const auto width = std::ranges::find(values, "WIDTH",
        &std::pair<std::string, std::int64_t>::first);
    if (width != values.end() && width->second > 0) {
        for (auto& port : result.ports) {
            if (port.name == "value" && width->second > 1) {
                port.type.packed_range = fsim::frontend::PackedRange {
                    width->second - 1, 0, true
                };
            }
        }
    }
    return result;
}

} // namespace fsim::tests::elaboration
