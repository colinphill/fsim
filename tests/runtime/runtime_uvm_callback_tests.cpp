// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_callback.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kComponentType{"work::callback_component"};
constexpr std::string_view kObjectType{"work::callback_object"};

void require_callback(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

void require_callback_error(const std::string_view code,
                            const std::function<void()> &operation,
                            const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmCallbackError &error) {
    require_callback(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

void require_transaction_error(const std::string_view code,
                               const std::function<void()> &operation,
                               const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmTransactionError &error) {
    require_callback(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

SystemVerilogClassDescriptor
class_descriptor(const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.declared_type = specialization == kComponentType
                             ? "uvm_pkg::uvm_component"
                             : "uvm_pkg::uvm_object";
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
  result.assignable_declared_types = {std::string{specialization},
                                      "uvm_pkg::uvm_object"};
  if (specialization == kComponentType) {
    result.assignable_declared_types.push_back("uvm_pkg::uvm_component");
  } else {
    result.properties = {
        {"value", SystemVerilogClassPropertyKind::Logic4, 8},
        {"label", SystemVerilogClassPropertyKind::String, 0}};
  }
  return result;
}

struct CallbackFixture {
  Scheduler scheduler;
  SystemVerilogClassHeap heap{{128, 4'096}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmActivityService activity;
  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};

  CallbackFixture()
      : objects(heap,
                [this](const std::string_view specialization,
                       const std::string_view, const std::string_view) {
                  return heap.allocate(class_descriptor(specialization));
                }),
        components(heap, objects), activity(scheduler) {
    for (const auto type : {kComponentType, kObjectType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      if (type == kObjectType) {
        descriptor.fields = {
            {"value", SystemVerilogUvmFieldFlag::None},
            {"label", SystemVerilogUvmFieldFlag::None}};
      }
      objects.register_type(std::move(descriptor));
    }
    first_root = components.create_root("first");
    second_root = components.create_root("second");
  }

  [[nodiscard]] SystemVerilogClassHandle
  make_object(const std::string_view type, std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle
  make_component(std::string name, const SystemVerilogUvmRootHandle root) {
    const auto result = make_object(kComponentType, name);
    components.initialize(result, std::move(name), 0, root);
    return result;
  }
};

} // namespace

void test_systemverilog_uvm_callbacks_and_transactions() {
  CallbackFixture fixture;
  const auto first = fixture.make_component("first", fixture.first_root);
  const auto peer = fixture.make_component("peer", fixture.first_root);
  const auto isolated = fixture.make_component("isolated", fixture.second_root);
  SystemVerilogUvmCallbackService callbacks{fixture.objects,
                                            fixture.components};
  callbacks.set_activity_service(fixture.activity);

  std::vector<std::string> order;
  bool mutate_registry{true};
  SystemVerilogUvmCallbackHandle instance;
  SystemVerilogUvmCallbackHandle future;
  (void)callbacks.add(
      {std::string{kComponentType}, "driver_callback", "append", 0, UINT64_C(1),
       SystemVerilogUvmCallbackOrdering::Append, [&](auto &invocation) {
         order.push_back("append:" + std::to_string(invocation.depth));
         invocation.attributes["append"] = "seen";
       }});
  instance = callbacks.add({std::string{kComponentType}, "driver_callback",
                            "instance", first, UINT64_C(1),
                            SystemVerilogUvmCallbackOrdering::Append,
                            [&](auto &) { order.emplace_back("instance"); }});
  const auto mutator = callbacks.add(
      {std::string{kComponentType}, "driver_callback", "mutator", 0,
       UINT64_C(1), SystemVerilogUvmCallbackOrdering::Prepend, [&](auto &) {
         order.emplace_back("mutator");
         if (!mutate_registry)
           return;
         mutate_registry = false;
         callbacks.remove(instance);
         future = callbacks.add(
             {std::string{kComponentType}, "driver_callback", "future", 0,
              UINT64_C(1), SystemVerilogUvmCallbackOrdering::Append,
              [&](auto &) { order.emplace_back("future"); }});
       }});
  const auto masked =
      callbacks.add({std::string{kComponentType}, "driver_callback", "end_only",
                     0, UINT64_C(2), SystemVerilogUvmCallbackOrdering::Append,
                     [&](auto &) { order.emplace_back("end"); }});
  const auto throwing =
      callbacks.add({std::string{kComponentType}, "driver_callback", "throwing",
                     0, UINT64_C(1), SystemVerilogUvmCallbackOrdering::Append,
                     [&](auto &invocation) {
                       invocation.attributes["leak"] = "must-roll-back";
                       throw std::runtime_error{"contained callback"};
                     }});

  const auto first_dispatch = callbacks.dispatch(
      first, "driver_callback", UINT64_C(1), "drive", {{"seed", "7"}});
  require_callback(
      order == std::vector<std::string>{"mutator", "append:1"} &&
          first_dispatch.invoked.size() == 3 &&
          first_dispatch.contained_failures == 1 &&
          first_dispatch.invocation.attributes.at("append") == "seen" &&
          !first_dispatch.invocation.attributes.contains("leak") &&
          callbacks.failures().size() == 1 &&
          callbacks.failures().front().callback.handle == throwing,
      "UVM callback dispatch must freeze order, observe deletion before a "
      "turn, defer additions, roll back a throwing callback, and continue");
  require_callback_error(
      "FSIM-UVM-CALLBACK-001", [&] { (void)callbacks.snapshot(instance); },
      "removed UVM callback handles must become stale");

  order.clear();
  const auto second_dispatch =
      callbacks.dispatch(first, "driver_callback", UINT64_C(1), "drive");
  require_callback(
      order == std::vector<std::string>{"mutator", "append:1", "future"} &&
          second_dispatch.invoked.size() == 4 &&
          second_dispatch.contained_failures == 1,
      "deferred UVM callback additions must join the next dispatch in exact "
      "registration order");
  order.clear();
  (void)callbacks.dispatch(first, "driver_callback", UINT64_C(2), "end");
  require_callback(
      order == std::vector<std::string>{"end"},
      "UVM callback masks must select only intersecting registrations");
  callbacks.set_mask(masked, 0);
  order.clear();
  (void)callbacks.dispatch(first, "driver_callback", UINT64_C(2), "end");
  require_callback(order.empty(),
                   "a zero callback mask must disable a registration");
  callbacks.set_mask(masked, UINT64_C(2));

  order.clear();
  (void)callbacks.dispatch(peer, "driver_callback", UINT64_C(1), "peer");
  require_callback(
      std::ranges::find(order, "instance") == order.end() &&
          std::ranges::find(order, "future") != order.end(),
      "type-wide callbacks must apply to peer instances while instance "
      "callbacks remain exact");

  SystemVerilogUvmCallbackLimits one_depth_limits;
  one_depth_limits.maximum_reentry_depth = 1;
  SystemVerilogUvmCallbackService one_depth{fixture.objects, fixture.components,
                                            one_depth_limits};
  (void)one_depth.add(
      {std::string{kComponentType}, "recursive", "recursive", 0, UINT64_C(1),
       SystemVerilogUvmCallbackOrdering::Append, [&](auto &) {
         (void)one_depth.dispatch(first, "recursive", UINT64_C(1));
       }});
  const auto bounded_reentry =
      one_depth.dispatch(first, "recursive", UINT64_C(1));
  require_callback(
      bounded_reentry.contained_failures == 1 &&
          one_depth.failures().size() == 1,
      "UVM callback re-entry ceilings must fail inside and remain contained "
      "by the outer callback dispatch");

  SystemVerilogUvmCallbackService foreign_callbacks{fixture.objects,
                                                    fixture.components};
  require_callback_error(
      "FSIM-UVM-CALLBACK-001", [&] { foreign_callbacks.remove(mutator); },
      "UVM callback handles must reject cross-service ownership");
  SystemVerilogUvmCallbackLimits callback_count_limits;
  callback_count_limits.maximum_callbacks = 1;
  SystemVerilogUvmCallbackService callback_count{
      fixture.objects, fixture.components, callback_count_limits};
  (void)callback_count.add(
      {std::string{kComponentType}, "bounded", "first", 0, UINT64_C(1),
       SystemVerilogUvmCallbackOrdering::Append, [](auto &) {}});
  require_callback_error(
      "FSIM-UVM-CALLBACK-002",
      [&] {
        (void)callback_count.add(
            {std::string{kComponentType}, "bounded", "second", 0, UINT64_C(1),
             SystemVerilogUvmCallbackOrdering::Append, [](auto &) {}});
      },
      "UVM callback count ceilings must reject transactionally");

  const std::array<std::function<void(SystemVerilogUvmCallbackLimits &)>, 10>
      zero_callback_limit{{
          [](auto &limits) { limits.maximum_callbacks = 0; },
          [](auto &limits) { limits.maximum_callbacks_per_dispatch = 0; },
          [](auto &limits) { limits.maximum_reentry_depth = 0; },
          [](auto &limits) { limits.maximum_failures = 0; },
          [](auto &limits) { limits.maximum_type_bytes = 0; },
          [](auto &limits) { limits.maximum_name_bytes = 0; },
          [](auto &limits) { limits.maximum_operation_bytes = 0; },
          [](auto &limits) { limits.maximum_attribute_count = 0; },
          [](auto &limits) { limits.maximum_attribute_bytes = 0; },
          [](auto &limits) { limits.maximum_mutations = 0; },
      }};
  for (const auto &zero : zero_callback_limit) {
    auto limits = SystemVerilogUvmCallbackLimits{};
    zero(limits);
    require_callback_error(
        "FSIM-UVM-CALLBACK-002",
        [&] {
          SystemVerilogUvmCallbackService rejected{fixture.objects,
                                                   fixture.components, limits};
        },
        "every zero UVM callback limit must reject construction");
  }

  SystemVerilogUvmCallbackLimits dispatch_limits;
  dispatch_limits.maximum_callbacks_per_dispatch = 1;
  SystemVerilogUvmCallbackService dispatch_bounded{
      fixture.objects, fixture.components, dispatch_limits};
  for (const auto name : {"first", "second"}) {
    (void)dispatch_bounded.add(
        {std::string{kComponentType}, "bounded", name, 0, UINT64_C(1),
         SystemVerilogUvmCallbackOrdering::Append, [](auto &) {}});
  }
  require_callback_error(
      "FSIM-UVM-CALLBACK-002",
      [&] { (void)dispatch_bounded.dispatch(first, "bounded", UINT64_C(1)); },
      "UVM callback dispatch fanout ceilings must reject before invocation");

  SystemVerilogUvmCallbackLimits failure_limits;
  failure_limits.maximum_failures = 1;
  SystemVerilogUvmCallbackService failure_bounded{
      fixture.objects, fixture.components, failure_limits};
  for (const auto name : {"first", "second"}) {
    (void)failure_bounded.add(
        {std::string{kComponentType}, "failure", name, 0, UINT64_C(1),
         SystemVerilogUvmCallbackOrdering::Append,
         [](auto &) { throw std::runtime_error{"bounded failure"}; }});
  }
  const auto bounded_failures =
      failure_bounded.dispatch(first, "failure", UINT64_C(1));
  require_callback(
      bounded_failures.contained_failures == 2 &&
          failure_bounded.failures().size() == 1,
      "callback exception containment must continue after its retained "
      "failure log reaches the configured ceiling");

  SystemVerilogUvmCallbackLimits invocation_limits;
  invocation_limits.maximum_attribute_count = 1;
  SystemVerilogUvmCallbackService invocation_bounded{
      fixture.objects, fixture.components, invocation_limits};
  (void)invocation_bounded.add({
      std::string{kComponentType}, "attribute", "expands", 0, UINT64_C(1),
      SystemVerilogUvmCallbackOrdering::Append,
      [](auto &invocation) { invocation.attributes["second"] = "2"; }});
  const auto bounded_invocation = invocation_bounded.dispatch(
      first, "attribute", UINT64_C(1), "expand", {{"first", "1"}});
  require_callback(
      bounded_invocation.contained_failures == 1 &&
          bounded_invocation.invocation.attributes ==
              SystemVerilogUvmCallbackAttributes{{"first", "1"}},
      "callback attribute ceilings must contain the callback and roll its "
      "invocation mutation back exactly");

  SystemVerilogUvmTransactionRecorderService transactions{
      fixture.objects, fixture.components, callbacks};
  transactions.set_scheduler(fixture.scheduler);
  transactions.set_activity_service(fixture.activity);
  std::vector<std::string> transaction_callback_order;
  (void)callbacks.add(
      {std::string{kComponentType}, "uvm_transaction_callback", "transaction",
       0, UINT64_C(15), SystemVerilogUvmCallbackOrdering::Prepend,
       [&](auto &invocation) {
         transaction_callback_order.push_back(invocation.operation);
         if (invocation.operation == "begin") {
           invocation.attributes["lane"] = "callback";
         }
       }});

  SystemVerilogUvmTransactionHandle parent;
  SystemVerilogUvmTransactionHandle child;
  (void)fixture.scheduler.schedule_after(
      1, SchedulerPhase::reactive, 0, [&](Scheduler &) {
        parent = transactions.begin({"parent",
                                     "bus",
                                     "request",
                                     fixture.first_root,
                                     first,
                                     std::nullopt,
                                     {{"address", std::uint64_t{16}}}});
      });
  (void)fixture.scheduler.schedule_after(
      2, SchedulerPhase::reactive, 0, [&](Scheduler &) {
        child = transactions.begin({"child",
                                    "bus",
                                    "response",
                                    fixture.first_root,
                                    first,
                                    parent,
                                    {}});
        transactions.add_link(child, parent, "response_to");
      });
  (void)fixture.scheduler.schedule_after(
      3, SchedulerPhase::reactive, 0, [&](Scheduler &) {
        transactions.record_attribute(child, {"status", std::string{"OK"}});
      });
  (void)fixture.scheduler.schedule_after(
      4, SchedulerPhase::reactive, 0,
      [&](Scheduler &) { transactions.end(child); });
  (void)fixture.scheduler.schedule_after(
      5, SchedulerPhase::reactive, 0, [&](Scheduler &) {
        transactions.end(parent, SystemVerilogUvmTransactionState::Cancelled);
      });
  require_callback(fixture.scheduler.run().status == RunStatus::completed,
                   "scheduled UVM transaction recording must complete");

  const auto parent_snapshot = transactions.snapshot(parent);
  const auto child_snapshot = transactions.snapshot(child);
  const auto traces = transactions.trace_records();
  require_callback(
      parent_snapshot.identity == 1 && child_snapshot.identity == 2 &&
          child_snapshot.parent_identity == std::optional<std::uint64_t>{1} &&
          child_snapshot.links ==
              std::vector<SystemVerilogUvmTransactionLink>{
                  {1, "response_to"}} &&
          parent_snapshot.begin_time == 1 && parent_snapshot.end_time == 5 &&
          child_snapshot.begin_time == 2 && child_snapshot.end_time == 4 &&
          parent_snapshot.state ==
              SystemVerilogUvmTransactionState::Cancelled &&
          child_snapshot.state == SystemVerilogUvmTransactionState::Completed &&
          std::ranges::any_of(parent_snapshot.attributes,
                              [](const auto &attribute) {
                                return attribute.name == "lane" &&
                                       std::get<std::string>(attribute.value) ==
                                           "callback";
                              }) &&
          std::ranges::equal(
              traces | std::views::transform(
                           &SystemVerilogUvmTransactionTraceRecord::sequence),
              std::views::iota(UINT64_C(1), UINT64_C(1) + traces.size())),
      "UVM transaction recording must retain stable identity, parent/link "
      "relations, callback attributes, scheduler timing, terminal state, and "
      "contiguous engine-neutral trace order");
  require_callback(
      transaction_callback_order ==
              std::vector<std::string>{"begin", "begin", "link", "attribute",
                                       "end", "end"} &&
          std::ranges::any_of(fixture.activity.events(),
                              [](const auto &event) {
                                return event.kind ==
                                       SystemVerilogUvmActivityKind::Callback;
                              }) &&
          std::ranges::count_if(
              fixture.activity.events(),
              [](const auto &event) {
                return event.kind == SystemVerilogUvmActivityKind::Transaction;
              }) == 6,
      "transaction begin/end, attributes, and links must dispatch callback "
      "masks and publish bounded activity events");

  const auto replayed = transactions.replay_trace(traces);
  require_callback(
      replayed.size() == 2 && replayed[0].identity == 1 &&
          replayed[0].state == SystemVerilogUvmTransactionState::Cancelled &&
          replayed[1].identity == 2 && replayed[1].parent_identity == 1 &&
          replayed[1].links == child_snapshot.links &&
          replayed[1].state == SystemVerilogUvmTransactionState::Completed,
      "UVM transaction traces must replay deterministic identity, hierarchy, "
      "links, attributes, timing, and terminal states");

  const auto payload = fixture.make_object(kObjectType, "payload");
  fixture.heap.property(payload, "value").packed =
      PackedLogic4::from_aval_bval(8, 0xa5, 0);
  fixture.heap.property(payload, "label").string = "recorded";
  const auto object_record = transactions.begin(
      {"object", "bus", "payload", fixture.first_root, first,
       std::nullopt, {}});
  transactions.record_object(object_record, payload, "packed");
  transactions.end(object_record);
  const auto object_snapshot = transactions.snapshot(object_record);
  require_callback(
      std::ranges::any_of(object_snapshot.attributes, [](const auto& attribute) {
        return attribute.name == "packed.payload.value" &&
            std::get<std::string>(attribute.value).ends_with(":10100101");
      }) &&
          std::ranges::any_of(
              object_snapshot.attributes, [](const auto& attribute) {
                return attribute.name == "packed.payload.label" &&
                    std::get<std::string>(attribute.value).ends_with(
                        ":recorded");
              }),
      "UVM object recorder must integrate typed automated entries as "
      "transaction attributes");

  SystemVerilogUvmTransactionLimits object_record_limits;
  object_record_limits.maximum_attributes_per_transaction = 2;
  SystemVerilogUvmTransactionRecorderService object_record_bounded{
      fixture.objects, fixture.components, callbacks, object_record_limits};
  const auto bounded_object_record = object_record_bounded.begin(
      {"bounded-object", "bus", "payload", fixture.first_root, 0,
       std::nullopt, {}});
  require_transaction_error(
      "FSIM-UVM-TR-002",
      [&] {
        object_record_bounded.record_object(
            bounded_object_record, payload, "packed");
      },
      "UVM object recording must enforce aggregate attribute ceilings");
  require_callback(
      object_record_bounded.snapshot(bounded_object_record).attributes.empty() &&
          object_record_bounded.trace_records().size() == 1,
      "rejected UVM object recording must not publish partial attributes");

  auto malformed_replay =
      std::vector<SystemVerilogUvmTransactionTraceRecord>{
          transactions.trace_records().begin(), transactions.trace_records().end()};
  malformed_replay[1].sequence = malformed_replay[0].sequence;
  require_transaction_error(
      "FSIM-UVM-TR-001",
      [&] { (void)transactions.replay_trace(malformed_replay); },
      "UVM transaction replay must reject non-monotonic trace sequences");
  SystemVerilogUvmTransactionLimits replay_limits;
  replay_limits.maximum_trace_records = 1;
  SystemVerilogUvmTransactionRecorderService replay_bounded{
      fixture.objects, fixture.components, callbacks, replay_limits};
  require_transaction_error(
      "FSIM-UVM-TR-002",
      [&] { (void)replay_bounded.replay_trace(transactions.trace_records()); },
      "UVM transaction replay must enforce retained-record ceilings");

  require_transaction_error(
      "FSIM-UVM-TR-001",
      [&] { transactions.record_attribute(child, {"late", std::uint64_t{1}}); },
      "ended UVM transactions must reject attribute mutation");
  require_transaction_error(
      "FSIM-UVM-TR-001",
      [&] {
        (void)transactions.begin({"cross",
                                  "bus",
                                  "request",
                                  fixture.second_root,
                                  isolated,
                                  parent,
                                  {}});
      },
      "UVM transaction parents must reject cross-root use");
  require_transaction_error(
      "FSIM-UVM-TR-001", [&] { transactions.release(parent); },
      "referenced UVM transaction records must reject premature release");
  transactions.release(child);
  transactions.release(parent);
  transactions.release(object_record);
  require_transaction_error(
      "FSIM-UVM-TR-001", [&] { (void)transactions.snapshot(child); },
      "released UVM transaction handles must become stale");

  SystemVerilogUvmTransactionRecorderService foreign_transactions{
      fixture.objects, fixture.components, callbacks};
  require_transaction_error(
      "FSIM-UVM-TR-001", [&] { (void)foreign_transactions.snapshot(parent); },
      "UVM transaction handles must reject cross-service ownership");
  const std::array<std::function<void(SystemVerilogUvmTransactionLimits &)>, 8>
      zero_transaction_limit{{
          [](auto &limits) { limits.maximum_transactions = 0; },
          [](auto &limits) { limits.maximum_active_transactions = 0; },
          [](auto &limits) { limits.maximum_attributes_per_transaction = 0; },
          [](auto &limits) { limits.maximum_links_per_transaction = 0; },
          [](auto &limits) { limits.maximum_trace_records = 0; },
          [](auto &limits) { limits.maximum_text_bytes = 0; },
          [](auto &limits) { limits.maximum_attribute_bytes = 0; },
          [](auto &limits) { limits.maximum_mutations = 0; },
      }};
  for (const auto &zero : zero_transaction_limit) {
    auto limits = SystemVerilogUvmTransactionLimits{};
    zero(limits);
    require_transaction_error(
        "FSIM-UVM-TR-002",
        [&] {
          SystemVerilogUvmTransactionRecorderService rejected{
              fixture.objects, fixture.components, callbacks, limits};
        },
        "every zero UVM transaction limit must reject construction");
  }
  SystemVerilogUvmTransactionLimits transaction_count_limits;
  transaction_count_limits.maximum_transactions = 1;
  SystemVerilogUvmTransactionRecorderService transaction_count{
      fixture.objects, fixture.components, callbacks, transaction_count_limits};
  const auto bounded = transaction_count.begin(
      {"one", "bus", "request", fixture.first_root, first, std::nullopt, {}});
  require_transaction_error(
      "FSIM-UVM-TR-002",
      [&] {
        (void)transaction_count.begin({"two",
                                       "bus",
                                       "request",
                                       fixture.first_root,
                                       first,
                                       std::nullopt,
                                       {}});
      },
      "UVM transaction count ceilings must reject without partial records");
  transaction_count.end(bounded);
  require_transaction_error(
      "FSIM-UVM-TR-001",
      [&] {
        (void)transaction_count.begin(
            {"nan",
             "bus",
             "request",
             fixture.first_root,
             first,
             std::nullopt,
             {{"bad", std::numeric_limits<double>::quiet_NaN()}}});
      },
      "UVM transaction attributes must reject unstable non-finite values");

  SystemVerilogUvmTransactionLimits active_limits;
  active_limits.maximum_active_transactions = 1;
  SystemVerilogUvmTransactionRecorderService active_bounded{
      fixture.objects, fixture.components, callbacks, active_limits};
  const auto active_one = active_bounded.begin(
      {"active", "bus", "request", fixture.first_root, 0, std::nullopt, {}});
  require_transaction_error(
      "FSIM-UVM-TR-002",
      [&] {
        (void)active_bounded.begin({"blocked", "bus", "request",
                                    fixture.first_root, 0, std::nullopt, {}});
      },
      "active UVM transaction ceilings must reject without publication");
  active_bounded.end(active_one);

  SystemVerilogUvmTransactionLimits attribute_byte_limits;
  attribute_byte_limits.maximum_attribute_bytes = 3;
  SystemVerilogUvmTransactionRecorderService attribute_bounded{
      fixture.objects, fixture.components, callbacks, attribute_byte_limits};
  const auto attribute_transaction = attribute_bounded.begin(
      {"bytes", "bus", "request", fixture.first_root, 0, std::nullopt,
       {{"a", std::string{"1"}}}});
  require_transaction_error(
      "FSIM-UVM-TR-002",
      [&] {
        attribute_bounded.record_attribute(
            attribute_transaction, {"b", std::string{"2"}});
      },
      "aggregate UVM transaction attribute-byte ceilings must reject "
      "without changing the prior attribute or trace");
  require_callback(
      attribute_bounded.snapshot(attribute_transaction).attributes.size() == 1
          && attribute_bounded.trace_records().size() == 2,
      "rejected aggregate attributes must leave transaction state exact");

  SystemVerilogUvmActivityLimits full_activity_limits;
  full_activity_limits.maximum_events = 1;
  SystemVerilogUvmActivityService full_activity{fixture.scheduler,
                                                full_activity_limits};
  SystemVerilogUvmTransactionRecorderService rollback_transactions{
      fixture.objects, fixture.components, callbacks};
  rollback_transactions.set_scheduler(fixture.scheduler);
  rollback_transactions.set_activity_service(full_activity);
  const auto rollback = rollback_transactions.begin(
      {"rollback", "bus", "request", fixture.first_root, 0, std::nullopt, {}});
  require_callback(
      rollback_transactions.trace_records().size() == 1,
      "a published UVM transaction begin must retain one stable trace");
  bool activity_rejected{};
  try {
    rollback_transactions.record_attribute(rollback,
                                           {"blocked", std::uint64_t{1}});
  } catch (const SystemVerilogUvmActivityError &error) {
    activity_rejected = error.diagnostic_code() == "FSIM-UVM-ACTIVITY-002";
  }
  require_callback(
      activity_rejected &&
          rollback_transactions.snapshot(rollback).attributes.empty() &&
          rollback_transactions.trace_records().size() == 1 &&
          rollback_transactions.snapshot(rollback).state ==
              SystemVerilogUvmTransactionState::Active,
      "activity publication rejection must roll back transaction attributes, "
      "trace state, and lifecycle atomically");
}

} // namespace fsim::tests::runtime
