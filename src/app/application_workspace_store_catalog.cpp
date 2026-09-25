// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_sqlite.hpp"

#include "fsim/support/path.hpp"

#include <map>
#include <set>

namespace fsim::app::workspace::detail {
namespace {

class PendingCatalog final {
public:
    PendingCatalog(std::filesystem::path path, const bool exists)
        : path_(std::move(path))
        , retained_(exists)
    {
    }

    ~PendingCatalog()
    {
        if (retained_)
            return;
        std::error_code code;
        std::filesystem::remove(path_, code);
        std::filesystem::remove(path_.parent_path() / "library.sqlite3-journal", code);
    }

    void retain() { retained_ = true; }

private:
    std::filesystem::path path_;
    bool retained_;
};

bool read_identity(Database& database, const LibraryLocation& location,
    std::string& error)
{
    Statement query { database, "SELECT version, library FROM metadata", error };
    if (!query || query.step(error) != SQLITE_ROW) {
        if (error.empty())
            error = "workspace library catalog has no identity metadata";
        return false;
    }
    const auto version = query.integer(0, error);
    const auto name = query.text(1, error);
    if (version != 1 || name != location.name) {
        error = "workspace library catalog version or logical library name does not match";
        return false;
    }
    if (query.step(error) != SQLITE_DONE) {
        error = "workspace library catalog has duplicate metadata";
        return false;
    }
    return true;
}

using RecordMap = std::map<std::string, std::size_t>;

ArtifactRecord* owner_record(Statement& query, LibraryCatalog& catalog,
    const RecordMap& records, std::size_t& row_count, std::string& error)
{
    const auto owner = query.text(0, error);
    if (!owner)
        return nullptr;
    const auto found = records.find(*owner);
    if (found == records.end() || ++row_count > kMaximumRecords) {
        error = "workspace library catalog has dangling or excessive records";
        return nullptr;
    }
    return &catalog.artifacts[found->second];
}

bool read_artifacts(Database& database, LibraryCatalog& catalog,
    RecordMap& records, std::size_t& count, std::string& error)
{
    Statement query { database,
        "SELECT id, kind, path, fingerprint FROM artifacts ORDER BY id", error };
    if (!query)
        return false;
    int status;
    while ((status = query.step(error)) == SQLITE_ROW) {
        auto id = query.text(0, error);
        const auto kind = query.integer(1, error);
        auto path = query.text(2, error);
        auto fingerprint = query.text(3, error);
        if (!id || !kind || !path || !fingerprint || *kind < 0 || *kind > 2
            || ++count > kMaximumRecords) {
            if (error.empty())
                error = "workspace library catalog has invalid or excessive artifacts";
            return false;
        }
        ArtifactRecord record;
        record.id = std::move(*id);
        record.kind = static_cast<ArtifactKind>(*kind);
        record.path = support::path_from_utf8(*path);
        record.fingerprint = std::move(*fingerprint);
        if (!managed_artifact_path(record)
            || !safe_descendant(catalog.location.directory, record.path, error)
            || !records.emplace(record.id, catalog.artifacts.size()).second) {
            if (error.empty())
                error = "workspace library catalog contains an unsafe artifact path";
            return false;
        }
        catalog.artifacts.push_back(std::move(record));
    }
    return status == SQLITE_DONE;
}

bool read_sources(Database& database, LibraryCatalog& catalog,
    const RecordMap& records, std::size_t& count, std::string& error)
{
    Statement query { database, "SELECT owner, source FROM sources ORDER BY owner, source", error };
    if (!query)
        return false;
    int status;
    while ((status = query.step(error)) == SQLITE_ROW) {
        auto* record = owner_record(query, catalog, records, count, error);
        auto source = query.text(1, error);
        if (!record || !source)
            return false;
        record->sources.push_back(std::move(*source));
    }
    return status == SQLITE_DONE;
}

bool read_units(Database& database, LibraryCatalog& catalog,
    const RecordMap& records, std::size_t& count, std::string& error)
{
    Statement query { database,
        "SELECT owner, identity, language, kind, name, primary_name, architecture, "
        "artifact, checksum, standard, profile, source FROM units ORDER BY owner, identity", error };
    if (!query)
        return false;
    int status;
    while ((status = query.step(error)) == SQLITE_ROW) {
        auto* record = owner_record(query, catalog, records, count, error);
        const auto identity = query.text(1, error);
        auto unit = read_unit(query, 2, error);
        auto source = query.text(11, error);
        if (!record || !unit || !identity || !source)
            return false;
        if (*identity != unit_identity(*unit)) {
            error = "workspace unit identity does not match its metadata";
            return false;
        }
        record->units.push_back({ std::move(*unit), std::move(*source) });
    }
    return status == SQLITE_DONE;
}

bool read_dependencies(Database& database, LibraryCatalog& catalog,
    const RecordMap& records, std::size_t& count, std::string& error)
{
    Statement query { database,
        "SELECT owner, ordinal, library, language, kind, name, primary_name, architecture, "
        "artifact, checksum, standard, profile, fingerprint FROM dependencies ORDER BY owner, ordinal", error };
    if (!query)
        return false;
    int status;
    while ((status = query.step(error)) == SQLITE_ROW) {
        auto* record = owner_record(query, catalog, records, count, error);
        const auto ordinal = query.integer(1, error);
        auto library = query.text(2, error);
        auto unit = read_unit(query, 3, error);
        auto fingerprint = query.text(12, error);
        if (!record || !ordinal || !library || !unit || !fingerprint)
            return false;
        if (*ordinal < 0 || static_cast<std::size_t>(*ordinal) != record->dependencies.size()) {
            error = "workspace dependency ordinal is invalid";
            return false;
        }
        record->dependencies.push_back(
            { std::move(*library), std::move(*unit), std::move(*fingerprint) });
    }
    return status == SQLITE_DONE;
}

bool insert_record(Database& database, const ArtifactRecord& record, std::string& error)
{
    Statement artifact { database, "INSERT INTO artifacts VALUES(?, ?, ?, ?)", error };
    if (!artifact || !artifact.bind(1, record.id, error)
        || !artifact.bind(2, static_cast<int>(record.kind), error)
        || !artifact.bind(3, support::path_to_utf8(record.path), error)
        || !artifact.bind(4, record.fingerprint, error) || !artifact.finish(error))
        return false;
    for (const auto& source : record.sources) {
        Statement query { database, "INSERT INTO sources VALUES(?, ?)", error };
        if (!query || !query.bind(1, record.id, error)
            || !query.bind(2, source, error) || !query.finish(error))
            return false;
    }
    for (const auto& owned : record.units) {
        Statement query { database,
            "INSERT INTO units VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", error };
        if (!query || !query.bind(1, unit_identity(owned.unit), error)
            || !query.bind(2, record.id, error) || !bind_unit(query, 3, owned.unit, error)
            || !query.bind(12, owned.source_key, error) || !query.finish(error))
            return false;
    }
    int ordinal = 0;
    for (const auto& dependency : record.dependencies) {
        Statement query { database,
            "INSERT INTO dependencies VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", error };
        if (!query || !query.bind(1, record.id, error)
            || !query.bind(2, ordinal++, error) || !query.bind(3, dependency.library, error)
            || !bind_unit(query, 4, dependency.unit, error)
            || !query.bind(13, dependency.fingerprint, error) || !query.finish(error))
            return false;
    }
    return true;
}

} // namespace

std::optional<LibraryCatalog> load_catalog(const LibraryLocation& location,
    const bool create, std::string& error)
{
    std::error_code code;
    if (create && !std::filesystem::exists(location.directory / "library.sqlite3", code) && !code) {
        LibraryCatalog empty { location, { } };
        if (save_catalog(empty, error))
            return empty;
        return std::nullopt;
    }
    Database database;
    if (!database.open(location, false, error)
        || !database.execute("BEGIN", error) || !database.validate_schema(error)
        || !read_identity(database, location, error))
        return std::nullopt;
    LibraryCatalog catalog { location, { } };
    RecordMap records;
    std::size_t count = 0U;
    if (!read_artifacts(database, catalog, records, count, error)
        || !read_sources(database, catalog, records, count, error)
        || !read_units(database, catalog, records, count, error)
        || !read_dependencies(database, catalog, records, count, error))
        return std::nullopt;
    for (const auto& record : catalog.artifacts) {
        if (!valid_record(record, error))
            return std::nullopt;
    }
    if (!database.execute("COMMIT", error))
        return std::nullopt;
    return catalog;
}

bool save_catalog(const LibraryCatalog& catalog, std::string& error)
{
    std::size_t count = catalog.artifacts.size();
    for (const auto& record : catalog.artifacts) {
        for (const auto size : { record.sources.size(), record.units.size(),
                 record.dependencies.size() }) {
            if (count > kMaximumRecords || size > kMaximumRecords - count) {
                error = "workspace library catalog exceeds its record limit";
                return false;
            }
            count += size;
        }
    }
    std::error_code code;
    const bool exists = std::filesystem::exists(catalog.location.directory / "library.sqlite3", code);
    if (code) {
        error = "cannot inspect library catalog before publication: " + code.message();
        return false;
    }
    PendingCatalog pending { catalog.location.directory / "library.sqlite3", exists };
    Database database;
    if (!database.open(catalog.location, true, error)
        || !database.execute("BEGIN IMMEDIATE", error))
        return false;
    if (exists) {
        if (!database.validate_schema(error) || !read_identity(database, catalog.location, error))
            return false;
    } else {
        if (!database.initialize_schema(error))
            return false;
        Statement identity { database, "INSERT INTO metadata VALUES(1, ?)", error };
        if (!identity || !identity.bind(1, catalog.location.name, error) || !identity.finish(error))
            return false;
    }
    if (!database.execute("DELETE FROM artifacts", error))
        return false;
    for (const auto& record : catalog.artifacts) {
        if (!valid_record(record, error) || !insert_record(database, record, error))
            return false;
    }
    if (!database.execute("COMMIT", error))
        return false;
    pending.retain();
    return true;
}

} // namespace fsim::app::workspace::detail
