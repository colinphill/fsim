// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_systemverilog_case_qualifiers() {
  const auto parsed = fsim::frontend::parse_text(
      "case_qualifiers.sv",
      R"(
module case_qualifiers;
  logic [1:0] selector;
  logic [3:0] unique_multiple;
  logic [3:0] unique_missing;
  logic [3:0] unique_default;
  logic [3:0] unique0_missing;
  logic [3:0] unique0_multiple;
  logic [3:0] priority_missing;
  logic [3:0] priority_multiple;
  logic [3:0] unknown_missing;
  logic [3:0] casez_multiple;
  logic [3:0] casex_multiple;
  logic [3:0] inside_multiple;
  logic [3:0] inside_later;

  initial begin
    selector = 2'b01;
    unique case (selector)
      2'b01: unique_multiple = 4'h1;
      2'b01, 2'b10: unique_multiple = 4'h2;
    endcase
    unique case (selector)
      2'b11: unique_missing = 4'h3;
    endcase
    unique case (selector)
      2'b11: unique_default = 4'h4;
      default: unique_default = 4'h5;
    endcase
    unique0 case (selector)
      2'b11: unique0_missing = 4'h6;
    endcase
    unique0 case (selector)
      2'b01: unique0_multiple = 4'h7;
      2'b01: unique0_multiple = 4'h8;
    endcase
    priority case (selector)
      2'b11: priority_missing = 4'h9;
    endcase
    priority case (selector)
      2'b01: priority_multiple = 4'ha;
      2'b01: priority_multiple = 4'hb;
    endcase
    unique case (2'bx1)
      2'b01: unknown_missing = 4'hc;
    endcase
    unique casez (selector)
      2'b0?: casez_multiple = 4'hd;
      2'b?1: casez_multiple = 4'he;
    endcase
    unique0 casex (2'bx1)
      2'b01: casex_multiple = 4'h1;
      2'b11: casex_multiple = 4'h2;
    endcase
    unique case (selector) inside
      2'b01: inside_multiple = 4'h3;
      [2'b00:2'b10]: inside_multiple = 4'h4;
    endcase
    unique case (2'bx1) inside
      2'b01: inside_later = 4'h5;
      2'bx1: inside_later = 4'h6;
    endcase
  end
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design, "sv:work.case_qualifiers");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());
  const auto& operations = elaborated.design->processes().front().operations;
  assert(std::ranges::count_if(
      operations,
      [](const fsim::runtime::simir::Operation& operation) {
        const auto* report =
            fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Report>(&operation);
        return report != nullptr
            && report->severity
                == fsim::runtime::simir::AssertionSeverity::warning;
      }) >= 12);

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
  assert(value("unique_multiple") == "0001");
  assert(value("unique_missing") == "XXXX");
  assert(value("unique_default") == "0101");
  assert(value("unique0_missing") == "XXXX");
  assert(value("unique0_multiple") == "0111");
  assert(value("priority_missing") == "XXXX");
  assert(value("priority_multiple") == "1010");
  assert(value("unknown_missing") == "XXXX");
  assert(value("casez_multiple") == "1101");
  assert(value("casex_multiple") == "0001");
  assert(value("inside_multiple") == "0011");
  assert(value("inside_later") == "0110");
  const std::vector<std::string> expected_messages{
      "unique case has multiple matching items",
      "unique case has no matching item",
      "unique0 case has multiple matching items",
      "priority case has no matching item",
      "unique case has no matching item",
      "unique case has multiple matching items",
      "unique0 case has multiple matching items",
      "unique case has multiple matching items"};
  std::vector<std::string> observed_messages;
  for (const auto& warning : warnings) {
    observed_messages.push_back(warning.message);
    assert(warning.source.path == "case_qualifiers.sv");
    assert(warning.source.line != 0);
  }
  if (observed_messages != expected_messages) {
    for (const auto& message : observed_messages) {
      std::cerr << "case qualifier warning: " << message << '\n';
    }
  }
  assert(observed_messages == expected_messages);

  const auto simple = fsim::frontend::parse_text(
      "case_qualifier_hir.sv",
      "module case_qualifier_hir; logic s; logic r; "
      "initial case (s) 1'b0: r = 1; endcase endmodule",
      fsim::frontend::Language::SystemVerilog2017);
  assert(simple.ok());
  auto malformed = simple.design;
  malformed.units.front().processes.front().statements.front()
      .case_qualifier =
          static_cast<fsim::frontend::CaseQualifier>(255);
  const auto malformed_result = fsim::elaboration::elaborate(
      malformed, "sv:work.case_qualifier_hir");
  assert(!malformed_result.ok());
  assert(has_diagnostic(
      malformed_result, "FSIM-ELAB-SVCASEQUAL-001"));

  const auto verilog = fsim::frontend::parse_text(
      "case_qualifier_language.v",
      "module case_qualifier_language; reg s; reg r; "
      "initial case (s) 1'b0: r = 1; endcase endmodule",
      fsim::frontend::Language::Verilog2005);
  assert(verilog.ok());
  auto wrong_language = verilog.design;
  wrong_language.units.front().processes.front().statements.front()
      .case_qualifier = fsim::frontend::CaseQualifier::Unique;
  const auto wrong_language_result = fsim::elaboration::elaborate(
      wrong_language, "sv:work.case_qualifier_language");
  assert(!wrong_language_result.ok());
  assert(has_diagnostic(
      wrong_language_result, "FSIM-ELAB-SVCASEQUAL-002"));
}

}  // namespace fsim::tests::elaboration
