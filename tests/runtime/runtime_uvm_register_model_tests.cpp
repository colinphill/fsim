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

void require_model(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

void require_model_error(const std::string_view code,
                         const std::function<void()> &operation,
                         const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmRegisterModelError &error) {
    require_model(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

struct RegisterModelFixture {
  SystemVerilogClassHeap heap;
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};

  RegisterModelFixture()
      : objects(heap,
                [](std::string_view, std::string_view, std::string_view) {
                  return SystemVerilogClassHandle{};
                }),
        components(heap, objects) {
    first_root = components.create_root("first");
    second_root = components.create_root("second");
  }
};

void require_invalid_limits(RegisterModelFixture &fixture) {
  const auto rejects = [&](const SystemVerilogUvmRegisterModelLimits limits) {
    try {
      SystemVerilogUvmRegisterModelService service{fixture.components, limits};
      (void)service;
    } catch (const std::invalid_argument &) {
      return;
    }
    throw std::runtime_error{"zero UVM register-model limit was accepted"};
  };
  SystemVerilogUvmRegisterModelLimits limits;
#define FSIM_REJECT_ZERO(member)                                               \
  limits = {};                                                                 \
  limits.member = 0;                                                           \
  rejects(limits)
  FSIM_REJECT_ZERO(maximum_blocks);
  FSIM_REJECT_ZERO(maximum_maps);
  FSIM_REJECT_ZERO(maximum_registers);
  FSIM_REJECT_ZERO(maximum_fields);
  FSIM_REJECT_ZERO(maximum_memories);
  FSIM_REJECT_ZERO(maximum_declarations);
  FSIM_REJECT_ZERO(maximum_declarations_per_block);
  FSIM_REJECT_ZERO(maximum_block_depth);
  FSIM_REJECT_ZERO(maximum_name_bytes);
  FSIM_REJECT_ZERO(maximum_full_name_bytes);
  FSIM_REJECT_ZERO(maximum_width_bits);
  FSIM_REJECT_ZERO(maximum_register_value_bits);
  FSIM_REJECT_ZERO(maximum_dimensions);
  FSIM_REJECT_ZERO(maximum_dimension_extent);
  FSIM_REJECT_ZERO(maximum_memory_words);
  FSIM_REJECT_ZERO(maximum_reset_kinds_per_field);
  FSIM_REJECT_ZERO(maximum_reset_name_bytes);
  FSIM_REJECT_ZERO(maximum_reset_value_bits);
  FSIM_REJECT_ZERO(maximum_materialized_memory_words);
  FSIM_REJECT_ZERO(maximum_materialized_memory_bits);
  FSIM_REJECT_ZERO(maximum_map_registrations);
  FSIM_REJECT_ZERO(maximum_map_registrations_per_map);
  FSIM_REJECT_ZERO(maximum_submap_depth);
  FSIM_REJECT_ZERO(maximum_bus_width_bytes);
  FSIM_REJECT_ZERO(maximum_physical_beats);
  FSIM_REJECT_ZERO(maximum_physical_byte_enables);
  FSIM_REJECT_ZERO(maximum_burst_words);
  FSIM_REJECT_ZERO(maximum_burst_value_bits);
  FSIM_REJECT_ZERO(maximum_address_lookup_work);
  FSIM_REJECT_ZERO(maximum_adapters);
  FSIM_REJECT_ZERO(maximum_predictors);
  FSIM_REJECT_ZERO(maximum_frontdoor_operations);
  FSIM_REJECT_ZERO(maximum_pending_frontdoor_operations);
  FSIM_REJECT_ZERO(maximum_frontdoor_bus_items);
  FSIM_REJECT_ZERO(maximum_frontdoor_value_bits);
  FSIM_REJECT_ZERO(maximum_frontdoor_byte_enables);
  FSIM_REJECT_ZERO(maximum_predictor_observations);
  FSIM_REJECT_ZERO(maximum_user_frontdoors);
  FSIM_REJECT_ZERO(maximum_hdl_paths);
  FSIM_REJECT_ZERO(maximum_hdl_slices);
  FSIM_REJECT_ZERO(maximum_hdl_path_bytes);
  FSIM_REJECT_ZERO(maximum_backdoor_operations);
  FSIM_REJECT_ZERO(maximum_backdoor_value_bits);
  FSIM_REJECT_ZERO(maximum_standard_sequences);
  FSIM_REJECT_ZERO(maximum_standard_sequence_operations);
  FSIM_REJECT_ZERO(maximum_standard_sequence_failures);
  FSIM_REJECT_ZERO(maximum_standard_sequence_failure_bytes);
  FSIM_REJECT_ZERO(maximum_standard_sequence_exclusions);
  FSIM_REJECT_ZERO(maximum_standard_sequence_exclusion_bytes);
  FSIM_REJECT_ZERO(maximum_register_callbacks);
  FSIM_REJECT_ZERO(maximum_register_callbacks_per_access);
  FSIM_REJECT_ZERO(maximum_register_callback_invocations);
  FSIM_REJECT_ZERO(maximum_register_callback_failures);
  FSIM_REJECT_ZERO(maximum_register_coverage_models);
  FSIM_REJECT_ZERO(maximum_register_coverage_samples);
  FSIM_REJECT_ZERO(maximum_register_coverage_bins);
  FSIM_REJECT_ZERO(maximum_register_coverage_bin_bytes);
  FSIM_REJECT_ZERO(maximum_operations);
  FSIM_REJECT_ZERO(maximum_mutations);
#undef FSIM_REJECT_ZERO
}

void require_live_limits(RegisterModelFixture &fixture) {
  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_blocks = 1;
  SystemVerilogUvmRegisterModelService blocks{fixture.components, limits};
  const auto block_top =
      blocks.create_block({fixture.first_root, std::nullopt, "top"});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)blocks.create_block({0, block_top, "child"}); },
      "register-block count limit was not enforced");

  limits = {};
  limits.maximum_maps = 1;
  SystemVerilogUvmRegisterModelService maps{fixture.components, limits};
  const auto map_top =
      maps.create_block({fixture.first_root, std::nullopt, "top"});
  (void)maps.create_map({map_top, "first", 0});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)maps.create_map({map_top, "second", 0}); },
      "register-map count limit was not enforced");

  limits = {};
  limits.maximum_registers = 1;
  SystemVerilogUvmRegisterModelService registers{fixture.components, limits};
  const auto register_top =
      registers.create_block({fixture.first_root, std::nullopt, "top"});
  (void)registers.create_register({register_top, "first", 8, 0});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)registers.create_register({register_top, "second", 8, 1});
      },
      "register count limit was not enforced");

  limits = {};
  limits.maximum_fields = 1;
  SystemVerilogUvmRegisterModelService fields{fixture.components, limits};
  const auto field_top =
      fields.create_block({fixture.first_root, std::nullopt, "top"});
  const auto field_register =
      fields.create_register({field_top, "register", 8, 0});
  (void)fields.create_field({field_register, "first", 4, 0});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)fields.create_field({field_register, "second", 4, 4}); },
      "register-field count limit was not enforced");

  limits = {};
  limits.maximum_memories = 1;
  SystemVerilogUvmRegisterModelService memories{fixture.components, limits};
  const auto memory_top =
      memories.create_block({fixture.first_root, std::nullopt, "top"});
  (void)memories.create_memory({memory_top, "first", 8, 0, {1}});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)memories.create_memory({memory_top, "second", 8, 1, {1}});
      },
      "register-memory count limit was not enforced");

  limits = {};
  limits.maximum_declarations = 1;
  SystemVerilogUvmRegisterModelService declarations{fixture.components, limits};
  const auto declaration_top =
      declarations.create_block({fixture.first_root, std::nullopt, "top"});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)declarations.create_map({declaration_top, "map", 0}); },
      "register-model global declaration limit was not enforced");

  limits = {};
  limits.maximum_name_bytes = 3;
  SystemVerilogUvmRegisterModelService names{fixture.components, limits};
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)names.create_block({fixture.first_root, std::nullopt, "four"});
      },
      "register-model name storage limit was not enforced");

  limits = {};
  limits.maximum_full_name_bytes = 8;
  SystemVerilogUvmRegisterModelService full_names{fixture.components, limits};
  const auto full_name_top =
      full_names.create_block({fixture.first_root, std::nullopt, "x"});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)full_names.create_block({0, full_name_top, "y"}); },
      "register-model full-name storage limit was not enforced");
}

} // namespace

void test_systemverilog_uvm_register_model() {
  RegisterModelFixture fixture;
  SystemVerilogUvmRegisterModelService model{fixture.components};
  const auto top = model.create_block({fixture.first_root, std::nullopt, "soc"});
  const auto child = model.create_block({0, top, "peripheral"});
  const auto map = model.create_map({child, "bus", UINT64_C(0x1000)});
  const auto status = model.create_register({child, "status", 32, 0x10});
  const auto low = model.create_field({status, "low", 16, 0});
  const auto high = model.create_field({status, "high", 16, 16});
  const auto memory =
      model.create_memory({child, "samples", 32, 0x100, {4, 8}});

  const auto top_snapshot = model.snapshot(top);
  const auto child_snapshot = model.snapshot(child);
  const auto map_snapshot = model.snapshot(map);
  const auto register_snapshot = model.snapshot(status);
  const auto low_snapshot = model.snapshot(low);
  const auto high_snapshot = model.snapshot(high);
  const auto memory_snapshot = model.snapshot(memory);
  require_model(top_snapshot.identity == 1 && child_snapshot.identity == 2 &&
                    map_snapshot.identity == 3 &&
                    register_snapshot.identity == 4 &&
                    low_snapshot.identity == 5 && high_snapshot.identity == 6 &&
                    memory_snapshot.identity == 7,
                "register-model identities are not stable source order");
  require_model(child_snapshot.parent == top && child_snapshot.depth == 2 &&
                    child_snapshot.root == fixture.first_root,
                "register block hierarchy or root ownership was lost");
  require_model(top_snapshot.full_name == "first:soc" &&
                    child_snapshot.full_name == "first:soc.peripheral" &&
                    map_snapshot.full_name == "first:soc.peripheral.bus" &&
                    low_snapshot.full_name ==
                        "first:soc.peripheral.status.low",
                "register-model hierarchical names are unstable");
  require_model(map_snapshot.base_offset == 0x1000 &&
                    register_snapshot.width_bits == 32 &&
                    register_snapshot.offset == 0x10 &&
                    low_snapshot.least_significant_bit == 0 &&
                    high_snapshot.least_significant_bit == 16 &&
                    memory_snapshot.word_width_bits == 32 &&
                    memory_snapshot.offset == 0x100 &&
                    memory_snapshot.dimensions ==
                        std::vector<std::size_t>{4, 8} &&
                    memory_snapshot.word_count == 32,
                "register-model shape or offset metadata was lost");
  const auto declarations = model.declarations();
  require_model(declarations.size() == 7,
                "register-model declaration stream has the wrong size");
  for (std::size_t index = 0; index < declarations.size(); ++index) {
    require_model(declarations[index].identity == index + 1 &&
                      declarations[index].declaration_order == index + 1,
                  "register-model declaration order is not contiguous");
  }
  require_model(declarations[1].owner_identity == top_snapshot.identity &&
                    declarations[4].owner_identity ==
                        register_snapshot.identity,
                "register-model declaration ownership was lost");
  require_model(model.blocks().size() == 2 && model.maps().size() == 1 &&
                    model.registers().size() == 1 &&
                    model.fields().size() == 2 &&
                    model.memories().size() == 1,
                "typed register-model snapshots are incomplete");

  model.lock_model(top);
  require_model(model.snapshot(top).state ==
                        SystemVerilogUvmRegisterModelState::Locked &&
                    model.snapshot(child).state ==
                        SystemVerilogUvmRegisterModelState::Locked,
                "register-model lock did not recursively freeze the hierarchy");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)model.create_register({child, "late", 8, 0}); },
      "locked register-model mutation was accepted");
  require_model_error("FSIM-UVM-REG-001", [&] { model.lock_model(child); },
                      "nested register block locked as a top model");

  SystemVerilogUvmRegisterModelService foreign{fixture.components};
  require_model_error(
      "FSIM-UVM-REG-001", [&] { (void)foreign.snapshot(top); },
      "cross-service register-model handle was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] {
        (void)foreign.snapshot(SystemVerilogUvmRegisterBlockHandle{});
      },
      "empty register-model handle was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_block({9999, std::nullopt, "stale"}); },
      "stale register-model root was accepted");

  const auto other =
      foreign.create_block({fixture.second_root, std::nullopt, "other"});
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] {
        (void)foreign.create_block({fixture.first_root, other, "wrong_root"});
      },
      "cross-root register-model child was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] {
        (void)foreign.create_block({fixture.second_root, std::nullopt, "other"});
      },
      "duplicate top register block was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_map({other, "", 0}); },
      "empty register-model name was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_map({other, "bad.name", 0}); },
      "ambiguous register-model name was accepted");
  const auto duplicate = foreign.create_map({other, "duplicate", 0});
  (void)duplicate;
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_register({other, "duplicate", 8, 0}); },
      "cross-kind register-model child name collision was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_register({other, "zero", 0, 0}); },
      "zero-width register was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] {
        (void)foreign.create_register(
            {other, "overflow", 16,
             std::numeric_limits<std::uint64_t>::max()});
      },
      "overflowing register address span was accepted");
  const auto narrow = foreign.create_register({other, "narrow", 16, 0});
  (void)foreign.create_field({narrow, "first", 8, 0});
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_field({narrow, "overlap", 8, 4}); },
      "overlapping register field was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_field({narrow, "outside", 9, 8}); },
      "out-of-range register field was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_memory({other, "scalar", 8, 0, {}}); },
      "dimensionless register memory was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)foreign.create_memory({other, "empty", 8, 0, {2, 0}}); },
      "zero register-memory extent was accepted");
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] {
        (void)foreign.create_memory(
            {other, "span", 16,
             std::numeric_limits<std::uint64_t>::max(), {1}});
      },
      "overflowing register-memory span was accepted");

  SystemVerilogUvmRegisterModelLimits limits;
  limits.maximum_width_bits = 16;
  limits.maximum_dimensions = 2;
  limits.maximum_dimension_extent = 4;
  limits.maximum_memory_words = 8;
  SystemVerilogUvmRegisterModelService bounded{fixture.components, limits};
  const auto bounded_top =
      bounded.create_block({fixture.first_root, std::nullopt, "bounded"});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)bounded.create_register({bounded_top, "wide", 17, 0}); },
      "register width limit was not enforced");
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)bounded.create_memory({bounded_top, "rank", 8, 0, {1, 1, 1}});
      },
      "register-memory dimension limit was not enforced");
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)bounded.create_memory({bounded_top, "extent", 8, 0, {5}});
      },
      "register-memory extent limit was not enforced");
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] {
        (void)bounded.create_memory({bounded_top, "words", 8, 0, {4, 4}});
      },
      "register-memory word-count limit was not enforced");

  SystemVerilogUvmRegisterModelLimits depth_limits;
  depth_limits.maximum_block_depth = 1;
  SystemVerilogUvmRegisterModelService shallow{fixture.components,
                                               depth_limits};
  const auto shallow_top =
      shallow.create_block({fixture.first_root, std::nullopt, "shallow"});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)shallow.create_block({0, shallow_top, "child"}); },
      "register-block depth limit was not enforced");

  SystemVerilogUvmRegisterModelLimits count_limits;
  count_limits.maximum_declarations_per_block = 1;
  SystemVerilogUvmRegisterModelService one_child{fixture.components,
                                                 count_limits};
  const auto one_child_top =
      one_child.create_block({fixture.first_root, std::nullopt, "one_child"});
  (void)one_child.create_map({one_child_top, "only", 0});
  require_model_error(
      "FSIM-UVM-REG-002",
      [&] { (void)one_child.create_register({one_child_top, "extra", 8, 0}); },
      "per-block register-model declaration limit was not enforced");

  count_limits = {};
  count_limits.maximum_mutations = 1;
  SystemVerilogUvmRegisterModelService one_mutation{fixture.components,
                                                    count_limits};
  const auto one_mutation_top = one_mutation.create_block(
      {fixture.first_root, std::nullopt, "one_mutation"});
  require_model_error(
      "FSIM-UVM-REG-002", [&] { one_mutation.lock_model(one_mutation_top); },
      "register-model mutation limit was not enforced");

  const auto transient_root = fixture.components.create_root("transient");
  SystemVerilogUvmRegisterModelService stale_model{fixture.components};
  const auto stale_top =
      stale_model.create_block({transient_root, std::nullopt, "stale"});
  fixture.components.destroy_root(transient_root);
  require_model_error(
      "FSIM-UVM-REG-001",
      [&] { (void)stale_model.create_map({stale_top, "map", 0}); },
      "mutation below a stale register-model root was accepted");

  require_live_limits(fixture);
  require_invalid_limits(fixture);
}

} // namespace fsim::tests::runtime
