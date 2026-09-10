// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <string>
#include <unordered_set>

namespace fsim::tests::elaboration {

void test_generate_slice_and_case_closure(
    const fsim::frontend::ParsedDesign& generated_design)
{
    const auto generated_sv_full_slice_bank =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_full_slice_bank");
    assert(generated_sv_full_slice_bank.ok());
    const auto full_slice_process = std::ranges::find_if(
        generated_sv_full_slice_bank.design->processes(),
        [](const auto& process) {
            return process.name.find(".continuous_fused_")
                != std::string::npos;
        });
    assert(
        full_slice_process
        != generated_sv_full_slice_bank.design->processes().end());
    assert(
        std::ranges::count_if(
            full_slice_process->operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::WriteUpdate>(operation);
            })
        == 1);
    assert(std::ranges::none_of(
        full_slice_process->operations,
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<
                fsim::runtime::simir::WriteUpdateSlice>(operation);
        }));
    const auto full_slice_observed =
        generated_sv_full_slice_bank.design->find_signal("observed");
    assert(full_slice_observed);
    auto full_slice_interpreter =
        generated_sv_full_slice_bank.design->create_interpreter();
    assert(
        full_slice_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        full_slice_interpreter->signal_value(*full_slice_observed)
            .to_msb_string()
        == "00001111");

    const auto generated_sv_partial_slice_bank =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_partial_slice_bank");
    assert(generated_sv_partial_slice_bank.ok());
    const auto partial_slice_process = std::ranges::find_if(
        generated_sv_partial_slice_bank.design->processes(),
        [](const auto& process) {
            return process.name.find(".continuous_fused_")
                != std::string::npos;
        });
    assert(
        partial_slice_process
        != generated_sv_partial_slice_bank.design->processes().end());
    assert(
        std::ranges::count_if(
            partial_slice_process->operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::WriteUpdateSlice>(operation);
            })
        == 8);

    const auto generated_sv_wide_slice_bank =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_wide_slice_bank");
    assert(generated_sv_wide_slice_bank.ok());
    const auto wide_slice_process = std::ranges::find_if(
        generated_sv_wide_slice_bank.design->processes(),
        [](const auto& process) {
            return process.name.find(".continuous_fused_")
                != std::string::npos;
        });
    assert(
        wide_slice_process
        != generated_sv_wide_slice_bank.design->processes().end());
    assert(
        std::ranges::count_if(
            wide_slice_process->operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::WriteUpdateSlice>(operation);
            })
        == 128);
    assert(std::ranges::none_of(
        wide_slice_process->operations,
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<
                fsim::runtime::simir::Insert>(operation);
        }));
    auto wide_slice_interpreter =
        generated_sv_wide_slice_bank.design->create_interpreter();
    assert(
        wide_slice_interpreter->run().status
        == fsim::runtime::RunStatus::completed);

    const auto generated_vhdl_bad_constant =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_bad_constant(rtl)");
    assert(!generated_vhdl_bad_constant.ok());
    assert(std::any_of(
        generated_vhdl_bad_constant.diagnostics.begin(),
        generated_vhdl_bad_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-012";
        }));

    const auto forward_generated_type =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_forward_generated_type(rtl)");
    assert(!forward_generated_type.ok());
    assert(has_diagnostic(
        forward_generated_type, "FSIM-ELAB-VHTYPE-001"));

    const auto duplicate_generated_callable =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_duplicate_callable(rtl)");
    assert(!duplicate_generated_callable.ok());
    assert(has_diagnostic(
        duplicate_generated_callable, "FSIM-ELAB-VHOVER-003"));
    assert(has_diagnostic(
        duplicate_generated_callable, "FSIM-ELAB-VHOVER-006"));

    const auto forward_generated_callable =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_forward_callable(rtl)");
    assert(!forward_generated_callable.ok());
    assert(has_diagnostic(
        forward_generated_callable, "FSIM-ELAB-VHNAME-001"));

    const auto forward_generated_generic_callable =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_forward_generic_callable(rtl)");
    assert(!forward_generated_generic_callable.ok());
    assert(has_diagnostic(
        forward_generated_generic_callable,
        "FSIM-ELAB-VHGSUB-001"));

    auto unevaluable_generate_design = generated_design;
    const auto unevaluable_unit = std::find_if(
        unevaluable_generate_design.units.begin(),
        unevaluable_generate_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_true";
        });
    assert(unevaluable_unit != unevaluable_generate_design.units.end());
    assert(!unevaluable_unit->generate_regions.empty());
    unevaluable_unit->generate_regions.front().condition.text =
        "MISSING_GENERATE_CONSTANT";
    const auto unevaluable_generate =
        fsim::elaboration::elaborate(
            unevaluable_generate_design,
            "sv:work.generated_sv_true");
    assert(!unevaluable_generate.ok());
    assert(has_diagnostic(
        unevaluable_generate, "FSIM-ELAB-GEN-001"));

    const auto generated_loop_unit =
        [](fsim::frontend::ParsedDesign& design)
        -> fsim::frontend::DesignUnit& {
          const auto found = std::find_if(
              design.units.begin(),
              design.units.end(),
              [](const auto& unit) {
                return unit.name == "generated_sv_loop";
              });
          assert(found != design.units.end());
          assert(!found->generate_regions.empty());
          return *found;
        };
    auto invalid_loop_initial_design = generated_design;
    auto& invalid_loop_initial =
        generated_loop_unit(invalid_loop_initial_design)
            .generate_regions.front()
            .initial;
    invalid_loop_initial.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_initial.text = "MISSING_LOOP_INITIAL";
    invalid_loop_initial.operands.clear();
    const auto invalid_loop_initial_result =
        fsim::elaboration::elaborate(
            invalid_loop_initial_design,
            "sv:work.generated_sv_loop");
    assert(!invalid_loop_initial_result.ok());
    assert(has_diagnostic(
        invalid_loop_initial_result, "FSIM-ELAB-GEN-002"));

    auto invalid_loop_condition_design = generated_design;
    auto& invalid_loop_condition =
        generated_loop_unit(invalid_loop_condition_design)
            .generate_regions.front()
            .condition;
    invalid_loop_condition.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_condition.text = "MISSING_LOOP_CONDITION";
    invalid_loop_condition.operands.clear();
    const auto invalid_loop_condition_result =
        fsim::elaboration::elaborate(
            invalid_loop_condition_design,
            "sv:work.generated_sv_loop");
    assert(!invalid_loop_condition_result.ok());
    assert(has_diagnostic(
        invalid_loop_condition_result, "FSIM-ELAB-GEN-003"));

    const std::vector<fsim::elaboration::Binding>
        first_generated_loop_binding{
            {"generated_sv_loop.lanes[0].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    auto stalled_loop_design = generated_design;
    auto& stalled_iteration =
        generated_loop_unit(stalled_loop_design)
            .generate_regions.front()
            .iteration;
    stalled_iteration.kind =
        fsim::frontend::ExpressionKind::Identifier;
    stalled_iteration.text = "i";
    stalled_iteration.operands.clear();
    const auto stalled_loop =
        fsim::elaboration::elaborate(
            stalled_loop_design,
            "sv:work.generated_sv_loop",
            first_generated_loop_binding);
    assert(!stalled_loop.ok());
    assert(has_diagnostic(stalled_loop, "FSIM-ELAB-GEN-006"));

    const auto cycling_loop_design = fsim::frontend::parse_text(
        "cycling-generate.sv",
        R"(
module cycling_generate;
  for (genvar i = 0; i < 2; i = 1 - i) begin : lane
    logic selected;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(cycling_loop_design.ok());
    const auto cycling_loop = fsim::elaboration::elaborate(
        cycling_loop_design.design, "cycling_generate");
    assert(!cycling_loop.ok());
    assert(has_diagnostic(cycling_loop, "FSIM-ELAB-GEN-014"));

    auto invalid_loop_iteration_design = generated_design;
    auto& invalid_loop_iteration =
        generated_loop_unit(invalid_loop_iteration_design)
            .generate_regions.front()
            .iteration;
    invalid_loop_iteration.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_iteration.text = "MISSING_LOOP_ITERATION";
    invalid_loop_iteration.operands.clear();
    const auto invalid_loop_iteration_result =
        fsim::elaboration::elaborate(
            invalid_loop_iteration_design,
            "sv:work.generated_sv_loop",
            first_generated_loop_binding);
    assert(!invalid_loop_iteration_result.ok());
    assert(has_diagnostic(
        invalid_loop_iteration_result, "FSIM-ELAB-GEN-005"));

    const auto shadowed_loop =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_shadow_loop");
    assert(!shadowed_loop.ok());
    assert(has_diagnostic(
        shadowed_loop, "FSIM-ELAB-GEN-007"));

    const auto generated_case_unit =
        [](fsim::frontend::ParsedDesign& design)
        -> fsim::frontend::DesignUnit& {
          const auto found = std::find_if(
              design.units.begin(),
              design.units.end(),
              [](const auto& unit) {
                return unit.name
                    == "generated_sv_case_selected";
              });
          assert(found != design.units.end());
          assert(!found->generate_regions.empty());
          return *found;
        };
    auto invalid_case_selector_design = generated_design;
    auto& invalid_case_selector =
        generated_case_unit(invalid_case_selector_design)
            .generate_regions.front()
            .condition;
    invalid_case_selector.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_case_selector.text = "MISSING_CASE_SELECTOR";
    invalid_case_selector.operands.clear();
    const auto invalid_case_selector_result =
        fsim::elaboration::elaborate(
            invalid_case_selector_design,
            "sv:work.generated_sv_case_selected");
    assert(!invalid_case_selector_result.ok());
    assert(has_diagnostic(
        invalid_case_selector_result, "FSIM-ELAB-GEN-008"));

    auto invalid_case_choice_design = generated_design;
    auto& invalid_case_choice =
        generated_case_unit(invalid_case_choice_design)
            .generate_regions.front()
            .alternatives.front()
            .choices.front();
    invalid_case_choice.left.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_case_choice.left.text = "MISSING_CASE_CHOICE";
    invalid_case_choice.left.operands.clear();
    const auto invalid_case_choice_result =
        fsim::elaboration::elaborate(
            invalid_case_choice_design,
            "sv:work.generated_sv_case_selected");
    assert(!invalid_case_choice_result.ok());
    assert(has_diagnostic(
        invalid_case_choice_result, "FSIM-ELAB-GEN-009"));

    auto overlapping_case_design = generated_design;
    auto& overlapping_choice =
        generated_case_unit(overlapping_case_design)
            .generate_regions.front()
            .alternatives.front()
            .choices.front();
    overlapping_choice.left.text = "2";
    const auto overlapping_case =
        fsim::elaboration::elaborate(
            overlapping_case_design,
            "sv:work.generated_sv_case_selected");
    assert(!overlapping_case.ok());
    assert(has_diagnostic(
        overlapping_case, "FSIM-ELAB-GEN-010"));

    auto unmatched_case_design = generated_design;
    const auto unmatched_case_unit = std::find_if(
        unmatched_case_design.units.begin(),
        unmatched_case_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_case_default";
        });
    assert(unmatched_case_unit != unmatched_case_design.units.end());
    auto& unmatched_alternatives =
        unmatched_case_unit->generate_regions.front().alternatives;
    unmatched_alternatives.erase(
        std::remove_if(
            unmatched_alternatives.begin(),
            unmatched_alternatives.end(),
            [](const auto& alternative) {
              return alternative.is_default;
            }),
        unmatched_alternatives.end());
    const auto unmatched_case =
        fsim::elaboration::elaborate(
            unmatched_case_design,
            "sv:work.generated_sv_case_default");
    assert(unmatched_case.ok());
    assert(unmatched_case.design->specializations().size() == 1);

    auto generated_class_design = fsim::frontend::parse_verilog(
        fsim::frontend::SourceText{
            "generated-class.sv",
            R"(
module generated_class_revision;
  logic [7:0] observed;
  if (1) begin
    interface class Contract;
      pure virtual function int value();
    endclass
    class Worker implements Contract;
      virtual function int value();
        return 13;
      endfunction
    endclass
    Worker worker;
    initial begin
      worker = new;
      observed = worker.value();
    end
  end else begin
    class Inactive extends Missing;
    endclass
  end
endmodule
)",},
        fsim::frontend::StandardRevision::SystemVerilog2023);
    assert(generated_class_design.ok());
    std::vector<fsim::frontend::Diagnostic> class_diagnostics;
    assert(fsim::frontend::resolve_systemverilog_classes(
        generated_class_design.design, class_diagnostics));
    const auto generated_class = fsim::elaboration::elaborate(
        generated_class_design.design, "generated_class_revision");
    if (!generated_class.ok()) {
        for (const auto& diagnostic : generated_class.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(generated_class.ok());
    const auto observed = generated_class.design->find_signal("observed");
    assert(observed);
    assert(std::ranges::any_of(
        generated_class.design->processes(),
        [](const auto& process) {
          return std::ranges::any_of(
                     process.operations,
                     [](const auto& operation) {
                       return fsim::runtime::simir::operation_holds<
                           fsim::runtime::simir::ClassAllocate>(operation);
                     })
              && std::ranges::any_of(
                  process.operations,
                  [](const auto& operation) {
                    return fsim::runtime::simir::operation_holds<
                        fsim::runtime::simir::ClassMethodCall>(operation);
                  });
        }));
}

} // namespace fsim::tests::elaboration
