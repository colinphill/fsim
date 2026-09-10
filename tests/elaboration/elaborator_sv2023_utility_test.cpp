// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <string_view>

namespace fsim::tests::elaboration {
namespace {

[[nodiscard]] bool contains_message(
    const std::vector<fsim::frontend::Diagnostic>& messages,
    const fsim::frontend::DiagnosticSeverity severity,
    const std::string_view text)
{
    return std::ranges::any_of(messages, [&](const auto& message) {
        return message.severity == severity
            && message.code == "FSIM-ELAB-SVCONST-002"
            && message.message.find(text) != std::string::npos;
    });
}

} // namespace

void test_systemverilog_2023_utility_system_callables()
{
    constexpr std::string_view accepted_source = R"(
module constant_severity;
  function automatic int announce(input int value);
    if (value == 5)
      $info("value=%0d", value);
    if (value == 6)
      $warning("inactive");
    return value + 1;
  endfunction
  function automatic int caution(input int value);
    $warning("wide=%04h", value);
    return value;
  endfunction
  localparam int FIRST = announce(5);
  localparam int SECOND = announce(5);
  localparam int THIRD = caution(10);
endmodule
)";
    for (const auto revision : {
             fsim::frontend::StandardRevision::SystemVerilog2017,
             fsim::frontend::StandardRevision::SystemVerilog2023 }) {
        const auto parsed = fsim::frontend::parse_verilog(
            fsim::frontend::SourceText {
                "constant-severity.sv", std::string { accepted_source } },
            revision);
        assert(parsed.ok());
        const auto elaborated = fsim::elaboration::elaborate(
            parsed.design, "constant_severity");
        if (elaborated.messages.size() != 3U) {
            for (const auto& message : elaborated.messages) {
                std::cerr << message.code << ": " << message.message << '\n';
            }
        }
        assert(elaborated.ok());
        assert(elaborated.messages.size() == 3U);
        assert(std::ranges::count_if(
            elaborated.messages,
            [](const auto& message) {
                return message.severity
                        == fsim::frontend::DiagnosticSeverity::Note
                    && message.message.find("value=5")
                        != std::string::npos;
            }) == 2);
        assert(contains_message(
            elaborated.messages,
            fsim::frontend::DiagnosticSeverity::Warning,
            "wide=0000000a"));
        assert(!contains_message(
            elaborated.messages,
            fsim::frontend::DiagnosticSeverity::Warning,
            "inactive"));
    }

    const auto rejected = fsim::frontend::parse_verilog(
        fsim::frontend::SourceText {
            "constant-severity-error.sv",
            R"(
module constant_severity_error;
  function automatic int reject(input int value);
    $error("invalid value %0d", value);
    return value;
  endfunction
  localparam int INVALID = reject(9);
endmodule
 )" },
        fsim::frontend::StandardRevision::SystemVerilog2023);
    assert(rejected.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        rejected.design, "constant_severity_error");
    assert(!elaborated.ok());
    assert(std::ranges::any_of(
        elaborated.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVCONST-002"
                && diagnostic.message.find("invalid value 9")
                    != std::string::npos;
        }));
}

} // namespace fsim::tests::elaboration
