// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_workspace_store_internal.hpp"

#include "fsim/support/sqlite_database.hpp"

#include <array>

namespace fsim::app::workspace::detail {

class Database final {
public:
    enum class OpenMode {
        read_only,
        read_write_existing,
        read_write_create,
    };

    [[nodiscard]] bool open(const LibraryLocation&, OpenMode, std::string& error);
    [[nodiscard]] bool execute(const char*, std::string& error);
    [[nodiscard]] bool validate_schema(std::string& error);
    [[nodiscard]] bool initialize_schema(std::string& error);
    support::sqlite::Database connection { "workspace library catalog" };
    std::size_t decoded_bytes { 0U };
};

class Statement final {
public:
    Statement(Database&, const char* sql, std::string& error);
    explicit operator bool() const { return static_cast<bool>(statement_); }
    [[nodiscard]] support::sqlite::Step step(std::string& error);
    [[nodiscard]] bool bind(int, std::string_view, std::string& error);
    [[nodiscard]] bool bind(int, int, std::string& error);
    [[nodiscard]] bool finish(std::string& error);
    [[nodiscard]] std::optional<std::string> text(int, std::string& error);
    [[nodiscard]] std::optional<int> integer(int, std::string& error);

private:
    Database& database_;
    support::sqlite::Statement statement_;
};

[[nodiscard]] std::optional<library::UnitIndexEntry> read_unit(
    Statement&, int offset, std::string& error);
[[nodiscard]] bool bind_unit(Statement&, int offset,
    const library::UnitIndexEntry&, std::string& error);

} // namespace fsim::app::workspace::detail
