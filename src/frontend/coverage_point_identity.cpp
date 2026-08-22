// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_point_identity.hpp"

#include "fsim/support/sha256.hpp"

#include <array>
#include <cstddef>

namespace fsim::frontend {
namespace {

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>((bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    void update_digest(support::Sha256& hash,
        const support::Sha256::Digest& digest) noexcept
    {
        std::array<std::byte, 32U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(digest[index]);
        }
        hash.update(bytes);
    }

    std::uint64_t digest_word(
        const support::Sha256::Digest& digest, const std::size_t first) noexcept
    {
        std::uint64_t value { };
        for (std::size_t index = first; index < first + 8U; ++index) {
            value = (value << 8U) | digest[index];
        }
        return value;
    }

} // namespace

CodeCoveragePointIdentityResult make_code_coverage_point_identity(
    const CodeCoverageSourceIdentity& source,
    const CodeCoverageLanguage language,
    const CodeCoverageConstructKind construct,
    const CodeCoverageSourceSpan span) noexcept
{
    if (!is_code_coverage_source_identity_valid(source)) {
        return { { }, CodeCoveragePointIdentityError::InvalidSourceIdentity };
    }
    if (code_coverage_language_name(language).empty()) {
        return { { }, CodeCoveragePointIdentityError::InvalidLanguage };
    }
    if (code_coverage_construct_kind_name(construct).empty()) {
        return { { }, CodeCoveragePointIdentityError::InvalidConstructKind };
    }
    if (span.end_offset < span.begin_offset) {
        return { { }, CodeCoveragePointIdentityError::ReversedSpan };
    }
    if (span.end_offset == span.begin_offset) {
        return { { }, CodeCoveragePointIdentityError::EmptySpan };
    }
    if (span.end_offset > source.content_bytes) {
        return { { }, CodeCoveragePointIdentityError::SpanOutsideSource };
    }

    support::Sha256 hash;
    update_u64(hash, kCodeCoveragePointIdentitySchema.size());
    hash.update(kCodeCoveragePointIdentitySchema);
    update_digest(hash, source.digest);
    update_u64(hash, static_cast<std::uint8_t>(language));
    update_u64(hash, static_cast<std::uint8_t>(construct));
    update_u64(hash, span.begin_offset);
    update_u64(hash, span.end_offset);
    const auto digest = hash.finish();
    const runtime::CodeCoveragePointId identity {
        digest_word(digest, 0U),
        digest_word(digest, 8U),
    };
    if (!runtime::is_code_coverage_identity_valid(identity)) {
        return { { }, CodeCoveragePointIdentityError::ZeroIdentity };
    }
    return { identity, CodeCoveragePointIdentityError::None };
}

std::string code_coverage_point_identity_hex(
    const runtime::CodeCoveragePointId identity)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(32U, '0');
    const std::array<std::uint64_t, 2U> words { identity.high, identity.low };
    std::size_t output = 0U;
    for (const auto word : words) {
        for (std::size_t nibble = 0U; nibble < 16U; ++nibble) {
            const auto shift = static_cast<unsigned>((15U - nibble) * 4U);
            result[output++] = digits[(word >> shift) & 0xfU];
        }
    }
    return result;
}

} // namespace fsim::frontend
