// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error{std::string{message}};
  }
}

[[nodiscard]] fsim::runtime::PackedLogic4 value(
    const std::uint32_t width,
    const std::uint64_t bits) {
  return fsim::runtime::PackedLogic4::from_aval_bval(
      width, bits, 0);
}

}  // namespace

void test_simir_containers() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  ContainerType array_type;
  array_type.element_width = 32;
  array_type.two_state = true;
  array_type.signed_elements = true;
  ContainerType queue_type;
  queue_type.element_width = 8;
  queue_type.queue = true;
  queue_type.maximum_elements = 3;
  const ContainerValue reduction_values{
      queue_type,
      {value(8, 3), value(8, 5), value(8, 7)},
      {}};
  require(
      reduce_container_value(
          reduction_values,
          ContainerReductionOperator::sum)
              == value(8, 15)
          && reduce_container_value(
                 reduction_values,
                 ContainerReductionOperator::product)
              == value(8, 105)
          && reduce_container_value(
                 reduction_values,
                 ContainerReductionOperator::bit_and)
              == value(8, 1)
          && reduce_container_value(
                 reduction_values,
                 ContainerReductionOperator::bit_or)
              == value(8, 7)
          && reduce_container_value(
                 reduction_values,
                 ContainerReductionOperator::bit_xor)
              == value(8, 1),
      "container reductions retain exact element width and values");
  const ContainerValue empty_values{queue_type, {}, {}};
  require(
      reduce_container_value(
          empty_values, ContainerReductionOperator::sum)
              == value(8, 0)
          && reduce_container_value(
                 empty_values,
                 ContainerReductionOperator::product)
              == value(8, 1)
          && reduce_container_value(
                 empty_values,
                 ContainerReductionOperator::bit_and)
              == value(8, 0xff)
          && reduce_container_value(
                 empty_values,
                 ContainerReductionOperator::bit_or)
              == value(8, 0)
          && reduce_container_value(
                 empty_values,
                 ContainerReductionOperator::bit_xor)
              == value(8, 0),
      "empty container reductions use SystemVerilog identities");
  const ContainerValue unknown_values{
      queue_type,
      {value(8, 3),
       PackedLogic4::from_aval_bval(8, 3, 2)},
      {}};
  require(
      reduce_container_value(
          unknown_values,
          ContainerReductionOperator::sum)
              .to_msb_string()
          == "XXXXXXXX"
          && reduce_container_value(
                 unknown_values,
                 ContainerReductionOperator::bit_and)
                 .to_msb_string()
              == "000000X1",
      "arithmetic and bitwise reductions propagate four-state values");
  ContainerType signed_order_type = queue_type;
  signed_order_type.signed_elements = true;
  signed_order_type.maximum_elements.reset();
  ContainerValue signed_order{
      signed_order_type,
      {value(8, 0x7f), value(8, 0xff), value(8, 0x80),
       value(8, 0), value(8, 0xff)},
      {}};
  order_container_value(
      signed_order, ContainerOrderingOperator::ascending);
  require(
      signed_order.elements
          == std::vector<PackedLogic4>{
              value(8, 0x80), value(8, 0xff), value(8, 0xff),
              value(8, 0), value(8, 0x7f)},
      "ascending ordering uses exact signed element semantics");
  order_container_value(
      signed_order, ContainerOrderingOperator::descending);
  require(
      signed_order.elements
          == std::vector<PackedLogic4>{
              value(8, 0x7f), value(8, 0), value(8, 0xff),
              value(8, 0xff), value(8, 0x80)},
      "descending ordering is stable and reverses the comparison");
  order_container_value(
      signed_order, ContainerOrderingOperator::reverse);
  require(
      signed_order.elements.front() == value(8, 0x80)
          && signed_order.elements.back() == value(8, 0x7f),
      "reverse permutes storage without changing container metadata");
  auto four_state_order_type = queue_type;
  four_state_order_type.maximum_elements.reset();
  ContainerValue four_state_order{
      four_state_order_type,
      {
          PackedLogic4::from_msb_string("0000000Z"),
          value(8, 1),
          PackedLogic4::from_msb_string("0000000X"),
          value(8, 0),
      },
      {}};
  order_container_value(
      four_state_order, ContainerOrderingOperator::ascending);
  require(
      four_state_order.elements[0] == value(8, 0)
          && four_state_order.elements[1] == value(8, 1)
          && four_state_order.elements[2].to_msb_string()
              == "0000000X"
          && four_state_order.elements[3].to_msb_string()
              == "0000000Z",
      "four-state ordering uses the documented 0/1/X/Z total order");
  ContainerValue locator_result{
      signed_order_type, {}, {}};
  ContainerType index_result_type;
  index_result_type.element_width = 32;
  index_result_type.two_state = true;
  index_result_type.signed_elements = true;
  index_result_type.queue = true;
  const ContainerValue empty_locator_source{
      signed_order_type, {}, {}};
  for (const auto operation :
       {ContainerLocatorOperator::minimum,
        ContainerLocatorOperator::maximum,
        ContainerLocatorOperator::unique,
        ContainerLocatorOperator::unique_index}) {
    ContainerValue empty_result{
        operation == ContainerLocatorOperator::unique_index
            ? index_result_type
            : signed_order_type,
        {}, {}};
    locate_container_values(
        empty_result, empty_locator_source, operation);
    require(
        empty_result.elements.empty(),
        "empty container locators return an empty queue");
  }
  locate_container_values(
      locator_result, signed_order,
      ContainerLocatorOperator::minimum);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{value(8, 0x80)},
      "minimum locator returns the signed extremum");
  locate_container_values(
      locator_result, signed_order,
      ContainerLocatorOperator::maximum);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{value(8, 0x7f)},
      "maximum locator returns the signed extremum");
  locate_container_values(
      locator_result, signed_order,
      ContainerLocatorOperator::unique);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{
              value(8, 0x80), value(8, 0xff),
              value(8, 0), value(8, 0x7f)},
      "unique locator preserves first-occurrence order");
  ContainerValue index_result{index_result_type, {}, {}};
  locate_container_values(
      index_result, signed_order,
      ContainerLocatorOperator::unique_index);
  require(
      index_result.elements
          == std::vector<PackedLogic4>{
              value(32, 0), value(32, 1),
              value(32, 3), value(32, 4)},
      "unique_index returns first current indices");
  locate_container_values(
      signed_order, signed_order,
      ContainerLocatorOperator::unique);
  require(
      signed_order.elements == locator_result.elements,
      "locator assignment safely supports an aliased queue receiver");
  ContainerType fixed_locator_type = signed_order_type;
  fixed_locator_type.queue = false;
  fixed_locator_type.fixed = true;
  fixed_locator_type.index_left = -2;
  fixed_locator_type.index_right = 1;
  ContainerValue fixed_locator{
      fixed_locator_type,
      {value(8, 5), value(8, 7), value(8, 5), value(8, 9)},
      {}};
  locate_container_values(
      index_result, fixed_locator,
      ContainerLocatorOperator::unique_index);
  require(
      index_result.elements
          == std::vector<PackedLogic4>{
              value(32, UINT32_C(0xfffffffe)),
              value(32, UINT32_C(0xffffffff)),
              value(32, 1)},
      "static unique_index preserves signed declared indices");
  const std::vector<ContainerPredicateNode> greater_than_five{
      {ContainerPredicateOperator::item, 0, 0, PackedLogic4{},
       ContainerPredicateValueKind::element},
      {ContainerPredicateOperator::constant, 0, 0, value(8, 5),
       ContainerPredicateValueKind::element},
      {ContainerPredicateOperator::greater, 0, 1, PackedLogic4{},
       ContainerPredicateValueKind::logical}};
  const std::vector<ContainerPredicateNode> equal_five{
      {ContainerPredicateOperator::item, 0, 0, PackedLogic4{},
       ContainerPredicateValueKind::element},
      {ContainerPredicateOperator::constant, 0, 0, value(8, 5),
       ContainerPredicateValueKind::element},
      {ContainerPredicateOperator::equal, 0, 1, PackedLogic4{},
       ContainerPredicateValueKind::logical}};
  const std::vector<ContainerPredicateNode> negative_index{
      {ContainerPredicateOperator::index, 0, 0, PackedLogic4{},
       ContainerPredicateValueKind::index},
      {ContainerPredicateOperator::constant, 0, 0, value(32, 0),
       ContainerPredicateValueKind::index},
      {ContainerPredicateOperator::less, 0, 1, PackedLogic4{},
       ContainerPredicateValueKind::logical}};
  const std::vector<ContainerPredicateNode> current_index_after_zero{
      {ContainerPredicateOperator::index, 0, 0, PackedLogic4{},
       ContainerPredicateValueKind::index},
      {ContainerPredicateOperator::constant, 0, 0, value(32, 0),
       ContainerPredicateValueKind::index},
      {ContainerPredicateOperator::greater, 0, 1, PackedLogic4{},
       ContainerPredicateValueKind::logical}};
  for (const auto operation :
       {ContainerLocatorOperator::find,
        ContainerLocatorOperator::find_index,
        ContainerLocatorOperator::find_first,
        ContainerLocatorOperator::find_first_index,
        ContainerLocatorOperator::find_last,
        ContainerLocatorOperator::find_last_index}) {
    const bool indices =
        operation == ContainerLocatorOperator::find_index
        || operation
            == ContainerLocatorOperator::find_first_index
        || operation
            == ContainerLocatorOperator::find_last_index;
    ContainerValue empty_result{
        indices ? index_result_type : signed_order_type,
        {}, {}};
    locate_container_values(
        empty_result, empty_locator_source, operation,
        greater_than_five);
    require(
        empty_result.elements.empty(),
        "empty predicate locators return an empty queue");
  }
  locate_container_values(
      locator_result, fixed_locator,
      ContainerLocatorOperator::find,
      greater_than_five);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{
              value(8, 7), value(8, 9)},
      "find preserves declared element order");
  locate_container_values(
      index_result, fixed_locator,
      ContainerLocatorOperator::find_index,
      equal_five);
  require(
      index_result.elements
          == std::vector<PackedLogic4>{
              value(32, UINT32_C(0xfffffffe)),
              value(32, 0)},
      "find_index returns signed declared static indices");
  locate_container_values(
      locator_result, fixed_locator,
      ContainerLocatorOperator::find,
      negative_index);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{
              value(8, 5), value(8, 7)},
      "predicate index comparisons use signed declared static indices");
  locate_container_values(
      locator_result, signed_order,
      ContainerLocatorOperator::find,
      current_index_after_zero);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{
              signed_order.elements[1],
              signed_order.elements[2],
              signed_order.elements[3]},
      "dynamic and queue predicate indices use current zero-based positions");
  locate_container_values(
      locator_result, fixed_locator,
      ContainerLocatorOperator::find_first,
      greater_than_five);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{value(8, 7)},
      "find_first returns the first matching value");
  locate_container_values(
      index_result, fixed_locator,
      ContainerLocatorOperator::find_first_index,
      greater_than_five);
  require(
      index_result.elements
          == std::vector<PackedLogic4>{
              value(32, UINT32_C(0xffffffff))},
      "find_first_index returns the first matching declared index");
  locate_container_values(
      locator_result, fixed_locator,
      ContainerLocatorOperator::find_last,
      greater_than_five);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{value(8, 9)},
      "find_last returns the last matching value");
  locate_container_values(
      index_result, fixed_locator,
      ContainerLocatorOperator::find_last_index,
      greater_than_five);
  require(
      index_result.elements
          == std::vector<PackedLogic4>{value(32, 1)},
      "find_last_index returns the last matching declared index");
  ContainerValue unknown_find_result{
      four_state_order_type, {}, {}};
  const std::vector<ContainerPredicateNode> equal_one{
      {ContainerPredicateOperator::item, 0, 0, PackedLogic4{},
       ContainerPredicateValueKind::element},
      {ContainerPredicateOperator::constant, 0, 0, value(8, 1),
       ContainerPredicateValueKind::element},
      {ContainerPredicateOperator::equal, 0, 1, PackedLogic4{},
       ContainerPredicateValueKind::logical}};
  locate_container_values(
      unknown_find_result, four_state_order,
      ContainerLocatorOperator::find,
      equal_one);
  require(
      unknown_find_result.elements
          == std::vector<PackedLogic4>{value(8, 1)},
      "unknown predicate results do not select an element");
  ContainerType bounded_find_type = signed_order_type;
  bounded_find_type.maximum_elements = 1;
  ContainerValue bounded_find{bounded_find_type, {}, {}};
  locate_container_values(
      bounded_find, fixed_locator,
      ContainerLocatorOperator::find,
      greater_than_five);
  require(
      bounded_find.elements
          == std::vector<PackedLogic4>{value(8, 7)},
      "find respects a bounded result queue");
  locate_container_values(
      locator_result, locator_result,
      ContainerLocatorOperator::find,
      greater_than_five);
  require(
      locator_result.elements
          == std::vector<PackedLogic4>{value(8, 9)},
      "predicate locator assignment safely supports an aliased queue");
  Interpreter interpreter;
  const auto array_object = interpreter.add_container_object(
      {"array", ContainerValue{array_type, {}, {}}});
  const auto queue_object = interpreter.add_container_object(
      {"queue", ContainerValue{queue_type, {}, {}}});
  Process process;
  process.id = 0;
  process.name = "containers";
  process.register_count = 8;
  process.container_register_count = 3;
  process.container_register_types = {
      array_type, queue_type, array_type};
  process.debug_locals = {
      {"size", "integer", 5, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"popped", "byte", 6, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}}};
  process.debug_container_locals = {
      {"values", 0, array_type, {}},
      {"pending", 1, queue_type, {}},
      {"copied", 2, array_type, {}}};
  process.operations = {
      LoadConstant{0, value(32, 3)},
      ResizeContainer{0, 0},
      LoadConstant{1, value(32, 0)},
      LoadConstant{2, value(32, 11)},
      ContainerWrite{0, 1, 2, true},
      LoadConstant{1, value(32, 2)},
      LoadConstant{2, value(32, 33)},
      ContainerWrite{0, 1, 2, true},
      CopyContainerRegister{2, 0},
      WriteContainerObject{array_object, 2},
      LoadConstant{3, value(8, 1)},
      PushContainer{1, 3, false},
      LoadConstant{3, value(8, 2)},
      PushContainer{1, 3, false},
      LoadConstant{3, value(8, 3)},
      PushContainer{1, 3, false},
      LoadConstant{3, value(8, 4)},
      PushContainer{1, 3, false},
      LoadConstant{3, value(8, 9)},
      PushContainer{1, 3, true},
      ContainerSize{5, 1},
      PopContainer{6, 1, false},
      WriteContainerObject{queue_object, 1},
      Halt{}};
  (void)interpreter.add_process(std::move(process));
  require(
      interpreter.run().status == RunStatus::completed,
      "container process completes");
  const auto& array =
      interpreter.container_object_value(array_object);
  require(
      array.elements
          == std::vector<PackedLogic4>{
              value(32, 11), value(32, 0), value(32, 33)},
      "dynamic arrays resize, index-write, and copy by value");
  const auto& queue =
      interpreter.container_object_value(queue_object);
  require(
      queue.elements
          == std::vector<PackedLogic4>{
              value(8, 9), value(8, 1)},
      "bounded queue overflow and pop ordering are deterministic");
  require(
      interpreter.read_debug_local(0, 0) == value(32, 3)
          && interpreter.read_debug_local(0, 1)
              == value(8, 2),
      "size and pop results retain exact scalar types");
  require(
      interpreter.read_debug_container_local(0, 0).elements
          == array.elements,
      "container locals remain debugger-visible");

  ContainerType associative_type;
  associative_type.element_width = 8;
  associative_type.associative = true;
  associative_type.index_width = 8;
  associative_type.signed_indices = true;
  Interpreter associative;
  const auto associative_object =
      associative.add_container_object(
          {"lookup",
           ContainerValue{associative_type, {}, {}}});
  Process associative_process;
  associative_process.id = 0;
  associative_process.name = "associative";
  associative_process.register_count = 14;
  associative_process.container_register_count = 1;
  associative_process.container_register_types = {
      associative_type};
  associative_process.debug_locals = {
      {"size", "int", 2, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"exists", "int", 3, 32, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"missing", "byte", 4, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"first_key", "byte", 7, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"next_key", "byte", 9, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"last_key", "byte", 11, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}},
      {"previous_key", "byte", 13, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}}};
  associative_process.operations = {
      LoadConstant{0, value(8, 2)},
      LoadConstant{1, value(8, 22)},
      ContainerWrite{0, 0, 1, true},
      LoadConstant{0, value(8, 0xff)},
      LoadConstant{1, value(8, 11)},
      ContainerWrite{0, 0, 1, true},
      LoadConstant{0, value(8, 7)},
      LoadConstant{1, value(8, 77)},
      ContainerWrite{0, 0, 1, true},
      ContainerSize{2, 0},
      LoadConstant{0, value(8, 2)},
      ContainerExists{3, 0, 0},
      LoadConstant{0, value(8, 3)},
      ContainerRead{4, 0, 0, true},
      LoadConstant{5, value(8, 0)},
      TraverseContainer{
          6, 0, 5, ContainerTraversal::first},
      CopyRegister{7, 5},
      TraverseContainer{
          8, 0, 5, ContainerTraversal::next},
      CopyRegister{9, 5},
      TraverseContainer{
          10, 0, 5, ContainerTraversal::last},
      CopyRegister{11, 5},
      TraverseContainer{
          12, 0, 5, ContainerTraversal::previous},
      CopyRegister{13, 5},
      LoadConstant{0, value(8, 2)},
      DeleteContainer{0, 0},
      WriteContainerObject{associative_object, 0},
      Halt{}};
  (void)associative.add_process(
      std::move(associative_process));
  require(
      associative.run().status == RunStatus::completed,
      "associative-array process completes");
  const auto& lookup =
      associative.container_object_value(associative_object);
  require(
      lookup.keys
              == std::vector<PackedLogic4>{
                  value(8, 0xff), value(8, 7)}
          && lookup.elements
              == std::vector<PackedLogic4>{
                  value(8, 11), value(8, 77)},
      "signed keys are canonically ordered and delete removes one pair");
  require(
      associative.read_debug_local(0, 0) == value(32, 3)
          && associative.read_debug_local(0, 1)
              == value(32, 1)
          && associative.read_debug_local(0, 2)
              == value(8, 0),
      "size, exists, and missing reads have deterministic results");
  require(
      associative.read_debug_local(0, 3)
              == value(8, 0xff)
          && associative.read_debug_local(0, 4)
              == value(8, 2)
          && associative.read_debug_local(0, 5)
              == value(8, 7)
          && associative.read_debug_local(0, 6)
              == value(8, 2),
      "first/next/last/previous traverse canonical key order");

  ContainerType static_type;
  static_type.element_width = 8;
  static_type.fixed = true;
  static_type.index_left = 2;
  static_type.index_right = -1;
  auto static_initial = default_container_value(static_type);
  require(
      static_initial.elements.size() == 4
          && static_initial.elements.front().to_msb_string()
              == "XXXXXXXX",
      "four-state static arrays materialize their full declared range "
      "with X defaults");
  auto oversized_static_type = static_type;
  oversized_static_type.index_left =
      std::numeric_limits<std::int32_t>::min();
  oversized_static_type.index_right =
      std::numeric_limits<std::int32_t>::max();
  try {
    (void)default_container_value(oversized_static_type);
    require(false, "oversized static type must fail before allocation");
  } catch (const std::length_error&) {
  }
  Interpreter fixed;
  const auto fixed_object = fixed.add_container_object(
      {"fixed", static_initial});
  Process fixed_process;
  fixed_process.id = 0;
  fixed_process.name = "fixed";
  fixed_process.register_count = 3;
  fixed_process.container_register_count = 2;
  fixed_process.container_register_types = {
      static_type, static_type};
  fixed_process.debug_locals = {
      {"selected", "byte", 2, 8, {}, std::nullopt,
       std::nullopt, ValueKind::logic4, {}}};
  fixed_process.operations = {
      LoadConstant{
          0, value(32, static_cast<std::uint32_t>(-1))},
      LoadConstant{1, value(8, 0x5a)},
      ContainerWrite{0, 0, 1, true},
      ContainerRead{2, 0, 0, true},
      CopyContainerRegister{1, 0},
      WriteContainerObject{fixed_object, 1},
      Halt{}};
  (void)fixed.add_process(std::move(fixed_process));
  require(
      fixed.run().status == RunStatus::completed
          && fixed.read_debug_local(0, 0) == value(8, 0x5a)
          && fixed.container_object_value(fixed_object)
                 .elements[3]
              == value(8, 0x5a),
      "descending signed static indices map to dense declared-order "
      "storage and whole copies preserve every element");

  ContainerType memory_type;
  memory_type.element_width = 8;
  memory_type.fixed = true;
  memory_type.index_left = 3;
  memory_type.index_right = 0;
  auto memory = default_container_value(memory_type);
  load_memory_text(
      memory,
      "a5 /* inline */ xz\n@3 0f // tail\n",
      true);
  require(
      memory.elements[0].to_msb_string() == "00001111"
          && memory.elements[1].to_msb_string()
              == "XXXXXXXX"
          && memory.elements[2].to_msb_string()
              == "XXXXZZZZ"
          && memory.elements[3].to_msb_string()
              == "10100101",
      "$readmemh semantics retain comments, @addresses, and exact X/Z "
      "nibbles while default addresses increase numerically");
  load_memory_text(
      memory, "10z1 0011", false, 1, 0);
  require(
      memory.elements[2].to_msb_string()
              == "000010Z1"
          && memory.elements[3].to_msb_string()
              == "00000011",
      "$readmemb optional bounds use declared indices and retain Z bits");
  try {
    load_memory_text(memory, "@7 00", true);
    require(false, "out-of-range read-memory address must fail");
  } catch (const std::out_of_range&) {
  }
  try {
    load_memory_text(memory, "2", false);
    require(false, "invalid binary read-memory digit must fail");
  } catch (const std::invalid_argument&) {
  }
  try {
    load_memory_text(
        memory,
        std::string(maximum_memory_file_bytes + 1U, '0'),
        false);
    require(false, "oversized read-memory input must fail");
  } catch (const std::length_error&) {
  }

  const auto expect_failure =
      [&](const ContainerType& type,
          std::vector<Operation> operations,
          const std::string_view expected) {
        Interpreter failing;
        Process candidate;
        candidate.id = 0;
        candidate.name = "container_failure";
        candidate.register_count = 3;
        candidate.container_register_count = 1;
        candidate.container_register_types = {type};
        candidate.operations = std::move(operations);
        (void)failing.add_process(std::move(candidate));
        try {
          (void)failing.run();
          require(false, "invalid container process must fail");
        } catch (const InterpreterError& error) {
          require(
              std::string_view{error.what()}.find(expected)
                  != std::string_view::npos,
              "container failure retains its diagnostic");
        }
      };
  expect_failure(
      array_type,
      {LoadConstant{0, value(32, 4097)},
       ResizeContainer{0, 0},
       Halt{}},
      "4096-element limit");
  expect_failure(
      array_type,
      {LoadConstant{0, value(32, 1)},
       ResizeContainer{0, 0},
       LoadConstant{1, value(32, 2)},
       ContainerRead{2, 0, 1, true},
       Halt{}},
      "out of range");
  expect_failure(
      array_type,
      {LoadConstant{0, value(8, 1)},
       PushContainer{0, 0, false},
       Halt{}},
      "dynamic array");
  expect_failure(
      queue_type,
      {PopContainer{0, 0, true}, Halt{}},
      "empty queue");
  expect_failure(
      associative_type,
      {LoadConstant{
           0,
           PackedLogic4::from_aval_bval(8, 1, 1)},
       ContainerExists{1, 0, 0},
       Halt{}},
      "known integral value");
  expect_failure(
      array_type,
      {LoadConstant{0, value(32, 1)},
       DeleteContainer{0, 0},
       Halt{}},
      "requires an associative array");
  expect_failure(
      static_type,
      {LoadConstant{
           0,
           PackedLogic4::from_aval_bval(32, 1, 1)},
       ContainerRead{1, 0, 0, true},
       Halt{}},
      "known 32-bit integral value");
  expect_failure(
      static_type,
      {LoadConstant{0, value(32, 3)},
       ContainerRead{1, 0, 0, true},
       Halt{}},
      "static-array index is out of range");
  expect_failure(
      static_type,
      {DeleteContainer{0, std::nullopt}, Halt{}},
      "cannot clear a static array");

  ContainerType limited_type = associative_type;
  limited_type.index_width = 13;
  std::vector<Operation> limit_operations;
  limit_operations.reserve(
      maximum_container_elements * 3U + 1U);
  for (std::size_t entry = 0;
       entry <= maximum_container_elements; ++entry) {
    limit_operations.emplace_back(
        LoadConstant{0, value(13, entry)});
    limit_operations.emplace_back(
        LoadConstant{1, value(8, entry)});
    limit_operations.emplace_back(
        ContainerWrite{0, 0, 1, false});
  }
  limit_operations.emplace_back(Halt{});
  expect_failure(
      limited_type,
      std::move(limit_operations),
      "4096-entry limit");
}

}  // namespace fsim::tests::runtime
