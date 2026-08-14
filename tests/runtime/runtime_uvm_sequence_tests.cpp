// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_context.hpp"
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kSequencerType{"work::sequencer#(32)"};
constexpr std::string_view kSequenceType{"work::sequence#(32)"};
constexpr std::string_view kRequestType{"work::request#(32)"};
constexpr std::string_view kResponseType{"work::response#(32)"};
constexpr std::string_view kWrongType{"work::wrong_object"};

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

SystemVerilogUvmSequenceProfile profile() {
  return {std::string{kRequestType}, std::string{kResponseType}};
}

SystemVerilogClassDescriptor class_descriptor(
    const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
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
  return result;
}

struct SequenceFixture {
  SystemVerilogClassHeap heap{{512, 65'536}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};

  SequenceFixture()
      : objects(
            heap,
            [this](const std::string_view specialization,
                   const std::string_view,
                   const std::string_view) {
              return heap.allocate(class_descriptor(specialization));
            }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}) {
    for (const auto type : {
             kSequencerType, kSequenceType, kRequestType, kResponseType,
             kWrongType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    first_root = components.create_root("first");
    second_root = components.create_root("second");
  }

  [[nodiscard]] SystemVerilogClassHandle make_object(
      const std::string_view type,
      std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_sequencer(
      std::string name,
      const SystemVerilogUvmRootHandle root) {
    const auto result = make_object(kSequencerType, name);
    components.initialize(result, std::move(name), 0, root);
    return result;
  }
};

SystemVerilogUvmSequencerDescriptor sequencer_descriptor(
    const SystemVerilogClassHandle component) {
  return {component, std::string{kSequencerType}, profile()};
}

SystemVerilogUvmSequenceDescriptor sequence_descriptor(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogUvmSequenceHandle parent = {},
    const SystemVerilogUvmSequencerHandle sequencer = {}) {
  SystemVerilogUvmSequenceDescriptor result;
  result.object = object;
  result.name = std::move(name);
  result.nominal_type = std::string{kSequenceType};
  result.profile = profile();
  result.parent = parent;
  result.sequencer = sequencer;
  return result;
}

SystemVerilogUvmSequenceItemDescriptor item_descriptor(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogUvmSequenceItemRole role,
    const SystemVerilogUvmSequenceHandle owner = {},
    const SystemVerilogUvmSequencerHandle sequencer = {}) {
  return {
      object,
      std::move(name),
      role == SystemVerilogUvmSequenceItemRole::Request
          ? std::string{kRequestType}
          : std::string{kResponseType},
      role,
      owner,
      sequencer};
}

}  // namespace

void test_systemverilog_uvm_sequence_identity() {
  static_assert(
      kSystemVerilogUvmOwnershipContract.sequences
      == SystemVerilogUvmStateScope::Simulation);

  SequenceFixture fixture;
  const auto sequencer_component =
      fixture.make_sequencer("sequencer", fixture.first_root);
  const auto peer_component =
      fixture.make_sequencer("sequencer", fixture.second_root);
  SystemVerilogUvmSequenceService sequences{
      fixture.heap, fixture.objects, fixture.components};

  const auto sequencer =
      sequences.register_sequencer(sequencer_descriptor(sequencer_component));
  const auto peer_sequencer =
      sequences.register_sequencer(sequencer_descriptor(peer_component));
  std::size_t callback_calls{};
  auto top_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "top_object"),
      "top_sequence", {}, sequencer);
  const auto callback = [&](const SystemVerilogUvmSequenceHandle) {
    ++callback_calls;
  };
  top_descriptor.hooks = {callback, callback, callback, callback, callback};
  const auto top = sequences.register_sequence(std::move(top_descriptor));
  const auto child = sequences.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "child_object"), "child", top));
  const auto request = sequences.register_item(item_descriptor(
      fixture.make_object(kRequestType, "request_object"), "request",
      SystemVerilogUvmSequenceItemRole::Request, child));
  const auto response = sequences.register_item(item_descriptor(
      fixture.make_object(kResponseType, "response_object"), "response",
      SystemVerilogUvmSequenceItemRole::Response, child));
  const auto peer_top = sequences.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "peer_top_object"),
      "top_sequence", {}, peer_sequencer));

  const auto sequencer_value = sequences.snapshot(sequencer);
  const auto top_value = sequences.snapshot(top);
  const auto child_value = sequences.snapshot(child);
  const auto request_value = sequences.snapshot(request);
  const auto response_value = sequences.snapshot(response);
  require(
      sequencer_value.root == fixture.first_root
          && sequencer_value.full_name == "sequencer"
          && sequencer_value.debug_name == "first:sequencer"
          && sequencer_value.declaration_order == 0
          && sequencer_value.state
              == SystemVerilogUvmSequencerState::Registered
          && top_value.root == fixture.first_root
          && top_value.full_name == "top_sequence"
          && top_value.debug_name == "first:top_sequence"
          && top_value.declaration_order == 2
          && top_value.state == SystemVerilogUvmSequenceState::Created
          && child_value.parent == top && child_value.sequencer == sequencer
          && child_value.root == fixture.first_root
          && child_value.full_name == "top_sequence.child"
          && child_value.debug_name == "first:top_sequence.child"
          && child_value.depth == 1 && child_value.declaration_order == 3
          && top_value.children
              == std::vector<SystemVerilogUvmSequenceHandle>{child}
          && child_value.items
              == std::vector<SystemVerilogUvmSequenceItemHandle>{
                  request, response}
          && request_value.owner_sequence == child
          && request_value.sequencer == sequencer
          && request_value.root == fixture.first_root
          && request_value.full_name == "top_sequence.child.request"
          && request_value.declaration_order == 4
          && request_value.state == SystemVerilogUvmSequenceItemState::Owned
          && response_value.role
              == SystemVerilogUvmSequenceItemRole::Response
          && response_value.declaration_order == 5
          && sequences.snapshot(peer_top).root == fixture.second_root
          && sequences.snapshot(peer_top).declaration_order == 6
          && sequences.sequencers()
              == std::vector<SystemVerilogUvmSequencerHandle>{
                  sequencer, peer_sequencer}
          && sequences.sequences()
              == std::vector<SystemVerilogUvmSequenceHandle>{
                  top, child, peer_top}
          && sequences.items()
              == std::vector<SystemVerilogUvmSequenceItemHandle>{
                  request, response}
          && callback_calls == 0,
      "sequence registration must retain simulation/root ownership, exact "
      "hierarchy, lifecycle, profile, source order, and dormant callbacks");

  SystemVerilogUvmSequenceService foreign{
      fixture.heap, fixture.objects, fixture.components};
  const auto foreign_sequencer = foreign.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto foreign_sequence = foreign.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "foreign_sequence_object"),
      "foreign_sequence", {}, foreign_sequencer));
  const auto foreign_item = foreign.register_item(item_descriptor(
      fixture.make_object(kRequestType, "foreign_item_object"),
      "foreign_item", SystemVerilogUvmSequenceItemRole::Request,
      foreign_sequence));
  require_error(
      "FSIM-UVM-SEQ-002", [&] { (void)sequences.snapshot(foreign_sequencer); },
      "foreign sequencer handles must reject");
  require_error(
      "FSIM-UVM-SEQ-002", [&] { (void)sequences.snapshot(foreign_sequence); },
      "foreign sequence handles must reject");
  require_error(
      "FSIM-UVM-SEQ-002", [&] { (void)sequences.snapshot(foreign_item); },
      "foreign item handles must reject");

  auto mismatched_profile = sequence_descriptor(
      fixture.make_object(kSequenceType, "profile_mismatch"),
      "profile_mismatch", {}, sequencer);
  mismatched_profile.profile.request_type = "work::request#(64)";
  require_error(
      "FSIM-UVM-SEQ-003",
      [&] { (void)sequences.register_sequence(mismatched_profile); },
      "sequence/sequencer profile mismatch must reject");
  auto wrong_type = sequence_descriptor(
      fixture.make_object(kWrongType, "wrong_type"), "wrong_type");
  require_error(
      "FSIM-UVM-SEQ-003",
      [&] { (void)sequences.register_sequence(wrong_type); },
      "nominal sequence type mismatch must reject");
  auto wrong_role = item_descriptor(
      fixture.make_object(kRequestType, "wrong_role"), "wrong_role",
      SystemVerilogUvmSequenceItemRole::Request, child);
  wrong_role.role = SystemVerilogUvmSequenceItemRole::Response;
  require_error(
      "FSIM-UVM-SEQ-003",
      [&] { (void)sequences.register_item(wrong_role); },
      "nominal response profile mismatch must reject");

  const auto failed_mutations = sequences.mutation_count();
  const auto failed_children = sequences.snapshot(top).children;
  require_error(
      "FSIM-UVM-SEQ-004",
      [&] {
        (void)sequences.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "duplicate_child"),
            "child", top));
      },
      "duplicate child sequence names must reject");
  require_error(
      "FSIM-UVM-SEQ-004",
      [&] {
        (void)sequences.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "cross_root_child"),
            "cross_root_child", top, peer_sequencer));
      },
      "child sequences must not change sequencer roots");
  require_error(
      "FSIM-UVM-SEQ-004",
      [&] {
        (void)sequences.register_item(item_descriptor(
            fixture.make_object(kRequestType, "cross_root_item"),
            "cross_root_item", SystemVerilogUvmSequenceItemRole::Request,
            child, peer_sequencer));
      },
      "items must not change owner sequencer roots");
  require(
      sequences.mutation_count() == failed_mutations
          && sequences.snapshot(top).children == failed_children,
      "failed hierarchy construction must not publish partial state");

  require_error(
      "FSIM-UVM-SEQ-005",
      [&] {
        (void)sequences.register_sequence(sequence_descriptor(
            top_value.object, "duplicate_object", {}, sequencer));
      },
      "duplicate object registration must reject");
  require_error(
      "FSIM-UVM-SEQ-005", [&] { sequences.release(top); },
      "parent release with live children must reject");
  require_error(
      "FSIM-UVM-SEQ-005", [&] { sequences.release(sequencer); },
      "sequencer release with live sequences must reject");

  const auto sibling = sequences.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "sibling_object"), "sibling", top));
  require(
      sequences.snapshot(sibling).declaration_order == 7
          && sequences.snapshot(top).children
              == std::vector<SystemVerilogUvmSequenceHandle>{child, sibling},
      "failed registrations must not consume source-order identity");
  const auto stale_item = sequences.register_item(item_descriptor(
      fixture.make_object(kRequestType, "stale_item"), "stale_item",
      SystemVerilogUvmSequenceItemRole::Request));
  sequences.release(stale_item);
  require_error(
      "FSIM-UVM-SEQ-001", [&] { (void)sequences.snapshot(stale_item); },
      "released item generations must reject as stale");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)sequences.snapshot(SystemVerilogUvmSequenceHandle{}); },
      "empty sequence handles must reject");

  SystemVerilogUvmSequenceLimits sequence_limit;
  sequence_limit.maximum_sequences = 1;
  SystemVerilogUvmSequenceService sequence_bounded{
      fixture.heap, fixture.objects, fixture.components, sequence_limit};
  const auto bounded_sequencer = sequence_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  (void)sequence_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "bounded_first"), "bounded_first",
      {}, bounded_sequencer));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)sequence_bounded.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "bounded_second"),
            "bounded_second", {}, bounded_sequencer));
      },
      "sequence count ceiling must reject");
  const auto after_limit = sequence_bounded.register_item(item_descriptor(
      fixture.make_object(kRequestType, "after_limit"), "after_limit",
      SystemVerilogUvmSequenceItemRole::Request));
  require(
      sequence_bounded.snapshot(after_limit).declaration_order == 2,
      "resource rejection must not consume declaration order");

  SystemVerilogUvmSequenceLimits sequencer_limit;
  sequencer_limit.maximum_sequencers = 1;
  SystemVerilogUvmSequenceService sequencer_bounded{
      fixture.heap, fixture.objects, fixture.components, sequencer_limit};
  (void)sequencer_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)sequencer_bounded.register_sequencer(
            sequencer_descriptor(peer_component));
      },
      "sequencer count ceiling must reject");

  SystemVerilogUvmSequenceLimits item_limit;
  item_limit.maximum_items = 1;
  SystemVerilogUvmSequenceService item_bounded{
      fixture.heap, fixture.objects, fixture.components, item_limit};
  (void)item_bounded.register_item(item_descriptor(
      fixture.make_object(kRequestType, "bounded_item_first"),
      "bounded_item_first", SystemVerilogUvmSequenceItemRole::Request));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)item_bounded.register_item(item_descriptor(
            fixture.make_object(kRequestType, "bounded_item_second"),
            "bounded_item_second",
            SystemVerilogUvmSequenceItemRole::Request));
      },
      "item count ceiling must reject");

  SystemVerilogUvmSequenceLimits registration_limit;
  registration_limit.maximum_registrations = 1;
  SystemVerilogUvmSequenceService registration_bounded{
      fixture.heap, fixture.objects, fixture.components, registration_limit};
  (void)registration_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)registration_bounded.register_item(item_descriptor(
            fixture.make_object(kRequestType, "registration_limit"),
            "registration_limit",
            SystemVerilogUvmSequenceItemRole::Request));
      },
      "total registration ceiling must reject");

  SystemVerilogUvmSequenceLimits mutation_limit;
  mutation_limit.maximum_mutations = 1;
  SystemVerilogUvmSequenceService mutation_bounded{
      fixture.heap, fixture.objects, fixture.components, mutation_limit};
  (void)mutation_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { mutation_bounded.release(mutation_bounded.sequencers().front()); },
      "mutation ceiling must reject release without invalidating identity");
  require(mutation_bounded.sequencer_count() == 1,
          "failed release must preserve the live sequencer");

  SystemVerilogUvmSequenceLimits byte_limits;
  byte_limits.maximum_name_bytes = 3;
  byte_limits.maximum_nominal_type_bytes = 8;
  byte_limits.maximum_profile_bytes = 8;
  SystemVerilogUvmSequenceService name_bounded{
      fixture.heap, fixture.objects, fixture.components, byte_limits};
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)name_bounded.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "long_name"), "long"));
      },
      "name byte ceiling must reject");
  auto long_type = sequence_descriptor(
      fixture.make_object(kSequenceType, "long_type"), "ok");
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)name_bounded.register_sequence(long_type); },
      "nominal type byte ceiling must reject");
  auto short_nominal = sequence_descriptor(
      fixture.make_object(kSequenceType, "long_profile"), "ok");
  short_nominal.nominal_type = "short";
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)name_bounded.register_sequence(short_nominal); },
      "profile byte ceiling must reject before nominal object matching");

  SystemVerilogUvmSequenceLimits full_name_limit;
  full_name_limit.maximum_full_name_bytes = 3;
  SystemVerilogUvmSequenceService full_name_bounded{
      fixture.heap, fixture.objects, fixture.components, full_name_limit};
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)full_name_bounded.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "full_name_limit"), "four"));
      },
      "full-name byte ceiling must reject");

  SystemVerilogUvmSequenceLimits hierarchy_limits;
  hierarchy_limits.maximum_depth = 1;
  hierarchy_limits.maximum_children_per_sequence = 1;
  hierarchy_limits.maximum_items_per_sequence = 1;
  SystemVerilogUvmSequenceService hierarchy_bounded{
      fixture.heap, fixture.objects, fixture.components, hierarchy_limits};
  const auto hierarchy_parent = hierarchy_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "hierarchy_parent"), "parent"));
  const auto hierarchy_child = hierarchy_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "hierarchy_child"), "child",
          hierarchy_parent));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)hierarchy_bounded.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "too_deep"), "grandchild",
            hierarchy_child));
      },
      "sequence depth ceiling must reject");
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)hierarchy_bounded.register_sequence(sequence_descriptor(
            fixture.make_object(kSequenceType, "too_many_children"),
            "second_child", hierarchy_parent));
      },
      "child count ceiling must reject");
  (void)hierarchy_bounded.register_item(item_descriptor(
      fixture.make_object(kRequestType, "hierarchy_item"), "first_item",
      SystemVerilogUvmSequenceItemRole::Request, hierarchy_child));
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)hierarchy_bounded.register_item(item_descriptor(
            fixture.make_object(kRequestType, "too_many_items"),
            "second_item", SystemVerilogUvmSequenceItemRole::Request,
            hierarchy_child));
      },
      "owned item count ceiling must reject");

  SystemVerilogUvmSequenceLimits invalid_limits;
  invalid_limits.maximum_items = 0;
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        SystemVerilogUvmSequenceService invalid{
            fixture.heap, fixture.objects, fixture.components, invalid_limits};
      },
      "zero resource ceilings must reject at construction");

  sequences.release(request);
  sequences.release(response);
  sequences.release(child);
  sequences.release(sibling);
  sequences.release(top);
  sequences.release(sequencer);
  sequences.release(peer_top);
  sequences.release(peer_sequencer);
  require(
      sequences.item_count() == 0 && sequences.sequence_count() == 0
          && sequences.sequencer_count() == 0 && sequences.items().empty()
          && sequences.sequences().empty() && sequences.sequencers().empty()
          && callback_calls == 0,
      "leaf-first release must invalidate every generation without executing "
      "registered sequence callbacks");
}

void test_systemverilog_uvm_sequence_execution() {
  SequenceFixture fixture;
  const auto sequencer_component =
      fixture.make_sequencer("sequencer", fixture.first_root);
  const auto peer_component =
      fixture.make_sequencer("sequencer", fixture.second_root);
  Scheduler scheduler;
  SystemVerilogUvmPhaseService phases{fixture.components, scheduler};
  const std::vector<SystemVerilogUvmRootHandle> roots{fixture.first_root};
  const auto schedule = phases.create_standard_schedule(roots);
  const auto run = schedule.phase(SystemVerilogUvmPhaseKind::Run);
  SystemVerilogUvmObjectionService objections{
      fixture.objects, fixture.components, phases, scheduler};
  phases.set_objection_service(objections);
  SystemVerilogUvmSequenceService sequences{
      fixture.heap, fixture.objects, fixture.components, phases, objections};
  const auto sequencer =
      sequences.register_sequencer(sequencer_descriptor(sequencer_component));
  const auto peer_sequencer =
      sequences.register_sequencer(sequencer_descriptor(peer_component));
  const auto peer_sequence = sequences.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "peer_execution_sequence"),
      "peer_execution", {}, peer_sequencer));

  std::vector<std::string> callbacks;
  SystemVerilogUvmSequenceHandle child;
  SystemVerilogUvmSequenceItemHandle owned_response;
  SystemVerilogUvmSequenceExecutionResult child_result;
  auto top_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "execution_top"), "top", {},
      sequencer);
  top_descriptor.hooks.pre_start = [&](const auto) {
    callbacks.emplace_back("top.pre_start");
  };
  top_descriptor.hooks.pre_body = [&](const auto) {
    callbacks.emplace_back("top.pre_body");
  };
  top_descriptor.hooks.body = [&](const auto) {
    callbacks.emplace_back("top.body.begin");
    child_result = sequences.start(child);
    callbacks.emplace_back("top.body.end");
  };
  top_descriptor.hooks.post_body = [&](const auto) {
    callbacks.emplace_back("top.post_body");
  };
  top_descriptor.hooks.post_start = [&](const auto) {
    callbacks.emplace_back("top.post_start");
  };
  const auto top = sequences.register_sequence(std::move(top_descriptor));
  auto child_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "execution_child"), "child", top);
  child_descriptor.hooks.pre_start = [&](const auto) {
    callbacks.emplace_back("child.pre_start");
  };
  child_descriptor.hooks.pre_body = [&](const auto) {
    callbacks.emplace_back("child.pre_body");
  };
  child_descriptor.hooks.body = [&](const auto) {
    callbacks.emplace_back("child.body");
    sequences.route_response(owned_response);
  };
  child_descriptor.hooks.post_body = [&](const auto) {
    callbacks.emplace_back("child.post_body");
  };
  child_descriptor.hooks.post_start = [&](const auto) {
    callbacks.emplace_back("child.post_start");
  };
  child = sequences.register_sequence(std::move(child_descriptor));
  owned_response = sequences.register_item(item_descriptor(
      fixture.make_object(kResponseType, "execution_response"), "response",
      SystemVerilogUvmSequenceItemRole::Response, child));

  auto stopped_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "stopped_sequence"), "stopped", {},
      sequencer);
  stopped_descriptor.hooks.pre_start = [&](const auto) {
    callbacks.emplace_back("stopped.pre_start");
  };
  stopped_descriptor.hooks.body = [&](const auto handle) {
    callbacks.emplace_back("stopped.body");
    sequences.request_stop(handle);
  };
  stopped_descriptor.hooks.post_body = [&](const auto) {
    callbacks.emplace_back("stopped.post_body");
  };
  const auto stopped =
      sequences.register_sequence(std::move(stopped_descriptor));
  const auto stopped_result = sequences.start(stopped);
  require(
      stopped_result.stopped && !stopped_result.killed
          && stopped_result.final_state == SystemVerilogUvmSequenceState::Stopped
          && std::ranges::find(callbacks, "stopped.post_body")
              == callbacks.end(),
      "cooperative stop must terminate after the active callback without "
      "executing later lifecycle hooks");
  require_error(
      "FSIM-UVM-SEQ-005", [&] { sequences.request_stop(stopped); },
      "inactive sequences must reject stop requests");

  auto failing_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "failing_sequence"), "failing", {},
      sequencer);
  failing_descriptor.hooks.body = [](const auto) {
    throw std::runtime_error{"contained sequence body failure"};
  };
  const auto failing =
      sequences.register_sequence(std::move(failing_descriptor));
  const auto failing_result = sequences.start(failing);
  require(
      failing_result.failures.size() == 1
          && failing_result.failures.front().diagnostic_code
              == "FSIM-UVM-SEQ-007"
          && failing_result.failures.front().callback
              == SystemVerilogUvmSequenceCallbackKind::Body
          && failing_result.failures.front().message
              == "contained sequence body failure"
          && failing_result.final_state
              == SystemVerilogUvmSequenceState::Stopped,
      "sequence callback exceptions must be contained with exact callback "
      "identity and deterministic stopped state");

  auto reusable_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "reusable_sequence"), "reusable", {},
      sequencer);
  const auto reusable =
      sequences.register_sequence(std::move(reusable_descriptor));
  require(
      sequences.start(reusable).success()
          && sequences.start(reusable).success()
          && sequences.snapshot(reusable).execution_count == 2,
      "finished sequence objects must restart with independent lifecycle runs");

  SystemVerilogUvmSequenceHandle kill_top;
  SystemVerilogUvmSequenceHandle kill_child;
  SystemVerilogUvmSequenceExecutionResult kill_child_result;
  auto kill_top_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "kill_top"), "kill_top", {},
      sequencer);
  kill_top_descriptor.hooks.body = [&](const auto) {
    callbacks.emplace_back("kill_top.body.begin");
    kill_child_result = sequences.start(kill_child);
    callbacks.emplace_back("kill_top.body.end");
  };
  kill_top_descriptor.hooks.post_body = [&](const auto) {
    callbacks.emplace_back("kill_top.post_body");
  };
  kill_top = sequences.register_sequence(std::move(kill_top_descriptor));
  auto kill_child_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "kill_child"), "kill_child",
      kill_top);
  kill_child_descriptor.hooks.body = [&](const auto) {
    callbacks.emplace_back("kill_child.body");
    sequences.kill(kill_top);
  };
  kill_child = sequences.register_sequence(std::move(kill_child_descriptor));

  SystemVerilogUvmSequenceExecutionResult top_result;
  SystemVerilogUvmSequenceExecutionResult kill_top_result;
  SystemVerilogUvmPhaseProcessHandle component_process;
  bool cross_root_process_rejected{};
  const auto objection_source = objections.bind_source(
      sequences.snapshot(top).object, fixture.first_root);
  callbacks.clear();
  const auto phase_result = phases.execute_task_phase(
      run,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto phase, const auto process) {
        require(
            component == sequencer_component,
            "sequence task phase must execute on its owning component");
        component_process = process;
        try {
          (void)sequences.start(
              peer_sequence,
              {phase, process, true, false});
        } catch (const SystemVerilogUvmSequenceError& error) {
          cross_root_process_rejected =
              error.diagnostic_code() == "FSIM-UVM-SEQ-005";
        }
        top_result = sequences.start(
            top,
            {phase, process, true, true});
        require(
            objections.source_count(phase, objection_source) == 0,
            "automatic sequence objection must drop before start returns");
        kill_top_result = sequences.start(
            kill_top,
            {phase, process, true, true});
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });

  const std::vector<std::string> expected_nested{
      "top.pre_start", "top.pre_body", "top.body.begin",
      "child.pre_start", "child.pre_body", "child.body",
      "child.post_body", "child.post_start", "top.body.end",
      "top.post_body", "top.post_start"};
  require(
      top_result.success() && child_result.success()
          && std::ranges::equal(
              callbacks.begin(),
              callbacks.begin()
                  + static_cast<std::ptrdiff_t>(expected_nested.size()),
              expected_nested.begin(), expected_nested.end())
          && sequences.snapshot(top).state
              == SystemVerilogUvmSequenceState::Finished
          && sequences.snapshot(child).state
              == SystemVerilogUvmSequenceState::Finished
          && top_result.events.size() == 7 && child_result.events.size() == 7
          && objections.source_count(run, objection_source) == 0
          && std::ranges::any_of(objections.trace(), [&](const auto& event) {
               return event.kind == SystemVerilogUvmObjectionEventKind::Raised
                   && event.source == objection_source;
             })
          && std::ranges::any_of(objections.trace(), [&](const auto& event) {
               return event.kind == SystemVerilogUvmObjectionEventKind::Dropped
                   && event.source == objection_source;
             })
          && cross_root_process_rejected,
      "sequence execution must preserve exact nested callback order, every "
      "normal lifecycle state, and balanced automatic objections");

  const auto process_snapshot = [&](const auto handle) {
    const auto found = std::ranges::find(
        phase_result.final_processes, handle,
        &SystemVerilogUvmPhaseProcessSnapshot::handle);
    require(
        found != phase_result.final_processes.end(),
        "sequence phase process must be retained in the final phase result");
    return *found;
  };
  const auto top_process = process_snapshot(sequences.snapshot(top).process);
  const auto child_process =
      process_snapshot(sequences.snapshot(child).process);
  const auto killed_top_process =
      process_snapshot(sequences.snapshot(kill_top).process);
  const auto killed_child_process =
      process_snapshot(sequences.snapshot(kill_child).process);
  require(
      top_process.parent
              == std::optional<SystemVerilogUvmPhaseProcessHandle>{
                  component_process}
          && child_process.parent
              == std::optional<SystemVerilogUvmPhaseProcessHandle>{
                  top_process.handle}
          && top_process.state
              == SystemVerilogUvmPhaseProcessState::Completed
          && child_process.state
              == SystemVerilogUvmPhaseProcessState::Completed
          && kill_top_result.killed && kill_child_result.killed
          && kill_top_result.final_state
              == SystemVerilogUvmSequenceState::Stopped
          && kill_child_result.final_state
              == SystemVerilogUvmSequenceState::Stopped
          && killed_top_process.state
              == SystemVerilogUvmPhaseProcessState::Cancelled
          && killed_child_process.state
              == SystemVerilogUvmPhaseProcessState::Cancelled
          && std::ranges::find(callbacks, "kill_top.post_body")
              == callbacks.end()
          && phase_result.final_state == SystemVerilogUvmPhaseState::Done,
      "sequence process trees must retain exact phase parentage, normal "
      "completion, recursive kill cancellation, and phase completion");

  require(
      sequences.snapshot(owned_response).state
              == SystemVerilogUvmSequenceItemState::Routed
          && sequences.responses(child)
              == std::vector<SystemVerilogUvmSequenceItemHandle>{
                  owned_response}
          && sequences.pop_response(child)
              == std::optional<SystemVerilogUvmSequenceItemHandle>{
                  owned_response}
          && sequences.snapshot(owned_response).state
              == SystemVerilogUvmSequenceItemState::Owned
          && !sequences.pop_response(child),
      "responses must route to and pop from their owning sequence in FIFO order");
  require_error(
      "FSIM-UVM-SEQ-005",
      [&] {
        const auto request = sequences.register_item(item_descriptor(
            fixture.make_object(kRequestType, "not_response"), "not_response",
            SystemVerilogUvmSequenceItemRole::Request, child));
        sequences.route_response(request);
      },
      "request items must reject response routing");
  require_error(
      "FSIM-UVM-SEQ-005", [&] { (void)sequences.start(child); },
      "registered child sequences must reject execution outside parent body");
  require_error(
      "FSIM-UVM-SEQ-005", [&] { sequences.kill(reusable); },
      "inactive sequences must reject kill requests");
  require_error(
      "FSIM-UVM-SEQ-005",
      [&] {
        (void)sequences.start(
            reusable, {{}, {}, true, true});
      },
      "automatic objections without an executing phase must reject");

  SystemVerilogUvmSequenceLimits event_limits;
  event_limits.maximum_execution_events = 7;
  SystemVerilogUvmSequenceService event_bounded{
      fixture.heap, fixture.objects, fixture.components, event_limits};
  const auto event_sequencer = event_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto event_first = event_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "event_first"), "event_first", {},
      event_sequencer));
  const auto event_second = event_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "event_second"), "event_second",
          {}, event_sequencer));
  require(event_bounded.start(event_first).success(),
          "one complete execution must fit an exact seven-event ceiling");
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)event_bounded.start(event_second); },
      "execution-event retention ceiling must reject before lifecycle mutation");
  require(
      event_bounded.snapshot(event_second).state
              == SystemVerilogUvmSequenceState::Created
          && event_bounded.snapshot(event_second).execution_count == 0,
      "event-limit rejection must preserve the candidate sequence state");

  SystemVerilogUvmSequenceLimits active_limits;
  active_limits.maximum_active_executions = 1;
  SystemVerilogUvmSequenceService active_bounded{
      fixture.heap, fixture.objects, fixture.components, active_limits};
  const auto active_sequencer = active_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  SystemVerilogUvmSequenceHandle active_child;
  bool active_limit_rejected{};
  auto active_parent_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "active_parent"), "active_parent",
      {}, active_sequencer);
  active_parent_descriptor.hooks.body = [&](const auto) {
    try {
      (void)active_bounded.start(active_child);
    } catch (const SystemVerilogUvmSequenceError& error) {
      active_limit_rejected =
          error.diagnostic_code() == "FSIM-UVM-SEQ-006";
    }
  };
  const auto active_parent = active_bounded.register_sequence(
      std::move(active_parent_descriptor));
  active_child = active_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "active_child"), "active_child",
      active_parent));
  require(
      active_bounded.start(active_parent).success() && active_limit_rejected
          && active_bounded.snapshot(active_child).state
              == SystemVerilogUvmSequenceState::Created,
      "active-execution ceiling must reject nested start transactionally");

  SystemVerilogUvmSequenceLimits response_limits;
  response_limits.maximum_responses_per_sequence = 1;
  SystemVerilogUvmSequenceService response_bounded{
      fixture.heap, fixture.objects, fixture.components, response_limits};
  const auto response_sequencer = response_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto response_owner = response_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "response_owner"),
          "response_owner", {}, response_sequencer));
  const auto first_response = response_bounded.register_item(item_descriptor(
      fixture.make_object(kResponseType, "first_response"), "first_response",
      SystemVerilogUvmSequenceItemRole::Response, response_owner));
  const auto second_response = response_bounded.register_item(item_descriptor(
      fixture.make_object(kResponseType, "second_response"),
      "second_response", SystemVerilogUvmSequenceItemRole::Response,
      response_owner));
  response_bounded.route_response(first_response);
  require_error(
      "FSIM-UVM-SEQ-009",
      [&] { response_bounded.route_response(second_response); },
      "response overflow policy must reject without replacing the first response");
  require(
      response_bounded.responses(response_owner)
          == std::vector<SystemVerilogUvmSequenceItemHandle>{first_response},
      "response-limit rejection must preserve FIFO contents");

  SystemVerilogUvmSequenceLimits failure_limits;
  failure_limits.maximum_execution_failures = 1;
  SystemVerilogUvmSequenceService failure_bounded{
      fixture.heap, fixture.objects, fixture.components, failure_limits};
  const auto failure_sequencer = failure_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  auto first_failure_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "first_failure"), "first_failure",
      {}, failure_sequencer);
  first_failure_descriptor.hooks.body = [](const auto) {
    throw std::runtime_error{"first retained failure"};
  };
  const auto first_failure = failure_bounded.register_sequence(
      std::move(first_failure_descriptor));
  const auto second_failure = failure_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "second_failure"),
          "second_failure", {}, failure_sequencer));
  require(
      failure_bounded.start(first_failure).failures.size() == 1,
      "one callback failure must fill the exact retained-failure ceiling");
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)failure_bounded.start(second_failure); },
      "retained execution-failure ceiling must reject the next start");

  const auto trace = sequences.execution_events();
  require(
      !trace.empty()
          && std::ranges::is_sorted(
              trace, std::ranges::less{},
              &SystemVerilogUvmSequenceExecutionEvent::order)
          && sequences.execution_failures().size() == 1,
      "sequence execution trace and contained failures must retain one global "
      "deterministic order");
}

void test_systemverilog_uvm_sequence_arbitration() {
  SequenceFixture fixture;
  const auto sequencer_component =
      fixture.make_sequencer("sequencer", fixture.first_root);
  const auto peer_component =
      fixture.make_sequencer("sequencer", fixture.second_root);
  SystemVerilogUvmSequenceService service{
      fixture.heap, fixture.objects, fixture.components};
  const auto sequencer =
      service.register_sequencer(sequencer_descriptor(sequencer_component));
  const auto peer_sequencer =
      service.register_sequencer(sequencer_descriptor(peer_component));
  const auto first = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "arb_first"), "first", {},
      sequencer));
  const auto second = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "arb_second"), "second", {},
      sequencer));
  const auto third = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "arb_third"), "third", {},
      sequencer));
  const auto peer_first = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "peer_arb_first"), "first", {},
      peer_sequencer));
  const auto peer_second = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "peer_arb_second"), "second", {},
      peer_sequencer));
  const auto peer_third = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "peer_arb_third"), "third", {},
      peer_sequencer));

  const auto enqueue = [&](const auto sequence, const std::uint32_t priority,
                           const bool relevant = true) {
    return service.enqueue_request(
        {sequence, priority, relevant, {}});
  };
  const auto select = [&] {
    const auto result = service.select_request(sequencer);
    require(
        result.status == SystemVerilogUvmSequenceSelectionStatus::Selected
            && result.request,
        "arbitration must select one relevant request");
    return *result.request;
  };
  const auto drain = [&] {
    const auto pending = service.requests(sequencer);
    for (const auto& request : pending) service.cancel_request(request);
  };

  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::Fifo, 11);
  const auto fifo_first = enqueue(first, 1);
  const auto fifo_second = enqueue(second, 100);
  const auto fifo_third = enqueue(third, 50);
  require(
      service.request_snapshot(fifo_first).queue_order
              < service.request_snapshot(fifo_second).queue_order
          && service.request_snapshot(fifo_second).queue_order
              < service.request_snapshot(fifo_third).queue_order
          && select().sequence == first && select().sequence == second
          && select().sequence == third
          && service.select_request(sequencer).status
              == SystemVerilogUvmSequenceSelectionStatus::Empty,
      "FIFO arbitration must ignore priority and retain source-order ties");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.request_snapshot(fifo_first); },
      "selected request generations must become stale");

  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::StrictFifo, 12);
  (void)enqueue(first, 10);
  (void)enqueue(second, 30);
  (void)enqueue(third, 30);
  require(
      select().sequence == second && select().sequence == third
          && select().sequence == first,
      "strict FIFO must choose maximum priority and break ties by queue order");

  bool dynamic_relevance{};
  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::Fifo, 13);
  const auto irrelevant = enqueue(first, 100, false);
  const auto dynamic = service.enqueue_request(
      {second, 100, true, [&](const auto sequence) {
         require(sequence == second,
                 "relevance callback must receive its owning sequence");
         return dynamic_relevance;
       }});
  require(
      service.select_request(sequencer).status
              == SystemVerilogUvmSequenceSelectionStatus::WaitingForRelevant
          && service.snapshot(sequencer).relevance_waits == 1
          && service.requests(sequencer)
              == std::vector<SystemVerilogUvmSequenceRequestHandle>{
                  irrelevant, dynamic},
      "an all-irrelevant queue must wait without consuming requests");
  service.set_request_relevant(irrelevant, true);
  require(
      select().sequence == first
          && service.snapshot(sequencer).relevance_waits == 0,
      "explicit relevance must wake selection and reset the wait count");
  dynamic_relevance = true;
  require(select().sequence == second,
          "dynamic relevance must admit a request without re-enqueueing");
  const auto throwing_relevance = service.enqueue_request(
      {first, 100, true, [](const auto) -> bool {
         throw std::runtime_error{"relevance failure"};
       }});
  require_error(
      "FSIM-UVM-SEQ-008", [&] { (void)service.select_request(sequencer); },
      "throwing relevance callbacks must reject with a cataloged diagnostic");
  require(
      service.requests(sequencer)
          == std::vector<SystemVerilogUvmSequenceRequestHandle>{
              throwing_relevance},
      "relevance callback failure must preserve request identity and order");
  service.cancel_request(throwing_relevance);

  const auto random_run = [&] {
    std::vector<SystemVerilogUvmSequenceHandle> result;
    (void)enqueue(first, 1);
    (void)enqueue(second, 1);
    (void)enqueue(third, 1);
    for (std::size_t iteration = 0; iteration < 24; ++iteration) {
      const auto chosen = select().sequence;
      result.push_back(chosen);
      (void)enqueue(chosen, 1);
    }
    drain();
    return result;
  };
  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::Random,
      0x1610'0003ULL);
  const auto random_first = random_run();
  const auto random_draws = service.snapshot(sequencer).random_draws;
  service.reseed_arbitration(sequencer, 0x1610'0003ULL);
  const auto random_second = random_run();
  require(
      random_first == random_second && random_draws >= random_first.size()
          && service.snapshot(sequencer).random_draws >= random_second.size(),
      "random arbitration must reproduce exactly after explicit reseeding");
  const auto main_draws_before_peer = service.snapshot(sequencer).random_draws;
  service.configure_arbitration(
      peer_sequencer, SystemVerilogUvmSequenceArbitrationMode::Random,
      0x1610'0003ULL);
  (void)service.enqueue_request({peer_first, 1, true, {}});
  (void)service.enqueue_request({peer_second, 1, true, {}});
  (void)service.enqueue_request({peer_third, 1, true, {}});
  std::vector<std::uint8_t> peer_random;
  for (std::size_t iteration = 0; iteration < random_first.size(); ++iteration) {
    const auto result = service.select_request(peer_sequencer);
    require(
        result.request.has_value(),
        "peer random arbitration must select a request");
    const auto chosen = result.request->sequence;
    const auto index = chosen == peer_first ? 0U : (chosen == peer_second ? 1U : 2U);
    peer_random.push_back(static_cast<std::uint8_t>(index));
    (void)service.enqueue_request({chosen, 1, true, {}});
  }
  std::vector<std::uint8_t> main_random;
  for (const auto& chosen : random_first) {
    const auto index = chosen == first ? 0U : (chosen == second ? 1U : 2U);
    main_random.push_back(static_cast<std::uint8_t>(index));
  }
  require(
      peer_random == main_random
          && service.snapshot(sequencer).random_draws == main_draws_before_peer,
      "equal seeds must reproduce across independent sequencer streams without "
      "cross-stream draw consumption");
  for (const auto& pending : service.requests(peer_sequencer)) {
    service.cancel_request(pending);
  }

  const auto strict_random_run = [&] {
    (void)enqueue(first, 10);
    (void)enqueue(second, 30);
    (void)enqueue(third, 30);
    const auto chosen = select().sequence;
    drain();
    return chosen;
  };
  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::StrictRandom,
      0x1610'1003ULL);
  const auto strict_random_first = strict_random_run();
  service.reseed_arbitration(sequencer, 0x1610'1003ULL);
  const auto strict_random_second = strict_random_run();
  require(
      strict_random_first == strict_random_second
          && (strict_random_first == second || strict_random_first == third),
      "strict random must exclude lower priorities and reproduce tied choice");

  const auto weighted_run = [&] {
    std::vector<SystemVerilogUvmSequenceHandle> result;
    const auto priority = [&](const auto sequence) -> std::uint32_t {
      if (sequence == first) return 1;
      if (sequence == second) return 4;
      return 64;
    };
    (void)enqueue(first, priority(first));
    (void)enqueue(second, priority(second));
    (void)enqueue(third, priority(third));
    for (std::size_t iteration = 0; iteration < 64; ++iteration) {
      const auto chosen = select().sequence;
      result.push_back(chosen);
      (void)enqueue(chosen, priority(chosen));
    }
    drain();
    return result;
  };
  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::Weighted,
      0x1610'2003ULL);
  const auto weighted_first = weighted_run();
  service.reseed_arbitration(sequencer, 0x1610'2003ULL);
  const auto weighted_second = weighted_run();
  require(
      weighted_first == weighted_second
          && std::ranges::count(weighted_first, third)
              > std::ranges::count(weighted_first, second)
          && std::ranges::count(weighted_first, second)
              >= std::ranges::count(weighted_first, first),
      "weighted arbitration must be deterministic and honor priority weights");

  service.configure_arbitration(
      sequencer, SystemVerilogUvmSequenceArbitrationMode::User, 17);
  std::vector<SystemVerilogUvmSequenceRequestSnapshot> user_candidates;
  service.set_user_arbitration(
      sequencer,
      [&](const std::span<const SystemVerilogUvmSequenceRequestSnapshot>
              candidates) {
        user_candidates.assign(candidates.begin(), candidates.end());
        return candidates.back().handle;
      });
  (void)enqueue(first, 100);
  (void)enqueue(second, 100, false);
  (void)enqueue(third, 100);
  require(
      select().sequence == third && user_candidates.size() == 2
          && user_candidates.front().sequence == first
          && user_candidates.back().sequence == third,
      "user arbitration must receive only relevant candidates in stable order");
  drain();
  service.set_user_arbitration(
      sequencer,
      [](const auto) -> SystemVerilogUvmSequenceRequestHandle {
        throw std::runtime_error{"user arbitration failure"};
      });
  (void)enqueue(first, 100);
  const auto before_user_failure = service.requests(sequencer);
  require_error(
      "FSIM-UVM-SEQ-008", [&] { (void)service.select_request(sequencer); },
      "throwing user arbitration must reject with a cataloged diagnostic");
  require(
      service.requests(sequencer) == before_user_failure,
      "user arbitration failure must preserve the complete queue");
  drain();

  require_error(
      "FSIM-UVM-SEQ-008",
      [&] {
        (void)service.enqueue_request({first, 0, true, {}});
      },
      "zero request priority must reject");
  require_error(
      "FSIM-UVM-SEQ-008",
      [&] {
        service.configure_arbitration(
            sequencer,
            static_cast<SystemVerilogUvmSequenceArbitrationMode>(255), 1);
      },
      "unknown arbitration modes must reject");
  SystemVerilogUvmSequenceService foreign{
      fixture.heap, fixture.objects, fixture.components};
  const auto foreign_sequencer = foreign.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto foreign_sequence = foreign.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "foreign_arb"), "foreign_arb", {},
      foreign_sequencer));
  const auto foreign_request = foreign.enqueue_request(
      {foreign_sequence, 100, true, {}});
  require_error(
      "FSIM-UVM-SEQ-002",
      [&] { (void)service.request_snapshot(foreign_request); },
      "arbitration request handles must reject across simulations");

  SystemVerilogUvmSequenceLimits wait_limits;
  wait_limits.maximum_relevance_waits = 1;
  SystemVerilogUvmSequenceService wait_bounded{
      fixture.heap, fixture.objects, fixture.components, wait_limits};
  const auto wait_sequencer = wait_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto wait_sequence = wait_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "wait_bounded"), "wait_bounded",
          {}, wait_sequencer));
  (void)wait_bounded.enqueue_request({wait_sequence, 100, false, {}});
  require(
      wait_bounded.select_request(wait_sequencer).status
          == SystemVerilogUvmSequenceSelectionStatus::WaitingForRelevant,
      "first bounded relevance wait must remain pending");
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)wait_bounded.select_request(wait_sequencer); },
      "wait-for-relevant ceiling must bound starvation");

  SystemVerilogUvmSequenceLimits work_limits;
  work_limits.maximum_arbitration_work = 1;
  SystemVerilogUvmSequenceService work_bounded{
      fixture.heap, fixture.objects, fixture.components, work_limits};
  const auto work_sequencer = work_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto work_first = work_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "work_first"), "work_first", {},
      work_sequencer));
  const auto work_second = work_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "work_second"), "work_second", {},
      work_sequencer));
  (void)work_bounded.enqueue_request({work_first, 100, true, {}});
  (void)work_bounded.enqueue_request({work_second, 100, true, {}});
  const auto before_work_failure = work_bounded.requests(work_sequencer);
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)work_bounded.select_request(work_sequencer); },
      "arbitration work ceiling must reject an oversized scan");
  require(
      work_bounded.requests(work_sequencer) == before_work_failure,
      "selection-work rejection must preserve queue order and identity");

  SystemVerilogUvmSequenceLimits queue_limits;
  queue_limits.maximum_pending_requests = 1;
  queue_limits.maximum_requests_per_sequencer = 1;
  SystemVerilogUvmSequenceService queue_bounded{
      fixture.heap, fixture.objects, fixture.components, queue_limits};
  const auto queue_sequencer = queue_bounded.register_sequencer(
      sequencer_descriptor(sequencer_component));
  const auto queue_first = queue_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "queue_first"), "queue_first", {},
      queue_sequencer));
  const auto queue_second = queue_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "queue_second"), "queue_second",
          {}, queue_sequencer));
  (void)queue_bounded.enqueue_request({queue_first, 100, true, {}});
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)queue_bounded.enqueue_request(
            {queue_second, 100, true, {}});
      },
      "pending and per-sequencer request ceilings must reject atomically");
}

}  // namespace fsim::tests::runtime
