// SPDX-License-Identifier: Apache-2.0
#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/plugin_compiler.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

class TestExecutionContext final
    : public fsim::runtime::simir::ProcessExecutionContext {
public:
    [[nodiscard]] fsim::runtime::PackedLogic4 read_signal(
        fsim::runtime::simir::SignalId) const override {
        return fsim::runtime::PackedLogic4{};
    }

    void write_blocking(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override {}
    void write_blocking_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override {}
    void write_update(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4) override {}
    void write_update_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t) override {}
    void write_after(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        fsim::runtime::SimulationTick) override {}
    void write_after_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        fsim::runtime::SimulationTick) override {}
    void write_inertial(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        const fsim::runtime::simir::TransitionDelays&) override {}
    void write_inertial_slice(
        fsim::runtime::simir::SignalId,
        fsim::runtime::PackedLogic4,
        std::size_t,
        const fsim::runtime::simir::TransitionDelays&) override {}
};

void append_text(
    const std::filesystem::path& path,
    const std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    stream << text;
    assert(stream);
}

void replace_text(
    const std::filesystem::path& path,
    const std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    assert(stream);
}

[[nodiscard]] bool has_diagnostic(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code) {
    return std::any_of(
        diagnostics.diagnostics().begin(),
        diagnostics.diagnostics().end(),
        [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

} // namespace

int main() {
    namespace filesystem = std::filesystem;
    const auto serial = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = filesystem::temp_directory_path()
        / ("fsim-systemc-matrix-" + serial);
    const auto working = root / "working directory";
    const auto source = working / "matrix plugin.cpp";
    std::error_code error;
    filesystem::create_directories(working, error);
    assert(!error);
    filesystem::copy_file(
        filesystem::path{FSIM_TEST_SOURCE_DIR}
            / "tests/systemc/macro_plugin.cpp",
        source,
        filesystem::copy_options::overwrite_existing,
        error);
    assert(!error);

    fsim::systemc::PluginCompileRequest request;
    request.sources = {source};
    request.working_directory = working;
    request.cache_directory = root / "cache directory";
    request.settings.include_directories = {
        filesystem::path{FSIM_TEST_SOURCE_DIR} / "include"};
#if defined(_WIN32)
    request.settings.compiler = "cl.exe";
#elif defined(FSIM_TEST_CXX_COMPILER)
    request.settings.compiler = FSIM_TEST_CXX_COMPILER;
#else
    request.settings.compiler = "c++";
#endif
    fsim::diagnostic::Engine diagnostics;
    const auto cold = fsim::systemc::compile_plugin(request, diagnostics);
    if (!cold.success) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(cold.success && !cold.cache_hit);
    const auto warm = fsim::systemc::compile_plugin(request, diagnostics);
    assert(warm.success && warm.cache_hit);
    assert(cold.cache_key == warm.cache_key);
    assert(!diagnostics.has_error());

    std::string load_error;
    auto old_registry = fsim::systemc::HierarchyRegistry::load(
        cold.library_path, load_error);
    assert(old_registry && load_error.empty());
    assert(old_registry->has_factory("a_parameterized"));
    assert(old_registry->has_factory("m_alias_one"));
    const auto bridge = old_registry->instantiate(
        "a_parameterized", "matrix.bridge", 0, load_error);
    assert(bridge && load_error.empty());
    const std::array roots{bridge->handle};
    old_registry->complete_elaboration(roots);
    old_registry->start_simulation(roots);

    append_text(source, "\n// matrix edit\n");
    diagnostics.clear();
    const auto edited = fsim::systemc::compile_plugin(request, diagnostics);
    assert(edited.success && !edited.cache_hit);
    assert(edited.cache_key != cold.cache_key);
    assert(fsim::platform::DynamicLibrary::is_loaded(cold.library_path));
    assert(!fsim::platform::DynamicLibrary::is_loaded(edited.library_path));

    replace_text(edited.library_path, "corrupt matrix image\n");
    const auto repaired = fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired.success && !repaired.cache_hit);
    assert(repaired.cache_key == edited.cache_key);

    const auto metadata = filesystem::path{
        repaired.library_path.string() + ".metadata"};
    assert(filesystem::remove(repaired.library_path, error));
    assert(!error);
    assert(filesystem::remove(metadata, error));
    assert(!error);
    std::array<std::future<fsim::systemc::PluginCompileResult>, 3> builds;
    for (auto& build : builds) {
        build = std::async(std::launch::async, [&request]() {
            fsim::diagnostic::Engine concurrent_diagnostics;
            const auto result = fsim::systemc::compile_plugin(
                request, concurrent_diagnostics);
            assert(!concurrent_diagnostics.has_error());
            return result;
        });
    }
    std::size_t hits = 0;
    filesystem::path current_library;
    for (auto& build : builds) {
        const auto result = build.get();
        assert(result.success && result.cache_key == repaired.cache_key);
        hits += result.cache_hit ? 1U : 0U;
        current_library = result.library_path;
    }
    assert(hits == builds.size() - 1);

    auto current_registry = fsim::systemc::HierarchyRegistry::load(
        current_library, load_error);
    assert(current_registry && load_error.empty());
    const auto current_bridge = current_registry->instantiate(
        "a_parameterized", "matrix.current", 0, load_error);
    assert(current_bridge && load_error.empty());
    const std::array current_roots{current_bridge->handle};
    current_registry->complete_elaboration(current_roots);
    current_registry->start_simulation(current_roots);
    current_registry->end_simulation(current_roots);

    old_registry.reset();
    // Official SystemC/TLM keeps process-global callback and type_info
    // registries. Content-addressed plug-in images therefore remain mapped
    // after logical teardown so those entries never dangle.
    assert(fsim::platform::DynamicLibrary::is_loaded(cold.library_path));
    assert(fsim::platform::DynamicLibrary::is_loaded(current_library));
    current_registry.reset();
    assert(fsim::platform::DynamicLibrary::is_loaded(current_library));

    const auto invalid_source = working / "invalid plugin.cpp";
    replace_text(
        invalid_source,
        "#error FSIM_SYSTEMC_MATRIX_COMPILE_FAILURE\n");
    auto invalid_request = request;
    invalid_request.sources = {invalid_source};
    fsim::diagnostic::Engine invalid_diagnostics;
    const auto invalid = fsim::systemc::compile_plugin(
        invalid_request, invalid_diagnostics);
    assert(!invalid.success && invalid.compiler_exit_code != 0);
    assert(has_diagnostic(invalid_diagnostics, "FSIM-SC-C007"));
    assert(
        invalid.compiler_output.find(
            "FSIM_SYSTEMC_MATRIX_COMPILE_FAILURE")
        != std::string::npos);

    filesystem::remove_all(root, error);
    std::cout << "SystemC compiler/cache/lifecycle matrix passed\n";
}
