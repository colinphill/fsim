// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_config_db.hpp"
#include "fsim/runtime/uvm_factory.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::size_t kSystemVerilogUvmBitstreamWidth = 4'096;
inline constexpr std::string_view kSystemVerilogUvmBitstreamConfigType =
    "uvm_config_db#(uvm_bitstream_t)";
inline constexpr std::string_view kSystemVerilogUvmStringConfigType =
    "uvm_config_db#(string)";

class SystemVerilogUvmCommandLineError final : public std::runtime_error {
 public:
  SystemVerilogUvmCommandLineError(
      std::size_t argument_index,
      std::string argument,
      std::string message);

  [[nodiscard]] std::size_t argument_index() const noexcept {
    return argument_index_;
  }
  [[nodiscard]] const std::string& argument() const noexcept {
    return argument_;
  }

 private:
  std::size_t argument_index_{};
  std::string argument_;
};

enum class SystemVerilogUvmCommandFactoryKind : std::uint8_t {
  Instance,
  Type,
};

enum class SystemVerilogUvmCommandConfigKind : std::uint8_t {
  Integer,
  Bitstream,
  String,
};

struct SystemVerilogUvmCommandFactorySetting {
  SystemVerilogUvmCommandFactoryKind kind{
      SystemVerilogUvmCommandFactoryKind::Type};
  std::string requested_type;
  std::string override_type;
  std::string instance_pattern;
  bool replace{true};
  std::size_t source_index{};
};

struct SystemVerilogUvmCommandConfigSetting {
  SystemVerilogUvmCommandConfigKind kind{
      SystemVerilogUvmCommandConfigKind::Integer};
  std::string component_pattern;
  std::string field;
  SystemVerilogUvmResourceValue value{PackedLogic4{}};
  std::size_t source_index{};
};

struct SystemVerilogUvmCommandVerbositySetting {
  std::string component_pattern;
  std::string id;
  std::int32_t verbosity{};
  std::string phase;
  std::optional<std::uint64_t> time_offset;
  std::size_t source_index{};
};

struct SystemVerilogUvmCommandTimeoutSetting {
  std::uint64_t ticks{};
  bool overridable{true};
  std::size_t source_index{};
};

struct SystemVerilogUvmCommandLineSettings {
  std::vector<std::string> arguments;
  std::vector<std::string> unknown_arguments;
  std::vector<SystemVerilogUvmCommandFactorySetting> factory_settings;
  std::vector<SystemVerilogUvmCommandConfigSetting> config_settings;
  std::vector<SystemVerilogUvmCommandVerbositySetting> verbosity_settings;
  std::optional<std::int32_t> initial_verbosity;
  std::size_t initial_verbosity_argument_count{};
  std::optional<SystemVerilogUvmCommandTimeoutSetting> timeout;
  std::size_t timeout_argument_count{};
  bool resource_db_trace{};
  bool config_db_trace{};
};

struct SystemVerilogUvmCommandLineLimits {
  std::size_t max_arguments{4'096};
  std::size_t max_argument_bytes{16'384};
  std::size_t max_total_argument_bytes{16U * 1'024U * 1'024U};
  std::size_t max_settings{65'536};
  std::size_t max_type_name_bytes{4'096};
  std::size_t max_component_pattern_bytes{16'384};
  std::size_t max_field_bytes{4'096};
  std::size_t max_id_bytes{4'096};
  std::size_t max_phase_bytes{256};
};

/// Bounded, simulation-owned host interpretation of UVM command-line
/// settings. Unknown plusargs remain ordered for later HDL-level consumption.
/// Parsing completes before any factory, resource, or config state is changed.
class SystemVerilogUvmCommandLineService final {
 public:
  SystemVerilogUvmCommandLineService(
      SystemVerilogUvmFactoryService& factory,
      SystemVerilogUvmResourcePoolService& resources,
      SystemVerilogUvmConfigDbService& config,
      SystemVerilogUvmCommandLineLimits limits = {});

  void apply(std::span<const std::string> arguments);

  [[nodiscard]] const SystemVerilogUvmCommandLineSettings& settings()
      const noexcept { return settings_; }
  [[nodiscard]] const SystemVerilogUvmCommandLineLimits& limits()
      const noexcept { return limits_; }

 private:
  [[nodiscard]] SystemVerilogUvmCommandLineSettings parse(
      std::span<const std::string> arguments) const;
  void validate_resource_capacity(
      const SystemVerilogUvmCommandLineSettings& settings) const;

  SystemVerilogUvmFactoryService* factory_{};
  SystemVerilogUvmResourcePoolService* resources_{};
  SystemVerilogUvmConfigDbService* config_{};
  SystemVerilogUvmCommandLineLimits limits_;
  SystemVerilogUvmCommandLineSettings settings_;
};

}  // namespace fsim::runtime
