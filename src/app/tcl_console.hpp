// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(FSIM_HAS_TCL)

#include "fsim/diagnostic/diagnostic.hpp"
#include "tcl_completion.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

namespace fsim::app {

enum class TclConsoleReadStatus {
    submitted,
    cancelled,
    end_of_file,
    interrupted,
    error,
};

struct TclConsoleReadResult {
    TclConsoleReadStatus status { TclConsoleReadStatus::error };
    std::string text;
};

/// Terminal-owning Tcl input adapter. All terminal operations occur on the
/// constructing thread; output and interrupt requests may arrive elsewhere.
class TclConsole final {
public:
    struct Options {
        std::istream* input { };
        std::ostream* output { };
        std::ostream* error { };
        std::filesystem::path workspace_root;
        std::filesystem::path history_path { ".fsim/tcl_history" };
        std::string prompt_text { "(fsim:tcl)" };
        std::size_t history_limit { 200U };
        bool persist_history { true };
        diagnostic::ColorMode color_mode { diagnostic::ColorMode::automatic };
        std::function<bool(std::string_view)> command_complete;
        std::function<tcl_completion::CompletionResult(
            std::string_view, std::size_t)>
            completion_query;
        std::function<tcl_completion::Generations()> live_generations;
    };

    explicit TclConsole(Options options);
    ~TclConsole();

    TclConsole(const TclConsole&) = delete;
    TclConsole& operator=(const TclConsole&) = delete;
    TclConsole(TclConsole&&) = delete;
    TclConsole& operator=(TclConsole&&) = delete;

    [[nodiscard]] TclConsoleReadResult read_command();

    /// Queue text for the active editor, or write it to the output stream when
    /// no read is in progress. These methods are safe to call from workers.
    void write_output(std::string_view text);
    void write_diagnostic(const diagnostic::Diagnostic& diagnostic);

    /// Stop the current read without calling a terminal API from this thread.
    void interrupt() noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace fsim::app

#endif // FSIM_HAS_TCL
