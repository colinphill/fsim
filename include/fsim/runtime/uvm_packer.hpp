// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogUvmPackerEndian : std::uint8_t {
  Big,
  Little,
};

enum class SystemVerilogUvmPackItemKind : std::uint8_t {
  Bits,
  Bytes,
  Integers,
  String,
  Real,
  Object,
  Array,
};

struct SystemVerilogUvmPackItem {
  std::string name;
  std::string type_name;
  SystemVerilogUvmPackItemKind kind{SystemVerilogUvmPackItemKind::Bits};
  PackedLogic4 bits;
  std::vector<std::uint8_t> bytes;
  std::vector<std::uint64_t> integers;
  std::string string_value;
  double real_value{};
  std::uint64_t object_identity{};
  std::vector<SystemVerilogUvmPackItem> elements;

  friend bool operator==(
      const SystemVerilogUvmPackItem&,
      const SystemVerilogUvmPackItem&) = default;
};

struct SystemVerilogUvmPackerPolicy {
  SystemVerilogUvmPackerEndian endian{SystemVerilogUvmPackerEndian::Big};
  bool use_metadata{true};
};

struct SystemVerilogUvmPackerLimits {
  std::size_t maximum_depth{1024};
  std::size_t maximum_items{1U << 20U};
  std::size_t maximum_bits{1U << 28U};
  std::size_t maximum_payload_bytes{1U << 28U};
  std::size_t maximum_text_bytes{1U << 24U};
};

struct SystemVerilogUvmPackedPayload {
  std::vector<std::uint8_t> bytes;
  std::size_t item_count{};
  std::size_t bit_count{};
};

class SystemVerilogUvmPackerError final : public std::runtime_error {
 public:
  SystemVerilogUvmPackerError(std::string code, std::string message);
  [[nodiscard]] const std::string& diagnostic_code() const noexcept {
    return diagnostic_code_;
  }

 private:
  std::string diagnostic_code_;
};

class SystemVerilogUvmPacker final {
 public:
  explicit SystemVerilogUvmPacker(
      SystemVerilogUvmPackerPolicy policy = {},
      SystemVerilogUvmPackerLimits limits = {});

  [[nodiscard]] SystemVerilogUvmPackedPayload pack(
      std::span<const SystemVerilogUvmPackItem> items) const;
  [[nodiscard]] std::vector<SystemVerilogUvmPackItem> unpack(
      std::span<const std::uint8_t> payload) const;

  [[nodiscard]] const SystemVerilogUvmPackerPolicy& policy() const noexcept {
    return policy_;
  }
  [[nodiscard]] const SystemVerilogUvmPackerLimits& limits() const noexcept {
    return limits_;
  }

 private:
  SystemVerilogUvmPackerPolicy policy_;
  SystemVerilogUvmPackerLimits limits_;
};

}  // namespace fsim::runtime
