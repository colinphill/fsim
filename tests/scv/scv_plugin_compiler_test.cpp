// SPDX-License-Identifier: Apache-2.0

#include "fsim/platform/dynamic_library.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/incremental.hpp"
#include "fsim/systemc/plugin_compiler.hpp"
#include "fsim/systemc/scv.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

void write_file(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output { path, std::ios::binary | std::ios::trunc };
    output << contents;
    assert(output.good());
}

std::string plugin_source()
{
    return "#include \"fsim/systemc.hpp\"\n"
           "#include \"fsim/systemc/scv.hpp\"\n"
           "#include \"scv_dependency.hpp\"\n"
           "#include <scv.h>\n"
           "extern \"C\" FSIM_SC_EXPORT const char* "
           "fsim_test_scv_identity() {\n"
           "  scv_bag<int> values{\"plugin-values\", SCV_TEST_REVISION};\n"
           "  values.add(42);\n"
           "  const auto& fixed = values;\n"
           "  return fixed.peekRandom() == 42\n"
           "      ? fsim_scv_compatibility_identity() : nullptr;\n"
           "}\n"
           "SC_MODULE(ScvProducer) { SC_CTOR(ScvProducer) {} };\n"
           "SC_FSIM_EXPORT_AS(ScvProducer, \"scv_producer\");\n";
}

std::size_t count_argument(
    const fsim::systemc::PluginCompilePlan& plan,
    const std::string_view expected)
{
    std::size_t count = 0;
    for (const auto& command : plan.commands) {
        for (const auto& argument : command.argv) {
            count += argument == expected ? 1U : 0U;
        }
    }
    return count;
}

void check_shared_identity(const std::filesystem::path& path)
{
    std::string error;
    auto library = fsim::platform::DynamicLibrary::open(path, error);
    assert(library && error.empty());
    auto* raw = library->symbol("fsim_test_scv_identity", error);
    assert(raw != nullptr && error.empty());
    using identity_function = const char* (*)();
    const auto identity = reinterpret_cast<identity_function>(raw);
    assert(identity() == fsim_scv_compatibility_identity());
}

#if !defined(_WIN32)
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
#endif

} // namespace

int main()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-scv-plugin-compiler-" + unique);
    const auto source = root / "scv_plugin.cpp";
    const auto dependency = root / "scv_dependency.hpp";
    write_file(source, plugin_source());
    write_file(dependency, "#define SCV_TEST_REVISION 1\n");

    fsim::systemc::PluginCompileRequest source_request;
    source_request.sources = { source };
    source_request.logical_library = "scv_source";
    source_request.working_directory = root;
    source_request.cache_directory = root / "source-cache";
    source_request.settings.include_directories = { root };
#if defined(FSIM_TEST_CXX_COMPILER)
    source_request.settings.compiler = FSIM_TEST_CXX_COMPILER;
#endif

    fsim::diagnostic::Engine diagnostics;
    const auto plan = fsim::systemc::plan_plugin_compile(
        source_request, diagnostics);
    assert(plan && plan->cacheable && !diagnostics.has_error());
    assert(count_argument(*plan, FSIM_TEST_SCV_LIBRARY_PATH) == 1);

    const auto source_cold = fsim::systemc::compile_plugin(
        source_request, diagnostics);
    const auto source_warm = fsim::systemc::compile_plugin(
        source_request, diagnostics);
    assert(source_cold.success && !source_cold.cache_hit);
    assert(source_warm.success && source_warm.cache_hit);
    assert(source_warm.cache_key == source_cold.cache_key);
    assert(!diagnostics.has_error());
    std::string load_error;
    auto source_registry = fsim::systemc::HierarchyRegistry::load(
        source_cold.library_path, load_error);
    assert(source_registry && load_error.empty());
    assert(source_registry->has_factory("scv_producer"));
    check_shared_identity(source_cold.library_path);

    write_file(dependency, "#define SCV_TEST_REVISION 2\n");
    const auto source_edited = fsim::systemc::compile_plugin(
        source_request, diagnostics);
    const auto source_edited_warm = fsim::systemc::compile_plugin(
        source_request, diagnostics);
    assert(source_edited.success && !source_edited.cache_hit);
    assert(source_edited.cache_key != source_cold.cache_key);
    assert(source_edited_warm.success && source_edited_warm.cache_hit);
    assert(source_edited_warm.cache_key == source_edited.cache_key);
    check_shared_identity(source_edited.library_path);

    fsim::systemc::IncrementalCompileRequest object_request;
    object_request.source = source;
    object_request.working_directory = root;
    object_request.scratch_directory = root / "incremental-scratch";
    object_request.settings = source_request.settings;
    const auto incremental_cache = root / "incremental-cache";
    const auto object_cold = fsim::systemc::compile_incremental_object_cached(
        object_request, incremental_cache, diagnostics);
    const auto object_warm = fsim::systemc::compile_incremental_object_cached(
        object_request, incremental_cache, diagnostics);
    assert(object_cold.success && !object_cold.cache_hit);
    assert(object_warm.success && object_warm.cache_hit);
    assert(object_warm.artifact == object_cold.artifact);

    fsim::systemc::IncrementalLinkRequest link_request;
    link_request.objects = { object_cold.artifact };
    link_request.logical_library = "scv_incremental";
    link_request.working_directory = root;
    link_request.scratch_directory = root / "incremental-scratch";
    link_request.settings = source_request.settings;
    const auto link_cold = fsim::systemc::link_incremental_plugin_cached(
        link_request, incremental_cache, diagnostics);
    const auto link_warm = fsim::systemc::link_incremental_plugin_cached(
        link_request, incremental_cache, diagnostics);
    assert(link_cold.success && !link_cold.cache_hit);
    assert(link_warm.success && link_warm.cache_hit);
    assert(link_warm.artifact == link_cold.artifact);
    assert(!diagnostics.has_error());

    const auto metadata = fsim::systemc::load_incremental_plugin_metadata(
        link_cold.artifact, diagnostics);
    assert(metadata);
    assert(metadata->scv_compatibility == fsim_scv_compatibility_identity());
    auto incremental_registry = fsim::systemc::load_incremental_plugin(
        link_cold.artifact, diagnostics);
    assert(incremental_registry);
    assert(incremental_registry->has_factory("scv_producer"));
    check_shared_identity(link_cold.artifact / metadata->library);

#if !defined(_WIN32)
    incremental_registry.reset();
    source_registry.reset();
    make_writable(root);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    assert(!error);
#endif
    return 0;
}
