// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_resource.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

fsim::runtime::SystemVerilogUvmResourceDescriptor packed_resource(
    std::string name,
    std::string scope,
    const std::string_view bits,
    const std::int64_t precedence = 0) {
  using namespace fsim::runtime;
  SystemVerilogUvmResourceDescriptor result;
  result.name = std::move(name);
  result.scope_pattern = std::move(scope);
  result.type = {
      "uvm_resource#(logic[7:0])",
      SystemVerilogUvmResourceValueKind::Packed,
      bits.size()};
  result.value = PackedLogic4::from_msb_string(bits);
  result.precedence = precedence;
  return result;
}

}  // namespace

void test_systemverilog_uvm_resource_pool() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{16, 8'192}};
  SystemVerilogUvmResourceLimits limits;
  limits.max_resources = 16;
  limits.max_lookup_results = 8;
  limits.max_audit_records = 4;
  limits.max_callbacks_per_resource = 4;
  limits.max_spell_candidates = 3;
  limits.max_spell_distance = 3;
  limits.max_report_bytes = 4'096;
  SystemVerilogUvmResourcePoolService pool{&heap, limits};

  const auto first = pool.insert(packed_resource(
      "timeout", "env.*", "00000001"));
  const auto second = pool.insert(packed_resource(
      "timeout", "env.agent?", "00000010"));
  require(
      pool.get_by_name("env.agent0", "timeout") == first
          && pool.lookup_name("env.agent0", "timeout").size() == 2
          && pool.get_by_type(
                 "env.agent0", "uvm_resource#(logic[7:0])") == first,
      "UVM resource lookup must match bounded scope wildcards and preserve "
      "insertion order at equal precedence and priority");
  pool.set_priority(second, SystemVerilogUvmResourcePriority::High);
  require(
      pool.get_by_name("env.agent0", "timeout") == second,
      "UVM high queue priority must move a matching resource ahead");
  pool.set_precedence(first, 10);
  require(
      pool.get_by_name("env.agent0", "timeout") == first,
      "UVM precedence must sort before queue priority");
  pool.set_scope(second, "other.*");
  require(
      pool.lookup_name("env.agent0", "timeout").size() == 1,
      "UVM resource scope updates must take effect without stale indexes");

  std::vector<SystemVerilogUvmResourceCallbackEvent> callback_events;
  SystemVerilogUvmResourceCallbackToken self_removing{};
  self_removing = pool.add_callback(
      first,
      [&](const SystemVerilogUvmResourceCallbackEvent event,
          const SystemVerilogUvmResource&) {
        callback_events.push_back(event);
        if (event == SystemVerilogUvmResourceCallbackEvent::PreWrite) {
          require(pool.remove_callback(first, self_removing),
                  "active callback removal must succeed");
        }
      });
  (void)pool.add_callback(
      first,
      [](const SystemVerilogUvmResourceCallbackEvent event,
         const SystemVerilogUvmResource&) {
        if (event == SystemVerilogUvmResourceCallbackEvent::PostWrite) {
          throw std::runtime_error{"contained resource callback failure"};
        }
      });
  require(
      pool.write(
          first, PackedLogic4::from_msb_string("10100101"), "writer")
          && callback_events.size() == 1
          && callback_events.front()
              == SystemVerilogUvmResourceCallbackEvent::PreWrite,
      "UVM resource callbacks must use snapshot dispatch and permit safe "
      "removal during a callback");
  const auto read_value = pool.read(first, "reader");
  require(
      std::get<PackedLogic4>(read_value).to_msb_string() == "10100101"
          && pool.snapshot(first).revision == 1
          && pool.snapshot(first).read_count == 1
          && pool.snapshot(first).write_count == 1
          && std::ranges::any_of(
              pool.audit_records(), [](const auto& audit) {
                return audit.action
                    == SystemVerilogUvmResourceAuditAction::CallbackFailure;
              }),
      "typed resource reads/writes must publish revisions and bounded audit "
      "records while containing callback exceptions");

  bool type_rejected{};
  try {
    (void)pool.write(first, std::string{"wrong"}, "writer");
  } catch (const std::invalid_argument&) {
    type_rejected = true;
  }
  pool.set_read_only(first, true);
  require(
      type_rejected
          && !pool.write(
              first, PackedLogic4::from_msb_string("11111111"), "writer")
          && pool.snapshot(first).revision == 1,
      "UVM resources must reject nominal value mismatches and audit read-only "
      "writes without mutating the stored value");

  (void)pool.insert(packed_resource(
      "timebase", "*", "00000000"));
  (void)pool.read(first, "reader2");
  const auto spelling = pool.spell_check("timeot", 2);
  require(
      spelling.size() == 1 && spelling.front() == "timeout",
      "UVM resource spell checking must return bounded deterministic nearest "
      "names");
  require(
      pool.audit_records().size() == limits.max_audit_records
          && pool.dropped_audit_records() > 0
          && pool.report().find("UVM Resource Pool\n") == 0
          && pool.report().find("timeout @ env.*") != std::string::npos,
      "UVM resource audits and reports must remain bounded and deterministic");

  SystemVerilogClassDescriptor object_descriptor;
  object_descriptor.declared_type = "uvm_pkg::uvm_object";
  object_descriptor.dynamic_type = "work::item";
  object_descriptor.specialization_identity = "work::item";
  object_descriptor.assignable_declared_types = {
      "work::item", "uvm_pkg::uvm_object"};
  const auto object = heap.allocate(object_descriptor);
  SystemVerilogUvmResourceDescriptor object_resource;
  object_resource.name = "object";
  object_resource.type = {
      "uvm_resource#(uvm_object)",
      SystemVerilogUvmResourceValueKind::Object,
      0};
  object_resource.value = object;
  const auto object_entry = pool.insert(std::move(object_resource));
  require(
      pool.erase(object_entry) && heap.contains(object),
      "resource erasure must release metadata and callbacks without taking "
      "ownership of an object-valued resource");
  require(heap.release(object), "the caller must retain object ownership");

  const auto stale_object = heap.allocate(object_descriptor);
  object_resource.name = "stale";
  object_resource.type = {
      "uvm_resource#(uvm_object)",
      SystemVerilogUvmResourceValueKind::Object,
      0};
  object_resource.value = stale_object;
  const auto stale_entry = pool.insert(std::move(object_resource));
  require(heap.release(stale_object), "stale-handle setup must release object");
  bool stale_rejected{};
  try {
    (void)pool.read(stale_entry, "reader");
  } catch (const std::invalid_argument&) {
    stale_rejected = true;
  }
  require(
      stale_rejected,
      "object-valued resources must validate generation-safe handles on read");

  SystemVerilogUvmResourceLimits tiny_limits;
  tiny_limits.max_resources = 1;
  tiny_limits.max_lookup_results = 1;
  tiny_limits.max_audit_records = 1;
  tiny_limits.max_callbacks_per_resource = 1;
  tiny_limits.max_spell_candidates = 1;
  tiny_limits.max_spell_distance = 1;
  tiny_limits.max_report_bytes = 8;
  SystemVerilogUvmResourcePoolService tiny{&heap, tiny_limits};
  const auto only = tiny.insert(packed_resource(
      "only", "*", "00000000"));
  bool resource_limit_rejected{};
  bool callback_limit_rejected{};
  bool report_limit_rejected{};
  (void)tiny.add_callback(only, [](auto, const auto&) {});
  try {
    (void)tiny.insert(packed_resource("extra", "*", "00000000"));
  } catch (const std::length_error&) {
    resource_limit_rejected = true;
  }
  try {
    (void)tiny.add_callback(only, [](auto, const auto&) {});
  } catch (const std::length_error&) {
    callback_limit_rejected = true;
  }
  try {
    (void)tiny.report();
  } catch (const std::length_error&) {
    report_limit_rejected = true;
  }
  require(
      resource_limit_rejected && callback_limit_rejected
          && report_limit_rejected,
      "UVM resource, callback, and report limits must reject before growth");
}

}  // namespace fsim::tests::runtime
