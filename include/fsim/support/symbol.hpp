// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

// Interned identifiers (owner ruling 11-19). Each identifier's canonical
// form is interned once, when it is lexed: VHDL basic identifiers lower-cased
// and extended identifiers exactly as written, backslashes included
// (IEEE 1076-2019 15.4); SystemVerilog identifiers exactly, escaped ones as
// written without their terminating white space (IEEE 1800-2023 5.6). Name
// lookup then compares SymbolIds, never identifier text.
//
// Ids are process-local: artifacts store a symbol as its text and reading
// re-interns it, so an id is never written. SymbolId is deliberately not an
// aggregate (pfr walks such as link relocation skip it) and has no ordering,
// so nothing can sort or hash outputs by an id's value.
namespace fsim::support {

class SymbolId {
public:
    constexpr SymbolId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0U; }
    [[nodiscard]] constexpr bool operator==(const SymbolId& other) const noexcept
    {
        return value_ == other.value_;
    }

    /// The table's own index (1-based; 0 is "no symbol"). For SymbolTable and
    /// hashing inside one process only.
    [[nodiscard]] constexpr std::uint32_t index() const noexcept { return value_; }

private:
    friend class SymbolTable;
    constexpr explicit SymbolId(const std::uint32_t value) noexcept
        : value_ { value }
    {
    }

    std::uint32_t value_ { };
};

/// The process-wide, append-only symbol table. Thread-safe: lexing runs in
/// parallel. text() of a valid id stays valid for the life of the process.
class SymbolTable {
public:
    [[nodiscard]] static SymbolTable& global();

    /// The symbol of `canonical` (already in canonical form).
    [[nodiscard]] SymbolId intern(std::string_view canonical);
    /// The canonical text of a valid symbol; empty for an invalid one.
    [[nodiscard]] std::string_view text(SymbolId symbol) const noexcept;

    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;

private:
    SymbolTable() = default;
    friend struct SymbolTableAccess;
};

/// The symbol of a VHDL identifier spelling: a basic identifier lower-cased,
/// an extended identifier (\...\) exactly as written.
[[nodiscard]] SymbolId vhdl_symbol(std::string_view spelling);
/// The symbol of a SystemVerilog identifier's text (escaped identifiers
/// without their terminating white space).
[[nodiscard]] SymbolId systemverilog_symbol(std::string_view text);

} // namespace fsim::support
