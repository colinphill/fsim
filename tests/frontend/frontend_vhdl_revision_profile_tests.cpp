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
    const auto vhdl_2019_context_word = parse_text(
        "vhdl2019-context-word.vhd",
        "entity context is end entity context;",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        vhdl_2002_context_name.ok()
            && has_code(vhdl_2008_context_word.diagnostics, "FSIM-VHDL-LEX-002")
            && has_code(vhdl_2019_context_word.diagnostics, "FSIM-VHDL-LEX-002"),
        "VHDL-2008 reserved words remain legal identifiers in VHDL-2002");

    require(
        to_string(VhdlStandard::Vhdl2019) == "2019"
            && to_string(StandardRevision::Vhdl2019) == "vhdl-2019"
            && revision_string(StandardRevision::Vhdl2019) == "2019",
        "VHDL-2019 frontend and durable revision identities are distinct");

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
    const auto wide_literal_2019 = parse_text(
        "vhdl2019-wide-literal.vhd",
        "architecture rtl of wide_literal is\n"
        "  signal value : bit_vector(136 downto 0);\n"
        "begin\n  value <= 137B\""
            + wide_bits + "\";\nend architecture;\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        wide_literal_2019.ok()
            && wide_literal_2019.design.units.front()
                    .concurrent_statements.front()
                    .value.decoded_string->size()
                == 137U,
        "VHDL-2019 inherits the complete retained VHDL-2008 lexical surface");

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

void test_vhdl_2019_predefined_integer_range()
{
    const auto source = R"(
package wide_integer_types is
  subtype entire_integer is integer range
    -9223372036854775808 to 9223372036854775807;
  type indexed_words is array (integer range <>) of bit;
end package;

entity wide_integer_endpoint is
  port (
    signed_value : in integer;
    natural_value : in natural;
    positive_value : in positive
  );
end entity;
)";
    const auto parsed_2019 = parse_text(
        "vhdl2019-integer-range.vhd", source,
        Language::Vhdl2008, VhdlStandard::Vhdl2019);
    require(parsed_2019.ok(),
        "VHDL-2019 accepts the complete required signed 64-bit INTEGER range");
    const auto* package = parsed_2019.design.find(
        UnitKind::VhdlPackage, "wide_integer_types");
    const auto* entity = parsed_2019.design.find(
        UnitKind::VhdlEntity, "wide_integer_endpoint");
    require(package != nullptr && package->type_aliases.size() == 2U
            && package->type_aliases[0].type.width() == 64U
            && package->type_aliases[0].type.integer_range
            && package->type_aliases[0].type.integer_range->left
                == std::numeric_limits<std::int64_t>::min()
            && package->type_aliases[0].type.integer_range->right
                == std::numeric_limits<std::int64_t>::max()
            && package->type_aliases[1].type.vhdl_array
            && package->type_aliases[1].type.vhdl_array->index_base_range
            && package->type_aliases[1].type.vhdl_array->index_base_range->left
                == std::numeric_limits<std::int64_t>::min()
            && package->type_aliases[1].type.vhdl_array->index_base_range->right
                == std::numeric_limits<std::int64_t>::max(),
        "VHDL-2019 subtype and array-index metadata retain signed 64-bit bounds");
    require(entity != nullptr && entity->ports.size() == 3U
            && entity->ports[0].type.width() == 64U
            && entity->ports[0].type.integer_range->left
                == std::numeric_limits<std::int64_t>::min()
            && entity->ports[1].type.width() == 64U
            && entity->ports[1].type.integer_range->left == 0
            && entity->ports[2].type.width() == 64U
            && entity->ports[2].type.integer_range->left == 1,
        "all predefined VHDL-2019 integer subtypes share 64-bit storage");

    constexpr std::array older_standards {
        VhdlStandard::Vhdl1987,
        VhdlStandard::Vhdl1993,
        VhdlStandard::Vhdl2000,
        VhdlStandard::Vhdl2002,
        VhdlStandard::Vhdl2008
    };
    for (const auto standard : older_standards) {
        const auto parsed = parse_text(
            "vhdl-legacy-integer-range.vhd",
            "entity legacy_integer is port (value : in integer); end entity;",
            Language::Vhdl2008, standard);
        const auto* legacy = parsed.design.find(
            UnitKind::VhdlEntity, "legacy_integer");
        require(parsed.ok() && parsed.design.vhdl_profile_compatible
                && legacy != nullptr
                && legacy->ports.front().type.width() == 32U
                && legacy->ports.front().type.integer_range->left
                    == std::numeric_limits<std::int32_t>::min()
                && legacy->ports.front().type.integer_range->right
                    == std::numeric_limits<std::int32_t>::max(),
            "every older VHDL profile retains its portable 32-bit INTEGER range");
    }
}

void test_vhdl_2019_conditional_analysis()
{
    const auto selected = parse_text(
        "vhdl2019-conditional.vhd",
        "`if VHDL_VERSION = \"2008\" then\n"
        "this is not lexically valid @@@\n"
        "`elsif TOOL_TYPE = \"SIMULATION\" and TOOL_NAME = \"FSIM\" then\n"
        "entity selected is end entity selected;\n"
        "`else\n"
        "nor is this @@@\n"
        "`end if\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        selected.ok() && selected.design.units.size() == 1U
            && selected.design.units.front().name == "selected"
            && selected.design.units.front().span.begin.line == 4U,
        "VHDL-2019 conditional analysis selects one branch before lexing "
        "while retaining physical source coordinates");

    const auto nested = parse_text(
        "vhdl2019-conditional-nested.vhd",
        "`if not (TOOL_TYPE = \"SYNTHESIS\") then\n"
        "  `if TOOL_VENDOR = \"FSIM PROJECT\" xor TOOL_EDITION = \"OTHER\" then\n"
        "entity nested_selection is end entity nested_selection;\n"
        "  `end if\n"
        "`end if\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        nested.ok() && nested.design.units.size() == 1U
            && nested.design.units.front().name == "nested_selection",
        "nested conditional analysis supports deterministic simulator "
        "identifiers and Boolean composition");

    const auto older = parse_text(
        "vhdl2008-conditional.vhd",
        "`if VHDL_VERSION = \"2019\" then\n"
        "entity isolated is end entity isolated;\n"
        "`end if\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2008);
    require(
        !older.ok()
            && std::ranges::count_if(
                   older.diagnostics, [](const Diagnostic& diagnostic) {
                       return diagnostic.code == "FSIM-VHDL-CA-001";
                   })
                == 2U,
        "conditional analysis never leaks into retained pre-2019 profiles");

    const auto malformed = parse_text(
        "vhdl2019-conditional-malformed.vhd",
        "  `if TOOL_TYPE = \"SIMULATION\"\n"
        "entity malformed is end entity malformed;\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    const auto invalid_expression = std::ranges::find_if(
        malformed.diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-CA-002";
        });
    const auto unterminated = std::ranges::find_if(
        malformed.diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-CA-003";
        });
    require(
        !malformed.ok()
            && invalid_expression != malformed.diagnostics.end()
            && unterminated != malformed.diagnostics.end()
            && invalid_expression->span.begin.line == 1U
            && invalid_expression->span.begin.column == 3U
            && unterminated->span.begin.line == 1U
            && unterminated->span.begin.column == 3U,
        "malformed and unterminated conditional groups have stable codes and "
        "exact directive coordinates");

    const auto case_sensitive = parse_text(
        "vhdl2019-conditional-case.vhd",
        "`if TOOL_TYPE = \"simulation\" then\n"
        "invalid inactive text @@@\n"
        "`else\n"
        "entity case_sensitive is end entity case_sensitive;\n"
        "`end if\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        case_sensitive.ok()
            && case_sensitive.design.units.front().name == "case_sensitive",
        "conditional identifier names are case-insensitive while their string "
        "values remain case-sensitive");

    const auto lexical_context = parse_text(
        "vhdl2019-conditional-context.vhd",
        "/*\n"
        "`if TOOL_TYPE = \"SYNTHESIS\" then\n"
        "*/\n"
        "`if TOOL_TYPE = \"SIMULATION\" then -- selected for fsim\n"
        "entity lexical_context is end entity lexical_context;\n"
        "`end if -- selected for fsim\n",
        Language::Vhdl2008,
        VhdlStandard::Vhdl2019);
    require(
        lexical_context.ok()
            && lexical_context.design.units.front().name == "lexical_context"
            && lexical_context.design.units.front().span.begin.line == 5U,
        "directive-like text inside block comments is ignored and ordinary "
        "VHDL line comments may follow directives");

    std::string deeply_nested;
    for (std::size_t depth = 0U; depth < 129U; ++depth) {
        deeply_nested += "`if TOOL_TYPE = \"SIMULATION\" then\n";
    }
    deeply_nested += "entity bounded is end entity bounded;\n";
    for (std::size_t depth = 0U; depth < 129U; ++depth) {
        deeply_nested += "`end if\n";
    }
    const auto bounded = parse_text(
        "vhdl2019-conditional-depth.vhd", deeply_nested,
        Language::Vhdl2008, VhdlStandard::Vhdl2019);
    require(
        !bounded.ok() && bounded.design.units.size() == 1U
            && std::ranges::count_if(
                   bounded.diagnostics, [](const Diagnostic& diagnostic) {
                       return diagnostic.code == "FSIM-VHDL-CA-004";
                   })
                == 1U
            && std::ranges::none_of(bounded.diagnostics, [](const Diagnostic& diagnostic) {
                   return diagnostic.code == "FSIM-VHDL-CA-003";
               }),
        "conditional analysis rejects excess nesting without losing group "
        "synchronization or growing its frame stack past the bound");
}

void test_vhdl_2019_profile_isolation()
{
    struct ProfileCase {
        std::string_view name;
        std::string_view source;
        std::string_view diagnostic;
    };
    constexpr std::array cases {
        ProfileCase {
            "protected-generic",
            R"(package P is
  type Guard is protected
    generic (type Element_T);
  end protected Guard;
end package;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "unspecified-type",
            "entity E is generic (type Element_T is private); end entity;",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "mode-view",
            R"(package P is
  type Pair_T is record Ready, Valid : bit; end record;
  view Producer of Pair_T is Ready : in; Valid : out; end view;
end package;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "view-interface",
            "entity E is port (Bus : view Producer); end entity;",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "conditional-expression",
            R"(architecture rtl of E is
  constant Value : integer := 1 when true else 2;
begin end architecture;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "result-subtype-identifier",
            R"(package P is
  function Make return Result of bit_vector;
end package;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "sequential-block",
            R"(architecture rtl of E is begin
  process begin Region : block begin null; end block; wait; end process;
end architecture;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "index-attribute",
            R"(package P is
  type Vector_T is array (integer range <>) of bit;
  subtype Index_T is Vector_T'index;
end package;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "reflect-attribute",
            R"(architecture rtl of E is
  signal Value : integer;
begin Value <= integer'reflect; end architecture;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "converse-attribute",
            R"(package P is
  alias Consumer is Producer'converse;
end package;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "object-attribute-shorthand",
            R"(architecture rtl of E is
  type State_T is (Idle, Busy);
  signal State : State_T;
  signal Position : integer;
begin Position <= State'pos; end architecture;)",
            "FSIM-FE-VHSTD-003" },
        ProfileCase {
            "conditional-analysis",
            "`if VHDL_VERSION = \"2019\" then\n"
            "entity E is end entity;\n"
            "`end if\n",
            "FSIM-VHDL-CA-001" }
    };
    constexpr std::array older_standards {
        VhdlStandard::Vhdl1987,
        VhdlStandard::Vhdl1993,
        VhdlStandard::Vhdl2000,
        VhdlStandard::Vhdl2002,
        VhdlStandard::Vhdl2008
    };
    for (const auto& profile_case : cases) {
        for (const auto standard : older_standards) {
            const auto parsed = parse_text(
                "vhdl-profile-" + std::string { profile_case.name } + ".vhd",
                std::string { profile_case.source }, Language::Vhdl2008,
                standard);
            require(
                !parsed.ok() && !parsed.design.vhdl_profile_compatible
                    && std::ranges::any_of(
                        parsed.diagnostics,
                        [&](const Diagnostic& diagnostic) {
                            return diagnostic.code == profile_case.diagnostic;
                        }),
                std::string { profile_case.name }
                    + " must be rejected without executable recovery in every "
                      "retained pre-2019 profile");
        }
    }

    const auto access_2019 = parse_text(
        "vhdl2019-access-policy.vhd",
        "package P is type Pointer_T is access integer; end package;",
        Language::Vhdl2008, VhdlStandard::Vhdl2019);
    require(
        access_2019.ok() && access_2019.design.vhdl_profile_compatible
            && access_2019.design.units.front().type_aliases.front()
                   .type.vhdl_access->reclaim_when_unreachable
            && !access_2019.design.units.front().type_aliases.front()
                    .type.vhdl_access->deallocate_releases_storage,
        "VHDL-2019 selects automatic access-storage reclamation");
    for (const auto standard : older_standards) {
        const auto access = parse_text(
            "vhdl-legacy-access-policy.vhd",
            "package P is type Pointer_T is access integer; end package;",
            Language::Vhdl2008, standard);
        const auto& info = *access.design.units.front()
                                .type_aliases.front().type.vhdl_access;
        require(
            access.ok() && access.design.vhdl_profile_compatible
                && info.deallocate_releases_storage
                && !info.reclaim_when_unreachable
                && info.simulation_lifetime,
            "every older profile retains its pre-2019 access-storage policy");
    }
}

void test_vhdl_2019_frontend_recovery_corpus()
{
    struct RecoveryCase {
        std::string_view feature;
        std::string_view source;
        std::string_view diagnostic;
        std::string_view recovered_entity;
    };
    constexpr std::array cases {
        RecoveryCase {
            "protected-members",
            R"(package Bad_Protected is
  type Guard is protected
    private nonsense;
  end protected Guard;
end package;
entity Recovered_Protected is end entity;)",
            "FSIM-VHDL-PARSE-245", "recovered_protected" },
        RecoveryCase {
            "unspecified-type",
            R"(entity Bad_Unspecified is
  generic (type Element_T is nonsense);
end entity;
entity Recovered_Unspecified is end entity;)",
            "FSIM-VHDL-PARSE-286", "recovered_unspecified" },
        RecoveryCase {
            "mode-view",
            R"(package Bad_Mode_View is
  type Pair_T is record Left, Right : bit; end record;
  view Broken of Pair_T is
    Left : sideways;
  end view Broken;
end package;
entity Recovered_Mode_View is end entity;)",
            "FSIM-VHDL-PARSE-287", "recovered_mode_view" },
        RecoveryCase {
            "view-interface",
            R"(entity Bad_View_Interface is
  port (Bus : view (Producer of Pair_T);
end entity;
entity Recovered_View_Interface is end entity;)",
            "FSIM-VHDL-PARSE-288", "recovered_view_interface" },
        RecoveryCase {
            "conditional-expression",
            R"(package Bad_Conditional is
  constant Value : integer := 1 when true;
end package;
entity Recovered_Conditional is end entity;)",
            "FSIM-VHDL-PARSE-115", "recovered_conditional" },
        RecoveryCase {
            "result-subtype-identifier",
            R"(package Bad_Result is
  function Make(Result : integer) return Result of integer;
end package;
entity Recovered_Result is end entity;)",
            "FSIM-VHDL-SEM-112", "recovered_result" },
        RecoveryCase {
            "sequential-block",
            R"(entity Block_Owner is end entity;
architecture rtl of Block_Owner is begin
  process begin
    Region : block
      variable Value integer;
    begin null; end block Region;
    wait;
  end process;
end architecture;
entity Recovered_Block is end entity;)",
            "FSIM-VHDL-PARSE-289", "recovered_block" },
        RecoveryCase {
            "subtype-attribute",
            R"(package Bad_Subtype_Attribute is
  type Vector_T is array (integer range <>) of bit;
  subtype Index_T is Vector_T'index(1, 2);
end package;
entity Recovered_Subtype_Attribute is end entity;)",
            "FSIM-VHDL-SEM-113", "recovered_subtype_attribute" },
        RecoveryCase {
            "converse-view",
            R"(package Bad_Converse is
  alias Consumer is Producer'converse(1);
end package;
entity Recovered_Converse is end entity;)",
            "FSIM-VHDL-SEM-113", "recovered_converse" },
        RecoveryCase {
            "reflection",
            R"(architecture rtl of Bad_Reflection is
  signal Value : integer;
begin Value <= integer'reflect(1); end architecture;
entity Recovered_Reflection is end entity;)",
            "FSIM-VHDL-SEM-113", "recovered_reflection" },
        RecoveryCase {
            "conditional-analysis",
            R"(`if TOOL_TYPE = "SIMULATION" then
entity Recovered_Conditional_Analysis is end entity;)",
            "FSIM-VHDL-CA-003", "recovered_conditional_analysis" }
    };

    for (const auto& recovery_case : cases) {
        const auto parsed = parse_text(
            "vhdl2019-recovery-" + std::string { recovery_case.feature }
                + ".vhd",
            std::string { recovery_case.source }, Language::Vhdl2008,
            VhdlStandard::Vhdl2019);
        require(
            !parsed.ok() && parsed.design.vhdl_profile_compatible
                && std::ranges::any_of(
                    parsed.diagnostics,
                    [&](const Diagnostic& diagnostic) {
                        return diagnostic.code == recovery_case.diagnostic;
                    })
                && parsed.design.find(
                       UnitKind::VhdlEntity,
                       recovery_case.recovered_entity)
                    != nullptr,
            std::string { recovery_case.feature }
                + " must diagnose locally, retain the VHDL-2019 profile, and "
                  "resume at the following independent design unit");
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
