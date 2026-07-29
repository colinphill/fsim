// SPDX-License-Identifier: Apache-2.0
#include "plugin_compiler_internal.hpp"

namespace fsim::systemc::plugin_detail {

#if defined(_WIN32)

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view input) {
    if (input.empty()) {
        return {};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            input.data(),
            static_cast<int>(input.size()),
            result.data(),
            required)
        <= 0) {
        return {};
    }
    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(const std::wstring& argument) {
    if (!argument.empty()
        && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result{L'"'};
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

TemporaryResponseFile::TemporaryResponseFile() = default;

TemporaryResponseFile::~TemporaryResponseFile()  {
        if (!path_.empty()) {
            DeleteFileW(path_.c_str());
        }
    }

[[nodiscard]] const std::filesystem::path& TemporaryResponseFile::path() const noexcept  {
        return path_;
    }

bool TemporaryResponseFile::write(
    const CompilerCommand& command,
    const std::vector<std::wstring>& wide_arguments,
    std::string& error_message)  {
        for (std::size_t attempt = 0; attempt < 100; ++attempt) {
            const auto serial =
                response_temporary_counter.fetch_add(1, std::memory_order_relaxed);
            path_ = command.working_directory
                / (".fsim-compiler-arguments-" + std::to_string(GetCurrentProcessId())
                   + "-" + std::to_string(serial) + ".rsp");
            const HANDLE file = CreateFileW(
                path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                if (GetLastError() == ERROR_FILE_EXISTS
                    || GetLastError() == ERROR_ALREADY_EXISTS) {
                    continue;
                }
                error_message =
                    "cannot create compiler response file (error "
                    + std::to_string(GetLastError()) + ")";
                path_.clear();
                return false;
            }

            bool ok = true;
            if (command.toolchain == HostToolchain::msvc) {
                std::wstring contents;
                contents.push_back(static_cast<wchar_t>(0xfeff));
                for (std::size_t index = 1; index < wide_arguments.size(); ++index) {
                    contents += quote_windows_argument(wide_arguments[index]);
                    contents += L"\r\n";
                }
                DWORD written = 0;
                const auto byte_count =
                    static_cast<DWORD>(contents.size() * sizeof(wchar_t));
                ok = WriteFile(
                         file, contents.data(), byte_count, &written, nullptr)
                    && written == byte_count;
            } else {
                std::string contents;
                for (std::size_t index = 1; index < command.argv.size(); ++index) {
                    contents.push_back('"');
                    for (const char character : command.argv[index]) {
                        if (character == '\\' || character == '"') {
                            contents.push_back('\\');
                        }
                        contents.push_back(character);
                    }
                    contents += "\"\r\n";
                }
                DWORD written = 0;
                ok = contents.size() <= std::numeric_limits<DWORD>::max()
                    && WriteFile(
                        file,
                        contents.data(),
                        static_cast<DWORD>(contents.size()),
                        &written,
                        nullptr)
                    && written == static_cast<DWORD>(contents.size());
            }
            ok = ok && FlushFileBuffers(file);
            const auto native_error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(file);
            if (!ok) {
                error_message =
                    "cannot write compiler response file (error "
                    + std::to_string(native_error) + ")";
                DeleteFileW(path_.c_str());
                path_.clear();
                return false;
            }
            return true;
        }
        error_message = "cannot allocate a unique compiler response file";
        path_.clear();
        return false;
    }

[[nodiscard]] ProcessResult run_process(const CompilerCommand& command) {
    ProcessResult result;
    if (command.argv.empty()) {
        result.start_error = "empty compiler argument vector";
        return result;
    }

    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(command.argv.size());
    for (const auto& argument : command.argv) {
        auto wide = utf8_to_wide(argument);
        if (wide.empty() && !argument.empty()) {
            result.start_error = "compiler argument is not valid UTF-8";
            return result;
        }
        wide_arguments.push_back(std::move(wide));
    }
    std::wstring command_line;
    for (std::size_t index = 0; index < wide_arguments.size(); ++index) {
        if (index != 0) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_argument(wide_arguments[index]);
    }
    TemporaryResponseFile response_file;
    if (command_line.size() >= 30'000) {
        if (!response_file.write(command, wide_arguments, result.start_error)) {
            return result;
        }
        command_line = quote_windows_argument(wide_arguments.front());
        command_line.push_back(L' ');
        command_line +=
            quote_windows_argument(L"@" + response_file.path().wstring());
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        result.start_error = "CreatePipe failed with error " + std::to_string(GetLastError());
        return result;
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        result.start_error =
            "SetHandleInformation failed with error " + std::to_string(GetLastError());
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    HANDLE child_input = nullptr;
    const HANDLE process_handle = GetCurrentProcess();
    const HANDLE standard_input = GetStdHandle(STD_INPUT_HANDLE);
    if (standard_input != nullptr && standard_input != INVALID_HANDLE_VALUE) {
        (void)DuplicateHandle(
            process_handle,
            standard_input,
            process_handle,
            &child_input,
            0,
            TRUE,
            DUPLICATE_SAME_ACCESS);
    }
    if (child_input == nullptr) {
        child_input = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }
    if (child_input == INVALID_HANDLE_VALUE || child_input == nullptr) {
        result.start_error =
            "cannot prepare compiler standard input (error "
            + std::to_string(GetLastError()) + ")";
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    SIZE_T attribute_bytes = 0;
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    std::vector<std::byte> attribute_storage(attribute_bytes);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        attribute_storage.data());
    if (!InitializeProcThreadAttributeList(
            attributes, 1, 0, &attribute_bytes)) {
        result.start_error =
            "InitializeProcThreadAttributeList failed with error "
            + std::to_string(GetLastError());
        CloseHandle(child_input);
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }
    const std::array inherited_handles{write_pipe, child_input};
    if (!UpdateProcThreadAttribute(
            attributes,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(inherited_handles.data()),
            sizeof(inherited_handles),
            nullptr,
            nullptr)) {
        result.start_error =
            "UpdateProcThreadAttribute failed with error "
            + std::to_string(GetLastError());
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(child_input);
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return result;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input;
    startup.StartupInfo.hStdOutput = write_pipe;
    startup.StartupInfo.hStdError = write_pipe;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    auto application = wide_arguments.front();
    const auto working_directory = utf8_to_wide(path_argument(command.working_directory));
    const BOOL created = CreateProcessW(
        application.empty() ? nullptr : application.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup.StartupInfo,
        &process);
    DeleteProcThreadAttributeList(attributes);
    CloseHandle(child_input);
    CloseHandle(write_pipe);
    if (!created) {
        result.start_error =
            "CreateProcessW failed with error " + std::to_string(GetLastError());
        CloseHandle(read_pipe);
        return result;
    }
    result.started = true;

    std::array<char, 4096> buffer{};
    DWORD count = 0;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)
           && count != 0) {
        if (result.output.size() < kMaximumCompilerOutput) {
            const auto remaining = kMaximumCompilerOutput - result.output.size();
            result.output.append(buffer.data(), std::min<std::size_t>(remaining, count));
        }
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }
    CloseHandle(read_pipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

#else

[[nodiscard]] ProcessResult run_process(const CompilerCommand& command) {
    ProcessResult result;
    if (command.argv.empty()) {
        result.start_error = "empty compiler argument vector";
        return result;
    }

    // All storage required by exec is prepared before fork. The child only
    // calls async-signal-safe POSIX functions, so a multithreaded caller cannot
    // deadlock on inherited allocator or iostream locks.
    std::vector<char*> argv;
    argv.reserve(command.argv.size() + 1);
    for (const auto& argument : command.argv) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    std::array<int, 2> pipe_descriptors{};
#if defined(__linux__)
    const auto pipe_result =
        ::pipe2(pipe_descriptors.data(), O_CLOEXEC);
#else
    const auto pipe_result = ::pipe(pipe_descriptors.data());
#endif
    if (pipe_result != 0) {
        result.start_error = "pipe failed: " + std::string{std::strerror(errno)};
        return result;
    }

    const auto child = ::fork();
    if (child < 0) {
        result.start_error = "fork failed: " + std::string{std::strerror(errno)};
        ::close(pipe_descriptors[0]);
        ::close(pipe_descriptors[1]);
        return result;
    }
    if (child == 0) {
        ::close(pipe_descriptors[0]);
#if defined(__linux__)
        if (pipe_descriptors[1] == STDOUT_FILENO
            || pipe_descriptors[1] == STDERR_FILENO) {
            const auto descriptor_flags =
                ::fcntl(pipe_descriptors[1], F_GETFD);
            if (descriptor_flags < 0
                || ::fcntl(
                       pipe_descriptors[1],
                       F_SETFD,
                       descriptor_flags & ~FD_CLOEXEC)
                    < 0) {
                _exit(126);
            }
        }
#endif
        if (pipe_descriptors[1] != STDOUT_FILENO
            && ::dup2(pipe_descriptors[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        if (pipe_descriptors[1] != STDERR_FILENO
            && ::dup2(pipe_descriptors[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        if (pipe_descriptors[1] != STDOUT_FILENO
            && pipe_descriptors[1] != STDERR_FILENO) {
            ::close(pipe_descriptors[1]);
        }
        if (::chdir(command.working_directory.c_str()) != 0) {
            _exit(126);
        }

        ::execv(argv.front(), argv.data());
        _exit(127);
    }

    result.started = true;
    ::close(pipe_descriptors[1]);
    std::array<char, 4096> buffer{};
    while (true) {
        const auto count = ::read(pipe_descriptors[0], buffer.data(), buffer.size());
        if (count > 0) {
            if (result.output.size() < kMaximumCompilerOutput) {
                const auto remaining = kMaximumCompilerOutput - result.output.size();
                result.output.append(
                    buffer.data(),
                    std::min<std::size_t>(remaining, static_cast<std::size_t>(count)));
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(pipe_descriptors[0]);

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.exit_code = -1;
        return result;
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

#endif

[[nodiscard]] bool shell_display_safe(const unsigned char character) noexcept {
    return std::isalnum(character) != 0 || character == '_' || character == '-'
        || character == '.' || character == '/' || character == '\\'
        || character == ':' || character == '+' || character == '=';
}

[[nodiscard]] std::string quote_for_display(const std::string& argument) {
    if (!argument.empty()
        && std::all_of(argument.begin(), argument.end(), [](const unsigned char character) {
               return shell_display_safe(character);
           })) {
        return argument;
    }
    std::string result{"'"};
    for (const char character : argument) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result.push_back(character);
        }
    }
    result.push_back('\'');
    return result;
}



} // namespace fsim::systemc::plugin_detail
