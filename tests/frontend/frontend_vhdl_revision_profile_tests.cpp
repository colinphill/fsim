// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

} // namespace

void test_vhdl_revision_lexical_profiles()
{
    const auto has_code = [](const auto& diagnostics,
                              const std::string_view code) {
        return std::ranges::any_of(
            diagnostics, [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    };

    const auto vhdl_1987_identifier = parse_text(
        "vhdl87-later-word.vhd",
        "entity shared is end entity shared;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl1987);
    require(
        vhdl_1987_identifier.ok() && vhdl_1987_identifier.design.units.front().name == "shared",
        "a word introduced as reserved in VHDL-1993 remains an identifier in "
        "VHDL-1987");
    const auto vhdl_1993_reserved = parse_text(
        "vhdl93-reserved-word.vhd",
        "entity shared is end entity shared;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl1993);
    const auto reserved_diagnostic = std::ranges::find_if(
        vhdl_1993_reserved.diagnostics,
        [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-LEX-002";
        });
    require(
        reserved_diagnostic != vhdl_1993_reserved.diagnostics.end() && reserved_diagnostic->span.begin.line == 1U && reserved_diagnostic->span.begin.column == 8U && reserved_diagnostic->span.end.column == 14U && reserved_diagnostic->message.find("VHDL-1993") != std::string::npos,
        "newly reserved words have revision and exact-coordinate migration "
        "diagnostics");

    const auto vhdl_1993_protected_name = parse_text(
        "vhdl93-protected-name.vhd",
        "entity protected is end entity protected;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl1993);
    const auto vhdl_2000_protected_word = parse_text(
        "vhdl2000-protected-word.vhd",
        "entity protected is end entity protected;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2000);
    require(
        vhdl_1993_protected_name.ok() && has_code(vhdl_2000_protected_word.diagnostics, "FSIM-VHDL-LEX-002"),
        "protected changes from an identifier to a reserved word at VHDL-2000");

    const auto vhdl_2002_context_name = parse_text(
        "vhdl2002-context-name.vhd",
        "entity context is end entity context;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2002);
    const auto vhdl_2008_context_word = parse_text(
        "vhdl2008-context-word.vhd",
        "entity context is end entity context;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2008);
    require(
        vhdl_2002_context_name.ok() && has_code(vhdl_2008_context_word.diagnostics, "FSIM-VHDL-LEX-002"),
        "VHDL-2008 reserved words remain legal identifiers in VHDL-2002");

    const auto vhdl_1987_extended = lex(
        SourceText { "vhdl87-extended.vhd", "\\Legacy Name\\" },
        Language::Vhdl2008,
        VhdlStandard::Vhdl1987);
    const auto vhdl_1993_extended = lex(
        SourceText { "vhdl93-extended.vhd", "\\Legacy Name\\" },
        Language::Vhdl2008,
        VhdlStandard::Vhdl1993);
    require(
        has_code(vhdl_1987_extended.diagnostics, "FSIM-VHDL-LEX-001") && vhdl_1987_extended.diagnostics.front().span.begin.column == 1U && vhdl_1987_extended.diagnostics.front().span.end.column == 14U && vhdl_1993_extended.ok(),
        "extended identifiers begin in VHDL-1993 and retain exact source spans");

    const auto older_delimiters = lex(
        SourceText { "vhdl2002-delimiters.vhd", "?= << >>" },
        Language::Vhdl2008,
        VhdlStandard::Vhdl2002);
    require(
        std::ranges::count_if(
            older_delimiters.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-LEX-001";
            }) == 3
            && older_delimiters.diagnostics[0].span.begin.column == 1U && older_delimiters.diagnostics[1].span.begin.column == 4U && older_delimiters.diagnostics[2].span.begin.column == 7U,
        "VHDL-2008 question-mark and external-name delimiters are rejected in "
        "older revisions at their exact coordinates");

    const auto older_literals = lex(
        SourceText { "vhdl2002-literals.vhd", "137B\"1\" D\"9\" SX\"F\"" },
        Language::Vhdl2008,
        VhdlStandard::Vhdl2002);
    require(
        std::ranges::count_if(
            older_literals.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-LEX-001";
            }) == 3
            && older_literals.diagnostics.front().span.begin.column == 1U,
        "VHDL-2008 sized, decimal, and signed bit-string forms are rejected in "
        "older revisions");

    const std::string wide_bits(137U, '1');
    const auto wide_literal = parse_text(
        "vhdl2008-wide-literal.vhd",
        "architecture rtl of wide_literal is\n"
        "  signal value : bit_vector(136 downto 0);\n"
        "begin\n  value <= 137B\""
            + wide_bits + "\";\nend architecture;\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2008);
    require(
        wide_literal.ok() && wide_literal.design.units.front().concurrent_statements.front().value.decoded_string->size() == 137U && std::ranges::all_of(*wide_literal.design.units.front().concurrent_statements.front().value.decoded_string, [](const char bit) { return bit == '1'; }),
        "VHDL-2008 lexical selection preserves arbitrary bit-string width");

    constexpr std::array standards {
        VhdlStandard::Vhdl1987,
        VhdlStandard::Vhdl1993,
        VhdlStandard::Vhdl2000,
        VhdlStandard::Vhdl2002
    };
    constexpr std::array<std::string_view, 4> package_names {
        "std_logic_signed", "std_logic_unsigned", "std_logic_arith",
        "std_logic_misc"
    };
    for (const auto standard : standards) {
        for (const auto package_name : package_names) {
            const auto source = "package " + std::string { package_name } + " is\n" + "end package " + std::string { package_name } + ";\n";
            const auto package_lexed = lex(
                SourceText { "ieee/" + std::string { package_name } + ".vhd", source },
                Language::Vhdl2008,
                standard);
            require(
                package_lexed.ok() && package_lexed.tokens.size() == 8U && package_lexed.tokens.front().span.begin.line == 1U && package_lexed.tokens[3].span.begin.line == 2U && package_lexed.tokens.back().span.begin.line == 3U,
                "compiler-owned Synopsys package projections must be lexically "
                "stable in every older owning revision");
        }
    }
}

void test_vhdl_synopsys_declaration_profiles()
{
    constexpr std::array older_standards {
        VhdlStandard::Vhdl1987,
        VhdlStandard::Vhdl1993,
        VhdlStandard::Vhdl2000,
        VhdlStandard::Vhdl2002
    };
    const auto declaration_source = [](const std::string_view package,
                                        const std::string_view declarations) {
        return "library ieee;\nuse ieee.std_logic_1164.all;\npackage "
            + std::string { package } + " is\n" + std::string { declarations }
        + "\nend package " + std::string { package } + ";\n";
    };
    for (const auto standard : older_standards) {
        const auto arith = parse_text(
            "ieee/std_logic_arith.vhd",
            declaration_source(
                "std_logic_arith",
                "  type unsigned is array (natural range <>) of std_logic;\n"
                "  type signed is array (natural range <>) of std_logic;\n"
                "  subtype small_int is integer range 0 to 1;\n"
                "  function \"+\"(left : unsigned; right : signed) return signed;\n"
                "  function conv_signed(value : integer; size : integer) return signed;\n"
                "  function ext(value : std_logic_vector; size : integer) return std_logic_vector;"),
            Language::Vhdl2008,
            standard);
        require(
            arith.ok() && arith.design.units.size() == 1U
                && arith.design.units.front().type_aliases.size() == 3U
                && arith.design.units.front().functions.size() == 3U,
            "std_logic_arith historical types and overload profiles parse in "
            "every older VHDL revision");

        for (const auto package : { std::string_view { "std_logic_signed" },
                 std::string_view { "std_logic_unsigned" } }) {
            const auto vectors = parse_text(
                "ieee/" + std::string { package } + ".vhd",
                declaration_source(
                    package,
                    "  function \"+\"(left : std_logic_vector; right : integer) return std_logic_vector;\n"
                    "  function \"<\"(left : integer; right : std_logic_vector) return boolean;\n"
                    "  function shl(value : std_logic_vector; count : std_logic_vector) return std_logic_vector;\n"
                    "  function conv_integer(value : std_logic_vector) return integer;"),
                Language::Vhdl2008,
                standard);
            require(
                vectors.ok() && vectors.design.units.front().functions.size() == 4U,
                std::string { package }
                    + " preserves vector/integer, comparison, shift and "
                      "conversion declarations in every older revision");
        }

        const auto misc = parse_text(
            "ieee/std_logic_misc.vhd",
            declaration_source(
                "std_logic_misc",
                "  type strength is (strn_x01, strn_x0h, strn_xl1);\n"
                "  function strength_map(value : std_ulogic; level : strength) return std_logic;\n"
                "  function and_reduce(value : std_ulogic_vector) return ux01;\n"
                "  function xnor_reduce(value : std_ulogic_vector) return ux01;"),
            Language::Vhdl2008,
            standard);
        require(
            misc.ok() && misc.design.units.front().type_aliases.size() == 1U
                && misc.design.units.front().functions.size() == 3U,
            "std_logic_misc strength and reduction declarations parse in every "
            "older VHDL revision");
    }

    const auto incompatible = parse_text(
        "ieee/std_logic_misc-incompatible.vhd",
        declaration_source(
            "std_logic_misc",
            "  type protected_state is protected\n"
            "    procedure update;\n"
            "  end protected protected_state;"),
        Language::Vhdl2008,
        VhdlStandard::Vhdl1993);
    const auto incompatibility = std::ranges::find_if(
        incompatible.diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-003";
        });
    require(
        !incompatible.ok()
            && incompatibility != incompatible.diagnostics.end()
            && std::ranges::count_if(
                   incompatible.diagnostics,
                   [](const Diagnostic& diagnostic) {
                       return diagnostic.code == "FSIM-FE-VHSTD-003";
                   })
                == 1U
            && incompatibility->message
                == "a protected type declaration requires VHDL-2000 or later, "
                   "but this source selects VHDL-1993; select VHDL-2000 or "
                   "replace the protected object with an ordinary package-"
                   "managed declaration"
            && incompatibility->span.begin.line == 4U
            && incompatibility->span.begin.column == 27U,
        "an incompatible Synopsys declaration retains the exact unavailable-"
        "feature diagnostic and coordinate");
}

void test_vhdl_revision_exact_diagnostics()
{
    struct Expected {
        std::string_view name;
        std::string_view source;
        VhdlStandard standard;
        std::string_view message;
        std::uint32_t line;
        std::uint32_t column;
    };
    constexpr std::array corpus {
        Expected {
            "corpus-vhdl87-direct-entity",
            "architecture rtl of corpus_vhdl87 is begin\n"
            "  u : entity work.child;\nend architecture;\n",
            VhdlStandard::Vhdl1987,
            "direct entity instantiation requires VHDL-1993 or later, but "
            "this source selects VHDL-1987; declare and instantiate a "
            "component in VHDL-87",
            2U, 7U },
        Expected {
            "corpus-vhdl93-protected",
            "package corpus_vhdl93 is\n"
            "  type Guard is protected\n  end protected Guard;\n"
            "end package;\n",
            VhdlStandard::Vhdl1993,
            "a protected type declaration requires VHDL-2000 or later, but "
            "this source selects VHDL-1993; select VHDL-2000 or replace the "
            "protected object with an ordinary package-managed declaration",
            2U, 17U },
        Expected {
            "corpus-vhdl2000-context",
            "context corpus_vhdl2000 is\n  library ieee;\nend context;\n",
            VhdlStandard::Vhdl2000,
            "context declarations requires VHDL-2008 or later, but this "
            "source selects VHDL-2000; replace the declaration with explicit "
            "library and use clauses",
            1U, 1U },
        Expected {
            "corpus-vhdl2002-context",
            "context corpus_vhdl2002 is\n  library ieee;\nend context;\n",
            VhdlStandard::Vhdl2002,
            "context declarations requires VHDL-2008 or later, but this "
            "source selects VHDL-2002; replace the declaration with explicit "
            "library and use clauses",
            1U, 1U }
    };
    for (const auto& expected : corpus) {
        const auto parsed = parse_text(
            std::string { expected.name } + ".vhd",
            std::string { expected.source }, Language::Vhdl2008,
            expected.standard);
        const auto diagnostic = std::ranges::find_if(
            parsed.diagnostics, [](const Diagnostic& item) {
                return item.code == "FSIM-FE-VHSTD-003";
            });
        require(
            !parsed.ok() && diagnostic != parsed.diagnostics.end()
                && std::ranges::count_if(
                       parsed.diagnostics, [](const Diagnostic& item) {
                           return item.code == "FSIM-FE-VHSTD-003";
                       })
                    == 1U
                && diagnostic->message == expected.message && diagnostic->span.begin.line == expected.line && diagnostic->span.begin.column == expected.column,
            std::string { expected.name } + " must retain its exact revision diagnostic and coordinate");
    }
}

} // namespace fsim::tests::frontend
