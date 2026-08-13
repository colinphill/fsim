// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_case_matches() {
    const auto parsed = fsim::frontend::parse_text(
        "case_matches.sv",
        R"(
module case_matches;
  typedef struct packed {
    logic [2:0] code;
    logic valid;
  } payload_t;
  typedef union tagged packed {
    payload_t packet;
    logic [3:0] raw;
  } tagged_t;
  logic [2:0] selector;
  logic [3:0] exact_result;
  logic [3:0] wildcard_result;
  logic [3:0] unknown_exact;
  logic [3:0] unique_multiple;
  logic [3:0] unique_missing;
  logic [3:0] unique0_missing;
  logic [3:0] priority_missing;
  logic [3:0] priority_multiple;
  logic [3:0] folded_result;
  logic [3:0] sized_result;
  logic [3:0] signedness_result;
  logic [3:0] concat_result;
  logic [3:0] wide_result;
  logic [3:0] qualified_sized_result;
  logic folded_short_circuit;
  logic direct_short_circuit;
  logic [136:0] direct_conditional;
  logic [31:0] direct_conditional_bits;
  logic [136:0] function_conditional;
  logic [2:0] binding_result;
  logic [2:0] qualified_binding_result;
  logic [2:0] folded_binding_result;
  tagged_t tagged_value;
  payload_t structured_value;
  logic [2:0] tagged_pattern_result;
  logic [2:0] structured_pattern_result;
  logic [2:0] qualified_tagged_pattern_result;
  logic [3:0] guarded_result;
  logic [3:0] qualified_guarded_result;
  logic [31:0] guard_calls;
  logic [31:0] guard_call_result;

  function automatic logic guard(input logic result);
    guard_calls = guard_calls + 1;
    return result;
  endfunction

  function automatic logic [3:0] choose(input logic [2:0] value);
    case (value) matches
      4'b0001 &&& value == 3'b001: return 4'h1;
      .*: return 4'h2;
    endcase
  endfunction
  localparam logic [3:0] FOLDED = choose(3'b001);
  function automatic logic shorted();
    return 1'b0 && (1 / 0);
  endfunction
  localparam logic FOLDED_SHORT = shorted();
  localparam logic DIRECT_SHORT = 1'b1 || (1 / 0);
  localparam DIRECT_CONDITIONAL =
      1'b1 ? 8'h5a : (137'h1 / 0);
  localparam logic [31:0] DIRECT_CONDITIONAL_BITS =
      $bits(DIRECT_CONDITIONAL);
  function automatic logic [136:0] lazy_conditional();
    return 1'b1 ? 8'h5a : (137'h1 / 0);
  endfunction
  localparam logic [136:0] FUNCTION_CONDITIONAL = lazy_conditional();
  function automatic logic [2:0] bind_choose(input logic [2:0] value);
    case (value) matches
      .captured &&& captured == 3'b001: return captured;
      default: return 3'b111;
    endcase
  endfunction
  localparam logic [2:0] FOLDED_BINDING = bind_choose(3'b001);

  initial begin
    selector = 3'b001;
    tagged_value = tagged packet '{code: 3'b101, valid: 1'b1};
    structured_value = '{code: 3'b110, valid: 1'b1};
    guard_calls = 0;
    folded_result = FOLDED;
    folded_short_circuit = FOLDED_SHORT;
    direct_short_circuit = DIRECT_SHORT;
    direct_conditional = DIRECT_CONDITIONAL;
    direct_conditional_bits = DIRECT_CONDITIONAL_BITS;
    function_conditional = FUNCTION_CONDITIONAL;
    folded_binding_result = FOLDED_BINDING;
    case (selector) matches
      3'b001: exact_result = 4'h1;
      .*: exact_result = 4'h2;
    endcase
    case (3'b010) matches
      3'b001: wildcard_result = 4'h3;
      .*: wildcard_result = 4'h4;
      default: wildcard_result = 4'hf;
    endcase
    case (3'bx01) matches
      3'b001: unknown_exact = 4'h5;
      3'bx01: unknown_exact = 4'h6;
      default: unknown_exact = 4'hf;
    endcase
    unique case (selector) matches
      3'b001: unique_multiple = 4'h7;
      .*: unique_multiple = 4'h8;
    endcase
    unique case (selector) matches
      3'b010: unique_missing = 4'h9;
    endcase
    unique0 case (selector) matches
      3'b010: unique0_missing = 4'ha;
    endcase
    priority case (selector) matches
      3'b010: priority_missing = 4'hb;
    endcase
    priority case (selector) matches
      3'b001: priority_multiple = 4'hc;
      .*: priority_multiple = 4'hd;
    endcase
    case (3'b001) matches
      4'b0001: sized_result = 4'h1;
      default: sized_result = 4'hf;
    endcase
    case (-3'sd1) matches
      4'b0111: signedness_result = 4'h2;
      default: signedness_result = 4'hf;
    endcase
    case ({2'b00, 3'b001}) matches
      5'b00001: concat_result = 4'h3;
      default: concat_result = 4'hf;
    endcase
    case (137'h1) matches
      8'h01: wide_result = 4'h4;
      default: wide_result = 4'hf;
    endcase
    unique case (3'b001) matches
      4'b0001: qualified_sized_result = 4'h5;
      default: qualified_sized_result = 4'hf;
    endcase
    case (selector) matches
      3'b010 &&& guard(1'b1): guarded_result = 4'h6;
      3'b001 &&& guard(1'b0): guarded_result = 4'h7;
      .* &&& guard(1'b1): guarded_result = 4'h8;
      default: guarded_result = 4'hf;
    endcase
    guard_call_result = guard_calls;
    unique case (selector) matches
      3'b001 &&& 1'b0: qualified_guarded_result = 4'h9;
      3'b001 &&& 1'b1: qualified_guarded_result = 4'ha;
      default: qualified_guarded_result = 4'hf;
    endcase
    case (selector) matches
      .captured &&& captured[0]: binding_result = captured;
      default: binding_result = 3'b111;
    endcase
    unique case (selector) matches
      .first &&& first == 3'b000: qualified_binding_result = first;
      .second &&& second == 3'b001: qualified_binding_result = second;
      default: qualified_binding_result = 3'b111;
    endcase
    case (tagged_value) matches
      tagged packet '{code: .code, valid: 1'b1} &&& code == 3'b101:
        tagged_pattern_result = code;
      default: tagged_pattern_result = 3'b111;
    endcase
    case (structured_value) matches
      '{valid: 1'b1, code: .captured}:
        structured_pattern_result = captured;
      default: structured_pattern_result = 3'b111;
    endcase
    unique case (tagged_value) matches
      tagged raw .raw_value &&& raw_value == 4'h0:
        qualified_tagged_pattern_result = raw_value[2:0];
      tagged packet '{valid: 1'b1, code: .code}:
        qualified_tagged_pattern_result = code;
      default: qualified_tagged_pattern_result = 3'b111;
    endcase
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.case_matches");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
  assert(elaborated.ok());

  struct Warning {
    std::string message;
    fsim::runtime::simir::SourceLocation source;
  };
  std::vector<Warning> warnings;
  auto interpreter = elaborated.design->create_interpreter();
  interpreter->set_report_hook(
      [&warnings](
          const fsim::runtime::simir::ProcessId,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation& source,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
        assert(severity
               == fsim::runtime::simir::AssertionSeverity::warning);
        warnings.push_back(Warning{std::string{message}, source});
      });
  (void)interpreter->run();
  const auto value = [&](const std::string_view name) {
    const auto signal = elaborated.design->find_signal(name);
    assert(signal);
    return interpreter->signal_value(*signal).to_msb_string();
  };
  assert(value("exact_result") == "0001");
  assert(value("wildcard_result") == "0100");
  assert(value("unknown_exact") == "0110");
  assert(value("unique_multiple") == "0111");
  assert(value("unique_missing") == "XXXX");
  assert(value("unique0_missing") == "XXXX");
  assert(value("priority_missing") == "XXXX");
  assert(value("priority_multiple") == "1100");
  assert(value("folded_result") == "0001");
  assert(value("sized_result") == "0001");
  assert(value("signedness_result") == "0010");
  assert(value("concat_result") == "0011");
  assert(value("wide_result") == "0100");
  assert(value("qualified_sized_result") == "0101");
  assert(value("folded_short_circuit") == "0");
  assert(value("direct_short_circuit") == "1");
  assert(value("direct_conditional").size() == 137U);
  assert(value("direct_conditional").ends_with("01011010"));
  assert(std::ranges::count(value("direct_conditional"), '1') == 4);
  assert(value("direct_conditional_bits")
      == "00000000000000000000000010001001");
  assert(value("function_conditional").size() == 137U);
  assert(value("function_conditional").ends_with("01011010"));
  assert(std::ranges::count(value("function_conditional"), '1') == 4);
  assert(value("binding_result") == "001");
  assert(value("qualified_binding_result") == "001");
  assert(value("folded_binding_result") == "001");
  assert(value("tagged_pattern_result") == "101");
  assert(value("structured_pattern_result") == "110");
  assert(value("qualified_tagged_pattern_result") == "101");
  assert(value("guarded_result") == "1000");
  assert(value("qualified_guarded_result") == "1010");
  assert(value("guard_call_result")
      == "00000000000000000000000000000010");
  const std::vector<std::string> expected{
      "unique case has multiple matching items",
      "unique case has no matching item",
      "priority case has no matching item"};
  std::vector<std::string> observed;
  for (const auto& warning : warnings) {
    observed.push_back(warning.message);
    assert(warning.source.path == "case_matches.sv");
    assert(warning.source.line != 0);
  }
  assert(observed == expected);

  const auto invalid = fsim::frontend::parse_text(
      "invalid_case_matches.sv",
      R"(
module invalid_case_matches;
  typedef struct packed {
    logic left;
    logic right;
  } pair_t;
  logic [2:0] selector;
  logic [3:0] result;
  pair_t pair;
  string text;
  initial case (selector) matches
    selector: result = 4'h1;
  endcase
  initial case (text) matches
    8'h00: result = 4'h4;
  endcase
  initial case (selector) matches
    tagged missing .value: result = 4'h5;
  endcase
  initial case (selector) matches
    '{left: 1'b0}: result = 4'h6;
  endcase
  initial case (pair) matches
    '{left: .duplicate, right: .duplicate}: result = 4'h7;
  endcase
  initial case (pair) matches
    '{1'b0}: result = 4'h8;
  endcase
  initial case (pair) matches
    '{left: 1'b0, 1'b1}: result = 4'h9;
  endcase
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto invalid_result = fsim::elaboration::elaborate(
      invalid.design, "sv:work.invalid_case_matches");
  assert(!invalid_result.ok());
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-003"));
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-002"));
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-007"));
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-008"));
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-009"));

  const auto simple = fsim::frontend::parse_text(
      "case_match_hir.sv",
      "module case_match_hir; logic [1:0] s; logic r; initial case (s) "
      "matches 2'b00: r = 1; endcase endmodule",
      fsim::frontend::Language::SystemVerilog2017);
  assert(simple.ok());
  auto malformed = simple.design;
  auto& malformed_statement =
      malformed.units.front().processes.front().statements.front();
  malformed_statement.case_alternatives.front().choices.clear();
  const auto malformed_result = fsim::elaboration::elaborate(
      malformed, "sv:work.case_match_hir");
  assert(!malformed_result.ok());
  assert(has_diagnostic(malformed_result, "FSIM-ELAB-SVMATCH-005"));

  auto multiple_patterns = simple.design;
  auto& multiple_statement =
      multiple_patterns.units.front().processes.front().statements.front();
  multiple_statement.case_alternatives.front().choices.push_back(
      multiple_statement.case_alternatives.front().choices.front());
  const auto multiple_result = fsim::elaboration::elaborate(
      multiple_patterns, "sv:work.case_match_hir");
  assert(!multiple_result.ok());
  assert(has_diagnostic(multiple_result, "FSIM-ELAB-SVMATCH-005"));

  auto unsupported = simple.design;
  unsupported.units.front().processes.front().statements.front()
      .case_alternatives.front().choices.front() = fsim::frontend::Expression{
          fsim::frontend::ExpressionKind::Call,
          "@match-unsupported",
          {},
          {}};
  const auto unsupported_result = fsim::elaboration::elaborate(
      unsupported, "sv:work.case_match_hir");
  assert(!unsupported_result.ok());
  assert(has_diagnostic(unsupported_result, "FSIM-ELAB-SVMATCH-003"));

  const auto verilog = fsim::frontend::parse_text(
      "case_match_language.v",
      "module case_match_language; reg [1:0] s; reg r; initial case (s) "
      "2'b00: r = 1; endcase endmodule",
      fsim::frontend::Language::Verilog2005);
  assert(verilog.ok());
  auto wrong_language = verilog.design;
  wrong_language.units.front().processes.front().statements.front()
      .case_match_kind = fsim::frontend::CaseMatchKind::Matches;
  const auto wrong_language_result = fsim::elaboration::elaborate(
      wrong_language, "sv:work.case_match_language");
  assert(!wrong_language_result.ok());
  assert(has_diagnostic(wrong_language_result, "FSIM-ELAB-SVMATCH-001"));
}

}  // namespace fsim::tests::elaboration
