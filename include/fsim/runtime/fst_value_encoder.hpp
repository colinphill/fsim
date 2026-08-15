// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

struct SystemVerilogVpiStrengthValue;

inline constexpr std::size_t fst_maximum_type_metadata_bytes { 1U << 20U };
inline constexpr std::size_t fst_maximum_string_bytes { 1U << 20U };
inline constexpr std::size_t fst_maximum_type_members { 1U << 16U };
inline constexpr std::size_t fst_maximum_shape_dimensions { 64U };

enum class FstValueProfile : std::uint8_t {
  BitVector,
  LogicVector,
  SystemVerilogBit,
  SystemVerilogLogic,
  SystemVerilogReg,
  SystemVerilogShortReal,
  SystemVerilogReal,
  SystemVerilogRealtime,
  SystemVerilogTime,
  SystemVerilogOpaqueHandle,
  SystemVerilogString,
  Enumeration,
  VhdlPhysical,
  VhdlTime,
  VhdlLogic9,
  TypedLeaf
};

enum class FstPayloadKind : std::uint8_t {
  Symbols,
  Real,
  String
};

enum class FstTypeLanguage : std::uint8_t {
  SystemVerilog,
  Vhdl
};

enum class FstExtendedTypeKind : std::uint8_t {
  Enumeration,
  VhdlPhysical,
  VhdlTime,
  VhdlLogic9
};

enum class FstLeafKind : std::uint8_t {
  ResolvedState,
  ResolvedStrengthZero,
  ResolvedStrengthOne,
  PackedAggregate,
  UnpackedAggregate,
  DynamicClass,
  Container,
  Coverage,
  Assertion
};

struct FstShapeDimensionMetadata {
  std::int64_t left { };
  std::int64_t right { };
  bool packed { };
  friend bool operator==(
      const FstShapeDimensionMetadata&,
      const FstShapeDimensionMetadata&) = default;
};

struct FstLeafTypeMetadata {
  FstLeafKind kind { FstLeafKind::PackedAggregate };
  FstTypeLanguage language { FstTypeLanguage::SystemVerilog };
  std::string owner_identity;
  std::string leaf_path;
  std::size_t width { };
  bool four_state { };
  std::vector<FstShapeDimensionMetadata> dimensions;
};

struct FstPhysicalUnitMetadata {
  std::string name;
  std::int64_t primary_ticks { };
  friend bool operator==(
      const FstPhysicalUnitMetadata&,
      const FstPhysicalUnitMetadata&) = default;
};

struct FstExtendedTypeMetadata {
  FstExtendedTypeKind kind { FstExtendedTypeKind::Enumeration };
  FstTypeLanguage language { FstTypeLanguage::SystemVerilog };
  std::string nominal_name;
  std::size_t width { };
  bool four_state { };
  std::vector<std::string> enumeration_literals;
  std::vector<FstPhysicalUnitMetadata> physical_units;
};

/// Immutable exact FST payload with canonical source-type identity.
class FstEncodedValue final {
public:
  [[nodiscard]] FstValueProfile profile() const noexcept { return profile_; }
  [[nodiscard]] FstPayloadKind payload_kind() const noexcept {
    return payload_kind_;
  }
  [[nodiscard]] std::size_t width() const noexcept { return width_; }
  [[nodiscard]] std::string_view symbols() const noexcept { return payload_; }
  [[nodiscard]] std::string_view string_bytes() const noexcept {
    return payload_;
  }
  [[nodiscard]] std::uint64_t real_bits() const noexcept { return real_bits_; }
  [[nodiscard]] std::string_view canonical_type() const noexcept {
    return canonical_type_;
  }

  friend bool operator==(
      const FstEncodedValue&, const FstEncodedValue&) = default;

private:
  FstEncodedValue(
      FstValueProfile profile,
      FstPayloadKind payload_kind,
      std::size_t width,
      std::string payload,
      std::uint64_t real_bits,
      std::string canonical_type);

  FstValueProfile profile_ { FstValueProfile::LogicVector };
  FstPayloadKind payload_kind_ { FstPayloadKind::Symbols };
  std::size_t width_ { };
  std::string payload_;
  std::uint64_t real_bits_ { };
  std::string canonical_type_;

  friend FstEncodedValue encode_fst_bit_value(
      const PackedBit2&, FstValueProfile);
  friend FstEncodedValue encode_fst_logic_value(
      const PackedLogic4&, FstValueProfile);
  friend FstEncodedValue encode_fst_systemverilog_scalar(
      const PackedLogic4&, SystemVerilogScalarKind);
  friend FstEncodedValue encode_fst_systemverilog_real(
      const SystemVerilogScalarValue&);
  friend FstEncodedValue encode_fst_string(std::string_view);
  friend FstEncodedValue encode_fst_extended_value(
      const PackedLogic4&,
      FstValueProfile,
      std::string_view);
  friend FstEncodedValue encode_fst_leaf_value(
      const PackedLogic4&,
      const FstLeafTypeMetadata&);
  friend FstEncodedValue encode_fst_leaf_value(
      const PackedLogic4&,
      std::string_view);
  friend FstEncodedValue make_fst_unknown_value(
      FstValueProfile, std::size_t);
};

struct FstResolvedStrengthEncoding {
  FstEncodedValue state;
  FstEncodedValue zero_strength;
  FstEncodedValue one_strength;
};

[[nodiscard]] FstEncodedValue encode_fst_bit_value(
    const PackedBit2& value,
    FstValueProfile profile = FstValueProfile::BitVector);

[[nodiscard]] FstEncodedValue encode_fst_logic_value(
    const PackedLogic4& value,
    FstValueProfile profile = FstValueProfile::LogicVector);

[[nodiscard]] FstEncodedValue encode_fst_systemverilog_scalar(
    const PackedLogic4& value,
    SystemVerilogScalarKind kind);

[[nodiscard]] FstEncodedValue encode_fst_systemverilog_real(
    const SystemVerilogScalarValue& value);

[[nodiscard]] std::string_view canonical_fst_systemverilog_real_type(
    SystemVerilogScalarKind kind);

[[nodiscard]] FstEncodedValue encode_fst_string(std::string_view value);

[[nodiscard]] std::string_view canonical_fst_string_type() noexcept;

[[nodiscard]] std::string canonical_fst_type_metadata(
    const FstExtendedTypeMetadata& metadata);

[[nodiscard]] FstEncodedValue encode_fst_extended_value(
    const PackedLogic4& value,
    FstValueProfile profile,
    const FstExtendedTypeMetadata& metadata);

[[nodiscard]] FstEncodedValue encode_fst_extended_value(
    const PackedLogic4& value,
    FstValueProfile profile,
    std::string_view canonical_type);

[[nodiscard]] std::string canonical_fst_leaf_type_metadata(
    const FstLeafTypeMetadata& metadata);

[[nodiscard]] FstEncodedValue encode_fst_leaf_value(
    const PackedLogic4& value,
    const FstLeafTypeMetadata& metadata);

[[nodiscard]] FstEncodedValue encode_fst_leaf_value(
    const PackedLogic4& value,
    std::string_view canonical_type);

[[nodiscard]] FstResolvedStrengthEncoding encode_fst_resolved_strength(
    const SystemVerilogVpiStrengthValue& value,
    std::string_view owner_identity,
    std::string_view leaf_path);

/// Represents an interval for which no exact value has been observed yet.
[[nodiscard]] FstEncodedValue make_fst_unknown_value(
    FstValueProfile profile,
    std::size_t width);

[[nodiscard]] bool fst_profile_is_packed(FstValueProfile profile) noexcept;

} // namespace fsim::runtime
