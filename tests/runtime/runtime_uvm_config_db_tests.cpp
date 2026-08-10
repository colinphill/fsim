// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_config_db.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

fsim::runtime::SystemVerilogUvmResourceType packed_type() {
  return {
      "uvm_config_db#(logic[7:0])",
      fsim::runtime::SystemVerilogUvmResourceValueKind::Packed,
      8};
}

fsim::runtime::PackedLogic4 packed(const std::uint64_t value) {
  return fsim::runtime::PackedLogic4::from_aval_bval(8, value, 0);
}

std::uint64_t packed_value(
    const std::optional<fsim::runtime::SystemVerilogUvmResourceValue>& value) {
  if (!value) return 0;
  return std::get<fsim::runtime::PackedLogic4>(*value).low_word().aval;
}

}  // namespace

void test_systemverilog_uvm_config_db() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap;
  SystemVerilogUvmResourceLimits resource_limits;
  resource_limits.max_resources = 32;
  resource_limits.max_audit_records = 32;
  resource_limits.max_spell_distance = 4;
  SystemVerilogUvmResourcePoolService resources{&heap, resource_limits};
  SystemVerilogUvmConfigDbLimits config_limits;
  config_limits.max_entries = 16;
  config_limits.max_waiters = 8;
  config_limits.max_wake_callbacks_per_set = 8;
  config_limits.max_regex_states = 1'024;
  config_limits.max_context_depth = 16;
  SystemVerilogUvmConfigDbService config{resources, config_limits};
  config.set_trace_enabled(true);
  config.set_trace_callback([](const auto&) {
    throw std::runtime_error{"contained config trace callback failure"};
  });

  const SystemVerilogUvmConfigContext root{"", 0};
  const SystemVerilogUvmConfigContext env{"uvm_test_top.env", 2};
  const auto root_setting = config.set(
      root, "uvm_test_top.env.*", "timeout", packed_type(), packed(1),
      SystemVerilogUvmConfigPhase::Build);
  const auto low_setting = config.set(
      env, "*", "timeout", packed_type(), packed(2),
      SystemVerilogUvmConfigPhase::Build);
  require(
      root_setting != low_setting
          && packed_value(config.get(
                 env, "agent0", "timeout", packed_type().identity,
                 "uvm_test_top.env.agent0")) == 1,
      "build-time config settings from higher hierarchy levels must have "
      "higher precedence than lower-level settings");

  const auto runtime_setting = config.set(
      env, "agent0", "timeout", packed_type(), packed(3),
      SystemVerilogUvmConfigPhase::Runtime);
  require(
      packed_value(config.get(
          env, "agent0", "timeout", packed_type().identity)) == 3,
      "runtime config settings must return to default precedence and use "
      "last-setting-wins ordering");
  const auto updated_setting = config.set(
      env, "agent0", "timeout", packed_type(), packed(4),
      SystemVerilogUvmConfigPhase::Runtime);
  require(
      updated_setting == runtime_setting && config.entries().size() == 3
          && packed_value(config.get(
                 env, "agent0", "timeout", packed_type().identity)) == 4,
      "repeated exact config sets must reuse the resource and move it to the "
      "front without growing the entry table");

  (void)config.set(
      root,
      "/^uvm_test_top\\.env\\.agent.$/",
      "/^mode.*$/",
      packed_type(), packed(7), SystemVerilogUvmConfigPhase::Runtime);
  require(
      packed_value(config.get(
          env, "agent1", "mode_select", packed_type().identity)) == 7
          && !config.get(
              env, "monitor", "mode_select", packed_type().identity),
      "config lookup must support bounded safe regular expressions for "
      "instance and field patterns");

  std::vector<unsigned> wake_order;
  (void)config.wait_modified(
      env, "agent1", "timeout",
      [&](const auto, const auto& entry) {
        require(
            resources.snapshot(entry.resource).revision == 0,
            "new config resources must publish before waiter callbacks");
        wake_order.push_back(1);
      });
  (void)config.wait_modified(
      env, "agent1", "timeout",
      [&](const auto, const auto&) {
        wake_order.push_back(2);
        throw std::runtime_error{"contained config waiter failure"};
      });
  const auto retained_waiter = config.wait_modified(
      env, "monitor", "timeout",
      [](const auto, const auto&) {});
  (void)config.set(
      root, "uvm_test_top.env.agent?", "timeout",
      packed_type(), packed(9), SystemVerilogUvmConfigPhase::Runtime);
  require(
      wake_order == std::vector<unsigned>({1, 2})
          && config.callback_failures() == 1
          && config.waiter_count() == 1
          && config.cancel_waiter(retained_waiter)
          && config.waiter_count() == 0,
      "wait_modified callbacks must wake once in registration order after "
      "publication, contain exceptions, and preserve nonmatching waiters");

  std::vector<std::string> spelling;
  require(
      config.exists(
          env, "agent0", "timeout", packed_type().identity)
          && !config.exists(
              env, "agent0", "timeout", "uvm_config_db#(string)")
          && !config.exists(
              env, "agent0", "timeou", packed_type().identity,
              true, &spelling)
          && spelling.size() == 1 && spelling.front() == "timeout",
      "config exists must preserve nominal typing and bounded optional spell "
      "checking without reading the resource value");
  require(
      config.trace_text().find("action=set") != std::string::npos
          && config.trace_text().find("action=get") != std::string::npos
          && config.trace_text().find("action=exists") != std::string::npos
          && config.trace_callback_failures() == config.trace_records().size()
          && config.report(true).find("UVM Config DB\n") == 0
          && config.report(true).find("audit=") != std::string::npos
          && config.report(true).find("reads=") != std::string::npos,
      "config trace and audited usage inventories must be deterministic and "
      "contain trace callback failures");

  bool regex_rejected{};
  try {
    (void)config.set(
        root, "/^agent[0]$/", "bad", packed_type(), packed(0),
        SystemVerilogUvmConfigPhase::Runtime);
  } catch (const std::invalid_argument&) {
    regex_rejected = true;
  }
  require(
      regex_rejected,
      "unsupported backtracking regex constructs must reject before entry "
      "or resource publication");

  SystemVerilogUvmResourcePoolService tiny_resources{&heap};
  SystemVerilogUvmConfigDbLimits tiny_limits;
  tiny_limits.max_entries = 1;
  tiny_limits.max_waiters = 1;
  tiny_limits.max_context_depth = 1;
  tiny_limits.default_precedence = 2;
  tiny_limits.max_wake_callbacks_per_set = 1;
  tiny_limits.max_report_bytes = 8;
  tiny_limits.max_trace_records = 1;
  tiny_limits.max_trace_bytes = 8;
  SystemVerilogUvmConfigDbService tiny{tiny_resources, tiny_limits};
  (void)tiny.set(
      root, "a", "first", packed_type(), packed(1),
      SystemVerilogUvmConfigPhase::Build);
  bool entry_limit_rejected{};
  bool waiter_limit_rejected{};
  bool report_limit_rejected{};
  bool trace_limit_rejected{};
  tiny.set_trace_enabled(true);
  (void)tiny.get(root, "a", "first", packed_type().identity);
  (void)tiny.exists(root, "a", "first", packed_type().identity);
  try {
    (void)tiny.set(
        root, "b", "second", packed_type(), packed(2),
        SystemVerilogUvmConfigPhase::Build);
  } catch (const std::length_error&) {
    entry_limit_rejected = true;
  }
  (void)tiny.wait_modified(root, "a", "first", [](auto, const auto&) {});
  try {
    (void)tiny.wait_modified(
        root, "b", "second", [](auto, const auto&) {});
  } catch (const std::length_error&) {
    waiter_limit_rejected = true;
  }
  try {
    (void)tiny.report(true);
  } catch (const std::length_error&) {
    report_limit_rejected = true;
  }
  try {
    (void)tiny.trace_text();
  } catch (const std::length_error&) {
    trace_limit_rejected = true;
  }
  require(
      entry_limit_rejected && waiter_limit_rejected
          && report_limit_rejected && trace_limit_rejected
          && tiny.entries().size() == 1 && tiny.waiter_count() == 1
          && tiny.trace_records().size() == 1
          && tiny.dropped_trace_records() == 1,
      "config entry, waiter, report, and trace capacities must reject or evict "
      "before unbounded growth");

  SystemVerilogUvmResourcePoolService wake_resources{&heap};
  SystemVerilogUvmConfigDbLimits wake_limits;
  wake_limits.max_entries = 2;
  wake_limits.max_waiters = 2;
  wake_limits.max_wake_callbacks_per_set = 1;
  SystemVerilogUvmConfigDbService wake_limited{
      wake_resources, wake_limits};
  (void)wake_limited.wait_modified(
      root, "target", "field", [](auto, const auto&) {});
  (void)wake_limited.wait_modified(
      root, "target", "field", [](auto, const auto&) {});
  bool wake_limit_rejected{};
  try {
    (void)wake_limited.set(
        root, "target", "field", packed_type(), packed(1),
        SystemVerilogUvmConfigPhase::Runtime);
  } catch (const std::length_error&) {
    wake_limit_rejected = true;
  }
  require(
      wake_limit_rejected && wake_limited.entries().empty()
          && wake_resources.size() == 0
          && wake_limited.waiter_count() == 2,
      "config wake fanout must reject before resource publication or waiter "
      "consumption");
}

}  // namespace fsim::tests::runtime
