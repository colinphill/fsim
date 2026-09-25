// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_hierarchy_path_codec.hpp"

#include "fsim/support/sha256.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace codec = fsim::app::hierarchy_path_codec;
using fsim::semantic::HierarchyPathId;
using fsim::semantic::HierarchyPathTable;

HierarchyPathTable table(
    const std::initializer_list<std::string_view> spellings)
{
    HierarchyPathTable::Builder builder;
    for (const auto spelling : spellings) {
        (void)builder.intern(spelling);
    }
    return std::move(builder).freeze();
}

void append_u32(std::string& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

std::string wire(
    const std::initializer_list<std::string_view> spellings)
{
    std::string bytes { codec::kMagic };
    append_u32(bytes, codec::kSchema);
    append_u32(bytes, static_cast<std::uint32_t>(spellings.size()));
    for (const auto spelling : spellings) {
        append_u32(bytes, static_cast<std::uint32_t>(spelling.size()));
        bytes.append(spelling);
    }
    return bytes;
}

void expect_error(
    const std::string_view bytes,
    const codec::Error expected,
    const codec::Limits limits = { })
{
    const auto result = codec::decode_hierarchy_paths(bytes, limits);
    assert(!result);
    assert(result.error == expected);
}

void test_canonical_union_and_inline_roundtrip()
{
    const auto runtime_a = table({ "zeta", "alpha", "\xC3\xA9" });
    const auto design_a = table({ "beta", "alpha" });
    const auto runtime_b = table({ "alpha", "\xC3\xA9", "zeta" });
    const auto design_b = table({ "alpha", "beta" });
    const auto first = codec::encode_canonical_hierarchy_paths(
        runtime_a, design_a);
    const auto second = codec::encode_canonical_hierarchy_paths(
        runtime_b, design_b);
    assert(first && second);
    assert(first.payload->bytes == second.payload->bytes);
    assert(first.payload->digest == second.payload->digest);
    assert(first.payload->paths.size() == 4U);
    assert(first.payload->paths.view(HierarchyPathId::from_index(0U))
        == "alpha");
    assert(first.payload->paths.view(HierarchyPathId::from_index(1U))
        == "beta");
    assert(first.payload->paths.view(HierarchyPathId::from_index(2U))
        == "zeta");
    assert(first.payload->paths.view(HierarchyPathId::from_index(3U))
        == "\xC3\xA9");
    assert(first.payload->digest == fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(first.payload->bytes)));

    const auto inline_payload = codec::encode_inline_hierarchy_paths(
        table({ "beta", "", "alpha" }));
    assert(inline_payload);
    const auto decoded = codec::decode_hierarchy_paths(
        inline_payload.payload->bytes);
    assert(decoded);
    assert(decoded.payload->bytes == inline_payload.payload->bytes);
    assert(decoded.payload->digest == inline_payload.payload->digest);
    assert(decoded.payload->paths.view(HierarchyPathId::from_index(0U))
        .empty());
    assert(decoded.payload->paths.view(HierarchyPathId::from_index(1U))
        == "alpha");
    const auto changed = codec::encode_inline_hierarchy_paths(
        table({ "alpha", "gamma" }));
    assert(changed);
    assert(changed.payload->digest != inline_payload.payload->digest);
}

std::string numbered_path(const std::size_t index)
{
    const auto suffix = std::to_string(index);
    return "top.path." + std::string(5U - suffix.size(), '0') + suffix;
}

void test_high_cardinality_canonical_union()
{
    constexpr std::size_t path_count = 50'000U;
    constexpr std::size_t runtime_count = 30'000U;
    constexpr std::size_t design_start = 20'000U;
    HierarchyPathTable::Builder runtime_forward;
    HierarchyPathTable::Builder runtime_reverse;
    HierarchyPathTable::Builder design_forward;
    HierarchyPathTable::Builder design_reverse;
    for (std::size_t index = 0; index < runtime_count; ++index) {
        (void)runtime_forward.intern(numbered_path(index));
        (void)runtime_reverse.intern(
            numbered_path(runtime_count - index - 1U));
        (void)design_forward.intern(numbered_path(design_start + index));
        (void)design_reverse.intern(
            numbered_path(path_count - index - 1U));
    }
    const auto runtime_a = std::move(runtime_forward).freeze();
    const auto runtime_b = std::move(runtime_reverse).freeze();
    const auto design_a = std::move(design_forward).freeze();
    const auto design_b = std::move(design_reverse).freeze();
    const auto first = codec::encode_canonical_hierarchy_paths(
        runtime_a, design_a);
    const auto second = codec::encode_canonical_hierarchy_paths(
        runtime_b, design_b);
    assert(first && second);
    assert(first.payload->paths.size() == path_count);
    assert(first.payload->bytes == second.payload->bytes);
    assert(first.payload->digest == second.payload->digest);

    std::string expected_bytes { codec::kMagic };
    append_u32(expected_bytes, codec::kSchema);
    append_u32(expected_bytes, static_cast<std::uint32_t>(path_count));
    for (std::size_t index = 0; index < path_count; ++index) {
        const auto path = numbered_path(index);
        append_u32(expected_bytes, static_cast<std::uint32_t>(path.size()));
        expected_bytes.append(path);
    }
    assert(first.payload->bytes == expected_bytes);
    const auto expected_digest = fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(expected_bytes));
    assert(first.payload->digest == expected_digest);

    const auto decoded = codec::decode_hierarchy_paths(first.payload->bytes);
    assert(decoded);
    assert(decoded.payload->bytes == expected_bytes);
    assert(decoded.payload->digest == expected_digest);
    assert(decoded.payload->paths.size() == path_count);
    for (std::size_t index = 0; index < path_count; ++index) {
        const auto id = HierarchyPathId::from_index(
            static_cast<std::uint32_t>(index));
        const auto path = numbered_path(index);
        assert(decoded.payload->paths.view(id) == path);
        assert(decoded.payload->paths.find(path) == id);
        assert(codec::decode_path_reference(decoded.payload->paths,
                   static_cast<std::uint32_t>(index)) == id);
    }
    const auto check_source_references = [&](const HierarchyPathTable& source) {
        for (std::size_t index = 0; index < source.size(); ++index) {
            const auto id = HierarchyPathId::from_index(
                static_cast<std::uint32_t>(index));
            const auto remapped = codec::remap_path_reference(
                source, id, decoded.payload->paths);
            assert(remapped);
            assert(decoded.payload->paths.view(*remapped) == source.view(id));
        }
    };
    check_source_references(runtime_a);
    check_source_references(runtime_b);
    check_source_references(design_a);
    check_source_references(design_b);
}

void test_decode_rejections()
{
    const auto canonical = wire({ "alpha", "beta" });
    auto bad_magic = canonical;
    bad_magic[0] = 'X';
    expect_error(bad_magic, codec::Error::invalid_magic);
    auto bad_schema = canonical;
    bad_schema[8] = static_cast<char>(codec::kSchema + 1U);
    expect_error(bad_schema, codec::Error::unsupported_schema);
    expect_error(canonical.substr(0U, 10U), codec::Error::truncated);
    expect_error(canonical.substr(0U, canonical.size() - 1U),
        codec::Error::truncated);
    expect_error(wire({ "alpha", "alpha" }),
        codec::Error::duplicate_path);
    expect_error(wire({ "beta", "alpha" }),
        codec::Error::unsorted_path);
    expect_error(wire({ "\xC0\xAF" }), codec::Error::invalid_utf8);
    expect_error(wire({ std::string_view { "\0", 1U } }),
        codec::Error::embedded_nul);
    expect_error(canonical + "x", codec::Error::trailing_bytes);

    auto invalid_count = canonical;
    for (std::size_t index = 12U; index < 16U; ++index) {
        invalid_count[index] = static_cast<char>(0xffU);
    }
    expect_error(invalid_count, codec::Error::path_count_limit);
    auto invalid_length = canonical;
    for (std::size_t index = 16U; index < 20U; ++index) {
        invalid_length[index] = static_cast<char>(0xffU);
    }
    expect_error(invalid_length, codec::Error::path_length_limit);

    codec::Limits limits;
    limits.maximum_payload_bytes = canonical.size() - 1U;
    expect_error(canonical, codec::Error::payload_limit, limits);
    limits = { };
    limits.maximum_paths = 1U;
    expect_error(canonical, codec::Error::path_count_limit, limits);
    limits = { };
    limits.maximum_path_bytes = 4U;
    expect_error(canonical, codec::Error::path_length_limit, limits);
    limits = { };
    limits.maximum_total_path_bytes = 8U;
    expect_error(canonical, codec::Error::total_path_bytes_limit, limits);
}

void test_reference_rejections()
{
    const auto source = table({ "beta", "alpha" });
    const auto canonical = codec::encode_inline_hierarchy_paths(source);
    assert(canonical);
    const auto alpha = canonical.payload->paths.find("alpha");
    assert(alpha);
    assert(codec::decode_path_reference(canonical.payload->paths, 0U)
        == alpha);
    assert(!codec::decode_path_reference(
        canonical.payload->paths,
        std::numeric_limits<std::uint32_t>::max()));
    assert(!codec::decode_path_reference(canonical.payload->paths, 2U));
    const auto source_alpha = source.find("alpha");
    assert(source_alpha);
    assert(codec::remap_path_reference(source, *source_alpha,
        canonical.payload->paths) == alpha);
    assert(!codec::remap_path_reference(source,
        HierarchyPathId::from_index(2U), canonical.payload->paths));
    assert(!codec::remap_path_reference(source, *source_alpha,
        table({ "beta" })));
}

} // namespace

int main()
{
    test_canonical_union_and_inline_roundtrip();
    test_high_cardinality_canonical_union();
    test_decode_rejections();
    test_reference_rejections();
}
