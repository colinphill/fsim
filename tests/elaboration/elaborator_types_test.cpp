// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_assertion_types_and_random_lowering() {
const auto invalid_vhdl_assertion =
        fsim::frontend::parse_text(
            "invalid_assertion.vhd",
            R"(
entity invalid_assertion is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_assertion is
begin
  invalid: process(gate)
  begin
    assert gate;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_assertion.ok());
    const auto rejected_vhdl_assertion =
        fsim::elaboration::elaborate(
            invalid_vhdl_assertion.design,
            "vhdl:work.invalid_assertion(rtl)");
    assert(!rejected_vhdl_assertion.ok());
    assert(has_diagnostic(
        rejected_vhdl_assertion, "FSIM-ELAB-051"));

    const auto concurrent_vhdl_assertion =
        fsim::frontend::parse_text(
            "concurrent_assertion.vhd",
            R"(
entity concurrent_assertion is
end entity;
architecture rtl of concurrent_assertion is
  signal gate : boolean;
begin
  gate_check: assert gate
    report "concurrent gate failed" severity failure;
  driver: process
  begin
    wait for 1 ns;
    gate <= false;
    wait;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(concurrent_vhdl_assertion.ok());
    const auto elaborated_concurrent_vhdl_assertion =
        fsim::elaboration::elaborate(
            concurrent_vhdl_assertion.design,
            "vhdl:work.concurrent_assertion(rtl)");
    assert(elaborated_concurrent_vhdl_assertion.ok());
    assert(
        elaborated_concurrent_vhdl_assertion.design
            ->processes().size()
        == 2);
    const auto& concurrent_assertion_process =
        elaborated_concurrent_vhdl_assertion.design
            ->processes().front();
    assert(
        concurrent_assertion_process.name
            == "concurrent_assertion.gate_check"
        && concurrent_assertion_process.static_sensitivity.size()
            == 1);
    const auto concurrent_gate =
        elaborated_concurrent_vhdl_assertion.design
            ->find_signal("gate");
    assert(concurrent_gate);
    auto concurrent_assertion_interpreter =
        elaborated_concurrent_vhdl_assertion.design
            ->create_interpreter();
    concurrent_assertion_interpreter->deposit_signal(
        *concurrent_gate,
        fsim::runtime::PackedLogic4::from_msb_string("1"));
    bool saw_concurrent_assertion = false;
    try {
        (void)concurrent_assertion_interpreter->run();
    } catch (const fsim::runtime::simir::AssertionError& error) {
        saw_concurrent_assertion = true;
        assert(
            std::string_view{error.what()}.find(
                "concurrent gate failed")
            != std::string_view::npos);
        assert(
            error.source().path == "concurrent_assertion.vhd");
    }
    assert(saw_concurrent_assertion);

    const auto vector_assertion =
        fsim::frontend::parse_text(
            "vector_assertion.sv",
            R"(
module vector_assertion;
  initial begin
    assert (4'bx001);
    assert (4'bx000) else $error("vector condition failed");
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(vector_assertion.ok());
    const auto elaborated_vector_assertion =
        fsim::elaboration::elaborate(
            vector_assertion.design,
            "sv:work.vector_assertion");
    assert(elaborated_vector_assertion.ok());
    auto vector_assertion_interpreter =
        elaborated_vector_assertion.design->create_interpreter();
    std::vector<std::string> vector_assertion_reports;
    vector_assertion_interpreter->set_report_hook(
        [&vector_assertion_reports](
            const fsim::runtime::simir::ProcessId,
            const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity severity,
            const fsim::runtime::simir::SourceLocation&,
            const fsim::runtime::SimulationTick,
            const std::uint64_t) {
          assert(
              severity
              == fsim::runtime::simir::AssertionSeverity::error);
          vector_assertion_reports.emplace_back(message);
        });
    const auto vector_assertion_result =
        vector_assertion_interpreter->run();
    assert(
        vector_assertion_result.status
            == fsim::runtime::RunStatus::completed
        && vector_assertion_reports
            == std::vector<std::string>{
                "vector condition failed"});

    const auto named_event_source = fsim::frontend::parse_text(
        "named_event.sv",
        R"(
module named_event;
  event fired;
  logic observed;
  initial begin
    #1 -> fired;
    #1 ->> fired;
    #1 ->> #2 fired;
    #3 $finish;
  end
  initial begin
    @(fired);
    observed = 1'b1;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(named_event_source.ok());
    const auto named_event_design =
        fsim::elaboration::elaborate(
            named_event_source.design, "sv:work.named_event");
    assert(named_event_design.ok());
    const auto event_signal =
        named_event_design.design->find_signal("fired");
    assert(event_signal);
    const auto named_event_interpreter =
        named_event_design.design->create_interpreter();
    assert(
        named_event_interpreter->signal_value(*event_signal)
            .to_msb_string()
        == "0");
    const auto& trigger_process =
        named_event_design.design->processes().front();
    assert(
        std::count_if(
            trigger_process.operations.begin(),
            trigger_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return fsim::runtime::simir::operation_holds<
                  fsim::runtime::simir::WriteBlocking>(operation);
            })
        == 1);
    assert(
        std::count_if(
            trigger_process.operations.begin(),
            trigger_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              const auto* delayed =
                  fsim::runtime::simir::operation_get_if<fsim::runtime::simir::WriteAfter>(
                      &operation);
              return delayed != nullptr && delayed->delay == 2;
            })
        == 1);
    assert(
        std::count_if(
            trigger_process.operations.begin(),
            trigger_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return fsim::runtime::simir::operation_holds<
                  fsim::runtime::simir::WriteUpdate>(operation);
            })
        == 1);
    const auto& waiting_process =
        named_event_design.design->processes().back();
    assert(
        std::count_if(
            waiting_process.operations.begin(),
            waiting_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
              return fsim::runtime::simir::operation_holds<
                  fsim::runtime::simir::WaitOn>(operation);
            })
        == 1);

    const auto invalid_event_source = fsim::frontend::parse_text(
        "invalid_event.sv",
        R"(
module invalid_event;
  logic ordinary;
  initial begin
    -> missing;
    -> ordinary;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_event_source.ok());
    const auto invalid_event_design =
        fsim::elaboration::elaborate(
            invalid_event_source.design, "sv:work.invalid_event");
    assert(!invalid_event_design.ok());
    assert(has_diagnostic(invalid_event_design, "FSIM-ELAB-100"));
    assert(has_diagnostic(invalid_event_design, "FSIM-ELAB-101"));

    const auto display_source = fsim::frontend::parse_text(
        "display.sv",
        R"(
module display;
  logic q;
  initial begin
    $display("first");
    $strobe("postponed");
    $write("continued");
    #1 $display;
    $write;
    $monitor("literal replacement");
    $monitor("q=%b", q);
    $monitoroff;
    $monitoron;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(display_source.ok());
    const auto display_design =
        fsim::elaboration::elaborate(
            display_source.design, "sv:work.display");
    assert(display_design.ok());
    const auto& display_operations =
        display_design.design->processes().front().operations;
    std::vector<fsim::runtime::simir::Display> displays;
    for (const auto& operation : display_operations) {
        if (const auto* display =
                fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Display>(
                    &operation)) {
            displays.push_back(*display);
        }
    }
    assert(
        displays.size() == 4
        && displays[0].text == "first"
        && displays[0].newline
        && !displays[0].postponed
        && displays[1].text == "continued"
        && !displays[1].newline
        && !displays[1].postponed
        && displays[2].text.empty()
        && displays[2].newline
        && displays[3].text.empty()
        && !displays[3].newline);
    std::vector<fsim::runtime::simir::MonitorInstall> monitors;
    for (const auto& operation : display_operations) {
        if (const auto* monitor =
                fsim::runtime::simir::operation_get_if<fsim::runtime::simir::MonitorInstall>(
                    &operation)) {
            monitors.push_back(*monitor);
        }
    }
    assert(
        monitors.size() == 3
        && monitors[0].values.empty()
        && monitors[0].trailing_text == "postponed"
        && monitors[0].one_shot
        && monitors[1].values.empty()
        && monitors[1].trailing_text == "literal replacement"
        && !monitors[1].one_shot);
    const auto& monitor = monitors[2];
    assert(
        monitor.values.size() == 1
        && monitor.values.front().kind
            == fsim::runtime::simir::MonitorValueKind::signal
        && monitor.values.front().format
            == fsim::runtime::simir::OutputFormat::binary
        && monitor.values.front().prefix == "q=");
    std::vector<bool> monitor_controls;
    for (const auto& operation : display_operations) {
        if (const auto* control =
                fsim::runtime::simir::operation_get_if<fsim::runtime::simir::MonitorControl>(
                    &operation)) {
            monitor_controls.push_back(control->enabled);
        }
    }
    assert(
        monitor_controls == std::vector<bool>({false, true}));

    const auto vhdl_record_source =
        fsim::frontend::parse_text(
            "record_execution.vhd",
            R"(
entity record_execution is
end entity;

architecture rtl of record_execution is
  type Packet_T is record
    Upper : std_logic_vector(3 downto 0);
    Lower : bit_vector(0 to 3);
    Flag  : boolean;
  end record Packet_T;
  signal source : Packet_T;
  signal result : Packet_T;
  signal equal_result : boolean;
begin
  drive_source : process
  begin
    source.Upper <= "ULH-";
    source.Lower <= "1010";
    source.Flag <= true;
    wait;
  end process;

  copy_record : process(source)
    variable local : Packet_T;
  begin
    local := source;
    local.Upper(1 downto 0) := source.Upper(3 downto 2);
    local.Lower(0) := source.Lower(3);
    result <= local;
  end process;

  equal_result <= result = source;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_record_source.ok());
    const auto vhdl_record_design =
        fsim::elaboration::elaborate(
            vhdl_record_source.design,
            "vhdl:work.record_execution(rtl)");
    if (!vhdl_record_design.ok()) {
        for (const auto& diagnostic :
             vhdl_record_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ":"
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(vhdl_record_design.ok());
    const auto record_source_signal =
        vhdl_record_design.design->find_signal("source");
    const auto record_result_signal =
        vhdl_record_design.design->find_signal("result");
    const auto record_equal_signal =
        vhdl_record_design.design->find_signal("equal_result");
    assert(
        record_source_signal && record_result_signal
        && record_equal_signal);
    const auto& record_source_info =
        vhdl_record_design.design->signals().at(
            *record_source_signal);
    assert(
        record_source_info.width == 9
        && record_source_info.source_domain
            == fsim::frontend::ValueDomain::Logic9
        && record_source_info.packed_members.size() == 3
        && record_source_info.packed_members[0].name == "upper"
        && record_source_info.packed_members[0].lsb_offset == 5
        && record_source_info.packed_members[1].name == "lower"
        && record_source_info.packed_members[1].lsb_offset == 1
        && record_source_info.packed_members[2].name == "flag"
        && record_source_info.packed_members[2].lsb_offset == 0);
    auto vhdl_record_interpreter =
        vhdl_record_design.design->create_interpreter();
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_source_signal)
            .to_msb_string()
        == "UUUU00000");
    const auto vhdl_record_result =
        vhdl_record_interpreter->run();
    assert(
        vhdl_record_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_source_signal)
            .to_msb_string()
        == "ULH-10101");
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_result_signal)
            .to_msb_string()
        == "ULUL00101");
    assert(
        vhdl_record_interpreter
            ->signal_value(*record_equal_signal)
            .to_msb_string()
        == "0");

    const auto vhdl_record_aggregate_source =
        fsim::frontend::parse_text(
            "record_aggregate_execution.vhd",
            R"(
entity record_aggregate_execution is
end entity;

architecture rtl of record_aggregate_execution is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal source : Packet_T;
  signal result : Packet_T;
  signal conditional_result : Packet_T;
  signal equal_result : boolean;
  signal different_result : boolean;
begin
  drive_source : process
    variable local : Packet_T :=
      (Valid => true, Data => "ULH-");
  begin
    local := ("10Z-", false);
    source <= local;
    wait;
  end process;

  result <= (Data => "ULH-", others => true);
  conditional_result <=
    (Data => "01LH", Valid => true) when true else
    ("0000", false);
  equal_result <=
    result = (Valid => true, Data => "ULH-");
  different_result <=
    (Data => "ULH-", Valid => true) /= source;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_record_aggregate_source.ok());
    const auto vhdl_record_aggregate_design =
        fsim::elaboration::elaborate(
            vhdl_record_aggregate_source.design,
            "vhdl:work.record_aggregate_execution(rtl)");
    if (!vhdl_record_aggregate_design.ok()) {
        for (const auto& diagnostic :
             vhdl_record_aggregate_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ":"
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(vhdl_record_aggregate_design.ok());
    const auto aggregate_source_signal =
        vhdl_record_aggregate_design.design->find_signal("source");
    const auto aggregate_result_signal =
        vhdl_record_aggregate_design.design->find_signal("result");
    const auto aggregate_conditional_signal =
        vhdl_record_aggregate_design.design->find_signal(
            "conditional_result");
    const auto aggregate_equal_signal =
        vhdl_record_aggregate_design.design->find_signal(
            "equal_result");
    const auto aggregate_different_signal =
        vhdl_record_aggregate_design.design->find_signal(
            "different_result");
    assert(
        aggregate_source_signal
        && aggregate_result_signal
        && aggregate_conditional_signal
        && aggregate_equal_signal
        && aggregate_different_signal);
    std::size_t aggregate_insert_count = 0;
    for (const auto& process :
         vhdl_record_aggregate_design.design->processes()) {
        aggregate_insert_count += static_cast<std::size_t>(
            std::count_if(
                process.operations.begin(),
                process.operations.end(),
                [](const auto& operation) {
                    return fsim::runtime::simir::operation_holds<
                        fsim::runtime::simir::Insert>(operation);
                }));
    }
    assert(aggregate_insert_count == 14);
    auto aggregate_interpreter =
        vhdl_record_aggregate_design.design->create_interpreter();
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_source_signal)
            .to_msb_string()
        == "UUUU0");
    const auto aggregate_run = aggregate_interpreter->run();
    assert(
        aggregate_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_source_signal)
            .to_msb_string()
        == "10Z-0");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_result_signal)
            .to_msb_string()
        == "ULH-1");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_conditional_signal)
            .to_msb_string()
        == "01LH1");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_equal_signal)
            .to_msb_string()
        == "1");
    assert(
        aggregate_interpreter
            ->signal_value(*aggregate_different_signal)
            .to_msb_string()
        == "1");

    const auto invalid_vhdl_record_aggregates =
        fsim::frontend::parse_text(
            "invalid_record_aggregate_execution.vhd",
            R"(
entity invalid_record_aggregate_execution is
end entity;

architecture rtl of invalid_record_aggregate_execution is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal result : Packet_T;
  signal flag : boolean;
begin
  invalid_forms : process
  begin
    flag <= (true, false);
    result <=
      (Unknown => "0000", Data => "0000", Valid => true);
    result <=
      (Data => "0000", DATA => "1111", Valid => true);
    result <= ("0000", true, false);
    result <= (Data => "0000");
    result <= (Data => "00", Valid => true);
    result <= (Data => "0000", Valid => 'X');
    wait;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_record_aggregates.ok());
    const auto invalid_aggregate_design =
        fsim::elaboration::elaborate(
            invalid_vhdl_record_aggregates.design,
            "vhdl:work.invalid_record_aggregate_execution(rtl)");
    const auto has_aggregate_diagnostic =
        [&](const std::string_view code) {
            return std::ranges::any_of(
                invalid_aggregate_design.diagnostics,
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                });
        };
    assert(
        !invalid_aggregate_design.ok()
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-001")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-003")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-004")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-005")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-006")
        && has_aggregate_diagnostic("FSIM-ELAB-VHAGG-007"));

    auto malformed_aggregate_source =
        fsim::frontend::parse_text(
            "malformed_record_aggregate_metadata.vhd",
            R"(
entity malformed_record_aggregate_metadata is
end entity;
architecture rtl of malformed_record_aggregate_metadata is
  type Pair_T is record
    Left, Right : bit;
  end record Pair_T;
  signal value : Pair_T;
begin
  value <= ('0', '1');
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(malformed_aggregate_source.ok());
    auto& malformed_expression =
        malformed_aggregate_source.design.units.back()
            .concurrent_statements.front().value;
    assert(
        malformed_expression.kind
        == fsim::frontend::ExpressionKind::Aggregate);
    malformed_expression.aggregate_choices.pop_back();
    const auto malformed_aggregate_design =
        fsim::elaboration::elaborate(
            malformed_aggregate_source.design,
            "vhdl:work.malformed_record_aggregate_metadata(rtl)");
    assert(
        !malformed_aggregate_design.ok()
        && std::ranges::any_of(
            malformed_aggregate_design.diagnostics,
            [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-ELAB-VHAGG-002";
            }));

    const auto package_record_source =
        fsim::frontend::parse_text(
            "package_record_hierarchy.vhd",
            R"(
package Packet_Types is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
end package;

use work.packet_types.all;
entity Record_Child is
  port (
    Source : in packet_t;
    Result : out work.packet_types.packet_t
  );
end entity;

architecture rtl of record_child is
begin
  result <= source;
end architecture;

use work.packet_types.packet_t;
entity Package_Record_Hierarchy is
end entity;

use work.packet_types.packet_t;
architecture rtl of package_record_hierarchy is
  signal source : packet_t;
  signal result : packet_types.packet_t;
  signal equal_result : boolean;
begin
  drive_source : process
  begin
    source.Data <= "ULH-";
    source.Valid <= true;
    wait;
  end process;

  child : entity work.record_child(rtl)
    port map (
      Source => source,
      Result => result
    );

  equal_result <= result = source;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(package_record_source.ok());
    const auto package_record_design =
        fsim::elaboration::elaborate(
            package_record_source.design,
            "vhdl:work.package_record_hierarchy(rtl)");
    if (!package_record_design.ok()) {
        for (const auto& diagnostic :
             package_record_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << " at "
                      << diagnostic.span.begin.line << ":"
                      << diagnostic.span.begin.column << '\n';
        }
    }
    assert(package_record_design.ok());
    assert(
        package_record_design.design->specializations().size()
        == 2);
    const auto package_record_input =
        package_record_design.design->find_signal("source");
    const auto package_record_output =
        package_record_design.design->find_signal("result");
    const auto package_record_equal =
        package_record_design.design->find_signal("equal_result");
    assert(
        package_record_input && package_record_output
        && package_record_equal);
    assert(
        package_record_design.design
            ->signals()
            .at(*package_record_input)
            .packed_members.size()
        == 2);
    auto package_record_interpreter =
        package_record_design.design->create_interpreter();
    assert(
        package_record_interpreter
            ->signal_value(*package_record_input)
            .to_msb_string()
        == "UUUU0");
    const auto package_record_run =
        package_record_interpreter->run();
    assert(
        package_record_run.status
        == fsim::runtime::RunStatus::completed);
    assert(
        package_record_interpreter
            ->signal_value(*package_record_input)
            .to_msb_string()
        == "ULH-1");
    assert(
        package_record_interpreter
            ->signal_value(*package_record_output)
            .to_msb_string()
        == "ULH-1");
    assert(
        package_record_interpreter
            ->signal_value(*package_record_equal)
            .to_msb_string()
        == "1");

    const auto unknown_record_type =
        fsim::frontend::parse_text(
            "unknown_record_type.vhd",
            R"(
entity unknown_record_type is
  port (value : in missing_packet_t);
end entity;
architecture rtl of unknown_record_type is
  type missing_packet_t is record
    value : bit;
  end record;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(unknown_record_type.ok());
    const auto rejected_unknown_record_type =
        fsim::elaboration::elaborate(
            unknown_record_type.design,
            "vhdl:work.unknown_record_type(rtl)");
    assert(!rejected_unknown_record_type.ok());
    assert(has_diagnostic(
        rejected_unknown_record_type,
        "FSIM-ELAB-VHTYPE-001"));

    const auto missing_imported_record_type =
        fsim::frontend::parse_text(
            "missing_imported_record_type.vhd",
            R"(
package Available_Types is
  type Packet_T is record
    value : bit;
  end record;
end package;
use work.available_types.missing_t;
entity missing_imported_record_type is
  port (value : in missing_t);
end entity;
architecture rtl of missing_imported_record_type is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(missing_imported_record_type.ok());
    const auto rejected_missing_imported_record_type =
        fsim::elaboration::elaborate(
            missing_imported_record_type.design,
            "vhdl:work.missing_imported_record_type(rtl)");
    assert(!rejected_missing_imported_record_type.ok());
    assert(has_diagnostic(
        rejected_missing_imported_record_type,
        "FSIM-ELAB-PKG-003"));

    const auto missing_selected_record_type =
        fsim::frontend::parse_text(
            "missing_selected_record_type.vhd",
            R"(
package Selected_Types is
  type Packet_T is record
    value : bit;
  end record;
end package;
entity missing_selected_record_type is
end entity;
architecture rtl of missing_selected_record_type is
  signal value : selected_types.missing_t;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(missing_selected_record_type.ok());
    const auto rejected_missing_selected_record_type =
        fsim::elaboration::elaborate(
            missing_selected_record_type.design,
            "vhdl:work.missing_selected_record_type(rtl)");
    assert(!rejected_missing_selected_record_type.ok());
    assert(has_diagnostic(
        rejected_missing_selected_record_type,
        "FSIM-ELAB-VHTYPE-004"));

    const auto conflicting_record_types =
        fsim::frontend::parse_text(
            "conflicting_record_types.vhd",
            R"(
package First_Types is
  type Packet_T is record
    Value : bit;
  end record;
end package;
package Second_Types is
  type Packet_T is record
    Value : bit;
  end record;
end package;
use work.first_types.all;
use work.second_types.all;
entity conflicting_record_types is
  port (value : in packet_t);
end entity;
architecture rtl of conflicting_record_types is
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(conflicting_record_types.ok());
    const auto rejected_conflicting_record_types =
        fsim::elaboration::elaborate(
            conflicting_record_types.design,
            "vhdl:work.conflicting_record_types(rtl)");
    assert(!rejected_conflicting_record_types.ok());
    assert(has_diagnostic(
        rejected_conflicting_record_types,
        "FSIM-ELAB-VHTYPE-003"));

    auto package_record_boundary =
        package_record_source.design;
    const auto package_record_sv_parent =
        fsim::frontend::parse_text(
            "package_record_parent.sv",
            R"(
module package_record_parent;
  logic [4:0] source;
  record_child child(.source(source));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(package_record_sv_parent.ok());
    package_record_boundary.units.insert(
        package_record_boundary.units.end(),
        package_record_sv_parent.design.units.begin(),
        package_record_sv_parent.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        package_record_binding{
            {"package_record_parent.child",
             "vhdl:work.record_child(rtl)",
             std::nullopt},
        };
    const auto rejected_package_record_boundary =
        fsim::elaboration::elaborate(
            package_record_boundary,
            "sv:work.package_record_parent",
            package_record_binding);
    assert(!rejected_package_record_boundary.ok());
    assert(has_diagnostic(
        rejected_package_record_boundary,
        "FSIM-ELAB-BIND-049"));

    const auto invalid_monitor_source =
        fsim::frontend::parse_text(
            "invalid_monitor.sv",
            R"(
module invalid_monitor;
  logic q;
  initial $monitor("%b", q + 1'b1);
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_monitor_source.ok());
    const auto invalid_monitor_design =
        fsim::elaboration::elaborate(
            invalid_monitor_source.design,
            "sv:work.invalid_monitor");
    assert(!invalid_monitor_design.ok());
    assert(
        has_diagnostic(
            invalid_monitor_design, "FSIM-ELAB-103"));

    const auto array_source = fsim::frontend::parse_text(
        "vhdl_arrays.vhd",
        R"(
package Scalar_Types is
  subtype External_Logic_T is std_logic;
end package;

package Array_Types is
  constant Low_Index : integer := 0;
  type Flags_T is array (natural range <>) of boolean;
  subtype Quartet_T is Flags_T(0 to 3);
  type Logic_Bus_T is array (7 downto 0)
    of work.scalar_types.external_logic_t;
  type Bits_T is array (integer range <>) of bit;
end package;

use work.array_types.all;
entity Array_Dut is
  port (
    Source : in Quartet_T;
    Result : out Quartet_T;
    Logic_Bus : out Logic_Bus_T
  );
end entity;

use work.array_types.all;
architecture rtl of Array_Dut is
  signal Local_Bits : Bits_T(-1 to 2);
  signal Aggregate_Positional : Quartet_T;
  signal Aggregate_Named : Quartet_T;
  signal Aggregate_Equal : boolean;
  signal Attribute_Left : integer;
  signal Attribute_Right : integer;
  signal Attribute_Length : integer;
  signal Attribute_Ascending : boolean;
  signal Range_Order : integer;
  signal Reverse_Order : integer;
  signal Attribute_Aggregate : Logic_Bus_T;
  signal Attribute_Slice : std_logic_vector(3 downto 0);
  signal Dynamic_Read : std_logic;
  signal Dynamic_Result : Logic_Bus_T;
begin
  Result <= Source;
  Logic_Bus <=
    (7 downto 4 => '1', 3 | 1 => 'Z', others => '0');
  Local_Bits <= (-1 | 1 => '1', others => '0');
  Aggregate_Positional <= (true, false, true, false);
  Aggregate_Named <=
    (Low_Index | 2 => true, 1 | 3 => false);
  Aggregate_Equal <=
    Aggregate_Named =
      (0 to 0 => true, 2 => true, others => false);
  attribute_forms : process
    variable Forward_Order : integer := 0;
    variable Backward_Order : integer := 0;
    variable Attribute_Local : Logic_Bus_T :=
      (Logic_Bus_T'left downto 4 => '1',
       3 | 1 => 'Z',
       work.array_types.logic_bus_t'right => 'H',
       others => '0');
    variable Dynamic_Local : Logic_Bus_T := "01LH10Z-";
    variable Dynamic_Index : integer := 5;
  begin
    for Index in Logic_Bus_T'range loop
      if Index = 5 then
        next;
      end if;
      Forward_Order := Forward_Order * 10 + Index;
    end loop;
    for Index in Logic_Bus_T'reverse_range(1) loop
      if Index = 6 then
        exit;
      end if;
      Backward_Order := Backward_Order * 10 + Index;
    end loop;
    Attribute_Local(Logic_Bus_T'right) := 'H';
    Attribute_Left <= Logic_Bus'left;
    Attribute_Right <=
      work.array_types.logic_bus_t'right(1);
    Attribute_Length <= Logic_Bus_T'length;
    Attribute_Ascending <= Logic_Bus_T'ascending;
    Range_Order <= Forward_Order;
    Reverse_Order <= Backward_Order;
    Attribute_Aggregate <= Attribute_Local;
    Attribute_Slice <=
      Attribute_Local(
        Logic_Bus_T'high downto Logic_Bus_T'high - 3);
    Dynamic_Read <= Dynamic_Local(Dynamic_Index);
    Dynamic_Local(Dynamic_Index) := 'H';
    Dynamic_Result <= (others => '0');
    Dynamic_Result(Dynamic_Index) <= 'H';
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(array_source.ok());
    const auto array_design = fsim::elaboration::elaborate(
        array_source.design, "vhdl:work.array_dut(rtl)");
    if (!array_design.ok()) {
        for (const auto& diagnostic : array_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(array_design.ok());
    const auto array_source_signal =
        array_design.design->find_signal("source");
    const auto array_result_signal =
        array_design.design->find_signal("result");
    const auto array_logic_signal =
        array_design.design->find_signal("logic_bus");
    const auto array_local_signal =
        array_design.design->find_signal("local_bits");
    const auto array_positional_signal =
        array_design.design->find_signal(
            "aggregate_positional");
    const auto array_named_signal =
        array_design.design->find_signal("aggregate_named");
    const auto array_equal_signal =
        array_design.design->find_signal("aggregate_equal");
    const auto attribute_left_signal =
        array_design.design->find_signal("attribute_left");
    const auto attribute_right_signal =
        array_design.design->find_signal("attribute_right");
    const auto attribute_length_signal =
        array_design.design->find_signal("attribute_length");
    const auto attribute_ascending_signal =
        array_design.design->find_signal(
            "attribute_ascending");
    const auto range_order_signal =
        array_design.design->find_signal("range_order");
    const auto reverse_order_signal =
        array_design.design->find_signal("reverse_order");
    const auto attribute_aggregate_signal =
        array_design.design->find_signal(
            "attribute_aggregate");
    const auto attribute_slice_signal =
        array_design.design->find_signal(
            "attribute_slice");
    const auto dynamic_read_signal =
        array_design.design->find_signal("dynamic_read");
    const auto dynamic_result_signal =
        array_design.design->find_signal("dynamic_result");
    assert(
        array_source_signal && array_result_signal
        && array_logic_signal && array_local_signal
        && array_positional_signal && array_named_signal
        && array_equal_signal && attribute_left_signal
        && attribute_right_signal && attribute_length_signal
        && attribute_ascending_signal && range_order_signal
        && reverse_order_signal && attribute_aggregate_signal
        && attribute_slice_signal && dynamic_read_signal
        && dynamic_result_signal);
    bool found_dynamic_extract = false;
    bool found_dynamic_insert = false;
    bool found_dynamic_write = false;
    for (const auto& process :
         array_design.design->processes()) {
        for (const auto& operation : process.operations) {
            found_dynamic_extract =
                found_dynamic_extract
                || fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::DynamicExtract>(
                    operation);
            found_dynamic_insert =
                found_dynamic_insert
                || fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::DynamicInsert>(
                    operation);
            found_dynamic_write =
                found_dynamic_write
                || fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::
                        WriteUpdateDynamicSlice>(
                    operation)
                || fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::
                        WriteProjectedDynamicSlice>(
                    operation);
        }
    }
    assert(found_dynamic_extract);
    assert(found_dynamic_insert);
    assert(found_dynamic_write);
    const auto& array_source_info =
        array_design.design->signals().at(*array_source_signal);
    const auto& array_result_info =
        array_design.design->signals().at(*array_result_signal);
    const auto& array_logic_info =
        array_design.design->signals().at(*array_logic_signal);
    const auto& array_local_info =
        array_design.design->signals().at(*array_local_signal);
    assert(
        array_source_info.vhdl_array
        && !array_source_info.vhdl_array->unconstrained
        && array_source_info.width == 4
        && array_source_info.source_domain
            == fsim::frontend::ValueDomain::Boolean
        && array_source_info.nominal_type
            == array_result_info.nominal_type
        && array_source_info.packed_range
        && array_source_info.packed_range->left == 0
        && array_source_info.packed_range->right == 3
        && !array_source_info.packed_range->descending);
    assert(
        array_logic_info.vhdl_array
        && array_logic_info.width == 8
        && array_logic_info.source_domain
            == fsim::frontend::ValueDomain::Logic9
        && array_logic_info.vhdl_array->element_spelling
            == "std_logic"
        && array_logic_info.vhdl_array->element_named_type.empty()
        && array_logic_info.packed_range
        && array_logic_info.packed_range->descending);
    assert(
        array_local_info.vhdl_array
        && array_local_info.width == 4
        && array_local_info.packed_range
        && array_local_info.packed_range->left == -1
        && array_local_info.packed_range->right == 2
        && !array_local_info.packed_range->descending);
    auto array_interpreter =
        array_design.design->create_interpreter();
    array_interpreter->deposit_signal(
        *array_source_signal,
        fsim::runtime::PackedLogic4::from_msb_string("1010"));
    array_interpreter->start();
    (void)array_interpreter->run();
    assert(
        array_interpreter->signal_value(*array_result_signal)
            .to_msb_string()
            == "1010");
    assert(
        array_interpreter->signal_value(*array_logic_signal)
            .to_msb_string()
            == "1111Z0Z0");
    assert(
        array_interpreter->signal_value(*array_local_signal)
            .to_msb_string()
            == "1010");
    assert(
        array_interpreter
            ->signal_value(*array_positional_signal)
            .to_msb_string()
            == "1010");
    assert(
        array_interpreter->signal_value(*array_named_signal)
            .to_msb_string()
            == "1010");
    assert(
        array_interpreter->signal_value(*array_equal_signal)
            .to_msb_string()
            == "1");
    assert(
        array_interpreter->signal_value(*attribute_left_signal)
            .to_msb_string()
            == "00000000000000000000000000000111");
    assert(
        array_interpreter->signal_value(*attribute_right_signal)
            .to_msb_string()
            == "00000000000000000000000000000000");
    assert(
        array_interpreter->signal_value(*attribute_length_signal)
            .to_msb_string()
            == "00000000000000000000000000001000");
    assert(
        array_interpreter
            ->signal_value(*attribute_ascending_signal)
            .to_msb_string()
            == "0");
    assert(
        array_interpreter->signal_value(*range_order_signal)
            .to_msb_string()
            == "00000000011101001010000001001010");
    assert(
        array_interpreter->signal_value(*reverse_order_signal)
            .to_msb_string()
            == "00000000000000000011000000111001");
    assert(
        array_interpreter
            ->signal_value(*attribute_aggregate_signal)
            .to_msb_string()
            == "1111Z0ZH");
    assert(
        array_interpreter->signal_value(*attribute_slice_signal)
            .to_msb_string()
            == "1111");
    assert(
        array_interpreter->signal_value(*dynamic_read_signal)
            .to_msb_string()
            == "L");
    assert(
        array_interpreter->signal_value(*dynamic_result_signal)
            .to_msb_string()
            == "00H00000");

    const auto invalid_array_source =
        fsim::frontend::parse_text(
            "invalid_vhdl_arrays.vhd",
            R"(
package Invalid_Array_Types is
  type A_T is array (natural range <>) of bit;
  type B_T is array (natural range <>) of bit;
  type Fixed_T is array (3 downto 0) of bit;
  type Open_2D_T is array
    (natural range <>, natural range <>) of bit;
end package;

use work.invalid_array_types.all;
entity Invalid_Array_Child is
  port (Value : in A_T(0 to 3));
end entity;
use work.invalid_array_types.all;
architecture rtl of Invalid_Array_Child is
begin
end architecture;

use work.invalid_array_types.all;
entity Invalid_Arrays is
end entity;
use work.invalid_array_types.all;
architecture rtl of Invalid_Arrays is
  subtype Bad_Reconstraint_T is Fixed_T(1 downto 0);
  subtype Bad_Rank_T is Open_2D_T(0 to 1);
  type Nested_T is array (0 to 1) of A_T;
  type Record_T is record
    X : bit;
    Y : bit;
  end record;
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  type Records_T is array (0 to 1) of Record_T;
  signal Null_Array : A_T(3 to 0);
  signal Negative_Natural : A_T(-1 to 2);
  signal Unconstrained : A_T;
  signal A : A_T(0 to 3);
  signal B : B_T(0 to 3);
  signal Equal : boolean;
  signal Dynamic_Index : integer;
  signal Logic_Value : std_logic;
  signal Missing_Aggregate : A_T(0 to 3);
  signal Duplicate_Aggregate : A_T(0 to 3);
  signal Outside_Aggregate : A_T(0 to 3);
  signal Nonstatic_Aggregate : A_T(0 to 3);
  signal Lossy_Aggregate : A_T(0 to 3);
  signal Wide_Element_Aggregate : A_T(0 to 3);
  signal Scalar_Target : bit;
  signal Record_Target : Record_T;
  signal Matrix_Missing : Matrix_T;
  signal Matrix_Inner_Missing : Matrix_T;
  signal Bad_Record_Elements : Records_T;
  signal Matrix_Value : Matrix_T;
  signal Matrix_Bit : bit;
  signal Matrix_Pair : bit_vector(1 downto 0);
  signal Attribute_Error : integer;
begin
  B <= A;
  Equal <= A = B;
  Equal <= A < A;
  Missing_Aggregate <= (0 => '1', 1 => '0');
  Duplicate_Aggregate <= (0 | 0 => '1', others => '0');
  Outside_Aggregate <= (4 => '1', others => '0');
  Nonstatic_Aggregate <=
    (Dynamic_Index => '1', others => '0');
  Lossy_Aggregate <=
    (0 => Logic_Value, others => '0');
  Wide_Element_Aggregate <=
    (0 => "10", others => '0');
  Scalar_Target <= (0 => '0');
  Scalar_Target <= A(Logic_Value);
  A(Logic_Value) <= '1';
  Record_Target <= (0 to 1 => '0');
  Matrix_Missing <= (0 => (others => '0'));
  Matrix_Inner_Missing <=
    (others => (3 => '1', 2 => '0'));
  Bad_Record_Elements <= (others => "10");
  Matrix_Bit <= Matrix_Value(2, 2);
  Matrix_Bit <= Matrix_Value(0, 0);
  Matrix_Bit <= Matrix_Value(0, Logic_Value);
  Matrix_Pair <= Matrix_Value(0, 1 to 2);
  Matrix_Value(2, 2) <= '1';
  Matrix_Value(0, Logic_Value) <= '1';
  Matrix_Value(0, 1 to 2) <= "10";
  Attribute_Error <= A_T'left;
  Attribute_Error <= Dynamic_Index'left;
  Attribute_Error <= Fixed_T'left(2);
  Attribute_Error <= Fixed_T'left(Dynamic_Index);
  Attribute_Error <= Missing_T'left;
  Attribute_Error <= Fixed_T'range;
  Child : entity work.Invalid_Array_Child(rtl)
    port map (Value => B);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_array_source.ok());
    const auto invalid_array_design =
        fsim::elaboration::elaborate(
            invalid_array_source.design,
            "vhdl:work.invalid_arrays(rtl)");
    assert(!invalid_array_design.ok());
    assert(
        !has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-002"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-001"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-003"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-005"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-006"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-007"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAY-008"));
    assert(
        std::ranges::any_of(
            invalid_array_design.diagnostics,
            [](const auto& diagnostic) {
              return diagnostic.code
                      == "FSIM-ELAB-VHARRAYAGG-009"
                  && diagnostic.message.find("contextual subtype")
                      != std::string::npos;
            }));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAYSEL-002"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAYSEL-003"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHARRAYSEL-004"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-VHSUBTYPE-004"));
    assert(
        has_diagnostic(
            invalid_array_design, "FSIM-ELAB-BIND-056"));

    const auto composite_array_layout =
        fsim::frontend::parse_text(
            "composite_array_layout.vhd",
            R"(
entity Composite_Array_Layout is
end entity;
architecture rtl of Composite_Array_Layout is
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  type Packed_Rows_T is array (1 downto 0) of bit_vector(3 downto 0);
  type Nibble_T is array (3 downto 0) of bit;
  type Nibbles_T is array (0 to 1) of Nibble_T;
  type Cell_T is record
    Flag : boolean;
    Data : std_logic;
  end record;
  type Cells_T is array (2 to 3) of Cell_T;
  type Open_T is array
    (natural range <>, positive range <>) of bit_vector(1 downto 0);
  subtype Window_T is Open_T(2 downto 0, 1 to 2);
  signal Matrix : Matrix_T;
  signal Packed_Rows : Packed_Rows_T;
  signal Nibbles : Nibbles_T;
  signal Cells : Cells_T;
  signal Window : Window_T;
begin
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(composite_array_layout.ok());
    const auto composite_array_design =
        fsim::elaboration::elaborate(
            composite_array_layout.design,
            "vhdl:work.composite_array_layout(rtl)");
    if (!composite_array_design.ok()) {
        for (const auto& diagnostic :
             composite_array_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(composite_array_design.ok());
    const auto assert_layout =
        [&](const std::string_view name,
            const std::size_t width,
            const std::vector<std::uint64_t>& strides) {
          const auto signal =
              composite_array_design.design->find_signal(name);
          assert(signal);
          const auto& info =
              composite_array_design.design->signals().at(*signal);
          assert(info.width == width && info.vhdl_array);
          assert(info.vhdl_array->flat_width == width);
          assert(info.vhdl_array->dimensions.size() == strides.size());
          for (std::size_t index = 0; index < strides.size(); ++index) {
              assert(info.vhdl_array->dimensions[index].range);
              assert(!info.vhdl_array->dimensions[index].null);
              assert(
                  info.vhdl_array->dimensions[index].stride
                  == strides[index]);
          }
        };
    assert_layout("matrix", 6, {3, 1});
    assert_layout("packed_rows", 8, {4});
    assert_layout("nibbles", 8, {4});
    assert_layout("cells", 4, {2});
    assert_layout("window", 12, {4, 2});

    const auto composite_array_aggregates =
        fsim::frontend::parse_text(
            "composite_array_aggregates.vhd",
            R"(
entity Composite_Array_Aggregates is
end entity;
architecture rtl of Composite_Array_Aggregates is
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  type Nibble_T is array (3 downto 0) of bit;
  type Nibbles_T is array (0 to 1) of Nibble_T;
  type Cell_T is record
    Flag : boolean;
    Data : bit_vector(1 downto 0);
  end record;
  type Cells_T is array (2 to 3) of Cell_T;
  signal Positional : Matrix_T;
  signal Named : Matrix_T;
  signal Ranged : Matrix_T;
  signal Nested : Nibbles_T;
  signal Cells : Cells_T;
begin
  Positional <= (('1', '0', '1'), ('0', '1', '0'));
  Named <=
    (0 => (3 => '1', 2 => '0', others => '1'),
     1 => (others => '0'));
  Ranged <=
    (0 to 0 => (others => '1'),
     others => ('0', '0', '1'));
  Nested <=
    ((3 => '1', others => '0'),
     ('0', '1', '0', '1'));
  Cells <=
    (2 => (Flag => true, Data => "10"),
     others => (Data => "01", Flag => false));
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(composite_array_aggregates.ok());
    const auto aggregate_design =
        fsim::elaboration::elaborate(
            composite_array_aggregates.design,
            "vhdl:work.composite_array_aggregates(rtl)");
    if (!aggregate_design.ok()) {
        for (const auto& diagnostic : aggregate_design.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(aggregate_design.ok());
    const auto aggregate_positional =
        aggregate_design.design->find_signal("positional");
    const auto aggregate_named =
        aggregate_design.design->find_signal("named");
    const auto aggregate_ranged =
        aggregate_design.design->find_signal("ranged");
    const auto aggregate_cells =
        aggregate_design.design->find_signal("cells");
    const auto aggregate_nested =
        aggregate_design.design->find_signal("nested");
    assert(
        aggregate_positional && aggregate_named
        && aggregate_ranged && aggregate_cells
        && aggregate_nested);
    auto composite_aggregate_interpreter =
        aggregate_design.design->create_interpreter();
    const auto composite_aggregate_result =
        composite_aggregate_interpreter->run();
    assert(
        composite_aggregate_result.status
        == fsim::runtime::RunStatus::completed);
    assert(
        composite_aggregate_interpreter
                ->signal_value(*aggregate_positional)
                .to_msb_string()
            == "101010");
    assert(
        composite_aggregate_interpreter
                ->signal_value(*aggregate_named)
                .to_msb_string()
            == "101000");
    assert(
        composite_aggregate_interpreter
                ->signal_value(*aggregate_ranged)
                .to_msb_string()
            == "111001");
    assert(
        composite_aggregate_interpreter
                ->signal_value(*aggregate_cells)
                .to_msb_string()
            == "110001");
    assert(
        composite_aggregate_interpreter
                ->signal_value(*aggregate_nested)
                .to_msb_string()
            == "10000101");
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYAGG-003"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-DYNINDEX-002"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYAGG-004"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYAGG-005"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYAGG-006"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYAGG-007"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHAGG-001"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHAGG-008"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYATTR-001"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYATTR-002"));
    assert(
        has_diagnostic(
            invalid_array_design,
            "FSIM-ELAB-VHARRAYATTR-003"));

    auto malformed_array_aggregate =
        fsim::frontend::parse_text(
            "malformed_array_aggregate_metadata.vhd",
            R"(
entity Malformed_Array_Aggregate is
end entity;
architecture rtl of Malformed_Array_Aggregate is
  type Bits_T is array (0 to 3) of bit;
  signal Value : Bits_T;
begin
  Value <= (others => '0');
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(malformed_array_aggregate.ok());
    auto combined_others_aggregate =
        malformed_array_aggregate;
    auto& malformed_array_expression =
        malformed_array_aggregate.design.units.back()
            .concurrent_statements.front().value;
    malformed_array_expression
        .aggregate_choice_expressions.pop_back();
    const auto malformed_array_design =
        fsim::elaboration::elaborate(
            malformed_array_aggregate.design,
            "vhdl:work.malformed_array_aggregate(rtl)");
    assert(
        !malformed_array_design.ok()
        && has_diagnostic(
            malformed_array_design,
            "FSIM-ELAB-VHARRAYAGG-002"));
    auto& combined_others_expression =
        combined_others_aggregate.design.units.back()
            .concurrent_statements.front().value;
    const auto combined_choice_span =
        combined_others_expression.span;
    combined_others_expression
        .aggregate_choice_expressions.front().push_back(
            fsim::frontend::Expression{
                fsim::frontend::ExpressionKind::IntegerLiteral,
                "0",
                {},
                combined_choice_span});
    const auto combined_others_design =
        fsim::elaboration::elaborate(
            combined_others_aggregate.design,
            "vhdl:work.malformed_array_aggregate(rtl)");
    assert(
        !combined_others_design.ok()
        && has_diagnostic(
            combined_others_design,
            "FSIM-ELAB-VHARRAYAGG-008"));

    const auto foreign_array_parent =
        fsim::frontend::parse_text(
            "foreign_array_parent.sv",
            R"(
module foreign_array_parent;
  logic [3:0] value;
  invalid_array_child child(.value(value));
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(foreign_array_parent.ok());
    auto mixed_array_design =
        invalid_array_source.design;
    mixed_array_design.units.insert(
        mixed_array_design.units.end(),
        foreign_array_parent.design.units.begin(),
        foreign_array_parent.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        foreign_array_binding{
            {
                "foreign_array_parent.child",
                "vhdl:work.invalid_array_child(rtl)",
                std::nullopt}};
    const auto rejected_foreign_array =
        fsim::elaboration::elaborate(
            mixed_array_design,
            "sv:work.foreign_array_parent",
            foreign_array_binding);
    assert(
        !rejected_foreign_array.ok()
        && has_diagnostic(
            rejected_foreign_array,
            "FSIM-ELAB-BIND-055"));

    const auto random_source = fsim::frontend::parse_text(
        "random.sv",
        R"(
module random_test;
  logic [31:0] a, b, c, d, e, f;
  initial begin
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(3, 9);
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(random_source.ok());
    const auto random_design = fsim::elaboration::elaborate(
        random_source.design, "sv:work.random_test");
    assert(random_design.ok());
    std::vector<fsim::runtime::simir::RandomValue> random_operations;
    for (const auto& operation :
         random_design.design->processes().front().operations) {
        if (const auto* random =
                fsim::runtime::simir::operation_get_if<fsim::runtime::simir::RandomValue>(
                    &operation)) {
            random_operations.push_back(*random);
        }
    }
    assert(
        random_operations.size() == 6
        && random_operations[0].kind
            == fsim::runtime::simir::RandomKind::urandom
        && random_operations[1].kind
            == fsim::runtime::simir::RandomKind::urandom
        && random_operations[2].kind
            == fsim::runtime::simir::RandomKind::random
        && random_operations[3].kind
            == fsim::runtime::simir::RandomKind::random
        && random_operations[4].kind
            == fsim::runtime::simir::RandomKind::urandom_range
        && random_operations[4].maximum
        && !random_operations[4].minimum
        && random_operations[5].maximum
        && random_operations[5].minimum);

    const auto invalid_random_source =
        fsim::frontend::parse_text(
            "invalid_random.sv",
            R"(
module invalid_random;
  logic [31:0] q;
  initial begin
    q = $urandom(1);
    q = $urandom_range();
    q = $urandom_range(1, 2, 3);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_random_source.ok());
    const auto invalid_random_design =
        fsim::elaboration::elaborate(
            invalid_random_source.design,
            "sv:work.invalid_random");
    assert(!invalid_random_design.ok());
    assert(
        has_diagnostic(
            invalid_random_design, "FSIM-ELAB-104"));

    const auto verilog_random_source =
        fsim::frontend::parse_text(
            "verilog_random.v",
            R"(
module verilog_random;
  reg [31:0] q;
  initial q = $random;
endmodule
)",
            fsim::frontend::Language::Verilog2005);
    assert(verilog_random_source.ok());
    const auto verilog_random_design =
        fsim::elaboration::elaborate(
            verilog_random_source.design,
            "verilog:work.verilog_random");
    assert(verilog_random_design.ok());
    assert(std::ranges::any_of(
        verilog_random_design.design->processes().front().operations,
        [](const auto& operation) {
          const auto* random =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::RandomValue>(
                  &operation);
          return random
              && random->kind
                  == fsim::runtime::simir::RandomKind::random;
        }));

    const auto invalid_verilog_random_source =
        fsim::frontend::parse_text(
            "invalid_verilog_random.v",
            R"(
module invalid_verilog_random;
  reg [31:0] q;
  initial q = $urandom;
endmodule
)",
            fsim::frontend::Language::Verilog2005);
    assert(invalid_verilog_random_source.ok());
    const auto invalid_verilog_random_design =
        fsim::elaboration::elaborate(
            invalid_verilog_random_source.design,
            "verilog:work.invalid_verilog_random");
    assert(!invalid_verilog_random_design.ok());
    assert(
        has_diagnostic(
            invalid_verilog_random_design, "FSIM-ELAB-104"));
}

} // namespace fsim::tests::elaboration
