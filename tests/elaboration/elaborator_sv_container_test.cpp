// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace fsim::tests::elaboration {

void test_systemverilog_container_lowering() {
  using namespace fsim::runtime::simir;
  const auto parsed = fsim::frontend::parse_text(
      "container-lowering.sv",
      R"(
module container_lowering #(
    parameter int STATIC_LEFT = 3);
  typedef logic signed [31:0] key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];
  logic [7:0] fixed_down[STATIC_LEFT:1];
  bit [3:0] fixed_up[-1:1];

  function automatic int count(input byte source[$:2]);
    byte copy[$:2];
    copy = '{5, 6};
    assert (copy[1] == 6);
    return source.size();
  endfunction

  function automatic int isolated_count(input byte source[key_t]);
    byte copy[key_t];
    copy = source;
    copy.delete(3);
    return copy.size();
  endfunction

  task automatic mutate(inout byte target[$:2]);
    byte ordered[$];
    ordered = '{3, 1, 2, 1};
    ordered.sort();
    assert (ordered[0] == 1);
    assert (ordered[3] == 3);
    ordered.rsort();
    assert (ordered[0] == 3);
    ordered.reverse();
    assert (ordered[0] == 1);
    target.push_back(4);
    target.rsort();
    assert (target[0] == 4);
    target.reverse();
    target.sort();
    #1;
    target.pop_front();
  endtask

  task automatic mutate_lookup(inout byte target[key_t]);
    target[-1] = 9;
    #1;
    target.delete(3);
  endtask

  function automatic byte copied_static(
      input logic [7:0] source[STATIC_LEFT:1]);
    logic [7:0] copy[STATIC_LEFT:1];
    copy = source;
    copy[2] = 8'h99;
    return copy[3];
  endfunction

  task automatic mutate_static(
      inout logic [7:0] target[STATIC_LEFT:1]);
    target[2] = 8'h22;
    #1;
    target[1] = 8'h11;
  endtask

  initial begin
    key_t key;
    int located[$];
    int locations[$];
    values = '{};
    assert ($size(values) == 0);
    assert (values.sum() == 0);
    assert (values.product() == 1);
    assert (values.and() == -1);
    assert (values.or() == 0);
    assert (values.xor() == 0);
    assert (
        values.product() with (
            item.index >= 0 ? item : 1) == 1);
    values = '{30, 10, 20, 11};
    values.sort() with (
        item.index < 2 ? 0 : item);
    assert (values[0] == 30);
    assert (values[1] == 10);
    assert (values[2] == 11);
    assert (values[3] == 20);
    values.rsort(entry) with (
        entry.index < 2 ? 0 : entry);
    assert (values[0] == 20);
    assert (values[1] == 11);
    assert (values[2] == 30);
    assert (values[3] == 10);
    values = '{7, 8, 7};
    assert (values.sum() == 22);
    assert (values.product() == 392);
    assert (values.and() == 0);
    assert (values.or() == 15);
    assert (values.xor() == 8);
    assert (
        values.sum() with (
            item.index == 1 ? item : 0) == 8);
    assert (
        values.sum(entry) with (
            entry.index == 1 ? entry : 0) == 8);
    assert (
        values.xor() with (
            item > 7 ? item : 0) == 8);
    located = values.min();
    assert (located.size() == 1);
    assert (located[0] == 7);
    located = values.max();
    assert (located[0] == 8);
    located =
        values.min() with (
            item == 7 ? 9 : item);
    assert (located.size() == 1);
    assert (located[0] == 8);
    located =
        values.max(entry) with (
            entry.index == 1 ? 0 : entry);
    assert (located.size() == 1);
    assert (located[0] == 7);
    located = values.unique();
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == 8);
    located =
        values.unique() with (
            item == 8 ? 7 : item);
    assert (located.size() == 1);
    assert (located[0] == 7);
    located =
        values.unique(alias_item) with (
            alias_item == 8 ? 7 : alias_item);
    assert (located.size() == 1);
    assert (located[0] == 7);
    locations = values.unique_index();
    assert (locations.size() == 2);
    assert (locations[0] == 0);
    assert (locations[1] == 1);
    locations =
        values.unique_index(entry) with (
            entry == 8 ? 7 : entry);
    assert (locations.size() == 1);
    assert (locations[0] == 0);
    located = values.find() with (item == 7);
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == 7);
    locations = values.find_index() with (item != 7);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    located =
        values.find_first() with (item >= STATIC_LEFT + 4);
    assert (located.size() == 1);
    assert (located[0] == 7);
    locations =
        values.find_first_index() with (item > 7 && item < 9);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    located =
        values.find_last() with (item <= 7 || item == 99);
    assert (located.size() == 1);
    assert (located[0] == 7);
    locations =
        values.find_last_index() with (!(item == 8));
    assert (locations.size() == 1);
    assert (locations[0] == 2);
    located =
        values.find(entry) with (
            entry == 7 && entry.index >= 0);
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == 7);
    located =
        values.find(alias_entry) with (
            alias_entry == 7 && alias_entry.index >= 0);
    assert (located.size() == 2);
    locations =
        values.find_index(entry) with (entry.index == 1);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    values.reverse();
    assert (values[0] == 7);
    values.sort();
    assert (values[0] == 7);
    values.rsort();
    assert (values[0] == 8);
    values.reverse();
    pending = '{1, 2};
    locations =
        pending.find_index(byte_entry) with (
            byte_entry.index > 0);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    lookup = '{-1: 10, 3: 30};
    assert (pending.sum() == 3);
    assert (
        pending.sum() with (
            item.index > 0 ? item : 0) == 2);
    assert (lookup.sum() == 40);
    fixed_down = '{8'h31, 8'h21, 8'h11};
    fixed_up = '{4'ha, 4'hb, 4'hc};
    fixed_up.rsort(slot) with (
        slot.index < 0 ? 0 : slot);
    assert (fixed_up[-1] == 4'hc);
    assert (fixed_up[0] == 4'hb);
    assert (fixed_up[1] == 4'ha);
    fixed_up = '{4'ha, 4'hb, 4'hc};
    fixed_down.reverse();
    assert (fixed_down[STATIC_LEFT] == 8'h11);
    fixed_down.sort();
    assert (fixed_down[STATIC_LEFT] == 8'h11);
    fixed_down.rsort();
    assert (fixed_down[STATIC_LEFT] == 8'h31);
    fixed_up.reverse();
    assert (fixed_up[-1] == 4'hc);
    fixed_up.reverse();
    assert (values[1] == 8);
    assert (pending[0] == 1);
    assert (lookup[-1] == 10);
    assert (fixed_down[STATIC_LEFT] == 8'h31);
    assert (fixed_down[1] == 8'h11);
    assert (fixed_up[-1] == 4'ha);
    assert (fixed_up[0] == 4'hb);
    assert (fixed_up[1] == 4'hc);
    fixed_up[0] = 0;
    locations =
        fixed_up.unique_index() with (
            item.index < 0 ? 0 : item);
    assert (locations.size() == 2);
    assert (locations[0] == -1);
    assert (locations[1] == 1);
    locations = fixed_up.find_index() with (item != 0);
    assert (locations.size() == 2);
    assert (locations[0] == -1);
    assert (locations[1] == 1);
    locations =
        fixed_up.find_index(bit_entry) with (
            bit_entry.index < 0 && bit_entry != 0);
    assert (locations.size() == 1);
    assert (locations[0] == -1);
    assert (fixed_down[2] == 8'h21);
    fixed_down[3] = 8'h33;
    fixed_up[-1] = 4'ha;
    fixed_up[1] = 4'hc;
    assert ($left(fixed_down) == STATIC_LEFT);
    assert ($right(fixed_down) == 1);
    assert ($low(fixed_down) == 1);
    assert ($high(fixed_down) == STATIC_LEFT);
    assert ($increment(fixed_down) == 1);
    assert ($size(fixed_down) == 3);
    assert ($bits(fixed_down) == 24);
    assert ($left(fixed_up) == -1);
    assert ($right(fixed_up) == 1);
    assert ($low(fixed_up) == -1);
    assert ($high(fixed_up) == 1);
    assert ($increment(fixed_up) == -1);
    assert ($size(fixed_up) == 3);
    assert ($bits(fixed_up) == 12);
    assert (copied_static(fixed_down) == 8'h33);
    assert (fixed_down[2] == 8'h21);
    assert (fixed_up[-1] == 4'ha);
    assert (fixed_up[0] == 0);
    assert (
        fixed_up.sum() with (
            item.index < 0 ? item : 0) == 4'ha);
    assert (lookup.size() == 2);
    assert (isolated_count(lookup) == 1);
    assert (lookup.size() == 2);
    assert (lookup.exists(3) == 1);
    assert (lookup[4] == 0);
    assert (lookup.first(key) == 1);
    assert (key == -1);
    assert (lookup.next(key) == 1);
    assert (key == 3);
    mutate(pending);
    mutate_lookup(lookup);
    mutate_static(fixed_down);
    assert (values[0] == 7);
    assert (count(pending) == 2);
    assert (fixed_down[3] == 8'h33);
    assert (fixed_down[2] == 8'h22);
    assert (fixed_down[1] == 8'h11);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "container_lowering");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  assert(elaborated.design->container_objects().size() == 5);
  const auto& process = elaborated.design->processes().front();
  assert(process.container_register_count != 0);
  assert(!process.debug_container_locals.empty());
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<ResizeContainer>(operation);
      }));
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<ContainerExists>(operation);
      }));
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        return std::holds_alternative<TraverseContainer>(operation);
      }));
  assert(
      std::ranges::count_if(
          process.operations,
          [](const auto& operation) {
            return std::holds_alternative<
                ContainerReduction>(operation);
          })
      >= 12);
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        const auto* reduction =
            std::get_if<ContainerReduction>(&operation);
        if (reduction == nullptr
            || reduction->transformation.empty()) {
          return false;
        }
        const auto& graph = reduction->transformation;
        return graph.back().value_kind
                == ContainerPredicateValueKind::element
            && std::ranges::any_of(
                graph,
                [](const auto& node) {
                  return node.operation
                          == ContainerPredicateOperator::index
                      && node.value_kind
                          == ContainerPredicateValueKind::index;
                })
            && std::ranges::any_of(
                graph,
                [&](const auto& node) {
                  return node.operation
                          == ContainerPredicateOperator::conditional
                      && node.value_kind
                          == ContainerPredicateValueKind::element
                      && node.left < graph.size()
                      && node.right < graph.size()
                      && node.third < graph.size();
                });
      }));
  assert(
      std::ranges::count_if(
          process.operations,
          [](const auto& operation) {
            return std::holds_alternative<
                OrderContainer>(operation);
          })
      >= 16);
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        const auto* ordering =
            std::get_if<OrderContainer>(&operation);
        if (ordering == nullptr || ordering->key.empty()) {
          return false;
        }
        const auto& graph = ordering->key;
        return graph.back().value_kind
                == ContainerPredicateValueKind::element
            && std::ranges::any_of(
                graph,
                [](const auto& node) {
                  return node.operation
                          == ContainerPredicateOperator::index
                      && node.value_kind
                          == ContainerPredicateValueKind::index;
                })
            && std::ranges::any_of(
                graph,
                [&](const auto& node) {
                  return node.operation
                          == ContainerPredicateOperator::conditional
                      && node.left < graph.size()
                      && node.right < graph.size()
                      && node.third < graph.size();
                });
      }));
  assert(
      std::ranges::count_if(
          process.operations,
          [](const auto& operation) {
            return std::holds_alternative<
                LocateContainer>(operation);
          })
      >= 10);
  assert(
      std::ranges::count_if(
          process.operations,
          [](const auto& operation) {
            const auto* locator =
                std::get_if<LocateContainer>(&operation);
            return locator != nullptr
                && !locator->predicate.empty();
          })
      >= 7);
  assert(std::ranges::any_of(
      process.operations,
      [](const auto& operation) {
        const auto* locator =
            std::get_if<LocateContainer>(&operation);
        return locator != nullptr
            && std::ranges::any_of(
                locator->predicate,
                [](const auto& node) {
                  return node.operation
                          == ContainerPredicateOperator::index
                      && node.value_kind
                          == ContainerPredicateValueKind::index;
                })
            && std::ranges::all_of(
                locator->predicate,
                [](const auto& node) {
                  const bool result_node =
                      node.operation
                          >= ContainerPredicateOperator::equal;
                  return !result_node
                      || node.value_kind
                          == ContainerPredicateValueKind::logical;
                });
      }));
  std::vector<const LocateContainer*> predicate_locators;
  for (const auto& operation : process.operations) {
    if (const auto* locator =
            std::get_if<LocateContainer>(&operation);
        locator && !locator->predicate.empty()) {
      predicate_locators.push_back(locator);
    }
  }
  const auto same_predicate =
      [](const auto& left, const auto& right) {
        return left.size() == right.size()
            && std::ranges::equal(
                left, right,
                [](const auto& left_node,
                   const auto& right_node) {
                  return left_node.operation
                          == right_node.operation
                      && left_node.left == right_node.left
                      && left_node.right == right_node.right
                      && left_node.third == right_node.third
                      && left_node.constant
                          == right_node.constant
                      && left_node.value_kind
                          == right_node.value_kind;
                });
      };
  std::vector<const ContainerReduction*> transformed_reductions;
  for (const auto& operation : process.operations) {
    if (const auto* reduction =
            std::get_if<ContainerReduction>(&operation);
        reduction && !reduction->transformation.empty()) {
      transformed_reductions.push_back(reduction);
    }
  }
  assert(transformed_reductions.size() >= 5);
  assert(same_predicate(
      transformed_reductions[1]->transformation,
      transformed_reductions[2]->transformation));
  bool spelling_independent = false;
  for (std::size_t left = 0;
       left < predicate_locators.size(); ++left) {
    for (std::size_t right = left + 1U;
         right < predicate_locators.size(); ++right) {
      if (predicate_locators[left]->predicate.size() >= 7
          && same_predicate(
              predicate_locators[left]->predicate,
              predicate_locators[right]->predicate)) {
        spelling_independent = true;
      }
    }
  }
  assert(spelling_independent);
  std::vector<const LocateContainer*> transformed_locators;
  for (const auto& operation : process.operations) {
    if (const auto* locator =
            std::get_if<LocateContainer>(&operation);
        locator && !locator->transformation.empty()) {
      transformed_locators.push_back(locator);
    }
  }
  assert(transformed_locators.size() >= 6);
  assert(same_predicate(
      transformed_locators[2]->transformation,
      transformed_locators[3]->transformation));
  std::vector<const OrderContainer*> keyed_orderings;
  for (const auto& operation : process.operations) {
    if (const auto* ordering =
            std::get_if<OrderContainer>(&operation);
        ordering && !ordering->key.empty()) {
      keyed_orderings.push_back(ordering);
    }
  }
  assert(keyed_orderings.size() >= 3);
  assert(same_predicate(
      keyed_orderings[0]->key,
      keyed_orderings[1]->key));
  const auto values =
      elaborated.design->container_objects()[0].id;
  const auto pending =
      elaborated.design->container_objects()[1].id;
  const auto lookup =
      elaborated.design->container_objects()[2].id;
  const auto fixed_down =
      elaborated.design->container_objects()[3].id;
  const auto fixed_up =
      elaborated.design->container_objects()[4].id;
  auto interpreter = elaborated.design->create_interpreter();
  const auto result = interpreter->run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 3);
  const auto& values_result =
      interpreter->container_object_value(values);
  const auto& pending_result =
      interpreter->container_object_value(pending);
  const auto& lookup_result =
      interpreter->container_object_value(lookup);
  const auto& fixed_down_result =
      interpreter->container_object_value(fixed_down);
  const auto& fixed_up_result =
      interpreter->container_object_value(fixed_up);
  assert(
      values_result.elements.size() == 3
      && values_result.elements[0].low_word().aval == 7
      && pending_result.elements.size() == 2
      && pending_result.elements[0].low_word().aval == 2
      && pending_result.elements[1].low_word().aval == 4
      && lookup_result.keys.size() == 1
      && lookup_result.keys[0].low_word().aval
          == UINT64_C(0xffffffff)
      && lookup_result.elements[0].low_word().aval == 9
      && fixed_down_result.type.fixed
      && fixed_down_result.type.index_left == 3
      && fixed_down_result.type.index_right == 1
      && fixed_down_result.elements.size() == 3
      && fixed_down_result.elements[0].low_word().aval == 0x33
      && fixed_down_result.elements[1].low_word().aval == 0x22
      && fixed_down_result.elements[2].low_word().aval == 0x11
      && fixed_up_result.type.fixed
      && fixed_up_result.type.index_left == -1
      && fixed_up_result.type.index_right == 1
      && fixed_up_result.elements[0].low_word().aval == 0xa
      && fixed_up_result.elements[1].low_word().aval == 0
      && fixed_up_result.elements[2].low_word().aval == 0xc);

  const auto port_parsed = fsim::frontend::parse_text(
      "container-ports.sv",
      R"(
module static_port_leaf #(
    parameter int LEFT = 3,
    parameter int RIGHT = 0) (
    input logic signed [7:0] source[LEFT:RIGHT],
    output logic signed [7:0] result[LEFT:RIGHT],
    inout bit [3:0] shared[-1:1]);
  initial begin
    #1;
    assert ($left(source) == LEFT);
    assert ($right(source) == RIGHT);
    assert ($low(source) == RIGHT);
    assert ($high(source) == LEFT);
    assert ($increment(source) == 1);
    assert ($size(source, RIGHT - RIGHT + 1) == 4);
    assert ($bits(source) == 32);
    assert ($dimensions(source) == 2);
    assert ($unpacked_dimensions(source) == 1);
    assert (source[LEFT] == 8'h31);
    assert (source[RIGHT] == 8'h04);
    result = source;
    result[LEFT] = source[LEFT] + 8'h01;
    result[RIGHT] = source[RIGHT] + 8'h02;
    shared[-1] = 4'ha;
    shared[1] = 4'hc;
  end
endmodule

module static_port_mid #(
    parameter int HIGH = 3,
    parameter int LOW = 0) (
    input logic signed [7:0] source[HIGH:LOW],
    output logic signed [7:0] result[HIGH:LOW],
    inout bit [3:0] shared[-1:1]);
  generate
    if (HIGH == 3) begin : generated
      static_port_leaf #(
          .LEFT(HIGH), .RIGHT(LOW)) child(
          .source(source), .result(result), .shared(shared));
    end
  endgenerate
endmodule

module non_ansi_port_leaf(result);
  parameter int LEFT = 2;
  output logic [7:0] result[LEFT:0];
  initial begin
    #1;
    result[LEFT] = 8'h5a;
  end
endmodule

module static_port_top;
  logic signed [7:0] source[3:0];
  logic signed [7:0] result[3:0];
  bit [3:0] shared[-1:1];
  logic [7:0] non_ansi_result[2:0];
  static_port_mid #(
      .HIGH(3), .LOW(0)) mid(
      .source(source), .result(result), .shared(shared));
  non_ansi_port_leaf non_ansi(
      .result(non_ansi_result));
  initial begin
    source[3] = 8'h31;
    source[0] = 8'h04;
    #2;
    assert (result[3] == 8'h32);
    assert (result[0] == 8'h06);
    assert (shared[-1] == 4'ha);
    assert (shared[0] == 0);
    assert (shared[1] == 4'hc);
    assert (non_ansi_result[2] == 8'h5a);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(port_parsed.ok());
  const auto port_elaborated = fsim::elaboration::elaborate(
      port_parsed.design, "static_port_top");
  if (!port_elaborated.ok()) {
    for (const auto& diagnostic : port_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(port_elaborated.ok());
  assert(
      port_elaborated.design->container_objects().size() == 4);
  const auto paths =
      port_elaborated.design->container_paths();
  assert(
      std::ranges::any_of(
          paths,
          [](const auto& path) {
            return path.first
                == "static_port_top.mid.source";
          })
      && std::ranges::any_of(
          paths,
          [](const auto& path) {
            return path.first
                == "static_port_top.mid.generated.child.result";
          }));
  const auto source_id =
      port_elaborated.design->find_container(
          "static_port_top.source");
  const auto child_source_id =
      port_elaborated.design->find_container(
          "static_port_top.mid.generated.child.source");
  assert(source_id && child_source_id);
  assert(*source_id == *child_source_id);
  auto port_interpreter =
      port_elaborated.design->create_interpreter();
  const auto port_result = port_interpreter->run();
  assert(
      port_result.status
          == fsim::runtime::RunStatus::completed
      && port_result.time == 2);
  const auto result_id =
      port_elaborated.design->find_container(
          "static_port_top.result");
  const auto shared_id =
      port_elaborated.design->find_container(
          "static_port_top.shared");
  assert(result_id && shared_id);
  const auto& result_value =
      port_interpreter->container_object_value(*result_id);
  const auto& shared_value =
      port_interpreter->container_object_value(*shared_id);
  assert(
      result_value.elements[0].low_word().aval == 0x32
      && result_value.elements[3].low_word().aval == 0x06
      && shared_value.elements[0].low_word().aval == 0xa
      && shared_value.elements[1].low_word().aval == 0
      && shared_value.elements[2].low_word().aval == 0xc);

  const auto dynamic_port_parsed = fsim::frontend::parse_text(
      "dynamic-container-ports.sv",
      R"(
module dynamic_port_leaf #(
    parameter int LIMIT = 3,
    parameter type KEY = logic signed [3:0]) (
    input int source[],
    output logic [7:0] result[$],
    inout bit bounded[$:LIMIT],
    inout logic [15:0] scores[KEY],
    inout int work[]);
  initial begin
    KEY cursor;
    #1;
    assert ($left(source) == 0);
    assert ($right(source) == 1);
    assert ($low(source) == 0);
    assert ($high(source) == 1);
    assert ($increment(source) == -1);
    assert ($size(source, LIMIT - LIMIT + 1) == 2);
    assert ($bits(source) == 64);
    assert ($dimensions(source) == 2);
    assert ($unpacked_dimensions(source) == 1);
    assert ($right(result) == -1);
    assert ($high(result) == -1);
    assert ($bits(result) == 0);
    assert (source.size() == 2);
    assert (source[0] == 11);
    result.push_back(8'h21);
    result.push_back(8'h22);
    bounded.push_back(1);
    bounded.push_front(0);
    scores[-1] = 16'h1234;
    scores[2] = 16'h5678;
    assert (scores.first(cursor) == 1);
    assert (cursor == -1);
    assert (scores.next(cursor) == 1);
    assert (cursor == 2);
    assert ($size(bounded) == 2);
    assert ($bits(bounded) == 2);
    assert ($size(scores) == 2);
    assert ($bits(scores) == 32);
    assert ($dimensions(scores) == 2);
    assert ($unpacked_dimensions(scores) == 1);
    work = new[3];
    work[0] = 31;
    work[2] = 33;
    assert ($right(work) == 2);
    assert ($high(work) == 2);
    assert ($size(work) == 3);
    assert ($bits(work) == 96);
  end
endmodule

module dynamic_port_mid #(
    parameter int MAXIMUM = 3,
    parameter type INDEX = logic signed [3:0]) (
    input int source[],
    output logic [7:0] result[$],
    inout bit bounded[$:MAXIMUM],
    inout logic [15:0] scores[INDEX],
    inout int work[]);
  generate
    if (MAXIMUM == 3) begin : generated
      dynamic_port_leaf #(
          .LIMIT(MAXIMUM), .KEY(INDEX)) child(
          .source(source),
          .result(result),
          .bounded(bounded),
          .scores(scores),
          .work(work));
    end
  endgenerate
endmodule

module dynamic_port_top;
  typedef logic signed [3:0] key_t;
  int source[];
  logic [7:0] result[$];
  bit bounded[$:3];
  logic [15:0] scores[key_t];
  int work[];
  dynamic_port_mid #(
      .MAXIMUM(3), .INDEX(key_t)) mid(
      .source(source),
      .result(result),
      .bounded(bounded),
      .scores(scores),
      .work(work));
  initial begin
    source = new[2];
    source[0] = 11;
    source[1] = 12;
    #2;
    assert (result.size() == 2);
    assert (result[0] == 8'h21);
    assert (result[1] == 8'h22);
    assert (bounded.size() == 2);
    assert (bounded[0] == 0);
    assert (bounded[1] == 1);
    assert (scores.size() == 2);
    assert (scores[-1] == 16'h1234);
    assert (scores[2] == 16'h5678);
    assert (work.size() == 3);
    assert (work[0] == 31);
    assert (work[2] == 33);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(dynamic_port_parsed.ok());
  const auto dynamic_port_elaborated =
      fsim::elaboration::elaborate(
          dynamic_port_parsed.design, "dynamic_port_top");
  if (!dynamic_port_elaborated.ok()) {
    for (const auto& diagnostic :
         dynamic_port_elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(dynamic_port_elaborated.ok());
  assert(
      dynamic_port_elaborated.design
          ->container_objects().size() == 5);
  const auto dynamic_source =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.source");
  const auto nested_dynamic_source =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.mid.generated.child.source");
  const auto dynamic_bounded =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.bounded");
  assert(
      dynamic_source && nested_dynamic_source
      && *dynamic_source == *nested_dynamic_source
      && dynamic_bounded);
  const auto& bounded_info =
      dynamic_port_elaborated.design->container_objects().at(
          *dynamic_bounded);
  assert(
      bounded_info.type.queue
      && bounded_info.type.maximum_elements
      && *bounded_info.type.maximum_elements == 4);
  auto dynamic_port_interpreter =
      dynamic_port_elaborated.design->create_interpreter();
  const auto dynamic_port_result =
      dynamic_port_interpreter->run();
  assert(
      dynamic_port_result.status
          == fsim::runtime::RunStatus::completed
      && dynamic_port_result.time == 2);
  const auto dynamic_result =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.result");
  const auto dynamic_scores =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.scores");
  const auto dynamic_work =
      dynamic_port_elaborated.design->find_container(
          "dynamic_port_top.work");
  assert(dynamic_result && dynamic_scores && dynamic_work);
  assert(
      dynamic_port_interpreter
              ->container_object_value(*dynamic_result)
              .elements.size()
          == 2
      && dynamic_port_interpreter
              ->container_object_value(*dynamic_scores)
              .elements.size()
          == 2
      && dynamic_port_interpreter
              ->container_object_value(*dynamic_work)
              .elements.size()
          == 3);

  const auto invalid = fsim::frontend::parse_text(
      "container-invalid-lowering.sv",
      R"(
module container_invalid_lowering;
  typedef struct packed {
    logic [3:0] value;
  } pair_t;
  byte composite_key[pair_t];
  pair_t composite_element[int];
  byte lookup[int];
  byte dynamic[];
  byte fixed[1:0];
  byte too_large[0:4096];
  int runtime_bound;
  byte nonconstant[runtime_bound:0];
  byte locator_result[$];
  int locator_indices[$];
  int result;
  int collision;
  function automatic byte identity(input byte value);
    return value;
  endfunction
  initial begin
    lookup.push_back(1);
    result = lookup.sort();
    result = lookup.find() with (item);
    result = result.sum();
    lookup.sum();
    result = lookup.sum() with (item);
    result = fixed.sum(collision) with (collision);
    result = fixed.sum() with (item + 1);
    result = fixed.sum() with (identity(item));
    result = fixed.sum() with (item > 0);
    result =
        fixed.sum() with (
            item ? (item ? item : 0) : 0);
    result =
        fixed.sum() with (
            item.index.member == 0 ? item : 0);
    result =
        fixed.sum() with (
            item == item.index ? item : 0);
    result =
        fixed.sum() with (
            runtime_bound ? item : 0);
    locator_result = fixed.min() with (item + 1);
    locator_result = fixed.max() with (identity(item));
    locator_result = fixed.unique() with (item > 0);
    locator_result =
        fixed.unique() with (
            item ? (item ? item : 0) : 0);
    locator_result =
        fixed.unique_index() with (
            item.index.member == 0 ? item : 0);
    locator_result =
        fixed.unique() with (
            item == item.index ? item : 0);
    locator_result =
        fixed.min() with (
            runtime_bound ? item : 0);
    locator_result =
        fixed.max(collision) with (collision);
    locator_result =
        fixed.unique(entry) with (
            unknown.index == 0 ? entry : 0);
    locator_result = lookup.min() with (item);
    fixed.sort() with (item + 1);
    fixed.sort() with (identity(item));
    fixed.sort() with (item > 0);
    fixed.sort() with (
        item ? (item ? item : 0) : 0);
    fixed.sort() with (
        item.index.member == 0 ? item : 0);
    fixed.sort() with (
        item == item.index ? item : 0);
    fixed.sort() with (
        runtime_bound ? item : 0);
    fixed.sort(collision) with (collision);
    fixed.sort(entry) with (unknown.index == 0 ? entry : 0);
    result.sort();
    lookup.sort();
    fixed[0].sort();
    result = fixed.sort();
    fixed.shuffle();
    locator_result = lookup.min();
    dynamic = fixed.min();
    locator_indices = fixed.unique();
    locator_result = fixed[0].min();
    result = fixed.min();
    fixed.min();
    locator_result = lookup.find() with (item);
    dynamic = fixed.find() with (item);
    locator_result = fixed.find_index() with (item);
    locator_result = fixed.find() with (item + 1 > 0);
    locator_result =
        fixed.find(collision) with (collision > 0);
    locator_result =
        fixed.find(entry) with (unknown.index == 0);
    locator_result =
        fixed.find(entry) with (entry.index.member == 0);
    locator_result =
        fixed.find(entry) with (entry.index() == 0);
    locator_result =
        fixed.find(entry) with (entry == entry.index);
    result = fixed.find() with (item);
    fixed.find() with (item);
    lookup[0] <= 1;
    dynamic.delete(0);
    fixed.delete();
    fixed = new[2];
    $readmemh("invalid.hex", dynamic);
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto rejected = fsim::elaboration::elaborate(
      invalid.design, "container_invalid_lowering");
  assert(!rejected.ok());
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVREDUCE-001"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVREDUCE-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVREDUCE-004"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVREDUCE-005"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVREDUCE-006"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVREDUCE-007"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-001"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-004"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-005"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-006"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-007"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVORDER-008"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-001"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-004"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-005"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-006"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-007"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVLOCATOR-008"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-001"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-003"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-004"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-005"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-006"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-007"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVFIND-008"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-013"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-018"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-019"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-009"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-020"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-021"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVCONTAINER-014"));
  assert(has_diagnostic(
      rejected, "FSIM-ELAB-SVMEMORY-003"));

  std::string oversized_predicate =
      "module oversized_predicate; byte values[]; "
      "byte result[$]; initial result = values.find() with (";
  for (int value = 0; value < 17; ++value) {
    if (value != 0) {
      oversized_predicate += " || ";
    }
    oversized_predicate +=
        "item == " + std::to_string(value);
  }
  oversized_predicate += "); endmodule";
  const auto oversized_parsed =
      fsim::frontend::parse_text(
          "container-predicate-oversized.sv",
          oversized_predicate,
          fsim::frontend::Language::SystemVerilog2017);
  assert(oversized_parsed.ok());
  const auto oversized_rejected =
      fsim::elaboration::elaborate(
          oversized_parsed.design, "oversized_predicate");
  assert(
      !oversized_rejected.ok()
      && has_diagnostic(
          oversized_rejected, "FSIM-ELAB-SVFIND-004"));

  const auto leaked_iterator = fsim::frontend::parse_text(
      "container-iterator-leak.sv",
      "module container_iterator_leak; "
      "int values[]; int located[$]; int result; "
      "initial begin "
      "located = values.find(entry) with (entry > 0); "
      "result = entry; "
      "end endmodule",
      fsim::frontend::Language::SystemVerilog2017);
  assert(
      !leaked_iterator.ok()
      && std::ranges::any_of(
          leaked_iterator.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-090";
          }));

  const auto invalid_ports = fsim::frontend::parse_text(
      "container-port-invalid.sv",
      R"(
module bad_input(
    input logic [7:0] memory[3:0]);
  initial begin
    memory[3] = 8'hff;
    memory.sort();
  end
endmodule

module incompatible(
    input logic [7:0] memory[0:3]);
endmodule

module output_driver(
    output logic [7:0] memory[3:0]);
  initial memory[3] = 8'h01;
endmodule

module input_forward(
    input logic [7:0] memory[3:0]);
  output_driver illegal_descendant(.memory(memory));
endmodule

module bad_port_top;
  logic [7:0] memory[3:0];
  bad_input input_child(.memory(memory));
  incompatible wrong_range(.memory(memory));
  incompatible expression_actual(.memory(memory[3]));
  incompatible unknown_actual(.memory(missing));
  output_driver first(.memory(memory));
  output_driver second(.memory(memory));
  input_forward forward(.memory(memory));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid_ports.ok());
  const auto rejected_ports = fsim::elaboration::elaborate(
      invalid_ports.design, "bad_port_top");
  assert(!rejected_ports.ok());
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-005"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-006"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-007"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-008"));
  assert(has_diagnostic(
      rejected_ports, "FSIM-ELAB-SVPORT-009"));

  const auto invalid_dynamic_ports =
      fsim::frontend::parse_text(
          "dynamic-container-port-invalid.sv",
          R"(
module dynamic_input(input int value[]);
  initial value = new[1];
endmodule

module dynamic_output(output int value[]);
  initial value = new[1];
endmodule

module dynamic_forward(input int value[]);
  dynamic_output illegal_descendant(.value(value));
endmodule

module dynamic_accept(input int value[]);
endmodule

module queue_accept(input int value[$]);
endmodule

module bounded_accept #(
    parameter int LIMIT = 3) (
    input int value[$:LIMIT]);
endmodule

module associative_accept(
    input int value[logic signed [3:0]]);
endmodule

module byte_dynamic_accept(input byte value[]);
endmodule

module bad_dynamic_port_top;
  typedef int item_t;
  int dynamic_value[];
  int queue_value[$];
  int bounded_value[$:2];
  int associative_value[logic signed [4:0]];
  int narrow_associative[logic signed [3:0]];
  int fixed_value[1:0];
  logic [7:0] four_state_value[];
  int query_result;
  dynamic_accept expression_actual(
      .value(dynamic_value[0]));
  dynamic_accept unknown_actual(.value(missing));
  queue_accept wrong_kind(.value(dynamic_value));
  bounded_accept #(.LIMIT(3)) wrong_bound(
      .value(bounded_value));
  associative_accept wrong_index(
      .value(associative_value));
  byte_dynamic_accept wrong_element(
      .value(four_state_value));
  dynamic_output first(.value(dynamic_value));
  dynamic_output second(.value(dynamic_value));
  dynamic_input read_only(.value(dynamic_value));
  dynamic_forward forward(.value(dynamic_value));
  initial begin
    query_result = $left(associative_value);
    query_result = $size(dynamic_value, 2);
    query_result = $bits(dynamic_value, 1);
    query_result = $dimensions(dynamic_value, 1);
    query_result = $size(item_t);
    fixed_value = '{1};
    bounded_value = '{1, 2, 3, 4};
    associative_value = '{1, 2};
    dynamic_value = '{0: 1};
    narrow_associative = '{-1: 1, 15: 2};
    dynamic_value = '{1, 2: 3};
    dynamic_value = '{default: 1};
    dynamic_value[0] = '{1};
    query_result = '{1};
  end
endmodule
)",
          fsim::frontend::Language::SystemVerilog2017);
  assert(invalid_dynamic_ports.ok());
  const auto rejected_dynamic_ports =
      fsim::elaboration::elaborate(
          invalid_dynamic_ports.design,
          "bad_dynamic_port_top");
  assert(!rejected_dynamic_ports.ok());
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-005"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-006"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-007"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-008"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPORT-009"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVQUERY-001"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVQUERY-002"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVQUERY-003"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVQUERY-004"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-001"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-002"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-003"));
  assert(has_diagnostic(
      rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-004"));

  const auto mixed_parent = fsim::frontend::parse_text(
      "mixed-container-port.vhd",
      R"(
entity mixed_port_top is
end entity;

architecture rtl of mixed_port_top is
  signal source : std_logic_vector(7 downto 0);
  signal result : std_logic_vector(7 downto 0);
  signal shared : std_logic_vector(3 downto 0);
  component static_port_leaf is
    port (
      source : in std_logic_vector(7 downto 0);
      result : out std_logic_vector(7 downto 0);
      shared : inout std_logic_vector(3 downto 0));
  end component;
begin
  child: static_port_leaf
    port map (
      source => source,
      result => result,
      shared => shared);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(mixed_parent.ok());
  auto mixed_design = port_parsed.design;
  mixed_design.units.insert(
      mixed_design.units.end(),
      mixed_parent.design.units.begin(),
      mixed_parent.design.units.end());
  const std::vector<fsim::elaboration::Binding> bindings{
      {"mixed_port_top.child",
       "sv:work.static_port_leaf",
       std::nullopt}};
  const auto mixed_rejected = fsim::elaboration::elaborate(
      mixed_design,
      "vhdl:work.mixed_port_top(rtl)",
      bindings);
  assert(!mixed_rejected.ok());
  assert(has_diagnostic(
      mixed_rejected, "FSIM-ELAB-SVPORT-004"));

  const auto mixed_dynamic_parent =
      fsim::frontend::parse_text(
          "mixed-dynamic-container-port.vhd",
          R"(
entity mixed_dynamic_port_top is
end entity;

architecture rtl of mixed_dynamic_port_top is
  signal source : integer;
  signal result : std_logic_vector(7 downto 0);
  signal bounded : bit;
  signal scores : std_logic_vector(15 downto 0);
  signal work : integer;
  component dynamic_port_leaf is
    port (
      source : in integer;
      result : out std_logic_vector(7 downto 0);
      bounded : inout bit;
      scores : inout std_logic_vector(15 downto 0);
      work : inout integer);
  end component;
begin
  child: dynamic_port_leaf
    port map (
      source => source,
      result => result,
      bounded => bounded,
      scores => scores,
      work => work);
end architecture;
)",
          fsim::frontend::Language::Vhdl2008);
  assert(mixed_dynamic_parent.ok());
  auto mixed_dynamic_design = dynamic_port_parsed.design;
  mixed_dynamic_design.units.insert(
      mixed_dynamic_design.units.end(),
      mixed_dynamic_parent.design.units.begin(),
      mixed_dynamic_parent.design.units.end());
  const std::vector<fsim::elaboration::Binding>
      dynamic_bindings{
          {"mixed_dynamic_port_top.child",
           "sv:work.dynamic_port_leaf",
           std::nullopt}};
  const auto mixed_dynamic_rejected =
      fsim::elaboration::elaborate(
          mixed_dynamic_design,
          "vhdl:work.mixed_dynamic_port_top(rtl)",
          dynamic_bindings);
  assert(!mixed_dynamic_rejected.ok());
  assert(has_diagnostic(
      mixed_dynamic_rejected, "FSIM-ELAB-SVPORT-004"));
}

}  // namespace fsim::tests::elaboration
