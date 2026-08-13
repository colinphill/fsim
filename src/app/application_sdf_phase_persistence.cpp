// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_phase_persistence.hpp"

#include "fsim/support/sha256.hpp"

#include <fstream>
#include <ranges>

namespace fsim::app {

SdfPhaseArtifact::SdfPhaseArtifact(
    std::shared_ptr<const SdfPortableArchiveSnapshot> snapshot,
    std::vector<std::byte> bytes, const bool cache_identity_applied)
    : snapshot_(std::move(snapshot))
    , bytes_(std::move(bytes))
    , cache_identity_applied_(cache_identity_applied)
{
}

const SdfPortableArchiveSnapshot& SdfPhaseArtifact::snapshot() const noexcept
{
    return *snapshot_;
}

std::span<const std::byte> SdfPhaseArtifact::bytes() const noexcept
{
    return bytes_;
}

bool SdfPhaseArtifact::cache_identity_applied() const noexcept
{
    return cache_identity_applied_;
}

bool install_sdf_phase_artifact(BuiltProject& project,
    const std::span<const std::byte> bytes,
    const artifact::DesignSdfAnnotation& expected_annotation,
    diagnostic::Engine& diagnostics,
    const SdfPortableArchiveLimits limits)
{
    if (std::ranges::any_of(project.sdf_phase_artifacts, [&](const auto& item) {
            return item->snapshot().annotation().cache_key
                == expected_annotation.cache_key;
        })) {
        diagnostics.error("FSIM-SDF-PORTABLE-003",
            "non-project phase state contains a duplicate SDF annotation");
        return false;
    }
    auto decoded = decode_sdf_portable_archive(
        bytes, expected_annotation, limits);
    if (!decoded.ok()) {
        for (const auto& diagnostic : decoded.diagnostics)
            diagnostics.error(diagnostic.code, diagnostic.message);
        return false;
    }
    project.sdf_phase_artifacts.push_back(
        std::make_shared<const SdfPhaseArtifact>(
            std::move(decoded.snapshot),
            std::vector<std::byte> { bytes.begin(), bytes.end() }));
    return true;
}

std::span<const std::shared_ptr<const SdfPhaseArtifact>> sdf_phase_artifacts(
    const BuiltProject& project) noexcept
{
    return project.sdf_phase_artifacts;
}

bool append_sdf_phase_payloads(const BuiltProject& project,
    artifact::DesignMetadata& metadata,
    std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics)
{
    for (const auto& phase : project.sdf_phase_artifacts) {
        const auto& annotation = phase->snapshot().annotation();
        if (phase->cache_identity_applied()) {
            metadata.sdf_annotations.push_back(annotation);
        } else {
            auto applied = apply_sdf_artifact_identity(metadata, annotation);
            if (!applied.ok()) {
                for (const auto& diagnostic : applied.diagnostics)
                    diagnostics.error(diagnostic.code, diagnostic.message);
                return false;
            }
            metadata = std::move(*applied.metadata);
        }
        const auto index = make_sdf_design_payload(annotation,
            std::filesystem::path { "sdf" } / (annotation.cache_key + ".bin"),
            phase->bytes());
        metadata.payloads.push_back(index);
        payloads.push_back({ index.artifact,
            std::string { reinterpret_cast<const char*>(phase->bytes().data()),
                phase->bytes().size() } });
    }
    std::ranges::sort(metadata.sdf_annotations, { },
        &artifact::DesignSdfAnnotation::cache_key);
    return true;
}

bool restore_sdf_phase_artifacts(const std::filesystem::path& directory,
    const artifact::DesignMetadata& metadata, BuiltProject& project,
    diagnostic::Engine& diagnostics, const SdfPortableArchiveLimits limits)
{
    for (const auto& annotation : metadata.sdf_annotations) {
        const auto kind = "sdf:" + annotation.cache_key;
        const auto index = std::ranges::find_if(metadata.payloads,
            [&](const auto& payload) { return payload.kind == kind; });
        if (index == metadata.payloads.end()) {
            diagnostics.error("FSIM-SDF-PORTABLE-003",
                ".fsimdesign is missing its indexed SDF phase payload");
            return false;
        }
        std::ifstream input(directory / index->artifact, std::ios::binary);
        std::vector<std::byte> bytes;
        char value { };
        while (input.get(value)) {
            if (bytes.size() >= limits.max_bytes) {
                diagnostics.error("FSIM-SDF-PORTABLE-004",
                    ".fsimdesign SDF phase payload exceeds its byte limit");
                return false;
            }
            bytes.push_back(std::byte { static_cast<unsigned char>(value) });
        }
        const auto actual_checksum = support::Sha256::hex(
            support::Sha256::digest(std::span { bytes }));
        if (!input.eof() || actual_checksum != index->checksum) {
            diagnostics.error("FSIM-SDF-PORTABLE-002",
                ".fsimdesign SDF phase payload is unreadable or corrupt");
            return false;
        }
        auto decoded = decode_sdf_portable_archive(bytes, annotation, limits);
        if (!decoded.ok()) {
            for (const auto& diagnostic : decoded.diagnostics)
                diagnostics.error(diagnostic.code, diagnostic.message);
            return false;
        }
        project.sdf_phase_artifacts.push_back(
            std::make_shared<const SdfPhaseArtifact>(
                std::move(decoded.snapshot), std::move(bytes), true));
    }
    return true;
}

} // namespace fsim::app
