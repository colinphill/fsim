// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

void test_vhdl_2008_lexical_surface()
{
    using namespace fsim::frontend;
    const auto parsed = parse_text("vhdl-2008-lexical.vhd",
        R"(
/* VHDL-2008 outer comment
   /* nested comment */
*/
entity \Lexical\\Name\ is
end entity;
architecture \RTL\\Body\ of \Lexical\\Name\ is
  signal bits : bit_vector(7 downto 0);
begin
  bits <= 8X"A5";
end architecture;
)",
        Language::Vhdl2008);
    require(parsed.ok(), "VHDL-2008 lexical surface must parse");
    require(parsed.design.units.size() == 2 && parsed.design.units[0].name == "Lexical\\Name" && parsed.design.units[1].name == "RTL\\Body" && parsed.design.units[1].primary_name == "Lexical\\Name",
        "extended identifiers retain case and decode doubled backslashes");
    require(parsed.design.units[1].concurrent_statements.size() == 1 && parsed.design.units[1].concurrent_statements[0].value.kind == ExpressionKind::StringLiteral && parsed.design.units[1].concurrent_statements[0].value.text == "\"10100101\"" && parsed.design.units[1].concurrent_statements[0].value.decoded_string == "10100101",
        "sized hexadecimal bit-string literal must normalize exactly");

    const auto bit_strings = parse_text("vhdl-2008-bit-strings.vhd",
        R"(
entity bit_strings is end entity;
architecture rtl of bit_strings is
  signal sx : bit_vector(6 downto 0);
  signal ux : bit_vector(8 downto 0);
  signal decimal_bits : bit_vector(7 downto 0);
  signal quoted : string(1 to 3);
  signal apostrophe : character;
begin
  sx <= 7SX"-X";
  ux <= 9UX"ZZ";
  decimal_bits <= D"165";
  quoted <= "a""b";
  apostrophe <= ''';
end architecture;
)",
        Language::Vhdl2008);
    require(bit_strings.ok(), "complete VHDL-2008 literal forms must parse");
    const auto& literal_statements = bit_strings.design.units[1].concurrent_statements;
    require(
        literal_statements.size() == 5 && literal_statements[0].value.decoded_string == "---XXXX" && literal_statements[1].value.decoded_string == "0ZZZZZZZZ" && literal_statements[2].value.decoded_string == "10100101" && literal_statements[3].value.decoded_string == "a\"b" && literal_statements[4].value.text == "'''",
        "signed, unsigned, decimal, string, and character literals normalize");

    const auto malformed_identifiers = lex(SourceText { "malformed-identifiers.vhd",
                                               "_leading adjacent__underscore trailing_ \\\\" },
        Language::Vhdl2008);
    const auto count_code = [](const auto& diagnostics,
                                const std::string_view code) {
        return std::count_if(
            diagnostics.begin(), diagnostics.end(),
            [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    };
    require(
        count_code(malformed_identifiers.diagnostics, "FSIM-FE-LEX-005") == 3 && count_code(malformed_identifiers.diagnostics, "FSIM-FE-LEX-006") == 1,
        "malformed VHDL basic and extended identifiers need exact diagnostics");

    const auto recovery = parse_text("lexical-recovery.vhd",
        R"(
library work.bad;
use lone;
context work.too.many;
entity recovered is
end entity;
architecture rtl of recovered is
  signal bits : bit_vector(3 downto 0);
begin
  bits <= X"GG";
end architecture;
library work;
)",
        Language::Vhdl2008);
    require(!recovery.ok() && count_code(recovery.diagnostics, "FSIM-VHDL-PARSE-044") == 3 && count_code(recovery.diagnostics, "FSIM-VHDL-PARSE-263") == 1 && count_code(recovery.diagnostics, "FSIM-VHDL-PARSE-264") == 1 && recovery.design.find(UnitKind::VhdlEntity, "recovered") && recovery.design.find(UnitKind::VhdlArchitecture, "rtl"),
        "lexical and context recovery must retain later library units");

    const auto malformed_bit_strings = parse_text("malformed-bit-strings.vhd",
        R"(
entity malformed_bits is end entity;
architecture rtl of malformed_bits is
  signal bits : bit_vector(6 downto 0);
begin
  bits <= 7UX"8F";
  bits <= 7SX"8F";
  bits <= 7 X"7F";
  bits <= 7X "7F";
  bits <= 7X"_7F";
  bits <= 7UD"1";
end architecture;
)",
        Language::Vhdl2008);
    require(
        count_code(malformed_bit_strings.diagnostics, "FSIM-VHDL-PARSE-263") == 6,
        "every malformed VHDL-2008 bit-string form needs one diagnostic");

    const auto wide_forms = parse_text("wide-bit-strings.vhd",
        R"(
entity wide_bits is end entity;
architecture rtl of wide_bits is
  signal bits : bit_vector(256 downto 0);
begin
  bits <= 257X"1";
  bits <= 257SX"F";
  bits <= D"18446744073709551616";
  bits <= B"";
  bits <= 8B"";
  bits <= 0B"0";
  bits <= 65UX"00000000000000001";
  bits <= 65SX"FFFFFFFFFFFFFFFFF";
  bits <= D"340282366920938463463374607431768211456";
end architecture;
)",
        Language::Vhdl2008);
    require(wide_forms.ok(), "arbitrary-width VHDL bit strings must parse");
    const auto& wide_statements = wide_forms.design.units[1].concurrent_statements;
    require(
        wide_statements.size() == 9 && wide_statements[0].value.decoded_string->size() == 257 && wide_statements[0].value.decoded_string->back() == '1' && std::ranges::all_of(*wide_statements[1].value.decoded_string, [](const char bit) { return bit == '1'; }) && *wide_statements[2].value.decoded_string == "1000000000000000000000000000000000000000000000000000000000000000"
                                                                                                                                                                                                                                                                                                                   "0"
            && wide_statements[3].value.decoded_string->empty() && *wide_statements[4].value.decoded_string == "00000000" && wide_statements[5].value.decoded_string->empty() && wide_statements[6].value.decoded_string->size() == 65 && wide_statements[6].value.decoded_string->back() == '1' && std::ranges::all_of(*wide_statements[7].value.decoded_string, [](const char bit) { return bit == '1'; }) && wide_statements[8].value.decoded_string->size() == 129 && wide_statements[8].value.decoded_string->front() == '1' && std::ranges::all_of(wide_statements[8].value.decoded_string->begin() + 1, wide_statements[8].value.decoded_string->end(), [](const char bit) { return bit == '0'; }),
        "sizing, signed extension, null values, and arbitrary-precision decimal "
        "conversion must preserve exact VHDL literal values");

    const auto adjustment_failures = parse_text("bit-string-adjustment-failures.vhd",
        R"(
entity bad_adjustment is end entity;
architecture rtl of bad_adjustment is
  signal bits : bit_vector(0 downto 0);
begin
  bits <= 0B"1";
  bits <= 0SX"0";
  bits <= 65UX"80000000000000001";
  bits <= 999999999999999999999999999999B"0";
end architecture;
)",
        Language::Vhdl2008);
    require(
        count_code(adjustment_failures.diagnostics, "FSIM-VHDL-PARSE-263") == 4,
        "lossy and non-host-addressable VHDL bit-string adjustment must fail");

    const auto long_literal = parse_text("long-bit-string.vhd",
        "entity long_bits is end entity;\n"
        "architecture rtl of long_bits is\n"
        "  signal bits : bit_vector(4096 downto 0);\n"
        "begin\n  bits <= B\""
            + std::string(4096, '0') + "1\";\n"
                                       "end architecture;\n",
        Language::Vhdl2008);
    require(
        count_code(long_literal.diagnostics, "FSIM-VHDL-PARSE-263") == 0 && long_literal.design.units.size() == 2 && long_literal.design.units[1].concurrent_statements.size() == 1 && long_literal.design.units[1].concurrent_statements[0].value.decoded_string.has_value() && long_literal.design.units[1].concurrent_statements[0].value.decoded_string->size() == 4097 && long_literal.design.units[1].concurrent_statements[0].value.decoded_string->back() == '1',
        "source-determined VHDL bit-string length must not have a semantic cap");

    std::string over_nested;
    for (std::size_t depth = 0; depth < 65; ++depth) {
        over_nested += "/*";
    }
    for (std::size_t depth = 0; depth < 65; ++depth) {
        over_nested += "*/";
    }
    const auto bounded = lex(SourceText { "nested-comments.vhd", std::move(over_nested) },
        Language::Vhdl2008);
    require(count_code(bounded.diagnostics, "FSIM-FE-LEX-007") == 1,
        "VHDL block-comment nesting must have one bounded diagnostic");
    const auto unterminated = lex(
        SourceText {
            "unterminated-vhdl-lexemes.vhd",
            "/* comment\n\\extended\n\"string\nentity recovered is end entity;" },
        Language::Vhdl2008);
    require(count_code(unterminated.diagnostics, "FSIM-FE-LEX-002") == 1,
        "an unterminated VHDL block comment needs one exact diagnostic");
    const auto recoverable_unterminated = lex(SourceText { "recoverable-vhdl-lexemes.vhd",
                                                  "\\extended\n\"string\nentity recovered is end entity;" },
        Language::Vhdl2008);
    require(
        count_code(recoverable_unterminated.diagnostics, "FSIM-FE-LEX-003") == 1 && count_code(recoverable_unterminated.diagnostics, "FSIM-FE-LEX-004") == 1 && std::ranges::any_of(recoverable_unterminated.tokens, [](const Token& token) { return token.text == "entity"; }),
        "unterminated extended and string literals diagnose and recover by line");

    const auto delimiters = lex(SourceText { "vhdl-2008-delimiters.vhd",
                                    "?? ?= ?/= ?< ?<= ?> ?>= <> << >> := => ** /= <= >=" },
        Language::Vhdl2008);
    const std::vector<TokenKind> expected_delimiters {
        TokenKind::Question, TokenKind::Question, TokenKind::Question,
        TokenKind::Assign, TokenKind::Question, TokenKind::NotEqual,
        TokenKind::Question, TokenKind::Less, TokenKind::Question,
        TokenKind::LessEqual, TokenKind::Question, TokenKind::Greater,
        TokenKind::Question, TokenKind::GreaterEqual, TokenKind::Less,
        TokenKind::Greater, TokenKind::ShiftLeft, TokenKind::ShiftRight,
        TokenKind::ColonEqual, TokenKind::Arrow, TokenKind::Power,
        TokenKind::NotEqual, TokenKind::LessEqual, TokenKind::GreaterEqual,
        TokenKind::EndOfFile
    };
    require(delimiters.ok() && delimiters.tokens.size() == expected_delimiters.size() && std::ranges::equal(delimiters.tokens, expected_delimiters, { }, &Token::kind),
        "VHDL-2008 simple, compound, matching, box, and external delimiters "
        "must tokenize without ambiguity");
}

} // namespace

int main()
{
    using namespace fsim::frontend;
    using namespace fsim::tests::frontend;
    try {
        test_vhdl_2008_lexical_surface();
        require(PackedRange { std::numeric_limits<std::int64_t>::max(),
                    std::numeric_limits<std::int64_t>::min(), true }
                    .width()
                == 0,
            "unrepresentable 2^64-element range must not overflow");
        test_vhdl_vertical_slice();
        test_vhdl_falling_edge_guard();
        test_vhdl_instance_diagnostics();
        test_vhdl_generics();
        test_vhdl_record_types();
        test_vhdl_select_and_concatenation_expressions();
        test_signed_type_and_expression_nodes();
        test_vhdl_runtime_integer_nodes();
        test_vhdl_subtype_declarations();
        test_vhdl_array_type_declarations();
        test_vhdl_access_protected_physical_hir();
        test_vhdl_revision_declaration_profiles();
        test_vhdl_incomplete_type_declarations();
        test_vhdl_nested_composite_hir();
        test_vhdl_enumeration_declarations();
        test_vhdl_enumeration_attributes();
        test_vhdl_enumeration_subtype_ranges();
        test_vhdl_case_generate_enumeration_choices();
        test_exponentiation_expression_nodes();
        test_systemverilog_procedural_updates();
        test_systemverilog_final_procedures();
        test_verilog_stop_task();
        test_vhdl_conditional_assignments();
        test_vhdl_array_attributes();
        test_vhdl_user_attribute_and_group_declarations();
        test_vhdl_selected_assignments();
        test_vhdl_delay_mechanisms();
        test_vhdl_ordered_waveforms();
        test_vhdl_batch119_retained_surface();
        test_vhdl_psl_declaration_ownership();
        test_vhdl_psl_temporal_analysis();
        test_vhdl_case_statements();
        test_vhdl_revision_lexical_profiles();
        test_vhdl_revision_expression_profiles();
        test_vhdl_synopsys_declaration_profiles();
        test_vhdl_revision_statement_profiles();
        test_vhdl_revision_exact_diagnostics();
        test_vhdl_sequential_for_loops();
        test_systemverilog_vertical_slice();
        test_systemverilog_preprocessor();
        test_systemverilog_uvm_macro_surface();
        test_systemverilog_public_conformance_frontend();
        test_msvc_debug_frontend_portability();
        test_systemverilog_line_directive();
        test_non_ansi_verilog_ports();
        test_diagnostics_and_spans();
        test_vhdl_context_diagnostics();
        test_vhdl_package_constants();
        test_ignored_initializers_are_rejected();
        test_duplicate_declarations_are_rejected();
        test_systemverilog_timescale_context();
        test_systemverilog_time_declarations();
        test_systemverilog_delay_triples();
        test_systemverilog_procedural_assignment_controls();
        test_systemverilog_compiler_directives();
        test_systemverilog_parameters();
        test_systemverilog_packages();
        test_systemverilog_interfaces();
        test_systemverilog_hierarchy_declarations();
        test_systemverilog_programs();
        test_systemverilog_clocking_blocks();
        test_systemverilog_assertion_declarations();
        test_systemverilog_covergroup_declarations();
        test_systemverilog_dpi_declaration_ownership();
        test_vhdl_function_declarations();
        test_vhdl_procedure_declarations();
        test_vhdl_generic_subprogram_declarations();
        test_vhdl_component_declarations();
        test_vhdl_configurations();
        test_vhdl_package_generics();
        test_systemverilog_function_declarations();
        test_systemverilog_real_time_declarations();
        test_systemverilog_chandle_declarations();
        test_systemverilog_task_declarations();
        test_systemverilog_callable_closure_declarations();
        test_systemverilog_class_declarations();
        test_immediate_assertions();
        test_vhdl_literal_report();
        test_process_variable_declarations();
        test_systemverilog_procedural_block_scopes();
        test_procedural_wait_statements();
        test_wildcard_and_always_comb_processes();
        test_systemverilog_case_statements();
        test_systemverilog_procedural_for_loops();
        test_verilog_repeat_statements();
        test_runtime_loop_statements();
        test_loop_control_statements();
        test_systemverilog_do_while_statements();
        test_fork_process_statements();
        test_systemverilog_conditional_expression();
        test_systemverilog_comparison_expressions();
        test_systemverilog_membership_expressions();
        test_systemverilog_arithmetic_expressions();
        test_gate_primitives();
        test_verilog_strength_and_switch_primitives();
        test_verilog_specify_blocks();
        test_verilog_udp_declarations();
        test_systemverilog_select_and_concatenation_expressions();
        test_conditional_statement_trees();
        test_conditional_generate_hierarchy();
        test_verilog_defparam_declarations();
        test_systemverilog_named_events();
        test_verilog_literal_display();
        test_systemverilog_random_functions();
        test_systemverilog_text_files();
        test_systemverilog_containers();
        std::cout << "frontend tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "frontend test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
