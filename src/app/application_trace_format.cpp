// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_trace_control.hpp"
#include "application_trace_observation.hpp"

#include "fsim/runtime/fst_writer.hpp"
#include "fsim/support/path.hpp"

#include <cctype>
#include <charconv>
#include <system_error>

namespace fsim::app::application_detail {
namespace {

    [[nodiscard]] std::string lowercase(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char byte) {
            return static_cast<char>(std::tolower(byte));
        });
        return value;
    }

    [[nodiscard]] std::runtime_error trace_error(
        const std::string& action,
        const std::error_code& error)
    {
        return std::runtime_error { action + ": " + error.message() };
    }

    void discard_staging(TraceState& state) noexcept
    {
        state.writer.reset();
        state.fst_writer.reset();
        if (state.stream.is_open()) {
            state.stream.close();
        }
        std::error_code error;
        if (!state.staging_path.empty()) {
            std::filesystem::remove(state.staging_path, error);
        }
        error.clear();
        const auto backup_exists = !state.backup_path.empty()
            && std::filesystem::exists(state.backup_path, error);
        if (!backup_exists && !state.lock_directory.empty()) {
            error.clear();
            std::filesystem::remove(state.lock_directory, error);
        }
    }

    void publish_staging(TraceState& state)
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(
            state.output_path, error);
        if (error
            && error != std::errc::no_such_file_or_directory) {
            throw trace_error("cannot inspect trace destination", error);
        }
        error.clear();
        const auto had_existing
            = status.type() != std::filesystem::file_type::not_found;
        if (had_existing) {
            if (!std::filesystem::is_regular_file(status)) {
                throw std::runtime_error {
                    "trace destination is not a regular file"
                };
            }
            state.backup_path = state.lock_directory / "previous";
            std::filesystem::rename(
                state.output_path, state.backup_path, error);
            if (error) {
                throw trace_error("cannot preserve previous trace", error);
            }
        }

        std::filesystem::rename(
            state.staging_path, state.output_path, error);
        if (error) {
            const auto publish_message = error.message();
            if (had_existing) {
                std::error_code restore_error;
                std::filesystem::rename(
                    state.backup_path, state.output_path, restore_error);
                if (restore_error) {
                    throw std::runtime_error {
                        "cannot publish trace: " + publish_message
                        + "; previous trace remains at '"
                        + support::path_to_utf8(state.backup_path) + "'"
                    };
                }
                state.backup_path.clear();
            }
            throw std::runtime_error { "cannot publish trace: " + publish_message };
        }

        if (had_existing) {
            std::filesystem::remove(state.backup_path, error);
            if (error) {
                const auto cleanup_message = error.message();
                std::error_code remove_error;
                std::filesystem::remove(state.output_path, remove_error);
                std::error_code restore_error;
                std::filesystem::rename(
                    state.backup_path, state.output_path, restore_error);
                if (remove_error || restore_error) {
                    throw std::runtime_error {
                        "cannot complete trace replacement cleanup: "
                        + cleanup_message
                    };
                }
                state.backup_path.clear();
                throw std::runtime_error {
                    "cannot remove staged trace backup: " + cleanup_message
                };
            }
            state.backup_path.clear();
        }
        std::filesystem::remove(state.lock_directory, error);
        if (error) {
            throw trace_error("cannot release trace output lock", error);
        }
    }

} // namespace

void fail_trace(
    TraceState& state,
    const std::string_view message,
    diagnostic::Engine* diagnostics) noexcept
{
    if (state.terminal_status == TraceTerminalStatus::complete) {
        return;
    }
    if (state.terminal_status != TraceTerminalStatus::failed) {
        state.terminal_status = TraceTerminalStatus::failed;
        try {
            state.terminal_diagnostic.assign(message);
        } catch (...) {
        }
    }
    if (diagnostics && !state.terminal_diagnostic_reported) {
        state.terminal_diagnostic_reported = true;
        try {
            diagnostics->error(
                state.format == project::TraceFormat::vcd
                    ? "FSIM-VCD-0003"
                    : "FSIM-TRACE-0003",
                state.terminal_diagnostic.empty()
                    ? std::string { message }
                    : state.terminal_diagnostic);
        } catch (...) {
        }
    }
    discard_staging(state);
}

TraceState::~TraceState()
{
    if (simulation && uvm_activity_observer != 0) {
        simulation->remove_uvm_activity_hook(uvm_activity_observer);
    }
    if (terminal_status == TraceTerminalStatus::open) {
        fail_trace(*this,
            "trace lifecycle ended without a clean close", diagnostics);
    } else if (terminal_status == TraceTerminalStatus::failed) {
        discard_staging(*this);
    }
}

std::optional<project::TraceFormat> resolve_trace_format(
    const project::RunSection& run,
    diagnostic::Engine& diagnostics)
{
    if (!run.trace_file) {
        return std::nullopt;
    }
    const auto extension = lowercase(
        support::path_to_utf8(run.trace_file->extension()));
    if (run.trace_format == project::TraceFormat::automatic) {
        return extension == ".fst"
            ? project::TraceFormat::fst
            : project::TraceFormat::vcd;
    }
    const auto conflict
        = (run.trace_format == project::TraceFormat::vcd && extension == ".fst")
        || (run.trace_format == project::TraceFormat::fst && extension == ".vcd");
    if (conflict) {
        diagnostics.error(
            "FSIM-TRACE-0001",
            "trace format '" + std::string(project::to_string(run.trace_format))
                + "' conflicts with output extension '" + extension + "'");
        return std::nullopt;
    }
    return run.trace_format;
}

std::optional<std::int8_t> fst_timescale_exponent(
    const std::string_view timescale,
    diagnostic::Engine& diagnostics)
{
    const auto unit_offset = timescale.find_first_not_of("0123456789");
    std::uint64_t magnitude = 0;
    const auto conversion = unit_offset == std::string_view::npos
        ? std::from_chars(timescale.data(), timescale.data(), magnitude)
        : std::from_chars(
              timescale.data(), timescale.data() + unit_offset, magnitude);
    if (unit_offset == std::string_view::npos
        || conversion.ec != std::errc { }
        || conversion.ptr != timescale.data() + unit_offset
        || magnitude == 0) {
        diagnostics.error(
            "FSIM-TRACE-0001",
            "cannot derive an FST timescale from '" + std::string(timescale)
                + "'");
        return std::nullopt;
    }
    const auto unit = timescale.substr(unit_offset);
    int exponent = unit == "fs" ? -15
        : unit == "ps"          ? -12
        : unit == "ns"          ? -9
        : unit == "us"          ? -6
        : unit == "ms"          ? -3
        : unit == "s"           ? 0
                                : 1;
    while (magnitude > 1 && magnitude % 10 == 0) {
        magnitude /= 10;
        ++exponent;
    }
    if (magnitude != 1 || exponent > 0 || exponent < -18) {
        diagnostics.error(
            "FSIM-TRACE-0001",
            "FST requires a power-of-ten timescale between 1as and 1s");
        return std::nullopt;
    }
    return static_cast<std::int8_t>(exponent);
}

bool prepare_trace_output(
    TraceState& state,
    const std::filesystem::path& output,
    diagnostic::Engine& diagnostics)
{
    state.output_path = output;
    state.lock_directory = output;
    state.lock_directory += ".fsim-lock";
    state.staging_path = state.lock_directory / "trace.tmp";

    std::error_code error;
    const auto parent = output.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
    }
    if (error) {
        diagnostics.error(
            state.format == project::TraceFormat::vcd
                ? "FSIM-VCD-0001"
                : "FSIM-TRACE-0002",
            "cannot create trace directory: " + error.message());
        return false;
    }
    const auto destination_status = std::filesystem::symlink_status(
        output, error);
    if (error
        && error != std::errc::no_such_file_or_directory) {
        diagnostics.error(
            state.format == project::TraceFormat::vcd
                ? "FSIM-VCD-0002"
                : "FSIM-TRACE-0002",
            "cannot inspect trace destination: " + error.message());
        return false;
    }
    error.clear();
    if (destination_status.type() != std::filesystem::file_type::not_found
        && !std::filesystem::is_regular_file(destination_status)) {
        diagnostics.error(
            state.format == project::TraceFormat::vcd
                ? "FSIM-VCD-0002"
                : "FSIM-TRACE-0002",
            "trace destination must be a regular file");
        return false;
    }
    const auto acquired = std::filesystem::create_directory(
        state.lock_directory, error);
    if (error || !acquired) {
        diagnostics.error(
            state.format == project::TraceFormat::vcd
                ? "FSIM-VCD-0002"
                : "FSIM-TRACE-0002",
            "cannot acquire trace output lock for '"
                + support::path_to_utf8(output) + "'");
        state.lock_directory.clear();
        state.staging_path.clear();
        return false;
    }
    state.stream.open(
        state.staging_path, std::ios::binary | std::ios::trunc);
    if (!state.stream) {
        diagnostics.error(
            state.format == project::TraceFormat::vcd
                ? "FSIM-VCD-0002"
                : "FSIM-TRACE-0002",
            "cannot open staged trace output for '"
                + support::path_to_utf8(output) + "'");
        discard_staging(state);
        return false;
    }
    state.diagnostics = &diagnostics;
    return true;
}

bool flush_trace(TraceState& state, diagnostic::Engine& diagnostics)
{
    if (state.terminal_status != TraceTerminalStatus::open) {
        return false;
    }
    try {
        if (state.format == project::TraceFormat::fst) {
            if (!state.fst_writer) {
                throw std::logic_error { "FST trace lifecycle is incomplete" };
            }
            state.fst_writer->flush();
        } else {
            if (!state.writer) {
                throw std::logic_error { "VCD trace lifecycle is incomplete" };
            }
            state.writer->flush();
        }
        state.stream.flush();
        if (!state.stream) {
            throw std::runtime_error { "cannot flush staged trace output" };
        }
        return true;
    } catch (const std::exception& error) {
        fail_trace(state, error.what(), &diagnostics);
    } catch (...) {
        fail_trace(state, "unknown trace flush failure", &diagnostics);
    }
    return false;
}

bool finish_trace(TraceState& state, diagnostic::Engine& diagnostics)
{
    if (state.terminal_status == TraceTerminalStatus::complete) {
        return true;
    }
    if (state.terminal_status == TraceTerminalStatus::failed) {
        return false;
    }
    try {
        if (!state.stream.is_open()) {
            throw std::logic_error { "trace output is not open" };
        }
        if (!state.observations) {
            throw std::logic_error { "trace observation lifecycle is incomplete" };
        }
        if (state.observations->callback_failures() != 0U) {
            try {
                std::rethrow_exception(
                    state.observations->callback_failure());
            } catch (const std::exception& error) {
                throw std::runtime_error {
                    "trace observation callback failed after accepting a record: "
                    + std::string { error.what() }
                };
            } catch (...) {
                throw std::runtime_error {
                    "trace observation callback failed after accepting a record"
                };
            }
        }
        if (state.format == project::TraceFormat::fst) {
            if (!state.fst_writer || !state.simulation) {
                throw std::logic_error { "FST trace lifecycle is incomplete" };
            }
            const auto now = state.simulation->now();
            if (now > std::numeric_limits<SimulationTick>::max()
                    / state.tick_multiplier) {
                throw std::overflow_error { "FST timestamp scaling overflow" };
            }
            state.fst_writer->close(now * state.tick_multiplier);
            state.fst_writer.reset();
        } else {
            if (!state.writer) {
                throw std::logic_error { "VCD trace lifecycle is incomplete" };
            }
            state.writer->flush();
            state.writer.reset();
        }
        state.stream.flush();
        if (!state.stream) {
            throw std::runtime_error { "cannot flush staged trace output" };
        }
        state.stream.close();
        if (state.stream.fail()) {
            throw std::runtime_error { "cannot close staged trace output" };
        }
        publish_staging(state);
        state.terminal_status = TraceTerminalStatus::complete;
        return true;
    } catch (const std::exception& error) {
        fail_trace(state, error.what(), &diagnostics);
        return false;
    } catch (...) {
        fail_trace(state, "unknown trace finalization failure", &diagnostics);
        return false;
    }
}

} // namespace fsim::app::application_detail
