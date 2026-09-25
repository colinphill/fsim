// SPDX-License-Identifier: Apache-2.0
#include "tcl_console.hpp"

#if defined(FSIM_HAS_TCL)

#include "fsim/support/path.hpp"

#include <isocline.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fsim::app {
namespace {

    std::mutex& isocline_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    bool stdin_stdout_are_terminals()
    {
#if defined(_WIN32)
        return ::_isatty(::_fileno(stdin)) != 0
            && ::_isatty(::_fileno(stdout)) != 0;
#else
        return ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
#endif
    }

    bool stream_is_terminal(const std::ostream* stream)
    {
#if defined(_WIN32)
        if (stream == &std::cout)
            return ::_isatty(::_fileno(stdout)) != 0;
        if (stream == &std::cerr)
            return ::_isatty(::_fileno(stderr)) != 0;
#else
        if (stream == &std::cout)
            return ::isatty(STDOUT_FILENO) != 0;
        if (stream == &std::cerr)
            return ::isatty(STDERR_FILENO) != 0;
#endif
        return false;
    }

    bool dumb_terminal()
    {
        const char* term = std::getenv("TERM");
        return term != nullptr && std::string_view { term } == "dumb";
    }

    bool no_color_requested()
    {
        const char* value = std::getenv("NO_COLOR");
        return value != nullptr && value[0] != '\0';
    }

    const char* severity_style(const diagnostic::Severity severity)
    {
        switch (diagnostic::severity_color(severity)) {
        case diagnostic::SeverityColor::cyan:
            return "console-note";
        case diagnostic::SeverityColor::yellow:
            return "console-warning";
        case diagnostic::SeverityColor::red:
            return "console-error";
        case diagnostic::SeverityColor::bold_red:
            return "console-fatal";
        }
        return "console-error";
    }

    const char* span_style(const tcl_completion::Span& span)
    {
        if (span.kind == tcl_completion::SpanKind::diagnostic) {
            switch (span.severity) {
            case tcl_completion::Severity::informational:
                return "console-note";
            case tcl_completion::Severity::warning:
                return "console-warning";
            case tcl_completion::Severity::error:
                return "console-error";
            case tcl_completion::Severity::none:
                return "tcl-diagnostic";
            }
        }
        switch (span.kind) {
        case tcl_completion::SpanKind::command:
            return "tcl-command";
        case tcl_completion::SpanKind::variable:
            return "tcl-variable";
        case tcl_completion::SpanKind::option:
            return "tcl-option";
        case tcl_completion::SpanKind::quoted_string:
            return "tcl-string";
        case tcl_completion::SpanKind::braced_word:
            return "tcl-braced";
        case tcl_completion::SpanKind::command_substitution:
            return "tcl-substitution";
        case tcl_completion::SpanKind::comment:
            return "tcl-comment";
        case tcl_completion::SpanKind::diagnostic:
            return "tcl-diagnostic";
        }
        return "tcl-command";
    }

    std::string bbcode_safe_text(const std::string_view text)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(text.size());
        for (const char byte : text) {
            const auto character = static_cast<unsigned char>(byte);
            if (character < 0x20U || character == 0x7FU) {
                result.append("\\x");
                result.push_back(hex[character >> 4U]);
                result.push_back(hex[character & 0x0FU]);
            } else {
                if (character == '[' || character == '\\')
                    result.push_back('\\');
                result.push_back(static_cast<char>(character));
            }
        }
        return result;
    }

} // namespace

struct TclConsole::State {
    struct PendingOutput {
        std::ostream* destination { };
        std::streambuf* sink { };
        bool terminal_destination { false };
        std::string style;
        std::string text;
    };

    class OutputBuffer final : public std::streambuf {
    public:
        OutputBuffer(State& state, std::ostream& destination,
            std::streambuf* sink, std::string style)
            : m_state(state)
            , m_destination(destination)
            , m_sink(sink)
            , m_style(std::move(style))
        {
        }

    protected:
        std::streamsize xsputn(const char* text, const std::streamsize count) override
        {
            if (count > 0) {
                m_state.enqueue(m_destination, m_sink, m_style,
                    std::string_view { text, static_cast<std::size_t>(count) });
            }
            return count;
        }

        int_type overflow(const int_type value) override
        {
            if (traits_type::eq_int_type(value, traits_type::eof())) {
                return traits_type::not_eof(value);
            }
            const char character = traits_type::to_char_type(value);
            m_state.enqueue(m_destination, m_sink, m_style,
                std::string_view { &character, 1U });
            return value;
        }

        int sync() override
        {
            return 0;
        }

    private:
        State& m_state;
        std::ostream& m_destination;
        std::streambuf* m_sink;
        std::string m_style;
    };

    explicit State(Options input_options)
        : options(std::move(input_options))
        , owner_thread(std::this_thread::get_id())
    {
        if (options.input == nullptr)
            options.input = &std::cin;
        if (options.output == nullptr)
            options.output = &std::cout;
        if (options.error == nullptr)
            options.error = &std::cerr;
        output_sink = options.output->rdbuf();
        error_sink = options.error->rdbuf();
        if (options.workspace_root.empty()) {
            std::error_code error;
            options.workspace_root = std::filesystem::current_path(error);
            if (error)
                options.workspace_root = ".";
        }
        if (!options.history_path.is_absolute()) {
            options.history_path = options.workspace_root / options.history_path;
        }
    }

    void enqueue(
        std::ostream& destination,
        std::streambuf* sink,
        const std::string_view style,
        const std::string_view text)
    {
        if (text.empty())
            return;
        std::lock_guard lock { output_mutex };
        if (!read_active) {
            destination.write(text.data(), static_cast<std::streamsize>(text.size()));
            destination.flush();
            return;
        }
        if (!pending.empty() && pending.back().destination == &destination
            && pending.back().style == style) {
            pending.back().text.append(text);
        } else {
            pending.push_back(PendingOutput { &destination, sink,
                stream_is_terminal(&destination), std::string { style },
                std::string { text } });
        }
    }

    std::deque<PendingOutput> take_pending()
    {
        std::lock_guard lock { output_mutex };
        std::deque<PendingOutput> result;
        result.swap(pending);
        return result;
    }

    void finish_editor_read()
    {
        std::lock_guard lock { output_mutex };
        read_active.store(false, std::memory_order_release);
        for (auto& item : pending) {
            if (item.destination != nullptr) {
                item.destination->write(item.text.data(),
                    static_cast<std::streamsize>(item.text.size()));
                item.destination->flush();
            }
        }
        pending.clear();
    }

    [[nodiscard]] bool terminal_input_output() const
    {
        return options.input == &std::cin && options.output == &std::cout
            && stdin_stdout_are_terminals();
    }

    [[nodiscard]] bool basic_terminal() const
    {
        return terminal_input_output() && dumb_terminal();
    }

    [[nodiscard]] bool editor_terminal() const
    {
        return terminal_input_output() && !basic_terminal();
    }

    [[nodiscard]] bool color_enabled() const
    {
        const bool terminal = terminal_input_output() && !basic_terminal();
        const bool color = diagnostic::color_enabled(
            options.color_mode, terminal, no_color_requested());
        return terminal && color;
    }

    [[nodiscard]] bool error_color_enabled() const
    {
        const bool terminal = stream_is_terminal(options.error) && !dumb_terminal();
        const bool color = diagnostic::color_enabled(
            options.color_mode, terminal, no_color_requested());
        return terminal && color;
    }

    [[nodiscard]] bool current(const tcl_completion::CompletionResult& result) const
    {
        if (!completion_ready || result.status != tcl_completion::CompletionStatus::ok) {
            return false;
        }
        if (!options.live_generations) {
            return tcl_completion::is_current(result, result.generations);
        }
        try {
            return tcl_completion::is_current(result, options.live_generations());
        } catch (...) {
            return false;
        }
    }

    void query(std::string_view input, const std::size_t cursor)
    {
        completion_ready = false;
        if (!options.completion_query || cursor > input.size())
            return;
        try {
            last_completion = options.completion_query(input, cursor);
            completion_ready = (last_completion.status
                == tcl_completion::CompletionStatus::ok);
        } catch (...) {
            completion_ready = false;
        }
    }

    static void completer(ic_completion_env_t* environment, const char*)
    {
        try {
            if (environment == nullptr)
                return;
            auto* state = static_cast<State*>(ic_completion_arg(environment));
            if (state == nullptr || !state->options.completion_query)
                return;
            long cursor = 0;
            const char* input = ic_completion_input(environment, &cursor);
            if (input == nullptr || cursor < 0)
                return;
            state->query(input, static_cast<std::size_t>(cursor));
            if (!state->current(state->last_completion))
                return;

            const auto& result = state->last_completion;
            const auto input_size = std::char_traits<char>::length(input);
            if (result.replace_begin_byte > result.replace_end_byte
                || result.replace_end_byte > input_size
                || static_cast<std::size_t>(cursor) < result.replace_begin_byte
                || static_cast<std::size_t>(cursor) > result.replace_end_byte) {
                return;
            }
            const auto delete_before = static_cast<std::size_t>(cursor)
                - result.replace_begin_byte;
            const auto delete_after = result.replace_end_byte
                - static_cast<std::size_t>(cursor);
            if (delete_before > static_cast<std::size_t>(std::numeric_limits<long>::max())
                || delete_after > static_cast<std::size_t>(std::numeric_limits<long>::max())) {
                return;
            }
            for (const auto& candidate : result.candidates) {
                const std::string display_text = bbcode_safe_text(
                    candidate.display.empty() ? std::string_view { candidate.text }
                                              : std::string_view { candidate.display });
                const std::string help_text = bbcode_safe_text(candidate.help);
                if (!ic_add_completion_prim(environment, candidate.text.c_str(),
                        display_text.c_str(),
                        candidate.help.empty() ? nullptr : help_text.c_str(),
                        static_cast<long>(delete_before), static_cast<long>(delete_after))) {
                    break;
                }
            }
            if (!result.hints.empty()) {
                std::string joined;
                for (const auto& hint : result.hints) {
                    if (hint.text.empty())
                        continue;
                    if (!joined.empty())
                        joined.append("  |  ");
                    joined.append(bbcode_safe_text(hint.text));
                }
                if (!joined.empty())
                    ic_add_hint(environment, joined.c_str());
            }
        } catch (...) {
            // Never let C++ allocation/provider exceptions cross the C ABI.
        }
    }

    static void highlighter(
        ic_highlight_env_t* environment,
        const char* input,
        void* argument)
    {
        auto* state = static_cast<State*>(argument);
        if (environment == nullptr || input == nullptr || state == nullptr)
            return;
        const auto input_size = std::char_traits<char>::length(input);
        state->query(input, input_size);
        if (!state->current(state->last_completion))
            return;
        for (const auto& span : state->last_completion.spans) {
            const auto begin = std::min(span.begin_byte, input_size);
            const auto end = std::min(span.end_byte, input_size);
            if (begin < end && end <= static_cast<std::size_t>(std::numeric_limits<long>::max())) {
                ic_highlight(environment, static_cast<long>(begin),
                    static_cast<long>(end - begin), span_style(span));
            }
        }
    }

    static bool command_complete(const char* input, void* argument)
    {
        auto* state = static_cast<State*>(argument);
        if (state == nullptr || input == nullptr || !state->options.command_complete) {
            return true;
        }
        try {
            return state->options.command_complete(input);
        } catch (...) {
            return true;
        }
    }

    static bool completion_current(void* argument)
    {
        auto* state = static_cast<State*>(argument);
        return state != nullptr && state->current(state->last_completion);
    }

    static void emit_pending(
        void* argument,
        ic_console_output_emit_fun_t* emit,
        void* emit_argument)
    {
        try {
            auto* state = static_cast<State*>(argument);
            if (state == nullptr || emit == nullptr)
                return;
            auto queued = state->take_pending();
            for (auto& item : queued) {
                // The C editor callback uses NUL-terminated strings. Escape NUL
                // explicitly so no queued output is silently truncated.
                std::string safe;
                safe.reserve(item.text.size());
                for (const char byte : item.text) {
                    const auto character = static_cast<unsigned char>(byte);
                    if (character == 0U)
                        safe.append("\\x00");
                    else
                        safe.push_back(static_cast<char>(character));
                }
                if (item.terminal_destination) {
                    emit(emit_argument, safe.c_str(),
                        (item.style.empty() || !state->color_enabled())
                            ? nullptr
                            : item.style.c_str());
                } else if (item.sink != nullptr) {
                    item.sink->sputn(safe.data(),
                        static_cast<std::streamsize>(safe.size()));
                    item.sink->pubsync();
                }
            }
        } catch (...) {
            // Keep the editor alive if a queued diagnostic cannot be rendered.
        }
    }

    static bool idle(void* argument,
        ic_console_output_emit_fun_t* emit,
        void* emit_argument)
    {
        auto* state = static_cast<State*>(argument);
        if (state == nullptr)
            return true;
        emit_pending(state, emit, emit_argument);
        return state->interrupt_requested.exchange(false, std::memory_order_acq_rel);
    }

    static bool plain_complete(const State& state, const std::string& text)
    {
        if (!state.options.command_complete)
            return true;
        try {
            return state.options.command_complete(text);
        } catch (...) {
            return true;
        }
    }

    TclConsoleReadResult read_plain(const bool show_prompt)
    {
        std::string command;
        std::string line;
        while (true) {
            if (show_prompt) {
                auto& output = *options.output;
                output << (command.empty() ? options.prompt_text + "> " : "... ");
                output.flush();
            }
            if (!std::getline(*options.input, line)) {
                return { TclConsoleReadStatus::end_of_file, std::move(command) };
            }
            command.append(line);
            command.push_back('\n');
            if (plain_complete(*this, command)) {
                return { TclConsoleReadStatus::submitted, std::move(command) };
            }
        }
    }

    TclConsoleReadResult read_editor()
    {
        if (std::this_thread::get_id() != owner_thread) {
            return { TclConsoleReadStatus::error, { } };
        }
        bool expected = false;
        if (!read_active.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel)) {
            return { TclConsoleReadStatus::error, { } };
        }
        interrupt_requested.store(false, std::memory_order_release);
        std::lock_guard editor_lock { isocline_mutex() };

        const bool colors_on = color_enabled();
        const bool previous_color = ic_enable_color(colors_on);
        ic_style_def("console-note", "color=cyan");
        ic_style_def("console-warning", "color=yellow");
        ic_style_def("console-error", "color=red");
        ic_style_def("console-fatal", "bold color=red");
        ic_style_def("tcl-command", "bold color=cyan");
        ic_style_def("tcl-variable", "color=magenta");
        ic_style_def("tcl-option", "color=yellow");
        ic_style_def("tcl-string", "color=green");
        ic_style_def("tcl-braced", "color=green");
        ic_style_def("tcl-substitution", "color=blue");
        ic_style_def("tcl-comment", "color=gray");
        ic_style_def("tcl-diagnostic", "color=red");
        ic_set_prompt_marker("> ", "... ");

        std::error_code history_error;
        if (options.persist_history && options.history_limit > 0U) {
            const auto history_parent = options.history_path.parent_path();
            if (!history_parent.empty()) {
                std::filesystem::create_directories(history_parent, history_error);
            }
        }
        const bool use_history = options.persist_history && options.history_limit > 0U
            && !history_error;
        const long history_limit = static_cast<long>(std::clamp<std::size_t>(
            options.history_limit, 0U, 10000U));
        std::string history_filename;
        if (use_history) {
            history_filename = fsim::support::path_to_utf8(options.history_path);
            ic_set_history(history_filename.c_str(), history_limit);
        } else {
            ic_set_history(nullptr, history_limit);
        }

        OutputBuffer output_buffer { *this, *options.output, output_sink, "" };
        OutputBuffer error_buffer { *this, *options.error,
            error_sink, error_color_enabled() ? severity_style(diagnostic::Severity::error) : "" };
        std::streambuf* const old_output_buffer = options.output->rdbuf(&output_buffer);
        std::streambuf* const old_error_buffer = (options.error == options.output)
            ? nullptr
            : options.error->rdbuf(&error_buffer);

        ic_readline_status_t status = IC_READLINE_ERROR;
        char* const raw = ic_readline_console(options.prompt_text.c_str(),
            &State::completer, this, &State::highlighter, this,
            &State::command_complete, this,
            &State::completion_current, this,
            &State::idle, this, 50, &status);

        options.output->rdbuf(old_output_buffer);
        if (old_error_buffer != nullptr)
            options.error->rdbuf(old_error_buffer);
        finish_editor_read();
        ic_enable_color(previous_color);

        std::string text;
        if (raw != nullptr) {
            text.assign(raw);
            ic_free(raw);
        }
        if (status == IC_READLINE_INTERRUPTED) {
            return { TclConsoleReadStatus::interrupted, std::move(text) };
        }
        if (status == IC_READLINE_CANCELLED) {
            return { TclConsoleReadStatus::cancelled, std::move(text) };
        }
        if (status == IC_READLINE_EOF) {
            return { TclConsoleReadStatus::end_of_file, std::move(text) };
        }
        if (status == IC_READLINE_ERROR) {
            return { TclConsoleReadStatus::error, std::move(text) };
        }
        return { TclConsoleReadStatus::submitted, std::move(text) };
    }

    Options options;
    const std::thread::id owner_thread;
    std::streambuf* output_sink { };
    std::streambuf* error_sink { };
    std::atomic<bool> read_active { false };
    std::atomic<bool> interrupt_requested { false };
    mutable std::mutex output_mutex;
    std::deque<PendingOutput> pending;
    tcl_completion::CompletionResult last_completion;
    bool completion_ready { false };
};

TclConsole::TclConsole(Options options)
    : m_state(std::make_unique<State>(std::move(options)))
{
}

TclConsole::~TclConsole() = default;

TclConsoleReadResult TclConsole::read_command()
{
    if (!m_state)
        return { TclConsoleReadStatus::error, { } };
    if (std::this_thread::get_id() != m_state->owner_thread) {
        return { TclConsoleReadStatus::error, { } };
    }
    const bool is_terminal = m_state->terminal_input_output();
    if (m_state->editor_terminal())
        return m_state->read_editor();
    return m_state->read_plain(is_terminal);
}

void TclConsole::write_output(const std::string_view text)
{
    if (!m_state || m_state->options.output == nullptr)
        return;
    m_state->enqueue(*m_state->options.output, m_state->output_sink,
        { }, text);
}

void TclConsole::write_diagnostic(const diagnostic::Diagnostic& value)
{
    if (!m_state || m_state->options.error == nullptr)
        return;
    std::ostringstream rendered;
    const bool enabled = m_state->error_color_enabled();
    diagnostic::print_text(rendered, value, enabled);
    // The editor recognizes only the diagnostic renderer's fixed SGR palette
    // and converts those codes to Isocline styles before writing to the TTY.
    m_state->enqueue(*m_state->options.error, m_state->error_sink,
        { }, rendered.str());
}

void TclConsole::interrupt() noexcept
{
    if (m_state) {
        m_state->interrupt_requested.store(true, std::memory_order_release);
    }
}

} // namespace fsim::app

#endif // FSIM_HAS_TCL
