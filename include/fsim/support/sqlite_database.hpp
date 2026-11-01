// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace fsim::support::sqlite {

// One SQLite connection with fsim's hardening applied: no symlink following,
// defensive mode, untrusted schema, no ATTACH, and bounded value and SQL
// sizes. Every error message starts with `context`.
class Database final {
public:
    enum class OpenMode {
        read_only,
        // A published artifact that is replaced only by rename: SQLite takes
        // no locks and never looks for a journal.
        immutable,
        read_write_existing,
        read_write_create,
    };

    struct Limits {
        std::int64_t value_bytes { 1024 * 1024 };
        int sql_bytes { 65536 };
        int columns { 32 };
    };

    Database() = default;
    explicit Database(std::string context);
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    [[nodiscard]] bool open(const std::filesystem::path&, OpenMode, const Limits&,
        std::string& error);
    // Closes the connection and reports a failed close (an unfinalized
    // statement or an unfinished write).
    [[nodiscard]] bool close(std::string& error);
    [[nodiscard]] bool execute(const char* sql, std::string& error);
    [[nodiscard]] std::optional<std::int64_t> pragma_integer(
        const char* pragma, std::string& error);
    // Reads one whole BLOB cell into `destination`, which must be exactly
    // the cell's size; avoids SQLite's own copy of large values.
    [[nodiscard]] bool read_blob(const char* table, const char* column, std::int64_t row,
        std::span<char> destination, std::string& error);
    // Rows changed by the last INSERT, UPDATE or DELETE.
    [[nodiscard]] std::int64_t changes() const noexcept;
    void fail(std::string& error) const;
    [[nodiscard]] sqlite3* handle() const noexcept { return handle_; }
    [[nodiscard]] const std::string& context() const noexcept { return context_; }

private:
    std::string context_ { "SQLite database" };
    sqlite3* handle_ { nullptr };
};

enum class Step {
    row,
    done,
    failed,
};

class Statement final {
public:
    Statement(Database&, const char* sql, std::string& error);
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement();
    explicit operator bool() const { return statement_ != nullptr; }

    // Step::failed sets `error`.
    [[nodiscard]] Step step(std::string& error);
    // Steps once and requires Step::done.
    [[nodiscard]] bool finish(std::string& error);
    [[nodiscard]] bool reset(std::string& error);
    [[nodiscard]] bool bind_text(int, std::string_view, std::string& error);
    [[nodiscard]] bool bind_blob(int, std::string_view, std::string& error);
    [[nodiscard]] bool bind_integer(int, std::int64_t, std::string& error);
    [[nodiscard]] bool is_null(int) const;
    [[nodiscard]] std::optional<std::string_view> text(int, std::string& error) const;
    [[nodiscard]] std::optional<std::string_view> blob(int, std::string& error) const;
    [[nodiscard]] std::optional<std::int64_t> integer(int, std::string& error) const;

private:
    Database& database_;
    sqlite3_stmt* statement_ { nullptr };
};

} // namespace fsim::support::sqlite
