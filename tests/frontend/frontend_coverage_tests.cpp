// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::tests::frontend {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

bool has_code(
    const fsim::frontend::ParseResult& result,
    const std::string_view code) {
  return std::ranges::any_of(
      result.diagnostics,
      [&](const fsim::frontend::Diagnostic& diagnostic) {
        return diagnostic.code == code;
      });
}

} // namespace

void test_systemverilog_covergroup_declarations() {
  using namespace fsim::frontend;

  auto parsed = parse_text(
      "covergroups.sv",
      R"(
package coverage_pkg;
  covergroup package_group (int seed) @(posedge seed);
    seed_point: coverpoint seed;
  endgroup : package_group
endpackage : coverage_pkg

interface coverage_if;
  logic clock;
  covergroup interface_group @(posedge clock);
    clock_point: coverpoint clock;
  endgroup
endinterface : coverage_if

program coverage_program;
  covergroup program_group;
  endgroup : program_group
endprogram : coverage_program

module coverage_top;
  logic clock;
  covergroup unit_group @(posedge clock);
    clock_point: coverpoint clock {
      bins values[] = {0, 1};
    }
  endgroup : unit_group

  class Monitor;
    covergroup class_group;
      sample_point: coverpoint sample;
    endgroup : class_group
    int sample;
    task record;
      class_group.sample();
    endtask
  endclass : Monitor
endmodule : coverage_top
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "raw covergroup ownership source must parse");

  const auto unit_for = [&](const std::string_view name) {
    return std::ranges::find(
        parsed.design.units,
        name,
        &DesignUnit::name);
  };
  const auto package = unit_for("coverage_pkg");
  const auto interface = unit_for("coverage_if");
  const auto program = unit_for("coverage_program");
  const auto module = unit_for("coverage_top");
  require(
      package != parsed.design.units.end()
          && interface != parsed.design.units.end()
          && program != parsed.design.units.end()
          && module != parsed.design.units.end(),
      "package, interface, program, and module owners are retained");
  require(
      package->systemverilog_covergroups.size() == 1
          && interface->systemverilog_covergroups.size() == 1
          && program->systemverilog_covergroups.size() == 1
          && module->systemverilog_covergroups.size() == 1,
      "each design unit owns its covergroup declaration");

  const auto& package_group =
      package->systemverilog_covergroups.front();
  require(
      package_group.owner_kind
              == SystemVerilogCovergroupOwnerKind::DesignUnit
          && package_group.name == "package_group"
          && package_group.name_token
          && package_group.end_name == package_group.name
          && package_group.end_name_token
          && !package_group.name_span.empty()
          && !package_group.header_span.empty()
          && !package_group.body_span.empty()
          && !package_group.span.empty(),
      "design-unit covergroup name, end name, spans, and owner are exact");
  require(
      std::ranges::any_of(
          package_group.header_tokens,
          [](const Token& token) { return token.text == "posedge"; })
          && std::ranges::any_of(
              package_group.body_tokens,
              [](const Token& token) {
                return token.text == "coverpoint";
              }),
      "header and body tokens are independently owned");
  require(
      package_group.formals.size() == 1
          && package_group.formals.front().name == "seed"
          && package_group.formals.front().name_token
          && package_group.formals.front().type_tokens.size() == 1
          && package_group.formals.front().type_tokens.front().text == "int"
          && package_group.sampling
          && package_group.sampling->kind
              == SystemVerilogCovergroupSamplingKind::Event
          && std::ranges::any_of(
              package_group.sampling->tokens,
              [](const Token& token) { return token.text == "posedge"; })
          && !package_group.formals.front().span.empty()
          && !package_group.sampling->span.empty(),
      "constructor formals and event sampling own exact structured source");

  require(
      module->systemverilog_classes.size() == 1
          && module->systemverilog_classes.front().covergroups.size() == 1,
      "a class independently owns its covergroup declaration");
  const auto& class_group =
      module->systemverilog_classes.front().covergroups.front();
  require(
      class_group.owner_kind == SystemVerilogCovergroupOwnerKind::Class
          && class_group.name == "class_group"
          && class_group.end_name == class_group.name
          && !class_group.body_span.empty(),
      "class covergroup ownership and source identity are exact");

  auto preprocessed = preprocess_verilog(
      SourceText{
          "coverage-macro.sv",
          R"(`define COVERAGE_NAME macro_group
module macro_owner;
  covergroup `COVERAGE_NAME;
    point: coverpoint value;
  endgroup : `COVERAGE_NAME
  int value;
endmodule
)"},
      Language::SystemVerilog2017);
  auto macro_parsed = parse_verilog(
      std::move(preprocessed.lexed), true);
  require(
      macro_parsed.ok()
          && macro_parsed.design.units.size() == 1
          && macro_parsed.design.units.front()
                 .systemverilog_covergroups.size() == 1,
      "macro-expanded covergroup names parse once");
  const auto& macro_group = macro_parsed.design.units.front()
      .systemverilog_covergroups.front();
  require(
      macro_group.name_token && macro_group.end_name_token
          && !macro_group.name_token->expansion_stack.empty()
          && !macro_group.end_name_token->expansion_stack.empty(),
      "name tokens retain macro definition and invocation ancestry");

  auto structured = parse_text(
      "covergroup-structure.sv",
      R"(module structured_owner;
  covergroup profiled_group (
    input int seed = 3,
    ref logic [3:0] sample_value
  ) with function sample (
    input logic [7:0] payload,
    const ref int lane
  );
    option.weight = seed;
    type_option.merge_instances = 1;
    payload_point: coverpoint payload iff (lane > 0) {
      option.at_least = 2;
      bins values[] = {[0:15]};
    }
    coverpoint sample_value;
    payload_lane: cross payload_point, sample_value iff (seed != 0) {
      bins selected = binsof(payload_point) intersect {[1:7]};
    }
  endgroup : profiled_group
endmodule
)",
      Language::SystemVerilog2017);
  require(
      structured.ok()
          && structured.design.units.size() == 1
          && structured.design.units.front()
                 .systemverilog_covergroups.size() == 1,
      "structured covergroup profile source must parse");
  const auto& profiled = structured.design.units.front()
      .systemverilog_covergroups.front();
  require(
      profiled.formals.size() == 2
          && profiled.formals[0].direction == PortDirection::Input
          && profiled.formals[0].name == "seed"
          && profiled.formals[0].default_tokens.size() == 1
          && profiled.formals[0].default_tokens.front().text == "3"
          && profiled.formals[1].direction == PortDirection::Ref
          && profiled.formals[1].name == "sample_value"
          && profiled.sampling
          && profiled.sampling->kind
              == SystemVerilogCovergroupSamplingKind::WithFunctionSample
          && profiled.sampling->formals.size() == 2
          && profiled.sampling->formals[0].name == "payload"
          && profiled.sampling->formals[0].direction
              == PortDirection::Input
          && profiled.sampling->formals[1].name == "lane"
          && profiled.sampling->formals[1].direction
              == PortDirection::Ref
          && profiled.sampling->formals[1].const_ref
          && !profiled.sampling->formals[1].name_span.empty(),
      "constructor and with-function sample profiles retain exact formals");
  require(
      profiled.option_assignments.size() == 2
          && profiled.option_assignments[0].scope
              == SystemVerilogCovergroupOptionScope::Instance
          && profiled.option_assignments[0].name == "weight"
          && profiled.option_assignments[0].name_token
          && profiled.option_assignments[0].value_tokens.size() == 1
          && profiled.option_assignments[0].value_tokens.front().text == "seed"
          && profiled.option_assignments[1].scope
              == SystemVerilogCovergroupOptionScope::Type
          && profiled.option_assignments[1].name == "merge_instances"
          && profiled.option_assignments[1].value_tokens.front().text == "1"
          && profiled.option_assignments[1].span.source_name
              == "covergroup-structure.sv",
      "only declaration-scope instance/type options are retained in order");
  require(
      profiled.coverage_declarations.size() == 3
          && profiled.coverage_declarations[0].kind
              == SystemVerilogCoverageDeclarationKind::Coverpoint
          && profiled.coverage_declarations[0].explicit_name
          && profiled.coverage_declarations[0].name == "payload_point"
          && profiled.coverage_declarations[0].name_token
          && profiled.coverage_declarations[0].declaration_index == 0
          && profiled.coverage_declarations[0].expression_tokens.size() == 1
          && profiled.coverage_declarations[0]
                 .expression_tokens.front().text == "payload"
          && profiled.coverage_declarations[0].iff_tokens.size() == 3
          && profiled.coverage_declarations[0].iff_tokens.front().text == "lane"
          && std::ranges::any_of(
              profiled.coverage_declarations[0].body_tokens,
              [](const Token& token) { return token.text == "bins"; })
          && profiled.coverage_declarations[0].span.source_name
              == "covergroup-structure.sv",
      "an explicit coverpoint owns expression, iff, body, order, and source");
  require(
      profiled.coverage_declarations[1].kind
              == SystemVerilogCoverageDeclarationKind::Coverpoint
          && !profiled.coverage_declarations[1].explicit_name
          && profiled.coverage_declarations[1].name == "$coverpoint$1"
          && !profiled.coverage_declarations[1].name_token
          && profiled.coverage_declarations[1].declaration_index == 1
          && profiled.coverage_declarations[1].expression_tokens.front().text
              == "sample_value"
          && profiled.coverage_declarations[2].kind
              == SystemVerilogCoverageDeclarationKind::Cross
          && profiled.coverage_declarations[2].name == "payload_lane"
          && profiled.coverage_declarations[2].declaration_index == 2
          && profiled.coverage_declarations[2].cross_operands.size() == 2
          && profiled.coverage_declarations[2].cross_operands[0].name
              == "payload_point"
          && profiled.coverage_declarations[2].cross_operands[1].name
              == "sample_value"
          && profiled.coverage_declarations[2].iff_tokens.front().text == "seed"
          && !profiled.coverage_declarations[2].body_span.empty(),
      "synthesized coverpoint and explicit cross identities preserve order");

  std::vector<Diagnostic> resolution_diagnostics;
  require(
      resolve_systemverilog_covergroups(
          structured.design, resolution_diagnostics)
          && resolution_diagnostics.empty(),
      "structured coverage names resolve without diagnostics");
  const auto& resolved_profiled = structured.design.units.front()
      .systemverilog_covergroups.front();
  require(
      resolved_profiled.owner_identity == "work.structured_owner"
          && resolved_profiled.canonical_identity
              == "work.structured_owner::profiled_group"
          && resolved_profiled.specialization_identity.starts_with(
              resolved_profiled.canonical_identity)
          && resolved_profiled.runtime_identity_prefix
              == resolved_profiled.canonical_identity + "@"
          && resolved_profiled.coverage_declarations[0].references.size() == 2
          && resolved_profiled.coverage_declarations[0].references[0].kind
              == SystemVerilogCoverageReferenceKind::SampleFormal
          && resolved_profiled.coverage_declarations[0].references[1].kind
              == SystemVerilogCoverageReferenceKind::SampleFormal
          && resolved_profiled.coverage_declarations[2]
                 .cross_operands[0].resolved_declaration_index == 0
          && resolved_profiled.coverage_declarations[2]
                 .cross_operands[1].implicit_coverpoint,
      "canonical identities and formal/cross references resolve exactly");

  auto resolved_instances = parse_text(
      "covergroup-resolution.sv",
      R"(package coverage_types;
  covergroup packet_group (int seed = 1)
      with function sample (input int value);
    option.weight = seed;
    value_point: coverpoint value iff (seed > 0);
    value_seed: cross value_point, seed;
  endgroup : packet_group
endpackage

module coverage_user;
  coverage_types::packet_group monitor = new(7);
  initial monitor.sample(3);
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> instance_diagnostics;
  const auto instances_resolved = resolve_systemverilog_covergroups(
      resolved_instances.design, instance_diagnostics);
  std::string instance_failure_codes;
  for (const auto& diagnostic : resolved_instances.diagnostics) {
    instance_failure_codes += " parse:" + diagnostic.code;
  }
  for (const auto& diagnostic : instance_diagnostics) {
    instance_failure_codes += " resolve:" + diagnostic.code;
  }
  require(
      resolved_instances.ok()
          && instances_resolved
          && instance_diagnostics.empty(),
      "package-qualified covergroup instances and sample calls resolve"
          + instance_failure_codes);
  const auto& resolved_type =
      resolved_instances.design.units[0].systemverilog_covergroups.front();
  require(
      resolved_type.canonical_identity
              == "work.coverage_types::packet_group"
          && resolved_type.coverage_declarations[0].references.size() == 2
          && resolved_type.coverage_declarations[0].references[0].kind
              == SystemVerilogCoverageReferenceKind::SampleFormal
          && resolved_type.coverage_declarations[0].references[1].kind
              == SystemVerilogCoverageReferenceKind::ConstructorFormal
          && resolved_type.coverage_declarations[1]
                 .cross_operands[0].resolved_declaration_index == 0
          && resolved_type.coverage_declarations[1]
                 .cross_operands[1].implicit_coverpoint,
      "package type, sample/constructor formals, and cross operands resolve");
  require(
      resolved_instances.design.systemverilog_covergroup_instances.size() == 1
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .name == "monitor"
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .declaration_identity
              == resolved_type.canonical_identity
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .constructor_actuals.size() == 1
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .initial_option_state.size() == 1
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .initial_option_state[0].name == "weight"
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .runtime_identity.find("monitor@")
              != std::string::npos
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .sample_calls.size() == 1
          && resolved_instances.design.systemverilog_covergroup_instances[0]
                 .sample_calls[0].actuals.size() == 1,
      "constructor/sample actuals and runtime instance identity are exact");

  std::vector<Diagnostic> class_resolution_diagnostics;
  require(
      resolve_systemverilog_covergroups(
          parsed.design, class_resolution_diagnostics)
          && class_resolution_diagnostics.empty()
          && std::ranges::any_of(
              parsed.design.systemverilog_covergroup_instances,
              [](const SystemVerilogCovergroupInstance& instance) {
                return instance.class_member_template
                    && instance.name == "class_group"
                    && instance.runtime_identity.ends_with("<object>")
                    && instance.sample_calls.size() == 1;
              }),
      "class-owned covergroups retain deterministic per-object templates "
      "and method sample calls");

  auto scalar_bins = parse_text(
      "covergroup-bins.sv",
      R"(module bin_owner;
  covergroup bin_group with function sample (input int value);
    value_point: coverpoint value {
      bins low = {1, -2};
      ignore_bins skipped = {3};
      illegal_bins forbidden = {4};
      bins other = default;
    }
    coverpoint value;
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> bin_resolution_diagnostics;
  require(
      scalar_bins.ok()
          && resolve_systemverilog_covergroups(
              scalar_bins.design, bin_resolution_diagnostics)
          && bin_resolution_diagnostics.empty(),
      "scalar and automatic bin source resolves");
  const auto& bin_group = scalar_bins.design.units.front()
      .systemverilog_covergroups.front();
  const auto& value_point = bin_group.coverage_declarations[0];
  const auto& automatic_point = bin_group.coverage_declarations[1];
  require(
      value_point.bins.size() == 4
          && value_point.bins[0].name == "low"
          && value_point.bins[0].values.size() == 2
          && value_point.bins[0].values[1].exact_value == -2
          && value_point.bins[1].kind
              == SystemVerilogCoverageBinKind::Ignore
          && value_point.bins[2].kind
              == SystemVerilogCoverageBinKind::Illegal
          && value_point.bins[3].selection
              == SystemVerilogCoverageBinSelection::Default
          && automatic_point.bins.size() == 1
          && automatic_point.bins.front().selection
              == SystemVerilogCoverageBinSelection::Automatic,
      "explicit, ignored, illegal, default, and automatic bins own source order");

  SystemVerilogCovergroupInstance bin_instance;
  bin_instance.declaration_identity = bin_group.canonical_identity;
  std::vector<Diagnostic> sample_diagnostics;
  const auto low_first = sample_systemverilog_coverpoint(
      bin_instance, bin_group, 0U, 1, sample_diagnostics);
  const auto low_second = sample_systemverilog_coverpoint(
      bin_instance, bin_group, 0U, 1, sample_diagnostics);
  const auto ignored = sample_systemverilog_coverpoint(
      bin_instance, bin_group, 0U, 3, sample_diagnostics);
  const auto illegal = sample_systemverilog_coverpoint(
      bin_instance, bin_group, 0U, 4, sample_diagnostics);
  const auto fallback = sample_systemverilog_coverpoint(
      bin_instance, bin_group, 0U, 9, sample_diagnostics);
  const auto automatic = sample_systemverilog_coverpoint(
      bin_instance, bin_group, 1U, 7, sample_diagnostics);
  require(
      low_first.hit_bin_identities.size() == 1
          && low_second.hit_bin_identities == low_first.hit_bin_identities
          && ignored.ignored && ignored.hit_bin_identities.empty()
          && illegal.illegal && illegal.hit_bin_identities.size() == 1
          && fallback.hit_bin_identities.size() == 1
          && automatic.hit_bin_identities.size() == 1
          && automatic.hit_bin_identities.front().ends_with("$auto[7]")
          && bin_instance.bin_hits.size() == 4
          && bin_instance.bin_hits[0].hit_count == 2
          && bin_instance.illegal_bin_reports.size() == 1
          && sample_diagnostics.size() == 1
          && sample_diagnostics.front().code == "FSIM-SV-COV-001",
      "sampling updates exact hit state and reports illegal bins deterministically");

  auto expanded_bins = parse_text(
      "covergroup-expanded-bins.sv",
      R"(module expanded_bin_owner;
  covergroup expanded_group with function sample (input int value);
    value_point: coverpoint value {
      bins window = {[2:4], 8'hff, 4'shF};
      wildcard bins pattern = {4'b1?0?};
      bins lanes[] = {[5:7]};
      bins groups[2] = {10, 11, 18, 19};
    }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> expanded_resolution_diagnostics;
  require(
      expanded_bins.ok()
          && resolve_systemverilog_covergroups(
              expanded_bins.design, expanded_resolution_diagnostics)
          && expanded_resolution_diagnostics.empty(),
      "ranged, wildcard, typed, and arrayed bin source resolves");
  const auto& expanded_group = expanded_bins.design.units.front()
      .systemverilog_covergroups.front();
  const auto& expanded_point =
      expanded_group.coverage_declarations.front();
  require(
      expanded_point.bins.size() == 7
          && expanded_point.bins[0].name == "window"
          && expanded_point.bins[0].values.size() == 3
          && expanded_point.bins[0].values[0].range_left == 2
          && expanded_point.bins[0].values[0].range_right == 4
          && expanded_point.bins[0].values[1].exact_value == 255
          && expanded_point.bins[0].values[2].exact_value == -1
          && expanded_point.bins[1].name == "pattern"
          && expanded_point.bins[1].wildcard
          && expanded_point.bins[1].values[0].wildcard_mask == 0xaU
          && expanded_point.bins[1].values[0].wildcard_value == 0x8U
          && expanded_point.bins[2].name == "lanes[0]"
          && expanded_point.bins[2].source_name == "lanes"
          && expanded_point.bins[2].array_index == 0
          && expanded_point.bins[2].values[0].exact_value == 5
          && expanded_point.bins[4].name == "lanes[2]"
          && expanded_point.bins[4].values[0].exact_value == 7
          && expanded_point.bins[5].name == "groups[0]"
          && expanded_point.bins[5].declared_array_size == 2
          && expanded_point.bins[5].values.size() == 2
          && expanded_point.bins[5].values[1].exact_value == 11
          && expanded_point.bins[6].name == "groups[1]"
          && expanded_point.bins[6].values.size() == 2
          && expanded_point.bins[6].values[1].exact_value == 19,
      "bin expansion preserves conversions, order, grouping, and stable identities");

  SystemVerilogCovergroupInstance expanded_instance;
  expanded_instance.declaration_identity = expanded_group.canonical_identity;
  std::vector<Diagnostic> expanded_sample_diagnostics;
  const auto ranged = sample_systemverilog_coverpoint(
      expanded_instance, expanded_group, 0U, 3,
      expanded_sample_diagnostics);
  const auto typed = sample_systemverilog_coverpoint(
      expanded_instance, expanded_group, 0U, -1,
      expanded_sample_diagnostics);
  const auto wildcard = sample_systemverilog_coverpoint(
      expanded_instance, expanded_group, 0U, 9,
      expanded_sample_diagnostics);
  const auto unsized_array = sample_systemverilog_coverpoint(
      expanded_instance, expanded_group, 0U, 5,
      expanded_sample_diagnostics);
  const auto sized_array = sample_systemverilog_coverpoint(
      expanded_instance, expanded_group, 0U, 19,
      expanded_sample_diagnostics);
  const auto sampled_identity = [](const auto& sample) {
    return sample.hit_bin_identities.empty()
        ? std::string{"<none>"} : sample.hit_bin_identities.front();
  };
  require(
      sampled_identity(ranged).ends_with(".window")
          && sampled_identity(typed).ends_with(".window")
          && sampled_identity(wildcard).ends_with(".pattern")
          && sampled_identity(unsized_array).ends_with(".lanes[0]")
          && sampled_identity(sized_array).ends_with(".groups[1]")
          && expanded_instance.bin_hits.size() == 4
          && expanded_sample_diagnostics.empty(),
      "sampling matches ranges, wildcards, conversions, and expanded identities: "
          + sampled_identity(ranged) + ", " + sampled_identity(typed) + ", "
          + sampled_identity(wildcard) + ", "
          + sampled_identity(unsized_array) + ", "
          + sampled_identity(sized_array) + ", hits="
          + std::to_string(expanded_instance.bin_hits.size()));

  auto transition_bins = parse_text(
      "covergroup-transition-bins.sv",
      R"(module transition_bin_owner;
  covergroup transition_group with function sample (input int value);
    value_point: coverpoint value {
      bins simple = (1 => 2 => 3);
      bins consecutive = (4[*2] => 5);
      bins jumped = (6[->2] => 7);
      bins nonconsecutive = (8[=2] => 9);
      bins delayed = (10 ##[2:3] 11);
      bins paths[] = (12 => 13), (14 => 15);
      bins overlap_first = (20 => 21);
      bins overlap_second = (20 => 21);
    }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> transition_resolution_diagnostics;
  require(
      transition_bins.ok()
          && resolve_systemverilog_covergroups(
              transition_bins.design, transition_resolution_diagnostics)
          && transition_resolution_diagnostics.empty(),
      "transition sequence source resolves");
  const auto& transition_group = transition_bins.design.units.front()
      .systemverilog_covergroups.front();
  const auto& transition_point =
      transition_group.coverage_declarations.front();
  require(
      transition_point.bins.size() == 9
          && transition_point.bins[0].transitions.front().steps.size() == 3
          && transition_point.bins[1].transitions.front().steps[0]
                 .repetition.kind
              == SystemVerilogCoverageTransitionRepetitionKind::Consecutive
          && transition_point.bins[1].transitions.front().steps[0]
                 .repetition.minimum == 2
          && transition_point.bins[2].transitions.front().steps[0]
                 .repetition.kind
              == SystemVerilogCoverageTransitionRepetitionKind::Goto
          && transition_point.bins[3].transitions.front().steps[0]
                 .repetition.kind
              == SystemVerilogCoverageTransitionRepetitionKind::Nonconsecutive
          && transition_point.bins[4].transitions.front().delays[0].minimum == 2
          && transition_point.bins[4].transitions.front().delays[0].maximum == 3
          && transition_point.bins[5].name == "paths[0]"
          && transition_point.bins[5].transitions.size() == 1
          && transition_point.bins[6].name == "paths[1]"
          && transition_point.bins[6].array_index == 1,
      "transition ownership retains sequence, repetition, delay, and array identity");

  SystemVerilogCovergroupInstance transition_instance;
  transition_instance.declaration_identity =
      transition_group.canonical_identity;
  std::vector<Diagnostic> transition_sample_diagnostics;
  const auto sample_transition = [&](const std::int64_t value) {
    return sample_systemverilog_coverpoint(
        transition_instance, transition_group, 0U, value,
        transition_sample_diagnostics);
  };
  (void)sample_transition(1);
  (void)sample_transition(2);
  const auto simple_transition = sample_transition(3);
  (void)sample_transition(4);
  (void)sample_transition(4);
  const auto consecutive_transition = sample_transition(5);
  (void)sample_transition(6);
  (void)sample_transition(0);
  (void)sample_transition(6);
  const auto goto_transition = sample_transition(7);
  (void)sample_transition(8);
  (void)sample_transition(0);
  (void)sample_transition(8);
  (void)sample_transition(0);
  const auto nonconsecutive_transition = sample_transition(9);
  (void)sample_transition(10);
  (void)sample_transition(0);
  const auto ranged_delay_transition = sample_transition(11);
  (void)sample_transition(12);
  const auto first_array_transition = sample_transition(13);
  (void)sample_transition(14);
  const auto second_array_transition = sample_transition(15);
  (void)sample_transition(20);
  const auto overlap_transition = sample_transition(21);
  require(
      sampled_identity(simple_transition).ends_with(".simple")
          && sampled_identity(consecutive_transition)
                 .ends_with(".consecutive")
          && sampled_identity(goto_transition).ends_with(".jumped")
          && sampled_identity(nonconsecutive_transition)
                 .ends_with(".nonconsecutive")
          && sampled_identity(ranged_delay_transition).ends_with(".delayed")
          && sampled_identity(first_array_transition).ends_with(".paths[0]")
          && sampled_identity(second_array_transition).ends_with(".paths[1]")
          && sampled_identity(overlap_transition).ends_with(".overlap_first")
          && transition_instance.bin_hits.size() == 8
          && transition_sample_diagnostics.empty(),
      "transition sampling advances per-instance state with source-order overlap");

  auto guarded_bins = parse_text(
      "covergroup-guarded-bins.sv",
      R"(module guarded_bin_owner;
  covergroup guarded_group with function sample (input int value);
    scalar_point: coverpoint value iff (value != 0) {
      bins regular = {1, 2};
      illegal_bins illegal = {2};
      ignore_bins ignored = {1};
      bins guarded = {4} iff (value > 3);
      bins overlap_first = {5};
      bins overlap_second = {5};
      bins fallback = default;
    }
    wildcard_point: coverpoint value {
      wildcard bins pattern = {4'b1?0?};
      bins fallback = default;
    }
    transition_point: coverpoint value {
      bins path = (10 => 11);
      bins other = default sequence;
    }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> guarded_resolution_diagnostics;
  std::string guarded_failure_codes;
  for (const auto& diagnostic : guarded_bins.diagnostics) {
    guarded_failure_codes += " parse:" + diagnostic.code + "="
        + diagnostic.message;
  }
  const bool guarded_resolved = resolve_systemverilog_covergroups(
      guarded_bins.design, guarded_resolution_diagnostics);
  for (const auto& diagnostic : guarded_resolution_diagnostics) {
    guarded_failure_codes += " resolve:" + diagnostic.code + "="
        + diagnostic.message;
  }
  require(
      guarded_bins.ok()
          && guarded_resolved
          && guarded_resolution_diagnostics.empty(),
      "guarded and default-sequence bin source resolves: "
          + guarded_failure_codes);
  const auto& guarded_group = guarded_bins.design.units.front()
      .systemverilog_covergroups.front();
  require(
      guarded_group.coverage_declarations[0].iff_tokens.size() == 3
          && guarded_group.coverage_declarations[0].bins[3]
                 .iff_tokens.size() == 3
          && guarded_group.coverage_declarations[2].bins[1].selection
              == SystemVerilogCoverageBinSelection::DefaultSequence,
      "coverpoint/bin guards and default-sequence selection retain ownership");

  SystemVerilogCovergroupInstance guarded_instance;
  guarded_instance.declaration_identity = guarded_group.canonical_identity;
  std::vector<Diagnostic> guarded_sample_diagnostics;
  const auto gated_coverpoint = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 0U, 0,
      guarded_sample_diagnostics);
  const auto ignored_precedence = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 0U, 1,
      guarded_sample_diagnostics);
  const auto illegal_precedence = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 0U, 2,
      guarded_sample_diagnostics);
  const auto guarded_match = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 0U, 4,
      guarded_sample_diagnostics);
  const auto overlap_match = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 0U, 5,
      guarded_sample_diagnostics);
  const auto wildcard_unknown_dontcare = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 1U,
      SystemVerilogCoverageSampleValue{8, 0x1U, 4U},
      guarded_sample_diagnostics);
  const auto wildcard_unknown_known_bit = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 1U,
      SystemVerilogCoverageSampleValue{8, 0x8U, 4U},
      guarded_sample_diagnostics);
  const auto first_unmatched_transition = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 2U, 30,
      guarded_sample_diagnostics);
  const auto default_sequence = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 2U, 31,
      guarded_sample_diagnostics);
  const auto transition_started = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 2U, 10,
      guarded_sample_diagnostics);
  const auto transition_completed = sample_systemverilog_coverpoint(
      guarded_instance, guarded_group, 2U, 11,
      guarded_sample_diagnostics);
  require(
      gated_coverpoint.hit_bin_identities.empty()
          && ignored_precedence.ignored
          && illegal_precedence.illegal
          && sampled_identity(guarded_match).ends_with(".guarded")
          && sampled_identity(overlap_match).ends_with(".overlap_first")
          && sampled_identity(wildcard_unknown_dontcare).ends_with(".pattern")
          && sampled_identity(wildcard_unknown_known_bit).ends_with(".fallback")
          && first_unmatched_transition.hit_bin_identities.empty()
          && sampled_identity(default_sequence).ends_with(".other")
          && transition_started.hit_bin_identities.empty()
          && sampled_identity(transition_completed).ends_with(".path")
          && guarded_sample_diagnostics.size() == 1
          && guarded_sample_diagnostics.front().code == "FSIM-SV-COV-001",
      "one sampling path enforces guards, four-state matching, precedence, and defaults");

  auto automatic_cross = parse_text(
      "covergroup-automatic-cross.sv",
      R"(module cross_owner;
  covergroup cross_group with function sample (input int left, input int right);
    left_point: coverpoint left {
      bins low = {1};
      ignore_bins excluded = {2};
    }
    right_point: coverpoint right {
      bins high = {3};
    }
    pair: cross left_point, right_point {
      type_option.weight = 2;
      type_option.goal = 80;
      option.at_least = 2;
      bins selected = binsof(left_point.low)
          && binsof(right_point).high intersect {[3:4]};
      ignore_bins complement = !binsof(left_point.low)
          && binsof(right_point);
    }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> cross_resolution_diagnostics;
  require(
      automatic_cross.ok()
          && resolve_systemverilog_covergroups(
              automatic_cross.design, cross_resolution_diagnostics)
          && cross_resolution_diagnostics.empty(),
      "automatic cross source resolves");
  const auto& cross_group = automatic_cross.design.units.front()
      .systemverilog_covergroups.front();
  require(
      cross_group.coverage_declarations[2].bins.size() == 2
          && cross_group.coverage_declarations[2].bins[0].name == "selected"
          && !cross_group.coverage_declarations[2].bins[0]
                  .cross_selection_tokens.empty()
          && cross_group.coverage_declarations[2].bins[1].kind
              == SystemVerilogCoverageBinKind::Ignore
          && cross_group.coverage_declarations[2].bins[0].weight == 2
          && cross_group.coverage_declarations[2].bins[0].goal == 80
          && cross_group.coverage_declarations[2].bins[0].at_least == 2,
      "explicit cross bins retain named, Boolean, complement, and intersect source");
  SystemVerilogCovergroupInstance cross_instance;
  cross_instance.declaration_identity = cross_group.canonical_identity;
  std::vector<Diagnostic> cross_sample_diagnostics;
  const std::vector<SystemVerilogCovergroupSampleInput> included_inputs{
      {0U, {1, 0U, 64U}}, {1U, {3, 0U, 64U}}};
  const auto included_cross_first = sample_systemverilog_covergroup(
      cross_instance, cross_group, included_inputs,
      cross_sample_diagnostics);
  const auto included_cross_second = sample_systemverilog_covergroup(
      cross_instance, cross_group, included_inputs,
      cross_sample_diagnostics);
  const std::vector<SystemVerilogCovergroupSampleInput> excluded_inputs{
      {0U, {2, 0U, 64U}}, {1U, {3, 0U, 64U}}};
  const auto excluded_cross = sample_systemverilog_covergroup(
      cross_instance, cross_group, excluded_inputs,
      cross_sample_diagnostics);
  const auto cross_state_before_invalid = cross_instance.cross_bin_state;
  const std::vector<SystemVerilogCovergroupSampleInput> duplicate_inputs{
      {0U, {1, 0U, 64U}}, {0U, {3, 0U, 64U}}};
  const auto invalid_cross_transaction = sample_systemverilog_covergroup(
      cross_instance, cross_group, duplicate_inputs,
      cross_sample_diagnostics);
  require(
      included_cross_first.coverpoints.size() == 2
          && included_cross_first.hit_cross_bin_identities.size() == 1
          && included_cross_first.hit_cross_bin_identities.front()
                 .find("pair.selected<") != std::string::npos
          && included_cross_second.hit_cross_bin_identities
              == included_cross_first.hit_cross_bin_identities
          && excluded_cross.excluded_cross_bin_identities.size() == 1
          && cross_instance.cross_bin_state.size() == 2
          && cross_instance.cross_bin_state[0].hit_count == 2
          && cross_instance.cross_bin_state[0].weight == 2
          && cross_instance.cross_bin_state[0].goal == 80
          && cross_instance.cross_bin_state[0].at_least == 2
          && cross_instance.cross_bin_state[0].covered
          && !cross_instance.cross_bin_state[0].excluded
          && cross_instance.cross_bin_state[0]
                 .operand_bin_identities.size() == 2
          && cross_instance.cross_bin_state[0]
                 .operand_bin_identities[0].ends_with(".low")
          && cross_instance.cross_bin_state[0]
                 .operand_bin_identities[1].ends_with(".high")
          && cross_instance.cross_bin_state[1].excluded
          && cross_instance.cross_bin_state[1].exclusion_count == 1
          && invalid_cross_transaction.coverpoints.empty()
          && cross_instance.cross_bin_state.size()
              == cross_state_before_invalid.size()
          && cross_sample_diagnostics.empty(),
      "automatic cross tuples sample transactionally with stable hit/exclusion state");

  cross_instance.cross_bin_state[0].hit_count =
      std::numeric_limits<std::uint64_t>::max();
  const auto cross_overflow = sample_systemverilog_covergroup(
      cross_instance, cross_group, included_inputs,
      cross_sample_diagnostics);
  require(
      cross_overflow.hit_cross_bin_identities.empty()
          && cross_instance.cross_bin_state[0].hit_count
              == std::numeric_limits<std::uint64_t>::max()
          && cross_sample_diagnostics.size() == 1
          && cross_sample_diagnostics.front().code == "FSIM-SV-COV-002",
      "cross hit counts stop exactly at the uint64 resource bound");

  auto weighted_options = parse_text(
      "covergroup-weighted-options.sv",
      R"(module option_owner;
  covergroup option_group with function sample (input int value);
    type_option.goal = 80;
    type_option.merge_instances = 1;
    option.goal = 50;
    option.per_instance = 1;
    excluded_point: coverpoint value {
      type_option.weight = 4;
      type_option.goal = 80;
      type_option.at_least = 3;
      option.weight = 0;
      option.goal = 90;
      option.at_least = 2;
      bins excluded = {1};
    }
    threshold_point: coverpoint value {
      type_option.weight = 2;
      type_option.goal = 80;
      type_option.at_least = 3;
      option.at_least = 2;
    }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> option_resolution_diagnostics;
  require(
      weighted_options.ok()
          && resolve_systemverilog_covergroups(
              weighted_options.design, option_resolution_diagnostics)
          && option_resolution_diagnostics.empty(),
      "bounded coverage options resolve");
  const auto& option_group = weighted_options.design.units.front()
      .systemverilog_covergroups.front();
  const auto& excluded_point = option_group.coverage_declarations[0];
  const auto& threshold_point = option_group.coverage_declarations[1];
  require(
      option_group.effective_type_goal == 80
          && option_group.effective_instance_goal == 50
          && option_group.effective_merge_instances
          && option_group.effective_per_instance
          && excluded_point.option_assignments.size() == 6
          && excluded_point.effective_weight == 0
          && excluded_point.effective_goal == 90
          && excluded_point.effective_at_least == 2
          && excluded_point.bins.front().weight == 0
          && excluded_point.bins.front().goal == 90
          && excluded_point.bins.front().at_least == 2
          && threshold_point.effective_weight == 2
          && threshold_point.effective_goal == 80
          && threshold_point.effective_at_least == 2
          && threshold_point.bins.size() == 1
          && threshold_point.bins.front().selection
              == SystemVerilogCoverageBinSelection::Automatic
          && threshold_point.bins.front().weight == 2
          && threshold_point.bins.front().goal == 80
          && threshold_point.bins.front().at_least == 2,
      "instance options override type options and propagate to every bin");
  SystemVerilogCovergroupInstance option_instance;
  option_instance.declaration_identity = option_group.canonical_identity;
  std::vector<Diagnostic> option_sample_diagnostics;
  const auto zero_weight = sample_systemverilog_coverpoint(
      option_instance, option_group, 0U, 1, option_sample_diagnostics);
  const auto threshold_first = sample_systemverilog_coverpoint(
      option_instance, option_group, 1U, 2, option_sample_diagnostics);
  const auto threshold_second = sample_systemverilog_coverpoint(
      option_instance, option_group, 1U, 2, option_sample_diagnostics);
  require(
      zero_weight.selected_bin_identity
          && zero_weight.zero_weight_excluded
          && zero_weight.hit_bin_identities.empty()
          && !threshold_first.threshold_reached
          && threshold_second.threshold_reached
          && option_instance.bin_hits.size() == 1
          && option_instance.bin_hits.front().hit_count == 2
          && option_instance.bin_hits.front().at_least == 2
          && option_instance.bin_hits.front().covered
          && option_sample_diagnostics.empty(),
      "zero-weight bins are excluded and at_least changes coverage at the exact hit");
  option_instance.bin_hits.front().hit_count =
      std::numeric_limits<std::uint64_t>::max();
  const auto option_overflow = sample_systemverilog_coverpoint(
      option_instance, option_group, 1U, 2, option_sample_diagnostics);
  require(
      option_overflow.hit_count_overflow
          && option_overflow.hit_bin_identities.empty()
          && option_instance.bin_hits.front().hit_count
              == std::numeric_limits<std::uint64_t>::max()
          && option_sample_diagnostics.size() == 1
          && option_sample_diagnostics.front().code == "FSIM-SV-COV-002",
      "coverpoint hit counts stop exactly at the uint64 resource bound");

  const auto cross_percentage =
      calculate_systemverilog_covergroup_instance_percentage(
          cross_group, cross_instance);
  require(
      cross_percentage.declarations.size() == 3
          && cross_percentage.declarations[2].kind
              == SystemVerilogCoverageDeclarationKind::Cross
          && cross_percentage.declarations[2].eligible_weight == 2
          && cross_percentage.declarations[2].covered_weight == 2
          && cross_percentage.declarations[2].coverage.basis_points == 10'000,
      "explicit cross percentages use logical regular bins and ignore exclusions");

  auto percentages = parse_text(
      "covergroup-percentages.sv",
      R"(module percentage_owner;
  covergroup percentage_group with function sample (input int value);
    option.goal = 80;
    option.per_instance = 1;
    type_option.goal = 50;
    type_option.merge_instances = 0;
    thirds: coverpoint value {
      bins one = {1};
      bins two = {2};
      bins three = {3};
    }
    excluded: coverpoint value {
      option.weight = 0;
      bins irrelevant = {9};
    }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> percentage_resolution_diagnostics;
  require(
      percentages.ok()
          && resolve_systemverilog_covergroups(
              percentages.design, percentage_resolution_diagnostics)
          && percentage_resolution_diagnostics.empty(),
      "percentage source resolves");
  auto& percentage_group = percentages.design.units.front()
      .systemverilog_covergroups.front();
  SystemVerilogCovergroupInstance percentage_z;
  percentage_z.declaration_identity =
      percentage_group.canonical_identity;
  percentage_z.runtime_identity = "z-instance";
  SystemVerilogCovergroupInstance percentage_a;
  percentage_a.declaration_identity =
      percentage_group.canonical_identity;
  percentage_a.runtime_identity = "a-instance";
  std::vector<Diagnostic> percentage_sample_diagnostics;
  (void)sample_systemverilog_coverpoint(
      percentage_z, percentage_group, 0U, 1,
      percentage_sample_diagnostics);
  const auto instance_percentage =
      calculate_systemverilog_covergroup_instance_percentage(
          percentage_group, percentage_z);
  const std::vector<SystemVerilogCovergroupInstance>
      percentage_instances{percentage_z, percentage_a};
  const auto unmerged_percentage =
      calculate_systemverilog_covergroup_type_percentage(
          percentage_group, percentage_instances);
  const auto empty_percentage =
      calculate_systemverilog_covergroup_type_percentage(
          percentage_group,
          std::span<const SystemVerilogCovergroupInstance>{});
  require(
      instance_percentage.declarations.size() == 2
          && instance_percentage.declarations[0].coverage.raw_basis_points
              == 3'333
          && instance_percentage.declarations[0].coverage.basis_points
              == 3'333
          && instance_percentage.declarations[1].coverage.empty
          && instance_percentage.declarations[1].coverage.basis_points == 0
          && instance_percentage.coverage.raw_basis_points == 3'333
          && instance_percentage.coverage.basis_points == 4'166
          && unmerged_percentage.per_instance
          && !unmerged_percentage.merge_instances
          && unmerged_percentage.instances.size() == 2
          && unmerged_percentage.instances[0].runtime_identity == "a-instance"
          && unmerged_percentage.instances[1].runtime_identity == "z-instance"
          && unmerged_percentage.coverage.raw_basis_points == 2'083
          && unmerged_percentage.coverage.basis_points == 4'166
          && empty_percentage.coverage.empty
          && empty_percentage.coverage.basis_points == 0
          && percentage_sample_diagnostics.empty(),
      "instance/type percentages round half-up, retain empty zero, and order identities");
  (void)sample_systemverilog_coverpoint(
      percentage_a, percentage_group, 0U, 2,
      percentage_sample_diagnostics);
  const std::vector<SystemVerilogCovergroupInstance>
      merged_instances{percentage_z, percentage_a};
  percentage_group.effective_merge_instances = true;
  const auto merged_percentage =
      calculate_systemverilog_covergroup_type_percentage(
          percentage_group, merged_instances);
  require(
      merged_percentage.merge_instances
          && merged_percentage.coverage.raw_basis_points == 8'334
          && merged_percentage.coverage.basis_points == 10'000
          && merged_percentage.coverage.goal_reached,
      "merge_instances unions instance hit state before instance/type goals");
  const auto percentage_report = build_systemverilog_coverage_report(
      percentage_group, merged_instances);
  const std::vector<SystemVerilogCovergroupInstance>
      reversed_report_instances{percentage_a, percentage_z};
  const auto reversed_percentage_report =
      build_systemverilog_coverage_report(
          percentage_group, reversed_report_instances);
  const auto percentage_text =
      render_systemverilog_coverage_report(percentage_report);
  const auto reversed_percentage_text =
      render_systemverilog_coverage_report(reversed_percentage_report);
  const auto* queried_instance = query_systemverilog_coverage_instance(
      percentage_report, "a-instance");
  const auto* queried_item = query_systemverilog_coverage_item(
      percentage_report,
      percentage_report.instances[0].items[0].identity);
  const auto first_bin_identity =
      percentage_report.instances[1].items[0].bins[0].identity;
  const auto* queried_bin = query_systemverilog_coverage_bin(
      percentage_report, first_bin_identity);
  require(
      percentage_report.instances.size() == 2
          && percentage_report.instances[0].runtime_identity == "a-instance"
          && percentage_report.instances[1].runtime_identity == "z-instance"
          && queried_instance
          && queried_instance->items.size() == 2
          && queried_item
          && queried_item->bins.size() == 3
          && queried_bin
          && queried_bin->source_name == "one"
          && queried_bin->hit_count == 1
          && queried_bin->covered
          && queried_bin->span.source_name == "covergroup-percentages.sv"
          && query_systemverilog_coverage_bin(
                 percentage_report, "missing") == nullptr
          && percentage_text == reversed_percentage_text
          && percentage_text.find("coverage=100.00%")
              != std::string::npos
          && percentage_text.find("hits=1 exclusions=0")
              != std::string::npos
          && percentage_text.find("source=covergroup-percentages.sv")
              != std::string::npos,
      "structured queries and text reports retain complete stable ordered state");
  cross_instance.runtime_identity = "cross-instance";
  const std::vector<SystemVerilogCovergroupInstance> cross_report_instances{
      cross_instance};
  const auto cross_report = build_systemverilog_coverage_report(
      cross_group, cross_report_instances);
  require(
      cross_report.instances.size() == 1
          && cross_report.instances[0].items.size() == 3
          && cross_report.instances[0].items[2].bins.size() == 2
          && cross_report.instances[0].items[2].bins[0].weight == 2
          && cross_report.instances[0].items[2].bins[0].goal == 80
          && cross_report.instances[0].items[2].bins[0].at_least == 2
          && cross_report.instances[0].items[2].bins[0].covered
          && cross_report.instances[0].items[2].bins[1].excluded
          && cross_report.instances[0].items[2].bins[1].exclusion_count == 1,
      "cross reports expose hit, exclusion, goal, threshold, and tuple identity");
  const std::vector<SystemVerilogCoverageRoot> observation_roots{
      {"top.coverage", percentage_report},
      {"secondary.coverage", cross_report}};
  const std::vector<SystemVerilogCoverageAlias> observation_aliases{
      {"alias.coverage", "top.coverage"}};
  const auto debug_snapshot =
      build_systemverilog_coverage_debug_snapshot(
          observation_roots, observation_aliases);
  require(
      !debug_snapshot.empty()
          && std::ranges::is_sorted(
              debug_snapshot, {}, &SystemVerilogCoverageObservation::path)
          && std::ranges::any_of(
              debug_snapshot,
              [](const SystemVerilogCoverageObservation& value) {
                return value.path.starts_with("secondary.coverage")
                    && value.vcd_compatible;
              })
          && std::ranges::any_of(
              debug_snapshot,
              [](const SystemVerilogCoverageObservation& value) {
                return value.path.starts_with("alias.coverage")
                    && value.canonical_path.starts_with("top.coverage");
              })
          && std::ranges::all_of(
              debug_snapshot,
              [](const SystemVerilogCoverageObservation& value) {
                return value.path.find("0x") == std::string::npos;
              }),
      "debug snapshots expose sorted multiple roots, aliases, and VCD scalars");

  auto execution_profiles = parse_text(
      "covergroup-execution-profiles.sv",
      R"(module execution_owner;
  int value;
  logic clock;
  covergroup explicit_group;
    point: coverpoint value {
      bins one = {1};
      illegal_bins forbidden = {2};
    }
  endgroup
  covergroup event_group @(posedge clock);
    point: coverpoint value { bins one = {1}; }
  endgroup
  covergroup procedural_group
      with function sample (input int sample_value);
    point: coverpoint sample_value { bins one = {1}; }
  endgroup
endmodule
)",
      Language::SystemVerilog2017);
  std::vector<Diagnostic> execution_resolution_diagnostics;
  require(
      execution_profiles.ok()
          && resolve_systemverilog_covergroups(
              execution_profiles.design, execution_resolution_diagnostics)
          && execution_resolution_diagnostics.empty(),
      "explicit, event, and procedural execution profiles resolve");
  const auto& execution_groups = execution_profiles.design.units.front()
      .systemverilog_covergroups;
  const auto& explicit_group = execution_groups[0];
  const auto& event_group = execution_groups[1];
  const auto& procedural_group = execution_groups[2];
  const std::vector<SystemVerilogCovergroupSampleInput> execution_input{
      {0U, {1, 0U, 64U}}};

  std::vector<std::string> parity_signatures;
  std::vector<std::string> differential_signatures;
  std::optional<SystemVerilogCoverageExecutionResult> parity_execution;
  constexpr SystemVerilogCoverageExecutionMode execution_modes[] = {
      SystemVerilogCoverageExecutionMode::Interpreter,
      SystemVerilogCoverageExecutionMode::LlvmO0,
      SystemVerilogCoverageExecutionMode::LlvmO2};
  for (const auto mode : execution_modes) {
    SystemVerilogCovergroupInstance mode_instance;
    mode_instance.declaration_identity =
        procedural_group.canonical_identity;
    mode_instance.runtime_identity = "procedural-instance";
    SystemVerilogCoverageExecutionState mode_state;
    std::vector<Diagnostic> mode_diagnostics;
    std::vector<SystemVerilogCoverageCallbackEvent> callback_events;
    const std::vector<SystemVerilogCoverageCallback> callbacks{
        [&](const SystemVerilogCoverageCallbackEvent& event) {
          callback_events.push_back(event);
        }};
    const auto executed = execute_systemverilog_covergroup_sample(
        mode_instance,
        procedural_group,
        SystemVerilogCoverageSampleTrigger::Procedural,
        mode,
        execution_input,
        callbacks,
        mode_state,
        mode_diagnostics);
    std::string signature;
    for (const auto& event : executed.events) {
      signature += std::to_string(static_cast<int>(event.kind));
      signature += event.bin_identity.value_or("-");
      require(event.mode == mode, "callbacks retain the selected execution mode");
    }
    parity_signatures.push_back(signature);
    const std::array mode_instances{mode_instance};
    const auto mode_report = build_systemverilog_coverage_report(
        procedural_group, mode_instances);
    signature += render_systemverilog_coverage_report(mode_report);
    const std::array mode_roots{
        SystemVerilogCoverageRoot{"primary.coverage", mode_report},
        SystemVerilogCoverageRoot{"secondary.coverage", mode_report}};
    const std::array mode_aliases{
        SystemVerilogCoverageAlias{
            "alias.coverage", "primary.coverage"}};
    for (const auto& observation :
         build_systemverilog_coverage_debug_snapshot(
             mode_roots, mode_aliases)) {
      signature += observation.path + "="
          + std::to_string(observation.unsigned_value) + ";";
    }
    for (const auto& event : build_systemverilog_coverage_trace_events(
             executed, 42U, 7U, mode_aliases)) {
      signature += event.path + "="
          + std::to_string(static_cast<int>(event.kind)) + ";";
    }
    differential_signatures.push_back(std::move(signature));
    if (!parity_execution) parity_execution = executed;
    require(
        executed.accepted
            && !executed.reentrant_rejected
            && !executed.trigger_rejected
            && executed.events.size() == 3
            && callback_events.size() == executed.events.size()
            && executed.events.front().kind
                == SystemVerilogCoverageCallbackKind::PreSample
            && executed.events[1].kind
                == SystemVerilogCoverageCallbackKind::Hit
            && executed.events.back().kind
                == SystemVerilogCoverageCallbackKind::PostSample
            && !mode_state.sampling
            && mode_diagnostics.empty(),
        "procedural sampling emits ordered pre, hit, and post callbacks");
  }
  require(
      parity_signatures[0] == parity_signatures[1]
          && parity_signatures[1] == parity_signatures[2],
      "interpreter and LLVM O0/O2 coverage schedules are identical");
  require(
      differential_signatures[0] == differential_signatures[1]
          && differential_signatures[1] == differential_signatures[2],
      "interpreter and LLVM O0/O2 reports, debugger roots/aliases, and traces are identical");
  const std::vector<SystemVerilogCoverageAlias> trace_aliases{
      {"alias-procedural", "procedural-instance"}};
  const auto trace_events = build_systemverilog_coverage_trace_events(
      *parity_execution, 42U, 7U, trace_aliases);
  require(
      trace_events.size() == parity_execution->events.size() * 2U
          && std::ranges::all_of(
              trace_events,
              [](const SystemVerilogCoverageTraceEvent& event) {
                return event.time == 42U && event.delta == 7U;
              })
          && std::ranges::count_if(
              trace_events,
              [](const SystemVerilogCoverageTraceEvent& event) {
                return event.sequence == 0U;
              }) == 2
          && std::ranges::any_of(
              trace_events,
              [](const SystemVerilogCoverageTraceEvent& event) {
                return event.path.starts_with("alias-procedural")
                    && event.canonical_path.starts_with(
                        "procedural-instance");
              })
          && std::ranges::count_if(
              trace_events,
              [](const SystemVerilogCoverageTraceEvent& event) {
                return event.vcd_compatible;
              }) == 2,
      "trace events preserve callback parity, aliases, time/delta, and VCD hits");

  SystemVerilogCovergroupInstance event_instance;
  event_instance.declaration_identity = event_group.canonical_identity;
  event_instance.runtime_identity = "event-instance";
  SystemVerilogCoverageExecutionState event_state;
  std::vector<Diagnostic> event_diagnostics;
  const auto event_execution = execute_systemverilog_covergroup_sample(
      event_instance,
      event_group,
      SystemVerilogCoverageSampleTrigger::Event,
      SystemVerilogCoverageExecutionMode::Interpreter,
      execution_input,
      std::span<const SystemVerilogCoverageCallback>{},
      event_state,
      event_diagnostics);
  require(
      event_execution.accepted
          && event_execution.events.size() == 3
          && event_diagnostics.empty(),
      "event-driven sampling executes through the shared callback scheduler");

  SystemVerilogCovergroupInstance explicit_instance;
  explicit_instance.declaration_identity =
      explicit_group.canonical_identity;
  explicit_instance.runtime_identity = "explicit-instance";
  SystemVerilogCoverageExecutionState explicit_state;
  std::vector<Diagnostic> explicit_diagnostics;
  std::optional<SystemVerilogCoverageExecutionResult> nested_execution;
  const std::vector<SystemVerilogCoverageCallback> reentrant_callback{
      [&](const SystemVerilogCoverageCallbackEvent& event) {
        if (event.kind != SystemVerilogCoverageCallbackKind::PreSample) {
          return;
        }
        nested_execution = execute_systemverilog_covergroup_sample(
            explicit_instance,
            explicit_group,
            SystemVerilogCoverageSampleTrigger::Explicit,
            SystemVerilogCoverageExecutionMode::Interpreter,
            execution_input,
            std::span<const SystemVerilogCoverageCallback>{},
            explicit_state,
            explicit_diagnostics);
      }};
  const auto explicit_execution = execute_systemverilog_covergroup_sample(
      explicit_instance,
      explicit_group,
      SystemVerilogCoverageSampleTrigger::Explicit,
      SystemVerilogCoverageExecutionMode::Interpreter,
      execution_input,
      reentrant_callback,
      explicit_state,
      explicit_diagnostics);
  require(
      explicit_execution.accepted
          && nested_execution
          && nested_execution->reentrant_rejected
          && explicit_instance.bin_hits.size() == 1
          && explicit_instance.bin_hits.front().hit_count == 1
          && explicit_diagnostics.size() == 1
          && explicit_diagnostics.front().code == "FSIM-SV-COV-003",
      "reentrant callbacks reject before mutation while the outer sample completes");

  std::vector<SystemVerilogCoverageCallbackEvent> illegal_events;
  const std::vector<SystemVerilogCoverageCallback> illegal_callback{
      [&](const SystemVerilogCoverageCallbackEvent& event) {
        illegal_events.push_back(event);
      }};
  const std::vector<SystemVerilogCovergroupSampleInput> illegal_input{
      {0U, {2, 0U, 64U}}};
  std::vector<Diagnostic> illegal_execution_diagnostics;
  const auto illegal_execution = execute_systemverilog_covergroup_sample(
      explicit_instance,
      explicit_group,
      SystemVerilogCoverageSampleTrigger::Explicit,
      SystemVerilogCoverageExecutionMode::LlvmO2,
      illegal_input,
      illegal_callback,
      explicit_state,
      illegal_execution_diagnostics);
  require(
      illegal_execution.accepted
          && std::ranges::any_of(
              illegal_events,
              [](const SystemVerilogCoverageCallbackEvent& event) {
                return event.kind
                    == SystemVerilogCoverageCallbackKind::IllegalBin;
              })
          && illegal_execution_diagnostics.size() == 1
          && illegal_execution_diagnostics.front().code == "FSIM-SV-COV-001",
      "illegal-bin callbacks follow the hit and retain backend parity");
  std::vector<Diagnostic> trigger_diagnostics;
  const auto wrong_trigger = execute_systemverilog_covergroup_sample(
      explicit_instance,
      explicit_group,
      SystemVerilogCoverageSampleTrigger::Event,
      SystemVerilogCoverageExecutionMode::Interpreter,
      execution_input,
      std::span<const SystemVerilogCoverageCallback>{},
      explicit_state,
      trigger_diagnostics);
  require(
      wrong_trigger.trigger_rejected
          && !wrong_trigger.accepted
          && trigger_diagnostics.size() == 1
          && trigger_diagnostics.front().code == "FSIM-SV-COV-004",
      "sampling-profile mismatches reject before callbacks or mutation");

  const auto require_static_budget_rejection = [](
      const SystemVerilogCovergroupDeclaration& declaration,
      const std::string_view description) {
    std::vector<Diagnostic> diagnostics;
    require(
        !validate_systemverilog_coverage_resources(
            declaration, diagnostics)
            && diagnostics.size() == 1
            && diagnostics.front().code == "FSIM-SV-SEM-221",
        std::string{description});
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
  storage_budget_instance.declaration_identity =
      explicit_group.canonical_identity;
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
      std::span<const SystemVerilogCoverageCallback>{},
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
  transaction_budget_instance.declaration_identity =
      explicit_group.canonical_identity;
  transaction_budget_instance.runtime_identity = "transaction-budget";
  SystemVerilogCoverageExecutionState transaction_budget_state;
  std::vector<Diagnostic> transaction_budget_diagnostics;
  const auto rejected_transaction = execute_systemverilog_covergroup_sample(
      transaction_budget_instance,
      explicit_group,
      SystemVerilogCoverageSampleTrigger::Explicit,
      SystemVerilogCoverageExecutionMode::LlvmO2,
      transaction_budget_inputs,
      std::span<const SystemVerilogCoverageCallback>{},
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
  const auto unsupported_selection = parse_text(
      "covergroup-selection-unsupported.sv",
      "module bad; covergroup cg; coverpoint value { "
      "bins filtered = {1} with (item > 0); } endgroup endmodule",
      Language::SystemVerilog2017);
  const auto unsupported_cross_selection = parse_text(
      "covergroup-cross-selection-unsupported.sv",
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
      std::string{"bad"},
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
          }) == 2,
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
          }) == 1,
      "a duplicate explicit cross bin has one exact diagnostic");
  require(
      std::ranges::count_if(
          invalid_coverage_option.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-217";
          }) == 2,
      "out-of-range goal and at_least options reject exactly");
  require(
      std::ranges::count_if(
          invalid_percentage_option.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-218";
          }) == 2,
      "out-of-range covergroup goals and Boolean merge options reject exactly");
  require(
      std::ranges::count_if(
          unsupported_option.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-217";
          }) == 1,
      "an unsupported coverage option has one exact diagnostic");
  require(
      std::ranges::count_if(
          unsupported_type.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-219";
          }) == 1,
      "a non-integral coverage formal has one exact diagnostic");
  require(
      std::ranges::count_if(
          unsupported_selection.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-220";
          }) == 1,
      "an unsupported with selection has one exact diagnostic");
  require(
      std::ranges::count_if(
          unsupported_cross_selection.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-220";
          }) == 1,
      "an unsupported cross with selection has one exact diagnostic");
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
          }) == 1,
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
              }) == 2,
      "resolution and constructor/sample profile failures reject exactly:"
          + unresolved_failure_codes);
}

} // namespace fsim::tests::frontend
