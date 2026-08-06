// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace fsim::tests::elaboration {
namespace {

using fsim::elaboration::Root;

fsim::elaboration::ElaborationResult elaborate_roots(
    const fsim::frontend::ParsedDesign& design,
    const std::span<const Root> roots,
    fsim::elaboration::SystemCFactoryProvider* provider = nullptr) {
    return fsim::elaboration::elaborate(
        design,
        roots,
        {},
        {},
        provider,
        {});
}

} // namespace

void test_multiple_root_elaboration() {
    auto parsed = fsim::frontend::parse_text(
        "multiple-roots.sv",
        R"(
module producer;
  logic [3:0] value;
  initial value = 4'ha;
endmodule
module consumer;
  logic [3:0] value;
  initial value = 4'h5;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const std::array roots{
        Root{"sv:work.producer", "source"},
        Root{"sv:work.consumer", "sink"}};
    const auto elaborated = elaborate_roots(parsed.design, roots);
    assert(elaborated.ok());
    assert(elaborated.design->top() == "source");
    assert((
        elaborated.design->roots()
        == std::vector<std::string>{"source", "sink"}));
    assert(elaborated.design->specializations().size() == 2);
    assert(elaborated.design->specializations()[0].instance == "source");
    assert(elaborated.design->specializations()[1].instance == "sink");
    assert(elaborated.design->find_signal("source.value"));
    assert(elaborated.design->find_signal("sink.value"));
    assert(!elaborated.design->find_signal("value"));
    auto interpreter = elaborated.design->create_interpreter();
    assert(
        interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto source = elaborated.design->find_signal("source.value");
    const auto sink = elaborated.design->find_signal("sink.value");
    assert(source && sink);
    assert(interpreter->signal_value(*source).to_msb_string() == "1010");
    assert(interpreter->signal_value(*sink).to_msb_string() == "0101");

    const std::array repeated{
        Root{"producer", "first"},
        Root{"producer", "second"}};
    const auto repeated_result = elaborate_roots(parsed.design, repeated);
    assert(repeated_result.ok());
    assert(repeated_result.design->specializations().size() == 2);

    auto scalar_roots = fsim::frontend::parse_text(
        "multiple-root-scalars.sv",
        R"(
package scalar_root_pkg;
  parameter string PREFIX = "pkg";
  function automatic string decorate(input string value);
    return {PREFIX, value};
  endfunction
endpackage
module scalar_leaf #(
    parameter real FACTOR = 2.0,
    parameter time TICKS = 11,
    parameter string SUFFIX = ":default");
  import scalar_root_pkg::*;
  logic [7:0] checks;
  function automatic real scaled(input real value);
    return value * FACTOR;
  endfunction
  function automatic real reset_local(input real value);
    real scratch = 1.0;
    scratch = scratch + value;
    return scratch;
  endfunction
  function static time accumulate_time(input time value);
    time retained = 1;
    retained = retained + value;
    return retained;
  endfunction
  function automatic string label_value(input string value);
    return {decorate(value), SUFFIX};
  endfunction
  task automatic transfer(
      input real real_in, output real real_out,
      input time time_in, output time time_out,
      input string string_in, output string string_out,
      input chandle handle_in, output chandle handle_out);
    real_out = real_in;
    time_out = time_in;
    string_out = string_in;
    handle_out = handle_in;
  endtask
  initial begin
    real real_out;
    time time_out;
    string string_out;
    chandle handle_out;
    checks = 0;
    checks[0] = scaled(1.5) == 1.5 * FACTOR;
    checks[1] = reset_local(FACTOR) == 1.0 + FACTOR;
    checks[2] = reset_local(FACTOR) == 1.0 + FACTOR;
    checks[3] = accumulate_time(TICKS) == 1 + TICKS;
    checks[4] = accumulate_time(TICKS) == 1 + TICKS + TICKS;
    checks[5] = label_value("-") == {"pkg-", SUFFIX};
    transfer(FACTOR, real_out, TICKS, time_out,
             SUFFIX, string_out, null, handle_out);
    checks[6] = real_out == FACTOR && time_out == TICKS;
    checks[7] = string_out == SUFFIX && handle_out == null;
  end
endmodule
module scalar_root_a;
  scalar_leaf #(.FACTOR(2.0), .TICKS(11), .SUFFIX(":a")) leaf();
endmodule
module scalar_root_b;
  scalar_leaf #(.FACTOR(3.0), .TICKS(22), .SUFFIX(":b")) leaf();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!scalar_roots.ok()) {
        for (const auto& diagnostic : scalar_roots.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(scalar_roots.ok());
    const std::array scalar_root_selection{
        Root{"scalar_root_a", "first"},
        Root{"scalar_root_b", "second"}};
    const auto scalar_result = elaborate_roots(
        scalar_roots.design, scalar_root_selection);
    if (!scalar_result.ok()) {
        for (const auto& diagnostic : scalar_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(scalar_result.ok());
    const auto first_checks =
        scalar_result.design->find_signal("first.leaf.checks");
    const auto second_checks =
        scalar_result.design->find_signal("second.leaf.checks");
    assert(first_checks && second_checks && *first_checks != *second_checks);
    auto scalar_interpreter = scalar_result.design->create_interpreter();
    assert(
        scalar_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        scalar_interpreter->signal_value(*first_checks).to_msb_string()
        == "11111111");
    assert(
        scalar_interpreter->signal_value(*second_checks).to_msb_string()
        == "11111111");
    const auto leaf_specializations = std::ranges::count_if(
        scalar_result.design->specializations(),
        [](const auto& specialization) {
          return specialization.unit == "sv:work.scalar_leaf";
        });
    assert(leaf_specializations == 2);

    auto wide_roots = fsim::frontend::parse_text(
        "multiple-root-wide-values.sv",
        R"(
package wide_root_types;
  typedef struct packed {
    logic [72:0] upper;
    logic [63:0] lower;
  } wide_root_t;
endpackage

import wide_root_types::*;

module wide_root_leaf #(
    parameter wide_root_t VALUE = wide_root_t'(137'h0)
) (output logic [136:0] observed);
  localparam wide_root_t RETAINED = VALUE;
  initial observed = RETAINED;
endmodule
module wide_root_a;
  localparam wide_root_t ROOT_VALUE = wide_root_t'(
      137'h10000000000000000000000000000000001);
  logic [136:0] observed;
  wide_root_leaf #(.VALUE(ROOT_VALUE)) leaf(observed);
endmodule
module wide_root_b;
  localparam wide_root_t ROOT_VALUE = wide_root_t'(
      137'h0ffffffffffffffffffffffffffffffffff);
  logic [136:0] observed;
  wide_root_leaf #(.VALUE(ROOT_VALUE)) leaf(observed);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wide_roots.ok());
    const std::array wide_root_selection{
        Root{"wide_root_a", "wide_a"},
        Root{"wide_root_b", "wide_b"}};
    const auto wide_root_result = elaborate_roots(
        wide_roots.design, wide_root_selection);
    if (!wide_root_result.ok()) {
        for (const auto& diagnostic : wide_root_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(wide_root_result.ok());
    const auto wide_a =
        wide_root_result.design->find_signal("wide_a.observed");
    const auto wide_b =
        wide_root_result.design->find_signal("wide_b.observed");
    assert(wide_a && wide_b && *wide_a != *wide_b);
    auto wide_root_interpreter =
        wide_root_result.design->create_interpreter();
    assert(
        wide_root_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        wide_root_interpreter->signal_value(*wide_a).to_msb_string()
        == "1" + std::string(135, '0') + "1");
    assert(
        wide_root_interpreter->signal_value(*wide_b).to_msb_string()
        == "0" + std::string(136, '1'));

    const std::array missing{
        Root{"producer", "valid"},
        Root{"absent", "missing"}};
    const auto missing_result = elaborate_roots(parsed.design, missing);
    assert(!missing_result.ok());
    assert(!missing_result.design);
    assert(has_diagnostic(missing_result, "FSIM-ELAB-001"));

    const std::array duplicate_alias{
        Root{"producer", "root"},
        Root{"consumer", "root"}};
    const auto duplicate_result =
        elaborate_roots(parsed.design, duplicate_alias);
    assert(!duplicate_result.ok());
    assert(has_diagnostic(duplicate_result, "FSIM-ELAB-ROOT-003"));

    const std::array missing_alias{
        Root{"producer", {}},
        Root{"consumer", "sink"}};
    const auto missing_alias_result =
        elaborate_roots(parsed.design, missing_alias);
    assert(!missing_alias_result.ok());
    assert(has_diagnostic(
        missing_alias_result, "FSIM-ELAB-ROOT-002"));

    const std::array unsafe_alias{Root{"producer", "bad.path"}};
    const auto unsafe_result = elaborate_roots(parsed.design, unsafe_alias);
    assert(!unsafe_result.ok());
    assert(has_diagnostic(unsafe_result, "FSIM-ELAB-ROOT-002"));

    TestSystemCFactoryProvider provider;
    provider.factory_candidates = {
        {"work", "native_root", "systemc:work.native_root"}};
    const std::array mixed_roots{
        Root{"producer", "hdl"},
        Root{"native_root", "native"}};
    const auto mixed_result =
        elaborate_roots(parsed.design, mixed_roots, &provider);
    assert(mixed_result.ok());
    assert(mixed_result.design->specializations().size() == 1);
    assert(mixed_result.design->systemc_instances().size() == 1);
    assert(mixed_result.design->systemc_instances().front().instance
           == "native");
    assert(provider.next_handle == 10'001);

    // A separately selected SystemVerilog root may provide the conventional
    // global-signaling surface used by vendor libraries. Put the consumer
    // first to prove that resolution does not depend on manifest order.
    auto global_signaling = fsim::frontend::parse_text(
        "multiple-root-global.sv",
        R"(
module glbl;
  logic GSR;
  initial begin
    GSR = 1'b1;
    #1 GSR = 1'b0;
  end
endmodule
module global_consumer;
  logic observed;
  initial begin
    #2 observed = glbl.GSR;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(global_signaling.ok());
    const std::array global_roots{
        Root{"global_consumer", "dut"},
        Root{"glbl", "glbl"}};
    const auto global_result =
        elaborate_roots(global_signaling.design, global_roots);
    assert(global_result.ok());
    const auto observed =
        global_result.design->find_signal("dut.observed");
    assert(observed);
    auto global_interpreter =
        global_result.design->create_interpreter();
    const auto global_run = global_interpreter->run();
    assert(global_run.status == fsim::runtime::RunStatus::completed);
    assert(global_run.time == 2);
    assert(
        global_interpreter->signal_value(*observed).to_msb_string()
        == "0");

    auto unsupported_shortcut = fsim::frontend::parse_text(
        "multiple-root-shortcut.sv",
        R"(
module global_leaf;
  logic hidden;
endmodule
module global_parent;
  global_leaf leaf();
endmodule
module shortcut_consumer;
  logic observed;
  initial observed = global_parent.leaf.hidden;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unsupported_shortcut.ok());
    const std::array shortcut_roots{
        Root{"shortcut_consumer", "dut"},
        Root{"global_parent", "global_parent"}};
    const auto shortcut_result =
        elaborate_roots(unsupported_shortcut.design, shortcut_roots);
    assert(!shortcut_result.ok());
    assert(has_diagnostic(shortcut_result, "FSIM-ELAB-ROOT-001"));
}

} // namespace fsim::tests::elaboration
