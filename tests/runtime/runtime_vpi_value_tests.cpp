// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::tests::runtime {

namespace {

using fsim::runtime::Logic4;
using fsim::runtime::Logic9;
using fsim::runtime::PackedBit2;
using fsim::runtime::PackedLogic4;
using fsim::runtime::PackedLogic9;
using fsim::runtime::SystemVerilogVpiDriveStrength;
using fsim::runtime::SystemVerilogVpiNetKind;
using fsim::runtime::SystemVerilogVpiObjectDescriptor;
using fsim::runtime::SystemVerilogVpiObjectError;
using fsim::runtime::SystemVerilogVpiObjectKind;
using fsim::runtime::SystemVerilogVpiObjectRegistry;
using fsim::runtime::SystemVerilogVpiStoredValue;
using fsim::runtime::SystemVerilogVpiStrengthRank;
using fsim::runtime::SystemVerilogVpiTypeInfo;
using fsim::runtime::SystemVerilogVpiValueError;
using fsim::runtime::SystemVerilogVpiValueFormat;
using fsim::runtime::SystemVerilogVpiValueReadBuffers;
using fsim::runtime::SystemVerilogVpiValueCategory;

void require_vpi_value(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

SystemVerilogVpiTypeInfo value_type(
    const SystemVerilogVpiValueCategory category,
    const std::uint32_t width,
    const bool is_signed = false) {
  SystemVerilogVpiTypeInfo type;
  type.category = category;
  type.width = width;
  type.is_signed = is_signed;
  return type;
}

template <typename Value>
SystemVerilogVpiStoredValue stored(Value value) {
  SystemVerilogVpiStoredValue result;
  result.payload = std::move(value);
  return result;
}

}  // namespace

void test_systemverilog_vpi_checked_value_reads() {
  SystemVerilogVpiObjectRegistry registry{701};
  const auto root =
      registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");
  const auto create = [&](const SystemVerilogVpiObjectKind kind,
                          const char* const name,
                          const SystemVerilogVpiTypeInfo& type) {
    return registry.create(SystemVerilogVpiObjectDescriptor{
        kind, root.value, name, std::nullopt, type});
  };

  const auto bit_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "bits",
      value_type(SystemVerilogVpiValueCategory::Bit2, 65));
  const auto logic4_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "logic4",
      value_type(SystemVerilogVpiValueCategory::Logic4, 65));
  const auto logic9_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "logic9",
      value_type(SystemVerilogVpiValueCategory::Logic9, 65));
  const auto integer_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "integer",
      value_type(SystemVerilogVpiValueCategory::Integer4, 8, true));
  const auto unknown_integer_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "unknown_integer",
      value_type(SystemVerilogVpiValueCategory::Integer4, 2));
  const auto real_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "real_value",
      value_type(SystemVerilogVpiValueCategory::Real, 64));
  const auto shortreal_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "shortreal_value",
      value_type(SystemVerilogVpiValueCategory::ShortReal, 32));
  const auto string_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "string_value",
      value_type(SystemVerilogVpiValueCategory::String, 0));
  const auto time_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "time_value",
      value_type(SystemVerilogVpiValueCategory::Time, 64));
  auto strength_type = value_type(
      SystemVerilogVpiValueCategory::Logic4, 1);
  strength_type.net_kind = SystemVerilogVpiNetKind::Wire;
  const auto strength_object = create(
      SystemVerilogVpiObjectKind::Net, "strength_value", strength_type);
  const auto unbound_object = create(
      SystemVerilogVpiObjectKind::Variable,
      "unbound",
      value_type(SystemVerilogVpiValueCategory::Logic4, 1));
  require_vpi_value(
      root && bit_object && logic4_object && logic9_object
          && integer_object && unknown_integer_object && real_object
          && shortreal_object && string_object && time_object
          && strength_object && unbound_object,
      "VPI value fixtures publish every readable scalar category");

  PackedBit2 bits{65};
  bits.set(0, true);
  bits.set(64, true);
  PackedLogic4 logic4{65, Logic4::zero};
  logic4.set(0, Logic4::one);
  logic4.set(63, Logic4::x);
  logic4.set(64, Logic4::z);
  PackedLogic9 logic9{65, Logic9::zero};
  logic9.set(1, Logic9::w);
  logic9.set(63, Logic9::h);
  logic9.set(64, Logic9::dont_care);
  auto unknown_integer = PackedLogic4::from_msb_string("x1");
  auto strength = stored(PackedLogic4{1, Logic4::one});
  strength.strength = SystemVerilogVpiDriveStrength{
      SystemVerilogVpiStrengthRank::Pull,
      SystemVerilogVpiStrengthRank::Supply};

  require_vpi_value(
      registry.bind_value(bit_object.value, stored(bits))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(logic4_object.value, stored(logic4))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(logic9_object.value, stored(logic9))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(
                 integer_object.value,
                 stored(PackedLogic4::from_msb_string("11111011")))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(
                 unknown_integer_object.value,
                 stored(unknown_integer))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(real_object.value, stored(3.25))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(shortreal_object.value, stored(1.25F))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(
                 string_object.value, stored(std::string{"a\0b", 3}))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(
                 time_object.value,
                 stored(std::uint64_t{0xfedcba9876543210ULL}))
              == SystemVerilogVpiValueError::None
          && registry.bind_value(strength_object.value, strength)
              == SystemVerilogVpiValueError::None,
      "VPI host binding accepts exact canonical values for every readable category");

  std::array<std::uint64_t, 1> short_words{
      0xaaaaaaaaaaaaaaaaULL};
  const auto short_bit_read = registry.read_value(
      bit_object.value,
      SystemVerilogVpiValueFormat::BitVector,
      SystemVerilogVpiValueReadBuffers{short_words, {}});
  require_vpi_value(
      short_bit_read.error == SystemVerilogVpiValueError::BufferTooSmall
          && short_bit_read.required_words == 2
          && short_words.front() == 0xaaaaaaaaaaaaaaaaULL,
      "VPI undersized vector reads report required words without partial writes");

  std::array<std::uint64_t, 2> bit_words{};
  const auto bit_read = registry.read_value(
      bit_object.value,
      SystemVerilogVpiValueFormat::BitVector,
      SystemVerilogVpiValueReadBuffers{bit_words, {}});
  require_vpi_value(
      bit_read && bit_read.required_words == 2
          && bit_words[0] == 1U && bit_words[1] == 1U,
      "VPI two-state reads preserve the 64-bit caller-buffer boundary");

  std::array<std::uint64_t, 4> logic4_words{};
  const auto logic4_read = registry.read_value(
      logic4_object.value,
      SystemVerilogVpiValueFormat::Logic4Vector,
      SystemVerilogVpiValueReadBuffers{logic4_words, {}});
  require_vpi_value(
      logic4_read && logic4_read.required_words == 4
          && logic4_words[0] == ((std::uint64_t{1} << 63U) | 1U)
          && logic4_words[1] == 0U
          && logic4_words[2] == (std::uint64_t{1} << 63U)
          && logic4_words[3] == 1U,
      "VPI four-state reads preserve aval/bval planes and X/Z at word boundaries");

  std::array<std::uint64_t, 8> logic9_words{};
  const auto logic9_read = registry.read_value(
      logic9_object.value,
      SystemVerilogVpiValueFormat::Logic9Vector,
      SystemVerilogVpiValueReadBuffers{logic9_words, {}});
  require_vpi_value(
      logic9_read && logic9_read.required_words == 8
          && logic9_words[0] == ((std::uint64_t{1} << 63U) | 2U)
          && logic9_words[2]
              == (std::numeric_limits<std::uint64_t>::max() & ~2ULL)
          && logic9_words[4] == ((std::uint64_t{1} << 63U) | 2U)
          && logic9_words[7] == 1U,
      "VPI nine-state reads preserve weak and don't-care planes at word boundaries");

  const auto integer_read = registry.read_value(
      integer_object.value, SystemVerilogVpiValueFormat::Integer);
  require_vpi_value(
      integer_read && integer_read.integer == 0xfbU
          && integer_read.integer_is_signed
          && registry.read_value(
                 unknown_integer_object.value,
                 SystemVerilogVpiValueFormat::Integer).error
              == SystemVerilogVpiValueError::UnknownState,
      "VPI integer reads retain raw signed bits and reject X/Z information loss");

  const auto real_read = registry.read_value(
      real_object.value, SystemVerilogVpiValueFormat::Real);
  const auto shortreal_read = registry.read_value(
      shortreal_object.value, SystemVerilogVpiValueFormat::Real);
  require_vpi_value(
      real_read && real_read.real == 3.25
          && shortreal_read && shortreal_read.real == 1.25,
      "VPI real reads preserve double values and exact widened shortreal values");

  std::array<char, 3> short_characters{'x', 'y', 'z'};
  const auto short_string_read = registry.read_value(
      string_object.value,
      SystemVerilogVpiValueFormat::String,
      SystemVerilogVpiValueReadBuffers{{}, short_characters});
  require_vpi_value(
      short_string_read.error == SystemVerilogVpiValueError::BufferTooSmall
          && short_string_read.required_characters == 4
          && short_characters == std::array<char, 3>{'x', 'y', 'z'},
      "VPI undersized string reads retain embedded data and leave caller buffers untouched");
  std::array<char, 4> characters{};
  const auto string_read = registry.read_value(
      string_object.value,
      SystemVerilogVpiValueFormat::String,
      SystemVerilogVpiValueReadBuffers{{}, characters});
  require_vpi_value(
      string_read && string_read.required_characters == 4
          && characters == std::array<char, 4>{'a', '\0', 'b', '\0'},
      "VPI string reads preserve embedded NUL bytes and append a bounded terminator");

  const auto time_read = registry.read_value(
      time_object.value, SystemVerilogVpiValueFormat::Time);
  const auto strength_read = registry.read_value(
      strength_object.value, SystemVerilogVpiValueFormat::Strength);
  require_vpi_value(
      time_read && time_read.time == 0xfedcba9876543210ULL
          && strength_read
          && strength_read.strength.state == Logic4::one
          && strength_read.strength.drive
              == SystemVerilogVpiDriveStrength{
                  SystemVerilogVpiStrengthRank::Pull,
                  SystemVerilogVpiStrengthRank::Supply},
      "VPI time and strength reads preserve full ticks and distinct zero/one ranks");

  require_vpi_value(
      registry.read_value(
          strength_object.value, SystemVerilogVpiValueFormat::Scalar).scalar
              == Logic9::one
          && registry.read_value(
                 logic9_object.value,
                 SystemVerilogVpiValueFormat::Logic4Vector).error
              == SystemVerilogVpiValueError::UnsupportedFormat
          && registry.read_value(
                 bit_object.value,
                 static_cast<SystemVerilogVpiValueFormat>(99)).error
              == SystemVerilogVpiValueError::UnsupportedFormat,
      "VPI scalar reads convert losslessly and reject lossy or unknown formats");

  require_vpi_value(
      registry.bind_value(bit_object.value, stored(bits))
              == SystemVerilogVpiValueError::AlreadyBound
          && registry.bind_value(
                 unbound_object.value, stored(PackedLogic4{2}))
              == SystemVerilogVpiValueError::TypeMismatch
          && registry.read_value(
                 unbound_object.value,
                 SystemVerilogVpiValueFormat::Scalar).error
              == SystemVerilogVpiValueError::NotBound
          && registry.read_value(
                 root.value, SystemVerilogVpiValueFormat::Scalar).error
              == SystemVerilogVpiValueError::NotBound,
      "VPI value ownership rejects rebinding and mismatched widths without publication");

  const auto transient = create(
      SystemVerilogVpiObjectKind::Variable,
      "transient",
      value_type(SystemVerilogVpiValueCategory::Logic4, 1));
  require_vpi_value(
      transient
          && registry.bind_value(
                 transient.value, stored(PackedLogic4{1, Logic4::zero}))
              == SystemVerilogVpiValueError::None
          && registry.release(transient.value)
              == SystemVerilogVpiObjectError::None
          && registry.read_value(
                 transient.value,
                 SystemVerilogVpiValueFormat::Scalar).error
              == SystemVerilogVpiValueError::ReleasedHandle,
      "VPI value reads distinguish released handles before slot reuse");
  const auto replacement = create(
      SystemVerilogVpiObjectKind::Variable,
      "replacement",
      value_type(SystemVerilogVpiValueCategory::Logic4, 1));

  SystemVerilogVpiObjectRegistry other{702};
  const auto other_root =
      other.create(SystemVerilogVpiObjectKind::Root, 0, "other");
  require_vpi_value(
      replacement && other_root
          && registry.read_value(
                 transient.value,
                 SystemVerilogVpiValueFormat::Scalar).error
              == SystemVerilogVpiValueError::StaleHandle
          && other.read_value(
                 bit_object.value,
                 SystemVerilogVpiValueFormat::BitVector).error
              == SystemVerilogVpiValueError::CrossSimulation
          && registry.read_value(
                 0, SystemVerilogVpiValueFormat::Scalar).error
              == SystemVerilogVpiValueError::InvalidHandle,
      "VPI value reads preserve stale, cross-simulation, and malformed handle identity");

  auto invalid_strength = stored(PackedLogic4{1, Logic4::zero});
  invalid_strength.strength = SystemVerilogVpiDriveStrength{
      static_cast<SystemVerilogVpiStrengthRank>(99),
      SystemVerilogVpiStrengthRank::Strong};
  require_vpi_value(
      registry.bind_value(replacement.value, invalid_strength)
          == SystemVerilogVpiValueError::TypeMismatch,
      "VPI value binding rejects foreign strength encodings");
}

}  // namespace fsim::tests::runtime
