// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/incremental.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace {

void write_file(
    const std::filesystem::path& path,
    const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    assert(output.good());
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

void copy_tree(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination);
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(source)) {
        const auto relative = std::filesystem::relative(entry.path(), source);
        if (entry.is_directory()) {
            std::filesystem::create_directories(destination / relative);
        } else {
            std::filesystem::copy_file(entry.path(), destination / relative);
        }
    }
}

void store_u32(
    std::string& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    assert(offset + 4U <= bytes.size());
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U]
            = static_cast<char>((value >> shift) & 0xffU);
    }
}

void store_u64(
    std::string& bytes,
    const std::size_t offset,
    const std::uint64_t value)
{
    assert(offset + 8U <= bytes.size());
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes[offset + shift / 8U]
            = static_cast<char>((value >> shift) & 0xffU);
    }
}

bool has_identity_diagnostic(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view family,
    const std::string_view found,
    const std::string_view required,
    const std::string_view artifact)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.message.find(
                       "unsupported " + std::string { family }
                       + " identity: found " + std::string { found })
                != std::string::npos
                && diagnostic.message.find(
                       "; required " + std::string { required })
                != std::string::npos
                && diagnostic.message.ends_with(
                    "; regenerate " + std::string { artifact }
                    + " with this fsim build");
        });
}

std::string module_source(
    const std::string& type,
    const std::string& alias,
    const bool parameterized)
{
    std::string result = "#include \"fsim/systemc.hpp\"\n"
                         "SC_MODULE("
        + type + ") {\n"
                 "  sc_core::sc_in<sc_dt::sc_logic> value{\"value\"};\n"
                 "  sc_core::sc_out<sc_dt::sc_logic> result{\"result\"};\n";
    if (parameterized) {
        result += "  inline static constexpr auto fsim_factory_parameters =\n"
                  "      fsim::systemc::make_factory_parameters(\n"
                  "          fsim::systemc::factory_parameter{\"width\", "
                  "FSIM_SC_CONSTRUCTION_POSITIVE, true, 1});\n";
    }
    result += "  SC_CTOR(" + type + ") {}\n"
                                    "};\n"
                                    "SC_FSIM_EXPORT_AS("
        + type + ", \"" + alias + "\");\n";
    return result;
}

[[nodiscard]] bool has_argument(
    const int argc,
    char* const* argv,
    const std::string_view expected)
{
    for (int index = 1; index < argc; ++index) {
        if (std::string_view { argv[index] } == expected) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::filesystem::path> prefixed_path_argument(
    const int argc,
    char* const* argv,
    const std::string_view prefix)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument { argv[index] };
        if (argument.starts_with(prefix)) {
            return std::filesystem::path { argument.substr(prefix.size()) };
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> clang_argument_after(
    const int argc,
    char* const* argv,
    const std::string_view option)
{
    constexpr std::string_view prefix = "/clang:";
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view { argv[index] } == option) {
            const std::string_view value { argv[index + 1] };
            if (value.starts_with(prefix)) {
                return std::filesystem::path { value.substr(prefix.size()) };
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> source_argument(
    const int argc,
    char* const* argv)
{
    for (int index = argc - 1; index > 0; --index) {
        const auto path = std::filesystem::path { argv[index] };
        if (path.extension() == ".cpp" || path.extension() == ".cc"
            || path.extension() == ".cxx") {
            return path;
        }
    }
    return std::nullopt;
}

int run_clang_cl_fake_compiler(const int argc, char* const* argv)
{
    const auto source = source_argument(argc, argv);
    const auto object = prefixed_path_argument(argc, argv, "/Fo");
    if (!source || !object) {
        return 81;
    }
    if (!has_argument(argc, argv, "/vmg")) {
        return 83;
    }
    if (has_argument(argc, argv, "/clang:-MD")) {
        const auto dependency_argument =
            clang_argument_after(argc, argv, "/clang:-MF");
        if (!dependency_argument
            || dependency_argument->filename() != "translation-unit.d") {
            return 82;
        }
        const auto dependency_source =
#if defined(_WIN32)
            source->string();
#else
            source->generic_string();
#endif
        write_file(
            *dependency_argument,
            "fsim_systemc_object: " + dependency_source + "\n");
    }
    write_file(*object, "fake clang-cl object\n");
    return 0;
}

} // namespace

int main(const int argc, char** argv)
{
    static_assert(fsim::systemc::kIncrementalObjectFormatVersion == 2U);
    static_assert(fsim::systemc::kIncrementalPluginFormatVersion == 2U);
    if (has_argument(
            argc, argv, "/DFSIM_TEST_CLANG_CL_INCREMENTAL_COMPILER=1")) {
        return run_clang_cl_fake_compiler(argc, argv);
    }
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-systemc-incremental-test-" + unique);
    std::filesystem::create_directories(root);
    const auto first_source = root / "first.cpp";
    const auto second_source = root / "second.cpp";
    const auto handwritten_source = root / "handwritten.cpp";
    write_file(first_source, module_source("FirstModule", "first", true));
    write_file(second_source, module_source("SecondModule", "second", false));
    write_file(
        handwritten_source,
        "#include \"fsim/systemc_abi.h\"\n"
        "namespace {\n"
        "fsim_sc_status_v1 elaborate(void*, const char*, fsim_sc_handle_v1,\n"
        "    fsim_sc_handle_v1, void** result) {\n"
        "  *result = reinterpret_cast<void*>(1);\n"
        "  return FSIM_SC_OK;\n"
        "}\n"
        "void destroy(void*, void*) {}\n"
        "}\n"
        "extern \"C\" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(\n"
        "    const fsim_sc_host_v1*, fsim_sc_registrar_v1* registrar) {\n"
        "  return registrar->register_elaboration_factory(\n"
        "      registrar->context, \"handwritten\", elaborate, destroy, nullptr);\n"
        "}\n");

    fsim::project::SystemCSection settings;
    fsim::diagnostic::Engine diagnostics;
    fsim::systemc::IncrementalCompileRequest first_request;
    first_request.source = first_source;
    first_request.output = root / "first.fsimscobj";
    first_request.settings = settings;
    first_request.working_directory = root;
    first_request.scratch_directory = root / "scratch";
    const auto first_compiled = fsim::systemc::compile_incremental_object(
        first_request, diagnostics);
    if (!first_compiled) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(first_compiled);
    assert(!diagnostics.has_error());

#if !defined(_WIN32)
    // Exercise clang-cl's Make dependency-file route on every host with a
    // modeled compiler. The real compile writes a non-empty object payload so
    // incremental publication and metadata validation run as well.
    const auto fake_clang_cl = root / "clang-cl.exe";
    std::error_code fake_error;
    std::filesystem::copy_file(
        std::filesystem::absolute(std::filesystem::path { argv[0] }, fake_error),
        fake_clang_cl,
        std::filesystem::copy_options::overwrite_existing,
        fake_error);
    assert(!fake_error);
    std::filesystem::permissions(
        fake_clang_cl,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add,
        fake_error);
    assert(!fake_error);
    const auto fake_source = root / "fake-clang-cl.cpp";
    write_file(fake_source, "extern \"C\" int fake_clang_cl() { return 1; }\n");
    auto fake_request = first_request;
    fake_request.source = fake_source;
    fake_request.output = root / "fake-clang-cl.fsimscobj";
    fake_request.settings.compiler = fake_clang_cl.string();
    fake_request.settings.defines = {
        "FSIM_TEST_CLANG_CL_INCREMENTAL_COMPILER=1"
    };
    fsim::diagnostic::Engine fake_diagnostics;
    assert(fsim::systemc::compile_incremental_object(
        fake_request, fake_diagnostics));
    assert(!fake_diagnostics.has_error());
    const auto fake_metadata = fsim::systemc::load_incremental_object_metadata(
        fake_request.output, fake_diagnostics);
    assert(fake_metadata && fake_metadata->toolchain == "msvc");
#endif

    auto cached_first_request = first_request;
    cached_first_request.output.clear();
    const auto incremental_cache = root / "incremental-cache";
    auto cached_first = fsim::systemc::compile_incremental_object_cached(
        cached_first_request, incremental_cache, diagnostics);
    assert(cached_first.success && !cached_first.cache_hit);
    auto cached_first_warm = fsim::systemc::compile_incremental_object_cached(
        cached_first_request, incremental_cache, diagnostics);
    assert(cached_first_warm.success && cached_first_warm.cache_hit);
    assert(cached_first_warm.artifact == cached_first.artifact);

    auto first_metadata = fsim::systemc::load_incremental_object_metadata(
        first_request.output, diagnostics);
    assert(first_metadata.has_value());
    assert(first_metadata->scv_compatibility
        == fsim_scv_compatibility_identity());
    assert(first_metadata->inputs.size() > 1);
    assert(first_metadata->compilation_digest
        == fsim::systemc::compute_incremental_object_digest(*first_metadata));
    const auto first_round_trip = fsim::systemc::serialize_incremental_object_metadata(*first_metadata);
    auto first_decoded = fsim::systemc::deserialize_incremental_object_metadata(
        first_round_trip, "first-round-trip", diagnostics);
    assert(first_decoded == first_metadata);

    const auto assert_object_input_changes = [&](const auto& mutate) {
        auto changed = *first_metadata;
        mutate(changed);
        assert(fsim::systemc::compute_incremental_object_input_digest(changed)
            != first_metadata->input_digest);
    };
    assert_object_input_changes([](auto& changed) {
        changed.toolchain += "-changed";
    });
    assert_object_input_changes([](auto& changed) {
        changed.target += "-changed";
    });
    assert_object_input_changes([](auto& changed) {
        changed.compiler_fingerprint = std::string(64, '1');
    });
    assert_object_input_changes([](auto& changed) {
        changed.scv_compatibility += "-changed";
    });
    assert_object_input_changes([](auto& changed) {
        changed.defines.push_back("FSIM_IDENTITY_CHANGE=1");
    });
    assert_object_input_changes([](auto& changed) {
        changed.compile_options.push_back("-ffsim-identity-change");
    });
    assert_object_input_changes([](auto& changed) {
        changed.inputs.back().checksum = std::string(64, '2');
    });
    assert_object_input_changes([](auto& changed) {
        changed.defines_plugin_entry_point = !changed.defines_plugin_entry_point;
    });
    assert_object_input_changes([](auto& changed) {
        changed.contains_macro_export = !changed.contains_macro_export;
    });

    auto corrupt_object_magic = first_round_trip;
    corrupt_object_magic[0] = 'X';
    fsim::diagnostic::Engine corrupt_object_magic_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        corrupt_object_magic, "corrupt-object-magic",
        corrupt_object_magic_diagnostics));
    auto stale_object_format = first_round_trip.substr(0, 20U);
    store_u32(stale_object_format, 8U, 1U);
    fsim::diagnostic::Engine stale_object_format_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        stale_object_format, "stale-object-format",
        stale_object_format_diagnostics));
    assert(has_identity_diagnostic(
        stale_object_format_diagnostics, ".fsimscobj", "format 1",
        "format 2", ".fsimscobj"));
    auto future_object_format = first_round_trip.substr(0, 20U);
    store_u32(future_object_format, 8U, 3U);
    fsim::diagnostic::Engine future_object_format_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        future_object_format, "future-object-format",
        future_object_format_diagnostics));
    assert(has_identity_diagnostic(
        future_object_format_diagnostics, ".fsimscobj", "format 3",
        "format 2", ".fsimscobj"));
    auto incompatible_object_runtime = first_round_trip.substr(0, 20U);
    store_u32(incompatible_object_runtime, 12U, 2U);
    fsim::diagnostic::Engine incompatible_object_runtime_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        incompatible_object_runtime, "incompatible-object-runtime",
        incompatible_object_runtime_diagnostics));
    assert(has_identity_diagnostic(
        incompatible_object_runtime_diagnostics, ".fsimscobj",
        "format 2, runtime ABI 2", "format 2, runtime ABI 1",
        ".fsimscobj"));
    auto incompatible_object_systemc = first_round_trip.substr(0, 20U);
    store_u32(incompatible_object_systemc, 16U, 0U);
    fsim::diagnostic::Engine incompatible_object_systemc_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        incompatible_object_systemc, "incompatible-object-systemc",
        incompatible_object_systemc_diagnostics));
    assert(has_identity_diagnostic(
        incompatible_object_systemc_diagnostics, ".fsimscobj",
        "format 2, runtime ABI 1 and SystemC ABI 0",
        "format 2, runtime ABI 1 and SystemC ABI 4", ".fsimscobj"));
    auto stale_object_producer = *first_metadata;
    stale_object_producer.producer += "-stale";
    fsim::diagnostic::Engine stale_object_producer_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        fsim::systemc::serialize_incremental_object_metadata(
            stale_object_producer),
        "stale-object-producer", stale_object_producer_diagnostics));
    assert(has_identity_diagnostic(
        stale_object_producer_diagnostics, ".fsimscobj producer",
        stale_object_producer.producer, "fsim ", ".fsimscobj"));
    auto oversized_object_identity = first_round_trip;
    store_u64(
        oversized_object_identity, 20U,
        std::numeric_limits<std::uint64_t>::max());
    fsim::diagnostic::Engine oversized_object_identity_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        oversized_object_identity, "oversized-object-identity",
        oversized_object_identity_diagnostics));
    fsim::diagnostic::Engine truncated_object_header_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        first_round_trip.substr(0, 16), "truncated-object-header",
        truncated_object_header_diagnostics));
    auto trailing_object_metadata = first_round_trip;
    trailing_object_metadata.push_back('\0');
    fsim::diagnostic::Engine trailing_object_metadata_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_object_metadata(
        trailing_object_metadata, "trailing-object-metadata",
        trailing_object_metadata_diagnostics));

    const auto incompatible_scv_object = root / "incompatible-scv.fsimscobj";
    copy_tree(first_request.output, incompatible_scv_object);
    make_writable(incompatible_scv_object);
    auto incompatible_scv_metadata = *first_metadata;
    const auto scv_version = incompatible_scv_metadata.scv_compatibility.find("|scv=2.0.1|");
    assert(scv_version != std::string::npos);
    incompatible_scv_metadata.scv_compatibility.replace(
        scv_version, 11, "|scv=2.0.2|");
    incompatible_scv_metadata.input_digest = fsim::systemc::compute_incremental_object_input_digest(
        incompatible_scv_metadata);
    incompatible_scv_metadata.compilation_digest =
        fsim::systemc::compute_incremental_object_digest(
            incompatible_scv_metadata);
    write_file(
        incompatible_scv_object
            / fsim::systemc::kIncrementalObjectMetadataFilename,
        fsim::systemc::serialize_incremental_object_metadata(
            incompatible_scv_metadata));
    fsim::diagnostic::Engine incompatible_scv_diagnostics;
    assert(!fsim::systemc::load_incremental_object_metadata(
        incompatible_scv_object, incompatible_scv_diagnostics));
    assert(incompatible_scv_diagnostics.has_error());

    fsim::diagnostic::Engine overwrite_diagnostics;
    assert(!fsim::systemc::compile_incremental_object(
        first_request, overwrite_diagnostics));
    assert(overwrite_diagnostics.has_error());

    auto missing_request = first_request;
    missing_request.source = root / "missing.cpp";
    missing_request.output = root / "missing.fsimscobj";
    fsim::diagnostic::Engine missing_diagnostics;
    assert(!fsim::systemc::compile_incremental_object(
        missing_request, missing_diagnostics));
    assert(missing_diagnostics.has_error());
    assert(!std::filesystem::exists(missing_request.output));

    const auto corrupt_object = root / "corrupt.fsimscobj";
    copy_tree(first_request.output, corrupt_object);
    make_writable(corrupt_object);
    {
        std::ofstream corrupt(
            corrupt_object / first_metadata->object,
            std::ios::binary | std::ios::app);
        corrupt.put('x');
    }
    fsim::diagnostic::Engine corrupt_object_diagnostics;
    assert(!fsim::systemc::load_incremental_object_metadata(
        corrupt_object, corrupt_object_diagnostics));
    assert(corrupt_object_diagnostics.has_error());

    fsim::systemc::IncrementalCompileRequest second_request = first_request;
    second_request.source = second_source;
    second_request.output = root / "second.fsimscobj";
    assert(fsim::systemc::compile_incremental_object(
        second_request, diagnostics));
    auto cached_second_request = second_request;
    cached_second_request.output.clear();
    auto cached_second = fsim::systemc::compile_incremental_object_cached(
        cached_second_request, incremental_cache, diagnostics);
    assert(cached_second.success && !cached_second.cache_hit);

    const auto incompatible_object = root / "incompatible.fsimscobj";
    copy_tree(second_request.output, incompatible_object);
    make_writable(incompatible_object);
    fsim::diagnostic::Engine incompatible_metadata_diagnostics;
    auto incompatible_metadata = fsim::systemc::load_incremental_object_metadata(
        incompatible_object, incompatible_metadata_diagnostics);
    assert(incompatible_metadata && !incompatible_metadata_diagnostics.has_error());
    incompatible_metadata->compiler_fingerprint = std::string(64, 'a');
    incompatible_metadata->input_digest = fsim::systemc::compute_incremental_object_input_digest(
        *incompatible_metadata);
    incompatible_metadata->compilation_digest = fsim::systemc::compute_incremental_object_digest(*incompatible_metadata);
    write_file(
        incompatible_object
            / fsim::systemc::kIncrementalObjectMetadataFilename,
        fsim::systemc::serialize_incremental_object_metadata(
            *incompatible_metadata));

    fsim::systemc::IncrementalLinkRequest link_request;
    link_request.objects = { first_request.output, second_request.output };
    link_request.output = root / "work.fsimscplugin";
    link_request.logical_library = "work";
    link_request.settings = settings;
    link_request.working_directory = root;
    link_request.scratch_directory = root / "scratch";
    const auto linked = fsim::systemc::link_incremental_plugin(
        link_request, diagnostics);
    if (!linked) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(linked);
    assert(!diagnostics.has_error());

    auto plugin_metadata = fsim::systemc::load_incremental_plugin_metadata(
        link_request.output, diagnostics);
    assert(plugin_metadata.has_value());
    assert(plugin_metadata->scv_compatibility
        == fsim_scv_compatibility_identity());
    assert(plugin_metadata->object_digests.size() == 2);
    assert(plugin_metadata->factories.size() == 2);
    assert(plugin_metadata->factories[0].name == "first");
    assert(plugin_metadata->factories[0].parameters.size() == 1);
    assert(plugin_metadata->factories[1].name == "second");
    assert(plugin_metadata->link_digest
        == fsim::systemc::compute_incremental_plugin_digest(*plugin_metadata));
    const auto plugin_round_trip
        = fsim::systemc::serialize_incremental_plugin_metadata(*plugin_metadata);
    fsim::diagnostic::Engine plugin_round_trip_diagnostics;
    assert(fsim::systemc::deserialize_incremental_plugin_metadata(
               plugin_round_trip, "plugin-round-trip",
               plugin_round_trip_diagnostics)
        == plugin_metadata);
    assert(!plugin_round_trip_diagnostics.has_error());

    const auto assert_plugin_input_changes = [&](const auto& mutate) {
        auto changed = *plugin_metadata;
        mutate(changed);
        assert(fsim::systemc::compute_incremental_plugin_input_digest(changed)
            != plugin_metadata->input_digest);
    };
    assert_plugin_input_changes([](auto& changed) {
        changed.logical_library += "_changed";
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.toolchain += "-changed";
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.target += "-changed";
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.compiler_fingerprint = std::string(64, '3');
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.scv_compatibility += "-changed";
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.object_digests.front() = std::string(64, '4');
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.link_options.push_back("-ffsim-link-identity-change");
    });
    assert_plugin_input_changes([](auto& changed) {
        changed.libraries.push_back("fsim_identity_change");
    });
    auto changed_factory = *plugin_metadata;
    changed_factory.factories.front().parameters.front().default_value++;
    assert(fsim::systemc::compute_incremental_plugin_digest(changed_factory)
        != plugin_metadata->link_digest);
    auto changed_library_payload = *plugin_metadata;
    changed_library_payload.library_checksum = std::string(64, '5');
    assert(fsim::systemc::compute_incremental_plugin_digest(
               changed_library_payload)
        != plugin_metadata->link_digest);

    auto corrupt_plugin_magic = plugin_round_trip;
    corrupt_plugin_magic[0] = 'X';
    fsim::diagnostic::Engine corrupt_plugin_magic_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        corrupt_plugin_magic, "corrupt-plugin-magic",
        corrupt_plugin_magic_diagnostics));
    auto stale_plugin_format = plugin_round_trip.substr(0, 20U);
    store_u32(stale_plugin_format, 8U, 1U);
    fsim::diagnostic::Engine stale_plugin_format_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        stale_plugin_format, "stale-plugin-format",
        stale_plugin_format_diagnostics));
    assert(has_identity_diagnostic(
        stale_plugin_format_diagnostics, ".fsimscplugin", "format 1",
        "format 2", ".fsimscplugin"));
    auto future_plugin_format = plugin_round_trip.substr(0, 20U);
    store_u32(future_plugin_format, 8U, 3U);
    fsim::diagnostic::Engine future_plugin_format_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        future_plugin_format, "future-plugin-format",
        future_plugin_format_diagnostics));
    assert(has_identity_diagnostic(
        future_plugin_format_diagnostics, ".fsimscplugin", "format 3",
        "format 2", ".fsimscplugin"));
    auto incompatible_plugin_runtime = plugin_round_trip.substr(0, 20U);
    store_u32(incompatible_plugin_runtime, 12U, 2U);
    fsim::diagnostic::Engine incompatible_plugin_runtime_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        incompatible_plugin_runtime, "incompatible-plugin-runtime",
        incompatible_plugin_runtime_diagnostics));
    assert(has_identity_diagnostic(
        incompatible_plugin_runtime_diagnostics, ".fsimscplugin",
        "format 2, runtime ABI 2", "format 2, runtime ABI 1",
        ".fsimscplugin"));
    auto incompatible_plugin_systemc = plugin_round_trip.substr(0, 20U);
    store_u32(incompatible_plugin_systemc, 16U, 0U);
    fsim::diagnostic::Engine incompatible_plugin_systemc_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        incompatible_plugin_systemc, "incompatible-plugin-systemc",
        incompatible_plugin_systemc_diagnostics));
    assert(has_identity_diagnostic(
        incompatible_plugin_systemc_diagnostics, ".fsimscplugin",
        "format 2, runtime ABI 1 and SystemC ABI 0",
        "format 2, runtime ABI 1 and SystemC ABI 4", ".fsimscplugin"));
    auto stale_plugin_producer = *plugin_metadata;
    stale_plugin_producer.producer += "-stale";
    fsim::diagnostic::Engine stale_plugin_producer_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        fsim::systemc::serialize_incremental_plugin_metadata(
            stale_plugin_producer),
        "stale-plugin-producer", stale_plugin_producer_diagnostics));
    assert(has_identity_diagnostic(
        stale_plugin_producer_diagnostics, ".fsimscplugin producer",
        stale_plugin_producer.producer, "fsim ", ".fsimscplugin"));
    auto oversized_plugin_identity = plugin_round_trip;
    store_u64(
        oversized_plugin_identity, 20U,
        std::numeric_limits<std::uint64_t>::max());
    fsim::diagnostic::Engine oversized_plugin_identity_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        oversized_plugin_identity, "oversized-plugin-identity",
        oversized_plugin_identity_diagnostics));
    fsim::diagnostic::Engine truncated_plugin_header_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        plugin_round_trip.substr(0, 16), "truncated-plugin-header",
        truncated_plugin_header_diagnostics));
    auto trailing_plugin_metadata = plugin_round_trip;
    trailing_plugin_metadata.push_back('\0');
    fsim::diagnostic::Engine trailing_plugin_metadata_diagnostics;
    assert(!fsim::systemc::deserialize_incremental_plugin_metadata(
        trailing_plugin_metadata, "trailing-plugin-metadata",
        trailing_plugin_metadata_diagnostics));
    auto registry = fsim::systemc::load_incremental_plugin(
        link_request.output, diagnostics);
    assert(registry != nullptr);
    assert(registry->has_factory("first"));
    assert(registry->has_factory("second"));

    const auto stale_plugin = root / "stale.fsimscplugin";
    copy_tree(link_request.output, stale_plugin);
    make_writable(stale_plugin);
    fsim::diagnostic::Engine stale_metadata_diagnostics;
    auto stale_metadata = fsim::systemc::load_incremental_plugin_metadata(
        stale_plugin, stale_metadata_diagnostics);
    assert(stale_metadata && !stale_metadata_diagnostics.has_error());
    stale_metadata->compiler_fingerprint = std::string(64, 'b');
    stale_metadata->input_digest = fsim::systemc::compute_incremental_plugin_input_digest(*stale_metadata);
    stale_metadata->link_digest = fsim::systemc::compute_incremental_plugin_digest(*stale_metadata);
    write_file(
        stale_plugin / fsim::systemc::kIncrementalPluginMetadataFilename,
        fsim::systemc::serialize_incremental_plugin_metadata(*stale_metadata));
    fsim::diagnostic::Engine stale_plugin_diagnostics;
    assert(!fsim::systemc::load_incremental_plugin(
        stale_plugin, stale_plugin_diagnostics));
    assert(stale_plugin_diagnostics.has_error());

    const auto stale_target_plugin = root / "stale-target.fsimscplugin";
    copy_tree(link_request.output, stale_target_plugin);
    make_writable(stale_target_plugin);
    fsim::diagnostic::Engine stale_target_metadata_diagnostics;
    auto stale_target_metadata = fsim::systemc::load_incremental_plugin_metadata(
        stale_target_plugin, stale_target_metadata_diagnostics);
    assert(stale_target_metadata
        && !stale_target_metadata_diagnostics.has_error());
    stale_target_metadata->target += "-stale";
    const std::string invalid_native = "not-a-native-shared-library";
    write_file(
        stale_target_plugin / stale_target_metadata->library,
        invalid_native);
    stale_target_metadata->library_checksum = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(invalid_native));
    stale_target_metadata->input_digest = fsim::systemc::compute_incremental_plugin_input_digest(
        *stale_target_metadata);
    stale_target_metadata->link_digest = fsim::systemc::compute_incremental_plugin_digest(
        *stale_target_metadata);
    write_file(
        stale_target_plugin
            / fsim::systemc::kIncrementalPluginMetadataFilename,
        fsim::systemc::serialize_incremental_plugin_metadata(
            *stale_target_metadata));
    fsim::diagnostic::Engine stale_target_diagnostics;
    assert(!fsim::systemc::load_incremental_plugin(
        stale_target_plugin, stale_target_diagnostics,
        plugin_metadata->compiler_fingerprint));
    assert(has_identity_diagnostic(
        stale_target_diagnostics, "SystemC plug-in producer",
        "producer " + stale_target_metadata->producer + " and target "
            + stale_target_metadata->target,
        "producer fsim ", ".fsimscplugin"));

    fsim::systemc::IncrementalLinkRequest incompatible_link = link_request;
    incompatible_link.objects = { first_request.output, incompatible_object };
    incompatible_link.output = root / "incompatible.fsimscplugin";
    fsim::diagnostic::Engine incompatible_link_diagnostics;
    assert(!fsim::systemc::link_incremental_plugin(
        incompatible_link, incompatible_link_diagnostics));
    assert(incompatible_link_diagnostics.has_error());
    assert(!std::filesystem::exists(incompatible_link.output));

    const auto corrupt_plugin = root / "corrupt.fsimscplugin";
    copy_tree(link_request.output, corrupt_plugin);
    make_writable(corrupt_plugin);
    {
        std::ofstream corrupt(
            corrupt_plugin / plugin_metadata->library,
            std::ios::binary | std::ios::app);
        corrupt.put('x');
    }
    fsim::diagnostic::Engine corrupt_plugin_diagnostics;
    assert(!fsim::systemc::load_incremental_plugin_metadata(
        corrupt_plugin, corrupt_plugin_diagnostics));
    assert(corrupt_plugin_diagnostics.has_error());

    auto cached_link_request = link_request;
    cached_link_request.objects = {
        cached_first.artifact, cached_second.artifact
    };
    cached_link_request.output.clear();
    auto cached_link = fsim::systemc::link_incremental_plugin_cached(
        cached_link_request, incremental_cache, diagnostics);
    if (!cached_link.success) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(cached_link.success && !cached_link.cache_hit);
    auto cached_link_warm = fsim::systemc::link_incremental_plugin_cached(
        cached_link_request, incremental_cache, diagnostics);
    assert(cached_link_warm.success && cached_link_warm.cache_hit);
    assert(cached_link_warm.artifact == cached_link.artifact);

    write_file(first_source, module_source("FirstModule", "first_changed", true));
    auto cached_first_changed = fsim::systemc::compile_incremental_object_cached(
        cached_first_request, incremental_cache, diagnostics);
    assert(cached_first_changed.success && !cached_first_changed.cache_hit);
    assert(cached_first_changed.artifact != cached_first.artifact);
    auto cached_second_unchanged = fsim::systemc::compile_incremental_object_cached(
        cached_second_request, incremental_cache, diagnostics);
    assert(cached_second_unchanged.success && cached_second_unchanged.cache_hit);
    assert(cached_second_unchanged.artifact == cached_second.artifact);

    fsim::diagnostic::Engine duplicate_diagnostics;
    auto duplicate_request = link_request;
    duplicate_request.output = root / "duplicate.fsimscplugin";
    duplicate_request.objects.push_back(first_request.output);
    assert(!fsim::systemc::link_incremental_plugin(
        duplicate_request, duplicate_diagnostics));
    assert(duplicate_diagnostics.has_error());
    assert(!std::filesystem::exists(duplicate_request.output));

    const auto duplicate_source = root / "duplicate.cpp";
    write_file(
        duplicate_source,
        module_source("DuplicateModule", "first", false));
    auto duplicate_compile = first_request;
    duplicate_compile.source = duplicate_source;
    duplicate_compile.output = root / "duplicate.fsimscobj";
    fsim::diagnostic::Engine duplicate_factory_diagnostics;
    assert(fsim::systemc::compile_incremental_object(
        duplicate_compile, duplicate_factory_diagnostics));
    auto duplicate_factory_link = link_request;
    duplicate_factory_link.objects = {
        first_request.output, duplicate_compile.output
    };
    duplicate_factory_link.output = root / "duplicate-factory.fsimscplugin";
    assert(!fsim::systemc::link_incremental_plugin(
        duplicate_factory_link, duplicate_factory_diagnostics));
    assert(duplicate_factory_diagnostics.has_error());
    assert(!std::filesystem::exists(duplicate_factory_link.output));

    const auto unexported_source = root / "unexported.cpp";
    write_file(
        unexported_source,
        "#include \"fsim/systemc.hpp\"\n"
        "SC_MODULE(Unexported) { SC_CTOR(Unexported) {} };\n");
    auto unexported_compile = first_request;
    unexported_compile.source = unexported_source;
    unexported_compile.output = root / "unexported.fsimscobj";
    fsim::diagnostic::Engine unexported_diagnostics;
    assert(fsim::systemc::compile_incremental_object(
        unexported_compile, unexported_diagnostics));
    auto unexported_link = link_request;
    unexported_link.objects = { unexported_compile.output };
    unexported_link.output = root / "unexported.fsimscplugin";
    assert(!fsim::systemc::link_incremental_plugin(
        unexported_link, unexported_diagnostics));
    assert(unexported_diagnostics.has_error());
    assert(!std::filesystem::exists(unexported_link.output));

    fsim::systemc::IncrementalCompileRequest handwritten_request = first_request;
    handwritten_request.source = handwritten_source;
    handwritten_request.output = root / "handwritten.fsimscobj";
    fsim::diagnostic::Engine handwritten_diagnostics;
    assert(fsim::systemc::compile_incremental_object(
        handwritten_request, handwritten_diagnostics));
    auto handwritten_metadata = fsim::systemc::load_incremental_object_metadata(
        handwritten_request.output, handwritten_diagnostics);
    assert(handwritten_metadata
        && handwritten_metadata->defines_plugin_entry_point);
    assert(!handwritten_metadata->contains_macro_export);
    fsim::systemc::IncrementalLinkRequest handwritten_link = link_request;
    handwritten_link.objects = { handwritten_request.output };
    handwritten_link.output = root / "handwritten.fsimscplugin";
    assert(fsim::systemc::link_incremental_plugin(
        handwritten_link, handwritten_diagnostics));
    auto handwritten_registry = fsim::systemc::load_incremental_plugin(
        handwritten_link.output, handwritten_diagnostics);
    assert(handwritten_registry
        && handwritten_registry->has_factory("handwritten"));
    assert(!handwritten_diagnostics.has_error());

    registry.reset();
    handwritten_registry.reset();
    make_writable(root);
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
#if !defined(_WIN32)
    assert(!cleanup_error);
#endif
}
