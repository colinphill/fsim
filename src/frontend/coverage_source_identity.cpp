// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_source_identity.hpp"

#include "fsim/support/path.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace fsim::frontend {
namespace {

    void update_size(support::Sha256& hash, const std::size_t value) noexcept
    {
        const auto encoded = static_cast<std::uint64_t>(value);
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>((bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((encoded >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    support::Sha256::Digest composite_digest(const std::string_view logical_path,
        const std::size_t content_bytes,
        const support::Sha256::Digest& content_digest) noexcept
    {
        support::Sha256 hash;
        update_size(hash, kCodeCoverageSourceIdentitySchema.size());
        hash.update(kCodeCoverageSourceIdentitySchema);
        update_size(hash, logical_path.size());
        hash.update(logical_path);
        update_size(hash, content_bytes);

        std::array<std::byte, 32U> digest_bytes { };
        for (std::size_t index = 0U; index < digest_bytes.size(); ++index) {
            digest_bytes[index] = static_cast<std::byte>(content_digest[index]);
        }
        hash.update(digest_bytes);
        return hash.finish();
    }

    bool contains_parent_component(const std::filesystem::path& path)
    {
        for (const auto& component : path) {
            if (component == "..") {
                return true;
            }
        }
        return false;
    }

    bool valid_utf8(const std::string_view text) noexcept
    {
        const auto continuation = [](const unsigned char byte) {
            return byte >= 0x80U && byte <= 0xbfU;
        };
        for (std::size_t index = 0U; index < text.size();) {
            const auto first = static_cast<unsigned char>(text[index]);
            if (first <= 0x7fU) {
                ++index;
                continue;
            }
            if (first >= 0xc2U && first <= 0xdfU) {
                if (index + 1U >= text.size()
                    || !continuation(
                        static_cast<unsigned char>(text[index + 1U]))) {
                    return false;
                }
                index += 2U;
                continue;
            }
            if (first >= 0xe0U && first <= 0xefU) {
                if (index + 2U >= text.size()) {
                    return false;
                }
                const auto second
                    = static_cast<unsigned char>(text[index + 1U]);
                const auto third
                    = static_cast<unsigned char>(text[index + 2U]);
                if (!continuation(third)
                    || (first == 0xe0U
                        && (second < 0xa0U || second > 0xbfU))
                    || (first == 0xedU
                        && (second < 0x80U || second > 0x9fU))
                    || (first != 0xe0U && first != 0xedU
                        && !continuation(second))) {
                    return false;
                }
                index += 3U;
                continue;
            }
            if (first >= 0xf0U && first <= 0xf4U) {
                if (index + 3U >= text.size()) {
                    return false;
                }
                const auto second
                    = static_cast<unsigned char>(text[index + 1U]);
                const auto third
                    = static_cast<unsigned char>(text[index + 2U]);
                const auto fourth
                    = static_cast<unsigned char>(text[index + 3U]);
                if (!continuation(third) || !continuation(fourth)
                    || (first == 0xf0U
                        && (second < 0x90U || second > 0xbfU))
                    || (first == 0xf4U
                        && (second < 0x80U || second > 0x8fU))
                    || (first != 0xf0U && first != 0xf4U
                        && !continuation(second))) {
                    return false;
                }
                index += 4U;
                continue;
            }
            return false;
        }
        return true;
    }

} // namespace

CodeCoverageSourceIdentityResult make_code_coverage_source_identity(
    const std::filesystem::path& checkout_root,
    const std::filesystem::path& source_path,
    const std::span<const std::byte> contents,
    const CodeCoverageSourceIdentityLimits limits) noexcept
{
    if (checkout_root.empty()) {
        return { { }, CodeCoverageSourceIdentityError::CheckoutRootRequired };
    }
    if (source_path.empty()) {
        return { { }, CodeCoverageSourceIdentityError::SourcePathRequired };
    }
    if (contents.size() > limits.maximum_content_bytes) {
        return { { }, CodeCoverageSourceIdentityError::ContentLimit };
    }

    try {
        const auto root = checkout_root.lexically_normal();
        const auto source = (source_path.is_absolute()
                ? source_path
                : root / source_path)
                                .lexically_normal();
        if (root.is_absolute() != source.is_absolute()
            || root.root_name() != source.root_name()) {
            return { { }, CodeCoverageSourceIdentityError::SourceOutsideCheckout };
        }

        const auto relative = source.lexically_relative(root).lexically_normal();
        if (relative.empty() || relative == "." || relative.is_absolute()
            || relative.has_root_name() || contains_parent_component(relative)) {
            return { { }, CodeCoverageSourceIdentityError::SourceOutsideCheckout };
        }

        auto logical_path = support::path_to_utf8(relative);
        if (logical_path.size() > limits.maximum_logical_path_bytes) {
            return { { }, CodeCoverageSourceIdentityError::PathLimit };
        }
        if (logical_path.empty() || logical_path.find('\0') != std::string::npos
            || !valid_utf8(logical_path)) {
            return { { }, CodeCoverageSourceIdentityError::InvalidPathEncoding };
        }

        auto content_digest = support::Sha256::digest(contents);
        auto digest
            = composite_digest(logical_path, contents.size(), content_digest);
        return { CodeCoverageSourceIdentity { std::move(logical_path),
                     contents.size(), std::move(content_digest),
                     std::move(digest) },
            CodeCoverageSourceIdentityError::None };
    } catch (...) {
        return { { }, CodeCoverageSourceIdentityError::InvalidPathEncoding };
    }
}

std::string code_coverage_source_content_hex(
    const CodeCoverageSourceIdentity& identity)
{
    return support::Sha256::hex(identity.content_digest);
}

std::string code_coverage_source_identity_hex(
    const CodeCoverageSourceIdentity& identity)
{
    return support::Sha256::hex(identity.digest);
}

bool is_code_coverage_source_identity_valid(
    const CodeCoverageSourceIdentity& identity) noexcept
{
    if (identity.logical_path.empty()
        || identity.logical_path.size()
            > CodeCoverageSourceIdentityLimits { }.maximum_logical_path_bytes
        || identity.content_bytes
            > CodeCoverageSourceIdentityLimits { }.maximum_content_bytes
        || identity.logical_path.find('\0') != std::string::npos
        || !valid_utf8(identity.logical_path)) {
        return false;
    }
    try {
        const auto path = support::path_from_utf8(identity.logical_path);
        if (path.is_absolute() || path.has_root_name()
            || path.lexically_normal() != path || path == "."
            || contains_parent_component(path)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    return identity.digest == composite_digest(identity.logical_path, identity.content_bytes, identity.content_digest);
}

} // namespace fsim::frontend
