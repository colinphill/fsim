// SPDX-License-Identifier: Apache-2.0
// An SDF file that names VHDL cells cannot annotate their VITAL timing
// generics through a session: the generics are constants of elaboration, and
// only `fsim elaborate --sdf` annotates them (IEEE 1076.4-2000 5.1.1). The
// session reports FSIM-SDF-VITAL-008 and changes nothing.

#include "fsim/api.h"
#include "fsim/support/path.hpp"

#include "api_workspace_support.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef FSIM_TEST_SOURCE_DIR
#error "FSIM_TEST_SOURCE_DIR is required"
#endif

void test_sdf_vital_api(const std::filesystem::path& parent)
{
    namespace fs = std::filesystem;
    // A workspace of its own: the shared one sets a 1 ns resolution, and the
    // VITAL cells' picosecond defaults need the VHDL default of 1 fs.
    const fs::path directory = parent / "api-sdf-vital";
    fsim::tests::api_workspace::write_configuration(directory, "");
    const fs::path fixtures = fs::path { FSIM_TEST_SOURCE_DIR } / "tests"
        / "fixtures" / "sdf" / "vital_annotation";
    for (const auto* name : { "cells.vhd", "tb_path.vhd", "path_30.sdf" }) {
        fs::copy_file(fixtures / name, directory / name,
            fs::copy_options::overwrite_existing);
    }
    fsim::tests::api_workspace::expect(
        directory, { "compile", "--standard", "2008", "cells.vhd" });
    fsim::tests::api_workspace::expect(
        directory, { "compile", "--standard", "2008", "tb_path.vhd" });
    fsim::tests::api_workspace::expect(
        directory, { "elaborate", "--snapshot", "vital", "tb_path" });

    fsim_session_t session = FSIM_INVALID_SESSION;
    assert(fsim_session_create(nullptr, &session) == FSIM_STATUS_OK);
    assert(fsim_session_open_snapshot(session,
               fsim::support::path_to_utf8(directory).c_str(), "vital")
        == FSIM_STATUS_OK);

    const std::string file = fsim::support::path_to_utf8(directory / "path_30.sdf");
    const std::string root = "tb_path";
    const std::string cell = "*";
    fsim_sdf_input_t input { };
    input.struct_size = sizeof(input);
    input.api_version = FSIM_API_VERSION;
    input.source_identity = { file.data(), file.size() };
    input.root = { root.data(), root.size() };
    input.cell_pattern = { cell.data(), cell.size() };
    fsim_sdf_options_t options { };
    options.struct_size = sizeof(options);
    options.api_version = FSIM_API_VERSION;
    options.selection = FSIM_SDF_DELAY_TYPICAL;
    options.phase = FSIM_SDF_PHASE_SIMULATE;
    options.inputs = &input;
    options.input_count = 1U;
    options.report_limit = 16U;
    assert(fsim_session_configure_sdf(session, &options) != FSIM_STATUS_OK);

    bool reported { };
    std::size_t count { };
    assert(fsim_session_diagnostic_count(session, &count) == FSIM_STATUS_OK);
    for (std::size_t index = 0; index < count; ++index) {
        fsim_diagnostic_t diagnostic { };
        diagnostic.struct_size = sizeof(diagnostic);
        diagnostic.api_version = FSIM_API_VERSION;
        if (fsim_session_get_diagnostic(session, index, &diagnostic)
            == FSIM_STATUS_OK) {
            const std::string code { diagnostic.code.data, diagnostic.code.size };
            reported = reported || code == "FSIM-SDF-VITAL-008";
        }
    }
    assert(reported);
    assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
}
