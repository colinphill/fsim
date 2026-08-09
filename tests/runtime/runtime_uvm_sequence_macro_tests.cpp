// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_sequence.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kSequencerType{"work::macro_sequencer#(32)"};
constexpr std::string_view kSequenceType{"work::macro_sequence#(32)"};
constexpr std::string_view kRequestType{"work::macro_request#(32)"};
constexpr std::string_view kResponseType{"work::macro_response#(32)"};

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

void require_error(
    const std::string_view code,
    const std::function<void()>& operation,
    const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmSequenceError& error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

PackedLogic4 packed(const std::uint64_t value) {
  return PackedLogic4::from_aval_bval(4, value, 0);
}

SystemVerilogConstraintClause equal_clause(
    std::string identity,
    const SystemVerilogConstraintVariableId variable,
    const std::uint64_t expected) {
  return {
      std::move(identity), {variable},
      [=](const SystemVerilogConstraintAssignment& assignment) {
        if (!assignment.assigned(variable)) {
          return SystemVerilogConstraintClauseState::Undetermined;
        }
        return assignment.value(variable).low_word().aval == expected
            ? SystemVerilogConstraintClauseState::Satisfied
            : SystemVerilogConstraintClauseState::Violated;
      }};
}

std::string property_identity(const std::string_view specialization) {
  return std::string{specialization} + "::payload";
}

SystemVerilogClassDescriptor class_descriptor(
    const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
  result.random_root_identity = std::string{specialization};
  if (specialization == kSequencerType) {
    result.declared_type = "uvm_pkg::uvm_sequencer";
    result.assignable_declared_types = {
        std::string{kSequencerType}, "uvm_pkg::uvm_sequencer",
        "uvm_pkg::uvm_component", "uvm_pkg::uvm_object"};
  } else if (specialization == kSequenceType) {
    result.declared_type = "uvm_pkg::uvm_sequence";
    result.assignable_declared_types = {
        std::string{kSequenceType}, "uvm_pkg::uvm_sequence",
        "uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_object"};
  } else {
    result.declared_type = "uvm_pkg::uvm_sequence_item";
    result.assignable_declared_types = {
        std::string{specialization}, "uvm_pkg::uvm_sequence_item",
        "uvm_pkg::uvm_object"};
  }
  if (specialization != kSequencerType) {
    SystemVerilogClassPropertyDescriptor payload{
        property_identity(specialization),
        SystemVerilogClassPropertyKind::Bit2, 4};
    payload.initial_packed = packed(0);
    payload.random_kind = SystemVerilogClassRandomKind::Rand;
    payload.nominal_type = "bit[3:0]";
    result.properties.push_back(std::move(payload));
  }
  return result;
}

struct MacroFixture {
  SystemVerilogClassHeap heap{{512, 65'536}, 0x1610'0004ULL};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle root{};

  MacroFixture()
      : objects(
            heap,
            [this](const std::string_view specialization,
                   const std::string_view,
                   const std::string_view) {
              return heap.allocate(class_descriptor(specialization));
            }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}) {
    for (const auto type : {
             kSequencerType, kSequenceType, kRequestType, kResponseType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    root = components.create_root("macro");
  }

  [[nodiscard]] SystemVerilogClassHandle make_object(
      const std::string_view type,
      std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_sequencer(std::string name) {
    const auto result = make_object(kSequencerType, name);
    components.initialize(result, std::move(name), 0, root);
    return result;
  }
};

SystemVerilogUvmSequenceProfile profile() {
  return {std::string{kRequestType}, std::string{kResponseType}};
}

SystemVerilogUvmSequenceDescriptor sequence_prototype(
    const SystemVerilogClassHandle prototype,
    std::string name,
    const SystemVerilogUvmSequencerHandle sequencer,
    SystemVerilogUvmSequenceHooks hooks = {}) {
  return {
      prototype, std::move(name), std::string{kSequenceType}, profile(), {},
      sequencer, std::move(hooks)};
}

SystemVerilogUvmSequenceItemDescriptor item_prototype(
    const SystemVerilogClassHandle prototype,
    std::string name,
    const SystemVerilogUvmSequenceItemRole role,
    const SystemVerilogUvmSequenceHandle owner,
    const SystemVerilogUvmSequencerHandle sequencer) {
  return {
      prototype, std::move(name),
      role == SystemVerilogUvmSequenceItemRole::Request
          ? std::string{kRequestType}
          : std::string{kResponseType},
      role, owner, sequencer};
}

SystemVerilogClassRandomizeRequest constrained_request(
    const std::string_view property,
    const std::uint64_t expected,
    std::string call_identity) {
  SystemVerilogClassRandomizeRequest result;
  result.call_identity = std::move(call_identity);
  result.limits.maximum_domain_values = 32;
  result.inline_constraints = [identity = std::string{property}, expected](
                                  auto& solver, const auto& variables) {
    const auto selected = variables.at(identity);
    solver.add_clause(equal_clause("inline-payload", selected, expected));
  };
  return result;
}

}  // namespace

void test_systemverilog_uvm_sequence_macros() {
  MacroFixture fixture;
  SystemVerilogUvmSequenceService service{
      fixture.heap, fixture.objects, fixture.components};
  const auto sequencer = service.register_sequencer(
      {fixture.make_sequencer("sequencer"), std::string{kSequencerType},
       profile()});
  const auto sequence_prototype_object =
      fixture.make_object(kSequenceType, "sequence_prototype");
  const auto request_prototype_object =
      fixture.make_object(kRequestType, "request_prototype");
  const auto sequence = service.macro_create(sequence_prototype(
      sequence_prototype_object, "macro_sequence", sequencer));
  const auto request_item = service.macro_create(item_prototype(
      request_prototype_object, "macro_item",
      SystemVerilogUvmSequenceItemRole::Request, sequence, sequencer));
  require(
      service.snapshot(sequence).object != sequence_prototype_object
          && service.snapshot(request_item).object != request_prototype_object
          && fixture.objects.contains(service.snapshot(sequence).object)
          && fixture.objects.contains(service.snapshot(request_item).object),
      "macro create must factory-create and register distinct live objects");

  const auto item_request = service.macro_send(request_item, 321);
  require(
      service.request_snapshot(item_request).sequence == sequence
          && service.request_snapshot(item_request).item == request_item
          && service.request_snapshot(item_request).priority == 321,
      "item send must retain the exact request item and sequence association");
  service.cancel_request(item_request);
  const auto sequence_request = service.macro_send(sequence, 123);
  require(
      service.request_snapshot(sequence_request).sequence == sequence
          && !service.request_snapshot(sequence_request).item,
      "sequence send must enqueue the sequence without fabricating an item");
  service.cancel_request(sequence_request);

  const auto response_prototype =
      fixture.make_object(kResponseType, "response_prototype");
  const auto response_item = service.macro_create(item_prototype(
      response_prototype, "macro_response",
      SystemVerilogUvmSequenceItemRole::Response, sequence, sequencer));
  require_error(
      "FSIM-UVM-SEQ-005", [&] { (void)service.macro_send(response_item); },
      "response items must not enter a request macro queue");

  const auto request_object = service.snapshot(request_item).object;
  auto exact = constrained_request(
      property_identity(kRequestType), 7,
      "work::macro_request::randomize@inline");
  const auto exact_result = service.macro_random_send(request_item, exact, 77);
  require(
      exact_result.success()
          && exact_result.status == SystemVerilogUvmSequenceMacroStatus::Queued
          && exact_result.request
          && service.request_snapshot(*exact_result.request).item == request_item
          && fixture.heap.property(request_object, "payload").packed == packed(7)
          && fixture.heap.random_state(request_object, "payload").revision == 1,
      "random-send must solve inline constraints before retaining the request");
  service.cancel_request(*exact_result.request);

  SystemVerilogClassRandomizeRequest replay;
  replay.call_identity = "work::macro_request::randomize@replay";
  replay.limits.maximum_domain_values = 32;
  fixture.heap.reseed_random(request_object, 0x1610'4004ULL);
  const auto replay_first = service.macro_random_send(request_item, replay);
  require(replay_first.request.has_value(),
          "first deterministic macro replay must queue");
  const auto first_value =
      fixture.heap.property(request_object, "payload").packed;
  service.cancel_request(*replay_first.request);
  fixture.heap.reseed_random(request_object, 0x1610'4004ULL);
  const auto replay_second = service.macro_random_send(request_item, replay);
  require(
      replay_second.request
          && fixture.heap.property(request_object, "payload").packed
              == first_value,
      "equal object seed and call identity must replay macro randomization");
  service.cancel_request(*replay_second.request);

  const auto before_failed_value =
      fixture.heap.property(request_object, "payload").packed;
  const auto before_failed_state =
      fixture.heap.random_state(request_object, "payload");
  const auto before_failed_mutations = service.mutation_count();
  auto impossible = constrained_request(
      property_identity(kRequestType), 1,
      "work::macro_request::randomize@unsatisfiable");
  impossible.class_constraints = [identity = property_identity(kRequestType)](
                                     auto& solver, const auto& variables) {
    solver.add_clause(equal_clause(
        "class-conflict", variables.at(identity), 2));
  };
  const auto impossible_result =
      service.macro_random_send(request_item, impossible);
  require(
      !impossible_result.success() && !impossible_result.request
          && impossible_result.randomization.status
              == SystemVerilogConstraintSolveStatus::Unsatisfiable
          && service.requests(sequencer).empty()
          && service.mutation_count() == before_failed_mutations
          && fixture.heap.property(request_object, "payload").packed
              == before_failed_value
          && fixture.heap.random_state(request_object, "payload").revision
              == before_failed_state.revision,
      "unsatisfiable macro randomization must roll back queue and object state");

  SystemVerilogClassRandomizeRequest exhausted;
  exhausted.call_identity = "work::macro_request::randomize@resource";
  exhausted.limits.maximum_domain_values = 1;
  const auto exhausted_result =
      service.macro_random_send(request_item, exhausted);
  require(
      !exhausted_result.success() && !exhausted_result.request
          && exhausted_result.randomization.status
              == SystemVerilogConstraintSolveStatus::ResourceExhausted
          && service.requests(sequencer).empty()
          && fixture.heap.property(request_object, "payload").packed
              == before_failed_value,
      "resource exhaustion must roll back a provisional macro request");

  auto throwing = replay;
  throwing.call_identity = "work::macro_request::randomize@throw";
  throwing.inline_constraints = [](auto&, const auto&) {
    throw std::runtime_error{"macro inline constraint failure"};
  };
  bool threw{};
  try {
    (void)service.macro_random_send(request_item, throwing);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  require(
      threw && service.requests(sequencer).empty()
          && service.mutation_count() == before_failed_mutations
          && fixture.heap.property(request_object, "payload").packed
              == before_failed_value,
      "constraint-construction exceptions must roll back the macro queue");

  std::size_t body_calls{};
  SystemVerilogUvmSequenceHooks hooks;
  hooks.body = [&](const auto executing) {
    require(executing.valid(), "macro-start body must receive a live sequence");
    ++body_calls;
  };
  const auto started = service.macro_create(sequence_prototype(
      sequence_prototype_object, "macro_started", sequencer, std::move(hooks)));
  const auto started_object = service.snapshot(started).object;
  auto start_request = constrained_request(
      property_identity(kSequenceType), 5,
      "work::macro_sequence::randomize@start");
  const auto start_result = service.macro_random_start(started, start_request);
  require(
      start_result.success()
          && start_result.status == SystemVerilogUvmSequenceMacroStatus::Started
          && start_result.execution && start_result.execution->success()
          && body_calls == 1
          && fixture.heap.property(started_object, "payload").packed == packed(5),
      "sequence do/start macro must randomize and execute exact lifecycle hooks");
  auto failed_start = constrained_request(
      property_identity(kSequenceType), 3,
      "work::macro_sequence::randomize@failed-start");
  failed_start.class_constraints = [identity = property_identity(kSequenceType)](
                                       auto& solver, const auto& variables) {
    solver.add_clause(equal_clause(
        "failed-start-conflict", variables.at(identity), 4));
  };
  const auto failed_start_result =
      service.macro_random_start(started, failed_start);
  require(
      !failed_start_result.success() && !failed_start_result.execution
          && body_calls == 1,
      "unsatisfiable sequence macro randomization must not execute callbacks");

  const auto live_before_failed_create = fixture.heap.live_objects();
  require_error(
      "FSIM-UVM-SEQ-004",
      [&] {
        (void)service.macro_create(sequence_prototype(
            sequence_prototype_object, {}, sequencer));
      },
      "macro create must preserve registration validation");
  require(
      fixture.heap.live_objects() == live_before_failed_create,
      "failed macro creation must release its provisional class object");

  SystemVerilogUvmSequenceService foreign{
      fixture.heap, fixture.objects, fixture.components};
  require_error(
      "FSIM-UVM-SEQ-002",
      [&] { (void)foreign.macro_random_send(request_item, replay); },
      "macro adapters must reject items owned by another sequence service");
}

}  // namespace fsim::tests::runtime
