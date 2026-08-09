// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_register_model.hpp"

#include <array>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using Policy = SystemVerilogUvmRegisterAccessPolicy;
using Status = SystemVerilogUvmRegisterOperationStatus;

void require_value(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

void require_value_error(const std::string_view code,
                         const std::function<void()> &operation,
                         const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require_value(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

[[nodiscard]] PackedLogic4 bits(const std::string_view value) {
  return PackedLogic4::from_msb_string(value);
}

struct ValueFixture {
  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle root{};

  ValueFixture()
      : objects(heap,
                [](std::string_view, std::string_view, std::string_view) {
                  return SystemVerilogClassHandle{};
                }),
        components(heap, objects) {
    root = components.create_root("values");
  }
};

struct PolicyExpectation {
  Policy policy;
  bool readable;
  bool writable;
  std::string_view write_value;
  std::string_view read_value;
  bool once{};
};

constexpr std::array kPolicies{
    PolicyExpectation{Policy::ReadWrite, true, true, "1100", "1010"},
    PolicyExpectation{Policy::ReadOnly, true, false, "1010", "1010"},
    PolicyExpectation{Policy::WriteOnly, false, true, "1100", "1010"},
    PolicyExpectation{Policy::ReadClear, true, false, "1010", "0000"},
    PolicyExpectation{Policy::ReadSet, true, false, "1010", "1111"},
    PolicyExpectation{Policy::WriteReadClear, true, true, "1100", "0000"},
    PolicyExpectation{Policy::WriteReadSet, true, true, "1100", "1111"},
    PolicyExpectation{Policy::WriteClear, true, true, "0000", "1010"},
    PolicyExpectation{Policy::WriteSet, true, true, "1111", "1010"},
    PolicyExpectation{Policy::WriteSetReadClear, true, true, "1111", "0000"},
    PolicyExpectation{Policy::WriteClearReadSet, true, true, "0000", "1111"},
    PolicyExpectation{Policy::WriteOneClear, true, true, "0010", "1010"},
    PolicyExpectation{Policy::WriteOneSet, true, true, "1110", "1010"},
    PolicyExpectation{Policy::WriteOneToggle, true, true, "0110", "1010"},
    PolicyExpectation{Policy::WriteZeroClear, true, true, "1000", "1010"},
    PolicyExpectation{Policy::WriteZeroSet, true, true, "1011", "1010"},
    PolicyExpectation{Policy::WriteZeroToggle, true, true, "1001", "1010"},
    PolicyExpectation{Policy::WriteOneSetReadClear, true, true, "1110", "0000"},
    PolicyExpectation{Policy::WriteOneClearReadSet, true, true, "0010", "1111"},
    PolicyExpectation{Policy::WriteZeroSetReadClear, true, true, "1011", "0000"},
    PolicyExpectation{Policy::WriteZeroClearReadSet, true, true, "1000", "1111"},
    PolicyExpectation{Policy::WriteOnce, false, true, "1100", "1010", true},
    PolicyExpectation{Policy::WriteOnlyClear, false, true, "0000", "1010"},
    PolicyExpectation{Policy::WriteOnlySet, false, true, "1111", "1010"},
    PolicyExpectation{Policy::WriteOnceReadWrite, true, true, "1100", "1010", true},
    PolicyExpectation{Policy::NoAccess, false, false, "1010", "1010"},
};

} // namespace

void test_systemverilog_uvm_register_values() {
  ValueFixture fixture;
  SystemVerilogUvmRegisterModelService model{fixture.components};
  const auto top = model.create_block({fixture.root, std::nullopt, "policy"});
  std::vector<SystemVerilogUvmRegisterHandle> registers;
  std::vector<SystemVerilogUvmRegisterFieldHandle> fields;
  for (std::size_t index = 0; index < kPolicies.size(); ++index) {
    const auto reg = model.create_register(
        {top, "register_" + std::to_string(index), 4,
         static_cast<std::uint64_t>(index)});
    const auto field = model.create_field(
        {reg, "field", 4, 0, kPolicies[index].policy, index == 1,
         SystemVerilogUvmRegisterComparePolicy::Check});
    model.set_reset(field, "HARD", bits("1010"));
    if (kPolicies[index].once)
      model.set_reset(field, "SOFT", bits("1010"));
    registers.push_back(reg);
    fields.push_back(field);
  }
  model.lock_model(top);

  for (std::size_t index = 0; index < kPolicies.size(); ++index) {
    const auto &expected = kPolicies[index];
    model.reset(top, "HARD");
    const auto before_write = model.value_snapshot(fields[index]);
    require_value(before_write.desired.to_msb_string() == "1010" &&
                      before_write.mirrored.to_msb_string() == "1010",
                  "register reset did not initialize desired and mirror state");
    model.set(fields[index], bits("1100"));
    require_value(model.get(fields[index]).to_msb_string() ==
                          expected.write_value &&
                      model.get_mirrored(fields[index]).to_msb_string() ==
                          "1010",
                  "register field set did not apply desired-value policy");
    model.reset(top, "HARD");
    const auto write = model.write(fields[index], bits("1100"));
    require_value((write.status == Status::IsOk) == expected.writable,
                  "register field write status did not propagate policy rights");
    require_value(model.get_mirrored(fields[index]).to_msb_string() ==
                      expected.write_value,
                  "register field write policy produced the wrong value");
    if (expected.once) {
      const auto second = model.write(fields[index], bits("0011"));
      require_value(second.success() && !second.changed &&
                        model.get_mirrored(fields[index]).to_msb_string() ==
                            expected.write_value,
                    "write-once register field accepted a second mutation");
      model.reset(top, "SOFT");
      (void)model.write(fields[index], bits("0011"));
      require_value(model.get_mirrored(fields[index]).to_msb_string() ==
                        "1010",
                    "soft reset incorrectly re-armed a write-once field");
    }

    model.reset(top, "HARD");
    const auto read = model.read(fields[index]);
    require_value((read.status == Status::IsOk) == expected.readable,
                  "register field read status did not propagate policy rights");
    if (expected.readable)
      require_value(read.value.to_msb_string() == "1010",
                    "register read did not return the pre-side-effect value");
    require_value(model.get_mirrored(fields[index]).to_msb_string() ==
                      expected.read_value,
                  "register field read side effect produced the wrong value");
  }
  require_value(!model.value_snapshot(fields[1]).needs_update,
                "volatile read-only field incorrectly required an update");

  ValueFixture state_fixture;
  SystemVerilogUvmRegisterModelService state_model{state_fixture.components};
  const auto state_top =
      state_model.create_block({state_fixture.root, std::nullopt, "state"});
  const auto mixed = state_model.create_register({state_top, "mixed", 8, 0});
  const auto low = state_model.create_field(
      {mixed, "low", 4, 0, Policy::ReadWrite, true,
       SystemVerilogUvmRegisterComparePolicy::Check});
  const auto high = state_model.create_field(
      {mixed, "high", 4, 4, Policy::ReadOnly, false,
       SystemVerilogUvmRegisterComparePolicy::NoCheck});
  state_model.set_reset(low, "HARD", bits("0001"));
  state_model.set_reset(high, "HARD", bits("1010"));
  const auto memory = state_model.create_memory(
      {state_top, "memory", 8, 0x100, {2, 3}, Policy::WriteReadClear});
  const auto read_only_memory = state_model.create_memory(
      {state_top, "read_only", 8, 0x200, {1}, Policy::ReadOnly});
  const auto write_only_memory = state_model.create_memory(
      {state_top, "write_only", 8, 0x300, {1}, Policy::WriteOnly});
  state_model.lock_model(state_top);
  state_model.reset(state_top, "HARD");
  require_value(state_model.get(mixed).to_msb_string() == "10100001" &&
                    state_model.get_mirrored(mixed).to_msb_string() ==
                        "10100001",
                "register reset kind did not compose field values");
  require_value(state_model.needs_update(mixed),
                "volatile register field did not force needs-update state");
  state_model.set(low, bits("0011"));
  require_value(state_model.get(low).to_msb_string() == "0011" &&
                    state_model.get_mirrored(low).to_msb_string() == "0001",
                "field set did not update desired state only");
  const auto comparison = state_model.compare(mixed, bits("01000000"));
  require_value(!comparison.matches && comparison.mismatches.size() == 1 &&
                    comparison.mismatches.front() == low,
                "register compare did not honor per-field compare policy");
  const auto direct = state_model.predict(
      low, bits("0110"), SystemVerilogUvmRegisterPredictKind::Direct);
  require_value(direct.success() &&
                    state_model.get(low).to_msb_string() == "0110" &&
                    state_model.get_mirrored(low).to_msb_string() == "0110",
                "direct field prediction did not synchronize state");
  const auto mixed_write = state_model.write(mixed, bits("01011100"));
  require_value(mixed_write.success() &&
                    state_model.get_mirrored(mixed).to_msb_string() ==
                        "10101100",
                "register write did not preserve read-only fields");

  const auto before_invalid = state_model.value_snapshot(mixed);
  const auto wrong_width = state_model.write(mixed, bits("1"));
  const auto unknown = state_model.predict(
      mixed, PackedLogic4{8, Logic4::x},
      SystemVerilogUvmRegisterPredictKind::Direct);
  require_value(wrong_width.status == Status::NotOk &&
                    unknown.status == Status::HasUnknown &&
                    state_model.value_snapshot(mixed).desired ==
                        before_invalid.desired &&
                    state_model.value_snapshot(mixed).mirrored ==
                        before_invalid.mirrored,
                "invalid register values partially mutated state");
  require_value_error(
      "FSIM-UVM-REG-003",
      [&] {
        (void)state_model.predict(
            mixed, bits("00000000"),
            static_cast<SystemVerilogUvmRegisterPredictKind>(99));
      },
      "invalid register prediction kind was accepted");
  require_value(state_model.compare(mixed, PackedLogic4{8, Logic4::x}).status ==
                    Status::HasUnknown,
                "register compare did not propagate unknown status");
  require_value_error(
      "FSIM-UVM-REG-003", [&] { state_model.set(mixed, bits("1")); },
      "invalid set width did not use the operation diagnostic");

  const std::array first_index{std::size_t{1}, std::size_t{2}};
  const std::array second_index{std::size_t{0}, std::size_t{1}};
  const auto memory_write =
      state_model.write(memory, first_index, bits("10100101"));
  const auto memory_read = state_model.read(memory, first_index);
  require_value(memory_write.success() && memory_read.success() &&
                    memory_read.value.to_msb_string() == "10100101",
                "multidimensional memory write/read lost its value");
  const auto cleared = state_model.read(memory, first_index);
  require_value(cleared.value.to_msb_string() == "00000000",
                "memory read-clear side effect was not retained");
  const auto memory_wrong_width =
      state_model.write(memory, second_index, bits("1"));
  const auto memory_unknown =
      state_model.write(memory, second_index, PackedLogic4{8, Logic4::x});
  const std::array only_index{std::size_t{0}};
  const auto read_only_write =
      state_model.write(read_only_memory, only_index, bits("00000001"));
  const auto write_only_read = state_model.read(write_only_memory, only_index);
  require_value(memory_wrong_width.status == Status::NotOk &&
                    memory_unknown.status == Status::HasUnknown &&
                    read_only_write.status == Status::NotOk &&
                    write_only_read.status == Status::NotOk,
                "memory width status was not propagated");
  const std::array bad_index{std::size_t{2}, std::size_t{0}};
  require_value_error(
      "FSIM-UVM-REG-003", [&] { (void)state_model.read(memory, bad_index); },
      "out-of-range memory index was accepted");
  require_value_error(
      "FSIM-UVM-REG-001",
      [&] { state_model.configure_field(low, Policy::ReadOnly, false,
                                        SystemVerilogUvmRegisterComparePolicy::Check); },
      "locked field policy remained mutable");

  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_materialized_memory_words = 1;
  SystemVerilogUvmRegisterModelService bounded{state_fixture.components, limits};
  const auto bounded_top =
      bounded.create_block({state_fixture.root, std::nullopt, "bounded"});
  const auto bounded_memory = bounded.create_memory(
      {bounded_top, "memory", 8, 0, {2}, Policy::ReadWrite});
  bounded.lock_model(bounded_top);
  const std::array zero{std::size_t{0}};
  const std::array one{std::size_t{1}};
  (void)bounded.write(bounded_memory, zero, bits("00000001"));
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] { (void)bounded.write(bounded_memory, one, bits("00000001")); },
      "materialized memory-word limit was not enforced");
  require_value(bounded.read(bounded_memory, zero).value.to_msb_string() ==
                    "00000001",
                "memory limit failure corrupted an existing word");

  limits = {};
  limits.maximum_operations = 1;
  SystemVerilogUvmRegisterModelService one_operation{state_fixture.components,
                                                     limits};
  const auto operation_top = one_operation.create_block(
      {state_fixture.root, std::nullopt, "one_operation"});
  const auto operation_register =
      one_operation.create_register({operation_top, "register", 1, 0});
  one_operation.lock_model(operation_top);
  one_operation.set(operation_register, bits("1"));
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] { (void)one_operation.write(operation_register, bits("0")); },
      "register operation limit was not enforced");
  require_value(one_operation.get(operation_register).to_msb_string() == "1",
                "operation-limit failure partially mutated register state");

  limits = {};
  limits.maximum_reset_kinds_per_field = 1;
  SystemVerilogUvmRegisterModelService one_reset{state_fixture.components,
                                                 limits};
  const auto reset_top = one_reset.create_block(
      {state_fixture.root, std::nullopt, "one_reset"});
  const auto reset_register =
      one_reset.create_register({reset_top, "register", 1, 0});
  const auto reset_field =
      one_reset.create_field({reset_register, "field", 1, 0});
  one_reset.set_reset(reset_field, "HARD", bits("0"));
  require_value_error(
      "FSIM-UVM-REG-003",
      [&] { one_reset.set_reset(reset_field, "BAD", bits("00")); },
      "invalid register reset width was accepted");
  require_value_error(
      "FSIM-UVM-REG-003",
      [&] {
        one_reset.set_reset(reset_field, "UNKNOWN",
                            PackedLogic4{1, Logic4::x});
      },
      "unknown register reset value was accepted");
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] { one_reset.set_reset(reset_field, "SOFT", bits("1")); },
      "register reset-kind count limit was not enforced");

  limits = {};
  limits.maximum_reset_name_bytes = 3;
  SystemVerilogUvmRegisterModelService reset_name{state_fixture.components,
                                                  limits};
  const auto reset_name_top = reset_name.create_block(
      {state_fixture.root, std::nullopt, "reset_name"});
  const auto reset_name_register =
      reset_name.create_register({reset_name_top, "register", 1, 0});
  const auto reset_name_field =
      reset_name.create_field({reset_name_register, "field", 1, 0});
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] { reset_name.set_reset(reset_name_field, "HARD", bits("0")); },
      "register reset-name byte limit was not enforced");

  limits = {};
  limits.maximum_mutations = 4;
  SystemVerilogUvmRegisterModelService mutation_limit{state_fixture.components,
                                                      limits};
  const auto mutation_top = mutation_limit.create_block(
      {state_fixture.root, std::nullopt, "mutation_limit"});
  const auto mutation_register =
      mutation_limit.create_register({mutation_top, "register", 1, 0});
  mutation_limit.lock_model(mutation_top);
  mutation_limit.set(mutation_register, bits("1"));
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] { (void)mutation_limit.write(mutation_register, bits("0")); },
      "runtime register mutation limit was not enforced");
  require_value(
      mutation_limit.get(mutation_register).to_msb_string() == "1" &&
          mutation_limit.get_mirrored(mutation_register).to_msb_string() == "0",
      "mutation-limit failure partially changed register state");

  limits = {};
  limits.maximum_register_value_bits = 4;
  SystemVerilogUvmRegisterModelService register_bits{state_fixture.components,
                                                     limits};
  const auto register_bits_top = register_bits.create_block(
      {state_fixture.root, std::nullopt, "register_bits"});
  (void)register_bits.create_register({register_bits_top, "first", 4, 0});
  require_value_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)register_bits.create_register({register_bits_top, "second", 1, 1});
      },
      "aggregate register-value bit limit was not enforced");

  limits = {};
  limits.maximum_reset_value_bits = 1;
  SystemVerilogUvmRegisterModelService reset_bits{state_fixture.components,
                                                  limits};
  const auto reset_bits_top = reset_bits.create_block(
      {state_fixture.root, std::nullopt, "reset_bits"});
  const auto reset_bits_register =
      reset_bits.create_register({reset_bits_top, "register", 1, 0});
  const auto reset_bits_field =
      reset_bits.create_field({reset_bits_register, "field", 1, 0});
  reset_bits.set_reset(reset_bits_field, "HARD", bits("0"));
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] { reset_bits.set_reset(reset_bits_field, "SOFT", bits("1")); },
      "aggregate reset-value bit limit was not enforced");

  limits = {};
  limits.maximum_materialized_memory_bits = 8;
  SystemVerilogUvmRegisterModelService memory_bits{state_fixture.components,
                                                   limits};
  const auto memory_bits_top = memory_bits.create_block(
      {state_fixture.root, std::nullopt, "memory_bits"});
  const auto memory_bits_memory = memory_bits.create_memory(
      {memory_bits_top, "memory", 8, 0, {2}, Policy::ReadWrite});
  memory_bits.lock_model(memory_bits_top);
  (void)memory_bits.write(memory_bits_memory, zero, bits("00000001"));
  require_value_error(
      "FSIM-UVM-REG-004",
      [&] {
        (void)memory_bits.write(memory_bits_memory, one, bits("00000010"));
      },
      "aggregate materialized memory-bit limit was not enforced");
}

} // namespace fsim::tests::runtime
