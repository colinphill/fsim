// SPDX-License-Identifier: Apache-2.0
//
// Precompiled VHDL standard library artifacts (gate BD build D2): the
// packages of the libraries STD and IEEE that fsim serves (IEEE 1076-2019
// 16), compiled from each edition's sources at fsim build time into managed
// libraries (one std + ieee pair per edition) that every workspace maps.
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/token.hpp"
#include "fsim/semantic/compiled_design.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

/// What an artifact was built from. An artifact whose key differs from the
/// one this build expects is never used (FSIM-FE-VHSTD-007).
struct StandardLibraryArtifactKey {
    /// fsim::standard_library_cache_version.
    std::string cache_version;
    frontend::VhdlStandard standard { frontend::VhdlStandard::Vhdl2019 };
    /// SHA-256 (hex) of the edition source tree's SHA256SUMS file, recorded
    /// at configure time (the generated vhdl_standard_library_keys.inc).
    std::string sources_digest;
    /// semantic::vhdl_native_intrinsics_digest().
    std::string natives_digest;

    friend bool operator==(const StandardLibraryArtifactKey&,
        const StandardLibraryArtifactKey&) = default;
};

/// The editions with an artifact: 1987, 1993, 2000 (the 2002 sources), 2002,
/// 2008 and 2019.
[[nodiscard]] std::span<const frontend::VhdlStandard>
standard_library_artifact_editions() noexcept;

/// The key this build expects for `standard`.
[[nodiscard]] StandardLibraryArtifactKey expected_standard_library_artifact_key(
    frontend::VhdlStandard standard);

/// The source tree `standard` is analyzed from: third_party/ieee-derived/<y>
/// or third_party/ieee-1076-2019, relative to the source root.
[[nodiscard]] std::filesystem::path standard_library_source_tree(
    frontend::VhdlStandard standard);

/// The directory name of `standard`'s precompiled libraries (vhdl-2008).
[[nodiscard]] std::string standard_library_edition_directory_name(
    frontend::VhdlStandard standard);

/// The directory the editions' libraries are read from: the
/// FSIM_STANDARD_LIBRARY environment variable when set; else
/// <exe>/../share/fsim/lib/<cache version> beside an installed fsim; else
/// the build tree's directory compiled into this file. The first existing
/// candidate is used; a found but invalid library is an error, never a
/// reason to look further.
[[nodiscard]] std::filesystem::path standard_library_artifact_directory();

/// One precompiled standard library of one edition, as workspace library
/// commands list it (read-only, mapped into every workspace).
struct StandardLibraryLocation {
    std::string name;       // "std" or "ieee"
    frontend::VhdlStandard standard { frontend::VhdlStandard::Vhdl2008 };
    std::string label;      // "VHDL-2008"
    std::filesystem::path directory;
};

/// STD and IEEE of every edition, in edition order.
[[nodiscard]] std::vector<StandardLibraryLocation> standard_library_locations();

/// The file in an edition's directory that records the key its libraries
/// were built with.
inline constexpr std::string_view standard_library_key_file_name
    = "standard-library.key";

/// Writes `key` to `directory`/standard-library.key; false (with the reason
/// in `error`) when the file cannot be written.
[[nodiscard]] bool write_standard_library_key(
    const std::filesystem::path& directory,
    const StandardLibraryArtifactKey& key, std::string& error);

/// The precompiled libraries STD and IEEE of `standard`: managed libraries
/// (a library.sqlite3 catalog and one .fsimobj object per design unit, the
/// `fsim library map` form) under standard_library_artifact_directory()
/// /vhdl-<edition>/.fsim/libraries, built at fsim build time. A missing
/// directory or key file, or a key different from
/// expected_standard_library_artifact_key(standard), is FSIM-FE-VHSTD-007
/// naming the path. The loading of the objects themselves is the caller's
/// (it lives in the application layer): this checks the key and returns the
/// edition's workspace root.
[[nodiscard]] std::optional<std::filesystem::path>
checked_standard_library_root(
    frontend::VhdlStandard standard, diagnostic::Engine& diagnostics);

} // namespace fsim::app
