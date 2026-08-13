// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_value.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_string_bytes = 1U << 20U;

bool valid_strength(const SystemVerilogVpiDriveStrength& strength) {
  return static_cast<unsigned>(strength.zero)
          <= static_cast<unsigned>(SystemVerilogVpiStrengthRank::Supply)
      && static_cast<unsigned>(strength.one)
          <= static_cast<unsigned>(SystemVerilogVpiStrengthRank::Supply);
}

std::size_t payload_width(const SystemVerilogVpiStoredValue& value) {
  if (const auto* bits = std::get_if<PackedBit2>(&value.payload)) {
    return bits->width();
  }
  if (const auto* logic = std::get_if<PackedLogic4>(&value.payload)) {
    return logic->width();
  }
  if (const auto* logic = std::get_if<PackedLogic9>(&value.payload)) {
    return logic->width();
  }
  return 0;
}

Logic9 logic9(const Logic4 value) {
  switch (value) {
    case Logic4::zero:
      return Logic9::zero;
    case Logic4::one:
      return Logic9::one;
    case Logic4::x:
      return Logic9::x;
    case Logic4::z:
      return Logic9::z;
  }
  return Logic9::x;
}

SystemVerilogVpiValueReadResult failure(
    const SystemVerilogVpiValueError error) {
  SystemVerilogVpiValueReadResult result;
  result.error = error;
  return result;
}

SystemVerilogVpiValueError read_integer(
    const SystemVerilogVpiStoredValue& value,
    const std::size_t width,
    std::uint64_t& output) {
  if (width == 0U || width > 64U) {
    return SystemVerilogVpiValueError::ResourceLimit;
  }
  output = 0;
  for (std::size_t bit = 0; bit < width; ++bit) {
    bool one{};
    if (const auto* bits = std::get_if<PackedBit2>(&value.payload)) {
      one = bits->get(bit);
    } else if (
        const auto* logic4 = std::get_if<PackedLogic4>(&value.payload)) {
      const auto state = logic4->get(bit);
      if (state != Logic4::zero && state != Logic4::one) {
        return SystemVerilogVpiValueError::UnknownState;
      }
      one = state == Logic4::one;
    } else if (
        const auto* logic9_value = std::get_if<PackedLogic9>(&value.payload)) {
      const auto state = logic9_value->get(bit);
      if (state != Logic9::zero && state != Logic9::one) {
        return SystemVerilogVpiValueError::UnknownState;
      }
      one = state == Logic9::one;
    } else {
      return SystemVerilogVpiValueError::UnsupportedFormat;
    }
    if (one) {
      output |= std::uint64_t{1} << bit;
    }
  }
  return SystemVerilogVpiValueError::None;
}

}  // namespace

bool validate_systemverilog_vpi_stored_value(
    const SystemVerilogVpiTypeInfo& type,
    const SystemVerilogVpiStoredValue& value) noexcept {
  if (value.strength && !valid_strength(*value.strength)) {
    return false;
  }
  const auto width = payload_width(value);
  switch (type.category) {
    case SystemVerilogVpiValueCategory::Bit2:
    case SystemVerilogVpiValueCategory::Integer2:
      return std::holds_alternative<PackedBit2>(value.payload)
          && width == type.width && width != 0U
          && !value.strength;
    case SystemVerilogVpiValueCategory::Logic4:
    case SystemVerilogVpiValueCategory::Integer4: {
      const auto* logic = std::get_if<PackedLogic4>(&value.payload);
      return logic && !logic->is_logic9() && width == type.width
          && width != 0U
          && (!value.strength
              || (type.category == SystemVerilogVpiValueCategory::Logic4
                  && width == 1U));
    }
    case SystemVerilogVpiValueCategory::Logic9:
      return std::holds_alternative<PackedLogic9>(value.payload)
          && width == type.width && width != 0U
          && !value.strength;
    case SystemVerilogVpiValueCategory::Real:
      return std::holds_alternative<double>(value.payload)
          && type.width == 64U && !value.strength;
    case SystemVerilogVpiValueCategory::ShortReal:
      return std::holds_alternative<float>(value.payload)
          && type.width == 32U && !value.strength;
    case SystemVerilogVpiValueCategory::String: {
      const auto* string = std::get_if<std::string>(&value.payload);
      return string && string->size() <= maximum_string_bytes
          && type.width == 0U && !value.strength;
    }
    case SystemVerilogVpiValueCategory::Time: {
        const auto* logic = std::get_if<PackedLogic4>(&value.payload);
        return type.width == 64U && !value.strength
            && (std::holds_alternative<std::uint64_t>(value.payload)
                || (logic && !logic->is_logic9()
                    && logic->width() == 64U));
    }
    case SystemVerilogVpiValueCategory::None:
    case SystemVerilogVpiValueCategory::Event:
      return false;
  }
  return false;
}

SystemVerilogVpiValueReadResult read_systemverilog_vpi_value(
    const SystemVerilogVpiTypeInfo& type,
    const SystemVerilogVpiStoredValue& value,
    const SystemVerilogVpiValueFormat format,
    const SystemVerilogVpiValueReadBuffers buffers) {
  if (!validate_systemverilog_vpi_stored_value(type, value)) {
    return failure(SystemVerilogVpiValueError::TypeMismatch);
  }

  SystemVerilogVpiValueReadResult result;
  switch (format) {
    case SystemVerilogVpiValueFormat::Scalar:
      if (payload_width(value) != 1U) {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      if (const auto* bits = std::get_if<PackedBit2>(&value.payload)) {
        result.scalar = bits->get(0) ? Logic9::one : Logic9::zero;
      } else if (
          const auto* logic4 = std::get_if<PackedLogic4>(&value.payload)) {
        result.scalar = logic9(logic4->get(0));
      } else if (
          const auto* logic9_value = std::get_if<PackedLogic9>(&value.payload)) {
        result.scalar = logic9_value->get(0);
      } else {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      return result;

    case SystemVerilogVpiValueFormat::Integer:
      result.error = read_integer(
          value, payload_width(value), result.integer);
      result.integer_is_signed = type.is_signed;
      return result;

    case SystemVerilogVpiValueFormat::Real:
      if (const auto* real = std::get_if<double>(&value.payload)) {
        result.real = *real;
        return result;
      }
      if (const auto* real = std::get_if<float>(&value.payload)) {
        result.real = static_cast<double>(*real);
        return result;
      }
      return failure(SystemVerilogVpiValueError::UnsupportedFormat);

    case SystemVerilogVpiValueFormat::String: {
      const auto* string = std::get_if<std::string>(&value.payload);
      if (!string) {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      result.required_characters = string->size() + 1U;
      if (buffers.characters.size() < result.required_characters) {
        result.error = SystemVerilogVpiValueError::BufferTooSmall;
        return result;
      }
      std::ranges::copy(*string, buffers.characters.begin());
      buffers.characters[string->size()] = '\0';
      return result;
    }

    case SystemVerilogVpiValueFormat::Time: {
        if (const auto* time = std::get_if<std::uint64_t>(&value.payload)) {
            result.time = *time;
            return result;
        }
        result.error = read_integer(value, 64U, result.time);
        return result;
    }

    case SystemVerilogVpiValueFormat::Strength: {
      const auto* logic = std::get_if<PackedLogic4>(&value.payload);
      if (!logic || logic->width() != 1U || !value.strength) {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      result.strength = {logic->get(0), *value.strength};
      return result;
    }

    case SystemVerilogVpiValueFormat::BitVector: {
      const auto* bits = std::get_if<PackedBit2>(&value.payload);
      if (!bits) {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      result.required_words = bits->words().size();
      if (buffers.words.size() < result.required_words) {
        result.error = SystemVerilogVpiValueError::BufferTooSmall;
        return result;
      }
      std::ranges::copy(bits->words(), buffers.words.begin());
      return result;
    }

    case SystemVerilogVpiValueFormat::Logic4Vector: {
      const auto* logic = std::get_if<PackedLogic4>(&value.payload);
      if (!logic || logic->is_logic9()) {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      const auto words = logic->aval_words().size();
      if (words > std::numeric_limits<std::size_t>::max() / 2U) {
        return failure(SystemVerilogVpiValueError::ResourceLimit);
      }
      result.required_words = words * 2U;
      if (buffers.words.size() < result.required_words) {
        result.error = SystemVerilogVpiValueError::BufferTooSmall;
        return result;
      }
      std::ranges::copy(logic->aval_words(), buffers.words.begin());
      std::ranges::copy(
          logic->bval_words(), buffers.words.begin() + words);
      return result;
    }

    case SystemVerilogVpiValueFormat::Logic9Vector: {
      const auto* logic = std::get_if<PackedLogic9>(&value.payload);
      if (!logic) {
        return failure(SystemVerilogVpiValueError::UnsupportedFormat);
      }
      const auto words = logic->plane(0).size();
      if (words > std::numeric_limits<std::size_t>::max() / 4U) {
        return failure(SystemVerilogVpiValueError::ResourceLimit);
      }
      result.required_words = words * 4U;
      if (buffers.words.size() < result.required_words) {
        result.error = SystemVerilogVpiValueError::BufferTooSmall;
        return result;
      }
      for (std::size_t plane = 0; plane < 4U; ++plane) {
        std::ranges::copy(
            logic->plane(plane), buffers.words.begin() + plane * words);
      }
      return result;
    }
  }
  return failure(SystemVerilogVpiValueError::UnsupportedFormat);
}

namespace {

SystemVerilogVpiValueConversionResult conversion_failure(
    const SystemVerilogVpiValueError error,
    const std::size_t required_words = 0) {
  return {std::nullopt, error, required_words};
}

bool padding_is_zero(
    const std::span<const std::uint64_t> words,
    const std::size_t offset,
    const std::size_t word_count,
    const std::size_t width) {
  const auto used = width % 64U;
  if (used == 0U || word_count == 0U) {
    return true;
  }
  const auto valid_mask = (std::uint64_t{1} << used) - 1U;
  return (words[offset + word_count - 1U] & ~valid_mask) == 0U;
}

std::optional<Logic4> logic4_from_logic9(const Logic9 state) {
  switch (state) {
    case Logic9::zero:
      return Logic4::zero;
    case Logic9::one:
      return Logic4::one;
    case Logic9::x:
      return Logic4::x;
    case Logic9::z:
      return Logic4::z;
    default:
      return std::nullopt;
  }
}

}  // namespace

SystemVerilogVpiValueConversionResult
make_systemverilog_vpi_stored_value(
    const SystemVerilogVpiTypeInfo& type,
    const SystemVerilogVpiValueFormat format,
    const SystemVerilogVpiValueWriteData& input) {
  SystemVerilogVpiStoredValue value;
  const auto width = static_cast<std::size_t>(type.width);
  const auto word_count = width == 0U ? 0U : (width + 63U) / 64U;
  const auto finish = [&]()
      -> SystemVerilogVpiValueConversionResult {
    if (!validate_systemverilog_vpi_stored_value(type, value)) {
      return conversion_failure(SystemVerilogVpiValueError::TypeMismatch);
    }
    return {std::move(value), {}, 0};
  };

  switch (format) {
    case SystemVerilogVpiValueFormat::Scalar:
      if (width != 1U) {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      if (type.category == SystemVerilogVpiValueCategory::Bit2) {
        if (input.scalar != Logic9::zero
            && input.scalar != Logic9::one) {
          return conversion_failure(
              SystemVerilogVpiValueError::UnknownState);
        }
        value.payload = PackedBit2{
            1, input.scalar == Logic9::one};
      } else if (
          type.category == SystemVerilogVpiValueCategory::Logic4) {
        const auto state = logic4_from_logic9(input.scalar);
        if (!state) {
          return conversion_failure(
              SystemVerilogVpiValueError::UnknownState);
        }
        value.payload = PackedLogic4{1, *state};
      } else if (
          type.category == SystemVerilogVpiValueCategory::Logic9
          && static_cast<unsigned>(input.scalar)
              <= static_cast<unsigned>(Logic9::dont_care)) {
        value.payload = PackedLogic9{1, input.scalar};
      } else {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      return finish();

    case SystemVerilogVpiValueFormat::Integer:
      if (width == 0U || width > 64U) {
        return conversion_failure(
            SystemVerilogVpiValueError::ResourceLimit);
      }
      if (type.category == SystemVerilogVpiValueCategory::Bit2
          || type.category == SystemVerilogVpiValueCategory::Integer2) {
        PackedBit2 result{width};
        for (std::size_t bit = 0; bit < width; ++bit) {
          result.set(bit, ((input.integer >> bit) & 1U) != 0U);
        }
        value.payload = std::move(result);
      } else if (
          type.category == SystemVerilogVpiValueCategory::Logic4
          || type.category == SystemVerilogVpiValueCategory::Integer4) {
        PackedLogic4 result{width, Logic4::zero};
        for (std::size_t bit = 0; bit < width; ++bit) {
          result.set(
              bit,
              ((input.integer >> bit) & 1U) != 0U
                  ? Logic4::one
                  : Logic4::zero);
        }
        value.payload = std::move(result);
      } else if (
          type.category == SystemVerilogVpiValueCategory::Logic9) {
        PackedLogic9 result{width, Logic9::zero};
        for (std::size_t bit = 0; bit < width; ++bit) {
          result.set(
              bit,
              ((input.integer >> bit) & 1U) != 0U
                  ? Logic9::one
                  : Logic9::zero);
        }
        value.payload = std::move(result);
      } else {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      return finish();

    case SystemVerilogVpiValueFormat::Real:
      if (type.category == SystemVerilogVpiValueCategory::Real) {
        value.payload = input.real;
      } else if (
          type.category == SystemVerilogVpiValueCategory::ShortReal) {
        const auto narrowed = static_cast<float>(input.real);
        if (std::isfinite(input.real) && !std::isfinite(narrowed)) {
          return conversion_failure(
              SystemVerilogVpiValueError::ResourceLimit);
        }
        value.payload = narrowed;
      } else {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      return finish();

    case SystemVerilogVpiValueFormat::String:
      if (type.category != SystemVerilogVpiValueCategory::String) {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      if (input.characters.size() > maximum_string_bytes) {
        return conversion_failure(
            SystemVerilogVpiValueError::ResourceLimit);
      }
      value.payload = std::string{
          input.characters.begin(), input.characters.end()};
      return finish();

    case SystemVerilogVpiValueFormat::Time:
      if (type.category != SystemVerilogVpiValueCategory::Time) {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      value.payload = input.time;
      return finish();

    case SystemVerilogVpiValueFormat::Strength: {
      if (type.category != SystemVerilogVpiValueCategory::Logic4
          || width != 1U || !valid_strength(input.strength.drive)) {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      value.payload = PackedLogic4{1, input.strength.state};
      value.strength = input.strength.drive;
      return finish();
    }

    case SystemVerilogVpiValueFormat::BitVector: {
      if (type.category != SystemVerilogVpiValueCategory::Bit2
          && type.category != SystemVerilogVpiValueCategory::Integer2) {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      if (input.words.size() < word_count) {
        return conversion_failure(
            SystemVerilogVpiValueError::BufferTooSmall, word_count);
      }
      if (!padding_is_zero(input.words, 0, word_count, width)) {
        return conversion_failure(
            SystemVerilogVpiValueError::InvalidEncoding, word_count);
      }
      PackedBit2 result{width};
      for (std::size_t bit = 0; bit < width; ++bit) {
        result.set(
            bit, ((input.words[bit / 64U] >> (bit % 64U)) & 1U) != 0U);
      }
      value.payload = std::move(result);
      return finish();
    }

    case SystemVerilogVpiValueFormat::Logic4Vector: {
        if (type.category != SystemVerilogVpiValueCategory::Logic4
            && type.category != SystemVerilogVpiValueCategory::Integer4
            && type.category != SystemVerilogVpiValueCategory::Time) {
            return conversion_failure(
                SystemVerilogVpiValueError::UnsupportedFormat);
        }
      const auto required = word_count * 2U;
      if (input.words.size() < required) {
        return conversion_failure(
            SystemVerilogVpiValueError::BufferTooSmall, required);
      }
      if (!padding_is_zero(input.words, 0, word_count, width)
          || !padding_is_zero(
              input.words, word_count, word_count, width)) {
        return conversion_failure(
            SystemVerilogVpiValueError::InvalidEncoding, required);
      }
      PackedLogic4 result{width, Logic4::zero};
      for (std::size_t bit = 0; bit < width; ++bit) {
        const auto word = bit / 64U;
        const auto shift = bit % 64U;
        const bool aval = ((input.words[word] >> shift) & 1U) != 0U;
        const bool bval =
            ((input.words[word_count + word] >> shift) & 1U) != 0U;
        result.set(
            bit,
            aval ? (bval ? Logic4::x : Logic4::one)
                 : (bval ? Logic4::z : Logic4::zero));
      }
      value.payload = std::move(result);
      return finish();
    }

    case SystemVerilogVpiValueFormat::Logic9Vector: {
      if (type.category != SystemVerilogVpiValueCategory::Logic9) {
        return conversion_failure(
            SystemVerilogVpiValueError::UnsupportedFormat);
      }
      const auto required = word_count * 4U;
      if (input.words.size() < required) {
        return conversion_failure(
            SystemVerilogVpiValueError::BufferTooSmall, required);
      }
      for (std::size_t plane = 0; plane < 4U; ++plane) {
        if (!padding_is_zero(
                input.words, plane * word_count, word_count, width)) {
          return conversion_failure(
              SystemVerilogVpiValueError::InvalidEncoding, required);
        }
      }
      PackedLogic9 result{width, Logic9::u};
      for (std::size_t bit = 0; bit < width; ++bit) {
        const auto word = bit / 64U;
        const auto shift = bit % 64U;
        std::uint8_t encoded{};
        for (std::size_t plane = 0; plane < 4U; ++plane) {
          const auto plane_bit = static_cast<std::uint8_t>(
              (input.words[plane * word_count + word] >> shift) & 1U);
          encoded = static_cast<std::uint8_t>(
              encoded | static_cast<std::uint8_t>(plane_bit << plane));
        }
        if (encoded > static_cast<std::uint8_t>(Logic9::dont_care)) {
          return conversion_failure(
              SystemVerilogVpiValueError::InvalidEncoding, required);
        }
        result.set(bit, static_cast<Logic9>(encoded));
      }
      value.payload = std::move(result);
      return finish();
    }
  }
  return conversion_failure(
      SystemVerilogVpiValueError::UnsupportedFormat);
}

}  // namespace fsim::runtime
