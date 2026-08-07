// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogVpiLanguage {
  Verilog2005,
  SystemVerilog2017,
};

enum class SystemVerilogVpiValueCategory {
  None,
  Bit2,
  Logic4,
  Logic9,
  Integer2,
  Integer4,
  Real,
  ShortReal,
  Time,
  String,
  Event,
};

enum class SystemVerilogVpiNetKind {
  None,
  Wire,
  Tri,
  Wand,
  Wor,
  Tri0,
  Tri1,
  Supply0,
  Supply1,
  Uwire,
};

enum class SystemVerilogVpiDirection {
  None,
  Input,
  Output,
  Inout,
};

enum class SystemVerilogVpiLifetime {
  Static,
  Automatic,
};

enum class SystemVerilogVpiDescriptorKind {
  Scalar,
  PackedArray,
  UnpackedArray,
  DynamicArray,
  Queue,
  AssociativeArray,
  Struct,
  Union,
  Enum,
  String,
  Class,
  ClassHandle,
};

struct SystemVerilogVpiRange {
  std::int64_t left{};
  std::int64_t right{};

  friend bool operator==(
      const SystemVerilogVpiRange&,
      const SystemVerilogVpiRange&) = default;
};

struct SystemVerilogVpiEnumLiteral {
  std::string name;
  std::int64_t value{};

  friend bool operator==(
      const SystemVerilogVpiEnumLiteral&,
      const SystemVerilogVpiEnumLiteral&) = default;
};

struct SystemVerilogVpiTypeDescriptor {
  SystemVerilogVpiDescriptorKind kind{
      SystemVerilogVpiDescriptorKind::Scalar};
  SystemVerilogVpiValueCategory category{
      SystemVerilogVpiValueCategory::Logic4};
  std::uint32_t width{1};
  bool is_signed{};
  std::string nominal_name;
  std::vector<SystemVerilogVpiRange> ranges;
  std::optional<std::uint64_t> maximum_size;
  std::vector<SystemVerilogVpiTypeDescriptor> children;
  std::vector<std::string> member_names;
  std::vector<SystemVerilogVpiEnumLiteral> enum_literals;

  friend bool operator==(
      const SystemVerilogVpiTypeDescriptor&,
      const SystemVerilogVpiTypeDescriptor&) = default;
};

struct SystemVerilogVpiTypeInfo {
  SystemVerilogVpiLanguage language{
      SystemVerilogVpiLanguage::SystemVerilog2017};
  SystemVerilogVpiValueCategory category{
      SystemVerilogVpiValueCategory::None};
  SystemVerilogVpiNetKind net_kind{SystemVerilogVpiNetKind::None};
  SystemVerilogVpiDirection direction{SystemVerilogVpiDirection::None};
  SystemVerilogVpiLifetime lifetime{SystemVerilogVpiLifetime::Static};
  std::uint32_t width{};
  bool is_signed{};
  bool is_constant{};
  std::shared_ptr<const SystemVerilogVpiTypeDescriptor> descriptor;
};

}  // namespace fsim::runtime
