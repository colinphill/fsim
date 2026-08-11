// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <iostream>

namespace fsim::tests::elaboration {

void test_verilog_defparam_elaboration()
{
    const auto parsed = fsim::frontend::parse_text(
        "defparam-elaboration.v",
        R"(
module defparam_leaf #(
  parameter [256:0] VALUE = 257'd0,
  localparam [256:0] LOCKED = 257'd7
) ();
  wire [((VALUE >> 256) ? 8 : 1):0] q;
  assign q = VALUE[8:0];
endmodule

module defparam_middle;
  defparam_leaf child ();
endmodule

module defparam_top #(
  parameter [256:0] ROOT_VALUE = (257'h1 << 256)
) ();
  defparam_middle middle ();
  defparam_leaf lanes [1:0] ();
  defparam defparam_top.middle.child.VALUE = ROOT_VALUE,
           lanes[1].VALUE = ROOT_VALUE,
           lanes[0].VALUE = 257'h2;
  generate
    if (1) begin : active
      defparam_leaf generated ();
      defparam generated.VALUE = ROOT_VALUE;
    end
  endgenerate
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(parsed.ok());

    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "verilog:work.defparam_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto specialization = [&](const std::string_view path)
        -> const fsim::elaboration::SpecializationInfo& {
        const auto found = std::ranges::find_if(
            elaborated.design->specializations(),
            [&](const auto& candidate) {
                return candidate.instance == path;
            });
        assert(found != elaborated.design->specializations().end());
        return *found;
    };
    const auto identity = [&](const std::string_view path) {
        const auto& selected = specialization(path);
        const auto found = std::ranges::find_if(
            selected.parameter_identity_values,
            [](const auto& parameter) {
                return parameter.first == "VALUE";
            });
        assert(found != selected.parameter_identity_values.end());
        return found->second;
    };

    const auto high_identity = identity("defparam_top.middle.child");
    assert(high_identity.starts_with("svconst-v2:w=257:s=0:"));
    assert(identity("defparam_top.lanes[1]") == high_identity);
    assert(identity("defparam_top.active.generated") == high_identity);
    assert(identity("defparam_top.lanes[0]") != high_identity);

    for (const auto path : {
             "defparam_top.middle.child.q",
             "defparam_top.lanes[1].q",
             "defparam_top.active.generated.q" }) {
        const auto signal = elaborated.design->find_signal(path);
        assert(signal);
        assert(elaborated.design->signals().at(*signal).width == 9U);
    }
    const auto low_signal = elaborated.design->find_signal("defparam_top.lanes[0].q");
    assert(low_signal);
    assert(elaborated.design->signals().at(*low_signal).width == 2U);

    auto unresolved_design = parsed.design;
    auto& unresolved_top = unresolved_design.units.back();
    unresolved_top.verilog_defparams.front().path.front().name = "missing";
    const auto unresolved = fsim::elaboration::elaborate(
        unresolved_design, "verilog:work.defparam_top");
    assert(!unresolved.ok());
    assert(has_diagnostic(unresolved, "FSIM-ELAB-DEFPARAM-002"));

    auto duplicate_design = parsed.design;
    auto& duplicate_top = duplicate_design.units.back();
    duplicate_top.verilog_defparams.push_back(
        duplicate_top.verilog_defparams[1]);
    const auto duplicate = fsim::elaboration::elaborate(
        duplicate_design, "verilog:work.defparam_top");
    assert(!duplicate.ok());
    assert(has_diagnostic(duplicate, "FSIM-ELAB-DEFPARAM-003"));

    auto local_design = parsed.design;
    auto& local_top = local_design.units.back();
    auto local_target = local_top.verilog_defparams[1];
    local_target.path.back().name = "LOCKED";
    local_top.verilog_defparams = { std::move(local_target) };
    const auto local = fsim::elaboration::elaborate(
        local_design, "verilog:work.defparam_top");
    assert(!local.ok());
    assert(has_diagnostic(local, "FSIM-ELAB-PARAM-001"));
}

} // namespace fsim::tests::elaboration
