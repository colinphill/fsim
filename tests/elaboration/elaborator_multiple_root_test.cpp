// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

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
