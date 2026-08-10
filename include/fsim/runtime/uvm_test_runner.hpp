// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_command_line.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_factory.hpp"
#include "fsim/runtime/uvm_object.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::runtime {

enum class SystemVerilogUvmRunStatus : std::uint8_t {
  Completed,
  Finished,
  Fatal,
  TimedOut,
};

struct SystemVerilogUvmRunExecution {
  SystemVerilogUvmRunStatus status{SystemVerilogUvmRunStatus::Completed};
  SimulationTick elapsed_ticks{};
  std::string message;
};

struct SystemVerilogUvmRunOptions {
  std::optional<std::string> test_name;
  std::optional<std::uint64_t> seed;
  std::optional<SimulationTick> timeout;
  bool print_topology{true};
  bool cleanup{true};
};

struct SystemVerilogUvmRunResult {
  std::uint64_t run_identity{};
  std::string test_name;
  std::uint64_t seed{};
  std::optional<SimulationTick> timeout;
  SystemVerilogUvmRunStatus status{SystemVerilogUvmRunStatus::Completed};
  SimulationTick elapsed_ticks{};
  std::string topology;
  std::string message;
  std::string diagnostic_code;
  bool cleaned{};

  [[nodiscard]] bool success() const noexcept {
    return status == SystemVerilogUvmRunStatus::Completed
        || status == SystemVerilogUvmRunStatus::Finished;
  }
};

struct SystemVerilogUvmRunLimits {
  std::size_t maximum_runs{1U << 20U};
  std::size_t maximum_test_name_bytes{4096};
  std::size_t maximum_topology_bytes{1U << 20U};
  std::size_t maximum_topology_components{1U << 20U};
  std::size_t maximum_message_bytes{1U << 20U};
};

class SystemVerilogUvmRunError final : public std::runtime_error {
 public:
  SystemVerilogUvmRunError(std::string code, std::string message);
  [[nodiscard]] const std::string& diagnostic_code() const noexcept {
    return diagnostic_code_;
  }

 private:
  std::string diagnostic_code_;
};

class SystemVerilogUvmTestRunnerService final {
 public:
  using Execution = std::function<SystemVerilogUvmRunExecution(
      SystemVerilogClassHandle, std::uint64_t)>;

  SystemVerilogUvmTestRunnerService(
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmFactoryService& factory,
      SystemVerilogUvmCommandLineService& command_line,
      SystemVerilogUvmRunLimits limits = {});

  [[nodiscard]] SystemVerilogUvmRunResult run_test(
      SystemVerilogUvmRootHandle root,
      SystemVerilogUvmRunOptions options = {},
      Execution execution = {});
  [[nodiscard]] std::string topology(SystemVerilogUvmRootHandle root) const;
  [[nodiscard]] std::uint64_t run_count() const noexcept { return run_count_; }
  [[nodiscard]] bool running() const noexcept { return running_; }

 private:
  [[nodiscard]] std::string select_test_name(
      const SystemVerilogUvmRunOptions& options) const;
  [[nodiscard]] std::uint64_t select_seed(
      const SystemVerilogUvmRunOptions& options) const;
  [[nodiscard]] std::optional<SimulationTick> select_timeout(
      const SystemVerilogUvmRunOptions& options) const;
  void append_topology(
      std::string& output,
      SystemVerilogClassHandle component,
      std::size_t& count) const;

  SystemVerilogUvmObjectService* objects_{};
  SystemVerilogUvmComponentService* components_{};
  SystemVerilogUvmFactoryService* factory_{};
  SystemVerilogUvmCommandLineService* command_line_{};
  SystemVerilogUvmRunLimits limits_;
  std::uint64_t run_count_{};
  bool running_{};
};

}  // namespace fsim::runtime
