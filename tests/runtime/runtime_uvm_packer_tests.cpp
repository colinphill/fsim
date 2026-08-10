// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_packer.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] std::vector<fsim::runtime::SystemVerilogUvmPackItem> items() {
  using namespace fsim::runtime;
  SystemVerilogUvmPackItem bits;
  bits.name = "logic";
  bits.type_name = "logic[3:0]";
  bits.kind = SystemVerilogUvmPackItemKind::Bits;
  bits.bits = PackedLogic4::from_msb_string("10xz");

  SystemVerilogUvmPackItem bytes;
  bytes.name = "bytes";
  bytes.type_name = "byte[]";
  bytes.kind = SystemVerilogUvmPackItemKind::Bytes;
  bytes.bytes = {0x01, 0x7f, 0x80, 0xff};

  SystemVerilogUvmPackItem integers;
  integers.name = "integers";
  integers.type_name = "longint[]";
  integers.kind = SystemVerilogUvmPackItemKind::Integers;
  integers.integers = {UINT64_C(0x0102030405060708), UINT64_C(0xfedcba98)};

  SystemVerilogUvmPackItem string;
  string.name = "text";
  string.type_name = "string";
  string.kind = SystemVerilogUvmPackItemKind::String;
  string.string_value = std::string{"pack\0text", 9};

  SystemVerilogUvmPackItem real;
  real.name = "real";
  real.type_name = "real";
  real.kind = SystemVerilogUvmPackItemKind::Real;
  real.real_value = 3.25;

  SystemVerilogUvmPackItem object;
  object.name = "object";
  object.type_name = "uvm_object";
  object.kind = SystemVerilogUvmPackItemKind::Object;
  object.object_identity = 42;

  SystemVerilogUvmPackItem array;
  array.name = "array";
  array.type_name = "uvm_field[]";
  array.kind = SystemVerilogUvmPackItemKind::Array;
  SystemVerilogUvmPackItem logic9;
  logic9.name = "logic9";
  logic9.type_name = "std_logic_vector";
  logic9.kind = SystemVerilogUvmPackItemKind::Bits;
  logic9.bits = PackedLogic4::from_logic9_msb_string("UX01");
  array.elements = {logic9, object};
  return {bits, bytes, integers, string, real, object, array};
}

}  // namespace

void test_systemverilog_uvm_packer() {
  using namespace fsim::runtime;
  const auto values = items();
  const SystemVerilogUvmPacker big;
  const auto big_payload = big.pack(values);
  require(
      big.unpack(big_payload.bytes) == values && big_payload.item_count == 9 &&
          big_payload.bit_count == 8,
      "big-endian UVM packer must round-trip every scalar/object/array profile");

  SystemVerilogUvmPackerPolicy little_policy;
  little_policy.endian = SystemVerilogUvmPackerEndian::Little;
  const SystemVerilogUvmPacker little{little_policy};
  const auto little_payload = little.pack(values);
  require(
      little_payload.bytes != big_payload.bytes &&
          little.unpack(little_payload.bytes) == values,
      "little-endian UVM packer must change transport order and round-trip");

  SystemVerilogUvmPackerPolicy bare_policy;
  bare_policy.use_metadata = false;
  const SystemVerilogUvmPacker bare{bare_policy};
  const auto bare_payload = bare.pack(values);
  const auto bare_values = bare.unpack(bare_payload.bytes);
  require(
      bare_payload.bytes.size() < big_payload.bytes.size() &&
          bare_values.front().name.empty() &&
          bare_values.front().type_name.empty() &&
          bare_values.front().bits == values.front().bits,
      "metadata-disabled UVM packing must omit names while preserving values");

  std::size_t malformed{};
  const auto expect_malformed = [&](std::vector<std::uint8_t> payload) {
    try {
      (void)big.unpack(payload);
    } catch (const SystemVerilogUvmPackerError& error) {
      if (error.diagnostic_code() == "FSIM-UVM-PACK-001") ++malformed;
    }
  };
  auto bad_magic = big_payload.bytes;
  bad_magic.front() = 0;
  expect_malformed(std::move(bad_magic));
  auto truncated = big_payload.bytes;
  truncated.pop_back();
  expect_malformed(std::move(truncated));
  auto trailing = big_payload.bytes;
  trailing.push_back(0);
  expect_malformed(std::move(trailing));
  expect_malformed(little_payload.bytes);

  std::size_t resource{};
  const auto expect_resource = [&](const auto& operation) {
    try {
      operation();
    } catch (const SystemVerilogUvmPackerError& error) {
      if (error.diagnostic_code() == "FSIM-UVM-PACK-002") ++resource;
    }
  };
  expect_resource([&] {
    SystemVerilogUvmPackerLimits limits;
    limits.maximum_items = 1;
    (void)SystemVerilogUvmPacker{{}, limits}.pack(values);
  });
  expect_resource([&] {
    SystemVerilogUvmPackerLimits limits;
    limits.maximum_bits = 3;
    (void)SystemVerilogUvmPacker{{}, limits}.pack(values);
  });
  expect_resource([&] {
    SystemVerilogUvmPackerLimits limits;
    limits.maximum_payload_bytes = 32;
    (void)SystemVerilogUvmPacker{{}, limits}.pack(values);
  });
  expect_resource([&] {
    SystemVerilogUvmPackerLimits limits;
    limits.maximum_depth = 1;
    (void)SystemVerilogUvmPacker{{}, limits}.pack(values);
  });
  bool invalid_policy{};
  try {
    auto invalid = SystemVerilogUvmPackerPolicy{};
    invalid.endian = static_cast<SystemVerilogUvmPackerEndian>(255);
    (void)SystemVerilogUvmPacker{invalid};
  } catch (const SystemVerilogUvmPackerError& error) {
    invalid_policy = error.diagnostic_code() == "FSIM-UVM-POLICY-001";
  }
  require(
      malformed == 4 && resource == 4 && invalid_policy,
      "UVM packer must reject malformed, mismatched, and resource-bound input");
}

}  // namespace fsim::tests::runtime
