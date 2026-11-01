// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/sqlite_database.hpp"

#include "fsim/support/native_filesystem.hpp"

#include <sqlite3.h>

#include <limits>
#include <utility>

namespace fsim::support::sqlite {
namespace {

// SQLite URI filenames reserve '%', '?' and '#'.
std::string uri_for(const std::filesystem::path& path)
{
    std::string uri { "file:" };
    for (const char character : native_fs::utf8_for_native_io(path)) {
        if (character == '%' || character == '?' || character == '#') {
            static constexpr char digits[] = "0123456789ABCDEF";
            const auto byte = static_cast<unsigned char>(character);
            uri += '%';
            uri += digits[byte >> 4U];
            uri += digits[byte & 15U];
        } else {
            uri += character;
        }
    }
    return uri;
}

} // namespace

Database::Database(std::string context)
    : context_(std::move(context))
{
}

Database::Database(Database&& other) noexcept
    : context_(std::move(other.context_))
    , handle_(std::exchange(other.handle_, nullptr))
{
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other) {
        if (handle_)
            sqlite3_close(handle_);
        context_ = std::move(other.context_);
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

Database::~Database()
{
    if (handle_)
        sqlite3_close(handle_);
}

void Database::fail(std::string& error) const
{
    error = context_ + ": ";
    error += handle_ ? sqlite3_errmsg(handle_) : "cannot open SQLite database";
}

bool Database::open(const std::filesystem::path& path, const OpenMode mode,
    const Limits& limits, std::string& error)
{
    if (handle_) {
        error = context_ + ": database is already open";
        return false;
    }
    int flags = SQLITE_OPEN_READONLY;
    if (mode == OpenMode::read_write_existing)
        flags = SQLITE_OPEN_READWRITE;
    else if (mode == OpenMode::read_write_create)
        flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    auto name = uri_for(path);
    if (mode == OpenMode::immutable)
        name += "?immutable=1";
    // SQLite's Windows VFS only widens drive paths and leaves '/' in place.
    if (sqlite3_open_v2(name.c_str(), &handle_,
            flags | SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_NOFOLLOW, nullptr)
        != SQLITE_OK) {
        fail(error);
        sqlite3_close(std::exchange(handle_, nullptr));
        return false;
    }
    const auto value_bytes = std::min<std::int64_t>(
        limits.value_bytes, std::numeric_limits<int>::max());
    sqlite3_limit(handle_, SQLITE_LIMIT_LENGTH, static_cast<int>(value_bytes));
    sqlite3_limit(handle_, SQLITE_LIMIT_SQL_LENGTH, limits.sql_bytes);
    sqlite3_limit(handle_, SQLITE_LIMIT_COLUMN, limits.columns);
    sqlite3_limit(handle_, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_db_config(handle_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_busy_timeout(handle_, 30000);
    return execute("PRAGMA trusted_schema=OFF", error);
}

bool Database::close(std::string& error)
{
    if (!handle_)
        return true;
    if (sqlite3_close(handle_) != SQLITE_OK) {
        fail(error);
        return false;
    }
    handle_ = nullptr;
    return true;
}

bool Database::execute(const char* sql, std::string& error)
{
    if (sqlite3_exec(handle_, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    fail(error);
    return false;
}

std::optional<std::int64_t> Database::pragma_integer(const char* pragma, std::string& error)
{
    Statement query { *this, pragma, error };
    if (!query || query.step(error) != Step::row)
        return std::nullopt;
    return query.integer(0, error);
}

bool Database::read_blob(const char* table, const char* column, const std::int64_t row,
    const std::span<char> destination, std::string& error)
{
    sqlite3_blob* blob = nullptr;
    if (sqlite3_blob_open(handle_, "main", table, column, row, 0, &blob) != SQLITE_OK) {
        fail(error);
        sqlite3_blob_close(blob);
        return false;
    }
    bool read = static_cast<std::uint64_t>(sqlite3_blob_bytes(blob)) == destination.size()
        && destination.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (!read)
        error = context_ + ": stored value size differs from its record";
    else if (!destination.empty()
        && sqlite3_blob_read(blob, destination.data(), static_cast<int>(destination.size()), 0)
            != SQLITE_OK) {
        fail(error);
        read = false;
    }
    sqlite3_blob_close(blob);
    return read;
}

std::int64_t Database::changes() const noexcept
{
    return sqlite3_changes64(handle_);
}

Statement::Statement(Database& database, const char* sql, std::string& error)
    : database_(database)
{
    if (sqlite3_prepare_v2(database.handle(), sql, -1, &statement_, nullptr) != SQLITE_OK)
        database_.fail(error);
}

Statement::~Statement()
{
    sqlite3_finalize(statement_);
}

Step Statement::step(std::string& error)
{
    switch (sqlite3_step(statement_)) {
    case SQLITE_ROW:
        return Step::row;
    case SQLITE_DONE:
        return Step::done;
    default:
        database_.fail(error);
        return Step::failed;
    }
}

bool Statement::finish(std::string& error)
{
    return step(error) == Step::done;
}

bool Statement::reset(std::string& error)
{
    sqlite3_clear_bindings(statement_);
    if (sqlite3_reset(statement_) == SQLITE_OK)
        return true;
    database_.fail(error);
    return false;
}

bool Statement::bind_text(const int index, const std::string_view value, std::string& error)
{
    if (sqlite3_bind_text64(statement_, index, value.empty() ? "" : value.data(),
            value.size(), SQLITE_TRANSIENT, SQLITE_UTF8)
        == SQLITE_OK)
        return true;
    database_.fail(error);
    return false;
}

bool Statement::bind_blob(const int index, const std::string_view value, std::string& error)
{
    // SQLITE_STATIC: callers keep the bytes alive until the statement steps.
    if (sqlite3_bind_blob64(statement_, index, value.empty() ? "" : value.data(),
            value.size(), SQLITE_STATIC)
        == SQLITE_OK)
        return true;
    database_.fail(error);
    return false;
}

bool Statement::bind_integer(const int index, const std::int64_t value, std::string& error)
{
    if (sqlite3_bind_int64(statement_, index, value) == SQLITE_OK)
        return true;
    database_.fail(error);
    return false;
}

bool Statement::is_null(const int index) const
{
    return sqlite3_column_type(statement_, index) == SQLITE_NULL;
}

std::optional<std::string_view> Statement::text(const int index, std::string& error) const
{
    if (sqlite3_column_type(statement_, index) != SQLITE_TEXT) {
        error = database_.context() + ": column is not text";
        return std::nullopt;
    }
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(statement_, index));
    const auto size = static_cast<std::size_t>(sqlite3_column_bytes(statement_, index));
    return std::string_view { data, size };
}

std::optional<std::string_view> Statement::blob(const int index, std::string& error) const
{
    if (sqlite3_column_type(statement_, index) != SQLITE_BLOB) {
        error = database_.context() + ": column is not a blob";
        return std::nullopt;
    }
    const auto* data = static_cast<const char*>(sqlite3_column_blob(statement_, index));
    const auto size = static_cast<std::size_t>(sqlite3_column_bytes(statement_, index));
    return std::string_view { data, size };
}

std::optional<std::int64_t> Statement::integer(const int index, std::string& error) const
{
    if (sqlite3_column_type(statement_, index) != SQLITE_INTEGER) {
        error = database_.context() + ": column is not an integer";
        return std::nullopt;
    }
    return sqlite3_column_int64(statement_, index);
}

} // namespace fsim::support::sqlite
