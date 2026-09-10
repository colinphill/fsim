// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/coverage_cross_inventory.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>


namespace fsim::tests::frontend {
namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string { message });
    }
}

bool has_code(
    const fsim::frontend::ParseResult& result,
    const std::string_view code)
{
    return std::ranges::any_of(
        result.diagnostics,
        [&](const fsim::frontend::Diagnostic& diagnostic) {
            return diagnostic.code == code;
        });
}

void test_systemverilog_2023_cross_auto_bin_retention()
{
    using namespace fsim::frontend;
    const auto source = [](const std::string_view option) {
        return std::string { R"(module cross_retention;
  covergroup cg with function sample(input int left, input int right);
)" } + std::string { option } + R"(
    left_point: coverpoint left {
      bins zero = {0};
      bins one = {1};
    }
    right_point: coverpoint right {
      bins zero = {0};
      bins one = {1};
    }
    pair: cross left_point, right_point {
      bins both_zero = binsof(left_point.zero)
          && binsof(right_point.zero);
    }
  endgroup
endmodule
)";
    };
    const auto resolve = [](ParseResult& parsed) {
        std::vector<Diagnostic> diagnostics;
        return parsed.ok()
            && resolve_systemverilog_covergroups(parsed.design, diagnostics)
            && diagnostics.empty();
    };
    const std::vector<SystemVerilogCovergroupSampleInput> residual_inputs {
        { 0U, { 1, 0U, 64U } },
        { 1U, { 1, 0U, 64U } }
    };

    auto retained_2017 = parse_verilog(
        SourceText { "cross-retention-2017.sv", source("") },
        StandardRevision::SystemVerilog2017);
    require(
        resolve(retained_2017),
        "the retained 2017 cross-auto-bin default resolves");
    const auto& retained_group = retained_2017.design.units.front()
                                     .systemverilog_covergroups.front();
    SystemVerilogCovergroupInstance retained_instance;
    retained_instance.declaration_identity
        = retained_group.canonical_identity;
    std::vector<Diagnostic> retained_diagnostics;
    require(
        initialize_systemverilog_cross_inventory(
            retained_instance, retained_group, retained_diagnostics)
            && retained_instance.cross_bin_state.size() == 4U
            && std::ranges::all_of(
                retained_instance.cross_bin_state,
                [](const auto& state) { return state.hit_count == 0U; })
            && build_systemverilog_coverage_report(
                   retained_group,
                   std::span<const SystemVerilogCovergroupInstance> {
                       &retained_instance, 1U })
                    .instances.front().items.back().bins.size()
                == 4U,
        "the complete cross denominator exists before the first sample");
    const auto retained_sample = sample_systemverilog_covergroup(
        retained_instance, retained_group, residual_inputs,
        retained_diagnostics);
    const auto retained_percentage
        = calculate_systemverilog_covergroup_instance_percentage(
            retained_group, retained_instance);
    const auto retained_report = build_systemverilog_coverage_report(
        retained_group,
        std::span<const SystemVerilogCovergroupInstance> {
            &retained_instance, 1U });
    require(
        retained_group.effective_cross_retain_auto_bins
            && retained_sample.hit_cross_bin_identities.size() == 1U
            && retained_instance.cross_bin_state.size() == 4U
            && std::ranges::count_if(
                   retained_instance.cross_bin_state,
                   [](const auto& state) {
                       return !state.bin_declaration_index;
                   })
                == 3U
            && retained_percentage.declarations.back().eligible_weight == 4U
            && retained_percentage.declarations.back().covered_weight == 1U
            && retained_report.instances.front().items.back().bins.size()
                == 4U
            && retained_diagnostics.empty(),
        "legacy default retention preserves residual automatic cross bins in state, scoring, and reports");

    auto disabled_2023 = parse_verilog(
        SourceText {
            "cross-retention-disabled-2023.sv",
            source("    option.cross_retain_auto_bins = 0;\n") },
        StandardRevision::SystemVerilog2023);
    require(
        resolve(disabled_2023),
        "the 2023 cross-auto-bin disable option resolves");
    const auto& disabled_group = disabled_2023.design.units.front()
                                     .systemverilog_covergroups.front();
    SystemVerilogCovergroupInstance disabled_instance;
    disabled_instance.declaration_identity
        = disabled_group.canonical_identity;
    std::vector<Diagnostic> disabled_diagnostics;
    const auto disabled_sample = sample_systemverilog_covergroup(
        disabled_instance, disabled_group, residual_inputs,
        disabled_diagnostics);
    require(
        disabled_group.standard_revision
                == StandardRevision::SystemVerilog2023
            && !disabled_group.effective_cross_retain_auto_bins
            && disabled_sample.hit_cross_bin_identities.empty()
            && disabled_instance.cross_bin_state.size() == 1U
            && disabled_instance.cross_bin_state.front().bin_declaration_index
            && calculate_systemverilog_covergroup_instance_percentage(
                   disabled_group, disabled_instance)
                    .declarations.back().eligible_weight
                == 1U
            && disabled_diagnostics.empty(),
        "option.cross_retain_auto_bins=0 removes residual automatic cross bins");

    auto enabled_2023 = parse_verilog(
        SourceText {
            "cross-retention-enabled-2023.sv",
            source("    option.cross_retain_auto_bins = 1;\n") },
        StandardRevision::SystemVerilog2023);
    require(
        resolve(enabled_2023)
            && enabled_2023.design.units.front().systemverilog_covergroups
                   .front().effective_cross_retain_auto_bins,
        "option.cross_retain_auto_bins=1 preserves the 2023 default");

    const auto legacy_option = parse_verilog(
        SourceText {
            "cross-retention-option-2017.sv",
            source("    option.cross_retain_auto_bins = 0;\n") },
        StandardRevision::SystemVerilog2017);
    const auto wrong_scope = parse_verilog(
        SourceText {
            "cross-retention-type-option-2023.sv",
            source("    type_option.cross_retain_auto_bins = 0;\n") },
        StandardRevision::SystemVerilog2023);
    const auto invalid_value = parse_verilog(
        SourceText {
            "cross-retention-value-2023.sv",
            source("    option.cross_retain_auto_bins = 2;\n") },
        StandardRevision::SystemVerilog2023);
    const auto duplicate = parse_verilog(
        SourceText {
            "cross-retention-duplicate-2023.sv",
            source("    option.cross_retain_auto_bins = 0;\n"
                   "    option.cross_retain_auto_bins = 1;\n") },
        StandardRevision::SystemVerilog2023);
    require(
        !legacy_option.ok() && has_code(legacy_option, "FSIM-SV-SEM-257")
            && !wrong_scope.ok() && has_code(wrong_scope, "FSIM-SV-SEM-258")
            && !invalid_value.ok()
            && has_code(invalid_value, "FSIM-SV-SEM-258")
            && !duplicate.ok() && has_code(duplicate, "FSIM-SV-SEM-258"),
        "cross-auto-bin option revision, scope, value, and immutability failures reject exactly");
}

void test_systemverilog_2023_covergroup_inheritance()
{
    using namespace fsim::frontend;
    constexpr std::string_view source = R"(
class base_monitor;
  int inherited_value;
  covergroup samples with function sample(input int left, input int right);
    left_point: coverpoint left { bins zero = {0}; }
    right_point: coverpoint right { bins one = {1}; }
    base_pair: cross left_point, right_point;
  endgroup
endclass

class derived_monitor extends base_monitor;
  int derived_value;
  covergroup extends samples;
    left_point: coverpoint left { bins one = {1}; }
    derived_pair: cross left_point, right_point;
  endgroup
endclass
)";
    auto parsed = parse_verilog(
        SourceText {
            "covergroup-inheritance-2023.sv", std::string { source } },
        StandardRevision::SystemVerilog2023);
    std::vector<Diagnostic> diagnostics;
    const auto classes_resolved
        = resolve_systemverilog_classes(parsed.design, diagnostics);
    const auto covergroups_resolved
        = resolve_systemverilog_covergroups(parsed.design, diagnostics);
    std::string failure_codes;
    for (const auto& diagnostic : parsed.diagnostics) {
        failure_codes += " parse:" + diagnostic.code;
    }
    for (const auto& diagnostic : diagnostics) {
        failure_codes += " resolve:" + diagnostic.code;
    }
    for (const auto& class_declaration
         : parsed.design.systemverilog_classes) {
        failure_codes += " class:" + class_declaration.canonical_identity;
        if (class_declaration.base) {
            failure_codes += " base:"
                + class_declaration.base->declaration_identity;
        }
    }
    require(
        parsed.ok() && classes_resolved && covergroups_resolved
            && diagnostics.empty(),
        "a 2023 covergroup extension resolves through its parent class:"
            + failure_codes);
    require(
        parsed.design.systemverilog_classes.size() == 2U,
        "the inheritance fixture retains both class declarations");
    const auto& base = parsed.design.systemverilog_classes.front();
    const auto& derived = parsed.design.systemverilog_classes.back();
    const auto& base_group = base.covergroups.front();
    const auto& derived_group = derived.covergroups.front();
    const auto inherited_shape = std::string { " base=" }
        + base_group.canonical_identity + " resolved="
        + derived_group.resolved_base_identity + " formals="
        + std::to_string(derived_group.formals.size()) + " sample-formals="
        + std::to_string(
            derived_group.sampling
                ? derived_group.sampling->formals.size()
                : 0U)
        + " declarations="
        + std::to_string(derived_group.coverage_declarations.size());
    require(
        derived_group.extends_parent
            && derived_group.resolved_base_identity
                == base_group.canonical_identity
            && derived_group.formals.empty()
            && derived_group.sampling
            && derived_group.sampling->formals.size() == 2U
            && derived_group.coverage_declarations.size() == 5U,
        "the extension inherits the parent profile and effective declarations"
            + inherited_shape);
    const auto& local_cross = derived_group.coverage_declarations[1];
    const auto& inherited_left = derived_group.coverage_declarations[2];
    const auto& inherited_right = derived_group.coverage_declarations[3];
    const auto& inherited_cross = derived_group.coverage_declarations[4];
    require(
        !local_cross.inherited
            && local_cross.cross_operands[0].resolved_declaration_index == 0U
            && local_cross.cross_operands[1].resolved_declaration_index == 3U
            && inherited_left.inherited
            && inherited_left.origin_covergroup_identity
                == base_group.canonical_identity
            && inherited_right.inherited && inherited_cross.inherited
            && inherited_cross.cross_operands[0].resolved_declaration_index
                == 2U
            && inherited_cross.cross_operands[1].resolved_declaration_index
                == 3U,
        "local crosses bind local shadows while inherited crosses retain parent bindings");
    const auto first_effective_count
        = derived_group.coverage_declarations.size();
    diagnostics.clear();
    require(
        resolve_systemverilog_covergroups(parsed.design, diagnostics)
            && diagnostics.empty()
            && derived.covergroups.front().coverage_declarations.size()
                == first_effective_count,
        "covergroup inheritance resolution is idempotent");

    const auto legacy = parse_verilog(
        SourceText {
            "covergroup-inheritance-2017.sv", std::string { source } },
        StandardRevision::SystemVerilog2017);
    require(
        !legacy.ok() && has_code(legacy, "FSIM-SV-SEM-259"),
        "covergroup inheritance is isolated from the 2017 profile");
    const auto wrong_owner = parse_verilog(
        SourceText { "covergroup-inheritance-owner.sv", R"(
module bad_owner;
  covergroup extends samples;
  endgroup
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        !wrong_owner.ok() && has_code(wrong_owner, "FSIM-SV-SEM-260"),
        "covergroup inheritance is rejected outside class scope");

    auto missing = parse_verilog(
        SourceText { "covergroup-inheritance-missing.sv", R"(
class root;
endclass
class leaf extends root;
  covergroup extends absent;
  endgroup
endclass
)" },
        StandardRevision::SystemVerilog2023);
    std::vector<Diagnostic> missing_diagnostics;
    require(
        missing.ok()
            && resolve_systemverilog_classes(
                missing.design, missing_diagnostics)
            && !resolve_systemverilog_covergroups(
                missing.design, missing_diagnostics)
            && std::ranges::any_of(
                missing_diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-262";
                }),
        "an extension without a parent covergroup rejects during resolution");
}

void test_systemverilog_2023_real_coverpoints()
{
    using namespace fsim::frontend;
    constexpr std::string_view source = R"(
module real_coverage;
  covergroup cg with function sample(input real value);
    type_option.real_interval = 0.2;
    measured: coverpoint value {
      bins exact = {1.0};
      bins absolute = {[2.0 +/- 0.1]};
      bins relative = {[4.0 +%- 10.0]};
      bins buckets[] = {[5.0:5.5]};
    }
  endgroup
endmodule
)";
    auto parsed = parse_verilog(
        SourceText { "real-coverpoints-2023.sv", std::string { source } },
        StandardRevision::SystemVerilog2023);
    std::vector<Diagnostic> diagnostics;
    require(
        parsed.ok()
            && resolve_systemverilog_covergroups(parsed.design, diagnostics)
            && diagnostics.empty(),
        "SystemVerilog-2023 real coverpoints and interval options resolve");
    const auto& declaration = parsed.design.units.front()
                                  .systemverilog_covergroups.front();
    const auto& coverpoint = declaration.coverage_declarations.front();
    require(
        declaration.effective_real_interval_bits
            && coverpoint.effective_real_interval_bits
            && coverpoint.sampled_scalar_kind == SystemVerilogScalarKind::Real
            && coverpoint.bins.size() == 6U
            && coverpoint.bins[3].name == "buckets[0]"
            && !coverpoint.bins[3].values.front().range_right_inclusive
            && coverpoint.bins[5].values.front().range_right_inclusive,
        "real_interval deterministically partitions an unsized interval array");

    SystemVerilogCovergroupInstance instance;
    instance.declaration_identity = declaration.canonical_identity;
    instance.runtime_identity = declaration.canonical_identity + "@dut.cg";
    const auto real_sample = [](const double value) {
        SystemVerilogCoverageSampleValue result;
        result.width = 64U;
        result.scalar_kind = SystemVerilogScalarKind::Real;
        result.scalar_bits = std::bit_cast<std::uint64_t>(value);
        return result;
    };
    SystemVerilogCoverageExecutionState state;
    const std::vector<SystemVerilogCovergroupSampleInput> inputs {
        { 0U, real_sample(5.5) }
    };
    const auto result = execute_systemverilog_covergroup_sample(
        instance, declaration, SystemVerilogCoverageSampleTrigger::Procedural,
        SystemVerilogCoverageExecutionMode::LlvmO2, inputs,
        std::span<const SystemVerilogCoverageCallback> { }, state,
        diagnostics);
    require(
        result.accepted && diagnostics.empty()
            && result.sample.coverpoints.front().result.selected_bin_identity
            && result.sample.coverpoints.front().result.selected_bin_identity
                   ->ends_with("buckets[2]")
            && result.events.size() == 3U
            && result.events[1].scalar_kind == SystemVerilogScalarKind::Real
            && result.events[1].scalar_bits
                == std::bit_cast<std::uint64_t>(5.5)
            && !result.events[1].value,
        "real samples retain their IEEE payload through matching and callbacks");

    const auto legacy = parse_verilog(
        SourceText { "real-coverpoints-2017.sv", std::string { source } },
        StandardRevision::SystemVerilog2017);
    const auto invalid_interval = parse_verilog(
        SourceText { "real-interval-invalid.sv", R"(
module bad;
  covergroup cg with function sample(input real value);
    option.real_interval = 0.0;
    cp: coverpoint value { bins point = {1.0}; }
  endgroup
endmodule
)" },
        StandardRevision::SystemVerilog2023);
    require(
        !legacy.ok() && has_code(legacy, "FSIM-SV-SEM-264")
            && !invalid_interval.ok()
            && has_code(invalid_interval, "FSIM-SV-SEM-265"),
        "real coverage remains profile-isolated and rejects invalid interval options");
}

} // namespace

void test_systemverilog_coverage_resources_and_persistence(
    const fsim::frontend::SystemVerilogCovergroupDeclaration& explicit_group,
    const std::vector<fsim::frontend::SystemVerilogCovergroupSampleInput>&
        execution_input)
{
    using namespace fsim::frontend;
    test_systemverilog_2023_cross_auto_bin_retention();
    test_systemverilog_2023_covergroup_inheritance();
    test_systemverilog_2023_real_coverpoints();
    const auto require_static_budget_rejection = [](
                                                     const SystemVerilogCovergroupDeclaration& declaration,
                                                     const std::string_view description) {
        std::vector<Diagnostic> diagnostics;
        require(
            !validate_systemverilog_coverage_resources(
                declaration, diagnostics)
                && diagnostics.size() == 1
                && diagnostics.front().code == "FSIM-SV-SEM-221",
            std::string { description });
    };
    SystemVerilogCovergroupDeclaration declaration_budget;
    declaration_budget.coverage_declarations.resize(
        kSystemVerilogCoverageMaximumDeclarations + 1U);
    require_static_budget_rejection(
        declaration_budget,
        "coverage declaration overflow rejects exactly");

    SystemVerilogCovergroupDeclaration bin_budget;
    bin_budget.coverage_declarations.resize(1);
    bin_budget.coverage_declarations.front().bins.resize(
        kSystemVerilogCoverageMaximumBins + 1U);
    require_static_budget_rejection(
        bin_budget, "coverage bin inventory overflow rejects exactly");

    SystemVerilogCovergroupDeclaration transition_work_budget;
    transition_work_budget.coverage_declarations.resize(1);
    auto& work_bin = transition_work_budget.coverage_declarations.front()
                         .bins.emplace_back();
    auto& work_sequence = work_bin.transitions.emplace_back();
    auto& work_step = work_sequence.steps.emplace_back();
    work_step.repetition.maximum = static_cast<std::uint32_t>(
        kSystemVerilogCoverageMaximumWork + 1U);
    require_static_budget_rejection(
        transition_work_budget,
        "coverage transition work overflow rejects exactly");

    SystemVerilogCovergroupDeclaration cross_product_budget;
    cross_product_budget.coverage_declarations.resize(3);
    cross_product_budget.coverage_declarations[0].bins.resize(1025);
    cross_product_budget.coverage_declarations[1].bins.resize(1025);
    auto& product_cross = cross_product_budget.coverage_declarations[2];
    product_cross.kind = SystemVerilogCoverageDeclarationKind::Cross;
    product_cross.cross_operands.resize(2);
    product_cross.cross_operands[0].resolved_declaration_index = 0;
    product_cross.cross_operands[1].resolved_declaration_index = 1;
    require_static_budget_rejection(
        cross_product_budget,
        "coverage cross-product overflow rejects exactly");

    SystemVerilogCovergroupInstance storage_budget_instance;
    storage_budget_instance.declaration_identity = explicit_group.canonical_identity;
    storage_budget_instance.runtime_identity = "storage-budget";
    storage_budget_instance.bin_hits.resize(
        kSystemVerilogCoverageMaximumStateRecords);
    SystemVerilogCoverageExecutionState storage_budget_state;
    std::vector<Diagnostic> storage_budget_diagnostics;
    const auto rejected_storage = execute_systemverilog_covergroup_sample(
        storage_budget_instance,
        explicit_group,
        SystemVerilogCoverageSampleTrigger::Explicit,
        SystemVerilogCoverageExecutionMode::Interpreter,
        execution_input,
        std::span<const SystemVerilogCoverageCallback> { },
        storage_budget_state,
        storage_budget_diagnostics);
    require(
        !rejected_storage.accepted
            && rejected_storage.events.empty()
            && storage_budget_instance.bin_hits.size()
                == kSystemVerilogCoverageMaximumStateRecords
            && storage_budget_diagnostics.size() == 1
            && storage_budget_diagnostics.front().code == "FSIM-SV-COV-005",
        "coverage state storage overflow rejects transactionally");

    std::vector<SystemVerilogCovergroupSampleInput> transaction_budget_inputs(
        kSystemVerilogCoverageMaximumTransactionInputs + 1U,
        execution_input.front());
    SystemVerilogCovergroupInstance transaction_budget_instance;
    transaction_budget_instance.declaration_identity = explicit_group.canonical_identity;
    transaction_budget_instance.runtime_identity = "transaction-budget";
    SystemVerilogCoverageExecutionState transaction_budget_state;
    std::vector<Diagnostic> transaction_budget_diagnostics;
    const auto rejected_transaction = execute_systemverilog_covergroup_sample(
        transaction_budget_instance,
        explicit_group,
        SystemVerilogCoverageSampleTrigger::Explicit,
        SystemVerilogCoverageExecutionMode::LlvmO2,
        transaction_budget_inputs,
        std::span<const SystemVerilogCoverageCallback> { },
        transaction_budget_state,
        transaction_budget_diagnostics);
    require(
        !rejected_transaction.accepted
            && rejected_transaction.events.empty()
            && transaction_budget_instance.bin_hits.empty()
            && transaction_budget_diagnostics.size() == 1
            && transaction_budget_diagnostics.front().code
                == "FSIM-SV-COV-005",
        "coverage transaction input/work overflow rejects before mutation");

    const auto malformed_bin = parse_text(
        "covergroup-bin-malformed.sv",
        "module bad; covergroup cg; coverpoint value { bins = {1}; } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto duplicate_bin = parse_text(
        "covergroup-bin-duplicate.sv",
        "module bad; covergroup cg; coverpoint value { bins same = {1}; "
        "bins same = {2}; } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_bin_value = parse_text(
        "covergroup-bin-value-malformed.sv",
        "module bad; covergroup cg; coverpoint value { bins bad = {[1:]}; } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto excessive_bin_array = parse_text(
        "covergroup-bin-array-excessive.sv",
        "module bad; covergroup cg; coverpoint value { bins huge[65537] = "
        "{[0:65536]}; } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_transition = parse_text(
        "covergroup-transition-malformed.sv",
        "module bad; covergroup cg; coverpoint value { bins bad = (1 =>); } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto invalid_transition_array = parse_text(
        "covergroup-transition-array-invalid.sv",
        "module bad; covergroup cg; coverpoint value { bins bad[2] = (1 => 2); } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_bin_iff = parse_text(
        "covergroup-bin-iff-malformed.sv",
        "module bad; covergroup cg; coverpoint value { bins bad = {1} iff (); } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_cross_bin = parse_text(
        "covergroup-cross-bin-malformed.sv",
        "module bad; covergroup cg; a: coverpoint x; b: coverpoint y; "
        "c: cross a, b { bins = binsof(a); } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto duplicate_cross_bin = parse_text(
        "covergroup-cross-bin-duplicate.sv",
        "module bad; covergroup cg; a: coverpoint x; b: coverpoint y; "
        "c: cross a, b { bins same = binsof(a); "
        "bins same = binsof(b); } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto invalid_coverage_option = parse_text(
        "covergroup-option-bounds.sv",
        "module bad; covergroup cg; coverpoint value { option.goal = 101; "
        "option.at_least = 0; } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto invalid_percentage_option = parse_text(
        "covergroup-percentage-option-bounds.sv",
        "module bad; covergroup cg; option.goal = 101; "
        "type_option.merge_instances = 2; endgroup endmodule",
        Language::SystemVerilog2017);
    const auto unsupported_option = parse_text(
        "covergroup-option-unsupported.sv",
        "module bad; covergroup cg; coverpoint value { "
        "option.auto_bin_max = 8; } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto unsupported_type = parse_text(
        "covergroup-type-unsupported.sv",
        "module bad; covergroup cg with function sample(input real value); "
        "coverpoint value; endgroup endmodule",
        Language::SystemVerilog2017);
    const auto with_selection = parse_text(
        "covergroup-selection-with.sv",
        "module bad; covergroup cg; coverpoint value { "
        "bins filtered = {1} with (item > 0); } endgroup endmodule",
        Language::SystemVerilog2017);
    const auto cross_with_selection = parse_text(
        "covergroup-cross-selection-with.sv",
        "module bad; covergroup cg; a: coverpoint x; b: coverpoint y; "
        "c: cross a, b { bins filtered = binsof(a) with (item > 0); } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    auto empty_cross_selection = parse_text(
        "covergroup-cross-bin-empty.sv",
        "module bad; covergroup cg; a: coverpoint x { bins good = {1}; } "
        "b: coverpoint y; c: cross a, b { bins bad = binsof(a.missing); } "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    std::vector<Diagnostic> empty_cross_diagnostics;
    (void)resolve_systemverilog_covergroups(
        empty_cross_selection.design, empty_cross_diagnostics);
    auto ambiguous_type = parse_text(
        "covergroup-type-ambiguous.sv",
        R"(package first;
  class Owner;
    covergroup shared; endgroup
  endclass
endpackage
package second;
  class Owner;
    covergroup shared; endgroup
  endclass
endpackage
module bad;
  Owner::shared ambiguous;
endmodule
)",
        Language::SystemVerilog2017);
    const auto ambiguous_owner = std::ranges::find(
        ambiguous_type.design.units,
        std::string { "bad" },
        &DesignUnit::name);
    require(
        ambiguous_owner != ambiguous_type.design.units.end(),
        "the ambiguous covergroup fixture retains its owner module");
    ambiguous_owner->variables.clear();
    VariableDeclaration ambiguous_variable;
    ambiguous_variable.name = "ambiguous";
    ambiguous_variable.type.named_type = "Owner::shared";
    ambiguous_variable.span = ambiguous_owner->span;
    ambiguous_owner->variables.push_back(std::move(ambiguous_variable));
    std::vector<Diagnostic> ambiguous_type_diagnostics;
    (void)resolve_systemverilog_covergroups(
        ambiguous_type.design, ambiguous_type_diagnostics);
    std::string ambiguous_type_failure;
    for (const auto& diagnostic : ambiguous_type.diagnostics) {
        ambiguous_type_failure += " parse:" + diagnostic.code;
    }
    for (const auto& diagnostic : ambiguous_type_diagnostics) {
        ambiguous_type_failure += " resolve:" + diagnostic.code;
    }

    const auto missing_name = parse_text(
        "covergroup-missing-name.sv",
        "module bad; covergroup ; endgroup endmodule",
        Language::SystemVerilog2017);
    const auto missing_header = parse_text(
        "covergroup-missing-header.sv",
        "module bad; covergroup cg endgroup endmodule",
        Language::SystemVerilog2017);
    const auto missing_end = parse_text(
        "covergroup-missing-end.sv",
        "module bad; covergroup cg; endmodule",
        Language::SystemVerilog2017);
    const auto missing_end_name = parse_text(
        "covergroup-missing-end-name.sv",
        "module bad; covergroup cg; endgroup : endmodule",
        Language::SystemVerilog2017);
    const auto mismatched_end_name = parse_text(
        "covergroup-mismatched-end-name.sv",
        "module bad; covergroup cg; endgroup : other endmodule",
        Language::SystemVerilog2017);
    const auto duplicate = parse_text(
        "covergroup-duplicate.sv",
        "module bad; covergroup cg; endgroup covergroup cg; endgroup endmodule",
        Language::SystemVerilog2017);
    const auto wrong_language = parse_text(
        "covergroup-verilog.v",
        "module bad; covergroup cg; endgroup endmodule",
        Language::Verilog2005);
    const auto malformed_formal_balance = parse_text(
        "covergroup-formal-balance.sv",
        "module bad; covergroup cg (int value; endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_formal = parse_text(
        "covergroup-formal-empty.sv",
        "module bad; covergroup cg (int first,, bit second); "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto duplicate_formal = parse_text(
        "covergroup-formal-duplicate.sv",
        "module bad; covergroup cg (int value, bit value); "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_sampling = parse_text(
        "covergroup-sampling-malformed.sv",
        "module bad; covergroup cg with function wrong(); "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_option = parse_text(
        "covergroup-option-malformed.sv",
        "module bad; covergroup cg; option = 1; endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_coverage_items = parse_text(
        "covergroup-items-malformed.sv",
        "module bad; covergroup cg; coverpoint ; cross only_one; "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto malformed_coverage_iff = parse_text(
        "covergroup-iff-malformed.sv",
        "module bad; covergroup cg; coverpoint value iff (); "
        "endgroup endmodule",
        Language::SystemVerilog2017);
    const auto duplicate_coverage_name = parse_text(
        "covergroup-item-duplicate.sv",
        "module bad; covergroup cg; point: coverpoint first; "
        "point: cross first, second; endgroup endmodule",
        Language::SystemVerilog2017);
    auto unresolved_coverage = parse_text(
        "covergroup-resolution-negative.sv",
        R"(module bad;
  covergroup cg;
    point: coverpoint missing_name;
    bad_cross: cross point, absent_operand;
  endgroup
  cg bad_instance = new(1);
  initial bad_instance.sample(1);
endmodule
)",
        Language::SystemVerilog2017);
    std::vector<Diagnostic> unresolved_diagnostics;
    (void)resolve_systemverilog_covergroups(
        unresolved_coverage.design, unresolved_diagnostics);
    std::string unresolved_failure_codes;
    for (const auto& diagnostic : unresolved_coverage.diagnostics) {
        unresolved_failure_codes += " parse:" + diagnostic.code + "="
            + diagnostic.message;
    }
    for (const auto& diagnostic : unresolved_diagnostics) {
        unresolved_failure_codes += " resolve:" + diagnostic.code;
    }
    unresolved_failure_codes += " variables="
        + std::to_string(unresolved_coverage.design.units.front().variables.size())
        + " instances="
        + std::to_string(
            unresolved_coverage.design.systemverilog_covergroup_instances.size());
    require(
        has_code(missing_name, "FSIM-SV-PARSE-309"),
        "a missing covergroup name has an exact diagnostic");
    require(
        has_code(missing_header, "FSIM-SV-PARSE-310"),
        "a missing covergroup header terminator has an exact diagnostic");
    require(
        has_code(missing_end, "FSIM-SV-PARSE-311"),
        "a missing endgroup has an exact diagnostic");
    require(
        has_code(missing_end_name, "FSIM-SV-PARSE-312"),
        "a missing endgroup label name has an exact diagnostic");
    require(
        has_code(mismatched_end_name, "FSIM-SV-SEM-204"),
        "a mismatched endgroup name has an exact diagnostic");
    require(
        has_code(duplicate, "FSIM-SV-SEM-205"),
        "a duplicate covergroup name has an exact diagnostic");
    require(
        has_code(wrong_language, "FSIM-SV-SEM-203"),
        "non-SystemVerilog covergroup use has an exact diagnostic");
    require(
        has_code(malformed_formal_balance, "FSIM-SV-PARSE-313"),
        "an unbalanced constructor profile has an exact diagnostic");
    require(
        has_code(malformed_formal, "FSIM-SV-PARSE-314"),
        "an empty constructor formal has an exact diagnostic");
    require(
        has_code(duplicate_formal, "FSIM-SV-SEM-206"),
        "a duplicate constructor formal has an exact diagnostic");
    require(
        has_code(malformed_sampling, "FSIM-SV-PARSE-315"),
        "a malformed sampling profile has an exact diagnostic");
    require(
        has_code(malformed_option, "FSIM-SV-PARSE-316"),
        "a malformed covergroup option has an exact diagnostic");
    require(
        std::ranges::count_if(
            malformed_coverage_items.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-317";
            })
            == 2,
        "missing coverpoint expressions and short crosses reject exactly");
    require(
        has_code(malformed_coverage_iff, "FSIM-SV-PARSE-318"),
        "an empty coverpoint iff guard has an exact diagnostic");
    require(
        has_code(duplicate_coverage_name, "FSIM-SV-SEM-207"),
        "duplicate coverpoint/cross names have an exact diagnostic");
    require(
        has_code(malformed_bin, "FSIM-SV-PARSE-319"),
        "a malformed explicit bin has an exact diagnostic");
    require(
        has_code(duplicate_bin, "FSIM-SV-SEM-212"),
        "a duplicate explicit bin has an exact diagnostic");
    require(
        has_code(malformed_bin_value, "FSIM-SV-PARSE-320"),
        "a malformed ranged bin has an exact diagnostic");
    require(
        has_code(excessive_bin_array, "FSIM-SV-SEM-213"),
        "an excessive bin-array expansion has an exact diagnostic");
    require(
        has_code(malformed_transition, "FSIM-SV-PARSE-321"),
        "a malformed transition sequence has an exact diagnostic");
    require(
        has_code(invalid_transition_array, "FSIM-SV-SEM-214"),
        "an empty transition-array expansion has an exact diagnostic");
    require(
        has_code(malformed_bin_iff, "FSIM-SV-PARSE-322"),
        "a malformed bin iff guard has an exact diagnostic");
    require(
        has_code(malformed_cross_bin, "FSIM-SV-PARSE-323"),
        "a malformed explicit cross bin has an exact diagnostic");
    require(
        std::ranges::count_if(
            duplicate_cross_bin.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-215";
            })
            == 1,
        "a duplicate explicit cross bin has one exact diagnostic");
    require(
        std::ranges::count_if(
            invalid_coverage_option.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-217";
            })
            == 2,
        "out-of-range goal and at_least options reject exactly");
    require(
        std::ranges::count_if(
            invalid_percentage_option.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-218";
            })
            == 2,
        "out-of-range covergroup goals and Boolean merge options reject exactly");
    require(
        std::ranges::count_if(
            unsupported_option.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-217";
            })
            == 1,
        "an unsupported coverage option has one exact diagnostic");
    require(
        std::ranges::count_if(
            unsupported_type.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-219";
            })
            == 1,
        "a non-integral coverage formal has one exact diagnostic");
    require(
        with_selection.ok()
            && with_selection.design.units.front()
                    .systemverilog_covergroups.front()
                    .coverage_declarations.front()
                    .bins.front()
                    .with_tokens.size()
                == 3U,
        "a coverpoint with selection retains its executable item predicate");
    require(
        cross_with_selection.ok()
            && std::ranges::any_of(
                cross_with_selection.design.units.front()
                    .systemverilog_covergroups.front()
                    .coverage_declarations.back()
                    .bins.front()
                    .cross_selection_tokens,
                [](const Token& token) { return token.text == "with"; }),
        "a cross with selection retains its complete select expression");
    require(
        std::ranges::any_of(
            empty_cross_diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-216";
            }),
        "an empty named cross selection has an exact diagnostic");
    require(
        std::ranges::count_if(
            ambiguous_type_diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-211";
            })
            == 1,
        "an ambiguous class-qualified covergroup type has one exact diagnostic:"
            + ambiguous_type_failure);
    require(
        std::ranges::any_of(
            unresolved_diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-208";
            })
            && std::ranges::any_of(
                unresolved_diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-209";
                })
            && std::ranges::count_if(
                   unresolved_diagnostics,
                   [](const Diagnostic& diagnostic) {
                       return diagnostic.code == "FSIM-SV-SEM-210";
                   })
                == 2,
        "resolution and constructor/sample profile failures reject exactly:"
            + unresolved_failure_codes);

    SystemVerilogCoverageState live_state;
    SystemVerilogCovergroupDeclaration live_declaration;
    live_declaration.canonical_identity = "work.coverage::group";
    live_state.declarations.push_back(std::move(live_declaration));
    SystemVerilogCovergroupInstance live_instance;
    live_instance.declaration_identity = "work.coverage::group";
    live_instance.runtime_identity = "coverage.group";
    SystemVerilogCoverageBinHit live_hit;
    live_hit.identity = "coverage.group::value.zero";
    live_hit.hit_count = 2U;
    live_hit.at_least = 1U;
    live_hit.covered = true;
    live_instance.bin_hits.push_back(std::move(live_hit));
    live_state.instances.push_back(std::move(live_instance));
    auto persisted_state = live_state;
    persisted_state.instances.front().bin_hits.front().hit_count = 3U;
    std::string merge_error;
    require(
        merge_systemverilog_coverage_state(
            live_state, persisted_state, merge_error)
            && merge_error.empty()
            && live_state.instances.front().bin_hits.front().hit_count == 5U,
        "coverage databases accumulate matching stable bin identities");
    auto mismatched_state = persisted_state;
    mismatched_state.declarations.front().canonical_identity
        = "work.other::group";
    const auto before_rejection = live_state;
    require(
        !merge_systemverilog_coverage_state(
            live_state, mismatched_state, merge_error)
            && !merge_error.empty()
            && live_state.instances.front().bin_hits.front().hit_count
                == before_rejection.instances.front().bin_hits.front().hit_count,
        "coverage database model mismatches reject transactionally");
}

} // namespace fsim::tests::frontend
