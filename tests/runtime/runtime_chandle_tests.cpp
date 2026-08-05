// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/systemverilog_chandle.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error{message};
}

template <typename Exception, typename Callback>
void require_throws(Callback&& callback, const char* message) {
  try {
    callback();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error{message};
}

}  // namespace

void test_systemverilog_chandle_registry() {
  using namespace fsim::runtime;
  std::size_t cleanup_count{};
  std::vector<SystemVerilogChandleEvent> events;
  SystemVerilogChandleRegistry registry;
  const auto observer = registry.add_observer(
      [&](const SystemVerilogChandleEvent& event) {
        events.push_back(event);
      });
  const auto throwing_observer = registry.add_observer(
      [](const SystemVerilogChandleEvent&) {
        throw std::runtime_error{"observer failure"};
      });

  require(!registry.contains(0), "null chandle must never be live");
  require(registry.format(0) == "null", "null chandle formatting");
  const auto first = registry.create({
      "dpi:file", "input-stream", [&] { ++cleanup_count; }});
  require(
      registry.remove_observer(throwing_observer),
      "observer failures are contained after publication");
  require(first != 0 && registry.contains(first), "create live chandle");
  require(
      registry.live_handles() == 1 && registry.storage_bytes() != 0,
      "chandle resource accounting");
  require(
      events.size() == 1
          && events[0].kind == SystemVerilogChandleEventKind::Created
          && events[0].value.handle == first
          && events[0].value.type_identity == "dpi:file",
      "creation callback carries pointer-free metadata");
  require(
      registry.format(first).find("chandle <opaque ") == 0
          && registry.format(first).find("dpi:file") != std::string::npos
          && registry.format(first).find("input-stream")
              != std::string::npos
          && registry.format(first).find("0x") == std::string::npos,
      "debug formatting exposes only an opaque simulator identity");

  const auto alias = registry.alias(first);
  require(
      alias == first && registry.inspect(first).alias_transfers == 1
          && events.back().kind == SystemVerilogChandleEventKind::Aliased,
      "alias transfer preserves identity and publishes a callback");
  require(
      SystemVerilogChandleRegistry::equal(first, alias)
          && !SystemVerilogChandleRegistry::equal(first, 0),
      "chandle equality compares opaque identities");

  const auto packed = encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::chandle(first));
  require(
      packed
          && packed.value.width() == 64
          && packed.value.low_word().aval == first
          && packed.value.low_word().bval == 0,
      "chandle scalar carrier contains only exact registry identity bits");
  const auto decoded = decode_systemverilog_scalar_payload(
      packed.value, SystemVerilogScalarKind::Chandle);
  require(
      decoded && decoded.value.as_chandle()
          && *decoded.value.as_chandle() == first,
      "chandle carrier round trip preserves exact opaque identity");
  require(
      !systemverilog_scalar_arithmetic(
          SystemVerilogScalarArithmetic::Add,
          decoded.value,
          SystemVerilogScalarValue::chandle(0)),
      "chandle values never enter numeric scalar arithmetic");

  simir::Interpreter interpreter;
  const auto signal = interpreter.add_signal({
      "foreign",
      encode_systemverilog_scalar_payload(
          SystemVerilogScalarValue::chandle(0)).value,
      simir::ResolutionKind::none,
      simir::ValueKind::logic4,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      SystemVerilogScalarKind::Chandle});
  std::vector<SystemVerilogScalarValue> changes;
  interpreter.set_scalar_signal_change_hook(
      [&](const simir::SignalId changed,
          const SystemVerilogScalarValue& value,
          const fsim::runtime::SimulationTick) {
        require(changed == signal, "typed chandle callback signal identity");
        changes.push_back(value);
      });
  interpreter.deposit_scalar_signal(
      signal, SystemVerilogScalarValue::chandle(alias));
  require(
      interpreter.scalar_signal_value(signal).as_chandle() == alias
          && interpreter.scalar_signal_snapshots().size() == 1
          && changes.size() == 1 && changes[0].as_chandle() == alias,
      "interpreter, callback, and snapshot preserve chandle identity");

  std::ostringstream trace;
  VcdWriter writer(trace, "1ns");
  const auto trace_signal = writer.declare_systemverilog_scalar(
      "top.foreign", SystemVerilogScalarKind::Chandle);
  writer.begin(0);
  writer.change(trace_signal, SystemVerilogScalarValue::chandle(alias));
  writer.flush();
  require(
      trace.str().find("$var wire 64") != std::string::npos
          && trace.str().find("real") == std::string::npos,
      "VCD records opaque chandle identity as an exact vector, not a real");

  require(registry.release(first), "live chandle releases exactly once");
  require(
      cleanup_count == 1 && !registry.contains(alias)
          && events.back().kind == SystemVerilogChandleEventKind::Released,
      "release cleans once and makes every alias stale");
  require(
      registry.format(alias).starts_with("stale chandle <opaque "),
      "stale chandle debugger formatting is explicit");
  require(!registry.release(alias), "stale release is idempotently rejected");
  require_throws<std::out_of_range>(
      [&] { (void)registry.alias(alias); },
      "stale chandle alias must reject");

  const auto second = registry.create({"dpi:file", "replacement", {}});
  require(
      second != first && registry.contains(second),
      "slot reuse advances the encoded generation");
  require(registry.remove_observer(observer), "observer removal succeeds");
  const auto event_count = events.size();
  registry.clear();
  require(
      registry.live_handles() == 0 && registry.storage_bytes() == 0
          && events.size() == event_count,
      "clear ends all lifetimes without removed-observer callbacks");

  SystemVerilogChandleRegistry limited{{1, 64, 1}};
  const auto limited_observer = limited.add_observer(
      [](const SystemVerilogChandleEvent&) {});
  require_throws<std::length_error>(
      [&] {
        (void)limited.add_observer(
            [](const SystemVerilogChandleEvent&) {});
      },
      "chandle observer budget must reject");
  const auto limited_handle = limited.create({"T", {}, {}});
  require_throws<std::length_error>(
      [&] { (void)limited.create({"U", {}, {}}); },
      "chandle live-handle budget must reject");
  require(limited.remove_observer(limited_observer), "remove limited observer");
  require(limited.release(limited_handle), "release limited handle");

  SystemVerilogChandleRegistry byte_limited{{4, 32, 1}};
  require_throws<std::length_error>(
      [&] { (void)byte_limited.create({"T", {}, {}}); },
      "chandle byte budget must reject before publication");
  require(
      byte_limited.live_handles() == 0
          && byte_limited.storage_bytes() == 0,
      "resource-rejected chandle creation publishes no partial identity");
  require_throws<std::invalid_argument>(
      [&] { (void)registry.create({}); },
      "chandle type identity must not be empty");

  SystemVerilogChandleRegistry throwing_cleanup;
  const auto throwing_handle = throwing_cleanup.create({
      "dpi:throwing", {}, [] { throw std::runtime_error{"cleanup"}; }});
  require_throws<std::runtime_error>(
      [&] { (void)throwing_cleanup.release(throwing_handle); },
      "cleanup failure is reported after release");
  require(
      !throwing_cleanup.contains(throwing_handle)
          && throwing_cleanup.live_handles() == 0
          && throwing_cleanup.storage_bytes() == 0,
      "cleanup failure cannot resurrect or leak a stale identity");
}

}  // namespace fsim::tests::runtime
