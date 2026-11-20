// SPDX-License-Identifier: Apache-2.0
//
// Precompiled VHDL standard libraries (gate BD build D2): the key each
// edition's managed libraries STD and IEEE are built with and checked
// against, and where they are looked up.
#include "application_standard_library_artifact_internal.hpp"

#include "fsim/semantic/vhdl_native_intrinsics.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#ifndef FSIM_STANDARD_LIBRARY_BUILD_DIR
#error "FSIM_STANDARD_LIBRARY_BUILD_DIR must name the build tree's standard library directory"
#endif

namespace fsim::app {

namespace {

constexpr std::string_view kMagic = "FSIMSTDL-KEY 1";
// standard_library_package_revision: fsim-stdlib:<version>:<edition>:<digest>.
constexpr std::string_view kRevisionPrefix = "fsim-stdlib:";

constexpr std::array<frontend::VhdlStandard, 6> kEditions {
    frontend::VhdlStandard::Vhdl1987,
    frontend::VhdlStandard::Vhdl1993,
    frontend::VhdlStandard::Vhdl2000,
    frontend::VhdlStandard::Vhdl2002,
    frontend::VhdlStandard::Vhdl2008,
    frontend::VhdlStandard::Vhdl2019,
};

std::string_view edition_year(const frontend::VhdlStandard standard)
{
    switch (standard) {
    case frontend::VhdlStandard::Vhdl1987:
        return "1987";
    case frontend::VhdlStandard::Vhdl1993:
        return "1993";
    case frontend::VhdlStandard::Vhdl2000:
        return "2000";
    case frontend::VhdlStandard::Vhdl2002:
        return "2002";
    case frontend::VhdlStandard::Vhdl2008:
        return "2008";
    case frontend::VhdlStandard::Vhdl2019:
        return "2019";
    }
    throw std::logic_error("unknown VHDL standard");
}

// SHA-256 of the SHA256SUMS file of the edition's source tree, recorded at
// configure time (the 2000 edition shares the 2002 tree).
std::string_view sources_digest(const frontend::VhdlStandard standard)
{
    switch (standard) {
#define FSIM_VHDL_STDLIB_SOURCES(name, digest)                                \
    case frontend::VhdlStandard::name:                                        \
        return digest;
#include "vhdl_standard_library_keys.inc"
#undef FSIM_VHDL_STDLIB_SOURCES
    default:
        break;
    }
    throw std::logic_error(
        "the generated standard library keys omit a VHDL edition");
}

// The running executable's own path (the same resolution as the driver's
// executable_path, without its argv[0] and PATH fallbacks: only an
// installed layout beside a resolvable executable is looked up).
std::filesystem::path running_executable()
{
    std::error_code code;
    auto path = support::native_fs::canonical("/proc/self/exe", code);
    if (code) {
        return { };
    }
    return path;
}

// SHA-256 of third_party/uvm-<release>/SHA256SUMS, recorded at configure
// time; empty without UVM sources.
std::string_view uvm_sources_digest(
    const project::SystemVerilogUvmRelease release)
{
    switch (release) {
#define FSIM_UVM_LIBRARY_SOURCES(name, digest)                                \
    case project::SystemVerilogUvmRelease::name:                              \
        return digest;
#include "uvm_library_keys.inc"
#undef FSIM_UVM_LIBRARY_SOURCES
    default:
        break;
    }
    return { };
}

constexpr std::string_view kUvmMagic = "FSIMUVML-KEY 1";

constexpr std::array<project::SystemVerilogUvmRelease, 2> kUvmReleases {
    project::SystemVerilogUvmRelease::uvm_1_2,
    project::SystemVerilogUvmRelease::ieee_1800_2_2020_3_1,
};

std::string field_text(const std::string_view value)
{
    constexpr std::size_t limit = 80;
    std::string text { value.substr(0U, limit) };
    if (value.size() > limit) {
        text += "...";
    }
    return text;
}

bool plain_field(const std::string_view value)
{
    return !value.empty()
        && value.find_first_of("\r\n=") == std::string_view::npos;
}

void report(diagnostic::Engine& diagnostics,
    const std::filesystem::path& path, const std::string_view reason)
{
    diagnostics.error("FSIM-FE-VHSTD-007",
        "the precompiled standard library key '" + support::path_to_utf8(path)
            + "' cannot be used: " + std::string { reason });
}

// Takes the next '\n'-terminated line of `bytes`.
std::optional<std::string_view> take_line(std::string_view& bytes)
{
    const auto end = bytes.find('\n');
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto line = bytes.substr(0U, end);
    bytes.remove_prefix(end + 1U);
    return line;
}

// Takes `name=value` from the next line.
std::optional<std::string_view> take_field(
    std::string_view& bytes, const std::string_view name)
{
    const auto line = take_line(bytes);
    if (!line || !line->starts_with(name) || line->size() <= name.size()
        || (*line)[name.size()] != '=') {
        return std::nullopt;
    }
    return line->substr(name.size() + 1U);
}

void report_uvm(diagnostic::Engine& diagnostics,
    const std::filesystem::path& path, const std::string_view reason)
{
    diagnostics.error("FSIM-UVM-LIBRARY-002",
        "the precompiled UVM library '" + support::path_to_utf8(path)
            + "' cannot be used: " + std::string { reason });
}

} // namespace

std::span<const frontend::VhdlStandard>
standard_library_artifact_editions() noexcept
{
    return kEditions;
}

StandardLibraryArtifactKey expected_standard_library_artifact_key(
    const frontend::VhdlStandard standard)
{
    StandardLibraryArtifactKey key;
    key.cache_version = std::string { standard_library_cache_version };
    key.standard = standard;
    key.sources_digest = std::string { sources_digest(standard) };
    key.natives_digest = std::string { semantic::vhdl_native_intrinsics_digest() };
    return key;
}

std::string standard_library_package_revision(
    const frontend::VhdlStandard standard)
{
    const auto key = expected_standard_library_artifact_key(standard);
    return std::string { kRevisionPrefix } + key.cache_version + ':'
        + std::string { edition_year(standard) } + ':' + key.sources_digest;
}

bool is_standard_library_package_revision(
    const std::string_view revision) noexcept
{
    return revision.starts_with(kRevisionPrefix);
}

std::filesystem::path standard_library_source_tree(
    const frontend::VhdlStandard standard)
{
    switch (standard) {
    case frontend::VhdlStandard::Vhdl1987:
        return std::filesystem::path { "third_party" } / "ieee-derived" / "1987";
    case frontend::VhdlStandard::Vhdl1993:
        return std::filesystem::path { "third_party" } / "ieee-derived" / "1993";
    case frontend::VhdlStandard::Vhdl2000:
    case frontend::VhdlStandard::Vhdl2002:
        return std::filesystem::path { "third_party" } / "ieee-derived" / "2002";
    case frontend::VhdlStandard::Vhdl2008:
        return std::filesystem::path { "third_party" } / "ieee-derived" / "2008";
    case frontend::VhdlStandard::Vhdl2019:
        return std::filesystem::path { "third_party" } / "ieee-1076-2019";
    }
    throw std::logic_error("unknown VHDL standard");
}

std::string standard_library_edition_directory_name(
    const frontend::VhdlStandard standard)
{
    return "vhdl-" + std::string { edition_year(standard) };
}

std::filesystem::path standard_library_artifact_directory()
{
    // An explicit directory is used as given; a missing file in it is then
    // reported by the load.
    if (const auto environment
        = support::environment_variable("FSIM_STANDARD_LIBRARY");
        environment && !environment->empty()) {
        return support::path_from_utf8(*environment);
    }
    const auto executable = running_executable();
    if (!executable.empty()) {
        const auto installed = executable.parent_path().parent_path()
            / "share" / "fsim" / "lib"
            / std::string { standard_library_cache_version };
        std::error_code code;
        if (support::native_fs::is_directory(installed, code)) {
            return installed;
        }
    }
    return support::path_from_utf8(FSIM_STANDARD_LIBRARY_BUILD_DIR);
}

std::vector<StandardLibraryLocation> standard_library_locations()
{
    std::vector<StandardLibraryLocation> result;
    const auto directory = standard_library_artifact_directory();
    for (const auto standard : kEditions) {
        const auto root = directory / standard_library_edition_directory_name(standard);
        for (const std::string_view name : { "std", "ieee" }) {
            result.push_back({ std::string { name }, standard,
                "VHDL-" + std::string { edition_year(standard) },
                root / ".fsim" / "libraries" / std::string { name } });
        }
    }
    return result;
}

bool write_standard_library_key(const std::filesystem::path& directory,
    const StandardLibraryArtifactKey& key, std::string& error)
{
    if (!plain_field(key.cache_version) || !plain_field(key.sources_digest)
        || !plain_field(key.natives_digest)) {
        throw std::logic_error(
            "a standard library key field is empty or holds a line break "
            "or '='");
    }
    std::string bytes;
    bytes += kMagic;
    bytes += "\ncache_version=" + key.cache_version;
    bytes += "\nstandard=" + std::string { edition_year(key.standard) };
    bytes += "\nsources_digest=" + key.sources_digest;
    bytes += "\nnatives_digest=" + key.natives_digest;
    bytes += "\n";
    const auto path = directory / std::string { standard_library_key_file_name };
    auto output = support::native_fs::open_ofstream(
        path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        error = "cannot write '" + support::path_to_utf8(path) + "'";
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> checked_standard_library_root(
    const frontend::VhdlStandard standard, diagnostic::Engine& diagnostics)
{
    const auto root = standard_library_artifact_directory()
        / standard_library_edition_directory_name(standard);
    const auto path = root / std::string { standard_library_key_file_name };
    std::error_code code;
    if (!support::native_fs::is_regular_file(path, code)) {
        report(diagnostics, path, "missing");
        return std::nullopt;
    }
    auto input = support::native_fs::open_ifstream(path, std::ios::binary);
    if (!input) {
        report(diagnostics, path, "missing (cannot be opened)");
        return std::nullopt;
    }
    const std::string content {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
    std::string_view bytes { content };
    const auto magic = take_line(bytes);
    if (!magic || *magic != kMagic) {
        report(diagnostics, path, "corrupt: not an FSIMSTDL-KEY 1 file");
        return std::nullopt;
    }
    const auto cache_version = take_field(bytes, "cache_version");
    const auto edition = take_field(bytes, "standard");
    const auto digest = take_field(bytes, "sources_digest");
    const auto natives = take_field(bytes, "natives_digest");
    if (!cache_version || !edition || !digest || !natives || !bytes.empty()) {
        report(diagnostics, path, "corrupt: malformed key");
        return std::nullopt;
    }
    const auto expected = expected_standard_library_artifact_key(standard);
    const auto differs = [&](const std::string_view name,
                             const std::string_view found,
                             const std::string_view wanted) {
        if (found == wanted) {
            return false;
        }
        report(diagnostics, path,
            "key field " + std::string { name } + " differs: found "
                + field_text(found) + ", expected " + std::string { wanted });
        return true;
    };
    if (differs("cache_version", *cache_version, expected.cache_version)
        || differs("standard", *edition, edition_year(standard))
        || differs("sources_digest", *digest, expected.sources_digest)
        || differs("natives_digest", *natives, expected.natives_digest)) {
        return std::nullopt;
    }
    return root;
}

std::span<const project::SystemVerilogUvmRelease>
uvm_library_releases() noexcept
{
    return kUvmReleases;
}

std::string uvm_library_compile_options()
{
    return "library:uvm;language:systemverilog;standard:2017;"
           "compilation_unit:file;defines:";
}

UvmLibraryArtifactKey expected_uvm_library_artifact_key(
    const project::SystemVerilogUvmRelease release)
{
    UvmLibraryArtifactKey key;
    key.cache_version = std::string { standard_library_cache_version };
    key.release = release;
    key.sources_digest = std::string { uvm_sources_digest(release) };
    key.options = uvm_library_compile_options();
    key.natives_digest = std::string { semantic::vhdl_native_intrinsics_digest() };
    return key;
}

std::string uvm_library_release_directory_name(
    const project::SystemVerilogUvmRelease release)
{
    return "uvm-" + std::string { project::to_string(release) };
}

std::filesystem::path uvm_library_root(
    const project::SystemVerilogUvmRelease release)
{
    return standard_library_artifact_directory() / "systemverilog"
        / uvm_library_release_directory_name(release);
}

std::filesystem::path uvm_library_directory(const std::filesystem::path& root)
{
    return root / ".fsim" / "libraries" / "uvm";
}

std::filesystem::path uvm_library_include_directory(
    const std::filesystem::path& root)
{
    return root / "src";
}

std::vector<UvmLibraryLocation> uvm_library_locations()
{
    std::vector<UvmLibraryLocation> result;
    for (const auto release : kUvmReleases) {
        const auto root = uvm_library_root(release);
        result.push_back({ "uvm", release,
            "UVM-" + std::string { project::to_string(release) },
            uvm_library_directory(root), root });
    }
    return result;
}

bool write_uvm_library_key(const std::filesystem::path& directory,
    const UvmLibraryArtifactKey& key, std::string& error)
{
    if (!plain_field(key.cache_version) || !plain_field(key.sources_digest)
        || !plain_field(key.options) || !plain_field(key.natives_digest)) {
        throw std::logic_error(
            "a UVM library key field is empty or holds a line break or '='");
    }
    std::string bytes;
    bytes += kUvmMagic;
    bytes += "\ncache_version=" + key.cache_version;
    bytes += "\nrelease=" + std::string { project::to_string(key.release) };
    bytes += "\nsources_digest=" + key.sources_digest;
    bytes += "\noptions=" + key.options;
    bytes += "\nnatives_digest=" + key.natives_digest;
    bytes += "\n";
    const auto path = directory / std::string { standard_library_key_file_name };
    auto output = support::native_fs::open_ofstream(
        path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) {
        error = "cannot write '" + support::path_to_utf8(path) + "'";
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> checked_uvm_library_root(
    const project::SystemVerilogUvmRelease release,
    diagnostic::Engine& diagnostics)
{
    const auto root = uvm_library_root(release);
    const auto path = root / std::string { standard_library_key_file_name };
    const auto expected = expected_uvm_library_artifact_key(release);
    if (expected.sources_digest.empty()) {
        report_uvm(diagnostics, path,
            "this fsim was built without the UVM " + std::string { project::to_string(release) }
                + " sources (FSIM_UVM_SOURCE_MODE=OFF)");
        return std::nullopt;
    }
    std::error_code code;
    if (!support::native_fs::is_regular_file(path, code)) {
        report_uvm(diagnostics, path, "missing");
        return std::nullopt;
    }
    auto input = support::native_fs::open_ifstream(path, std::ios::binary);
    if (!input) {
        report_uvm(diagnostics, path, "missing (cannot be opened)");
        return std::nullopt;
    }
    const std::string content {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
    std::string_view bytes { content };
    const auto magic = take_line(bytes);
    if (!magic || *magic != kUvmMagic) {
        report_uvm(diagnostics, path, "corrupt: not an FSIMUVML-KEY 1 file");
        return std::nullopt;
    }
    const auto cache_version = take_field(bytes, "cache_version");
    const auto found_release = take_field(bytes, "release");
    const auto digest = take_field(bytes, "sources_digest");
    const auto options = take_field(bytes, "options");
    const auto natives = take_field(bytes, "natives_digest");
    if (!cache_version || !found_release || !digest || !options || !natives
        || !bytes.empty()) {
        report_uvm(diagnostics, path, "corrupt: malformed key");
        return std::nullopt;
    }
    const auto differs = [&](const std::string_view name,
                             const std::string_view found,
                             const std::string_view wanted) {
        if (found == wanted) {
            return false;
        }
        report_uvm(diagnostics, path,
            "key field " + std::string { name } + " differs: found "
                + field_text(found) + ", expected " + field_text(wanted));
        return true;
    };
    if (differs("cache_version", *cache_version, expected.cache_version)
        || differs("release", *found_release, project::to_string(release))
        || differs("sources_digest", *digest, expected.sources_digest)
        || differs("options", *options, expected.options)
        || differs("natives_digest", *natives, expected.natives_digest)) {
        return std::nullopt;
    }
    return root;
}

} // namespace fsim::app
