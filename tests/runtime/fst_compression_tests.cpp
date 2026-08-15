// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/fst_compression.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Function>
void require_rejects(Function&& function, const std::string_view message)
{
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    } catch (const std::length_error&) {
        return;
    }
    require(false, message);
}

std::vector<std::uint8_t> bytes(const std::string_view value)
{
    return { value.begin(), value.end() };
}

void test_profiles()
{
    using namespace fsim::runtime;
    require(kFstCompressionBundleIdentity == "fst-compression-bundle-v1:gzip-store-v1:zlib-fixed-rle-v1",
        "compression bundle identity must remain frozen");
    require(kFstStoredBundleIdentity == "fst-compression-bundle-v1:gzip-store-v1:values-none",
        "stored bundle identity must remain frozen");
    const auto& hierarchy = fst_compression_profile(FstCompressionKind::HierarchyGzipStoreV1);
    require(hierarchy.schema == 1U && hierarchy.algorithm_version == 1U && hierarchy.window_bits == 15U && hierarchy.block_bytes == 65'535U && hierarchy.maximum_match_bytes == 0U && hierarchy.minimum_input_bytes == 0U && hierarchy.identity == "fst-gzip-store-v1",
        "hierarchy compression profile must remain frozen");
    const auto& values = fst_compression_profile(FstCompressionKind::InitialValueZlibFixedV1);
    require(values.schema == 1U && values.algorithm_version == 1U && values.window_bits == 15U && values.block_bytes == 65'535U && values.maximum_match_bytes == 258U && values.minimum_input_bytes == 128U && values.identity == "fst-zlib-fixed-rle-v1",
        "initial-value compression profile must remain frozen");
}

void test_canonical_empty_and_small()
{
    using namespace fsim::runtime;
    const auto gzip_empty = compress_fst_block(
        { }, FstCompressionKind::HierarchyGzipStoreV1);
    const std::vector<std::uint8_t> expected_gzip_empty {
        0x1f,
        0x8b,
        0x08,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0xff,
        0x01,
        0x00,
        0x00,
        0xff,
        0xff,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
    };
    require(gzip_empty.bytes == expected_gzip_empty,
        "empty stored gzip bytes must be canonical");

    const auto abc = bytes("abc");
    const auto gzip_abc = compress_fst_block(
        abc, FstCompressionKind::HierarchyGzipStoreV1);
    const std::vector<std::uint8_t> expected_gzip_abc {
        0x1f,
        0x8b,
        0x08,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0xff,
        0x01,
        0x03,
        0x00,
        0xfc,
        0xff,
        0x61,
        0x62,
        0x63,
        0xc2,
        0x41,
        0x24,
        0x35,
        0x03,
        0x00,
        0x00,
        0x00,
    };
    require(gzip_abc.bytes == expected_gzip_abc,
        "small stored gzip bytes must be canonical");
    require(gzip_abc.semantic_digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "semantic digest must identify uncompressed input");

    const auto zlib_empty = compress_fst_block(
        { }, FstCompressionKind::InitialValueZlibFixedV1);
    const std::vector<std::uint8_t> expected_zlib_empty {
        0x78,
        0x01,
        0x03,
        0x00,
        0x00,
        0x00,
        0x00,
        0x01,
    };
    require(zlib_empty.bytes == expected_zlib_empty,
        "empty fixed zlib bytes must be canonical");
    require(gzip_empty.semantic_digest == zlib_empty.semantic_digest && gzip_empty.byte_digest != zlib_empty.byte_digest,
        "profile identity must separate bytes without changing semantics");
}

void test_large_block_and_runs()
{
    using namespace fsim::runtime;
    std::vector<std::uint8_t> large(65'536U);
    for (std::size_t index = 0U; index < large.size(); ++index) {
        large[index] = static_cast<std::uint8_t>((index * 37U + 11U) % 251U);
    }
    const auto first = compress_fst_block(
        large, FstCompressionKind::HierarchyGzipStoreV1);
    const auto second = compress_fst_block(
        large, FstCompressionKind::HierarchyGzipStoreV1);
    require(first.bytes == second.bytes && first.byte_digest == second.byte_digest,
        "large stored gzip output must be byte deterministic");
    require(first.byte_digest == "152c9d063df7a3eb38bbaa61db4f0f462f7511762768fecfc3b5704f57423d5f",
        "large stored gzip bytes must retain their platform identity");
    require(first.bytes.size() == 65'564U && first.bytes.at(10U) == 0x00U && first.bytes.at(65'550U) == 0x01U,
        "large stored gzip input must use fixed 65535-byte blocks");

    std::vector<std::uint8_t> runs(8'192U, 0x5aU);
    for (std::size_t index = 0U; index < runs.size(); index += 521U) {
        runs[index] = static_cast<std::uint8_t>(index);
    }
    const auto compressed = compress_fst_block(
        runs, FstCompressionKind::InitialValueZlibFixedV1);
    const auto repeated = compress_fst_block(
        runs, FstCompressionKind::InitialValueZlibFixedV1);
    require(compressed.bytes == repeated.bytes && compressed.bytes.size() < runs.size() && compressed.platform_byte_identical && compressed.variability_reason.empty(),
        "fixed run profile must be compact and platform byte identical");
    require(compressed.byte_digest == "b1e8c31dc637417374e0139b943b68334447d2e9844647932c89a8d3ad4b3cd4",
        "fixed run bytes must retain their platform identity");

    std::vector<std::uint8_t> large_runs(65'536U, 0x44U);
    const auto large_fixed = compress_fst_block(
        large_runs, FstCompressionKind::InitialValueZlibFixedV1);
    const auto repeated_large_fixed = compress_fst_block(
        large_runs, FstCompressionKind::InitialValueZlibFixedV1);
    require(large_fixed.bytes == repeated_large_fixed.bytes && large_fixed.bytes.size() < large_runs.size(),
        "large fixed input must retain deterministic block boundaries");
    require(large_fixed.byte_digest == "e63b080721272d771f4d9ee3c4a8482d5b21c4c4b71ce6d66d2c40ed3681ad5c",
        "large fixed blocks must retain their platform identity");
}

void test_limits_and_unknown_profile()
{
    using namespace fsim::runtime;
    const auto input = bytes("bounded");
    require_rejects(
        [&] {
            static_cast<void>(compress_fst_block(
                input, FstCompressionKind::HierarchyGzipStoreV1,
                { .maximum_input_bytes = 3U, .maximum_output_bytes = 64U }));
        },
        "input limit must reject before compression");
    require_rejects(
        [&] {
            static_cast<void>(compress_fst_block(
                input, FstCompressionKind::HierarchyGzipStoreV1,
                { .maximum_input_bytes = 64U, .maximum_output_bytes = 8U }));
        },
        "output limit must reject without partial output");
    require_rejects(
        [] {
            static_cast<void>(fst_compression_profile(
                static_cast<FstCompressionKind>(255U)));
        },
        "unknown compression profile must reject");
}

} // namespace

int main()
{
    test_profiles();
    test_canonical_empty_and_small();
    test_large_block_and_runs();
    test_limits_and_unknown_profile();
    return 0;
}
