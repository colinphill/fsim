// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

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
