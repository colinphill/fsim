/* SPDX-License-Identifier: Apache-2.0 */
#include "../../src/app/tcl_transcript.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fsim::tests::app::tcl_transcript_test {

using fsim::app::tcl_detail::TclTranscript;
using fsim::app::tcl_detail::TclTranscriptTeeStreambuf;

void require(const bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error { message };
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        static std::atomic<unsigned> next_id { 0U };
        for (unsigned attempt = 0U; attempt < 100U; ++attempt) {
            const auto unique_id = next_id.fetch_add(1U, std::memory_order_relaxed);
            m_path = std::filesystem::temp_directory_path()
                / ("fsim-tcl-transcript-"
                    + std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count())
                    + "-" + std::to_string(unique_id));
            std::error_code error;
            if (std::filesystem::create_directory(m_path, error))
                return;
            if (error && error != std::errc::file_exists)
                throw std::runtime_error { "could not create temporary directory: "
                    + error.message() };
        }
        throw std::runtime_error { "could not allocate a unique temporary directory" };
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input { path, std::ios::binary };
    require(static_cast<bool>(input), "could not read the transcript file");
    return { std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { } };
}

void test_start_stop_and_append()
{
    TemporaryDirectory directory;
    const auto transcript_path = directory.path() / "session.log";
    TclTranscript transcript;
    require(!transcript.active(), "new transcript is unexpectedly active");

    std::string error;
    require(transcript.start(transcript_path, error),
        "transcript did not start: " + error);
    require(error.empty(), "successful start returned an error");
    require(transcript.active(), "transcript did not report active after start");
    require(transcript.path() == std::filesystem::absolute(transcript_path).lexically_normal(),
        "transcript path was not anchored at start time");
    transcript.record_command("puts first");
    transcript.stop();
    require(!transcript.active(), "transcript remained active after stop");
    require(read_file(transcript_path) == "> puts first\n",
        "first command was not recorded with a readable command marker");

    require(transcript.start(transcript_path, error),
        "transcript did not restart in append mode: " + error);
    transcript.record_command("puts second");
    transcript.stop();
    require(read_file(transcript_path) == "> puts first\n> puts second\n",
        "restarting the transcript did not append to the prior session");
    require(transcript.last_error().empty(), "successful transcript retained an error");
}

void test_multiline_commands()
{
    TemporaryDirectory directory;
    const auto transcript_path = directory.path() / "commands.log";
    TclTranscript transcript;
    std::string error;
    require(transcript.start(transcript_path, error),
        "multiline transcript did not start: " + error);
    transcript.record_command("set value {first\nsecond}");
    transcript.record_command("set next third\n");
    transcript.stop();
    require(read_file(transcript_path)
            == "> set value {first\nsecond}\n> set next third\n",
        "multiline command boundaries were not preserved");
}

void test_output_error_tee_and_sgr_stripping()
{
    TemporaryDirectory directory;
    const auto transcript_path = directory.path() / "streams.log";
    TclTranscript transcript;
    std::string error;
    require(transcript.start(transcript_path, error),
        "stream transcript did not start: " + error);

    std::ostringstream terminal_output;
    std::ostringstream terminal_error;
    TclTranscriptTeeStreambuf output_buffer { *terminal_output.rdbuf(), transcript };
    TclTranscriptTeeStreambuf error_buffer { *terminal_error.rdbuf(), transcript };
    std::ostream output { &output_buffer };
    std::ostream error_output { &error_buffer };
    const std::string colored_output { "stdout \033[31mred\033[0m done\n" };
    const std::string colored_error { "stderr \033[1;31mfatal\033[0m done\n" };
    output.write(colored_output.data(), static_cast<std::streamsize>(colored_output.size()));
    error_output.write(colored_error.data(), static_cast<std::streamsize>(colored_error.size()));
    output.flush();
    error_output.flush();
    transcript.stop();

    require(terminal_output.str() == colored_output,
        "stdout tee changed the raw terminal bytes");
    require(terminal_error.str() == colored_error,
        "stderr tee changed the raw terminal bytes");
    require(read_file(transcript_path) == "stdout red done\nstderr fatal done\n",
        "transcript did not tee output and errors without ANSI SGR codes");
}

void test_split_sgr_sequences()
{
    TemporaryDirectory directory;
    const auto transcript_path = directory.path() / "split-sgr.log";
    TclTranscript transcript;
    std::string error;
    require(transcript.start(transcript_path, error),
        "split-SGR transcript did not start: " + error);

    std::stringbuf destination;
    TclTranscriptTeeStreambuf tee_buffer { destination, transcript };
    std::ostream output { &tee_buffer };
    output.write("before \033[3", 10);
    output.write("1mred\033[0", 8);
    output.write("m after\n", 8);
    output.flush();
    transcript.stop();

    require(destination.str() == "before \033[31mred\033[0m after\n",
        "split SGR writes changed the terminal output");
    require(read_file(transcript_path) == "before red after\n",
        "split SGR sequences were not removed from the transcript");
}

void test_failed_path_reports_error()
{
    TemporaryDirectory directory;
    const auto blocker = directory.path() / "not-a-directory";
    {
        std::ofstream file { blocker };
        require(static_cast<bool>(file), "could not create failed-path fixture");
    }

    TclTranscript transcript;
    std::string error;
    require(!transcript.start(blocker / "transcript.log", error),
        "transcript unexpectedly started beneath a regular file");
    require(!error.empty(), "failed start did not return an error");
    require(transcript.last_error() == error,
        "failed start did not retain its error");
    require(!transcript.active(), "failed start left the transcript active");
}

void test_thread_safe_concurrent_writes()
{
    constexpr unsigned thread_count = 4U;
    constexpr unsigned records_per_thread = 25U;
    TemporaryDirectory directory;
    const auto transcript_path = directory.path() / "concurrent.log";
    TclTranscript transcript;
    std::string error;
    require(transcript.start(transcript_path, error),
        "concurrent transcript did not start: " + error);

    std::vector<std::thread> writers;
    writers.reserve(thread_count);
    for (unsigned thread_index = 0U; thread_index < thread_count; ++thread_index) {
        writers.emplace_back([thread_index, &transcript] {
            std::stringbuf destination;
            TclTranscriptTeeStreambuf tee_buffer { destination, transcript };
            std::ostream output { &tee_buffer };
            for (unsigned record_index = 0U; record_index < records_per_thread;
                ++record_index) {
                transcript.record_command("command-" + std::to_string(thread_index)
                    + "-" + std::to_string(record_index));
                const auto line = "output-" + std::to_string(thread_index)
                    + "-" + std::to_string(record_index) + '\n';
                output.write(line.data(), static_cast<std::streamsize>(line.size()));
            }
            output.flush();
        });
    }
    for (auto& writer : writers)
        writer.join();
    transcript.stop();

    const auto contents = read_file(transcript_path);
    for (unsigned thread_index = 0U; thread_index < thread_count; ++thread_index) {
        for (unsigned record_index = 0U; record_index < records_per_thread;
            ++record_index) {
            const auto command = "> command-" + std::to_string(thread_index)
                + "-" + std::to_string(record_index) + '\n';
            const auto output = "output-" + std::to_string(thread_index)
                + "-" + std::to_string(record_index) + '\n';
            require(contents.find(command) != std::string::npos,
                "concurrent command record was lost or interleaved");
            require(contents.find(output) != std::string::npos,
                "concurrent output record was lost or interleaved");
        }
    }
}

} // namespace fsim::tests::app::tcl_transcript_test

int main()
{
    using namespace fsim::tests::app::tcl_transcript_test;
    try {
        test_start_stop_and_append();
        test_multiline_commands();
        test_output_error_tee_and_sgr_stripping();
        test_split_sgr_sequences();
        test_failed_path_reports_error();
        test_thread_safe_concurrent_writes();
        std::cout << "Tcl transcript tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tcl_transcript_test: " << error.what() << '\n';
        return 1;
    }
}
