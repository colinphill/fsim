// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_test_runner.hpp"

#include <charconv>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kRunInvalid{"FSIM-UVM-RUN-001"};
constexpr std::string_view kRunResource{"FSIM-UVM-RUN-002"};
constexpr std::string_view kRunFatal{"FSIM-UVM-RUN-003"};

[[noreturn]] void fail(
    const std::string_view code, const std::string_view message) {
  throw SystemVerilogUvmRunError{std::string{code}, std::string{message}};
}

[[nodiscard]] std::uint64_t parse_seed(const std::string_view value) {
  std::uint64_t result{};
  const auto parsed = std::from_chars(
      value.data(), value.data() + value.size(), result, 10);
  if (value.empty() || parsed.ec != std::errc{}
      || parsed.ptr != value.data() + value.size()) {
    fail(kRunInvalid, "UVM run seed is not an unsigned decimal value");
  }
  return result;
}

}  // namespace

SystemVerilogUvmRunError::SystemVerilogUvmRunError(
    std::string code, std::string message)
    : std::runtime_error{std::move(message)}, diagnostic_code_{std::move(code)} {}

SystemVerilogUvmTestRunnerService::SystemVerilogUvmTestRunnerService(
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmFactoryService& factory,
    SystemVerilogUvmCommandLineService& command_line,
    SystemVerilogUvmRunLimits limits)
    : objects_{&objects}, components_{&components}, factory_{&factory},
      command_line_{&command_line}, limits_{std::move(limits)} {
  if (limits_.maximum_runs == 0 || limits_.maximum_test_name_bytes == 0
      || limits_.maximum_topology_bytes == 0
      || limits_.maximum_topology_components == 0
      || limits_.maximum_message_bytes == 0) {
    fail(kRunInvalid, "UVM run-test limits must be nonzero");
  }
}

std::string SystemVerilogUvmTestRunnerService::select_test_name(
    const SystemVerilogUvmRunOptions& options) const {
  std::string selected;
  if (options.test_name) {
    selected = *options.test_name;
  } else {
    const auto values = command_line_->get_arg_values("+UVM_TESTNAME=");
    if (!values.empty()) selected = values.front();
  }
  if (selected.empty()) {
    fail(kRunInvalid, "UVM run_test has no selected test name");
  }
  if (selected.size() > limits_.maximum_test_name_bytes) {
    fail(kRunResource, "UVM run_test name exceeds its text ceiling");
  }
  return selected;
}

std::uint64_t SystemVerilogUvmTestRunnerService::select_seed(
    const SystemVerilogUvmRunOptions& options) const {
  if (options.seed) return *options.seed;
  for (const auto prefix : {std::string_view{"+ntb_random_seed="},
                            std::string_view{"+UVM_SEED="}}) {
    if (const auto value = command_line_->get_arg_value(prefix)) {
      return parse_seed(*value);
    }
  }
  return 1;
}

std::optional<SimulationTick>
SystemVerilogUvmTestRunnerService::select_timeout(
    const SystemVerilogUvmRunOptions& options) const {
  if (options.timeout) return options.timeout;
  if (command_line_->settings().timeout) {
    return command_line_->settings().timeout->ticks;
  }
  return std::nullopt;
}

void SystemVerilogUvmTestRunnerService::append_topology(
    std::string& output,
    const SystemVerilogClassHandle component,
    std::size_t& count) const {
  if (++count > limits_.maximum_topology_components) {
    fail(kRunResource, "UVM topology component ceiling was exceeded");
  }
  const auto snapshot = components_->snapshot(component);
  const auto line = std::string(snapshot.depth * 2U, ' ') + snapshot.full_name
      + " (" + objects_->type_name(component) + ")\n";
  if (line.size() > limits_.maximum_topology_bytes - output.size()) {
    fail(kRunResource, "UVM topology text ceiling was exceeded");
  }
  output += line;
  for (const auto child : components_->children(component)) {
    append_topology(output, child, count);
  }
}

std::string SystemVerilogUvmTestRunnerService::topology(
    const SystemVerilogUvmRootHandle root) const {
  if (!components_->contains_root(root)) {
    fail(kRunInvalid, "UVM topology root is empty or stale");
  }
  std::string result{"UVM Topology\n"};
  if (result.size() > limits_.maximum_topology_bytes) {
    fail(kRunResource, "UVM topology text ceiling was exceeded");
  }
  std::size_t count{};
  for (const auto component : components_->top_components(root)) {
    append_topology(result, component, count);
  }
  return result;
}

SystemVerilogUvmRunResult SystemVerilogUvmTestRunnerService::run_test(
    const SystemVerilogUvmRootHandle root,
    SystemVerilogUvmRunOptions options,
    Execution execution) {
  if (running_ || !components_->contains_root(root)) {
    fail(kRunInvalid, "UVM run_test is reentrant or has a stale root");
  }
  if (run_count_ >= limits_.maximum_runs) {
    fail(kRunResource, "UVM run_test count ceiling was exceeded");
  }
  running_ = true;
  struct RunningGuard {
    bool* running;
    ~RunningGuard() { *running = false; }
  } guard{&running_};
  SystemVerilogUvmRunResult result;
  result.run_identity = ++run_count_;
  result.test_name = select_test_name(options);
  result.seed = select_seed(options);
  result.timeout = select_timeout(options);
  SystemVerilogClassHandle test{};
  try {
    test = factory_->create_component_by_name(
        result.test_name, {}, "uvm_test_top", 0, root);
    if (options.print_topology) result.topology = topology(root);
    auto outcome = execution
        ? execution(test, result.seed)
        : SystemVerilogUvmRunExecution{};
    result.status = outcome.status;
    result.elapsed_ticks = outcome.elapsed_ticks;
    result.message = std::move(outcome.message);
    if (result.message.size() > limits_.maximum_message_bytes) {
      fail(kRunResource, "UVM run_test message exceeds its text ceiling");
    }
    if (result.timeout && result.elapsed_ticks > *result.timeout) {
      result.status = SystemVerilogUvmRunStatus::TimedOut;
      result.diagnostic_code = kRunResource;
      result.message = "UVM global timeout expired";
    } else if (result.status == SystemVerilogUvmRunStatus::Fatal) {
      result.diagnostic_code = kRunFatal;
    }
  } catch (const SystemVerilogUvmRunError&) {
    if (test && components_->contains(test)) components_->release(test);
    throw;
  } catch (const std::exception& error) {
    result.status = SystemVerilogUvmRunStatus::Fatal;
    result.diagnostic_code = kRunFatal;
    result.message = error.what();
  } catch (...) {
    result.status = SystemVerilogUvmRunStatus::Fatal;
    result.diagnostic_code = kRunFatal;
    result.message = "unknown UVM run_test execution failure";
  }
  if (options.cleanup && test && components_->contains(test)) {
    components_->release(test);
    result.cleaned = true;
  }
  return result;
}

}  // namespace fsim::runtime
