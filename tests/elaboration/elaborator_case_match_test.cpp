// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_case_matches() {
  const auto parsed = fsim::frontend::parse_text(
      "case_matches.sv",
      R"(
module case_matches;
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

  function automatic logic [3:0] choose(input logic [2:0] value);
    case (value) matches
      3'b001: return 4'h1;
      .*: return 4'h2;
    endcase
  endfunction
  localparam logic [3:0] FOLDED = choose(3'b001);

  initial begin
    selector = 3'b001;
    folded_result = FOLDED;
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
  logic [2:0] selector;
  logic [3:0] result;
  string text;
  initial case (selector) matches
    selector: result = 4'h1;
  endcase
  initial case (selector) matches
    2'b01: result = 4'h2;
  endcase
  initial case (selector) matches
    3'sb001: result = 4'h3;
  endcase
  initial case (text) matches
    8'h00: result = 4'h4;
  endcase
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(invalid.ok());
  const auto invalid_result = fsim::elaboration::elaborate(
      invalid.design, "sv:work.invalid_case_matches");
  assert(!invalid_result.ok());
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-003"));
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-004"));
  assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVMATCH-002"));

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
