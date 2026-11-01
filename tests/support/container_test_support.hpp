// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/container.hpp"
#include "fsim/support/sqlite_database.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fsim::test {

// Publishes `path` again with some payloads replaced byte for byte. No fsim
// format check runs, so tests can corrupt or downgrade a published artifact.
inline void rewrite_container_payloads(const std::filesystem::path& path,
    const std::map<std::string, std::string>& replacements)
{
    std::string error;
    const auto kind = artifact::ContainerReader::read_kind(path, error);
    assert(kind);
    auto reader = artifact::ContainerReader::open(path, *kind, error);
    assert(reader);
    const auto meta = reader->meta_entries(error);
    const auto entries = reader->entries(error);
    assert(meta && entries);
    auto writer = artifact::ContainerWriter::create(path, *kind,
        *kind == "fsim-object" ? artifact::ContainerPages::compact
                               : artifact::ContainerPages::bulk,
        error);
    assert(writer);
    for (const auto& [key, value] : *meta) {
        if (key != "kind")
            assert(writer->meta(key, value, error));
    }
    for (const auto& entry : *entries) {
        const auto replaced = replacements.find(entry.name);
        if (replaced != replacements.end()) {
            assert(writer->put(entry.name, replaced->second, error));
            continue;
        }
        const auto bytes = reader->read(entry.name, entry.size, error);
        assert(bytes);
        assert(writer->put(entry.name, *bytes, error));
    }
    for (const auto& [name, bytes] : replacements)
        assert(reader->entry(name, error) || writer->put(name, bytes, error));
    reader.reset();
    assert(writer->publish(error));
}

// Records a payload size without storing that many bytes, for tests of the
// size checks readers apply before reading any chunk.
inline void forge_container_payload_size(const std::filesystem::path& path,
    const std::string& name, const std::uint64_t size)
{
    std::string error;
    support::sqlite::Database database { "container under test" };
    assert(database.open(path, support::sqlite::Database::OpenMode::read_write_existing,
        { }, error));
    {
        support::sqlite::Statement update { database,
            "UPDATE payloads SET size = ?1 WHERE name = ?2", error };
        assert(update && update.bind_integer(1, static_cast<std::int64_t>(size), error)
            && update.bind_text(2, name, error) && update.finish(error));
    }
    assert(database.close(error));
}

// Drops one payload, as a damaged or partial artifact would.
inline void remove_container_payload(const std::filesystem::path& path,
    const std::string& name)
{
    std::string error;
    support::sqlite::Database database { "container under test" };
    assert(database.open(path, support::sqlite::Database::OpenMode::read_write_existing,
        { }, error));
    for (const auto* sql : { "DELETE FROM payloads WHERE name = ?1",
             "DELETE FROM chunks WHERE name = ?1" }) {
        support::sqlite::Statement remove { database, sql, error };
        assert(remove && remove.bind_text(1, name, error) && remove.finish(error));
    }
    assert(database.close(error));
}

// The bytes of one container payload, unchecked by any fsim format.
inline std::string read_container_payload(
    const std::filesystem::path& path, const std::string& name)
{
    std::string error;
    const auto kind = artifact::ContainerReader::read_kind(path, error);
    assert(kind);
    auto reader = artifact::ContainerReader::open(path, *kind, error);
    assert(reader);
    const auto entry = reader->entry(name, error);
    assert(entry);
    auto bytes = reader->read(name, entry->size, error);
    assert(bytes);
    return std::move(*bytes);
}

} // namespace fsim::test

namespace fsim::test {

// Every meta property (as "meta:<key>") and payload of a container, in
// name order, for comparing artifacts by content rather than file bytes.
inline std::vector<std::pair<std::string, std::string>> container_contents(
    const std::filesystem::path& path)
{
    std::string error;
    auto reader = artifact::ContainerReader::open(path, { }, error);
    assert(reader);
    std::vector<std::pair<std::string, std::string>> contents;
    const auto meta = reader->meta_entries(error);
    const auto entries = reader->entries(error);
    assert(meta && entries);
    for (const auto& [key, value] : *meta)
        contents.emplace_back("meta:" + key, value);
    for (const auto& entry : *entries) {
        auto bytes = reader->read(entry.name, entry.size, error);
        assert(bytes);
        contents.emplace_back(entry.name, std::move(*bytes));
    }
    return contents;
}

// Appends bytes to one payload; the container checksum is recomputed, so
// only the artifact's own index can detect the change.
inline void append_container_payload(const std::filesystem::path& path,
    const std::string& name, const std::string& suffix)
{
    auto bytes = read_container_payload(path, name);
    bytes += suffix;
    rewrite_container_payloads(path, { { name, bytes } });
}

} // namespace fsim::test
