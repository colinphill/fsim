// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_workspace_store.hpp"

#include "fsim/compiler/cache_support.hpp"

#include <map>
#include <set>

namespace fsim::app::workspace::detail {

using LibraryMappings = std::map<std::string, std::filesystem::path>;

constexpr std::size_t kMaximumRecords = 1000000U;
constexpr std::size_t kMaximumTextBytes = 1024U * 1024U;

[[nodiscard]] bool ensure_directory(
    const std::filesystem::path&, std::string& error);
[[nodiscard]] bool safe_descendant(const std::filesystem::path& root,
    const std::filesystem::path& relative, std::string& error);
[[nodiscard]] bool managed_artifact_path(const ArtifactRecord&);
[[nodiscard]] bool valid_record(const ArtifactRecord&, std::string& error);
[[nodiscard]] std::string new_revision();
void remove_revision(const std::filesystem::path&) noexcept;
void collect_revisions(const std::filesystem::path& root,
    const std::set<std::string>& retained) noexcept;
[[nodiscard]] bool replace_text(const std::filesystem::path& destination,
    std::string_view text, std::string& error);
[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path&, std::size_t limit, std::string& error);
[[nodiscard]] std::optional<LibraryMappings> read_mappings(
    const Store&, std::string& error);
[[nodiscard]] bool write_mappings(
    const Store&, const LibraryMappings&, std::string& error);
[[nodiscard]] std::optional<LibraryLocation> locate_library(
    const Store&, std::string_view name, bool create, std::string& error);
[[nodiscard]] std::unique_ptr<compiler::detail::CacheDirectoryLock> lock_directory(
    const std::filesystem::path&, std::string& error);
[[nodiscard]] std::optional<LibraryCatalog> load_catalog(
    const LibraryLocation&, bool create, std::string& error);
[[nodiscard]] bool save_catalog(const LibraryCatalog&, std::string& error);
[[nodiscard]] bool merge_records(LibraryCatalog&,
    const std::vector<ArtifactRecord>& replacements,
    std::span<const std::string> replaced_sources, std::string& error);

} // namespace fsim::app::workspace::detail
