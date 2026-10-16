// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

struct TemporaryDirectory final {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

class ScopedEnvironment final {
public:
  ScopedEnvironment(const char* name, const char* value)
      : name_(name) {
    if (const auto* previous = std::getenv(name_.c_str())) {
      had_previous_ = true;
      previous_ = previous;
    }
    if (!set(value)) {
      throw std::runtime_error("failed to set process environment");
    }
  }

  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

  ~ScopedEnvironment() {
    static_cast<void>(set(had_previous_ ? previous_.c_str() : nullptr));
  }

private:
  [[nodiscard]] bool set(const char* value) const noexcept {
#if defined(_WIN32)
    return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
    const auto status = value == nullptr
        ? ::unsetenv(name_.c_str())
        : ::setenv(name_.c_str(), value, 1);
    return status == 0;
#endif
  }

  std::string name_;
  std::string previous_;
  bool had_previous_ { };
};

class ScopedCerrCapture final {
public:
  explicit ScopedCerrCapture(std::ostringstream& output)
      : previous_(std::cerr.rdbuf(output.rdbuf())) {}

  ScopedCerrCapture(const ScopedCerrCapture&) = delete;
  ScopedCerrCapture& operator=(const ScopedCerrCapture&) = delete;

  ~ScopedCerrCapture() {
    std::cerr.rdbuf(previous_);
  }

private:
  std::streambuf* previous_ { };
};

class ScopedStderrCapture final {
public:
  ScopedStderrCapture() {
    capture_ = std::tmpfile();
    if (capture_ == nullptr) {
      throw std::runtime_error("could not create stderr capture file");
    }
    const int target = stderr_descriptor();
    saved_descriptor_ = duplicate_descriptor(target);
    if (saved_descriptor_ < 0 || std::fflush(stderr) != 0
        || duplicate_to(capture_descriptor(), target) != 0) {
      close_descriptor(saved_descriptor_);
      saved_descriptor_ = -1;
      static_cast<void>(std::fclose(capture_));
      capture_ = nullptr;
      throw std::runtime_error("could not redirect process stderr");
    }
    active_ = true;
  }

  ScopedStderrCapture(const ScopedStderrCapture&) = delete;
  ScopedStderrCapture& operator=(const ScopedStderrCapture&) = delete;

  ~ScopedStderrCapture() {
    static_cast<void>(restore());
    if (capture_ != nullptr) {
      static_cast<void>(std::fclose(capture_));
    }
  }

  [[nodiscard]] std::string finish() {
    if (!active_) {
      throw std::logic_error("process stderr capture already finished");
    }
    if (std::fflush(stderr) != 0) {
      static_cast<void>(restore());
      throw std::runtime_error("could not flush captured process stderr");
    }
    const bool restored = restore();
    if (!restored) {
      throw std::runtime_error("could not restore process stderr");
    }
    if (std::fseek(capture_, 0L, SEEK_SET) != 0) {
      throw std::runtime_error("could not rewind captured process stderr");
    }
    std::string text;
    char buffer[4096];
    while (true) {
      const auto count = std::fread(buffer, 1U, sizeof(buffer), capture_);
      text.append(buffer, count);
      if (count < sizeof(buffer)) {
        if (std::ferror(capture_) != 0) {
          throw std::runtime_error("could not read captured process stderr");
        }
        break;
      }
    }
    return text;
  }

private:
  [[nodiscard]] static int stderr_descriptor() noexcept {
#if defined(_WIN32)
    return ::_fileno(stderr);
#else
    return ::fileno(stderr);
#endif
  }

  [[nodiscard]] int capture_descriptor() const noexcept {
#if defined(_WIN32)
    return ::_fileno(capture_);
#else
    return ::fileno(capture_);
#endif
  }

  [[nodiscard]] static int duplicate_descriptor(const int descriptor) noexcept {
#if defined(_WIN32)
    return ::_dup(descriptor);
#else
    return ::dup(descriptor);
#endif
  }

  [[nodiscard]] static int duplicate_to(
      const int source, const int target) noexcept {
#if defined(_WIN32)
    return ::_dup2(source, target);
#else
    return ::dup2(source, target) == target ? 0 : -1;
#endif
  }

  static void close_descriptor(const int descriptor) noexcept {
    if (descriptor < 0) {
      return;
    }
#if defined(_WIN32)
    static_cast<void>(::_close(descriptor));
#else
    static_cast<void>(::close(descriptor));
#endif
  }

  [[nodiscard]] bool restore() noexcept {
    if (!active_) {
      return true;
    }
    const bool restored = duplicate_to(
        saved_descriptor_, stderr_descriptor()) == 0;
    close_descriptor(saved_descriptor_);
    saved_descriptor_ = -1;
    active_ = false;
    return restored;
  }

  std::FILE* capture_ { };
  int saved_descriptor_ { -1 };
  bool active_ { };
};

void write_file(const std::filesystem::path& path, const std::string_view text) {
  std::ofstream output(path, std::ios::binary);
  if (!output || !output.write(text.data(),
          static_cast<std::streamsize>(text.size()))) {
    throw std::runtime_error("could not write selected-history fixture");
  }
}

[[nodiscard]] fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& vhdl_source,
    const std::filesystem::path& sv_source,
    const std::string_view cache_name,
    const std::string_view top_name = "selected_signal_frontier_top",
    const std::string_view sv_module = "selected_signal_writer",
    const std::string_view binding_instance = "writer") {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = std::string { cache_name };
  config.project.top = "vhdl:work." + std::string { top_name } + "(sim)";
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o2;
  config.build.cache_path = directory / cache_name;
  config.run.max_deltas = 1000U;

  fsim::project::SourceSet vhdl;
  vhdl.language = fsim::project::Language::vhdl;
  vhdl.standard = "2008";
  vhdl.library = "work";
  vhdl.files.push_back(vhdl_source);
  config.source_sets.push_back(std::move(vhdl));

  fsim::project::SourceSet systemverilog;
  systemverilog.language = fsim::project::Language::system_verilog;
  systemverilog.standard = "2017";
  systemverilog.library = "work";
  systemverilog.files.push_back(sv_source);
  config.source_sets.push_back(std::move(systemverilog));
  config.bindings.push_back({
      std::string { top_name } + "." + std::string { binding_instance },
      "sv:work." + std::string { sv_module },
      std::nullopt });
  return config;
}

struct RunCapture final {
  fsim::runtime::RunStatus status { fsim::runtime::RunStatus::stopped };
  std::string failed;
  std::string four_state_stage;
  std::size_t compiled_processes { };
  std::uint64_t native_frontier_members { };
  std::string profile;
};

[[nodiscard]] std::uint64_t profile_metric(
    const std::string_view text,
    const std::string_view name) {
  const auto key = std::string { name } + '=';
  const auto start = text.rfind(key);
  if (start == std::string_view::npos) {
    return 0U;
  }
  const auto value_start = start + key.size();
  const auto end = text.find_first_not_of("0123456789", value_start);
  if (end == value_start) {
    return 0U;
  }
  return static_cast<std::uint64_t>(std::stoull(std::string {
      text.substr(value_start, end == std::string_view::npos
              ? std::string_view::npos : end - value_start) }));
}

struct TraceAccess final {
  std::uint64_t id { };
  std::string name;
};

struct TraceMember final {
  std::uint64_t process { };
  std::string name;
  std::vector<TraceAccess> reads;
  std::vector<TraceAccess> writes;
};

struct TraceOutput final {
  std::uint64_t owner { };
  std::uint64_t signal { };
  std::string name;
  std::uint64_t observations { };
  std::vector<TraceAccess> readers;
};

struct TraceComponent final {
  bool accepted { };
  std::map<std::uint64_t, std::string> accepted_internal;
  std::vector<TraceMember> members;
  std::vector<TraceOutput> outputs;
};

[[nodiscard]] std::optional<std::uint64_t> number_after(
    const std::string_view text, const std::string_view key) {
  const auto key_position = text.find(key);
  if (key_position == std::string_view::npos) {
    return std::nullopt;
  }
  const auto begin = key_position + key.size();
  const auto end = text.find_first_not_of("0123456789", begin);
  if (end == begin) {
    return std::nullopt;
  }
  std::uint64_t value { };
  const auto result = std::from_chars(text.data() + begin,
      text.data() + (end == std::string_view::npos ? text.size() : end), value);
  return result.ec == std::errc { } ? std::optional<std::uint64_t> { value }
                                    : std::nullopt;
}

[[nodiscard]] std::optional<std::string> quoted_after(
    const std::string_view text, const std::string_view key) {
  const auto begin = text.find(key);
  if (begin == std::string_view::npos) {
    return std::nullopt;
  }
  const auto value_begin = begin + key.size();
  const auto value_end = text.find('\'', value_begin);
  if (value_end == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string { text.substr(value_begin, value_end - value_begin) };
}

[[nodiscard]] std::optional<TraceAccess> parse_access_token(
    const std::string_view token) {
  if (token.size() < 5U || (token.front() != 'r' && token.front() != 'w')) {
    return std::nullopt;
  }
  const auto first_colon = token.find(':', 1U);
  const auto plus = token.find('+', first_colon == std::string_view::npos
          ? first_colon : first_colon + 1U);
  const auto second_colon = token.find(':', plus == std::string_view::npos
          ? plus : plus + 1U);
  if (first_colon == std::string_view::npos || plus == std::string_view::npos
      || second_colon == std::string_view::npos
      || second_colon + 1U == token.size()) {
    return std::nullopt;
  }
  std::uint64_t id { };
  const auto parsed = std::from_chars(
      token.data() + 1U, token.data() + first_colon, id);
  if (parsed.ec != std::errc { }) {
    return std::nullopt;
  }
  return TraceAccess { id, std::string { token.substr(second_colon + 1U) } };
}

[[nodiscard]] bool has_leaf_name(
    const std::string_view name, const std::string_view leaf) {
  if (name == leaf || (name.size() > leaf.size() && name.ends_with(leaf))) {
    const auto prefix_size = name.size() - leaf.size();
    if (prefix_size == 0U) {
      return true;
    }
    const auto separator = name[prefix_size - 1U];
    return separator == '.' || separator == '/' || separator == ':'
        || separator == '$';
  }
  return false;
}

[[nodiscard]] std::map<std::uint64_t, TraceComponent> parse_frontier_trace(
    const std::string_view text) {
  std::map<std::uint64_t, TraceComponent> components;
  std::size_t line_begin { };
  while (line_begin < text.size()) {
    const auto line_end = text.find('\n', line_begin);
    const auto line = text.substr(line_begin,
        line_end == std::string_view::npos
            ? std::string_view::npos : line_end - line_begin);
    if (line.starts_with("[fsim region-program-context] ")) {
      const auto component = number_after(line, "component=");
      const auto result = line.find("result=accepted");
      if (component && result != std::string_view::npos) {
        components[*component].accepted = true;
      }
    } else if (line.starts_with(
                   "[fsim region-program-context-signals] ")
        && line.find("kind=accepted-internal") != std::string_view::npos) {
      const auto component = number_after(line, "component=");
      const auto ids_position = line.find("ids=");
      if (component && ids_position != std::string_view::npos) {
        auto ids = line.substr(ids_position + 4U);
        const auto ids_end = ids.find_first_of(" \t\r");
        if (ids_end != std::string_view::npos) {
          ids = ids.substr(0U, ids_end);
        }
        while (!ids.empty()) {
          const auto comma = ids.find(',');
          const auto item = ids.substr(0U,
              comma == std::string_view::npos
                  ? std::string_view::npos : comma);
          const auto colon = item.find(':');
          if (colon != std::string_view::npos) {
            std::uint64_t signal { };
            const auto parsed = std::from_chars(
                item.data(), item.data() + colon, signal);
            if (parsed.ec == std::errc { }) {
              components[*component].accepted_internal.emplace(signal,
                  item.substr(colon + 1U));
            }
          }
          if (comma == std::string_view::npos) {
            break;
          }
          ids.remove_prefix(comma + 1U);
        }
      }
    } else if (line.starts_with(
                   "[fsim region-program-context-output] ")
        && line.find(" signal=") != std::string_view::npos) {
      const auto component = number_after(line, "component=");
      const auto owner = number_after(line, "owner=");
      const auto signal = number_after(line, "signal=");
      const auto observations = number_after(line, "observations=");
      const auto name = quoted_after(line, "name='");
      if (component && owner && signal && observations && name) {
        TraceOutput output { *owner, *signal, *name, *observations, { } };
        std::istringstream fields { std::string { line } };
        std::string token;
        while (fields >> token) {
          if (auto access = parse_access_token(token)) {
            output.readers.push_back(std::move(*access));
          }
        }
        components[*component].outputs.push_back(std::move(output));
      }
    } else if (line.starts_with(
                   "[fsim region-program-context-member] ")
        && line.find(" process=") != std::string_view::npos) {
      const auto component = number_after(line, "component=");
      const auto process = number_after(line, "process=");
      const auto name = quoted_after(line, "name='");
      if (component && process && name) {
        TraceMember member { *process, *name, { }, { } };
        std::istringstream fields { std::string { line } };
        std::string token;
        while (fields >> token) {
          if (auto access = parse_access_token(token)) {
            if (token.front() == 'r') {
              member.reads.push_back(std::move(*access));
            } else {
              member.writes.push_back(std::move(*access));
            }
          }
        }
        components[*component].members.push_back(std::move(member));
      }
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    line_begin = line_end + 1U;
  }
  return components;
}

[[nodiscard]] bool accesses_signal(
    const std::vector<TraceAccess>& accesses, const std::uint64_t signal) {
  return std::ranges::any_of(accesses,
      [signal](const TraceAccess& access) { return access.id == signal; });
}

void require_exact_staged_admission(const RunCapture& result) {
  constexpr std::uint64_t required_history_observations = (1U << 1U) | (1U << 4U);
  const auto components = parse_frontier_trace(result.profile);
  std::size_t exact_matches { };
  for (const auto& [component_id, component] : components) {
    if (!component.accepted) {
      continue;
    }
    for (const auto& output : component.outputs) {
      if (!has_leaf_name(output.name, "observed")
          || (output.observations & required_history_observations)
              != required_history_observations) {
        continue;
      }
      const bool selected_reader = std::ranges::any_of(output.readers,
          [](const TraceAccess& reader) {
            return has_leaf_name(reader.name, "check");
          });
      if (!selected_reader) {
        continue;
      }
      for (const auto& [stage_signal, stage_name]
           : component.accepted_internal) {
        if (!has_leaf_name(stage_name, "stage")) {
          continue;
        }
        const auto output_writer = std::ranges::find_if(component.members,
            [&](const TraceMember& member) {
              return member.process == output.owner
                  && accesses_signal(member.writes, output.signal)
                  && accesses_signal(member.reads, stage_signal);
            });
        if (output_writer == component.members.end()) {
          continue;
        }
        const bool stage_has_distinct_writer = std::ranges::any_of(
            component.members, [&](const TraceMember& member) {
              return member.process != output_writer->process
                  && accesses_signal(member.writes, stage_signal);
            });
        if (stage_has_distinct_writer) {
          ++exact_matches;
          break;
        }
      }
    }
    static_cast<void>(component_id);
  }
  if (exact_matches != 1U) {
    throw std::runtime_error(
        "native staged frontier trace must identify exactly one accepted "
        "component whose output writer writes VHDL-bound observed, reads "
        "the accepted internal stage written by a distinct member, and has "
        "the selected check reader plus event/previous observations; "
        "matching components=" + std::to_string(exact_matches)
        + "; captured process trace:\n" + result.profile);
  }
}

[[nodiscard]] std::uint64_t require_exact_converter_admission(
    const RunCapture& result) {
  const auto components = parse_frontier_trace(result.profile);
  std::size_t exact_matches { };
  std::uint64_t matched_component { };
  for (const auto& [component_id, component] : components) {
    if (!component.accepted) {
      continue;
    }
    for (const auto& output : component.outputs) {
      if (!has_leaf_name(output.name, "observed")) {
        continue;
      }
      for (const auto& [stage_signal, stage_name]
           : component.accepted_internal) {
        if (!has_leaf_name(stage_name, "xz_stage")) {
          continue;
        }
        const auto output_writer = std::ranges::find_if(component.members,
            [&](const TraceMember& member) {
              return member.process == output.owner
                  && accesses_signal(member.writes, output.signal)
                  && accesses_signal(member.reads, stage_signal);
            });
        if (output_writer == component.members.end()) {
          continue;
        }
        const bool stage_has_distinct_writer = std::ranges::any_of(
            component.members, [&](const TraceMember& member) {
              return member.process != output_writer->process
                  && accesses_signal(member.writes, stage_signal);
            });
        if (stage_has_distinct_writer) {
          ++exact_matches;
          matched_component = component_id;
          break;
        }
      }
    }
  }
  if (exact_matches != 1U) {
    throw std::runtime_error(
        "native conversion trace must identify exactly one accepted "
        "component whose output writer publishes observed, reads the "
        "accepted internal xz_stage written by a distinct member; matching "
        "components=" + std::to_string(exact_matches)
        + "; captured process trace:\n" + result.profile);
  }
  return matched_component;
}

void require_native_dispatch_for_component(
    const RunCapture& result,
    const std::uint64_t expected_component,
    const std::string_view label) {
  constexpr std::string_view prefix
      = "[fsim region-access-range-record] kind=component ";
  std::size_t matching_rows { };
  std::optional<std::uint64_t> dispatches;
  std::size_t line_begin { };
  while (line_begin < result.profile.size()) {
    const auto line_end = result.profile.find('\n', line_begin);
    const auto line = std::string_view { result.profile }.substr(line_begin,
        line_end == std::string::npos
            ? std::string_view::npos : line_end - line_begin);
    if (line.starts_with(prefix)) {
      const auto component = number_after(line, "component=");
      if (component && *component == expected_component) {
        ++matching_rows;
        dispatches = number_after(line, "native_member_dispatches=");
      }
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_begin = line_end + 1U;
  }
  if (matching_rows != 1U || !dispatches || *dispatches == 0U) {
    throw std::runtime_error(std::string { label }
        + " native dispatch record must occur exactly once with a positive "
          "native_member_dispatches count for the accepted xz_stage/observed "
          "component=" + std::to_string(expected_component)
        + "; matching rows=" + std::to_string(matching_rows)
        + "; dispatches=" + (dispatches
            ? std::to_string(*dispatches) : std::string { "missing" })
        + "; captured profile:\n" + result.profile);
  }
}

[[nodiscard]] RunCapture run_case(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const bool region_kernel,
    const std::string_view result_signal_name
        = "selected_signal_frontier_top.failed",
    const std::string_view observed_signal_name = { }) {
  ScopedEnvironment region_enabled {
    "FSIM_ENABLE_SV_REGION_KERNEL", region_kernel ? "1" : "0" };
  ScopedEnvironment local_wave_enabled {
    "FSIM_ENABLE_SV_LOCAL_WAVE", region_kernel ? "1" : "0" };
  ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
  std::ostringstream profile;
  ScopedCerrCapture capture { profile };
  ScopedStderrCapture process_stderr;

  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    throw std::runtime_error("selected-history mixed project did not build: "
        + profile.str() + process_stderr.finish());
  }

  RunCapture result;
  {
    fsim::app::Simulation simulation {
      std::move(*project), config.run.max_deltas, engine,
      fsim::app::SystemVerilogVpiRuntimeUpdates::disabled };
    result.compiled_processes = simulation.compiled_process_count();
    const auto failed = simulation.find_signal(
        result_signal_name);
    if (!failed) {
      throw std::runtime_error("selected-history result signal is missing");
    }
    const auto four_state_stage = observed_signal_name.empty()
        ? std::optional<fsim::runtime::simir::SignalId> { }
        : simulation.find_signal(observed_signal_name);
    if (!observed_signal_name.empty() && !four_state_stage) {
      throw std::runtime_error(
          "selected-history observation signal is missing");
    }
    result.status = simulation.run().status;
    result.failed = simulation.read_signal(*failed).to_msb_string();
    if (four_state_stage) {
      result.four_state_stage
          = simulation.read_signal(*four_state_stage).to_msb_string();
    }
  }
  result.profile = profile.str() + process_stderr.finish();
  result.native_frontier_members = profile_metric(
      result.profile, "native_frontier_member_dispatches");
  return result;
}

} // namespace

int main() {
  const auto unique = std::chrono::steady_clock::now()
      .time_since_epoch().count();
  TemporaryDirectory directory {
    std::filesystem::temp_directory_path()
        / ("fsim-selected-frontier-history-" + std::to_string(unique)) };
  std::filesystem::create_directories(directory.path);

  constexpr std::string_view vhdl_source = R"(
entity selected_signal_frontier_top is
  port (failed : out bit_vector(10 downto 0));
end entity;

architecture sim of selected_signal_frontier_top is
  signal data : bit_vector(3 downto 0) := "0000";
  signal observed : bit_vector(3 downto 0) := "0000";
  signal failed_local : bit_vector(10 downto 0) := (others => '0');

  component selected_signal_writer is
    port (
      data : in bit_vector(3 downto 0);
      value : out bit_vector(3 downto 0));
  end component;

  function selected_event(signal value : in bit) return boolean is
  begin
    return value'event;
  end function;

  function selected_last_value(signal value : in bit)
    return bit is
  begin
    return value'last_value;
  end function;

  function selected_last_event(signal value : in bit) return time is
  begin
    return value'last_event;
  end function;
begin
  data <= "0101" after 1 ns, "0110" after 3 ns;
  failed <= failed_local;

  writer : selected_signal_writer
    port map (data => data, value => observed);

  check : process
    variable failures : bit_vector(10 downto 0) := (others => '0');
  begin
    wait on observed for 6 ns;
    if not observed'event then
      failures(0) := '1';
    else
      if not selected_event(observed(0)) then
        failures(1) := '1';
      end if;
      if selected_last_value(observed(0)) /= '0' then
        failures(2) := '1';
      end if;
      if selected_last_event(observed(0)) /= 0 ns then
        failures(3) := '1';
      end if;

      wait on observed for 6 ns;
      if not observed'event then
        failures(4) := '1';
      else
        if not selected_event(observed(0)) then
          failures(5) := '1';
        end if;
        if selected_last_value(observed(0)) /= '1' then
          failures(6) := '1';
        end if;
        if selected_last_event(observed(0)) /= 0 ns then
          failures(7) := '1';
        end if;
        if selected_event(observed(2)) then
          failures(8) := '1';
        end if;
        if selected_last_value(observed(2)) /= '0' then
          failures(9) := '1';
        end if;
        if selected_last_event(observed(2)) /= 2 ns then
          failures(10) := '1';
        end if;
      end if;
    end if;

    failed_local <= failures;
    wait;
  end process;

end architecture;
)";

  constexpr std::string_view sv_source = R"(
module selected_signal_writer(
  input bit [3:0] data,
  output bit [3:0] value);
  always_comb value = data;
endmodule
)";

  constexpr std::string_view staged_sv_source = R"(
module selected_signal_writer(
  input bit [3:0] data,
  output bit [3:0] value);
  bit [3:0] stage;
  always_comb stage = data;
  always_comb value = stage;
endmodule
)";

  // The four-state wire keeps the selected-history route legal and exposes
  // its required Logic4-to-Bit2 conversion.
  constexpr std::string_view staged_update_sv_source = R"(
module selected_signal_writer(
  input bit [3:0] data,
  output bit [3:0] value);
  wire logic [3:0] stage;
  assign stage = data;
  assign value = stage;
endmodule
)";

  constexpr std::string_view xz_vhdl_source = R"(
library ieee;
use ieee.std_logic_1164.all;

entity selected_signal_xz_top is
  port (failed : out bit_vector(2 downto 0));
end entity;

architecture sim of selected_signal_xz_top is
  signal data : std_logic_vector(1 downto 0) := "00";
  signal observed : bit_vector(1 downto 0) := "00";
  signal failed_local : bit_vector(2 downto 0) := (others => '0');

  component selected_signal_xz_writer is
    port (
      data : in std_logic_vector(1 downto 0);
      value : out bit_vector(1 downto 0));
  end component;
begin
  failed <= failed_local;

  writer: selected_signal_xz_writer
    port map (data => data, value => observed);

  check: process
    variable failures : bit_vector(2 downto 0) := (others => '0');
  begin
    wait for 1 ns;
    data <= "11";
    wait for 1 ns;
    if observed /= "11" then
      failures(0) := '1';
    end if;

    data <= "1X";
    wait for 1 ns;
    if observed /= "10" then
      failures(1) := '1';
    end if;

    data <= "Z0";
    wait for 1 ns;
    if observed /= "00" then
      failures(2) := '1';
    end if;

    failed_local <= failures;
    wait;
  end process;
end architecture;
)";

  constexpr std::string_view xz_sv_source = R"(
module selected_signal_xz_writer(
  input logic [1:0] data,
  output bit [1:0] value);
  wire logic [1:0] xz_stage;
  assign xz_stage = data;
  assign value = xz_stage;
endmodule
)";

  // Exercise all nine std_logic values across the implicit VHDL Logic9 to
  // SystemVerilog Logic4 input boundary and check their two-state image at
  // the existing Logic4-to-Bit2 output boundary.
  constexpr std::string_view nine_state_vhdl_source = R"(
library ieee;
use ieee.std_logic_1164.all;

entity selected_signal_nine_top is
  port (failed : out bit);
end entity;

architecture sim of selected_signal_nine_top is
  signal data : std_logic_vector(8 downto 0) := "000000000";
  signal observed : bit_vector(8 downto 0) := "000000000";
  signal failed_local : bit := '0';

  component selected_signal_nine_writer is
    port (
      data : in std_logic_vector(8 downto 0);
      value : out bit_vector(8 downto 0));
  end component;
begin
  failed <= failed_local;

  writer: selected_signal_nine_writer
    port map (data => data, value => observed);

  check: process
  begin
    wait for 1 ns;
    data <= "UX01ZWLH-";
    wait for 1 ns;
    if observed /= "000100010" then
      failed_local <= '1';
    end if;
    wait;
  end process;
end architecture;
)";

  constexpr std::string_view nine_state_sv_source = R"(
module selected_signal_nine_writer(
  input logic [8:0] data,
  output bit [8:0] value);
  wire logic [8:0] xz_stage;
  assign xz_stage = data;
  assign value = xz_stage;
endmodule
)";

  const auto vhdl_path = directory.path / "selected_signal_frontier_top.vhd";
  const auto sv_path = directory.path / "selected_signal_writer.sv";
  const auto staged_sv_path
      = directory.path / "selected_signal_writer_staged.sv";
  const auto staged_update_sv_path
      = directory.path / "selected_signal_writer_staged_update.sv";
  const auto xz_vhdl_path
      = directory.path / "selected_signal_frontier_xz_top.vhd";
  const auto xz_sv_path
      = directory.path / "selected_signal_writer_xz.sv";
  const auto nine_state_vhdl_path
      = directory.path / "selected_signal_frontier_nine_top.vhd";
  const auto nine_state_sv_path
      = directory.path / "selected_signal_writer_nine.sv";
  write_file(vhdl_path, vhdl_source);
  write_file(sv_path, sv_source);
  write_file(staged_sv_path, staged_sv_source);
  write_file(staged_update_sv_path, staged_update_sv_source);
  write_file(xz_vhdl_path, xz_vhdl_source);
  write_file(xz_sv_path, xz_sv_source);
  write_file(nine_state_vhdl_path, nine_state_vhdl_source);
  write_file(nine_state_sv_path, nine_state_sv_source);

  const auto require_history_pass = [](const RunCapture& result,
                                       const std::string_view label) {
    if (result.status != fsim::runtime::RunStatus::completed
        || result.failed != "00000000000") {
      throw std::runtime_error(std::string { label }
          + " selected-signal history failure bits [10:0]: "
          + result.failed);
    }
  };

  // The one-process design has no internal state. Keep it as an interpreter
  // and compiled-engine compatibility control with native dispatch disabled.
  const auto original_interpreter = run_case(
      make_config(directory.path, vhdl_path, sv_path,
          "cache-original-interpreter"),
      fsim::app::SimulationEngine::interpreter, false);
  require_history_pass(original_interpreter, "original interpreter");
  const auto original_compiled = run_case(
      make_config(directory.path, vhdl_path, sv_path,
          "cache-original-compiled"),
      fsim::app::SimulationEngine::compiled, false);
  require_history_pass(original_compiled, "original compiled");

  // The staged design gives the active-domain component one real internal
  // signal while retaining the same externally observed selected-history
  // output and every VHDL assertion above.
  const auto staged_interpreter = run_case(
      make_config(directory.path, vhdl_path, staged_sv_path,
          "cache-staged-interpreter"),
      fsim::app::SimulationEngine::interpreter, false);
  require_history_pass(staged_interpreter, "staged interpreter");
  const auto staged_compiled = run_case(
      make_config(directory.path, vhdl_path, staged_sv_path,
          "cache-staged-compiled"),
      fsim::app::SimulationEngine::compiled, false);
  require_history_pass(staged_compiled, "staged compiled");

  // Blocking-immediate publications currently use the ordinary fallback.
  // Keep their history checks with the optional region backend enabled.
  const auto staged_blocking_enabled = run_case(
      make_config(directory.path, vhdl_path, staged_sv_path,
          "cache-staged-blocking-enabled"),
      fsim::app::SimulationEngine::compiled, true);
  require_history_pass(staged_blocking_enabled, "staged blocking enabled");

  // Continuous assignments use update publications accepted by the native
  // frontier. Check the same selected-history oracle on both ordinary engines
  // before requiring native execution for this publication contract.
  const auto update_interpreter = run_case(
      make_config(directory.path, vhdl_path, staged_update_sv_path,
          "cache-staged-update-interpreter"),
      fsim::app::SimulationEngine::interpreter, false);
  require_history_pass(update_interpreter, "staged update interpreter");
  const auto update_compiled = run_case(
      make_config(directory.path, vhdl_path, staged_update_sv_path,
          "cache-staged-update-compiled"),
      fsim::app::SimulationEngine::compiled, false);
  require_history_pass(update_compiled, "staged update compiled");

  const auto staged_native = run_case(
      make_config(directory.path, vhdl_path, staged_update_sv_path,
          "cache-staged-native-frontier"),
      fsim::app::SimulationEngine::compiled, true);
  if (staged_native.compiled_processes == 0U
      || staged_native.native_frontier_members == 0U) {
    throw std::runtime_error(
        "staged control did not prove native-frontier execution; "
        "the build-decline context must identify the same component as the "
        "selected-history output writer; captured profile:\n"
        + staged_native.profile);
  }
  require_history_pass(staged_native, "staged native frontier");
  require_exact_staged_admission(staged_native);
  std::cerr << "[fsim selected-frontier captured process stderr begin]\n"
            << staged_native.profile
            << "[fsim selected-frontier captured process stderr end]\n";

  const auto xz_config = [&](const std::string_view cache_name) {
    return make_config(directory.path, xz_vhdl_path, xz_sv_path,
        cache_name, "selected_signal_xz_top",
        "selected_signal_xz_writer");
  };
  const auto nine_state_config = [&](const std::string_view cache_name) {
    return make_config(directory.path, nine_state_vhdl_path,
        nine_state_sv_path, cache_name, "selected_signal_nine_top",
        "selected_signal_nine_writer");
  };
  const auto require_xz_pass = [](const RunCapture& result,
                                  const std::string_view label) {
    if (result.status != fsim::runtime::RunStatus::completed
        || result.failed != "000") {
      throw std::runtime_error(std::string { label }
          + " X/Z-to-two-state failure bits [2:0]: "
          + result.failed + "; captured profile:\n" + result.profile);
    }
  };
  const auto xz_interpreter = run_case(
      xz_config("cache-xz-interpreter"),
      fsim::app::SimulationEngine::interpreter, false,
      "selected_signal_xz_top.failed");
  require_xz_pass(xz_interpreter, "X/Z interpreter");
  const auto xz_compiled = run_case(
      xz_config("cache-xz-compiled"),
      fsim::app::SimulationEngine::compiled, false,
      "selected_signal_xz_top.failed");
  require_xz_pass(xz_compiled, "X/Z compiled");

  const auto xz_native = run_case(
      xz_config("cache-xz-native-frontier"),
      fsim::app::SimulationEngine::compiled, true,
      "selected_signal_xz_top.failed");
  require_xz_pass(xz_native, "X/Z native frontier");
  if (xz_native.compiled_processes == 0U
      || xz_native.native_frontier_members == 0U) {
    throw std::runtime_error(
        "X/Z conversion control did not prove native-frontier execution; "
        "captured profile:\n" + xz_native.profile);
  }
  const auto xz_component = require_exact_converter_admission(xz_native);
  require_native_dispatch_for_component(
      xz_native, xz_component, "X/Z conversion");
  std::cerr << "[fsim selected-frontier X/Z process stderr begin]\n"
            << xz_native.profile
            << "[fsim selected-frontier X/Z process stderr end]\n";

  const auto require_nine_state_pass = [](
      const RunCapture& result, const std::string_view label) {
    if (result.status != fsim::runtime::RunStatus::completed
        || result.failed != "0"
        || result.four_state_stage != "XX01ZX01X") {
      throw std::runtime_error(std::string { label }
          + " nine-state conversion failure; expected Logic4 stage "
            "XX01ZX01X and Bit2 image 000100010, got Logic4 stage '"
          + result.four_state_stage + "' and failure bit '"
          + result.failed + "'; captured profile:\n" + result.profile);
    }
  };
  const auto nine_state_interpreter = run_case(
      nine_state_config("cache-nine-state-interpreter"),
      fsim::app::SimulationEngine::interpreter, false,
      "selected_signal_nine_top.failed",
      "selected_signal_nine_top.writer.xz_stage");
  require_nine_state_pass(nine_state_interpreter, "nine-state interpreter");
  const auto nine_state_compiled = run_case(
      nine_state_config("cache-nine-state-compiled"),
      fsim::app::SimulationEngine::compiled, false,
      "selected_signal_nine_top.failed",
      "selected_signal_nine_top.writer.xz_stage");
  require_nine_state_pass(nine_state_compiled, "nine-state compiled");
  const auto nine_state_native = run_case(
      nine_state_config("cache-nine-state-native-frontier"),
      fsim::app::SimulationEngine::compiled, true,
      "selected_signal_nine_top.failed",
      "selected_signal_nine_top.writer.xz_stage");
  require_nine_state_pass(nine_state_native, "nine-state native frontier");
  if (nine_state_native.compiled_processes == 0U
      || nine_state_native.native_frontier_members == 0U) {
    throw std::runtime_error(
        "nine-state conversion control did not prove native-frontier "
        "execution; captured profile:\n" + nine_state_native.profile);
  }
  const auto nine_state_component
      = require_exact_converter_admission(nine_state_native);
  require_native_dispatch_for_component(
      nine_state_native, nine_state_component, "nine-state conversion");
  std::cerr << "[fsim selected-frontier nine-state process stderr begin]\n"
            << nine_state_native.profile
            << "[fsim selected-frontier nine-state process stderr end]\n";
}
