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
[[nodiscard]] std::string_view artifact_extension(ArtifactKind);
// The file name stem a committed artifact is given before collisions.
[[nodiscard]] std::string artifact_name(const ArtifactRecord&);
[[nodiscard]] bool valid_artifact_id(std::string_view);
[[nodiscard]] bool managed_artifact_path(const ArtifactRecord&);
// Removes the library's artifact files, and the pending or temporary files
// of interrupted writers, that `retained` does not name. Callers hold the
// library's writer lock.
void collect_artifacts(const std::filesystem::path& library,
    const std::set<std::string>& retained) noexcept;
[[nodiscard]] bool valid_record(const ArtifactRecord&, std::string& error);
[[nodiscard]] std::string new_revision();
// How to delete state an older fsim left read-only at `path`, in this
// platform's commands. fsim never deletes it itself.
[[nodiscard]] std::string legacy_removal_advice(const std::filesystem::path& path);
void remove_artifact(const std::filesystem::path&) noexcept;
[[nodiscard]] bool replace_text(const std::filesystem::path& destination,
    std::string_view text, std::string& error);
[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path&, std::size_t limit, std::string& error);
// .fsim/libraries.toml: [libraries] maps names to library directories and
// [exported] maps names to exported .fsimlib files.
struct MappingFile {
    LibraryMappings libraries;
    LibraryMappings exported;
};
[[nodiscard]] std::optional<MappingFile> read_mapping_file(
    const Store&, std::string& error);
[[nodiscard]] bool write_mapping_file(
    const Store&, const MappingFile&, std::string& error);
// The directory mappings alone; writing them keeps the exported ones.
[[nodiscard]] std::optional<LibraryMappings> read_mappings(
    const Store&, std::string& error);
[[nodiscard]] bool write_mappings(
    const Store&, const LibraryMappings&, std::string& error);
[[nodiscard]] std::optional<LibraryLocation> locate_library(
    const Store&, std::string_view name, bool create, std::string& error);
[[nodiscard]] std::unique_ptr<compiler::detail::CacheDirectoryLock> lock_directory(
    const std::filesystem::path&, std::string& error);

enum class CatalogAccess {
    read_only,
    // Writer entrypoints hold this library's lock and may recover a hot journal.
    writer_lock_held,
};

[[nodiscard]] std::optional<LibraryCatalog> load_catalog(
    const LibraryLocation&, bool create, CatalogAccess, std::string& error);
[[nodiscard]] bool save_catalog(const LibraryCatalog&, std::string& error);
[[nodiscard]] bool merge_records(LibraryCatalog&,
    const std::vector<ArtifactRecord>& replacements,
    std::span<const std::string> replaced_sources, std::string& error);

} // namespace fsim::app::workspace::detail
