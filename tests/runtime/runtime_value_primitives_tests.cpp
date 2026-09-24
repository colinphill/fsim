// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace fsim::tests::runtime {

namespace {

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_logic4_word_primitives()
{
  using namespace fsim::runtime;

  constexpr std::array<Logic4, 4> states {
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z
  };
  const auto to_word = [](const Logic4 state) {
    switch (state) {
    case Logic4::zero:
      return Logic4Word { 1U, 0U, 0U };
    case Logic4::one:
      return Logic4Word { 1U, 1U, 0U };
    case Logic4::x:
      return Logic4Word { 1U, 1U, 1U };
    case Logic4::z:
      return Logic4Word { 1U, 0U, 1U };
    }
    return Logic4Word { };
  };

  const Logic4ResolutionAccumulator no_drivers { 1U };
  require(
      no_drivers.result() == Logic4Word { 1U, 0U, 1U },
      "an empty narrow resolution accumulator must produce Z");
  for (const auto left : states) {
    for (const auto right : states) {
      const std::array<Logic4, 2> scalar_drivers { left, right };
      Logic4ResolutionAccumulator accumulator { 1U };
      accumulator.add(to_word(left));
      accumulator.add(to_word(right));
      const auto expected = PackedLogic4 {
          1U, resolve(std::span<const Logic4> { scalar_drivers })
      }.low_word();
      require(
          accumulator.result() == expected,
          "narrow Logic4 word resolution must match scalar resolution");
    }
  }

  Logic4ResolutionAccumulator one_bit { 1U };
  one_bit.add(Logic4Word { 1U, ~UINT64_C(0), 0U });
  require(
      one_bit.result() == Logic4Word { 1U, 1U, 0U },
      "narrow Logic4 word resolution must mask unused bits");

  constexpr std::array<Logic4Word, 4> wide_word_drivers {
      Logic4Word { 64U, 0U, 0U },
      Logic4Word { 64U, ~UINT64_C(0), 0U },
      Logic4Word { 64U, UINT64_C(0x00ff00ff00ff00ff),
          UINT64_C(0x0f0f0f0f0f0f0f0f) },
      Logic4Word { 64U, 0U, ~UINT64_C(0) }
  };
  Logic4ResolutionAccumulator sixty_four_bits { 64U };
  std::array<PackedLogic4, wide_word_drivers.size()> packed_drivers;
  for (std::size_t index = 0; index < wide_word_drivers.size(); ++index) {
    const auto driver = wide_word_drivers[index];
    sixty_four_bits.add(driver);
    packed_drivers[index] = PackedLogic4::from_aval_bval(
        driver.width, driver.aval, driver.bval);
  }
  require(
      sixty_four_bits.result()
          == resolve(std::span<const PackedLogic4> { packed_drivers })
                 .low_word(),
      "64-bit word resolution must match packed four-state resolution");

  for (const auto invalid_width : { 0U, 65U }) {
    try {
      (void)Logic4ResolutionAccumulator { invalid_width };
      throw std::runtime_error(
          "narrow Logic4 resolution accepted an out-of-range width");
    } catch (const std::invalid_argument&) {
    }
  }
  try {
    Logic4ResolutionAccumulator mismatched { 1U };
    mismatched.add(Logic4Word { 64U, 0U, 0U });
    throw std::runtime_error(
        "narrow Logic4 resolution accepted a mismatched driver width");
  } catch (const std::invalid_argument&) {
  }

  const auto one_bit_force = apply_force_word(
      Logic4Word { 1U, ~UINT64_C(0), ~UINT64_C(0) },
      Logic4Word { 1U, 0U, ~UINT64_C(0) },
      ~UINT64_C(0));
  require(
      one_bit_force == Logic4Word { 1U, 0U, 1U },
      "one-bit force overlay must preserve the selected Z state and mask");

  const Logic4Word driven {
      64U, UINT64_C(0x0123456789abcdef),
      UINT64_C(0xfedcba9876543210)
  };
  const Logic4Word forced {
      64U, UINT64_C(0xa5a5f0f00f0f5a5a),
      UINT64_C(0x5a5a0f0ff0f0a5a5)
  };
  constexpr std::uint64_t force_mask = UINT64_C(0x00ff00ff55aa55aa);
  const auto forced_result = apply_force_word(driven, forced, force_mask);
  auto expected_force_result = PackedLogic4::from_aval_bval(
      64U, driven.aval, driven.bval);
  const auto packed_forced = PackedLogic4::from_aval_bval(
      64U, forced.aval, forced.bval);
  for (std::size_t bit = 0; bit < 64U; ++bit) {
    if (((force_mask >> bit) & UINT64_C(1)) != 0U) {
      expected_force_result.set(bit, packed_forced.get(bit));
    }
  }
  require(
      forced_result == expected_force_result.low_word(),
      "64-bit force overlay must replace only masked aval and bval bits");
  try {
    (void)apply_force_word(
        Logic4Word { 1U, 0U, 0U }, Logic4Word { 64U, 0U, 0U }, 0U);
    throw std::runtime_error(
        "force overlay accepted mismatched Logic4 word widths");
  } catch (const std::invalid_argument&) {
  }

  const auto initial = PackedLogic4 { 65U, Logic4::zero };
  const auto source_slice = PackedLogic4::from_msb_string("X10Z01XZ");
  const auto slice = source_slice.low_word();
  auto forced_value = initial;
  forced_value.insert_word(slice, 57U);
  const auto force_mask_word = Logic4Word { 8U, UINT64_C(0xff), 0U };
  auto forced_mask = PackedLogic4 { 65U, Logic4::zero };
  forced_mask.insert_masked_word(
      force_mask_word, UINT64_C(0x55), 57U);
  auto released_mask = forced_mask;
  released_mask.insert_masked_word(
      Logic4Word { 8U, 0U, 0U }, UINT64_C(0x04), 57U);
  auto applied_value = initial;
  applied_value.insert_masked_word(slice, UINT64_C(0x55), 57U);
  auto expected_value = initial;
  auto expected_mask = PackedLogic4 { 65U, Logic4::zero };
  for (std::size_t bit = 0; bit < source_slice.width(); ++bit) {
    if (((UINT64_C(0x55) >> bit) & UINT64_C(1)) == 0U) {
      continue;
    }
    expected_value.set(57U + bit, source_slice.get(bit));
    expected_mask.set(57U + bit, Logic4::one);
  }
  auto expected_released_mask = expected_mask;
  expected_released_mask.set(59U, Logic4::zero);
  require(
      forced_value.matches_word(slice, 57U)
          && applied_value == expected_value
          && initial == PackedLogic4 { 65U, Logic4::zero }
          && forced_mask == expected_mask
          && released_mask == expected_released_mask,
      "cross-word force and release mask edits must preserve COW copies");

  auto wide_target = PackedLogic4 { 130U, Logic4::z };
  wide_target.set(0U, Logic4::one);
  wide_target.set(129U, Logic4::x);
  const auto target_before = wide_target.to_msb_string();
  const auto target_snapshot = wide_target;
  const auto wide_source = PackedLogic4::from_msb_string(
      "XZ10" + std::string(60U, '1') + "X");
  const auto source_before = wide_source.to_msb_string();
  auto expected_wide_target = wide_target;
  for (std::size_t bit = 0; bit < wide_source.width(); ++bit) {
    expected_wide_target.set(31U + bit, wide_source.get(bit));
  }
  wide_target.insert_bits(wide_source, 31U);
  require(
      wide_source.width() == 65U
          && wide_target == expected_wide_target
          && wide_target.extract_bits(31U, 65U) == wide_source
          && wide_target.get(31U + 63U) == Logic4::z
          && wide_target.get(31U + 64U) == Logic4::x
          && target_snapshot.to_msb_string() == target_before
          && wide_source.to_msb_string() == source_before,
      "unaligned 65-bit Logic4 insertion must preserve boundary bits and COW snapshots");
}

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

  test_logic4_word_primitives();

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
  auto assigned_word = PackedLogic4(5U, Logic4::zero);
  assigned_word.assign_word(
      Logic4Word { 5U, ~UINT64_C(0), UINT64_C(0b10100) });
  require(
      assigned_word == word_value,
      "single-word assignment must replace and mask inline planes");
  try {
    assigned_word.assign_word(Logic4Word { 4U, 0U, 0U });
    throw std::runtime_error(
        "mismatched single-word assignment width was accepted");
  } catch (const std::invalid_argument&) {
  }

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
  require(
      !wide_value.known_unsigned_value()
          && !wide_value.known_signed_value(),
      "unknown wide values must not convert to host integers");
  std::array<PackedLogic4, 4> wide_drivers {
      PackedLogic4 { 257U, Logic4::z },
      PackedLogic4 { 257U, Logic4::z },
      PackedLogic4 { 257U, Logic4::z },
      PackedLogic4 { 257U, Logic4::z }
  };
  constexpr std::array states {
      Logic4::zero, Logic4::one, Logic4::x, Logic4::z
  };
  for (std::size_t bit = 0; bit < 257U; ++bit) {
    for (std::size_t driver = 0; driver < wide_drivers.size(); ++driver) {
      wide_drivers[driver].set(
          bit, states[(bit >> (driver * 2U)) & 3U]);
    }
  }
  const auto wide_resolved = resolve(
      std::span<const PackedLogic4> { wide_drivers });
  auto scalar_resolved = PackedLogic4 { 257U, Logic4::z };
  for (std::size_t bit = 0; bit < scalar_resolved.width(); ++bit) {
    std::array<Logic4, 4> bit_drivers { };
    for (std::size_t driver = 0; driver < wide_drivers.size(); ++driver) {
      bit_drivers[driver] = wide_drivers[driver].get(bit);
    }
    scalar_resolved.set(
        bit, resolve(std::span<const Logic4> { bit_drivers }));
  }
  require(
      wide_resolved == scalar_resolved,
      "wide four-state resolution matches scalar wire semantics across words");
  try {
    (void)wide_value.low_word();
    throw std::runtime_error(
        "wide four-state value exposed a single low word");
  } catch (const std::invalid_argument&) {
  }

  const auto shared_four = PackedLogic4::from_msb_string(
      "10XZ" + std::string(121, '1') + "Z01X");
  const auto shared_four_text = shared_four.to_msb_string();
  require(
      shared_four == PackedLogic4::from_msb_string(shared_four_text),
      "separately allocated wide four-state values compare by content");
  auto set_copy = shared_four;
  set_copy.set(0U, Logic4::zero);
  require(
      shared_four.to_msb_string() == shared_four_text
          && set_copy != shared_four,
      "wide four-state bit mutation must detach shared storage");
  PackedLogic4 assigned_copy;
  assigned_copy = shared_four;
  assigned_copy.insert_word(
      Logic4Word { 64U, UINT64_C(0x55aa55aa55aa55aa), 0U }, 63U);
  require(
      shared_four.to_msb_string() == shared_four_text
          && assigned_copy != shared_four,
      "wide four-state word insertion must detach copy-assigned storage");
  auto filled_copy = shared_four;
  filled_copy.fill(Logic4::z);
  require(
      shared_four.to_msb_string() == shared_four_text
          && filled_copy == PackedLogic4(129U, Logic4::z),
      "wide four-state fill must detach shared storage");

  const auto shared_nine = PackedValue::from_logic9_msb_string(
      "U01ZWLH-" + std::string(121, 'H'));
  const auto shared_nine_text = shared_nine.to_msb_string();
  auto nine_set_copy = shared_nine;
  nine_set_copy.set_logic9(64U, Logic9::dont_care);
  require(
      shared_nine.to_msb_string() == shared_nine_text
          && nine_set_copy != shared_nine,
      "wide nine-state bit mutation must detach every shared plane");
  auto nine_filled_copy = shared_nine;
  nine_filled_copy.fill(Logic9::l);
  require(
      shared_nine.to_msb_string() == shared_nine_text
          && nine_filled_copy
              == PackedValue::from_logic9_msb_string(std::string(129, 'L')),
      "wide nine-state fill must detach every shared plane");

  const auto wide_zero = PackedLogic4::from_msb_string(
      std::string(257, '0'));
  require(
      wide_zero.known_unsigned_value() == 0
          && wide_zero.known_signed_value() == 0,
      "known wide zero must convert without narrowing by declared width");
  auto wide_positive_text = std::string(257, '0');
  wide_positive_text.back() = '1';
  const auto wide_positive = PackedLogic4::from_msb_string(wide_positive_text);
  require(
      wide_positive.known_unsigned_value() == 1
          && wide_positive.known_signed_value() == 1,
      "known wide positive values must accept zero-extension");
  const auto wide_negative = PackedLogic4::from_msb_string(
      std::string(257, '1'));
  require(
      !wide_negative.known_unsigned_value()
          && wide_negative.known_signed_value() == -1,
      "known wide signed values must accept exact sign-extension");
  auto wide_overflow_text = std::string(257, '0');
  wide_overflow_text[192] = '1';
  const auto wide_overflow = PackedLogic4::from_msb_string(wide_overflow_text);
  require(
      !wide_overflow.known_unsigned_value()
          && !wide_overflow.known_signed_value(),
      "known wide values outside host range must not be narrowed");

  auto inserted = PackedLogic4(257, Logic4::zero);
  inserted.set(0U, Logic4::one);
  inserted.set(256U, Logic4::z);
  const auto inserted_source = PackedLogic4::from_msb_string(
      "10XZ" + std::string(121, '1') + "Z01X");
  auto inserted_expected = inserted;
  for (std::size_t bit = 0; bit < inserted_source.width(); ++bit) {
    inserted_expected.set(63U + bit, inserted_source.get(bit));
  }
  inserted.insert_bits(inserted_source, 63U);
  require(inserted == inserted_expected,
      "wide four-state insertion must splice unaligned word planes");
  require(
      inserted.extract_bits(63U, inserted_source.width()) == inserted_source,
      "wide four-state extraction must splice unaligned word planes");
  try {
    (void)inserted.extract_bits(257U, 1U);
    throw std::runtime_error(
        "out-of-range packed extraction was accepted");
  } catch (const std::invalid_argument&) {
  }

  auto word_inserted = PackedLogic4(257, Logic4::x);
  auto word_expected = word_inserted;
  const auto inserted_word = Logic4Word {
      64U, UINT64_C(0x9a55c33cf00f8765),
      UINT64_C(0x0f0ff0f055aa33cc) };
  const auto inserted_word_value = PackedLogic4::from_aval_bval(
      inserted_word.width, inserted_word.aval, inserted_word.bval);
  for (std::size_t bit = 0; bit < inserted_word.width; ++bit) {
    word_expected.set(61U + bit, inserted_word_value.get(bit));
  }
  word_inserted.insert_word(inserted_word, 61U);
  require(word_inserted == word_expected,
      "four-state word insertion must splice an unaligned wide target");
  require(
      word_inserted.matches_word(inserted_word, 61U),
      "word comparison must match an unaligned cross-word range");
  auto mismatched_word = inserted_word;
  mismatched_word.bval ^= UINT64_C(1) << 31U;
  require(
      !word_inserted.matches_word(mismatched_word, 61U),
      "word comparison must distinguish bval changes in wide targets");

  const auto inline_target = PackedLogic4::from_msb_string("10XZ0110");
  const auto inline_slice = inline_target.extract_bits(1U, 6U).low_word();
  require(
      inline_target.matches_word(inline_slice, 1U),
      "word comparison must match an unaligned inline range");
  auto mismatched_inline = inline_slice;
  mismatched_inline.aval ^= UINT64_C(1) << 4U;
  require(
      !inline_target.matches_word(mismatched_inline, 1U),
      "word comparison must distinguish aval changes in inline targets");

  for (const auto invalid : std::array {
           Logic4Word { 0U, 0U, 0U },
           Logic4Word { 65U, 0U, 0U } }) {
    try {
      (void)inline_target.matches_word(invalid, 0U);
      throw std::runtime_error("invalid word comparison width was accepted");
    } catch (const std::invalid_argument&) {
    }
  }
  try {
    (void)inline_target.matches_word(inline_slice, 3U);
    throw std::runtime_error("out-of-range word comparison was accepted");
  } catch (const std::invalid_argument&) {
  }

  const auto exact_word_target = PackedValue::from_logic9_msb_string("10XZ01");
  require(
      !exact_word_target.matches_word(
          PackedLogic4::from_msb_string("10XZ01").low_word(), 0U),
      "word comparison must reject exact nine-state target storage");

  auto exact_inserted = PackedValue::from_logic9_msb_string(
      std::string(257, '-'));
  const auto exact_source = PackedValue::from_logic9_msb_string(
      "U01ZWLH-" + std::string(57, 'H'));
  auto exact_expected = exact_inserted;
  for (std::size_t bit = 0; bit < exact_source.width(); ++bit) {
    exact_expected.set_logic9(61U + bit, exact_source.get_logic9(bit));
  }
  exact_inserted.insert_bits(exact_source, 61U);
  require(exact_inserted == exact_expected,
      "wide nine-state insertion must splice every ordinal plane");
  require(
      exact_inserted.extract_bits(61U, exact_source.width()) == exact_source,
      "wide nine-state extraction must splice every ordinal plane");

  auto promoted_inserted = PackedValue::from_logic9_msb_string(
      std::string(129, 'L'));
  const auto four_source = PackedLogic4::from_msb_string("10XZ01");
  auto promoted_expected = promoted_inserted;
  for (std::size_t bit = 0; bit < four_source.width(); ++bit) {
    promoted_expected.set_logic9(64U + bit, four_source.get_logic9(bit));
  }
  promoted_inserted.insert_bits(four_source, 64U);
  require(promoted_inserted == promoted_expected,
      "four-state insertion into a nine-state target must preserve domains");

  auto promoted_word_inserted = PackedValue::from_logic9_msb_string(
      std::string(129, 'L'));
  auto promoted_word_expected = promoted_word_inserted;
  const auto promoted_word = Logic4Word {
      6U, UINT64_C(0b101101), UINT64_C(0b011010) };
  const auto promoted_word_value = PackedLogic4::from_aval_bval(
      promoted_word.width, promoted_word.aval, promoted_word.bval);
  for (std::size_t bit = 0; bit < promoted_word.width; ++bit) {
    promoted_word_expected.set_logic9(
        63U + bit, promoted_word_value.get_logic9(bit));
  }
  promoted_word_inserted.insert_word(promoted_word, 63U);
  require(promoted_word_inserted == promoted_word_expected,
      "four-state word insertion into a nine-state target must preserve domains");

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
  const auto payload_narrowed = systemverilog_scalar_binary_payload(
      SystemVerilogScalarBinaryOperator::Convert,
      payload_product.value,
      SystemVerilogScalarKind::Real,
      payload_product.value,
      SystemVerilogScalarKind::Real,
      SystemVerilogScalarKind::ShortReal);
  require(
      payload_product && payload_greater && payload_narrowed
          && decode_systemverilog_scalar_payload(
                 payload_product.value,
                 SystemVerilogScalarKind::Real).value.as_real() == 3.0
          && decode_systemverilog_scalar_payload(
                 payload_narrowed.value,
                 SystemVerilogScalarKind::ShortReal).value.as_shortreal()
              == 3.0F
          && payload_greater.value.to_msb_string() == "1",
      "scalar binary payload service must preserve typed arithmetic, conversion, and predicates");

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

} // namespace fsim::tests::runtime
