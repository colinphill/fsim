// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_register_model.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using Kind = SystemVerilogUvmRegisterStandardSequenceKind;
using Rights = SystemVerilogUvmRegisterMapRights;

void require_sequence(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

template <typename Function>
void require_sequence_error(const std::string_view code, Function &&function,
                            const std::string_view message) {
  try {
    function();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require_sequence(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

struct SequenceFixture {
  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRegisterModelService model;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmRegisterBlockHandle top;
  SystemVerilogUvmRegisterMapHandle primary;
  SystemVerilogUvmRegisterMapHandle shared;
  SystemVerilogUvmRegisterMapHandle read_only;
  SystemVerilogUvmRegisterHandle reg;
  SystemVerilogUvmRegisterFieldHandle field;
  SystemVerilogUvmRegisterMemoryHandle memory;

  explicit SequenceFixture(SystemVerilogUvmRegisterModelLimits limits = {})
      : objects(
            heap, [](std::string_view, std::string_view, std::string_view) {
              return SystemVerilogClassHandle{};
            }),
        components(heap, objects), model(components, limits) {
    root = components.create_root("standard_sequences");
    top = model.create_block({root, std::nullopt, "registers"});
    primary = model.create_map(
        {top, "primary", 0x100, 4,
         SystemVerilogUvmRegisterMapEndianness::Little, true});
    shared = model.create_map(
        {top, "shared", 0x200, 4,
         SystemVerilogUvmRegisterMapEndianness::Little, true});
    read_only = model.create_map(
        {top, "read_only", 0x300, 4,
         SystemVerilogUvmRegisterMapEndianness::Little, true});
    reg = model.create_register({top, "control", 8, 0});
    field = model.create_field(
        {reg, "value", 8, 0, SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    model.set_reset(field, "HARD", PackedLogic4::from_aval_bval(8, 0x12, 0));
    memory = model.create_memory(
        {top, "samples", 8, 0, {4},
         SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
    model.add_register(primary, reg, 0);
    model.add_memory(primary, memory, 0x10);
    model.add_register(shared, reg, 0);
    model.add_register(read_only, reg, 0, Rights::ReadOnly);
    model.lock_model(top);
  }

  [[nodiscard]] SystemVerilogUvmRegisterStandardSequenceOptions
  options(const Kind kind, const bool select_map = true) const {
    SystemVerilogUvmRegisterStandardSequenceOptions result;
    result.kind = kind;
    result.top = top;
    if (select_map)
      result.map = primary;
    result.seed = 0x12345678;
    return result;
  }
};

} // namespace

void test_systemverilog_uvm_register_standard_sequences() {
  SequenceFixture fixture;
  auto reset_options = fixture.options(Kind::Reset, false);
  fixture.model.set(fixture.field, PackedLogic4::from_aval_bval(8, 0xff, 0));
  const auto reset = fixture.model.run_standard_sequence(reset_options);
  require_sequence(reset.success() && reset.operations == 1 &&
                       fixture.model.get_mirrored(fixture.reg).low_word().aval ==
                           0x12,
                   "standard reset sequence must apply the selected reset kind");

  bool reset_hardware{};
  auto hardware_options = fixture.options(Kind::HardwareReset);
  hardware_options.hardware_reset = [&] { reset_hardware = true; };
  const auto hardware = fixture.model.run_standard_sequence(hardware_options);
  require_sequence(reset_hardware && hardware.success() && hardware.reads == 1 &&
                       hardware.comparisons == 1,
                   "hardware-reset sequence must reset the DUT and check mirrors");

  const auto bit_bash =
      fixture.model.run_standard_sequence(fixture.options(Kind::BitBash));
  require_sequence(bit_bash.success() && bit_bash.writes == 9 &&
                       bit_bash.reads == 8 && bit_bash.comparisons == 8 &&
                       fixture.model.get_mirrored(fixture.reg).low_word().aval ==
                           0x12,
                   "bit-bash sequence must toggle every eligible field bit and restore");

  const auto access =
      fixture.model.run_standard_sequence(fixture.options(Kind::Access));
  const auto access_repeat =
      fixture.model.run_standard_sequence(fixture.options(Kind::Access));
  require_sequence(access.success() && access.reads == 1 && access.writes == 2 &&
                       access.comparisons == 1 &&
                       access.final_random_state ==
                           access_repeat.final_random_state,
                   "access sequences must use deterministic per-run random streams");

  const auto shared =
      fixture.model.run_standard_sequence(fixture.options(Kind::SharedAccess));
  require_sequence(shared.success() && shared.visited_registers == 1 &&
                       shared.operations == 3,
                   "shared-access sequence must select multiply mapped registers");

  const auto memory = fixture.model.run_standard_sequence(
      fixture.options(Kind::MemoryAccess));
  const auto walk = fixture.model.run_standard_sequence(
      fixture.options(Kind::MemoryWalk));
  require_sequence(memory.success() && memory.visited_memories == 1 &&
                       memory.writes == 4 && memory.reads == 4 &&
                       walk.success() && walk.comparisons == 4,
                   "memory access and walk sequences must cover every selected word");

  auto excluded_options = fixture.options(Kind::MemoryAccess);
  excluded_options.exclusions.push_back(
      {fixture.model.snapshot(fixture.memory).full_name,
       UINT32_C(1) << static_cast<std::uint8_t>(Kind::MemoryAccess)});
  const auto excluded = fixture.model.run_standard_sequence(excluded_options);
  require_sequence(excluded.success() && excluded.excluded == 1 &&
                       excluded.visited_memories == 0 && excluded.operations == 0,
                   "resource exclusions must suppress exact selected subtrees");

  auto read_only_options = fixture.options(Kind::Access);
  read_only_options.map = fixture.read_only;
  const auto read_only = fixture.model.run_standard_sequence(read_only_options);
  require_sequence(read_only.success() && read_only.operations == 0,
                   "standard sequences must honor selected-map rights");

  const auto traversal = fixture.model.run_standard_sequence(
      fixture.options(Kind::Traverse, false));
  require_sequence(traversal.success() && traversal.visited_blocks == 1 &&
                       traversal.visited_maps == 3 &&
                       traversal.visited_registers == 1 &&
                       traversal.visited_fields == 1 &&
                       traversal.visited_memories == 1 &&
                       fixture.model.standard_sequences().size() == 11,
                   "register-model traversal must retain stable complete inventory");
}

void test_systemverilog_uvm_register_standard_sequence_limits() {
  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_standard_sequences = 1;
  SequenceFixture bounded{limits};
  (void)bounded.model.run_standard_sequence(bounded.options(Kind::Traverse, false));
  require_sequence_error(
      "FSIM-UVM-REG-012",
      [&] {
        (void)bounded.model.run_standard_sequence(
            bounded.options(Kind::Traverse, false));
      },
      "standard-sequence inventory ceilings must reject before publication");

  SequenceFixture failures;
  auto failure_options = failures.options(Kind::Access);
  failure_options.maximum_failures = 1;
  failure_options.write = [](const auto &, const auto &) {
    return SystemVerilogUvmRegisterOperationResult{
        SystemVerilogUvmRegisterOperationStatus::IsOk, PackedLogic4{0}, false,
        {}};
  };
  failure_options.read = [](const auto &) {
    return SystemVerilogUvmRegisterOperationResult{
        SystemVerilogUvmRegisterOperationStatus::NotOk, PackedLogic4{0}, false,
        "injected read failure"};
  };
  const auto failed = failures.model.run_standard_sequence(failure_options);
  require_sequence(!failed.success() && failed.failures.size() == 1 &&
                       failed.failures.front().message == "injected read failure",
                   "standard sequences must contain and bound transport failures");

  auto invalid = failures.options(Kind::Traverse, false);
  invalid.exclusions.push_back({"", 1});
  require_sequence_error(
      "FSIM-UVM-REG-011",
      [&] { (void)failures.model.run_standard_sequence(invalid); },
      "invalid standard-sequence exclusions must be cataloged");

  auto missing_map = failures.options(Kind::MemoryWalk, false);
  require_sequence_error(
      "FSIM-UVM-REG-011",
      [&] { (void)failures.model.run_standard_sequence(missing_map); },
      "access sequences must require an explicit resource-map selection");

  SystemVerilogUvmRegisterModelLimits operation_limits;
  operation_limits.maximum_standard_sequence_operations = 1;
  SequenceFixture operation_bounded{operation_limits};
  (void)operation_bounded.model.run_standard_sequence(
      operation_bounded.options(Kind::Reset, false));
  require_sequence_error(
      "FSIM-UVM-REG-012",
      [&] {
        (void)operation_bounded.model.run_standard_sequence(
            operation_bounded.options(Kind::Reset, false));
      },
      "aggregate standard-sequence operation ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits failure_limits;
  failure_limits.maximum_standard_sequence_failures = 1;
  SequenceFixture failure_bounded{failure_limits};
  auto bounded_failure = failure_bounded.options(Kind::Access);
  bounded_failure.maximum_failures = 1;
  bounded_failure.write = failure_options.write;
  bounded_failure.read = failure_options.read;
  (void)failure_bounded.model.run_standard_sequence(bounded_failure);
  require_sequence_error(
      "FSIM-UVM-REG-012",
      [&] {
        (void)failure_bounded.model.run_standard_sequence(bounded_failure);
      },
      "aggregate standard-sequence failure ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits failure_byte_limits;
  failure_byte_limits.maximum_standard_sequence_failure_bytes = 100;
  SequenceFixture failure_byte_bounded{failure_byte_limits};
  auto byte_failure = failure_byte_bounded.options(Kind::Access);
  byte_failure.maximum_failures = 1;
  byte_failure.write = failure_options.write;
  byte_failure.read = failure_options.read;
  (void)failure_byte_bounded.model.run_standard_sequence(byte_failure);
  require_sequence_error(
      "FSIM-UVM-REG-012",
      [&] {
        (void)failure_byte_bounded.model.run_standard_sequence(byte_failure);
      },
      "aggregate standard-sequence failure-byte ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits exclusion_limits;
  exclusion_limits.maximum_standard_sequence_exclusions = 1;
  SequenceFixture exclusion_bounded{exclusion_limits};
  auto too_many_exclusions = exclusion_bounded.options(Kind::Traverse, false);
  too_many_exclusions.exclusions = {{"one", 1}, {"two", 1}};
  require_sequence_error(
      "FSIM-UVM-REG-012",
      [&] {
        (void)exclusion_bounded.model.run_standard_sequence(
            too_many_exclusions);
      },
      "per-run standard-sequence exclusion ceilings must reject exactly");

  SystemVerilogUvmRegisterModelLimits exclusion_byte_limits;
  exclusion_byte_limits.maximum_standard_sequence_exclusion_bytes = 4;
  SequenceFixture exclusion_byte_bounded{exclusion_byte_limits};
  auto oversized_exclusion =
      exclusion_byte_bounded.options(Kind::Traverse, false);
  oversized_exclusion.exclusions = {{"12345", 1}};
  require_sequence_error(
      "FSIM-UVM-REG-012",
      [&] {
        (void)exclusion_byte_bounded.model.run_standard_sequence(
            oversized_exclusion);
      },
      "standard-sequence exclusion-byte ceilings must reject exactly");
}

} // namespace fsim::tests::runtime
