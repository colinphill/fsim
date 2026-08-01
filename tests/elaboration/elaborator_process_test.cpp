// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

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
    const auto rejected_edge =
        fsim::elaboration::elaborate(unsafe_edge.design, "unsafe_edge");
    assert(!rejected_edge.ok());
    assert(has_diagnostic(rejected_edge, "FSIM-ELAB-045"));

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
        fsim::elaboration::elaborate(logical_not.design, "logical_not");
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
        fsim::elaboration::elaborate(
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
            assert(
                wait_index
                < operation_index
                      .operator()<fsim::runtime::simir::ReadSignal>(
                          operations));
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
    inconsistent_assignment_control.design.units.front()
        .processes.front()
        .statements.front()
        .procedural_assignment_control =
        fsim::frontend::ProceduralAssignmentControl::Event;
    const auto rejected_assignment_control =
        fsim::elaboration::elaborate(
            inconsistent_assignment_control.design,
            "inconsistent_assignment_control");
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
    inconsistent_update.design.units.front()
        .processes.front()
        .statements.front()
        .procedural_update_operator = "-";
    const auto rejected_update = fsim::elaboration::elaborate(
        inconsistent_update.design, "inconsistent_update");
    assert(!rejected_update.ok());
    assert(has_diagnostic(rejected_update, "FSIM-ELAB-106"));

    const auto invalid_force = fsim::frontend::parse_text(
        "invalid_force.sv",
        R"(
module invalid_force;
  logic [3:0] four_state;
  bit [3:0] two_state;
  logic [1:0] index;
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
    const auto rejected_force = fsim::elaboration::elaborate(
        invalid_force.design, "invalid_force");
    assert(!rejected_force.ok());
    assert(has_diagnostic(rejected_force, "FSIM-ELAB-SVFORCE-001"));
    assert(has_diagnostic(rejected_force, "FSIM-ELAB-SVFORCE-002"));
    assert(has_diagnostic(rejected_force, "FSIM-ELAB-SVFORCE-003"));

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
        fsim::elaboration::elaborate(
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
    unsupported_domain.design.units.front().ports.front().type.domain =
        fsim::frontend::ValueDomain::Unknown;
    const auto rejected_domain = fsim::elaboration::elaborate(
        unsupported_domain.design, "unsupported_domain");
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
    const auto elaborated_defaults = fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
    const auto rejected_lossy_assignment =
        fsim::elaboration::elaborate(
            lossy_assignment.design, "lossy_assignment");
    assert(!rejected_lossy_assignment.ok());
    assert(has_diagnostic(
        rejected_lossy_assignment, "FSIM-ELAB-050"));

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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
    const auto missing_resolver = fsim::elaboration::elaborate(
        multiple_drivers.design, "driver_top");
    assert(!missing_resolver.ok());
    assert(has_diagnostic(missing_resolver, "FSIM-ELAB-BIND-024"));
    const std::vector<fsim::elaboration::Binding> resolver_bindings{
        {"driver_top.first", "sv:work.driver", std::string{"sv_wire"}},
        {"driver_top.second", "sv:work.other_driver", std::string{"sv_wire"}},
    };
    const auto resolved_boundary = fsim::elaboration::elaborate(
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
    const auto invalid_resolver = fsim::elaboration::elaborate(
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
    const auto rejected_process_drivers = fsim::elaboration::elaborate(
        process_drivers.design, "process_drivers");
    assert(!rejected_process_drivers.ok());
    assert(has_diagnostic(
        rejected_process_drivers, "FSIM-ELAB-DRV-001"));

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
    const auto rejected_selected_process_drivers =
        fsim::elaboration::elaborate(
            selected_process_drivers.design,
            "selected_process_drivers");
    assert(!rejected_selected_process_drivers.ok());
    assert(has_diagnostic(
        rejected_selected_process_drivers,
        "FSIM-ELAB-DRV-001"));

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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
    const auto elaborated_locals = fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
            oversize_string.design,
            "sv:work.oversize_string");
    assert(!rejected_oversize_string.ok());
    assert(has_diagnostic(
        rejected_oversize_string,
        "FSIM-ELAB-SVSTRING-007"));
}

} // namespace fsim::tests::elaboration
