// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_fsm_validation.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageFsmDescriptionValidationError;
    using DiagnosticKey = std::tuple<std::uint8_t, std::uint8_t, std::uint64_t,
        std::uint64_t, std::string, std::string>;

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.empty() || text.find('\0') != std::string_view::npos;
    }

    bool valid_kind(const CoverageFsmDescriptionIssueKind kind) noexcept
    {
        switch (kind) {
        case CoverageFsmDescriptionIssueKind::Ambiguous:
        case CoverageFsmDescriptionIssueKind::Incomplete:
        case CoverageFsmDescriptionIssueKind::Conflicting:
            return true;
        }
        return false;
    }

    bool valid_subject(const CoverageFsmDescriptionSubject subject) noexcept
    {
        switch (subject) {
        case CoverageFsmDescriptionSubject::CurrentState:
        case CoverageFsmDescriptionSubject::NextState:
        case CoverageFsmDescriptionSubject::LegalStates:
            return true;
        }
        return false;
    }

    bool valid_origin(const CoverageFsmDescriptionOrigin origin) noexcept
    {
        switch (origin) {
        case CoverageFsmDescriptionOrigin::Enum:
        case CoverageFsmDescriptionOrigin::Case:
        case CoverageFsmDescriptionOrigin::Assignment:
        case CoverageFsmDescriptionOrigin::SystemVerilogPragma:
        case CoverageFsmDescriptionOrigin::VhdlSource:
        case CoverageFsmDescriptionOrigin::Manifest:
            return true;
        }
        return false;
    }

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>(
                (bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    void update_string(support::Sha256& hash, const std::string_view value) noexcept
    {
        update_u64(hash, value.size());
        hash.update(value);
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

    runtime::CodeCoveragePointId diagnostic_identity(
        const CoverageFsmDescriptionDiagnostic& diagnostic) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageFsmValidationSchema);
        update_u64(hash, static_cast<std::uint8_t>(diagnostic.kind));
        update_u64(hash, static_cast<std::uint8_t>(diagnostic.subject));
        update_u64(hash, diagnostic.instance_identity.high);
        update_u64(hash, diagnostic.instance_identity.low);
        update_string(hash, diagnostic.instance);
        update_string(hash, diagnostic.object);
        update_u64(hash, diagnostic.origins.size());
        for (const auto origin : diagnostic.origins) {
            update_u64(hash, static_cast<std::uint8_t>(origin));
        }
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    DiagnosticKey key(const CoverageFsmDescriptionIssueCandidate& candidate)
    {
        return { static_cast<std::uint8_t>(candidate.kind),
            static_cast<std::uint8_t>(candidate.subject),
            candidate.instance_identity.high, candidate.instance_identity.low,
            candidate.instance, candidate.object };
    }

} // namespace

CoverageFsmDescriptionValidationResult
make_coverage_fsm_description_diagnostics(
    const std::span<const CoverageFsmDescriptionIssueCandidate> candidates,
    const CoverageFsmDescriptionValidationLimits limits) noexcept
{
    CoverageFsmDescriptionValidationResult result;
    const auto reject = [&](const Error error,
                            const std::size_t index = 0U) {
        result.diagnostics.reset();
        result.error = error;
        result.candidate_index = index;
        return result;
    };
    try {
        if (candidates.size() > limits.maximum_candidates) {
            return reject(Error::ResourceLimit);
        }
        std::map<DiagnosticKey, CoverageFsmDescriptionDiagnostic> merged;
        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            if (!valid_kind(candidate.kind)) {
                return reject(Error::InvalidKind, index);
            }
            if (!valid_subject(candidate.subject)) {
                return reject(Error::InvalidSubject, index);
            }
            if (!is_coverage_instance_identity_valid(
                    candidate.instance_identity)) {
                return reject(Error::InvalidInstanceIdentity, index);
            }
            if (invalid_text(candidate.instance)) {
                return reject(Error::InvalidInstance, index);
            }
            if (invalid_text(candidate.object)) {
                return reject(Error::InvalidObject, index);
            }
            if (candidate.instance.size() > limits.maximum_instance_bytes
                || candidate.object.size() > limits.maximum_object_bytes
                || candidate.origins.size() > limits.maximum_origins) {
                return reject(Error::ResourceLimit, index);
            }
            if (candidate.origins.empty()) {
                return reject(Error::MissingOrigin, index);
            }
            for (const auto origin : candidate.origins) {
                if (!valid_origin(origin)) {
                    return reject(Error::InvalidOrigin, index);
                }
            }
            const auto candidate_key = key(candidate);
            auto [entry, inserted] = merged.try_emplace(candidate_key);
            if (inserted) {
                entry->second.kind = candidate.kind;
                entry->second.subject = candidate.subject;
                entry->second.instance_identity = candidate.instance_identity;
                entry->second.instance = candidate.instance;
                entry->second.object = candidate.object;
            }
            for (const auto origin : candidate.origins) {
                if (std::ranges::find(entry->second.origins, origin)
                    != entry->second.origins.end()) {
                    continue;
                }
                if (entry->second.origins.size() >= limits.maximum_origins) {
                    return reject(Error::ResourceLimit, index);
                }
                entry->second.origins.push_back(origin);
            }
        }
        if (merged.size() > limits.maximum_diagnostics) {
            return reject(Error::ResourceLimit);
        }

        std::vector<CoverageFsmDescriptionDiagnostic> diagnostics;
        diagnostics.reserve(merged.size());
        std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
        for (auto& [candidate_key, diagnostic] : merged) {
            static_cast<void>(candidate_key);
            std::ranges::sort(diagnostic.origins, { }, [](const auto origin) {
                return static_cast<std::uint8_t>(origin);
            });
            diagnostic.origins.erase(std::unique(diagnostic.origins.begin(),
                                         diagnostic.origins.end()),
                diagnostic.origins.end());
            diagnostic.id = diagnostic_identity(diagnostic);
            if (!runtime::is_code_coverage_identity_valid(diagnostic.id)
                || !identities.emplace(
                                  diagnostic.id.high, diagnostic.id.low)
                    .second) {
                return reject(Error::DuplicateDiagnosticIdentity);
            }
            diagnostics.push_back(std::move(diagnostic));
        }
        result.diagnostics = std::move(diagnostics);
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit);
    }
}

} // namespace fsim::elaboration
