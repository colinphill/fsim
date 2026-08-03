// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <csignal>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

struct CapturedSimulation {
  runtime::RunResult result;
  std::vector<std::tuple<
      runtime::simir::SignalId,
      std::string,
      runtime::SimulationTick,
      std::uint64_t>> changes;
  std::vector<std::string> final_values;
  std::string normalized_vcd;
  app::NativeCacheStatistics native_cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  std::size_t process_count{};
};

struct ParameterRun {
  std::vector<std::pair<std::string, std::string>> keys;
  CapturedSimulation simulation;
};

class InterruptingOutputBuffer final : public std::stringbuf {
 protected:
  std::streamsize xsputn(
      const char* value,
      std::streamsize count) override;

 private:
  bool raised_{};
};

extern volatile std::sig_atomic_t restored_interrupt_count;
extern "C" void record_restored_interrupt(int);

class ApplicationTestFixture {
 public:
  ApplicationTestFixture();
  ~ApplicationTestFixture();

  ApplicationTestFixture(const ApplicationTestFixture&) = delete;
  ApplicationTestFixture& operator=(const ApplicationTestFixture&) = delete;

  void test_systemc_integration();
  void test_systemc_scheduling_matrix();
  void test_simulation_semantics();
  void test_specialization_and_packages();
  void test_mixed_language_and_generate();
  void test_preprocessing_debug_and_cli();

 private:
  void create_common_sources();
  void create_mixed_language_sources();
  void create_systemc_sources();
  void write_provenance_source(std::string_view comment);
  void write_unused_source(std::string_view comment);
  void write_parameter_top(std::uint64_t narrow_value);
  void write_vhdl_generic_entity(std::string_view revision);
  void write_vhdl_generic_top(std::uint64_t narrow_value);

  [[nodiscard]] project::Config base_config() const;
  [[nodiscard]] CapturedSimulation capture_simulation(
      app::BuiltProject project,
      app::SimulationEngine engine,
      std::optional<runtime::SimulationTick> until = std::nullopt) const;
  static void compare_captures(
      const CapturedSimulation& reference,
      const CapturedSimulation& hybrid);

  std::filesystem::path directory;
  std::filesystem::path source;
  std::filesystem::path scheduled_source;
  std::filesystem::path sensitivity_source;
  std::filesystem::path vhdl_wait_source;
  std::filesystem::path wildcard_source;
  std::filesystem::path case_source;
  std::filesystem::path conditional_source;
  std::filesystem::path comparison_source;
  std::filesystem::path logical_source;
  std::filesystem::path arithmetic_source;
  std::filesystem::path select_concat_source;
  std::filesystem::path vhdl_select_concat_source;
  std::filesystem::path vhdl_signed_source;
  std::filesystem::path conditional_statement_source;
  std::filesystem::path vhdl_conditional_statement_source;
  std::filesystem::path partial_group_source;
  std::filesystem::path assertion_source;
  std::filesystem::path provenance_source;
  std::filesystem::path unused_source;
  std::filesystem::path parameter_child_source;
  std::filesystem::path parameter_top_source;
  std::filesystem::path vhdl_generic_entity_source;
  std::filesystem::path vhdl_generic_architecture_source;
  std::filesystem::path vhdl_generic_top_source;
  std::filesystem::path mixed_actual_sv_top_source;
  std::filesystem::path mixed_actual_sv_child_source;
  std::filesystem::path mixed_actual_vhdl_top_source;
  std::filesystem::path generated_mixed_sv_top_source;
  std::filesystem::path generated_loop_vhdl_source;
  std::filesystem::path generated_loop_sv_top_source;
  std::filesystem::path generated_case_sv_top_source;
  std::filesystem::path generated_behavior_sv_source;
  std::filesystem::path generated_behavior_vhdl_source;
  std::filesystem::path generated_enum_behavior_vhdl_source;
  std::filesystem::path vhdl_statement_behavior_source;
  std::filesystem::path generated_static_behavior_sv_source;
  std::filesystem::path generated_implicit_behavior_sv_source;
  std::filesystem::path generated_block_behavior_vhdl_source;
  std::filesystem::path systemc_source;
  std::filesystem::path systemc_boundary_source;
  std::filesystem::path systemc_method_vhdl_source;
};

}  // namespace fsim::test
