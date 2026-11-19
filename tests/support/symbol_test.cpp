// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_design_artifact_codec_internal.hpp"
#include "fsim/frontend/token.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/symbol.hpp"

#include <atomic>
#include <cassert>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace codec_detail = fsim::app::codec_detail;
using fsim::support::SymbolId;
using fsim::support::SymbolTable;
using fsim::support::systemverilog_symbol;
using fsim::support::vhdl_symbol;

void test_vhdl_canonical_forms()
{
    // Basic identifiers are case-insensitive (IEEE 1076-2019 15.4.2).
    assert(vhdl_symbol("Foo").valid());
    assert(vhdl_symbol("Foo") == vhdl_symbol("FOO"));
    assert(vhdl_symbol("Foo") == vhdl_symbol("foo"));
    assert(SymbolTable::global().text(vhdl_symbol("FOO")) == "foo");
    // Extended identifiers keep their case and backslashes (15.4.3): three
    // distinct identifiers.
    assert(!(vhdl_symbol("\\Foo\\") == vhdl_symbol("\\foo\\")));
    assert(!(vhdl_symbol("\\foo\\") == vhdl_symbol("foo")));
    assert(!(vhdl_symbol("\\Foo\\") == vhdl_symbol("foo")));
    assert(SymbolTable::global().text(vhdl_symbol("\\Foo\\")) == "\\Foo\\");
    assert(SymbolTable::global().text(vhdl_symbol("\\a\\\\B\\")) == "\\a\\\\B\\");
    assert(vhdl_symbol("\\a\\\\B\\") == vhdl_symbol("\\a\\\\B\\"));
}

void test_systemverilog_canonical_forms()
{
    // Identifiers are case-sensitive; an escaped identifier's backslash and
    // terminating white space are not part of it (IEEE 1800-2023 5.6.1).
    assert(systemverilog_symbol("abc") == systemverilog_symbol("\\abc"));
    assert(systemverilog_symbol("abc") == systemverilog_symbol("\\abc "));
    assert(systemverilog_symbol("abc") == systemverilog_symbol("abc\n"));
    assert(!(systemverilog_symbol("Foo") == systemverilog_symbol("foo")));
    assert(systemverilog_symbol("\\a+b ") == systemverilog_symbol("a+b"));
    assert(SymbolTable::global().text(systemverilog_symbol("\\a+b ")) == "a+b");
}

void test_invalid_symbol()
{
    const SymbolId none;
    assert(!none.valid());
    assert(SymbolTable::global().text(none).empty());
    assert(!(none == vhdl_symbol("x")));
}

void test_concurrent_interning()
{
    constexpr int thread_count = 8;
    constexpr int name_count = 2000;
    std::vector<std::vector<SymbolId>> results(thread_count);
    std::atomic<int> ready { 0 };
    std::vector<std::thread> threads;
    for (int thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&, thread] {
            ready.fetch_add(1);
            while (ready.load() < thread_count) {
            }
            auto& mine = results[static_cast<std::size_t>(thread)];
            for (int index = 0; index < name_count; ++index) {
                // Each thread walks the names in a different order.
                const auto at = (index * (thread + 1) * 7 + thread) % name_count;
                mine.resize(static_cast<std::size_t>(name_count));
                mine[static_cast<std::size_t>(at)] = SymbolTable::global().intern(
                    "concurrent_" + std::to_string(at));
            }
            for (int index = 0; index < name_count; ++index) {
                // Fill any slot this thread's stride skipped.
                auto& slot = mine[static_cast<std::size_t>(index)];
                if (!slot.valid()) {
                    slot = SymbolTable::global().intern(
                        "concurrent_" + std::to_string(index));
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (int index = 0; index < name_count; ++index) {
        const auto expected = results[0][static_cast<std::size_t>(index)];
        assert(expected.valid());
        assert(SymbolTable::global().text(expected)
            == "concurrent_" + std::to_string(index));
        for (int thread = 1; thread < thread_count; ++thread) {
            assert(results[static_cast<std::size_t>(thread)]
                       [static_cast<std::size_t>(index)]
                == expected);
        }
    }
}

void test_lexer_sets_identifier_symbols()
{
    using fsim::frontend::Language;
    using fsim::frontend::TokenKind;
    const auto vhdl = fsim::frontend::lex(
        { "t.vhd", "signal Foo, \\Foo\\ : bit; 12" }, Language::Vhdl2008);
    bool saw_basic = false;
    bool saw_extended = false;
    for (const auto& token : vhdl.tokens) {
        if (token.kind != TokenKind::Identifier) {
            assert(!token.symbol.valid());
            continue;
        }
        assert(token.symbol == vhdl_symbol(token.text));
        saw_basic = saw_basic || token.symbol == vhdl_symbol("foo");
        saw_extended = saw_extended || token.symbol == vhdl_symbol("\\Foo\\");
    }
    assert(saw_basic && saw_extended);

    const auto verilog = fsim::frontend::lex(
        { "t.sv", "wire \\abc , abc, Foo, foo;" }, Language::SystemVerilog2017);
    std::vector<SymbolId> symbols;
    for (const auto& token : verilog.tokens) {
        if (token.kind == TokenKind::Identifier) {
            symbols.push_back(token.symbol);
        }
    }
    // wire, \abc, abc, Foo, foo
    assert(symbols.size() == 5U);
    assert(symbols[1] == symbols[2]);
    assert(!(symbols[3] == symbols[4]));
}

void test_name_round_trip()
{
    namespace vhdl = fsim::semantic::vhdl;
    for (const std::string_view spelling :
        { "Foo", "\\Foo\\", "\\a\\\\b\\", "unrelated_name" }) {
        vhdl::Name name;
        name.spelling = std::string { spelling };
        name.canonical = fsim::support::canonical_vhdl_spelling(spelling);
        name.symbol = vhdl_symbol(name.text());
        assert(SymbolTable::global().text(name.symbol) == name.text());

        codec_detail::Writer writer;
        writer.write(name);
        assert(writer.failure().empty());
        const auto bytes = std::move(writer).finish();
        // The id is process-local: the artifact holds the text only.
        assert(bytes.find(name.text()) != std::string::npos);

        codec_detail::Reader reader { bytes };
        vhdl::Name decoded;
        assert(reader.read(decoded));
        assert(reader.remaining() == 0U);
        assert(decoded == name);
        assert(decoded.symbol == name.symbol);
        assert(SymbolTable::global().text(decoded.symbol) == name.text());
    }
    // A name without a symbol round-trips without one.
    vhdl::Name bare;
    bare.spelling = "x";
    codec_detail::Writer writer;
    writer.write(bare);
    const auto bytes = std::move(writer).finish();
    codec_detail::Reader reader { bytes };
    vhdl::Name decoded;
    assert(reader.read(decoded));
    assert(!decoded.symbol.valid());
}

void test_intern_names_sets_every_name()
{
    namespace vhdl = fsim::semantic::vhdl;
    vhdl::Hir hir;
    vhdl::Expression expression;
    vhdl::Name name;
    name.spelling = "MixedCase";
    name.canonical = "mixedcase";
    expression.referenced_name = name;
    hir.mutable_expressions().push_back(std::move(expression));
    vhdl::intern_names(hir);
    assert(hir.expressions().front().referenced_name->symbol == vhdl_symbol("mixedcase"));
    // Idempotent.
    vhdl::intern_names(hir);
    assert(hir.expressions().front().referenced_name->symbol == vhdl_symbol("MIXEDCASE"));
}

} // namespace

int main()
{
    test_vhdl_canonical_forms();
    test_systemverilog_canonical_forms();
    test_invalid_symbol();
    test_concurrent_interning();
    test_lexer_sets_identifier_symbols();
    test_name_round_trip();
    test_intern_names_sets_every_name();
    return 0;
}
