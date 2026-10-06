// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace fsim::tests::elaboration {

void test_mixed_container_port_rejections(
    const fsim::frontend::ParsedDesign& port_design,
    const fsim::frontend::ParsedDesign& dynamic_port_design);
void test_systemverilog_static_generated_container_reads();
void test_systemverilog_static_typed_container_reads();

void test_systemverilog_container_lowering()
{
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
  logic [7:0] shadowed[0:2];

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
    copy = '{
        STATIC_LEFT: source[STATIC_LEFT],
        default: 8'h55,
        1: source[1]};
    assert (copy[2] == 8'h55);
    copy[2] = 8'h99;
    return copy[3];
  endfunction

  task automatic mutate_static(
      inout logic [7:0] target[STATIC_LEFT:1]);
    target[2] = 8'h22;
    #1;
    target = '{
        default: 8'h44,
        STATIC_LEFT: 8'h33,
        1: 8'h11};
  endtask

  task automatic fill_shadowed(
      output logic [7:0] shadowed[0:2]);
    shadowed[0] = 8'h12;
    shadowed[1] = 8'h34;
    shadowed[2] = 8'h56;
  endtask

  initial begin
    key_t key;
    int located[$];
    int locations[$];
    int preserved[];
    logic [7:0] fresh[];
    fill_shadowed(shadowed);
    assert (shadowed[0] == 8'h12);
    assert (shadowed[1] == 8'h34);
    assert (shadowed[2] == 8'h56);
    preserved = '{4, 5};
    preserved = new[4](preserved);
    assert (preserved.size() == 4);
    assert (preserved[0] == 4);
    assert (preserved[1] == 5);
    assert (preserved[2] == 0);
    fresh = new[2];
    assert ($isunknown(fresh[1]));
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
    pending.insert(1, 9);
    assert (pending[1] == 9);
    pending.delete(0);
    assert (pending[0] == 9);
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
    fixed_down = '{
        STATIC_LEFT: 8'h31,
        default: 8'b10xz0011,
        1: 8'h11};
    assert (fixed_down[STATIC_LEFT] == 8'h31);
    assert ($isunknown(fixed_down[2]));
    assert (fixed_down[1] == 8'h11);
    fixed_up = '{default: 4'h7};
    assert (fixed_up[-1] == 4'h7);
    assert (fixed_up[0] == 4'h7);
    assert (fixed_up[1] == 4'h7);
    fixed_up = '{
        32'hffffffff: 4'ha,
        default: 4'hb,
        1: 4'hc};
    assert (fixed_up[-1] == 4'ha);
    assert (fixed_up[0] == 4'hb);
    assert (fixed_up[1] == 4'hc);
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
    assert (fixed_down[2] == 8'h44);
    assert (fixed_down[1] == 8'h11);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "container_lowering");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design->container_objects().size() == 6);
    const auto& process = elaborated.design->processes().front();
    assert(process.container_register_count != 0);
    assert(!process.debug_container_locals.empty());
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            const auto* resize = fsim::runtime::simir::operation_get_if<ResizeContainer>(&operation);
            return resize != nullptr && resize->initializer.has_value();
        }));
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            const auto* push = fsim::runtime::simir::operation_get_if<PushContainer>(&operation);
            return push != nullptr && push->index.has_value();
        }));
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<ContainerExists>(operation);
        }));
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<TraverseContainer>(operation);
        }));
    assert(
        std::ranges::count_if(
            process.operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    ContainerReduction>(operation);
            })
        >= 12);
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            const auto* reduction = fsim::runtime::simir::operation_get_if<ContainerReduction>(&operation);
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
                return fsim::runtime::simir::operation_holds<
                    OrderContainer>(operation);
            })
        >= 16);
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            const auto* ordering = fsim::runtime::simir::operation_get_if<OrderContainer>(&operation);
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
                return fsim::runtime::simir::operation_holds<
                    LocateContainer>(operation);
            })
        >= 10);
    assert(
        std::ranges::count_if(
            process.operations,
            [](const auto& operation) {
                const auto* locator = fsim::runtime::simir::operation_get_if<LocateContainer>(&operation);
                return locator != nullptr
                    && !locator->predicate.empty();
            })
        >= 7);
    assert(std::ranges::any_of(
        process.operations,
        [](const auto& operation) {
            const auto* locator = fsim::runtime::simir::operation_get_if<LocateContainer>(&operation);
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
                        const bool result_node = node.operation
                            >= ContainerPredicateOperator::equal;
                        return !result_node
                            || node.value_kind
                            == ContainerPredicateValueKind::logical;
                    });
        }));
    std::vector<const LocateContainer*> predicate_locators;
    for (const auto& operation : process.operations) {
        if (const auto* locator = fsim::runtime::simir::operation_get_if<LocateContainer>(&operation);
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
        if (const auto* reduction = fsim::runtime::simir::operation_get_if<ContainerReduction>(&operation);
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
        if (const auto* locator = fsim::runtime::simir::operation_get_if<LocateContainer>(&operation);
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
        if (const auto* ordering = fsim::runtime::simir::operation_get_if<OrderContainer>(&operation);
            ordering && !ordering->key.empty()) {
            keyed_orderings.push_back(ordering);
        }
    }
    assert(keyed_orderings.size() >= 3);
    assert(same_predicate(
        keyed_orderings[0]->key,
        keyed_orderings[1]->key));
    const auto values = elaborated.design->container_objects()[0].id;
    const auto pending = elaborated.design->container_objects()[1].id;
    const auto lookup = elaborated.design->container_objects()[2].id;
    const auto fixed_down = elaborated.design->container_objects()[3].id;
    const auto fixed_up = elaborated.design->container_objects()[4].id;
    auto interpreter = elaborated.design->create_interpreter();
    const auto result = interpreter->run();
    assert(
        result.status == fsim::runtime::RunStatus::completed
        && result.time == 3);
    const auto& values_result = interpreter->container_object_value(values);
    const auto& pending_result = interpreter->container_object_value(pending);
    const auto& lookup_result = interpreter->container_object_value(lookup);
    const auto& fixed_down_result = interpreter->container_object_value(fixed_down);
    const auto& fixed_up_result = interpreter->container_object_value(fixed_up);
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
        && fixed_down_result.elements[1].low_word().aval == 0x44
        && fixed_down_result.elements[2].low_word().aval == 0x11
        && fixed_up_result.type.fixed
        && fixed_up_result.type.index_left == -1
        && fixed_up_result.type.index_right == 1
        && fixed_up_result.elements[0].low_word().aval == 0xa
        && fixed_up_result.elements[1].low_word().aval == 0
        && fixed_up_result.elements[2].low_word().aval == 0xc);

    const auto atomic_pattern_parsed = fsim::frontend::parse_text(
        "static-pattern-atomic.sv",
        R"(
module static_pattern_atomic;
  logic [7:0] target[2:0];
  logic [7:0] selected;
  initial begin
    selected = 8'h33;
    target = '{2: selected, default: 8'h22, 0: 8'h11};
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(atomic_pattern_parsed.ok());
    const auto atomic_pattern_elaborated = compile_and_elaborate(
        atomic_pattern_parsed.design,
        "static_pattern_atomic");
    assert(atomic_pattern_elaborated.ok());
    const auto& atomic_process = atomic_pattern_elaborated.design->processes().front();
    const CopyContainerRegister* atomic_copy { };
    std::size_t atomic_copy_position { };
    std::size_t atomic_object_write_position { };
    std::vector<std::size_t> atomic_element_writes;
    for (std::size_t position = 0;
        position < atomic_process.operations.size();
        ++position) {
        const auto& operation = atomic_process.operations[position];
        if (const auto* copy = fsim::runtime::simir::operation_get_if<CopyContainerRegister>(&operation)) {
            assert(atomic_copy == nullptr);
            atomic_copy = copy;
            atomic_copy_position = position;
        } else if (
            fsim::runtime::simir::operation_holds<ContainerWrite>(operation)) {
            atomic_element_writes.push_back(position);
        } else if (
            fsim::runtime::simir::operation_holds<WriteContainerObject>(operation)) {
            atomic_object_write_position = position;
        }
    }
    assert(
        atomic_copy != nullptr
        && atomic_element_writes.size() == 3
        && std::ranges::all_of(
            atomic_element_writes,
            [&](const auto position) {
                const auto& write = fsim::runtime::simir::operation_get<ContainerWrite>(
                    atomic_process.operations[position]);
                return write.target == atomic_copy->source
                    && position < atomic_copy_position;
            })
        && atomic_copy->destination != atomic_copy->source
        && atomic_object_write_position > atomic_copy_position
        && atomic_copy->source
            < atomic_process.container_register_types.size());
    const auto& atomic_type = atomic_process
                                  .container_register_types[atomic_copy->source];
    assert(
        atomic_type.fixed
        && atomic_type.element_width == 8
        && !atomic_type.two_state
        && atomic_type.index_left == 2
        && atomic_type.index_right == 0);

    const auto slice_parsed = fsim::frontend::parse_text(
        "static-slices.sv",
        R"(
module static_slices;
  logic [7:0] down[5:0];
  logic [7:0] pair[1:0];
  logic [7:0] up[-2:3];
  bit signed [7:0] signed_down[1:0];
  bit signed [7:0] signed_up[-1:0];
  initial begin
    logic [7:0] located[$];
    int locations[$];
    down = '{8'h55, 8'h44, 8'h33, 8'h22, 8'h11, 8'h00};
    assert ($left(down[4:2]) == 4);
    assert ($right(down[4:2]) == 2);
    assert ($low(down[4:2]) == 2);
    assert ($high(down[4:2]) == 4);
    assert ($increment(down[4:2]) == 1);
    assert ($size(down[4:2], 1) == 3);
    assert ($bits(down[4:2]) == 24);
    assert ($dimensions(down[4:2]) == 2);
    assert ($unpacked_dimensions(down[4:2]) == 1);
    assert (down[4:2].size() == 3);
    assert (down[4:2].sum() == 8'h99);
    assert (down[4:2].product() == 8'h98);
    assert (down[4:2].and() == 8'h00);
    assert (down[4:2].or() == 8'h77);
    assert (down[4:2].xor() == 8'h55);
    assert ($left(down[2 +: 3]) == 4);
    assert ($right(down[2 +: 3]) == 2);
    assert ($size(down[2 +: 3]) == 3);
    assert ($bits(down[2 +: 3]) == 24);
    assert (down[2 +: 3].sum() == 8'h99);
    assert (down[4 -: 3].sum() == 8'h99);
    assert (
        down[4:2].sum(entry) with (
            entry.index == 3 ? entry : 8'h00)
        == 8'h33);
    located = down[4:2].min();
    assert (located.size() == 1);
    assert (located[0] == 8'h22);
    located = down[4:2].max();
    assert (located[0] == 8'h44);
    located = down[4:2].unique();
    assert (located.size() == 3);
    locations = down[4:2].unique_index();
    assert (locations.size() == 3);
    assert (locations[0] == 4);
    assert (locations[2] == 2);
    located = down[4:2].find() with (item >= 8'h33);
    assert (located.size() == 2);
    locations =
        down[4:2].find_index(entry) with (
            entry.index == 2 && entry == 8'h22);
    assert (locations.size() == 1);
    assert (locations[0] == 2);
    located =
        down[4:2].find_first() with (item < 8'h44);
    assert (located[0] == 8'h33);
    locations =
        down[4:2].find_first_index() with (
            item < 8'h44);
    assert (locations[0] == 3);
    located =
        down[4:2].find_last() with (item >= 8'h33);
    assert (located[0] == 8'h33);
    locations =
        down[4:2].find_last_index() with (
            item >= 8'h33);
    assert (locations[0] == 3);
    locations =
        down[2 +: 3].find_index() with (
            item.index == 3);
    assert (locations.size() == 1);
    assert (locations[0] == 3);
    pair = down[3 -: 2];
    assert (pair[1] == 8'h33);
    assert (pair[0] == 8'h22);
    down[1 +: 2] = pair;
    assert (down[2] == 8'h33);
    assert (down[1] == 8'h22);
    down = '{8'h55, 8'h44, 8'h33, 8'h22, 8'h11, 8'h00};
    pair = down[4:3];
    assert (pair[1] == 8'h44);
    assert (pair[0] == 8'h33);
    down[2:1] = pair;
    assert (down[2] == 8'h44);
    assert (down[1] == 8'h33);
    down = '{
        8'h55, 8'h44, 8'b10xz0011,
        8'h22, 8'h11, 8'h00};
    located =
        down[4:2].find() with (item == 8'h33);
    assert (located.size() == 0);
    down[4:2] = down[3:1];
    assert ($isunknown(down[4]));
    assert ($isunknown(down[4:2].sum()));
    assert (down[3] == 8'h22);
    assert (down[2] == 8'h11);
    up = '{default: 8'h00};
    up[-1:1] = down[4:2];
    assert ($isunknown(up[-1]));
    assert (up[0] == 8'h22);
    assert (up[1] == 8'h11);
    signed_down = '{8'hfe, 8'h01};
    signed_up = signed_down[1:0];
    assert (signed_up[-1] == 8'hfe);
    assert (signed_up[0] == 8'h01);
    signed_down[1:0] = signed_up;
    assert (signed_down[1] == 8'hfe);
    assert (signed_down[0] == 8'h01);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(slice_parsed.ok());
    const auto slice_elaborated = compile_and_elaborate(
        slice_parsed.design, "static_slices");
    if (!slice_elaborated.ok()) {
        for (const auto& diagnostic :
            slice_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(slice_elaborated.ok());
    assert(
        slice_elaborated.design->container_objects().size()
        == 5);
    const auto& slice_process = slice_elaborated.design->processes().front();
    const auto has_selected_type =
        [&](const std::int32_t left,
            const std::int32_t right) {
            return std::ranges::any_of(
                slice_process.container_register_types,
                [&](const auto& type) {
                    return type.fixed
                        && type.element_width == 8
                        && !type.two_state
                        && !type.signed_elements
                        && type.index_left == left
                        && type.index_right == right;
                });
        };
    assert(
        has_selected_type(4, 3)
        && has_selected_type(2, 1)
        && has_selected_type(4, 2)
        && has_selected_type(3, 1)
        && has_selected_type(-1, 1));
    assert(std::ranges::any_of(
        slice_process.container_register_types,
        [](const auto& type) {
            return type.fixed
                && type.element_width == 8
                && type.two_state
                && type.signed_elements
                && type.index_left == 1
                && type.index_right == 0;
        }));
    assert(
        std::ranges::count_if(
            slice_process.operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    ContainerRead>(operation);
            })
        >= 13);
    assert(
        std::ranges::count_if(
            slice_process.operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    ContainerWrite>(operation);
            })
        >= 13);
    auto slice_interpreter = slice_elaborated.design->create_interpreter();
    const auto slice_result = slice_interpreter->run();
    assert(
        slice_result.status
            == fsim::runtime::RunStatus::completed
        && slice_result.time == 0);
    const auto& down_result = slice_interpreter->container_object_value(
        slice_elaborated.design->container_objects()[0].id);
    const auto& pair_result = slice_interpreter->container_object_value(
        slice_elaborated.design->container_objects()[1].id);
    const auto& up_result = slice_interpreter->container_object_value(
        slice_elaborated.design->container_objects()[2].id);
    assert(
        down_result.elements.size() == 6
        && down_result.elements[0].low_word().aval == 0x55
        && down_result.elements[1].low_word().aval == 0xa3
        && down_result.elements[1].low_word().bval == 0x30
        && down_result.elements[2].low_word().aval == 0x22
        && down_result.elements[3].low_word().aval == 0x11
        && down_result.elements[4].low_word().aval == 0x11
        && down_result.elements[5].low_word().aval == 0x00
        && pair_result.elements.size() == 2
        && pair_result.elements[0].low_word().aval == 0x44
        && pair_result.elements[1].low_word().aval == 0x33
        && up_result.type.index_left == -2
        && up_result.type.index_right == 3
        && up_result.elements.size() == 6
        && up_result.elements[1].low_word().aval == 0xa3
        && up_result.elements[1].low_word().bval == 0x30
        && up_result.elements[2].low_word().aval == 0x22
        && up_result.elements[3].low_word().aval == 0x11);

    const auto atomic_slice_parsed = fsim::frontend::parse_text(
        "static-slice-atomic.sv",
        R"(
module static_slice_atomic;
  logic [7:0] target[3:0];
  initial target[1 +: 3] = target[2 -: 3];
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(atomic_slice_parsed.ok());
    const auto atomic_slice_elaborated = compile_and_elaborate(
        atomic_slice_parsed.design,
        "static_slice_atomic");
    assert(atomic_slice_elaborated.ok());
    const auto& atomic_slice_process = atomic_slice_elaborated.design->processes().front();
    std::vector<std::size_t> atomic_slice_reads;
    std::vector<std::size_t> atomic_slice_writes;
    std::vector<std::pair<
        std::size_t, const CopyContainerRegister*>>
        atomic_slice_copies;
    std::size_t atomic_slice_object_write { };
    for (std::size_t position = 0;
        position
        < atomic_slice_process.operations.size();
        ++position) {
        const auto& operation = atomic_slice_process.operations[position];
        if (fsim::runtime::simir::operation_holds<ContainerRead>(operation)) {
            atomic_slice_reads.push_back(position);
        } else if (
            fsim::runtime::simir::operation_holds<ContainerWrite>(operation)) {
            atomic_slice_writes.push_back(position);
        } else if (
            const auto* copy = fsim::runtime::simir::operation_get_if<CopyContainerRegister>(&operation)) {
            atomic_slice_copies.emplace_back(position, copy);
        } else if (
            fsim::runtime::simir::operation_holds<WriteContainerObject>(
                operation)) {
            atomic_slice_object_write = position;
        }
    }
    assert(
        atomic_slice_reads.size() == 9
        && atomic_slice_writes.size() == 9
        && atomic_slice_copies.size() == 2);
    const auto [replacement_position, replacement_copy] = atomic_slice_copies[0];
    const auto [commit_position, commit_copy] = atomic_slice_copies[1];
    assert(
        replacement_copy->destination
            == commit_copy->source
        && replacement_copy->source
            == commit_copy->destination
        && std::ranges::all_of(
            atomic_slice_reads,
            [&](const auto position) {
                return position < replacement_position
                    || (position > replacement_position
                        && position < commit_position);
            })
        && std::ranges::all_of(
            atomic_slice_writes,
            [&](const auto position) {
                return position < commit_position;
            })
        && std::ranges::none_of(
            atomic_slice_process.operations,
            [&](const auto& operation) {
                const auto* write = fsim::runtime::simir::operation_get_if<ContainerWrite>(&operation);
                return write != nullptr
                    && write->target
                    == commit_copy->destination;
            })
        && replacement_position < commit_position
        && commit_position < atomic_slice_object_write);

    const auto memory_word_output_parsed = fsim::frontend::parse_text(
        "memory-word-output.sv",
        R"(
module memory_word_source(output wire [7:0] value);
  assign value = 8'h5a;
endmodule
module memory_word_sink(
    input wire [7:0] value,
    output wire [7:0] observed);
  assign observed = value;
endmodule
module memory_word_output;
  wire [7:0] words[0:1];
  wire [7:0] observed;
  memory_word_source source(.value(words[1]));
  memory_word_sink sink(.value(words[1]), .observed(observed));
  initial begin
    #1;
    assert (words[1] == 8'h5a);
    assert (observed == 8'h5a);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(memory_word_output_parsed.ok());
    const auto memory_word_output_elaborated = compile_and_elaborate(
        memory_word_output_parsed.design,
        "memory_word_output");
    assert(memory_word_output_elaborated.ok());
    const auto memory_word_container
        = memory_word_output_elaborated.design->find_container(
            "memory_word_output.words");
    assert(memory_word_container);
    const auto memory_word_state
        = memory_word_output_elaborated.design->state();
    const auto memory_word_aliases
        = memory_word_output_elaborated.design->container_element_signal_aliases();
    assert(std::ranges::count_if(
        memory_word_aliases, [&](const auto& alias) {
            return alias.object == *memory_word_container;
        }) == 2);
    assert(std::ranges::count_if(
        memory_word_state.container_signal_aliases,
        [&](const auto& alias) {
            return alias.object == *memory_word_container;
        }) == 0);
    const auto memory_word_aggregate
        = memory_word_output_elaborated.design->find_signal(
            "memory_word_output.words");
    assert(memory_word_aggregate);
    assert(std::ranges::count_if(
        memory_word_output_elaborated.design
            ->container_aggregate_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *memory_word_container
                && alias.signal == *memory_word_aggregate;
        }) == 1);
    const auto memory_word_zero
        = memory_word_output_elaborated.design->find_signal(
            "memory_word_output.words[0]");
    const auto memory_word_one
        = memory_word_output_elaborated.design->find_signal(
            "memory_word_output.words[1]");
    assert(memory_word_zero && memory_word_one);
    assert(std::ranges::any_of(
        memory_word_output_elaborated.design->processes(),
        [&](const Process& process) {
            return std::ranges::any_of(
                process.operations, [&](const Operation& operation) {
                    const auto* read
                        = operation_get_if<ReadSignal>(&operation);
                    return read != nullptr
                        && read->signal == *memory_word_one
                        && read->kind == SignalReadKind::current;
                });
        }));
    assert(std::ranges::any_of(
        memory_word_output_elaborated.design->processes(),
        [&](const Process& process) {
            const bool reads_leaf = std::ranges::any_of(
                process.operations, [&](const Operation& operation) {
                    const auto* read
                        = operation_get_if<ReadSignal>(&operation);
                    return read != nullptr
                        && read->signal == *memory_word_one;
                });
            const bool waits_on_leaf = std::ranges::any_of(
                process.static_sensitivity,
                [&](const Sensitivity& sensitivity) {
                    return sensitivity.signal == *memory_word_one
                        && sensitivity.offset == 0U
                        && sensitivity.width == 8U;
                });
            return reads_leaf && waits_on_leaf;
        }));
    assert(std::ranges::any_of(
        memory_word_output_elaborated.design->processes(),
        [&](const Process& process) {
            const bool writes_leaf_directly = std::ranges::any_of(
                process.operations,
                [&](const Operation& operation) {
                    if (const auto* write
                        = operation_get_if<WriteBlocking>(&operation)) {
                        return write->signal == *memory_word_one;
                    }
                    if (const auto* write
                        = operation_get_if<WriteUpdate>(&operation)) {
                        return write->signal == *memory_word_one;
                    }
                    return false;
                });
            const bool owns_leaf_region = std::ranges::any_of(
                process.driver_regions,
                [&](const Process::DriverRegion& region) {
                    return region.signal == *memory_word_one
                        && region.whole;
                });
            const bool retains_object_element_write
                = std::ranges::any_of(
                    process.operations,
                    [&](const Operation& operation) {
                        const auto* write
                            = operation_get_if<WriteContainerObjectElement>(
                                &operation);
                        return write != nullptr
                            && write->object == *memory_word_container;
                    });
            return writes_leaf_directly
                && owns_leaf_region
                && !retains_object_element_write;
        }));
    auto memory_word_output_interpreter = memory_word_output_elaborated.design->create_interpreter();
    const auto memory_word_output_result = memory_word_output_interpreter->run();
    assert((
        memory_word_output_result.status
            == fsim::runtime::RunStatus::completed
        && memory_word_output_result.time == 1
        && memory_word_output_interpreter->signal_value(
               *memory_word_aggregate)
            == fsim::runtime::PackedLogic4::from_msb_string(
                "ZZZZZZZZ01011010")
        && memory_word_output_interpreter->container_object_value(
               *memory_word_container).elements
            == std::vector<fsim::runtime::PackedLogic4> {
                fsim::runtime::PackedLogic4::from_msb_string("ZZZZZZZZ"),
                fsim::runtime::PackedLogic4::from_msb_string("01011010")
            }));

    const auto dynamic_net_array_write_parsed = fsim::frontend::parse_text(
        "physical-net-array-dynamic-write.sv",
        R"(
module physical_net_array_dynamic_word_source(
    input wire [7:0] value,
    output wire [7:0] driven);
  assign driven = value;
endmodule
module physical_net_array_dynamic_write(
    input wire select,
    input wire [7:0] value);
  wire [7:0] words[0:1];
  physical_net_array_dynamic_word_source source(
      .value(value), .driven(words[select]));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_net_array_write_parsed.ok());
    const auto dynamic_net_array_write_elaborated = compile_and_elaborate(
        dynamic_net_array_write_parsed.design,
        "physical_net_array_dynamic_write");
    assert(dynamic_net_array_write_elaborated.ok());
    const auto dynamic_net_array_write_object
        = dynamic_net_array_write_elaborated.design->find_container("words");
    assert(dynamic_net_array_write_object);
    assert(std::ranges::count_if(
        dynamic_net_array_write_elaborated.design
            ->container_element_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *dynamic_net_array_write_object;
        }) == 2);
    assert(std::ranges::any_of(
        dynamic_net_array_write_elaborated.design->processes(),
        [&](const Process& process) {
            for (std::size_t index = 1U;
                index < process.operations.size();
                ++index) {
                const auto* write
                    = operation_get_if<WriteContainerObjectElement>(
                        &process.operations[index]);
                if (write == nullptr
                    || write->object != *dynamic_net_array_write_object
                    || write->linear_index
                    || write->dynamic_part
                    || write->transaction_signal) {
                    continue;
                }
                const auto* previous
                    = operation_get_if<LoadConstant>(
                        &process.operations[index - 1U]);
                if (previous == nullptr
                    || previous->destination != write->index) {
                    return true;
                }
            }
            return false;
        }));

    const auto linear_net_array_write_parsed = fsim::frontend::parse_text(
        "physical-net-array-linear-write.sv",
        R"(
module physical_net_array_linear_word_source(
    output wire [7:0] driven);
  assign driven = 8'h5a;
endmodule
module physical_net_array_linear_write;
  wire [7:0] words[0:1][0:1];
  physical_net_array_linear_word_source source(
      .driven(words[0][1]));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(linear_net_array_write_parsed.ok());
    const auto linear_net_array_write_elaborated = compile_and_elaborate(
        linear_net_array_write_parsed.design,
        "physical_net_array_linear_write");
    assert(linear_net_array_write_elaborated.ok());
    const auto linear_net_array_write_object
        = linear_net_array_write_elaborated.design->find_container("words");
    assert(linear_net_array_write_object);
    assert(std::ranges::count_if(
        linear_net_array_write_elaborated.design
            ->container_element_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *linear_net_array_write_object;
        }) == 4U);
    const auto linear_proxy = linear_net_array_write_elaborated.design
        ->find_signal("physical_net_array_linear_write.words");
    const auto linear_leaf = linear_net_array_write_elaborated.design
        ->find_signal("physical_net_array_linear_write.words[0][1]");
    assert(linear_proxy && linear_leaf);
    auto linear_runtime = linear_net_array_write_elaborated.design
        ->create_interpreter();
    assert(linear_runtime->run().status == fsim::runtime::RunStatus::completed);
    assert(linear_runtime->signal_value(*linear_leaf).to_msb_string()
        == "01011010");
    assert(linear_runtime->signal_value(*linear_proxy).to_msb_string()
        == "ZZZZZZZZ01011010ZZZZZZZZZZZZZZZZ");
    assert(linear_runtime->container_object_value(*linear_net_array_write_object)
        .elements.at(1U).to_msb_string() == "01011010");
    assert(std::ranges::any_of(
        linear_net_array_write_elaborated.design->processes(),
        [&](const Process& process) {
            return std::ranges::any_of(
                process.operations,
                [&](const Operation& operation) {
                    const auto* write
                        = operation_get_if<WriteContainerObjectElement>(
                            &operation);
                    return write != nullptr
                        && write->object == *linear_net_array_write_object
                        && write->linear_index;
                });
        }));

    const auto three_dimensional_parsed = fsim::frontend::parse_text(
        "physical-net-array-three-dimensional.sv",
        R"(
module physical_net_array_three_dimensional;
  localparam int ROW = 0;
  localparam int COLUMN = -1;
  wire flags [1:0][-2:-1][4:3];
  wire [64:0] wide [0:1][-1:0];
  wire flags_copy;
  wire [64:0] wide_copy;
  logic [64:0] backing [1:0][-1:0];
  wire [64:0] backing_copy;
  wire [64:0] generated_copy [0:1];
  assign flags_copy = flags[0][-1][3];
  assign wide_copy = wide[ROW][COLUMN];
  assign backing_copy = backing[1][-1];
  for (genvar g = 0; g < 2; g = g + 1) begin : reads
    assign generated_copy[g] = wide[g][-1];
  end
  assign flags[0][-1][3] = 1'b1;
  assign flags[1][-2][4] = 1'b0;
  assign flags[1][-2][4] = 1'b1;
  assign wide[0][-1][64:1] = 64'hffffffffffffffff;
  assign wide[0][-1][0] = 1'b1;
  assign wide[1][-1] = 65'h10000000000000000;
  initial begin
    backing[1][-1] = 65'h10000000000000001;
    #1;
    assert (flags_copy === 1'b1);
    assert (wide_copy === 65'h1ffffffffffffffff);
    assert (backing_copy === 65'h10000000000000001);
    assert (generated_copy[0] === 65'h1ffffffffffffffff);
    assert (generated_copy[1] === 65'h10000000000000000);
    assert (flags[0][-1][3] === 1'b1);
    assert (flags[1][-2][4] === 1'bx);
    assert (wide[0][-1] === 65'h1ffffffffffffffff);
    assert (wide[1][-1] === 65'h10000000000000000);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(three_dimensional_parsed.ok());
    const auto three_dimensional = compile_and_elaborate(
        three_dimensional_parsed.design,
        "physical_net_array_three_dimensional");
    assert(three_dimensional.ok());
    const auto flags_object = three_dimensional.design->find_container("flags");
    const auto wide_object = three_dimensional.design->find_container("wide");
    assert(flags_object && wide_object);
    const auto& dimensions = three_dimensional.design
        ->container_objects().at(*flags_object).type.dimensions;
    assert((dimensions == std::vector<std::pair<std::int32_t, std::int32_t>> {
        { 1, 0 }, { -2, -1 }, { 4, 3 }
    }));
    assert(std::ranges::count_if(
        three_dimensional.design->container_element_signal_aliases(),
        [&](const auto& alias) { return alias.object == *flags_object; }) == 8U);
    assert(std::ranges::count_if(
        three_dimensional.design->container_element_signal_aliases(),
        [&](const auto& alias) { return alias.object == *wide_object; }) == 4U);
    const std::vector<std::string> flag_suffixes {
        "[1][-2][4]", "[1][-2][3]", "[1][-1][4]", "[1][-1][3]",
        "[0][-2][4]", "[0][-2][3]", "[0][-1][4]", "[0][-1][3]"
    };
    for (std::size_t ordinal = 0U; ordinal < flag_suffixes.size(); ++ordinal) {
        const auto leaf = three_dimensional.design->find_signal(
            "physical_net_array_three_dimensional.flags" + flag_suffixes[ordinal]);
        assert(leaf);
        assert(std::ranges::any_of(
            three_dimensional.design->container_element_signal_aliases(),
            [&](const auto& alias) {
                return alias.object == *flags_object
                    && alias.ordinal == ordinal && alias.signal == *leaf;
            }));
    }
    const auto find_copy_writer = [&](const std::string_view name) -> const Process& {
        const auto output = three_dimensional.design->find_signal(name);
        assert(output);
        const auto writer = std::ranges::find_if(three_dimensional.design->processes(),
            [&](const Process& process) {
                return std::ranges::any_of(process.driver_regions,
                    [&](const auto& region) { return region.signal == *output; });
            });
        assert(writer != three_dimensional.design->processes().end());
        assert(std::ranges::none_of(writer->operations,
            [](const Operation& operation) {
                return operation_holds<ReadContainerObject>(operation)
                    || operation_holds<ContainerRead>(operation);
            }));
        return *writer;
    };
    const auto require_leaf_read = [&](const std::string_view output,
                                       const std::string_view source) {
        const auto leaf = three_dimensional.design->find_signal(source);
        assert(leaf);
        const auto& writer = find_copy_writer(output);
        assert(std::ranges::any_of(writer.operations,
            [&](const Operation& operation) {
                const auto* read = operation_get_if<ReadSignal>(&operation);
                return read != nullptr && read->signal == *leaf;
            }));
        assert(std::ranges::any_of(writer.static_sensitivity,
            [&](const Sensitivity& sensitivity) {
                return sensitivity.signal == *leaf && sensitivity.edge == EdgeKind::any;
            }));
    };
    require_leaf_read("flags_copy", "flags[0][-1][3]");
    require_leaf_read("wide_copy", "wide[0][-1]");
    require_leaf_read("generated_copy[0]", "wide[0][-1]");
    require_leaf_read("generated_copy[1]", "wide[1][-1]");
    const auto& backing_writer = find_copy_writer("backing_copy");
    const auto backing_signal = three_dimensional.design->find_signal("backing");
    assert(backing_signal);
    assert(std::ranges::any_of(backing_writer.static_sensitivity,
        [&](const Sensitivity& sensitivity) {
            return sensitivity.signal == *backing_signal
                && sensitivity.offset == 195U && sensitivity.width == 65U;
        }));
    auto restored_three_dimensional = fsim::elaboration::ElaboratedDesign::from_state(
        three_dimensional.design->state());
    assert(restored_three_dimensional);
    assert(restored_three_dimensional->container_element_signal_aliases()
        == three_dimensional.design->container_element_signal_aliases());
    auto invalid_name_state = three_dimensional.design->state();
    const auto first_alias = invalid_name_state.container_element_signal_aliases.front();
    invalid_name_state.signal_info[first_alias.signal].name += "[0]";
    invalid_name_state.signals[first_alias.signal].name += "[0]";
    assert(!fsim::elaboration::ElaboratedDesign::from_state(std::move(invalid_name_state)));
    auto three_dimensional_runtime = restored_three_dimensional->create_interpreter();
    assert(three_dimensional_runtime->run().status
        == fsim::runtime::RunStatus::completed);
    const auto& flags = three_dimensional_runtime->container_object_value(*flags_object);
    assert(flags.elements.size() == 8U);
    for (std::size_t ordinal = 0U; ordinal < flags.elements.size(); ++ordinal) {
        assert(flags.elements[ordinal].to_msb_string() == (ordinal == 7U ? "1" : ordinal == 0U ? "X" : "Z"));
    }
    const auto& wide_elements = three_dimensional_runtime
        ->container_object_value(*wide_object).elements;
    assert(wide_elements.size() == 4U);
    assert(wide_elements[0U].to_msb_string() == std::string(65U, '1'));
    assert(wide_elements[2U].width() == 65U);
    assert(wide_elements[2U].to_msb_string() == "1" + std::string(64U, '0'));

    const auto dynamic_net_array_parsed = fsim::frontend::parse_text(
        "physical-net-array-dynamic.sv",
        R"(
module physical_net_array_dynamic;
  wire [7:0] words[0:1];
  integer index;
  logic [7:0] dynamic_value;
  logic whole_changed;
  assign words[0] = 8'h11;
  assign words[1] = 8'h22;
  always_comb dynamic_value = words[index];
  always @(words) whole_changed = ~whole_changed;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_net_array_parsed.ok());
    const auto dynamic_net_array_elaborated = compile_and_elaborate(
        dynamic_net_array_parsed.design,
        "physical_net_array_dynamic");
    assert(dynamic_net_array_elaborated.ok());
    const auto dynamic_net_object
        = dynamic_net_array_elaborated.design->find_container("words");
    const auto dynamic_net_proxy
        = dynamic_net_array_elaborated.design->find_signal("words");
    assert(dynamic_net_object && dynamic_net_proxy);
    assert(std::ranges::count_if(
        dynamic_net_array_elaborated.design->container_element_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *dynamic_net_object;
        }) == 2);
    assert(std::ranges::any_of(
        dynamic_net_array_elaborated.design->processes(),
        [&](const Process& process) {
            const bool dynamic_read = std::ranges::any_of(
                process.operations, [&](const Operation& operation) {
                    const auto* read
                        = operation_get_if<ReadContainerObject>(&operation);
                    if (read == nullptr || read->object != *dynamic_net_object) {
                        return false;
                    }
                    return std::ranges::any_of(
                        process.operations, [&](const Operation& candidate) {
                            const auto* element_read
                                = operation_get_if<ContainerRead>(&candidate);
                            return element_read != nullptr
                                && element_read->source == read->destination;
                        });
                });
            const bool public_proxy_dependency = std::ranges::any_of(
                process.static_sensitivity,
                [&](const Sensitivity& sensitivity) {
                    return sensitivity.signal == *dynamic_net_proxy
                        && sensitivity.width == 0U;
                });
            return dynamic_read && public_proxy_dependency;
        }));

    const auto specify_unrelated_parsed = fsim::frontend::parse_text(
        "physical-net-array-unrelated-specify.sv",
        R"(
module physical_net_array_unrelated_specify(input wire a, output wire z);
  wire [7:0] words[0:1];
  assign words[0] = {8{a}};
  assign words[1] = 8'h00;
  assign z = words[0][0];
  specify (a => z) = 1; endspecify
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(specify_unrelated_parsed.ok());
    const auto specify_unrelated_elaborated = compile_and_elaborate(
        specify_unrelated_parsed.design,
        "physical_net_array_unrelated_specify");
    assert(specify_unrelated_elaborated.ok());
    const auto specify_unrelated_object
        = specify_unrelated_elaborated.design->find_container("words");
    const auto specify_unrelated_proxy
        = specify_unrelated_elaborated.design->find_signal("words");
    const auto specify_unrelated_source
        = specify_unrelated_elaborated.design->find_signal("a");
    const auto specify_unrelated_destination
        = specify_unrelated_elaborated.design->find_signal("z");
    assert(specify_unrelated_object && specify_unrelated_proxy);
    assert(specify_unrelated_source && specify_unrelated_destination);
    const auto& specify_unrelated_paths
        = specify_unrelated_elaborated.design->verilog_specify_paths();
    assert(specify_unrelated_paths.size() == 1U);
    assert(specify_unrelated_paths.front().identity
        == "sdf:iopath:physical_net_array_unrelated_specify:0");
    assert(specify_unrelated_paths.front().sources.size() == 1U);
    assert(specify_unrelated_paths.front().destinations.size() == 1U);
    assert(specify_unrelated_paths.front().sources.front().signal
        == *specify_unrelated_source);
    assert(specify_unrelated_paths.front().destinations.front().signal
        == *specify_unrelated_destination);
    // This fixture's specify path references a and z, not words. It checks
    // that an unrelated timing path does not disable physical word aliases.
    assert(std::ranges::count_if(
        specify_unrelated_elaborated.design
            ->container_element_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *specify_unrelated_object;
        }) == 2);
    assert(std::ranges::any_of(
        specify_unrelated_elaborated.design
            ->container_aggregate_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *specify_unrelated_object
                && alias.signal == *specify_unrelated_proxy;
        }));

    const auto wand_fallback_parsed = fsim::frontend::parse_text(
        "physical-net-array-wand-fallback.sv",
        R"(
module physical_net_array_wand_fallback;
  wand [7:0] words[0:1];
  assign words[0] = 8'hff;
  assign words[1] = 8'h00;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wand_fallback_parsed.ok());
    const auto wand_fallback_elaborated = compile_and_elaborate(
        wand_fallback_parsed.design,
        "physical_net_array_wand_fallback");
    assert(wand_fallback_elaborated.ok());
    const auto wand_fallback_object
        = wand_fallback_elaborated.design->find_container("words");
    assert(wand_fallback_object);
    assert(std::ranges::none_of(
        wand_fallback_elaborated.design->container_element_signal_aliases(),
        [&](const auto& alias) {
            return alias.object == *wand_fallback_object;
        }));

    // Element writes whose value comes from Binary, ConditionalSelect or
    // Shift results must still target the physical leaf nets rather than
    // the aggregate proxy (cascade shapes such as XOR trees and reductions).
    const auto computed_leaf_parsed = fsim::frontend::parse_text(
        "physical-net-array-computed-writes.sv",
        R"(
module physical_net_array_computed_writes;
  wire [7:0] x = 8'h0f;
  wire [7:0] chain[0:3];
  wire [7:0] picks[0:1];
  wire [7:0] shifted[0:1];
  assign chain[0] = x;
  genvar i;
  generate
    for (i = 1; i < 4; i = i + 1) begin : g
      assign chain[i] = chain[i-1] ^ x;
    end
  endgenerate
  assign picks[0] = x;
  assign picks[1] = x[0] ? ~picks[0] : picks[0];
  assign shifted[0] = x;
  assign shifted[1] = shifted[0] << 2;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(computed_leaf_parsed.ok());
    const auto computed_leaf_elaborated = compile_and_elaborate(
        computed_leaf_parsed.design,
        "physical_net_array_computed_writes");
    assert(computed_leaf_elaborated.ok());
    for (const auto* array_name : { "chain", "picks", "shifted" }) {
        const auto object
            = computed_leaf_elaborated.design->find_container(array_name);
        const auto proxy
            = computed_leaf_elaborated.design->find_signal(array_name);
        assert(object && proxy);
        assert(std::ranges::any_of(
            computed_leaf_elaborated.design->container_element_signal_aliases(),
            [&](const auto& alias) { return alias.object == *object; }));
        for (const auto& computed_process
            : computed_leaf_elaborated.design->processes()) {
            for (const auto& operation : computed_process.operations) {
                if (const auto* slice
                    = operation_get_if<WriteUpdateSlice>(&operation)) {
                    assert(slice->signal != *proxy);
                }
                if (const auto* whole
                    = operation_get_if<WriteUpdate>(&operation)) {
                    assert(whole->signal != *proxy);
                }
            }
        }
    }
    auto computed_leaf_runtime
        = computed_leaf_elaborated.design->create_interpreter();
    assert(computed_leaf_runtime->run().status
        == fsim::runtime::RunStatus::completed);
    const auto element_strings = [&](const char* name) {
        const auto object = computed_leaf_elaborated.design->find_container(name);
        std::vector<std::string> values;
        for (const auto& element
            : computed_leaf_runtime->container_object_value(*object).elements) {
            values.push_back(element.to_msb_string());
        }
        return values;
    };
    assert((element_strings("chain") == std::vector<std::string> {
        "00001111", "00000000", "00001111", "00000000" }));
    assert((element_strings("picks") == std::vector<std::string> {
        "00001111", "11110000" }));
    assert((element_strings("shifted") == std::vector<std::string> {
        "00001111", "00111100" }));

    const auto procedural_memory_parsed = fsim::frontend::parse_text(
        "procedural-memory.sv",
        R"(
module procedural_memory;
  reg clk = 0;
  reg resetn = 0;
  reg [7:0] words[0:3];
  integer index;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    if (!resetn) begin
      for (index = 0; index < 4; index = index + 1)
        words[index] <= 0;
    end else begin
      words[0] <= 8'h11;
      for (index = 1; index < 4; index = index + 1)
        words[index] <= words[index - 1] + 1;
    end
  end
  initial begin
    #6 resetn = 1;
    @(posedge clk);
    #1;
    assert (words[0] == 8'h11);
    assert (words[1] == 8'h01);
    assert (words[2] == 8'h01);
    assert (words[3] == 8'h01);
    $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(procedural_memory_parsed.ok());
    const auto procedural_memory_elaborated = compile_and_elaborate(
        procedural_memory_parsed.design,
        "procedural_memory");
    assert(procedural_memory_elaborated.ok());
    auto procedural_memory_interpreter = procedural_memory_elaborated.design->create_interpreter();
    const auto procedural_memory_result = procedural_memory_interpreter->run();
    assert(
        procedural_memory_result.status
            == fsim::runtime::RunStatus::stopped
        && procedural_memory_result.time == 16);

    const auto memory_dynamic_part_parsed = fsim::frontend::parse_text(
        "memory-dynamic-part.sv",
        R"(
module memory_dynamic_part;
  logic [15:0] words[0:1];
  integer address;
  integer lane;
  logic [7:0] data;
  initial begin
    words[0] = 16'h0000;
    words[1] = 16'h0000;
    address = 0;
    lane = 0;
    data = 8'h12;
    words[address][lane * 8 +: 8] <= data;
    lane = 1;
    data = 8'h34;
    words[address][lane * 8 +: 8] <= data;
    lane = 0;
    data = 8'h56;
    words[address][lane * 8 +: 8] <= data;
    address = 1;
    lane = 0;
    data = 8'bx1010101;
    words[address][lane * 8 +: 8] <= data;
    address = 0;
    lane = 1;
    data = 8'hee;
    #1;
    assert (words[0] == 16'h3456);
    assert (words[1][7:0] === 8'bx1010101);
    $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(memory_dynamic_part_parsed.ok());
    const auto memory_dynamic_part_elaborated = compile_and_elaborate(
        memory_dynamic_part_parsed.design,
        "memory_dynamic_part");
    assert(memory_dynamic_part_elaborated.ok());
    auto memory_dynamic_part_interpreter
        = memory_dynamic_part_elaborated.design->create_interpreter();
    const auto memory_dynamic_part_result
        = memory_dynamic_part_interpreter->run();
    assert(
        memory_dynamic_part_result.status
            == fsim::runtime::RunStatus::stopped
        && memory_dynamic_part_result.time == 1);

    const auto function_memory_parsed = fsim::frontend::parse_text(
        "function-memory.sv",
        R"(
module function_memory #(parameter integer N = 4);
  integer values[0:2*N];
  integer wide_values[0:255];
  integer observed;
  integer guarded_observed;
  integer wide_observed;
  logic [7:0] wide_index;
  function automatic integer lookup;
    input [1:0] index;
    begin
      lookup = values[index];
    end
  endfunction
  function automatic integer guarded_add;
    input integer a;
    input integer b;
    begin
      if (a == 0 || b == 0)
        guarded_add = 0;
      else
        guarded_add = a + b;
    end
  endfunction
  task automatic mirror;
    integer index;
    begin
      for (index = N; index < 2*N; index = index + 1)
        values[index] = values[index - N];
    end
  endtask
  task automatic outer;
    begin
      mirror();
      observed = lookup(0);
      guarded_observed = guarded_add(values[0], values[1]);
    end
  endtask
  initial begin
    values[0] = 8'h41;
    values[1] = 8'h52;
    values[2] = 8'h63;
    values[3] = 8'h74;
    wide_values[198] = 32'd123;
    wide_index = 8'd198;
    outer();
    wide_observed = wide_values[wide_index];
    assert (observed == 8'h41);
    assert (guarded_observed == 8'h93);
    assert (wide_observed == 32'd123);
    assert (values[N] == 8'h41);
    assert (values[2*N-1] == 8'h74);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(function_memory_parsed.ok());
    const auto function_memory_elaborated = compile_and_elaborate(
        function_memory_parsed.design,
        "function_memory");
    assert(function_memory_elaborated.ok());
    auto function_memory_interpreter = function_memory_elaborated.design->create_interpreter();
    const auto function_memory_result = function_memory_interpreter->run();
    assert(
        function_memory_result.status
            == fsim::runtime::RunStatus::completed
        && function_memory_result.time == 0);
    const auto wide_observed = function_memory_elaborated.design->find_signal(
        "wide_observed");
    assert(wide_observed);
    assert(function_memory_interpreter->signal_value(*wide_observed)
               .to_msb_string()
        == "00000000000000000000000001111011");

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
    result = '{
        RIGHT: source[RIGHT] + 8'h02,
        default: 8'h20,
        LEFT: source[LEFT] + 8'h01};
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
    assert (result[2] == 8'h20);
    assert (result[1] == 8'h20);
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
    const auto port_elaborated = compile_and_elaborate(
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
    const auto paths = port_elaborated.design->container_paths();
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
    const auto source_id = port_elaborated.design->find_container(
        "static_port_top.source");
    const auto child_source_id = port_elaborated.design->find_container(
        "static_port_top.mid.generated.child.source");
    assert(source_id && child_source_id);
    assert(*source_id == *child_source_id);
    auto port_interpreter = port_elaborated.design->create_interpreter();
    const auto port_result = port_interpreter->run();
    assert(
        port_result.status
            == fsim::runtime::RunStatus::completed
        && port_result.time == 2);
    const auto result_id = port_elaborated.design->find_container(
        "static_port_top.result");
    const auto shared_id = port_elaborated.design->find_container(
        "static_port_top.shared");
    assert(result_id && shared_id);
    const auto& result_value = port_interpreter->container_object_value(*result_id);
    const auto& shared_value = port_interpreter->container_object_value(*shared_id);
    assert(
        result_value.elements[0].low_word().aval == 0x32
        && result_value.elements[1].low_word().aval == 0x20
        && result_value.elements[2].low_word().aval == 0x20
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
    const auto dynamic_port_elaborated = compile_and_elaborate(
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
            ->container_objects()
            .size()
        == 5);
    const auto dynamic_source = dynamic_port_elaborated.design->find_container(
        "dynamic_port_top.source");
    const auto nested_dynamic_source = dynamic_port_elaborated.design->find_container(
        "dynamic_port_top.mid.generated.child.source");
    const auto dynamic_bounded = dynamic_port_elaborated.design->find_container(
        "dynamic_port_top.bounded");
    assert(
        dynamic_source && nested_dynamic_source
        && *dynamic_source == *nested_dynamic_source
        && dynamic_bounded);
    const auto& bounded_info = dynamic_port_elaborated.design->container_objects().at(
        *dynamic_bounded);
    assert(
        bounded_info.type.queue
        && bounded_info.type.maximum_elements
        && *bounded_info.type.maximum_elements == 4);
    auto dynamic_port_interpreter = dynamic_port_elaborated.design->create_interpreter();
    const auto dynamic_port_result = dynamic_port_interpreter->run();
    assert(
        dynamic_port_result.status
            == fsim::runtime::RunStatus::completed
        && dynamic_port_result.time == 2);
    const auto dynamic_result = dynamic_port_elaborated.design->find_container(
        "dynamic_port_top.result");
    const auto dynamic_scores = dynamic_port_elaborated.design->find_container(
        "dynamic_port_top.scores");
    const auto dynamic_work = dynamic_port_elaborated.design->find_container(
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

    const auto expanded_static = fsim::frontend::parse_text(
        "container-expanded-static.sv",
        R"(
module container_expanded_static;
  byte values[0:4096];
  initial begin
    values[0] = 1;
    values[4096] = 2;
    assert ($size(values) == 4097);
    assert (values[0] == 1);
    assert (values[4096] == 2);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(expanded_static.ok());
    const auto expanded_static_design = compile_and_elaborate(
        expanded_static.design, "container_expanded_static");
    assert(expanded_static_design.ok());
    auto expanded_static_interpreter = expanded_static_design.design->create_interpreter();
    assert(
        expanded_static_interpreter->run().status
        == fsim::runtime::RunStatus::completed);

    const auto malformed_shuffle = fsim::frontend::parse_text(
        "container-malformed-shuffle.sv",
        "module container_malformed_shuffle; byte values[]; "
        "initial values.shuffle(1); endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(!malformed_shuffle.ok());
    assert(std::ranges::any_of(
        malformed_shuffle.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-081";
        }));

    const auto invalid = fsim::frontend::parse_text(
        "container-invalid-lowering.sv",
        R"(
module container_invalid_lowering;
  typedef struct packed {
    logic [3:0] value;
  } pair_t;
  byte composite_key[pair_t];
  byte nonintegral_key[real];
  pair_t composite_element[int];
  logic [64:0] too_wide_elements[];
  byte lookup[int];
  byte dynamic[];
  byte fixed[1:0];
  string strings[1:0];
  byte too_large[0:1000000000];
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
    lookup.shuffle();
    strings.shuffle();
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
    dynamic.insert(0, 1);
    fixed.delete();
    fixed = new[2];
    dynamic = new[2](fixed);
    $readmemh("invalid.hex", dynamic);
    $writememh("invalid-out.hex", dynamic);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid.ok());
    const auto rejected = compile_and_elaborate(
        invalid.design, "container_invalid_lowering");
    assert(!rejected.ok());
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
        rejected, "FSIM-ELAB-SVCONTAINER-023"));
    assert(has_diagnostic(
        rejected, "FSIM-ELAB-SVMEMORY-003"));

    std::string oversized_predicate = "module oversized_predicate; byte values[]; "
                                      "byte result[$]; initial result = values.find() with (";
    for (int value = 0; value < 17; ++value) {
        if (value != 0) {
            oversized_predicate += " || ";
        }
        oversized_predicate += "item == " + std::to_string(value);
    }
    oversized_predicate += "); endmodule";
    const auto oversized_parsed = fsim::frontend::parse_text(
        "container-predicate-oversized.sv",
        oversized_predicate,
        fsim::frontend::Language::SystemVerilog2017);
    assert(oversized_parsed.ok());
    const auto oversized_rejected = compile_and_elaborate(
        oversized_parsed.design, "oversized_predicate");
    assert(
        !oversized_rejected.ok()
        && has_diagnostic(
            oversized_rejected, "FSIM-ELAB-SVFIND-004"));

    const auto invalid_slices = fsim::frontend::parse_text(
        "static-slice-invalid.sv",
        R"(
module static_slice_invalid(
    input logic [7:0] input_fixed[3:0]);
  logic [7:0] down[3:0];
  logic [7:0] pair[1:0];
  logic [3:0] narrow[1:0];
  bit [7:0] bits[1:0];
  logic signed [7:0] signed_values[1:0];
  logic [7:0] dynamic[];
  logic [7:0] queued[$];
  logic [7:0] associative[int];
  int runtime_bound;
  int result;
  logic [7:0] located[$];
  int locations[$];
  initial begin
    down[runtime_bound:0] = down;
    down[32'hxxxxxxxx:0] = down;
    down[2:0] = down[0:2];
    down[4:3] = pair;
    down[2:0] = pair;
    down[1:0] = narrow;
    down[1:0] = bits;
    down[1:0] = signed_values;
    down[1:0] = dynamic;
    down[1:0] = queued;
    down[1:0] = associative;
    down[1 +: 2] = pair;
    pair = down[1 +: 2];
    down[1:0] = '{8'h01, 8'h02};
    down[3:2][0] = 8'h00;
    input_fixed[2:1] = pair;
    result = $size(down[runtime_bound:0]);
    result = $size(down[3:1], 2);
    result = down[0:2].sum();
    located = down[4:3].min();
    locations =
        down[1 +: 2].find_index() with (item);
    result = dynamic[1:0].sum();
    located = queued[1:0].min();
    locations =
        associative[1:0].find_index() with (item);
    result = down[3:1][2:1].sum();
    down[3:1].sort();
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_slices.ok());
    const auto rejected_slices = compile_and_elaborate(
        invalid_slices.design, "static_slice_invalid");
    assert(!rejected_slices.ok());
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVSLICE-001"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVSLICE-002"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVSLICE-003"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVSLICE-004"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVSLICE-005"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVSLICE-006"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-031"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVPORT-009"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVQUERY-001"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVQUERY-002"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVREDUCE-001"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVLOCATOR-001"));
    assert(has_diagnostic(
        rejected_slices, "FSIM-ELAB-SVFIND-001"));
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
  incompatible slice_actual(.memory(memory[3:0]));
  incompatible unknown_actual(.memory(missing));
  output_driver first(.memory(memory));
  output_driver second(.memory(memory));
  input_forward forward(.memory(memory));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_ports.ok());
    const auto rejected_ports = compile_and_elaborate(
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

    const auto invalid_dynamic_ports = fsim::frontend::parse_text(
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
  int keyed_static[1:0];
  logic [7:0] signed_key_static[-1:1];
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
    query_result = $size(item_t);
    fixed_value = '{1};
    bounded_value = '{1, 2, 3, 4};
    associative_value = '{1, 2};
    dynamic_value = '{0: 1};
    narrow_associative = '{-1: 1, 15: 2};
    dynamic_value = '{1, 2: 3};
    dynamic_value = '{default: 1};
    keyed_static = '{0: 1, 1: 2};
    keyed_static = '{default: 0, default: 1};
    keyed_static = '{default: 0, query_result: 1};
    keyed_static = '{default: 0, 32'hxxxxxxxx: 1};
    keyed_static = '{default: 0, 2: 1};
    signed_key_static = '{
        default: 8'h00, -1: 8'h01,
        32'hffffffff: 8'h02};
    keyed_static = '{default: 0, 0: 1, 2};
    dynamic_value[0] = '{1};
    query_result = '{1};
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_dynamic_ports.ok());
    const auto rejected_dynamic_ports = compile_and_elaborate(
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
        rejected_dynamic_ports, "FSIM-ELAB-SVQUERY-002"));
    const auto rejected_query_arity = fsim::frontend::parse_text(
        "invalid-query-arity.sv",
        R"(module invalid_query_arity;
  int values[];
  int result;
  initial begin
    result = $bits(values, 1);
    result = $dimensions(values, 1);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(!rejected_query_arity.ok());
    assert(std::ranges::count_if(
               rejected_query_arity.diagnostics,
               [](const auto& diagnostic) {
                   return diagnostic.code == "FSIM-SV-SEM-075";
               })
        == 2);
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-001"));
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-002"));
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-003"));
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-004"));
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-005"));
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-006"));
    assert(has_diagnostic(
        rejected_dynamic_ports, "FSIM-ELAB-SVPATTERN-007"));

    test_mixed_container_port_rejections(
        port_parsed.design, dynamic_port_parsed.design);
    test_systemverilog_static_generated_container_reads();
    test_systemverilog_static_typed_container_reads();
}

void test_systemverilog_static_generated_container_reads()
{
    using namespace fsim::runtime::simir;

    const auto parsed = fsim::frontend::parse_text(
        "static-generated-container-reads.sv",
        R"(
module static_generated_container_reads;
  logic [7:0] down_source[3:0];
  logic [7:0] down_observed[3:0];
  logic [7:0] up_source[-1:2];
  logic [7:0] up_observed[-1:2];

  for (genvar down_index = 3; down_index >= 0;
       down_index = down_index - 1) begin: down_read
    always_comb
      down_observed[down_index] = down_source[down_index];
  end
  for (genvar up_index = -1; up_index <= 2;
       up_index = up_index + 1) begin: up_read
    always_comb
      up_observed[up_index] = up_source[up_index];
  end
endmodule

module static_generated_container_read_fallbacks;
  logic [7:0] values[0:3];
  int selector;
  int cursor;
  logic [7:0] dynamic_result;
  logic [7:0] out_of_range_result;
  logic [7:0] side_effect_result;

  function automatic int next_index();
    next_index = cursor;
    cursor = cursor + 1;
  endfunction

  initial begin
    values = '{8'h10, 8'h20, 8'h30, 8'h40};
    selector = 1;
    cursor = 2;
    dynamic_result = values[selector];
    out_of_range_result = values[4];
    side_effect_result = values[next_index()];
  end
endmodule

module static_generated_container_read_bit2;
  bit [7:0] bit_source[0:1];
  bit [7:0] bit_observed[0:1];

  for (genvar bit_index = 0; bit_index < 2;
       bit_index = bit_index + 1) begin: bit_read
    always_comb
      bit_observed[bit_index] = bit_source[bit_index];
  end

  initial bit_source = '{8'h35, 8'hca};
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());

    const auto generated = compile_and_elaborate(
        parsed.design, "static_generated_container_reads");
    assert(generated.ok());
    const auto down_source = generated.design->find_signal("down_source");
    const auto up_source = generated.design->find_signal("up_source");
    const auto down_object = generated.design->find_container("down_source");
    const auto up_object = generated.design->find_container("up_source");
    assert(down_source && up_source && down_object && up_object);

    const auto expect_extract_offsets =
        [&](const SignalId signal,
            const std::vector<std::uint32_t>& expected_offsets) {
            std::vector<std::uint32_t> offsets;
            for (const auto& process : generated.design->processes()) {
                for (std::size_t position = 0;
                    position + 1U < process.operations.size();
                    ++position) {
                    const auto* read = operation_get_if<ReadSignal>(
                        &process.operations[position]);
                    if (read == nullptr || read->signal != signal) {
                        continue;
                    }
                    assert(read->kind == SignalReadKind::current);
                    const auto* extract = operation_get_if<Extract>(
                        &process.operations[position + 1U]);
                    assert(extract != nullptr && extract->width == 8U);
                    offsets.push_back(extract->offset);
                    assert(std::ranges::any_of(
                        process.static_sensitivity,
                        [&](const Sensitivity& sensitivity) {
                            return sensitivity.signal == signal;
                        }));
                }
            }
            std::ranges::sort(offsets);
            assert((offsets == expected_offsets));
        };
    const std::vector<std::uint32_t> all_byte_offsets { 0U, 8U, 16U, 24U };
    expect_extract_offsets(*down_source, all_byte_offsets);
    expect_extract_offsets(*up_source, all_byte_offsets);
    for (const auto& process : generated.design->processes()) {
        for (const auto& operation : process.operations) {
            const auto* object_read
                = operation_get_if<ReadContainerObject>(&operation);
            assert(object_read == nullptr
                || (object_read->object != *down_object
                    && object_read->object != *up_object));
        }
    }

    auto generated_interpreter = generated.design->create_interpreter();
    const auto down_observed
        = generated.design->find_signal("down_observed");
    const auto up_observed = generated.design->find_signal("up_observed");
    assert(down_observed && up_observed);
    generated_interpreter->force_signal(
        *down_source,
        fsim::runtime::PackedLogic4::from_msb_string(
            "0001001000XZ01000101011001111000"));
    generated_interpreter->force_signal(
        *up_source,
        fsim::runtime::PackedLogic4::from_msb_string(
            "10000001001000110100010101100111"));
    const auto generated_result = generated_interpreter->run();
    assert(generated_result.status == fsim::runtime::RunStatus::completed);
    assert(generated_interpreter->signal_value(*down_source)
        .to_msb_string() == "0001001000XZ01000101011001111000");
    assert(generated_interpreter->signal_value(*down_source)
        == generated_interpreter->signal_value(*down_observed));
    assert(generated_interpreter->signal_value(*up_source)
        .to_msb_string() == "10000001001000110100010101100111");
    assert(generated_interpreter->signal_value(*up_source)
        == generated_interpreter->signal_value(*up_observed));

    const auto fallback = compile_and_elaborate(
        parsed.design, "static_generated_container_read_fallbacks");
    assert(fallback.ok());
    const auto values_signal = fallback.design->find_signal("values");
    const auto values_object = fallback.design->find_container("values");
    assert(values_signal && values_object);
    std::size_t legacy_values_reads { };
    for (const auto& process : fallback.design->processes()) {
        std::vector<ContainerRegisterId> values_registers;
        for (const auto& operation : process.operations) {
            if (const auto* object_read
                = operation_get_if<ReadContainerObject>(&operation);
                object_read != nullptr
                && object_read->object == *values_object) {
                values_registers.push_back(object_read->destination);
            }
        }
        for (const auto& operation : process.operations) {
            if (const auto* container_read
                = operation_get_if<ContainerRead>(&operation);
                container_read != nullptr
                && std::ranges::find(
                    values_registers, container_read->source)
                    != values_registers.end()) {
                ++legacy_values_reads;
            }
            const auto* signal_read = operation_get_if<ReadSignal>(&operation);
            assert(signal_read == nullptr
                || signal_read->signal != *values_signal);
        }
    }
    assert(legacy_values_reads >= 3U);
    auto fallback_interpreter = fallback.design->create_interpreter();
    const auto fallback_result = fallback_interpreter->run();
    assert(fallback_result.status == fsim::runtime::RunStatus::completed);
    const auto dynamic_result
        = fallback.design->find_signal("dynamic_result");
    const auto out_of_range_result
        = fallback.design->find_signal("out_of_range_result");
    const auto side_effect_result
        = fallback.design->find_signal("side_effect_result");
    const auto cursor_signal = fallback.design->find_signal("cursor");
    assert(dynamic_result && out_of_range_result
        && side_effect_result && cursor_signal);
    assert(fallback_interpreter->signal_value(*dynamic_result)
        .to_msb_string() == "00100000");
    assert(fallback_interpreter->signal_value(*out_of_range_result)
        .to_msb_string() == "XXXXXXXX");
    assert(fallback_interpreter->signal_value(*side_effect_result)
        .to_msb_string() == "00110000");
    assert(fallback_interpreter->signal_value(*cursor_signal)
        .low_word().aval == 3U);

    const auto bit2 = compile_and_elaborate(
        parsed.design, "static_generated_container_read_bit2");
    assert(bit2.ok());
    const auto bit_source = bit2.design->find_signal("bit_source");
    const auto bit_object = bit2.design->find_container("bit_source");
    const auto bit_observed = bit2.design->find_signal("bit_observed");
    assert(bit_source && bit_object && bit_observed);
    std::size_t bit2_legacy_reads { };
    for (const auto& process : bit2.design->processes()) {
        std::vector<ContainerRegisterId> bit_registers;
        for (const auto& operation : process.operations) {
            if (const auto* object_read
                = operation_get_if<ReadContainerObject>(&operation);
                object_read != nullptr && object_read->object == *bit_object) {
                bit_registers.push_back(object_read->destination);
            }
            const auto* signal_read = operation_get_if<ReadSignal>(&operation);
            assert(signal_read == nullptr
                || signal_read->signal != *bit_source);
        }
        for (const auto& operation : process.operations) {
            if (const auto* container_read
                = operation_get_if<ContainerRead>(&operation);
                container_read != nullptr
                && std::ranges::find(bit_registers, container_read->source)
                    != bit_registers.end()) {
                ++bit2_legacy_reads;
            }
        }
    }
    assert(bit2_legacy_reads >= 2U);
    auto bit2_interpreter = bit2.design->create_interpreter();
    const auto bit2_result = bit2_interpreter->run();
    assert(bit2_result.status == fsim::runtime::RunStatus::completed);
    assert(bit2_interpreter->signal_value(*bit_source)
        == bit2_interpreter->signal_value(*bit_observed));
}

void test_systemverilog_static_typed_container_reads()
{
    using namespace fsim::runtime::simir;
    using fsim::runtime::Logic4;
    using fsim::runtime::PackedLogic4;

    for (const auto width : { 65U, 129U }) {
        const auto source = std::string {
            "module static_typed_container_reads; localparam W = " }
            + std::to_string(width) + R"(;
  localparam int PICK = 1;
  logic [W-1:0] down_values[2:-1];
  logic [W-1:0] up_values[-1:2];
  logic [W-1:0] high_values[255:254];
  logic [W-1:0] blocking_values[1:0];
  int selector;
  logic [W-1:0] literal_result, parameter_result, arithmetic_result;
  logic [W-1:0] wide_out_of_range_result, wrapped_result, unsigned_result;
  logic [W-1:0] int_cast_result, signed_result, unknown_result, dynamic_result;
  logic [W-1:0] signed_in_range_result, out_of_range_result;
  logic [W-1:0] blocking_result;
  assign literal_result = down_values[2];
  assign parameter_result = down_values[PICK];
  assign arithmetic_result = up_values[1 + 1];
  assign wide_out_of_range_result = down_values[64'h100000001];
  assign wrapped_result = up_values[8'(8'hff + 8'd2)];
  assign unsigned_result = high_values[8'hff];
  assign int_cast_result = high_values[int'(8'hff)];
  assign signed_result = high_values[$signed(8'hff)];
  assign signed_in_range_result = up_values[$signed(8'hff)];
  assign unknown_result = down_values[2'bx1];
  assign out_of_range_result = down_values[3];
  assign dynamic_result = down_values[selector];
  always_comb begin
    blocking_values[selector - 1] = down_values[2];
    blocking_result = blocking_values[0];
  end
endmodule
)";
        const auto parsed = fsim::frontend::parse_text(
            "static-typed-container-reads.sv", source,
            fsim::frontend::Language::SystemVerilog2017);
        assert(parsed.ok());
        const auto elaborated = compile_and_elaborate(
            parsed.design, "static_typed_container_reads");
        assert(elaborated.ok());
        auto lowered_state = elaborated.design->state();

        const auto signal = [&](const std::string_view name) {
            const auto found = elaborated.design->find_signal(name);
            assert(found);
            return *found;
        };
        const auto literal_writer = std::ranges::find_if(
            elaborated.design->processes(), [&](const Process& process) {
                return std::ranges::any_of(process.operations,
                    [&](const Operation& operation) {
                        const auto* write = operation_get_if<WriteUpdate>(&operation);
                        return write != nullptr
                            && write->signal == signal("literal_result");
                    });
            });
        assert(literal_writer != elaborated.design->processes().end());
        assert(literal_writer->scheduling_domain
            == ProcessSchedulingDomain::systemverilog);
        assert(literal_writer->container_register_count == 0U);
        assert(std::ranges::none_of(
            literal_writer->operations,
            [](const Operation& operation) {
                return operation_holds<ReadContainerObject>(operation)
                    || operation_holds<ContainerRead>(operation);
            }));
        const auto literal_source = signal("down_values");
        const auto literal_read = std::ranges::find_if(
            literal_writer->operations,
            [&](const Operation& operation) {
                const auto* read = operation_get_if<ReadSignal>(&operation);
                return read != nullptr && read->signal == literal_source;
            });
        assert(literal_read != literal_writer->operations.end());
        const auto* literal_signal_read
            = operation_get_if<ReadSignal>(&*literal_read);
        assert(literal_signal_read != nullptr);
        const auto literal_extract = std::ranges::find_if(
            literal_writer->operations,
            [&](const Operation& operation) {
                const auto* extract = operation_get_if<Extract>(&operation);
                return extract != nullptr
                    && extract->source == literal_signal_read->destination
                    && extract->offset == 3U * width
                    && extract->width == width;
            });
        assert(literal_extract != literal_writer->operations.end());
        assert(std::ranges::any_of(
            literal_writer->static_sensitivity,
            [&](const Sensitivity& entry) {
                return entry.signal == literal_source
                    && entry.edge == EdgeKind::any
                    && entry.offset == 3U * width
                    && entry.width == width;
            }));
        assert(std::ranges::any_of(
            literal_writer->operations,
            [](const Operation& operation) {
                const auto* write = operation_get_if<WriteUpdate>(&operation);
                return write != nullptr
                    && write->domain
                        == SignalUpdateDomain::systemverilog_active;
            }));

        const auto expect_read = [&](const std::string_view output_name,
                                     const std::string_view input_name,
                                     const std::optional<std::uint32_t> offset,
                                     const std::size_t backing_width) {
            const auto output = signal(output_name);
            const auto input = signal(input_name);
            std::size_t writers { };
            for (const auto& process : lowered_state.processes) {
                const auto writes_output = std::ranges::any_of(
                    process.operations, [&](const Operation& operation) {
                        const auto* write
                            = operation_get_if<WriteUpdate>(&operation);
                        return write != nullptr && write->signal == output;
                    });
                if (!writes_output) {
                    continue;
                }
                ++writers;
                std::size_t signal_reads { };
                std::size_t container_reads { };
                for (std::size_t position { };
                    position < process.operations.size(); ++position) {
                    const auto& operation = process.operations[position];
                    container_reads += operation_holds<ContainerRead>(
                        operation) ? 1U : 0U;
                    const auto* read = operation_get_if<ReadSignal>(&operation);
                    if (read == nullptr || read->signal != input) {
                        continue;
                    }
                    ++signal_reads;
                    if (!offset) {
                        std::cerr << "typed container reader " << output_name
                                  << " expected a container fallback for "
                                  << input_name << " (offset=dynamic, width="
                                  << width << ", backing_width="
                                  << backing_width
                                  << ") but found a direct signal read; aliases:";
                        for (const auto& alias : lowered_state
                                 .container_signal_aliases) {
                            if (alias.object
                                < lowered_state.container_objects.size()) {
                                std::cerr << " object="
                                          << lowered_state
                                                 .container_objects[alias.object]
                                                 .name;
                            }
                            std::cerr << " signal=" << alias.signal
                                      << " readable=" << alias.readable
                                      << " writable=" << alias.writable;
                            if (alias.signal
                                < lowered_state.signal_info.size()) {
                                std::cerr << ':'
                                          << lowered_state
                                                 .signal_info[alias.signal]
                                                 .name;
                            }
                            std::cerr << ';';
                        }
                        std::cerr << '\n';
                    }
                    assert(offset);
                    assert(read->kind == SignalReadKind::current);
                    assert(elaborated.design->signals()[input].width
                        == backing_width);
                    assert(position + 1U < process.operations.size());
                    const auto* extract = operation_get_if<Extract>(
                        &process.operations[position + 1U]);
                    assert(extract != nullptr);
                    assert(extract->source == read->destination);
                    assert(extract->offset == *offset);
                    assert(extract->width == width);
                }
                if (signal_reads != (offset ? 1U : 0U)) {
                    std::cerr << "typed container reader " << output_name
                              << ": signal reads=" << signal_reads
                              << ", container reads=" << container_reads << '\n';
                }
                assert(signal_reads == (offset ? 1U : 0U));
                assert(container_reads == (offset ? 0U : 1U));
                assert(std::ranges::any_of(process.static_sensitivity,
                    [&](const Sensitivity& entry) {
                        return entry.signal == input;
                    }));
            }
            assert(writers == 1U);
        };
        expect_read("literal_result", "down_values", 3U * width, 4U * width);
        expect_read("parameter_result", "down_values", 2U * width, 4U * width);
        expect_read("arithmetic_result", "up_values", 0U, 4U * width);
        // Keep the 64-bit index intact: it is outside this unpacked range.
        // Narrowing is covered separately by explicit sized and int casts.
        expect_read("wide_out_of_range_result", "down_values",
            std::nullopt, 4U * width);
        // Sized constant arithmetic is range-checked after its result width is
        // applied, so this index wraps to one and selects a static element.
        expect_read("wrapped_result", "up_values", width, 4U * width);
        // A resolved unsigned literal and explicit integer cast select their
        // declared element before the array range is checked.
        expect_read("unsigned_result", "high_values", width, 2U * width);
        expect_read("int_cast_result", "high_values", width, 2U * width);
        expect_read("signed_result", "high_values", std::nullopt, 2U * width);
        expect_read("signed_in_range_result", "up_values", 3U * width,
            4U * width);
        expect_read("unknown_result", "down_values", std::nullopt, 4U * width);
        expect_read("out_of_range_result", "down_values", std::nullopt,
            4U * width);
        expect_read("dynamic_result", "down_values", std::nullopt, 4U * width);

        const auto lowered_design = fsim::elaboration::ElaboratedDesign::from_state(
            std::move(lowered_state));
        assert(lowered_design);
        auto interpreter = lowered_design->create_interpreter();
        auto original_interpreter = elaborated.design->create_interpreter();
        for (std::size_t round { }; round < 2U; ++round) {
            auto down = PackedLogic4(4U * width, Logic4::zero);
            auto up = down;
            auto high = PackedLogic4(2U * width, Logic4::zero);
            const auto pattern = PackedLogic4::from_msb_string("10XZ");
            for (std::size_t bit { }; bit < down.width(); ++bit) {
                down.set(bit, pattern.get((bit + bit / 17U + round) % 4U));
                up.set(bit, pattern.get((bit + bit / 19U + round + 1U) % 4U));
            }
            for (std::size_t bit { }; bit < high.width(); ++bit) {
                high.set(bit, pattern.get((bit + bit / 23U + round) % 4U));
            }
            interpreter->force_signal(signal("down_values"), down);
            interpreter->force_signal(signal("up_values"), up);
            interpreter->force_signal(signal("high_values"), high);
            interpreter->force_signal(signal("selector"),
                PackedLogic4::from_aval_bval(32U, 1U, 0U));
            original_interpreter->force_signal(signal("down_values"), down);
            original_interpreter->force_signal(signal("up_values"), up);
            original_interpreter->force_signal(signal("high_values"), high);
            original_interpreter->force_signal(signal("selector"),
                PackedLogic4::from_aval_bval(32U, 1U, 0U));
            assert(interpreter->run().status
                == fsim::runtime::RunStatus::completed);
            assert(original_interpreter->run().status
                == fsim::runtime::RunStatus::completed);
            const auto expect_value = [&](const std::string_view name,
                                          const PackedLogic4& expected) {
                assert(interpreter->signal_value(signal(name)) == expected);
                assert(original_interpreter->signal_value(signal(name)) == expected);
            };
            expect_value("literal_result", down.extract_bits(3U * width, width));
            expect_value("parameter_result", down.extract_bits(2U * width, width));
            expect_value("arithmetic_result", up.extract_bits(0U, width));
            expect_value("wide_out_of_range_result",
                PackedLogic4(width, Logic4::x));
            expect_value("wrapped_result", up.extract_bits(width, width));
            expect_value("unsigned_result", high.extract_bits(width, width));
            expect_value("int_cast_result", high.extract_bits(width, width));
            expect_value("signed_in_range_result", up.extract_bits(3U * width,
                width));
            expect_value("dynamic_result", down.extract_bits(2U * width, width));
            expect_value("blocking_result", down.extract_bits(3U * width, width));
            const auto unknown = PackedLogic4(width, Logic4::x);
            expect_value("signed_result", unknown);
            expect_value("unknown_result", unknown);
            expect_value("out_of_range_result", unknown);
        }
    }
}

} // namespace fsim::tests::elaboration
