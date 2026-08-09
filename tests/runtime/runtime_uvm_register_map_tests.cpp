// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_register_model.hpp"

#include <array>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using Endian = SystemVerilogUvmRegisterMapEndianness;
using Rights = SystemVerilogUvmRegisterMapRights;
using Status = SystemVerilogUvmRegisterOperationStatus;

void require_map(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

void require_map_error(const std::string_view code,
                       const std::function<void()> &operation,
                       const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require_map(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

[[nodiscard]] PackedLogic4 map_bits(const std::string_view value) {
  return PackedLogic4::from_msb_string(value);
}

struct MapFixture {
  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle root{};

  MapFixture()
      : objects(heap,
                [](std::string_view, std::string_view, std::string_view) {
                  return SystemVerilogClassHandle{};
                }),
        components(heap, objects) {
    root = components.create_root("maps");
  }
};

[[nodiscard]] std::vector<std::uint64_t> addresses(
    const std::vector<SystemVerilogUvmRegisterBusBeat> &beats) {
  std::vector<std::uint64_t> result;
  result.reserve(beats.size());
  for (const auto &beat : beats)
    result.push_back(beat.address);
  return result;
}

} // namespace

void test_systemverilog_uvm_register_maps() {
  MapFixture fixture;
  SystemVerilogUvmRegisterModelService model{fixture.components};
  const auto top = model.create_block({fixture.root, std::nullopt, "soc"});
  const auto child = model.create_block({0, top, "peripheral"});
  const auto little =
      model.create_map({top, "little", 0x1000, 4, Endian::Little, true});
  const auto big =
      model.create_map({top, "big", 0x2000, 4, Endian::Big, true});
  const auto little_fifo = model.create_map(
      {top, "little_fifo", 0x3000, 4, Endian::LittleFifo, true});
  const auto big_fifo = model.create_map(
      {top, "big_fifo", 0x4000, 4, Endian::BigFifo, true});
  const auto write_only = model.create_map(
      {top, "write_only", 0x5000, 4, Endian::Little, true});
  const auto word_map =
      model.create_map({top, "word", 0x6000, 4, Endian::Little, false});
  const auto child_map =
      model.create_map({child, "child", 0, 2, Endian::Little, true});
  const auto wide = model.create_register({top, "wide", 64, 0});
  (void)model.create_field({wide, "value", 64, 0});
  const auto hidden = model.create_register({top, "hidden", 8, 0});
  const auto memory = model.create_memory(
      {top, "memory", 16, 0, {8},
       SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  const auto child_register =
      model.create_register({child, "control", 32, 0});
  (void)model.create_field({child_register, "value", 32, 0});

  model.add_register(little, wide, 0x10);
  model.add_memory(little, memory, 0x20);
  model.add_register(little, hidden, 0, Rights::ReadWrite, true);
  model.add_register(big, wide, 0x40, Rights::ReadOnly);
  model.add_register(little_fifo, wide, 0x80);
  model.add_register(big_fifo, wide, 0x80);
  model.add_register(write_only, wide, 0x40, Rights::WriteOnly);
  model.add_register(word_map, wide, 0x2);
  model.add_memory(word_map, memory, 0x10);
  model.add_register(child_map, child_register, 0x4);
  model.add_submap(little, child_map, 0x100);

  const auto map_snapshot = model.snapshot(little);
  require_map(map_snapshot.base_offset == 0x1000 &&
                  map_snapshot.bus_width_bytes == 4 &&
                  map_snapshot.endianness == Endian::Little &&
                  map_snapshot.byte_addressing,
              "register-map bus configuration was not retained");
  require_map(model.mapped_registers(little).size() == 2 &&
                  model.mapped_memories(little).size() == 1 &&
                  model.submaps(little).size() == 1 &&
                  model.mapped_registers(little, true).size() == 3,
              "register-map direct or hierarchical inventory is incomplete");

  model.lock_model(top);
  const auto little_beats = model.register_accesses(little, wide);
  require_map(addresses(little_beats) ==
                      std::vector<std::uint64_t>{0x1010, 0x1014} &&
                  little_beats[0].data_byte_offset == 0 &&
                  little_beats[1].data_byte_offset == 4 &&
                  little_beats[0].byte_enables ==
                      std::vector<std::uint8_t>({1, 1, 1, 1}),
              "little-endian register-map beats are incorrect");
  require_map(addresses(model.register_accesses(big, wide)) ==
                  std::vector<std::uint64_t>{0x2044, 0x2040},
              "big-endian register-map beats are incorrect");
  const auto little_fifo_beats = model.register_accesses(little_fifo, wide);
  const auto big_fifo_beats = model.register_accesses(big_fifo, wide);
  require_map(addresses(little_fifo_beats) ==
                      std::vector<std::uint64_t>{0x3080, 0x3080} &&
                  little_fifo_beats[0].data_byte_offset == 0 &&
                  little_fifo_beats[1].data_byte_offset == 4,
              "little-FIFO register-map ordering is incorrect");
  require_map(addresses(big_fifo_beats) ==
                      std::vector<std::uint64_t>{0x4080, 0x4080} &&
                  big_fifo_beats[0].data_byte_offset == 4 &&
                  big_fifo_beats[1].data_byte_offset == 0,
              "big-FIFO register-map ordering is incorrect");

  const auto child_beats = model.register_accesses(child_map, child_register);
  require_map(addresses(child_beats) ==
                  std::vector<std::uint64_t>{0x1104, 0x1106},
              "hierarchical submap base or narrow bus width was lost");
  const auto memory_beats = model.memory_accesses(little, memory, 3);
  require_map(addresses(memory_beats) ==
                      std::vector<std::uint64_t>{0x1026} &&
                  memory_beats.front().byte_enables ==
                      std::vector<std::uint8_t>({1, 1, 0, 0}),
              "memory word addressing or partial byte enables are incorrect");

  const auto wide_read = model.lookup(little, 0x1014, true);
  const auto wide_write = model.lookup(little, 0x1010, false);
  const auto child_lookup = model.lookup(little, 0x1106, true);
  const auto memory_lookup = model.lookup(little, 0x1026, false);
  require_map(wide_read && wide_read->register_handle == wide && wide_write &&
                  wide_write->register_handle == wide && child_lookup &&
                  child_lookup->register_handle == child_register &&
                  memory_lookup && memory_lookup->memory == memory &&
                  memory_lookup->memory_word_index == 3,
              "register-map address lookup lost register or memory identity");
  require_map(model.lookup(big, 0x2040, true).has_value() &&
                  !model.lookup(big, 0x2040, false).has_value(),
              "per-map read-only rights were not applied during lookup");
  require_map(!model.lookup(write_only, 0x5040, true).has_value() &&
                  model.lookup(write_only, 0x5040, false).has_value(),
              "per-map write-only rights were not applied during lookup");
  require_map(addresses(model.register_accesses(word_map, wide)) ==
                      std::vector<std::uint64_t>{0x6002, 0x6003} &&
                  addresses(model.memory_accesses(word_map, memory, 3)) ==
                      std::vector<std::uint64_t>{0x6013},
              "word-addressed register-map units are incorrect");
  require_map(!model.lookup(little, 0x1000, true).has_value(),
              "unmapped register appeared in address lookup");
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] { (void)model.register_accesses(little, hidden); },
      "unmapped register produced physical addresses");
  require_map_error(
      "FSIM-UVM-REG-001",
      [&] { model.add_register(big, hidden, 0x90); },
      "locked register map remained mutable");

  MapFixture burst_fixture;
  SystemVerilogUvmRegisterModelService burst{burst_fixture.components};
  const auto burst_top =
      burst.create_block({burst_fixture.root, std::nullopt, "burst"});
  const auto burst_map =
      burst.create_map({burst_top, "rw", 0, 2, Endian::Little, true});
  const auto read_only_map =
      burst.create_map({burst_top, "ro", 0x100, 2, Endian::Little, true});
  const auto burst_memory = burst.create_memory(
      {burst_top, "memory", 16, 0, {4},
       SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  burst.add_memory(burst_map, burst_memory, 0x20);
  burst.add_memory(read_only_map, burst_memory, 0x20, Rights::ReadOnly);
  burst.lock_model(burst_top);
  const std::array values{map_bits("0001000100100010"),
                          map_bits("0011001101000100")};
  const std::array enables{std::vector<std::uint8_t>{1, 0},
                           std::vector<std::uint8_t>{0, 1}};
  const auto write_result =
      burst.write_burst(burst_map, burst_memory, 1, values, enables);
  require_map(write_result.success() && write_result.completed_words == 2 &&
                  write_result.changed,
              "byte-enabled memory burst write did not complete");
  const auto read_result = burst.read_burst(burst_map, burst_memory, 1, 2);
  require_map(read_result.success() && read_result.completed_words == 2 &&
                  read_result.values[0].to_msb_string() == "0000000000100010" &&
                  read_result.values[1].to_msb_string() == "0011001100000000",
              "byte-enabled memory burst values are incorrect");
  const auto denied = burst.write_burst(read_only_map, burst_memory, 0,
                                        std::span<const PackedLogic4>{values});
  require_map(denied.status == Status::NotOk && denied.completed_words == 0,
              "per-map read-only rights did not reject burst write");
  const std::array bad_enables{std::vector<std::uint8_t>{2, 0},
                               std::vector<std::uint8_t>{1, 1}};
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] {
        (void)burst.write_burst(burst_map, burst_memory, 0, values,
                                bad_enables);
      },
      "invalid memory byte enable was accepted");
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] { (void)burst.read_burst(burst_map, burst_memory, 3, 2); },
      "out-of-range memory burst was accepted");

  MapFixture policy_fixture;
  SystemVerilogUvmRegisterModelService policy_model{policy_fixture.components};
  const auto policy_top =
      policy_model.create_block({policy_fixture.root, std::nullopt, "policy"});
  const auto policy_map = policy_model.create_map(
      {policy_top, "map", 0, 2, Endian::Little, true});
  const auto policy_memory = policy_model.create_memory(
      {policy_top, "memory", 16, 0, {1},
       SystemVerilogUvmRegisterAccessPolicy::WriteOneSet});
  policy_model.add_memory(policy_map, policy_memory, 0);
  policy_model.lock_model(policy_top);
  const std::array policy_value{map_bits("1111111111111111")};
  const std::array low_byte_only{std::vector<std::uint8_t>{1, 0}};
  require_map(policy_model
                  .write_burst(policy_map, policy_memory, 0, policy_value,
                               low_byte_only)
                  .success(),
              "policy-aware byte-enabled burst write failed");
  require_map(policy_model.read_burst(policy_map, policy_memory, 0, 1)
                      .values.front()
                      .to_msb_string() == "0000000011111111",
              "disabled bytes were changed by a write policy transform");

  MapFixture invalid_fixture;
  SystemVerilogUvmRegisterModelService invalid{invalid_fixture.components};
  const auto invalid_top =
      invalid.create_block({invalid_fixture.root, std::nullopt, "invalid"});
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] {
        (void)invalid.create_map(
            {invalid_top, "zero_bus", 0, 0, Endian::Little, true});
      },
      "zero-width register-map bus was accepted");
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] {
        (void)invalid.create_map(
            {invalid_top, "bad_endian", 0, 1,
             static_cast<Endian>(99), true});
      },
      "invalid register-map endian mode was accepted");
  const auto invalid_map =
      invalid.create_map({invalid_top, "map", 0, 4, Endian::Little, true});
  const auto first = invalid.create_register({invalid_top, "first", 32, 0});
  const auto second = invalid.create_register({invalid_top, "second", 32, 0});
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] {
        invalid.add_register(invalid_map, first, 0x8,
                             static_cast<Rights>(99));
      },
      "invalid per-map rights were accepted");
  invalid.add_register(invalid_map, first, 0x10);
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] { invalid.add_register(invalid_map, first, 0x20); },
      "duplicate register-map membership was accepted");
  invalid.add_register(invalid_map, second, 0x10);
  const auto mutations_before_lock = invalid.mutation_count();
  require_map_error("FSIM-UVM-REG-005",
                    [&] { invalid.lock_model(invalid_top); },
                    "overlapping register-map addresses were accepted");
  require_map(invalid.mutation_count() == mutations_before_lock &&
                  model.snapshot(top).state ==
                      SystemVerilogUvmRegisterModelState::Locked,
              "failed map lock partially mutated model state");

  SystemVerilogUvmRegisterModelService overflow{invalid_fixture.components};
  const auto overflow_top =
      overflow.create_block({invalid_fixture.root, std::nullopt, "overflow"});
  const auto overflow_map = overflow.create_map(
      {overflow_top, "map", std::numeric_limits<std::uint64_t>::max(), 1,
       Endian::Little, true});
  const auto overflow_register =
      overflow.create_register({overflow_top, "register", 16, 0});
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] { overflow.add_register(overflow_map, overflow_register, 0); },
      "overflowing register-map address was accepted");

  SystemVerilogUvmRegisterModelService alignment{invalid_fixture.components};
  const auto alignment_top =
      alignment.create_block({invalid_fixture.root, std::nullopt, "alignment"});
  const auto alignment_child = alignment.create_block({0, alignment_top, "child"});
  const auto word_parent = alignment.create_map(
      {alignment_top, "parent", 0, 4, Endian::Little, false});
  const auto word_child = alignment.create_map(
      {alignment_child, "child_map", 0, 2, Endian::Little, false});
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] { alignment.add_submap(word_parent, word_child, 0); },
      "misaligned submap address units were accepted");

  SystemVerilogUvmRegisterModelService submap_overlap{
      invalid_fixture.components};
  const auto overlap_top = submap_overlap.create_block(
      {invalid_fixture.root, std::nullopt, "submap_overlap"});
  const auto overlap_child =
      submap_overlap.create_block({0, overlap_top, "child"});
  const auto overlap_parent_map = submap_overlap.create_map(
      {overlap_top, "parent", 0, 4, Endian::Little, true});
  const auto overlap_child_map = submap_overlap.create_map(
      {overlap_child, "child_map", 0, 2, Endian::Little, true});
  const auto overlap_register =
      submap_overlap.create_register({overlap_top, "register", 32, 0});
  submap_overlap.add_register(overlap_parent_map, overlap_register, 0);
  submap_overlap.add_submap(overlap_parent_map, overlap_child_map, 0);
  require_map_error(
      "FSIM-UVM-REG-005",
      [&] { submap_overlap.lock_model(overlap_top); },
      "overlapping submap and register ranges were accepted");

  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_map_registrations = 1;
  SystemVerilogUvmRegisterModelService one_registration{invalid_fixture.components,
                                                        limits};
  const auto one_top = one_registration.create_block(
      {invalid_fixture.root, std::nullopt, "one_registration"});
  const auto one_map = one_registration.create_map(
      {one_top, "map", 0, 1, Endian::Little, true});
  const auto one_first =
      one_registration.create_register({one_top, "first", 8, 0});
  const auto one_second =
      one_registration.create_register({one_top, "second", 8, 0});
  one_registration.add_register(one_map, one_first, 0);
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] { one_registration.add_register(one_map, one_second, 1); },
      "register-map registration limit was not enforced");

  limits = {};
  limits.maximum_submap_depth = 1;
  SystemVerilogUvmRegisterModelService shallow_maps{invalid_fixture.components,
                                                   limits};
  const auto shallow_top = shallow_maps.create_block(
      {invalid_fixture.root, std::nullopt, "shallow_maps"});
  const auto shallow_child =
      shallow_maps.create_block({0, shallow_top, "child"});
  const auto shallow_parent_map = shallow_maps.create_map(
      {shallow_top, "parent", 0, 1, Endian::Little, true});
  const auto shallow_child_map = shallow_maps.create_map(
      {shallow_child, "child_map", 0, 1, Endian::Little, true});
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] {
        shallow_maps.add_submap(shallow_parent_map, shallow_child_map, 0);
      },
      "register submap-depth limit was not enforced transactionally");

  limits = {};
  limits.maximum_bus_width_bytes = 1;
  SystemVerilogUvmRegisterModelService narrow_bus{invalid_fixture.components,
                                                  limits};
  const auto narrow_bus_top = narrow_bus.create_block(
      {invalid_fixture.root, std::nullopt, "narrow_bus"});
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] {
        (void)narrow_bus.create_map(
            {narrow_bus_top, "map", 0, 2, Endian::Little, true});
      },
      "register-map bus-width limit was not enforced");

  limits = {};
  limits.maximum_physical_beats = 1;
  SystemVerilogUvmRegisterModelService one_beat{invalid_fixture.components,
                                               limits};
  const auto beat_top = one_beat.create_block(
      {invalid_fixture.root, std::nullopt, "one_beat"});
  const auto beat_map = one_beat.create_map(
      {beat_top, "map", 0, 1, Endian::Little, true});
  const auto beat_register =
      one_beat.create_register({beat_top, "register", 16, 0});
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] { one_beat.add_register(beat_map, beat_register, 0); },
      "register-map physical-beat limit was not enforced");

  limits = {};
  limits.maximum_physical_byte_enables = 3;
  SystemVerilogUvmRegisterModelService enable_bytes{invalid_fixture.components,
                                                    limits};
  const auto enable_top = enable_bytes.create_block(
      {invalid_fixture.root, std::nullopt, "enable_bytes"});
  const auto enable_map = enable_bytes.create_map(
      {enable_top, "map", 0, 4, Endian::Little, true});
  const auto enable_register =
      enable_bytes.create_register({enable_top, "register", 8, 0});
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] { enable_bytes.add_register(enable_map, enable_register, 0); },
      "aggregate register-map byte-enable limit was not enforced");

  limits = {};
  limits.maximum_address_lookup_work = 1;
  SystemVerilogUvmRegisterModelService one_lookup{invalid_fixture.components,
                                                  limits};
  const auto lookup_top = one_lookup.create_block(
      {invalid_fixture.root, std::nullopt, "one_lookup"});
  const auto lookup_map = one_lookup.create_map(
      {lookup_top, "map", 0, 1, Endian::Little, true});
  const auto lookup_register =
      one_lookup.create_register({lookup_top, "register", 16, 0});
  one_lookup.add_register(lookup_map, lookup_register, 0);
  one_lookup.lock_model(lookup_top);
  require_map_error(
      "FSIM-UVM-REG-006", [&] { (void)one_lookup.lookup(lookup_map, 1, true); },
      "register-map lookup-work limit was not enforced");

  limits = {};
  limits.maximum_burst_words = 1;
  SystemVerilogUvmRegisterModelService one_burst{invalid_fixture.components,
                                                 limits};
  const auto one_burst_top = one_burst.create_block(
      {invalid_fixture.root, std::nullopt, "one_burst"});
  const auto one_burst_map = one_burst.create_map(
      {one_burst_top, "map", 0, 1, Endian::Little, true});
  const auto one_burst_memory = one_burst.create_memory(
      {one_burst_top, "memory", 8, 0, {2},
       SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  one_burst.add_memory(one_burst_map, one_burst_memory, 0);
  one_burst.lock_model(one_burst_top);
  const std::array two_values{map_bits("00000001"), map_bits("00000010")};
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] {
        (void)one_burst.write_burst(one_burst_map, one_burst_memory, 0,
                                    two_values);
      },
      "register-memory burst-word limit was not enforced");

  limits = {};
  limits.maximum_burst_value_bits = 8;
  SystemVerilogUvmRegisterModelService burst_bits{invalid_fixture.components,
                                                  limits};
  const auto burst_bits_top = burst_bits.create_block(
      {invalid_fixture.root, std::nullopt, "burst_bits"});
  const auto burst_bits_map = burst_bits.create_map(
      {burst_bits_top, "map", 0, 1, Endian::Little, true});
  const auto burst_bits_memory = burst_bits.create_memory(
      {burst_bits_top, "memory", 8, 0, {2},
       SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  burst_bits.add_memory(burst_bits_map, burst_bits_memory, 0);
  burst_bits.lock_model(burst_bits_top);
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] {
        (void)burst_bits.write_burst(burst_bits_map, burst_bits_memory, 0,
                                     two_values);
      },
      "aggregate register-memory burst-value limit was not enforced");

  limits = {};
  limits.maximum_materialized_memory_words = 1;
  SystemVerilogUvmRegisterModelService atomic_burst{invalid_fixture.components,
                                                    limits};
  const auto atomic_top = atomic_burst.create_block(
      {invalid_fixture.root, std::nullopt, "atomic_burst"});
  const auto atomic_map = atomic_burst.create_map(
      {atomic_top, "map", 0, 1, Endian::Little, true});
  const auto atomic_memory = atomic_burst.create_memory(
      {atomic_top, "memory", 8, 0, {2},
       SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  atomic_burst.add_memory(atomic_map, atomic_memory, 0);
  atomic_burst.lock_model(atomic_top);
  const std::array atomic_values{map_bits("00000001"), map_bits("00000010")};
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] {
        (void)atomic_burst.write_burst(atomic_map, atomic_memory, 0,
                                       atomic_values);
      },
      "burst storage limit was not checked transactionally");
  const std::array index{std::size_t{0}};
  require_map(atomic_burst.write(atomic_memory, index, map_bits("00000011"))
                  .success(),
              "failed burst partially materialized memory state");

  limits = {};
  limits.maximum_mutations = 6;
  SystemVerilogUvmRegisterModelService mutation_burst{
      invalid_fixture.components, limits};
  const auto mutation_top = mutation_burst.create_block(
      {invalid_fixture.root, std::nullopt, "mutation_burst"});
  const auto mutation_map = mutation_burst.create_map(
      {mutation_top, "map", 0, 1, Endian::Little, true});
  const auto mutation_memory = mutation_burst.create_memory(
      {mutation_top, "memory", 8, 0, {2},
       SystemVerilogUvmRegisterAccessPolicy::ReadWrite});
  mutation_burst.add_memory(mutation_map, mutation_memory, 0);
  mutation_burst.lock_model(mutation_top);
  require_map_error(
      "FSIM-UVM-REG-006",
      [&] {
        (void)mutation_burst.write_burst(mutation_map, mutation_memory, 0,
                                         two_values);
      },
      "burst mutation failure was not contained and translated");
  require_map(mutation_burst
                  .write(mutation_memory, index, map_bits("00000111"))
                  .success(),
              "burst mutation failure did not roll back touched words");
}

} // namespace fsim::tests::runtime
