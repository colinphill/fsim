// SPDX-License-Identifier: Apache-2.0
#include "tcl_command_catalog.hpp"
#include "tcl_completion_adapter.hpp"

#include "tcl_internal.hpp"

#include "application_workspace_store.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/support/path.hpp"

#include <tcl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using namespace fsim::app;
using namespace fsim::app::tcl_completion;
namespace fs = std::filesystem;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

bool has_candidate(
    const CompletionResult& result,
    const std::string_view display,
    const CandidateKind kind)
{
    for (const auto& candidate : result.candidates) {
        if (candidate.display == display && candidate.kind == kind) {
            return true;
        }
    }
    return false;
}

struct TclDeleter {
    void operator()(Tcl_Interp* interpreter) const noexcept
    {
        if (interpreter != nullptr) {
            Tcl_DeleteInterp(interpreter);
        }
    }
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path()
            / ("fsim-tcl-completion-" + std::to_string(stamp));
        std::error_code error;
        if (!fs::create_directories(m_path, error) || error) {
            throw std::runtime_error { "cannot create Tcl completion test directory" };
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return m_path; }

private:
    fs::path m_path;
};

class CurrentDirectoryRestore {
public:
    CurrentDirectoryRestore()
        : m_directory(fs::current_path())
    {
    }

    ~CurrentDirectoryRestore()
    {
        std::error_code error;
        fs::current_path(m_directory, error);
    }

    CurrentDirectoryRestore(const CurrentDirectoryRestore&) = delete;
    CurrentDirectoryRestore& operator=(const CurrentDirectoryRestore&) = delete;

private:
    fs::path m_directory;
};

void create_library_fixture(const fs::path& root, const std::string_view name)
{
    fsim::app::workspace::Store store { root };
    std::string error;
    auto transaction = store.begin_library(name, error);
    require(transaction != nullptr, "workspace library fixture must be created");
    require(transaction->commit(
                std::vector<fsim::app::workspace::ArtifactRecord> { }, error),
        "workspace library fixture must be published");
}

void write_snapshot_fixture(const fs::path& root)
{
    constexpr std::string_view revision = "r0123456789abcdef0123456789abcdef.fsimdesign";
    const auto directory = root / ".fsim" / "snapshots" / "smoke";
    fs::create_directories(directory / "revisions" / revision);
    std::ofstream index { directory / "snapshot.index", std::ios::binary };
    index << "FSIM-SNAPSHOT 1\n"
          << revision << '\n';
    if (!index) {
        throw std::runtime_error { "cannot write snapshot fixture" };
    }
}

const fsim::app::tcl_detail::TclCommandSpec* find_spec(const std::string_view name)
{
    const auto specs = fsim::app::tcl_detail::command_specs();
    const auto found = std::find_if(specs.begin(), specs.end(), [name](const auto& spec) {
        return spec.name == name || (spec.name.starts_with("::") && spec.name.substr(2U) == name);
    });
    return found == specs.end() ? nullptr : &*found;
}

bool has_argument(
    const std::span<const fsim::app::tcl_detail::TclCommandArgument> arguments,
    const std::string_view name,
    const fsim::app::tcl_detail::TclCompletionDomain domain)
{
    return std::ranges::any_of(arguments, [name, domain](const auto& argument) {
        return argument.name == name && argument.completion == domain;
    });
}

const fsim::app::tcl_detail::TclSubcommandSpec* find_subcommand(
    const fsim::app::tcl_detail::TclCommandSpec& command,
    const std::string_view name)
{
    const auto found = std::find_if(
        command.subcommands.begin(), command.subcommands.end(), [name](const auto& subcommand) {
            return subcommand.name == name;
        });
    return found == command.subcommands.end() ? nullptr : &*found;
}

void test_command_catalog_completion_metadata()
{
    using namespace fsim::app::tcl_detail;
    const auto* compile = find_spec("fsim::compile");
    const auto* elaborate = find_spec("fsim::elaborate");
    const auto* library = find_spec("fsim::library");
    const auto* object = find_spec("fsim::object");
    const auto* debug = find_spec("fsim::debug");
    const auto* load = find_spec("fsim::load");
    require(compile != nullptr && compile->capability == TclCommandCapability::workspace
            && compile->result_shape == TclResultShape::dictionary
            && !compile->diagnostic_domain.empty()
            && has_argument(compile->arguments, "source", TclCompletionDomain::source_path)
            && has_argument(compile->arguments, "-library", TclCompletionDomain::library),
        "compile catalog metadata must route source and library completion with result help");
    require(elaborate != nullptr && elaborate->capability == TclCommandCapability::workspace
            && elaborate->result_shape == TclResultShape::dictionary
            && has_argument(elaborate->arguments, "top", TclCompletionDomain::compiled_definition)
            && has_argument(elaborate->arguments, "-snapshot", TclCompletionDomain::snapshot),
        "elaborate catalog metadata must route roots and snapshot options");
    require(library != nullptr && library->capability == TclCommandCapability::workspace
            && find_subcommand(*library, "map") != nullptr
            && has_argument(find_subcommand(*library, "map")->arguments,
                "directory", TclCompletionDomain::directory_path),
        "library subcommand metadata must route map directory completion");
    require(object != nullptr && object->capability == TclCommandCapability::workspace
            && find_subcommand(*object, "info") != nullptr
            && find_subcommand(*object, "info")->capability == TclCommandCapability::loaded_design
            && find_subcommand(*object, "definition") != nullptr,
        "object command metadata must separate loaded objects from workspace definitions");
    require(debug != nullptr && debug->capability == TclCommandCapability::debugger
            && find_subcommand(*debug, "break") != nullptr
            && find_subcommand(*debug, "break")->capability == TclCommandCapability::debugger
            && debug->diagnostic_domain == "FSIM-TCL-DEBUG",
        "debug command metadata must expose debugger subcommands and diagnostics");
    require(load != nullptr
            && has_argument(load->arguments, "snapshot", TclCompletionDomain::snapshot),
        "load metadata must expose the snapshot provider");
}

void test_read_only_completion_and_generation_snapshot()
{
    Tcl_FindExecutable("fsim-tcl-completion-test");
    std::unique_ptr<Tcl_Interp, TclDeleter> interpreter { Tcl_CreateInterp() };
    require(interpreter != nullptr, "Tcl_CreateInterp must create a headless interpreter");
    require(Tcl_Init(interpreter.get()) == TCL_OK, "Tcl_Init must initialize core commands");

    TemporaryDirectory temporary;
    const auto root_a = temporary.path() / "workspace-a";
    const auto root_b = temporary.path() / "workspace-b";
    fs::create_directories(root_a / "a_hdl");
    fs::create_directories(root_b / "b_hdl");
    const auto source_a = root_a / "a_hdl" / fsim::support::path_from_utf8("δ_first.sv");
    const auto source_b = root_b / "b_hdl" / fsim::support::path_from_utf8("δ_second.sv");
    std::ofstream source_file_a { source_a, std::ios::binary };
    source_file_a << "module first; endmodule\n";
    require(static_cast<bool>(source_file_a), "first source path fixture must be created");
    source_file_a.close();
    std::ofstream source_file_b { source_b, std::ios::binary };
    source_file_b << "module second; endmodule\n";
    require(static_cast<bool>(source_file_b), "second source path fixture must be created");
    source_file_b.close();
    write_snapshot_fixture(root_a);
    create_library_fixture(root_a, "alpha");
    create_library_fixture(root_b, "beta");

    CurrentDirectoryRestore restore_current_directory;

    constexpr std::string_view setup = "set ::completion_counter 0; "
                                       "namespace eval ::completion_probe { "
                                       "variable present 1; "
                                       "proc run {} {incr ::completion_counter} "
                                       "}";
    require(Tcl_EvalEx(
                interpreter.get(), setup.data(), static_cast<Tcl_Size>(setup.size()),
                TCL_EVAL_GLOBAL)
            == TCL_OK,
        "Tcl fixture commands must initialize the read-only provider state");

    const auto cd_a = "cd {" + fsim::support::path_to_utf8(root_a) + "}";
    require(Tcl_EvalEx(
                interpreter.get(), cd_a.c_str(), static_cast<Tcl_Size>(cd_a.size()),
                TCL_EVAL_GLOBAL)
            == TCL_OK,
        "Tcl cd must select the first workspace before completion starts");

    fsim::diagnostic::Engine diagnostics;
    std::ostringstream output;
    std::ostringstream error;
    fsim::app::tcl_detail::TclContext context {
        fsim::cli::Invocation { },
        fsim::project::Config { },
        fsim::project::Config { },
        diagnostics,
        interpreter.get(),
        output,
        error,
        std::nullopt,
        nullptr,
        std::nullopt,
        nullptr,
        nullptr,
        nullptr,
        { },
        0U,
        0U,
        0U,
        false,
        false,
        std::nullopt,
        false,
        0,
        { },
        nullptr,
        nullptr
    };
    context.config.base_directory = root_a;
    context.initial_config.base_directory = root_a;
    context.references = std::make_unique<tcl_detail::TclReferenceTable>();
    fsim::project::SourceSet source_set;
    source_set.files.push_back(source_a);
    context.config.source_sets.push_back(std::move(source_set));

    constexpr std::string_view procedure_input = "::completion_probe::run";
    const auto procedure_result = fsim::app::tcl_detail::complete_tcl(
        context, procedure_input, procedure_input.size());
    require(procedure_result.status == CompletionStatus::ok
            && has_candidate(
                procedure_result, "::completion_probe::run", CandidateKind::procedure),
        "completion must expose read-only Tcl procedure names");

    constexpr std::string_view variable_input = "puts $::completion_probe::pre";
    const auto variable_cursor = variable_input.size();
    const auto variable_result = fsim::app::tcl_detail::complete_tcl(
        context, variable_input, variable_cursor);
    require(has_candidate(
                variable_result, "::completion_probe::present", CandidateKind::variable),
        "completion must expose namespaced Tcl variables");

    constexpr std::string_view path_input = "::fsim::compile a_hdl/δ";
    const auto path_result = fsim::app::tcl_detail::complete_tcl(
        context, path_input, path_input.size());
    require(has_candidate(path_result, "a_hdl/δ_first.sv", CandidateKind::path),
        "completion must expose bounded UTF-8 workspace paths");

    constexpr std::string_view library_input_a = "::fsim::library map a";
    const auto library_result_a = fsim::app::tcl_detail::complete_tcl(
        context, library_input_a, library_input_a.size());
    require(has_candidate(library_result_a, "alpha", CandidateKind::library),
        "completion must read libraries from the current workspace");

    constexpr std::string_view library_value_input = "::fsim::compile -lang systemverilog -library wo";
    const auto option_library_result = fsim::app::tcl_detail::complete_tcl(
        context, library_value_input, library_value_input.size());
    require(has_candidate(option_library_result, "work", CandidateKind::library),
        "completion must route a valued option to the library provider");

    constexpr std::string_view snapshot_input = "::fsim::load smo";
    const auto snapshot_result = fsim::app::tcl_detail::complete_tcl(
        context, snapshot_input, snapshot_input.size());
    require(has_candidate(snapshot_result, "smoke", CandidateKind::snapshot),
        "completion must read and validate workspace snapshot metadata");

    constexpr std::string_view counter_name = "::completion_counter";
    const auto* before = Tcl_GetVar(interpreter.get(), counter_name.data(), TCL_GLOBAL_ONLY);
    require(before != nullptr && std::string_view { before } == "0",
        "completion must not evaluate a procedure name from the edited buffer");

    const auto generations = fsim::app::tcl_detail::tcl_completion_generations(context);
    require(is_current(option_library_result, generations),
        "unchanged Tcl/workspace snapshots must retain the returned generations");
    require(Tcl_SetVar(
                interpreter.get(), "::completion_probe::later", "1", TCL_GLOBAL_ONLY)
            != nullptr,
        "test variable mutation must succeed");
    require(!is_current(
                procedure_result,
                fsim::app::tcl_detail::tcl_completion_generations(context)),
        "a Tcl namespace variable change must reject a stale completion generation");

    const auto cd_b = "cd {" + fsim::support::path_to_utf8(root_b) + "}";
    require(Tcl_EvalEx(
                interpreter.get(), cd_b.c_str(), static_cast<Tcl_Size>(cd_b.size()),
                TCL_EVAL_GLOBAL)
            == TCL_OK,
        "Tcl cd must select the second workspace");
    constexpr std::string_view path_input_b = "::fsim::compile b_hdl/δ";
    const auto path_result_b = fsim::app::tcl_detail::complete_tcl(
        context, path_input_b, path_input_b.size());
    require(context.config.base_directory == root_b
            && has_candidate(path_result_b, "b_hdl/δ_second.sv", CandidateKind::path),
        "direct completion must synchronize Tcl cd and use the new workspace paths");
    constexpr std::string_view library_input_b = "::fsim::library map b";
    const auto library_result_b = fsim::app::tcl_detail::complete_tcl(
        context, library_input_b, library_input_b.size());
    require(has_candidate(library_result_b, "beta", CandidateKind::library)
            && !has_candidate(library_result_b, "alpha", CandidateKind::library),
        "direct completion must read libraries from the workspace selected by Tcl cd");
    require(!is_current(
                library_result_a,
                fsim::app::tcl_detail::tcl_completion_generations(context)),
        "a Tcl cd must reject candidates captured from the previous workspace");
}

} // namespace

int main()
{
    try {
        test_command_catalog_completion_metadata();
        test_read_only_completion_and_generation_snapshot();
        std::cout << "Tcl completion adapter tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
