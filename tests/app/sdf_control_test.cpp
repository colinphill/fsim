// SPDX-License-Identifier: Apache-2.0

#include "fsim/api.h"
#include "fsim/app/sdf_control.hpp"
#include "fsim/cli/driver.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error { std::string { message } };
}

void require_diagnostic(
    const std::vector<fsim::frontend::Diagnostic>& diagnostics,
    const std::string_view code)
{
    require(std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    }),
        "expected cataloged SDF control diagnostic");
}

fsim::app::SdfControlRequest request(
    const fsim::app::SdfControlSurface surface,
    const fsim::app::SdfControlPhase phase
    = fsim::app::SdfControlPhase::Elaborate,
    const std::size_t report_limit = 16U)
{
    using namespace fsim::app;
    SdfControlRequest result;
    result.surface = surface;
    result.phase = phase;
    result.selection = SdfDelaySelection::Maximum;
    result.report_limit = report_limit;
    result.inputs = { { "alpha.sdf", "alpha", "u*", 0U, 0U },
        { "beta.sdf", "beta", "u?", 1U, 2U } };
    return result;
}

void test_cpp_model_and_reports()
{
    using namespace fsim::app;
    const auto first = apply_sdf_control(
        request(SdfControlSurface::CppApi));
    const auto repeated = apply_sdf_control(
        request(SdfControlSurface::CppApi));
    require(first.ok() && repeated.ok(),
        "valid C++ SDF control must publish");
    require(first.application->semantic_identity()
            == repeated.application->semantic_identity(),
        "identical C++ SDF control must have a stable identity");
    require(first.application->summary().input_count == 2U
            && first.application->summary().file_count == 2U
            && first.application->summary().report_entry_count == 2U,
        "input and file summaries must be exact");
    require(first.application->report().size() == 2U
            && first.application->report()[0].kind
                == SdfControlEntryKind::Input,
        "input reports must retain stable source/object identities");

    const auto cli = apply_sdf_control(
        request(SdfControlSurface::ProjectCli));
    const auto tcl = apply_sdf_control(
        request(SdfControlSurface::Tcl));
    const auto c_api = apply_sdf_control(
        request(SdfControlSurface::CApi));
    require(cli.ok() && tcl.ok() && c_api.ok(),
        "all public SDF control surfaces must publish");
    require(cli.application->semantic_identity()
                != tcl.application->semantic_identity()
            && tcl.application->semantic_identity()
                != c_api.application->semantic_identity(),
        "surface identity must be explicit");

    const auto bounded = apply_sdf_control(
        request(SdfControlSurface::CppApi,
            SdfControlPhase::Elaborate, 1U));
    require(bounded.ok()
            && bounded.application->summary().report_entry_count == 2U
            && bounded.application->summary().report_truncated
            && bounded.application->report().size() == 1U,
        "bounded reports must retain exact totals and flag truncation");
}

void test_effective_counts_and_legality()
{
    using namespace fsim::app;
    auto configured = request(SdfControlSurface::CppApi,
        SdfControlPhase::Simulate);
    configured.generation = 7U;
    std::vector<SdfReannotationRevision> revisions {
        { "alpha.u0:q", "alpha.sdf", "alpha", "u*", 0U, 0U,
            false, "path-revision" },
        { "beta.u1:hold", "beta.sdf", "beta", "u?", 1U, 2U,
            true, "check-revision" }
    };
    auto effective = std::make_shared<const SdfReannotationApplication>(
        nullptr, fsim::elaboration::ElaboratedDesign { },
        std::move(revisions), 7U, "effective-7");
    const auto applied = apply_sdf_control(configured, effective);
    require(applied.ok()
            && applied.application->summary().applied_path_count == 1U
            && applied.application->summary().applied_timing_check_count == 1U
            && applied.application->summary().report_entry_count == 4U,
        "effective reports must count paths and checks exactly");

    auto compile = request(SdfControlSurface::CppApi,
        SdfControlPhase::Compile);
    const auto illegal = apply_sdf_control(std::move(compile));
    require(!illegal.ok(), "compile-phase annotation must be rejected");
    require_diagnostic(illegal.diagnostics, "FSIM-SDF-CONTROL-003");

    auto duplicate = request(SdfControlSurface::CppApi);
    duplicate.inputs.push_back(duplicate.inputs.front());
    const auto duplicated = apply_sdf_control(std::move(duplicate));
    require(!duplicated.ok(), "duplicate input scopes must be rejected");
    require_diagnostic(duplicated.diagnostics, "FSIM-SDF-CONTROL-002");

    configured.generation = 8U;
    const auto mismatch = apply_sdf_control(configured, effective);
    require(!mismatch.ok(), "generation mismatch must be rejected");
    require_diagnostic(mismatch.diagnostics, "FSIM-SDF-CONTROL-003");

    auto limited_request = request(SdfControlSurface::CppApi);
    SdfControlLimits limits;
    limits.max_inputs = 1U;
    const auto limited = apply_sdf_control(
        std::move(limited_request), nullptr, limits);
    require(!limited.ok(), "input resource limit must be enforced");
    require_diagnostic(limited.diagnostics, "FSIM-SDF-CONTROL-004");
}

void test_cli_surface()
{
    const std::array arguments { "fsim", "build", "--sdf", "alpha.sdf",
        "--sdf", "beta.sdf", "--sdf-root", "top", "--sdf-cell", "u*",
        "--sdf-report-limit", "3", "--delay-mode", "max" };
    fsim::diagnostic::Engine diagnostics;
    const auto parsed = fsim::cli::parse_arguments(
        static_cast<int>(arguments.size()), arguments.data(), diagnostics);
    require(parsed && !diagnostics.has_error(),
        "project CLI must accept SDF elaborate controls");
    require(parsed->sdf_files.size() == 2U
            && parsed->sdf_root == "top" && parsed->sdf_cell == "u*"
            && parsed->sdf_report_limit == 3U
            && parsed->delay_mode == fsim::project::DelayMode::maximum,
        "project CLI must retain ordered SDF controls");
    require(parsed->sdf_files.front().is_absolute(),
        "CLI SDF inputs must be normalized");

    const std::array illegal_arguments {
        "fsim", "check", "--sdf", "alpha.sdf"
    };
    fsim::diagnostic::Engine illegal_diagnostics;
    require(!fsim::cli::parse_arguments(
                static_cast<int>(illegal_arguments.size()),
                illegal_arguments.data(), illegal_diagnostics)
            && illegal_diagnostics.has_error(),
        "compile/check phase SDF controls must be rejected atomically");
}

fsim_string_view_t view(const std::string_view value)
{
    return { value.data(), value.size() };
}

void test_c_api_surface_and_rollback()
{
    fsim_session_t session = FSIM_INVALID_SESSION;
    require(fsim_session_create(nullptr, &session) == FSIM_STATUS_OK,
        "C API session creation must succeed");
    const fsim_sdf_input_t inputs[] { { sizeof(fsim_sdf_input_t),
        FSIM_API_VERSION, view("alpha.sdf"), view("alpha"), view("u*"),
        0U, 0U } };
    fsim_sdf_options_t options { sizeof(fsim_sdf_options_t),
        FSIM_API_VERSION, FSIM_SDF_DELAY_MAXIMUM, FSIM_SDF_PHASE_ELABORATE,
        inputs, 1U, 1U };
    require(fsim_session_configure_sdf(session, &options) == FSIM_STATUS_OK,
        "C API SDF configuration must publish");

    fsim_sdf_summary_t summary { };
    summary.struct_size = sizeof(summary);
    summary.api_version = FSIM_API_VERSION;
    require(fsim_session_get_sdf_summary(session, &summary) == FSIM_STATUS_OK
            && summary.input_count == 1U && summary.file_count == 1U
            && summary.report_entry_count == 1U
            && summary.returned_report_entry_count == 1U
            && summary.semantic_identity.size != 0U,
        "C API SDF summary must retain exact stable counts and identity");
    const std::string identity { summary.semantic_identity.data,
        summary.semantic_identity.size };

    fsim_sdf_report_entry_t entry { };
    entry.struct_size = sizeof(entry);
    entry.api_version = FSIM_API_VERSION;
    require(fsim_session_get_sdf_report_entry(session, 0U, &entry)
                == FSIM_STATUS_OK
            && entry.kind == FSIM_SDF_REPORT_INPUT
            && std::string_view { entry.source_identity.data,
                   entry.source_identity.size }
                == "alpha.sdf"
            && entry.canonical_identity.size != 0U,
        "C API report entries must expose source/object identities");

    options.phase = FSIM_SDF_PHASE_COMPILE;
    require(fsim_session_configure_sdf(session, &options)
            == FSIM_STATUS_INVALID_ARGUMENT,
        "C API compile-phase configuration must fail");
    fsim_sdf_summary_t retained { };
    retained.struct_size = sizeof(retained);
    retained.api_version = FSIM_API_VERSION;
    require(fsim_session_get_sdf_summary(session, &retained)
                == FSIM_STATUS_OK
            && std::string_view { retained.semantic_identity.data,
                   retained.semantic_identity.size }
                == identity,
        "failed C API configuration must retain the prior application");
    size_t diagnostic_count = 0U;
    require(fsim_session_diagnostic_count(session, &diagnostic_count)
                == FSIM_STATUS_OK
            && diagnostic_count == 1U,
        "C API failures must publish a bounded cataloged diagnostic");
    fsim_diagnostic_t diagnostic { };
    diagnostic.struct_size = sizeof(diagnostic);
    diagnostic.api_version = FSIM_API_VERSION;
    require(fsim_session_get_diagnostic(session, 0U, &diagnostic)
                == FSIM_STATUS_OK
            && std::string_view { diagnostic.code.data,
                   diagnostic.code.size }
                == "FSIM-SDF-CONTROL-003",
        "C API failure must retain the SDF control diagnostic code");
    require(fsim_session_destroy(session) == FSIM_STATUS_OK,
        "C API session destruction must succeed");
}

} // namespace

int main()
{
    try {
        test_cpp_model_and_reports();
        test_effective_counts_and_legality();
        test_cli_surface();
        test_c_api_surface_and_rollback();
        std::cout << "SDF control tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
