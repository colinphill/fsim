// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/sqlite_database.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::artifact {

// Every fsim artifact (object, design snapshot, exported library) is one
// SQLite file: a `meta` table of text properties and named payloads stored
// as ordered chunks. Chunks keep each value below SQLite's value limit and
// bound the memory a streamed payload needs.
inline constexpr std::int32_t kContainerApplicationId = 0x4653494D; // "FSIM"
inline constexpr std::int32_t kContainerSchema = 1;
inline constexpr std::size_t kContainerChunkBytes = 16U * 1024U * 1024U;

// SQLite page size of a new container. Objects are many and mostly small,
// so they use small pages; snapshots and libraries hold large payloads,
// which read about a third faster from 64 KiB pages.
enum class ContainerPages : std::uint8_t { compact, bulk };

struct ContainerEntry {
    std::string name;
    std::uint64_t size {};
    std::string checksum;
    std::uint64_t chunks {};
};

class ContainerReader final {
public:
    // Opens a published container read-only and checks its application id,
    // schema and kind; an empty `kind` accepts any.
    [[nodiscard]] static std::optional<ContainerReader> open(
        const std::filesystem::path&, std::string_view kind, std::string& error);
    // The `kind` recorded in a container, for callers that dispatch on it.
    [[nodiscard]] static std::optional<std::string> read_kind(
        const std::filesystem::path&, std::string& error);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::optional<std::string> meta(std::string_view key, std::string& error);
    // Every meta property, ordered by key.
    [[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>> meta_entries(
        std::string& error);
    [[nodiscard]] std::optional<std::vector<ContainerEntry>> entries(std::string& error);
    // nullopt with an empty `error` when the payload is absent.
    [[nodiscard]] std::optional<ContainerEntry> entry(std::string_view name, std::string& error);
    // The payload's bytes, verified against its stored checksum. Payloads
    // larger than `maximum_bytes` are refused before any chunk is read.
    [[nodiscard]] std::optional<std::string> read(
        std::string_view name, std::uint64_t maximum_bytes, std::string& error);
    // Hands each verified chunk to `consume` in order; for payloads parsed
    // incrementally or copied to a file.
    [[nodiscard]] bool read_chunks(std::string_view name,
        const std::function<bool(std::string_view)>& consume, std::string& error);
    // A stream buffer over the payload's chunks for incremental decoders.
    // It does not verify the checksum; verify with read_chunks first. It
    // must not outlive the reader.
    [[nodiscard]] std::unique_ptr<std::streambuf> open_chunks(
        std::string_view name, std::string& error);

private:
    ContainerReader(std::filesystem::path, support::sqlite::Database);

    std::filesystem::path path_;
    support::sqlite::Database database_;
};

class ContainerWriter final {
public:
    // Starts a private temporary file beside `destination`; nothing is
    // visible at `destination` until publish().
    [[nodiscard]] static std::optional<ContainerWriter> create(
        const std::filesystem::path& destination, std::string_view kind, ContainerPages pages,
        std::string& error);

    ContainerWriter(ContainerWriter&&) noexcept;
    ContainerWriter& operator=(ContainerWriter&&) = delete;
    ContainerWriter(const ContainerWriter&) = delete;
    ContainerWriter& operator=(const ContainerWriter&) = delete;
    // Removes the temporary file of an unpublished container.
    ~ContainerWriter();

    [[nodiscard]] bool meta(std::string_view key, std::string_view value, std::string& error);
    [[nodiscard]] bool put(std::string_view name, std::string_view bytes, std::string& error);
    // Streams a payload: `produce` writes to the stream, which stores a
    // chunk whenever kContainerChunkBytes are buffered.
    [[nodiscard]] bool put_stream(std::string_view name,
        const std::function<bool(std::ostream&)>& produce, std::string& error);
    // Commits, closes and renames the file onto `destination`, replacing
    // an existing file. On POSIX a reader of the old file keeps its version.
    [[nodiscard]] bool publish(std::string& error);
    [[nodiscard]] const std::filesystem::path& destination() const noexcept
    {
        return destination_;
    }

private:
    class ChunkBuffer;

    ContainerWriter(std::filesystem::path destination, std::filesystem::path temporary,
        support::sqlite::Database);
    [[nodiscard]] bool put_chunk(std::string_view name, std::uint64_t ordinal,
        std::string_view bytes, std::string& error);
    [[nodiscard]] bool put_entry(const ContainerEntry&, std::string& error);

    std::filesystem::path destination_;
    std::filesystem::path temporary_;
    support::sqlite::Database database_;
    bool published_ {};
};

} // namespace fsim::artifact
