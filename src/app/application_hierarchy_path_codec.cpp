// SPDX-License-Identifier: Apache-2.0
#include "application_hierarchy_path_codec.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::app::hierarchy_path_codec {
namespace {

constexpr std::size_t kHeaderBytes = 8U + 4U + 4U;
constexpr std::uint32_t kInvalidId
    = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool unsigned_byte_less(
    const std::string_view left,
    const std::string_view right) noexcept
{
    const auto common = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < common; ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (a != b) {
            return a < b;
        }
    }
    return left.size() < right.size();
}

[[nodiscard]] Error validate_path(const std::string_view path) noexcept
{
    const auto continuation = [](const unsigned char byte) {
        return byte >= 0x80U && byte <= 0xbfU;
    };
    for (std::size_t index = 0; index < path.size();) {
        const auto first = static_cast<unsigned char>(path[index]);
        if (first == 0U) {
            return Error::embedded_nul;
        }
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= path.size()
                || !continuation(
                    static_cast<unsigned char>(path[index + 1U]))) {
                return Error::invalid_utf8;
            }
            index += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
            if (index + 2U >= path.size()) {
                return Error::invalid_utf8;
            }
            const auto second = static_cast<unsigned char>(path[index + 1U]);
            const auto third = static_cast<unsigned char>(path[index + 2U]);
            if (!continuation(third)
                || (first == 0xe0U && (second < 0xa0U || second > 0xbfU))
                || (first == 0xedU && (second < 0x80U || second > 0x9fU))
                || (first != 0xe0U && first != 0xedU
                    && !continuation(second))) {
                return Error::invalid_utf8;
            }
            index += 3U;
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
            if (index + 3U >= path.size()) {
                return Error::invalid_utf8;
            }
            const auto second = static_cast<unsigned char>(path[index + 1U]);
            const auto third = static_cast<unsigned char>(path[index + 2U]);
            const auto fourth = static_cast<unsigned char>(path[index + 3U]);
            if (!continuation(third) || !continuation(fourth)
                || (first == 0xf0U && (second < 0x90U || second > 0xbfU))
                || (first == 0xf4U && (second < 0x80U || second > 0x8fU))
                || (first != 0xf0U && first != 0xf4U
                    && !continuation(second))) {
                return Error::invalid_utf8;
            }
            index += 4U;
            continue;
        }
        return Error::invalid_utf8;
    }
    return Error::none;
}

void append_u32(std::string& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

[[nodiscard]] bool read_u32(
    const std::string_view bytes,
    std::size_t& position,
    std::uint32_t& value) noexcept
{
    if (position > bytes.size() || bytes.size() - position < 4U) {
        return false;
    }
    value = 0U;
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        value |= static_cast<std::uint32_t>(
            static_cast<unsigned char>(bytes[position++])) << shift;
    }
    return true;
}

[[nodiscard]] bool add_within_limit(
    const std::size_t current,
    const std::size_t amount,
    const std::size_t limit) noexcept
{
    return current <= limit && amount <= limit - current;
}

[[nodiscard]] Result make_payload(
    const std::vector<std::string_view>& paths,
    std::string bytes)
{
    semantic::HierarchyPathTable::Builder builder;
    for (const auto path : paths) {
        (void)builder.intern(path);
    }
    Payload payload {
        std::move(builder).freeze(), std::move(bytes), { }
    };
    payload.digest = support::Sha256::hex(
        support::Sha256::digest(payload.bytes));
    return { std::move(payload), Error::none };
}

[[nodiscard]] Result encode_tables(
    const semantic::HierarchyPathTable& first,
    const semantic::HierarchyPathTable* const second,
    const Limits limits)
{
    try {
        if (first.size() >= kInvalidId
            || (second != nullptr && second->size() >= kInvalidId)) {
            return { std::nullopt, Error::path_count_limit };
        }
        std::vector<std::string_view> paths;
        const auto collect = [&](const semantic::HierarchyPathTable& table) {
            for (std::size_t index = 0; index < table.size(); ++index) {
                const auto id = semantic::HierarchyPathId::from_index(
                    static_cast<std::uint32_t>(index));
                paths.push_back(table.view(id));
            }
        };
        collect(first);
        if (second != nullptr) {
            collect(*second);
        }
        std::ranges::sort(paths, unsigned_byte_less);
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        if (paths.size() > limits.maximum_paths
            || paths.size() >= kInvalidId) {
            return { std::nullopt, Error::path_count_limit };
        }
        std::size_t total_path_bytes { };
        std::size_t payload_bytes = kHeaderBytes;
        for (const auto path : paths) {
            if (path.size() > limits.maximum_path_bytes
                || path.size() > kInvalidId) {
                return { std::nullopt, Error::path_length_limit };
            }
            if (const auto error = validate_path(path); error != Error::none) {
                return { std::nullopt, error };
            }
            if (!add_within_limit(total_path_bytes, path.size(),
                    limits.maximum_total_path_bytes)) {
                return { std::nullopt, Error::total_path_bytes_limit };
            }
            total_path_bytes += path.size();
            if (!add_within_limit(payload_bytes, 4U,
                    limits.maximum_payload_bytes)
                || !add_within_limit(payload_bytes + 4U, path.size(),
                    limits.maximum_payload_bytes)) {
                return { std::nullopt, Error::payload_limit };
            }
            payload_bytes += 4U;
            payload_bytes += path.size();
        }
        if (payload_bytes > limits.maximum_payload_bytes) {
            return { std::nullopt, Error::payload_limit };
        }
        std::string bytes;
        bytes.reserve(payload_bytes);
        bytes.append(kMagic);
        append_u32(bytes, kSchema);
        append_u32(bytes, static_cast<std::uint32_t>(paths.size()));
        for (const auto path : paths) {
            append_u32(bytes, static_cast<std::uint32_t>(path.size()));
            bytes.append(path);
        }
        return make_payload(paths, std::move(bytes));
    } catch (const std::bad_alloc&) {
        return { std::nullopt, Error::allocation_failure };
    } catch (const std::length_error&) {
        return { std::nullopt, Error::allocation_failure };
    }
}

} // namespace

Result encode_canonical_hierarchy_paths(
    const semantic::HierarchyPathTable& runtime_paths,
    const semantic::HierarchyPathTable& design_ir_paths,
    const Limits limits)
{
    return encode_tables(runtime_paths, &design_ir_paths, limits);
}

Result encode_inline_hierarchy_paths(
    const semantic::HierarchyPathTable& paths,
    const Limits limits)
{
    return encode_tables(paths, nullptr, limits);
}

Result decode_hierarchy_paths(
    const std::string_view bytes,
    const Limits limits)
{
    if (bytes.size() > limits.maximum_payload_bytes) {
        return { std::nullopt, Error::payload_limit };
    }
    if (bytes.size() < kHeaderBytes) {
        return { std::nullopt, Error::truncated };
    }
    if (!bytes.starts_with(kMagic)) {
        return { std::nullopt, Error::invalid_magic };
    }
    std::size_t position = kMagic.size();
    std::uint32_t schema { };
    std::uint32_t count { };
    if (!read_u32(bytes, position, schema)
        || !read_u32(bytes, position, count)) {
        return { std::nullopt, Error::truncated };
    }
    if (schema != kSchema) {
        return { std::nullopt, Error::unsupported_schema };
    }
    if (count > limits.maximum_paths || count >= kInvalidId) {
        return { std::nullopt, Error::path_count_limit };
    }
    if (count > (bytes.size() - position) / 4U) {
        return { std::nullopt, Error::truncated };
    }
    try {
        std::vector<std::string_view> paths;
        paths.reserve(count);
        std::size_t total_path_bytes { };
        for (std::uint32_t index = 0U; index < count; ++index) {
            std::uint32_t length { };
            if (!read_u32(bytes, position, length)) {
                return { std::nullopt, Error::truncated };
            }
            if (length > limits.maximum_path_bytes) {
                return { std::nullopt, Error::path_length_limit };
            }
            if (!add_within_limit(total_path_bytes, length,
                    limits.maximum_total_path_bytes)) {
                return { std::nullopt, Error::total_path_bytes_limit };
            }
            if (length > bytes.size() - position) {
                return { std::nullopt, Error::truncated };
            }
            const auto path = bytes.substr(position, length);
            if (const auto error = validate_path(path); error != Error::none) {
                return { std::nullopt, error };
            }
            if (!paths.empty() && !unsigned_byte_less(paths.back(), path)) {
                return { std::nullopt,
                    paths.back() == path
                        ? Error::duplicate_path : Error::unsorted_path };
            }
            paths.push_back(path);
            total_path_bytes += length;
            position += length;
        }
        if (position != bytes.size()) {
            return { std::nullopt, Error::trailing_bytes };
        }
        return make_payload(paths, std::string { bytes });
    } catch (const std::bad_alloc&) {
        return { std::nullopt, Error::allocation_failure };
    } catch (const std::length_error&) {
        return { std::nullopt, Error::allocation_failure };
    }
}

std::optional<semantic::HierarchyPathId> decode_path_reference(
    const semantic::HierarchyPathTable& paths,
    const std::uint32_t raw_id) noexcept
{
    if (raw_id == kInvalidId || raw_id >= paths.size()) {
        return std::nullopt;
    }
    return semantic::HierarchyPathId::from_index(raw_id);
}

std::optional<semantic::HierarchyPathId> remap_path_reference(
    const semantic::HierarchyPathTable& source,
    const semantic::HierarchyPathId source_id,
    const semantic::HierarchyPathTable& canonical) noexcept
{
    if (!source_id.valid() || source_id.value() >= source.size()) {
        return std::nullopt;
    }
    return canonical.find(source.view(source_id));
}

} // namespace fsim::app::hierarchy_path_codec
