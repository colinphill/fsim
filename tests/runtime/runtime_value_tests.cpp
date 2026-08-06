// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::tests::runtime {

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

void test_logic() {
  using namespace fsim::runtime;

  require(to_logic4(Logic9::l) == Logic4::zero, "L must collapse to zero");
  require(to_logic4(Logic9::h) == Logic4::one, "H must collapse to one");
  require(to_logic4(Logic9::w) == Logic4::x, "W must collapse to X");
  require(resolve(Logic4::z, Logic4::one) == Logic4::one,
          "Z must be the four-state identity");
  require(resolve(Logic4::zero, Logic4::one) == Logic4::x,
          "conflicting four-state drivers must resolve to X");
  require(resolve(Logic9::l, Logic9::h) == Logic9::w,
          "weak conflict must resolve to W");
  require(resolve(Logic9::z, Logic9::h) == Logic9::h,
          "Z must be the std_logic identity");

  constexpr std::array states{
      Logic9::u,
      Logic9::x,
      Logic9::zero,
      Logic9::one,
      Logic9::z,
      Logic9::w,
      Logic9::l,
      Logic9::h,
      Logic9::dont_care};
  constexpr std::array<std::string_view, 9> golden{
      "UUUUUUUUU",
      "UXXXXXXXX",
      "UX0X0000X",
      "UXX11111X",
      "UX01ZWLHX",
      "UX01WWWWX",
      "UX01LWLWX",
      "UX01HWWHX",
      "UXXXXXXXX"};
  for (std::size_t left = 0; left < states.size(); ++left) {
    for (std::size_t right = 0; right < states.size(); ++right) {
      require(
          to_char(resolve(states[left], states[right]))
              == golden[left][right],
          "std_logic resolution table must match IEEE std_logic_1164");
    }
  }

  constexpr std::array<std::string_view, 9> and_golden{
      "UU0UUU0UU",
      "UX0XXX0XX",
      "000000000",
      "UX01XX01X",
      "UX0XXX0XX",
      "UX0XXX0XX",
      "000000000",
      "UX01XX01X",
      "UX0XXX0XX"};
  constexpr std::array<std::string_view, 9> or_golden{
      "UUU1UUU1U",
      "UXX1XXX1X",
      "UX01XX01X",
      "111111111",
      "UXX1XXX1X",
      "UXX1XXX1X",
      "UX01XX01X",
      "111111111",
      "UXX1XXX1X"};
  constexpr std::array<std::string_view, 9> xor_golden{
      "UUUUUUUUU",
      "UXXXXXXXX",
      "UX01XX01X",
      "UX10XX10X",
      "UXXXXXXXX",
      "UXXXXXXXX",
      "UX01XX01X",
      "UX10XX10X",
      "UXXXXXXXX"};
  constexpr std::string_view not_golden{"UX10XX10X"};
  for (std::size_t left = 0; left < states.size(); ++left) {
    require(
        to_char(logic_not(states[left])) == not_golden[left],
        "std_logic not table must match IEEE std_logic_1164");
    for (std::size_t right = 0; right < states.size(); ++right) {
      require(
          to_char(logic_and(states[left], states[right]))
              == and_golden[left][right],
          "std_logic and table must match IEEE std_logic_1164");
      require(
          to_char(logic_or(states[left], states[right]))
              == or_golden[left][right],
          "std_logic or table must match IEEE std_logic_1164");
      require(
          to_char(logic_xor(states[left], states[right]))
              == xor_golden[left][right],
          "std_logic xor table must match IEEE std_logic_1164");
    }
  }
  const std::array single_driver{Logic9::dont_care};
  require(
      resolve(std::span<const Logic9>{single_driver})
          == Logic9::dont_care,
      "one unresolved '-' driver must remain '-'");
}

void test_packed_values() {
  using namespace fsim::runtime;

  auto bits = PackedBit2::from_msb_string("101001");
  require(bits.width() == 6, "two-state width");
  require(bits.get(0), "rightmost input digit must be bit zero");
  bits.set(1, true);
  require(bits.to_msb_string() == "101011", "two-state mutation");

  const auto four = PackedLogic4::from_msb_string("10XZ");
  require(four.get(0) == Logic4::z, "four-state Z encoding");
  require(four.get(1) == Logic4::x, "four-state X encoding");
  require(four.to_msb_string() == "10XZ", "four-state round trip");

  const auto word_value = PackedLogic4::from_aval_bval(
      5, ~std::uint64_t{0}, UINT64_C(0b10100));
  require(
      word_value.to_msb_string() == "X1X11",
      "four-state word factory encoding");
  require(
      word_value.low_word()
          == Logic4Word{5, UINT64_C(0b11111), UINT64_C(0b10100)},
      "four-state word factory must mask unused bits");
  require(
      word_value.aval_words().size() == 1
          && word_value.bval_words().size() == 1,
      "four-state values up to 64 bits must expose one packed word");

  for (const auto invalid_width : {std::size_t{0}, std::size_t{65}}) {
    try {
      (void)PackedLogic4::from_aval_bval(invalid_width, 0, 0);
      throw std::runtime_error(
          "invalid four-state word width was accepted");
    } catch (const std::invalid_argument&) {
    }
  }
  std::string wide_text(65, '0');
  wide_text.front() = 'Z';
  wide_text.back() = 'X';
  const auto wide_value = PackedLogic4::from_msb_string(wide_text);
  require(
      wide_value.to_msb_string() == wide_text
          && wide_value.aval_words().size() == 2
          && wide_value.bval_words().size() == 2,
      "wide four-state values must retain multi-word storage");
  try {
    (void)wide_value.low_word();
    throw std::runtime_error(
        "wide four-state value exposed a single low word");
  } catch (const std::invalid_argument&) {
  }

  const auto nine = PackedLogic9::from_msb_string("U01ZWLH-");
  require(nine.to_msb_string() == "U01ZWLH-", "nine-state round trip");
  require(collapse_to_logic4(nine).to_msb_string() == "X01ZX01X",
          "nine-state collapse");

  const auto exact =
      PackedValue::from_logic9_msb_string("U01ZWLH-");
  require(exact.is_logic9(), "common value must retain Logic9 domain");
  require(
      exact.to_msb_string() == "U01ZWLH-",
      "common value must round-trip all nine states");
  require(
      exact.logic9_low_word()
          == Logic9Word{
              8,
              {
                  UINT64_C(0b00101010),
                  UINT64_C(0b01100110),
                  UINT64_C(0b00011110),
                  UINT64_C(0b00000001)}},
      "common Logic9 low word must retain four ordinal planes");
  require(
      collapse_to_logic4(exact).to_msb_string() == "X01ZX01X",
      "explicit common-value collapse must follow boundary mapping");
  require(
      collapse_to_logic4(exact)
              .promoted_to_logic9()
              .to_msb_string()
          == "X01ZX01X",
      "four-to-nine expansion must map states directly");
  try {
    (void)exact.low_word();
    throw std::runtime_error(
        "exact Logic9 value exposed a lossy aval/bval word");
  } catch (const std::invalid_argument&) {
  }

  const std::vector four_drivers = {
      PackedLogic4::from_msb_string("ZZ01"),
      PackedLogic4::from_msb_string("10Z1"),
  };
  require(resolve(std::span<const PackedLogic4>(four_drivers))
              .to_msb_string() == "1001",
          "packed four-state resolution");

  const std::vector exact_drivers = {
      PackedValue::from_logic9_msb_string("ZL-H"),
      PackedValue::from_logic9_msb_string("HZZL"),
  };
  require(
      resolve(std::span<const PackedValue>{exact_drivers})
              .to_msb_string()
          == "HLXW",
      "packed common values must use exact std_logic resolution");
}

void test_systemverilog_scalar_values() {
  using namespace fsim::runtime;
  using Arithmetic = SystemVerilogScalarArithmetic;
  using Comparison = SystemVerilogScalarComparison;
  using Error = SystemVerilogScalarError;
  using Rounding = SystemVerilogScalarRounding;

  const auto negative_zero = SystemVerilogScalarValue::real(-0.0);
  require(
      negative_zero.bits == UINT64_C(0x8000000000000000)
          && negative_zero.canonical()
              == "svruntime-scalar-v1:k=2:b=8000000000000000",
      "runtime real storage must retain canonical negative-zero bits");
  const auto short_product = systemverilog_scalar_arithmetic(
      Arithmetic::Multiply,
      SystemVerilogScalarValue::shortreal(1.5F),
      SystemVerilogScalarValue::shortreal(2.0F));
  require(
      short_product
          && short_product.value.kind == SystemVerilogScalarKind::ShortReal
          && short_product.value.bits
              == std::bit_cast<std::uint32_t>(3.0F),
      "shortreal arithmetic must round into canonical binary32 storage");
  const auto real_sum = systemverilog_scalar_arithmetic(
      Arithmetic::Add,
      SystemVerilogScalarValue::real(0.1),
      SystemVerilogScalarValue::real(0.2));
  require(
      real_sum
          && real_sum.value.bits == UINT64_C(0x3fd3333333333334),
      "real arithmetic must retain the deterministic binary64 result");
  const auto mixed_time = systemverilog_scalar_arithmetic(
      Arithmetic::Add,
      SystemVerilogScalarValue::realtime(2.5),
      SystemVerilogScalarValue::time(2));
  require(
      mixed_time
          && mixed_time.value.kind == SystemVerilogScalarKind::Realtime
          && mixed_time.value.as_real() == 4.5,
      "realtime arithmetic must promote exact ticks deterministically");
  require(
      systemverilog_scalar_arithmetic(
          Arithmetic::Add,
          SystemVerilogScalarValue::time(
              std::numeric_limits<std::uint64_t>::max()),
          SystemVerilogScalarValue::time(1)).error == Error::Overflow,
      "time addition must reject tick overflow");
  require(
      systemverilog_scalar_arithmetic(
          Arithmetic::Subtract,
          SystemVerilogScalarValue::time(10),
          SystemVerilogScalarValue::time(11)).error == Error::Overflow,
      "time subtraction must reject tick underflow");
  require(
      systemverilog_scalar_arithmetic(
          Arithmetic::Divide,
          SystemVerilogScalarValue::time(10),
          SystemVerilogScalarValue::time(0)).error == Error::DivideByZero,
      "time division must reject zero divisors");
  const auto previous_rounding = std::fegetround();
  if (std::fesetround(FE_DOWNWARD) == 0) {
    const auto rejected_rounding = systemverilog_scalar_arithmetic(
        Arithmetic::Add,
        SystemVerilogScalarValue::real(1.0),
        SystemVerilogScalarValue::real(2.0));
    (void)std::fesetround(previous_rounding);
    require(
        rejected_rounding.error == Error::UnsupportedRoundingMode,
        "real arithmetic must reject a non-nearest host rounding mode");
  }

  SystemVerilogScalarStorage bounded{{2, 32, 4}};
  const auto first = bounded.materialize(SystemVerilogScalarValue::real(1.0));
  const auto second = bounded.materialize(SystemVerilogScalarValue::time(2));
  require(
      first && second && first.id == 0 && second.id == 1
          && bounded.materialized_bytes() == 32,
      "scalar storage must use canonical resource accounting");
  require(
      bounded.materialize(SystemVerilogScalarValue::real(3.0)).error
          == Error::ResourceLimit,
      "scalar materialization must enforce value and byte limits");
  require(
      bounded.materialize(SystemVerilogScalarValue::real(
          std::numeric_limits<double>::infinity())).error
          == Error::Nonfinite,
      "scalar storage must reject nonfinite IEEE values");
  require(
      bounded.store(99, SystemVerilogScalarValue::real(0.0))
          == Error::InvalidId,
      "scalar storage must reject invalid identifiers");
  require(
      bounded.store(
          first.id,
          SystemVerilogScalarValue::real(
              std::numeric_limits<double>::infinity()))
              == Error::Nonfinite
          && bounded.load(first.id)->as_real() == 1.0,
      "rejected scalar stores leave the prior value unchanged");

  SystemVerilogScalarStorage operations{{4, 64, 1}};
  const auto lhs = operations.materialize(SystemVerilogScalarValue::time(7));
  const auto rhs = operations.materialize(SystemVerilogScalarValue::time(5));
  const auto total = operations.arithmetic(Arithmetic::Add, lhs.id, rhs.id);
  require(
      total && operations.load(total.id)->as_time() == 12
          && operations.operations() == 1,
      "scalar storage arithmetic must materialize exact tick results");
  require(
      operations.arithmetic(Arithmetic::Add, lhs.id, rhs.id).error
          == Error::ResourceLimit,
      "scalar arithmetic must enforce the deterministic work budget");

  const auto exact_left = SystemVerilogScalarValue::time(
      UINT64_C(9007199254740993));
  const auto exact_right = SystemVerilogScalarValue::time(
      UINT64_C(9007199254740992));
  require(
      systemverilog_scalar_compare(
          Comparison::Greater, exact_left, exact_right).value,
      "tick comparisons must remain exact beyond binary64 integer precision");
  require(
      systemverilog_scalar_compare(
          Comparison::Less,
          SystemVerilogScalarValue::integral(-1),
          SystemVerilogScalarValue::time(0)).value,
      "signed-integral and unsigned-time comparison must retain sign");
  require(
      !systemverilog_scalar_truth(SystemVerilogScalarValue::real(-0.0)).value
          && systemverilog_scalar_truth(
                 SystemVerilogScalarValue::shortreal(-1.0F)).value,
      "real logical truth must distinguish zero from nonzero values");
  const auto nan = SystemVerilogScalarValue::real(
      std::numeric_limits<double>::quiet_NaN());
  require(
      !systemverilog_scalar_compare(
          Comparison::Equal, nan, nan).value
          && systemverilog_scalar_compare(
                 Comparison::NotEqual, nan, nan).value
          && systemverilog_scalar_truth(nan).error == Error::Nonfinite,
      "NaN predicates and logical truth must have explicit IEEE behavior");
  const auto payload_left = encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::real(1.5));
  const auto payload_right = encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::real(2.0));
  require(payload_left && payload_right, "scalar binary payload fixtures");
  const auto payload_product = systemverilog_scalar_binary_payload(
      SystemVerilogScalarBinaryOperator::Multiply,
      payload_left.value,
      SystemVerilogScalarKind::Real,
      payload_right.value,
      SystemVerilogScalarKind::Real,
      SystemVerilogScalarKind::Real);
  const auto payload_greater = systemverilog_scalar_binary_payload(
      SystemVerilogScalarBinaryOperator::Greater,
      payload_product.value,
      SystemVerilogScalarKind::Real,
      payload_right.value,
      SystemVerilogScalarKind::Real,
      SystemVerilogScalarKind::None);
  require(
      payload_product && payload_greater
          && decode_systemverilog_scalar_payload(
                 payload_product.value,
                 SystemVerilogScalarKind::Real).value.as_real() == 3.0
          && payload_greater.value.to_msb_string() == "1",
      "scalar binary payload service must preserve typed arithmetic and predicates");

  const auto nearest = convert_systemverilog_scalar(
      SystemVerilogScalarValue::real(2.5),
      SystemVerilogScalarKind::None,
      Rounding::NearestAwayFromZero);
  const auto negative_nearest = convert_systemverilog_scalar(
      SystemVerilogScalarValue::real(-2.5),
      SystemVerilogScalarKind::None,
      Rounding::NearestAwayFromZero);
  const auto truncated = convert_systemverilog_scalar(
      SystemVerilogScalarValue::real(-2.9),
      SystemVerilogScalarKind::None,
      Rounding::TowardZero);
  const auto floored = convert_systemverilog_scalar(
      SystemVerilogScalarValue::real(-2.1),
      SystemVerilogScalarKind::None,
      Rounding::Floor);
  const auto ceiled = convert_systemverilog_scalar(
      SystemVerilogScalarValue::real(-2.9),
      SystemVerilogScalarKind::None,
      Rounding::Ceil);
  require(
      nearest.value.as_integral() == 3
          && negative_nearest.value.as_integral() == -3
          && truncated.value.as_integral() == -2
          && floored.value.as_integral() == -3
          && ceiled.value.as_integral() == -2,
      "scalar conversions must implement every explicit rounding mode");
  require(
      convert_systemverilog_scalar(
          SystemVerilogScalarValue::integral(-1),
          SystemVerilogScalarKind::Time).error == Error::Overflow
          && convert_systemverilog_scalar(
                 SystemVerilogScalarValue::time(
                     std::numeric_limits<std::uint64_t>::max()),
                 SystemVerilogScalarKind::None).error == Error::Overflow,
      "integral/time conversions must reject signed and unsigned overflow");
  const auto narrowed_real = convert_systemverilog_scalar(
      SystemVerilogScalarValue::real(1.25),
      SystemVerilogScalarKind::ShortReal);
  require(
      narrowed_real && narrowed_real.value.as_shortreal() == 1.25F,
      "real-to-shortreal conversion must materialize binary32");

  const auto signed_packed = systemverilog_scalar_from_packed(
      PackedLogic4::from_msb_string("11111111"), true,
      SystemVerilogScalarKind::Real);
  const auto unsigned_packed = systemverilog_scalar_from_packed(
      PackedLogic4::from_msb_string("11111111"), false,
      SystemVerilogScalarKind::Real);
  require(
      signed_packed.value.as_real() == -1.0
          && unsigned_packed.value.as_real() == 255.0,
      "packed-to-real conversion must retain declared signedness");
  require(
      systemverilog_scalar_from_packed(
          PackedLogic4::from_msb_string("10X1"), false,
          SystemVerilogScalarKind::Real).error == Error::UnknownValue,
      "packed X/Z values must not fabricate a real payload");
  const auto packed_rounded = systemverilog_scalar_to_packed(
      SystemVerilogScalarValue::real(3.5), 8, false);
  require(
      packed_rounded
          && packed_rounded.value.to_msb_string() == "00000100",
      "real-to-packed conversion must round before checked materialization");
  require(
      systemverilog_scalar_to_packed(
          SystemVerilogScalarValue::real(200.0), 8, true).error
          == Error::Overflow,
      "real-to-packed conversion must reject destination-width overflow");

  const auto subnormal = classify_systemverilog_scalar(
      SystemVerilogScalarValue::real(
          std::numeric_limits<double>::denorm_min()));
  const auto short_subnormal = classify_systemverilog_scalar(
      SystemVerilogScalarValue::shortreal(
          std::numeric_limits<float>::denorm_min()));
  const auto infinite = classify_systemverilog_scalar(
      SystemVerilogScalarValue::real(
          std::numeric_limits<double>::infinity()));
  const auto classified_nan = classify_systemverilog_scalar(nan);
  require(
      subnormal.subnormal && subnormal.finite && !subnormal.normal
          && short_subnormal.subnormal && short_subnormal.finite
          && infinite.infinite && !infinite.finite
          && classified_nan.nan && !classified_nan.finite,
      "runtime scalar classification must expose IEEE categories exactly");
}

void test_scheduler_phase_order() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  std::vector<std::string> events;
  scheduler.schedule(
      SchedulerPhase::active, 2, [&](Scheduler &runtime) {
        events.emplace_back("active-2");
        runtime.schedule(SchedulerPhase::inactive, 2,
                         [&](Scheduler &) { events.emplace_back("inactive"); });
        runtime.schedule(SchedulerPhase::update, 2, [&](Scheduler &update) {
          events.emplace_back("update");
          update.note_signal_change(7);
          update.schedule(SchedulerPhase::active, 1, [&](Scheduler &) {
            events.emplace_back("next-delta");
          });
        });
        runtime.schedule(SchedulerPhase::reactive, 2, [&](Scheduler &) {
          events.emplace_back("reactive");
        });
        runtime.schedule(SchedulerPhase::postponed, 2, [&](Scheduler &) {
          events.emplace_back("postponed");
        });
      });
  scheduler.schedule(SchedulerPhase::active, 1, [&](Scheduler &) {
    events.emplace_back("active-1");
  });
  scheduler.schedule_at(4, SchedulerPhase::active, 0, [&](Scheduler &) {
    events.emplace_back("time-4");
  });

  const auto result = scheduler.run();
  require(result.status == RunStatus::completed, "scheduler must complete");
  require(result.time == 4, "scheduler must advance to future event");
  const std::vector<std::string> expected = {
      "active-1", "active-2", "inactive", "update",
      "reactive", "postponed", "next-delta", "time-4"};
  require(events == expected, "scheduler phase ordering");
}

void test_scheduler_stop_resume() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  int count = 0;
  scheduler.schedule(SchedulerPhase::active, 0, [&](Scheduler &runtime) {
    ++count;
    runtime.request_stop();
  });
  scheduler.schedule(SchedulerPhase::active, 1,
                     [&](Scheduler &) { ++count; });

  require(scheduler.run().status == RunStatus::stopped,
          "stop request must stop at a callback boundary");
  require(count == 1, "pending callback must be retained");
  scheduler.clear_stop();
  require(scheduler.run().status == RunStatus::completed,
          "scheduler must resume");
  require(count == 2, "retained callback must execute after resume");
}

void test_scheduler_ownership_and_failure_containment() {
  using namespace fsim::runtime;

  Scheduler owner;
  Scheduler unrelated;
  int count = 0;
  const auto throwing = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 0,
      [](Scheduler&) { throw std::runtime_error("scheduled failure"); });
  const auto retained = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 1,
      [&](Scheduler&) { count += 10; });
  const auto cross_owner = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 2,
      [&](Scheduler&) { ++count; });
  const auto cancelled = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 3,
      [&](Scheduler&) { count += 100; });
  unrelated.cancel(cross_owner);
  owner.cancel(cancelled);
  require(
      throwing && retained && cross_owner && !cancelled,
      "cancelable handles retain owner identity before execution");

  bool caught = false;
  try {
    (void)owner.run();
  } catch (const std::runtime_error& error) {
    caught = std::string_view{error.what()} == "scheduled failure";
  }
  require(
      caught && !throwing && retained && cross_owner && !owner.running(),
      "callback failures propagate once and release scheduler run state");
  require(
      owner.run().status == RunStatus::completed && count == 11
          && !retained && !cross_owner,
      "pending callbacks survive a contained failure in stable order");

  const auto discarded = owner.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [&](Scheduler&) { ++count; });
  require(discarded && owner.has_pending(),
          "future cancelable work exposes a live handle");
  owner.discard_pending();
  require(
      !discarded && !owner.has_pending(),
      "discarding work invalidates every pending handle");

  ScheduledTaskHandle expired;
  {
    Scheduler transient;
    expired = transient.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [](Scheduler&) {});
    require(
        static_cast<bool>(expired),
        "a scheduled handle is live while its owner exists");
  }
  require(!expired, "a handle expires with its owning scheduler");

  Scheduler moving;
  const auto moved = moving.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [](Scheduler&) {});
  Scheduler destination = std::move(moving);
  moving.cancel(moved);
  require(
      static_cast<bool>(moved),
      "a moved-from scheduler cannot cancel transferred work");
  destination.cancel(moved);
  require(!moved, "scheduler moves preserve cancelable-work ownership");
}

void test_scheduler_time_limit_before_future_event() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  scheduler.schedule_at(
      10, SchedulerPhase::active, 0, [](Scheduler&) {});

  const auto limited = scheduler.run(4);
  require(
      limited.status == RunStatus::time_limit,
      "scheduler must report an intermediate time limit");
  require(
      limited.time == 4 && scheduler.now() == 4,
      "scheduler must advance to a time limit before the next future event");
  require(scheduler.has_pending(), "future event must remain pending");

  const auto completed = scheduler.run();
  require(
      completed.status == RunStatus::completed && completed.time == 10,
      "scheduler must resume from the intermediate time limit");
}

void test_scheduler_safe_point_scheduling() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  int callbacks = 0;
  bool scheduled = false;
  scheduler.schedule(
      SchedulerPhase::active, 0,
      [&](Scheduler&) { ++callbacks; });
  scheduler.set_safe_point_hook(
      [&](Scheduler& runtime, const SchedulerPhase phase) {
        if (phase == SchedulerPhase::active && !scheduled) {
          scheduled = true;
          runtime.schedule(
              SchedulerPhase::active, 1,
              [&](Scheduler&) { ++callbacks; });
        }
      });
  const auto result = scheduler.run();
  require(result.status == RunStatus::completed,
          "safe-point scheduled work must complete");
  require(callbacks == 2,
          "work scheduled into a completed phase must run next delta");
}

void test_scheduler_delta_limit() {
  using namespace fsim::runtime;

  Scheduler scheduler({3, 4});
  std::function<void(Scheduler &)> oscillate;
  oscillate = [&](Scheduler &runtime) {
    runtime.note_signal_change(42);
    runtime.schedule_next_delta(SchedulerPhase::active, 9, oscillate);
  };
  scheduler.schedule(SchedulerPhase::active, 9, oscillate);
  try {
    (void)scheduler.run();
    throw std::runtime_error("delta limit did not fire");
  } catch (const DeltaCycleLimitError &error) {
    require(error.time() == 0, "delta error time");
    require(error.limit() == 3, "delta error limit");
    require(!error.pending_orders().empty() &&
                error.pending_orders().front() == 9,
            "delta error pending process");
    require(!error.recent_signals().empty() &&
                error.recent_signals().back() == 42,
            "delta error recent signal");
  }
}

void test_simir() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.q", PackedLogic4::from_msb_string("0")});
  Process process;
  process.id = 0;
  process.name = "driver";
  process.register_count = 1;
  process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("1")});
  process.operations.emplace_back(WriteUpdate{signal, 0});
  process.operations.emplace_back(WaitFor{5});
  process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("0")});
  process.operations.emplace_back(WriteBlocking{signal, 0});
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  std::vector<std::pair<SimulationTick, std::string>> changes;
  interpreter.set_signal_change_hook(
      [&](SignalId, const PackedLogic4 &value, SimulationTick time) {
        changes.emplace_back(time, value.to_msb_string());
      });
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed, "SimIR run must complete");
  require(interpreter.signal_value(signal).to_msb_string() == "0",
          "SimIR final signal value");
  require(changes ==
              std::vector<std::pair<SimulationTick, std::string>>{
                  {0, "1"}, {5, "0"}},
          "SimIR update/delay behavior");
}

void test_simir_permanent_wait() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.unreachable", PackedLogic4::from_msb_string("0")});
  Process process;
  process.id = 0;
  process.name = "permanent_wait";
  process.register_count = 1;
  process.operations = {
      WaitForever{},
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{signal, 0},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  std::vector<ExecutionPoint> points;
  interpreter.set_execution_point_hook(
      [&](Scheduler&, const ExecutionPoint& point) {
        points.push_back(point);
      });
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "a permanently suspended process leaves the design quiescent");
  require(
      interpreter.signal_value(signal).to_msb_string() == "0",
      "operations after a permanent wait must remain unreachable");
  require(
      points.size() == 1
          && points.front().kind
              == ExecutionPointKind::process_suspend
          && points.front().instruction == 0,
      "a permanent wait remains debugger-visible as process suspension");
}

void test_simir_update_coalescing() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.q", PackedLogic4::from_msb_string("X")});
  Process process;
  process.id = 0;
  process.name = "two_nbas";
  process.register_count = 2;
  process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("0")});
  process.operations.emplace_back(WriteUpdate{signal, 0});
  process.operations.emplace_back(
      LoadConstant{1, PackedLogic4::from_msb_string("1")});
  process.operations.emplace_back(WriteUpdate{signal, 1});
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  std::vector<std::string> changes;
  interpreter.set_signal_change_hook(
      [&](SignalId, const PackedLogic4& value, SimulationTick) {
        changes.push_back(value.to_msb_string());
      });
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed, "coalesced run completes");
  require(
      changes == std::vector<std::string>{"1"},
      "one update phase must publish only the final value per signal");

  Interpreter ordered;
  const auto cross_process = ordered.add_signal(
      {"top.cross_process", PackedLogic4::from_msb_string("X")});
  const auto zero_delay = ordered.add_signal(
      {"top.zero_delay", PackedLogic4::from_msb_string("X")});
  const auto equal_deadline = ordered.add_signal(
      {"top.equal_deadline", PackedLogic4::from_msb_string("X")});
  const auto whole_then_slice = ordered.add_signal(
      {"top.whole_then_slice", PackedLogic4::from_msb_string("XXXX")});
  const auto slice_then_whole = ordered.add_signal(
      {"top.slice_then_whole", PackedLogic4::from_msb_string("XXXX")});

  const auto add_writer =
      [&](const ProcessId id,
          const std::string_view name,
          const PackedLogic4& value,
          const Operation& write) {
        Process writer;
        writer.id = id;
        writer.name = std::string{name};
        writer.register_count = 1;
        writer.operations = {
            LoadConstant{0, value},
            write,
            Halt{}};
        (void)ordered.add_process(std::move(writer));
      };
  add_writer(
      0,
      "cross_process_first",
      PackedLogic4::from_msb_string("0"),
      WriteUpdate{cross_process, 0});
  add_writer(
      1,
      "cross_process_last",
      PackedLogic4::from_msb_string("1"),
      WriteUpdate{cross_process, 0});

  Process zero_writer;
  zero_writer.id = 2;
  zero_writer.name = "zero_delay_order";
  zero_writer.register_count = 2;
  zero_writer.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WriteUpdate{zero_delay, 0},
      LoadConstant{1, PackedLogic4::from_msb_string("1")},
      WriteAfter{zero_delay, 1, 0},
      Halt{}};
  (void)ordered.add_process(std::move(zero_writer));

  add_writer(
      3,
      "equal_deadline_first",
      PackedLogic4::from_msb_string("0"),
      WriteAfter{equal_deadline, 0, 5});
  add_writer(
      4,
      "equal_deadline_last",
      PackedLogic4::from_msb_string("1"),
      WriteAfter{equal_deadline, 0, 5});
  add_writer(
      5,
      "whole_before_slice",
      PackedLogic4::from_msb_string("1010"),
      WriteUpdate{whole_then_slice, 0});
  add_writer(
      6,
      "slice_after_whole",
      PackedLogic4::from_msb_string("11"),
      WriteUpdateSlice{whole_then_slice, 0, 1});
  add_writer(
      7,
      "slice_before_whole",
      PackedLogic4::from_msb_string("11"),
      WriteUpdateSlice{slice_then_whole, 0, 1});
  add_writer(
      8,
      "whole_after_slice",
      PackedLogic4::from_msb_string("1010"),
      WriteUpdate{slice_then_whole, 0});

  struct OrderedChange {
    SignalId signal{};
    std::string value;
    SimulationTick time{};
  };
  std::vector<OrderedChange> ordered_changes;
  ordered.set_signal_change_hook(
      [&](const SignalId changed,
          const PackedLogic4& value,
          const SimulationTick time) {
        ordered_changes.push_back(
            {changed, value.to_msb_string(), time});
      });
  const auto ordered_result = ordered.run();
  require(
      ordered_result.status == RunStatus::completed
          && ordered_result.time == 5,
      "ordered NBA scenarios complete through their last deadline");
  require(
      ordered.signal_value(cross_process).to_msb_string() == "1",
      "stable process order gives the later same-slot NBA precedence");
  require(
      ordered.signal_value(zero_delay).to_msb_string() == "1",
      "a zero-delay NBA joins the current update slot after an immediate "
      "NBA from the same process");
  require(
      ordered.signal_value(equal_deadline).to_msb_string() == "1",
      "equal future deadlines retain stable process ordering");
  require(
      ordered.signal_value(whole_then_slice).to_msb_string() == "1110",
      "a later partial NBA overrides its overlapping whole-value bits");
  require(
      ordered.signal_value(slice_then_whole).to_msb_string() == "1010",
      "a later whole-value NBA overrides an earlier partial assignment");
  for (const auto target :
       {cross_process,
        zero_delay,
        equal_deadline,
        whole_then_slice,
        slice_then_whole}) {
    require(
        std::ranges::count_if(
            ordered_changes,
            [&](const OrderedChange& change) {
              return change.signal == target;
            })
            == 1,
        "each coalesced target publishes exactly one committed change");
  }
  require(
      std::ranges::any_of(
          ordered_changes,
          [&](const OrderedChange& change) {
            return change.signal == equal_deadline
                && change.time == 5
                && change.value == "1";
          }),
      "the equal-deadline winner commits at the requested future time");
}

void test_resolved_driver_slots() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto whole = interpreter.add_signal(
      {
          "top.whole",
          PackedLogic4::from_msb_string("ZZZZ"),
          ResolutionKind::sv_wire});
  const auto sliced = interpreter.add_signal(
      {
          "top.sliced",
          PackedLogic4::from_msb_string("ZZZZ"),
          ResolutionKind::sv_wire});
  const auto standard_logic = interpreter.add_signal(
      {
          "top.standard_logic",
          PackedLogic4::from_msb_string("X"),
          ResolutionKind::std_logic});
  const auto strength_resolved = interpreter.add_signal(
      {
          "top.strength_resolved",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_wire});
  const auto switch_pair = interpreter.add_signal(
      {
          "top.switch_pair",
          PackedLogic4::from_msb_string("ZZ"),
          ResolutionKind::sv_wire});
  const auto switch_triplet = interpreter.add_signal(
      {
          "top.switch_triplet",
          PackedLogic4::from_msb_string("ZZZ"),
          ResolutionKind::sv_wire});

  Process first;
  first.id = 0;
  first.name = "first_driver";
  first.register_count = 4;
  first.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0000")},
      WriteUpdate{whole, 0},
      LoadConstant{1, PackedLogic4::from_msb_string("ZZZZ")},
      WriteAfter{whole, 1, 5},
      LoadConstant{2, PackedLogic4::from_msb_string("10")},
      WriteUpdateSlice{sliced, 2, 0},
      LoadConstant{3, PackedLogic4::from_msb_string("0")},
      WriteUpdate{standard_logic, 3},
      Halt{}};
  (void)interpreter.add_process(std::move(first));

  Process second;
  second.id = 1;
  second.name = "second_driver";
  second.register_count = 5;
  second.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1111")},
      WriteUpdate{whole, 0},
      LoadConstant{1, PackedLogic4::from_msb_string("0011")},
      WriteAfter{whole, 1, 3},
      LoadConstant{2, PackedLogic4::from_msb_string("11")},
      WriteUpdateSlice{sliced, 2, 2},
      LoadConstant{3, PackedLogic4::from_msb_string("01")},
      WriteAfterSlice{sliced, 3, 0, 4},
      LoadConstant{4, PackedLogic4::from_msb_string("Z")},
      WriteUpdate{standard_logic, 4},
      Halt{}};
  (void)interpreter.add_process(std::move(second));

  Process weak_zero;
  weak_zero.id = 2;
  weak_zero.name = "weak_zero_driver";
  weak_zero.register_count = 1;
  weak_zero.drive_strength = {
      StrengthRank::weak, StrengthRank::weak};
  weak_zero.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WriteUpdate{strength_resolved, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(weak_zero));

  Process strong_one;
  strong_one.id = 3;
  strong_one.name = "strong_one_driver";
  strong_one.register_count = 1;
  strong_one.drive_strength = {
      StrengthRank::strong, StrengthRank::strong};
  strong_one.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteUpdate{strength_resolved, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(strong_one));

  const auto rejects_process = [&](Process process) {
    try {
      (void)interpreter.add_process(std::move(process));
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  Process invalid_strength;
  invalid_strength.id = 4;
  invalid_strength.name = "invalid_strength";
  invalid_strength.drive_strength.zero =
      static_cast<StrengthRank>(255);
  invalid_strength.operations = {Halt{}};
  require(
      rejects_process(std::move(invalid_strength)),
      "runtime construction rejects an invalid strength rank");
  Process incomplete_switch;
  incomplete_switch.id = 4;
  incomplete_switch.name = "incomplete_switch";
  incomplete_switch.switch_bidirectional = true;
  incomplete_switch.operations = {Halt{}};
  require(
      rejects_process(std::move(incomplete_switch)),
      "runtime construction rejects an incomplete transmission edge");
  Process incompatible_switch;
  incompatible_switch.id = 4;
  incompatible_switch.name = "incompatible_switch";
  incompatible_switch.switch_source = switch_pair;
  incompatible_switch.switch_target = switch_triplet;
  incompatible_switch.switch_bidirectional = true;
  incompatible_switch.operations = {Halt{}};
  require(
      rejects_process(std::move(incompatible_switch)),
      "runtime construction rejects incompatible transmission widths");

  struct Change {
    SignalId signal{};
    std::string value;
    SimulationTick time{};
  };
  std::vector<Change> changes;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        changes.push_back(
            {signal, value.to_msb_string(), time});
      });
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 5,
      "resolved driver simulation reaches its final transaction");
  require(
      interpreter.signal_value(whole).to_msb_string() == "0011",
      "a released wire driver exposes the other process slot");
  require(
      interpreter.driver_value(0, whole).to_msb_string() == "ZZZZ"
          && interpreter.driver_value(1, whole).to_msb_string()
              == "0011",
      "whole-signal process driver slots retain independent values");
  require(
      interpreter.signal_value(sliced).to_msb_string() == "11XX",
      "partial driver slots resolve disjoint and overlapping packed bits");
  require(
      interpreter.signal_value(strength_resolved).to_msb_string() == "1",
      "the strongest opposing Verilog driver determines the visible value");
  require(
      interpreter.driver_value(0, sliced).to_msb_string() == "ZZ10"
          && interpreter.driver_value(1, sliced).to_msb_string()
              == "1101",
      "slice writes update only the issuing process's full driver slot");
  require(
      interpreter.signal_value(standard_logic).to_msb_string() == "0",
      "the supported std_logic 0/1/X/Z subset uses standard resolution");

  const auto whole_changes =
      [&] {
        std::vector<std::pair<std::string, SimulationTick>> observed;
        for (const auto& change : changes) {
          if (change.signal == whole) {
            observed.emplace_back(change.value, change.time);
          }
        }
        return observed;
      }();
  require(
      whole_changes
          == std::vector<std::pair<std::string, SimulationTick>>{
              {"XXXX", 0}, {"00XX", 3}, {"0011", 5}},
      "NBA and future driver updates resolve once per destination slot");

  Interpreter forced;
  const auto forced_signal = forced.add_signal(
      {
          "top.forced",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_wire});
  Process forced_first;
  forced_first.id = 0;
  forced_first.name = "forced_first";
  forced_first.register_count = 1;
  forced_first.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WriteAfter{forced_signal, 0, 2},
      Halt{}};
  (void)forced.add_process(std::move(forced_first));
  Process forced_second;
  forced_second.id = 1;
  forced_second.name = "forced_second";
  forced_second.register_count = 1;
  forced_second.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("Z")},
      WriteAfter{forced_signal, 0, 2},
      Halt{}};
  (void)forced.add_process(std::move(forced_second));
  forced.force_signal(
      forced_signal,
      PackedLogic4::from_msb_string("1"));
  const auto forced_result = forced.run();
  require(
      forced_result.status == RunStatus::completed
          && forced.signal_value(forced_signal).to_msb_string() == "1",
      "a force masks resolved driver activity through completion");
  require(
      forced.driver_value(0, forced_signal).to_msb_string() == "0"
          && forced.driver_value(1, forced_signal).to_msb_string() == "Z",
      "resolved drivers continue updating beneath a force");
  forced.release_signal(forced_signal);
  require(
      forced.signal_value(forced_signal).to_msb_string() == "0",
      "force release publishes the latest resolved underlying value");
}

void test_simir_expressions_and_edges() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto clock = interpreter.add_signal(
      {"top.clock", PackedLogic4::from_msb_string("0")});
  const auto edge_seen = interpreter.add_signal(
      {"top.edge_seen", PackedLogic4::from_msb_string("0")});
  const auto expression = interpreter.add_signal(
      {"top.expression", PackedLogic4::from_msb_string("0000")});

  Process edge_process;
  edge_process.id = 0;
  edge_process.name = "posedge_observer";
  edge_process.register_count = 1;
  edge_process.static_sensitivity.push_back({clock, EdgeKind::posedge});
  edge_process.operations.emplace_back(WaitSensitivity{});
  edge_process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("1")});
  edge_process.operations.emplace_back(WriteBlocking{edge_seen, 0});
  edge_process.operations.emplace_back(Jump{0});
  (void)interpreter.add_process(std::move(edge_process));

  Process expression_process;
  expression_process.id = 1;
  expression_process.name = "expression";
  expression_process.register_count = 4;
  expression_process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("0011")});
  expression_process.operations.emplace_back(
      LoadConstant{1, PackedLogic4::from_msb_string("0001")});
  expression_process.operations.emplace_back(
      Binary{BinaryOperator::add_unsigned, 2, 0, 1});
  expression_process.operations.emplace_back(UnaryNot{3, 2});
  expression_process.operations.emplace_back(WriteBlocking{expression, 3});
  expression_process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(expression_process));

  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("1"), 5, 0);
  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("0"), 10, 0);
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed,
          "edge-sensitive SimIR run must complete");
  require(interpreter.signal_value(edge_seen).to_msb_string() == "1",
          "posedge must activate a waiting process");
  require(interpreter.signal_value(expression).to_msb_string() == "1011",
      "SimIR add and unary-not operations");
}

void test_simir_noninitializing_static_process() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto trigger = interpreter.add_signal(
      {"top.trigger", PackedLogic4::from_msb_string("0")});
  const auto observed = interpreter.add_signal(
      {"top.observed", PackedLogic4::from_msb_string("0")});

  Process process;
  process.id = 0;
  process.name = "dont_initialize";
  process.register_count = 1;
  process.static_sensitivity.push_back({trigger, EdgeKind::any});
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{observed, 0},
      Halt{},
  };
  process.initialize = false;
  (void)interpreter.add_process(std::move(process));
  interpreter.schedule_signal_at(
      trigger, PackedLogic4::from_msb_string("1"), 1, 0);

  const auto before_event = interpreter.run(0);
  require(
      before_event.status == RunStatus::time_limit
          && interpreter.signal_value(observed).to_msb_string() == "0",
      "a noninitializing static process must not execute at time zero");
  const auto after_event = interpreter.run();
  require(
      after_event.status == RunStatus::completed
          && after_event.time == 1
          && interpreter.signal_value(observed).to_msb_string() == "1",
      "a noninitializing static process must wake on its sensitivity");
}

void test_simir_wide_truth_and_comparison() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto greater = interpreter.add_signal(
      {"top.greater", PackedLogic4::from_msb_string("0")});
  const auto logical_not_known = interpreter.add_signal(
      {"top.logical_not_known", PackedLogic4::from_msb_string("X")});
  const auto logical_not_unknown = interpreter.add_signal(
      {"top.logical_not_unknown", PackedLogic4::from_msb_string("0")});
  const auto case_equal_unknown = interpreter.add_signal(
      {"top.case_equal_unknown", PackedLogic4::from_msb_string("0")});
  const auto case_equal_distinct = interpreter.add_signal(
      {"top.case_equal_distinct", PackedLogic4::from_msb_string("1")});
  const auto case_not_equal_distinct = interpreter.add_signal(
      {"top.case_not_equal_distinct",
       PackedLogic4::from_msb_string("0")});

  Process process;
  process.id = 0;
  process.name = "wide_truth_and_comparison";
  process.register_count = 12;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string(
                 "1" + std::string(64, '0'))},
      LoadConstant{
          1, PackedLogic4::from_msb_string(
                 "0" + std::string(64, '1'))},
      Binary{BinaryOperator::greater_unsigned, 2, 0, 1},
      WriteBlocking{greater, 2},
      LogicalNot{3, 0},
      WriteBlocking{logical_not_known, 3},
      LoadConstant{
          4, PackedLogic4::from_msb_string(
                 "X" + std::string(64, '0'))},
      LogicalNot{5, 4},
      WriteBlocking{logical_not_unknown, 5},
      LoadConstant{
          6, PackedLogic4::from_msb_string(
                 "X" + std::string(64, '0'))},
      LoadConstant{
          7, PackedLogic4::from_msb_string(
                 "X" + std::string(64, '0'))},
      Binary{BinaryOperator::case_equal, 8, 6, 7},
      WriteBlocking{case_equal_unknown, 8},
      LoadConstant{
          9, PackedLogic4::from_msb_string(
                 "Z" + std::string(64, '0'))},
      Binary{BinaryOperator::case_equal, 10, 6, 9},
      WriteBlocking{case_equal_distinct, 10},
      UnaryNot{11, 10},
      WriteBlocking{case_not_equal_distinct, 11},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed,
          "wide comparison process completes");
  require(interpreter.signal_value(greater).to_msb_string() == "1",
          "wide unsigned comparison uses high bits");
  require(
      interpreter.signal_value(logical_not_known).to_msb_string()
          == "0",
      "known one dominates wide logical negation");
  require(
      interpreter.signal_value(logical_not_unknown).to_msb_string()
          == "X",
      "wide unknown-only truth value remains unknown");
  require(
      interpreter.signal_value(case_equal_unknown).to_msb_string()
          == "1",
      "wide case equality matches identical unknown bits");
  require(
      interpreter.signal_value(case_equal_distinct).to_msb_string()
              == "0"
          && interpreter.signal_value(case_not_equal_distinct)
                  .to_msb_string()
              == "1",
      "wide case equality distinguishes X from Z");
}

void test_simir_wildcard_case_matching() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 8> results{};
  for (std::size_t index = 0; index < results.size(); ++index) {
    results[index] = interpreter.add_signal(
        {"top.wildcard_" + std::to_string(index),
         PackedLogic4::from_msb_string("X")});
  }

  struct Match {
    BinaryOperator operation;
    std::string_view lhs;
    std::string_view rhs;
  };
  const std::array matches{
      Match{BinaryOperator::casez_equal, "10Z1", "1011"},
      Match{BinaryOperator::casez_equal, "10X1", "1011"},
      Match{BinaryOperator::casez_equal, "1011", "10Z1"},
      Match{BinaryOperator::casez_equal, "10X1", "10X1"},
      Match{BinaryOperator::casex_equal, "10X1", "1011"},
      Match{BinaryOperator::casex_equal, "10Z1", "1001"},
      Match{BinaryOperator::casex_equal, "11X1", "10Z1"},
      Match{BinaryOperator::casex_equal, "1101", "1001"},
  };
  Process process;
  process.id = 0;
  process.name = "wildcard_case_matching";
  process.register_count = 3;
  for (std::size_t index = 0; index < matches.size(); ++index) {
    process.operations.emplace_back(LoadConstant{
        0, PackedLogic4::from_msb_string(matches[index].lhs)});
    process.operations.emplace_back(LoadConstant{
        1, PackedLogic4::from_msb_string(matches[index].rhs)});
    process.operations.emplace_back(
        Binary{matches[index].operation, 2, 0, 1});
    process.operations.emplace_back(WriteBlocking{results[index], 2});
  }
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  require(
      interpreter.run().status == RunStatus::completed,
      "wildcard case comparison process completes");
  const std::array expected{"1", "0", "1", "1", "1", "1", "0", "0"};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(
        interpreter.signal_value(results[index]).to_msb_string()
            == expected[index],
        "casez/casex wildcard truth table");
  }
}

void test_simir_wildcard_equality() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 9> results{};
  for (std::size_t index = 0; index < results.size(); ++index) {
    results[index] = interpreter.add_signal(
        {"top.wildcard_equality_" + std::to_string(index),
         PackedLogic4::from_msb_string("0")});
  }

  struct Comparison {
    BinaryOperator operation;
    std::string_view lhs;
    std::string_view rhs;
  };
  const std::array comparisons{
      Comparison{BinaryOperator::wildcard_equal, "1001", "10X1"},
      Comparison{BinaryOperator::wildcard_equal, "10Z1", "10Z1"},
      Comparison{BinaryOperator::wildcard_equal, "10X1", "1011"},
      Comparison{BinaryOperator::wildcard_equal, "10Z1", "1011"},
      Comparison{BinaryOperator::wildcard_equal, "1101", "10Z1"},
      Comparison{BinaryOperator::wildcard_equal, "X101", "0001"},
      Comparison{BinaryOperator::wildcard_equal, "1001", "1001"},
      Comparison{BinaryOperator::wildcard_equal, "1001", "1101"},
      Comparison{BinaryOperator::equal, "X0", "X1"},
  };
  Process process;
  process.id = 0;
  process.name = "wildcard_equality";
  process.register_count = 3;
  for (std::size_t index = 0; index < comparisons.size(); ++index) {
    process.operations.emplace_back(LoadConstant{
        0,
        PackedLogic4::from_msb_string(comparisons[index].lhs)});
    process.operations.emplace_back(LoadConstant{
        1,
        PackedLogic4::from_msb_string(comparisons[index].rhs)});
    process.operations.emplace_back(Binary{
        comparisons[index].operation, 2, 0, 1});
    process.operations.emplace_back(WriteBlocking{results[index], 2});
  }
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  require(
      interpreter.run().status == RunStatus::completed,
      "wildcard equality process completes");
  const std::array expected{
      "1", "1", "X", "X", "0", "X", "1", "0", "X"};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(
        interpreter.signal_value(results[index]).to_msb_string()
            == expected[index],
        "one-sided wildcard equality truth table");
  }
}

void test_simir_vhdl_matching_equality() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  struct Comparison {
    std::string_view lhs;
    std::string_view rhs;
    std::string_view expected;
  };
  const std::array comparisons{
      Comparison{"10LH", "1001", "1"},
      Comparison{"10LH", "10--", "1"},
      Comparison{"----", "UXZW", "1"},
      Comparison{"UXZW", "----", "1"},
      Comparison{"UXZW", "UXZW", "0"},
      Comparison{"10LH", "1010", "0"},
      Comparison{"01", "01", "1"},
      Comparison{"01", "11", "0"},
  };
  Interpreter interpreter;
  std::array<SignalId, comparisons.size()> results{};
  Process process;
  process.id = 0;
  process.name = "vhdl_matching_equality";
  process.register_count = 3;
  process.register_value_kinds = {
      ValueKind::logic9, ValueKind::logic9, ValueKind::logic4};
  for (std::size_t index = 0; index < comparisons.size(); ++index) {
    results[index] = interpreter.add_signal(
        {"top.vhdl_match_" + std::to_string(index),
         PackedLogic4::from_msb_string("X")});
    process.operations.emplace_back(LoadConstant{
        0,
        PackedLogic4::from_logic9_msb_string(comparisons[index].lhs)});
    process.operations.emplace_back(LoadConstant{
        1,
        PackedLogic4::from_logic9_msb_string(comparisons[index].rhs)});
    process.operations.emplace_back(Binary{
        BinaryOperator::vhdl_match_equal, 2, 0, 1});
    process.operations.emplace_back(WriteBlocking{results[index], 2});
  }
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));
  require(
      interpreter.run().status == RunStatus::completed,
      "VHDL matching equality process completes");
  for (std::size_t index = 0; index < comparisons.size(); ++index) {
    const auto observed =
        interpreter.signal_value(results[index]).to_msb_string();
    if (observed != comparisons[index].expected) {
      throw std::runtime_error(
          "VHDL matching truth-table row " + std::to_string(index)
          + " expected " + std::string{comparisons[index].expected}
          + " but observed " + observed);
    }
  }
}

void test_simir_wide_reduction_and_shift() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto reduced_and = interpreter.add_signal(
      {"top.reduced_and", PackedLogic4::from_msb_string("X")});
  const auto reduced_or = interpreter.add_signal(
      {"top.reduced_or", PackedLogic4::from_msb_string("X")});
  const auto reduced_xor = interpreter.add_signal(
      {"top.reduced_xor", PackedLogic4::from_msb_string("0")});
  const auto one_hot = interpreter.add_signal(
      {"top.one_hot", PackedLogic4::from_msb_string("0")});
  const auto one_hot_or_zero = interpreter.add_signal(
      {"top.one_hot_or_zero", PackedLogic4::from_msb_string("0")});
  const auto one_count = interpreter.add_signal(
      {"top.one_count", PackedLogic4(32, Logic4::zero)});
  const auto selected_count = interpreter.add_signal(
      {"top.selected_count", PackedLogic4(32, Logic4::zero)});
  const auto shifted_left = interpreter.add_signal(
      {"top.shifted_left", PackedLogic4(65, Logic4::x)});
  const auto shifted_right = interpreter.add_signal(
      {"top.shifted_right", PackedLogic4(65, Logic4::x)});
  const auto shifted_unknown = interpreter.add_signal(
      {"top.shifted_unknown", PackedLogic4(65, Logic4::zero)});
  const auto shifted_oversized = interpreter.add_signal(
      {"top.shifted_oversized", PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic = interpreter.add_signal(
      {"top.shifted_arithmetic", PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic_oversized =
      interpreter.add_signal(
          {"top.shifted_arithmetic_oversized",
           PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic_left = interpreter.add_signal(
      {"top.shifted_arithmetic_left", PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic_left_oversized =
      interpreter.add_signal(
          {"top.shifted_arithmetic_left_oversized",
           PackedLogic4(65, Logic4::x)});
  const auto rotated_left = interpreter.add_signal(
      {"top.rotated_left", PackedLogic4(65, Logic4::x)});
  const auto rotated_right = interpreter.add_signal(
      {"top.rotated_right", PackedLogic4(65, Logic4::x)});
  const auto rotated_full_width = interpreter.add_signal(
      {"top.rotated_full_width", PackedLogic4(65, Logic4::x)});

  const auto source_text =
      "1" + std::string(63, '0') + "Z";
  Process process;
  process.id = 0;
  process.name = "wide_reduction_and_shift";
  process.register_count = 22;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string(source_text)},
      LoadConstant{
          1, PackedLogic4::from_msb_string("0000001")},
      Reduction{ReductionOperator::bit_and, 2, 0},
      WriteBlocking{reduced_and, 2},
      Reduction{ReductionOperator::bit_or, 3, 0},
      WriteBlocking{reduced_or, 3},
      Reduction{ReductionOperator::bit_xor, 4, 0},
      WriteBlocking{reduced_xor, 4},
      Reduction{ReductionOperator::one_hot, 18, 0},
      WriteBlocking{one_hot, 18},
      Reduction{
          ReductionOperator::one_hot_or_zero, 19, 0},
      WriteBlocking{one_hot_or_zero, 19},
      CountOnes{20, 0},
      WriteBlocking{one_count, 20},
      CountBits{21, 0, 0x9U},
      WriteBlocking{selected_count, 21},
      Shift{ShiftOperator::logical_left, 5, 0, 1},
      WriteBlocking{shifted_left, 5},
      Shift{ShiftOperator::logical_right, 6, 0, 1},
      WriteBlocking{shifted_right, 6},
      LoadConstant{
          7, PackedLogic4::from_msb_string("00000X1")},
      Shift{ShiftOperator::logical_left, 8, 0, 7},
      WriteBlocking{shifted_unknown, 8},
      LoadConstant{
          9, PackedLogic4::from_msb_string("1000001")},
      Shift{ShiftOperator::logical_right, 10, 0, 9},
      WriteBlocking{shifted_oversized, 10},
      Shift{ShiftOperator::arithmetic_right, 11, 0, 1},
      WriteBlocking{shifted_arithmetic, 11},
      Shift{ShiftOperator::arithmetic_right, 12, 0, 9},
      WriteBlocking{shifted_arithmetic_oversized, 12},
      Shift{ShiftOperator::arithmetic_left, 13, 0, 1},
      WriteBlocking{shifted_arithmetic_left, 13},
      Shift{ShiftOperator::arithmetic_left, 14, 0, 9},
      WriteBlocking{shifted_arithmetic_left_oversized, 14},
      Shift{ShiftOperator::rotate_left, 15, 0, 1},
      WriteBlocking{rotated_left, 15},
      Shift{ShiftOperator::rotate_right, 16, 0, 1},
      WriteBlocking{rotated_right, 16},
      Shift{ShiftOperator::rotate_left, 17, 0, 9},
      WriteBlocking{rotated_full_width, 17},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide reduction and shift process completes");
  require(
      interpreter.signal_value(reduced_and).to_msb_string() == "0"
          && interpreter.signal_value(reduced_or).to_msb_string()
              == "1"
          && interpreter.signal_value(reduced_xor).to_msb_string()
              == "X",
      "wide four-state reductions honor controlling values");
  require(
      interpreter.signal_value(one_hot).to_msb_string() == "1"
          && interpreter.signal_value(one_hot_or_zero)
                 .to_msb_string()
              == "1",
      "wide one-hot reductions count exact one bits and ignore X/Z");
  require(
      interpreter.signal_value(one_count).low_word().aval == 1
          && interpreter.signal_value(one_count).low_word().bval == 0,
      "wide count-ones counts exact one bits and ignores X/Z");
  require(
      interpreter.signal_value(selected_count).low_word().aval == 64
          && interpreter.signal_value(selected_count)
                 .low_word()
                 .bval
              == 0,
      "wide count-bits selects exact zero and Z states");
  require(
      interpreter.signal_value(shifted_left).to_msb_string()
          == std::string(63, '0') + "Z0",
      "wide logical left shift crosses packed storage words");
  require(
      interpreter.signal_value(shifted_right).to_msb_string()
          == "01" + std::string(63, '0'),
      "wide logical right shift crosses packed storage words");
  require(
      interpreter.signal_value(shifted_unknown).to_msb_string()
          == std::string(65, 'X'),
      "wide shift with an unknown amount produces all unknown bits");
  require(
      interpreter.signal_value(shifted_oversized).to_msb_string()
          == std::string(65, '0'),
      "wide oversized shift produces zero");
  require(
      interpreter.signal_value(shifted_arithmetic).to_msb_string()
          == "11" + std::string(63, '0'),
      "wide arithmetic right shift replicates the sign bit");
  require(
      interpreter.signal_value(shifted_arithmetic_oversized)
              .to_msb_string()
          == std::string(65, '1'),
      "wide oversized arithmetic right shift fills with the sign bit");
  require(
      interpreter.signal_value(shifted_arithmetic_left)
              .to_msb_string()
          == std::string(63, '0') + "ZZ",
      "wide arithmetic left shift fills with the rightmost element");
  require(
      interpreter.signal_value(shifted_arithmetic_left_oversized)
              .to_msb_string()
          == std::string(65, 'Z'),
      "wide oversized arithmetic left shift fills with the rightmost element");
  require(
      interpreter.signal_value(rotated_left).to_msb_string()
          == std::string(63, '0') + "Z1",
      "wide rotate left wraps the leftmost element");
  require(
      interpreter.signal_value(rotated_right).to_msb_string()
          == "Z1" + std::string(63, '0'),
      "wide rotate right wraps the rightmost element");
  require(
      interpreter.signal_value(rotated_full_width).to_msb_string()
          == source_text,
      "wide rotate reduces its amount modulo the operand width");
}

void test_simir_signed_shift_counts() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 6> outputs{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    outputs[index] = interpreter.add_signal({
        "top.signed_shift_" + std::to_string(index),
        PackedLogic4(65, Logic4::x)});
  }

  const auto source =
      "1" + std::string(63, '0') + "Z";
  Process process;
  process.id = 0;
  process.name = "signed_shift_counts";
  process.register_count = 8;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string(source)},
      LoadConstant{
          1,
          PackedLogic4::from_msb_string(
              std::string(70, '1'))},
      Shift{
          ShiftOperator::logical_left, 2, 0, 1, true},
      WriteBlocking{outputs[0], 2},
      Shift{
          ShiftOperator::logical_right, 3, 0, 1, true},
      WriteBlocking{outputs[1], 3},
      Shift{
          ShiftOperator::arithmetic_left, 4, 0, 1, true},
      WriteBlocking{outputs[2], 4},
      Shift{
          ShiftOperator::arithmetic_right, 5, 0, 1, true},
      WriteBlocking{outputs[3], 5},
      Shift{
          ShiftOperator::rotate_left, 6, 0, 1, true},
      WriteBlocking{outputs[4], 6},
      Shift{
          ShiftOperator::rotate_right, 7, 0, 1, true},
      WriteBlocking{outputs[5], 7},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "signed-count shift process completes");
  const std::array expected{
      "01" + std::string(63, '0'),
      std::string(63, '0') + "Z0",
      "11" + std::string(63, '0'),
      std::string(63, '0') + "ZZ",
      "Z1" + std::string(63, '0'),
      std::string(63, '0') + "Z1"};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index]).to_msb_string()
            == expected[index],
        "negative arbitrary-width shift count reverses its operation");
  }
}

void test_simir_wide_unsigned_arithmetic() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 9> outputs{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    outputs[index] = interpreter.add_signal({
        "top.arithmetic_" + std::to_string(index),
        PackedLogic4(65, Logic4::zero)});
  }

  const auto lhs =
      "1" + std::string(64, '0');
  const auto rhs =
      std::string(63, '0') + "11";
  Process process;
  process.id = 0;
  process.name = "wide_unsigned_arithmetic";
  process.register_count = 15;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string(lhs)},
      LoadConstant{1, PackedLogic4::from_msb_string(rhs)},
      Binary{BinaryOperator::add_unsigned, 2, 0, 1},
      WriteBlocking{outputs[0], 2},
      Binary{BinaryOperator::subtract_unsigned, 3, 0, 1},
      WriteBlocking{outputs[1], 3},
      Binary{BinaryOperator::multiply_unsigned, 4, 0, 1},
      WriteBlocking{outputs[2], 4},
      Binary{BinaryOperator::divide_unsigned, 5, 0, 1},
      WriteBlocking{outputs[3], 5},
      Binary{BinaryOperator::modulo_unsigned, 6, 0, 1},
      WriteBlocking{outputs[4], 6},
      LoadConstant{
          7,
          PackedLogic4::from_msb_string(
              "X" + std::string(64, '0'))},
      Binary{BinaryOperator::add_unsigned, 8, 7, 1},
      WriteBlocking{outputs[5], 8},
      LoadConstant{9, PackedLogic4(65, Logic4::zero)},
      Binary{BinaryOperator::divide_unsigned, 10, 0, 9},
      WriteBlocking{outputs[6], 10},
      LoadConstant{
          11,
          PackedLogic4::from_msb_string(
              std::string(63, '0') + "11")},
      LoadConstant{
          12,
          PackedLogic4::from_msb_string(
              std::string(62, '0') + "100")},
      Binary{BinaryOperator::power_unsigned, 13, 11, 12},
      WriteBlocking{outputs[7], 13},
      Binary{BinaryOperator::power_unsigned, 14, 7, 12},
      WriteBlocking{outputs[8], 14},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide unsigned arithmetic process completes");
  const std::array expected{
      "1" + std::string(62, '0') + "11",
      "0" + std::string(62, '1') + "01",
      lhs,
      "0" + std::string{"01010101010101010101010101010101"
                        "01010101010101010101010101010101"},
      std::string(64, '0') + "1",
      std::string(65, 'X'),
      std::string(65, 'X'),
      std::string(58, '0') + "1010001",
      std::string(65, 'X')};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index]).to_msb_string()
            == expected[index],
        "wide unsigned arithmetic result");
  }
}

void test_simir_wide_signed_arithmetic() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto signed_value =
      [](const std::int64_t value, const std::size_t width) {
        PackedLogic4 result(width, Logic4::zero);
        const auto encoded = static_cast<std::uint64_t>(value);
        for (std::size_t bit = 0; bit < width; ++bit) {
          const bool one =
              bit < 64
                  ? ((encoded >> bit) & UINT64_C(1)) != 0
                  : value < 0;
          result.set(bit, one ? Logic4::one : Logic4::zero);
        }
        return result;
      };

  Interpreter interpreter;
  std::array<SignalId, 10> outputs{};
  for (std::size_t index = 0; index < 6; ++index) {
    outputs[index] = interpreter.add_signal(
        {"top.signed_" + std::to_string(index),
         PackedLogic4(65, Logic4::zero)});
  }
  outputs[6] = interpreter.add_signal(
      {"top.signed_less", PackedLogic4(1, Logic4::zero)});
  outputs[7] = interpreter.add_signal(
      {"top.signed_greater", PackedLogic4(1, Logic4::zero)});
  outputs[8] = interpreter.add_signal(
      {"top.signed_overflow", PackedLogic4(65, Logic4::zero)});
  outputs[9] = interpreter.add_signal(
      {"top.signed_unknown", PackedLogic4(65, Logic4::zero)});

  auto minimum = PackedLogic4(65, Logic4::zero);
  minimum.set(64, Logic4::one);
  auto unknown = PackedLogic4(65, Logic4::zero);
  unknown.set(37, Logic4::x);

  Process process;
  process.id = 0;
  process.name = "wide_signed_arithmetic";
  process.register_count = 15;
  process.operations = {
      LoadConstant{0, signed_value(-5, 65)},
      LoadConstant{1, signed_value(3, 65)},
      Binary{BinaryOperator::add_signed, 2, 0, 1},
      WriteBlocking{outputs[0], 2},
      Binary{BinaryOperator::subtract_signed, 3, 0, 1},
      WriteBlocking{outputs[1], 3},
      Binary{BinaryOperator::multiply_signed, 4, 0, 1},
      WriteBlocking{outputs[2], 4},
      Binary{BinaryOperator::divide_signed, 5, 0, 1},
      WriteBlocking{outputs[3], 5},
      Binary{BinaryOperator::remainder_signed, 6, 0, 1},
      WriteBlocking{outputs[4], 6},
      Binary{BinaryOperator::modulo_signed, 7, 0, 1},
      WriteBlocking{outputs[5], 7},
      Binary{BinaryOperator::less_signed, 8, 0, 1},
      WriteBlocking{outputs[6], 8},
      Binary{BinaryOperator::greater_signed, 9, 0, 1},
      WriteBlocking{outputs[7], 9},
      LoadConstant{10, std::move(minimum)},
      LoadConstant{11, signed_value(-1, 65)},
      Binary{BinaryOperator::divide_signed, 12, 10, 11},
      WriteBlocking{outputs[8], 12},
      LoadConstant{13, std::move(unknown)},
      Binary{BinaryOperator::divide_signed, 14, 13, 1},
      WriteBlocking{outputs[9], 14},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide signed arithmetic process completes");
  const std::array expected{
      signed_value(-2, 65),
      signed_value(-8, 65),
      signed_value(-15, 65),
      signed_value(-1, 65),
      signed_value(-2, 65),
      signed_value(1, 65)};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index]) == expected[index],
        "wide signed arithmetic result");
  }
  require(
      interpreter.signal_value(outputs[6]).to_msb_string() == "1"
          && interpreter.signal_value(outputs[7]).to_msb_string()
              == "0",
      "wide signed relational ordering");
  require(
      interpreter.signal_value(outputs[8]).to_msb_string()
          == "1" + std::string(64, '0'),
      "signed minimum divided by negative one wraps at fixed width");
  require(
      interpreter.signal_value(outputs[9]).to_msb_string()
          == std::string(65, 'X'),
      "unknown signed arithmetic produces an all-X result");
}

} // namespace fsim::tests::runtime
