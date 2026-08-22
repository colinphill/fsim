// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_identity.hpp"

#include "fsim/support/sha256.hpp"

#include <array>
#include <cstddef>
#include <new>
#include <utility>

namespace fsim::artifact {
namespace {

void update_u32(support::Sha256& hash, const std::uint32_t value) noexcept
{
    const std::array bytes {
        static_cast<std::byte>((value >> 24U) & 0xffU),
        static_cast<std::byte>((value >> 16U) & 0xffU),
        static_cast<std::byte>((value >> 8U) & 0xffU),
        static_cast<std::byte>(value & 0xffU),
    };
    hash.update(bytes);
}

std::string identity_digest(
    const std::uint32_t schema,
    const bool enabled,
    const std::string_view model)
{
    support::Sha256 hash;
    hash.update("fsim-code-coverage-artifact-identity-v3");
    update_u32(hash, schema);
    const std::array enabled_byte {
        static_cast<std::byte>(enabled ? 1U : 0U),
    };
    hash.update(enabled_byte);
    update_u32(hash, static_cast<std::uint32_t>(model.size()));
    hash.update(model);
    return support::Sha256::hex(hash.finish());
}

} // namespace

CodeCoverageArtifactIdentityResult
make_code_coverage_artifact_identity(const bool enabled) noexcept
{
    try {
        CodeCoverageArtifactIdentity identity;
        identity.enabled = enabled;
        identity.model = enabled ? kCodeCoverageBroadMetricsModel
                                 : kCodeCoverageDisabledModel;
        identity.digest = identity_digest(
            identity.schema, identity.enabled, identity.model);
        return { std::move(identity),
            CodeCoverageArtifactIdentityError::None };
    } catch (const std::bad_alloc&) {
        return { { }, CodeCoverageArtifactIdentityError::AllocationFailure };
    }
}

CodeCoverageArtifactIdentityError validate_code_coverage_artifact_identity(
    const CodeCoverageArtifactIdentity& identity) noexcept
{
    if (identity.schema != kCodeCoverageArtifactSchema) {
        return CodeCoverageArtifactIdentityError::SchemaMismatch;
    }
    const auto expected_model = identity.enabled
        ? kCodeCoverageBroadMetricsModel
        : kCodeCoverageDisabledModel;
    if (identity.model != expected_model) {
        return CodeCoverageArtifactIdentityError::ModelMismatch;
    }
    try {
        if (identity.digest != identity_digest(
                identity.schema, identity.enabled, identity.model)) {
            return CodeCoverageArtifactIdentityError::DigestMismatch;
        }
    } catch (const std::bad_alloc&) {
        return CodeCoverageArtifactIdentityError::AllocationFailure;
    }
    return CodeCoverageArtifactIdentityError::None;
}

} // namespace fsim::artifact
