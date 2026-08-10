// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_packer.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <limits>
#include <string_view>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kMalformed{"FSIM-UVM-PACK-001"};
constexpr std::string_view kResource{"FSIM-UVM-PACK-002"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmPackerError{std::string{code}, std::string{message}};
}

[[nodiscard]] bool valid_endian(
    const SystemVerilogUvmPackerEndian endian) noexcept {
  return endian == SystemVerilogUvmPackerEndian::Big
      || endian == SystemVerilogUvmPackerEndian::Little;
}

[[nodiscard]] bool valid_kind(
    const SystemVerilogUvmPackItemKind kind) noexcept {
  switch (kind) {
  case SystemVerilogUvmPackItemKind::Bits:
  case SystemVerilogUvmPackItemKind::Bytes:
  case SystemVerilogUvmPackItemKind::Integers:
  case SystemVerilogUvmPackItemKind::String:
  case SystemVerilogUvmPackItemKind::Real:
  case SystemVerilogUvmPackItemKind::Object:
  case SystemVerilogUvmPackItemKind::Array:
    return true;
  }
  return false;
}

}  // namespace

SystemVerilogUvmPackerError::SystemVerilogUvmPackerError(
    std::string code,
    std::string message)
    : std::runtime_error{std::move(message)},
      diagnostic_code_(std::move(code)) {}

SystemVerilogUvmPacker::SystemVerilogUvmPacker(
    const SystemVerilogUvmPackerPolicy policy,
    const SystemVerilogUvmPackerLimits limits)
    : policy_(policy), limits_(limits) {
  if (!valid_endian(policy_.endian)) {
    throw SystemVerilogUvmPackerError{
        "FSIM-UVM-POLICY-001", "unknown UVM packer endian policy"};
  }
  if (limits_.maximum_depth == 0 || limits_.maximum_items == 0
      || limits_.maximum_bits == 0
      || limits_.maximum_payload_bytes == 0
      || limits_.maximum_text_bytes == 0) {
    fail(kResource, "UVM packer limits must all be positive");
  }
}

SystemVerilogUvmPackedPayload SystemVerilogUvmPacker::pack(
    const std::span<const SystemVerilogUvmPackItem> items) const {
  SystemVerilogUvmPackedPayload result;
  const auto append_byte = [&](const std::uint8_t value) {
    if (result.bytes.size() >= limits_.maximum_payload_bytes) {
      fail(kResource, "UVM packer payload byte ceiling exceeded");
    }
    result.bytes.push_back(value);
  };
  const auto append_bytes = [&](const std::span<const std::uint8_t> values) {
    if (values.size() > limits_.maximum_payload_bytes - result.bytes.size()) {
      fail(kResource, "UVM packer payload byte ceiling exceeded");
    }
    result.bytes.insert(result.bytes.end(), values.begin(), values.end());
  };
  const auto append_u64 = [&](const std::uint64_t value) {
    for (std::size_t byte = 0; byte < 8; ++byte) {
      const auto shift = policy_.endian == SystemVerilogUvmPackerEndian::Big
          ? (7U - byte) * 8U
          : byte * 8U;
      append_byte(static_cast<std::uint8_t>(value >> shift));
    }
  };
  const auto append_string = [&](const std::string_view value) {
    if (value.size() > limits_.maximum_text_bytes) {
      fail(kResource, "UVM packer text ceiling exceeded");
    }
    append_u64(value.size());
    append_bytes(std::span{
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
  };

  append_byte('F');
  append_byte('U');
  append_byte('P');
  append_byte(1);
  append_byte(static_cast<std::uint8_t>(policy_.endian));
  append_byte(policy_.use_metadata ? 1U : 0U);
  append_u64(items.size());

  std::function<void(const SystemVerilogUvmPackItem&, std::size_t)> append_item;
  append_item = [&](const SystemVerilogUvmPackItem& item,
                    const std::size_t depth) {
    if (depth >= limits_.maximum_depth) {
      fail(kResource, "UVM packer nesting depth ceiling exceeded");
    }
    if (++result.item_count > limits_.maximum_items) {
      fail(kResource, "UVM packer item ceiling exceeded");
    }
    if (!valid_kind(item.kind)) fail(kMalformed, "unknown UVM pack item kind");
    append_byte(static_cast<std::uint8_t>(item.kind));
    if (policy_.use_metadata) {
      append_string(item.name);
      append_string(item.type_name);
    }
    switch (item.kind) {
    case SystemVerilogUvmPackItemKind::Bits: {
      if (item.bits.width() > limits_.maximum_bits - result.bit_count) {
        fail(kResource, "UVM packer bit ceiling exceeded");
      }
      result.bit_count += item.bits.width();
      append_byte(item.bits.is_logic9() ? 1U : 0U);
      append_u64(item.bits.width());
      auto text = item.bits.to_msb_string();
      if (policy_.endian == SystemVerilogUvmPackerEndian::Little) {
        std::ranges::reverse(text);
      }
      append_string(text);
      break;
    }
    case SystemVerilogUvmPackItemKind::Bytes:
      append_u64(item.bytes.size());
      if (policy_.endian == SystemVerilogUvmPackerEndian::Big) {
        append_bytes(item.bytes);
      } else {
        for (auto value = item.bytes.rbegin(); value != item.bytes.rend();
             ++value) {
          append_byte(*value);
        }
      }
      break;
    case SystemVerilogUvmPackItemKind::Integers:
      append_u64(item.integers.size());
      for (const auto value : item.integers) append_u64(value);
      break;
    case SystemVerilogUvmPackItemKind::String:
      append_string(item.string_value);
      break;
    case SystemVerilogUvmPackItemKind::Real:
      append_u64(std::bit_cast<std::uint64_t>(item.real_value));
      break;
    case SystemVerilogUvmPackItemKind::Object:
      append_u64(item.object_identity);
      break;
    case SystemVerilogUvmPackItemKind::Array:
      append_u64(item.elements.size());
      for (const auto& element : item.elements) {
        append_item(element, depth + 1U);
      }
      break;
    }
  };
  for (const auto& item : items) append_item(item, 0);
  return result;
}

std::vector<SystemVerilogUvmPackItem> SystemVerilogUvmPacker::unpack(
    const std::span<const std::uint8_t> payload) const {
  if (payload.size() > limits_.maximum_payload_bytes) {
    fail(kResource, "UVM unpack payload byte ceiling exceeded");
  }
  std::size_t offset{};
  std::size_t item_count{};
  std::size_t bit_count{};
  const auto read_byte = [&]() {
    if (offset >= payload.size()) fail(kMalformed, "truncated UVM pack payload");
    return payload[offset++];
  };
  const auto read_u64 = [&]() {
    if (payload.size() - offset < 8) {
      fail(kMalformed, "truncated UVM pack integer");
    }
    std::uint64_t value{};
    for (std::size_t byte = 0; byte < 8; ++byte) {
      const auto shift = policy_.endian == SystemVerilogUvmPackerEndian::Big
          ? (7U - byte) * 8U
          : byte * 8U;
      value |= static_cast<std::uint64_t>(payload[offset++]) << shift;
    }
    return value;
  };
  const auto read_size = [&](const std::size_t maximum) {
    const auto value = read_u64();
    if (value > maximum) fail(kResource, "UVM unpack length exceeds ceiling");
    return static_cast<std::size_t>(value);
  };
  const auto read_string = [&]() {
    const auto size = read_size(limits_.maximum_text_bytes);
    if (size > payload.size() - offset) {
      fail(kMalformed, "truncated UVM pack text");
    }
    std::string result{
        reinterpret_cast<const char*>(payload.data() + offset), size};
    offset += size;
    return result;
  };

  if (read_byte() != 'F' || read_byte() != 'U' || read_byte() != 'P'
      || read_byte() != 1) {
    fail(kMalformed, "invalid UVM pack header or schema");
  }
  if (read_byte() != static_cast<std::uint8_t>(policy_.endian)
      || read_byte() != (policy_.use_metadata ? 1U : 0U)) {
    fail(kMalformed, "UVM pack payload policy does not match the unpacker");
  }
  const auto root_count = read_size(limits_.maximum_items);

  std::function<SystemVerilogUvmPackItem(std::size_t)> read_item;
  read_item = [&](const std::size_t depth) {
    if (depth >= limits_.maximum_depth) {
      fail(kResource, "UVM unpack nesting depth ceiling exceeded");
    }
    if (++item_count > limits_.maximum_items) {
      fail(kResource, "UVM unpack item ceiling exceeded");
    }
    SystemVerilogUvmPackItem item;
    item.kind = static_cast<SystemVerilogUvmPackItemKind>(read_byte());
    if (!valid_kind(item.kind)) fail(kMalformed, "unknown UVM pack item tag");
    if (policy_.use_metadata) {
      item.name = read_string();
      item.type_name = read_string();
    }
    switch (item.kind) {
    case SystemVerilogUvmPackItemKind::Bits: {
      const auto logic9 = read_byte();
      if (logic9 > 1U) fail(kMalformed, "invalid UVM packed logic domain");
      const auto width = read_size(limits_.maximum_bits);
      if (width > limits_.maximum_bits - bit_count) {
        fail(kResource, "UVM unpack bit ceiling exceeded");
      }
      bit_count += width;
      auto text = read_string();
      if (text.size() != width) fail(kMalformed, "UVM packed width mismatch");
      if (policy_.endian == SystemVerilogUvmPackerEndian::Little) {
        std::ranges::reverse(text);
      }
      item.bits = logic9 != 0
          ? PackedLogic4::from_logic9_msb_string(text)
          : PackedLogic4::from_msb_string(text);
      break;
    }
    case SystemVerilogUvmPackItemKind::Bytes: {
      const auto size = read_size(limits_.maximum_payload_bytes);
      if (size > payload.size() - offset) {
        fail(kMalformed, "truncated UVM packed byte array");
      }
      item.bytes.assign(
          payload.begin() + static_cast<std::ptrdiff_t>(offset),
          payload.begin() + static_cast<std::ptrdiff_t>(offset + size));
      offset += size;
      if (policy_.endian == SystemVerilogUvmPackerEndian::Little) {
        std::ranges::reverse(item.bytes);
      }
      break;
    }
    case SystemVerilogUvmPackItemKind::Integers: {
      const auto size = read_size(limits_.maximum_items);
      item.integers.reserve(size);
      for (std::size_t index = 0; index < size; ++index) {
        item.integers.push_back(read_u64());
      }
      break;
    }
    case SystemVerilogUvmPackItemKind::String:
      item.string_value = read_string();
      break;
    case SystemVerilogUvmPackItemKind::Real:
      item.real_value = std::bit_cast<double>(read_u64());
      break;
    case SystemVerilogUvmPackItemKind::Object:
      item.object_identity = read_u64();
      break;
    case SystemVerilogUvmPackItemKind::Array: {
      const auto size = read_size(limits_.maximum_items);
      item.elements.reserve(size);
      for (std::size_t index = 0; index < size; ++index) {
        item.elements.push_back(read_item(depth + 1U));
      }
      break;
    }
    }
    return item;
  };

  std::vector<SystemVerilogUvmPackItem> result;
  result.reserve(root_count);
  for (std::size_t index = 0; index < root_count; ++index) {
    result.push_back(read_item(0));
  }
  if (offset != payload.size()) fail(kMalformed, "trailing UVM pack payload");
  return result;
}

}  // namespace fsim::runtime
