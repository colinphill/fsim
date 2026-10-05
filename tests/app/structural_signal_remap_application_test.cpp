// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#if defined(FSIM_HAS_LLVM)
#include "../../src/app/application_internal.hpp"
#endif

#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(FSIM_HAS_LLVM)
namespace fsim::app::application_detail {

struct SignalCallbackOperandTestAccess {
  [[nodiscard]] static std::uint32_t mapped_signal(
      const bool actual_ids, const std::uint32_t signal)
  {
    using Executor = LlvmProcessExecutor;
    using Mapping = Executor::SignalRemap::value_type;
    const std::array<Mapping, 2U> remap {{{ 7U, 65U }, { 65U, 1000U }}};
    Executor::CallbackState state { actual_ids };
    state.signal_remap = remap;
    return Executor::mapped_signal(state, signal);
  }
};

}  // namespace fsim::app::application_detail
#endif

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult run;
  std::array<std::string, 8> values;
  std::size_t process_count{};
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}

std::string repeated_logic9_pattern(
    const std::string_view pattern, const std::size_t width) {
  assert(!pattern.empty());
  std::string result;
  result.reserve(width);
  for (std::size_t index = 0; index < width; ++index) {
    result.push_back(pattern[index % pattern.size()]);
  }
  return result;
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::string_view root = "structural_signal_remap_app",
    const std::optional<fsim::runtime::SimulationTick> until = std::nullopt,
    const bool capture_logic9 = false) {
  fsim::app::Simulation simulation{std::move(project), 1000, engine};
  const std::array suffixes{
      std::string_view{"source_a"},
      std::string_view{"source_b"},
      std::string_view{"result_a"},
      std::string_view{"result_b"},
      std::string_view{"a.result"},
      std::string_view{"b.result"},
  };
  std::array<fsim::runtime::simir::SignalId, suffixes.size()> signals{};
  for (std::size_t index = 0; index < suffixes.size(); ++index) {
    const auto path = std::string{root} + "." + std::string{suffixes[index]};
    const auto signal = simulation.find_signal(path);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  capture.process_count = simulation.design_ir().processes().size();
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.run = simulation.run(until);
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index]
        = simulation.read_signal(signals[index]).to_msb_string();
  }
  if (capture_logic9) {
    constexpr std::array logic9_suffixes{
        std::string_view{"state_result_a"},
        std::string_view{"state_result_b"},
    };
    for (std::size_t index = 0; index < logic9_suffixes.size(); ++index) {
      const auto path
          = std::string{root} + "." + std::string{logic9_suffixes[index]};
      const auto signal = simulation.find_signal(path);
      assert(signal);
      capture.values[signals.size() + index]
          = simulation.read_signal(*signal).to_msb_string();
    }
  }
  return capture;
}

void test_vhdl_projected_slice_level(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-structural-signal-remap-application-test";
  config.project.top = "vhdl:work.structural_signal_remap_vhdl_app(test)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path
      = directory
      / (optimization == fsim::project::Optimization::o0
              ? "vhdl-cache-o0"
              : "vhdl-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project = fsim::app::build_project(config, diagnostics);
  auto compiled_project = fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  constexpr std::string_view root = "structural_signal_remap_vhdl_app";
  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      root,
      4U,
      true);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      root,
      4U,
      true);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(compiled.run.callbacks_executed
      <= reference.run.callbacks_executed);
  assert(reference.run.callbacks_executed
          - compiled.run.callbacks_executed
      <= compiled.compiled_processes);
  assert(compiled.values == reference.values);
  assert((reference.values == std::array<std::string, 8>{
                                  "10100101",
                                  "00111100",
                                  "11111111",
                                  "01100110",
                                  "11111111",
                                  "01100110",
                                  repeated_logic9_pattern("UX01ZWLH-", 65U),
                                  repeated_logic9_pattern("HLWZ10XU-", 65U)}));
#if defined(FSIM_HAS_LLVM)
  assert(reference.process_count == 131);
  // The four structurally identical projected-slice leaf instances share one
  // template; the independently eligible 35-operation top stimulus is the
  // fifth compiled process in its own module.
  assert(compiled.compiled_processes == 5);
  assert(compiled.compiled_modules == 2);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_level(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "structural-signal-remap-application-test";
  config.project.top = "sv:work.structural_signal_remap_app";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path
      = directory
      / (optimization == fsim::project::Optimization::o0
              ? "cache-o0"
              : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2005";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project = fsim::app::build_project(config, diagnostics);
  auto compiled_project = fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(reference.run.status == fsim::runtime::RunStatus::stopped);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(compiled.run.callbacks_executed == reference.run.callbacks_executed);
  assert(compiled.values == reference.values);
  assert((reference.values == std::array<std::string, 8>{
                                  "10100101",
                                  "00111100",
                                  "00000010",
                                  "01101001",
                                  "00000010",
                                  "01101001",
                                  "",
                                  ""}));
#if defined(FSIM_HAS_LLVM)
  assert(reference.process_count == 3);
  assert(compiled.compiled_processes == 3);
  assert(compiled.compiled_modules == 3);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_large_bound_literal_sharing(
    const std::filesystem::path& directory)
{
#if defined(FSIM_HAS_LLVM)
  const auto require = [](const bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto source = directory / "bound_literal_sharing.sv";
  {
    std::ofstream output(source);
    output << R"(
module bound_literal_leaf #(parameter integer VALUE = 0)(
  input logic [7:0] source,
  output logic [7:0] result
);
  logic [7:0] memory [0:1];
  initial begin
    memory[0] = VALUE;
    result = memory[0] ^ source;
  end
endmodule

module bound_literal_dummy;
  logic done;
  initial done = 1'b1;
endmodule

module bound_literal_sharing_top;
  logic [7:0] source_a = 8'h0f;
  logic [7:0] source_b = 8'hf0;
  wire [7:0] result_a;
  wire [7:0] result_b;
  bound_literal_leaf #(.VALUE(8'h21)) a(source_a, result_a);
  bound_literal_leaf #(.VALUE(8'hd4)) b(source_b, result_b);
)";
    for (std::size_t index = 0; index < 126U; ++index) {
      output << "  bound_literal_dummy dummy_" << index << "();\n";
    }
    output << "endmodule\n";
    require(output.good(), "cannot write bound-literal fixture");
  }
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "bound-literal-sharing-test";
  config.project.top = "sv:work.bound_literal_sharing_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o2;
  config.build.cache_path = directory / "bound-literal-cache";
  config.run.max_deltas = 1000U;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    print_diagnostics(diagnostics);
  }
  require(project.has_value(), "cannot build bound-literal fixture");
  auto state = std::move(project->design).state();
  std::size_t padded = 0U;
  std::array<bool, 2U> memory_instances_seen { };
  std::array<fsim::runtime::simir::ContainerObjectId, 2U>
      memory_object_ids { };
  for (auto& process : state.processes) {
    const auto has_container_write = std::ranges::any_of(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WriteContainerObjectElement>(operation);
        });
    if (!has_container_write) {
      continue;
    }
    std::optional<fsim::runtime::simir::ContainerObjectId>
        written_memory_object;
    for (const auto& operation : process.operations) {
      const auto* const write
          = fsim::runtime::simir::operation_get_if<
              fsim::runtime::simir::WriteContainerObjectElement>(
              &operation);
      if (write == nullptr) {
        continue;
      }
      require(!written_memory_object.has_value(),
          "bound-literal process writes multiple container objects");
      written_memory_object = write->object;
    }
    require(written_memory_object.has_value(),
        "bound-literal process lost its container-object write");
    const auto object_index
        = static_cast<std::size_t>(*written_memory_object);
    require(object_index < state.container_objects.size()
            && object_index < state.container_object_info.size(),
        "bound-literal write has no retained container-object backing");
    const auto& object = state.container_objects[object_index];
    const auto& object_info = state.container_object_info[object_index];
    require(object_info.id == *written_memory_object
            && object_info.name == object.name
            && object.initial_value.type == object_info.type,
        "bound-literal container-object metadata is inconsistent");
    const auto& memory_type = object.initial_value.type;
    require(memory_type.fixed
            && memory_type.element_kind
                == fsim::runtime::simir::ContainerElementKind::Packed
            && memory_type.element_width == 8U
            && memory_type.dimensions.size() == 1U
            && memory_type.dimensions.front().first == 0
            && memory_type.dimensions.front().second == 1
            && object.initial_value.elements.size() == 2U
            && !object.slice_alias.has_value(),
        "bound-literal instance memory has the wrong retained shape");
    const auto instance_index
        = object.name == "bound_literal_sharing_top.a.memory"
        ? std::optional<std::size_t> { 0U }
        : object.name == "bound_literal_sharing_top.b.memory"
        ? std::optional<std::size_t> { 1U }
        : std::nullopt;
    require(instance_index.has_value(),
        "bound-literal write does not target either leaf memory");
    require(!memory_instances_seen[*instance_index],
        "bound-literal leaf memory has multiple padded writers");
    memory_instances_seen[*instance_index] = true;
    memory_object_ids[*instance_index] = *written_memory_object;
    require(process.static_sensitivity.empty(),
        "bound-literal fixture is recurring");
    while (process.operations.size() < 8191U) {
      process.operations.push_back(fsim::runtime::simir::DebugPoint{});
    }
    process.operations.push_back(fsim::runtime::simir::Halt{});
    ++padded;
  }
  require(padded == 2U, "bound-literal fixture did not select two leaves");
  require(memory_instances_seen[0] && memory_instances_seen[1]
          && memory_object_ids[0] != memory_object_ids[1],
      "bound-literal leaves do not retain distinct memory objects");
  auto restored = fsim::elaboration::ElaboratedDesign::from_state(
      std::move(state));
  require(restored.has_value(), "cannot restore bound-literal fixture");
  project->design = std::move(*restored);

  const auto run = [&](fsim::app::BuiltProject built,
                      const fsim::app::SimulationEngine engine) {
    fsim::app::Simulation simulation(std::move(built), 1000U, engine);
    require(simulation.design_ir().processes().size() >= 128U,
        "bound-literal fixture did not enable selective compilation");
    if (engine == fsim::app::SimulationEngine::compiled) {
      require(simulation.compiled_process_count() == 2U,
          "bound-literal fixture did not select both large processes");
    }
    const auto modules = simulation.compiled_module_count();
    simulation.await_all_native_compilation();
    const auto result = simulation.run();
    require(result.status == fsim::runtime::RunStatus::completed,
        "bound-literal fixture did not complete");
    const auto a = simulation.find_signal(
        "bound_literal_sharing_top.a.result");
    const auto b = simulation.find_signal(
        "bound_literal_sharing_top.b.result");
    require(a.has_value() && b.has_value(),
        "bound-literal result signals are missing");
    return std::pair { std::array {
        simulation.read_signal(*a).to_msb_string(),
        simulation.read_signal(*b).to_msb_string() }, modules };
  };
  const auto reference = run(*project,
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = run(std::move(*project),
      fsim::app::SimulationEngine::compiled);
  require(reference.first == compiled.first,
      "bound-literal shared instance values differ from interpreter");
  require((compiled.first == std::array<std::string, 2>{
      "00101110", "00100100"}),
      "bound-literal shared instance values are wrong");
  require(compiled.second == 1U,
      "bound-literal instances did not share one large native module");
#endif
}

void test_signal_callback_operand_remap_capability()
{
#if defined(FSIM_HAS_LLVM)
  using Access =
      fsim::app::application_detail::SignalCallbackOperandTestAccess;
  const auto require = [](const bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  require(Access::mapped_signal(false, 7U) == 65U,
      "canonical callback ID 7 was not remapped to 65");
  require(Access::mapped_signal(false, 65U) == 1000U,
      "canonical callback ID 65 was not remapped to 1000");
  require(Access::mapped_signal(true, 65U) == 65U,
      "bound actual callback ID 65 was remapped a second time");
  require(Access::mapped_signal(true, 1000U) == 1000U,
      "bound actual callback ID 1000 was remapped a second time");
  require(Access::mapped_signal(true, UINT32_MAX) == UINT32_MAX,
      "an optional callback signal sentinel was remapped");
#endif
}

void test_shared_container_error_process_identity(
    const std::filesystem::path& directory)
{
#if defined(FSIM_HAS_LLVM)
  const auto require = [](const bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  };
  const auto source = directory / "shared_container_error.sv";
  {
    std::ofstream output(source);
    output << R"(
module shared_error_leaf(input logic [31:0] index);
  logic [7:0] memory [0:1];
  logic [7:0] result;
  initial begin
    memory[index] = 8'h5a;
    result = memory[0];
  end
endmodule

module shared_error_dummy;
  logic done;
  initial done = 1'b1;
endmodule

module shared_container_error_top;
  logic [31:0] index_a = 32'd0;
  logic [31:0] index_b = 32'bx;
  shared_error_leaf a(index_a);
  shared_error_leaf b(index_b);
)";
    for (std::size_t index = 0; index < 126U; ++index) {
      output << "  shared_error_dummy dummy_" << index << "();\n";
    }
    output << "endmodule\n";
    require(output.good(), "cannot write shared-container-error fixture");
  }
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "shared-container-error-test";
  config.project.top = "sv:work.shared_container_error_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o2;
  config.build.cache_path = directory / "shared-container-error-cache";
  config.run.max_deltas = 1000U;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    print_diagnostics(diagnostics);
  }
  require(project.has_value(), "cannot build shared-container-error fixture");
  auto state = std::move(project->design).state();
  std::optional<fsim::runtime::simir::ProcessId> representative_process;
  std::optional<fsim::runtime::simir::ProcessId> failing_process;
  std::size_t padded = 0U;
  std::size_t synthetic_error_guards = 0U;
  for (auto& process : state.processes) {
    std::optional<std::size_t> selected_write;
    for (std::size_t operation_index = 0U;
         operation_index < process.operations.size();
         ++operation_index) {
      const auto& operation = process.operations[operation_index];
      const auto* write = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::WriteContainerObjectElement>(&operation);
      if (write == nullptr) {
        continue;
      }
      require(write->object < state.container_objects.size(),
          "shared-container-error object ID is invalid");
      const auto& name = state.container_objects[write->object].name;
      if (name.find(".a.memory") != std::string::npos) {
        require(!selected_write.has_value(),
            "shared-container-error representative has multiple writes");
        representative_process = process.id;
        selected_write = operation_index;
      }
      if (name.find(".b.memory") != std::string::npos) {
        require(!selected_write.has_value(),
            "shared-container-error failing process has multiple writes");
        failing_process = process.id;
        selected_write = operation_index;
      }
    }
    if (selected_write) {
      require(*selected_write >= 3U,
          "shared-container-error write has no fixed-index guard");
      const auto guard_index = *selected_write - 1U;
      const auto* guard = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::Branch>(&process.operations[guard_index]);
      const auto* comparison = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::Binary>(&process.operations[guard_index - 1U]);
      const auto* sentinel = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::LoadConstant>(
              &process.operations[guard_index - 2U]);
      const auto* write = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::WriteContainerObjectElement>(
              &process.operations[*selected_write]);
      std::optional<std::int64_t> sentinel_value;
      if (sentinel != nullptr) {
        sentinel_value = sentinel->value.known_signed_value();
      }
      const auto write_target = guard == nullptr
          ? fsim::runtime::simir::InstructionIndex { }
          : guard->when_true;
      require(guard != nullptr
              && guard->when_true == *selected_write
              && guard->when_false == *selected_write + 1U
              && guard->unknown_policy
                  == fsim::runtime::simir::UnknownBranchPolicy::when_false
              && comparison != nullptr
              && comparison->operation
                  == fsim::runtime::simir::BinaryOperator::not_equal
              && guard->condition == comparison->destination
              && write != nullptr
              && sentinel != nullptr
              && sentinel_value.has_value()
              && *sentinel_value == std::numeric_limits<std::int64_t>::min()
              && ((comparison->lhs == write->index
                      && comparison->rhs == sentinel->destination)
                  || (comparison->rhs == write->index
                      && comparison->lhs == sentinel->destination)),
          "shared-container-error lost the generated sentinel skip guard");

      // The HDL lowering correctly skips an X-index write. For this test of
      // shared-code callback error identity, bypass only the final generated
      // sentinel guard in both leaf programs so the checked container write
      // receives the invalid-index sentinel and reports the actual process.
      process.operations[guard_index]
          = fsim::runtime::simir::Jump { write_target };
      ++synthetic_error_guards;
    }
    const auto has_container_write = std::ranges::any_of(
        process.operations,
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WriteContainerObjectElement>(operation);
        });
    if (!has_container_write) {
      continue;
    }
    require(process.static_sensitivity.empty(),
        "shared-container-error process is recurring");
    while (process.operations.size() < 8191U) {
      process.operations.push_back(fsim::runtime::simir::DebugPoint{});
    }
    process.operations.push_back(fsim::runtime::simir::Halt{});
    ++padded;
  }
  require(padded == 2U && synthetic_error_guards == 2U
          && representative_process.has_value()
          && failing_process.has_value()
          && *representative_process < *failing_process,
      "shared-container-error fixture did not identify two leaves");
  auto restored = fsim::elaboration::ElaboratedDesign::from_state(
      std::move(state));
  require(restored.has_value(), "cannot restore shared-container-error fixture");
  project->design = std::move(*restored);

  // The shared A process is a valid representative. Give only the later B
  // instance malformed expression-profile metadata so the compiler must
  // validate that candidate instead of inheriting the representative's
  // supports_process result.
  auto invalid_candidate_project = *project;
  auto invalid_candidate_state = invalid_candidate_project.design.state();
  const auto find_process = [&](const fsim::runtime::simir::ProcessId id) {
    return std::ranges::find_if(
        invalid_candidate_state.processes,
        [id](const fsim::runtime::simir::Process& process) {
          return process.id == id;
        });
  };
  const auto representative_before = find_process(*representative_process);
  const auto candidate_before = find_process(*failing_process);
  require(representative_before != invalid_candidate_state.processes.end()
          && candidate_before != invalid_candidate_state.processes.end(),
      "shared-container-error metadata fixture lost a selected process");
  const auto representative_body
      = representative_before->operations.body_identity();
  const auto representative_operation_count
      = representative_before->operations.size();
  const auto representative_profiles
      = representative_before->expression_profiles;
  const auto candidate_body = candidate_before->operations.body_identity();
  const auto candidate_operation_count = candidate_before->operations.size();
  const auto candidate_profiles = candidate_before->expression_profiles;
  require(representative_body != nullptr && candidate_body != nullptr,
      "shared-container-error process bodies are missing");
  const fsim::runtime::simir::ExpressionProfile invalid_profile {
      fsim::runtime::simir::SourceLocation {
          "shared-container-candidate.sv", 1U, 1U },
      1U,
      false,
      static_cast<fsim::runtime::simir::ExpressionSizingKind>(99U),
      fsim::runtime::simir::ExpressionValueDomain::four_state };
  bool invalid_profile_installed = false;
  for (auto& process : invalid_candidate_state.processes) {
    if (process.id != *failing_process) {
      continue;
    }
    process.expression_profiles.push_back(invalid_profile);
    invalid_profile_installed = true;
  }
  require(invalid_profile_installed,
      "shared-container-error candidate process was not found");
  const auto representative_after = find_process(*representative_process);
  const auto candidate_after = find_process(*failing_process);
  require(representative_after != invalid_candidate_state.processes.end()
          && representative_after->operations.body_identity()
              == representative_body
          && representative_after->operations.size()
              == representative_operation_count
          && representative_after->expression_profiles
              == representative_profiles
          && candidate_after != invalid_candidate_state.processes.end()
          && candidate_after->operations.body_identity() == candidate_body
          && candidate_after->operations.size() == candidate_operation_count,
      "candidate metadata mutation changed a selected process body");
  auto old_profile = candidate_profiles.begin();
  auto new_profile = candidate_after->expression_profiles.begin();
  for (; old_profile != candidate_profiles.end(); ++old_profile) {
    require(new_profile != candidate_after->expression_profiles.end()
            && *old_profile == *new_profile,
        "candidate metadata mutation changed its existing profiles");
    ++new_profile;
  }
  require(new_profile != candidate_after->expression_profiles.end()
          && *new_profile == invalid_profile
          && ++new_profile == candidate_after->expression_profiles.end(),
      "candidate metadata mutation was not limited to one invalid profile");
  auto invalid_candidate_design
      = fsim::elaboration::ElaboratedDesign::from_state(
          std::move(invalid_candidate_state));
  require(invalid_candidate_design.has_value(),
      "design-state validation rejected the JIT-only candidate metadata");
  invalid_candidate_project.design = std::move(*invalid_candidate_design);
  bool candidate_validation_rejected = false;
  try {
    fsim::app::Simulation invalid_candidate_simulation(
        std::move(invalid_candidate_project), 1000U,
        fsim::app::SimulationEngine::compiled);
    invalid_candidate_simulation.await_all_native_compilation();
  } catch (const std::exception& error) {
    constexpr std::string_view expected
        = "expression profile has an invalid sizing kind";
    require(std::string_view { error.what() }.find(expected)
            != std::string_view::npos,
        "candidate metadata failed for a reason other than JIT validation");
    candidate_validation_rejected = true;
  }
  require(candidate_validation_rejected,
      "candidate reused the valid representative's validation result");

  const auto run = [&](fsim::app::BuiltProject built,
                      const fsim::app::SimulationEngine engine) {
    fsim::app::Simulation simulation(std::move(built), 1000U, engine);
    require(simulation.design_ir().processes().size() >= 128U,
        "shared-container-error fixture did not enable selective compilation");
    if (engine == fsim::app::SimulationEngine::compiled) {
      require(simulation.compiled_process_count() == 2U,
          "shared-container-error fixture did not select both leaves");
      if (simulation.compiled_module_count() != 1U) {
        throw std::runtime_error(
            "shared-container-error module count "
            + std::to_string(simulation.compiled_module_count()));
      }
      simulation.await_all_native_compilation();
    }
    try {
      (void)simulation.run();
    } catch (const fsim::runtime::simir::InterpreterError& error) {
      return error.process();
    }
    throw std::runtime_error("shared-container-error fixture did not fail");
  };
  require(run(*project, fsim::app::SimulationEngine::interpreter)
          == *failing_process,
      "interpreted container error has the wrong process ID");
  require(run(std::move(*project), fsim::app::SimulationEngine::compiled)
          == *failing_process,
      "shared compiled container error has the wrong process ID");
#endif
}

}  // namespace

int main() {
  const auto nonce
      = std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-structural-signal-remap-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "structural_signal_remap.sv";
  {
    std::ofstream output(source);
    output << R"(
module structural_remap_leaf(
  input  logic       clock,
  input  logic [7:0] source,
  output logic [7:0] result
);
  always_ff @(posedge clock) begin
    result <= (source ^ 8'h5a) + 8'h03;
  end
endmodule

module structural_signal_remap_app;
  logic       clock;
  logic [7:0] source_a;
  logic [7:0] source_b;
  logic [7:0] result_a;
  logic [7:0] result_b;

  structural_remap_leaf a(clock, source_a, result_a);
  structural_remap_leaf b(clock, source_b, result_b);

  initial begin
    clock = 1'b0;
    source_a = 8'h12;
    source_b = 8'hc3;
    #1;
    clock = 1'b1;
    #1;
    clock = 1'b0;
    source_a = 8'ha5;
    source_b = 8'h3c;
    #1;
    clock = 1'b1;
    #1;
    $finish;
  end
endmodule
)";
  }
  const auto vhdl_source
      = directory.path / "structural_signal_remap.vhd";
  const auto state_source_a = repeated_logic9_pattern("UX01ZWLH-", 65U);
  const auto state_source_b = repeated_logic9_pattern("HLWZ10XU-", 65U);
  {
    std::ofstream output(vhdl_source);
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity projected_slice_leaf is
  port (
    clock        : in  std_logic;
    source       : in  std_logic_vector(7 downto 0);
    result       : out std_logic_vector(7 downto 0);
    state_source : in  std_logic_vector(64 downto 0);
    state_result : out std_logic_vector(64 downto 0)
  );
end entity;

architecture rtl of projected_slice_leaf is
begin
  process (clock)
  begin
    if rising_edge(clock) then
      assert not is_x(source)
        report "structurally shared input contains an unknown value"
        severity warning;
      result(3 downto 0) <= source(3 downto 0) xor "1010";
      result(7 downto 4) <= source(7 downto 4) xor "0101";
      state_result <= state_source;
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity structural_signal_remap_vhdl_app is
end entity;

architecture test of structural_signal_remap_vhdl_app is
  signal clock    : std_logic := '0';
  signal source_a : std_logic_vector(7 downto 0) := x"12";
  signal source_b : std_logic_vector(7 downto 0) := x"c3";
  signal result_a : std_logic_vector(7 downto 0);
  signal result_b : std_logic_vector(7 downto 0);
  signal result_c : std_logic_vector(7 downto 0);
  signal result_d : std_logic_vector(7 downto 0);
  signal state_source_a : std_logic_vector(64 downto 0) := ")";
    output << state_source_a << "\";\n"
           << "  signal state_source_b : std_logic_vector(64 downto 0) := \""
           << state_source_b << "\";\n"
           << R"(  signal state_result_a : std_logic_vector(64 downto 0);
  signal state_result_b : std_logic_vector(64 downto 0);
  signal state_result_c : std_logic_vector(64 downto 0);
  signal state_result_d : std_logic_vector(64 downto 0);
begin
  a : entity work.projected_slice_leaf
    port map (clock => clock, source => source_a, result => result_a,
      state_source => state_source_a, state_result => state_result_a);
  b : entity work.projected_slice_leaf
    port map (clock => clock, source => source_b, result => result_b,
      state_source => state_source_b, state_result => state_result_b);
  c : entity work.projected_slice_leaf
    port map (clock => clock, source => source_a, result => result_c,
      state_source => state_source_a, state_result => state_result_c);
  d : entity work.projected_slice_leaf
    port map (clock => clock, source => source_b, result => result_d,
      state_source => state_source_b, state_result => state_result_d);

  dummy_processes : for index in 0 to 125 generate
    dormant : process
    begin
      wait;
    end process;
  end generate;

  stimulus : process
  begin
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    clock <= '0';
    source_a <= x"a5";
    source_b <= x"3c";
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    wait;
  end process;
end architecture;
)";
  }

  test_level(
      directory.path, source, fsim::project::Optimization::o0);
  test_level(
      directory.path, source, fsim::project::Optimization::o2);
  test_vhdl_projected_slice_level(
      directory.path, vhdl_source, fsim::project::Optimization::o0);
  test_vhdl_projected_slice_level(
      directory.path, vhdl_source, fsim::project::Optimization::o2);
  test_large_bound_literal_sharing(directory.path);
  test_signal_callback_operand_remap_capability();
  test_shared_container_error_process_identity(directory.path);
  return 0;
}
