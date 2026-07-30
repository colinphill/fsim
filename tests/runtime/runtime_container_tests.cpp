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

  const ContainerType array_type{32, true, true, false, std::nullopt};
  const ContainerType queue_type{8, false, false, true, 3};
  Interpreter interpreter;
  const auto array_object = interpreter.add_container_object(
      {"array", ContainerValue{array_type, {}}});
  const auto queue_object = interpreter.add_container_object(
      {"queue", ContainerValue{queue_type, {}}});
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
}

}  // namespace fsim::tests::runtime
