// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/file_binary.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <system_error>

namespace fsim::runtime::simir {

namespace {

    constexpr std::string_view multichannel_write_mode = "\x1f"
                                                         "fsim-multichannel-write";
    constexpr FileHandle file_descriptor_flag = UINT32_C(0x80000000);

    [[nodiscard]] bool below_root(
        const std::filesystem::path& root,
        const std::filesystem::path& candidate)
    {
        auto root_part = root.begin();
        auto candidate_part = candidate.begin();
        for (; root_part != root.end(); ++root_part, ++candidate_part) {
            if (candidate_part == candidate.end()
                || *root_part != *candidate_part) {
                return false;
            }
        }
        return true;
    }

    struct Mode {
        std::ios::openmode flags { };
        bool readable { };
        bool writable { };
    };

    [[nodiscard]] std::optional<Mode> file_mode(
        const std::string_view spelling)
    {
        using std::ios;
        if (spelling == "r" || spelling == "rb") {
            return Mode { ios::in | ios::binary, true, false };
        }
        if (spelling == "w" || spelling == "wb") {
            return Mode {
                ios::out | ios::trunc | ios::binary, false, true
            };
        }
        if (spelling == "a" || spelling == "ab") {
            return Mode {
                ios::out | ios::app | ios::binary, false, true
            };
        }
        if (spelling == "r+" || spelling == "r+b"
            || spelling == "rb+") {
            return Mode {
                ios::in | ios::out | ios::binary, true, true
            };
        }
        if (spelling == "w+" || spelling == "w+b"
            || spelling == "wb+") {
            return Mode {
                ios::in | ios::out | ios::trunc | ios::binary,
                true,
                true
            };
        }
        if (spelling == "a+" || spelling == "a+b"
            || spelling == "ab+") {
            return Mode {
                ios::in | ios::out | ios::app | ios::binary,
                true,
                true
            };
        }
        return std::nullopt;
    }

    [[nodiscard]] std::uint32_t vhdl_open_status(
        const std::string_view message)
    {
        return message.find("unsupported") != std::string_view::npos
                && message.find("mode") != std::string_view::npos
            ? 3U
            : 2U;
    }

} // namespace

void Interpreter::Impl::set_file_root(
    std::filesystem::path root)
{
    if (started) {
        throw std::logic_error {
            "cannot set the SimIR file root after start"
        };
    }
    if (root.empty()) {
        file_root.clear();
        vhdl_working_directory.clear();
        return;
    }
    std::error_code error;
    root = support::native_fs::weakly_canonical(root, error);
    if (error || !support::native_fs::is_directory(root, error)) {
        throw std::invalid_argument {
            "SimIR file root is not an accessible directory"
        };
    }
    file_root = std::move(root);
    vhdl_working_directory = file_root;
}

FileHandle Interpreter::Impl::open_file(
    const ProcessId process,
    const std::string_view path_text,
    const std::string_view mode_text,
    const bool vhdl)
{
    if (processes.scheduling_domain(process)
        != ProcessSchedulingDomain::systemverilog) {
        return open_file_unchecked(process, path_text, mode_text, vhdl);
    }
    // $fopen returns zero when the file cannot be opened (IEEE 1800-2017
    // 21.3.1).
    // A name outside the manifest root stays an error: file access is
    // confined to the project.
    try {
        return open_file_unchecked(process, path_text, mode_text, vhdl);
    } catch (const std::runtime_error& error) {
        const std::string_view message { error.what() };
        if (message.find("manifest root") != std::string_view::npos
            || message.find("escapes") != std::string_view::npos) {
            throw;
        }
        return 0;
    } catch (const std::length_error&) {
        return 0;
    }
}

Interpreter::Impl::FileState* Interpreter::Impl::open_systemverilog_file(
    const FileHandle handle)
{
    const auto found = files.find(handle);
    if (handle == 0 || found == files.end() || found->second.closed
        || !found->second.stream) {
        return nullptr;
    }
    return &found->second;
}

FileHandle Interpreter::Impl::open_file_unchecked(
    const ProcessId process,
    const std::string_view path_text,
    const std::string_view mode_text,
    const bool vhdl)
{
    if (files.size() >= maximum_open_file_handles) {
        throw std::length_error {
            "SimIR file-handle lifetime ceiling is exhausted"
        };
    }
    if (file_root.empty()) {
        throw std::runtime_error {
            "SimIR file access has no configured project root"
        };
    }
    const bool multichannel = mode_text == multichannel_write_mode;
    const auto mode = file_mode(multichannel ? std::string_view { "w" } : mode_text);
    if (!mode) {
        throw std::runtime_error {
            "unsupported SimIR text file mode '"
            + std::string { mode_text } + "'"
        };
    }
    std::filesystem::path relative;
    try {
        relative = fsim::support::path_from_utf8(path_text).lexically_normal();
    } catch (const std::exception&) {
        throw std::runtime_error {
            "SimIR filename is not a valid UTF-8 path"
        };
    }
    if (relative.empty() || relative.is_absolute()
        || relative.has_root_name()) {
        throw std::runtime_error {
            "SimIR filename must be manifest-root-relative"
        };
    }
    for (const auto& component : relative) {
        if (component == "..") {
            throw std::runtime_error {
                "SimIR filename escapes the manifest root"
            };
        }
    }
    // A VHDL file name is relative to the working directory that
    // STD.ENV.DIR_WORKINGDIR sets (IEEE 1076-2019 16.5.6).
    const auto joined = (vhdl && !vhdl_working_directory.empty()
                            ? vhdl_working_directory
                            : file_root)
        / relative;
    std::error_code error;
    auto checked = support::native_fs::weakly_canonical(
        mode->readable && !mode->writable
            ? joined
            : joined.parent_path(),
        error);
    if (error) {
        throw std::runtime_error {
            "cannot resolve SimIR filename '"
            + std::string { path_text } + "': " + error.message()
        };
    }
    if (mode->writable) {
        checked /= joined.filename();
    }
    if (!below_root(file_root, checked)) {
        throw std::runtime_error {
            "SimIR filename resolves outside the manifest root"
        };
    }

    auto stream = std::make_unique<std::fstream>(
        support::native_fs::open_fstream(checked, mode->flags));
    if (!stream->is_open()) {
        throw std::runtime_error {
            "cannot open SimIR text file '"
            + std::string { path_text } + "' in mode '"
            + std::string { mode_text } + "'"
        };
    }
    FileHandle handle { };
    if (multichannel) {
        if (next_multichannel_channel >= 31U) {
            // A closed channel may be opened again.
            for (std::uint32_t channel = 1; channel < 31U; ++channel) {
                const auto bit = FileHandle { 1U } << channel;
                if (const auto found = files.find(bit);
                    found != files.end() && found->second.closed) {
                    files.erase(found);
                    handle = bit;
                    break;
                }
            }
            if (handle == 0) {
                throw std::length_error {
                    "SystemVerilog multichannel descriptor space is "
                    "exhausted"
                };
            }
        } else {
            handle = FileHandle { 1U } << next_multichannel_channel++;
        }
    } else {
        if (next_file_handle == 0
            || next_file_handle >= file_descriptor_flag) {
            throw std::length_error {
                "SystemVerilog file descriptor space is exhausted"
            };
        }
        handle = file_descriptor_flag | next_file_handle++;
    }
    files.emplace(
        handle,
        FileState {
            process,
            std::move(checked),
            std::string { path_text },
            std::string { mode_text },
            std::move(stream),
            std::nullopt,
            { },
            false,
            mode->readable,
            mode->writable,
            vhdl });
    return handle;
}

Interpreter::Impl::FileState& Interpreter::Impl::checked_file(
    // File descriptors are design-wide (IEEE 1800-2017 21.3.1): any process
    // may use a file another process opened.
    const ProcessId process,
    const FileHandle handle)
{
    // A SystemVerilog operation through a descriptor that names no open
    // file reads end of file and records the error for $ferror (IEEE
    // 1800-2017 21.3).
    if (processes.scheduling_domain(process)
            == ProcessSchedulingDomain::systemverilog
        && open_systemverilog_file(handle) == nullptr) {
        auto& invalid = invalid_systemverilog_file;
        if (!invalid.stream) {
            invalid.stream = std::make_unique<std::fstream>();
        }
        invalid.stream->clear();
        invalid.stream->setstate(std::ios::eofbit);
        invalid.pushback.reset();
        invalid.closed = false;
        invalid.readable = false;
        invalid.writable = false;
        invalid.last_error = "invalid file descriptor";
        return invalid;
    }
    if (handle == 0) {
        throw std::runtime_error {
            "invalid zero SimIR file handle"
        };
    }
    const auto found = files.find(handle);
    if (found == files.end()) {
        throw std::runtime_error {
            "unknown SimIR file handle "
            + std::to_string(handle)
        };
    }
    if (found->second.closed || !found->second.stream) {
        throw std::runtime_error {
            "SimIR file handle is already closed"
        };
    }
    return found->second;
}

void Interpreter::Impl::close_file(
    const ProcessId process,
    const FileHandle handle)
{
    const auto close_one = [](FileState& file) {
        // A successful read through EOF leaves eofbit/failbit set. Clear those
        // input-status bits before close so they cannot masquerade as a close
        // failure.
        file.stream->clear();
        if (file.writable)
            file.stream->flush();
        file.stream->close();
        const bool failed = file.stream->fail();
        file.stream.reset();
        file.closed = true;
        if (failed) {
            file.last_error = "failed to close text file";
            throw std::runtime_error { file.last_error };
        }
    };
    const bool systemverilog = processes.scheduling_domain(process)
        == ProcessSchedulingDomain::systemverilog;
    if ((handle & file_descriptor_flag) != 0) {
        if (systemverilog) {
            // Closing a descriptor that names no open file does nothing
            // (IEEE 1800-2017 21.3.1).
            if (auto* file = open_systemverilog_file(handle)) {
                close_one(*file);
            }
            return;
        }
        close_one(checked_file(process, handle));
        return;
    }
    if (handle == 0 && !systemverilog) {
        static_cast<void>(checked_file(process, handle));
    }
    std::vector<FileState*> selected;
    for (std::uint32_t channel = 1; channel < 31U; ++channel) {
        const auto bit = FileHandle { 1U } << channel;
        if ((handle & bit) != 0) {
            if (systemverilog) {
                if (auto* file = open_systemverilog_file(bit)) {
                    selected.push_back(file);
                }
                continue;
            }
            selected.push_back(&checked_file(process, bit));
        }
    }
    for (auto* file : selected)
        close_one(*file);
}

void Interpreter::Impl::write_file(
    const ProcessId process,
    const FileHandle handle,
    const std::string_view text,
    const bool newline)
{
    const auto validate = [](FileState& file) {
        if (!file.writable) {
            throw std::runtime_error {
                "SimIR file handle is not open for writing"
            };
        }
    };
    const auto write_one = [&](FileState& file) {
        file.stream->write(
            text.data(), static_cast<std::streamsize>(text.size()));
        if (newline)
            file.stream->put('\n');
        file.stream->flush();
        if (!file.stream->good()) {
            file.last_error = "failed to write SimIR text file";
            throw std::runtime_error { file.last_error };
        }
    };
    const bool systemverilog = processes.scheduling_domain(process)
        == ProcessSchedulingDomain::systemverilog;
    const auto write_standard = [&](std::ostream& stream) {
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (newline)
            stream.put('\n');
        stream.flush();
    };
    if ((handle & file_descriptor_flag) != 0) {
        if (systemverilog) {
            // Descriptors 0x80000001 and 0x80000002 are the preopened
            // STDOUT and STDERR (IEEE 1800-2017 21.3.1); a write through a
            // descriptor that names no open file does nothing.
            if (handle == (file_descriptor_flag | 1U)) {
                write_standard(std::cout);
                return;
            }
            if (handle == (file_descriptor_flag | 2U)) {
                write_standard(std::cerr);
                return;
            }
            if (auto* file = open_systemverilog_file(handle)) {
                validate(*file);
                write_one(*file);
            }
            return;
        }
        auto& file = checked_file(process, handle);
        validate(file);
        write_one(file);
        return;
    }
    if (handle == 0) {
        if (systemverilog) {
            // A zero multichannel descriptor selects no file.
            return;
        }
        throw std::runtime_error {
            "invalid zero SystemVerilog multichannel descriptor"
        };
    }
    std::vector<FileState*> selected;
    for (std::uint32_t channel = 1; channel < 31U; ++channel) {
        const auto bit = FileHandle { 1U } << channel;
        if ((handle & bit) != 0) {
            if (systemverilog) {
                if (auto* file = open_systemverilog_file(bit)) {
                    validate(*file);
                    selected.push_back(file);
                }
                continue;
            }
            auto& file = checked_file(process, bit);
            validate(file);
            selected.push_back(&file);
        }
    }
    if ((handle & 1U) != 0) {
        std::cout.write(
            text.data(), static_cast<std::streamsize>(text.size()));
        if (newline)
            std::cout.put('\n');
        std::cout.flush();
        if (!std::cout.good()) {
            throw std::runtime_error {
                "failed to write SystemVerilog standard output"
            };
        }
    }
    for (auto* file : selected)
        write_one(*file);
}

bool Interpreter::Impl::soft_unreadable_file(
    const ProcessId process, FileState& file)
{
    // A SystemVerilog read of a file not opened for reading is an error
    // the call reports through its result (EOF) and $ferror (IEEE
    // 1800-2017 21.3.4, 21.3.7), not a simulation failure.
    if (processes.scheduling_domain(process)
        != ProcessSchedulingDomain::systemverilog) {
        return false;
    }
    file.last_error = "file is not open for reading";
    return true;
}

std::string Interpreter::Impl::read_file_line(
    const ProcessId process,
    const FileHandle handle,
    std::uint32_t& count)
{
    auto& file = checked_file(process, handle);
    // $ferror reports the most recent operation (IEEE 1800-2017 21.3.7).
    if (&file != &invalid_systemverilog_file) {
        file.last_error.clear();
    }
    if (!file.readable && soft_unreadable_file(process, file)) {
        count = 0;
        return { };
    }
    if (!file.readable) {
        throw std::runtime_error {
            "SimIR file handle is not open for reading"
        };
    }
    std::string line;
    line.reserve(128);
    const auto maximum_line_bytes = file.vhdl
        ? static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())
        : maximum_string_bytes;
    while (line.size() < maximum_line_bytes) {
        const auto character = read_file_character(process, handle);
        if (character < 0) {
            if (file.stream->eof()) {
                count = static_cast<std::uint32_t>(line.size());
                return line;
            }
            count = 0;
            return { };
        }
        line.push_back(static_cast<char>(character));
        if (character == '\n') {
            count = static_cast<std::uint32_t>(line.size());
            return line;
        }
    }
    if (file.vhdl) {
        const auto extra = read_file_character(process, handle);
        if (extra < 0) {
            if (file.stream->eof()) {
                count = static_cast<std::uint32_t>(line.size());
                return line;
            }
            count = 0;
            return { };
        }
        throw std::length_error {
            "VHDL file line exceeds the 32-bit byte-count limit"
        };
    }
    const auto extra = read_file_character(process, handle);
    if (extra < 0) {
        if (file.stream->eof()) {
            count = static_cast<std::uint32_t>(line.size());
            return line;
        }
        count = 0;
        return { };
    }
    auto character = extra;
    while (character >= 0 && character != '\n') {
        character = read_file_character(process, handle);
    }
    file.last_error = "SystemVerilog input line exceeds the 4096-byte limit";
    count = 0;
    return { };
}

std::int32_t Interpreter::Impl::read_file_character(
    const ProcessId process,
    const FileHandle handle)
{
    auto& file = checked_file(process, handle);
    // $ferror reports the most recent operation (IEEE 1800-2017 21.3.7).
    if (&file != &invalid_systemverilog_file) {
        file.last_error.clear();
    }
    if (file.pushback) {
        const auto character = *file.pushback;
        file.pushback.reset();
        return character;
    }
    if (!file.readable && soft_unreadable_file(process, file)) {
        return -1;
    }
    if (!file.readable) {
        throw std::runtime_error {
            "SimIR file handle is not open for reading"
        };
    }
    const auto character = file.stream->get();
    if (character != std::char_traits<char>::eof()) {
        return static_cast<unsigned char>(character);
    }
    if (!file.stream->eof()) {
        file.last_error = "failed to read SimIR text file character";
    }
    return -1;
}

std::int32_t Interpreter::Impl::unread_file_character(
    const ProcessId process,
    const FileHandle handle,
    const std::int32_t character)
{
    auto& file = checked_file(process, handle);
    if (&file == &invalid_systemverilog_file) {
        return -1;
    }
    file.last_error.clear();
    // $ungetc pushes back one character whatever the file's mode.
    if (!file.readable
        && processes.scheduling_domain(process)
            != ProcessSchedulingDomain::systemverilog) {
        throw std::runtime_error {
            "SimIR file handle is not open for reading"
        };
    }
    if (file.pushback || character < 0 || character > 255) {
        return -1;
    }
    if (file.readable) {
        file.stream->clear();
    }
    file.pushback = static_cast<std::uint8_t>(character);
    return character;
}

bool Interpreter::Impl::file_end_of_file(
    const ProcessId process,
    const FileHandle handle)
{
    const auto& file = checked_file(process, handle);
    return !file.pushback && file.stream->eof();
}

std::string Interpreter::Impl::file_error(
    const ProcessId process,
    const FileHandle handle,
    bool& has_error)
{
    auto& file = checked_file(process, handle);
    has_error = !file.last_error.empty();
    return file.last_error;
}

std::int32_t Interpreter::Impl::position_file(
    const ProcessId process,
    const FileHandle handle,
    const FilePositionKind kind,
    std::int32_t offset,
    std::int32_t origin,
    const bool lines)
{
    if (kind == FilePositionKind::state) {
        // STATE_OPEN is 0 and STATE_CLOSED 1 (IEEE 1076-2019 16.3).
        const auto found = handle == 0 ? files.end() : files.find(handle);
        return found == files.end() || found->second.closed
                || !found->second.stream
            ? 1
            : 0;
    }
    if (kind >= FilePositionKind::position
        || (lines && kind == FilePositionKind::seek)) {
        return position_vhdl_file(
            process, handle, kind, offset, origin, lines);
    }
    auto& file = checked_file(process, handle);
    // $ferror reports the most recent operation (IEEE 1800-2017 21.3.7).
    if (&file != &invalid_systemverilog_file) {
        file.last_error.clear();
    }
    const auto fail = [&](const std::string_view message) {
        file.last_error = message;
        return std::int32_t { -1 };
    };
    if (kind == FilePositionKind::tell) {
        const auto position = file.readable
            ? file.stream->tellg()
            : file.stream->tellp();
        if (position == std::streampos { -1 })
            return fail("failed to query SystemVerilog file position");
        auto value = static_cast<std::streamoff>(position);
        if (file.pushback)
            --value;
        if (value < 0 || value > std::numeric_limits<std::int32_t>::max())
            return fail("SystemVerilog file position exceeds 32-bit range");
        return static_cast<std::int32_t>(value);
    }
    if (kind == FilePositionKind::rewind) {
        offset = 0;
        origin = 0;
    }
    std::ios::seekdir direction;
    if (origin == 0)
        direction = std::ios::beg;
    else if (origin == 1)
        direction = std::ios::cur;
    else if (origin == 2)
        direction = std::ios::end;
    else
        return fail("$fseek origin must be 0, 1, or 2");
    file.stream->clear();
    if (file.writable)
        file.stream->flush();
    file.pushback.reset();
    if (file.readable)
        file.stream->seekg(offset, direction);
    else
        file.stream->seekp(offset, direction);
    if (!file.stream->good()) {
        if (file.vhdl)
            throw std::runtime_error { "failed to seek VHDL file" };
        return fail("failed to seek SystemVerilog file");
    }
    return 0;
}

std::int32_t Interpreter::Impl::position_vhdl_file(
    const ProcessId process,
    const FileHandle handle,
    const FilePositionKind kind,
    const std::int32_t offset,
    const std::int32_t origin,
    const bool lines)
{
    // Each is an error on a file that is not open (IEEE 1076-2019 5.5.2).
    auto& file = checked_file(process, handle);
    const auto fail = [](const std::string_view message) -> std::int32_t {
        throw std::runtime_error { std::string { message } };
    };
    const auto checked = [&](const std::int64_t value) {
        if (value < 0 || value > std::numeric_limits<std::int32_t>::max())
            fail("VHDL file position exceeds the INTEGER range");
        return static_cast<std::int32_t>(value);
    };
    if (kind == FilePositionKind::canseek)
        return 1;
    if (kind == FilePositionKind::mode) {
        // FILE_OPEN_KIND: READ_MODE, WRITE_MODE, APPEND_MODE,
        // READ_WRITE_MODE (16.3).
        if (file.mode.starts_with("a"))
            return 2;
        if (file.mode.find('+') != std::string::npos)
            return 3;
        return file.mode.starts_with("w") ? 1 : 0;
    }
    file.stream->clear();
    if (file.writable)
        file.stream->flush();
    const auto byte_position = position_file(
        process, handle, FilePositionKind::tell, 0, 0);
    if (byte_position < 0)
        fail("failed to query VHDL file position");
    // Element starts: byte offsets, one per element, then the file size.
    // A typed file holds one element per line; other files one per byte.
    std::vector<std::int64_t> starts;
    std::error_code error;
    const auto size_bytes = std::filesystem::file_size(file.path, error);
    if (error)
        fail("failed to query VHDL file size");
    if (lines) {
        std::ifstream content { file.path, std::ios::binary };
        std::int64_t at { };
        bool line_open { };
        for (char character; content.get(character); ++at) {
            if (!line_open) {
                starts.push_back(at);
                line_open = true;
            }
            if (character == '\n')
                line_open = false;
        }
    }
    const auto byte_size = static_cast<std::int64_t>(size_bytes);
    const auto size = lines
        ? static_cast<std::int64_t>(starts.size())
        : byte_size;
    const auto element_of = [&](const std::int64_t byte) -> std::int64_t {
        if (!lines)
            return byte;
        return std::ranges::upper_bound(starts, byte - 1) - starts.begin();
    };
    const auto byte_of = [&](const std::int64_t element) -> std::int64_t {
        if (!lines)
            return element;
        return element < size ? starts[static_cast<std::size_t>(element)]
                              : byte_size;
    };
    if (kind == FilePositionKind::size)
        return checked(size);
    const auto position = element_of(byte_position);
    // FILE_ORIGIN_KIND: FILE_ORIGIN_BEGIN, FILE_ORIGIN_CURRENT,
    // FILE_ORIGIN_END (16.3).
    if (origin < 0 || origin > 2)
        fail("VHDL file origin must be FILE_ORIGIN_BEGIN, "
             "FILE_ORIGIN_CURRENT or FILE_ORIGIN_END");
    if (kind == FilePositionKind::position) {
        return checked(origin == 0 ? position
                : origin == 1      ? 0
                                   : size - position);
    }
    const auto base = origin == 0 ? 0 : origin == 1 ? position : size;
    const auto target = base + offset;
    if (kind == FilePositionKind::seek) {
        if (target < 0 || target > size)
            fail("FILE_SEEK moves outside the file");
        return position_file(process, handle, FilePositionKind::seek,
            checked(byte_of(target)), 0);
    }
    if (!file.writable)
        fail("FILE_TRUNCATE on a file opened in READ_MODE");
    if (target < 0)
        fail("FILE_TRUNCATE size is negative");
    if (lines && target > size)
        fail("FILE_TRUNCATE cannot add elements to a typed file");
    const auto resized = byte_of(target);
    std::filesystem::resize_file(
        file.path, static_cast<std::uintmax_t>(resized), error);
    if (error)
        fail("failed to resize VHDL file");
    // Discard buffered input and keep the position within the file.
    (void)position_file(process, handle, FilePositionKind::seek,
        checked(std::min<std::int64_t>(byte_position, resized)), 0);
    return 0;
}

void Interpreter::Impl::flush_file(
    const ProcessId process,
    const std::optional<FileHandle> handle)
{
    const auto flush = [](FileState& file) {
        if (!file.writable)
            return;
        file.stream->flush();
        if (!file.stream->good()) {
            file.last_error = "failed to flush SystemVerilog file";
            throw std::runtime_error { file.last_error };
        }
    };
    if (handle) {
        if ((*handle & file_descriptor_flag) != 0) {
            flush(checked_file(process, *handle));
            return;
        }
        if (*handle == 0) {
            if (processes.scheduling_domain(process)
                == ProcessSchedulingDomain::systemverilog) {
                return;
            }
            throw std::runtime_error {
                "invalid zero SystemVerilog multichannel descriptor"
            };
        }
        std::vector<FileState*> selected;
        for (std::uint32_t channel = 1; channel < 31U; ++channel) {
            const auto bit = FileHandle { 1U } << channel;
            if ((*handle & bit) != 0) {
                selected.push_back(&checked_file(process, bit));
            }
        }
        if ((*handle & 1U) != 0)
            std::cout.flush();
        for (auto* file : selected)
            flush(*file);
        return;
    }
    // $fflush with no argument flushes every open file (IEEE 1800-2017
    // 21.3.6), whichever process opened it.
    for (auto& [id, file] : files) {
        static_cast<void>(id);
        if (!file.closed && file.stream)
            flush(file);
    }
}

FileHandle Interpreter::Impl::known_file_handle(
    ProcessState& process,
    const RegisterId handle_register)
{
    const auto value = process.executor
        ? process.executor->read_register(handle_register, 32U)
        : get_register(process, handle_register);
    const auto word = value.low_word();
    // A SystemVerilog descriptor with unknown bits selects no file (IEEE
    // 1800-2017 21.3.1).
    if (word.width != 0 && word.width <= 32 && word.bval != 0
        && processes.scheduling_domain(process.id)
            == ProcessSchedulingDomain::systemverilog) {
        return 0;
    }
    if (word.width == 0 || word.width > 32
        || word.bval != 0
        || word.aval
            > std::numeric_limits<FileHandle>::max()) {
        throw InterpreterError {
            process.id,
            process.pc,
            "file handle must be a known 32-bit integral value"
        };
    }
    return static_cast<FileHandle>(word.aval);
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileOpen& operation)
{
    try {
        if (operation.vhdl
            && known_file_handle(process, operation.destination) != 0) {
            if (!operation.status) {
                throw std::runtime_error {
                    "VHDL file object is already open"
                };
            }
            get_register(process, *operation.status) = PackedLogic4::from_aval_bval(2, 1, 0);
            ++process.pc;
            return;
        }
        const auto handle = open_file(
            process.id,
            get_string_register(process, operation.path),
            get_string_register(process, operation.mode),
            operation.vhdl);
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, handle, 0);
        if (operation.status) {
            get_register(process, *operation.status) = PackedLogic4::from_aval_bval(2, 0, 0);
        }
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        if (operation.status) {
            get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, 0, 0);
            get_register(process, *operation.status) = PackedLogic4::from_aval_bval(
                2, vhdl_open_status(error.what()), 0);
            ++process.pc;
            return;
        }
        // $fopen returns zero when the file cannot be opened (IEEE
        // 1800-2017 21.3.1). A name outside the file root, or handle
        // exhaustion, still stops the run.
        if (!operation.vhdl
            && std::string_view { error.what() }.starts_with(
                "cannot open SimIR text file")) {
            get_register(process, operation.destination)
                = PackedLogic4::from_aval_bval(32, 0, 0);
            ++process.pc;
            return;
        }
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileClose& operation)
{
    try {
        const auto handle = known_file_handle(process, operation.handle);
        if (handle != 0) {
            close_file(process.id, handle);
        } else if (!operation.ignore_zero) {
            if (operation.clear_handle) {
                throw std::runtime_error { "VHDL file object is not open" };
            }
            close_file(process.id, handle);
        }
        if (operation.clear_handle) {
            get_register(process, operation.handle) = PackedLogic4::from_aval_bval(32, 0, 0);
        }
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileWriteLiteral& operation)
{
    try {
        write_file(
            process.id,
            known_file_handle(process, operation.handle),
            operation.text,
            operation.newline);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileWriteFormatted& operation)
{
    try {
        const auto& value = get_register(process, operation.source);
        std::string text;
        // An integral %t argument is in its module's time unit (IEEE
        // 1800-2017 21.2.1.3).
        const bool integral_time = operation.format == OutputFormat::time
            && (operation.scalar_kind == SystemVerilogScalarKind::None
                || (operation.scalar_kind != SystemVerilogScalarKind::Chandle
                    && operation.time_unit_femtoseconds != 0U));
        const auto integral_tick = integral_time
            ? integral_time_ticks(value, operation.signed_decimal,
                  operation.time_unit_femtoseconds, time_format,
                  operation.scalar_kind)
            : std::nullopt;
        if (integral_time && !integral_tick) {
            text = make_formatted_output(
                operation.prefix,
                operation.suffix,
                OutputFormat::decimal,
                value,
                operation.signed_decimal,
                operation.suppress_leading_zero,
                operation.minimum_width,
                operation.left_justify,
                operation.zero_pad);
        } else if (operation.format == OutputFormat::time) {
            const auto decoded = integral_tick
                ? decltype(decode_systemverilog_scalar_payload(
                      value, operation.scalar_kind)) { }
                : decode_systemverilog_scalar_payload(
                      value, operation.scalar_kind);
            const auto tick = integral_tick
                ? integral_tick
                : decoded ? decoded.value.as_time() : std::nullopt;
            if (!tick) {
                throw std::runtime_error {
                    "invalid time payload for formatted file output"
                };
            }
            text = make_time_output(
                operation.prefix,
                operation.suffix,
                *tick,
                time_format,
                operation.minimum_width == 0
                    && !operation.suppress_leading_zero,
                operation.minimum_width,
                operation.left_justify,
                operation.zero_pad);
        } else {
            text = make_formatted_output(
                operation.prefix,
                operation.suffix,
                operation.format,
                value,
                operation.signed_decimal,
                operation.suppress_leading_zero,
                operation.minimum_width,
                operation.left_justify,
                operation.zero_pad,
                operation.scalar_kind,
                operation.precision);
        }
        write_file(
            process.id,
            known_file_handle(process, operation.handle),
            text,
            operation.newline);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileWriteString& operation)
{
    try {
        write_file(
            process.id,
            known_file_handle(process, operation.handle),
            operation.prefix
                + get_string_register(process, operation.source)
                + operation.suffix,
            operation.newline);
        if (operation.clear_source) {
            get_string_register(process, operation.source).clear();
        }
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileReadLine& operation)
{
    try {
        std::int32_t result { };
        const auto handle = known_file_handle(process, operation.handle);
        if (operation.kind == FileReadKind::line) {
            std::uint32_t count { };
            auto line = read_file_line(process.id, handle, count);
            if (operation.vhdl_textio) {
                if (count == 0) {
                    throw std::runtime_error { "VHDL readline reached end of file" };
                }
                if (!line.empty() && line.back() == '\n')
                    line.pop_back();
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
            }
            // A string target holds exactly the characters read, so a read
            // at end of file leaves it empty (IEEE 1800-2023 21.3.4.2); a
            // failed read leaves it unchanged.
            if (count == 0
                && operation.target_kind == FileTextTargetKind::string_register
                && checked_file(process.id, handle).last_error.empty()) {
                get_string_register(process, operation.target).clear();
            }
            if (count != 0) {
                switch (operation.target_kind) {
                case FileTextTargetKind::string_register:
                    get_string_register(process, operation.target) = std::move(line);
                    break;
                case FileTextTargetKind::packed_register:
                    get_register(process, operation.target) = pack_file_text(line, operation.target_width);
                    break;
                case FileTextTargetKind::packed_signal:
                    commit_driver(
                        process.id, operation.target,
                        pack_file_text(line, operation.target_width));
                    break;
                }
            }
            result = static_cast<std::int32_t>(count);
        } else if (operation.kind == FileReadKind::character) {
            result = read_file_character(process.id, handle);
        } else {
            const auto character = get_register(process, operation.source).low_word();
            if (character.bval == 0) {
                const auto requested = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(character.aval));
                result = requested >= 0 && requested <= 255
                        && unread_file_character(
                               process.id, handle, requested)
                            == requested
                    ? 0
                    : -1;
            } else {
                result = -1;
            }
        }
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(result), 0);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileEndOfFile& operation)
{
    try {
        const auto handle = known_file_handle(process, operation.handle);
        bool eof { };
        if (operation.lookahead) {
            const auto character = read_file_character(
                process.id, handle);
            eof = character < 0;
            if (!eof && unread_file_character(process.id, handle, character) != character) {
                throw std::runtime_error {
                    "VHDL endfile could not preserve lookahead"
                };
            }
        } else {
            eof = file_end_of_file(process.id, handle);
        }
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            operation.lookahead ? 1U : 32U,
            eof ? 1U : 0U, 0);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process,
    const FileErrorStatus& operation)
{
    try {
        bool has_error { };
        auto message = file_error(
            process.id,
            known_file_handle(process, operation.handle),
            has_error);
        switch (operation.target_kind) {
        case FileTextTargetKind::string_register:
            get_string_register(process, operation.target) = std::move(message);
            break;
        case FileTextTargetKind::packed_register:
            get_register(process, operation.target) = pack_file_text(message, operation.target_width);
            break;
        case FileTextTargetKind::packed_signal:
            commit_driver(
                process.id, operation.target,
                pack_file_text(message, operation.target_width));
            break;
        }
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            32, has_error ? 1U : 0U, 0);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError {
            process.id, process.pc, error.what()
        };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FileScan& operation)
{
    try {
        InputScanResult scanned;
        const auto active_time_format = time_format;
        auto effective_operation = operation;
        if (!size_vhdl_textio_string_reads(effective_operation,
                [&](const InputScanTarget& target)
                    -> std::optional<std::size_t> {
                    if (target.kind == InputScanTargetKind::string_register) {
                        return get_string_register(process, target.id).size();
                    }
                    if (target.kind == InputScanTargetKind::string_object) {
                        return get_string_object(target.id)
                            .initial_value.size();
                    }
                    return std::nullopt;
                })) {
            throw std::length_error {
                "VHDL TextIO STRING target exceeds scan limit"
            };
        }
        // An invalid run-time format, or one whose conversions do not match
        // the targets, assigns nothing.
        const bool format_valid = !operation.runtime_format
            || apply_runtime_scan_format(effective_operation,
                get_string_register(process, *operation.runtime_format));
        if (!format_valid) {
        } else if (operation.string_source) {
            auto& source = get_string_register(process, operation.source);
            scanned = scan_formatted_string(
                effective_operation, source, active_time_format);
            complete_plusarg_value(effective_operation, source, scanned);
            consume_scanned_source(operation, source, scanned);
        } else {
            const auto handle = known_file_handle(process, operation.handle);
            const std::function<std::int32_t()> read = [&] {
                return read_file_character(process.id, handle);
            };
            const std::function<void(std::int32_t)> unread = [&](const auto value) {
                if (unread_file_character(process.id, handle, value) != value) {
                    throw std::runtime_error { "failed to preserve file scan lookahead" };
                }
            };
            scanned = scan_formatted_input(
                effective_operation, read, unread, active_time_format);
        }
        const bool complete = format_valid
            && file_scan_complete(effective_operation, scanned);
        if (operation.success) {
            get_register(process, *operation.success) = PackedLogic4::from_aval_bval(1, complete ? 1U : 0U, 0);
        }
        if (operation.require_assignments && !complete) {
            throw std::runtime_error {
                "VHDL file read did not convert the requested element"
            };
        }
        for (std::size_t index = 0; index < scanned.values.size(); ++index) {
            if (!scanned.values[index])
                continue;
            const auto& target = effective_operation.conversions[index].target;
            auto& value = *scanned.values[index];
            switch (target.kind) {
            case InputScanTargetKind::packed_register:
                get_register(process, target.id) = std::move(value.packed);
                break;
            case InputScanTargetKind::packed_signal:
                commit_driver(process.id, target.id, std::move(value.packed));
                break;
            case InputScanTargetKind::string_register:
                get_string_register(process, target.id) = std::move(value.text);
                break;
            case InputScanTargetKind::string_object:
                get_string_object(target.id).initial_value = std::move(value.text);
                break;
            }
        }
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(scanned.assignments), 0);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError { process.id, process.pc, error.what() };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FileBinaryRead& operation)
{
    try {
        const auto known_integer = [&](const RegisterId id) {
            const auto word = get_register(process, id).low_word();
            if (word.width != 32 || word.bval != 0) {
                throw std::runtime_error { "$fread bounds must be known 32-bit integers" };
            }
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(word.aval));
        };
        std::optional<ContainerValue> container;
        if (operation.target_kind == FileBinaryTargetKind::container_register) {
            container = read_container_register(process, operation.target);
        } else if (operation.target_kind
            == FileBinaryTargetKind::container_object) {
            container = read_container_object_value(operation.target);
        }
        const auto handle = known_file_handle(process, operation.handle);
        const std::function<std::int32_t()> read = [&] {
            return read_file_character(process.id, handle);
        };
        auto result = read_binary_file(
            operation, std::move(container),
            operation.has_start
                ? std::optional { known_integer(operation.start) }
                : std::nullopt,
            operation.has_count
                ? std::optional { known_integer(operation.count) }
                : std::nullopt,
            read);
        switch (operation.target_kind) {
        case FileBinaryTargetKind::packed_register:
            get_register(process, operation.target) = std::move(result.packed);
            break;
        case FileBinaryTargetKind::packed_signal:
            commit_driver(
                process.id, operation.target, std::move(result.packed));
            break;
        case FileBinaryTargetKind::container_register:
            get_container_register(process, operation.target) = std::move(*result.container);
            break;
        case FileBinaryTargetKind::container_object:
            write_container_object_value(
                operation.target, *result.container, process.id);
            break;
        }
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(32, result.bytes, 0);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError { process.id, process.pc, error.what() };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FilePosition& operation)
{
    try {
        const auto known_integer = [&](const RegisterId id) {
            const auto word = get_register(process, id).low_word();
            if (word.width != 32 || word.bval != 0)
                throw std::runtime_error {
                    "file position operands must be known 32-bit integers"
                };
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(word.aval));
        };
        const auto result = position_file(
            process.id,
            known_file_handle(process, operation.handle),
            operation.kind,
            file_position_uses_operands(operation.kind)
                ? known_integer(operation.offset)
                : 0,
            file_position_uses_operands(operation.kind)
                ? known_integer(operation.origin)
                : 0,
            operation.lines);
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            32, static_cast<std::uint32_t>(result), 0);
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError { process.id, process.pc, error.what() };
    }
}

void Interpreter::Impl::execute_file(
    ProcessState& process, const FileFlush& operation)
{
    try {
        flush_file(
            process.id,
            operation.all
                ? std::nullopt
                : std::optional { known_file_handle(process, operation.handle) });
        ++process.pc;
    } catch (const InterpreterError&) {
        throw;
    } catch (const std::exception& error) {
        throw InterpreterError { process.id, process.pc, error.what() };
    }
}

} // namespace fsim::runtime::simir
