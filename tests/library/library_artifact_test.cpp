// SPDX-License-Identifier: Apache-2.0
#include "fsim/library/artifact.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

namespace {

std::filesystem::path workspace()
{
    const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("fsim-library-artifact-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path);
    return path;
}

fsim::library::Metadata example_metadata()
{
    fsim::library::Metadata metadata;
    metadata.library = "vendor";
    metadata.producer = "fsim 0.2.0-dev";
    metadata.runtime_schema = 3;
    metadata.compiled_hir_artifact = "compiled/design.fsimhir";
    metadata.compiled_hir_checksum = std::string(64, '9');
    metadata.standards = {
        { "systemverilog", "2017" }, { "verilog", "2005" },
        { "vhdl", "2008" }
    };
    metadata.vhdl_package_dependencies = { { "2008", "ieee-1076-standard:2008:fsim-v3",
        "ieee.std_logic_unsigned",
        "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
        std::string(64, 'f') } };
    metadata.dependencies = { "ieee_models", "common" };
    metadata.sources = {
        { "sources/00000000/stage.sv", "sources/00000000/stage.sv",
            std::string(64, 'c'), "systemverilog", "2017", "none" }
    };
    metadata.units = {
        { "systemverilog", "module", "stage", { }, { },
            { }, { }, "2017", "none" },
        { "vhdl", "architecture", "rtl", "counter", "rtl",
            { }, { }, "2008",
            "fsim-synopsys-ieee-compat-v2" },
        { "verilog", "primitive", "invert_udp", { }, { }, { }, { },
            "2005", "none" }
    };
    metadata.auxiliary_artifacts = { { "sdf-annotation", "timing",
        "auxiliary/timing.fsimsdf", std::string(64, 'a'), "4.0",
        "none" } };
    metadata.native_artifacts = { { "llvm_object", "native/llvm/fixture.fobj", std::string(64, 'd'),
        1, 0, { }, std::string(64, 'f'), "22.1.0", "x86_64-test",
        "e-m:e-p:64:64", "generic",
        "+sse2", "O2", std::string(64, 'e') } };
    return metadata;
}

bool has_identity_diagnostic(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view family,
    const std::string_view found,
    const std::string_view required,
    const std::string_view artifact)
{
    const auto expected = "unsupported " + std::string { family }
        + " identity: found " + std::string { found } + "; required "
        + std::string { required } + "; regenerate "
        + std::string { artifact } + " with this fsim build";
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.message == expected;
        });
}

} // namespace

int main()
{
    static_assert(fsim::library::kFormatVersion == 6);
    static_assert(fsim::library::kPortableSchemaVersion == 15);
    static_assert(fsim::library::kCompiledHirSchemaVersion == 1);
    const auto expected = example_metadata();
    const auto serialized = fsim::library::serialize_metadata(expected);
    assert(fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(serialized))
        == "5959f55b12a09d8a1faad8dd7139c9688b22d3b5e765befc98ccd708b5643bf7");
    assert(serialized.starts_with(
        "format = 6\nlibrary = \"vendor\"\nproducer = \"fsim 0.2.0-dev\"\n"));
    assert(serialized.find("compiled_hir_schema = 1")
        != std::string::npos);
    assert(serialized.find("trace_archive = \"\"") != std::string::npos);
    assert(serialized.find("[[dependency]]") != std::string::npos);
    assert(serialized.find("[[vhdl_package_dependency]]")
        != std::string::npos);
    assert(serialized.find(".fsimir") == std::string::npos);
    assert(serialized.find(
               "kind = \"primitive\"\nname = \"invert_udp\"\n")
        != std::string::npos);
    assert(serialized.find("[[native]]") != std::string::npos);
    assert(serialized.find("[[auxiliary]]") != std::string::npos);
    assert(serialized.find("kind = \"sdf-annotation\"")
        != std::string::npos);

    fsim::diagnostic::Engine parse_diagnostics;
    const auto parsed = fsim::library::parse_metadata(
        serialized, "fsim-library.toml", parse_diagnostics);
    assert(parsed.has_value());
    assert(!parse_diagnostics.has_error());
    assert(*parsed == expected);
    assert(fsim::library::serialize_metadata(*parsed) == serialized);

    auto legacy_udp_metadata = expected;
    legacy_udp_metadata.units.back().artifact = "units/invert.fsimudp";
    legacy_udp_metadata.units.back().checksum = std::string(64, 'e');
    fsim::diagnostic::Engine legacy_udp_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(legacy_udp_metadata),
        "legacy-udp.fsimlib", legacy_udp_diagnostics));
    assert(std::ranges::any_of(
        legacy_udp_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0003";
        }));
    auto legacy_unit_metadata = expected;
    legacy_unit_metadata.units.front().artifact = "units/stage.fsimir";
    legacy_unit_metadata.units.front().checksum = std::string(64, 'a');
    fsim::diagnostic::Engine legacy_unit_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(legacy_unit_metadata),
        "legacy-unit.fsimlib", legacy_unit_diagnostics));
    auto legacy_class_metadata = expected;
    legacy_class_metadata.units.push_back({ "systemverilog",
        "class-runtime-adapter", "fixture", { }, { },
        "units/fixture.fsimclass", std::string(64, 'b'), "2017", "none" });
    fsim::diagnostic::Engine legacy_class_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(legacy_class_metadata),
        "legacy-class.fsimlib", legacy_class_diagnostics));

    auto invalid_auxiliary_metadata = expected;
    invalid_auxiliary_metadata.auxiliary_artifacts.front().artifact
        = "../timing.fsimsdf";
    fsim::diagnostic::Engine invalid_auxiliary_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(invalid_auxiliary_metadata),
        "invalid-auxiliary.fsimlib", invalid_auxiliary_diagnostics));
    assert(std::ranges::any_of(
        invalid_auxiliary_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0003"
                && diagnostic.message.find("every [[auxiliary]]")
                    != std::string::npos;
        }));

    auto duplicate_auxiliary_metadata = expected;
    duplicate_auxiliary_metadata.auxiliary_artifacts.push_back(
        duplicate_auxiliary_metadata.auxiliary_artifacts.front());
    duplicate_auxiliary_metadata.auxiliary_artifacts.back().artifact
        = "auxiliary/duplicate.fsimsdf";
    fsim::diagnostic::Engine duplicate_auxiliary_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(duplicate_auxiliary_metadata),
        "duplicate-auxiliary.fsimlib", duplicate_auxiliary_diagnostics));
    assert(std::ranges::any_of(
        duplicate_auxiliary_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0003"
                && diagnostic.message.find("every [[auxiliary]]")
                    != std::string::npos;
        }));

    const auto directory = workspace() / "vendor.fsimlib";
    std::filesystem::create_directories(directory);
    {
        std::ofstream output(
            directory / fsim::library::kMetadataFilename, std::ios::binary);
        output << serialized;
        assert(output.good());
    }
    fsim::diagnostic::Engine load_diagnostics;
    const auto loaded = fsim::library::load_metadata(
        directory, "vendor", load_diagnostics);
    assert(loaded == parsed);
    assert(!load_diagnostics.has_error());
    // Metadata loading is deliberately lazy: neither indexed payload exists.
    assert(!std::filesystem::exists(directory / "units"));

    const auto published = directory.parent_path() / "published.fsimlib";
    auto published_metadata = expected;
    const std::vector<fsim::library::PortablePayload> payloads {
        { "sources/00000000/stage.sv", "module stage; endmodule\n" },
        { "native/llvm/fixture.fobj", "native object fixture" },
        { "compiled/design.fsimhir", "compiled HIR fixture" },
        { "auxiliary/timing.fsimsdf", "portable SDF annotation fixture" }
    };
    for (auto& unit : published_metadata.units) {
        if (unit.artifact.empty()) {
            continue;
        }
        const auto payload = std::ranges::find(
            payloads, unit.artifact, &fsim::library::PortablePayload::path);
        assert(payload != payloads.end());
        unit.checksum = fsim::support::Sha256::hex(
            fsim::support::Sha256::digest(payload->bytes));
    }
    published_metadata.sources.front().checksum = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(payloads[0].bytes));
    published_metadata.native_artifacts.front().checksum = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(payloads[1].bytes));
    published_metadata.compiled_hir_checksum = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(payloads[2].bytes));
    published_metadata.auxiliary_artifacts.front().checksum
        = fsim::support::Sha256::hex(
            fsim::support::Sha256::digest(payloads[3].bytes));
    auto stale_publication_metadata = published_metadata;
    stale_publication_metadata.format = fsim::library::kFormatVersion - 1U;
    const auto stale_publication = directory.parent_path() / "stale-publication.fsimlib";
    fsim::diagnostic::Engine stale_publication_diagnostics;
    assert(!fsim::library::publish(
        stale_publication, stale_publication_metadata, payloads,
        stale_publication_diagnostics));
    assert(has_identity_diagnostic(
        stale_publication_diagnostics, ".fsimlib publication",
        "format 5 and portable-unit schema 15 and compiled-HIR schema 1",
        "format 6 and portable-unit schema 15 and compiled-HIR schema 1",
        ".fsimlib"));
    assert(!std::filesystem::exists(stale_publication));
    fsim::diagnostic::Engine publish_diagnostics;
    assert(fsim::library::publish(
        published, published_metadata, payloads, publish_diagnostics));
    assert(!publish_diagnostics.has_error());
    assert(std::filesystem::is_regular_file(
        published / fsim::library::kMetadataFilename));
    assert(std::ranges::none_of(
        std::filesystem::recursive_directory_iterator { published },
        [](const auto& entry) {
            return entry.path().extension() == ".fsimudp"
                || entry.path().extension() == ".fsimir";
        }));
    fsim::diagnostic::Engine published_load_diagnostics;
    assert(fsim::library::load_metadata(
               published, "vendor", published_load_diagnostics)
        == std::optional { published_metadata });

    auto source_hidden_metadata = published_metadata;
    source_hidden_metadata.sources.front().artifact.clear();
    source_hidden_metadata.sources.front().checksum.clear();
    auto source_hidden_payloads = payloads;
    source_hidden_payloads.erase(source_hidden_payloads.begin());
    const auto source_hidden = directory.parent_path() / "source-hidden.fsimlib";
    fsim::diagnostic::Engine source_hidden_diagnostics;
    assert(fsim::library::publish(
        source_hidden, source_hidden_metadata, source_hidden_payloads,
        source_hidden_diagnostics));
    assert(!source_hidden_diagnostics.has_error());
    assert(!std::filesystem::exists(source_hidden / "sources"));
    assert(std::filesystem::is_regular_file(
        source_hidden / source_hidden_metadata.native_artifacts.front().artifact));
    assert(std::filesystem::is_regular_file(
        source_hidden / source_hidden_metadata.compiled_hir_artifact));
    assert(std::filesystem::is_regular_file(
        source_hidden
        / source_hidden_metadata.auxiliary_artifacts.front().artifact));
    fsim::diagnostic::Engine source_hidden_load_diagnostics;
    assert(fsim::library::load_metadata(
               source_hidden, "vendor", source_hidden_load_diagnostics)
        == std::optional { source_hidden_metadata });
    assert(!source_hidden_load_diagnostics.has_error());
    fsim::diagnostic::Engine overwrite_diagnostics;
    assert(!fsim::library::publish(
        published, published_metadata, payloads, overwrite_diagnostics));
    fsim::diagnostic::Engine rollback_diagnostics;
    assert(fsim::library::load_metadata(
               published, "vendor", rollback_diagnostics)
        == std::optional { published_metadata });
    {
        std::ifstream input(published / payloads.front().path, std::ios::binary);
        assert((std::string {
                    std::istreambuf_iterator<char> { input },
                    std::istreambuf_iterator<char> { } }
            == payloads.front().bytes));
    }

    auto bad_payloads = payloads;
    bad_payloads.front().bytes = "corrupt";
    const auto rejected = directory.parent_path() / "rejected.fsimlib";
    fsim::diagnostic::Engine rejected_diagnostics;
    assert(!fsim::library::publish(
        rejected, published_metadata, bad_payloads, rejected_diagnostics));
    assert(!std::filesystem::exists(rejected));

    fsim::diagnostic::Engine wrong_name_diagnostics;
    assert(!fsim::library::load_metadata(
        directory, "other", wrong_name_diagnostics));
    assert(std::ranges::any_of(
        wrong_name_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0003"
                && diagnostic.message.find("metadata for 'vendor'")
                != std::string::npos;
        }));

    auto invalid_text = serialized;
    const auto safe_path = invalid_text.find("compiled/design.fsimhir");
    assert(safe_path != std::string::npos);
    invalid_text.replace(
        safe_path, std::string { "compiled/design.fsimhir" }.size(),
        "../escape.fsimir");
    fsim::diagnostic::Engine path_diagnostics;
    assert(!fsim::library::parse_metadata(
        invalid_text, "unsafe.toml", path_diagnostics));
    assert(std::ranges::any_of(
        path_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-LIB-0003"
                && diagnostic.message.find("contained compiled-HIR")
                != std::string::npos;
        }));

    auto duplicate_path_metadata = expected;
    duplicate_path_metadata.sources.front().artifact
        = duplicate_path_metadata.compiled_hir_artifact;
    fsim::diagnostic::Engine duplicate_path_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(duplicate_path_metadata),
        "duplicate-path.toml", duplicate_path_diagnostics));
    assert(std::ranges::any_of(
        duplicate_path_diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.message.find("unique logical_name")
                != std::string::npos;
        }));

    auto incompatible_text = serialized;
    incompatible_text.replace(
        incompatible_text.find("format = 6"),
        std::string { "format = 6" }.size(), "format = 99");
    fsim::diagnostic::Engine schema_diagnostics;
    assert(!fsim::library::parse_metadata(
        incompatible_text, "future.toml", schema_diagnostics));
    assert(has_identity_diagnostic(
        schema_diagnostics, ".fsimlib", "format 99", "format 6",
        ".fsimlib"));

    for (std::uint32_t format = 0;
        format < fsim::library::kFormatVersion; ++format) {
        auto stale_text = serialized;
        stale_text.replace(
            stale_text.find("format = 6"),
            std::string { "format = 6" }.size(),
            "format = " + std::to_string(format));
        fsim::diagnostic::Engine stale_schema_diagnostics;
        assert(!fsim::library::parse_metadata(
            stale_text, "stale.toml", stale_schema_diagnostics));
        assert(has_identity_diagnostic(
            stale_schema_diagnostics, ".fsimlib",
            "format " + std::to_string(format), "format 6", ".fsimlib"));
    }

    for (std::uint32_t schema = 0;
        schema < fsim::library::kPortableSchemaVersion; ++schema) {
        auto stale_portable_text = serialized;
        stale_portable_text.replace(
            stale_portable_text.find("portable_schema = 15"),
            std::string { "portable_schema = 15" }.size(),
            "portable_schema = " + std::to_string(schema));
        fsim::diagnostic::Engine stale_portable_diagnostics;
        assert(!fsim::library::parse_metadata(
            stale_portable_text, "stale-portable.toml",
            stale_portable_diagnostics));
        assert(has_identity_diagnostic(
            stale_portable_diagnostics, ".fsimlib",
            "portable-unit schema " + std::to_string(schema),
            "portable-unit schema 15", ".fsimlib"));
    }
    auto v2_library_text = serialized;
    v2_library_text.replace(
        v2_library_text.find("portable_schema = 15"),
        std::string { "portable_schema = 15" }.size(),
        "portable_schema = 10");
    for (const auto* const source : { "v2-library.toml",
             "v2-library-repeat.toml" }) {
        fsim::diagnostic::Engine v2_library_diagnostics;
        assert(!fsim::library::parse_metadata(
            v2_library_text, source, v2_library_diagnostics));
        assert(has_identity_diagnostic(
            v2_library_diagnostics, ".fsimlib", "portable-unit schema 10",
            "portable-unit schema 15", ".fsimlib"));
    }

    auto future_portable_text = serialized;
    future_portable_text.replace(
        future_portable_text.find("portable_schema = 15"),
        std::string { "portable_schema = 15" }.size(),
        "portable_schema = 16");
    fsim::diagnostic::Engine future_portable_diagnostics;
    assert(!fsim::library::parse_metadata(
        future_portable_text, "future-portable.toml",
        future_portable_diagnostics));
    assert(has_identity_diagnostic(
        future_portable_diagnostics, ".fsimlib",
        "portable-unit schema 16", "portable-unit schema 15", ".fsimlib"));

    for (const std::uint32_t schema : { 0U, 2U }) {
        auto incompatible_compiled_hir_text = serialized;
        incompatible_compiled_hir_text.replace(
            incompatible_compiled_hir_text.find(
                "compiled_hir_schema = 1"),
            std::string { "compiled_hir_schema = 1" }.size(),
            "compiled_hir_schema = " + std::to_string(schema));
        fsim::diagnostic::Engine compiled_hir_schema_diagnostics;
        assert(!fsim::library::parse_metadata(
            incompatible_compiled_hir_text,
            schema == 0U ? "stale-compiled-hir.toml"
                         : "future-compiled-hir.toml",
            compiled_hir_schema_diagnostics));
        assert(has_identity_diagnostic(
            compiled_hir_schema_diagnostics, ".fsimlib",
            "compiled-HIR schema " + std::to_string(schema),
            "compiled-HIR schema 1", ".fsimlib"));
    }

    auto systemc_metadata = expected;
    systemc_metadata.native_artifacts = { { "systemc_plugin", "native/systemc/libfixture.so",
        std::string(64, 'd'), 1, 4, "scv-2.0.1",
        "compiler-producer-fingerprint", { }, "x86_64-test", { },
        "generic", "+sse2", { }, { } } };
    fsim::diagnostic::Engine systemc_metadata_diagnostics;
    assert(fsim::library::parse_metadata(
               fsim::library::serialize_metadata(systemc_metadata),
               "systemc-native.toml", systemc_metadata_diagnostics)
        == std::optional { systemc_metadata });
    assert(!systemc_metadata_diagnostics.has_error());

    auto missing_scv_metadata = systemc_metadata;
    missing_scv_metadata.native_artifacts.front().scv_compatibility.clear();
    fsim::diagnostic::Engine missing_scv_metadata_diagnostics;
    assert(!fsim::library::parse_metadata(
        fsim::library::serialize_metadata(missing_scv_metadata),
        "missing-scv-native.toml", missing_scv_metadata_diagnostics));
    assert(missing_scv_metadata_diagnostics.has_error());
    const auto rejected_native = directory.parent_path() / "rejected-native.fsimlib";
    fsim::diagnostic::Engine rejected_native_diagnostics;
    assert(!fsim::library::publish(
        rejected_native, missing_scv_metadata, { },
        rejected_native_diagnostics));
    assert(rejected_native_diagnostics.has_error());
    assert(!std::filesystem::exists(rejected_native));

    std::error_code ignored;
    std::filesystem::remove_all(directory.parent_path(), ignored);
    std::cout << "library_artifact_test: all tests passed\n";
    return 0;
}
