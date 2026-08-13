// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {
namespace {

    void append_units(
        fsim::frontend::ParsedDesign& destination,
        fsim::frontend::ParseResult parsed,
        const std::string_view library)
    {
        assert(parsed.ok());
        for (auto& unit : parsed.design.units) {
            unit.library = library;
            destination.units.push_back(std::move(unit));
        }
    }

    const fsim::elaboration::SpecializationInfo* find_specialization(
        const fsim::elaboration::ElaboratedDesign& design,
        const std::string_view instance)
    {
        const auto found = std::ranges::find_if(
            design.specializations(),
            [&](const auto& specialization) {
                return specialization.instance == instance;
            });
        return found == design.specializations().end() ? nullptr : &*found;
    }

} // namespace

void test_systemverilog_hierarchy_configuration()
{
    fsim::frontend::ParsedDesign design;
    append_units(
        design,
        fsim::frontend::parse_text(
            "hierarchy-work.sv",
            R"(
extern module monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);

module monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);
  logic [136:0] observed;
  assign observed = value ^ MAGIC;
endmodule

module configured_top;
  logic [136:0] left_value;
  logic [136:0] right_value;
  leaf left(.value(left_value));
  leaf right(.value(right_value));
  defparam right.P = 137'h1_0000_0000_0000_0000_0000_0000_0000_0001;

  for (genvar index = 0; index < 2; ++index) begin : lanes
    leaf generated();
  end

  bind leaf monitor #(
    .MAGIC(137'h1_0000_0000_0000_0000_0000_0000_0000_0002)
  ) all_leaf_monitor(.value(value));
  bind configured_top.lanes[1].generated monitor #(
    .MAGIC(137'h1_0000_0000_0000_0000_0000_0000_0000_0003)
  ) selected_monitor(.value(value));
endmodule

config configured;
  design work.configured_top;
  instance configured_top.left use fast.leaf;
  instance configured_top.lanes[1].generated use fast.leaf;
  cell leaf liblist slow;
endconfig : configured
)",
            fsim::frontend::Language::SystemVerilog2017),
        "work");
    append_units(
        design,
        fsim::frontend::parse_text(
            "hierarchy-fast.sv",
            R"(
module leaf #(
  parameter logic [136:0] P = 137'h11
) (
  output logic [136:0] value
);
  assign value = P;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017),
        "fast");
    append_units(
        design,
        fsim::frontend::parse_text(
            "hierarchy-slow.sv",
            R"(
module leaf #(
  parameter logic [136:0] P = 137'h22
) (
  output logic [136:0] value
);
  assign value = P;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017),
        "slow");

    const auto elaborated = fsim::elaboration::elaborate(
        design, "sv:work.configured");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    assert(elaborated.design);

    const auto* top = find_specialization(*elaborated.design, "configured");
    const auto* left = find_specialization(
        *elaborated.design, "configured.left");
    const auto* right = find_specialization(
        *elaborated.design, "configured.right");
    const auto* lane0 = find_specialization(
        *elaborated.design, "configured.lanes[0].generated");
    const auto* lane1 = find_specialization(
        *elaborated.design, "configured.lanes[1].generated");
    assert(top && left && right && lane0 && lane1);
    assert(std::ranges::any_of(
        top->parameter_identity_values,
        [](const auto& value) {
            return value.first == "__configuration"
                && value.second.starts_with("sv-config-v1;");
        }));
    assert(left->library == "fast");
    assert(right->library == "slow");
    assert(lane0->library == "slow");
    assert(lane1->library == "fast");
    assert(!left->parameter_identity_values.empty());
    assert(!right->parameter_identity_values.empty());
    const auto parameter_identity = [](const auto& specialization,
                                        const std::string_view name) {
        const auto found = std::ranges::find_if(
            specialization.parameter_identity_values,
            [&](const auto& value) { return value.first == name; });
        assert(found != specialization.parameter_identity_values.end());
        return found->second;
    };
    assert(
        parameter_identity(*left, "P")
        != parameter_identity(*right, "P"));
    assert(std::ranges::any_of(
        left->parameter_identity_values,
        [](const auto& value) {
            return value.first == "__configuration"
                && value.second.starts_with("sv-config-v1;");
        }));
    assert(std::ranges::any_of(
        right->parameter_values,
        [](const auto& value) {
            return value.first == "P"
                && value.second.find('1') != std::string::npos;
        }));

    for (const auto path : {
             "configured.left.all_leaf_monitor",
             "configured.right.all_leaf_monitor",
             "configured.lanes[0].generated.all_leaf_monitor",
             "configured.lanes[1].generated.all_leaf_monitor",
             "configured.lanes[1].generated.selected_monitor" }) {
        const auto* bound = find_specialization(*elaborated.design, path);
        assert(bound);
        assert(bound->library == "work");
        assert(!bound->parameter_values.empty());
    }
    assert(
        find_specialization(
            *elaborated.design,
            "configured.lanes[0].generated.selected_monitor")
        == nullptr);

    const auto mismatch = fsim::frontend::parse_text(
        "extern-mismatch.sv",
        R"(
extern module mismatch(input logic [136:0] value);
module mismatch(input logic [135:0] value); endmodule
module mismatch_top; mismatch child(); endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatch.ok());
    const auto mismatch_result = fsim::elaboration::elaborate(
        mismatch.design, "sv:work.mismatch_top");
    assert(!mismatch_result.ok());
    assert(has_diagnostic(mismatch_result, "FSIM-ELAB-SVEXTERN-002"));

    const auto missing = fsim::frontend::parse_text(
        "extern-missing.sv",
        R"(
extern module missing(input logic [136:0] value);
module missing_top; endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(missing.ok());
    const auto missing_result = fsim::elaboration::elaborate(
        missing.design, "sv:work.missing_top");
    assert(!missing_result.ok());
    assert(has_diagnostic(missing_result, "FSIM-ELAB-SVEXTERN-001"));
}

} // namespace fsim::tests::elaboration
