// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_point_identity.hpp"

#include "fsim/support/identity128.hpp"
#include "fsim/support/sha256.hpp"

#include <array>
#include <cstddef>

namespace fsim::frontend {
namespace {

    void update_digest(support::Sha256& hash,
        const support::Sha256::Digest& digest) noexcept
    {
        std::array<std::byte, 32U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(digest[index]);
        }
        hash.update(bytes);
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
    support::sha256_update_u64_be(hash, kCodeCoveragePointIdentitySchema.size());
    hash.update(kCodeCoveragePointIdentitySchema);
    update_digest(hash, source.digest);
    support::sha256_update_u64_be(hash, static_cast<std::uint8_t>(language));
    support::sha256_update_u64_be(hash, static_cast<std::uint8_t>(construct));
    support::sha256_update_u64_be(hash, span.begin_offset);
    support::sha256_update_u64_be(hash, span.end_offset);
    const auto digest = hash.finish();
    const runtime::CodeCoveragePointId identity {
        support::sha256_digest_word_be(digest, 0U),
        support::sha256_digest_word_be(digest, 8U),
    };
    if (!support::identity128_nonzero(identity)) {
        return { { }, CodeCoveragePointIdentityError::ZeroIdentity };
    }
    return { identity, CodeCoveragePointIdentityError::None };
}

std::string code_coverage_point_identity_hex(
    const runtime::CodeCoveragePointId identity)
{
    return support::identity128_hex(identity);
}

} // namespace fsim::frontend
