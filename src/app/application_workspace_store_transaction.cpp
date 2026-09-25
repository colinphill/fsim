// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/support/path.hpp"

#include <algorithm>

namespace fsim::app::workspace {
namespace detail {

bool merge_records(LibraryCatalog& catalog,
    const std::vector<ArtifactRecord>& replacements,
    const std::span<const std::string> replaced_sources, std::string& error)
{
    std::set<std::string> sources { replaced_sources.begin(), replaced_sources.end() };
    bool native_changed = false;
    bool plugin_changed = false;
    for (const auto& record : replacements) {
        if (!valid_record(record, error))
            return false;
        if (record.kind != ArtifactKind::SystemCPlugin)
            sources.insert(record.sources.begin(), record.sources.end());
        native_changed |= record.kind == ArtifactKind::SystemCObject;
        plugin_changed |= record.kind == ArtifactKind::SystemCPlugin;
    }
    if (sources.contains("")) {
        error = "replacement source identity must not be empty";
        return false;
    }
    for (auto& record : catalog.artifacts) {
        if (record.kind == ArtifactKind::SystemCPlugin)
            continue;
        const auto removed = std::erase_if(record.sources,
            [&](const auto& source) { return sources.contains(source); });
        native_changed |= removed && record.kind == ArtifactKind::SystemCObject;
        std::erase_if(record.units,
            [&](const auto& unit) { return sources.contains(unit.source_key); });
    }
    std::erase_if(catalog.artifacts, [&](const auto& record) {
        if (record.kind == ArtifactKind::SystemCPlugin)
            return native_changed || plugin_changed;
        return record.sources.empty();
    });
    std::map<std::string, std::string> owners;
    for (const auto& record : catalog.artifacts) {
        for (const auto& owned : record.units)
            owners.emplace(unit_identity(owned.unit), owned.source_key);
    }
    for (const auto& record : replacements) {
        for (const auto& owned : record.units) {
            if (!owners.emplace(unit_identity(owned.unit), owned.source_key).second) {
                error = "workspace definition collision for " + owned.unit.kind
                    + " '" + owned.unit.name + "'; another source already owns this unit";
                return false;
            }
        }
        catalog.artifacts.push_back(record);
    }
    return true;
}

} // namespace detail

struct LibraryTransaction::Impl {
    LibraryCatalog catalog;
    std::unique_ptr<compiler::detail::CacheDirectoryLock> lock;
    std::vector<ArtifactRecord> allocated;
    bool committed { false };
};

LibraryTransaction::LibraryTransaction(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

LibraryTransaction::~LibraryTransaction()
{
    for (const auto& allocated : impl_->allocated) {
        const bool retained = impl_->committed
            && std::ranges::any_of(impl_->catalog.artifacts, [&](const auto& record) {
                return record.id == allocated.id;
            });
        if (!retained)
            detail::remove_revision(impl_->catalog.location.directory / allocated.path);
    }
}

const LibraryCatalog& LibraryTransaction::catalog() const
{
    return impl_->catalog;
}

std::optional<ArtifactRecord> LibraryTransaction::allocate_artifact(
    const ArtifactKind kind, std::string& error)
{
    error.clear();
    if (impl_->committed) {
        error = "workspace transaction has already committed";
        return std::nullopt;
    }
    const auto directory = impl_->catalog.location.directory / "artifacts";
    if (!detail::ensure_directory(directory, error))
        return std::nullopt;
    ArtifactRecord record;
    record.id = detail::new_revision();
    record.kind = kind;
    std::string extension;
    switch (kind) {
    case ArtifactKind::Hdl:
        extension = ".fsimobj";
        break;
    case ArtifactKind::SystemCObject:
        extension = ".fsimscobj";
        break;
    case ArtifactKind::SystemCPlugin:
        extension = ".fsimscplugin";
        break;
    default:
        error = "invalid workspace artifact kind";
        return std::nullopt;
    }
    record.path = std::filesystem::path { "artifacts" } / (record.id + extension);
    std::error_code code;
    if (std::filesystem::exists(artifact_path(record), code) || code) {
        error = "generated workspace artifact path is unavailable";
        return std::nullopt;
    }
    impl_->allocated.push_back(record);
    return record;
}

std::filesystem::path LibraryTransaction::artifact_path(const ArtifactRecord& record) const
{
    return impl_->catalog.location.directory / record.path;
}

bool LibraryTransaction::commit(ArtifactRecord record, std::string& error)
{
    std::vector<ArtifactRecord> records;
    records.push_back(std::move(record));
    return commit(std::move(records), error);
}

bool LibraryTransaction::commit(std::vector<ArtifactRecord> records, std::string& error)
{
    return commit(std::move(records), { }, error);
}

bool LibraryTransaction::commit(std::vector<ArtifactRecord> records,
    const std::span<const std::string> replaced_sources, std::string& error)
{
    error.clear();
    if (impl_->committed) {
        error = "workspace transaction has already committed";
        return false;
    }
    for (const auto& record : records) {
        const bool allocated = std::ranges::any_of(impl_->allocated, [&](const auto& candidate) {
            return candidate.id == record.id && candidate.path == record.path
                && candidate.kind == record.kind;
        });
        std::error_code code;
        if (!allocated || !detail::safe_descendant(impl_->catalog.location.directory, record.path, error)
            || !std::filesystem::is_directory(artifact_path(record), code) || code) {
            if (error.empty())
                error = "workspace artifact was not allocated and published by this transaction";
            return false;
        }
    }
    auto replacement = impl_->catalog;
    if (!detail::merge_records(replacement, records, replaced_sources, error)
        || !detail::save_catalog(replacement, error))
        return false;
    impl_->catalog = std::move(replacement);
    impl_->committed = true;
    std::set<std::string> retained;
    for (const auto& record : impl_->catalog.artifacts)
        retained.insert(support::path_to_utf8(record.path.filename()));
    detail::collect_revisions(impl_->catalog.location.directory / "artifacts", retained);
    return true;
}

std::unique_ptr<LibraryTransaction> Store::begin_library(
    const std::string_view name, std::string& error) const
{
    error.clear();
    const auto location = detail::locate_library(*this, name, true, error);
    if (!location)
        return nullptr;
    auto impl = std::make_unique<LibraryTransaction::Impl>();
    impl->lock = detail::lock_directory(location->directory / ".writer-lock", error);
    if (!impl->lock)
        return nullptr;
    auto catalog = detail::load_catalog(*location, !location->mapped, error);
    if (!catalog)
        return nullptr;
    impl->catalog = std::move(*catalog);
    return std::unique_ptr<LibraryTransaction> { new LibraryTransaction { std::move(impl) } };
}

bool Store::delete_artifact(const std::string_view name,
    const std::string_view artifact_id, std::string& error) const
{
    error.clear();
    const auto location = detail::locate_library(*this, name, false, error);
    if (!location)
        return false;
    auto lock = detail::lock_directory(location->directory / ".writer-lock", error);
    auto catalog = lock ? detail::load_catalog(*location, false, error) : std::nullopt;
    if (!catalog)
        return false;
    const auto found = std::ranges::find(catalog->artifacts, artifact_id, &ArtifactRecord::id);
    if (found == catalog->artifacts.end()) {
        error = "workspace library does not contain artifact '" + std::string { artifact_id } + "'";
        return false;
    }
    const bool native = found->kind == ArtifactKind::SystemCObject;
    catalog->artifacts.erase(found);
    if (native)
        std::erase_if(catalog->artifacts, [](const auto& record) {
            return record.kind == ArtifactKind::SystemCPlugin;
        });
    if (!detail::save_catalog(*catalog, error))
        return false;
    std::set<std::string> retained;
    for (const auto& record : catalog->artifacts)
        retained.insert(support::path_to_utf8(record.path.filename()));
    detail::collect_revisions(location->directory / "artifacts", retained);
    return true;
}

bool Store::delete_library(const std::string_view name, std::string& error) const
{
    error.clear();
    if (!detail::ensure_directory(managed_directory(), error))
        return false;
    auto mapping_lock = detail::lock_directory(managed_directory() / ".mapping-lock", error);
    const auto location = mapping_lock ? detail::locate_library(*this, name, false, error) : std::nullopt;
    if (!location)
        return false;
    auto lock = detail::lock_directory(location->directory / ".writer-lock", error);
    auto catalog = lock ? detail::load_catalog(*location, false, error) : std::nullopt;
    if (!catalog)
        return false;
    // Publishing the empty catalog first keeps readers from observing stale
    // references while physical deletion and mapping removal are completed.
    catalog->artifacts.clear();
    if (!detail::save_catalog(*catalog, error))
        return false;
    if (location->mapped) {
        auto mappings = detail::read_mappings(*this, error);
        if (!mappings)
            return false;
        mappings->erase(std::string { name });
        if (!detail::write_mappings(*this, *mappings, error))
            return false;
    }
    detail::collect_revisions(location->directory / "artifacts", { });
    std::error_code code;
    std::filesystem::remove(location->directory / "artifacts", code);
    code.clear();
    std::filesystem::remove(location->directory / "library.sqlite3", code);
    if (code) {
        error = "cannot delete workspace library catalog: " + code.message();
        return false;
    }
    lock.reset();
    if (!location->mapped)
        std::filesystem::remove(location->directory, code);
    return true;
}

} // namespace fsim::app::workspace
