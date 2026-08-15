// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kFstCompressionBundleIdentity =
    "fst-compression-bundle-v1:gzip-store-v1:zlib-fixed-rle-v1";
inline constexpr std::string_view kFstStoredBundleIdentity =
    "fst-compression-bundle-v1:gzip-store-v1:values-none";
inline constexpr std::string_view kFstContainerProfileIdentity =
    "fst-clean-room-profile-v3:declaration-metadata-v1";
inline constexpr std::string_view kFstDeterministicContainerProfile =
    "fsim clean-room FST profile 3; gzip-store-v1; zlib-fixed-rle-v1; metadata-v1";
inline constexpr std::string_view kFstStoredContainerProfile =
    "fsim clean-room FST profile 3; gzip-store-v1; values-none; metadata-v1";

enum class FstCompressionKind : std::uint8_t {
  HierarchyGzipStoreV1,
  InitialValueZlibFixedV1,
};

struct FstCompressionProfile {
  static constexpr std::uint32_t schema_version = 1U;

  FstCompressionKind kind{FstCompressionKind::HierarchyGzipStoreV1};
  std::uint32_t schema{schema_version};
  std::uint32_t algorithm_version{1U};
  std::uint8_t window_bits{15U};
  std::size_t block_bytes{65'535U};
  std::size_t maximum_match_bytes{258U};
  std::size_t minimum_input_bytes{};
  std::string_view identity;
};

struct FstCompressionLimits {
  std::size_t maximum_input_bytes{512U << 20U};
  std::size_t maximum_output_bytes{1U << 30U};
};

struct FstCompressionResult {
  std::vector<std::uint8_t> bytes;
  std::string profile_identity;
  std::string semantic_digest;
  std::string byte_digest;
  bool platform_byte_identical{true};
  std::string variability_reason;
};

[[nodiscard]] const FstCompressionProfile& fst_compression_profile(
    FstCompressionKind kind);

[[nodiscard]] FstCompressionResult compress_fst_block(
    std::span<const std::uint8_t> input, FstCompressionKind kind,
    FstCompressionLimits limits = {});

}  // namespace fsim::runtime
