// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_selection_and_assignment_lowering() {
const auto select_concat_process =
        fsim::frontend::parse_text(
            "select_concat_process.sv",
            R"(
module select_concat_process;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected_descending;
  logic selected_ascending;
  logic selected_local;
  logic [3:0] descending_part;
  logic [3:0] ascending_part;
  logic [8:0] joined;
  always_comb begin
    logic [5:2] local_copy;
    local_copy = descending[15:12];
    selected_descending = descending[10];
    selected_ascending = ascending[2];
    selected_local = local_copy[3];
    descending_part = descending[15:12];
    ascending_part = ascending[2:5];
    joined = {
      descending[15:12], descending[10], ascending[4:7]
    };
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(select_concat_process.ok());
    const auto elaborated_select_concat =
        fsim::elaboration::elaborate(
            select_concat_process.design,
            "sv:work.select_concat_process");
    assert(elaborated_select_concat.ok());
    const auto descending =
        elaborated_select_concat.design->find_signal("descending");
    const auto ascending =
        elaborated_select_concat.design->find_signal("ascending");
    const std::array select_concat_outputs{
        elaborated_select_concat.design->find_signal(
            "selected_descending"),
        elaborated_select_concat.design->find_signal(
            "selected_ascending"),
        elaborated_select_concat.design->find_signal(
            "selected_local"),
        elaborated_select_concat.design->find_signal(
            "descending_part"),
        elaborated_select_concat.design->find_signal(
            "ascending_part"),
        elaborated_select_concat.design->find_signal("joined")};
    assert(descending && ascending);
    assert(std::ranges::all_of(
        select_concat_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto select_concat_interpreter =
        elaborated_select_concat.design->create_interpreter();
    select_concat_interpreter->deposit_signal(
        *descending,
        fsim::runtime::PackedLogic4::from_msb_string("10XZ0110"));
    select_concat_interpreter->deposit_signal(
        *ascending,
        fsim::runtime::PackedLogic4::from_msb_string("01ZX1100"));
    (void)select_concat_interpreter->run();
    const std::array<std::string_view, 6> expected_select_concat{
        "1", "Z", "X", "10XZ", "ZX11", "10XZ11100"};
    for (std::size_t index = 0;
         index < select_concat_outputs.size(); ++index) {
      assert(
          select_concat_interpreter
              ->signal_value(*select_concat_outputs[index])
              .to_msb_string()
          == expected_select_concat[index]);
    }

    const auto selected_assignment =
        fsim::frontend::parse_text(
            "selected_assignment.sv",
            R"(
module selected_assignment;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic [5:2] local_result;
  initial begin
    logic [5:2] local_copy;
    descending[8] <= 1'b1;
    descending = 8'b00000000;
    descending[9] = 1'b1;
    descending[15:12] = 4'b10xz;
    ascending <= 8'b10101010;
    ascending[4:5] <= 2'bxz;
    local_copy = 4'b0000;
    local_copy[3] = 1'b1;
    local_copy[5:4] = 2'bxz;
    local_result = local_copy;
    descending[11:10] <= #5 2'b11;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(selected_assignment.ok());
    const auto elaborated_selected_assignment =
        fsim::elaboration::elaborate(
            selected_assignment.design,
            "sv:work.selected_assignment");
    if (!elaborated_selected_assignment.ok()) {
      for (const auto& diagnostic :
           elaborated_selected_assignment.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_selected_assignment.ok());
    const auto selected_descending =
        elaborated_selected_assignment.design->find_signal(
            "descending");
    const auto selected_ascending =
        elaborated_selected_assignment.design->find_signal(
            "ascending");
    const auto selected_local =
        elaborated_selected_assignment.design->find_signal(
            "local_result");
    assert(
        selected_descending && selected_ascending
        && selected_local);
    auto selected_assignment_interpreter =
        elaborated_selected_assignment.design->create_interpreter();
    const auto selected_assignment_result =
        selected_assignment_interpreter->run();
    assert(
        selected_assignment_result.status
            == fsim::runtime::RunStatus::completed
        && selected_assignment_result.time == 5);
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_descending)
            .to_msb_string()
        == "10XZ1111");
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_ascending)
            .to_msb_string()
        == "1010XZ10");
    assert(
        selected_assignment_interpreter
            ->signal_value(*selected_local)
            .to_msb_string()
        == "XZ10");

    const auto reversed_assignment_select =
        fsim::frontend::parse_text(
            "reversed_assignment_select.sv",
            R"(
module reversed_assignment_select;
  logic [0:7] value;
  initial value[5:2] = 4'b1010;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(reversed_assignment_select.ok());
    const auto rejected_reversed_assignment_select =
        fsim::elaboration::elaborate(
            reversed_assignment_select.design,
            "sv:work.reversed_assignment_select");
    assert(!rejected_reversed_assignment_select.ok());
    assert(has_diagnostic(
        rejected_reversed_assignment_select, "FSIM-ELAB-068"));

    const auto reversed_select = fsim::frontend::parse_text(
        "reversed_select.sv",
        R"(
module reversed_select;
  logic [0:7] value;
  logic [3:0] result;
  always_comb result = value[5:2];
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(reversed_select.ok());
    const auto rejected_reversed_select =
        fsim::elaboration::elaborate(
            reversed_select.design, "sv:work.reversed_select");
    assert(!rejected_reversed_select.ok());
    assert(has_diagnostic(
        rejected_reversed_select, "FSIM-ELAB-068"));

    const auto dynamic_select = fsim::frontend::parse_text(
        "dynamic_select.sv",
        R"(
module dynamic_select;
  logic [7:0] value;
  logic signed [31:0] index;
  logic result;
  initial begin
    value = 8'b00101000;
    index = 32'd5;
    result = value[index];
    value[index] = 1'b0;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_select.ok());
    const auto elaborated_dynamic_select =
        fsim::elaboration::elaborate(
            dynamic_select.design, "sv:work.dynamic_select");
    assert(elaborated_dynamic_select.ok());
    const auto dynamic_index_value =
        elaborated_dynamic_select.design->find_signal("value");
    const auto dynamic_result =
        elaborated_dynamic_select.design->find_signal("result");
    assert(dynamic_index_value && dynamic_result);
    auto dynamic_select_interpreter =
        elaborated_dynamic_select.design->create_interpreter();
    const auto dynamic_select_result =
        dynamic_select_interpreter->run();
    assert(
        dynamic_select_result.status
            == fsim::runtime::RunStatus::completed);
    assert(
        dynamic_select_interpreter
            ->signal_value(*dynamic_index_value)
            .to_msb_string()
        == "00001000");
    assert(
        dynamic_select_interpreter
            ->signal_value(*dynamic_result)
            .to_msb_string()
        == "1");

    const auto dynamic_part_select = fsim::frontend::parse_text(
        "dynamic_part_select.sv",
        R"(
module dynamic_part_select;
  logic [15:0] down;
  logic [0:15] up;
  bit [15:0] bits;
  logic signed [31:0] base;
  logic [3:0] down_plus;
  logic [3:0] down_minus;
  logic [3:0] partial;
  logic [3:0] unknown;
  logic [3:0] up_plus;
  logic [3:0] up_minus;
  logic [3:0] bit_partial;
  logic [136:0] wide_source;
  logic [95:0] wide_result;
  initial begin
    down = 16'habcd;
    up = 16'habcd;
    bits = 16'habcd;
    base = 4;
    down_plus = down[base +: 4];
    base = 7;
    down_minus = down[base -: 4];
    base = 14;
    partial = down[base +: 4];
    base = 32'bx;
    unknown = down[base +: 4];
    base = 4;
    up_plus = up[base +: 4];
    base = 7;
    up_minus = up[base -: 4];
    base = 14;
    bit_partial = bits[base +: 4];
    wide_source = '1;
    base = 17;
    wide_result = wide_source[base +: 96];
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_part_select.ok());
    const auto elaborated_dynamic_part_select =
        fsim::elaboration::elaborate(
            dynamic_part_select.design,
            "sv:work.dynamic_part_select");
    assert(elaborated_dynamic_part_select.ok());
    const auto& part_process =
        elaborated_dynamic_part_select.design->processes().front();
    assert(
        std::count_if(
            part_process.operations.begin(),
            part_process.operations.end(),
            [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::DynamicPartSelect>(operation);
            })
        == 8);
    auto part_interpreter =
        elaborated_dynamic_part_select.design->create_interpreter();
    assert(
        part_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
        std::array {
            std::pair { "down_plus", "1100" },
            std::pair { "down_minus", "1100" },
            std::pair { "partial", "XX10" },
            std::pair { "unknown", "XXXX" },
            std::pair { "up_plus", "1011" },
            std::pair { "up_minus", "1011" },
            std::pair { "bit_partial", "0010" },
            std::pair {
                "wide_result",
                "11111111111111111111111111111111"
                "11111111111111111111111111111111"
                "11111111111111111111111111111111" } }) {
        const auto signal = elaborated_dynamic_part_select.design->find_signal(name);
        assert(signal);
        assert(
            part_interpreter->signal_value(*signal).to_msb_string()
            == expected);
    }

    const auto invalid_dynamic_parts = fsim::frontend::parse_text(
        "invalid_dynamic_parts.sv",
        R"(
module dynamic_width;
  logic [15:0] source;
  logic signed [31:0] base;
  logic signed [31:0] width;
  logic [3:0] result;
  initial result = source[base +: width];
endmodule
module dynamic_target;
  logic [15:0] target;
  logic signed [31:0] base;
  initial begin
    target = 16'h1234;
    base = 14;
    target[base +: 4] = 4'ha;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_dynamic_parts.ok());
    const auto rejected_dynamic_width =
        fsim::elaboration::elaborate(
            invalid_dynamic_parts.design,
            "sv:work.dynamic_width");
    assert(!rejected_dynamic_width.ok());
    assert(has_diagnostic(
        rejected_dynamic_width, "FSIM-ELAB-SVEXPR-004"));
    const auto elaborated_dynamic_target =
        fsim::elaboration::elaborate(
            invalid_dynamic_parts.design,
            "sv:work.dynamic_target");
    assert(elaborated_dynamic_target.ok());
    const auto dynamic_target_signal =
        elaborated_dynamic_target.design->find_signal("target");
    assert(dynamic_target_signal);
    auto dynamic_target_interpreter =
        elaborated_dynamic_target.design->create_interpreter();
    assert(
        dynamic_target_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        dynamic_target_interpreter
            ->signal_value(*dynamic_target_signal)
            .to_msb_string()
        == "1001001000110100");

    const auto invalid_streams = fsim::frontend::parse_text(
        "invalid_streams.sv",
        R"(
module dynamic_stream;
  logic [7:0] source;
  logic signed [31:0] slice_size;
  logic [7:0] result;
  initial result = {<<slice_size{source}};
endmodule
module wide_stream;
  logic [63:0] lhs;
  logic [63:0] rhs;
  logic [127:0] result;
  initial begin
    lhs = 64'h0123_4567_89ab_cdef;
    rhs = 64'hfedc_ba98_7654_3210;
    result = {>>{lhs, rhs}};
  end
endmodule
module container_stream;
  logic [7:0] values[$];
  logic [7:0] result;
  initial result = {>>{values}};
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_streams.ok());
    const auto rejected_dynamic_stream =
        fsim::elaboration::elaborate(
            invalid_streams.design,
            "sv:work.dynamic_stream");
    assert(!rejected_dynamic_stream.ok());
    assert(has_diagnostic(
        rejected_dynamic_stream, "FSIM-ELAB-SVEXPR-002"));
    const auto elaborated_wide_stream = fsim::elaboration::elaborate(
        invalid_streams.design,
        "sv:work.wide_stream");
    assert(elaborated_wide_stream.ok());
    const auto wide_stream_result = elaborated_wide_stream.design->find_signal("result");
    assert(wide_stream_result);
    auto wide_stream_interpreter = elaborated_wide_stream.design->create_interpreter();
    assert(
        wide_stream_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        wide_stream_interpreter
            ->signal_value(*wide_stream_result)
            .to_msb_string()
        == "0000000100100011010001010110011110001001101010111100110111101111"
           "1111111011011100101110101001100001110110010101000011001000010000");
    const auto rejected_container_stream =
        fsim::elaboration::elaborate(
            invalid_streams.design,
            "sv:work.container_stream");
    assert(!rejected_container_stream.ok());
    assert(has_diagnostic(
        rejected_container_stream, "FSIM-ELAB-SVEXPR-003"));

    const auto narrow_dynamic_select =
        fsim::frontend::parse_text(
            "narrow_dynamic_select.sv",
            R"(
module narrow_dynamic_select;
  logic [7:0] value;
  logic [2:0] index;
  logic result;
  always_comb result = value[index];
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(narrow_dynamic_select.ok());
    const auto elaborated_narrow_dynamic_select =
        fsim::elaboration::elaborate(
            narrow_dynamic_select.design,
            "sv:work.narrow_dynamic_select");
    assert(elaborated_narrow_dynamic_select.ok());

    const auto empty_concatenation =
        fsim::frontend::parse_text(
            "empty_concatenation.sv",
            R"(
module empty_concatenation;
  logic result;
  always_comb result = {};
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(empty_concatenation.ok());
    const auto rejected_empty_concatenation =
        fsim::elaboration::elaborate(
            empty_concatenation.design,
            "sv:work.empty_concatenation");
    assert(!rejected_empty_concatenation.ok());
    assert(has_diagnostic(
        rejected_empty_concatenation, "FSIM-ELAB-069"));

    const auto vhdl_select_concat =
        fsim::frontend::parse_text(
            "vhdl_select_concat.vhd",
            R"(
entity vhdl_select_concat is
  port (
    descending : in std_logic_vector(7 downto 4);
    ascending : in std_logic_vector(2 to 5);
    selected_descending : out std_logic;
    selected_ascending : out std_logic;
    selected_local : out std_logic;
    descending_part : out std_logic_vector(1 downto 0);
    ascending_part : out std_logic_vector(1 downto 0);
    joined : out std_logic_vector(5 downto 0)
  );
end entity;

architecture rtl of vhdl_select_concat is
begin
  observe: process(descending, ascending)
    variable local_copy : std_logic_vector(9 downto 8);
  begin
    local_copy := descending(7 downto 6);
    selected_descending <= descending(5);
    selected_ascending <= ascending(4);
    selected_local <= local_copy(8);
    descending_part <= descending(7 downto 6);
    ascending_part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_select_concat.ok());
    const auto elaborated_vhdl_select_concat =
        fsim::elaboration::elaborate(
            vhdl_select_concat.design,
            "vhdl:work.vhdl_select_concat(rtl)");
    assert(elaborated_vhdl_select_concat.ok());
    const auto vhdl_descending =
        elaborated_vhdl_select_concat.design->find_signal(
            "descending");
    const auto vhdl_ascending =
        elaborated_vhdl_select_concat.design->find_signal(
            "ascending");
    const std::array vhdl_select_concat_outputs{
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_descending"),
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_ascending"),
        elaborated_vhdl_select_concat.design->find_signal(
            "selected_local"),
        elaborated_vhdl_select_concat.design->find_signal(
            "descending_part"),
        elaborated_vhdl_select_concat.design->find_signal(
            "ascending_part"),
        elaborated_vhdl_select_concat.design->find_signal("joined")};
    assert(vhdl_descending && vhdl_ascending);
    assert(std::ranges::all_of(
        vhdl_select_concat_outputs,
        [](const auto& signal) {
          return signal.has_value();
        }));
    auto vhdl_select_concat_interpreter =
        elaborated_vhdl_select_concat.design->create_interpreter();
    vhdl_select_concat_interpreter->deposit_signal(
        *vhdl_descending,
        fsim::runtime::PackedLogic4::from_msb_string("1XZ0"));
    vhdl_select_concat_interpreter->deposit_signal(
        *vhdl_ascending,
        fsim::runtime::PackedLogic4::from_msb_string("01Z1"));
    (void)vhdl_select_concat_interpreter->run();
    const std::array<std::string_view, 6>
        expected_vhdl_select_concat{
            "Z", "Z", "X", "1X", "1Z", "1X10Z1"};
    for (std::size_t index = 0;
         index < vhdl_select_concat_outputs.size(); ++index) {
      assert(
          vhdl_select_concat_interpreter
              ->signal_value(*vhdl_select_concat_outputs[index])
              .to_msb_string()
          == expected_vhdl_select_concat[index]);
    }

    const auto vhdl_selected_assignment =
        fsim::frontend::parse_text(
            "vhdl_selected_assignment.vhd",
            R"(
entity vhdl_selected_assignment is
end entity;

architecture rtl of vhdl_selected_assignment is
  signal trigger : std_logic;
  signal descending : std_logic_vector(15 downto 8);
  signal ascending : std_logic_vector(0 to 7);
  signal local_result : std_logic_vector(5 downto 2);
begin
  update: process(trigger)
    variable local_copy : std_logic_vector(5 downto 2);
  begin
    descending <= "00000000";
    descending(9) <= '1';
    descending(15 downto 12) <= "10XZ";
    ascending <= "10101010";
    ascending(4 to 5) <= "XZ";
    local_copy := "0000";
    local_copy(3) := '1';
    local_copy(5 downto 4) := "XZ";
    local_result <= local_copy;
    descending(11 downto 10) <= "11" after 5 ns;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_selected_assignment.ok());
    const auto elaborated_vhdl_selected_assignment =
        fsim::elaboration::elaborate(
            vhdl_selected_assignment.design,
            "vhdl:work.vhdl_selected_assignment(rtl)");
    if (!elaborated_vhdl_selected_assignment.ok()) {
      for (const auto& diagnostic :
           elaborated_vhdl_selected_assignment.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(elaborated_vhdl_selected_assignment.ok());
    const auto vhdl_assigned_descending =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "descending");
    const auto vhdl_assigned_ascending =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "ascending");
    const auto vhdl_assigned_local =
        elaborated_vhdl_selected_assignment.design->find_signal(
            "local_result");
    assert(
        vhdl_assigned_descending && vhdl_assigned_ascending
        && vhdl_assigned_local);
    auto vhdl_selected_assignment_interpreter =
        elaborated_vhdl_selected_assignment.design
            ->create_interpreter();
    const auto vhdl_selected_assignment_result =
        vhdl_selected_assignment_interpreter->run();
    assert(
        vhdl_selected_assignment_result.status
            == fsim::runtime::RunStatus::completed
        && vhdl_selected_assignment_result.time == 5);
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_descending)
            .to_msb_string()
        == "10XZ1110");
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_ascending)
            .to_msb_string()
        == "1010XZ10");
    assert(
        vhdl_selected_assignment_interpreter
            ->signal_value(*vhdl_assigned_local)
            .to_msb_string()
        == "XZ10");

    const auto reversed_vhdl_select =
        fsim::frontend::parse_text(
            "reversed_vhdl_select.vhd",
            R"(
entity reversed_vhdl_select is
  port (
    value : in std_logic_vector(2 to 5);
    result : out std_logic_vector(1 downto 0)
  );
end entity;
architecture rtl of reversed_vhdl_select is
begin
  result <= value(4 downto 3);
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(reversed_vhdl_select.ok());
    const auto rejected_reversed_vhdl_select =
        fsim::elaboration::elaborate(
            reversed_vhdl_select.design,
            "vhdl:work.reversed_vhdl_select(rtl)");
    assert(!rejected_reversed_vhdl_select.ok());
    assert(has_diagnostic(
        rejected_reversed_vhdl_select, "FSIM-ELAB-068"));

    const auto empty_wildcard = fsim::frontend::parse_text(
        "empty_wildcard.sv",
        R"(
module empty_wildcard;
  logic q;
  always @* q = 1'b0;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(empty_wildcard.ok());
    const auto rejected_empty_wildcard =
        fsim::elaboration::elaborate(
            empty_wildcard.design, "sv:work.empty_wildcard");
    assert(!rejected_empty_wildcard.ok());
    assert(has_diagnostic(
        rejected_empty_wildcard, "FSIM-ELAB-061"));

    const auto callable_wildcard = fsim::frontend::parse_text(
        "callable_wildcard.sv",
        R"(
module callable_wildcard;
  logic source_a;
  logic source_b;
  bit source_c;
  logic function_result;
  logic task_result;
  logic [2:0] loop_result;
  function automatic logic read_a_leaf;
    return source_a;
  endfunction
  function automatic logic read_a;
    logic nested = read_a_leaf();
    return nested;
  endfunction
  task automatic read_b_leaf;
    task_result = source_b;
  endtask
  task automatic read_b;
    read_b_leaf();
  endtask
  always_comb function_result = read_a();
  always_comb read_b();
  always_comb begin
    loop_result = 3'd0;
    for (int lane = source_c; lane < 3; lane += 2)
      loop_result = loop_result + 1'b1;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(callable_wildcard.ok());
    const auto elaborated_callable_wildcard =
        fsim::elaboration::elaborate(
            callable_wildcard.design,
            "sv:work.callable_wildcard");
    assert(elaborated_callable_wildcard.ok());
    const auto callable_source_a =
        elaborated_callable_wildcard.design->find_signal("source_a");
    const auto callable_source_b =
        elaborated_callable_wildcard.design->find_signal("source_b");
    const auto callable_source_c =
        elaborated_callable_wildcard.design->find_signal("source_c");
    assert(callable_source_a && callable_source_b && callable_source_c);
    const auto& callable_processes =
        elaborated_callable_wildcard.design->processes();
    assert(callable_processes.size() == 3);
    assert(
        callable_processes[0].static_sensitivity.size() == 1
        && callable_processes[0].static_sensitivity[0].signal
            == *callable_source_a);
    assert(
        callable_processes[1].static_sensitivity.size() == 1
        && callable_processes[1].static_sensitivity[0].signal
            == *callable_source_b);
    assert(
        std::ranges::any_of(
            callable_processes[2].static_sensitivity,
            [&](const auto& item) {
              return item.signal == *callable_source_c;
            }));

    const auto packed_event_expression = fsim::frontend::parse_text(
        "packed_event_expression.sv",
        R"(
module packed_event_expression;
  logic left;
  logic right;
  bit [1:0] static_observed;
  bit [1:0] dynamic_observed;
  bit [1:0] repeated_observed;
  initial begin
    left = 1'b0;
    right = 1'b0;
    #1 left = 1'b1;
    #1 right = 1'b1;
    #1 left = 1'b0;
    #1 right = 1'b0;
    #1 $finish;
  end
  always @(left | right)
    static_observed = static_observed + 1'b1;
  initial begin
    @(left | right);
    dynamic_observed = dynamic_observed + 1'b1;
    @(left | right);
    dynamic_observed = dynamic_observed + 1'b1;
  end
  initial
    repeated_observed <= repeat (2'b10) @(left | right) 2'b11;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(packed_event_expression.ok());
    const auto elaborated_packed_event_expression =
        fsim::elaboration::elaborate(
            packed_event_expression.design,
            "sv:work.packed_event_expression");
    if (!elaborated_packed_event_expression.ok()) {
        for (const auto& diagnostic :
             elaborated_packed_event_expression.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << std::endl;
        }
    }
    assert(elaborated_packed_event_expression.ok());
    assert(std::ranges::any_of(
        elaborated_packed_event_expression.design->processes(),
        [](const auto& process) {
          return std::ranges::any_of(
              process.operations,
              [](const auto& operation) {
                const auto* binary = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::Binary>(&operation);
                return binary != nullptr
                    && binary->operation
                        == fsim::runtime::simir::BinaryOperator::less_unsigned;
              });
        }));
    auto packed_event_interpreter =
        elaborated_packed_event_expression.design
            ->create_interpreter();
    const auto packed_event_result =
        packed_event_interpreter->run();
    assert(
        packed_event_result.status
        == fsim::runtime::RunStatus::stopped);
    assert(packed_event_result.time == 5);
    for (const auto name : {
             std::string_view{"static_observed"},
             std::string_view{"dynamic_observed"}}) {
        const auto signal =
            elaborated_packed_event_expression.design
                ->find_signal(name);
        assert(signal);
        assert(
            packed_event_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == "10");
    }
    const auto repeated_observed =
        elaborated_packed_event_expression.design
            ->find_signal("repeated_observed");
    assert(repeated_observed);
    assert(
        packed_event_interpreter
            ->signal_value(*repeated_observed)
            .to_msb_string()
        == "11");

    auto malformed_event_expression = fsim::frontend::parse_text(
        "malformed_event_expression.sv",
        R"(
module malformed_event_expression;
  logic left;
  logic right;
  logic observed;
  always @(left | right) observed = left;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(malformed_event_expression.ok());
    malformed_event_expression.design.units.front()
        .processes.front().sensitivities.push_back(
            fsim::frontend::Sensitivity{
                fsim::frontend::EdgeKind::Any,
                "left",
                {},
                {}});
    const auto rejected_malformed_event_expression =
        fsim::elaboration::elaborate(
            malformed_event_expression.design,
            "sv:work.malformed_event_expression");
    assert(!rejected_malformed_event_expression.ok());
    assert(has_diagnostic(
        rejected_malformed_event_expression,
        "FSIM-ELAB-SVEVENT-002"));

    auto malformed_repeated_event = fsim::frontend::parse_text(
        "malformed_repeated_event.sv",
        R"(
module malformed_repeated_event;
  logic clock;
  logic source;
  logic observed;
  initial observed <= @(clock) source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(malformed_repeated_event.ok());
    malformed_repeated_event.design.units.front().processes.front()
        .statements.front().procedural_assignment_repeat = true;
    const auto rejected_malformed_repeated_event =
        fsim::elaboration::elaborate(
            malformed_repeated_event.design,
            "sv:work.malformed_repeated_event");
    assert(!rejected_malformed_repeated_event.ok());
    assert(has_diagnostic(
        rejected_malformed_repeated_event,
        "FSIM-ELAB-105"));

    const auto invalid_event_alias_source = fsim::frontend::parse_text(
        "invalid_event_alias_source.sv",
        R"(
module invalid_event_alias_source;
  event target;
  logic source;
  initial target = source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_event_alias_source.ok());
    const auto rejected_event_alias_source = fsim::elaboration::elaborate(
        invalid_event_alias_source.design,
        "sv:work.invalid_event_alias_source");
    assert(!rejected_event_alias_source.ok());
    assert(has_diagnostic(
        rejected_event_alias_source,
        "FSIM-ELAB-SVEVENT-009"));

    const auto invalid_event_alias_timing = fsim::frontend::parse_text(
        "invalid_event_alias_timing.sv",
        R"(
module invalid_event_alias_timing;
  event target;
  event source;
  initial target <= source;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_event_alias_timing.ok());
    const auto rejected_event_alias_timing = fsim::elaboration::elaborate(
        invalid_event_alias_timing.design,
        "sv:work.invalid_event_alias_timing");
    assert(!rejected_event_alias_timing.ok());
    assert(has_diagnostic(
        rejected_event_alias_timing,
        "FSIM-ELAB-SVEVENT-010"));

    const auto dynamic_wildcard = fsim::frontend::parse_text(
        "dynamic_wildcard.sv",
        R"(
module dynamic_wildcard;
  logic trigger;
  logic observed;
  initial @* observed = trigger;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 $finish;
  end
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_wildcard.ok());
    const auto elaborated_dynamic_wildcard =
        fsim::elaboration::elaborate(
            dynamic_wildcard.design, "sv:work.dynamic_wildcard");
    assert(elaborated_dynamic_wildcard.ok());
    const auto& dynamic_wait_process =
        elaborated_dynamic_wildcard.design->processes().front();
    const auto dynamic_wait = std::find_if(
        dynamic_wait_process.operations.begin(),
        dynamic_wait_process.operations.end(),
        [](const fsim::runtime::simir::Operation& operation) {
          return fsim::runtime::simir::operation_holds<
              fsim::runtime::simir::WaitOn>(operation);
        });
    assert(dynamic_wait != dynamic_wait_process.operations.end());
    assert(
        fsim::runtime::simir::operation_get<fsim::runtime::simir::WaitOn>(*dynamic_wait)
            .signals.size()
        == 1);
    auto dynamic_wildcard_interpreter =
        elaborated_dynamic_wildcard.design->create_interpreter();
    const auto dynamic_observed =
        elaborated_dynamic_wildcard.design->find_signal("observed");
    assert(dynamic_observed);
    const auto dynamic_wildcard_run =
        dynamic_wildcard_interpreter->run();
    assert(
        dynamic_wildcard_run.status
        == fsim::runtime::RunStatus::stopped);
    assert(
        dynamic_wildcard_interpreter
            ->signal_value(*dynamic_observed)
            .to_msb_string()
        == "0");

    const auto empty_dynamic_wildcard =
        fsim::frontend::parse_text(
            "empty_dynamic_wildcard.sv",
            R"(
module empty_dynamic_wildcard;
  initial @*;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(empty_dynamic_wildcard.ok());
    const auto rejected_empty_dynamic_wildcard =
        fsim::elaboration::elaborate(
            empty_dynamic_wildcard.design,
            "sv:work.empty_dynamic_wildcard");
    assert(!rejected_empty_dynamic_wildcard.ok());
    assert(has_diagnostic(
        rejected_empty_dynamic_wildcard, "FSIM-ELAB-062"));

    const auto parsed_sv_conditionals =
        fsim::frontend::parse_text(
            "conditional_flow.sv",
            R"(
module conditional_flow;
  logic zero_case;
  logic one_x_case;
  logic unknown_case;
  logic [3:0] nested_case;
  initial begin
    if (4'b0000)
      zero_case = 1'b1;
    else
      zero_case = 1'b0;
    if (4'bx001)
      one_x_case = 1'b1;
    else
      one_x_case = 1'b0;
    if (4'bx000)
      unknown_case = 1'b1;
    else
      unknown_case = 1'b0;
    if (4'b0010) begin
      if (1'b0)
        nested_case = 4'b0001;
      else
        nested_case = 4'b0010;
    end else begin
      nested_case = 4'b0011;
    end
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(parsed_sv_conditionals.ok());
    const auto elaborated_sv_conditionals =
        fsim::elaboration::elaborate(
            parsed_sv_conditionals.design,
            "sv:work.conditional_flow");
    assert(elaborated_sv_conditionals.ok());
    const auto& sv_conditional_process =
        elaborated_sv_conditionals.design->processes().front();
    assert(
        std::count_if(
            sv_conditional_process.operations.begin(),
            sv_conditional_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::Branch>(operation);
            })
        == 2);
    assert(
        std::count_if(
            sv_conditional_process.operations.begin(),
            sv_conditional_process.operations.end(),
            [](const fsim::runtime::simir::Operation& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::LogicalNot>(operation);
            })
        == 4);
    auto sv_conditional_interpreter =
        elaborated_sv_conditionals.design->create_interpreter();
    const auto sv_conditional_result =
        sv_conditional_interpreter->run();
    assert(
        sv_conditional_result.status
        == fsim::runtime::RunStatus::completed);
    const auto zero_case =
        elaborated_sv_conditionals.design->find_signal("zero_case");
    const auto one_x_case =
        elaborated_sv_conditionals.design->find_signal("one_x_case");
    const auto unknown_case =
        elaborated_sv_conditionals.design->find_signal("unknown_case");
    const auto nested_case =
        elaborated_sv_conditionals.design->find_signal("nested_case");
    assert(zero_case && one_x_case && unknown_case && nested_case);
    assert(
        sv_conditional_interpreter
            ->signal_value(*zero_case)
            .to_msb_string()
        == "0");
    assert(
        sv_conditional_interpreter
            ->signal_value(*one_x_case)
            .to_msb_string()
        == "1");
    assert(
        sv_conditional_interpreter
            ->signal_value(*unknown_case)
            .to_msb_string()
        == "0");
    assert(
        sv_conditional_interpreter
            ->signal_value(*nested_case)
            .to_msb_string()
        == "0010");

    const auto parsed_vhdl_conditionals =
        fsim::frontend::parse_text(
            "conditional_flow.vhd",
            R"(
entity conditional_flow is
  port (
    true_case : out boolean;
    elsif_case : out boolean;
    nested_case : out boolean;
    boolean_expression_case : out boolean
  );
end entity;

architecture rtl of conditional_flow is
  signal trigger : std_logic;
begin
  choose: process(trigger)
  begin
    if true then
      true_case <= true;
    else
      true_case <= false;
    end if;
    if false then
      elsif_case <= false;
    elsif true /= false then
      elsif_case <= true;
    else
      elsif_case <= false;
    end if;
    if 1 = 1 then
      if false then
        nested_case <= false;
      else
        nested_case <= true;
      end if;
    else
      nested_case <= false;
    end if;
    if (not false) and (true nand false)
       and (false nor false) and (true xnor true)
       and (true /= false) then
      boolean_expression_case <= true;
    else
      boolean_expression_case <= false;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(parsed_vhdl_conditionals.ok());
    const auto elaborated_vhdl_conditionals =
        fsim::elaboration::elaborate(
            parsed_vhdl_conditionals.design,
            "vhdl:work.conditional_flow(rtl)");
    if (!elaborated_vhdl_conditionals.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_conditionals.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_conditionals.ok());
    auto vhdl_conditional_interpreter =
        elaborated_vhdl_conditionals.design->create_interpreter();
    const auto vhdl_conditional_result =
        vhdl_conditional_interpreter->run();
    assert(
        vhdl_conditional_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto name :
         {"true_case", "elsif_case", "nested_case",
          "boolean_expression_case"}) {
        const auto signal =
            elaborated_vhdl_conditionals.design->find_signal(name);
        assert(signal);
        assert(
            vhdl_conditional_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == "1");
    }

    const auto vhdl_sequential_loops =
        fsim::frontend::parse_text(
            "sequential_loops.vhd",
            R"(
entity sequential_loops is
  port (
    kick : in std_logic;
    observed : out std_logic_vector(3 downto 0);
    null_range_observed : out std_logic_vector(1 downto 0)
  );
end entity;
architecture rtl of sequential_loops is
begin
  populate: process(kick)
    variable assembled : std_logic_vector(3 downto 0) := "0000";
    variable untouched : std_logic_vector(1 downto 0) := "00";
  begin
    for lane in 0 to 3 loop
      assembled(lane) := '1';
    end loop;
    for lane in 3 downto 2 loop
      assembled(lane) := '0';
    end loop;
    for lane in 2 to 1 loop
      untouched(0) := '1';
    end loop;
    observed <= assembled;
    null_range_observed <= untouched;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_sequential_loops.ok());
    const auto elaborated_vhdl_sequential_loops =
        fsim::elaboration::elaborate(
            vhdl_sequential_loops.design,
            "vhdl:work.sequential_loops(rtl)");
    if (!elaborated_vhdl_sequential_loops.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_sequential_loops.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_sequential_loops.ok());
    auto vhdl_loop_interpreter =
        elaborated_vhdl_sequential_loops.design
            ->create_interpreter();
    const auto vhdl_loop_result =
        vhdl_loop_interpreter->run();
    assert(
        vhdl_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto loop_observed =
        elaborated_vhdl_sequential_loops.design
            ->find_signal("observed");
    const auto null_range_observed =
        elaborated_vhdl_sequential_loops.design
            ->find_signal("null_range_observed");
    assert(loop_observed && null_range_observed);
    assert(
        vhdl_loop_interpreter
            ->signal_value(*loop_observed)
            .to_msb_string()
        == "0011");
    assert(
        vhdl_loop_interpreter
            ->signal_value(*null_range_observed)
            .to_msb_string()
        == "00");

    const auto invalid_vhdl_loops =
        fsim::frontend::parse_text(
            "invalid_sequential_loops.vhd",
            R"(
entity invalid_sequential_loops is
  port (dynamic_bound : in std_logic);
end entity;
architecture rtl of invalid_sequential_loops is
begin
  invalid: process(dynamic_bound)
  begin
    for lane in dynamic_bound to 1 loop
      null;
    end loop;
    for lane in 0 to dynamic_bound loop
      null;
    end loop;
    for lane in 0 to 1000000 loop
      null;
    end loop;
    for lane in 0 to 1 loop
      lane := lane + 1;
    end loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_loops.ok());
    const auto rejected_vhdl_loops =
        fsim::elaboration::elaborate(
            invalid_vhdl_loops.design,
            "vhdl:work.invalid_sequential_loops(rtl)");
    assert(!rejected_vhdl_loops.ok());
    for (const auto code :
         {"FSIM-ELAB-071", "FSIM-ELAB-072",
          "FSIM-ELAB-073", "FSIM-ELAB-074"}) {
        assert(has_diagnostic(rejected_vhdl_loops, code));
    }

    const auto systemverilog_procedural_loops =
        fsim::frontend::parse_text(
            "procedural_loops.sv",
            R"(
module procedural_loops;
  logic [3:0] observed;
  logic [1:0] null_range_observed;
  initial begin
    observed = 4'b0000;
    null_range_observed = 2'b00;
    for (int lane = 0; lane < 4; lane++)
      observed[lane] = 1'b1;
    for (int lane = 3; lane >= 2; --lane)
      observed[lane] = 1'b0;
    for (int lane = 2; lane < 1; lane += 1)
      null_range_observed[0] = 1'b1;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_procedural_loops.ok());
    const auto elaborated_systemverilog_procedural_loops =
        fsim::elaboration::elaborate(
            systemverilog_procedural_loops.design,
            "sv:work.procedural_loops");
    if (!elaborated_systemverilog_procedural_loops.ok()) {
        for (const auto& diagnostic :
             elaborated_systemverilog_procedural_loops.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_systemverilog_procedural_loops.ok());
    auto systemverilog_loop_interpreter =
        elaborated_systemverilog_procedural_loops.design
            ->create_interpreter();
    const auto systemverilog_loop_result =
        systemverilog_loop_interpreter->run();
    assert(
        systemverilog_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto systemverilog_loop_observed =
        elaborated_systemverilog_procedural_loops.design
            ->find_signal("observed");
    const auto systemverilog_null_range_observed =
        elaborated_systemverilog_procedural_loops.design
            ->find_signal("null_range_observed");
    assert(
        systemverilog_loop_observed
        && systemverilog_null_range_observed);
    assert(
        systemverilog_loop_interpreter
            ->signal_value(*systemverilog_loop_observed)
            .to_msb_string()
        == "0011");
    assert(
        systemverilog_loop_interpreter
            ->signal_value(*systemverilog_null_range_observed)
            .to_msb_string()
        == "00");

    const auto dynamic_systemverilog_loops =
        fsim::frontend::parse_text(
            "dynamic_procedural_loops.sv",
            R"(
module dynamic_procedural_loops;
  logic [2:0] dynamic_initial;
  logic [2:0] dynamic_bound;
  logic [3:0] observed;
  initial begin
    dynamic_initial = 1;
    dynamic_bound = 3;
    observed = 0;
    for (int lane = dynamic_initial; lane < dynamic_bound; lane++)
      observed = observed + 1;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(dynamic_systemverilog_loops.ok());
    const auto elaborated_dynamic_systemverilog_loops =
        fsim::elaboration::elaborate(
            dynamic_systemverilog_loops.design,
            "sv:work.dynamic_procedural_loops");
    assert(elaborated_dynamic_systemverilog_loops.ok());
    auto dynamic_loop_interpreter =
        elaborated_dynamic_systemverilog_loops.design
            ->create_interpreter();
    assert(
        dynamic_loop_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto dynamic_loop_observed =
        elaborated_dynamic_systemverilog_loops.design
            ->find_signal("observed");
    assert(dynamic_loop_observed);
    assert(
        dynamic_loop_interpreter
            ->signal_value(*dynamic_loop_observed)
            .to_msb_string()
        == "0010");

    const auto invalid_systemverilog_loops =
        fsim::frontend::parse_text(
            "invalid_procedural_loops.sv",
            R"(
module invalid_procedural_loops;
  initial begin
    for (int lane = 0; lane < 1000001; lane++);
    for (int lane = 0; lane < 1; lane++) lane = 2;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_systemverilog_loops.ok());
    const auto rejected_systemverilog_loops =
        fsim::elaboration::elaborate(
            invalid_systemverilog_loops.design,
            "sv:work.invalid_procedural_loops");
    assert(!rejected_systemverilog_loops.ok());
    for (const auto code :
         {"FSIM-ELAB-073", "FSIM-ELAB-074"}) {
        assert(
            has_diagnostic(
                rejected_systemverilog_loops, code));
    }

    const auto repeat_statements =
        fsim::frontend::parse_text(
            "repeat_statements.sv",
            R"(
module repeat_statements;
  logic [2:0] observed;
  initial begin
    observed = 3'b000;
    repeat (3) observed = observed + 1;
    repeat (0) observed = 3'b111;
    begin
      logic [2:0] runtime_count = 3'd2;
      logic [2:0] unknown_count = 3'bxxx;
      repeat (runtime_count) observed = observed + 1;
      repeat (unknown_count) observed = 3'b111;
    end
    repeat (-1) observed = 3'b111;
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(repeat_statements.ok());
    const auto elaborated_repeat_statements =
        fsim::elaboration::elaborate(
            repeat_statements.design,
            "sv:work.repeat_statements");
    assert(elaborated_repeat_statements.ok());
    assert(std::ranges::any_of(
        elaborated_repeat_statements.design->processes().front().operations,
        [](const auto& operation) {
          const auto* binary = fsim::runtime::simir::operation_get_if<
              fsim::runtime::simir::Binary>(&operation);
          return binary != nullptr
              && binary->operation
                  == fsim::runtime::simir::BinaryOperator::less_unsigned;
        }));
    auto repeat_interpreter =
        elaborated_repeat_statements.design
            ->create_interpreter();
    const auto repeat_result = repeat_interpreter->run();
    assert(
        repeat_result.status
        == fsim::runtime::RunStatus::completed);
    const auto repeat_observed =
        elaborated_repeat_statements.design
            ->find_signal("observed");
    assert(repeat_observed);
    assert(
        repeat_interpreter
            ->signal_value(*repeat_observed)
            .to_msb_string()
        == "101");

    const auto runtime_for_statements =
        fsim::frontend::parse_text(
            "runtime_for_statements.sv",
            R"(
module runtime_for_statements;
  logic [3:0] observed;
  initial begin
    integer lane;
    observed = 4'd0;
    for (lane = 0; lane < 4; lane += 2)
      observed = observed + 1;
    for (int local_lane = 0; local_lane < 4;
         local_lane = local_lane + 2) begin
      observed = observed + 1;
      continue;
      observed = 4'd15;
    end
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(runtime_for_statements.ok());
    const auto elaborated_runtime_for =
        fsim::elaboration::elaborate(
            runtime_for_statements.design,
            "sv:work.runtime_for_statements");
    assert(elaborated_runtime_for.ok());
    auto runtime_for_interpreter =
        elaborated_runtime_for.design->create_interpreter();
    assert(
        runtime_for_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto runtime_for_observed =
        elaborated_runtime_for.design->find_signal("observed");
    assert(runtime_for_observed);
    assert(
        runtime_for_interpreter
            ->signal_value(*runtime_for_observed)
            .to_msb_string()
        == "0100");

    const auto invalid_repeat_statements =
        fsim::frontend::parse_text(
            "invalid_repeat_statements.sv",
            R"(
module invalid_repeat_statements;
  initial begin
    repeat (1000001);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_repeat_statements.ok());
    const auto rejected_repeat_statements =
        fsim::elaboration::elaborate(
            invalid_repeat_statements.design,
            "sv:work.invalid_repeat_statements");
    assert(!rejected_repeat_statements.ok());
    assert(has_diagnostic(
        rejected_repeat_statements, "FSIM-ELAB-073"));

    const auto runtime_loop_statements =
        fsim::frontend::parse_text(
            "runtime_loop_statements.sv",
            R"(
module runtime_loop_statements;
  logic [2:0] observed;
  logic unknown_body;
  logic clock;
  initial begin
    observed = 3'b000;
    unknown_body = 1'b0;
    while (observed < 3) observed = observed + 1;
    while (1'bx) unknown_body = 1'b1;
  end
  initial begin
    clock = 1'b0;
    forever #1 clock = ~clock;
  end
  initial #3 $finish;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(runtime_loop_statements.ok());
    const auto elaborated_runtime_loop_statements =
        fsim::elaboration::elaborate(
            runtime_loop_statements.design,
            "sv:work.runtime_loop_statements");
    if (!elaborated_runtime_loop_statements.ok()) {
        for (const auto& diagnostic :
             elaborated_runtime_loop_statements.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_runtime_loop_statements.ok());
    auto runtime_loop_interpreter =
        elaborated_runtime_loop_statements.design
            ->create_interpreter();
    const auto runtime_loop_result =
        runtime_loop_interpreter->run();
    assert(
        runtime_loop_result.status
        == fsim::runtime::RunStatus::stopped);
    assert(runtime_loop_result.time == 3);
    const auto runtime_loop_observed =
        elaborated_runtime_loop_statements.design
            ->find_signal("observed");
    const auto runtime_loop_unknown_body =
        elaborated_runtime_loop_statements.design
            ->find_signal("unknown_body");
    assert(runtime_loop_observed && runtime_loop_unknown_body);
    assert(
        runtime_loop_interpreter
            ->signal_value(*runtime_loop_observed)
            .to_msb_string()
        == "011");
    assert(
        runtime_loop_interpreter
            ->signal_value(*runtime_loop_unknown_body)
            .to_msb_string()
        == "0");

    const auto systemverilog_loop_control =
        fsim::frontend::parse_text(
            "systemverilog_loop_control.sv",
            R"(
module systemverilog_loop_control;
  logic [3:0] static_result;
  logic [3:0] break_result;
  logic [3:0] runtime_result;
  logic [3:0] nested_result;
  logic [3:0] cursor;
  initial begin
    static_result = 4'b0000;
    repeat (3) begin
      static_result = static_result + 1;
      continue;
      static_result = static_result + 4;
    end
    break_result = 4'b0000;
    repeat (3) begin
      break_result = break_result + 1;
      break;
      break_result = break_result + 4;
    end
    runtime_result = 4'b0000;
    cursor = 4'b0000;
    while (cursor < 6) begin
      cursor = cursor + 1;
      if (cursor == 2) continue;
      if (cursor == 5) break;
      runtime_result = runtime_result + cursor;
    end
    nested_result = 4'b0000;
    for (int outer = 0; outer < 2; outer++) begin
      for (int inner = 0; inner < 3; inner++) begin
        nested_result = nested_result + 1;
        break;
      end
    end
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(systemverilog_loop_control.ok());
    const auto elaborated_systemverilog_loop_control =
        fsim::elaboration::elaborate(
            systemverilog_loop_control.design,
            "sv:work.systemverilog_loop_control");
    if (!elaborated_systemverilog_loop_control.ok()) {
        for (const auto& diagnostic :
             elaborated_systemverilog_loop_control.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_systemverilog_loop_control.ok());
    auto systemverilog_loop_control_interpreter =
        elaborated_systemverilog_loop_control.design
            ->create_interpreter();
    const auto systemverilog_loop_control_result =
        systemverilog_loop_control_interpreter->run();
    assert(
        systemverilog_loop_control_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<std::pair<
             std::string_view, std::string_view>>{
             {"static_result", "0011"},
             {"break_result", "0001"},
             {"runtime_result", "1000"},
             {"nested_result", "0010"}}) {
        const auto signal =
            elaborated_systemverilog_loop_control.design
                ->find_signal(name);
        assert(signal);
        const auto actual =
            systemverilog_loop_control_interpreter
                ->signal_value(*signal)
                .to_msb_string();
        if (actual != expected) {
            std::cerr << name << ": expected " << expected
                      << ", got " << actual << '\n';
        }
        assert(
            actual == expected);
    }

    const auto orphan_loop_control =
        fsim::frontend::parse_text(
            "orphan_loop_control.sv",
            R"(
module orphan_loop_control;
  initial break;
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(!orphan_loop_control.ok());
    const auto rejected_orphan_loop_control =
        fsim::elaboration::elaborate(
            orphan_loop_control.design,
            "sv:work.orphan_loop_control");
    assert(!rejected_orphan_loop_control.ok());
    assert(has_diagnostic(
        rejected_orphan_loop_control, "FSIM-ELAB-078"));

    const auto vhdl_runtime_loop =
        fsim::frontend::parse_text(
            "vhdl_runtime_loop.vhd",
            R"(
entity vhdl_runtime_loop is
  port (
    trigger : in std_logic;
    observed : out boolean
  );
end entity;
architecture rtl of vhdl_runtime_loop is
begin
  execute: process(trigger)
    variable keep_going : boolean := true;
    variable result : boolean := false;
  begin
    while keep_going loop
      result := true;
      keep_going := false;
    end loop;
    observed <= result;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(vhdl_runtime_loop.ok());
    const auto elaborated_vhdl_runtime_loop =
        fsim::elaboration::elaborate(
            vhdl_runtime_loop.design,
            "vhdl:work.vhdl_runtime_loop(rtl)");
    assert(elaborated_vhdl_runtime_loop.ok());
    auto vhdl_runtime_loop_interpreter =
        elaborated_vhdl_runtime_loop.design
            ->create_interpreter();
    const auto vhdl_runtime_loop_result =
        vhdl_runtime_loop_interpreter->run();
    assert(
        vhdl_runtime_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto vhdl_runtime_loop_observed =
        elaborated_vhdl_runtime_loop.design
            ->find_signal("observed");
    assert(vhdl_runtime_loop_observed);
    assert(
        vhdl_runtime_loop_interpreter
            ->signal_value(*vhdl_runtime_loop_observed)
            .to_msb_string()
        == "1");

    const auto vhdl_loop_control =
        fsim::frontend::parse_text(
            "vhdl_loop_control.vhd",
            R"(
entity vhdl_loop_control is
  port (
    trigger : in std_logic;
    static_ok : out boolean;
    runtime_ok : out boolean;
    nested_ok : out boolean;
    targeted_ok : out boolean
  );
end entity;
architecture rtl of vhdl_loop_control is
begin
  execute: process(trigger)
    variable static_result : boolean := false;
    variable runtime_result : boolean := false;
    variable nested_result : boolean := false;
    variable targeted_result : boolean := false;
    variable keep_going : boolean := true;
    variable skipped : boolean := false;
  begin
    for lane in 0 to 4 loop
      next when lane = 0;
      static_result := true;
      exit;
    end loop;
    while keep_going loop
      if not skipped then
        skipped := true;
        next;
      end if;
      runtime_result := true;
      exit;
    end loop;
    for outer in 0 to 1 loop
      for inner in 0 to 2 loop
        nested_result := true;
        exit;
      end loop;
    end loop;
    outer_loop: for outer in 0 to 1 loop
      inner_loop: loop
        if outer = 0 then
          next outer_loop;
        end if;
        targeted_result := true;
        exit outer_loop;
      end loop inner_loop;
      targeted_result := false;
    end loop outer_loop;
    static_ok <= static_result;
    runtime_ok <= runtime_result;
    nested_ok <= nested_result;
    targeted_ok <= targeted_result;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    if (!vhdl_loop_control.ok()) {
        for (const auto& diagnostic :
             vhdl_loop_control.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl_loop_control.ok());
    const auto elaborated_vhdl_loop_control =
        fsim::elaboration::elaborate(
            vhdl_loop_control.design,
            "vhdl:work.vhdl_loop_control(rtl)");
    if (!elaborated_vhdl_loop_control.ok()) {
        for (const auto& diagnostic :
             elaborated_vhdl_loop_control.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_vhdl_loop_control.ok());
    auto vhdl_loop_control_interpreter =
        elaborated_vhdl_loop_control.design
            ->create_interpreter();
    const auto vhdl_loop_control_result =
        vhdl_loop_control_interpreter->run();
    assert(
        vhdl_loop_control_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto name :
         {"static_ok", "runtime_ok", "nested_ok", "targeted_ok"}) {
        const auto signal =
            elaborated_vhdl_loop_control.design
                ->find_signal(name);
        assert(signal);
        assert(
            vhdl_loop_control_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == "1");
    }

    const auto orphan_vhdl_loop_target =
        fsim::frontend::parse_text(
            "orphan_vhdl_loop_target.vhd",
            R"(
entity orphan_vhdl_loop_target is
  port (trigger : in std_logic);
end entity;
architecture rtl of orphan_vhdl_loop_target is
begin
  execute: process(trigger)
  begin
    outer_loop: loop
      exit missing_loop;
    end loop outer_loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(!orphan_vhdl_loop_target.ok());
    const auto rejected_orphan_vhdl_loop_target =
        fsim::elaboration::elaborate(
            orphan_vhdl_loop_target.design,
            "vhdl:work.orphan_vhdl_loop_target(rtl)");
    assert(!rejected_orphan_vhdl_loop_target.ok());
    assert(has_diagnostic(
        rejected_orphan_vhdl_loop_target,
        "FSIM-ELAB-080"));

    const auto post_test_loop =
        fsim::frontend::parse_text(
            "post_test_loop.sv",
            R"(
module post_test_loop;
  logic [3:0] controlled;
  logic [3:0] executes_once;
  initial begin
    controlled = 4'b0000;
    do begin
      controlled = controlled + 1;
      if (controlled == 1) continue;
      if (controlled == 4) break;
      controlled = controlled + 1;
    end while (controlled < 6);
    executes_once = 4'b0000;
    do executes_once = executes_once + 1;
    while (1'b0);
  end
endmodule
)",
            fsim::frontend::Language::SystemVerilog2017);
    assert(post_test_loop.ok());
    const auto elaborated_post_test_loop =
        fsim::elaboration::elaborate(
            post_test_loop.design,
            "sv:work.post_test_loop");
    if (!elaborated_post_test_loop.ok()) {
        for (const auto& diagnostic :
             elaborated_post_test_loop.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated_post_test_loop.ok());
    auto post_test_loop_interpreter =
        elaborated_post_test_loop.design
            ->create_interpreter();
    const auto post_test_loop_result =
        post_test_loop_interpreter->run();
    assert(
        post_test_loop_result.status
        == fsim::runtime::RunStatus::completed);
    for (const auto& [name, expected] :
         std::initializer_list<std::pair<
             std::string_view, std::string_view>>{
             {"controlled", "0100"},
             {"executes_once", "0001"}}) {
        const auto signal =
            elaborated_post_test_loop.design
                ->find_signal(name);
        assert(signal);
        assert(
            post_test_loop_interpreter
                ->signal_value(*signal)
                .to_msb_string()
            == expected);
    }

    const auto unconditional_vhdl_loop =
        fsim::frontend::parse_text(
            "unconditional_vhdl_loop.vhd",
            R"(
entity unconditional_vhdl_loop is
  port (
    trigger : in std_logic;
    observed : out boolean
  );
end entity;
architecture rtl of unconditional_vhdl_loop is
begin
  execute: process(trigger)
    variable skipped : boolean := false;
    variable result : boolean := false;
  begin
    loop
      if not skipped then
        skipped := true;
        next;
      end if;
      result := true;
      exit;
    end loop;
    observed <= result;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(unconditional_vhdl_loop.ok());
    const auto elaborated_unconditional_vhdl_loop =
        fsim::elaboration::elaborate(
            unconditional_vhdl_loop.design,
            "vhdl:work.unconditional_vhdl_loop(rtl)");
    assert(elaborated_unconditional_vhdl_loop.ok());
    auto unconditional_vhdl_loop_interpreter =
        elaborated_unconditional_vhdl_loop.design
            ->create_interpreter();
    const auto unconditional_vhdl_loop_result =
        unconditional_vhdl_loop_interpreter->run();
    assert(
        unconditional_vhdl_loop_result.status
        == fsim::runtime::RunStatus::completed);
    const auto unconditional_vhdl_loop_observed =
        elaborated_unconditional_vhdl_loop.design
            ->find_signal("observed");
    assert(unconditional_vhdl_loop_observed);
    assert(
        unconditional_vhdl_loop_interpreter
            ->signal_value(*unconditional_vhdl_loop_observed)
            .to_msb_string()
        == "1");

    const auto invalid_vhdl_runtime_loop =
        fsim::frontend::parse_text(
            "invalid_vhdl_runtime_loop.vhd",
            R"(
entity invalid_vhdl_runtime_loop is
  port (condition : in std_logic);
end entity;
architecture rtl of invalid_vhdl_runtime_loop is
begin
  execute: process(condition)
  begin
    while condition loop
      null;
    end loop;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_runtime_loop.ok());
    const auto rejected_vhdl_runtime_loop =
        fsim::elaboration::elaborate(
            invalid_vhdl_runtime_loop.design,
            "vhdl:work.invalid_vhdl_runtime_loop(rtl)");
    assert(!rejected_vhdl_runtime_loop.ok());
    assert(
        has_diagnostic(
            rejected_vhdl_runtime_loop,
            "FSIM-ELAB-077"));

    const auto invalid_vhdl_condition =
        fsim::frontend::parse_text(
            "invalid_condition.vhd",
            R"(
entity invalid_condition is
  port (gate : in std_logic);
end entity;
architecture rtl of invalid_condition is
begin
  invalid: process(gate)
  begin
    if gate then
      null;
    end if;
  end process;
end architecture;
)",
            fsim::frontend::Language::Vhdl2008);
    assert(invalid_vhdl_condition.ok());
    const auto rejected_vhdl_condition =
        fsim::elaboration::elaborate(
            invalid_vhdl_condition.design,
            "vhdl:work.invalid_condition(rtl)");
    assert(!rejected_vhdl_condition.ok());
    assert(has_diagnostic(
        rejected_vhdl_condition, "FSIM-ELAB-048"));
}

} // namespace fsim::tests::elaboration
