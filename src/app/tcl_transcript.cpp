/* SPDX-License-Identifier: Apache-2.0 */
#include "tcl_transcript.hpp"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>
#include <utility>

namespace fsim::app::tcl_detail {

struct TclTranscript::State {
    enum class EscapeState {
        plain,
        escape,
        control_sequence,
    };

    mutable std::mutex mutex;
    std::ofstream output;
    std::filesystem::path transcript_path;
    std::string error;
    std::string pending_escape;
    EscapeState escape_state { EscapeState::plain };
    bool is_active { false };
    bool at_line_start { true };

    void set_error(std::string message)
    {
        error = std::move(message);
        is_active = false;
        if (output.is_open())
            output.close();
    }

    void append_locked(const std::string_view text)
    {
        if (!is_active || text.empty())
            return;

        constexpr auto max_write_size = static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());
        std::size_t offset = 0U;
        while (offset < text.size()) {
            const auto count = std::min(max_write_size, text.size() - offset);
            output.write(text.data() + offset, static_cast<std::streamsize>(count));
            if (!output) {
                set_error("failed while writing the Tcl transcript");
                return;
            }
            offset += count;
        }

        for (const char character : text) {
            if (character == '\n')
                at_line_start = true;
            else if (character != '\r')
                at_line_start = false;
        }
        flush_locked();
    }

    void flush_locked()
    {
        if (!is_active || !output.is_open())
            return;
        output.flush();
        if (!output)
            set_error("failed while flushing the Tcl transcript");
    }

    void emit_pending_escape_locked()
    {
        if (!pending_escape.empty())
            append_locked(pending_escape);
        pending_escape.clear();
        escape_state = EscapeState::plain;
    }

    void finish_escape_locked()
    {
        emit_pending_escape_locked();
    }

    void append_output_locked(const std::string_view text)
    {
        std::string plain;
        plain.reserve(text.size());

        for (const char character : text) {
            const auto value = static_cast<unsigned char>(character);
            switch (escape_state) {
            case EscapeState::plain:
                if (value == 0x1bU) {
                    pending_escape.assign(1U, character);
                    escape_state = EscapeState::escape;
                } else {
                    plain.push_back(character);
                }
                break;
            case EscapeState::escape:
                pending_escape.push_back(character);
                if (character == '[') {
                    escape_state = EscapeState::control_sequence;
                } else {
                    plain.append(pending_escape);
                    pending_escape.clear();
                    escape_state = EscapeState::plain;
                }
                break;
            case EscapeState::control_sequence:
                pending_escape.push_back(character);
                if (value >= 0x40U && value <= 0x7eU) {
                    if (character != 'm')
                        plain.append(pending_escape);
                    pending_escape.clear();
                    escape_state = EscapeState::plain;
                } else if ((value < 0x20U || value > 0x3fU)
                    || pending_escape.size() > 256U) {
                    plain.append(pending_escape);
                    pending_escape.clear();
                    escape_state = EscapeState::plain;
                }
                break;
            }
        }

        append_locked(plain);
        flush_locked();
    }

    void record_command_locked(const std::string_view command)
    {
        if (!is_active)
            return;

        finish_escape_locked();
        std::string record;
        record.reserve(command.size() + 4U);
        if (!at_line_start)
            record.push_back('\n');
        record.append("> ");
        record.append(command);
        if (command.empty() || command.back() != '\n')
            record.push_back('\n');
        append_output_locked(record);
    }

    void close_locked()
    {
        if (!is_active)
            return;

        finish_escape_locked();
        flush_locked();
        if (output.is_open()) {
            output.close();
            if (output.fail() && error.empty())
                error = "failed while closing the Tcl transcript";
        }
        is_active = false;
    }
};

namespace {

    std::string error_message(const std::string_view action,
        const std::error_code& error)
    {
        std::string message { action };
        if (error)
            message.append(": ").append(error.message());
        return message;
    }

    bool ends_with_newline(const std::filesystem::path& path)
    {
        std::ifstream input { path, std::ios::binary };
        if (!input)
            return true;

        input.seekg(0, std::ios::end);
        const auto end = input.tellg();
        if (end <= std::streampos { 0 })
            return true;

        input.seekg(-1, std::ios::end);
        char last_character = 0;
        input.get(last_character);
        return !input || last_character == '\n';
    }

} // namespace

TclTranscript::TclTranscript()
    : m_state(std::make_unique<State>())
{
}

TclTranscript::~TclTranscript()
{
    stop();
}

bool TclTranscript::start(const std::filesystem::path& path, std::string& error)
{
    std::error_code filesystem_error;
    auto resolved_path = std::filesystem::absolute(path, filesystem_error);
    if (filesystem_error) {
        error = error_message("could not resolve Tcl transcript path", filesystem_error);
        std::lock_guard lock { m_state->mutex };
        m_state->error = error;
        return false;
    }
    resolved_path = resolved_path.lexically_normal();

    const auto parent = resolved_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            error = error_message("could not create Tcl transcript directory",
                filesystem_error);
            std::lock_guard lock { m_state->mutex };
            m_state->error = error;
            return false;
        }
    }

    const bool had_trailing_newline = ends_with_newline(resolved_path);
    std::ofstream new_output { resolved_path,
        std::ios::out | std::ios::app | std::ios::binary };
    if (!new_output) {
        const std::error_code open_error { errno, std::generic_category() };
        error = error_message("could not open Tcl transcript", open_error);
        std::lock_guard lock { m_state->mutex };
        m_state->error = error;
        return false;
    }

    std::lock_guard lock { m_state->mutex };
    m_state->close_locked();
    m_state->output = std::move(new_output);
    m_state->transcript_path = std::move(resolved_path);
    m_state->error.clear();
    m_state->pending_escape.clear();
    m_state->escape_state = State::EscapeState::plain;
    m_state->is_active = true;
    m_state->at_line_start = had_trailing_newline;
    error.clear();
    return true;
}

void TclTranscript::stop()
{
    std::lock_guard lock { m_state->mutex };
    m_state->close_locked();
}

bool TclTranscript::active() const
{
    std::lock_guard lock { m_state->mutex };
    return m_state->is_active;
}

std::filesystem::path TclTranscript::path() const
{
    std::lock_guard lock { m_state->mutex };
    return m_state->transcript_path;
}

std::string TclTranscript::last_error() const
{
    std::lock_guard lock { m_state->mutex };
    return m_state->error;
}

void TclTranscript::record_command(const std::string_view command)
{
    std::lock_guard lock { m_state->mutex };
    m_state->record_command_locked(command);
}

void TclTranscript::record_output(const std::string_view text)
{
    std::lock_guard lock { m_state->mutex };
    m_state->append_output_locked(text);
}

TclTranscriptTeeStreambuf::TclTranscriptTeeStreambuf(
    std::streambuf& destination,
    TclTranscript& transcript) noexcept
    : m_destination(destination)
    , m_transcript(transcript)
{
}

std::streamsize TclTranscriptTeeStreambuf::xsputn(
    const char* text,
    const std::streamsize count)
{
    if (count <= 0)
        return 0;

    const auto destination_count = m_destination.sputn(text, count);
    if (destination_count > 0) {
        m_transcript.record_output(
            std::string_view { text, static_cast<std::size_t>(destination_count) });
    }
    return destination_count;
}

TclTranscriptTeeStreambuf::int_type TclTranscriptTeeStreambuf::overflow(
    const int_type value)
{
    if (traits_type::eq_int_type(value, traits_type::eof())) {
        return sync() == 0 ? traits_type::not_eof(value) : traits_type::eof();
    }

    const char character = traits_type::to_char_type(value);
    const int_type destination_result = m_destination.sputc(character);
    if (traits_type::eq_int_type(destination_result, traits_type::eof()))
        return traits_type::eof();

    m_transcript.record_output(std::string_view { &character, 1U });
    return value;
}

int TclTranscriptTeeStreambuf::sync()
{
    const int destination_result = m_destination.pubsync();
    m_transcript.record_output({ });
    return destination_result;
}

} // namespace fsim::app::tcl_detail
