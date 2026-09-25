/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <filesystem>
#include <memory>
#include <streambuf>
#include <string>
#include <string_view>

namespace fsim::app::tcl_detail {

class TclTranscriptTeeStreambuf;

class TclTranscript final {
public:
    TclTranscript();
    ~TclTranscript();

    TclTranscript(const TclTranscript&) = delete;
    TclTranscript& operator=(const TclTranscript&) = delete;
    TclTranscript(TclTranscript&&) = delete;
    TclTranscript& operator=(TclTranscript&&) = delete;

    [[nodiscard]] bool start(const std::filesystem::path&, std::string& error);
    void stop();
    [[nodiscard]] bool active() const;
    [[nodiscard]] std::filesystem::path path() const;
    [[nodiscard]] std::string last_error() const;
    void record_command(std::string_view command);

private:
    friend class TclTranscriptTeeStreambuf;

    void record_output(std::string_view text);

    struct State;
    std::unique_ptr<State> m_state;
};

class TclTranscriptTeeStreambuf final : public std::streambuf {
public:
    TclTranscriptTeeStreambuf(std::streambuf& destination,
        TclTranscript& transcript) noexcept;

protected:
    std::streamsize xsputn(const char* text, std::streamsize count) override;
    int_type overflow(int_type value) override;
    int sync() override;

private:
    std::streambuf& m_destination;
    TclTranscript& m_transcript;
};

} // namespace fsim::app::tcl_detail
