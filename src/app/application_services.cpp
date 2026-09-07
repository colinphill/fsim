// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app {
using namespace application_detail;

int run_debug_repl(
    Simulation& simulation,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) {
  return run_debug_repl_impl(
      simulation, input, output, error, nullptr);
}

std::optional<PackedLogic4> parse_value(
    const std::string_view text,
    const std::size_t width,
    std::string& error) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const char character : text) {
    if (character == '_') {
      continue;
    }
    normalized.push_back(static_cast<char>(
        std::toupper(static_cast<unsigned char>(character))));
  }
  if (normalized.starts_with("0B")) {
    normalized.erase(0, 2);
  }
  if (normalized.size() != width) {
    error = "value width is " + std::to_string(normalized.size())
        + " but the signal width is " + std::to_string(width);
    return std::nullopt;
  }
  try {
    if (normalized.find_first_of("UWLH-")
        != std::string::npos) {
      return PackedLogic4::from_logic9_msb_string(
          normalized);
    }
    return PackedLogic4::from_msb_string(normalized);
  } catch (const std::invalid_argument&) {
    error =
        "value must contain only 0, 1, X, Z, U, W, L, H, or -";
    return std::nullopt;
  }
}

std::optional<SimulationTick> parse_time(
    const std::string_view text,
    const std::string_view resolution,
    std::string& error) {
  const auto requested = magnitude_and_unit(text);
  if (!requested) {
    error = "invalid time '" + std::string(text) + "'";
    return std::nullopt;
  }
  if (requested->unit.empty()) {
    return requested->magnitude;
  }
  const auto requested_factor = unit_femtoseconds(requested->unit);
  if (!requested_factor) {
    error = "unknown time unit '" + requested->unit + "'";
    return std::nullopt;
  }
  const auto effective_resolution =
      resolution == "auto" ? std::string_view{"1ns"} : resolution;
  const auto tick = magnitude_and_unit(effective_resolution);
  if (!tick || tick->unit.empty()) {
    error = "invalid project time resolution '"
        + std::string(effective_resolution) + "'";
    return std::nullopt;
  }
  const auto tick_factor = unit_femtoseconds(tick->unit);
  if (!tick_factor) {
    error = "unknown project time-resolution unit '" + tick->unit + "'";
    return std::nullopt;
  }
  if (requested->magnitude
      > std::numeric_limits<std::uint64_t>::max() / *requested_factor) {
    error = "time value overflows 64-bit simulation ticks";
    return std::nullopt;
  }
  if (tick->magnitude
      > std::numeric_limits<std::uint64_t>::max() / *tick_factor) {
    error = "time resolution overflows its internal representation";
    return std::nullopt;
  }
  const auto requested_fs = requested->magnitude * *requested_factor;
  const auto tick_fs = tick->magnitude * *tick_factor;
  if (tick_fs == 0 || requested_fs % tick_fs != 0) {
    error = "time is not exactly representable at resolution '"
        + std::string(effective_resolution) + "'";
    return std::nullopt;
  }
  return requested_fs / tick_fs;
}

cli::Services make_cli_services(std::istream& input) {
  cli::Handler debug =
      [&input](
          const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine& diagnostics,
          std::ostream& output,
          std::ostream& error) {
        return handle_debug(
            invocation, config, diagnostics, input, output, error);
      };
  cli::Handler tcl =
      [&input](
          const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine& diagnostics,
          std::ostream& output,
          std::ostream& error) {
        return handle_tcl(
            invocation,
            config,
            diagnostics,
            input,
            output,
            error);
      };
  return {
      handle_check,
      handle_build,
      handle_run,
      std::move(debug),
      std::move(tcl),
      handle_compile,
      handle_elaborate,
      handle_simulate,
      handle_systemc_compile,
      handle_systemc_link,
      handle_coverage_merge,
      handle_coverage_report};
}

cli::Services make_cli_services() {
  return make_cli_services(std::cin);
}


} // namespace fsim::app
