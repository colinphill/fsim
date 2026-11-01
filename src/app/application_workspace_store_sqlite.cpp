// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_store_sqlite.hpp"

#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
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
        "checksum TEXT NOT NULL, standard TEXT NOT NULL, profile TEXT NOT NULL, source TEXT NOT NULL, "
        "name_form INTEGER NOT NULL DEFAULT 0, primary_name_form INTEGER NOT NULL DEFAULT 0)" },
    { "dependencies",
        "CREATE TABLE dependencies(owner TEXT NOT NULL REFERENCES artifacts(id) ON DELETE CASCADE, "
        "ordinal INTEGER NOT NULL, library TEXT NOT NULL, language TEXT NOT NULL, kind TEXT NOT NULL, "
        "name TEXT NOT NULL, primary_name TEXT NOT NULL, architecture TEXT NOT NULL, "
        "artifact TEXT NOT NULL, checksum TEXT NOT NULL, standard TEXT NOT NULL, profile TEXT NOT NULL, "
        "fingerprint TEXT NOT NULL, name_form INTEGER NOT NULL DEFAULT 0, "
        "primary_name_form INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(owner, ordinal))" },
} };

} // namespace

bool Database::open(const LibraryLocation& location, const OpenMode mode, std::string& error)
{
    for (const auto* name : { "library.sqlite3", "library.sqlite3-journal",
             "library.sqlite3-wal", "library.sqlite3-shm" }) {
        if (!safe_descendant(location.directory, name, error))
            return false;
    }
    const auto path = location.directory / "library.sqlite3";
    std::error_code code;
    if (support::native_fs::exists(path, code)) {
        const auto size = support::native_fs::file_size(path, code);
        if (code || size > 256U * 1024U * 1024U) {
            error = "workspace library catalog exceeds its size limit";
            return false;
        }
    }
    auto open_mode = support::sqlite::Database::OpenMode::read_only;
    if (mode == OpenMode::read_write_existing)
        open_mode = support::sqlite::Database::OpenMode::read_write_existing;
    else if (mode == OpenMode::read_write_create)
        open_mode = support::sqlite::Database::OpenMode::read_write_create;
    return connection.open(path, open_mode,
               { static_cast<std::int64_t>(kMaximumTextBytes), 65536, 32 }, error)
        && execute("PRAGMA foreign_keys=ON", error);
}

bool Database::execute(const char* sql, std::string& error)
{
    return connection.execute(sql, error);
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
    support::sqlite::Step status;
    while ((status = query.step(error)) == support::sqlite::Step::row) {
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
    if (status != support::sqlite::Step::done || count != kSchema.size()) {
        if (error.empty())
            error = "workspace library catalog schema is incomplete";
        return false;
    }
    return true;
}

Statement::Statement(Database& database, const char* sql, std::string& error)
    : database_(database)
    , statement_(database.connection, sql, error)
{
}

support::sqlite::Step Statement::step(std::string& error)
{
    return statement_.step(error);
}

bool Statement::bind(const int index, const std::string_view value, std::string& error)
{
    if (value.size() > kMaximumTextBytes
        || value.find('\0') != std::string_view::npos) {
        error = "workspace catalog field is invalid or exceeds its size limit";
        return false;
    }
    return statement_.bind_text(index, value, error);
}

bool Statement::bind(const int index, const int value, std::string& error)
{
    return statement_.bind_integer(index, value, error);
}

bool Statement::finish(std::string& error)
{
    return statement_.finish(error);
}

std::optional<std::string> Statement::text(const int index, std::string& error)
{
    const auto view = statement_.text(index, error);
    if (!view || view->size() > kMaximumTextBytes
        || view->size() > 128U * 1024U * 1024U - database_.decoded_bytes) {
        error = "workspace library catalog contains invalid or excessive text";
        return std::nullopt;
    }
    database_.decoded_bytes += view->size();
    std::string value { *view };
    if (value.find('\0') != std::string::npos) {
        error = "workspace library catalog text contains a null byte";
        return std::nullopt;
    }
    return value;
}

std::optional<int> Statement::integer(const int index, std::string& error)
{
    const auto value = statement_.integer(index, error);
    if (!value || *value < std::numeric_limits<int>::min()
        || *value > std::numeric_limits<int>::max()) {
        error = "workspace library catalog integer is invalid";
        return std::nullopt;
    }
    return static_cast<int>(*value);
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
    const auto name_form = query.integer(offset + 9, error);
    const auto primary_name_form = query.integer(offset + 10, error);
    if (!name_form || !primary_name_form || *name_form < 0 || *name_form > 2
        || *primary_name_form < 0 || *primary_name_form > 2) {
        error = "workspace catalog has an invalid VHDL identifier form";
        return std::nullopt;
    }
    unit.name_form = static_cast<library::VhdlIdentifierForm>(*name_form);
    unit.primary_name_form
        = static_cast<library::VhdlIdentifierForm>(*primary_name_form);
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
    const auto name_form = static_cast<int>(unit.name_form);
    const auto primary_name_form = static_cast<int>(unit.primary_name_form);
    if (name_form < 0 || name_form > 2 || primary_name_form < 0
        || primary_name_form > 2
        || !query.bind(offset + 9, name_form, error)
        || !query.bind(offset + 10, primary_name_form, error)) {
        if (error.empty())
            error = "workspace catalog unit has an invalid VHDL identifier form";
        return false;
    }
    return true;
}

} // namespace fsim::app::workspace::detail
