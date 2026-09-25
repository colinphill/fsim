// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_sqlite.hpp"

#include "fsim/support/path.hpp"

#include <limits>

namespace fsim::app::workspace::detail {
namespace {

constexpr std::array<std::pair<std::string_view, const char*>, 5> kSchema { {
    { "metadata",
        "CREATE TABLE metadata(version INTEGER NOT NULL, library TEXT NOT NULL)" },
    { "artifacts",
        "CREATE TABLE artifacts(id TEXT PRIMARY KEY NOT NULL, kind INTEGER NOT NULL, "
        "path TEXT NOT NULL, fingerprint TEXT NOT NULL)" },
    { "sources",
        "CREATE TABLE sources(owner TEXT NOT NULL REFERENCES artifacts(id) ON DELETE CASCADE, "
        "source TEXT NOT NULL, PRIMARY KEY(owner, source))" },
    { "units",
        "CREATE TABLE units(identity TEXT PRIMARY KEY NOT NULL, "
        "owner TEXT NOT NULL REFERENCES artifacts(id) ON DELETE CASCADE, "
        "language TEXT NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, "
        "primary_name TEXT NOT NULL, architecture TEXT NOT NULL, artifact TEXT NOT NULL, "
        "checksum TEXT NOT NULL, standard TEXT NOT NULL, profile TEXT NOT NULL, source TEXT NOT NULL)" },
    { "dependencies",
        "CREATE TABLE dependencies(owner TEXT NOT NULL REFERENCES artifacts(id) ON DELETE CASCADE, "
        "ordinal INTEGER NOT NULL, library TEXT NOT NULL, language TEXT NOT NULL, kind TEXT NOT NULL, "
        "name TEXT NOT NULL, primary_name TEXT NOT NULL, architecture TEXT NOT NULL, "
        "artifact TEXT NOT NULL, checksum TEXT NOT NULL, standard TEXT NOT NULL, profile TEXT NOT NULL, "
        "fingerprint TEXT NOT NULL, PRIMARY KEY(owner, ordinal))" },
} };

void database_error(Database& database, std::string& error)
{
    error = "workspace library catalog: ";
    error += database.handle ? sqlite3_errmsg(database.handle) : "cannot open SQLite database";
}

} // namespace

Database::~Database()
{
    if (handle)
        sqlite3_close(handle);
}

bool Database::open(const LibraryLocation& location, const bool write, std::string& error)
{
    for (const auto* name : { "library.sqlite3", "library.sqlite3-journal",
             "library.sqlite3-wal", "library.sqlite3-shm" }) {
        if (!safe_descendant(location.directory, name, error))
            return false;
    }
    const auto path = location.directory / "library.sqlite3";
    std::error_code code;
    if (std::filesystem::exists(path, code)) {
        const auto size = std::filesystem::file_size(path, code);
        if (code || size > 256U * 1024U * 1024U) {
            error = "workspace library catalog exceeds its size limit";
            return false;
        }
    }
    const int flags = write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY;
    if (sqlite3_open_v2(support::path_to_utf8(path).c_str(), &handle,
            flags | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK) {
        database_error(*this, error);
        return false;
    }
    sqlite3_limit(handle, SQLITE_LIMIT_LENGTH, static_cast<int>(kMaximumTextBytes));
    sqlite3_limit(handle, SQLITE_LIMIT_SQL_LENGTH, 65536);
    sqlite3_limit(handle, SQLITE_LIMIT_COLUMN, 32);
    sqlite3_limit(handle, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_db_config(handle, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_busy_timeout(handle, 30000);
    return execute("PRAGMA trusted_schema=OFF", error)
        && execute("PRAGMA foreign_keys=ON", error);
}

bool Database::execute(const char* sql, std::string& error)
{
    if (sqlite3_exec(handle, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    database_error(*this, error);
    return false;
}

bool Database::initialize_schema(std::string& error)
{
    for (const auto& [unused, sql] : kSchema) {
        (void)unused;
        if (!execute(sql, error))
            return false;
    }
    return true;
}

bool Database::validate_schema(std::string& error)
{
    Statement query { *this,
        "SELECT type, name, sql FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'", error };
    if (!query)
        return false;
    std::size_t count = 0U;
    int status;
    while ((status = query.step(error)) == SQLITE_ROW) {
        const auto type = query.text(0, error);
        const auto name = query.text(1, error);
        const auto sql = query.text(2, error);
        bool found = false;
        for (const auto& [expected_name, expected_sql] : kSchema) {
            if (name == expected_name && sql == expected_sql)
                found = true;
        }
        if (!found || type != "table" || ++count > kSchema.size()) {
            error = "workspace library catalog has an unsupported schema";
            return false;
        }
    }
    if (status != SQLITE_DONE || count != kSchema.size()) {
        if (error.empty())
            error = "workspace library catalog schema is incomplete";
        return false;
    }
    return true;
}

Statement::Statement(Database& database, const char* sql, std::string& error)
    : database_(database)
{
    if (sqlite3_prepare_v2(database.handle, sql, -1, &statement_, nullptr) != SQLITE_OK)
        database_error(database_, error);
}

Statement::~Statement()
{
    sqlite3_finalize(statement_);
}

int Statement::step(std::string& error)
{
    const int status = sqlite3_step(statement_);
    if (status != SQLITE_ROW && status != SQLITE_DONE)
        database_error(database_, error);
    return status;
}

bool Statement::bind(const int index, const std::string_view value, std::string& error)
{
    if (value.size() > kMaximumTextBytes
        || value.find('\0') != std::string_view::npos) {
        error = "workspace catalog field is invalid or exceeds its size limit";
        return false;
    }
    if (sqlite3_bind_text(statement_, index, value.empty() ? "" : value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        database_error(database_, error);
        return false;
    }
    return true;
}

bool Statement::bind(const int index, const int value, std::string& error)
{
    if (sqlite3_bind_int(statement_, index, value) == SQLITE_OK)
        return true;
    database_error(database_, error);
    return false;
}

bool Statement::finish(std::string& error)
{
    return step(error) == SQLITE_DONE;
}

std::optional<std::string> Statement::text(const int index, std::string& error)
{
    const auto size = static_cast<std::size_t>(sqlite3_column_bytes(statement_, index));
    if (sqlite3_column_type(statement_, index) != SQLITE_TEXT
        || size > kMaximumTextBytes || size > 128U * 1024U * 1024U - database_.decoded_bytes) {
        error = "workspace library catalog contains invalid or excessive text";
        return std::nullopt;
    }
    database_.decoded_bytes += size;
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(statement_, index));
    std::string value { data, size };
    if (value.find('\0') != std::string::npos) {
        error = "workspace library catalog text contains a null byte";
        return std::nullopt;
    }
    return value;
}

std::optional<int> Statement::integer(const int index, std::string& error)
{
    const auto value = sqlite3_column_int64(statement_, index);
    if (sqlite3_column_type(statement_, index) != SQLITE_INTEGER
        || value < std::numeric_limits<int>::min()
        || value > std::numeric_limits<int>::max()) {
        error = "workspace library catalog integer is invalid";
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<library::UnitIndexEntry> read_unit(
    Statement& query, const int offset, std::string& error)
{
    library::UnitIndexEntry unit;
    std::array<std::string, 9> fields;
    for (int index = 0; index < 9; ++index) {
        auto field = query.text(offset + index, error);
        if (!field)
            return std::nullopt;
        fields[static_cast<std::size_t>(index)] = std::move(*field);
    }
    unit.language = std::move(fields[0]);
    unit.kind = std::move(fields[1]);
    unit.name = std::move(fields[2]);
    unit.primary_name = std::move(fields[3]);
    unit.architecture = std::move(fields[4]);
    unit.artifact = support::path_from_utf8(fields[5]);
    unit.checksum = std::move(fields[6]);
    unit.standard = std::move(fields[7]);
    unit.compatibility_profile = std::move(fields[8]);
    return unit;
}

bool bind_unit(Statement& query, const int offset,
    const library::UnitIndexEntry& unit, std::string& error)
{
    const std::array<std::string, 9> fields { unit.language, unit.kind, unit.name,
        unit.primary_name, unit.architecture, support::path_to_utf8(unit.artifact),
        unit.checksum, unit.standard, unit.compatibility_profile };
    for (int index = 0; index < 9; ++index) {
        if (!query.bind(offset + index, fields[static_cast<std::size_t>(index)], error))
            return false;
    }
    return true;
}

} // namespace fsim::app::workspace::detail
