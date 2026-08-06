// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_verilog_specify_specialization() {
    const auto parsed = fsim::frontend::parse_text(
        "specify-specialization.v",
        R"(
module specify_specialization #(
  parameter BASE = 2
) (input a, input clock, output z);
  reg notifier;
  specify
    specparam PATH_DELAY = BASE + 1;
    specparam PATHPULSE$a$z = BASE:BASE + 1:BASE + 2;
    (a => z) = PATH_DELAY;
    $setup(posedge a, posedge clock, PATH_DELAY, notifier);
  endspecify
  assign z = a;
endmodule

module specify_top;
  wire a, clock, z;
  specify_specialization #(.BASE(5)) dut(a, clock, z);
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(parsed.ok());
    auto elaborated = fsim::elaboration::elaborate(
        parsed.design,
        "specify_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    const auto specialization = std::ranges::find_if(
        elaborated.design->specializations(),
        [](const auto& candidate) {
            return candidate.instance == "specify_top.dut";
        });
    assert(specialization != elaborated.design->specializations().end());
    const auto& identities = specialization->parameter_identity_values;
    const auto path_delay = std::ranges::find_if(
        identities,
        [](const auto& identity) {
            return identity.first == "@specparam:PATH_DELAY";
        });
    const auto path_pulse = std::ranges::find_if(
        identities,
        [](const auto& identity) {
            return identity.first == "@specparam:PATHPULSE$a$z";
        });
    assert(path_delay != identities.end());
    assert(path_pulse != identities.end());
    assert(
        path_delay->second.find(":v=00000000000000000000000000000110")
        != std::string::npos);
    assert(
        path_pulse->second.find(":v=00000000000000000000000000000110")
        != std::string::npos);
    const auto& paths = elaborated.design->verilog_specify_paths();
    assert(paths.size() == 1);
    assert(paths.front().id == 0);
    assert(paths.front().instance == "specify_top.dut");
    assert(paths.front().sources.size() == 1);
    assert(paths.front().destinations.size() == 1);
    assert(paths.front().sources.front().width == 1);
    assert(paths.front().destinations.front().width == 1);
    assert(paths.front().drivers.size() == 1);
    assert(paths.front().delays == std::vector<std::uint64_t>{6});
    const auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter);
    const auto& timing_checks =
        elaborated.design->verilog_timing_checks();
    assert(timing_checks.size() == 1);
    assert(timing_checks.front().limits
           == std::vector<std::int64_t>{6});
    assert(timing_checks.front().notifier.has_value());
    auto state = elaborated.design->state();
    const auto restored =
        fsim::elaboration::ElaboratedDesign::from_state(state);
    assert(restored);
    assert(restored->verilog_specify_paths().size() == 1);
    state.verilog_specify_paths.front().sources.front().width = 0;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(state)));

    const auto selected = fsim::frontend::parse_text(
        "selected-specify.v",
        R"(
module selected_specify(
  input [3:0] source,
  output [3:0] result
);
  specify
    (source[2:1] => result[2:1]) = (2, 3);
  endspecify
  assign result = source;
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(selected.ok());
    const auto selected_result = fsim::elaboration::elaborate(
        selected.design, "selected_specify");
    assert(selected_result.ok());
    const auto& selected_path =
        selected_result.design->verilog_specify_paths().front();
    assert(selected_path.sources.front().offset == 1);
    assert(selected_path.sources.front().width == 2);
    assert(selected_path.destinations.front().offset == 1);
    assert(selected_path.destinations.front().width == 2);
    assert((selected_path.delays == std::vector<std::uint64_t>{2, 3}));

    const auto conditional = fsim::frontend::parse_text(
        "conditional-specify.v",
        R"(
module conditional_specify(
  input source,
  input enable,
  input data,
  output result
);
  specify
    specparam PATHPULSE$source$result = (1, 4);
    if (enable) (source => (result +: data)) = 5;
    ifnone (source => result) = 2;
    pulsestyle_ondetect result;
    showcancelled result;
  endspecify
  assign result = source;
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(conditional.ok());
    const auto conditional_result = fsim::elaboration::elaborate(
        conditional.design, "conditional_specify");
    assert(conditional_result.ok());
    const auto& conditional_paths =
        conditional_result.design->verilog_specify_paths();
    assert(conditional_paths.size() == 2);
    assert(!conditional_paths[0].condition_program.empty());
    assert(!conditional_paths[0].data_source_program.empty());
    assert(conditional_paths[0].selection_group
           == conditional_paths[1].selection_group);
    assert(conditional_paths[0].pulse_style
           == fsim::frontend::VerilogPulseStyle::Ondetect);
    assert(conditional_paths[0].show_cancelled);
    assert(conditional_paths[0].pulse_reject_limit == 1);
    assert(conditional_paths[0].pulse_error_limit == 4);
    assert(conditional_result.design->create_interpreter());

    const auto compound = fsim::frontend::parse_text(
        "compound-specify.v",
        R"(
module compound_specify(
  input reference,
  input data,
  input stamp_enable,
  input check_enable,
  output delayed_reference,
  output delayed_data
);
  reg notifier;
  specify
    $setuphold(posedge reference, posedge data, -2, 5, notifier,
               stamp_enable, check_enable,
               delayed_reference, delayed_data);
    $timeskew(posedge reference, posedge data, 6, notifier, 1, 1);
  endspecify
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(compound.ok());
    const auto compound_result = fsim::elaboration::elaborate(
        compound.design, "compound_specify");
    if (!compound_result.ok()) {
        for (const auto& diagnostic : compound_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(compound_result.ok());
    const auto& compound_checks =
        compound_result.design->verilog_timing_checks();
    assert(compound_checks.size() == 2);
    assert((compound_checks[0].limits
            == std::vector<std::int64_t>{-2, 5}));
    assert(!compound_checks[0].timestamp_condition.empty());
    assert(!compound_checks[0].timecheck_condition.empty());
    assert(compound_checks[0].delayed_reference.has_value());
    assert(compound_checks[0].delayed_data.has_value());
    assert(compound_checks[1].event_based);
    assert(compound_checks[1].remain_active);
    assert(compound_result.design->create_interpreter());

    const auto dynamic = fsim::frontend::parse_text(
        "dynamic-specparam.v",
        R"(
module dynamic_specparam(input a);
  specify
    specparam BAD = a;
  endspecify
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(dynamic.ok());
    const auto rejected = fsim::elaboration::elaborate(
        dynamic.design, "dynamic_specparam");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVSPEC-001"));

    const auto invalid_terminals = fsim::frontend::parse_text(
        "invalid-specify-terminals.v",
        R"(
module invalid_specify_terminals(
  input [1:0] input_bus,
  output output_bit
);
  specify
    (output_bit => input_bus) = 1;
    (input_bus => output_bit) = 1;
    (unknown_input => output_bit) = 1;
    pulsestyle_onevent input_bus;
  endspecify
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(invalid_terminals.ok());
    const auto invalid_result = fsim::elaboration::elaborate(
        invalid_terminals.design, "invalid_specify_terminals");
    assert(!invalid_result.ok());
    assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVSPEC-003"));
    assert(has_diagnostic(invalid_result, "FSIM-ELAB-SVSPEC-005"));

    const auto invalid_timing = fsim::frontend::parse_text(
        "invalid-specify-timing.v",
        R"(
module invalid_specify_timing(
  input a,
  input b,
  output z,
  output [1:0] delayed
);
  specify
    $setup(posedge a, posedge b, -1);
    $setup(posedge a, posedge b, a);
    $setuphold(posedge a, posedge b, -5, 4);
    $period(a, 1);
    $width(edge [01, qq] a, 2, 1);
    $nochange(posedge a, posedge b, 4, -2);
    $setuphold(posedge a, posedge b, 1, 1, , , , delayed, delayed);
    $setup(posedge a, posedge b, 1, delayed);
  endspecify
  assign z = a;
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(invalid_timing.ok());
    const auto invalid_timing_result = fsim::elaboration::elaborate(
        invalid_timing.design, "invalid_specify_timing");
    assert(!invalid_timing_result.ok());
    assert(has_diagnostic(
        invalid_timing_result, "FSIM-ELAB-SVSPEC-015"));
    assert(has_diagnostic(
        invalid_timing_result, "FSIM-ELAB-SVSPEC-016"));
    assert(has_diagnostic(
        invalid_timing_result, "FSIM-ELAB-SVSPEC-017"));
    assert(has_diagnostic(
        invalid_timing_result, "FSIM-ELAB-SVSPEC-019"));

    auto corrupt_state = compound_result.design->state();
    corrupt_state.verilog_timing_checks.front().id = 7;
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(corrupt_state)));
    corrupt_state = compound_result.design->state();
    auto& corrupt_condition = corrupt_state.verilog_timing_checks.front()
                                  .timestamp_condition;
    corrupt_condition.root = static_cast<std::uint32_t>(
        corrupt_condition.nodes.size());
    assert(!fsim::elaboration::ElaboratedDesign::from_state(
        std::move(corrupt_state)));
}

}  // namespace fsim::tests::elaboration
