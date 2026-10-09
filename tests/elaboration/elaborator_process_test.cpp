// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/semantic/compiled_design_normalization.hpp"

namespace fsim::tests::elaboration {
namespace Simir = fsim::runtime::simir;

void test_process_and_wait_lowering() {
const auto unsafe_edge = fsim::frontend::parse_text(
        "unsafe_edge.vhd",
        R"(
entity unsafe_edge is
  port (clk : in std_logic; q : out std_logic);
end entity;
architecture rtl of unsafe_edge is
begin
  p: process(clk)
  begin
    if rising_edge(clk) then
      q <= '1';
    else
      q <= '0';
    end if;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(unsafe_edge.ok());
    // An edge predicate with an else branch is an ordinary boolean
    // expression (IEEE 1076-2008 16.7).
    const auto edge_with_else =
        compile_and_elaborate(unsafe_edge.design, "unsafe_edge");
    assert(edge_with_else.ok());

    const auto logical_not = fsim::frontend::parse_text(
        "logical_not.sv",
        R"(
module logical_not(input logic [3:0] value, output logic result);
  assign result = !value;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(logical_not.ok());
    const auto elaborated_not =
        compile_and_elaborate(logical_not.design, "logical_not");
    assert(elaborated_not.ok());
    const auto not_value =
        elaborated_not.design->find_signal("value");
    const auto not_result =
        elaborated_not.design->find_signal("result");
    assert(not_value && not_result);
    auto not_interpreter =
        elaborated_not.design->create_interpreter();
    for (const auto& [value, expected] :
         std::array{
             std::pair{
                 std::string_view{"0000"},
                 std::string_view{"1"}},
             std::pair{
                 std::string_view{"00X0"},
                 std::string_view{"X"}},
             std::pair{
                 std::string_view{"01X0"},
                 std::string_view{"0"}}}) {
        not_interpreter->deposit_signal(
            *not_value,
            fsim::runtime::PackedLogic4::from_msb_string(value));
        (void)not_interpreter->run();
        assert(
            not_interpreter
                ->signal_value(*not_result)
                .to_msb_string()
            == expected);
    }

    const auto verilog_continuous_active = fsim::frontend::parse_text(
        "verilog_continuous_active.v",
        R"(
module verilog_continuous_active;
  reg source_value;
  wire continuous_value;
  assign continuous_value = source_value;
endmodule
)",
        fsim::frontend::Language::Verilog2005);
    assert(verilog_continuous_active.ok());
    const auto elaborated_verilog_continuous_active
        = compile_and_elaborate(
            verilog_continuous_active.design,
            "verilog_continuous_active");
    assert(elaborated_verilog_continuous_active.ok());
    const auto verilog_source =
        elaborated_verilog_continuous_active.design->find_signal(
            "source_value");
    const auto verilog_destination =
        elaborated_verilog_continuous_active.design->find_signal(
            "continuous_value");
    assert(verilog_source && verilog_destination);
    const auto& verilog_processes
        = elaborated_verilog_continuous_active.design->processes();
    const auto verilog_driver = std::ranges::find_if(
        verilog_processes,
        [&](const Simir::Process& process) {
            return std::ranges::any_of(process.driver_regions,
                [&](const Simir::Process::DriverRegion& region) {
                    return region.signal == *verilog_destination;
                });
        });
    assert(verilog_driver != verilog_processes.end());
    assert(verilog_driver->scheduling_domain
        == Simir::ProcessSchedulingDomain::systemverilog);
    std::size_t verilog_active_writes { };
    bool verilog_reads_source { };
    for (const auto& operation : verilog_driver->operations) {
        if (const auto* read
            = Simir::operation_get_if<Simir::ReadSignal>(&operation)) {
            verilog_reads_source
                = verilog_reads_source || read->signal == *verilog_source;
        } else if (const auto* write
            = Simir::operation_get_if<Simir::WriteUpdate>(&operation)) {
            assert(write->domain
                == Simir::SignalUpdateDomain::systemverilog_active);
            verilog_active_writes += write->signal == *verilog_destination;
        } else if (const auto* write_slice
            = Simir::operation_get_if<Simir::WriteUpdateSlice>(
                &operation)) {
            assert(write_slice->domain
                == Simir::SignalUpdateDomain::systemverilog_active);
            verilog_active_writes
                += write_slice->signal == *verilog_destination;
        }
    }
    assert(verilog_reads_source && verilog_active_writes == 1U);

    const auto assignment_timing = fsim::frontend::parse_text(
        "assignment_timing.sv",
        R"(
module assignment_timing;
  logic clock;
  logic source;
  logic delayed_blocking;
  logic [3:0] delayed_slice;
  logic event_blocking;
  logic event_nba;
  logic wildcard_nba;
  logic local_result;

  initial delayed_blocking = #5 source;
  initial delayed_slice[2:1] <= #2 2'b10;
  initial event_blocking = @(posedge clock) source;
  initial event_nba <= @(negedge clock or source) source;
  initial wildcard_nba <= @* source;
  initial begin
    logic local_value;
    local_value = #4 source;
    local_result = local_value;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(assignment_timing.ok());
    const auto elaborated_assignment_timing =
        compile_and_elaborate(
            assignment_timing.design, "assignment_timing");
    if (!elaborated_assignment_timing.ok()) {
        for (const auto& diagnostic :
             elaborated_assignment_timing.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_assignment_timing.ok());
    assert(
        elaborated_assignment_timing.design->processes().size()
        == 6);
    const auto operation_index =
        []<typename OperationType>(const auto& operations) {
            const auto found = std::find_if(
                operations.begin(),
                operations.end(),
                [](const auto& operation) {
                    return fsim::runtime::simir::operation_holds<OperationType>(
                        operation);
                });
            assert(found != operations.end());
            return static_cast<std::size_t>(
                std::distance(operations.begin(), found));
        };
    const auto wait_debug_index =
        [](const auto& operations) {
            const auto found = std::find_if(
                operations.begin(),
                operations.end(),
                [](const auto& operation) {
                    const auto* point = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::DebugPoint>(
                        &operation);
                    return point != nullptr
                        && point->kind
                            == fsim::runtime::simir::
                                DebugPointKind::wait;
                });
            assert(found != operations.end());
            return static_cast<std::size_t>(
                std::distance(operations.begin(), found));
        };
    const auto& delayed_blocking_operations =
        elaborated_assignment_timing.design
            ->processes()[0]
            .operations;
    assert(
        operation_index
            .operator()<fsim::runtime::simir::ReadSignal>(
                delayed_blocking_operations)
        < wait_debug_index(delayed_blocking_operations));
    assert(
        wait_debug_index(delayed_blocking_operations)
        < operation_index
              .operator()<fsim::runtime::simir::WaitFor>(
                  delayed_blocking_operations));
    assert(
        operation_index
            .operator()<fsim::runtime::simir::WaitFor>(
                delayed_blocking_operations)
        < operation_index
              .operator()<fsim::runtime::simir::WriteBlocking>(
                  delayed_blocking_operations));
    const auto* blocking_wait =
        fsim::runtime::simir::operation_get_if<fsim::runtime::simir::WaitFor>(
            &delayed_blocking_operations[
                operation_index
                    .operator()<fsim::runtime::simir::WaitFor>(
                        delayed_blocking_operations)]);
    assert(blocking_wait && blocking_wait->delay == 5);

    const auto& delayed_slice_operations =
        elaborated_assignment_timing.design
            ->processes()[1]
            .operations;
    const auto delayed_slice = std::find_if(
        delayed_slice_operations.begin(),
        delayed_slice_operations.end(),
        [](const auto& operation) {
            return fsim::runtime::simir::operation_holds<
                fsim::runtime::simir::WriteAfterSlice>(
                    operation);
        });
    assert(delayed_slice != delayed_slice_operations.end());
    const auto& delayed_slice_write =
        fsim::runtime::simir::operation_get<fsim::runtime::simir::WriteAfterSlice>(
            *delayed_slice);
    assert(
        delayed_slice_write.delay == 2
        && delayed_slice_write.offset == 1);
    assert(
        std::ranges::none_of(
            delayed_slice_operations,
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::WaitFor>(operation);
            }));

    const auto verify_event_assignment =
        [&](const std::size_t process_index,
            const bool nonblocking,
            const std::size_t sensitivity_count) {
            const auto& operations =
                elaborated_assignment_timing.design
                    ->processes()[process_index]
                    .operations;
            const auto wait_index =
                operation_index
                    .operator()<fsim::runtime::simir::WaitOn>(
                        operations);
            assert(wait_debug_index(operations) < wait_index);
            // A nonblocking assignment evaluates its value before the
            // intra-assignment event and waits in a join_none branch
            // (IEEE 1800-2017 10.4.2); a blocking one waits first in this
            // lowering.
            const auto read_index
                = operation_index
                      .operator()<fsim::runtime::simir::ReadSignal>(
                          operations);
            assert(nonblocking ? read_index < wait_index
                               : wait_index < read_index);
            if (nonblocking) {
                assert(
                    operation_index
                        .operator()<fsim::runtime::simir::Fork>(operations)
                    < wait_index);
            }
            if (nonblocking) {
                assert(
                    wait_index
                    < operation_index
                          .operator()<
                              fsim::runtime::simir::WriteUpdate>(
                              operations));
            } else {
                assert(
                    wait_index
                    < operation_index
                          .operator()<
                              fsim::runtime::simir::WriteBlocking>(
                              operations));
            }
            const auto& wait =
                fsim::runtime::simir::operation_get<fsim::runtime::simir::WaitOn>(
                    operations[wait_index]);
            assert(wait.signals.size() == sensitivity_count);
        };
    verify_event_assignment(2, false, 1);
    verify_event_assignment(3, true, 2);
    verify_event_assignment(4, true, 1);
    const auto& wildcard_wait =
        fsim::runtime::simir::operation_get<fsim::runtime::simir::WaitOn>(
            elaborated_assignment_timing.design
                ->processes()[4]
                .operations[
                    operation_index
                        .operator()<fsim::runtime::simir::WaitOn>(
                            elaborated_assignment_timing.design
                                ->processes()[4]
                                .operations)]);
    const auto source_signal =
        elaborated_assignment_timing.design->find_signal("source");
    assert(
        source_signal
        && wildcard_wait.signals
               == std::vector<fsim::runtime::simir::SignalId>{
                   *source_signal});

    const auto& local_operations =
        elaborated_assignment_timing.design
            ->processes()[5]
            .operations;
    assert(
        operation_index
            .operator()<fsim::runtime::simir::ReadSignal>(
                local_operations)
        < operation_index
              .operator()<fsim::runtime::simir::WaitFor>(
                  local_operations));
    assert(
        operation_index
            .operator()<fsim::runtime::simir::WaitFor>(
                local_operations)
        < operation_index
              .operator()<fsim::runtime::simir::CopyRegister>(
                  local_operations));

    auto inconsistent_assignment_control =
        fsim::frontend::parse_text(
            "inconsistent_assignment_control.sv",
            R"(
module inconsistent_assignment_control;
  logic source;
  logic result;
  initial result = source;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(inconsistent_assignment_control.ok());
    auto malformed_assignment_control = compile_test_design(
        std::move(inconsistent_assignment_control.design));
    assert(semantic::normalize_compiled_design(
        malformed_assignment_control));
    auto& malformed_control_statements = malformed_assignment_control
                                             .mutable_systemverilog()
                                             .mutable_statements();
    auto malformed_control = std::ranges::find_if(
        malformed_control_statements,
        [](const semantic::sv::Statement& statement) {
            return statement.kind
                == semantic::sv::StatementKind::assignment;
        });
    assert(malformed_control != malformed_control_statements.end());
    malformed_control->assignment_control
        = semantic::sv::AssignmentControl::event;
    malformed_assignment_control.refresh_lookup_indexes();
    const auto rejected_assignment_control = fsim::elaboration::elaborate(
        malformed_assignment_control,
        "sv:work.inconsistent_assignment_control");
    assert(!rejected_assignment_control.ok());
    assert(has_diagnostic(
        rejected_assignment_control, "FSIM-ELAB-105"));

    auto inconsistent_update = fsim::frontend::parse_text(
        "inconsistent_update.sv",
        R"(
module inconsistent_update;
  logic [3:0] source;
  logic [3:0] result;
  initial result += source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(inconsistent_update.ok());
    auto malformed_update = compile_test_design(
        std::move(inconsistent_update.design));
    assert(semantic::normalize_compiled_design(malformed_update));
    auto& malformed_update_statements
        = malformed_update.mutable_systemverilog().mutable_statements();
    auto malformed_update_statement = std::ranges::find_if(
        malformed_update_statements,
        [](const semantic::sv::Statement& statement) {
            return statement.kind
                    == semantic::sv::StatementKind::assignment
                && statement.update_kind
                    != semantic::sv::UpdateKind::none;
        });
    assert(malformed_update_statement
        != malformed_update_statements.end());
    malformed_update_statement->update_operator = "-";
    malformed_update.refresh_lookup_indexes();
    const auto rejected_update = fsim::elaboration::elaborate(
        malformed_update, "sv:work.inconsistent_update");
    assert(!rejected_update.ok());
    assert(has_diagnostic(rejected_update, "FSIM-ELAB-106"));

    const auto invalid_force = fsim::frontend::parse_text(
        "invalid_force.sv",
        R"(
module invalid_force;
  logic [3:0] four_state;
  bit [3:0] two_state;
  logic signed [31:0] index;
  initial begin
    logic [3:0] local_value;
    force local_value = 4'h1;
    force four_state[index] = 1'b1;
    force two_state = four_state;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_force.ok());
    const auto rejected_force = compile_and_elaborate(
        invalid_force.design, "invalid_force");
    assert(!rejected_force.ok());
    // IEEE 1800-2017 10.6.2: a force bit-select needs a constant index.
    assert(has_diagnostic(rejected_force, "FSIM-ELAB-SVFORCE-001"));
    assert(has_diagnostic(rejected_force, "FSIM-ELAB-SVFORCE-002"));
    assert(has_diagnostic(rejected_force, "FSIM-ELAB-SVFORCE-003"));

    const auto invalid_procedural_assign = fsim::frontend::parse_text(
        "invalid_procedural_assign.sv",
        R"(
module invalid_procedural_assign;
  logic source;
  initial begin
    automatic logic local_value;
    assign local_value = source;
    deassign local_value;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_procedural_assign.ok());
    const auto rejected_procedural_assign = compile_and_elaborate(
        invalid_procedural_assign.design,
        "invalid_procedural_assign");
    assert(!rejected_procedural_assign.ok());
    assert(has_diagnostic(
        rejected_procedural_assign,
        "FSIM-ELAB-SVPROCASSIGN-001"));

    const auto dynamic_force = fsim::frontend::parse_text(
        "dynamic_force.sv",
        R"(
module dynamic_force;
  logic [3:0] value;
  logic signed [31:0] index;
  initial begin
    force value[index] = 1'b1;
    release value[index];
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_force.ok());
    const auto elaborated_dynamic_force = compile_and_elaborate(
        dynamic_force.design, "dynamic_force");
    // IEEE 1800-2017 10.6.2: force and release select with a constant
    // index only.
    assert(!elaborated_dynamic_force.ok());
    assert(has_diagnostic(elaborated_dynamic_force, "FSIM-ELAB-SVFORCE-001"));

    const auto width_conversion = fsim::frontend::parse_text(
        "width_conversion.sv",
        R"(
module width_conversion;
  logic [7:0] q;
  initial q = 4'b1010;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(width_conversion.ok());
    const auto converted_width =
        compile_and_elaborate(
            width_conversion.design, "width_conversion");
    assert(converted_width.ok());
    const auto converted_q =
        converted_width.design->find_signal("width_conversion.q");
    assert(converted_q);
    auto converted_interpreter =
        converted_width.design->create_interpreter();
    converted_interpreter->start();
    (void)converted_interpreter->run();
    assert(
        converted_interpreter->signal_value(*converted_q).to_msb_string()
        == "00001010");

    auto unsupported_domain = fsim::frontend::parse_text(
        "unsupported_domain.sv",
        "module unsupported_domain(input logic value); endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unsupported_domain.ok());
    auto unsupported_domain_hir = compile_test_design(
        std::move(unsupported_domain.design));
    assert(semantic::normalize_compiled_design(unsupported_domain_hir));
    auto& unsupported_declarations
        = unsupported_domain_hir.mutable_systemverilog()
              .mutable_declarations();
    auto unsupported_port = std::ranges::find_if(
        unsupported_declarations,
        [](const semantic::sv::Declaration& declaration) {
            return declaration.form
                    == semantic::sv::DeclarationForm::port
                && declaration.name == "value";
        });
    assert(
        unsupported_port != unsupported_declarations.end()
        && unsupported_port->type);
    unsupported_port->type->executable_width = 0U;
    unsupported_domain_hir.refresh_lookup_indexes();
    const auto rejected_domain = fsim::elaboration::elaborate(
        unsupported_domain_hir, "sv:work.unsupported_domain");
    assert(!rejected_domain.ok());
    assert(has_diagnostic(
        rejected_domain, "FSIM-ELAB-TYPE-001"));

    const auto default_values = fsim::frontend::parse_text(
        "default_values.vhd",
        R"(
entity default_values is
  port (
    bit_value : out bit;
    boolean_value : out boolean;
    logic_value : out std_logic
  );
end entity;
architecture rtl of default_values is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(default_values.ok());
    const auto elaborated_defaults = compile_and_elaborate(
        default_values.design, "default_values");
    assert(elaborated_defaults.ok());
    auto default_interpreter =
        elaborated_defaults.design->create_interpreter();
    const auto bit_value =
        elaborated_defaults.design->find_signal("bit_value");
    const auto boolean_value =
        elaborated_defaults.design->find_signal("boolean_value");
    const auto logic_value =
        elaborated_defaults.design->find_signal("logic_value");
    assert(bit_value && boolean_value && logic_value);
    assert(
        default_interpreter->signal_value(*bit_value).to_msb_string()
        == "0");
    assert(
        default_interpreter->signal_value(*boolean_value).to_msb_string()
        == "0");
    assert(
        default_interpreter->signal_value(*logic_value).to_msb_string()
        == "U");

    const auto nine_state_literals = fsim::frontend::parse_text(
        "nine_state_literals.vhd",
        R"(
entity nine_state_literals is
  port (value : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of nine_state_literals is
begin
  value <= "ULH-WZ01";
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(nine_state_literals.ok());
    const auto elaborated_nine_state_literals =
        compile_and_elaborate(
            nine_state_literals.design, "nine_state_literals");
    assert(elaborated_nine_state_literals.ok());
    auto nine_state_interpreter =
        elaborated_nine_state_literals.design->create_interpreter();
    const auto nine_state_value =
        elaborated_nine_state_literals.design->find_signal("value");
    assert(nine_state_value);
    const auto nine_state_run = nine_state_interpreter->run();
    assert(
        nine_state_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        nine_state_interpreter
            ->signal_value(*nine_state_value)
            .to_msb_string()
        == "ULH-WZ01");

    const auto lossy_assignment = fsim::frontend::parse_text(
        "lossy_assignment.sv",
        R"(
module lossy_assignment(input logic source, output bit target);
  assign target = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(lossy_assignment.ok());
    const auto converted_lossy_assignment = compile_and_elaborate(
        lossy_assignment.design, "lossy_assignment");
    assert(converted_lossy_assignment.ok());
    auto lossy_assignment_interpreter = converted_lossy_assignment.design->create_interpreter();
    const auto lossy_assignment_target = converted_lossy_assignment.design->find_signal("target");
    assert(lossy_assignment_target);
    const auto lossy_assignment_run = lossy_assignment_interpreter->run();
    assert(
        lossy_assignment_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        lossy_assignment_interpreter
            ->signal_value(*lossy_assignment_target)
            .to_msb_string()
        == "0");

    const auto two_state_assignment = fsim::frontend::parse_text(
        "two_state_assignment.sv",
        R"(
module two_state_assignment(output bit target);
  initial target = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(two_state_assignment.ok());
    const auto accepted_two_state_assignment =
        compile_and_elaborate(
            two_state_assignment.design, "two_state_assignment");
    assert(accepted_two_state_assignment.ok());

    const auto unknown_condition = fsim::frontend::parse_text(
        "unknown_condition.sv",
        R"(
module unknown_condition;
  logic condition;
  logic result;
  initial begin
    if (condition)
      result = 1'b1;
    else
      result = 1'b0;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_condition.ok());
    const auto elaborated_unknown_condition =
        compile_and_elaborate(
            unknown_condition.design, "unknown_condition");
    assert(elaborated_unknown_condition.ok());
    auto unknown_condition_interpreter =
        elaborated_unknown_condition.design->create_interpreter();
    const auto condition_result =
        elaborated_unknown_condition.design->find_signal("result");
    assert(condition_result);
    const auto unknown_condition_run =
        unknown_condition_interpreter->run();
    assert(
        unknown_condition_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        unknown_condition_interpreter
            ->signal_value(*condition_result)
            .to_msb_string()
        == "0");

    const auto multiple_drivers = fsim::frontend::parse_text(
        "multiple_drivers.sv",
        R"(
module driver(output logic value);
  assign value = 1'b0;
endmodule
module other_driver(output logic value);
  assign value = 1'b1;
endmodule
module driver_top;
  logic shared;
  driver first(.value(shared));
  other_driver second(.value(shared));
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(multiple_drivers.ok());
    const auto missing_resolver = compile_and_elaborate(
        multiple_drivers.design, "driver_top");
    assert(!missing_resolver.ok());
    assert(has_diagnostic(missing_resolver, "FSIM-ELAB-BIND-024"));
    const std::vector<fsim::elaboration::Binding> resolver_bindings{
        {"driver_top.first", "sv:work.driver", std::string{"sv_wire"}},
        {"driver_top.second", "sv:work.other_driver", std::string{"sv_wire"}},
    };
    const auto resolved_boundary = compile_and_elaborate(
        multiple_drivers.design, "driver_top", resolver_bindings);
    assert(resolved_boundary.ok());
    const auto shared =
        resolved_boundary.design->find_signal("shared");
    assert(shared);
    assert(
        resolved_boundary.design->signals().at(*shared).resolution
        == fsim::runtime::simir::ResolutionKind::sv_wire);
    auto resolved_boundary_interpreter =
        resolved_boundary.design->create_interpreter();
    const auto resolved_boundary_result =
        resolved_boundary_interpreter->run();
    assert(
        resolved_boundary_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        resolved_boundary_interpreter
            ->signal_value(*shared)
            .to_msb_string()
        == "X");

    const std::vector<fsim::elaboration::Binding> invalid_resolver_bindings{
        {"driver_top.first", "sv:work.driver", std::string{"wired"}},
        {"driver_top.second", "sv:work.other_driver", std::string{"wired"}},
    };
    const auto invalid_resolver = compile_and_elaborate(
        multiple_drivers.design,
        "driver_top",
        invalid_resolver_bindings);
    assert(!invalid_resolver.ok());
    assert(has_diagnostic(
        invalid_resolver, "FSIM-ELAB-BIND-050"));

    const auto process_drivers = fsim::frontend::parse_text(
        "process_drivers.sv",
        R"(
module process_drivers;
  logic q;
  initial q = 1'b0;
  initial q = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(process_drivers.ok());
    // A variable may be written by procedural statements in several
    // processes; the last write determines its value (IEEE 1800-2017 6.5).
    const auto procedural_process_drivers = compile_and_elaborate(
        process_drivers.design, "process_drivers");
    assert(procedural_process_drivers.ok());
    assert(!has_diagnostic(
        procedural_process_drivers, "FSIM-ELAB-DRV-001"));

    const auto initialized_event_driver =
        fsim::frontend::parse_text(
            "initialized_event_driver.sv",
            R"(
module initialized_event_driver;
  logic clock = 1'b0;
  logic ready;
  initial begin
    ready = 1'b1;
    #3 $finish;
  end
  always #1 clock = ~clock;
  always @(posedge clock) ready <= ~ready;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(initialized_event_driver.ok());
    const auto accepted_initialized_event_driver =
        compile_and_elaborate(
            initialized_event_driver.design,
            "initialized_event_driver");
    assert(accepted_initialized_event_driver.ok());
    auto initialized_event_interpreter =
        accepted_initialized_event_driver.design->create_interpreter();
    const auto initialized_event_result =
        initialized_event_interpreter->run();
    assert(
        initialized_event_result.status
            == fsim::runtime::RunStatus::stopped
        && initialized_event_result.time == 3);

    const auto legacy_integral_process_drivers =
        fsim::frontend::parse_text(
            "legacy_integral_process_drivers.sv",
            R"(
module legacy_integral_process_drivers;
  reg clock;
  integer cycles;
  initial begin
    clock = 1'b0;
    cycles = 0;
    #3 $finish;
  end
  always #1 clock = ~clock;
  always @(posedge clock) cycles <= cycles + 1;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(legacy_integral_process_drivers.ok());
    const auto accepted_legacy_integral_process_drivers =
        compile_and_elaborate(
            legacy_integral_process_drivers.design,
            "legacy_integral_process_drivers");
    assert(accepted_legacy_integral_process_drivers.ok());
    auto legacy_integral_interpreter =
        accepted_legacy_integral_process_drivers.design
            ->create_interpreter();
    const auto legacy_integral_result =
        legacy_integral_interpreter->run();
    assert(
        legacy_integral_result.status
            == fsim::runtime::RunStatus::stopped
        && legacy_integral_result.time == 3);

    const auto declaration_initializer_driver =
        fsim::frontend::parse_text(
            "declaration_initializer_driver.sv",
            R"(
module declaration_initializer_driver;
  logic clock = 1'b0;
  always #1 clock = ~clock;
  initial #3 $finish;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(declaration_initializer_driver.ok());
    const auto accepted_declaration_initializer_driver =
        compile_and_elaborate(
            declaration_initializer_driver.design,
            "declaration_initializer_driver");
    assert(accepted_declaration_initializer_driver.ok());
    auto declaration_initializer_interpreter =
        accepted_declaration_initializer_driver.design->create_interpreter();
    const auto declaration_initializer_result =
        declaration_initializer_interpreter->run();
    assert(
        declaration_initializer_result.status
            == fsim::runtime::RunStatus::stopped
        && declaration_initializer_result.time == 3);

    const auto selected_process_drivers =
        fsim::frontend::parse_text(
            "selected_process_drivers.sv",
            R"(
module selected_process_drivers;
  logic [3:0] q;
  initial q[0] = 1'b0;
  initial q[3:2] = 2'b11;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_process_drivers.ok());
    // Procedural writes from several processes are legal for variables,
    // including writes to parts of one vector (IEEE 1800-2017 6.5).
    const auto accepted_selected_process_drivers =
        compile_and_elaborate(
            selected_process_drivers.design,
            "selected_process_drivers");
    assert(accepted_selected_process_drivers.ok());
    assert(!has_diagnostic(
        accepted_selected_process_drivers,
        "FSIM-ELAB-DRV-001"));

    const auto selected_continuous_drivers =
        fsim::frontend::parse_text(
            "selected_continuous_drivers.sv",
            R"(
module selected_continuous_drivers;
  logic [3:0] q;
  assign q[0] = 1'b0;
  assign q[3:2] = 2'b11;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_continuous_drivers.ok());
    const auto accepted_selected_continuous_drivers =
        compile_and_elaborate(
            selected_continuous_drivers.design,
            "selected_continuous_drivers");
    assert(accepted_selected_continuous_drivers.ok());

    const auto continuous_concat_lvalue = fsim::frontend::parse_text(
        "continuous_concat_lvalue.sv",
        R"(
module continuous_concat_lvalue;
  logic [128:0] wide_source;
  logic [64:0] wide_head;
  logic [127:0] descending_target;
  logic [0:127] ascending_target;
  logic [8:0] slice_source;
  logic [7:0] descending_slices;
  logic [0:7] ascending_slice;

  assign {wide_head,
          {descending_target[63:32], ascending_target[0:31]}}
      = wide_source;
  assign {descending_slices[7:5], ascending_slice[1:4],
          descending_slices[4:3]} = slice_source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(continuous_concat_lvalue.ok());
    const auto elaborated_continuous_concat = compile_and_elaborate(
        continuous_concat_lvalue.design,
        "continuous_concat_lvalue");
    if (!elaborated_continuous_concat.ok()) {
        for (const auto& diagnostic :
             elaborated_continuous_concat.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_continuous_concat.ok());

    const auto wide_source_signal =
        elaborated_continuous_concat.design->find_signal("wide_source");
    const auto wide_head_signal =
        elaborated_continuous_concat.design->find_signal("wide_head");
    const auto descending_target_signal =
        elaborated_continuous_concat.design->find_signal(
            "descending_target");
    const auto ascending_target_signal =
        elaborated_continuous_concat.design->find_signal(
            "ascending_target");
    const auto slice_source_signal =
        elaborated_continuous_concat.design->find_signal("slice_source");
    const auto descending_slices_signal =
        elaborated_continuous_concat.design->find_signal(
            "descending_slices");
    const auto ascending_slice_signal =
        elaborated_continuous_concat.design->find_signal(
            "ascending_slice");
    assert(wide_source_signal && wide_head_signal
        && descending_target_signal && ascending_target_signal
        && slice_source_signal && descending_slices_signal
        && ascending_slice_signal);

    const auto process_for_driver = [&](const Simir::SignalId signal)
        -> const Simir::Process& {
        const auto& processes =
            elaborated_continuous_concat.design->processes();
        const auto found = std::ranges::find_if(
            processes,
            [&](const Simir::Process& process) {
                return std::ranges::any_of(
                    process.driver_regions,
                    [&](const Simir::Process::DriverRegion& region) {
                        return region.signal == signal;
                    });
            });
        assert(found != processes.end());
        return *found;
    };
    const auto check_active_update_sequence = [&]
        (const Simir::Process& process,
         const Simir::SignalId source_signal,
         const std::vector<std::pair<Simir::SignalId, bool>>& expected) {
        assert(process.initialize);
        assert(process.scheduling_domain
            == Simir::ProcessSchedulingDomain::systemverilog);
        assert((process.static_sensitivity
            == std::vector<Simir::Sensitivity> {
                { source_signal, Simir::EdgeKind::any, 0U, 0U },
            }));
        std::vector<std::pair<Simir::SignalId, bool>> actual;
        std::size_t source_reads { };
        for (const auto& operation : process.operations) {
            if (const auto* update
                = Simir::operation_get_if<Simir::WriteUpdate>(
                    &operation)) {
                assert(update->domain
                    == Simir::SignalUpdateDomain::systemverilog_active);
                actual.emplace_back(update->signal, true);
            } else if (const auto* update_slice
                = Simir::operation_get_if<Simir::WriteUpdateSlice>(
                    &operation)) {
                assert(update_slice->domain
                    == Simir::SignalUpdateDomain::systemverilog_active);
                actual.emplace_back(update_slice->signal, false);
            }
            if (Simir::operation_holds<Simir::ReadSignal>(operation)) {
                ++source_reads;
            }
        }
        assert(actual == expected);
        assert(source_reads == 1U);
    };

    const auto& wide_concat_process = process_for_driver(*wide_head_signal);
    check_active_update_sequence(
        wide_concat_process,
        *wide_source_signal,
        { { *wide_head_signal, true },
          { *descending_target_signal, false },
          { *ascending_target_signal, false } });
    assert((wide_concat_process.driver_regions
        == std::vector<Simir::Process::DriverRegion> {
            { *wide_head_signal, 0U, 0U, true },
            { *descending_target_signal, 32U, 32U, false },
            { *ascending_target_signal, 96U, 32U, false },
        }));

    const auto& slice_concat_process
        = process_for_driver(*descending_slices_signal);
    check_active_update_sequence(
        slice_concat_process,
        *slice_source_signal,
        { { *descending_slices_signal, false },
          { *ascending_slice_signal, false },
          { *descending_slices_signal, false } });
    assert((slice_concat_process.driver_regions
        == std::vector<Simir::Process::DriverRegion> {
            { *descending_slices_signal, 5U, 3U, false },
            { *ascending_slice_signal, 3U, 4U, false },
            { *descending_slices_signal, 3U, 2U, false },
        }));

    auto continuous_concat_interpreter =
        elaborated_continuous_concat.design->create_interpreter();
    const std::string unknown_65(65U, 'X');
    const std::string unknown_128(128U, 'X');
    const std::string unknown_8(8U, 'X');
    assert(continuous_concat_interpreter->signal_value(
               *wide_head_signal).to_msb_string() == unknown_65);
    assert(continuous_concat_interpreter->signal_value(
               *descending_target_signal).to_msb_string()
        == unknown_128);
    assert(continuous_concat_interpreter->signal_value(
               *ascending_target_signal).to_msb_string()
        == unknown_128);
    assert(continuous_concat_interpreter->signal_value(
               *descending_slices_signal).to_msb_string()
        == unknown_8);
    assert(continuous_concat_interpreter->signal_value(
               *ascending_slice_signal).to_msb_string()
        == unknown_8);

    std::string wide_source_value;
    wide_source_value.reserve(129U);
    constexpr std::string_view four_state_pattern { "10XZ" };
    for (std::size_t index { }; index < 129U; ++index) {
        wide_source_value.push_back(
            four_state_pattern[index % four_state_pattern.size()]);
    }
    const std::string slice_source_value { "ZX01XZ10Z" };
    continuous_concat_interpreter->deposit_signal(
        *wide_source_signal,
        fsim::runtime::PackedLogic4::from_msb_string(
            wide_source_value));
    continuous_concat_interpreter->deposit_signal(
        *slice_source_signal,
        fsim::runtime::PackedLogic4::from_msb_string(
            slice_source_value));
    (void)continuous_concat_interpreter->run();

    assert(continuous_concat_interpreter->signal_value(
               *wide_head_signal).to_msb_string()
        == wide_source_value.substr(0U, 65U));
    auto expected_descending_target = unknown_128;
    expected_descending_target.replace(
        64U, 32U, wide_source_value.substr(65U, 32U));
    assert(continuous_concat_interpreter->signal_value(
               *descending_target_signal).to_msb_string()
        == expected_descending_target);
    const auto expected_ascending_target
        = wide_source_value.substr(97U, 32U) + std::string(96U, 'X');
    assert(continuous_concat_interpreter->signal_value(
               *ascending_target_signal).to_msb_string()
        == expected_ascending_target);
    const auto expected_descending_slices
        = slice_source_value.substr(0U, 3U)
        + slice_source_value.substr(7U, 2U) + std::string(3U, 'X');
    assert(continuous_concat_interpreter->signal_value(
               *descending_slices_signal).to_msb_string()
        == expected_descending_slices);
    const auto expected_ascending_slice
        = std::string("X") + slice_source_value.substr(3U, 4U)
        + std::string(3U, 'X');
    assert(continuous_concat_interpreter->signal_value(
               *ascending_slice_signal).to_msb_string()
        == expected_ascending_slice);

    const auto procedural_concat_lvalue = fsim::frontend::parse_text(
        "procedural_concat_lvalue.sv",
        R"(
module procedural_concat_lvalue;
  logic [7:0] source;
  logic [3:0] left;
  logic [3:0] right;
  initial {left, right} = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(procedural_concat_lvalue.ok());
    const auto elaborated_procedural_concat = compile_and_elaborate(
        procedural_concat_lvalue.design,
        "procedural_concat_lvalue");
    assert(elaborated_procedural_concat.ok());
    assert(elaborated_procedural_concat.design->processes().size() == 1U);
    const auto& procedural_concat_operations
        = elaborated_procedural_concat.design->processes().front().operations;
    assert(std::ranges::count_if(
               procedural_concat_operations,
               [](const auto& operation) {
                   return Simir::operation_holds<Simir::WriteBlocking>(
                       operation);
               })
        == 2U);
    assert(std::ranges::none_of(
        procedural_concat_operations,
        [](const auto& operation) {
            return Simir::operation_holds<Simir::WriteUpdate>(operation)
                || Simir::operation_holds<Simir::WriteUpdateSlice>(
                    operation);
        }));

    const auto overlapping_concat_lvalue = fsim::frontend::parse_text(
        "overlapping_concat_lvalue.sv",
        R"(
module overlapping_concat_lvalue;
  logic target;
  assign {target, target} = 2'b01;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(overlapping_concat_lvalue.ok());
    const auto rejected_overlapping_concat_lvalue = compile_and_elaborate(
        overlapping_concat_lvalue.design,
        "overlapping_concat_lvalue");
    assert(!rejected_overlapping_concat_lvalue.ok());

    const auto dynamic_concat_lvalue = fsim::frontend::parse_text(
        "dynamic_concat_lvalue.sv",
        R"(
module dynamic_concat_lvalue;
  logic [3:0] packed_target;
  logic index;
  logic tail;
  logic [1:0] source;
  assign {packed_target[index], tail} = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_concat_lvalue.ok());
    const auto rejected_dynamic_concat_lvalue = compile_and_elaborate(
        dynamic_concat_lvalue.design,
        "dynamic_concat_lvalue");
    assert(!rejected_dynamic_concat_lvalue.ok());

    const auto container_concat_lvalue = fsim::frontend::parse_text(
        "container_concat_lvalue.sv",
        R"(
module container_concat_lvalue;
  logic [3:0] words [0:1];
  logic tail;
  logic [4:0] source;
  assign {words[0], tail} = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(container_concat_lvalue.ok());
    const auto rejected_container_concat_lvalue = compile_and_elaborate(
        container_concat_lvalue.design,
        "container_concat_lvalue");
    assert(!rejected_container_concat_lvalue.ok());

    const auto delayed_concat_lvalue = fsim::frontend::parse_text(
        "delayed_concat_lvalue.sv",
        R"(
module delayed_concat_lvalue;
  wire [3:0] left;
  wire right;
  logic [4:0] source;
  assign #1 {left, right} = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(delayed_concat_lvalue.ok());
    const auto rejected_delayed_concat_lvalue = compile_and_elaborate(
        delayed_concat_lvalue.design,
        "delayed_concat_lvalue");
    assert(!rejected_delayed_concat_lvalue.ok());

    const auto native_wire_drivers = fsim::frontend::parse_text(
        "native_wire_drivers.sv",
        R"(
module native_wire_drivers;
  wire q;
  native_wire_zero zero(.value(q));
  native_wire_one one(.value(q));
endmodule
module native_wire_zero(output logic value);
  assign value = 1'b0;
endmodule
module native_wire_one(output logic value);
  assign value = 1'b1;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(native_wire_drivers.ok());
    const auto elaborated_native_wire =
        compile_and_elaborate(
            native_wire_drivers.design, "native_wire_drivers");
    assert(elaborated_native_wire.ok());
    const auto native_wire_q =
        elaborated_native_wire.design->find_signal("q");
    assert(native_wire_q);
    assert(
        elaborated_native_wire.design->signals()
            .at(*native_wire_q)
            .resolution
        == fsim::runtime::simir::ResolutionKind::sv_wire);
    auto native_wire_interpreter =
        elaborated_native_wire.design->create_interpreter();
    const auto native_wire_result =
        native_wire_interpreter->run();
    assert(
        native_wire_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        native_wire_interpreter
            ->signal_value(*native_wire_q)
            .to_msb_string()
        == "X");

    const auto native_std_logic_drivers =
        fsim::frontend::parse_text(
            "native_std_logic_drivers.vhd",
            R"(
entity native_std_logic_drivers is
end entity;
architecture rtl of native_std_logic_drivers is
  signal q : std_logic;
begin
  q <= '0';
  q <= '1';
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(native_std_logic_drivers.ok());
    const auto elaborated_native_std_logic =
        compile_and_elaborate(
            native_std_logic_drivers.design,
            "native_std_logic_drivers");
    assert(elaborated_native_std_logic.ok());
    const auto native_std_logic_q =
        elaborated_native_std_logic.design->find_signal("q");
    assert(native_std_logic_q);
    assert(
        elaborated_native_std_logic.design->signals()
            .at(*native_std_logic_q)
            .resolution
        == fsim::runtime::simir::ResolutionKind::std_logic);
    auto native_std_logic_interpreter =
        elaborated_native_std_logic.design->create_interpreter();
    const auto native_std_logic_result =
        native_std_logic_interpreter->run();
    assert(
        native_std_logic_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        native_std_logic_interpreter
            ->signal_value(*native_std_logic_q)
            .to_msb_string()
        == "X");

    const auto local_variables = fsim::frontend::parse_text(
        "local_variables.sv",
        R"(
module local_variables;
  logic q;
  initial begin
    logic state = 1'b0;
    state = 1'b1;
    q = state;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(local_variables.ok());
    const auto elaborated_locals = compile_and_elaborate(
        local_variables.design, "local_variables");
    assert(elaborated_locals.ok());
    const auto& local_process =
        elaborated_locals.design->processes().front();
    assert(
        local_process.debug_locals.size() == 1
        && local_process.debug_locals.front().name == "state"
        && local_process.debug_locals.front().type_name == "logic"
        && local_process.debug_locals.front().width == 1);
    assert(std::any_of(
        local_process.operations.begin(),
        local_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::CopyRegister>(operation);
        }));
    auto local_interpreter =
        elaborated_locals.design->create_interpreter();
    const auto local_run = local_interpreter->run();
    assert(local_run.status == fsim::runtime::RunStatus::completed);
    assert(
        local_interpreter->read_debug_local(0, 0).to_msb_string()
        == "1");
    const auto local_q =
        elaborated_locals.design->find_signal("q");
    assert(local_q);
    assert(
        local_interpreter->signal_value(*local_q).to_msb_string()
        == "1");

    const auto scoped_variables = fsim::frontend::parse_text(
        "scoped_variables.sv",
        R"(
module scoped_variables;
  logic [7:0] result;
  logic [1:0] count;
  logic [7:0] value;
  initial begin : root_scope
    logic [7:0] value = 8'd1;
    result = 8'd0;
    count = 2'd0;
    begin : inner_scope
      logic [7:0] value = 8'd4;
      result = result + value;
    end : inner_scope
    result = result + value;
    begin
      logic [7:0] anonymous = 8'd1;
      result = result + anonymous;
    end
    for (int lane = 0; lane < 2; lane++) begin : each
      logic [7:0] scratch = 8'd2;
      result = result + scratch;
      scratch = 8'd9;
    end : each
    while (count < 2) begin : dynamic
      logic [7:0] scratch = 8'd3;
      result = result + scratch;
      scratch = 8'd7;
      count++;
    end : dynamic
    if (count == 2) begin : selected
      logic [7:0] branch = 8'd5;
      result = result + branch;
    end : selected
    else begin : alternate
      logic [7:0] branch = 8'd8;
      result = result + branch;
    end : alternate
  end : root_scope
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(scoped_variables.ok());
    const auto elaborated_scoped =
        compile_and_elaborate(
            scoped_variables.design, "scoped_variables");
    if (!elaborated_scoped.ok()) {
        for (const auto& diagnostic :
             elaborated_scoped.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_scoped.ok());
    const auto& scoped_process =
        elaborated_scoped.design->processes().front();
    assert(scoped_process.debug_locals.size() == 7);
    assert(
        scoped_process.debug_locals[0].name
        == "root_scope.value");
    assert(
        scoped_process.debug_locals[1].name
        == "root_scope.inner_scope.value");
    assert(
        scoped_process.debug_locals[2].name.starts_with(
            "root_scope.$block_")
        && scoped_process.debug_locals[2].name.ends_with(
            ".anonymous"));
    assert(
        scoped_process.debug_locals[3].name
        == "root_scope.each.scratch");
    assert(
        scoped_process.debug_locals[4].name
        == "root_scope.dynamic.scratch");
    assert(
        scoped_process.debug_locals[5].name
        == "root_scope.selected.branch");
    assert(
        scoped_process.debug_locals[6].name
        == "root_scope.alternate.branch");
    // The statically expanded two-iteration loop reuses one frame register
    // for its lexical declaration instead of duplicating debugger objects.
    assert(
        std::count_if(
            scoped_process.debug_locals.begin(),
            scoped_process.debug_locals.end(),
            [](const auto& local) {
                return local.name
                    == "root_scope.each.scratch";
            })
        == 1);
    auto scoped_interpreter =
        elaborated_scoped.design->create_interpreter();
    const auto scoped_run = scoped_interpreter->run();
    assert(scoped_run.status == fsim::runtime::RunStatus::completed);
    const auto scoped_result =
        elaborated_scoped.design->find_signal("result");
    const auto scoped_count =
        elaborated_scoped.design->find_signal("count");
    const auto shadowed_signal =
        elaborated_scoped.design->find_signal("value");
    assert(scoped_result && scoped_count && shadowed_signal);
    assert(
        scoped_interpreter
            ->signal_value(*scoped_result)
            .to_msb_string()
        == "00010101");
    assert(
        scoped_interpreter
            ->signal_value(*scoped_count)
            .to_msb_string()
        == "10");
    assert(
        scoped_interpreter
            ->signal_value(*shadowed_signal)
            .to_msb_string()
        == "XXXXXXXX");
    assert(
        scoped_interpreter->read_debug_local(0, 0).to_msb_string()
        == "00000001");
    assert(
        scoped_interpreter->read_debug_local(0, 1).to_msb_string()
        == "00000100");
    assert(
        scoped_interpreter->read_debug_local(0, 2).to_msb_string()
        == "00000001");
    assert(
        scoped_interpreter->read_debug_local(0, 3).to_msb_string()
        == "00001001");
    assert(
        scoped_interpreter->read_debug_local(0, 4).to_msb_string()
        == "00000111");
    assert(
        scoped_interpreter->read_debug_local(0, 5).to_msb_string()
        == "00000101");

    const auto duplicate_scoped_variables =
        fsim::frontend::parse_text(
            "duplicate_scoped_variables.sv",
            R"(
module duplicate_scoped_variables;
  initial begin
    logic duplicate;
    logic duplicate;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(duplicate_scoped_variables.ok());
    const auto rejected_duplicate_scoped =
        compile_and_elaborate(
            duplicate_scoped_variables.design,
            "duplicate_scoped_variables");
    assert(!rejected_duplicate_scoped.ok());
    assert(has_diagnostic(
        rejected_duplicate_scoped, "FSIM-ELAB-053"));

    const auto vhdl_call_point = fsim::frontend::parse_text(
        "vhdl_call_point.vhd",
        R"(
entity vhdl_call_point is end entity;
architecture rtl of vhdl_call_point is
  signal input_value : std_logic;
  signal result : boolean;
begin
  observe: process
  begin
    result <= input_value'stable;
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_call_point.ok());
    const auto elaborated_vhdl_call_point =
        compile_and_elaborate(
            vhdl_call_point.design,
            "vhdl:work.vhdl_call_point(rtl)");
    assert(elaborated_vhdl_call_point.ok());
    assert(std::any_of(
        elaborated_vhdl_call_point.design->processes().front()
            .operations.begin(),
        elaborated_vhdl_call_point.design->processes().front()
            .operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          const auto* point =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::DebugPoint>(
                  &operation);
          return point != nullptr
              && point->kind
                  == fsim::runtime::simir::DebugPointKind::call
              && point->source.path == "vhdl_call_point.vhd"
              && point->source.line == 9;
        }));

    const auto vhdl_local_variables = fsim::frontend::parse_text(
        "local_variables.vhd",
        R"(
entity local_variables is
  port (clk : in std_logic; q : out std_logic);
end entity;
architecture rtl of local_variables is begin
  worker: process(clk)
    variable state : std_logic := '0';
  begin
    state := not state;
    q <= state;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_local_variables.ok());
    const auto elaborated_vhdl_locals =
        compile_and_elaborate(
            vhdl_local_variables.design,
            "vhdl:work.local_variables(rtl)");
    assert(elaborated_vhdl_locals.ok());
    const auto& vhdl_local_process =
        elaborated_vhdl_locals.design->processes().front();
    assert(
        vhdl_local_process.debug_locals.size() == 1
        && vhdl_local_process.debug_locals.front().name == "state"
        && vhdl_local_process.debug_locals.front().type_name
            == "std_logic");
    assert(std::count_if(
               vhdl_local_process.operations.begin(),
               vhdl_local_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return fsim::runtime::simir::operation_holds<
                     fsim::runtime::simir::CopyRegister>(operation);
               })
           >= 2);
    auto vhdl_local_interpreter =
        elaborated_vhdl_locals.design->create_interpreter();
    const auto vhdl_local_clk =
        elaborated_vhdl_locals.design->find_signal("clk");
    const auto vhdl_local_q =
        elaborated_vhdl_locals.design->find_signal("q");
    assert(vhdl_local_clk && vhdl_local_q);
    vhdl_local_interpreter->schedule_signal_at(
        *vhdl_local_clk,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        1);
    const auto vhdl_local_run = vhdl_local_interpreter->run(2);
    assert(
        vhdl_local_run.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_local_interpreter
            ->signal_value(*vhdl_local_q)
            .to_msb_string()
        == "0");
    assert(
        vhdl_local_interpreter
            ->read_debug_local(0, 0)
            .to_msb_string()
        == "0");

    const auto vhdl_waits = fsim::frontend::parse_text(
        "waits.vhd",
        R"(
entity waits is end entity;
architecture rtl of waits is
  signal trigger : std_logic;
  signal q : std_logic;
begin
  worker: process
  begin
    q <= '0';
    wait for 2 ns;
    q <= '1';
    wait on trigger;
    q <= '0';
    wait on trigger;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(vhdl_waits.ok());
    const auto elaborated_vhdl_waits =
        compile_and_elaborate(
            vhdl_waits.design, "vhdl:work.waits(rtl)");
    assert(elaborated_vhdl_waits.ok());
    const auto& vhdl_wait_process =
        elaborated_vhdl_waits.design->processes().front();
    assert(std::count_if(
               vhdl_wait_process.operations.begin(),
               vhdl_wait_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::WaitFor>(operation)
                     || fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::WaitOn>(operation);
               })
           == 3);
    assert(fsim::runtime::simir::operation_holds<fsim::runtime::simir::Jump>(
        vhdl_wait_process.operations.back()));
    auto vhdl_wait_interpreter =
        elaborated_vhdl_waits.design->create_interpreter();
    const auto vhdl_wait_trigger =
        elaborated_vhdl_waits.design->find_signal("trigger");
    const auto vhdl_wait_q =
        elaborated_vhdl_waits.design->find_signal("q");
    assert(vhdl_wait_trigger && vhdl_wait_q);
    vhdl_wait_interpreter->schedule_signal_at(
        *vhdl_wait_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        3);
    const auto vhdl_wait_mid = vhdl_wait_interpreter->run(2);
    assert(
        vhdl_wait_mid.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_wait_interpreter
            ->signal_value(*vhdl_wait_q)
            .to_msb_string()
        == "1");
    const auto vhdl_wait_end = vhdl_wait_interpreter->run(4);
    assert(
        vhdl_wait_end.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_wait_interpreter
            ->signal_value(*vhdl_wait_q)
            .to_msb_string()
        == "0");

    const auto sv_events = fsim::frontend::parse_text(
        "events.sv",
        R"(
module events;
  logic trigger;
  logic observed;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 trigger = 1'b0;
    #1 $finish;
  end
  initial begin
    @(posedge trigger);
    observed = trigger;
    @(negedge trigger) observed = trigger;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(sv_events.ok());
    const auto elaborated_sv_events =
        compile_and_elaborate(
            sv_events.design, "sv:work.events");
    assert(elaborated_sv_events.ok());
    assert(elaborated_sv_events.design->processes().size() == 2);
    const auto& observer_process =
        elaborated_sv_events.design->processes().back();
    assert(std::count_if(
               observer_process.operations.begin(),
               observer_process.operations.end(),
               [](const fsim::runtime::simir::Operation& operation) {
                 return fsim::runtime::simir::operation_holds<
                     fsim::runtime::simir::WaitOn>(operation);
               })
           == 2);
    const auto first_dynamic_wait_operation = std::find_if(
        observer_process.operations.begin(),
        observer_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WaitOn>(operation);
        });
    const auto second_dynamic_wait_operation = std::find_if(
        std::next(first_dynamic_wait_operation),
        observer_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WaitOn>(operation);
        });
    const auto& first_dynamic_wait =
        fsim::runtime::simir::operation_get<fsim::runtime::simir::WaitOn>(
            *first_dynamic_wait_operation);
    const auto& second_dynamic_wait =
        fsim::runtime::simir::operation_get<fsim::runtime::simir::WaitOn>(
            *second_dynamic_wait_operation);
    assert(
        first_dynamic_wait.edges.size() == 1
        && first_dynamic_wait.edges.front()
            == fsim::runtime::simir::EdgeKind::posedge);
    assert(
        second_dynamic_wait.edges.size() == 1
        && second_dynamic_wait.edges.front()
            == fsim::runtime::simir::EdgeKind::negedge);
    auto sv_event_interpreter =
        elaborated_sv_events.design->create_interpreter();
    const auto sv_observed =
        elaborated_sv_events.design->find_signal("observed");
    assert(sv_observed);
    const auto sv_event_mid = sv_event_interpreter->run(1);
    assert(
        sv_event_mid.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        sv_event_interpreter
            ->signal_value(*sv_observed)
            .to_msb_string()
        == "1");
    const auto sv_event_end = sv_event_interpreter->run();
    assert(
        sv_event_end.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        sv_event_interpreter
            ->signal_value(*sv_observed)
            .to_msb_string()
        == "0");

    const auto vhdl_condition_wait =
        fsim::frontend::parse_text(
            "condition_wait.vhd",
            R"(
entity condition_wait is end entity;
architecture rtl of condition_wait is
  signal trigger : boolean;
  signal observed : boolean;
begin
  observer: process
  begin
    wait until trigger;
    observed <= true;
    wait on trigger;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_condition_wait.ok());
    const auto elaborated_vhdl_condition_wait =
        compile_and_elaborate(
            vhdl_condition_wait.design,
            "vhdl:work.condition_wait(rtl)");
    assert(elaborated_vhdl_condition_wait.ok());
    const auto& vhdl_condition_wait_process =
        elaborated_vhdl_condition_wait.design
            ->processes().front();
    assert(
        std::count_if(
            vhdl_condition_wait_process.operations.begin(),
            vhdl_condition_wait_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return fsim::runtime::simir::operation_holds<
                  fsim::runtime::simir::WaitOn>(operation);
            })
        == 3);
    auto vhdl_condition_wait_interpreter =
        elaborated_vhdl_condition_wait.design
            ->create_interpreter();
    const auto vhdl_condition_trigger =
        elaborated_vhdl_condition_wait.design
            ->find_signal("trigger");
    const auto vhdl_condition_observed =
        elaborated_vhdl_condition_wait.design
            ->find_signal("observed");
    assert(vhdl_condition_trigger && vhdl_condition_observed);
    vhdl_condition_wait_interpreter->schedule_signal_at(
        *vhdl_condition_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("0"),
        1);
    vhdl_condition_wait_interpreter->schedule_signal_at(
        *vhdl_condition_trigger,
        fsim::runtime::PackedLogic4::from_msb_string("1"),
        2);
    const auto vhdl_condition_wait_before =
        vhdl_condition_wait_interpreter->run(1);
    assert(
        vhdl_condition_wait_before.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_condition_wait_interpreter
            ->signal_value(*vhdl_condition_observed)
            .to_msb_string()
        == "0");
    const auto vhdl_condition_wait_after =
        vhdl_condition_wait_interpreter->run(3);
    assert(
        vhdl_condition_wait_after.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        vhdl_condition_wait_interpreter
            ->signal_value(*vhdl_condition_observed)
            .to_msb_string()
        == "1");

    const auto vhdl_combined_wait =
        fsim::frontend::parse_text(
            "combined_wait.vhd",
            R"(
entity combined_wait is end entity;
architecture rtl of combined_wait is
  signal trigger : boolean;
  signal timed_result : boolean;
  signal event_result : boolean;
  signal constant_timeout_result : boolean;
  signal permanent_result : boolean;
begin
  driver: process
  begin
    wait for 1 ns;
    trigger <= true;
    wait for 2 ns;
    trigger <= false;
    wait;
  end process;
  observer: process
  begin
    wait on trigger until false for 2 ns;
    timed_result <= true;
    wait on trigger until not trigger for 2 ns;
    event_result <= true;
    wait until true for 1 ns;
    constant_timeout_result <= true;
    wait until true;
    permanent_result <= true;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_combined_wait.ok());
    const auto elaborated_vhdl_combined_wait =
        compile_and_elaborate(
            vhdl_combined_wait.design,
            "vhdl:work.combined_wait(rtl)");
    if (!elaborated_vhdl_combined_wait.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_combined_wait.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_combined_wait.ok());
    const auto& combined_wait_observer =
        elaborated_vhdl_combined_wait.design
            ->processes()
            .back();
    assert(std::ranges::any_of(
        combined_wait_observer.operations,
        [](const fsim::runtime::simir::Operation& operation) {
            const auto* wait =
                fsim::runtime::simir::operation_get_if<fsim::runtime::simir::WaitOn>(
                    &operation);
            return wait != nullptr
                && wait->timeout
                && wait->timeout_result
                && wait->timeout_origin;
        }));
    assert(std::ranges::any_of(
        combined_wait_observer.operations,
        [](const fsim::runtime::simir::Operation& operation) {
            return fsim::runtime::simir::operation_holds<
                fsim::runtime::simir::WaitForever>(
                    operation);
        }));

    auto combined_wait_interpreter =
        elaborated_vhdl_combined_wait.design
            ->create_interpreter();
    const auto combined_timed =
        elaborated_vhdl_combined_wait.design
            ->find_signal("timed_result");
    const auto combined_event =
        elaborated_vhdl_combined_wait.design
            ->find_signal("event_result");
    const auto combined_constant_timeout =
        elaborated_vhdl_combined_wait.design
            ->find_signal("constant_timeout_result");
    const auto combined_permanent =
        elaborated_vhdl_combined_wait.design
            ->find_signal("permanent_result");
    assert(
        combined_timed
        && combined_event
        && combined_constant_timeout
        && combined_permanent);
    const auto combined_before_timeout =
        combined_wait_interpreter->run(1);
    assert(
        combined_before_timeout.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_timed)
            .to_msb_string()
        == "0");
    const auto combined_after_timeout =
        combined_wait_interpreter->run(2);
    assert(
        combined_after_timeout.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_timed)
            .to_msb_string()
        == "1");
    const auto combined_after_event =
        combined_wait_interpreter->run(3);
    assert(
        combined_after_event.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_event)
            .to_msb_string()
        == "1");
    const auto combined_after_constant_timeout =
        combined_wait_interpreter->run(4);
    assert(
        combined_after_constant_timeout.status
        == fsim::runtime::RunStatus::completed);
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_constant_timeout)
            .to_msb_string()
        == "1");
    assert(
        combined_wait_interpreter
            ->signal_value(*combined_permanent)
            .to_msb_string()
        == "0");

    const auto sv_condition_wait =
        fsim::frontend::parse_text(
            "condition_wait.sv",
            R"(
module condition_wait;
  logic gate;
  logic enable;
  logic observed;
  logic constant_wait_result;
  initial begin
    gate = 1'b0;
    enable = 1'b0;
    #1 gate = 1'bx;
    #1 enable = 1'b1;
    #1 $finish;
  end
  initial begin
    observed = 1'b0;
    wait (gate || enable) observed = 1'b1;
  end
  initial begin
    constant_wait_result = 1'b0;
    wait (1'b0);
    constant_wait_result = 1'b1;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(sv_condition_wait.ok());
    const auto elaborated_sv_condition_wait =
        compile_and_elaborate(
            sv_condition_wait.design,
            "sv:work.condition_wait");
    if (!elaborated_sv_condition_wait.ok()) {
        for (const auto& diagnostic :
             elaborated_sv_condition_wait.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_sv_condition_wait.ok());
    assert(std::ranges::any_of(
        elaborated_sv_condition_wait.design->processes(),
        [](const fsim::runtime::simir::Process& process) {
          return std::ranges::any_of(
              process.operations,
              [](const fsim::runtime::simir::Operation& operation) {
                const auto* wait = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::WaitOn>(
                    &operation);
                return wait != nullptr
                    && wait->signals.size() == 2;
              });
        }));
    auto sv_condition_wait_interpreter =
        elaborated_sv_condition_wait.design
            ->create_interpreter();
    const auto sv_condition_observed =
        elaborated_sv_condition_wait.design
            ->find_signal("observed");
    const auto sv_constant_wait_result =
        elaborated_sv_condition_wait.design
            ->find_signal("constant_wait_result");
    assert(sv_condition_observed && sv_constant_wait_result);
    const auto sv_condition_wait_before =
        sv_condition_wait_interpreter->run(1);
    assert(
        sv_condition_wait_before.status
        == fsim::runtime::RunStatus::time_limit);
    assert(
        sv_condition_wait_interpreter
            ->signal_value(*sv_condition_observed)
            .to_msb_string()
        == "0");
    const auto sv_condition_wait_after =
        sv_condition_wait_interpreter->run();
    assert(
        sv_condition_wait_after.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        sv_condition_wait_interpreter
            ->signal_value(*sv_condition_observed)
            .to_msb_string()
        == "1");
    assert(
        sv_condition_wait_interpreter
            ->signal_value(*sv_constant_wait_result)
            .to_msb_string()
        == "0");

    const auto invalid_vhdl_condition_wait =
        fsim::frontend::parse_text(
            "invalid_condition_wait.vhd",
            R"(
entity invalid_condition_wait is end entity;
architecture rtl of invalid_condition_wait is
  signal trigger : std_logic;
begin
  observer: process
  begin
    wait until trigger;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_condition_wait.ok());
    const auto rejected_vhdl_condition_wait =
        compile_and_elaborate(
            invalid_vhdl_condition_wait.design,
            "vhdl:work.invalid_condition_wait(rtl)");
    assert(!rejected_vhdl_condition_wait.ok());
    assert(has_diagnostic(
        rejected_vhdl_condition_wait, "FSIM-ELAB-079"));

    const auto unknown_wait = fsim::frontend::parse_text(
        "unknown_wait.vhd",
        R"(
entity unknown_wait is end entity;
architecture rtl of unknown_wait is begin
  worker: process begin
    wait on missing;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(unknown_wait.ok());
    const auto rejected_unknown_wait =
        compile_and_elaborate(
            unknown_wait.design,
            "vhdl:work.unknown_wait(rtl)");
    assert(!rejected_unknown_wait.ok());
    assert(has_diagnostic(rejected_unknown_wait, "FSIM-ELAB-059"));

    const auto vector_edge_wait = fsim::frontend::parse_text(
        "vector_edge_wait.sv",
        R"(
module vector_edge_wait;
  logic [1:0] trigger;
  initial @(posedge trigger);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(vector_edge_wait.ok());
    const auto rejected_vector_edge_wait =
        compile_and_elaborate(
            vector_edge_wait.design, "sv:work.vector_edge_wait");
    assert(!rejected_vector_edge_wait.ok());
    assert(has_diagnostic(
        rejected_vector_edge_wait, "FSIM-ELAB-060"));

    const auto wildcard_processes = fsim::frontend::parse_text(
        "wildcard.sv",
        R"(
module wildcard_processes;
  logic a;
  logic q;
  logic y;
  logic latched;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latched = q;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wildcard_processes.ok());
    const auto elaborated_wildcard =
        compile_and_elaborate(
            wildcard_processes.design, "sv:work.wildcard_processes");
    assert(elaborated_wildcard.ok());
    assert(elaborated_wildcard.design->processes().size() == 3);
    const auto wildcard_a =
        elaborated_wildcard.design->find_signal("a");
    const auto wildcard_q =
        elaborated_wildcard.design->find_signal("q");
    const auto wildcard_y =
        elaborated_wildcard.design->find_signal("y");
    const auto wildcard_latched =
        elaborated_wildcard.design->find_signal("latched");
    assert(
        wildcard_a && wildcard_q && wildcard_y
        && wildcard_latched);
    assert((
        elaborated_wildcard.design->processes()[0]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_a,
             fsim::runtime::simir::EdgeKind::any}}));
    assert((
        elaborated_wildcard.design->processes()[1]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_q,
             fsim::runtime::simir::EdgeKind::any}}));
    assert((
        elaborated_wildcard.design->processes()[2]
            .static_sensitivity
        == std::vector<fsim::runtime::simir::Sensitivity>{
            {*wildcard_a,
             fsim::runtime::simir::EdgeKind::any},
            {*wildcard_q,
             fsim::runtime::simir::EdgeKind::any}}));
    auto wildcard_interpreter =
        elaborated_wildcard.design->create_interpreter();
    (void)wildcard_interpreter->run();
    wildcard_interpreter->deposit_signal(
        *wildcard_a,
        fsim::runtime::PackedLogic4::from_msb_string("0"));
    (void)wildcard_interpreter->run();
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_q)
            .to_msb_string()
        == "0");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_y)
            .to_msb_string()
        == "1");
    wildcard_interpreter->deposit_signal(
        *wildcard_a,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    (void)wildcard_interpreter->run();
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_q)
            .to_msb_string()
        == "1");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_y)
            .to_msb_string()
        == "0");
    assert(
        wildcard_interpreter
            ->signal_value(*wildcard_latched)
            .to_msb_string()
        == "1");

    const auto mutable_strings = fsim::frontend::parse_text(
        "mutable_strings.sv",
        R"(
module mutable_strings;
  string title = "fsim";
  function automatic string decorate(input string value);
    string suffix = "-v1";
    return {value, suffix};
  endfunction
  task automatic remember(
      input string value,
      output string copied);
    string temporary;
    #1;
    temporary = {value, "!"};
    copied = temporary;
  endtask
  initial begin : worker
    string copy;
    remember(decorate(title), copy);
    if (copy != "")
      copy[0] = "F";
    if (copy.len() == 8)
      title = copy;
    $display("%s", title);
    $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mutable_strings.ok());
    const auto elaborated_strings =
        compile_and_elaborate(
            mutable_strings.design, "sv:work.mutable_strings");
    assert(elaborated_strings.ok());
    assert(
        elaborated_strings.design->string_objects().size() == 1
        && elaborated_strings.design->string_objects()[0].name
            == "mutable_strings.title");
    const auto string_id =
        elaborated_strings.design->string_objects()[0].id;
    auto string_interpreter =
        elaborated_strings.design->create_interpreter();
    std::vector<std::string> string_output;
    string_interpreter->set_output_hook(
        [&](const auto,
            const std::string_view text,
            const bool,
            const auto,
            const auto) {
          string_output.emplace_back(text);
        });
    const auto string_result = string_interpreter->run();
    assert(
        string_result.status
            == fsim::runtime::RunStatus::stopped
        && string_interpreter->string_object_value(string_id)
            == "Fsim-v1!"
        && string_output
            == std::vector<std::string>{"Fsim-v1!"}
        && string_result.time == 1);

    const auto nonblocking_string =
        fsim::frontend::parse_text(
            "nonblocking_string.sv",
            R"(
module nonblocking_string;
  string value;
  initial value <= "bad";
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(nonblocking_string.ok());
    const auto rejected_nonblocking_string =
        compile_and_elaborate(
            nonblocking_string.design,
            "sv:work.nonblocking_string");
    assert(!rejected_nonblocking_string.ok());
    assert(has_diagnostic(
        rejected_nonblocking_string,
        "FSIM-ELAB-SVSTRING-013"));

    const auto oversize_source =
        std::string{"module oversize_string; string value = \""}
        + std::string(
            fsim::runtime::simir::maximum_string_bytes + 1U,
            'x')
        + "\"; endmodule";
    const auto oversize_string =
        fsim::frontend::parse_text(
            "oversize_string.sv",
            oversize_source,
            fsim::frontend::Language::SystemVerilog2017);
    assert(oversize_string.ok());
    const auto rejected_oversize_string =
        compile_and_elaborate(
            oversize_string.design,
            "sv:work.oversize_string");
    assert(!rejected_oversize_string.ok());
    assert(has_diagnostic(
        rejected_oversize_string,
        "FSIM-ELAB-SVSTRING-007"));
}

} // namespace fsim::tests::elaboration
