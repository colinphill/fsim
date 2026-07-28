// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace {

volatile std::sig_atomic_t restored_interrupt_count = 0;

extern "C" void record_restored_interrupt(int) {
  restored_interrupt_count = 1;
}

class InterruptingOutputBuffer final : public std::stringbuf {
 protected:
  std::streamsize xsputn(
      const char* value,
      const std::streamsize count) override {
    const auto written = std::stringbuf::xsputn(value, count);
    if (!raised_ && str().find("(fsim) ") != std::string::npos) {
      raised_ = true;
      (void)std::raise(SIGINT);
    }
    return written;
  }

 private:
  bool raised_{};
};

}  // namespace

int main() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-application-test-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  const auto source = directory / "tb.sv";
  {
    std::ofstream output(source);
    output << R"(
module child(input logic value, output logic inverted);
  assign inverted = ~value;
endmodule

module tb;
  logic q;
  logic child_y;
  bit two_state;
  child u_child(.value(q), .inverted(child_y));
  initial begin
    logic local_state = 1'b0;
    q = local_state;
    #2 local_state = 1'b1;
    q = local_state;
    #1 $finish;
  end
endmodule
)";
  }
  const auto scheduled_source = directory / "scheduled.sv";
  {
    std::ofstream output(scheduled_source);
    output << R"(
module scheduled;
  logic q;
  initial begin
    q <= 1'b0;
    q <= #2 1'b1;
    #3 $finish;
  end
endmodule

module scheduled_overflow;
  logic q;
  initial begin
    #1 q <= #18446744073709551615 1'b1;
  end
endmodule
)";
  }
  const auto sensitivity_source = directory / "sensitivity.sv";
  {
    std::ofstream output(sensitivity_source);
    output << R"(
module sensitivity;
  logic trigger;
  logic observed;
  logic dynamic_observed;
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 trigger = 1'b0;
    #1 $finish;
  end
  always @(posedge trigger) observed <= trigger;
  initial begin
    @(posedge trigger);
    dynamic_observed = trigger;
    @(negedge trigger) dynamic_observed = trigger;
  end
endmodule
)";
  }
  const auto vhdl_wait_source = directory / "vhdl_wait.vhd";
  {
    std::ofstream output(vhdl_wait_source);
    output << R"(
entity vhdl_wait is
end entity;
architecture rtl of vhdl_wait is
  signal q : std_logic;
begin
  worker: process
  begin
    q <= '0';
    wait for 1 ns;
    q <= '1';
    wait for 1 ns;
  end process;
end architecture;
)";
  }
  const auto wildcard_source = directory / "wildcard.sv";
  {
    std::ofstream output(wildcard_source);
    output << R"(
module wildcard_app;
  logic a;
  logic q;
  logic y;
  logic latched;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latched = q;
  initial begin
    a = 1'b0;
    #1 a = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto case_source = directory / "case.sv";
  {
    std::ofstream output(case_source);
    output << R"(
module case_app;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b00;
    2'b01, 2'b10: result = 2'b01;
    2'bx0: result = 2'b10;
    2'bz1: result = 2'b11;
    default: result = 2'b00;
  endcase
  initial begin
    selector = 2'b00;
    #1 selector = 2'b10;
    #1 selector = 2'bx0;
    #1 selector = 2'bz1;
    #1 selector = 2'b11;
    #1 $finish;
  end
endmodule
)";
  }
  const auto conditional_source = directory / "conditional.sv";
  {
    std::ofstream output(conditional_source);
    output << R"(
module conditional_app;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  always_comb result = select ? lhs : rhs;
  initial begin
    lhs = 4'b101z;
    rhs = 4'b100z;
    select = 1'b0;
    #1 select = 1'b1;
    #1 select = 1'bx;
    #1 select = 1'bz;
    #1 $finish;
  end
endmodule
)";
  }
  const auto comparison_source = directory / "comparison.sv";
  {
    std::ofstream output(comparison_source);
    output << R"(
module comparison_app;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic neq;
  logic lt;
  logic le;
  logic gt;
  logic ge;
  logic logical_not;
  always_comb begin
    neq = lhs != rhs;
    lt = lhs < rhs;
    le = lhs <= rhs;
    gt = lhs > rhs;
    ge = lhs >= rhs;
    logical_not = !lhs;
  end
  initial begin
    lhs = 4'b0010;
    rhs = 4'b0011;
    #1 lhs = 4'b0000;
    rhs = 4'b0000;
    #1 lhs = 4'b00x0;
    rhs = 4'b0011;
    #1 lhs = 4'b01z0;
    #1 $finish;
  end
endmodule
)";
  }
  const auto logical_source = directory / "logical.sv";
  {
    std::ofstream output(logical_source);
    output << R"(
module logical_app;
  logic [3:0] lhs;
  logic [1:0] rhs;
  logic [2:0] amount;
  logic conjunction;
  logic disjunction;
  logic reduced_and;
  logic reduced_or;
  logic reduced_xor;
  logic [3:0] shifted_left;
  logic [3:0] shifted_right;
  always_comb begin
    conjunction = lhs && rhs;
    disjunction = lhs || rhs;
    reduced_and = &lhs;
    reduced_or = |lhs;
    reduced_xor = ^lhs;
    shifted_left = lhs << amount;
    shifted_right = lhs >> amount;
  end
  initial begin
    lhs = 4'b0000;
    rhs = 2'bx1;
    amount = 3'b001;
    #1 lhs = 4'b00x0;
    rhs = 2'b00;
    amount = 3'b0x1;
    #1 rhs = 2'b01;
    amount = 3'b100;
    #1 lhs = 4'b0010;
    rhs = 2'bzz;
    #1 rhs = 2'b01;
    amount = 3'b001;
    #1 $finish;
  end
endmodule
)";
  }
  const auto arithmetic_source = directory / "arithmetic.sv";
  {
    std::ofstream output(arithmetic_source);
    output << R"(
module arithmetic_app;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] difference;
  logic [7:0] product;
  logic [7:0] quotient;
  logic [7:0] remainder;
  logic [7:0] positive;
  logic [7:0] negative;
  logic signed [7:0] signed_lhs;
  logic signed [7:0] signed_rhs;
  logic signed [7:0] signed_sum;
  logic signed [7:0] signed_difference;
  logic signed [7:0] signed_product;
  logic signed [7:0] signed_quotient;
  logic signed [7:0] signed_remainder;
  logic signed_less;
  always_comb begin
    difference = lhs - rhs;
    product = lhs * rhs;
    quotient = lhs / rhs;
    remainder = lhs % rhs;
    positive = +lhs;
    negative = -lhs;
    signed_sum = signed_lhs + signed_rhs;
    signed_difference = signed_lhs - signed_rhs;
    signed_product = signed_lhs * signed_rhs;
    signed_quotient = signed_lhs / signed_rhs;
    signed_remainder = signed_lhs % signed_rhs;
    signed_less = signed_lhs < signed_rhs;
  end
  initial begin
    lhs = 8'b11001000;
    rhs = 8'b00000111;
    signed_lhs = 8'b11111011;
    signed_rhs = 8'b00000011;
    #1 lhs = 8'b10x01000;
    signed_lhs = 8'b00000101;
    signed_rhs = 8'b11111101;
    #1 lhs = 8'b11001000;
    rhs = 8'b00000000;
    #1 rhs = 8'b00000111;
    #1 $finish;
  end
endmodule
)";
  }
  const auto select_concat_source =
      directory / "select_concat.sv";
  {
    std::ofstream output(select_concat_source);
    output << R"(
module select_concat_app;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected_descending;
  logic selected_ascending;
  logic selected_local;
  logic [3:0] descending_part;
  logic [3:0] ascending_part;
  logic [8:0] joined;
  logic [7:0] assigned;
  logic [5:2] local_assigned;
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
  initial begin
    logic [5:2] assignment_local;
    descending = 8'b10xz0110;
    ascending = 8'b01zx1100;
    assigned[0] <= 1'b1;
    assigned = 8'b00000000;
    assigned[1] = 1'b1;
    assigned[7:4] = 4'b10xz;
    assigned[3:2] <= #1 2'b11;
    assignment_local = 4'b0000;
    assignment_local[3] = 1'b1;
    assignment_local[5:4] = 2'bxz;
    local_assigned = assignment_local;
    #1 descending = 8'bz10100x1;
    ascending = 8'b1100xz01;
    #1 $finish;
  end
endmodule
)";
  }
  const auto vhdl_select_concat_source =
      directory / "vhdl_select_concat.vhd";
  {
    std::ofstream output(vhdl_select_concat_source);
    output << R"(
entity vhdl_select_concat_app is
end entity;

architecture rtl of vhdl_select_concat_app is
  signal descending : std_logic_vector(7 downto 4);
  signal ascending : std_logic_vector(2 to 5);
  signal selected_descending : std_logic;
  signal selected_concurrent : std_logic;
  signal selected_ascending : std_logic;
  signal selected_local : std_logic;
  signal descending_part : std_logic_vector(1 downto 0);
  signal ascending_part : std_logic_vector(1 downto 0);
  signal joined : std_logic_vector(5 downto 0);
  signal assigned : std_logic_vector(7 downto 0);
  signal local_assigned : std_logic_vector(5 downto 2);
begin
  descending <= "1XZ0";
  ascending <= "01Z1";
  selected_concurrent <= descending(5);

  observe: process(descending, ascending)
    variable local_copy : std_logic_vector(9 downto 8);
    variable assignment_local : std_logic_vector(5 downto 2);
  begin
    local_copy := descending(7 downto 6);
    selected_descending <= descending(5);
    selected_ascending <= ascending(4);
    selected_local <= local_copy(8);
    descending_part <= descending(7 downto 6);
    ascending_part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
    assigned <= "00000000";
    assigned(1) <= '1';
    assigned(7 downto 4) <= "10XZ";
    assigned(3 downto 2) <= "11" after 5 ns;
    assignment_local := "0000";
    assignment_local(3) := '1';
    assignment_local(5 downto 4) := "XZ";
    local_assigned <= assignment_local;
  end process;
end architecture;
)";
  }
  const auto vhdl_signed_source =
      directory / "vhdl_signed.vhd";
  {
    std::ofstream output(vhdl_signed_source);
    output << R"(
entity vhdl_signed_app is
end entity;

architecture rtl of vhdl_signed_app is
  signal lhs : signed(7 downto 0);
  signal rhs : signed(7 downto 0);
  signal sum : signed(7 downto 0);
  signal difference : signed(7 downto 0);
  signal product : signed(7 downto 0);
  signal quotient : signed(7 downto 0);
  signal remainder : signed(7 downto 0);
  signal modulo : signed(7 downto 0);
  signal less : std_logic;
begin
  lhs <= "11111011";
  rhs <= "00000011";
  calculate: process(lhs, rhs)
  begin
    sum <= lhs + rhs;
    difference <= lhs - rhs;
    product <= lhs * rhs;
    quotient <= lhs / rhs;
    remainder <= lhs rem rhs;
    modulo <= lhs mod rhs;
    less <= lhs < rhs;
  end process;
end architecture;
)";
  }
  const auto conditional_statement_source =
      directory / "conditional_statements.sv";
  {
    std::ofstream output(conditional_statement_source);
    output << R"(
module conditional_statement_app;
  logic [3:0] selector;
  logic zero_case;
  logic one_x_case;
  logic unknown_case;
  logic [3:0] nested_case;
  always_comb begin
    if (selector) begin
      if (selector[3])
        nested_case = 4'b0001;
      else
        nested_case = 4'b0010;
    end else begin
      nested_case = 4'b0011;
    end
  end
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
    selector = 4'b0000;
    #1 selector = 4'b0010;
    #1 selector = 4'b1000;
    #1 $finish;
  end
endmodule
)";
  }
  const auto vhdl_conditional_statement_source =
      directory / "vhdl_conditional_statements.vhd";
  {
    std::ofstream output(vhdl_conditional_statement_source);
    output << R"(
entity vhdl_conditional_statement_app is
end entity;

architecture rtl of vhdl_conditional_statement_app is
  signal trigger : std_logic;
  signal true_case : boolean;
  signal elsif_case : boolean;
  signal nested_case : boolean;
  signal boolean_expression_case : boolean;
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
)";
  }
  const auto partial_group_source = directory / "partial_group.sv";
  {
    std::ofstream output(partial_group_source);
    output << R"(
module partial_group;
  logic narrow;
  logic [64:0] wide;
  initial narrow = 1'b1;
  initial begin
    wide = 65'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto assertion_source = directory / "assertion.vhd";
  {
    std::ofstream output(assertion_source);
    output << R"(
entity assertion_test is
end entity;
architecture rtl of assertion_test is
  signal trigger : std_logic;
begin
  check: process(trigger)
  begin
    assert 0 = 1 report "cross-engine mismatch" severity failure;
  end process;
end architecture;
)";
  }
  const auto provenance_source = directory / "provenance.sv";
  const auto unused_source = directory / "unused.sv";
  const auto write_provenance_source =
      [&](const std::string_view comment) {
        std::ofstream output(provenance_source);
        output << R"(
module provenance;
  logic q;
  initial begin
    q = 1'b1;
    #1 $finish;
  end
endmodule
)";
        output << "// " << comment << '\n';
      };
  const auto write_unused_source =
      [&](const std::string_view comment) {
        std::ofstream output(unused_source);
        output << R"(
module unused;
  logic q;
  initial q = 1'b0;
endmodule
)";
        output << "// " << comment << '\n';
      };
  write_provenance_source("top revision 1");
  write_unused_source("unused revision 1");
  const auto parameter_child_source =
      directory / "parameter_child.sv";
  {
    std::ofstream output(parameter_child_source);
    output << R"(
module parameter_child #(
  parameter WIDTH = 1,
  parameter VALUE = 1,
  localparam LAST = WIDTH - 1
) (
  output logic [LAST:0] q
);
  initial q = VALUE;
endmodule
)";
  }
  const auto parameter_top_source =
      directory / "parameter_top.sv";
  const auto write_parameter_top =
      [&](const std::uint64_t narrow_value) {
        std::ofstream output(parameter_top_source);
        output
            << "module parameter_top;\n"
            << "  logic [3:0] narrow;\n"
            << "  logic [7:0] wide;\n"
            << "  parameter_child #(.WIDTH(4), .VALUE("
            << narrow_value
            << ")) u_narrow(.q(narrow));\n"
            << "  parameter_child #(8, 3) u_wide(.q(wide));\n"
            << "  initial #1 $finish;\n"
            << "endmodule\n";
      };
  write_parameter_top(2);
  const auto vhdl_generic_entity_source =
      directory / "vhdl_generic_child_entity.vhd";
  const auto write_vhdl_generic_entity =
      [&](const std::string_view revision) {
        std::ofstream output(vhdl_generic_entity_source);
        output << R"(
entity vhdl_generic_child is
  generic (
    width : positive := 1;
    value : natural := 1;
    last : integer := width - 1
  );
  port (
    q : out unsigned(last downto 0)
  );
end entity;
)";
        output << "-- " << revision << '\n';
      };
  write_vhdl_generic_entity("interface revision 1");
  const auto vhdl_generic_architecture_source =
      directory / "vhdl_generic_child_architecture.vhd";
  {
    std::ofstream output(vhdl_generic_architecture_source);
    output << R"(
architecture rtl of vhdl_generic_child is
begin
  q <= value;
end architecture;
)";
  }
  const auto vhdl_generic_top_source =
      directory / "vhdl_generic_top.vhd";
  const auto write_vhdl_generic_top =
      [&](const std::uint64_t narrow_value) {
        std::ofstream output(vhdl_generic_top_source);
        output << R"(
entity vhdl_generic_top is
end entity;

architecture rtl of vhdl_generic_top is
  signal narrow : unsigned(3 downto 0);
  signal wide : unsigned(7 downto 0);
begin
  narrow_child: entity work.vhdl_generic_child(rtl)
    generic map (
      width => 4,
      value => )"
               << narrow_value << R"(
    )
    port map (
      q => narrow
    );
  wide_child: entity work.vhdl_generic_child(rtl)
    generic map (
      8,
      value => 3
    )
    port map (
      q => wide
    );
  stopper: process
  begin
    wait for 1 ns;
  end process;
end architecture;
)";
      };
  write_vhdl_generic_top(2);
  const auto mixed_actual_sv_top_source =
      directory / "mixed_actual_sv_top.sv";
  {
    std::ofstream output(mixed_actual_sv_top_source);
    output << R"(
module mixed_actual_sv_top;
  logic [3:0] q;
  vhdl_generic_bound #(
    .WIDTH(4),
    .VALUE(5)
  ) child(.q(q));
  initial #1 $finish;
endmodule
)";
  }
  const auto mixed_actual_sv_child_source =
      directory / "mixed_actual_sv_child.sv";
  {
    std::ofstream output(mixed_actual_sv_child_source);
    output << R"(
module mixed_actual_sv_child #(
  parameter width = 1,
  parameter value = 1,
  localparam last = width - 1
) (
  output logic [last:0] q
);
  initial q = value;
endmodule
)";
  }
  const auto mixed_actual_vhdl_top_source =
      directory / "mixed_actual_vhdl_top.vhd";
  {
    std::ofstream output(mixed_actual_vhdl_top_source);
    output << R"(
entity mixed_actual_vhdl_top is
end entity;

architecture rtl of mixed_actual_vhdl_top is
  signal q : unsigned(3 downto 0);
begin
  child: entity work.sv_parameter_bound(rtl)
    generic map (
      4,
      value => 6
    )
    port map (
      q => q
    );
  stopper: process
  begin
    wait for 1 ns;
  end process;
end architecture;
)";
  }
  const auto generated_mixed_sv_top_source =
      directory / "generated_mixed_sv_top.sv";
  {
    std::ofstream output(generated_mixed_sv_top_source);
    output << R"(
module generated_local_leaf #(
  parameter WIDTH = 4,
  parameter VALUE = 3
) (
  output logic [WIDTH - 1:0] q
);
  initial q = VALUE;
endmodule

module generated_mixed_sv_top #(
  parameter ENABLE_FOREIGN = 1
);
  logic [3:0] q;
  generate
    if (ENABLE_FOREIGN) begin : foreign_branch
      vhdl_generated_bound #(
        .WIDTH(4),
        .VALUE(9)
      ) child(.q(q));
    end else begin : local_branch
      generated_local_leaf #(
        .WIDTH(4),
        .VALUE(3)
      ) child(.q(q));
    end
  endgenerate
  initial #1 $finish;
endmodule
)";
  }
  const auto generated_loop_vhdl_source =
      directory / "generated_loop_child.vhd";
  {
    std::ofstream output(generated_loop_vhdl_source);
    output << R"(
entity generated_loop_child is
  generic (
    value : natural := 0
  );
end entity;

architecture rtl of generated_loop_child is
  signal q : unsigned(3 downto 0);
begin
  q <= value;
end architecture;
)";
  }
  const auto generated_loop_sv_top_source =
      directory / "generated_loop_top.sv";
  {
    std::ofstream output(generated_loop_sv_top_source);
    output << R"(
module generated_loop_top #(
  parameter COUNT = 3
);
  genvar i;
  generate
    for (i = 0; i < COUNT; i++) begin : lanes
      generated_loop_bound #(.VALUE(i + 5)) child();
    end
  endgenerate
  initial #1 $finish;
endmodule
)";
  }
  const auto generated_case_sv_top_source =
      directory / "generated_case_top.sv";
  {
    std::ofstream output(generated_case_sv_top_source);
    output << R"(
module generated_case_local_leaf #(
  parameter VALUE = 4
);
  logic [3:0] q;
  initial q = VALUE;
endmodule

module generated_case_top #(
  parameter MODE = 2
);
  generate
    case (MODE)
      0: begin : zero
        generated_case_local_leaf #(.VALUE(1)) child();
      end
      1, 2: begin : selected
        generated_case_bound #(.VALUE(10)) child();
      end
      default: begin : fallback
        generated_case_local_leaf #(.VALUE(4)) child();
      end
    endcase
  endgenerate
  initial #1 $finish;
endmodule
)";
  }
  const auto generated_behavior_sv_source =
      directory / "generated_behavior.sv";
  {
    std::ofstream output(generated_behavior_sv_source);
    output << R"(
module generated_behavior_sv #(parameter ENABLED = 1) (
  output logic [3:0] observed
);
  generate
    if (ENABLED) begin : selected
      localparam int BASE_VALUE = 4;
      localparam int LOCAL_VALUE = BASE_VALUE + 1;
      logic [3:0] generated_value;
      assign generated_value = LOCAL_VALUE;
      always_comb observed = generated_value + 1;
    end else begin : fallback
      assign observed = 4'd1;
    end
  endgenerate
endmodule
)";
  }
  const auto generated_behavior_vhdl_source =
      directory / "generated_behavior.vhd";
  {
    std::ofstream output(generated_behavior_vhdl_source);
    output << R"(
entity generated_behavior_vhdl is
  generic (
    enabled : boolean := true
  );
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of generated_behavior_vhdl is
begin
  chosen: if enabled generate
    constant base_value : natural := 5;
    constant local_value : natural := base_value + 1;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= local_value;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  else generate
    observed <= 1;
  end generate chosen;
end architecture;

entity generated_range_behavior_vhdl is
  generic (
    mode : integer := 6
  );
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of generated_range_behavior_vhdl is
begin
  selection: case mode generate
    zero: when 0 =>
      observed <= 1;
    selected: when 1 to 2 | 7 downto 5 =>
      observed <= 9;
    empty_choice: when 4 to 3 =>
      observed <= 15;
    fallback: when others =>
      observed <= 3;
  end generate selection;
end architecture;
)";
  }
  const auto generated_static_behavior_sv_source =
      directory / "generated_static_behavior.sv";
  {
    std::ofstream output(generated_static_behavior_sv_source);
    output << R"(
module generated_static_behavior_sv (
  output logic [3:0] observed
);
  generate
    localparam int DIRECT_BASE = 1;
    logic [3:0] direct_value;
    assign direct_value = DIRECT_BASE + 1;
    begin : named_scope
      localparam int NESTED_OFFSET = DIRECT_BASE;
      logic [3:0] nested_value;
      assign nested_value = direct_value + NESTED_OFFSET;
      always_comb observed = nested_value + 1;
    end
  endgenerate
endmodule
)";
  }
  const auto generated_implicit_behavior_sv_source =
      directory / "generated_implicit_behavior.sv";
  {
    std::ofstream output(generated_implicit_behavior_sv_source);
    output << R"(
module generated_implicit_behavior_sv #(
  parameter ENABLED = 1
) (
  output logic [3:0] observed
);
  if (ENABLED) begin : implicit_scope
    localparam int BASE_VALUE = 5;
    parameter int LOCAL_VALUE = BASE_VALUE + 1;
    logic [3:0] generated_value;
    assign generated_value = LOCAL_VALUE;
    always_comb observed = generated_value + 1;
  end
endmodule
)";
  }
  const auto generated_block_behavior_vhdl_source =
      directory / "generated_block_behavior.vhd";
  {
    std::ofstream output(generated_block_behavior_vhdl_source);
    output << R"(
entity generated_block_behavior_vhdl is
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of generated_block_behavior_vhdl is
begin
  static_scope: block is
    constant base_value : natural := 6;
    constant local_value : natural := base_value + 1;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= local_value;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  end block static_scope;
end architecture;
)";
  }
  const auto systemc_source = directory / "model.cpp";
  {
    std::ofstream output(systemc_source);
    output << R"(
#include <systemc>

SC_MODULE(MethodBridge) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  bool passthrough{};

  SC_CTOR(MethodBridge) {
    passthrough =
        fsim::systemc::construction_value<bool>("PASSTHROUGH");
    SC_METHOD(evaluate);
    sensitive << value;
  }

  void evaluate() {
    const auto input = value.read();
    if (passthrough) {
      inverted.write(input);
      return;
    }
    if (input == sc_dt::sc_logic{'0'}) {
      inverted.write(sc_dt::sc_logic{'1'});
    } else if (input == sc_dt::sc_logic{'1'}) {
      inverted.write(sc_dt::sc_logic{'0'});
    } else {
      inverted.write(sc_dt::sc_logic{'Z'});
    }
  }
};

SC_MODULE(NoInitBridge) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};

  SC_CTOR(NoInitBridge) {
    SC_METHOD(evaluate);
    sensitive << value;
    dont_initialize();
  }

  void evaluate() {
    inverted.write(sc_dt::sc_logic{'0'});
  }
};

SC_MODULE(EdgeCounter) {
  sc_core::sc_in<bool> clock{"clock"};
  sc_core::sc_out<sc_dt::sc_uint<8>> count{"count"};
  sc_dt::sc_uint<8> state{};

  SC_CTOR(EdgeCounter) {
    SC_METHOD(tick);
    sensitive << clock.pos();
    dont_initialize();
  }

  void tick() {
    state = state.to_uint64() + 1;
    count.write(state);
  }
};

SC_MODULE(ThrowingMethod) {
  SC_CTOR(ThrowingMethod) {
    SC_METHOD(fail);
  }

  void fail() {
    throw std::runtime_error{"intentional SystemC method failure"};
  }
};

SC_MODULE(DynamicEvents) {
  sc_core::sc_out<sc_dt::sc_uint<8>> count{"count"};
  sc_core::sc_event pulse{"pulse"};
  sc_dt::sc_uint<8> observed{};
  unsigned producer_state{};

  SC_CTOR(DynamicEvents) {
    SC_METHOD(produce);
    SC_METHOD(consume);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      pulse.notify(sc_core::sc_time{5, sc_core::SC_NS});
      pulse.notify(sc_core::sc_time{7, sc_core::SC_NS});
      pulse.notify(sc_core::SC_ZERO_TIME);
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 1) {
      ++producer_state;
      pulse.notify();
      next_trigger(sc_core::sc_time{2, sc_core::SC_NS});
    } else if (producer_state == 2) {
      ++producer_state;
      pulse.notify(sc_core::sc_time{1, sc_core::SC_NS});
      next_trigger(sc_core::sc_time{2, sc_core::SC_NS});
    } else if (producer_state == 3) {
      ++producer_state;
      pulse.notify(sc_core::sc_time{2, sc_core::SC_NS});
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else {
      pulse.cancel();
      pulse.notify(sc_core::sc_time{2, sc_core::SC_NS});
      pulse.notify();
    }
  }

  void consume() {
    if (observed.to_uint64() != 0) {
      count.write(observed);
    }
    observed = observed.to_uint64() + 1;
    next_trigger(pulse);
  }
};

SC_MODULE(EventLists) {
  sc_core::sc_out<sc_dt::sc_uint<8>> or_seen{"or_seen"};
  sc_core::sc_out<sc_dt::sc_uint<8>> and_seen{"and_seen"};
  sc_core::sc_event first{"first"};
  sc_core::sc_event second{"second"};
  sc_core::sc_event third{"third"};
  unsigned producer_state{};
  bool or_initialized{};
  bool and_initialized{};

  SC_CTOR(EventLists) {
    SC_METHOD(produce);
    SC_METHOD(observe_or);
    SC_METHOD(observe_and);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 1) {
      ++producer_state;
      first.notify();
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 2) {
      ++producer_state;
      second.notify();
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else {
      third.notify();
    }
  }

  void observe_or() {
    if (!or_initialized) {
      or_initialized = true;
      next_trigger(first | second);
      return;
    }
    or_seen.write(sc_dt::sc_uint<8>{producer_state - 1});
  }

  void observe_and() {
    if (!and_initialized) {
      and_initialized = true;
      next_trigger(first & second & third);
      return;
    }
    and_seen.write(sc_dt::sc_uint<8>{producer_state});
  }
};

class DeferredChannel final : public sc_core::sc_prim_channel {
 public:
  DeferredChannel(
      const char* name,
      sc_core::sc_out<sc_dt::sc_uint<8>>& value,
      sc_core::sc_out<sc_dt::sc_uint<8>>& updates)
      : sc_core::sc_prim_channel(name),
        value_(value),
        updates_(updates) {}

  void write(const unsigned value) {
    pending_ = value;
    request_update();
  }

 protected:
  void update() override {
    ++update_count_;
    value_.write(sc_dt::sc_uint<8>{pending_});
    updates_.write(sc_dt::sc_uint<8>{update_count_});
    request_update();
  }

 private:
  sc_core::sc_out<sc_dt::sc_uint<8>>& value_;
  sc_core::sc_out<sc_dt::sc_uint<8>>& updates_;
  unsigned pending_{};
  unsigned update_count_{};
};

SC_MODULE(KernelChannels) {
  sc_core::sc_out<sc_dt::sc_uint<8>> value{"value"};
  sc_core::sc_out<sc_dt::sc_uint<8>> updates{"updates"};
  sc_core::sc_out<sc_dt::sc_uint<8>> event_count{"event_count"};
  sc_core::sc_event pulse{"pulse"};
  DeferredChannel deferred;
  unsigned producer_state{};
  unsigned observed_events{};

  SC_CTOR(KernelChannels) : deferred{"deferred", value, updates} {
    SC_METHOD(produce);
    SC_METHOD(consume);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      deferred.write(1);
      deferred.write(2);
      pulse.notify_delayed(
          sc_core::sc_time{2, sc_core::SC_NS});
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else if (producer_state == 1) {
      ++producer_state;
      deferred.write(3);
      next_trigger(sc_core::sc_time{2, sc_core::SC_NS});
    } else {
      deferred.write(4);
    }
  }

  void consume() {
    if (observed_events != 0) {
      event_count.write(
          sc_dt::sc_uint<8>{observed_events});
    }
    ++observed_events;
    next_trigger(pulse);
  }
};

SC_MODULE(InternalSignals) {
  sc_core::sc_out<sc_dt::sc_uint<8>> observed{"observed"};
  sc_core::sc_out<sc_dt::sc_uint<8>> event_count{"event_count"};
  sc_core::sc_out<sc_dt::sc_uint<8>> dynamic_count{
      "dynamic_count"};
  sc_core::sc_signal<sc_dt::sc_uint<8>> internal{
      "internal", sc_dt::sc_uint<8>{5}};
  unsigned producer_state{};
  unsigned static_events{};
  unsigned dynamic_events{};
  bool dynamic_initialized{};

  SC_CTOR(InternalSignals) {
    SC_METHOD(produce);
    SC_METHOD(observe_static);
    sensitive << internal;
    dont_initialize();
    SC_METHOD(observe_dynamic);
  }

  void produce() {
    if (producer_state == 0) {
      ++producer_state;
      if (internal.read().to_uint64() != 5) {
        throw std::runtime_error{
            "SystemC internal signal lost its initial value"};
      }
      internal.write(sc_dt::sc_uint<8>{1});
      internal.write(sc_dt::sc_uint<8>{2});
      if (internal.read().to_uint64() != 5) {
        throw std::runtime_error{
            "SystemC signal write became visible before update"};
      }
      next_trigger(sc_core::sc_time{1, sc_core::SC_NS});
    } else {
      internal.write(sc_dt::sc_uint<8>{3});
    }
  }

  void observe_static() {
    observed.write(internal.read());
    if (internal.event()) {
      ++static_events;
    }
    event_count.write(sc_dt::sc_uint<8>{static_events});
  }

  void observe_dynamic() {
    if (dynamic_initialized) {
      ++dynamic_events;
      dynamic_count.write(
          sc_dt::sc_uint<8>{dynamic_events});
    }
    dynamic_initialized = true;
    next_trigger(internal.value_changed_event());
  }
};

SC_MODULE(BoundPorts) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};

  SC_CTOR(BoundPorts) {
    value(value_channel);
    inverted(inverted_channel);
    SC_METHOD(evaluate);
    sensitive << value_channel;
    dont_initialize();
  }

  void evaluate() {
    const auto input = value.read().to_char();
    inverted.write(
        sc_dt::sc_logic{
            input == '0' ? '1'
            : input == '1' ? '0'
                           : 'X'});
  }
};

SC_MODULE(NativeLeaf) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};

  SC_CTOR(NativeLeaf) {
    SC_METHOD(evaluate);
    sensitive << value;
    dont_initialize();
  }

  void evaluate() {
    const auto input = value.read().to_char();
    inverted.write(
        sc_dt::sc_logic{
            input == '0' ? '1'
            : input == '1' ? '0'
                           : 'X'});
  }
};

SC_MODULE(NativeHierarchy) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};
  NativeLeaf leaf;

  SC_CTOR(NativeHierarchy)
      : leaf{"leaf"} {
    value(value_channel);
    inverted(inverted_channel);
    leaf.value(value_channel);
    leaf.inverted(inverted_channel);
  }
};

SC_MODULE(DuplicateNativeHierarchy) {
  NativeLeaf first;
  NativeLeaf second;

  SC_CTOR(DuplicateNativeHierarchy)
      : first{"duplicate"},
        second{"duplicate"} {}
};

SC_MODULE(PortChainHierarchy) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  NativeLeaf leaf;

  SC_CTOR(PortChainHierarchy)
      : leaf{"leaf"} {
    leaf.value(value);
    leaf.inverted(inverted);
  }
};

SC_MODULE(ExportHierarchy) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  sc_core::sc_signal<sc_dt::sc_logic> value_channel{
      "value_channel", sc_dt::sc_logic{'0'}};
  sc_core::sc_signal<sc_dt::sc_logic> inverted_channel{
      "inverted_channel", sc_dt::sc_logic{'1'}};
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> value_endpoint{
          "value_endpoint"};
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> value_export{
          "value_export"};
  sc_core::sc_export<
      sc_core::sc_signal_inout_if<sc_dt::sc_logic>>
      inverted_endpoint{
          "inverted_endpoint"};
  sc_core::sc_export<
      sc_core::sc_signal_inout_if<sc_dt::sc_logic>>
      inverted_export{
          "inverted_export"};
  NativeLeaf leaf;

  SC_CTOR(ExportHierarchy)
      : leaf{"leaf"} {
    value(value_channel);
    inverted(inverted_channel);
    value_endpoint(value_channel);
    value_export(value_endpoint);
    inverted_endpoint(inverted_channel);
    inverted_export(inverted_endpoint);
    leaf.value(value_export);
    leaf.inverted(inverted_export);
  }
};

SC_MODULE(UnboundExport) {
  sc_core::sc_export<
      sc_core::sc_signal_in_if<sc_dt::sc_logic>> dangling{
          "dangling"};

  SC_CTOR(UnboundExport) {}
};

SC_MODULE(DeepMiddle) {
  NativeLeaf leaf;

  SC_CTOR(DeepMiddle)
      : leaf{"leaf"} {}
};

SC_MODULE(InvalidDeepBinding) {
  sc_core::sc_signal<sc_dt::sc_logic> root_signal{
      "root_signal", sc_dt::sc_logic{'0'}};
  DeepMiddle middle;

  SC_CTOR(InvalidDeepBinding)
      : middle{"middle"} {
    middle.leaf.value(root_signal);
  }
};

struct LifecycleLeaf : sc_core::sc_module {
  explicit LifecycleLeaf(
      sc_core::sc_module_name name,
      std::vector<int>& events)
      : sc_core::sc_module(name),
        events_(events) {}

 protected:
  void before_end_of_elaboration() override {
    require_size(1);
    events_.push_back(2);
  }

  void end_of_elaboration() override {
    require_size(3);
    events_.push_back(4);
  }

  void start_of_simulation() override {
    require_size(5);
    events_.push_back(6);
  }

  void end_of_simulation() override {
    require_size(6);
    events_.push_back(7);
  }

 private:
  void require_size(const std::size_t expected) const {
    if (events_.size() != expected) {
      throw std::runtime_error{
          "native SystemC lifecycle order mismatch"};
    }
  }

  std::vector<int>& events_;
};

SC_MODULE(LifecycleModule) {
  std::vector<int> lifecycle_events;
  sc_core::sc_in<sc_dt::sc_logic> trigger{"trigger"};
  sc_core::sc_out<sc_dt::sc_logic> ready{"ready"};
  LifecycleLeaf leaf;

  SC_CTOR(LifecycleModule)
      : leaf{"leaf", lifecycle_events} {
    SC_METHOD(observe);
    sensitive << trigger;
    dont_initialize();
  }

  void observe() {
    ready.write(
        sc_dt::sc_logic{
            lifecycle_events.size() == 6 ? '1' : '0'});
  }

 protected:
  void before_end_of_elaboration() override {
    require_size(0);
    lifecycle_events.push_back(1);
  }

  void end_of_elaboration() override {
    require_size(2);
    lifecycle_events.push_back(3);
  }

  void start_of_simulation() override {
    require_size(4);
    lifecycle_events.push_back(5);
  }

  void end_of_simulation() override {
    require_size(7);
    lifecycle_events.push_back(8);
  }

 private:
  void require_size(const std::size_t expected) const {
    if (lifecycle_events.size() != expected) {
      throw std::runtime_error{
          "root SystemC lifecycle order mismatch"};
    }
  }
};

SC_MODULE(ThrowingLifecycle) {
  SC_CTOR(ThrowingLifecycle) {}

 protected:
  void before_end_of_elaboration() override {
    throw std::runtime_error{
        "intentional lifecycle elaboration failure"};
  }
};

SC_MODULE(ThrowingEndLifecycle) {
  SC_CTOR(ThrowingEndLifecycle) {}

 protected:
  void end_of_simulation() override {
    throw std::runtime_error{
        "intentional lifecycle terminal failure"};
  }
};

namespace {
void* create_model(void*, const char*, fsim_sc_handle_v1) {
  return reinterpret_cast<void*>(0x1);
}
void destroy_model(void*, void*) {}
}

SC_MODULE(HdlBridge) {
  sc_core::sc_in<sc_dt::sc_logic> value{"value"};
  sc_core::sc_out<sc_dt::sc_logic> inverted{"inverted"};
  fsim::systemc::hdl_instance u_hdl{"u_hdl"};

  SC_CTOR(HdlBridge) {
    u_hdl.set_actual(
        "INVERT",
        fsim::systemc::construction_value<int>("CHILD_INVERT"));
    u_hdl.bind_input("value", value);
    u_hdl.bind_output("inverted", inverted);
  }
};

SC_MODULE(FiberThreads) {
  sc_core::sc_in<sc_dt::sc_logic> clock{"clock"};
  sc_core::sc_out<sc_dt::sc_lv<8>> count{"count"};
  sc_core::sc_out<sc_dt::sc_lv<8>> timed{"timed"};
  sc_core::sc_out<sc_dt::sc_lv<8>> event_count{"event_count"};
  sc_core::sc_event pulse{"pulse"};

  SC_CTOR(FiberThreads) {
    SC_CTHREAD(clocked_run, clock.pos());
    SC_THREAD(timed_run);
    SC_METHOD(notify_run);
    sensitive << clock.pos();
    dont_initialize();
    SC_THREAD(event_run);
  }

  void clocked_run() {
    unsigned value = 0;
    while (true) {
      sc_core::wait();
      ++value;
      count.write(
          value == 1
              ? sc_dt::sc_lv<8>{"00000001"}
              : sc_dt::sc_lv<8>{"00000010"});
    }
  }

  void timed_run() {
    timed.write(sc_dt::sc_lv<8>{"00000001"});
    sc_core::wait(sc_core::sc_time{2, sc_core::SC_NS});
    timed.write(sc_dt::sc_lv<8>{"00000010"});
    sc_core::wait(sc_core::SC_ZERO_TIME);
    timed.write(sc_dt::sc_lv<8>{"00000011"});
  }

  void notify_run() {
    pulse.notify();
  }

  void event_run() {
    sc_core::wait(pulse);
    event_count.write(sc_dt::sc_lv<8>{"00000001"});
    sc_core::wait(pulse);
    event_count.write(sc_dt::sc_lv<8>{"00000010"});
  }
};

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || host->struct_size < sizeof(fsim_sc_host_v1)
      || registrar->struct_size < sizeof(fsim_sc_registrar_v1)
      || registrar->register_factory == nullptr
      || registrar->register_elaboration_factory == nullptr) {
    return FSIM_SC_ABI_MISMATCH;
  }
  const auto status = registrar->register_factory(
      registrar->context,
      "model",
      create_model,
      destroy_model,
      nullptr);
  if (status != FSIM_SC_OK) {
    return status;
  }
  constexpr std::array<fsim::systemc::factory_parameter, 1>
      bridge_parameters{{
          {"CHILD_INVERT",
           FSIM_SC_CONSTRUCTION_BOOLEAN,
           true,
           0},
      }};
  const auto bridge_status =
      fsim::systemc::register_module_factory<HdlBridge>(
          host, registrar, "bridge", bridge_parameters);
  if (bridge_status != FSIM_SC_OK) {
    return bridge_status;
  }
  const auto thread_status =
      fsim::systemc::register_module_factory<FiberThreads>(
          host, registrar, "fiber_threads");
  if (thread_status != FSIM_SC_OK) {
    return thread_status;
  }
  constexpr std::array<fsim::systemc::factory_parameter, 1>
      method_parameters{{
          {"PASSTHROUGH",
           FSIM_SC_CONSTRUCTION_BOOLEAN,
           true,
           0},
      }};
  const auto method_status =
      fsim::systemc::register_module_factory<MethodBridge>(
          host, registrar, "method_bridge", method_parameters);
  if (method_status != FSIM_SC_OK) {
    return method_status;
  }
  const auto noinit_status =
      fsim::systemc::register_module_factory<NoInitBridge>(
      host, registrar, "noinit_bridge");
  if (noinit_status != FSIM_SC_OK) {
    return noinit_status;
  }
  const auto edge_status =
      fsim::systemc::register_module_factory<EdgeCounter>(
          host, registrar, "edge_counter");
  if (edge_status != FSIM_SC_OK) {
    return edge_status;
  }
  const auto throwing_status =
      fsim::systemc::register_module_factory<ThrowingMethod>(
      host, registrar, "throwing_method");
  if (throwing_status != FSIM_SC_OK) {
    return throwing_status;
  }
  const auto dynamic_status =
      fsim::systemc::register_module_factory<DynamicEvents>(
          host, registrar, "dynamic_events");
  if (dynamic_status != FSIM_SC_OK) {
    return dynamic_status;
  }
  const auto event_list_status =
      fsim::systemc::register_module_factory<EventLists>(
          host, registrar, "event_lists");
  if (event_list_status != FSIM_SC_OK) {
    return event_list_status;
  }
  const auto kernel_channel_status =
      fsim::systemc::register_module_factory<KernelChannels>(
          host, registrar, "kernel_channels");
  if (kernel_channel_status != FSIM_SC_OK) {
    return kernel_channel_status;
  }
  const auto internal_signal_status =
      fsim::systemc::register_module_factory<InternalSignals>(
          host, registrar, "internal_signals");
  if (internal_signal_status != FSIM_SC_OK) {
    return internal_signal_status;
  }
  const auto bound_port_status =
      fsim::systemc::register_module_factory<BoundPorts>(
          host, registrar, "bound_ports");
  if (bound_port_status != FSIM_SC_OK) {
    return bound_port_status;
  }
  const auto native_hierarchy_status =
      fsim::systemc::register_module_factory<NativeHierarchy>(
          host, registrar, "native_hierarchy");
  if (native_hierarchy_status != FSIM_SC_OK) {
    return native_hierarchy_status;
  }
  const auto duplicate_status =
      fsim::systemc::register_module_factory<
      DuplicateNativeHierarchy>(
          host, registrar, "duplicate_native_hierarchy");
  if (duplicate_status != FSIM_SC_OK) {
    return duplicate_status;
  }
  const auto lifecycle_status =
      fsim::systemc::register_module_factory<LifecycleModule>(
          host, registrar, "lifecycle_module");
  if (lifecycle_status != FSIM_SC_OK) {
    return lifecycle_status;
  }
  const auto throwing_lifecycle_status =
      fsim::systemc::register_module_factory<ThrowingLifecycle>(
          host, registrar, "throwing_lifecycle");
  if (throwing_lifecycle_status != FSIM_SC_OK) {
    return throwing_lifecycle_status;
  }
  const auto throwing_end_status =
      fsim::systemc::register_module_factory<
          ThrowingEndLifecycle>(
              host, registrar, "throwing_end_lifecycle");
  if (throwing_end_status != FSIM_SC_OK) {
    return throwing_end_status;
  }
  const auto port_chain_status =
      fsim::systemc::register_module_factory<PortChainHierarchy>(
          host, registrar, "port_chain_hierarchy");
  if (port_chain_status != FSIM_SC_OK) {
    return port_chain_status;
  }
  const auto export_status =
      fsim::systemc::register_module_factory<ExportHierarchy>(
          host, registrar, "export_hierarchy");
  if (export_status != FSIM_SC_OK) {
    return export_status;
  }
  const auto unbound_export_status =
      fsim::systemc::register_module_factory<UnboundExport>(
          host, registrar, "unbound_export");
  if (unbound_export_status != FSIM_SC_OK) {
    return unbound_export_status;
  }
  return fsim::systemc::register_module_factory<
      InvalidDeepBinding>(
          host, registrar, "invalid_deep_binding");
}
)";
  }

  const auto systemc_boundary_source =
      directory / "systemc_boundary.sv";
  {
    std::ofstream output(systemc_boundary_source);
    output << R"(
module systemc_hdl_child #(
  parameter INVERT = 1
) (
  input logic value,
  output logic inverted
);
  assign inverted = INVERT ? ~value : value;
endmodule

module systemc_host #(
  parameter CHILD_MODE = 1
);
  logic value;
  logic inverted;
  bridge_placeholder #(
      .CHILD_INVERT(CHILD_MODE)
  ) u_bridge(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_method_host;
  logic value;
  logic inverted;
  logic observed;
  method_bridge_placeholder u_method(
      .value(value),
      .inverted(inverted));
  always_comb observed = inverted;
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_edge_host;
  bit clock;
  logic [7:0] count;
  edge_counter_placeholder u_counter(
      .clock(clock),
      .count(count));
  initial begin
    clock = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_bound_port_host;
  logic value;
  logic inverted;
  bound_ports_placeholder u_bound(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_native_hierarchy_host;
  logic value;
  logic inverted;
  native_hierarchy_placeholder u_native(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_lifecycle_host;
  logic trigger;
  logic ready;
  lifecycle_module_placeholder u_lifecycle(
      .trigger(trigger),
      .ready(ready));
  initial begin
    trigger = 1'b0;
    #1 trigger = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_port_chain_host;
  logic value;
  logic inverted;
  port_chain_hierarchy_placeholder u_chain(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_export_host;
  logic value;
  logic inverted;
  export_hierarchy_placeholder u_export(
      .value(value),
      .inverted(inverted));
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule

module systemc_thread_host;
  logic clock;
  logic [7:0] count;
  logic [7:0] timed;
  logic [7:0] event_count;
  fiber_threads_placeholder u_threads(
      .clock(clock),
      .count(count),
      .timed(timed),
      .event_count(event_count));
  initial begin
    clock = 1'b0;
    #1 clock = 1'b1;
    #1 clock = 1'b0;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto systemc_method_vhdl_source =
      directory / "systemc_method.vhd";
  {
    std::ofstream output(systemc_method_vhdl_source);
    output << R"(
entity systemc_method_vhdl_host is
end entity systemc_method_vhdl_host;

architecture rtl of systemc_method_vhdl_host is
  signal value : std_logic;
  signal inverted : std_logic;
begin
  value <= '1';
  u_method: method_bridge_placeholder
    generic map (passthrough => true)
    port map (value => value, inverted => inverted);
end architecture rtl;
)";
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "application-test";
  config.project.top = "sv:work.tb";
  config.project.time_resolution = "1ns";
  config.build.cache_path = directory / "cache";
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  fsim::project::SourceSet systemc_sources;
  systemc_sources.language = fsim::project::Language::systemc;
  systemc_sources.standard = "2023-subset";
  systemc_sources.files.push_back(systemc_source);
  systemc_sources.include_directories.emplace_back(
      std::filesystem::path{FSIM_TEST_SOURCE_DIR} / "include");
  config.source_sets.push_back(std::move(systemc_sources));

  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  assert(checked->source_count == 2);
  assert(checked->hdl_sources.size() == 1);
  assert(checked->hdl_sources.front().path == source);
  assert(checked->hdl_sources.front().content_digest.size() == 64);
  assert(
      checked->hdl_sources.front().compilation_unit_digest.size()
      == 64);
  assert(checked->parsed.units.size() == 2);

  auto first = fsim::app::build_project(config, diagnostics);
  if (!first) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
      for (const auto& note : diagnostic.notes) {
        std::cerr << note.message << '\n';
      }
    }
  }
  assert(first);
  assert(first->systemc_plugins.size() == 1);
  assert(!first->cache_hit);
  auto second = fsim::app::build_project(config, diagnostics);
  assert(second);
  assert(second->cache_hit);
  assert(first->systemc_hierarchy == second->systemc_hierarchy);
  const auto child_q = first->design.find_signal("tb.u_child.value");
  assert(child_q);
  const auto paths = first->design.signal_paths();
  assert(std::find_if(
             paths.begin(), paths.end(),
             [](const auto& path) {
               return path.first == "tb.u_child.value";
         })
         != paths.end());

  auto hdl_systemc_config = config;
  hdl_systemc_config.project.top = "sv:work.systemc_host";
  hdl_systemc_config.source_sets.front().files = {
      systemc_boundary_source};
  hdl_systemc_config.bindings = {
      {"systemc_host.u_bridge",
       "systemc:models.bridge",
       std::nullopt},
      {"systemc_host.u_bridge.u_hdl",
       "sv:work.systemc_hdl_child",
       std::nullopt},
  };
  fsim::diagnostic::Engine hdl_systemc_diagnostics;
  auto hdl_systemc_project = fsim::app::build_project(
      hdl_systemc_config, hdl_systemc_diagnostics);
  assert(hdl_systemc_project);
  assert(hdl_systemc_project->systemc_hierarchy);
  assert(
      hdl_systemc_project->design.systemc_instances().size()
      == 1);
  assert((
      hdl_systemc_project->systemc_hierarchy
          ->factory_parameters("bridge")
          ->front()
          .default_value
      == 0));
  const auto hdl_systemc_value =
      hdl_systemc_project->design.find_signal("value");
  const auto hdl_systemc_child_value =
      hdl_systemc_project->design.find_signal(
          "systemc_host.u_bridge.u_hdl.value");
  const auto hdl_systemc_inverted =
      hdl_systemc_project->design.find_signal("inverted");
  assert(
      hdl_systemc_value && hdl_systemc_child_value
      && hdl_systemc_inverted);
  assert(*hdl_systemc_value == *hdl_systemc_child_value);
  auto hdl_systemc_interpreter =
      hdl_systemc_project->design.create_interpreter();
  const auto hdl_systemc_result =
      hdl_systemc_interpreter->run();
  assert(
      hdl_systemc_result.status
      == fsim::runtime::RunStatus::stopped);
  assert(
      hdl_systemc_interpreter
          ->signal_value(*hdl_systemc_inverted)
          .to_msb_string()
      == "0");
  assert((
      hdl_systemc_project->design.systemc_instances().front()
          .construction_values
      == std::vector<std::pair<std::string, std::int64_t>>{
          {"CHILD_INVERT", 1}}));
  const auto hdl_systemc_child_specialization =
      std::find_if(
          hdl_systemc_project->design.specializations().begin(),
          hdl_systemc_project->design.specializations().end(),
          [](const auto& specialization) {
            return specialization.instance
                == "systemc_host.u_bridge.u_hdl";
          });
  assert(
      hdl_systemc_child_specialization
      != hdl_systemc_project->design.specializations().end());
  assert((
      hdl_systemc_child_specialization->parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"INVERT", "1"}}));
  const auto run_compiled_systemc_actual =
      [&]() {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            hdl_systemc_config, run_diagnostics);
        assert(project);
        assert((
            project->design.systemc_instances().front()
                .construction_values
            == std::vector<
                std::pair<std::string, std::int64_t>>{
                {"CHILD_INVERT", 1}}));
        const auto output =
            project->design.find_signal("inverted");
        assert(output);
        fsim::app::Simulation simulation{
            std::move(*project),
            hdl_systemc_config.run.max_deltas,
            fsim::app::SimulationEngine::compiled};
        const auto result = simulation.run();
        assert(
            result.status == fsim::runtime::RunStatus::stopped);
        assert(
            simulation.read_signal(*output).to_msb_string()
            == "0");
        return std::pair{
            simulation.native_cache_statistics(),
            simulation.compiled_process_count()};
      };
  const auto [systemc_actual_cold_cache,
              systemc_actual_cold_processes] =
      run_compiled_systemc_actual();
  const auto [systemc_actual_warm_cache,
              systemc_actual_warm_processes] =
      run_compiled_systemc_actual();
  assert(
      systemc_actual_warm_processes
      == systemc_actual_cold_processes);
#if defined(FSIM_HAS_LLVM)
  assert(systemc_actual_cold_processes > 0);
  assert(systemc_actual_cold_cache.misses > 0);
  assert(systemc_actual_cold_cache.stores > 0);
  assert(systemc_actual_warm_cache.hits > 0);
  assert(systemc_actual_warm_cache.misses == 0);
#else
  assert(systemc_actual_cold_processes == 0);
  assert(
      systemc_actual_cold_cache
      == fsim::app::NativeCacheStatistics{});
  assert(
      systemc_actual_warm_cache
      == fsim::app::NativeCacheStatistics{});
#endif

  auto bound_port_config = hdl_systemc_config;
  bound_port_config.project.top =
      "sv:work.systemc_bound_port_host";
  bound_port_config.bindings = {
      {"systemc_bound_port_host.u_bound",
       "systemc:models.bound_ports",
       std::nullopt},
  };
  fsim::diagnostic::Engine bound_port_diagnostics;
  auto bound_port_reference = fsim::app::build_project(
      bound_port_config, bound_port_diagnostics);
  auto bound_port_compiled = fsim::app::build_project(
      bound_port_config, bound_port_diagnostics);
  assert(bound_port_reference);
  assert(bound_port_compiled);
  const auto bound_input =
      bound_port_reference->design.find_signal("value");
  const auto bound_input_channel =
      bound_port_reference->design.find_signal(
          "systemc_bound_port_host.u_bound.value_channel");
  const auto bound_output =
      bound_port_reference->design.find_signal("inverted");
  const auto bound_output_channel =
      bound_port_reference->design.find_signal(
          "systemc_bound_port_host.u_bound.inverted_channel");
  assert(
      bound_input && bound_input_channel
      && bound_output && bound_output_channel);
  assert(*bound_input == *bound_input_channel);
  assert(*bound_output == *bound_output_channel);
  const auto& bound_instance =
      bound_port_reference->design.systemc_instances().front();
  assert(bound_instance.ports.size() == 2);
  assert(bound_instance.internal_signals.size() == 2);
  assert(
      bound_instance.ports[0].signal
      == bound_instance.internal_signals[0].signal);
  assert(
      bound_instance.ports[1].signal
      == bound_instance.internal_signals[1].signal);
  const auto run_bound_ports =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto output =
            project.design.find_signal("inverted");
        assert(output);
        fsim::app::Simulation simulation{
            std::move(project),
            bound_port_config.run.max_deltas,
            engine};
        assert(
            simulation.read_signal(*output).to_msb_string()
            == "1");
        const auto result = simulation.run();
        return std::pair{
            result,
            simulation.read_signal(*output).to_msb_string()};
      };
  const auto bound_reference = run_bound_ports(
      std::move(*bound_port_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto bound_compiled = run_bound_ports(
      std::move(*bound_port_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(
      bound_reference.first.status
      == fsim::runtime::RunStatus::stopped);
  assert(bound_reference.first.time == 2);
  assert(bound_reference.second == "0");
  assert(
      bound_reference.first.status
      == bound_compiled.first.status);
  assert(bound_reference.first.time == bound_compiled.first.time);
  assert(bound_reference.second == bound_compiled.second);

  auto native_hierarchy_config = hdl_systemc_config;
  native_hierarchy_config.project.top =
      "sv:work.systemc_native_hierarchy_host";
  native_hierarchy_config.bindings = {
      {"systemc_native_hierarchy_host.u_native",
       "systemc:models.native_hierarchy",
       std::nullopt},
  };
  fsim::diagnostic::Engine native_hierarchy_diagnostics;
  auto native_hierarchy_reference = fsim::app::build_project(
      native_hierarchy_config, native_hierarchy_diagnostics);
  auto native_hierarchy_compiled = fsim::app::build_project(
      native_hierarchy_config, native_hierarchy_diagnostics);
  assert(native_hierarchy_reference);
  assert(native_hierarchy_compiled);
  assert(
      native_hierarchy_reference->design.systemc_instances().size()
      == 2);
  const auto native_parent = std::find_if(
      native_hierarchy_reference->design.systemc_instances().begin(),
      native_hierarchy_reference->design.systemc_instances().end(),
      [](const fsim::elaboration::SystemCInstanceInfo& instance) {
        return instance.instance
            == "systemc_native_hierarchy_host.u_native";
      });
  const auto native_leaf = std::find_if(
      native_hierarchy_reference->design.systemc_instances().begin(),
      native_hierarchy_reference->design.systemc_instances().end(),
      [](const fsim::elaboration::SystemCInstanceInfo& instance) {
        return instance.instance
            == "systemc_native_hierarchy_host.u_native.leaf";
      });
  assert(
      native_parent
      != native_hierarchy_reference->design.systemc_instances().end());
  assert(
      native_leaf
      != native_hierarchy_reference->design.systemc_instances().end());
  assert(native_parent->ports.size() == 2);
  assert(native_parent->internal_signals.size() == 2);
  assert(native_leaf->ports.size() == 2);
  assert(
      native_leaf->ports[0].signal
      == native_parent->internal_signals[0].signal);
  assert(
      native_leaf->ports[1].signal
      == native_parent->internal_signals[1].signal);
  assert(std::any_of(
      native_hierarchy_reference->design.processes().begin(),
      native_hierarchy_reference->design.processes().end(),
      [](const fsim::runtime::simir::Process& process) {
        return process.name
            == "systemc_native_hierarchy_host.u_native.leaf.evaluate";
      }));
  const auto run_native_hierarchy =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto output =
            project.design.find_signal("inverted");
        assert(output);
        fsim::app::Simulation simulation{
            std::move(project),
            native_hierarchy_config.run.max_deltas,
            engine};
        assert(
            simulation.read_signal(*output).to_msb_string()
            == "1");
        const auto result = simulation.run();
        return std::pair{
            result,
            simulation.read_signal(*output).to_msb_string()};
      };
  const auto native_reference = run_native_hierarchy(
      std::move(*native_hierarchy_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto native_compiled = run_native_hierarchy(
      std::move(*native_hierarchy_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(
      native_reference.first.status
      == fsim::runtime::RunStatus::stopped);
  assert(native_reference.first.time == 2);
  assert(native_reference.second == "0");
  assert(
      native_reference.first.status
      == native_compiled.first.status);
  assert(native_reference.first.time == native_compiled.first.time);
  assert(native_reference.second == native_compiled.second);

  auto lifecycle_config = hdl_systemc_config;
  lifecycle_config.project.top =
      "sv:work.systemc_lifecycle_host";
  lifecycle_config.bindings = {
      {"systemc_lifecycle_host.u_lifecycle",
       "systemc:models.lifecycle_module",
       std::nullopt},
  };
  fsim::diagnostic::Engine lifecycle_diagnostics;
  auto lifecycle_reference_project = fsim::app::build_project(
      lifecycle_config, lifecycle_diagnostics);
  auto lifecycle_compiled_project = fsim::app::build_project(
      lifecycle_config, lifecycle_diagnostics);
  assert(lifecycle_reference_project);
  assert(lifecycle_compiled_project);
  assert(lifecycle_reference_project->systemc_roots.size() == 1);
  assert(lifecycle_compiled_project->systemc_roots.size() == 1);
  const auto run_lifecycle =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto ready = project.design.find_signal("ready");
        assert(ready);
        fsim::app::Simulation simulation{
            std::move(project),
            lifecycle_config.run.max_deltas,
            engine};
        const auto result = simulation.run();
        return std::pair{
            result,
            simulation.read_signal(*ready).to_msb_string()};
      };
  const auto lifecycle_reference = run_lifecycle(
      std::move(*lifecycle_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto lifecycle_compiled = run_lifecycle(
      std::move(*lifecycle_compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(
      lifecycle_reference.first.status
      == fsim::runtime::RunStatus::stopped);
  assert(lifecycle_reference.first.time == 2);
  assert(lifecycle_reference.second == "1");
  assert(
      lifecycle_reference.first.status
      == lifecycle_compiled.first.status);
  assert(
      lifecycle_reference.first.time
      == lifecycle_compiled.first.time);
  assert(lifecycle_reference.second == lifecycle_compiled.second);

  const auto exercise_native_binding =
      [&](const std::string& top,
          const std::string& instance_path,
          const std::string& target,
          const bool through_internal_signals) {
        auto binding_config = hdl_systemc_config;
        binding_config.project.top = "sv:work." + top;
        binding_config.bindings = {
            {instance_path, target, std::nullopt},
        };
        fsim::diagnostic::Engine binding_diagnostics;
        auto reference_project = fsim::app::build_project(
            binding_config, binding_diagnostics);
        auto compiled_project = fsim::app::build_project(
            binding_config, binding_diagnostics);
        assert(reference_project);
        assert(compiled_project);
        const auto& instances =
            reference_project->design.systemc_instances();
        const auto parent = std::find_if(
            instances.begin(),
            instances.end(),
            [&](const fsim::elaboration::SystemCInstanceInfo& instance) {
              return instance.instance == instance_path;
            });
        const auto leaf = std::find_if(
            instances.begin(),
            instances.end(),
            [&](const fsim::elaboration::SystemCInstanceInfo& instance) {
              return instance.instance == instance_path + ".leaf";
            });
        assert(parent != instances.end());
        assert(leaf != instances.end());
        assert(parent->ports.size() == 2);
        assert(leaf->ports.size() == 2);
        if (through_internal_signals) {
          assert(parent->internal_signals.size() == 2);
          assert(parent->exports.size() == 4);
          assert(
              parent->exports[0].signal
              == parent->internal_signals[0].signal);
          assert(
              parent->exports[1].signal
              == parent->internal_signals[0].signal);
          assert(
              parent->exports[2].signal
              == parent->internal_signals[1].signal);
          assert(
              parent->exports[3].signal
              == parent->internal_signals[1].signal);
          const auto exported_input =
              reference_project->design.find_signal(
                  instance_path + ".value_export");
          const auto exported_output =
              reference_project->design.find_signal(
                  instance_path + ".inverted_export");
          assert(exported_input && exported_output);
          assert(
              *exported_input
              == parent->internal_signals[0].signal);
          assert(
              *exported_output
              == parent->internal_signals[1].signal);
          assert(
              leaf->ports[0].signal
              == parent->internal_signals[0].signal);
          assert(
              leaf->ports[1].signal
              == parent->internal_signals[1].signal);
        } else {
          assert(
              leaf->ports[0].signal
              == parent->ports[0].signal);
          assert(
              leaf->ports[1].signal
              == parent->ports[1].signal);
        }
        const auto run_binding =
            [&](fsim::app::BuiltProject project,
                const fsim::app::SimulationEngine engine) {
              const auto output =
                  project.design.find_signal("inverted");
              assert(output);
              fsim::app::Simulation simulation{
                  std::move(project),
                  binding_config.run.max_deltas,
                  engine};
              const auto result = simulation.run();
              return std::pair{
                  result,
                  simulation.read_signal(*output).to_msb_string()};
            };
        const auto reference = run_binding(
            std::move(*reference_project),
            fsim::app::SimulationEngine::interpreter);
        const auto compiled = run_binding(
            std::move(*compiled_project),
            fsim::app::SimulationEngine::compiled);
        assert(
            reference.first.status
            == fsim::runtime::RunStatus::stopped);
        assert(reference.first.time == 2);
        assert(reference.second == "0");
        assert(reference.first.status == compiled.first.status);
        assert(reference.first.time == compiled.first.time);
        assert(reference.second == compiled.second);
      };
  exercise_native_binding(
      "systemc_port_chain_host",
      "systemc_port_chain_host.u_chain",
      "systemc:models.port_chain_hierarchy",
      false);
  exercise_native_binding(
      "systemc_export_host",
      "systemc_export_host.u_export",
      "systemc:models.export_hierarchy",
      true);

  auto duplicate_native_config = hdl_systemc_config;
  duplicate_native_config.project.top =
      "systemc:models.duplicate_native_hierarchy";
  duplicate_native_config.bindings.clear();
  fsim::diagnostic::Engine duplicate_native_diagnostics;
  assert(!fsim::app::build_project(
      duplicate_native_config, duplicate_native_diagnostics));
  assert(std::any_of(
      duplicate_native_diagnostics.diagnostics().begin(),
      duplicate_native_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-SC-A004";
      }));

  auto throwing_lifecycle_config = hdl_systemc_config;
  throwing_lifecycle_config.project.top =
      "systemc:models.throwing_lifecycle";
  throwing_lifecycle_config.bindings.clear();
  fsim::diagnostic::Engine throwing_lifecycle_diagnostics;
  assert(!fsim::app::build_project(
      throwing_lifecycle_config,
      throwing_lifecycle_diagnostics));
  assert(std::any_of(
      throwing_lifecycle_diagnostics.diagnostics().begin(),
      throwing_lifecycle_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-SC-A008";
      }));

  auto throwing_end_config = hdl_systemc_config;
  throwing_end_config.project.top =
      "systemc:models.throwing_end_lifecycle";
  throwing_end_config.bindings.clear();
  fsim::diagnostic::Engine throwing_end_diagnostics;
  auto throwing_end_project = fsim::app::build_project(
      throwing_end_config, throwing_end_diagnostics);
  assert(throwing_end_project);
  fsim::app::Simulation throwing_end_simulation{
      std::move(*throwing_end_project),
      throwing_end_config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  bool rejected_end_lifecycle = false;
  try {
    (void)throwing_end_simulation.run();
  } catch (const std::runtime_error&) {
    rejected_end_lifecycle = true;
  }
  assert(rejected_end_lifecycle);
  assert(throwing_end_simulation.poisoned());

  auto unbound_export_config = hdl_systemc_config;
  unbound_export_config.project.top =
      "systemc:models.unbound_export";
  unbound_export_config.bindings.clear();
  fsim::diagnostic::Engine unbound_export_diagnostics;
  assert(!fsim::app::build_project(
      unbound_export_config, unbound_export_diagnostics));
  assert(std::any_of(
      unbound_export_diagnostics.diagnostics().begin(),
      unbound_export_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-BIND-048";
      }));

  auto invalid_deep_binding_config = hdl_systemc_config;
  invalid_deep_binding_config.project.top =
      "systemc:models.invalid_deep_binding";
  invalid_deep_binding_config.bindings.clear();
  fsim::diagnostic::Engine invalid_deep_binding_diagnostics;
  assert(!fsim::app::build_project(
      invalid_deep_binding_config,
      invalid_deep_binding_diagnostics));
  assert(std::any_of(
      invalid_deep_binding_diagnostics.diagnostics().begin(),
      invalid_deep_binding_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-SC-A004";
      }));

  auto legacy_systemc_config = hdl_systemc_config;
  legacy_systemc_config.bindings.front().target =
      "systemc:models.model";
  fsim::diagnostic::Engine legacy_systemc_diagnostics;
  assert(!fsim::app::build_project(
      legacy_systemc_config, legacy_systemc_diagnostics));
  assert(std::any_of(
      legacy_systemc_diagnostics.diagnostics().begin(),
      legacy_systemc_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-SC-A003";
      }));

  auto systemc_hdl_config = hdl_systemc_config;
  systemc_hdl_config.project.top = "systemc:models.bridge";
  systemc_hdl_config.bindings = {
      {"bridge.u_hdl",
       "sv:work.systemc_hdl_child",
       std::nullopt},
  };
  fsim::diagnostic::Engine systemc_hdl_diagnostics;
  auto systemc_hdl_project = fsim::app::build_project(
      systemc_hdl_config, systemc_hdl_diagnostics);
  assert(systemc_hdl_project);
  assert(systemc_hdl_project->systemc_hierarchy);
  assert(
      systemc_hdl_project->design.systemc_instances().size()
      == 1);
  assert(
      systemc_hdl_project->design.systemc_instances().front().instance
      == "bridge");
  const auto systemc_root_value =
      systemc_hdl_project->design.find_signal("bridge.value");
  const auto systemc_root_inverted =
      systemc_hdl_project->design.find_signal("bridge.inverted");
  const auto systemc_root_child_inverted =
      systemc_hdl_project->design.find_signal(
          "bridge.u_hdl.inverted");
  assert(
      systemc_root_value && systemc_root_inverted
      && systemc_root_child_inverted);
  assert(*systemc_root_inverted == *systemc_root_child_inverted);
  auto systemc_hdl_interpreter =
      systemc_hdl_project->design.create_interpreter();
  systemc_hdl_interpreter->deposit_signal(
      *systemc_root_value,
      fsim::runtime::PackedLogic4::from_msb_string("1"));
  const auto systemc_hdl_result =
      systemc_hdl_interpreter->run();
  assert(
      systemc_hdl_result.status
      == fsim::runtime::RunStatus::completed);
  assert(
      systemc_hdl_interpreter
          ->signal_value(*systemc_root_inverted)
          .to_msb_string()
      == "1");

#if defined(FSIM_HAS_BOOST_CONTEXT)
  auto systemc_thread_config = hdl_systemc_config;
  systemc_thread_config.project.top =
      "sv:work.systemc_thread_host";
  systemc_thread_config.bindings = {
      {"systemc_thread_host.u_threads",
       "systemc:models.fiber_threads",
       std::nullopt},
  };
  fsim::diagnostic::Engine systemc_thread_diagnostics;
  auto systemc_thread_project = fsim::app::build_project(
      systemc_thread_config, systemc_thread_diagnostics);
  assert(systemc_thread_project);
  const auto thread_count =
      systemc_thread_project->design.find_signal("count");
  const auto thread_timed =
      systemc_thread_project->design.find_signal("timed");
  const auto thread_event_count =
      systemc_thread_project->design.find_signal("event_count");
  assert(thread_count && thread_timed && thread_event_count);
  fsim::app::Simulation systemc_thread_simulation{
      std::move(*systemc_thread_project),
      systemc_thread_config.run.max_deltas,
#if defined(FSIM_HAS_LLVM)
      fsim::app::SimulationEngine::compiled};
#else
      fsim::app::SimulationEngine::interpreter};
#endif
  const auto systemc_thread_result =
      systemc_thread_simulation.run();
  assert(
      systemc_thread_result.status
      == fsim::runtime::RunStatus::stopped);
  assert(systemc_thread_result.time == 4);
  assert(
      systemc_thread_simulation
          .read_signal(*thread_count)
          .to_msb_string()
      == "00000010");
  assert(
      systemc_thread_simulation
          .read_signal(*thread_timed)
          .to_msb_string()
      == "00000011");
  assert(
      systemc_thread_simulation
          .read_signal(*thread_event_count)
          .to_msb_string()
      == "00000010");
#endif

  auto systemc_method_config = hdl_systemc_config;
  systemc_method_config.project.top =
      "sv:work.systemc_method_host";
  systemc_method_config.bindings = {
      {"systemc_method_host.u_method",
       "systemc:models.method_bridge",
       std::nullopt},
  };
  fsim::diagnostic::Engine systemc_method_diagnostics;
  auto systemc_method_reference = fsim::app::build_project(
      systemc_method_config, systemc_method_diagnostics);
  auto systemc_method_compiled = fsim::app::build_project(
      systemc_method_config, systemc_method_diagnostics);
  assert(systemc_method_reference);
  assert(systemc_method_compiled);
  assert(
      systemc_method_reference->design.systemc_processes().size()
      == 1);
  const auto method_process_id =
      systemc_method_reference->design.systemc_processes().front().process;
  const auto& method_process =
      systemc_method_reference->design.processes().at(
          method_process_id);
  assert(
      method_process.name == "systemc_method_host.u_method.evaluate");
  assert(method_process.initialize);
  assert(method_process.static_sensitivity.size() == 1);
  const auto run_systemc_method =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto inverted =
            project.design.find_signal("inverted");
        const auto observed =
            project.design.find_signal("observed");
        assert(inverted && observed);
        fsim::app::Simulation simulation{
            std::move(project),
            systemc_method_config.run.max_deltas,
            engine};
        const auto result = simulation.run();
        return std::tuple{
            result,
            simulation.read_signal(*inverted).to_msb_string(),
            simulation.read_signal(*observed).to_msb_string()};
      };
  const auto method_reference = run_systemc_method(
      std::move(*systemc_method_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto method_compiled = run_systemc_method(
      std::move(*systemc_method_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(std::get<0>(method_reference).status
         == fsim::runtime::RunStatus::stopped);
  assert(std::get<0>(method_reference).time == 2);
  assert(std::get<1>(method_reference) == "0");
  assert(std::get<2>(method_reference) == "0");
  assert(std::get<0>(method_reference).status
         == std::get<0>(method_compiled).status);
  assert(std::get<0>(method_reference).time
         == std::get<0>(method_compiled).time);
  assert(std::get<1>(method_reference)
         == std::get<1>(method_compiled));
  assert(std::get<2>(method_reference)
         == std::get<2>(method_compiled));

  auto vhdl_systemc_method_config = systemc_method_config;
  vhdl_systemc_method_config.project.top =
      "vhdl:work.systemc_method_vhdl_host(rtl)";
  vhdl_systemc_method_config.source_sets.front().language =
      fsim::project::Language::vhdl;
  vhdl_systemc_method_config.source_sets.front().standard = "2008";
  vhdl_systemc_method_config.source_sets.front().files = {
      systemc_method_vhdl_source};
  vhdl_systemc_method_config.bindings = {
      {"systemc_method_vhdl_host.u_method",
       "systemc:models.method_bridge",
       std::nullopt},
  };
  fsim::diagnostic::Engine vhdl_systemc_method_diagnostics;
  auto vhdl_systemc_method_project = fsim::app::build_project(
      vhdl_systemc_method_config,
      vhdl_systemc_method_diagnostics);
  assert(vhdl_systemc_method_project);
  const auto vhdl_method_output =
      vhdl_systemc_method_project->design.find_signal("inverted");
  assert(vhdl_method_output);
  fsim::app::Simulation vhdl_systemc_method_simulation{
      std::move(*vhdl_systemc_method_project),
      vhdl_systemc_method_config.run.max_deltas,
      fsim::app::SimulationEngine::compiled};
  const auto vhdl_systemc_method_result =
      vhdl_systemc_method_simulation.run();
  assert(
      vhdl_systemc_method_result.status
      == fsim::runtime::RunStatus::completed);
  assert(
      vhdl_systemc_method_simulation
          .read_signal(*vhdl_method_output)
          .to_msb_string()
      == "1");

  auto systemc_method_top_config = systemc_method_config;
  systemc_method_top_config.project.top =
      "systemc:models.method_bridge";
  systemc_method_top_config.bindings.clear();
  fsim::diagnostic::Engine systemc_method_top_diagnostics;
  auto systemc_method_top = fsim::app::build_project(
      systemc_method_top_config,
      systemc_method_top_diagnostics);
  assert(systemc_method_top);
  const auto method_top_output =
      systemc_method_top->design.find_signal(
          "method_bridge.inverted");
  assert(method_top_output);
  fsim::app::Simulation method_top_simulation{
      std::move(*systemc_method_top),
      systemc_method_top_config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  const auto method_top_result = method_top_simulation.run();
  assert(
      method_top_result.status
      == fsim::runtime::RunStatus::completed);
  assert(
      method_top_simulation.read_signal(*method_top_output)
          .to_msb_string()
      == "Z");

  auto noinit_config = systemc_method_top_config;
  noinit_config.project.top =
      "systemc:models.noinit_bridge";
  fsim::diagnostic::Engine noinit_diagnostics;
  auto noinit_project = fsim::app::build_project(
      noinit_config, noinit_diagnostics);
  assert(noinit_project);
  const auto noinit_input =
      noinit_project->design.find_signal("noinit_bridge.value");
  const auto noinit_output =
      noinit_project->design.find_signal(
          "noinit_bridge.inverted");
  assert(noinit_input && noinit_output);
  fsim::app::Simulation noinit_simulation{
      std::move(*noinit_project),
      noinit_config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  const auto noinit_result = noinit_simulation.run();
  assert(noinit_result.status == fsim::runtime::RunStatus::completed);
  assert(
      noinit_simulation.read_signal(*noinit_output).to_msb_string()
      == "X");

  noinit_diagnostics.clear();
  auto awakened_noinit_project = fsim::app::build_project(
      noinit_config, noinit_diagnostics);
  assert(awakened_noinit_project);
  const auto awakened_input =
      awakened_noinit_project->design.find_signal(
          "noinit_bridge.value");
  const auto awakened_output =
      awakened_noinit_project->design.find_signal(
          "noinit_bridge.inverted");
  assert(awakened_input && awakened_output);
  fsim::app::Simulation awakened_noinit_simulation{
      std::move(*awakened_noinit_project),
      noinit_config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  awakened_noinit_simulation.deposit_signal(
      *awakened_input,
      fsim::runtime::PackedLogic4::from_msb_string("0"));
  const auto awakened_noinit_result =
      awakened_noinit_simulation.run();
  assert(
      awakened_noinit_result.status
      == fsim::runtime::RunStatus::completed);
  assert(
      awakened_noinit_simulation
          .read_signal(*awakened_output)
          .to_msb_string()
      == "0");

  auto edge_method_config = systemc_method_config;
  edge_method_config.project.top =
      "sv:work.systemc_edge_host";
  edge_method_config.bindings = {
      {"systemc_edge_host.u_counter",
       "systemc:models.edge_counter",
       std::nullopt},
  };
  fsim::diagnostic::Engine edge_method_diagnostics;
  auto edge_method_project = fsim::app::build_project(
      edge_method_config, edge_method_diagnostics);
  assert(edge_method_project);
  const auto edge_count =
      edge_method_project->design.find_signal("count");
  assert(edge_count);
  fsim::app::Simulation edge_method_simulation{
      std::move(*edge_method_project),
      edge_method_config.run.max_deltas,
      fsim::app::SimulationEngine::compiled};
  const auto edge_method_result = edge_method_simulation.run();
  assert(
      edge_method_result.status
      == fsim::runtime::RunStatus::stopped);
  assert(edge_method_result.time == 4);
  assert(
      edge_method_simulation.read_signal(*edge_count).to_msb_string()
      == "00000010");

  auto throwing_method_config = systemc_method_top_config;
  throwing_method_config.project.top =
      "systemc:models.throwing_method";
  fsim::diagnostic::Engine throwing_method_diagnostics;
  auto throwing_method_project = fsim::app::build_project(
      throwing_method_config, throwing_method_diagnostics);
  assert(throwing_method_project);
  fsim::app::Simulation throwing_method_simulation{
      std::move(*throwing_method_project),
      throwing_method_config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  bool caught_systemc_failure = false;
  try {
    (void)throwing_method_simulation.run();
  } catch (const std::runtime_error& error) {
    caught_systemc_failure =
        std::string_view{error.what()}.find(
            "intentional SystemC method failure")
        != std::string_view::npos;
  }
  assert(caught_systemc_failure);
  assert(throwing_method_simulation.poisoned());

  auto dynamic_event_config = systemc_method_top_config;
  dynamic_event_config.project.top =
      "systemc:models.dynamic_events";
  fsim::diagnostic::Engine dynamic_event_diagnostics;
  auto dynamic_event_reference = fsim::app::build_project(
      dynamic_event_config, dynamic_event_diagnostics);
  auto dynamic_event_compiled = fsim::app::build_project(
      dynamic_event_config, dynamic_event_diagnostics);
  assert(dynamic_event_reference);
  assert(dynamic_event_compiled);
  assert(
      dynamic_event_reference->design.systemc_processes().size()
      == 2);
  assert(
      dynamic_event_reference->design.systemc_instances().size()
      == 1);
  assert(
      dynamic_event_reference->design.systemc_instances().front()
          .events.size()
      == 1);
  const auto run_dynamic_events =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto count =
            project.design.find_signal("dynamic_events.count");
        assert(count);
        fsim::app::Simulation simulation{
            std::move(project),
            dynamic_event_config.run.max_deltas,
            engine};
        const auto result = simulation.run();
        return std::pair{
            result,
            simulation.read_signal(*count).to_msb_string()};
      };
  const auto dynamic_reference = run_dynamic_events(
      std::move(*dynamic_event_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto dynamic_compiled = run_dynamic_events(
      std::move(*dynamic_event_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(
      dynamic_reference.first.status
      == fsim::runtime::RunStatus::completed);
  assert(dynamic_reference.first.time == 8);
  assert(dynamic_reference.second == "00000100");
  assert(dynamic_reference.first.status == dynamic_compiled.first.status);
  assert(dynamic_reference.first.time == dynamic_compiled.first.time);
  assert(dynamic_reference.second == dynamic_compiled.second);

  auto event_list_config = systemc_method_top_config;
  event_list_config.project.top =
      "systemc:models.event_lists";
  fsim::diagnostic::Engine event_list_diagnostics;
  auto event_list_reference = fsim::app::build_project(
      event_list_config, event_list_diagnostics);
  auto event_list_compiled = fsim::app::build_project(
      event_list_config, event_list_diagnostics);
  assert(event_list_reference);
  assert(event_list_compiled);
  assert(
      event_list_reference->design.systemc_instances().front()
          .events.size()
      == 3);
  const auto run_event_lists =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto or_seen =
            project.design.find_signal("event_lists.or_seen");
        const auto and_seen =
            project.design.find_signal("event_lists.and_seen");
        assert(or_seen && and_seen);
        fsim::app::Simulation simulation{
            std::move(project),
            event_list_config.run.max_deltas,
            engine};
        const auto result = simulation.run();
        return std::tuple{
            result,
            simulation.read_signal(*or_seen).to_msb_string(),
            simulation.read_signal(*and_seen).to_msb_string()};
      };
  const auto event_lists_reference = run_event_lists(
      std::move(*event_list_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto event_lists_compiled = run_event_lists(
      std::move(*event_list_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(
      std::get<0>(event_lists_reference).status
      == fsim::runtime::RunStatus::completed);
  assert(std::get<0>(event_lists_reference).time == 3);
  assert(std::get<1>(event_lists_reference) == "00000001");
  assert(std::get<2>(event_lists_reference) == "00000011");
  assert(
      std::get<0>(event_lists_reference).status
      == std::get<0>(event_lists_compiled).status);
  assert(
      std::get<0>(event_lists_reference).time
      == std::get<0>(event_lists_compiled).time);
  assert(
      std::get<1>(event_lists_reference)
      == std::get<1>(event_lists_compiled));
  assert(
      std::get<2>(event_lists_reference)
      == std::get<2>(event_lists_compiled));

  auto kernel_channel_config = systemc_method_top_config;
  kernel_channel_config.project.top =
      "systemc:models.kernel_channels";
  fsim::diagnostic::Engine kernel_channel_diagnostics;
  auto kernel_channel_reference = fsim::app::build_project(
      kernel_channel_config, kernel_channel_diagnostics);
  auto kernel_channel_compiled = fsim::app::build_project(
      kernel_channel_config, kernel_channel_diagnostics);
  assert(kernel_channel_reference);
  assert(kernel_channel_compiled);
  assert(
      kernel_channel_reference->design.systemc_instances()
          .front().primitive_channels.size()
      == 1);
  const auto run_kernel_channels =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto value =
            project.design.find_signal("kernel_channels.value");
        const auto updates =
            project.design.find_signal("kernel_channels.updates");
        const auto event_count =
            project.design.find_signal(
                "kernel_channels.event_count");
        assert(value && updates && event_count);
        fsim::app::Simulation simulation{
            std::move(project),
            kernel_channel_config.run.max_deltas,
            engine};
        const auto result = simulation.run();
        return std::tuple{
            result,
            simulation.read_signal(*value).to_msb_string(),
            simulation.read_signal(*updates).to_msb_string(),
            simulation.read_signal(*event_count).to_msb_string()};
      };
  const auto kernel_reference = run_kernel_channels(
      std::move(*kernel_channel_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto kernel_compiled = run_kernel_channels(
      std::move(*kernel_channel_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(
      std::get<0>(kernel_reference).status
      == fsim::runtime::RunStatus::completed);
  assert(std::get<0>(kernel_reference).time == 3);
  assert(std::get<1>(kernel_reference) == "00000100");
  assert(std::get<2>(kernel_reference) == "00000011");
  assert(std::get<3>(kernel_reference) == "00000001");
  assert(
      std::get<0>(kernel_reference).status
      == std::get<0>(kernel_compiled).status);
  assert(
      std::get<0>(kernel_reference).time
      == std::get<0>(kernel_compiled).time);
  assert(
      std::get<1>(kernel_reference)
      == std::get<1>(kernel_compiled));
  assert(
      std::get<2>(kernel_reference)
      == std::get<2>(kernel_compiled));
  assert(
      std::get<3>(kernel_reference)
      == std::get<3>(kernel_compiled));

  auto internal_signal_config = systemc_method_top_config;
  internal_signal_config.project.top =
      "systemc:models.internal_signals";
  fsim::diagnostic::Engine internal_signal_diagnostics;
  auto internal_signal_reference = fsim::app::build_project(
      internal_signal_config, internal_signal_diagnostics);
  auto internal_signal_compiled = fsim::app::build_project(
      internal_signal_config, internal_signal_diagnostics);
  assert(internal_signal_reference);
  assert(internal_signal_compiled);
  const auto& internal_instance =
      internal_signal_reference->design.systemc_instances().front();
  assert(internal_instance.internal_signals.size() == 1);
  assert(internal_instance.primitive_channels.size() == 1);
  const auto run_internal_signals =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine) {
        const auto internal =
            project.design.find_signal(
                "internal_signals.internal");
        const auto observed =
            project.design.find_signal(
                "internal_signals.observed");
        const auto event_count =
            project.design.find_signal(
                "internal_signals.event_count");
        const auto dynamic_count =
            project.design.find_signal(
                "internal_signals.dynamic_count");
        assert(
            internal && observed && event_count && dynamic_count);
        fsim::app::Simulation simulation{
            std::move(project),
            internal_signal_config.run.max_deltas,
            engine};
        assert(
            simulation.read_signal(*internal).to_msb_string()
            == "00000101");
        const auto result = simulation.run();
        return std::tuple{
            result,
            simulation.read_signal(*internal).to_msb_string(),
            simulation.read_signal(*observed).to_msb_string(),
            simulation.read_signal(*event_count).to_msb_string(),
            simulation.read_signal(*dynamic_count).to_msb_string()};
      };
  const auto internal_reference = run_internal_signals(
      std::move(*internal_signal_reference),
      fsim::app::SimulationEngine::interpreter);
  const auto internal_compiled = run_internal_signals(
      std::move(*internal_signal_compiled),
      fsim::app::SimulationEngine::compiled);
  assert(
      std::get<0>(internal_reference).status
      == fsim::runtime::RunStatus::completed);
  assert(std::get<0>(internal_reference).time == 1);
  assert(std::get<1>(internal_reference) == "00000011");
  assert(std::get<2>(internal_reference) == "00000011");
  assert(std::get<3>(internal_reference) == "00000010");
  assert(std::get<4>(internal_reference) == "00000010");
  assert(
      std::get<0>(internal_reference).status
      == std::get<0>(internal_compiled).status);
  assert(
      std::get<0>(internal_reference).time
      == std::get<0>(internal_compiled).time);
  assert(
      std::get<1>(internal_reference)
      == std::get<1>(internal_compiled));
  assert(
      std::get<2>(internal_reference)
      == std::get<2>(internal_compiled));
  assert(
      std::get<3>(internal_reference)
      == std::get<3>(internal_compiled));
  assert(
      std::get<4>(internal_reference)
      == std::get<4>(internal_compiled));

  struct CapturedSimulation {
    fsim::runtime::RunResult result;
    std::vector<std::tuple<
        fsim::runtime::simir::SignalId,
        std::string,
        fsim::runtime::SimulationTick,
        std::uint64_t>> changes;
    std::vector<std::string> final_values;
    std::string normalized_vcd;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes{};
    std::size_t compiled_modules{};
    std::size_t process_count{};
  };
  const auto capture_simulation =
      [&](fsim::app::BuiltProject project,
          const fsim::app::SimulationEngine engine,
          const std::optional<fsim::runtime::SimulationTick> until =
              std::nullopt) {
        CapturedSimulation captured;
        captured.process_count = project.design.processes().size();
        fsim::app::Simulation candidate(
            std::move(project), config.run.max_deltas, engine);
        captured.compiled_processes =
            candidate.compiled_process_count();
        captured.compiled_modules =
            candidate.compiled_module_count();
        captured.native_cache =
            candidate.native_cache_statistics();
        std::ostringstream vcd_output;
        fsim::runtime::VcdWriter vcd(vcd_output, "1ns", 256);
        std::vector<fsim::runtime::VcdSignal> vcd_signals;
        vcd_signals.reserve(candidate.design().signals().size());
        for (const auto& signal : candidate.design().signals()) {
          vcd_signals.push_back(
              vcd.declare_signal(signal.name, signal.width));
        }
        vcd.begin(candidate.now());
        for (const auto& signal : candidate.design().signals()) {
          vcd.change(
              vcd_signals.at(signal.id),
              candidate.read_signal(signal.id));
        }
        candidate.set_signal_change_hook(
            [&captured, &vcd, &vcd_signals](
                const fsim::runtime::simir::SignalId signal,
                const fsim::runtime::PackedLogic4& value,
                const fsim::runtime::SimulationTick time,
                const std::uint64_t delta) {
              captured.changes.emplace_back(
                  signal, value.to_msb_string(), time, delta);
              vcd.set_time(time);
              vcd.change(vcd_signals.at(signal), value);
            });
        captured.result = candidate.run(until);
        vcd.flush();
        captured.normalized_vcd = vcd_output.str();
        captured.final_values.reserve(
            candidate.design().signals().size());
        for (const auto& signal : candidate.design().signals()) {
          captured.final_values.push_back(
              candidate.read_signal(signal.id).to_msb_string());
        }
        return captured;
      };
  const auto compare_captures =
      [](const CapturedSimulation& reference,
         const CapturedSimulation& hybrid) {
        assert(reference.result.status == hybrid.result.status);
        assert(reference.result.time == hybrid.result.time);
        assert(reference.result.delta == hybrid.result.delta);
        assert(
            reference.result.callbacks_executed
            == hybrid.result.callbacks_executed);
        assert(reference.changes == hybrid.changes);
        assert(reference.final_values == hybrid.final_values);
        assert(reference.normalized_vcd == hybrid.normalized_vcd);
        assert(
            reference.normalized_vcd.find("$version fsim $end")
            != std::string::npos);
        assert(reference.compiled_processes == 0);
        assert(reference.compiled_modules == 0);
        assert(
            reference.native_cache
            == fsim::app::NativeCacheStatistics{});
#if defined(FSIM_HAS_LLVM)
        assert(hybrid.compiled_processes > 0);
        assert(
            hybrid.compiled_processes
            <= hybrid.process_count);
        assert(hybrid.compiled_modules > 0);
        assert(
            hybrid.compiled_modules
            <= hybrid.compiled_processes);
#else
        assert(hybrid.compiled_processes == 0);
        assert(hybrid.compiled_modules == 0);
#endif
      };

  auto differential_config = config;
  std::erase_if(
      differential_config.source_sets,
      [](const fsim::project::SourceSet& source_set) {
        return source_set.language
            == fsim::project::Language::systemc;
      });
  differential_config.build.cache_path =
      directory / "differential-cache";
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    differential_config.build.optimization = optimization;
    fsim::diagnostic::Engine differential_diagnostics;
    auto reference_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    auto hybrid_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    assert(reference_project);
    assert(hybrid_project);
    const auto reference = capture_simulation(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto hybrid = capture_simulation(
        std::move(*hybrid_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(reference, hybrid);
#if defined(FSIM_HAS_LLVM)
    assert(hybrid.process_count == 2);
    assert(
        hybrid.compiled_processes
        == hybrid.process_count);
    assert(hybrid.compiled_modules == 2);
#endif

    auto warm_project = fsim::app::build_project(
        differential_config, differential_diagnostics);
    assert(warm_project);
    const auto warm = capture_simulation(
        std::move(*warm_project),
        fsim::app::SimulationEngine::compiled);
    compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
    assert(
        hybrid.native_cache.misses
        == hybrid.compiled_modules);
    assert(
        hybrid.native_cache.stores
        == hybrid.compiled_modules);
    assert(hybrid.native_cache.hits == 0);
    assert(
        warm.native_cache.hits
        == warm.compiled_modules);
    assert(warm.native_cache.misses == 0);
    assert(warm.native_cache.load_failures == 0);
    assert(warm.native_cache.store_failures == 0);
#else
    assert(
        hybrid.native_cache
        == fsim::app::NativeCacheStatistics{});
    assert(
        warm.native_cache
        == fsim::app::NativeCacheStatistics{});
#endif
  }

  struct CapturedAssertion {
    fsim::runtime::simir::ProcessId process{};
    fsim::runtime::simir::InstructionIndex instruction{};
    fsim::runtime::simir::AssertionSeverity severity{
        fsim::runtime::simir::AssertionSeverity::error};
    fsim::runtime::simir::SourceLocation source;
    std::string message;
    std::size_t compiled_processes{};
    std::size_t compiled_modules{};
  };
  auto assertion_config = config;
  assertion_config.project.name = "assertion-differential";
  assertion_config.project.top = "vhdl:work.assertion_test(rtl)";
  assertion_config.build.cache_path = directory / "assertion-cache";
  assertion_config.source_sets.clear();
  fsim::project::SourceSet assertion_sources;
  assertion_sources.language = fsim::project::Language::vhdl;
  assertion_sources.standard = "2008";
  assertion_sources.library = "work";
  assertion_sources.files.push_back(assertion_source);
  assertion_config.source_sets.push_back(std::move(assertion_sources));
  const auto capture_assertion =
      [&](const fsim::project::Optimization optimization,
          const fsim::app::SimulationEngine engine) {
        assertion_config.build.optimization = optimization;
        fsim::diagnostic::Engine assertion_diagnostics;
        auto project =
            fsim::app::build_project(assertion_config, assertion_diagnostics);
        assert(project);
        fsim::app::Simulation simulation(
            std::move(*project), assertion_config.run.max_deltas, engine);
        CapturedAssertion captured;
        captured.compiled_processes =
            simulation.compiled_process_count();
        captured.compiled_modules = simulation.compiled_module_count();
        try {
          (void)simulation.run();
        } catch (const fsim::runtime::simir::AssertionError& error) {
          captured.process = error.process();
          captured.instruction = error.instruction();
          captured.severity = error.severity();
          captured.source = error.source();
          captured.message = error.what();
          return captured;
        }
        throw std::runtime_error("false assertion completed successfully");
      };
  const auto assertion_reference = capture_assertion(
      fsim::project::Optimization::o0,
      fsim::app::SimulationEngine::interpreter);
  const auto compare_assertion =
      [&](const CapturedAssertion& candidate) {
        assert(candidate.process == assertion_reference.process);
        assert(candidate.instruction == assertion_reference.instruction);
        assert(candidate.severity == assertion_reference.severity);
        assert(candidate.source.path == assertion_reference.source.path);
        assert(candidate.source.line == assertion_reference.source.line);
        assert(candidate.source.column == assertion_reference.source.column);
        assert(candidate.message == assertion_reference.message);
      };
  assert(
      assertion_reference.severity
      == fsim::runtime::simir::AssertionSeverity::failure);
  assert(assertion_reference.source.path == assertion_source.string());
  assert(assertion_reference.source.line == 9);
  assert(assertion_reference.source.column == 5);
  assert(
      assertion_reference.message.find("cross-engine mismatch")
      != std::string::npos);
  assert(assertion_reference.compiled_processes == 0);
  assert(assertion_reference.compiled_modules == 0);
  const auto assertion_o0 = capture_assertion(
      fsim::project::Optimization::o0,
      fsim::app::SimulationEngine::compiled);
  const auto assertion_o2 = capture_assertion(
      fsim::project::Optimization::o2,
      fsim::app::SimulationEngine::compiled);
  compare_assertion(assertion_o0);
  compare_assertion(assertion_o2);
#if defined(FSIM_HAS_LLVM)
  assert(assertion_o0.compiled_processes == 1);
  assert(assertion_o0.compiled_modules == 1);
  assert(assertion_o2.compiled_processes == 1);
  assert(assertion_o2.compiled_modules == 1);
#else
  assert(assertion_o0.compiled_processes == 0);
  assert(assertion_o0.compiled_modules == 0);
  assert(assertion_o2.compiled_processes == 0);
  assert(assertion_o2.compiled_modules == 0);
#endif

  auto scheduled_config = config;
  scheduled_config.project.name = "scheduled-write-test";
  scheduled_config.project.top = "sv:work.scheduled";
  scheduled_config.build.optimization =
      fsim::project::Optimization::o2;
  scheduled_config.build.cache_path =
      directory / "scheduled-write-cache";
  scheduled_config.source_sets.clear();
  fsim::project::SourceSet scheduled_sources;
  scheduled_sources.language =
      fsim::project::Language::system_verilog;
  scheduled_sources.standard = "2017";
  scheduled_sources.library = "work";
  scheduled_sources.files.push_back(scheduled_source);
  scheduled_config.source_sets.push_back(
      std::move(scheduled_sources));
  fsim::diagnostic::Engine scheduled_diagnostics;
  auto scheduled_reference_project =
      fsim::app::build_project(
          scheduled_config, scheduled_diagnostics);
  auto scheduled_hybrid_project =
      fsim::app::build_project(
          scheduled_config, scheduled_diagnostics);
  assert(scheduled_reference_project);
  assert(scheduled_hybrid_project);
  const auto scheduled_reference = capture_simulation(
      std::move(*scheduled_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto scheduled_hybrid = capture_simulation(
      std::move(*scheduled_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      scheduled_reference, scheduled_hybrid);
  assert(scheduled_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(scheduled_hybrid.compiled_processes == 1);
  assert(scheduled_hybrid.compiled_modules == 1);
#endif
  assert(
      scheduled_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(scheduled_hybrid.result.time == 3);
  assert(scheduled_hybrid.final_values.size() == 1);
  assert(scheduled_hybrid.final_values.front() == "1");
  assert(scheduled_hybrid.changes.size() == 2);
  assert(
      std::get<1>(scheduled_hybrid.changes.front())
      == "0");
  assert(
      std::get<2>(scheduled_hybrid.changes.front())
      == 0);
  assert(
      std::get<3>(scheduled_hybrid.changes.front())
      == 0);
  assert(
      std::get<1>(scheduled_hybrid.changes.back())
      == "1");
  assert(
      std::get<2>(scheduled_hybrid.changes.back())
      == 2);
  assert(
      std::get<3>(scheduled_hybrid.changes.back())
      == 0);

  auto overflow_config = scheduled_config;
  overflow_config.project.name =
      "scheduled-write-overflow-test";
  overflow_config.project.top =
      "sv:work.scheduled_overflow";
  overflow_config.build.cache_path =
      directory / "scheduled-write-overflow-cache";
  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled}) {
    fsim::diagnostic::Engine overflow_diagnostics;
    auto overflow_project = fsim::app::build_project(
        overflow_config, overflow_diagnostics);
    assert(overflow_project);
    fsim::app::Simulation overflow_simulation(
        std::move(*overflow_project),
        overflow_config.run.max_deltas,
        engine);
#if defined(FSIM_HAS_LLVM)
    assert(
        overflow_simulation.compiled_process_count()
        == (engine == fsim::app::SimulationEngine::compiled
                ? 1U
                : 0U));
#endif
    const auto overflow_q =
        overflow_simulation.find_signal("q");
    assert(overflow_q);
    bool overflow_thrown = false;
    try {
      (void)overflow_simulation.run();
    } catch (const std::overflow_error& exception) {
      overflow_thrown =
          std::string_view{exception.what()}
          == "simulation time overflow while scheduling event";
    }
    assert(overflow_thrown);
    assert(overflow_simulation.poisoned());
    assert(!overflow_simulation.finished());
    assert(overflow_simulation.now() == 1);
    assert(
        overflow_simulation.read_signal(*overflow_q)
            .to_msb_string()
        == "X");
  }

  auto sensitivity_config = config;
  sensitivity_config.project.name =
      "sensitivity-wakeup-test";
  sensitivity_config.project.top =
      "sv:work.sensitivity";
  sensitivity_config.build.optimization =
      fsim::project::Optimization::o2;
  sensitivity_config.build.cache_path =
      directory / "sensitivity-cache";
  sensitivity_config.source_sets.clear();
  fsim::project::SourceSet sensitivity_sources;
  sensitivity_sources.language =
      fsim::project::Language::system_verilog;
  sensitivity_sources.standard = "2017";
  sensitivity_sources.library = "work";
  sensitivity_sources.files.push_back(sensitivity_source);
  sensitivity_config.source_sets.push_back(
      std::move(sensitivity_sources));
  fsim::diagnostic::Engine sensitivity_diagnostics;
  auto sensitivity_reference_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  auto sensitivity_hybrid_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  assert(sensitivity_reference_project);
  assert(sensitivity_hybrid_project);
  const auto sensitivity_trigger =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.trigger");
  const auto sensitivity_observed =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.observed");
  const auto sensitivity_dynamic_observed =
      sensitivity_reference_project->design.find_signal(
          "sensitivity.dynamic_observed");
  assert(sensitivity_trigger);
  assert(sensitivity_observed);
  assert(sensitivity_dynamic_observed);
  const auto sensitivity_reference = capture_simulation(
      std::move(*sensitivity_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto sensitivity_hybrid = capture_simulation(
      std::move(*sensitivity_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      sensitivity_reference, sensitivity_hybrid);
  assert(sensitivity_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
  assert(sensitivity_hybrid.compiled_processes == 3);
  assert(sensitivity_hybrid.compiled_modules == 1);
#endif
  assert(
      sensitivity_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(sensitivity_hybrid.result.time == 3);
  const decltype(sensitivity_reference.changes)
      expected_sensitivity_changes = {
          {*sensitivity_trigger, "0", 0, 0},
          {*sensitivity_trigger, "1", 1, 0},
          {*sensitivity_dynamic_observed, "1", 1, 1},
          {*sensitivity_observed, "1", 1, 1},
          {*sensitivity_trigger, "0", 2, 0},
          {*sensitivity_dynamic_observed, "0", 2, 1},
      };
  assert(
      sensitivity_reference.changes
      == expected_sensitivity_changes);
  assert(
      sensitivity_hybrid.changes
      == expected_sensitivity_changes);
  assert((
      sensitivity_hybrid.final_values
      == std::vector<std::string>{"0", "1", "0"}));
#if defined(FSIM_HAS_LLVM)
  assert(sensitivity_hybrid.native_cache.hits == 0);
  assert(sensitivity_hybrid.native_cache.misses == 1);
  assert(sensitivity_hybrid.native_cache.stores == 1);
  auto sensitivity_warm_project =
      fsim::app::build_project(
          sensitivity_config, sensitivity_diagnostics);
  assert(sensitivity_warm_project);
  const auto sensitivity_warm = capture_simulation(
      std::move(*sensitivity_warm_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      sensitivity_reference, sensitivity_warm);
  assert(sensitivity_warm.compiled_processes == 3);
  assert(sensitivity_warm.compiled_modules == 1);
  assert(sensitivity_warm.native_cache.hits == 1);
  assert(sensitivity_warm.native_cache.misses == 0);
  assert(sensitivity_warm.native_cache.stores == 0);
#endif

  auto vhdl_wait_config = config;
  vhdl_wait_config.project.name = "vhdl-explicit-wait-test";
  vhdl_wait_config.project.top = "vhdl:work.vhdl_wait(rtl)";
  vhdl_wait_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_wait_config.build.cache_path =
      directory / "vhdl-wait-cache";
  vhdl_wait_config.source_sets.clear();
  fsim::project::SourceSet vhdl_wait_sources;
  vhdl_wait_sources.language = fsim::project::Language::vhdl;
  vhdl_wait_sources.standard = "2008";
  vhdl_wait_sources.library = "work";
  vhdl_wait_sources.files.push_back(vhdl_wait_source);
  vhdl_wait_config.source_sets.push_back(
      std::move(vhdl_wait_sources));
  fsim::diagnostic::Engine vhdl_wait_diagnostics;
  auto vhdl_wait_reference_project =
      fsim::app::build_project(
          vhdl_wait_config, vhdl_wait_diagnostics);
  auto vhdl_wait_hybrid_project =
      fsim::app::build_project(
          vhdl_wait_config, vhdl_wait_diagnostics);
  assert(vhdl_wait_reference_project);
  assert(vhdl_wait_hybrid_project);
  const auto vhdl_wait_q =
      vhdl_wait_reference_project->design.find_signal(
          "vhdl_wait.q");
  assert(vhdl_wait_q);
  const auto vhdl_wait_reference = capture_simulation(
      std::move(*vhdl_wait_reference_project),
      fsim::app::SimulationEngine::interpreter,
      4);
  const auto vhdl_wait_hybrid = capture_simulation(
      std::move(*vhdl_wait_hybrid_project),
      fsim::app::SimulationEngine::compiled,
      4);
  compare_captures(vhdl_wait_reference, vhdl_wait_hybrid);
  assert(
      vhdl_wait_hybrid.result.status
      == fsim::runtime::RunStatus::time_limit);
  assert(vhdl_wait_hybrid.result.time == 4);
  assert(vhdl_wait_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(vhdl_wait_hybrid.compiled_processes == 1);
  assert(vhdl_wait_hybrid.compiled_modules == 1);
#endif
  const decltype(vhdl_wait_reference.changes)
      expected_vhdl_wait_changes = {
          {*vhdl_wait_q, "0", 0, 0},
          {*vhdl_wait_q, "1", 1, 0},
          {*vhdl_wait_q, "0", 2, 0},
          {*vhdl_wait_q, "1", 3, 0},
          {*vhdl_wait_q, "0", 4, 0},
      };
  assert(
      vhdl_wait_reference.changes
      == expected_vhdl_wait_changes);
  assert(
      vhdl_wait_hybrid.changes
      == expected_vhdl_wait_changes);
  assert((
      vhdl_wait_hybrid.final_values
      == std::vector<std::string>{"0"}));

  auto wildcard_config = config;
  wildcard_config.project.name = "wildcard-sensitivity-test";
  wildcard_config.project.top = "sv:work.wildcard_app";
  wildcard_config.build.optimization =
      fsim::project::Optimization::o2;
  wildcard_config.build.cache_path =
      directory / "wildcard-cache";
  wildcard_config.source_sets.clear();
  fsim::project::SourceSet wildcard_sources;
  wildcard_sources.language =
      fsim::project::Language::system_verilog;
  wildcard_sources.standard = "2017";
  wildcard_sources.library = "work";
  wildcard_sources.files.push_back(wildcard_source);
  wildcard_config.source_sets.push_back(
      std::move(wildcard_sources));
  fsim::diagnostic::Engine wildcard_diagnostics;
  auto wildcard_reference_project =
      fsim::app::build_project(
          wildcard_config, wildcard_diagnostics);
  auto wildcard_hybrid_project =
      fsim::app::build_project(
          wildcard_config, wildcard_diagnostics);
  assert(wildcard_reference_project);
  assert(wildcard_hybrid_project);
  const auto wildcard_a =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.a");
  const auto wildcard_q =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.q");
  const auto wildcard_y =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.y");
  const auto wildcard_latched =
      wildcard_reference_project->design.find_signal(
          "wildcard_app.latched");
  assert(
      wildcard_a && wildcard_q && wildcard_y
      && wildcard_latched);
  const auto wildcard_reference = capture_simulation(
      std::move(*wildcard_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto wildcard_hybrid = capture_simulation(
      std::move(*wildcard_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(wildcard_reference, wildcard_hybrid);
  assert(
      wildcard_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(wildcard_hybrid.result.time == 2);
  assert(wildcard_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
  assert(wildcard_hybrid.compiled_processes == 4);
  assert(wildcard_hybrid.compiled_modules == 1);
#endif
  const decltype(wildcard_reference.changes)
      expected_wildcard_changes = {
          {*wildcard_a, "0", 0, 0},
          {*wildcard_q, "0", 0, 1},
          {*wildcard_y, "1", 0, 2},
          {*wildcard_a, "1", 1, 0},
          {*wildcard_q, "1", 1, 1},
          {*wildcard_latched, "1", 1, 1},
          {*wildcard_y, "0", 1, 2},
      };
  assert(
      wildcard_reference.changes
      == expected_wildcard_changes);
  assert(
      wildcard_hybrid.changes
      == expected_wildcard_changes);
  assert((
      wildcard_hybrid.final_values
      == std::vector<std::string>{"1", "1", "0", "1"}));

  auto case_config = config;
  case_config.project.name = "case-statement-test";
  case_config.project.top = "sv:work.case_app";
  case_config.build.optimization =
      fsim::project::Optimization::o2;
  case_config.build.cache_path = directory / "case-cache";
  case_config.source_sets.clear();
  fsim::project::SourceSet case_sources;
  case_sources.language =
      fsim::project::Language::system_verilog;
  case_sources.standard = "2017";
  case_sources.library = "work";
  case_sources.files.push_back(case_source);
  case_config.source_sets.push_back(std::move(case_sources));
  fsim::diagnostic::Engine case_diagnostics;
  auto case_reference_project =
      fsim::app::build_project(case_config, case_diagnostics);
  auto case_hybrid_project =
      fsim::app::build_project(case_config, case_diagnostics);
  assert(case_reference_project);
  assert(case_hybrid_project);
  const auto case_selector =
      case_reference_project->design.find_signal(
          "case_app.selector");
  const auto case_result =
      case_reference_project->design.find_signal(
          "case_app.result");
  assert(case_selector && case_result);
  const auto case_reference = capture_simulation(
      std::move(*case_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto case_hybrid = capture_simulation(
      std::move(*case_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(case_reference, case_hybrid);
  assert(
      case_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(case_hybrid.result.time == 5);
  assert(case_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(case_hybrid.compiled_processes == 2);
  assert(case_hybrid.compiled_modules == 1);
#endif
  assert((
      case_hybrid.final_values
      == std::vector<std::string>{"11", "00"}));
  assert(std::find(
             case_hybrid.changes.begin(),
             case_hybrid.changes.end(),
             std::tuple{
                 *case_result, std::string{"10"},
                 fsim::runtime::SimulationTick{2},
                 std::uint64_t{1}})
         != case_hybrid.changes.end());
  assert(std::find(
             case_hybrid.changes.begin(),
             case_hybrid.changes.end(),
             std::tuple{
                 *case_result, std::string{"11"},
                 fsim::runtime::SimulationTick{3},
                 std::uint64_t{1}})
         != case_hybrid.changes.end());

  auto conditional_config = config;
  conditional_config.project.name =
      "conditional-expression-test";
  conditional_config.project.top =
      "sv:work.conditional_app";
  conditional_config.build.optimization =
      fsim::project::Optimization::o2;
  conditional_config.build.cache_path =
      directory / "conditional-cache";
  conditional_config.source_sets.clear();
  fsim::project::SourceSet conditional_sources;
  conditional_sources.language =
      fsim::project::Language::system_verilog;
  conditional_sources.standard = "2017";
  conditional_sources.library = "work";
  conditional_sources.files.push_back(conditional_source);
  conditional_config.source_sets.push_back(
      std::move(conditional_sources));
  fsim::diagnostic::Engine conditional_diagnostics;
  auto conditional_reference_project =
      fsim::app::build_project(
          conditional_config, conditional_diagnostics);
  auto conditional_hybrid_project =
      fsim::app::build_project(
          conditional_config, conditional_diagnostics);
  assert(conditional_reference_project);
  assert(conditional_hybrid_project);
  const auto conditional_reference = capture_simulation(
      std::move(*conditional_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto conditional_hybrid = capture_simulation(
      std::move(*conditional_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      conditional_reference, conditional_hybrid);
  assert(
      conditional_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(conditional_hybrid.result.time == 4);
  assert(conditional_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(conditional_hybrid.compiled_processes == 2);
  assert(conditional_hybrid.compiled_modules == 1);
#endif
  assert((
      conditional_hybrid.final_values
      == std::vector<std::string>{
          "Z", "101Z", "100Z", "10XZ"}));

  auto comparison_config = config;
  comparison_config.project.name =
      "comparison-expression-test";
  comparison_config.project.top = "sv:work.comparison_app";
  comparison_config.build.optimization =
      fsim::project::Optimization::o2;
  comparison_config.build.cache_path =
      directory / "comparison-cache";
  comparison_config.source_sets.clear();
  fsim::project::SourceSet comparison_sources;
  comparison_sources.language =
      fsim::project::Language::system_verilog;
  comparison_sources.standard = "2017";
  comparison_sources.library = "work";
  comparison_sources.files.push_back(comparison_source);
  comparison_config.source_sets.push_back(
      std::move(comparison_sources));
  fsim::diagnostic::Engine comparison_diagnostics;
  auto comparison_reference_project =
      fsim::app::build_project(
          comparison_config, comparison_diagnostics);
  auto comparison_hybrid_project =
      fsim::app::build_project(
          comparison_config, comparison_diagnostics);
  assert(comparison_reference_project);
  assert(comparison_hybrid_project);
  const auto comparison_reference = capture_simulation(
      std::move(*comparison_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto comparison_hybrid = capture_simulation(
      std::move(*comparison_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(comparison_reference, comparison_hybrid);
  assert(
      comparison_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(comparison_hybrid.result.time == 4);
  assert(comparison_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(comparison_hybrid.compiled_processes == 2);
  assert(comparison_hybrid.compiled_modules == 1);
#endif
  assert((
      comparison_hybrid.final_values
      == std::vector<std::string>{
          "01Z0", "0011", "X", "X", "X", "X", "X", "0"}));

  auto logical_config = config;
  logical_config.project.name = "logical-expression-test";
  logical_config.project.top = "sv:work.logical_app";
  logical_config.build.optimization =
      fsim::project::Optimization::o2;
  logical_config.build.cache_path =
      directory / "logical-cache";
  logical_config.source_sets.clear();
  fsim::project::SourceSet logical_sources;
  logical_sources.language =
      fsim::project::Language::system_verilog;
  logical_sources.standard = "2017";
  logical_sources.library = "work";
  logical_sources.files.push_back(logical_source);
  logical_config.source_sets.push_back(
      std::move(logical_sources));
  fsim::diagnostic::Engine logical_diagnostics;
  auto logical_reference_project =
      fsim::app::build_project(
          logical_config, logical_diagnostics);
  auto logical_hybrid_project =
      fsim::app::build_project(
          logical_config, logical_diagnostics);
  assert(logical_reference_project);
  assert(logical_hybrid_project);
  const auto logical_reference = capture_simulation(
      std::move(*logical_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto logical_hybrid = capture_simulation(
      std::move(*logical_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(logical_reference, logical_hybrid);
  assert(
      logical_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(logical_hybrid.result.time == 5);
  assert(logical_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(logical_hybrid.compiled_processes == 2);
  assert(logical_hybrid.compiled_modules == 1);
#endif
  assert((
      logical_hybrid.final_values
      == std::vector<std::string>{
          "0010", "01", "001", "1", "1",
          "0", "1", "1", "0100", "0001"}));

  auto arithmetic_config = config;
  arithmetic_config.project.name = "arithmetic-expression-test";
  arithmetic_config.project.top = "sv:work.arithmetic_app";
  arithmetic_config.build.optimization =
      fsim::project::Optimization::o2;
  arithmetic_config.build.cache_path =
      directory / "arithmetic-cache";
  arithmetic_config.source_sets.clear();
  fsim::project::SourceSet arithmetic_sources;
  arithmetic_sources.language =
      fsim::project::Language::system_verilog;
  arithmetic_sources.standard = "2017";
  arithmetic_sources.library = "work";
  arithmetic_sources.files.push_back(arithmetic_source);
  arithmetic_config.source_sets.push_back(
      std::move(arithmetic_sources));
  fsim::diagnostic::Engine arithmetic_diagnostics;
  auto arithmetic_reference_project =
      fsim::app::build_project(
          arithmetic_config, arithmetic_diagnostics);
  auto arithmetic_hybrid_project =
      fsim::app::build_project(
          arithmetic_config, arithmetic_diagnostics);
  assert(arithmetic_reference_project);
  assert(arithmetic_hybrid_project);
  const auto arithmetic_reference = capture_simulation(
      std::move(*arithmetic_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto arithmetic_hybrid = capture_simulation(
      std::move(*arithmetic_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(arithmetic_reference, arithmetic_hybrid);
  assert(
      arithmetic_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(arithmetic_hybrid.result.time == 4);
  assert(arithmetic_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(arithmetic_hybrid.compiled_processes == 2);
  assert(arithmetic_hybrid.compiled_modules == 1);
#endif
  assert((
      arithmetic_hybrid.final_values
      == std::vector<std::string>{
          "11001000", "00000111", "11000001", "01111000",
          "00011100", "00000100", "11001000", "00111000",
          "00000101", "11111101", "00000010", "00001000",
          "11110001", "11111111", "00000010", "0"}));

  auto select_concat_config = config;
  select_concat_config.project.name = "select-concat-test";
  select_concat_config.project.top = "sv:work.select_concat_app";
  select_concat_config.build.optimization =
      fsim::project::Optimization::o2;
  select_concat_config.build.cache_path =
      directory / "select-concat-cache";
  select_concat_config.source_sets.clear();
  fsim::project::SourceSet select_concat_sources;
  select_concat_sources.language =
      fsim::project::Language::system_verilog;
  select_concat_sources.standard = "2017";
  select_concat_sources.library = "work";
  select_concat_sources.files.push_back(
      select_concat_source);
  select_concat_config.source_sets.push_back(
      std::move(select_concat_sources));
  fsim::diagnostic::Engine select_concat_diagnostics;
  auto select_concat_reference_project =
      fsim::app::build_project(
          select_concat_config, select_concat_diagnostics);
  auto select_concat_hybrid_project =
      fsim::app::build_project(
          select_concat_config, select_concat_diagnostics);
  assert(select_concat_reference_project);
  assert(select_concat_hybrid_project);
  const auto select_concat_reference = capture_simulation(
      std::move(*select_concat_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto select_concat_hybrid = capture_simulation(
      std::move(*select_concat_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      select_concat_reference, select_concat_hybrid);
  assert(
      select_concat_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(select_concat_hybrid.result.time == 2);
  assert(select_concat_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(select_concat_hybrid.compiled_processes == 2);
  assert(select_concat_hybrid.compiled_modules == 1);
#endif
  assert((
      select_concat_hybrid.final_values
      == std::vector<std::string>{
          "Z10100X1", "1100XZ01", "0", "0", "0",
          "Z101", "00XZ", "Z1010XZ01", "10XZ1111",
          "XZ10"}));

  auto vhdl_select_concat_config = config;
  vhdl_select_concat_config.project.name =
      "vhdl-select-concat-test";
  vhdl_select_concat_config.project.top =
      "vhdl:work.vhdl_select_concat_app(rtl)";
  vhdl_select_concat_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_select_concat_config.build.cache_path =
      directory / "vhdl-select-concat-cache";
  vhdl_select_concat_config.source_sets.clear();
  fsim::project::SourceSet vhdl_select_concat_sources;
  vhdl_select_concat_sources.language =
      fsim::project::Language::vhdl;
  vhdl_select_concat_sources.standard = "2008";
  vhdl_select_concat_sources.library = "work";
  vhdl_select_concat_sources.files.push_back(
      vhdl_select_concat_source);
  vhdl_select_concat_config.source_sets.push_back(
      std::move(vhdl_select_concat_sources));
  fsim::diagnostic::Engine vhdl_select_concat_diagnostics;
  auto vhdl_select_concat_reference_project =
      fsim::app::build_project(
          vhdl_select_concat_config,
          vhdl_select_concat_diagnostics);
  auto vhdl_select_concat_hybrid_project =
      fsim::app::build_project(
          vhdl_select_concat_config,
          vhdl_select_concat_diagnostics);
  assert(vhdl_select_concat_reference_project);
  assert(vhdl_select_concat_hybrid_project);
  const auto vhdl_select_concat_reference =
      capture_simulation(
          std::move(*vhdl_select_concat_reference_project),
          fsim::app::SimulationEngine::interpreter);
  const auto vhdl_select_concat_hybrid =
      capture_simulation(
          std::move(*vhdl_select_concat_hybrid_project),
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      vhdl_select_concat_reference,
      vhdl_select_concat_hybrid);
  assert(
      vhdl_select_concat_hybrid.result.status
      == fsim::runtime::RunStatus::completed);
  assert(vhdl_select_concat_hybrid.result.time == 5);
  assert(vhdl_select_concat_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
  assert(vhdl_select_concat_hybrid.compiled_processes == 4);
  assert(vhdl_select_concat_hybrid.compiled_modules == 1);
#endif
  assert((
      vhdl_select_concat_hybrid.final_values
      == std::vector<std::string>{
          "1XZ0", "01Z1", "Z", "Z", "Z", "X",
          "1X", "1Z", "1X10Z1", "10XZ1110",
          "XZ10"}));

  auto vhdl_signed_config = config;
  vhdl_signed_config.project.name = "vhdl-signed-test";
  vhdl_signed_config.project.top =
      "vhdl:work.vhdl_signed_app(rtl)";
  vhdl_signed_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_signed_config.build.cache_path =
      directory / "vhdl-signed-cache";
  vhdl_signed_config.source_sets.clear();
  fsim::project::SourceSet vhdl_signed_sources;
  vhdl_signed_sources.language =
      fsim::project::Language::vhdl;
  vhdl_signed_sources.standard = "2008";
  vhdl_signed_sources.library = "work";
  vhdl_signed_sources.files.push_back(vhdl_signed_source);
  vhdl_signed_config.source_sets.push_back(
      std::move(vhdl_signed_sources));
  fsim::diagnostic::Engine vhdl_signed_diagnostics;
  auto vhdl_signed_reference_project =
      fsim::app::build_project(
          vhdl_signed_config, vhdl_signed_diagnostics);
  auto vhdl_signed_hybrid_project =
      fsim::app::build_project(
          vhdl_signed_config, vhdl_signed_diagnostics);
  assert(vhdl_signed_reference_project);
  assert(vhdl_signed_hybrid_project);
  const auto vhdl_signed_reference = capture_simulation(
      std::move(*vhdl_signed_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto vhdl_signed_hybrid = capture_simulation(
      std::move(*vhdl_signed_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      vhdl_signed_reference, vhdl_signed_hybrid);
  assert(
      vhdl_signed_hybrid.result.status
      == fsim::runtime::RunStatus::completed);
  assert(vhdl_signed_hybrid.result.time == 0);
  assert(vhdl_signed_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
  assert(vhdl_signed_hybrid.compiled_processes == 3);
  assert(vhdl_signed_hybrid.compiled_modules == 1);
#endif
  assert((
      vhdl_signed_hybrid.final_values
      == std::vector<std::string>{
          "11111011", "00000011", "11111110", "11111000",
          "11110001", "11111111", "11111110", "00000001",
          "1"}));

  auto conditional_statement_config = config;
  conditional_statement_config.project.name =
      "conditional-statement-test";
  conditional_statement_config.project.top =
      "sv:work.conditional_statement_app";
  conditional_statement_config.build.cache_path =
      directory / "conditional-statement-cache";
  conditional_statement_config.source_sets.clear();
  fsim::project::SourceSet conditional_statement_sources;
  conditional_statement_sources.language =
      fsim::project::Language::system_verilog;
  conditional_statement_sources.standard = "2017";
  conditional_statement_sources.library = "work";
  conditional_statement_sources.files.push_back(
      conditional_statement_source);
  conditional_statement_config.source_sets.push_back(
      std::move(conditional_statement_sources));
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    conditional_statement_config.build.optimization =
        optimization;
    fsim::diagnostic::Engine conditional_statement_diagnostics;
    auto conditional_statement_reference_project =
        fsim::app::build_project(
            conditional_statement_config,
            conditional_statement_diagnostics);
    auto conditional_statement_hybrid_project =
        fsim::app::build_project(
            conditional_statement_config,
            conditional_statement_diagnostics);
    assert(conditional_statement_reference_project);
    assert(conditional_statement_hybrid_project);
    const auto conditional_statement_reference =
        capture_simulation(
            std::move(*conditional_statement_reference_project),
            fsim::app::SimulationEngine::interpreter);
    const auto conditional_statement_hybrid =
        capture_simulation(
            std::move(*conditional_statement_hybrid_project),
            fsim::app::SimulationEngine::compiled);
    compare_captures(
        conditional_statement_reference,
        conditional_statement_hybrid);
    assert(
        conditional_statement_hybrid.result.status
        == fsim::runtime::RunStatus::stopped);
    assert(conditional_statement_hybrid.result.time == 3);
    assert(conditional_statement_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
    assert(conditional_statement_hybrid.compiled_processes == 2);
    assert(conditional_statement_hybrid.compiled_modules == 1);
#endif
    assert((
        conditional_statement_hybrid.final_values
        == std::vector<std::string>{
            "1000", "0", "1", "0", "0001"}));
  }

  auto vhdl_conditional_statement_config = config;
  vhdl_conditional_statement_config.project.name =
      "vhdl-conditional-statement-test";
  vhdl_conditional_statement_config.project.top =
      "vhdl:work.vhdl_conditional_statement_app(rtl)";
  vhdl_conditional_statement_config.build.cache_path =
      directory / "vhdl-conditional-statement-cache";
  vhdl_conditional_statement_config.source_sets.clear();
  fsim::project::SourceSet vhdl_conditional_statement_sources;
  vhdl_conditional_statement_sources.language =
      fsim::project::Language::vhdl;
  vhdl_conditional_statement_sources.standard = "2008";
  vhdl_conditional_statement_sources.library = "work";
  vhdl_conditional_statement_sources.files.push_back(
      vhdl_conditional_statement_source);
  vhdl_conditional_statement_config.source_sets.push_back(
      std::move(vhdl_conditional_statement_sources));
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    vhdl_conditional_statement_config.build.optimization =
        optimization;
    fsim::diagnostic::Engine vhdl_conditional_statement_diagnostics;
    auto vhdl_conditional_statement_reference_project =
        fsim::app::build_project(
            vhdl_conditional_statement_config,
            vhdl_conditional_statement_diagnostics);
    auto vhdl_conditional_statement_hybrid_project =
        fsim::app::build_project(
            vhdl_conditional_statement_config,
            vhdl_conditional_statement_diagnostics);
    assert(vhdl_conditional_statement_reference_project);
    assert(vhdl_conditional_statement_hybrid_project);
    const auto vhdl_conditional_statement_reference =
        capture_simulation(
            std::move(*vhdl_conditional_statement_reference_project),
            fsim::app::SimulationEngine::interpreter);
    const auto vhdl_conditional_statement_hybrid =
        capture_simulation(
            std::move(*vhdl_conditional_statement_hybrid_project),
            fsim::app::SimulationEngine::compiled);
    compare_captures(
        vhdl_conditional_statement_reference,
        vhdl_conditional_statement_hybrid);
    assert(
        vhdl_conditional_statement_hybrid.result.status
        == fsim::runtime::RunStatus::completed);
    assert(vhdl_conditional_statement_hybrid.result.time == 0);
    assert(vhdl_conditional_statement_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
    assert(
        vhdl_conditional_statement_hybrid.compiled_processes
        == 1);
    assert(
        vhdl_conditional_statement_hybrid.compiled_modules
        == 1);
#endif
    assert((
        vhdl_conditional_statement_hybrid.final_values
        == std::vector<std::string>{
            "X", "1", "1", "1", "1"}));
  }

  auto partial_group_config = config;
  partial_group_config.project.name =
      "partial-specialization-group-test";
  partial_group_config.project.top =
      "sv:work.partial_group";
  partial_group_config.build.optimization =
      fsim::project::Optimization::o2;
  partial_group_config.build.cache_path =
      directory / "partial-group-cache";
  partial_group_config.source_sets.clear();
  fsim::project::SourceSet partial_group_sources;
  partial_group_sources.language =
      fsim::project::Language::system_verilog;
  partial_group_sources.standard = "2017";
  partial_group_sources.library = "work";
  partial_group_sources.files.push_back(partial_group_source);
  partial_group_config.source_sets.push_back(
      std::move(partial_group_sources));
  fsim::diagnostic::Engine partial_group_diagnostics;
  auto partial_group_reference_project =
      fsim::app::build_project(
          partial_group_config, partial_group_diagnostics);
  auto partial_group_hybrid_project =
      fsim::app::build_project(
          partial_group_config, partial_group_diagnostics);
  assert(partial_group_reference_project);
  assert(partial_group_hybrid_project);
  const auto partial_group_reference = capture_simulation(
      std::move(*partial_group_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto partial_group_hybrid = capture_simulation(
      std::move(*partial_group_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(
      partial_group_reference, partial_group_hybrid);
  assert(partial_group_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(partial_group_hybrid.compiled_processes == 1);
  assert(partial_group_hybrid.compiled_modules == 1);
  assert(partial_group_hybrid.native_cache.misses == 1);
  assert(partial_group_hybrid.native_cache.stores == 1);
#endif
  assert(
      partial_group_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(partial_group_hybrid.result.time == 1);

  auto provenance_config = config;
  provenance_config.project.name =
      "specialization-provenance-test";
  provenance_config.project.top = "sv:work.provenance";
  provenance_config.build.optimization =
      fsim::project::Optimization::o2;
  provenance_config.build.cache_path =
      directory / "provenance-cache";
  provenance_config.source_sets.clear();
  fsim::project::SourceSet provenance_sources;
  provenance_sources.language =
      fsim::project::Language::system_verilog;
  provenance_sources.standard = "2017";
  provenance_sources.library = "work";
  provenance_sources.files = {
      provenance_source,
      unused_source,
  };
  provenance_config.source_sets.push_back(
      std::move(provenance_sources));
  struct ProvenanceRun {
    std::string specialization_key;
    bool analysis_cache_hit{};
    CapturedSimulation simulation;
  };
  const auto run_provenance =
      [&](const fsim::project::Config& run_config) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            run_config, run_diagnostics);
        assert(project);
        assert(project->design.specializations().size() == 1);
        assert(project->specialization_cache_keys.size() == 1);
        auto key = project->specialization_cache_keys.front();
        const auto analysis_cache_hit = project->cache_hit;
        auto captured = capture_simulation(
            std::move(*project),
            fsim::app::SimulationEngine::compiled);
        assert(
            captured.result.status
            == fsim::runtime::RunStatus::stopped);
        assert(captured.result.time == 1);
        assert(captured.process_count == 1);
        return ProvenanceRun{
            std::move(key),
            analysis_cache_hit,
            std::move(captured)};
      };

  const auto provenance_cold =
      run_provenance(provenance_config);
  const auto provenance_warm =
      run_provenance(provenance_config);
  assert(!provenance_cold.analysis_cache_hit);
  assert(provenance_warm.analysis_cache_hit);
  assert(
      provenance_warm.specialization_key
      == provenance_cold.specialization_key);
  assert(
      provenance_warm.simulation.final_values
      == provenance_cold.simulation.final_values);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_cold.simulation.compiled_processes == 1);
  assert(provenance_cold.simulation.compiled_modules == 1);
  assert(provenance_cold.simulation.native_cache.hits == 0);
  assert(provenance_cold.simulation.native_cache.misses == 1);
  assert(provenance_cold.simulation.native_cache.stores == 1);
  assert(provenance_warm.simulation.native_cache.hits == 1);
  assert(provenance_warm.simulation.native_cache.misses == 0);
#endif

  // An uninstantiated source changes the project-analysis key, but not the
  // instantiated specialization's native provenance.
  write_unused_source("unused revision 2");
  const auto provenance_unrelated =
      run_provenance(provenance_config);
  assert(!provenance_unrelated.analysis_cache_hit);
  assert(
      provenance_unrelated.specialization_key
      == provenance_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_unrelated.simulation.native_cache.hits == 1);
  assert(provenance_unrelated.simulation.native_cache.misses == 0);
#endif

  // A comment-only owning-source edit leaves SimIR unchanged but must still
  // invalidate the specialization object by source provenance.
  write_provenance_source("top revision 2");
  const auto provenance_changed_source =
      run_provenance(provenance_config);
  assert(!provenance_changed_source.analysis_cache_hit);
  assert(
      provenance_changed_source.specialization_key
      != provenance_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_changed_source.simulation.native_cache.hits == 0);
  assert(provenance_changed_source.simulation.native_cache.misses == 1);
  assert(provenance_changed_source.simulation.native_cache.stores == 1);
#endif

  auto provenance_standard_config = provenance_config;
  provenance_standard_config.source_sets.front().standard = "2012";
  const auto provenance_changed_standard =
      run_provenance(provenance_standard_config);
  assert(!provenance_changed_standard.analysis_cache_hit);
  assert(
      provenance_changed_standard.specialization_key
      != provenance_changed_source.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(provenance_changed_standard.simulation.native_cache.hits == 0);
  assert(provenance_changed_standard.simulation.native_cache.misses == 1);
  assert(provenance_changed_standard.simulation.native_cache.stores == 1);
#endif

  auto parameter_config = config;
  parameter_config.project.name = "parameter-specialization-test";
  parameter_config.project.top = "sv:work.parameter_top";
  parameter_config.build.optimization =
      fsim::project::Optimization::o2;
  parameter_config.build.cache_path =
      directory / "parameter-specialization-cache";
  parameter_config.source_sets.clear();
  fsim::project::SourceSet parameter_sources;
  parameter_sources.language =
      fsim::project::Language::system_verilog;
  parameter_sources.standard = "2017";
  parameter_sources.library = "work";
  parameter_sources.compilation_unit = "file";
  parameter_sources.files = {
      parameter_child_source,
      parameter_top_source,
  };
  parameter_config.source_sets.push_back(
      std::move(parameter_sources));
  struct ParameterRun {
    std::vector<std::pair<std::string, std::string>> keys;
    CapturedSimulation simulation;
  };
  const auto run_parameter_specializations =
      [&](const fsim::app::SimulationEngine engine,
          const std::string_view narrow_value) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            parameter_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 3);
        assert(project->specialization_cache_keys.size() == 3);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (specialization.instance
              == "parameter_top.u_narrow") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"WIDTH", "4"},
                    {"VALUE", std::string{narrow_value}},
                    {"LAST", "3"}}));
          } else if (
              specialization.instance
              == "parameter_top.u_wide") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"WIDTH", "8"},
                    {"VALUE", "3"},
                    {"LAST", "7"}}));
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine);
        return result;
      };
  const auto key_for_instance =
      [](const ParameterRun& run,
         const std::string_view instance) -> const std::string& {
        const auto found = std::find_if(
            run.keys.begin(),
            run.keys.end(),
            [&](const auto& entry) {
              return entry.first == instance;
            });
        assert(found != run.keys.end());
        return found->second;
      };

  const auto parameter_reference =
      run_parameter_specializations(
          fsim::app::SimulationEngine::interpreter, "2");
  const auto parameter_cold =
      run_parameter_specializations(
          fsim::app::SimulationEngine::compiled, "2");
  compare_captures(
      parameter_reference.simulation,
      parameter_cold.simulation);
  assert((
      parameter_cold.simulation.final_values
      == std::vector<std::string>{"0010", "00000011"}));
  assert(parameter_cold.simulation.process_count == 3);
  assert(
      key_for_instance(
          parameter_cold, "parameter_top.u_narrow")
      != key_for_instance(
          parameter_cold, "parameter_top.u_wide"));
#if defined(FSIM_HAS_LLVM)
  assert(parameter_cold.simulation.compiled_processes == 3);
  assert(parameter_cold.simulation.compiled_modules == 3);
  assert(parameter_cold.simulation.native_cache.hits == 0);
  assert(parameter_cold.simulation.native_cache.misses == 3);
  assert(parameter_cold.simulation.native_cache.stores == 3);
#endif

  const auto parameter_warm =
      run_parameter_specializations(
          fsim::app::SimulationEngine::compiled, "2");
  assert(parameter_warm.keys == parameter_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(parameter_warm.simulation.native_cache.hits == 3);
  assert(parameter_warm.simulation.native_cache.misses == 0);
#endif

  write_parameter_top(5);
  const auto parameter_changed_reference =
      run_parameter_specializations(
          fsim::app::SimulationEngine::interpreter, "5");
  const auto parameter_changed =
      run_parameter_specializations(
          fsim::app::SimulationEngine::compiled, "5");
  compare_captures(
      parameter_changed_reference.simulation,
      parameter_changed.simulation);
  assert((
      parameter_changed.simulation.final_values
      == std::vector<std::string>{"0101", "00000011"}));
  assert(
      key_for_instance(
          parameter_changed, "parameter_top")
      != key_for_instance(
          parameter_cold, "parameter_top"));
  assert(
      key_for_instance(
          parameter_changed, "parameter_top.u_narrow")
      != key_for_instance(
          parameter_cold, "parameter_top.u_narrow"));
  assert(
      key_for_instance(
          parameter_changed, "parameter_top.u_wide")
      == key_for_instance(
          parameter_cold, "parameter_top.u_wide"));
#if defined(FSIM_HAS_LLVM)
  assert(parameter_changed.simulation.native_cache.hits == 1);
  assert(parameter_changed.simulation.native_cache.misses == 2);
  assert(parameter_changed.simulation.native_cache.stores == 2);
#endif

  auto generic_config = config;
  generic_config.project.name = "vhdl-generic-specialization-test";
  generic_config.project.top =
      "vhdl:work.vhdl_generic_top(rtl)";
  generic_config.build.optimization =
      fsim::project::Optimization::o2;
  generic_config.build.cache_path =
      directory / "vhdl-generic-specialization-cache";
  generic_config.source_sets.clear();
  fsim::project::SourceSet generic_sources;
  generic_sources.language = fsim::project::Language::vhdl;
  generic_sources.standard = "2008";
  generic_sources.library = "work";
  generic_sources.compilation_unit = "file";
  generic_sources.files = {
      vhdl_generic_entity_source,
      vhdl_generic_architecture_source,
      vhdl_generic_top_source,
  };
  generic_config.source_sets.push_back(
      std::move(generic_sources));
  const auto run_generic_specializations =
      [&](const fsim::app::SimulationEngine engine,
          const std::string_view narrow_value) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            generic_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 3);
        assert(project->specialization_cache_keys.size() == 3);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (specialization.instance
              == "vhdl_generic_top.narrow_child") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"width", "4"},
                    {"value", std::string{narrow_value}},
                    {"last", "3"}}));
            assert(
                specialization.source_dependencies
                == std::vector<std::string>{
                    vhdl_generic_entity_source.string()});
          } else if (
              specialization.instance
              == "vhdl_generic_top.wide_child") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"width", "8"},
                    {"value", "3"},
                    {"last", "7"}}));
            assert(
                specialization.source_dependencies
                == std::vector<std::string>{
                    vhdl_generic_entity_source.string()});
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine, 0);
        return result;
      };

  const auto generic_reference =
      run_generic_specializations(
          fsim::app::SimulationEngine::interpreter, "2");
  const auto generic_cold =
      run_generic_specializations(
          fsim::app::SimulationEngine::compiled, "2");
  compare_captures(
      generic_reference.simulation,
      generic_cold.simulation);
  assert(
      generic_cold.simulation.result.status
      == fsim::runtime::RunStatus::time_limit);
  assert(generic_cold.simulation.result.time == 0);
  assert((
      generic_cold.simulation.final_values
      == std::vector<std::string>{"0010", "00000011"}));
  assert(generic_cold.simulation.process_count == 3);
  assert(
      key_for_instance(
          generic_cold,
          "vhdl_generic_top.narrow_child")
      != key_for_instance(
          generic_cold,
          "vhdl_generic_top.wide_child"));
#if defined(FSIM_HAS_LLVM)
  assert(generic_cold.simulation.compiled_processes == 3);
  assert(generic_cold.simulation.compiled_modules == 3);
  assert(generic_cold.simulation.native_cache.hits == 0);
  assert(generic_cold.simulation.native_cache.misses == 3);
  assert(generic_cold.simulation.native_cache.stores == 3);
#endif

  const auto generic_warm =
      run_generic_specializations(
          fsim::app::SimulationEngine::compiled, "2");
  assert(generic_warm.keys == generic_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(generic_warm.simulation.native_cache.hits == 3);
  assert(generic_warm.simulation.native_cache.misses == 0);
#endif

  write_vhdl_generic_top(5);
  const auto generic_changed_reference =
      run_generic_specializations(
          fsim::app::SimulationEngine::interpreter, "5");
  const auto generic_changed =
      run_generic_specializations(
          fsim::app::SimulationEngine::compiled, "5");
  compare_captures(
      generic_changed_reference.simulation,
      generic_changed.simulation);
  assert((
      generic_changed.simulation.final_values
      == std::vector<std::string>{"0101", "00000011"}));
  assert(
      key_for_instance(
          generic_changed, "vhdl_generic_top")
      != key_for_instance(
          generic_cold, "vhdl_generic_top"));
  assert(
      key_for_instance(
          generic_changed,
          "vhdl_generic_top.narrow_child")
      != key_for_instance(
          generic_cold,
          "vhdl_generic_top.narrow_child"));
  assert(
      key_for_instance(
          generic_changed,
          "vhdl_generic_top.wide_child")
      == key_for_instance(
          generic_cold,
          "vhdl_generic_top.wide_child"));
#if defined(FSIM_HAS_LLVM)
  assert(generic_changed.simulation.native_cache.hits == 1);
  assert(generic_changed.simulation.native_cache.misses == 2);
  assert(generic_changed.simulation.native_cache.stores == 2);
#endif

  write_vhdl_generic_entity("interface revision 2");
  const auto generic_interface_changed =
      run_generic_specializations(
          fsim::app::SimulationEngine::compiled, "5");
  assert(
      key_for_instance(
          generic_interface_changed,
          "vhdl_generic_top")
      == key_for_instance(
          generic_changed,
          "vhdl_generic_top"));
  assert(
      key_for_instance(
          generic_interface_changed,
          "vhdl_generic_top.narrow_child")
      != key_for_instance(
          generic_changed,
          "vhdl_generic_top.narrow_child"));
  assert(
      key_for_instance(
          generic_interface_changed,
          "vhdl_generic_top.wide_child")
      != key_for_instance(
          generic_changed,
          "vhdl_generic_top.wide_child"));
#if defined(FSIM_HAS_LLVM)
  assert(
      generic_interface_changed.simulation.native_cache.hits
      == 1);
  assert(
      generic_interface_changed.simulation.native_cache.misses
      == 2);
  assert(
      generic_interface_changed.simulation.native_cache.stores
      == 2);
#endif

  const auto package_base_constant_source =
      directory / "package_base_constants.vhd";
  const auto package_constant_source =
      directory / "package_constants.vhd";
  const auto package_base_context_source =
      directory / "package_base_context.vhd";
  const auto package_context_source =
      directory / "package_context.vhd";
  const auto unused_package_source =
      directory / "unused_package.vhd";
  const auto package_constant_user_source =
      directory / "package_constant_user.vhd";
  const auto write_package_constants =
      [&](const std::uint64_t base_value) {
        std::ofstream output(package_base_constant_source);
        output << "package base_constants is\n"
               << "  constant base_width : natural := 4;\n"
               << "  constant base_value : natural := "
               << base_value << ";\n"
               << "end package base_constants;\n";
      };
  write_package_constants(5);
  {
    std::ofstream output(package_constant_source);
    output << R"(
use work.base_constants.all;
package constants is
  constant width : natural := base_width;
  constant next_value : natural := base_value + 1;
end package constants;
)";
  }
  {
    std::ofstream output(package_base_context_source);
    output << R"(
context package_base_context is
  library work;
  use work.constants.all;
end context package_base_context;
)";
  }
  const auto write_package_context =
      [&](const std::string_view revision) {
        std::ofstream output(package_context_source);
        output << "-- " << revision << '\n'
               << "context package_context is\n"
               << "  context work.package_base_context;\n"
               << "end context package_context;\n";
      };
  write_package_context("context revision 1");
  const auto write_unused_package =
      [&](const std::string_view revision) {
        std::ofstream output(unused_package_source);
        output << "-- " << revision << '\n'
               << "package unused_constants is\n"
               << "  constant unrelated : natural := 99;\n"
               << "end package unused_constants;\n";
      };
  write_unused_package("unused revision 1");
  {
    std::ofstream output(package_constant_user_source);
    output << R"(
entity package_constant_user is
  port (
    observed : out unsigned(
      support.constants.width - 1 downto 0)
  );
end entity package_constant_user;

context support.package_context;
architecture rtl of package_constant_user is
  signal local_value : unsigned(width - 1 downto 0);
begin
  local_value <= next_value;
  observed <= local_value + 1;
end architecture rtl;
)";
  }
  auto package_constant_config = config;
  package_constant_config.project.name =
      "vhdl-package-constant-test";
  package_constant_config.project.top =
      "vhdl:work.package_constant_user(rtl)";
  package_constant_config.build.optimization =
      fsim::project::Optimization::o2;
  package_constant_config.build.cache_path =
      directory / "vhdl-package-constant-cache";
  package_constant_config.source_sets.clear();
  fsim::project::SourceSet package_library_sources;
  package_library_sources.language =
      fsim::project::Language::vhdl;
  package_library_sources.standard = "2008";
  package_library_sources.library = "support";
  package_library_sources.compilation_unit = "file";
  package_library_sources.files = {
      package_base_constant_source,
      package_constant_source,
      package_base_context_source,
      package_context_source,
      unused_package_source,
  };
  package_constant_config.source_sets.push_back(
      std::move(package_library_sources));
  fsim::project::SourceSet package_user_sources;
  package_user_sources.language =
      fsim::project::Language::vhdl;
  package_user_sources.standard = "2008";
  package_user_sources.library = "work";
  package_user_sources.compilation_unit = "file";
  package_user_sources.files = {package_constant_user_source};
  package_constant_config.source_sets.push_back(
      std::move(package_user_sources));
  struct PackageConstantRun {
    std::string specialization_key;
    CapturedSimulation simulation;
  };
  const auto run_package_constants =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            package_constant_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 1);
        assert(project->specialization_cache_keys.size() == 1);
        const auto& dependencies =
            project->design.specializations()
                .front()
                .source_dependencies;
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                package_constant_source.string())
            != dependencies.end());
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                package_base_constant_source.string())
            != dependencies.end());
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                package_base_context_source.string())
            != dependencies.end());
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                package_context_source.string())
            != dependencies.end());
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                unused_package_source.string())
            == dependencies.end());
        auto key = project->specialization_cache_keys.front();
        auto simulation = capture_simulation(
            std::move(*project), engine);
        return PackageConstantRun{
            std::move(key), std::move(simulation)};
      };
  const auto package_constant_reference =
      run_package_constants(
          fsim::app::SimulationEngine::interpreter);
  const auto package_constant_cold =
      run_package_constants(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      package_constant_reference.simulation,
      package_constant_cold.simulation);
  assert((
      package_constant_cold.simulation.final_values
      == std::vector<std::string>{"0111", "0110"}));
  assert(package_constant_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(
      package_constant_cold.simulation.compiled_processes == 2);
  assert(
      package_constant_cold.simulation.compiled_modules == 1);
  assert(package_constant_cold.simulation.native_cache.hits == 0);
  assert(
      package_constant_cold.simulation.native_cache.misses == 1);
  assert(
      package_constant_cold.simulation.native_cache.stores == 1);
#endif

  const auto package_constant_warm =
      run_package_constants(
          fsim::app::SimulationEngine::compiled);
  assert(
      package_constant_warm.specialization_key
      == package_constant_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(package_constant_warm.simulation.native_cache.hits == 1);
  assert(
      package_constant_warm.simulation.native_cache.misses == 0);
#endif

  write_unused_package("unused revision 2");
  const auto package_unrelated_changed =
      run_package_constants(
          fsim::app::SimulationEngine::compiled);
  assert(
      package_unrelated_changed.specialization_key
      == package_constant_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(
      package_unrelated_changed.simulation.native_cache.hits
      == 1);
  assert(
      package_unrelated_changed.simulation.native_cache.misses
      == 0);
#endif

  write_package_context("context revision 2");
  const auto package_context_changed =
      run_package_constants(
          fsim::app::SimulationEngine::compiled);
  assert(
      package_context_changed.specialization_key
      != package_constant_cold.specialization_key);
  assert(
      package_context_changed.simulation.final_values
      == package_constant_cold.simulation.final_values);
#if defined(FSIM_HAS_LLVM)
  assert(
      package_context_changed.simulation.native_cache.hits == 0);
  assert(
      package_context_changed.simulation.native_cache.misses == 1);
  assert(
      package_context_changed.simulation.native_cache.stores == 1);
#endif

  write_package_constants(9);
  const auto package_constant_changed_reference =
      run_package_constants(
          fsim::app::SimulationEngine::interpreter);
  const auto package_constant_changed =
      run_package_constants(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      package_constant_changed_reference.simulation,
      package_constant_changed.simulation);
  assert(
      package_constant_changed.specialization_key
      != package_context_changed.specialization_key);
  assert((
      package_constant_changed.simulation.final_values
      == std::vector<std::string>{"1011", "1010"}));
#if defined(FSIM_HAS_LLVM)
  assert(
      package_constant_changed.simulation.native_cache.hits == 0);
  assert(
      package_constant_changed.simulation.native_cache.misses == 1);
  assert(
      package_constant_changed.simulation.native_cache.stores == 1);
#endif

  const auto systemverilog_base_package_source =
      directory / "systemverilog_base_package.sv";
  const auto systemverilog_derived_package_source =
      directory / "systemverilog_derived_package.sv";
  const auto systemverilog_unused_package_source =
      directory / "systemverilog_unused_package.sv";
  const auto systemverilog_package_user_source =
      directory / "systemverilog_package_user.sv";
  const auto write_systemverilog_base_package =
      [&](const std::uint64_t base_value,
          const std::uint64_t width) {
        std::ofstream output(
            systemverilog_base_package_source);
        output << "package base_values;\n"
               << "  parameter int WIDTH = "
               << width << ";\n"
               << "  localparam int BASE = "
               << base_value << ";\n"
               << "  typedef logic [WIDTH-1:0] word_t;\n"
               << "endpackage : base_values\n";
      };
  write_systemverilog_base_package(5, 4);
  {
    std::ofstream output(
        systemverilog_derived_package_source);
    output << R"(
import base_values::*;
package derived_values;
  localparam int NEXT = BASE + 1;
  typedef base_values::word_t result_t;
endpackage : derived_values
)";
  }
  const auto write_systemverilog_unused_package =
      [&](const std::string_view revision) {
        std::ofstream output(
            systemverilog_unused_package_source);
        output << "// " << revision << '\n'
               << "package unused_values;\n"
               << "  localparam int UNRELATED = 99;\n"
               << "endpackage : unused_values\n";
      };
  write_systemverilog_unused_package("unused revision 1");
  {
    std::ofstream output(
        systemverilog_package_user_source);
    output << R"(
import derived_values::NEXT, derived_values::result_t;
module systemverilog_package_user(
  output result_t observed
);
  assign observed = NEXT;
endmodule
)";
  }
  auto systemverilog_package_config = config;
  systemverilog_package_config.project.name =
      "systemverilog-package-test";
  systemverilog_package_config.project.top =
      "sv:work.systemverilog_package_user";
  systemverilog_package_config.build.optimization =
      fsim::project::Optimization::o2;
  systemverilog_package_config.build.cache_path =
      directory / "systemverilog-package-cache";
  systemverilog_package_config.source_sets.clear();
  fsim::project::SourceSet systemverilog_package_sources;
  systemverilog_package_sources.language =
      fsim::project::Language::system_verilog;
  systemverilog_package_sources.standard = "2017";
  systemverilog_package_sources.library = "work";
  systemverilog_package_sources.compilation_unit = "file";
  systemverilog_package_sources.files = {
      systemverilog_base_package_source,
      systemverilog_derived_package_source,
      systemverilog_unused_package_source,
      systemverilog_package_user_source,
  };
  systemverilog_package_config.source_sets.push_back(
      std::move(systemverilog_package_sources));
  const auto run_systemverilog_packages =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            systemverilog_package_config,
            run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 1);
        assert(project->specialization_cache_keys.size() == 1);
        const auto& dependencies =
            project->design.specializations()
                .front()
                .source_dependencies;
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                systemverilog_base_package_source.string())
            != dependencies.end());
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                systemverilog_derived_package_source.string())
            != dependencies.end());
        assert(
            std::find(
                dependencies.begin(),
                dependencies.end(),
                systemverilog_unused_package_source.string())
            == dependencies.end());
        auto key = project->specialization_cache_keys.front();
        auto simulation = capture_simulation(
            std::move(*project), engine);
        return PackageConstantRun{
            std::move(key), std::move(simulation)};
      };
  const auto systemverilog_package_reference =
      run_systemverilog_packages(
          fsim::app::SimulationEngine::interpreter);
  const auto systemverilog_package_cold =
      run_systemverilog_packages(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      systemverilog_package_reference.simulation,
      systemverilog_package_cold.simulation);
  assert((
      systemverilog_package_cold.simulation.final_values
      == std::vector<std::string>{"0110"}));
  assert(
      systemverilog_package_cold.simulation.process_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(
      systemverilog_package_cold.simulation.compiled_processes
      == 1);
  assert(
      systemverilog_package_cold.simulation.compiled_modules
      == 1);
  assert(
      systemverilog_package_cold.simulation.native_cache.misses
      == 1);
#endif

  const auto systemverilog_package_warm =
      run_systemverilog_packages(
          fsim::app::SimulationEngine::compiled);
  assert(
      systemverilog_package_warm.specialization_key
      == systemverilog_package_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(
      systemverilog_package_warm.simulation.native_cache.hits
      == 1);
#endif

  write_systemverilog_unused_package("unused revision 2");
  const auto systemverilog_package_unrelated =
      run_systemverilog_packages(
          fsim::app::SimulationEngine::compiled);
  assert(
      systemverilog_package_unrelated.specialization_key
      == systemverilog_package_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
  assert(
      systemverilog_package_unrelated
          .simulation.native_cache.hits
      == 1);
#endif

  write_systemverilog_base_package(9, 5);
  const auto systemverilog_package_changed_reference =
      run_systemverilog_packages(
          fsim::app::SimulationEngine::interpreter);
  const auto systemverilog_package_changed =
      run_systemverilog_packages(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      systemverilog_package_changed_reference.simulation,
      systemverilog_package_changed.simulation);
  assert(
      systemverilog_package_changed.specialization_key
      != systemverilog_package_cold.specialization_key);
  assert((
      systemverilog_package_changed.simulation.final_values
      == std::vector<std::string>{"01010"}));
#if defined(FSIM_HAS_LLVM)
  assert(
      systemverilog_package_changed.simulation.native_cache.misses
      == 1);
#endif

  // Explicit mixed-language bindings carry construction actuals from the
  // parent syntax into the selected foreign unit before boundary widths are
  // checked. Exercise both hierarchy directions through the interpreter and
  // the native specialization cache.
  auto sv_to_vhdl_actual_config = config;
  sv_to_vhdl_actual_config.project.name =
      "sv-to-vhdl-construction-actual-test";
  sv_to_vhdl_actual_config.project.top =
      "sv:work.mixed_actual_sv_top";
  sv_to_vhdl_actual_config.build.optimization =
      fsim::project::Optimization::o2;
  sv_to_vhdl_actual_config.build.cache_path =
      directory / "sv-to-vhdl-construction-actual-cache";
  sv_to_vhdl_actual_config.source_sets.clear();
  fsim::project::SourceSet sv_to_vhdl_actual_vhdl_sources;
  sv_to_vhdl_actual_vhdl_sources.language =
      fsim::project::Language::vhdl;
  sv_to_vhdl_actual_vhdl_sources.standard = "2008";
  sv_to_vhdl_actual_vhdl_sources.library = "work";
  sv_to_vhdl_actual_vhdl_sources.compilation_unit = "file";
  sv_to_vhdl_actual_vhdl_sources.files = {
      vhdl_generic_entity_source,
      vhdl_generic_architecture_source,
  };
  sv_to_vhdl_actual_config.source_sets.push_back(
      std::move(sv_to_vhdl_actual_vhdl_sources));
  fsim::project::SourceSet sv_to_vhdl_actual_sv_sources;
  sv_to_vhdl_actual_sv_sources.language =
      fsim::project::Language::system_verilog;
  sv_to_vhdl_actual_sv_sources.standard = "2017";
  sv_to_vhdl_actual_sv_sources.library = "work";
  sv_to_vhdl_actual_sv_sources.compilation_unit = "file";
  sv_to_vhdl_actual_sv_sources.files = {
      mixed_actual_sv_top_source};
  sv_to_vhdl_actual_config.source_sets.push_back(
      std::move(sv_to_vhdl_actual_sv_sources));
  sv_to_vhdl_actual_config.bindings = {
      {"mixed_actual_sv_top.child",
       "vhdl:work.vhdl_generic_child(rtl)",
       std::nullopt},
  };
  const auto run_sv_to_vhdl_actual =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            sv_to_vhdl_actual_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 2);
        assert(project->specialization_cache_keys.size() == 2);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (specialization.instance
              == "mixed_actual_sv_top.child") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"width", "4"},
                    {"value", "5"},
                    {"last", "3"}}));
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine);
        return result;
      };

  const auto sv_to_vhdl_actual_reference =
      run_sv_to_vhdl_actual(
          fsim::app::SimulationEngine::interpreter);
  const auto sv_to_vhdl_actual_cold =
      run_sv_to_vhdl_actual(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      sv_to_vhdl_actual_reference.simulation,
      sv_to_vhdl_actual_cold.simulation);
  assert(
      sv_to_vhdl_actual_cold.simulation.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(sv_to_vhdl_actual_cold.simulation.result.time == 1);
  assert((
      sv_to_vhdl_actual_cold.simulation.final_values
      == std::vector<std::string>{"0101"}));
  assert(sv_to_vhdl_actual_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(
      sv_to_vhdl_actual_cold.simulation.compiled_processes
      == 2);
  assert(
      sv_to_vhdl_actual_cold.simulation.compiled_modules == 2);
  assert(
      sv_to_vhdl_actual_cold.simulation.native_cache.hits == 0);
  assert(
      sv_to_vhdl_actual_cold.simulation.native_cache.misses
      == 2);
  assert(
      sv_to_vhdl_actual_cold.simulation.native_cache.stores
      == 2);
#endif
  const auto sv_to_vhdl_actual_warm =
      run_sv_to_vhdl_actual(
          fsim::app::SimulationEngine::compiled);
  assert(
      sv_to_vhdl_actual_warm.keys
      == sv_to_vhdl_actual_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(
      sv_to_vhdl_actual_warm.simulation.native_cache.hits == 2);
  assert(
      sv_to_vhdl_actual_warm.simulation.native_cache.misses
      == 0);
#endif

  auto vhdl_to_sv_actual_config = config;
  vhdl_to_sv_actual_config.project.name =
      "vhdl-to-sv-construction-actual-test";
  vhdl_to_sv_actual_config.project.top =
      "vhdl:work.mixed_actual_vhdl_top(rtl)";
  vhdl_to_sv_actual_config.build.optimization =
      fsim::project::Optimization::o2;
  vhdl_to_sv_actual_config.build.cache_path =
      directory / "vhdl-to-sv-construction-actual-cache";
  vhdl_to_sv_actual_config.source_sets.clear();
  fsim::project::SourceSet vhdl_to_sv_actual_vhdl_sources;
  vhdl_to_sv_actual_vhdl_sources.language =
      fsim::project::Language::vhdl;
  vhdl_to_sv_actual_vhdl_sources.standard = "2008";
  vhdl_to_sv_actual_vhdl_sources.library = "work";
  vhdl_to_sv_actual_vhdl_sources.compilation_unit = "file";
  vhdl_to_sv_actual_vhdl_sources.files = {
      mixed_actual_vhdl_top_source};
  vhdl_to_sv_actual_config.source_sets.push_back(
      std::move(vhdl_to_sv_actual_vhdl_sources));
  fsim::project::SourceSet vhdl_to_sv_actual_sv_sources;
  vhdl_to_sv_actual_sv_sources.language =
      fsim::project::Language::system_verilog;
  vhdl_to_sv_actual_sv_sources.standard = "2017";
  vhdl_to_sv_actual_sv_sources.library = "work";
  vhdl_to_sv_actual_sv_sources.compilation_unit = "file";
  vhdl_to_sv_actual_sv_sources.files = {
      mixed_actual_sv_child_source};
  vhdl_to_sv_actual_config.source_sets.push_back(
      std::move(vhdl_to_sv_actual_sv_sources));
  vhdl_to_sv_actual_config.bindings = {
      {"mixed_actual_vhdl_top.child",
       "sv:work.mixed_actual_sv_child",
       std::nullopt},
  };
  const auto run_vhdl_to_sv_actual =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            vhdl_to_sv_actual_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 2);
        assert(project->specialization_cache_keys.size() == 2);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (specialization.instance
              == "mixed_actual_vhdl_top.child") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"width", "4"},
                    {"value", "6"},
                    {"last", "3"}}));
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine, 0);
        return result;
      };

  const auto vhdl_to_sv_actual_reference =
      run_vhdl_to_sv_actual(
          fsim::app::SimulationEngine::interpreter);
  const auto vhdl_to_sv_actual_cold =
      run_vhdl_to_sv_actual(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      vhdl_to_sv_actual_reference.simulation,
      vhdl_to_sv_actual_cold.simulation);
  assert(
      vhdl_to_sv_actual_cold.simulation.result.status
      == fsim::runtime::RunStatus::time_limit);
  assert(vhdl_to_sv_actual_cold.simulation.result.time == 0);
  assert((
      vhdl_to_sv_actual_cold.simulation.final_values
      == std::vector<std::string>{"0110"}));
  assert(vhdl_to_sv_actual_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(
      vhdl_to_sv_actual_cold.simulation.compiled_processes
      == 2);
  assert(
      vhdl_to_sv_actual_cold.simulation.compiled_modules == 2);
  assert(
      vhdl_to_sv_actual_cold.simulation.native_cache.hits == 0);
  assert(
      vhdl_to_sv_actual_cold.simulation.native_cache.misses
      == 2);
  assert(
      vhdl_to_sv_actual_cold.simulation.native_cache.stores
      == 2);
#endif
  const auto vhdl_to_sv_actual_warm =
      run_vhdl_to_sv_actual(
          fsim::app::SimulationEngine::compiled);
  assert(
      vhdl_to_sv_actual_warm.keys
      == vhdl_to_sv_actual_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(
      vhdl_to_sv_actual_warm.simulation.native_cache.hits == 2);
  assert(
      vhdl_to_sv_actual_warm.simulation.native_cache.misses
      == 0);
#endif

  // A selected generate branch contributes its label to the stable hierarchy
  // path. Explicit bindings therefore address generated foreign instances
  // without making language-dependent guesses.
  auto generated_mixed_config = config;
  generated_mixed_config.project.name =
      "generated-mixed-hierarchy-test";
  generated_mixed_config.project.top =
      "sv:work.generated_mixed_sv_top";
  generated_mixed_config.build.optimization =
      fsim::project::Optimization::o2;
  generated_mixed_config.build.cache_path =
      directory / "generated-mixed-hierarchy-cache";
  generated_mixed_config.source_sets.clear();
  fsim::project::SourceSet generated_mixed_vhdl_sources;
  generated_mixed_vhdl_sources.language =
      fsim::project::Language::vhdl;
  generated_mixed_vhdl_sources.standard = "2008";
  generated_mixed_vhdl_sources.library = "work";
  generated_mixed_vhdl_sources.compilation_unit = "file";
  generated_mixed_vhdl_sources.files = {
      vhdl_generic_entity_source,
      vhdl_generic_architecture_source,
  };
  generated_mixed_config.source_sets.push_back(
      std::move(generated_mixed_vhdl_sources));
  fsim::project::SourceSet generated_mixed_sv_sources;
  generated_mixed_sv_sources.language =
      fsim::project::Language::system_verilog;
  generated_mixed_sv_sources.standard = "2017";
  generated_mixed_sv_sources.library = "work";
  generated_mixed_sv_sources.compilation_unit = "file";
  generated_mixed_sv_sources.files = {
      generated_mixed_sv_top_source};
  generated_mixed_config.source_sets.push_back(
      std::move(generated_mixed_sv_sources));
  generated_mixed_config.bindings = {
      {"generated_mixed_sv_top.foreign_branch.child",
       "vhdl:work.vhdl_generic_child(rtl)",
       std::nullopt},
  };
  const auto run_generated_mixed =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            generated_mixed_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 2);
        assert(project->specialization_cache_keys.size() == 2);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (specialization.instance
              == "generated_mixed_sv_top.foreign_branch.child") {
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"width", "4"},
                    {"value", "9"},
                    {"last", "3"}}));
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine);
        return result;
      };

  const auto generated_mixed_reference =
      run_generated_mixed(
          fsim::app::SimulationEngine::interpreter);
  const auto generated_mixed_cold =
      run_generated_mixed(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      generated_mixed_reference.simulation,
      generated_mixed_cold.simulation);
  assert(
      generated_mixed_cold.simulation.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(generated_mixed_cold.simulation.result.time == 1);
  assert((
      generated_mixed_cold.simulation.final_values
      == std::vector<std::string>{"1001"}));
  assert(generated_mixed_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(
      generated_mixed_cold.simulation.compiled_processes == 2);
  assert(
      generated_mixed_cold.simulation.compiled_modules == 2);
  assert(
      generated_mixed_cold.simulation.native_cache.hits == 0);
  assert(
      generated_mixed_cold.simulation.native_cache.misses == 2);
  assert(
      generated_mixed_cold.simulation.native_cache.stores == 2);
#endif
  const auto generated_mixed_warm =
      run_generated_mixed(
          fsim::app::SimulationEngine::compiled);
  assert(generated_mixed_warm.keys == generated_mixed_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(
      generated_mixed_warm.simulation.native_cache.hits == 2);
  assert(
      generated_mixed_warm.simulation.native_cache.misses == 0);
#endif

  // Loop-generate expansion is also specialization-owned: the loop variable
  // becomes a constant construction actual and the indexed scope is stable
  // enough to bind every selected foreign child explicitly.
  auto generated_loop_config = config;
  generated_loop_config.project.name =
      "generated-loop-mixed-hierarchy-test";
  generated_loop_config.project.top =
      "sv:work.generated_loop_top";
  generated_loop_config.build.optimization =
      fsim::project::Optimization::o2;
  generated_loop_config.build.cache_path =
      directory / "generated-loop-mixed-hierarchy-cache";
  generated_loop_config.source_sets.clear();
  fsim::project::SourceSet generated_loop_vhdl_sources;
  generated_loop_vhdl_sources.language =
      fsim::project::Language::vhdl;
  generated_loop_vhdl_sources.standard = "2008";
  generated_loop_vhdl_sources.library = "work";
  generated_loop_vhdl_sources.compilation_unit = "file";
  generated_loop_vhdl_sources.files = {
      generated_loop_vhdl_source};
  generated_loop_config.source_sets.push_back(
      std::move(generated_loop_vhdl_sources));
  fsim::project::SourceSet generated_loop_sv_sources;
  generated_loop_sv_sources.language =
      fsim::project::Language::system_verilog;
  generated_loop_sv_sources.standard = "2017";
  generated_loop_sv_sources.library = "work";
  generated_loop_sv_sources.compilation_unit = "file";
  generated_loop_sv_sources.files = {
      generated_loop_sv_top_source};
  generated_loop_config.source_sets.push_back(
      std::move(generated_loop_sv_sources));
  generated_loop_config.bindings = {
      {"generated_loop_top.lanes[0].child",
       "vhdl:work.generated_loop_child(rtl)",
       std::nullopt},
      {"generated_loop_top.lanes[1].child",
       "vhdl:work.generated_loop_child(rtl)",
       std::nullopt},
      {"generated_loop_top.lanes[2].child",
       "vhdl:work.generated_loop_child(rtl)",
       std::nullopt},
  };
  const auto run_generated_loop =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            generated_loop_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 4);
        assert(project->specialization_cache_keys.size() == 4);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (index > 0) {
            const auto lane = index - 1;
            assert(
                specialization.instance
                == "generated_loop_top.lanes["
                    + std::to_string(lane) + "].child");
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"value", std::to_string(lane + 5)}}));
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine);
        return result;
      };

  const auto generated_loop_reference =
      run_generated_loop(
          fsim::app::SimulationEngine::interpreter);
  const auto generated_loop_cold =
      run_generated_loop(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      generated_loop_reference.simulation,
      generated_loop_cold.simulation);
  assert(
      generated_loop_cold.simulation.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(generated_loop_cold.simulation.result.time == 1);
  assert((
      generated_loop_cold.simulation.final_values
      == std::vector<std::string>{"0101", "0110", "0111"}));
  assert(generated_loop_cold.simulation.process_count == 4);
#if defined(FSIM_HAS_LLVM)
  assert(
      generated_loop_cold.simulation.compiled_processes == 4);
  assert(
      generated_loop_cold.simulation.compiled_modules == 4);
  assert(
      generated_loop_cold.simulation.native_cache.hits == 0);
  assert(
      generated_loop_cold.simulation.native_cache.misses == 4);
  assert(
      generated_loop_cold.simulation.native_cache.stores == 4);
#endif
  const auto generated_loop_warm =
      run_generated_loop(
          fsim::app::SimulationEngine::compiled);
  assert(generated_loop_warm.keys == generated_loop_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(
      generated_loop_warm.simulation.native_cache.hits == 4);
  assert(
      generated_loop_warm.simulation.native_cache.misses == 0);
#endif

  auto generated_case_config = config;
  generated_case_config.project.name =
      "generated-case-mixed-hierarchy-test";
  generated_case_config.project.top =
      "sv:work.generated_case_top";
  generated_case_config.build.optimization =
      fsim::project::Optimization::o2;
  generated_case_config.build.cache_path =
      directory / "generated-case-mixed-hierarchy-cache";
  generated_case_config.source_sets.clear();
  fsim::project::SourceSet generated_case_vhdl_sources;
  generated_case_vhdl_sources.language =
      fsim::project::Language::vhdl;
  generated_case_vhdl_sources.standard = "2008";
  generated_case_vhdl_sources.library = "work";
  generated_case_vhdl_sources.compilation_unit = "file";
  generated_case_vhdl_sources.files = {
      generated_loop_vhdl_source};
  generated_case_config.source_sets.push_back(
      std::move(generated_case_vhdl_sources));
  fsim::project::SourceSet generated_case_sv_sources;
  generated_case_sv_sources.language =
      fsim::project::Language::system_verilog;
  generated_case_sv_sources.standard = "2017";
  generated_case_sv_sources.library = "work";
  generated_case_sv_sources.compilation_unit = "file";
  generated_case_sv_sources.files = {
      generated_case_sv_top_source};
  generated_case_config.source_sets.push_back(
      std::move(generated_case_sv_sources));
  generated_case_config.bindings = {
      {"generated_case_top.selected.child",
       "vhdl:work.generated_loop_child(rtl)",
       std::nullopt},
  };
  const auto run_generated_case =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            generated_case_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 2);
        assert(project->specialization_cache_keys.size() == 2);
        ParameterRun result;
        for (std::size_t index = 0;
             index < project->design.specializations().size();
             ++index) {
          const auto& specialization =
              project->design.specializations()[index];
          result.keys.emplace_back(
              specialization.instance,
              project->specialization_cache_keys[index]);
          if (index == 1) {
            assert(
                specialization.instance
                == "generated_case_top.selected.child");
            assert((
                specialization.parameter_values
                == std::vector<
                    std::pair<std::string, std::string>>{
                    {"value", "10"}}));
          }
        }
        result.simulation =
            capture_simulation(std::move(*project), engine);
        return result;
      };

  const auto generated_case_reference =
      run_generated_case(
          fsim::app::SimulationEngine::interpreter);
  const auto generated_case_cold =
      run_generated_case(
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      generated_case_reference.simulation,
      generated_case_cold.simulation);
  assert(
      generated_case_cold.simulation.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(generated_case_cold.simulation.result.time == 1);
  assert((
      generated_case_cold.simulation.final_values
      == std::vector<std::string>{"1010"}));
  assert(generated_case_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
  assert(
      generated_case_cold.simulation.compiled_processes == 2);
  assert(
      generated_case_cold.simulation.compiled_modules == 2);
  assert(
      generated_case_cold.simulation.native_cache.hits == 0);
  assert(
      generated_case_cold.simulation.native_cache.misses == 2);
  assert(
      generated_case_cold.simulation.native_cache.stores == 2);
#endif
  const auto generated_case_warm =
      run_generated_case(
          fsim::app::SimulationEngine::compiled);
  assert(generated_case_warm.keys == generated_case_cold.keys);
#if defined(FSIM_HAS_LLVM)
  assert(
      generated_case_warm.simulation.native_cache.hits == 2);
  assert(
      generated_case_warm.simulation.native_cache.misses == 0);
#endif

  const auto make_generated_behavior_config =
      [&](const std::string_view name,
          const std::string_view top,
          const fsim::project::Language language,
          const std::filesystem::path& behavior_source) {
        auto behavior_config = config;
        behavior_config.project.name = std::string{name};
        behavior_config.project.top = std::string{top};
        behavior_config.build.optimization =
            fsim::project::Optimization::o2;
        behavior_config.build.cache_path =
            directory / (std::string{name} + "-cache");
        behavior_config.source_sets.clear();
        fsim::project::SourceSet behavior_sources;
        behavior_sources.language = language;
        behavior_sources.standard =
            language == fsim::project::Language::vhdl
            ? "2008"
            : "2017";
        behavior_sources.library = "work";
        behavior_sources.compilation_unit = "file";
        behavior_sources.files = {behavior_source};
        behavior_config.source_sets.push_back(
            std::move(behavior_sources));
        return behavior_config;
      };
  auto generated_behavior_sv_config =
      make_generated_behavior_config(
          "generated-behavior-sv-test",
          "sv:work.generated_behavior_sv",
          fsim::project::Language::system_verilog,
          generated_behavior_sv_source);
  auto generated_behavior_vhdl_config =
      make_generated_behavior_config(
          "generated-behavior-vhdl-test",
          "vhdl:work.generated_behavior_vhdl(rtl)",
          fsim::project::Language::vhdl,
          generated_behavior_vhdl_source);
  auto generated_range_behavior_vhdl_config =
      make_generated_behavior_config(
          "generated-range-behavior-vhdl-test",
          "vhdl:work.generated_range_behavior_vhdl(rtl)",
          fsim::project::Language::vhdl,
          generated_behavior_vhdl_source);
  auto generated_static_behavior_sv_config =
      make_generated_behavior_config(
          "generated-static-behavior-sv-test",
          "sv:work.generated_static_behavior_sv",
          fsim::project::Language::system_verilog,
          generated_static_behavior_sv_source);
  auto generated_implicit_behavior_sv_config =
      make_generated_behavior_config(
          "generated-implicit-behavior-sv-test",
          "sv:work.generated_implicit_behavior_sv",
          fsim::project::Language::system_verilog,
          generated_implicit_behavior_sv_source);
  auto generated_block_behavior_vhdl_config =
      make_generated_behavior_config(
          "generated-block-behavior-vhdl-test",
          "vhdl:work.generated_block_behavior_vhdl(rtl)",
          fsim::project::Language::vhdl,
          generated_block_behavior_vhdl_source);
  const auto run_generated_behavior =
      [&](const fsim::project::Config& behavior_config,
          const fsim::app::SimulationEngine engine,
          const std::vector<std::string_view>& local_paths) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            behavior_config, run_diagnostics);
        if (!project) {
          fsim::diagnostic::print_text(
              std::cerr, run_diagnostics);
        }
        assert(project);
        assert(project->design.specializations().size() == 1);
        assert(project->specialization_cache_keys.size() == 1);
        assert(project->design.find_signal("observed"));
        for (const auto local_path : local_paths) {
          assert(project->design.find_signal(local_path));
        }
        ParameterRun result;
        result.keys.emplace_back(
            project->design.specializations().front().instance,
            project->specialization_cache_keys.front());
        result.simulation =
            capture_simulation(std::move(*project), engine);
        return result;
      };
  const auto verify_generated_behavior =
      [&](const fsim::project::Config& behavior_config,
          const std::vector<std::string_view>& local_paths,
          const std::vector<std::string>& expected_values,
          const std::size_t expected_processes) {
        const auto reference = run_generated_behavior(
            behavior_config,
            fsim::app::SimulationEngine::interpreter,
            local_paths);
        const auto cold = run_generated_behavior(
            behavior_config,
            fsim::app::SimulationEngine::compiled,
            local_paths);
        compare_captures(reference.simulation, cold.simulation);
        assert(
            cold.simulation.result.status
            == fsim::runtime::RunStatus::completed);
        assert(cold.simulation.result.time == 0);
        assert(cold.simulation.final_values == expected_values);
        assert(cold.simulation.process_count == expected_processes);
        for (const auto local_path : local_paths) {
          const auto local_separator = local_path.find_last_of('.');
          if (local_separator != std::string_view::npos) {
            const auto local_scope =
                local_path.substr(0, local_separator);
            assert(
                cold.simulation.normalized_vcd.find(
                    "$scope module " + std::string{local_scope}
                    + " $end")
                != std::string::npos);
          }
          const auto local_name =
              local_separator == std::string_view::npos
              ? local_path
              : local_path.substr(local_separator + 1);
          assert(
              cold.simulation.normalized_vcd.find(
                  " " + std::string{local_name} + " $end")
              != std::string::npos);
        }
#if defined(FSIM_HAS_LLVM)
        assert(
            cold.simulation.compiled_processes
            == expected_processes);
        assert(cold.simulation.compiled_modules == 1);
        assert(cold.simulation.native_cache.hits == 0);
        assert(cold.simulation.native_cache.misses == 1);
        assert(cold.simulation.native_cache.stores == 1);
#endif
        const auto warm = run_generated_behavior(
            behavior_config,
            fsim::app::SimulationEngine::compiled,
            local_paths);
        assert(warm.keys == cold.keys);
#if defined(FSIM_HAS_LLVM)
        assert(warm.simulation.native_cache.hits == 1);
        assert(warm.simulation.native_cache.misses == 0);
#endif
      };
  verify_generated_behavior(
      generated_behavior_sv_config,
      {"selected.generated_value"},
      {"0110", "0101"},
      2);
  verify_generated_behavior(
      generated_behavior_vhdl_config,
      {"chosen.generated_value"},
      {"0111", "0110"},
      2);
  verify_generated_behavior(
      generated_range_behavior_vhdl_config,
      {},
      {"1001"},
      1);
  verify_generated_behavior(
      generated_static_behavior_sv_config,
      {"direct_value", "named_scope.nested_value"},
      {"0100", "0010", "0011"},
      3);
  verify_generated_behavior(
      generated_implicit_behavior_sv_config,
      {"implicit_scope.generated_value"},
      {"0111", "0110"},
      2);
  verify_generated_behavior(
      generated_block_behavior_vhdl_config,
      {"static_scope.generated_value"},
      {"1000", "0111"},
      2);

  // Verilog preprocessing consumes exact transitive snapshots. A header edit
  // must invalidate both the analysis object and the owning specialization,
  // while interpreter and compiled execution retain identical semantics.
  const auto preprocessor_include =
      directory / "preprocessor-include";
  std::filesystem::create_directories(preprocessor_include);
  const auto preprocessor_header =
      preprocessor_include / "values.svh";
  const auto preprocessor_source =
      directory / "preprocessor.sv";
  const auto write_preprocessor_header =
      [&](const std::string_view value) {
        std::ofstream output(
            preprocessor_header, std::ios::binary);
        output
            << "`define PREPROCESSED_VALUE " << value << '\n'
            << R"(module preprocessor_app;
  logic [3:0] value;
  initial begin
    value = `PREPROCESSED_VALUE;
    #1 $finish;
  end
endmodule
)";
        assert(output.good());
      };
  write_preprocessor_header("4'b1010");
  {
    std::ofstream output(
        preprocessor_source, std::ios::binary);
    output << R"(`ifdef ENABLE_PREPROCESSOR_APP
`include "values.svh"
`endif
)";
    assert(output.good());
  }
  auto preprocessor_config = config;
  preprocessor_config.project.name = "preprocessor-test";
  preprocessor_config.project.top =
      "sv:work.preprocessor_app";
  preprocessor_config.build.cache_path =
      directory / "preprocessor-cache";
  preprocessor_config.source_sets.clear();
  fsim::project::SourceSet preprocessor_sources;
  preprocessor_sources.language =
      fsim::project::Language::system_verilog;
  preprocessor_sources.standard = "2017";
  preprocessor_sources.library = "work";
  preprocessor_sources.files = {preprocessor_source};
  preprocessor_sources.include_directories = {
      preprocessor_include};
  preprocessor_sources.defines = {
      "ENABLE_PREPROCESSOR_APP=1"};
  preprocessor_config.source_sets.push_back(
      std::move(preprocessor_sources));

  fsim::diagnostic::Engine preprocessor_check_diagnostics;
  const auto preprocessor_checked =
      fsim::app::check_project(
          preprocessor_config,
          preprocessor_check_diagnostics);
  assert(preprocessor_checked);
  assert(preprocessor_checked->hdl_sources.size() == 1);
  assert(
      preprocessor_checked->hdl_sources.front()
          .dependencies.size()
      == 1);
  assert(
      preprocessor_checked->hdl_sources.front()
          .dependencies.front().path.filename()
      == "values.svh");

  const auto run_preprocessed =
      [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            preprocessor_config, run_diagnostics);
        assert(project);
        assert(
            project->specialization_cache_keys.size() == 1);
        auto key =
            project->specialization_cache_keys.front();
        const bool analysis_hit = project->cache_hit;
        auto capture =
            capture_simulation(std::move(*project), engine);
        return std::tuple{
            std::move(key),
            analysis_hit,
            std::move(capture)};
      };
  auto [preprocessor_key_a, preprocessor_miss_a,
        preprocessor_reference_a] =
      run_preprocessed(
          fsim::app::SimulationEngine::interpreter);
  auto [preprocessor_key_a_warm, preprocessor_hit_a,
        preprocessor_hybrid_a] =
      run_preprocessed(
          fsim::app::SimulationEngine::compiled);
  assert(!preprocessor_miss_a);
  assert(preprocessor_hit_a);
  assert(preprocessor_key_a == preprocessor_key_a_warm);
  compare_captures(
      preprocessor_reference_a, preprocessor_hybrid_a);
  assert(
      preprocessor_hybrid_a.final_values
      == std::vector<std::string>{"1010"});

  write_preprocessor_header("4'b0101");
  auto [preprocessor_key_b, preprocessor_miss_b,
        preprocessor_reference_b] =
      run_preprocessed(
          fsim::app::SimulationEngine::interpreter);
  auto [preprocessor_key_b_warm, preprocessor_hit_b,
        preprocessor_hybrid_b] =
      run_preprocessed(
          fsim::app::SimulationEngine::compiled);
  assert(!preprocessor_miss_b);
  assert(preprocessor_hit_b);
  assert(preprocessor_key_b == preprocessor_key_b_warm);
  assert(preprocessor_key_b != preprocessor_key_a);
  compare_captures(
      preprocessor_reference_b, preprocessor_hybrid_b);
  assert(
      preprocessor_hybrid_b.final_values
      == std::vector<std::string>{"0101"});

  const auto shared_macro_source =
      directory / "shared-macros.sv";
  const auto shared_module_source =
      directory / "shared-module.sv";
  const auto write_shared_macro =
      [&](const std::string_view value) {
        std::ofstream output(
            shared_macro_source, std::ios::binary);
        output << "`default_nettype tri0\n"
               << "`celldefine\n"
               << "`define SHARED_RUNTIME_VALUE "
               << value << '\n';
        assert(output.good());
      };
  write_shared_macro("1'b0");
  {
    std::ofstream output(
        shared_module_source, std::ios::binary);
    output << R"(module shared_preprocessor_app;
  assign value = `SHARED_RUNTIME_VALUE;
  initial #1 $finish;
endmodule
`endcelldefine
`resetall
)";
    assert(output.good());
  }
  auto shared_preprocessor_config = config;
  shared_preprocessor_config.project.name =
      "shared-preprocessor-test";
  shared_preprocessor_config.project.top =
      "sv:work.shared_preprocessor_app";
  shared_preprocessor_config.build.cache_path =
      directory / "shared-preprocessor-cache";
  shared_preprocessor_config.source_sets.clear();
  fsim::project::SourceSet shared_preprocessor_sources;
  shared_preprocessor_sources.language =
      fsim::project::Language::system_verilog;
  shared_preprocessor_sources.standard = "2017";
  shared_preprocessor_sources.library = "work";
  shared_preprocessor_sources.compilation_unit = "source-set";
  shared_preprocessor_sources.files = {
      shared_macro_source, shared_module_source};
  shared_preprocessor_config.source_sets.push_back(
      shared_preprocessor_sources);

  fsim::diagnostic::Engine shared_check_diagnostics;
  const auto shared_checked = fsim::app::check_project(
      shared_preprocessor_config, shared_check_diagnostics);
  assert(shared_checked);
  assert(shared_checked->hdl_sources.size() == 2);
  assert(
      !shared_checked->hdl_sources[0]
           .compilation_unit_digest.empty());
  assert(
      shared_checked->hdl_sources[0].compilation_unit_digest
      == shared_checked->hdl_sources[1].compilation_unit_digest);
  assert(shared_checked->parsed.units.size() == 1);
  assert(shared_checked->parsed.units.front().is_cell);
  assert(
      shared_checked->parsed.units.front().default_nettype == "tri0");
  assert(shared_checked->parsed.units.front().signals.size() == 1);
  assert(
      shared_checked->parsed.units.front().signals.front().name == "value");
  assert(
      shared_checked->parsed.units.front().signals.front().type.spelling
      == "tri0");

  const auto run_shared_preprocessor =
      [&](const fsim::project::Config& run_config,
          const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine run_diagnostics;
        auto project = fsim::app::build_project(
            run_config, run_diagnostics);
        assert(project);
        assert(project->specialization_cache_keys.size() == 1);
        assert(project->design.specializations().size() == 1);
        assert(project->design.specializations().front().is_cell);
        auto key = project->specialization_cache_keys.front();
        auto capture =
            capture_simulation(std::move(*project), engine);
        return std::pair{
            std::move(key), std::move(capture)};
      };
  auto [shared_key_a, shared_reference_a] =
      run_shared_preprocessor(
          shared_preprocessor_config,
          fsim::app::SimulationEngine::interpreter);
  auto [shared_key_a_warm, shared_hybrid_a] =
      run_shared_preprocessor(
          shared_preprocessor_config,
          fsim::app::SimulationEngine::compiled);
  assert(shared_key_a == shared_key_a_warm);
  compare_captures(shared_reference_a, shared_hybrid_a);
  assert(
      shared_hybrid_a.final_values
      == std::vector<std::string>{"0"});

  write_shared_macro("1'b1");
  auto [shared_key_b, shared_reference_b] =
      run_shared_preprocessor(
          shared_preprocessor_config,
          fsim::app::SimulationEngine::interpreter);
  auto [shared_key_b_warm, shared_hybrid_b] =
      run_shared_preprocessor(
          shared_preprocessor_config,
          fsim::app::SimulationEngine::compiled);
  assert(shared_key_b == shared_key_b_warm);
  assert(shared_key_b != shared_key_a);
  compare_captures(shared_reference_b, shared_hybrid_b);
  assert(
      shared_hybrid_b.final_values
      == std::vector<std::string>{"1"});

  auto independent_file_config = shared_preprocessor_config;
  independent_file_config.source_sets.front().compilation_unit =
      "file";
  fsim::diagnostic::Engine independent_file_diagnostics;
  assert(
      !fsim::app::check_project(
          independent_file_config,
          independent_file_diagnostics));
  assert(std::any_of(
      independent_file_diagnostics.diagnostics().begin(),
      independent_file_diagnostics.diagnostics().end(),
      [](const fsim::diagnostic::Diagnostic& diagnostic) {
        return diagnostic.code == "FSIM-SV-PP-028";
      }));

  auto combined_preprocessor_config =
      shared_preprocessor_config;
  combined_preprocessor_config.project.name =
      "combined-preprocessor-test";
  combined_preprocessor_config.build.cache_path =
      directory / "combined-preprocessor-cache";
  combined_preprocessor_config.source_sets.clear();
  auto combined_definitions = shared_preprocessor_sources;
  combined_definitions.library = "definitions";
  combined_definitions.compilation_unit = "combined";
  combined_definitions.files = {shared_macro_source};
  auto combined_module = shared_preprocessor_sources;
  combined_module.compilation_unit = "combined";
  combined_module.files = {shared_module_source};
  combined_preprocessor_config.source_sets = {
      std::move(combined_definitions),
      std::move(combined_module)};
  auto [combined_key, combined_reference] =
      run_shared_preprocessor(
          combined_preprocessor_config,
          fsim::app::SimulationEngine::interpreter);
  auto [combined_key_warm, combined_hybrid] =
      run_shared_preprocessor(
          combined_preprocessor_config,
          fsim::app::SimulationEngine::compiled);
  assert(combined_key == combined_key_warm);
  compare_captures(combined_reference, combined_hybrid);
  assert(
      combined_hybrid.final_values
      == std::vector<std::string>{"1"});

  fsim::diagnostic::Engine mixed_diagnostics;
  const auto mixed_manifest =
      std::filesystem::path{FSIM_TEST_SOURCE_DIR}
      / "examples/vertical_slice/fsim.toml";
  auto mixed_config =
      fsim::project::load(mixed_manifest, mixed_diagnostics);
  assert(mixed_config);
  mixed_config->build.cache_path = directory / "mixed-cache";
  mixed_config->run.trace_file.reset();
  auto mixed_reference_project =
      fsim::app::build_project(*mixed_config, mixed_diagnostics);
  auto mixed_hybrid_project =
      fsim::app::build_project(*mixed_config, mixed_diagnostics);
  assert(mixed_reference_project);
  assert(mixed_hybrid_project);
  const auto mixed_reference = capture_simulation(
      std::move(*mixed_reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto mixed_hybrid = capture_simulation(
      std::move(*mixed_hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(mixed_reference, mixed_hybrid);
  assert(
      mixed_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(mixed_hybrid.result.time == 6);

  fsim::app::Simulation simulation(std::move(*first), config.run.max_deltas);
  const auto q = simulation.find_signal("q");
  const auto two_state = simulation.find_signal("two_state");
  assert(q && two_state);
  assert(simulation.read_signal(*two_state).to_msb_string() == "0");
  bool rejected_lossy_deposit = false;
  try {
    simulation.deposit_signal(
        *two_state,
        fsim::runtime::PackedLogic4::from_msb_string("X"));
  } catch (const std::invalid_argument&) {
    rejected_lossy_deposit = true;
  }
  assert(rejected_lossy_deposit);
  const auto result = simulation.run();
  assert(result.status == fsim::runtime::RunStatus::stopped);
  assert(result.time == 3);
  assert(simulation.finished());
  assert(!simulation.poisoned());
  assert(simulation.read_signal(*q).to_msb_string() == "1");

  simulation.force_signal(
      *q, fsim::runtime::PackedLogic4::from_msb_string("0"));
  simulation.deposit_signal(
      *q, fsim::runtime::PackedLogic4::from_msb_string("1"));
  assert(simulation.read_signal(*q).to_msb_string() == "0");
  simulation.release_signal(*q);
  assert(simulation.read_signal(*q).to_msb_string() == "1");

  std::string error;
  assert(fsim::app::parse_time("25ns", "1ns", error) == 25);
  assert(!fsim::app::parse_time("1ps", "1ns", error));
  const auto value = fsim::app::parse_value("10xz", 4, error);
  assert(value && value->to_msb_string() == "10XZ");

  auto compiled_debug_project =
      fsim::app::build_project(config, diagnostics);
  assert(compiled_debug_project);
  const std::string debug_commands =
      "scope\n"
      "scopes\n"
      "scope u_child\n"
      "signals\n"
      "show value\n"
      "scope ..\n"
      "break signal q == 1\n"
      "break time 1ns\n"
      "breakpoints\n"
      "continue\n"
      "delete 2\n"
      "break source tb.sv:14\n"
      "run-until 3ns\n"
      "delete 3\n"
      "locals\n"
      "step statement\n"
      "locals\n"
      "step statement\n"
      "delete 1\n"
      "locals\n"
      "step process\n"
      "where\n"
      "break signal child_y\n"
      "clear\n"
      "breakpoints\n"
      "continue\n"
      "continue\n"
      "step delta\n"
      "quit\n";
  fsim::app::Simulation debug_simulation(
      std::move(*second),
      config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter);
  assert(debug_simulation.compiled_process_count() == 0);
  std::size_t observed_changes = 0;
  debug_simulation.set_signal_change_hook(
      [&observed_changes](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) { ++observed_changes; });
  debug_simulation.start();
  std::istringstream debug_input{debug_commands};
  std::ostringstream debug_output;
  std::ostringstream debug_error;
  assert(
      fsim::app::run_debug_repl(
          debug_simulation, debug_input, debug_output, debug_error)
      == 0);
  assert(debug_error.str().empty());
  const auto transcript = debug_output.str();
  assert(transcript.find("tb.u_child") != std::string::npos);
  assert(
      transcript.find("tb.u_child.value = X") != std::string::npos);
  assert(
      transcript.find("breakpoint 1 set on tb.q == 1")
      != std::string::npos);
  assert(
      transcript.find("breakpoint 2 set at time 1") != std::string::npos);
  assert(
      transcript.find("hit breakpoint 1: tb.q changed to 1 at time 2")
      != std::string::npos);
  assert(
      transcript.find("hit breakpoint 2: time 1") != std::string::npos);
  assert(
      transcript.find("breakpoint 3 set at tb.sv:14")
      != std::string::npos);
  assert(
      transcript.find(
          "hit breakpoint 3: " + source.string() + ":14:")
      != std::string::npos);
  assert(
      transcript.find(" at " + source.string() + ":15:")
      != std::string::npos);
  assert(
      transcript.find(" at " + source.string() + ":16:")
      != std::string::npos);
  assert(
      transcript.find("local_state = 0") != std::string::npos);
  assert(
      transcript.find("local_state = 1") != std::string::npos);
  assert(transcript.find("stopped at time 2") != std::string::npos);
  assert(transcript.find("time 2, delta") != std::string::npos);
  assert(transcript.find("cleared all breakpoints") != std::string::npos);
  assert(transcript.find("no breakpoints") != std::string::npos);
  assert(
      transcript.find("simulation finished at time 3")
      != std::string::npos);
  const auto first_finished =
      transcript.find("simulation has finished");
  assert(first_finished != std::string::npos);
  assert(
      transcript.find("simulation has finished", first_finished + 1)
      != std::string::npos);
  assert(debug_simulation.finished());
  assert(!debug_simulation.poisoned());
  assert(observed_changes > 0);

  fsim::app::Simulation compiled_debug_simulation(
      std::move(*compiled_debug_project),
      config.run.max_deltas,
      fsim::app::SimulationEngine::debug);
#if defined(FSIM_HAS_LLVM)
  assert(compiled_debug_simulation.compiled_process_count() == 2);
  assert(compiled_debug_simulation.compiled_module_count() == 2);
  const auto debug_native_cache =
      compiled_debug_simulation.native_cache_statistics();
  // The same specialization modules were already cached at the configured O2
  // run setting. Cold objects here therefore prove that debug forces O0.
  assert(debug_native_cache.hits == 0);
  assert(debug_native_cache.misses == 2);
  assert(debug_native_cache.stores == 2);
#else
  assert(compiled_debug_simulation.compiled_process_count() == 0);
  assert(compiled_debug_simulation.compiled_module_count() == 0);
#endif
  std::size_t compiled_observed_changes = 0;
  compiled_debug_simulation.set_signal_change_hook(
      [&compiled_observed_changes](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) { ++compiled_observed_changes; });
  compiled_debug_simulation.start();
  std::istringstream compiled_debug_input{debug_commands};
  std::ostringstream compiled_debug_output;
  std::ostringstream compiled_debug_error;
  assert(
      fsim::app::run_debug_repl(
          compiled_debug_simulation,
          compiled_debug_input,
          compiled_debug_output,
          compiled_debug_error)
      == 0);
  assert(compiled_debug_error.str().empty());
  assert(compiled_debug_output.str() == transcript);
  assert(compiled_debug_simulation.finished());
  assert(!compiled_debug_simulation.poisoned());
  assert(compiled_observed_changes == observed_changes);
  for (const auto& signal : debug_simulation.design().signals()) {
    assert(
        compiled_debug_simulation.read_signal(signal.id)
        == debug_simulation.read_signal(signal.id));
  }

  auto poisoned_project = fsim::app::build_project(config, diagnostics);
  assert(poisoned_project);
  fsim::app::Simulation poisoned_simulation(
      std::move(*poisoned_project), config.run.max_deltas);
#if defined(FSIM_HAS_LLVM)
  assert(poisoned_simulation.compiled_process_count() > 0);
#else
  assert(poisoned_simulation.compiled_process_count() == 0);
#endif
  poisoned_simulation.set_signal_change_hook(
      [](
          fsim::runtime::simir::SignalId,
          const fsim::runtime::PackedLogic4&,
          fsim::runtime::SimulationTick,
          std::uint64_t) {
        throw std::runtime_error("fatal signal observer");
      });
  poisoned_simulation.start();
  std::istringstream poisoned_input{
      "continue\n"
      "continue\n"
      "step delta\n"
      "quit\n"};
  std::ostringstream poisoned_output;
  std::ostringstream poisoned_error;
  assert(
      fsim::app::run_debug_repl(
          poisoned_simulation,
          poisoned_input,
          poisoned_output,
          poisoned_error)
      == 0);
  assert(poisoned_simulation.poisoned());
  assert(!poisoned_simulation.finished());
  assert(
      poisoned_error.str().find("fatal signal observer")
      != std::string::npos);
  const auto unavailable =
      poisoned_output.str().find(
          "simulation is unavailable after a fatal runtime error");
  assert(unavailable != std::string::npos);
  assert(
      poisoned_output.str().find(
          "simulation is unavailable after a fatal runtime error",
          unavailable + 1)
      != std::string::npos);

  const auto manifest = directory / "fsim.toml";
  const auto debug_trace = directory / "debug-select.vcd";
  {
    std::ofstream output(manifest);
    output << R"(
schema = 1

[project]
name = "debug-cli-test"
top = "sv:work.tb"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "cli-cache"

[run]
max_deltas = 1000
trace_file = "debug-select.vcd"
trace_filters = ["__none__"]
)";
  }
  std::istringstream cli_input{
      "where\n"
      "trace list\n"
      "trace add q\n"
      "trace list\n"
      "run 1ns\n"
      "trace remove q\n"
      "trace list\n"
      "continue\n"
      "quit\n"};
  std::ostringstream cli_output;
  std::ostringstream cli_error;
  auto services = fsim::app::make_cli_services(cli_input);
  const auto manifest_text = manifest.string();
  const std::vector<const char*> arguments{
      "fsim", "debug", "-p", manifest_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(arguments.size()),
          arguments.data(),
          services,
          cli_output,
          cli_error)
      == 0);
  assert(
      cli_output.str().find("fsim debugger: tb") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
  assert(
      cli_output.str().find(
          "(O0 hybrid, 2 compiled process(es) in "
          "2 specialization module(s))")
      != std::string::npos);
#else
  assert(
      cli_output.str().find("(reference evaluator)")
      != std::string::npos);
#endif
  assert(
      cli_output.str().find("time 0, delta 0, scope tb")
      != std::string::npos);
  assert(
      cli_output.str().find("(no traced signals)")
      != std::string::npos);
  assert(
      cli_output.str().find("tracing tb.q")
      != std::string::npos);
  assert(
      cli_output.str().find("stopped tracing tb.q")
      != std::string::npos);
  std::ifstream debug_trace_stream(debug_trace);
  const std::string debug_vcd{
      std::istreambuf_iterator<char>{debug_trace_stream},
      std::istreambuf_iterator<char>{}};
  assert(!debug_vcd.empty());
  std::string q_identifier;
  std::istringstream debug_vcd_lines{debug_vcd};
  for (std::string line; std::getline(debug_vcd_lines, line);) {
    if (line.starts_with("$var wire 1 ")
        && line.ends_with(" q $end")) {
      std::istringstream declaration{line};
      std::string directive;
      std::string kind;
      std::string width;
      declaration >> directive >> kind >> width >> q_identifier;
      break;
    }
  }
  assert(!q_identifier.empty());
  assert(
      debug_vcd.find("\nx" + q_identifier + "\n")
      != std::string::npos);
  assert(
      debug_vcd.find("\n0" + q_identifier + "\n")
      != std::string::npos);
  assert(
      debug_vcd.find("\n1" + q_identifier + "\n")
      == std::string::npos);

  restored_interrupt_count = 0;
  const auto previous_interrupt_handler =
      std::signal(SIGINT, record_restored_interrupt);
  assert(previous_interrupt_handler != SIG_ERR);
  std::istringstream interrupted_cli_input{
      "continue\n"
      "continue\n"
      "quit\n"};
  InterruptingOutputBuffer interrupted_output_buffer;
  std::ostream interrupted_cli_output{&interrupted_output_buffer};
  std::ostringstream interrupted_cli_error;
  auto interrupted_services =
      fsim::app::make_cli_services(interrupted_cli_input);
  assert(
      fsim::cli::run(
          static_cast<int>(arguments.size()),
          arguments.data(),
          interrupted_services,
          interrupted_cli_output,
          interrupted_cli_error)
      == 0);
  assert(interrupted_cli_error.str().empty());
  const auto interrupted_transcript =
      interrupted_output_buffer.str();
  assert(
      interrupted_transcript.find("process ")
      != std::string::npos);
  assert(
      interrupted_transcript.find("stopped at time 0")
      != std::string::npos);
  assert(
      interrupted_transcript.find("simulation finished at time 3")
      != std::string::npos);
  (void)std::raise(SIGINT);
  assert(restored_interrupt_count == 1);
  assert(std::signal(SIGINT, previous_interrupt_handler) != SIG_ERR);

  const auto differently_named = directory / "different_filename.sv";
  {
    std::ofstream output(differently_named);
    output << "module actual_top; endmodule\n";
  }
  std::ostringstream direct_output;
  std::ostringstream direct_error;
  const auto direct_text = differently_named.string();
  const std::vector<const char*> direct_arguments{
      "fsim", "run", direct_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(direct_arguments.size()),
          direct_arguments.data(),
          services,
          direct_output,
          direct_error)
      == 0);
  assert(
      direct_output.str().find("simulation completed at tick 0")
      != std::string::npos);

  std::ostringstream json_output;
  std::ostringstream json_error;
  const std::vector<const char*> json_arguments{
      "fsim",
      "check",
      "--diagnostics=json",
      "--definitely-invalid"};
  assert(
      fsim::cli::run(
          static_cast<int>(json_arguments.size()),
          json_arguments.data(),
          services,
          json_output,
          json_error)
      == 2);
  assert(
      json_error.str().find("\"code\":\"FSIM-CLI-0001\"")
      != std::string::npos);

  std::ostringstream standard_output;
  std::ostringstream standard_error;
  const std::vector<const char*> standard_arguments{
      "fsim",
      "check",
      "--standard=bogus",
      direct_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(standard_arguments.size()),
          standard_arguments.data(),
          services,
          standard_output,
          standard_error)
      == 1);
  assert(
      standard_error.str().find("unsupported standard 'bogus'")
      != std::string::npos);

  const auto scaled_manifest = directory / "scaled.toml";
  const auto scaled_trace = directory / "scaled.vcd";
  {
    std::ofstream output(scaled_manifest);
    output << R"(
schema = 1
[project]
name = "scaled-vcd"
top = "sv:work.tb"
time_resolution = "2ps"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "scaled-cache"

[run]
max_deltas = 1000
trace_file = "scaled.vcd"
)";
  }
  std::ostringstream scaled_output;
  std::ostringstream scaled_error;
  const auto scaled_manifest_text = scaled_manifest.string();
  const std::vector<const char*> scaled_arguments{
      "fsim", "run", "-p", scaled_manifest_text.c_str()};
  assert(
      fsim::cli::run(
          static_cast<int>(scaled_arguments.size()),
          scaled_arguments.data(),
          services,
          scaled_output,
          scaled_error)
      == 0);
  std::ifstream scaled_stream(scaled_trace);
  const std::string scaled_vcd{
      std::istreambuf_iterator<char>{scaled_stream},
      std::istreambuf_iterator<char>{}};
  assert(
      scaled_vcd.find("$timescale 1ps $end") != std::string::npos);
  assert(scaled_vcd.find("#4") != std::string::npos);

  const auto timescale_source = directory / "timescale.sv";
  {
    std::ofstream output(timescale_source);
    output << R"(`timescale 10ns/100ps
module timed;
  initial #2 $finish;
endmodule
)";
  }
  fsim::project::Config timescale_config;
  timescale_config.base_directory = directory;
  timescale_config.project.name = "timescale";
  timescale_config.project.top = "sv:work.timed";
  timescale_config.project.time_resolution = "auto";
  timescale_config.build.cache_path = directory / "timescale-cache";
  timescale_config.run.max_deltas = 1000;
  fsim::project::SourceSet timescale_sources;
  timescale_sources.language =
      fsim::project::Language::system_verilog;
  timescale_sources.standard = "2017";
  timescale_sources.library = "work";
  timescale_sources.files.push_back(timescale_source);
  timescale_config.source_sets.push_back(
      std::move(timescale_sources));
  fsim::diagnostic::Engine timescale_diagnostics;
  auto timed_project =
      fsim::app::build_project(
          timescale_config, timescale_diagnostics);
  assert(timed_project);
  assert(timed_project->time_resolution == "100ps");
  fsim::app::Simulation timed_simulation(
      std::move(*timed_project),
      timescale_config.run.max_deltas);
  const auto timed_result = timed_simulation.run();
  assert(timed_result.status == fsim::runtime::RunStatus::stopped);
  assert(timed_result.time == 200);
  timescale_config.project.time_resolution = "1ns";
  fsim::diagnostic::Engine coarse_time_diagnostics;
  assert(!fsim::app::build_project(
      timescale_config, coarse_time_diagnostics));
  assert(coarse_time_diagnostics.has_error());

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  std::cout << "application tests passed\n";
}
