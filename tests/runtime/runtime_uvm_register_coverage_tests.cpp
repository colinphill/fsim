// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_register_model.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using Scope = SystemVerilogUvmRegisterCallbackScope;
using Phase = SystemVerilogUvmRegisterCallbackPhase;

void require_coverage(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

template <typename Function>
void require_coverage_error(const std::string_view code, Function &&function,
                            const std::string_view message) {
  try {
    function();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require_coverage(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::uint32_t phase_bit(const Phase phase) {
  return UINT32_C(1) << static_cast<std::uint8_t>(phase);
}

[[nodiscard]] std::string_view phase_name(const Phase phase) {
  switch (phase) {
  case Phase::PreRead:
    return "pre_read";
  case Phase::PostRead:
    return "post_read";
  case Phase::PreWrite:
    return "pre_write";
  case Phase::PostWrite:
    return "post_write";
  }
  return "invalid";
}

struct CoverageFixture {
  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRegisterModelService model;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmRegisterBlockHandle top;
  SystemVerilogUvmRegisterMapHandle map;
  SystemVerilogUvmRegisterMapHandle read_only;
  SystemVerilogUvmRegisterHandle reg;
  SystemVerilogUvmRegisterFieldHandle field;
  SystemVerilogUvmRegisterMemoryHandle memory;

  explicit CoverageFixture(SystemVerilogUvmRegisterModelLimits limits = {})
      : objects(
            heap, [](std::string_view, std::string_view, std::string_view) {
              return SystemVerilogClassHandle{};
            }),
        components(heap, objects), model(components, limits) {
    root = components.create_root("register_coverage");
    top = model.create_block({root, std::nullopt, "model"});
    map = model.create_map(
        {top, "bus", 0x100, 4,
         SystemVerilogUvmRegisterMapEndianness::Little, true});
    read_only = model.create_map(
        {top, "read_only", 0x200, 4,
         SystemVerilogUvmRegisterMapEndianness::Little, true});
    reg = model.create_register({top, "control", 8, 0});
    field = model.create_field(
        {reg, "value", 8, 0, SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    model.set_reset(field, "HARD", PackedLogic4::from_aval_bval(8, 0x12, 0));
    memory = model.create_memory(
        {top, "samples", 8, 0x10, {2},
         SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    model.add_register(map, reg, 0);
    model.add_memory(map, memory, 0x10);
    model.add_register(read_only, reg, 0,
                       SystemVerilogUvmRegisterMapRights::ReadOnly);
    model.lock_model(top);
    model.reset(top, "HARD");
  }

  [[nodiscard]] SystemVerilogUvmRegisterCallbackHandle add_callback(
      const Scope scope, std::string name,
      SystemVerilogUvmRegisterCallbackDescriptor::Callback callback,
      const std::uint32_t phases = UINT32_C(0x0f),
      const std::int32_t priority = 0) {
    SystemVerilogUvmRegisterCallbackDescriptor descriptor;
    descriptor.scope = scope;
    switch (scope) {
    case Scope::Block:
      descriptor.block = top;
      break;
    case Scope::Map:
      descriptor.map = map;
      break;
    case Scope::Register:
      descriptor.reg = reg;
      break;
    case Scope::Memory:
      descriptor.memory = memory;
      break;
    case Scope::Field:
      descriptor.field = field;
      break;
    }
    descriptor.name = std::move(name);
    descriptor.phases = phases;
    descriptor.priority = priority;
    descriptor.callback = std::move(callback);
    return model.register_callback(std::move(descriptor));
  }

  [[nodiscard]] SystemVerilogUvmRegisterCoverageHandle add_coverage(
      const bool per_map = true, const bool per_field = true,
      const bool cross = true) {
    return model.register_coverage_model(
        {top, "functional", per_map, per_field, cross});
  }
};

} // namespace

void test_systemverilog_uvm_register_callbacks_and_coverage() {
  CoverageFixture fixture;
  const auto coverage = fixture.add_coverage();
  std::vector<std::string> order;
  const auto record = [&](const std::string_view scope) {
    return [&, scope](SystemVerilogUvmRegisterCallbackContext &context) {
      order.push_back(std::string{scope} + ":" +
                      std::string{phase_name(context.phase)});
    };
  };
  const auto block = fixture.add_callback(Scope::Block, "block", record("block"));
  const auto map = fixture.add_callback(Scope::Map, "map", record("map"));
  const auto reg = fixture.add_callback(Scope::Register, "register",
                                        record("register"));
  const auto field = fixture.add_callback(
      Scope::Field, "field", [&](SystemVerilogUvmRegisterCallbackContext &context) {
        order.push_back("field:" + std::string{phase_name(context.phase)});
        if (context.phase == Phase::PreWrite) {
          context.value = PackedLogic4::from_aval_bval(8, 0x5a, 0);
        } else if (context.phase == Phase::PostRead) {
          context.value = PackedLogic4::from_aval_bval(8, 0xa5, 0);
        }
      });
  const auto memory_callback =
      fixture.add_callback(Scope::Memory, "memory", record("memory"));

  const SystemVerilogUvmRegisterTarget register_target{
      fixture.reg, std::nullopt, std::nullopt, 0};
  const auto written = fixture.model.callback_write(
      register_target, PackedLogic4::from_aval_bval(8, 0xff, 0), fixture.map);
  const auto read = fixture.model.callback_read(register_target, fixture.map);
  require_coverage(
      written.success() && read.success() &&
          fixture.model.get_mirrored(fixture.reg).low_word().aval == 0xa5 &&
          read.value.low_word().aval == 0xa5,
      "pre-write and post-read callbacks must mutate exact values and prediction");
  require_coverage(
      order == std::vector<std::string>{
                   "block:pre_write", "map:pre_write", "register:pre_write",
                   "field:pre_write", "field:post_write",
                   "register:post_write", "map:post_write", "block:post_write",
                   "block:pre_read", "map:pre_read", "register:pre_read",
                   "field:pre_read", "field:post_read", "register:post_read",
                   "map:post_read", "block:post_read"},
      "register callback scope order must be stable and reverse after access");
  require_coverage(
      fixture.model.callback_snapshot(block).invocations == 4 &&
          fixture.model.callback_snapshot(map).invocations == 4 &&
          fixture.model.callback_snapshot(reg).invocations == 4 &&
          fixture.model.callback_snapshot(field).invocations == 4 &&
          fixture.model.callback_snapshot(memory_callback).invocations == 0,
      "callback inventories must retain exact per-scope invocation counts");

  order.clear();
  const SystemVerilogUvmRegisterTarget memory_target{
      std::nullopt, std::nullopt, fixture.memory, 1};
  const auto memory_write = fixture.model.callback_write(
      memory_target, PackedLogic4::from_aval_bval(8, 0x3c, 0), fixture.map);
  require_coverage(
      memory_write.success() &&
          order == std::vector<std::string>{
                       "block:pre_write", "map:pre_write", "memory:pre_write",
                       "memory:post_write", "map:post_write",
                       "block:post_write"},
      "memory callbacks must compose block, map, and memory scopes exactly");

  const auto sampled = fixture.model.coverage_snapshot(coverage);
  require_coverage(sampled.samples == 3 && sampled.bins.size() == 6 &&
                       sampled.bins.at("map:2:write") == 2 &&
                       sampled.bins.at("map:2:read") == 1,
                   "coverage must retain per-map, per-field, and reset crosses");

  const auto throwing = fixture.add_callback(
      Scope::Field, "throwing",
      [](auto &) { throw std::runtime_error{"injected register callback failure"}; },
      phase_bit(Phase::PreWrite), 100);
  const auto before = fixture.model.get_mirrored(fixture.reg);
  const auto failed = fixture.model.callback_write(
      register_target, PackedLogic4::from_aval_bval(8, 0, 0), fixture.map);
  require_coverage(!failed.success() &&
                       failed.message == "injected register callback failure" &&
                       fixture.model.get_mirrored(fixture.reg) == before &&
                       fixture.model.callback_snapshot(throwing).failures == 1,
                   "callback exceptions must be contained before model mutation");
  const auto invalid_mutation = fixture.add_callback(
      Scope::Field, "invalid_mutation",
      [](auto &context) {
        context.value = PackedLogic4::from_aval_bval(4, 0, 0);
      },
      phase_bit(Phase::PreWrite), 200);
  const auto invalid_result = fixture.model.callback_write(
      register_target, PackedLogic4::from_aval_bval(8, 0, 0), fixture.map);
  require_coverage(
      !invalid_result.success() &&
          invalid_result.message ==
              "callback produced an invalid access mutation" &&
          fixture.model.get_mirrored(fixture.reg) == before &&
          fixture.model.callback_snapshot(invalid_mutation).failures == 1,
      "invalid callback mutations must be contained and counted exactly");
  require_coverage_error(
      "FSIM-UVM-REG-013",
      [&] {
        (void)fixture.model.callback_write(
            register_target, PackedLogic4::from_aval_bval(8, 1, 0),
            fixture.read_only);
      },
      "selected-map rights failures must be cataloged before callback dispatch");

  SystemVerilogUvmRegisterStandardSequenceOptions sequence;
  sequence.kind = SystemVerilogUvmRegisterStandardSequenceKind::Traverse;
  sequence.top = fixture.top;
  const auto traversal = fixture.model.run_standard_sequence(sequence);
  require_coverage(traversal.success() &&
                       fixture.model.register_callbacks().size() == 7 &&
                       fixture.model.coverage_models().size() == 1,
                   "register callbacks and coverage must coexist with sequences");
}

void test_systemverilog_uvm_register_callback_coverage_limits() {
  SystemVerilogUvmRegisterModelLimits callback_limits;
  callback_limits.maximum_register_callbacks = 1;
  CoverageFixture callback_bounded{callback_limits};
  (void)callback_bounded.add_callback(Scope::Block, "one", [](auto &) {});
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] {
        (void)callback_bounded.add_callback(Scope::Register, "two",
                                            [](auto &) {});
      },
      "callback inventory ceilings must reject before publication");

  SystemVerilogUvmRegisterModelLimits dispatch_limits;
  dispatch_limits.maximum_register_callbacks_per_access = 1;
  CoverageFixture dispatch_bounded{dispatch_limits};
  (void)dispatch_bounded.add_callback(Scope::Block, "block", [](auto &) {});
  (void)dispatch_bounded.add_callback(Scope::Register, "register", [](auto &) {});
  const SystemVerilogUvmRegisterTarget dispatch_target{
      dispatch_bounded.reg, std::nullopt, std::nullopt, 0};
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] {
        (void)dispatch_bounded.model.callback_read(dispatch_target,
                                                   dispatch_bounded.map);
      },
      "per-access callback ceilings must reject before invocation");

  SystemVerilogUvmRegisterModelLimits invocation_limits;
  invocation_limits.maximum_register_callback_invocations = 1;
  CoverageFixture invocation_bounded{invocation_limits};
  (void)invocation_bounded.add_callback(Scope::Block, "block", [](auto &) {});
  (void)invocation_bounded.add_callback(Scope::Register, "register", [](auto &) {});
  const SystemVerilogUvmRegisterTarget invocation_target{
      invocation_bounded.reg, std::nullopt, std::nullopt, 0};
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] {
        (void)invocation_bounded.model.callback_read(invocation_target,
                                                     invocation_bounded.map);
      },
      "aggregate callback invocation ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits failure_limits;
  failure_limits.maximum_register_callback_failures = 1;
  CoverageFixture failure_bounded{failure_limits};
  (void)failure_bounded.add_callback(
      Scope::Block, "throwing",
      [](auto &) { throw std::runtime_error{"failure"}; },
      phase_bit(Phase::PreRead));
  const SystemVerilogUvmRegisterTarget failure_target{
      failure_bounded.reg, std::nullopt, std::nullopt, 0};
  require_coverage(
      !failure_bounded.model.callback_read(failure_target, failure_bounded.map)
           .success(),
      "first bounded callback failure must be contained");
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] {
        (void)failure_bounded.model.callback_read(failure_target,
                                                  failure_bounded.map);
      },
      "aggregate callback failure ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits model_limits;
  model_limits.maximum_register_coverage_models = 1;
  CoverageFixture model_bounded{model_limits};
  (void)model_bounded.add_coverage();
  require_coverage_error(
      "FSIM-UVM-REG-014", [&] { (void)model_bounded.add_coverage(); },
      "coverage-model ceilings must reject before publication");

  SystemVerilogUvmRegisterModelLimits sample_limits;
  sample_limits.maximum_register_coverage_samples = 1;
  CoverageFixture sample_bounded{sample_limits};
  (void)sample_bounded.add_coverage(true, false, false);
  const SystemVerilogUvmRegisterTarget sample_target{
      sample_bounded.reg, std::nullopt, std::nullopt, 0};
  require_coverage(
      sample_bounded.model.callback_read(sample_target, sample_bounded.map)
          .success(),
      "first bounded coverage sample must succeed");
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] {
        (void)sample_bounded.model.callback_read(sample_target,
                                                 sample_bounded.map);
      },
      "aggregate coverage sample ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits bin_limits;
  bin_limits.maximum_register_coverage_bins = 1;
  CoverageFixture bin_bounded{bin_limits};
  (void)bin_bounded.add_coverage(true, true, false);
  const SystemVerilogUvmRegisterTarget bin_target{
      bin_bounded.reg, std::nullopt, std::nullopt, 0};
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] { (void)bin_bounded.model.callback_read(bin_target, bin_bounded.map); },
      "aggregate coverage bin ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits byte_limits;
  byte_limits.maximum_register_coverage_bin_bytes = 4;
  CoverageFixture byte_bounded{byte_limits};
  (void)byte_bounded.add_coverage(true, false, false);
  const SystemVerilogUvmRegisterTarget byte_target{
      byte_bounded.reg, std::nullopt, std::nullopt, 0};
  require_coverage_error(
      "FSIM-UVM-REG-014",
      [&] {
        (void)byte_bounded.model.callback_read(byte_target, byte_bounded.map);
      },
      "aggregate coverage bin-byte ceilings must reject exactly");

  CoverageFixture invalid;
  SystemVerilogUvmRegisterCallbackDescriptor descriptor;
  descriptor.scope = Scope::Block;
  descriptor.block = invalid.top;
  descriptor.name = "invalid";
  descriptor.phases = 0;
  descriptor.callback = [](auto &) {};
  require_coverage_error(
      "FSIM-UVM-REG-013",
      [&] { (void)invalid.model.register_callback(descriptor); },
      "invalid callback phase masks must be cataloged");
}

} // namespace fsim::tests::runtime
