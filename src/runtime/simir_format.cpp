// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {


[[nodiscard]] std::string error_text(ProcessId process,
                                     InstructionIndex instruction,
                                     const std::string &message) {
  std::ostringstream result;
  result << "SimIR process " << process << ", instruction " << instruction
         << ": " << message;
  return result.str();
}

[[nodiscard]] std::optional<SignalId> output_signal(
    const Operation& operation) {
  return fsim::runtime::simir::visit_operation(
      [&](const auto& value) -> std::optional<SignalId> {
        using OperationType = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
          return std::optional{value.signal};
        } else if constexpr (std::is_same_v<
                                 OperationType,
                                 WriteInertialDynamicPartSlice>) {
            return std::optional { value.signal };
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
            return std::optional { value.signal };
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
            return std::optional { value.signal };
        } else {
            return { };
        }
      },
      operation);
}

[[nodiscard]] std::string format_output_value(
    const PackedLogic4& value,
    const OutputFormat format,
    const bool signed_decimal,
    const bool suppress_leading_zero) {
  const auto maybe_suppress_leading_zero =
      [suppress_leading_zero](std::string text) {
        if (!suppress_leading_zero || text.size() <= 1U) {
          return text;
        }
        const auto first = text.find_first_not_of('0');
        if (first == std::string::npos) {
          return std::string{"0"};
        }
        text.erase(0, first);
        return text;
      };
  switch (format) {
  case OutputFormat::binary: {
    auto text = value.to_msb_string();
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](const unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    return maybe_suppress_leading_zero(std::move(text));
  }
  case OutputFormat::hexadecimal: {
    constexpr std::string_view digits{"0123456789abcdef"};
    const auto digit_count = (value.width() + 3U) / 4U;
    std::string text(digit_count, '0');
    for (std::size_t digit = 0; digit < digit_count; ++digit) {
      const auto offset = digit * 4U;
      const auto bit_count =
          std::min<std::size_t>(4U, value.width() - offset);
      unsigned known_value{};
      bool all_x = true;
      bool all_z = true;
      bool has_unknown = false;
      for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const auto state = value.get(offset + bit);
        all_x = all_x && state == Logic4::x;
        all_z = all_z && state == Logic4::z;
        has_unknown =
            has_unknown || state == Logic4::x || state == Logic4::z;
        if (state == Logic4::one) {
          known_value |= 1U << bit;
        }
      }
      const char character =
          all_x ? 'x'
          : all_z ? 'z'
          : has_unknown ? 'x'
                        : digits[known_value];
      text[digit_count - digit - 1U] = character;
    }
    return maybe_suppress_leading_zero(std::move(text));
  }
  case OutputFormat::octal: {
    constexpr std::string_view digits{"01234567"};
    const auto digit_count = (value.width() + 2U) / 3U;
    std::string text(digit_count, '0');
    for (std::size_t digit = 0; digit < digit_count; ++digit) {
      const auto offset = digit * 3U;
      const auto bit_count =
          std::min<std::size_t>(3U, value.width() - offset);
      unsigned known_value{};
      bool all_x = true;
      bool all_z = true;
      bool has_unknown = false;
      for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const auto state = value.get(offset + bit);
        all_x = all_x && state == Logic4::x;
        all_z = all_z && state == Logic4::z;
        has_unknown =
            has_unknown || state == Logic4::x || state == Logic4::z;
        if (state == Logic4::one) {
          known_value |= 1U << bit;
        }
      }
      const char character =
          all_x ? 'x'
          : all_z ? 'z'
          : has_unknown ? 'x'
                        : digits[known_value];
      text[digit_count - digit - 1U] = character;
    }
    return maybe_suppress_leading_zero(std::move(text));
  }
  case OutputFormat::decimal: {
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
      const auto state = value.get(bit);
      if (state == Logic4::x || state == Logic4::z) {
        return "x";
      }
    }
    auto magnitude = value;
    bool negative =
        signed_decimal
        && value.get(value.width() - 1U) == Logic4::one;
    if (negative) {
      bool carry = true;
      for (std::size_t bit = 0; bit < magnitude.width(); ++bit) {
        const bool inverted = value.get(bit) == Logic4::zero;
        const bool result = inverted != carry;
        carry = inverted && carry;
        magnitude.set(
            bit, result ? Logic4::one : Logic4::zero);
      }
    }
    std::string text{"0"};
    for (std::size_t bit = magnitude.width(); bit-- > 0;) {
      unsigned carry =
          magnitude.get(bit) == Logic4::one ? 1U : 0U;
      for (std::size_t digit = text.size(); digit-- > 0;) {
        const auto value_digit =
            static_cast<unsigned>(text[digit] - '0') * 2U
            + carry;
        text[digit] =
            static_cast<char>('0' + (value_digit % 10U));
        carry = value_digit / 10U;
      }
      if (carry != 0) {
        text.insert(text.begin(), static_cast<char>('0' + carry));
      }
    }
    if (negative && text != "0") {
      text.insert(text.begin(), '-');
    }
    return text;
  }
  case OutputFormat::character: {
    unsigned character{};
    const auto bit_count =
        std::min<std::size_t>(8U, value.width());
    for (std::size_t bit = 0; bit < bit_count; ++bit) {
      const auto state = value.get(bit);
      if (state == Logic4::x || state == Logic4::z) {
        return "x";
      }
      if (state == Logic4::one) {
        character |= 1U << bit;
      }
    }
    return std::string(1, static_cast<char>(character));
  }
  case OutputFormat::string: {
    const auto byte_count = (value.width() + 7U) / 8U;
    std::string text;
    text.reserve(byte_count);
    bool leading_padding = true;
    for (std::size_t byte = byte_count; byte-- > 0;) {
      const auto offset = byte * 8U;
      const auto bit_count =
          std::min<std::size_t>(8U, value.width() - offset);
      unsigned character{};
      bool unknown = false;
      for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const auto state = value.get(offset + bit);
        unknown =
            unknown || state == Logic4::x || state == Logic4::z;
        if (state == Logic4::one) {
          character |= 1U << bit;
        }
      }
      if (unknown) {
        text.push_back('x');
        leading_padding = false;
      } else if (character != 0U || !leading_padding) {
        text.push_back(static_cast<char>(character));
        leading_padding = false;
      }
    }
    return text;
  }
  case OutputFormat::real_scientific:
  case OutputFormat::real_fixed:
  case OutputFormat::real_general:
  case OutputFormat::time:
    throw std::logic_error{
        "scalar formatted-output conversion has no scalar metadata"};
  }
  throw std::logic_error{"invalid formatted-output conversion"};
}

[[nodiscard]] std::string make_formatted_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const OutputFormat format,
    const PackedLogic4& value,
    const bool signed_decimal,
    const bool suppress_leading_zero,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad,
    const SystemVerilogScalarKind scalar_kind) {
  const bool scalar_text = scalar_kind != SystemVerilogScalarKind::None
      && scalar_kind != SystemVerilogScalarKind::Chandle
      && (format == OutputFormat::real_scientific
          || format == OutputFormat::real_fixed
          || format == OutputFormat::real_general
          || format == OutputFormat::time
          || format == OutputFormat::decimal);
  if (scalar_text) {
    const auto decoded = decode_systemverilog_scalar_payload(value, scalar_kind);
    if (!decoded) throw std::runtime_error{"invalid scalar formatted payload"};
    SystemVerilogScalarFormatOptions options;
    options.format = format == OutputFormat::real_scientific
        ? SystemVerilogScalarTextFormat::Scientific
        : format == OutputFormat::real_fixed
            ? SystemVerilogScalarTextFormat::Fixed
        : format == OutputFormat::time
            ? SystemVerilogScalarTextFormat::Time
        : scalar_kind == SystemVerilogScalarKind::Time
            ? SystemVerilogScalarTextFormat::Decimal
            : SystemVerilogScalarTextFormat::General;
    options.minimum_width = minimum_width;
    options.left_justify = left_justify;
    options.padding = zero_pad ? '0' : ' ';
    const auto formatted = format_systemverilog_scalar(decoded.value, options);
    if (!formatted) throw std::runtime_error{"scalar formatting failed"};
    return std::string{prefix} + formatted.text + std::string{suffix};
  }
  auto formatted =
      format_output_value(
          value, format, signed_decimal, suppress_leading_zero);
  if (formatted.size() < minimum_width) {
    const auto padding =
        static_cast<std::size_t>(minimum_width) - formatted.size();
    if (left_justify) {
      formatted.append(padding, ' ');
    } else if (zero_pad && !formatted.empty()
               && formatted.front() == '-') {
      formatted.insert(1U, padding, '0');
    } else {
      formatted.insert(
          0U, padding, zero_pad ? '0' : ' ');
    }
  }
  std::string result;
  result.reserve(prefix.size() + formatted.size() + suffix.size());
  result.append(prefix);
  result.append(formatted);
  result.append(suffix);
  return result;
}

[[nodiscard]] std::string make_time_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const SimulationTick tick,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad) {
  auto formatted = std::to_string(tick);
  if (formatted.size() < minimum_width) {
    const auto padding =
        static_cast<std::size_t>(minimum_width) - formatted.size();
    if (left_justify) {
      formatted.append(padding, ' ');
    } else {
      formatted.insert(0U, padding, zero_pad ? '0' : ' ');
    }
  }
  std::string result;
  result.reserve(prefix.size() + formatted.size() + suffix.size());
  result.append(prefix);
  result.append(formatted);
  result.append(suffix);
  return result;
}

[[nodiscard]] bool edge_matches(EdgeKind edge, Logic4 old_value,
                                Logic4 new_value) noexcept {
  if (old_value == new_value) {
    return false;
  }
  if (edge == EdgeKind::any) {
    return true;
  }
  if (edge == EdgeKind::posedge) {
    return (old_value == Logic4::zero &&
            (new_value == Logic4::one || new_value == Logic4::x ||
             new_value == Logic4::z)) ||
           ((old_value == Logic4::x || old_value == Logic4::z) &&
            new_value == Logic4::one);
  }
  return (old_value == Logic4::one &&
          (new_value == Logic4::zero || new_value == Logic4::x ||
           new_value == Logic4::z)) ||
         ((old_value == Logic4::x || old_value == Logic4::z) &&
          new_value == Logic4::zero);
}



} // namespace fsim::runtime::simir
