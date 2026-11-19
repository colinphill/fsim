// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/symbol.hpp"

#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace fsim::support {

// The table's storage lives here, not in the header. Entries are appended in
// fixed-size chunks that never move, so text(id) reads without a lock; a
// symbol's text lives in an arena block that is never freed.
struct SymbolTableAccess {
    static constexpr std::size_t shard_count = 64U;
    static constexpr std::size_t chunk_size = 4096U;
    static constexpr std::size_t chunk_limit = 1U << 16U;
    static constexpr std::size_t arena_block = 1U << 16U;

    struct Shard {
        std::mutex mutex;
        std::unordered_map<std::string_view, std::uint32_t> ids;
    };
    struct Chunk {
        std::array<std::string_view, chunk_size> entries { };
    };

    std::array<Shard, shard_count> shards;
    // Entry `index` (1-based) lives in chunks[index / chunk_size].
    std::array<std::atomic<Chunk*>, chunk_limit> chunks { };
    std::mutex growth_mutex;
    std::uint32_t next_index { 1U };
    char* arena_cursor { nullptr };
    std::size_t arena_left { 0U };

    static SymbolTableAccess& instance()
    {
        // Never destroyed: symbols are read during static destruction.
        static SymbolTableAccess* const table = new SymbolTableAccess;
        return *table;
    }

    // Caller holds growth_mutex.
    std::string_view store(const std::string_view text)
    {
        if (text.empty()) {
            return { };
        }
        if (text.size() > arena_left) {
            const auto size = std::max(text.size(), arena_block);
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            arena_cursor = new char[size];
            arena_left = size;
        }
        char* const at = arena_cursor;
        std::memcpy(at, text.data(), text.size());
        arena_cursor += text.size();
        arena_left -= text.size();
        return { at, text.size() };
    }

    std::uint32_t append(const std::string_view text, std::string_view& stored)
    {
        const std::lock_guard lock { growth_mutex };
        const auto index = next_index;
        const auto chunk_at = static_cast<std::size_t>(index) / chunk_size;
        if (chunk_at >= chunk_limit) {
            throw std::length_error { "the symbol table is full" };
        }
        auto* chunk = chunks[chunk_at].load(std::memory_order_relaxed);
        if (chunk == nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            chunk = new Chunk;
            chunks[chunk_at].store(chunk, std::memory_order_release);
        }
        stored = store(text);
        chunk->entries[static_cast<std::size_t>(index) % chunk_size] = stored;
        ++next_index;
        return index;
    }
};

SymbolTable& SymbolTable::global()
{
    static SymbolTable* const table = new SymbolTable;
    return *table;
}

SymbolId SymbolTable::intern(const std::string_view canonical)
{
    auto& access = SymbolTableAccess::instance();
    auto& shard = access.shards[std::hash<std::string_view> { }(canonical)
        % SymbolTableAccess::shard_count];
    const std::lock_guard lock { shard.mutex };
    if (const auto found = shard.ids.find(canonical); found != shard.ids.end()) {
        return SymbolId { found->second };
    }
    std::string_view stored;
    const auto index = access.append(canonical, stored);
    shard.ids.emplace(stored, index);
    return SymbolId { index };
}

std::string_view SymbolTable::text(const SymbolId symbol) const noexcept
{
    if (!symbol.valid()) {
        return { };
    }
    const auto index = static_cast<std::size_t>(symbol.index());
    const auto* const chunk = SymbolTableAccess::instance()
                                  .chunks[index / SymbolTableAccess::chunk_size]
                                  .load(std::memory_order_acquire);
    return chunk == nullptr
        ? std::string_view { }
        : chunk->entries[index % SymbolTableAccess::chunk_size];
}

SymbolId vhdl_symbol(const std::string_view spelling)
{
    return SymbolTable::global().intern(canonical_vhdl_spelling(spelling));
}

SymbolId systemverilog_symbol(std::string_view text)
{
    // An escaped identifier's backslash and terminating white space are not
    // part of the identifier (IEEE 1800-2023 5.6.1).
    while (!text.empty()
        && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n'
            || text.back() == '\r' || text.back() == '\f'
            || text.back() == '\v')) {
        text.remove_suffix(1U);
    }
    if (text.size() >= 2U && text.front() == '\\') {
        text.remove_prefix(1U);
    }
    return SymbolTable::global().intern(text);
}

} // namespace fsim::support
