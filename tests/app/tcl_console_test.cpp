#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

// SPDX-License-Identifier: Apache-2.0
#include "tcl_console.hpp"

#if defined(FSIM_HAS_TCL)

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

using fsim::app::TclConsole;
using fsim::app::TclConsoleReadStatus;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "tcl_console_test: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool braces_complete(const std::string_view input)
{
    std::size_t depth = 0U;
    bool escaped = false;
    for (const char character : input) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (character == '\\') {
            escaped = true;
        } else if (character == '{') {
            ++depth;
        } else if (character == '}' && depth > 0U) {
            --depth;
        }
    }
    return depth == 0U;
}

#if !defined(_WIN32)

void check_pty(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error { std::string { message } };
}

std::string hex_encode(const std::string_view value)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2U);
    for (const char byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        result.push_back(digits[character >> 4U]);
        result.push_back(digits[character & 0x0fU]);
    }
    return result;
}

char status_code(const TclConsoleReadStatus status)
{
    switch (status) {
    case TclConsoleReadStatus::submitted:
        return 'S';
    case TclConsoleReadStatus::cancelled:
        return 'C';
    case TclConsoleReadStatus::end_of_file:
        return 'E';
    case TclConsoleReadStatus::interrupted:
        return 'I';
    case TclConsoleReadStatus::error:
        return 'X';
    }
    return 'X';
}

void print_result(const std::size_t index, const fsim::app::TclConsoleReadResult& result)
{
    std::cout << "__RESULT" << index << '=' << status_code(result.status) << ':'
              << hex_encode(result.text) << '\n'
              << std::flush;
}

void write_test_diagnostic(TclConsole& console)
{
    using fsim::diagnostic::Diagnostic;
    using fsim::diagnostic::Note;
    using fsim::diagnostic::Severity;
    console.write_diagnostic(Diagnostic { Severity::warning, "PTY-WARN",
        "warning output from worker", { }, { Note { "PTY-NOTE", { } } } });
}

void pty_child_interactive(const std::filesystem::path& workspace)
{
    TclConsole::Options options;
    options.workspace_root = workspace;
    options.color_mode = fsim::diagnostic::ColorMode::always;
    options.command_complete = braces_complete;
    options.completion_query = [](const std::string_view buffer,
                                   const std::size_t cursor) {
        using namespace fsim::app::tcl_completion;
        CompletionResult result;
        result.status = CompletionStatus::ok;
        result.generations = { 1U, 1U, 1U, 1U };
        result.replace_begin_byte = 0U;
        result.replace_end_byte = buffer.size();
        if (buffer == "he" && cursor == buffer.size()) {
            result.candidates.push_back({ "he_alpha", "he_alpha", "alpha candidate",
                CandidateKind::literal });
            result.candidates.push_back({ "he_beta", "he_beta", "beta candidate",
                CandidateKind::literal });
            result.hints.push_back({ "choose a candidate" });
        }
        return result;
    };

    TclConsole console { std::move(options) };
    std::thread reporter { [&console] {
        std::this_thread::sleep_for(std::chrono::milliseconds { 600 });
        console.write_output("PTY-NOTICE\n");
        write_test_diagnostic(console);
    } };
    print_result(0U, console.read_command());
    reporter.join();

    print_result(1U, console.read_command());
    print_result(2U, console.read_command());
    print_result(3U, console.read_command());
    print_result(4U, console.read_command());
    print_result(5U, console.read_command());

    std::thread interrupter { [&console] {
        std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
        console.interrupt();
    } };
    print_result(6U, console.read_command());
    interrupter.join();
    print_result(7U, console.read_command());
}

void pty_child_redirected_stderr(const std::filesystem::path& workspace)
{
    TclConsole::Options options;
    options.workspace_root = workspace;
    options.color_mode = fsim::diagnostic::ColorMode::always;
    options.command_complete = braces_complete;
    TclConsole console { std::move(options) };
    std::thread reporter { [&console] {
        std::this_thread::sleep_for(std::chrono::milliseconds { 350 });
        console.write_output("PTY-REDIRECT-OUTPUT\n");
        write_test_diagnostic(console);
    } };
    print_result(0U, console.read_command());
    reporter.join();
}

class PtyChild final {
public:
    ~PtyChild()
    {
        if (m_master >= 0)
            ::close(m_master);
        if (m_pid > 0) {
            ::kill(m_pid, SIGKILL);
            int status = 0;
            (void)::waitpid(m_pid, &status, 0);
        }
    }

    PtyChild(const PtyChild&) = delete;
    PtyChild& operator=(const PtyChild&) = delete;
    PtyChild() = default;

    bool start(const std::filesystem::path& executable,
        const std::string_view scenario, const std::filesystem::path& workspace,
        const std::optional<std::filesystem::path>& error_file = std::nullopt)
    {
        m_master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (m_master < 0 || ::grantpt(m_master) != 0 || ::unlockpt(m_master) != 0)
            return false;
        const char* const slave_name = ::ptsname(m_master);
        if (slave_name == nullptr)
            return false;
        const std::string slave_path { slave_name };
        m_pid = ::fork();
        if (m_pid < 0)
            return false;
        if (m_pid == 0) {
            ::close(m_master);
            if (::setsid() < 0)
                _exit(120);
            const int slave = ::open(slave_path.c_str(), O_RDWR);
            if (slave < 0 || ::ioctl(slave, TIOCSCTTY, 0) != 0)
                _exit(121);
            if (::dup2(slave, STDIN_FILENO) < 0
                || ::dup2(slave, STDOUT_FILENO) < 0)
                _exit(122);
            if (error_file.has_value()) {
                const int error = ::open(error_file->c_str(),
                    O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (error < 0 || ::dup2(error, STDERR_FILENO) < 0)
                    _exit(123);
            } else if (::dup2(slave, STDERR_FILENO) < 0) {
                _exit(124);
            }
            if (slave > STDERR_FILENO)
                ::close(slave);
            ::setenv("TERM", "xterm-256color", 1);
            ::setenv("NO_COLOR", "", 1);
            const std::string scenario_copy { scenario };
            const std::string workspace_copy = workspace.string();
            ::execl(executable.c_str(), executable.c_str(), "--pty-child",
                scenario_copy.c_str(), workspace_copy.c_str(), nullptr);
            _exit(125);
        }
        struct winsize size { };
        size.ws_col = 100U;
        size.ws_row = 30U;
        (void)::ioctl(m_master, TIOCSWINSZ, &size);
        const int flags = ::fcntl(m_master, F_GETFL, 0);
        if (flags < 0 || ::fcntl(m_master, F_SETFL, flags | O_NONBLOCK) < 0)
            return false;
        return true;
    }

    bool send(const std::string_view bytes)
    {
        std::size_t written = 0U;
        while (written < bytes.size()) {
            const auto amount = ::write(m_master, bytes.data() + written,
                bytes.size() - written);
            if (amount > 0) {
                written += static_cast<std::size_t>(amount);
                continue;
            }
            if (amount < 0 && errno == EINTR)
                continue;
            return false;
        }
        return true;
    }

    bool resize(const std::uint16_t columns, const std::uint16_t rows)
    {
        struct winsize size { };
        size.ws_col = columns;
        size.ws_row = rows;
        return ::ioctl(m_master, TIOCSWINSZ, &size) == 0;
    }

    bool wait_for(const std::string_view marker, const std::size_t offset,
        const std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (m_transcript.find(marker, offset) != std::string::npos)
                return true;
            pollfd descriptor { m_master, POLLIN, 0 };
            const int ready = ::poll(&descriptor, 1, 50);
            if (ready < 0 && errno != EINTR)
                return false;
            if (ready > 0 && (descriptor.revents & POLLIN) != 0)
                drain();
            if (ready > 0 && (descriptor.revents & (POLLERR | POLLNVAL)) != 0)
                return false;
        }
        drain();
        return m_transcript.find(marker, offset) != std::string::npos;
    }

    bool wait_for_exit(const std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            drain();
            int status = 0;
            const pid_t result = ::waitpid(m_pid, &status, WNOHANG);
            if (result == m_pid) {
                m_pid = -1;
                return WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
            if (result < 0 && errno != EINTR)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
        }
        return false;
    }

    [[nodiscard]] const std::string& transcript() const noexcept
    {
        return m_transcript;
    }

private:
    void drain()
    {
        char bytes[4096];
        while (true) {
            const auto amount = ::read(m_master, bytes, sizeof(bytes));
            if (amount > 0) {
                m_transcript.append(bytes, static_cast<std::size_t>(amount));
                continue;
            }
            if (amount < 0 && errno == EINTR)
                continue;
            break;
        }
    }

    int m_master { -1 };
    pid_t m_pid { -1 };
    std::string m_transcript;
};

std::size_t find_result_offset(const std::string& transcript,
    const std::size_t index)
{
    const std::string marker = "__RESULT" + std::to_string(index) + "=";
    const auto offset = transcript.find(marker);
    return offset == std::string::npos ? offset : offset + marker.size();
}

std::string result_record(const std::size_t index, const char status,
    const std::string_view value)
{
    return "__RESULT" + std::to_string(index) + "=" + status + ":"
        + hex_encode(value);
}

void require_result(const PtyChild& child, const std::size_t index,
    const char status, const std::string_view text)
{
    const std::string marker = result_record(index, status, text);
    if (child.transcript().find(marker) != std::string::npos)
        return;
    const auto actual_offset = find_result_offset(child.transcript(), index);
    const auto line_end = child.transcript().find('\n', actual_offset);
    const auto actual = actual_offset == std::string::npos
        ? std::string { "missing" }
        : child.transcript().substr(actual_offset,
              (line_end == std::string::npos ? child.transcript().size() : line_end)
                  - actual_offset);
    throw std::runtime_error { "PTY result mismatch, expected " + marker
        + ", got " + actual };
}

bool contains_colorized_severity(const std::string& text,
    const std::string_view sgr, const std::string_view label)
{
    const std::string expected = std::string { sgr } + std::string { label };
    return text.find(expected) != std::string::npos;
}

void interactive_pty_contract_test(const std::filesystem::path& executable,
    const std::filesystem::path& workspace)
{
    PtyChild child;
    check_pty(child.start(executable, "interactive", workspace),
        "could not create a Unix PTY child");
    const auto timeout = std::chrono::seconds { 5 };
    check_pty(child.wait_for("(fsim:tcl)", 0U, timeout),
        "interactive console did not draw its prompt");
    check_pty(child.send("set name \xce\xbb"), "could not write Unicode PTY input");
    check_pty(child.resize(20U, 12U), "could not resize the interactive PTY");
    check_pty(child.wait_for("PTY-NOTICE", 0U, timeout),
        "owner-thread output was not displayed while editing");
    check_pty(child.send("\x1b[D\xce\xa9\x1b[3~\xce\xbb\x1b[1;5D\x1b[1;5C\x1b[H\x05\r"),
        "could not send Unicode navigation and editing controls");
    check_pty(child.wait_for("__RESULT0=", 0U, timeout),
        "edited Unicode command was not submitted");
    require_result(child, 0U, 'S', "set name \xce\xa9\xce\xbb");
    check_pty(contains_colorized_severity(child.transcript(), "\x1b[93m", "warning")
            || contains_colorized_severity(child.transcript(), "\x1b[33m", "warning"),
        "warning severity did not receive its diagnostic color");
    check_pty(contains_colorized_severity(child.transcript(), "\x1b[96m", "note")
            || contains_colorized_severity(child.transcript(), "\x1b[36m", "note"),
        "nested note severity did not receive its diagnostic color");

    auto cursor = find_result_offset(child.transcript(), 0U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "second interactive prompt was not drawn");
    const std::string paste = "\x1b[200~set pasted {first\r\nsecond}\x01\x1b[201~";
    check_pty(child.send(paste), "could not send bracketed multiline paste");
    std::this_thread::sleep_for(std::chrono::milliseconds { 150 });
    check_pty(child.transcript().find("__RESULT1=") == std::string::npos,
        "multiline paste submitted before a separate Enter key");
    check_pty(child.send("\x1b[A\x1b[B\r"),
        "could not navigate the pasted multiline command");
    check_pty(child.wait_for("__RESULT1=", 0U, timeout),
        "pasted multiline command was not submitted");
    require_result(child, 1U, 'S', "set pasted {first\nsecond}");

    cursor = find_result_offset(child.transcript(), 1U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "completion prompt was not drawn");
    check_pty(child.send("he\t"), "could not invoke completion");
    check_pty(child.wait_for("he_beta", cursor, timeout),
        "completion menu did not display its candidates");
    check_pty(child.send("\x1b[B\r\r"),
        "could not cycle and select a completion candidate");
    check_pty(child.wait_for("__RESULT2=", 0U, timeout),
        "completion candidate was not submitted");
    require_result(child, 2U, 'S', "he_beta");

    cursor = find_result_offset(child.transcript(), 2U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "history search prompt was not drawn");
    check_pty(child.send("\x12set name\r\r"),
        "could not invoke and accept incremental history search");
    check_pty(child.wait_for("__RESULT3=", 0U, timeout),
        "history search result was not submitted");
    require_result(child, 3U, 'S', "set name \xce\xa9\xce\xbb");

    cursor = find_result_offset(child.transcript(), 3U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "undo/redo prompt was not drawn");
    check_pty(child.send("set undo_probe 1\x1a\x19\r"),
        "could not exercise undo and redo controls");
    check_pty(child.wait_for("__RESULT4=", 0U, timeout),
        "undo/redo command was not submitted");
    require_result(child, 4U, 'S', "set undo_probe 1");

    cursor = find_result_offset(child.transcript(), 4U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "cancel prompt was not drawn");
    check_pty(child.send("\x03"), "could not send Ctrl+C cancellation");
    check_pty(child.wait_for("__RESULT5=", 0U, timeout),
        "Ctrl+C did not cancel the editor read");
    require_result(child, 5U, 'C', "");

    cursor = find_result_offset(child.transcript(), 5U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "history-search interrupt prompt was not drawn");
    check_pty(child.send("\x12"), "could not enter incremental history search");
    check_pty(child.wait_for("history search", cursor, timeout),
        "incremental history search did not open");
    check_pty(child.wait_for("__RESULT6=", 0U, timeout),
        "cross-thread interrupt did not stop history search and editor read");
    require_result(child, 6U, 'I', "");

    cursor = find_result_offset(child.transcript(), 6U);
    check_pty(child.wait_for("(fsim:tcl)", cursor, timeout),
        "EOF prompt was not drawn");
    check_pty(child.send("\x04"), "could not send Ctrl+D EOF");
    check_pty(child.wait_for("__RESULT7=", 0U, timeout),
        "Ctrl+D did not return editor EOF");
    require_result(child, 7U, 'E', "");
    check_pty(child.wait_for_exit(timeout), "interactive PTY child did not exit cleanly");

    std::ifstream history { workspace / ".fsim" / "tcl_history", std::ios::binary };
    check_pty(history.good(), "PTY console did not create persistent workspace history");
    const std::string history_text { std::istreambuf_iterator<char> { history }, { } };
    check_pty(history_text.find("set name") != std::string::npos
            && history_text.find("he_beta") != std::string::npos,
        "PTY console history omitted accepted commands");
}

void redirected_stderr_pty_test(const std::filesystem::path& executable,
    const std::filesystem::path& workspace, const std::filesystem::path& error_file)
{
    PtyChild child;
    check_pty(child.start(executable, "redirected", workspace, error_file),
        "could not create redirected-stderr PTY child");
    const auto timeout = std::chrono::seconds { 5 };
    check_pty(child.wait_for("(fsim:tcl)", 0U, timeout),
        "redirected-stderr console did not draw its prompt");
    check_pty(child.send("set redirected 1"), "could not write redirected test input");
    check_pty(child.wait_for("PTY-REDIRECT-OUTPUT", 0U, timeout),
        "normal output was not redrawn through the PTY");
    check_pty(child.send("\r"), "could not submit redirected test command");
    check_pty(child.wait_for("__RESULT0=", 0U, timeout),
        "redirected-stderr console did not submit input");
    require_result(child, 0U, 'S', "set redirected 1");
    check_pty(child.wait_for_exit(timeout), "redirected-stderr PTY child did not exit");
    std::ifstream error { error_file, std::ios::binary };
    check_pty(error.good(), "redirected stderr file was not created");
    const std::string text { std::istreambuf_iterator<char> { error }, { } };
    check_pty(text.find("warning[PTY-WARN]: warning output from worker")
                != std::string::npos
            && text.find("note: PTY-NOTE") != std::string::npos,
        "structured diagnostics were not routed to redirected stderr");
    check_pty(text.find('\033') == std::string::npos,
        "ColorMode::always inserted ANSI into redirected stderr");
}

void pty_contract_tests(const std::filesystem::path& executable)
{
    char directory_template[] = "/tmp/fsim-tcl-console-pty-XXXXXX";
    const char* const temporary_directory = ::mkdtemp(directory_template);
    check_pty(temporary_directory != nullptr,
        "could not create a temporary PTY test workspace");
    const std::filesystem::path root { temporary_directory };
    try {
        interactive_pty_contract_test(executable, root / "interactive");
        redirected_stderr_pty_test(executable, root / "redirected",
            root / "redirected-stderr.log");
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(root, cleanup_error);
        throw;
    }
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
}

#endif // !defined(_WIN32)

void redirected_input_accumulates_multiline_without_prompt()
{
    std::istringstream input { "set value {first\nsecond}\n" };
    std::ostringstream output;
    std::ostringstream error;
    TclConsole::Options options;
    options.input = &input;
    options.output = &output;
    options.error = &error;
    options.command_complete = braces_complete;
    options.persist_history = true;

    TclConsole console { std::move(options) };
    const auto result = console.read_command();

    check(result.status == TclConsoleReadStatus::submitted,
        "complete multiline command was not submitted");
    check(result.text == "set value {first\nsecond}\n",
        "multiline input changed while accumulating");
    check(output.str().empty(), "redirected input unexpectedly printed a prompt");
    check(error.str().empty(), "redirected input unexpectedly wrote an error");
}

void redirected_eof_preserves_an_incomplete_command()
{
    std::istringstream input { "set value {unfinished\n" };
    std::ostringstream output;
    std::ostringstream error;
    TclConsole::Options options;
    options.input = &input;
    options.output = &output;
    options.error = &error;
    options.command_complete = braces_complete;

    TclConsole console { std::move(options) };
    const auto result = console.read_command();

    check(result.status == TclConsoleReadStatus::end_of_file,
        "EOF while accumulating did not return EOF");
    check(result.text == "set value {unfinished\n",
        "incomplete command was lost at EOF");
    check(output.str().empty(), "redirected EOF unexpectedly printed a prompt");
}

void forced_color_does_not_add_ansi_to_redirected_diagnostics()
{
    std::istringstream input { };
    std::ostringstream output;
    std::ostringstream error;
    TclConsole::Options options;
    options.input = &input;
    options.output = &output;
    options.error = &error;
    options.color_mode = fsim::diagnostic::ColorMode::always;

    TclConsole console { std::move(options) };
    console.write_diagnostic({ fsim::diagnostic::Severity::fatal,
        "FSIM-TEST", "fatal test", { }, { } });

    check(error.str().find("fatal[FSIM-TEST]: fatal test") != std::string::npos,
        "structured diagnostic was not rendered");
    check(error.str().find('\033') == std::string::npos,
        "redirected diagnostic contained terminal control sequences");
}

} // namespace

int main(const int argc, char** argv)
{
#if !defined(_WIN32)
    if (argc >= 4 && std::string_view { argv[1] } == "--pty-child") {
        const std::filesystem::path workspace { argv[3] };
        if (std::string_view { argv[2] } == "interactive")
            pty_child_interactive(workspace);
        else if (std::string_view { argv[2] } == "redirected")
            pty_child_redirected_stderr(workspace);
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#endif
    try {
        redirected_input_accumulates_multiline_without_prompt();
        redirected_eof_preserves_an_incomplete_command();
        forced_color_does_not_add_ansi_to_redirected_diagnostics();
#if !defined(_WIN32)
        std::error_code executable_error;
        const auto executable = std::filesystem::absolute(argv[0], executable_error);
        check_pty(!executable_error, "could not resolve the PTY test executable path");
        pty_contract_tests(executable);
#endif
    } catch (const std::exception& error) {
        std::cerr << "tcl_console_test: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#endif // FSIM_HAS_TCL
