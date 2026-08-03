// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/plugin_compiler.hpp"
#include "fsim/support/environment.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

// FSIM-CONFORMANCE CF-SC-COMPILER-001 source=SRC-SYSTEMC expectation=execute
// FSIM-CONFORMANCE CF-SC-CACHE-001 source=SRC-SYSTEMC expectation=execute
void write_source(
    const std::filesystem::path& path,
    const std::string& symbol,
    const std::string& value,
    const std::string& include = {},
    const bool macro_include = false,
    const std::string& preamble = {}) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << preamble;
    if (!include.empty()) {
        stream << "#include ";
        if (macro_include || include.front() == '<' || include.front() == '"') {
            stream << include << '\n';
        } else {
            stream << '"' << include << "\"\n";
        }
    }
    stream << "extern \"C\" ";
#if defined(_WIN32)
    stream << "__declspec(dllexport) ";
#else
    stream << "__attribute__((visibility(\"default\"))) ";
#endif
    stream << "int " << symbol << "() { return " << value << "; }\n";
    assert(stream);
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    assert(stream);
}

[[nodiscard]] bool has_argument(
    const fsim::systemc::PluginCompilePlan& plan,
    const std::string& argument) {
    return std::any_of(
        plan.commands.begin(), plan.commands.end(), [&](const auto& command) {
            return std::find(command.argv.begin(), command.argv.end(), argument)
                != command.argv.end();
        });
}

[[nodiscard]] bool command_has_argument(
    const fsim::systemc::CompilerCommand& command,
    const std::string_view argument) {
    return std::find(command.argv.begin(), command.argv.end(), argument)
        != command.argv.end();
}

[[nodiscard]] std::string_view expected_msvc_runtime_option() noexcept {
#if defined(_MSC_VER)
#  if defined(_DLL)
#    if defined(_DEBUG)
    return "/MDd";
#    else
    return "/MD";
#    endif
#  else
#    if defined(_DEBUG)
    return "/MTd";
#    else
    return "/MT";
#    endif
#  endif
#else
    return "/MD";
#endif
}

[[nodiscard]] bool is_msvc_runtime_option(
    const std::string_view argument) noexcept {
    return argument == "/MD" || argument == "/MDd"
        || argument == "/MT" || argument == "/MTd";
}

[[nodiscard]] bool has_diagnostic_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code) {
    return std::any_of(
        diagnostics.diagnostics().begin(),
        diagnostics.diagnostics().end(),
        [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

[[nodiscard]] bool has_raw_argument(
    const int argc,
    char* const* argv,
    const std::string_view expected) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == expected) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::filesystem::path> argument_after(
    const int argc,
    char* const* argv,
    const std::string_view option) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view{argv[index]} == option) {
            return std::filesystem::path{argv[index + 1]};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> source_argument(
    const int argc,
    char* const* argv) {
    for (int index = argc - 1; index > 0; --index) {
        const auto path = std::filesystem::path{argv[index]};
        const auto extension = path.extension().string();
        if (extension == ".cpp" || extension == ".cc"
            || extension == ".cxx") {
            return path;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string make_dependency_escape(
    const std::string_view path) {
    std::string result;
    for (const char character : path) {
        if (character == '$') {
            result += "$$";
        } else if (
            character == ' ' || character == '\t' || character == '#'
            || character == ':' || character == '\\') {
            result.push_back('\\');
            result.push_back(character);
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::string make_json_escape(const std::string_view value) {
    std::string result;
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            assert(character >= 0x20U);
            result.push_back(static_cast<char>(character));
            break;
        }
    }
    return result;
}

int run_msvc_dependency_fake_compiler(
    const int argc,
    char* const* argv) {
    const auto source = source_argument(argc, argv);
    const auto dependency_file =
        argument_after(argc, argv, "/sourceDependencies");
    if (!source || !dependency_file) {
        return 81;
    }
    if (has_raw_argument(argc, argv, "/DFSIM_TEST_MSVC_MALFORMED=1")) {
        write_text(*dependency_file, "{ malformed dependency json\n");
        return 0;
    }

    const auto header = source->parent_path() / "msvc generated header.hpp";
    const auto module = source->parent_path() / "msvc module.ifc";
    std::ofstream output(
        *dependency_file,
        std::ios::binary | std::ios::trunc);
    output
        << "\xef\xbb\xbf"
        << "{\n  \"Version\": \"1.2\",\n  \"Data\": {\n"
        << "    \"Source\": \""
        << make_json_escape(source->generic_string()) << "\",\n"
        << "    \"Includes\": [\""
        << make_json_escape(header.generic_string()) << "\"],\n"
        << "    \"ImportedModules\": [{\"Name\": \"fixture\", \"BMI\": \""
        << make_json_escape(module.generic_string()) << "\"}],\n"
        << "    \"ImportedHeaderUnits\": [{\"Header\": \""
        << make_json_escape(header.generic_string()) << "\", \"BMI\": \""
        << make_json_escape(module.generic_string()) << "\"}]\n"
        << "  }\n}\n";
    return output ? 0 : 82;
}

int run_mutating_fake_compiler(
    const int argc,
    char* const* argv) {
    const auto source = source_argument(argc, argv);
    if (!source) {
        return 91;
    }
    if (has_raw_argument(argc, argv, "-M")) {
        const auto dependency_file =
            argument_after(argc, argv, "-MF");
        if (!dependency_file) {
            return 92;
        }
        {
            std::ofstream output(
                *dependency_file,
                std::ios::binary | std::ios::trunc);
            output << "fsim_fake_target: "
                   << make_dependency_escape(source->generic_string())
                   << '\n';
            if (!output) {
                return 93;
            }
        }

        auto marker = *source;
        marker += ".fsim-mutated";
        std::error_code error;
        if (!std::filesystem::exists(marker, error)) {
            std::ifstream input(*source, std::ios::binary);
            const std::string contents{
                std::istreambuf_iterator<char>{input},
                std::istreambuf_iterator<char>{}};
            const auto position = contents.find("return 1");
            if (!input || position == std::string::npos) {
                return 94;
            }
            auto changed = contents;
            changed.replace(position, std::string_view{"return 1"}.size(), "return 2");
            write_text(*source, changed);
            write_text(marker, "mutated\n");
        }
        return 0;
    }

    const auto output_file = argument_after(argc, argv, "-o");
    if (!output_file) {
        return 95;
    }
    std::ifstream input(*source, std::ios::binary);
    const std::string contents{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    std::ofstream output(
        *output_file,
        std::ios::binary | std::ios::trunc);
    output << "FSIM-FAKE-PLUGIN\n" << contents;
    return input && output ? 0 : 96;
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(std::string name, const std::string& value)
        : name_(std::move(name)) {
        if (auto previous =
                fsim::support::environment_variable(name_)) {
            previous_ = std::move(*previous);
        }
#if defined(_WIN32)
        assert(::_putenv_s(name_.c_str(), value.c_str()) == 0);
#else
        assert(::setenv(name_.c_str(), value.c_str(), 1) == 0);
#endif
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment() {
        if (previous_) {
#if defined(_WIN32)
            (void)::_putenv_s(name_.c_str(), previous_->c_str());
#else
            (void)::setenv(name_.c_str(), previous_->c_str(), 1);
#endif
        } else {
#if defined(_WIN32)
            (void)::_putenv_s(name_.c_str(), "");
#else
            (void)::unsetenv(name_.c_str());
#endif
        }
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

} // namespace

int main(const int argc, char** argv) {
    if (has_raw_argument(
            argc, argv, "/DFSIM_TEST_MSVC_DEPENDENCY_COMPILER=1")) {
        return run_msvc_dependency_fake_compiler(argc, argv);
    }
    if (has_raw_argument(
            argc, argv, "-DFSIM_TEST_MUTATING_COMPILER=1")) {
        return run_mutating_fake_compiler(argc, argv);
    }

    namespace filesystem = std::filesystem;
    using fsim::systemc::PluginCompileRequest;

    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root =
        filesystem::temp_directory_path() / ("fsim-plugin-compiler-test-" + unique);
    const auto working = root / "working directory";
    const auto include = root / "include directory";
    const auto source = working / "source with spaces.cpp";
    const auto second_source = working / "second.cpp";
    const auto dependency = include / "plugin_value.hpp";
    const auto transitive_dependency = include / "nested" / "plugin_number.hpp";
    std::error_code error;
    filesystem::create_directories(working, error);
    assert(!error);
    filesystem::create_directories(include, error);
    assert(!error);
    filesystem::create_directories(transitive_dependency.parent_path(), error);
    assert(!error);
    const auto fsim_include = filesystem::weakly_canonical(
        filesystem::path{__FILE__}
            .parent_path()
            .parent_path()
            .parent_path()
            / "include",
        error);
    assert(!error);
    assert(filesystem::is_regular_file(fsim_include / "systemc"));
    write_text(
        dependency,
        "#include \"nested/plugin_number.hpp\"\n"
        "#define FSIM_PLUGIN_VALUE FSIM_PLUGIN_NUMBER\n");
    write_text(transitive_dependency, "#define FSIM_PLUGIN_NUMBER 7\n");
    write_source(
        source,
        "fsim_compiler_smoke",
        "FSIM_PLUGIN_VALUE",
        "FSIM_DEP_HEADER",
        true,
        "#define FSIM_DEP_HEADER \"plugin_value.hpp\"\n");
    write_source(second_source, "fsim_compiler_second", "9");

    PluginCompileRequest request;
    request.sources = {source, second_source};
    request.working_directory = working;
    request.cache_directory = root / "cache directory";
#if defined(_WIN32)
    request.settings.compiler = "cl.exe";
#else
    if (const auto compiler =
            fsim::support::environment_variable("CXX");
        compiler && !compiler->empty()) {
        request.settings.compiler = *compiler;
    } else {
        request.settings.compiler = "c++";
    }
#endif
    request.settings.include_directories = {include};
    request.settings.defines = {"FSIM_PLUGIN_TEST=1"};

    fsim::diagnostic::Engine diagnostics;
    const auto first_plan = fsim::systemc::plan_plugin_compile(request, diagnostics);
    assert(first_plan);
    assert(!diagnostics.has_error());
    assert(first_plan->cacheable);
    assert(first_plan->cache_key.size() == 64);
    assert(!first_plan->commands.empty());
    assert(first_plan->commands.front().working_directory == working);
#if defined(_WIN32)
    assert(has_argument(*first_plan, "/DFSIM_PLUGIN_TEST=1"));
    assert(has_argument(*first_plan, "/I" + include.string()));
    assert(has_argument(*first_plan, "/utf-8"));
    assert(has_argument(*first_plan, "/Zc:__cplusplus"));
    assert(has_argument(*first_plan, "/FC"));
    assert(first_plan->commands.size() == request.sources.size() + 1);
#else
    assert(has_argument(*first_plan, "-DFSIM_PLUGIN_TEST=1"));
    assert(has_argument(*first_plan, "-I" + include.string()));
    assert(has_argument(*first_plan, source.string()));
    assert(first_plan->commands.size() == 1);
#endif
    const auto display =
        fsim::systemc::format_compiler_command(first_plan->commands.front());
    assert(display.find("source with spaces.cpp") != std::string::npos);
    assert(has_argument(*first_plan, second_source.string()));

    auto reversed_sources = request;
    std::reverse(
        reversed_sources.sources.begin(),
        reversed_sources.sources.end());
    fsim::diagnostic::Engine reversed_diagnostics;
    const auto reversed_plan = fsim::systemc::plan_plugin_compile(
        reversed_sources, reversed_diagnostics);
    assert(reversed_plan && reversed_plan->cacheable);
    assert(reversed_plan->cache_key != first_plan->cache_key);

    auto changed_define = request;
    changed_define.settings.defines = {"FSIM_PLUGIN_TEST=2"};
    fsim::diagnostic::Engine define_diagnostics;
    const auto define_plan = fsim::systemc::plan_plugin_compile(
        changed_define, define_diagnostics);
    assert(define_plan && define_plan->cacheable);
    assert(define_plan->cache_key != first_plan->cache_key);

    const auto environment_name =
#if defined(_WIN32)
        std::string{"LIB"};
#else
        std::string{"LIBRARY_PATH"};
#endif
    {
        const ScopedEnvironment changed_environment{
            environment_name,
            (root / "alternate toolchain environment").string()};
        fsim::diagnostic::Engine environment_diagnostics;
        const auto environment_plan =
            fsim::systemc::plan_plugin_compile(
                request, environment_diagnostics);
        assert(environment_plan && environment_plan->cacheable);
        assert(environment_plan->cache_key != first_plan->cache_key);
    }
    fsim::diagnostic::Engine restored_environment_diagnostics;
    const auto restored_environment_plan =
        fsim::systemc::plan_plugin_compile(
            request, restored_environment_diagnostics);
    assert(restored_environment_plan);
    assert(restored_environment_plan->cache_key == first_plan->cache_key);

    auto literal_arguments = request;
    literal_arguments.settings.defines = {"FSIM_LITERAL_ARGUMENT=hello world;$HOME*"};
    fsim::diagnostic::Engine literal_diagnostics;
    const auto literal_plan =
        fsim::systemc::plan_plugin_compile(literal_arguments, literal_diagnostics);
    assert(literal_plan);
#if defined(_WIN32)
    assert(has_argument(
        *literal_plan, "/DFSIM_LITERAL_ARGUMENT=hello world;$HOME*"));
#else
    assert(has_argument(
        *literal_plan, "-DFSIM_LITERAL_ARGUMENT=hello world;$HOME*"));
#endif

    // Exercise recovery of a stale per-key lock before the first compile.
    const auto stale_lock =
        request.cache_directory / "systemc" / "locks"
        / (first_plan->cache_key + ".lock");
    filesystem::create_directories(stale_lock, error);
    assert(!error);
    filesystem::last_write_time(
        stale_lock,
        filesystem::file_time_type::clock::now() - std::chrono::hours{1},
        error);
    assert(!error);

    const auto first = fsim::systemc::compile_plugin(request, diagnostics);
    assert(first.success);
    assert(!first.cache_hit);
    assert(first.compiler_exit_code == 0);
    assert(filesystem::is_regular_file(first.library_path));
    assert(!diagnostics.has_error());

    const auto second = fsim::systemc::compile_plugin(request, diagnostics);
    assert(second.success);
    assert(second.cache_hit);
    assert(second.library_path == first.library_path);
    assert(second.cache_key == first.cache_key);

#if defined(_WIN32)
    // Force both dependency discovery and the real cl/clang-cl build through
    // a UTF-16 response file in a working directory containing spaces. The
    // compiler must receive every literal argument and no temporary response
    // file may remain after either the cold or warm invocation.
    auto long_response_request = request;
    for (std::size_t index = 0; index < 512; ++index) {
        long_response_request.settings.defines.push_back(
            "FSIM_LONG_RESPONSE_" + std::string(64, 'A')
            + std::to_string(index) + "=1");
    }
    fsim::diagnostic::Engine long_response_diagnostics;
    const auto long_response_cold = fsim::systemc::compile_plugin(
        long_response_request, long_response_diagnostics);
    const auto long_response_warm = fsim::systemc::compile_plugin(
        long_response_request, long_response_diagnostics);
    assert(long_response_cold.success && !long_response_cold.cache_hit);
    assert(long_response_warm.success && long_response_warm.cache_hit);
    assert(!long_response_diagnostics.has_error());
    for (const auto& entry : filesystem::directory_iterator{working}) {
        assert(!entry.path().filename().string().starts_with(
            ".fsim-compiler-arguments-"));
    }
#endif

    const auto artifact_metadata =
        filesystem::path{first.library_path.string() + ".metadata"};
    assert(filesystem::is_regular_file(artifact_metadata));

    // Missing, truncated, corrupt, and incompatible committed pairs are
    // deterministic misses. The per-key writer repairs each one and the next
    // lookup must be a checksum-validated hit.
    write_text(first.library_path, "truncated shared library\n");
    const auto repaired_library =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired_library.success && !repaired_library.cache_hit);
    const auto repaired_library_warm =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired_library_warm.success && repaired_library_warm.cache_hit);

    assert(filesystem::remove(artifact_metadata, error));
    assert(!error);
    const auto repaired_missing_metadata =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired_missing_metadata.success);
    assert(!repaired_missing_metadata.cache_hit);

    write_text(artifact_metadata, "fsim-systemc-artifact-v1\ntruncated\n");
    const auto repaired_truncated_metadata =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired_truncated_metadata.success);
    assert(!repaired_truncated_metadata.cache_hit);

    write_text(
        artifact_metadata,
        "fsim-systemc-artifact-v999\n"
        + first.cache_key + "\n1\n"
        + std::string(64, '0') + "\n");
    const auto repaired_incompatible_metadata =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired_incompatible_metadata.success);
    assert(!repaired_incompatible_metadata.cache_hit);

    assert(filesystem::remove(first.library_path, error));
    assert(!error);
    const auto repaired_missing_library =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(repaired_missing_library.success);
    assert(!repaired_missing_library.cache_hit);

    const auto stale_metadata_temporary = filesystem::path{
        artifact_metadata.string() + ".tmp.abandoned"};
    const auto legacy_checksum =
        filesystem::path{first.library_path.string() + ".sha256"};
    const auto stale_legacy_temporary = filesystem::path{
        legacy_checksum.string() + ".tmp.abandoned"};
    write_text(first_plan->build_path, "stale compiler output\n");
    write_text(stale_metadata_temporary, "stale metadata\n");
    write_text(legacy_checksum, std::string(64, '0') + "\n");
    write_text(stale_legacy_temporary, "stale checksum\n");
    write_text(artifact_metadata, "corrupt metadata\n");
    const auto recovered_staging =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(recovered_staging.success && !recovered_staging.cache_hit);
    assert(!filesystem::exists(first_plan->build_path));
    assert(!filesystem::exists(stale_metadata_temporary));
    assert(!filesystem::exists(legacy_checksum));
    assert(!filesystem::exists(stale_legacy_temporary));

    // Concurrent callers may all fingerprint independently, but exactly one
    // writer publishes the missing key and every waiter consumes that complete
    // committed pair.
    assert(filesystem::remove(first.library_path, error));
    assert(!error);
    assert(filesystem::remove(artifact_metadata, error));
    assert(!error);
    std::array<std::future<fsim::systemc::PluginCompileResult>, 3>
        concurrent_compiles;
    for (auto& future : concurrent_compiles) {
        future = std::async(std::launch::async, [&request]() {
            fsim::diagnostic::Engine concurrent_diagnostics;
            auto result = fsim::systemc::compile_plugin(
                request, concurrent_diagnostics);
            assert(!concurrent_diagnostics.has_error());
            return result;
        });
    }
    std::size_t concurrent_hits = 0;
    for (auto& future : concurrent_compiles) {
        const auto concurrent = future.get();
        assert(concurrent.success);
        assert(concurrent.cache_key == first.cache_key);
        concurrent_hits += concurrent.cache_hit ? 1U : 0U;
    }
    assert(concurrent_hits == concurrent_compiles.size() - 1);
    const auto concurrent_warm =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(concurrent_warm.success && concurrent_warm.cache_hit);

    write_text(transitive_dependency, "#define FSIM_PLUGIN_NUMBER 11\n");
    const auto header_changed_plan =
        fsim::systemc::plan_plugin_compile(request, diagnostics);
    assert(header_changed_plan);
    assert(header_changed_plan->cacheable);
    assert(header_changed_plan->cache_key != first_plan->cache_key);
    const auto invalidated =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(invalidated.success);
    assert(!invalidated.cache_hit);
    assert(invalidated.cache_key == header_changed_plan->cache_key);
    const auto invalidated_warm =
        fsim::systemc::compile_plugin(request, diagnostics);
    assert(invalidated_warm.success);
    assert(invalidated_warm.cache_hit);
    assert(invalidated_warm.cache_key == invalidated.cache_key);

    write_source(source, "fsim_compiler_smoke", "13");
    const auto changed_plan = fsim::systemc::plan_plugin_compile(request, diagnostics);
    assert(changed_plan);
    assert(changed_plan->cache_key != first_plan->cache_key);

    const auto linked_library = working / "linked library.a";
    write_text(linked_library, "first library content");
    auto linked_request = request;
    linked_request.settings.libraries = {linked_library.string()};
    fsim::diagnostic::Engine linked_diagnostics;
    const auto linked_plan =
        fsim::systemc::plan_plugin_compile(linked_request, linked_diagnostics);
    assert(linked_plan);
    write_text(linked_library, "changed library content");
    const auto changed_linked_plan =
        fsim::systemc::plan_plugin_compile(linked_request, linked_diagnostics);
    assert(changed_linked_plan);
    assert(changed_linked_plan->cache_key != linked_plan->cache_key);
    assert(linked_plan->cacheable);

    auto response_request = request;
    response_request.settings.compile_options = {"@compiler-flags.rsp"};
    fsim::diagnostic::Engine response_diagnostics;
    const auto response_plan =
        fsim::systemc::plan_plugin_compile(response_request, response_diagnostics);
    const auto second_response_plan =
        fsim::systemc::plan_plugin_compile(response_request, response_diagnostics);
    assert(response_plan);
    assert(second_response_plan);
    assert(!response_plan->cacheable);
    assert(response_plan->cache_key != second_response_plan->cache_key);

    const auto volatile_source = working / "volatile-builtin.cpp";
    write_source(
        volatile_source,
        "fsim_volatile_builtin",
        "__DATE__[0] + __TIME__[0] + __TIMESTAMP__[0]");
    auto volatile_request = request;
    volatile_request.sources = {volatile_source};
    volatile_request.settings.defines.clear();
    fsim::diagnostic::Engine volatile_diagnostics;
    const auto volatile_plan =
        fsim::systemc::plan_plugin_compile(
            volatile_request, volatile_diagnostics);
    const auto second_volatile_plan =
        fsim::systemc::plan_plugin_compile(
            volatile_request, volatile_diagnostics);
    assert(volatile_plan);
    assert(second_volatile_plan);
    assert(!volatile_plan->cacheable);
    assert(volatile_plan->cache_key != second_volatile_plan->cache_key);
    assert(has_diagnostic_code(volatile_diagnostics, "FSIM-SC-C012"));

    const auto volatile_header = include / "volatile-header.hpp";
    const auto volatile_header_source =
        working / "volatile-header-source.cpp";
    write_text(
        volatile_header,
        "#define FSIM_VOLATILE_HEADER_VALUE __TIMESTAMP__[0]\n");
    write_source(
        volatile_header_source,
        "fsim_volatile_header",
        "FSIM_VOLATILE_HEADER_VALUE",
        "\"volatile-header.hpp\"");
    auto volatile_header_request = request;
    volatile_header_request.sources = {volatile_header_source};
    volatile_header_request.settings.defines.clear();
    fsim::diagnostic::Engine volatile_header_diagnostics;
    const auto volatile_header_plan =
        fsim::systemc::plan_plugin_compile(
            volatile_header_request, volatile_header_diagnostics);
    assert(volatile_header_plan);
    assert(!volatile_header_plan->cacheable);
    assert(has_diagnostic_code(
        volatile_header_diagnostics, "FSIM-SC-C012"));
    const auto implicit_source = working / "implicit-header.cpp";
    write_source(
        implicit_source,
        "fsim_implicit_header",
        "1",
        "<systemc>");
    auto implicit_request = request;
    implicit_request.sources = {implicit_source};
    implicit_request.settings.include_directories = {fsim_include};
    implicit_request.settings.defines.clear();
    fsim::diagnostic::Engine implicit_diagnostics;
    const auto implicit_plan =
        fsim::systemc::plan_plugin_compile(implicit_request, implicit_diagnostics);
    assert(implicit_plan);
#if defined(_WIN32)
    assert(implicit_plan->cacheable);
    assert(!implicit_diagnostics.has_error());
#else
    assert(implicit_plan->cacheable);
    assert(!implicit_diagnostics.has_error());
    const auto implicit_cold =
        fsim::systemc::compile_plugin(implicit_request, implicit_diagnostics);
    assert(implicit_cold.success);
    assert(!implicit_cold.cache_hit);
    const auto implicit_warm =
        fsim::systemc::compile_plugin(implicit_request, implicit_diagnostics);
    assert(implicit_warm.success);
    assert(implicit_warm.cache_hit);
    assert(implicit_warm.cache_key == implicit_cold.cache_key);

    // The dependency is visible only through the compiler's implicit CPATH
    // search. It must still participate in the persistent key.
    const auto compiler_implicit_root = root / "compiler implicit include";
    const auto compiler_implicit_nested =
        compiler_implicit_root / "nested" / "implicit_value.hpp";
    filesystem::create_directories(
        compiler_implicit_nested.parent_path(), error);
    assert(!error);
    write_text(
        compiler_implicit_root / "fsim_compiler_implicit.hpp",
        "#include <nested/implicit_value.hpp>\n"
        "#define FSIM_COMPILER_IMPLICIT_VALUE FSIM_NESTED_IMPLICIT_VALUE\n");
    write_text(
        compiler_implicit_nested,
        "#define FSIM_NESTED_IMPLICIT_VALUE 17\n");
    const auto compiler_implicit_source =
        working / "compiler-implicit-header.cpp";
    write_source(
        compiler_implicit_source,
        "fsim_compiler_implicit_header",
        "FSIM_COMPILER_IMPLICIT_VALUE",
        "<fsim_compiler_implicit.hpp>");
    auto compiler_implicit_request = request;
    compiler_implicit_request.sources = {compiler_implicit_source};
    compiler_implicit_request.settings.include_directories.clear();
    compiler_implicit_request.settings.defines.clear();
    std::string compiler_path = compiler_implicit_root.string();
    if (const auto previous =
            fsim::support::environment_variable("CPATH");
        previous && !previous->empty()) {
        compiler_path += ":";
        compiler_path += *previous;
    }
    const ScopedEnvironment implicit_path{"CPATH", compiler_path};
    fsim::diagnostic::Engine compiler_implicit_diagnostics;
    const auto compiler_implicit_plan =
        fsim::systemc::plan_plugin_compile(
            compiler_implicit_request, compiler_implicit_diagnostics);
    assert(compiler_implicit_plan);
    assert(compiler_implicit_plan->cacheable);
    const auto compiler_implicit_cold =
        fsim::systemc::compile_plugin(
            compiler_implicit_request, compiler_implicit_diagnostics);
    assert(compiler_implicit_cold.success);
    assert(!compiler_implicit_cold.cache_hit);
    const auto compiler_implicit_warm =
        fsim::systemc::compile_plugin(
            compiler_implicit_request, compiler_implicit_diagnostics);
    assert(compiler_implicit_warm.success);
    assert(compiler_implicit_warm.cache_hit);
    write_text(
        compiler_implicit_nested,
        "#define FSIM_NESTED_IMPLICIT_VALUE 23\n");
    const auto compiler_implicit_changed_plan =
        fsim::systemc::plan_plugin_compile(
            compiler_implicit_request, compiler_implicit_diagnostics);
    assert(compiler_implicit_changed_plan);
    assert(
        compiler_implicit_changed_plan->cache_key
        != compiler_implicit_plan->cache_key);
    const auto compiler_implicit_invalidated =
        fsim::systemc::compile_plugin(
            compiler_implicit_request, compiler_implicit_diagnostics);
    assert(compiler_implicit_invalidated.success);
    assert(!compiler_implicit_invalidated.cache_hit);
    assert(
        compiler_implicit_invalidated.cache_key
        == compiler_implicit_changed_plan->cache_key);

    // Exercise makefile escaping in compiler-emitted dependency paths.
    const auto escaped_dependency = include / "escaped $#.hpp";
    const auto escaped_source = working / "escaped-dependency.cpp";
    write_text(
        escaped_dependency,
        "#define FSIM_ESCAPED_DEPENDENCY_VALUE 31\n");
    write_source(
        escaped_source,
        "fsim_escaped_dependency",
        "FSIM_ESCAPED_DEPENDENCY_VALUE",
        "\"escaped $#.hpp\"");
    auto escaped_request = request;
    escaped_request.sources = {escaped_source};
    escaped_request.settings.defines.clear();
    fsim::diagnostic::Engine escaped_diagnostics;
    const auto escaped_plan =
        fsim::systemc::plan_plugin_compile(
            escaped_request, escaped_diagnostics);
    assert(escaped_plan);
    assert(escaped_plan->cacheable);
    write_text(
        escaped_dependency,
        "#define FSIM_ESCAPED_DEPENDENCY_VALUE 37\n");
    const auto escaped_changed_plan =
        fsim::systemc::plan_plugin_compile(
            escaped_request, escaped_diagnostics);
    assert(escaped_changed_plan);
    assert(escaped_changed_plan->cacheable);
    assert(escaped_changed_plan->cache_key != escaped_plan->cache_key);

    // A missing modeled cl.exe cannot establish compiler identity, so command
    // planning stays noncacheable even though its argv remains inspectable.
    auto msvc_implicit_request = implicit_request;
    msvc_implicit_request.settings.compiler = "cl.exe";
    fsim::diagnostic::Engine msvc_implicit_diagnostics;
    const auto msvc_implicit_plan =
        fsim::systemc::plan_plugin_compile(
            msvc_implicit_request, msvc_implicit_diagnostics);
    assert(msvc_implicit_plan);
    assert(!msvc_implicit_plan->cacheable);
    assert(has_diagnostic_code(msvc_implicit_diagnostics, "FSIM-SC-C012"));

    // Model cl.exe on a non-Windows host so compiler-emitted JSON dependency
    // closure, paths with spaces, BMI identity, removal, and fallback all run
    // in every ordinary build rather than relying only on Windows CI.
    const auto fake_msvc = working / "cl.exe";
    filesystem::copy_file(
        filesystem::absolute(filesystem::path{argv[0]}, error),
        fake_msvc,
        filesystem::copy_options::overwrite_existing,
        error);
    assert(!error);
    filesystem::permissions(
        fake_msvc,
        filesystem::perms::owner_exec
            | filesystem::perms::group_exec
            | filesystem::perms::others_exec,
        filesystem::perm_options::add,
        error);
    assert(!error);
    const auto msvc_dependency_source =
        working / "msvc dependency source.cpp";
    const auto msvc_dependency_header =
        working / "msvc generated header.hpp";
    const auto msvc_dependency_module = working / "msvc module.ifc";
    write_text(
        msvc_dependency_header,
        "#define FSIM_MSVC_DEPENDENCY_VALUE 41\n");
    write_text(msvc_dependency_module, "first bmi image\n");
    write_source(
        msvc_dependency_source,
        "fsim_msvc_dependency",
        "FSIM_MSVC_DEPENDENCY_VALUE",
        "\"msvc generated header.hpp\"");
    auto emitted_msvc_request = request;
    emitted_msvc_request.sources = {msvc_dependency_source};
    emitted_msvc_request.settings.compiler = fake_msvc.string();
    emitted_msvc_request.settings.include_directories = {working};
    emitted_msvc_request.settings.defines = {
        "FSIM_TEST_MSVC_DEPENDENCY_COMPILER=1"};
    fsim::diagnostic::Engine emitted_msvc_diagnostics;
    const auto emitted_msvc_plan = fsim::systemc::plan_plugin_compile(
        emitted_msvc_request, emitted_msvc_diagnostics);
    assert(emitted_msvc_plan && emitted_msvc_plan->cacheable);
    assert(!emitted_msvc_diagnostics.has_error());

    write_text(
        msvc_dependency_header,
        "#define FSIM_MSVC_DEPENDENCY_VALUE 43\n");
    const auto edited_msvc_header_plan =
        fsim::systemc::plan_plugin_compile(
            emitted_msvc_request, emitted_msvc_diagnostics);
    assert(edited_msvc_header_plan && edited_msvc_header_plan->cacheable);
    assert(
        edited_msvc_header_plan->cache_key
        != emitted_msvc_plan->cache_key);

    write_text(msvc_dependency_module, "second bmi image\n");
    const auto edited_msvc_bmi_plan =
        fsim::systemc::plan_plugin_compile(
            emitted_msvc_request, emitted_msvc_diagnostics);
    assert(edited_msvc_bmi_plan && edited_msvc_bmi_plan->cacheable);
    assert(
        edited_msvc_bmi_plan->cache_key
        != edited_msvc_header_plan->cache_key);

    filesystem::remove(msvc_dependency_header, error);
    assert(!error);
    fsim::diagnostic::Engine missing_msvc_dependency_diagnostics;
    const auto missing_msvc_dependency_plan =
        fsim::systemc::plan_plugin_compile(
            emitted_msvc_request, missing_msvc_dependency_diagnostics);
    assert(missing_msvc_dependency_plan);
    assert(!missing_msvc_dependency_plan->cacheable);
    assert(has_diagnostic_code(
        missing_msvc_dependency_diagnostics, "FSIM-SC-C012"));

    write_text(
        msvc_dependency_header,
        "#define FSIM_MSVC_DEPENDENCY_VALUE 47\n");
    auto fallback_msvc_request = emitted_msvc_request;
    fallback_msvc_request.settings.defines.push_back(
        "FSIM_TEST_MSVC_MALFORMED=1");
    fsim::diagnostic::Engine fallback_msvc_diagnostics;
    const auto fallback_msvc_plan = fsim::systemc::plan_plugin_compile(
        fallback_msvc_request, fallback_msvc_diagnostics);
    assert(fallback_msvc_plan && fallback_msvc_plan->cacheable);
    assert(!fallback_msvc_diagnostics.has_error());
#endif

    // A sibling source directory is not an include search root. Finding this
    // header there would incorrectly make an unresolved angle include look
    // cacheable even though the host compiler cannot resolve it.
    const auto sibling_first_directory = working / "sibling first";
    const auto sibling_second_directory = working / "sibling second";
    filesystem::create_directories(sibling_first_directory, error);
    assert(!error);
    filesystem::create_directories(sibling_second_directory, error);
    assert(!error);
    const auto sibling_first_source =
        sibling_first_directory / "sibling-first.cpp";
    const auto sibling_second_source =
        sibling_second_directory / "sibling-second.cpp";
    write_source(
        sibling_first_source,
        "fsim_sibling_first",
        "1",
        "<fsim-sibling-only.hpp>");
    write_source(sibling_second_source, "fsim_sibling_second", "2");
    write_text(
        sibling_second_directory / "fsim-sibling-only.hpp",
        "#define FSIM_SIBLING_ONLY 1\n");
    auto sibling_request = request;
    sibling_request.sources = {sibling_first_source, sibling_second_source};
    fsim::diagnostic::Engine sibling_diagnostics;
    const auto sibling_plan =
        fsim::systemc::plan_plugin_compile(sibling_request, sibling_diagnostics);
    assert(sibling_plan);
    assert(!sibling_plan->cacheable);

    auto bare_library_request = request;
    bare_library_request.settings.libraries = {"library-resolved-by-linker"};
    fsim::diagnostic::Engine bare_library_diagnostics;
    const auto bare_library_plan =
        fsim::systemc::plan_plugin_compile(
            bare_library_request, bare_library_diagnostics);
    assert(bare_library_plan);
    assert(!bare_library_plan->cacheable);

    auto missing_link_request = request;
    missing_link_request.settings.libraries = {
        "fsim_systemc_library_that_does_not_exist"};
    fsim::diagnostic::Engine missing_link_diagnostics;
    const auto missing_link = fsim::systemc::compile_plugin(
        missing_link_request, missing_link_diagnostics);
    assert(!missing_link.success);
    assert(missing_link.compiler_exit_code != 0);
    assert(has_diagnostic_code(missing_link_diagnostics, "FSIM-SC-C007"));

    // cl/clang-cl must use unique object outputs even when sources from
    // different directories share a basename.
    const auto first_duplicate = working / "first" / "duplicate.cpp";
    const auto second_duplicate = working / "second" / "duplicate.cpp";
    filesystem::create_directories(first_duplicate.parent_path(), error);
    filesystem::create_directories(second_duplicate.parent_path(), error);
    write_source(first_duplicate, "fsim_duplicate_first", "1");
    write_source(second_duplicate, "fsim_duplicate_second", "2");
    auto msvc_request = request;
    msvc_request.sources = {first_duplicate, second_duplicate};
    msvc_request.settings.compiler = "cl.exe";
    fsim::diagnostic::Engine msvc_diagnostics;
    const auto msvc_plan =
        fsim::systemc::plan_plugin_compile(msvc_request, msvc_diagnostics);
    assert(msvc_plan);
    assert(msvc_plan->toolchain == fsim::systemc::HostToolchain::msvc);
    assert(msvc_plan->commands.size() == 3);
    assert(msvc_plan->intermediate_paths.size() == 7);
    assert(msvc_plan->intermediate_paths[0] != msvc_plan->intermediate_paths[2]);
    assert(has_argument(
        *msvc_plan, "/Fo" + msvc_plan->intermediate_paths[0].string()));
    assert(has_argument(
        *msvc_plan, "/Fo" + msvc_plan->intermediate_paths[2].string()));
    assert(has_argument(*msvc_plan, "/INCREMENTAL:NO"));
    assert(has_argument(*msvc_plan, "/MACHINE:X64"));
    assert(has_argument(
        *msvc_plan,
        "/PDB:" + msvc_plan->intermediate_paths[4].string()));
    assert(has_argument(
        *msvc_plan,
        "/IMPLIB:" + msvc_plan->intermediate_paths[5].string()));
    assert(msvc_plan->intermediate_paths[6].extension() == ".exp");
    for (std::size_t index = 0; index < msvc_request.sources.size(); ++index) {
#if defined(_DEBUG)
        assert(command_has_argument(msvc_plan->commands[index], "/Od"));
        assert(command_has_argument(msvc_plan->commands[index], "/Z7"));
        assert(!command_has_argument(msvc_plan->commands[index], "/O2"));
#else
        assert(command_has_argument(msvc_plan->commands[index], "/O2"));
        assert(!command_has_argument(msvc_plan->commands[index], "/Od"));
        assert(!command_has_argument(msvc_plan->commands[index], "/Z7"));
#endif
    }
#if defined(_DEBUG)
    assert(command_has_argument(msvc_plan->commands.back(), "/DEBUG:FULL"));
#else
    assert(!command_has_argument(msvc_plan->commands.back(), "/DEBUG:FULL"));
#endif
    for (const auto& command : msvc_plan->commands) {
        const auto runtime_arguments = std::count_if(
            command.argv.begin(),
            command.argv.end(),
            [](const auto& argument) {
                return is_msvc_runtime_option(argument);
            });
        assert(runtime_arguments == 1);
        assert(std::find(
                   command.argv.begin(),
                   command.argv.end(),
                   expected_msvc_runtime_option())
               != command.argv.end());
    }

    PluginCompileRequest unsafe = request;
    unsafe.settings.compile_options = {
#if defined(_WIN32)
        "/c"
#else
        "-c"
#endif
    };
    fsim::diagnostic::Engine unsafe_diagnostics;
    assert(!fsim::systemc::plan_plugin_compile(unsafe, unsafe_diagnostics));
    assert(unsafe_diagnostics.has_error());
    assert(unsafe_diagnostics.diagnostics().front().code == "FSIM-SC-C004");

    for (const auto runtime_option : {"/MD", "/MDd", "/MT", "/MTd"}) {
        auto runtime_override = msvc_request;
        runtime_override.settings.compile_options = {runtime_option};
        fsim::diagnostic::Engine runtime_diagnostics;
        assert(!fsim::systemc::plan_plugin_compile(
            runtime_override, runtime_diagnostics));
        assert(runtime_diagnostics.has_error());
        assert(runtime_diagnostics.diagnostics().front().code == "FSIM-SC-C004");
    }
    auto runtime_link_override = msvc_request;
    runtime_link_override.settings.link_options = {"/mTd"};
    fsim::diagnostic::Engine runtime_link_diagnostics;
    assert(!fsim::systemc::plan_plugin_compile(
        runtime_link_override, runtime_link_diagnostics));
    assert(runtime_link_diagnostics.has_error());
    assert(runtime_link_diagnostics.diagnostics().front().code == "FSIM-SC-C004");

    for (const auto output_option : {
             "/PDB:other.pdb",
             "/IMPLIB:other.lib",
             "/MACHINE:ARM64",
             "/INCREMENTAL"}) {
        auto output_override = msvc_request;
        output_override.settings.link_options = {output_option};
        fsim::diagnostic::Engine output_diagnostics;
        assert(!fsim::systemc::plan_plugin_compile(
            output_override, output_diagnostics));
        assert(output_diagnostics.has_error());
        assert(
            output_diagnostics.diagnostics().front().code
            == "FSIM-SC-C004");
    }

    PluginCompileRequest missing = request;
    missing.sources = {working / "missing.cpp"};
    fsim::diagnostic::Engine missing_diagnostics;
    assert(!fsim::systemc::plan_plugin_compile(missing, missing_diagnostics));
    assert(missing_diagnostics.has_error());
    assert(missing_diagnostics.diagnostics().front().code == "FSIM-SC-C003");

    const auto invalid_compile_source = working / "invalid-compile.cpp";
    write_text(
        invalid_compile_source,
        "#error FSIM_INTENTIONAL_SYSTEMC_COMPILE_FAILURE\n");
    auto invalid_compile_request = request;
    invalid_compile_request.sources = {invalid_compile_source};
    invalid_compile_request.settings.defines.clear();
    fsim::diagnostic::Engine invalid_compile_diagnostics;
    const auto invalid_compile = fsim::systemc::compile_plugin(
        invalid_compile_request, invalid_compile_diagnostics);
    assert(!invalid_compile.success);
    assert(invalid_compile.compiler_exit_code != 0);
    assert(has_diagnostic_code(
        invalid_compile_diagnostics, "FSIM-SC-C007"));
    assert(
        invalid_compile.compiler_output.find(
            "FSIM_INTENTIONAL_SYSTEMC_COMPILE_FAILURE")
        != std::string::npos);

    PluginCompileRequest missing_compiler = request;
    missing_compiler.settings.compiler =
        (working / "compiler-that-does-not-exist").string();
    fsim::diagnostic::Engine missing_compiler_diagnostics;
    const auto compiler_failure =
        fsim::systemc::compile_plugin(missing_compiler, missing_compiler_diagnostics);
    assert(!compiler_failure.success);
    assert(missing_compiler_diagnostics.has_error());
    assert(missing_compiler_diagnostics.diagnostics().back().code == "FSIM-SC-C007");

    // The fake compiler changes its input after the dependency scan and
    // emits a nonempty artifact from the changed source. The post-compile
    // fingerprint must reject that artifact instead of publishing it under
    // the stale key; a stable retry may then populate and hit the cache.
    const auto mutating_source = working / "mutating-input.cpp";
    write_source(mutating_source, "fsim_mutating_input", "1");
    auto mutating_request = request;
    mutating_request.sources = {mutating_source};
    mutating_request.settings.compiler =
        filesystem::absolute(
            filesystem::path{argv[0]}, error).string();
    assert(!error);
    mutating_request.settings.include_directories.clear();
    mutating_request.settings.defines = {
        "FSIM_TEST_MUTATING_COMPILER=1"};
    fsim::diagnostic::Engine mutating_diagnostics;
    const auto changed_during_compile =
        fsim::systemc::compile_plugin(
            mutating_request, mutating_diagnostics);
    assert(!changed_during_compile.success);
    assert(changed_during_compile.compiler_exit_code == 0);
    assert(has_diagnostic_code(
        mutating_diagnostics, "FSIM-SC-C013"));
    assert(!filesystem::exists(changed_during_compile.library_path));

    mutating_diagnostics.clear();
    const auto stable_retry =
        fsim::systemc::compile_plugin(
            mutating_request, mutating_diagnostics);
    assert(stable_retry.success);
    assert(!stable_retry.cache_hit);
    const auto stable_warm =
        fsim::systemc::compile_plugin(
            mutating_request, mutating_diagnostics);
    assert(stable_warm.success);
    assert(stable_warm.cache_hit);
    assert(stable_warm.cache_key == stable_retry.cache_key);

#if !defined(_WIN32)
    // Force pipe2() to allocate descriptors 1 and 2. The write end must remain
    // available as stderr across exec so compiler diagnostics are captured.
    const auto invalid_source = working / "low-fd-error.cpp";
    write_text(invalid_source, "#error FSIM_LOW_FD_DIAGNOSTIC\n");
    auto low_fd_request = request;
    low_fd_request.sources = {invalid_source};
    fsim::diagnostic::Engine low_fd_diagnostics;

    const auto saved_stdout = ::dup(STDOUT_FILENO);
    const auto saved_stderr = ::dup(STDERR_FILENO);
    assert(saved_stdout >= 0);
    assert(saved_stderr >= 0);
    assert(::close(STDOUT_FILENO) == 0);
    assert(::close(STDERR_FILENO) == 0);
    const auto low_fd_failure =
        fsim::systemc::compile_plugin(low_fd_request, low_fd_diagnostics);
    const auto restore_stdout = ::dup2(saved_stdout, STDOUT_FILENO);
    const auto restore_stderr = ::dup2(saved_stderr, STDERR_FILENO);
    ::close(saved_stdout);
    ::close(saved_stderr);

    assert(restore_stdout == STDOUT_FILENO);
    assert(restore_stderr == STDERR_FILENO);
    assert(!low_fd_failure.success);
    assert(low_fd_failure.compiler_exit_code != 0);
    assert(
        low_fd_failure.compiler_output.find("FSIM_LOW_FD_DIAGNOSTIC")
        != std::string::npos);
#endif

    filesystem::remove_all(root, error);
    std::cout << "SystemC plug-in compiler tests passed\n";
}
