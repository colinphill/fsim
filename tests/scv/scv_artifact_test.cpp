// SPDX-License-Identifier: Apache-2.0

#include "fsim/library/artifact.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/scv.hpp"
#include "fsim/systemc/scv_artifact.hpp"
#include "fsim/systemc_abi.h"
#include "fsim/version.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string checksum(const std::string_view bytes)
{
    return fsim::support::Sha256::hex(fsim::support::Sha256::digest(bytes));
}

void make_writable(const std::filesystem::path& root)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
        !error && iterator != end; iterator.increment(error)) {
        std::filesystem::permissions(
            iterator->path(), std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, error);
        error.clear();
    }
    std::filesystem::permissions(
        root, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, error);
}

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

} // namespace

int main()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-scv-artifact-" + unique);
    std::filesystem::create_directories(root);

    const std::string native_bytes = "bounded SCV native image fixture";
    fsim::library::Metadata metadata;
    metadata.library = "scv_models";
    metadata.producer = "fsim test";
    metadata.runtime_schema = fsim::runtime_abi_version;
    metadata.sources.push_back({
        "models.cpp", { }, { }, "systemc", { }, "none" });
    metadata.native_artifacts.push_back({
        "systemc_plugin", "native/systemc/scv-models.so",
        checksum(native_bytes), fsim::runtime_abi_version,
        FSIM_SYSTEMC_ABI_VERSION, fsim_scv_compatibility_identity(),
        checksum("compiler"), { }, "test-target", { }, "compiler-default",
        { }, { }, { } });

    const auto serialized = fsim::library::serialize_metadata(metadata);
    assert(serialized.starts_with("format = 5\n"));
    assert(serialized.find("scv_compatibility = \"") != std::string::npos);
    fsim::diagnostic::Engine parse_diagnostics;
    const auto parsed = fsim::library::parse_metadata(
        serialized, "scv-mapped-library", parse_diagnostics);
    assert(parsed == metadata && !parse_diagnostics.has_error());

    fsim::diagnostic::Engine exact_diagnostics;
    assert(fsim::systemc::validate_scv_artifact_compatibility(
        parsed->native_artifacts.front().scv_compatibility,
        "mapped SystemC library", exact_diagnostics));
    assert(!exact_diagnostics.has_error());

    const auto published = root / "scv-models.fsimlib";
    const std::vector<fsim::library::PortablePayload> payloads {
        { metadata.native_artifacts.front().artifact, native_bytes }
    };
    fsim::diagnostic::Engine publish_diagnostics;
    assert(fsim::library::publish(
        published, metadata, payloads, publish_diagnostics));
    assert(!publish_diagnostics.has_error());
    assert(metadata.sources.front().artifact.empty());

    const auto relocated = root
        / fsim::support::path_from_utf8("relocated-\xe6\xa8\xa1\xe5\x9e\x8b.fsimlib");
    std::filesystem::rename(published, relocated);
    fsim::diagnostic::Engine relocation_diagnostics;
    const auto relocated_metadata = fsim::library::load_metadata(
        relocated, "scv_models", relocation_diagnostics);
    assert(relocated_metadata == metadata && !relocation_diagnostics.has_error());
    std::ifstream relocated_payload(
        relocated / metadata.native_artifacts.front().artifact,
        std::ios::binary);
    const std::string relocated_bytes {
        std::istreambuf_iterator<char> { relocated_payload },
        std::istreambuf_iterator<char> { }
    };
    assert(relocated_bytes == native_bytes);

    auto stale_identity = relocated_metadata->native_artifacts.front()
                              .scv_compatibility;
    const auto compiler = stale_identity.find("|compiler=");
    assert(compiler != std::string::npos);
    stale_identity[compiler + 10U] =
        stale_identity[compiler + 10U] == 'x' ? 'y' : 'x';
    fsim::diagnostic::Engine stale_diagnostics;
    assert(!fsim::systemc::validate_scv_artifact_compatibility(
        stale_identity, "mapped SystemC library", stale_diagnostics));
    assert(has_code(stale_diagnostics, "FSIM-SCV-A001"));

    fsim::diagnostic::Engine missing_diagnostics;
    assert(!fsim::systemc::validate_scv_artifact_compatibility(
        { }, ".fsimscobj producer", missing_diagnostics));
    assert(has_code(missing_diagnostics, "FSIM-SCV-A001"));
    fsim::diagnostic::Engine oversized_diagnostics;
    assert(!fsim::systemc::validate_scv_artifact_compatibility(
        std::string(fsim::systemc::scv_artifact_identity_limit + 1U, 'x'),
        ".fsimdesign producer", oversized_diagnostics));
    assert(has_code(oversized_diagnostics, "FSIM-SCV-A001"));

    auto future = serialized;
    const auto current_format = "format = "
        + std::to_string(fsim::library::kFormatVersion);
    const auto future_format = "format = "
        + std::to_string(fsim::library::kFormatVersion + 1U);
    const auto format_offset = future.find(current_format);
    assert(format_offset != std::string::npos);
    future.replace(format_offset, current_format.size(), future_format);
    fsim::diagnostic::Engine future_diagnostics;
    assert(!fsim::library::parse_metadata(
        future, "future-scv-library", future_diagnostics));
    assert(future_diagnostics.has_error());

    auto corrupt_payloads = payloads;
    corrupt_payloads.front().bytes += " corrupt";
    const auto rejected = root / "rejected.fsimlib";
    fsim::diagnostic::Engine corrupt_diagnostics;
    assert(!fsim::library::publish(
        rejected, metadata, corrupt_payloads, corrupt_diagnostics));
    assert(corrupt_diagnostics.has_error());
    assert(!std::filesystem::exists(rejected));

    make_writable(relocated);
    std::filesystem::remove_all(root);
}
