// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_internal.hpp"

#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <map>
#include <system_error>

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
    // VHDL re-analysis (IEEE 1076-2008 13.5): a unit analyzed again replaces
    // the unit of the same name even when another source defined it. The
    // secondary units of a replaced primary unit, and objects that depend on
    // a replaced or obsolete unit, are obsolete and leave the catalog.
    const auto secondary = [](const library::UnitIndexEntry& unit) {
        return unit.kind == "architecture" || unit.kind == "package_body"
            || (unit.kind == "package" && !unit.primary_name.empty());
    };
    std::set<std::string> gone;
    std::set<std::string> replaced_primaries;
    const auto primary_identity = [](const library::UnitIndexEntry& unit) {
        auto primary = unit;
        primary.kind = "primary";
        // A configuration names its entity in primary_name but is itself a
        // primary unit.
        const bool own_name = unit.primary_name.empty()
            || (unit.kind != "architecture" && unit.kind != "package_body"
                && unit.kind != "package");
        primary.name = own_name ? unit.name : unit.primary_name;
        primary.primary_name.clear();
        primary.architecture.clear();
        primary.name_form = own_name
            ? unit.name_form : unit.primary_name_form;
        primary.primary_name_form = library::VhdlIdentifierForm::unknown;
        return unit_identity(primary);
    };
    std::set<std::string> emptied;
    for (const auto& record : replacements) {
        for (const auto& owned : record.units) {
            if (owned.unit.language != "vhdl")
                continue;
            gone.insert(unit_identity(owned.unit));
            if (!secondary(owned.unit))
                replaced_primaries.insert(primary_identity(owned.unit));
        }
    }
    if (!gone.empty()) {
        for (auto& record : catalog.artifacts) {
            if (record.kind != ArtifactKind::Hdl)
                continue;
            const auto removed = std::erase_if(record.units, [&](const auto& owned) {
                return owned.unit.language == "vhdl"
                    && (gone.contains(unit_identity(owned.unit))
                        || (secondary(owned.unit)
                            && replaced_primaries.contains(primary_identity(owned.unit))));
            });
            if (removed != 0U && record.units.empty())
                emptied.insert(record.id);
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (auto& record : catalog.artifacts) {
                if (record.kind != ArtifactKind::Hdl || record.units.empty())
                    continue;
                const bool obsolete = std::ranges::any_of(
                    record.dependencies, [&](const auto& dependency) {
                        return dependency.library == catalog.location.name
                            && gone.contains(unit_identity(dependency.unit));
                    });
                if (!obsolete)
                    continue;
                for (const auto& owned : record.units)
                    gone.insert(unit_identity(owned.unit));
                record.units.clear();
                emptied.insert(record.id);
                changed = true;
            }
        }
    }
    std::erase_if(catalog.artifacts, [&](const auto& record) {
        if (record.kind == ArtifactKind::SystemCPlugin)
            return native_changed || plugin_changed;
        return record.sources.empty() || emptied.contains(record.id);
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
    // Allocated records keep their revision id; until commit names them
    // after their primary unit, publishers write to the hidden pending
    // file in `path`.
    std::vector<ArtifactRecord> allocated;
    bool committed { false };
};

namespace {

std::string folded_filename(const std::filesystem::path& path)
{
    auto name = support::path_to_utf8(path);
    std::ranges::transform(name, name.begin(), [](const unsigned char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 'a' - 'A')
                                          : static_cast<char>(value);
    });
    return name;
}

bool install_pending(const std::filesystem::path& pending,
    const std::filesystem::path& destination, std::string& error)
{
    std::error_code code;
    if (!compiler::detail::atomic_replace_file(pending, destination, code) && !code)
        code = std::make_error_code(std::errc::io_error);
    if (code) {
        error = "cannot install workspace artifact '" + support::path_to_utf8(destination.filename())
            + "': " + code.message();
        return false;
    }
    return true;
}

} // namespace

LibraryTransaction::LibraryTransaction(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

LibraryTransaction::~LibraryTransaction()
{
    if (impl_->committed)
        return;
    for (const auto& record : impl_->allocated)
        detail::remove_artifact(impl_->catalog.location.directory / record.path);
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
    const auto extension = detail::artifact_extension(kind);
    if (extension.empty()) {
        error = "invalid workspace artifact kind";
        return std::nullopt;
    }
    if (!detail::ensure_directory(impl_->catalog.location.directory, error))
        return std::nullopt;
    ArtifactRecord record;
    record.id = detail::new_revision();
    record.kind = kind;
    record.path = ".pending-" + record.id + std::string { extension };
    std::error_code code;
    if (support::native_fs::exists(impl_->catalog.location.directory / record.path, code)
        || code) {
        error = "generated workspace artifact path is unavailable";
        return std::nullopt;
    }
    impl_->allocated.push_back(record);
    return record;
}

std::filesystem::path LibraryTransaction::artifact_path(const ArtifactRecord& record) const
{
    // After commit the record is named; callers may still hold the copy
    // they allocated.
    if (impl_->committed) {
        const auto named = std::ranges::find(impl_->catalog.artifacts, record.id,
            &ArtifactRecord::id);
        if (named != impl_->catalog.artifacts.end())
            return impl_->catalog.location.directory / named->path;
    }
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
    const auto& directory = impl_->catalog.location.directory;
    std::map<std::string, std::filesystem::path> pending;
    for (auto& record : records) {
        const bool allocated = std::ranges::any_of(impl_->allocated, [&](const auto& candidate) {
            return candidate.id == record.id && candidate.path == record.path
                && candidate.kind == record.kind;
        });
        std::error_code code;
        if (!allocated || !support::native_fs::exists(directory / record.path, code) || code
            || !pending.emplace(record.id, directory / record.path).second) {
            error = "workspace artifact was not allocated and published by this transaction";
            return false;
        }
        // A placeholder name until the merged catalog is known.
        record.path = record.id + std::string { detail::artifact_extension(record.kind) };
    }
    auto replacement = impl_->catalog;
    if (!detail::merge_records(replacement, records, replaced_sources, error))
        return false;
    // Name each new artifact after its primary unit, avoiding (without regard
    // to case) every name the catalog keeps. A recompiled unit takes the
    // name of the record it replaces.
    std::set<std::string> used;
    for (const auto& record : replacement.artifacts) {
        if (!pending.contains(record.id))
            used.insert(folded_filename(record.path));
    }
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> installs;
    for (auto& record : replacement.artifacts) {
        const auto source = pending.find(record.id);
        if (source == pending.end())
            continue;
        const auto base = detail::artifact_name(record);
        const auto extension = std::string { detail::artifact_extension(record.kind) };
        auto name = base;
        for (std::size_t counter = 2U; used.contains(folded_filename(name + extension)); ++counter)
            name = base + "~" + std::to_string(counter);
        used.insert(folded_filename(name + extension));
        record.path = name + extension;
        installs.emplace_back(source->second, directory / record.path);
    }
    if (!detail::save_catalog(replacement, error))
        return false;
    impl_->catalog = std::move(replacement);
    impl_->committed = true;
    for (const auto& [published, destination] : installs) {
        if (!install_pending(published, destination, error))
            return false;
    }
    std::set<std::string> retained;
    for (const auto& record : impl_->catalog.artifacts)
        retained.insert(support::path_to_utf8(record.path.filename()));
    detail::collect_artifacts(impl_->catalog.location.directory, retained);
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
    auto catalog = detail::load_catalog(*location, !location->mapped,
        detail::CatalogAccess::writer_lock_held, error);
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
    auto catalog = lock ? detail::load_catalog(*location, false,
        detail::CatalogAccess::writer_lock_held, error) : std::nullopt;
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
    detail::collect_artifacts(location->directory, retained);
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
    auto catalog = lock ? detail::load_catalog(*location, false,
        detail::CatalogAccess::writer_lock_held, error) : std::nullopt;
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
    detail::collect_artifacts(location->directory, { });
    std::error_code code;
    support::native_fs::remove(location->directory / "library.sqlite3", code);
    if (code) {
        error = "cannot delete workspace library catalog: " + code.message();
        return false;
    }
    lock.reset();
    if (!location->mapped)
        support::native_fs::remove(location->directory, code);
    return true;
}

} // namespace fsim::app::workspace
