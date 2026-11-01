// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/container.hpp"

#include "fsim/compiler/cache_support.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/payload_checksum.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <streambuf>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fsim::artifact {
namespace {

constexpr std::array<std::pair<std::string_view, const char*>, 3> kSchema { {
    { "meta", "CREATE TABLE meta(key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID" },
    { "payloads",
        "CREATE TABLE payloads(name TEXT PRIMARY KEY NOT NULL, size INTEGER NOT NULL, "
        "checksum TEXT NOT NULL, chunks INTEGER NOT NULL) WITHOUT ROWID" },
    { "chunks",
        "CREATE TABLE chunks(name TEXT NOT NULL, ordinal INTEGER NOT NULL, "
        "data BLOB NOT NULL, PRIMARY KEY(name, ordinal))" },
} };

// A row holds one chunk plus its key; SQLite's length limit covers the row.
constexpr support::sqlite::Database::Limits kLimits {
    static_cast<std::int64_t>(kContainerChunkBytes) + 65536, 65536, 8
};

std::atomic_uint64_t temporary_sequence { 0U };

std::filesystem::path temporary_path(const std::filesystem::path& destination)
{
#if defined(_WIN32)
    const auto process = static_cast<long long>(_getpid());
#else
    const auto process = static_cast<long long>(::getpid());
#endif
    const auto sequence = temporary_sequence.fetch_add(1U, std::memory_order_relaxed);
    return destination.parent_path()
        / ("." + support::path_to_utf8(destination.filename()) + ".tmp-"
            + std::to_string(process) + "-" + std::to_string(sequence));
}

bool validate_schema(support::sqlite::Database& database, std::string& error)
{
    support::sqlite::Statement query { database,
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
            if (name == expected_name && sql == std::string_view { expected_sql })
                found = true;
        }
        if (!found || type != "table" || ++count > kSchema.size()) {
            error = database.context() + ": unsupported container schema";
            return false;
        }
    }
    if (status != support::sqlite::Step::done || count != kSchema.size()) {
        if (error.empty())
            error = database.context() + ": container schema is incomplete";
        return false;
    }
    return true;
}

class ChunkStreamBuffer final : public std::streambuf {
public:
    ChunkStreamBuffer(support::sqlite::Database& database, const std::string_view name,
        std::string& error)
        : query_ { database, "SELECT data FROM chunks WHERE name = ?1 ORDER BY ordinal", error }
        , ready_ { query_ && query_.bind_text(1, name, error) }
    {
    }

    [[nodiscard]] bool ready() const noexcept { return ready_; }

protected:
    int_type underflow() override
    {
        while (gptr() == egptr()) {
            std::string error;
            if (!ready_ || query_.step(error) != support::sqlite::Step::row)
                return traits_type::eof();
            const auto bytes = query_.blob(0, error);
            if (!bytes) {
                ready_ = false;
                return traits_type::eof();
            }
            buffer_.assign(*bytes);
            setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
        }
        return traits_type::to_int_type(*gptr());
    }

private:
    support::sqlite::Statement query_;
    bool ready_;
    std::string buffer_;
};

std::string context_for(const std::filesystem::path& path)
{
    return "artifact '" + support::path_to_utf8(path) + "'";
}

} // namespace

ContainerReader::ContainerReader(std::filesystem::path path, support::sqlite::Database database)
    : path_(std::move(path))
    , database_(std::move(database))
{
}

std::optional<ContainerReader> ContainerReader::open(
    const std::filesystem::path& path, const std::string_view kind, std::string& error)
{
    support::sqlite::Database database { context_for(path) };
    if (!database.open(path, support::sqlite::Database::OpenMode::immutable, kLimits, error))
        return std::nullopt;
    const auto application = database.pragma_integer("PRAGMA application_id", error);
    const auto schema = database.pragma_integer("PRAGMA user_version", error);
    if (!application || !schema)
        return std::nullopt;
    if (*application != kContainerApplicationId) {
        error = database.context() + " is not an fsim artifact";
        return std::nullopt;
    }
    if (*schema != kContainerSchema) {
        error = database.context() + " has container schema " + std::to_string(*schema)
            + "; this fsim reads schema " + std::to_string(kContainerSchema)
            + ". Regenerate the artifact";
        return std::nullopt;
    }
    if (!validate_schema(database, error))
        return std::nullopt;
    ContainerReader reader { path, std::move(database) };
    const auto stored = reader.meta("kind", error);
    if (!stored)
        return std::nullopt;
    if (!kind.empty() && *stored != kind) {
        error = reader.database_.context() + " is a '" + *stored + "' artifact, not '"
            + std::string { kind } + "'";
        return std::nullopt;
    }
    return reader;
}

std::optional<std::string> ContainerReader::read_kind(
    const std::filesystem::path& path, std::string& error)
{
    auto reader = open(path, { }, error);
    return reader ? reader->meta("kind", error) : std::nullopt;
}

std::optional<std::vector<std::pair<std::string, std::string>>> ContainerReader::meta_entries(
    std::string& error)
{
    support::sqlite::Statement query { database_, "SELECT key, value FROM meta ORDER BY key",
        error };
    if (!query)
        return std::nullopt;
    std::vector<std::pair<std::string, std::string>> result;
    support::sqlite::Step status;
    while ((status = query.step(error)) == support::sqlite::Step::row) {
        const auto key = query.text(0, error);
        const auto value = query.text(1, error);
        if (!key || !value)
            return std::nullopt;
        result.emplace_back(std::string { *key }, std::string { *value });
    }
    if (status != support::sqlite::Step::done)
        return std::nullopt;
    return result;
}

std::optional<std::string> ContainerReader::meta(const std::string_view key, std::string& error)
{
    support::sqlite::Statement query { database_, "SELECT value FROM meta WHERE key = ?1", error };
    if (!query || !query.bind_text(1, key, error))
        return std::nullopt;
    const auto status = query.step(error);
    if (status == support::sqlite::Step::done) {
        error = database_.context() + " has no '" + std::string { key } + "' property";
        return std::nullopt;
    }
    if (status != support::sqlite::Step::row)
        return std::nullopt;
    const auto value = query.text(0, error);
    if (!value)
        return std::nullopt;
    return std::string { *value };
}

std::optional<std::vector<ContainerEntry>> ContainerReader::entries(std::string& error)
{
    support::sqlite::Statement query { database_,
        "SELECT name, size, checksum, chunks FROM payloads ORDER BY name", error };
    if (!query)
        return std::nullopt;
    std::vector<ContainerEntry> result;
    support::sqlite::Step status;
    while ((status = query.step(error)) == support::sqlite::Step::row) {
        const auto name = query.text(0, error);
        const auto size = query.integer(1, error);
        const auto checksum = query.text(2, error);
        const auto chunks = query.integer(3, error);
        if (!name || !size || !checksum || !chunks || *size < 0 || *chunks < 0)
            return std::nullopt;
        result.push_back({ std::string { *name }, static_cast<std::uint64_t>(*size),
            std::string { *checksum }, static_cast<std::uint64_t>(*chunks) });
    }
    if (status != support::sqlite::Step::done)
        return std::nullopt;
    return result;
}

std::optional<ContainerEntry> ContainerReader::entry(const std::string_view name, std::string& error)
{
    error.clear();
    support::sqlite::Statement query { database_,
        "SELECT size, checksum, chunks FROM payloads WHERE name = ?1", error };
    if (!query || !query.bind_text(1, name, error))
        return std::nullopt;
    const auto status = query.step(error);
    if (status != support::sqlite::Step::row)
        return std::nullopt;
    const auto size = query.integer(0, error);
    const auto checksum = query.text(1, error);
    const auto chunks = query.integer(2, error);
    if (!size || !checksum || !chunks || *size < 0 || *chunks < 0) {
        if (error.empty())
            error = database_.context() + ": invalid payload index";
        return std::nullopt;
    }
    return ContainerEntry { std::string { name }, static_cast<std::uint64_t>(*size),
        std::string { *checksum }, static_cast<std::uint64_t>(*chunks) };
}

bool ContainerReader::read_chunks(const std::string_view name,
    const std::function<bool(std::string_view)>& consume, std::string& error)
{
    const auto indexed = entry(name, error);
    if (!indexed) {
        if (error.empty())
            error = database_.context() + " has no payload '" + std::string { name } + "'";
        return false;
    }
    support::sqlite::Statement query { database_,
        "SELECT ordinal, data FROM chunks WHERE name = ?1 ORDER BY ordinal", error };
    if (!query || !query.bind_text(1, name, error))
        return false;
    support::PayloadChecksumVerifier verifier { indexed->checksum };
    std::uint64_t ordinal = 0U;
    std::uint64_t total = 0U;
    support::sqlite::Step status;
    while ((status = query.step(error)) == support::sqlite::Step::row) {
        const auto stored_ordinal = query.integer(0, error);
        const auto bytes = query.blob(1, error);
        if (!stored_ordinal || !bytes || *stored_ordinal != static_cast<std::int64_t>(ordinal)
            || bytes->size() > indexed->size - total) {
            error = database_.context() + ": payload '" + std::string { name }
                + "' has a damaged chunk sequence";
            return false;
        }
        ++ordinal;
        total += bytes->size();
        verifier.update(*bytes);
        if (!consume(*bytes))
            return false;
    }
    if (status != support::sqlite::Step::done)
        return false;
    if (ordinal != indexed->chunks || total != indexed->size || !verifier.matches()) {
        error = database_.context() + ": payload '" + std::string { name }
            + "' fails its checksum";
        return false;
    }
    return true;
}

std::unique_ptr<std::streambuf> ContainerReader::open_chunks(
    const std::string_view name, std::string& error)
{
    error.clear();
    auto buffer = std::make_unique<ChunkStreamBuffer>(database_, name, error);
    if (!buffer->ready()) {
        if (error.empty())
            error = database_.context() + ": cannot read payload '" + std::string { name } + "'";
        return nullptr;
    }
    return buffer;
}

std::optional<std::string> ContainerReader::read(
    const std::string_view name, const std::uint64_t maximum_bytes, std::string& error)
{
    const auto indexed = entry(name, error);
    if (!indexed) {
        if (error.empty())
            error = database_.context() + " has no payload '" + std::string { name } + "'";
        return std::nullopt;
    }
    if (indexed->size > maximum_bytes) {
        error = database_.context() + ": payload '" + std::string { name }
            + "' exceeds its size limit";
        return std::nullopt;
    }
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(indexed->size));
    if (!read_chunks(name, [&](const std::string_view chunk) {
            bytes.append(chunk);
            return true;
        }, error))
        return std::nullopt;
    return bytes;
}

class ContainerWriter::ChunkBuffer final : public std::streambuf {
public:
    ChunkBuffer(ContainerWriter& writer, std::string_view name, std::string& error)
        : writer_(writer)
        , name_(name)
        , error_(error)
    {
        buffer_.resize(kContainerChunkBytes);
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    [[nodiscard]] bool finish()
    {
        return failed_ ? false : flush_chunk() && writer_.put_entry({ name_, total_,
                             support::payload_checksum(checksum_), ordinal_ },
                             error_);
    }

protected:
    int_type overflow(const int_type character) override
    {
        if (!flush_chunk())
            return traits_type::eof();
        if (!traits_type::eq_int_type(character, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(character);
            pbump(1);
        }
        return traits_type::not_eof(character);
    }

    int sync() override { return 0; }

private:
    [[nodiscard]] bool flush_chunk()
    {
        const auto size = static_cast<std::size_t>(pptr() - pbase());
        if (failed_)
            return false;
        if (size == 0U)
            return true;
        const std::string_view bytes { pbase(), size };
        checksum_.update(bytes);
        if (!writer_.put_chunk(name_, ordinal_, bytes, error_)) {
            failed_ = true;
            return false;
        }
        ++ordinal_;
        total_ += size;
        setp(buffer_.data(), buffer_.data() + buffer_.size());
        return true;
    }

    ContainerWriter& writer_;
    std::string name_;
    std::string& error_;
    std::string buffer_;
    support::Xxh64 checksum_;
    std::uint64_t ordinal_ {};
    std::uint64_t total_ {};
    bool failed_ {};
};

ContainerWriter::ContainerWriter(std::filesystem::path destination,
    std::filesystem::path temporary, support::sqlite::Database database)
    : destination_(std::move(destination))
    , temporary_(std::move(temporary))
    , database_(std::move(database))
{
}

ContainerWriter::ContainerWriter(ContainerWriter&& other) noexcept
    : destination_(std::move(other.destination_))
    , temporary_(std::move(other.temporary_))
    , database_(std::move(other.database_))
    , published_(std::exchange(other.published_, true))
{
}

ContainerWriter::~ContainerWriter()
{
    if (published_ || temporary_.empty())
        return;
    std::string ignored;
    (void)database_.close(ignored);
    std::error_code code;
    support::native_fs::remove(temporary_, code);
}

std::optional<ContainerWriter> ContainerWriter::create(
    const std::filesystem::path& destination, const std::string_view kind,
    const ContainerPages pages, std::string& error)
{
    auto temporary = temporary_path(destination);
    support::sqlite::Database database { context_for(destination) };
    if (!database.open(temporary, support::sqlite::Database::OpenMode::read_write_create,
            kLimits, error))
        return std::nullopt;
    // The temporary file is private until the rename, so it needs neither
    // a journal nor durable intermediate writes.
    ContainerWriter writer { destination, std::move(temporary), std::move(database) };
    auto& handle = writer.database_;
    if (!handle.execute(pages == ContainerPages::compact
                ? "PRAGMA page_size=4096" : "PRAGMA page_size=65536", error)
        || !handle.execute("PRAGMA journal_mode=OFF", error)
        || !handle.execute("PRAGMA synchronous=OFF", error)
        || !handle.execute(("PRAGMA application_id=" + std::to_string(kContainerApplicationId))
                               .c_str(),
            error)
        || !handle.execute(("PRAGMA user_version=" + std::to_string(kContainerSchema)).c_str(),
            error)
        || !handle.execute("BEGIN", error))
        return std::nullopt;
    for (const auto& [unused, sql] : kSchema) {
        (void)unused;
        if (!handle.execute(sql, error))
            return std::nullopt;
    }
    if (!writer.meta("kind", kind, error))
        return std::nullopt;
    return writer;
}

bool ContainerWriter::meta(const std::string_view key, const std::string_view value,
    std::string& error)
{
    support::sqlite::Statement insert { database_,
        "INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2)", error };
    return insert && insert.bind_text(1, key, error) && insert.bind_text(2, value, error)
        && insert.finish(error);
}

bool ContainerWriter::put_chunk(const std::string_view name, const std::uint64_t ordinal,
    const std::string_view bytes, std::string& error)
{
    support::sqlite::Statement insert { database_,
        "INSERT INTO chunks(name, ordinal, data) VALUES(?1, ?2, ?3)", error };
    return insert && insert.bind_text(1, name, error)
        && insert.bind_integer(2, static_cast<std::int64_t>(ordinal), error)
        && insert.bind_blob(3, bytes, error) && insert.finish(error);
}

bool ContainerWriter::put_entry(const ContainerEntry& entry, std::string& error)
{
    support::sqlite::Statement insert { database_,
        "INSERT INTO payloads(name, size, checksum, chunks) VALUES(?1, ?2, ?3, ?4)", error };
    return insert && insert.bind_text(1, entry.name, error)
        && insert.bind_integer(2, static_cast<std::int64_t>(entry.size), error)
        && insert.bind_text(3, entry.checksum, error)
        && insert.bind_integer(4, static_cast<std::int64_t>(entry.chunks), error)
        && insert.finish(error);
}

bool ContainerWriter::put(const std::string_view name, const std::string_view bytes,
    std::string& error)
{
    std::uint64_t ordinal = 0U;
    for (std::size_t offset = 0U; offset < bytes.size(); offset += kContainerChunkBytes) {
        if (!put_chunk(name, ordinal++, bytes.substr(offset, kContainerChunkBytes), error))
            return false;
    }
    return put_entry({ std::string { name }, bytes.size(), support::payload_checksum(bytes),
                         ordinal },
        error);
}

bool ContainerWriter::put_stream(const std::string_view name,
    const std::function<bool(std::ostream&)>& produce, std::string& error)
{
    ChunkBuffer buffer { *this, name, error };
    std::ostream stream { &buffer };
    if (!produce(stream))
        return false;
    stream.flush();
    if (!stream) {
        if (error.empty())
            error = database_.context() + ": cannot write payload '" + std::string { name } + "'";
        return false;
    }
    return buffer.finish();
}

bool ContainerWriter::publish(std::string& error)
{
    if (published_) {
        error = database_.context() + " is already published";
        return false;
    }
    if (!database_.execute("COMMIT", error) || !database_.close(error))
        return false;
    std::error_code code;
    if (!compiler::detail::atomic_replace_file(temporary_, destination_, code)) {
        error = database_.context() + ": cannot publish: " + code.message();
        return false;
    }
    published_ = true;
    return true;
}

} // namespace fsim::artifact
