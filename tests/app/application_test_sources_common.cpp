// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <cassert>
#include <fstream>
#include <string_view>

namespace fsim::test {

void ApplicationTestFixture::create_common_sources() {
source = directory / "tb.sv";
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
scheduled_source = directory / "scheduled.sv";
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
class_source = directory / "classes.sv";
{
  std::ofstream output(class_source);
  output << R"(
class AppBase;
  static int shared = 2;
  static AppBase shared_peer;
  AppBase peer;
  AppBase fixed_handles[0:1];
  AppBase dynamic_handles[];
  AppBase queued_handles[$:2];
  AppBase associative_handles[int];
  logic [7:0] value;
  function new(int initial_value = 0);
    value = initial_value;
  endfunction
  virtual function int bump(input int amount);
    value = value + amount;
    return value;
  endfunction
  static function int add_shared(input int amount);
    shared = shared + amount;
    return shared;
  endfunction
  static task bump_shared(input int amount, output int observed);
    #1;
    shared = shared + amount;
    observed = shared;
  endtask
  static function AppBase remember_shared(input AppBase candidate);
    shared_peer = candidate;
    return shared_peer;
  endfunction
  function AppBase remember(input AppBase candidate);
    peer = candidate;
    return peer;
  endfunction
  task delayed_remember(input AppBase candidate, output AppBase observed);
    #1;
    peer = candidate;
    observed = peer;
  endtask
endclass

class AppDerived extends AppBase;
  logic [7:0] value;
  logic [3:0] generated_value;
  function new(int initial_value = 0);
    super.new(initial_value);
    value = initial_value;
  endfunction
  function int bump(input int amount);
    value = value + amount + 1;
    return value;
  endfunction
  function int transfer(input int amount, output int prior,
                        inout int accumulator, ref int alias_value);
    int local_value;
    local_value = value;
    prior = local_value;
    accumulator = accumulator + amount;
    alias_value = alias_value + 1;
    value = accumulator + alias_value;
    return value;
  endfunction
  function int recurse(input int count);
    if (count == 0) return value;
    return recurse(count - 1);
  endfunction
  function static int next_count();
    int calls = 0;
    calls = calls + 1;
    return calls;
  endfunction
  function int bump_base(input int amount);
    return super.bump(amount);
  endfunction
  task delayed_update(input logic [31:0] amount,
                      output logic [31:0] observed,
                      inout logic [31:0] accumulator);
    logic [31:0] retained_across_delay;
    retained_across_delay = value + amount;
    #1;
    value = retained_across_delay;
    accumulator = accumulator + value;
    observed = value;
    assert (value == retained_across_delay);
  endtask
endclass

module class_top;
  logic task_trigger;
  logic task_event_observed;
  class LocalWaiter;
    task wait_for_trigger(output logic observed);
      @(posedge task_trigger);
      observed = 1'b1;
    endtask
  endclass
  AppDerived source_object;
  AppBase source_other;
  AppBase source_returned;
  AppBase source_base_view;
  AppBase source_cast;
  AppDerived source_failed_cast;
  AppBase source_function_returned;
  AppBase source_task_returned;
  LocalWaiter source_waiter;
  int source_prior;
  int source_accumulator;
  int source_alias;
  int source_result;
  int source_recursive;
  int source_static_first;
  int source_static_second;
  int source_base_result;
  int source_virtual_result;
  logic [31:0] source_task_observed;
  logic [31:0] source_task_accumulator;
  int source_static_result;
  int source_static_task_observed;
  int source_static_property;
  logic source_handle_alias;
  logic source_handle_property_alias;
  logic source_task_handle_alias;
  logic source_static_handle_alias;
  logic source_fixed_handle_alias;
  logic source_dynamic_handle_alias;
  logic source_queued_handle_alias;
  logic source_queue_pop_alias;
  int source_queue_size;
  logic source_associative_handle_alias;
  logic source_cast_alias;
  logic source_failed_cast_preserved;
  logic source_function_handle_alias;
  logic source_module_task_handle_alias;
  logic source_final_seen;
  logic [7:0] source_property;
  function automatic AppBase pass_handle(input AppBase candidate);
    return candidate;
  endfunction
  task automatic delayed_handle(input AppBase candidate,
                                output AppBase observed);
    #1;
    observed = candidate;
  endtask
  initial begin
    source_accumulator = 4;
    source_alias = 5;
    source_object = new(3);
    source_other = new(9);
    source_cast = null;
    source_cast_alias =
        $cast(source_cast, source_object) && source_cast == source_object;
    source_failed_cast = source_object;
    source_failed_cast_preserved =
        !$cast(source_failed_cast, source_other)
        && source_failed_cast == source_object;
    source_function_returned = pass_handle(source_other);
    source_function_handle_alias = source_function_returned == source_other;
    source_returned = source_object.remember(source_other);
    source_handle_alias = source_returned == source_other;
    source_handle_property_alias = source_object.peer == source_other;
    source_object.delayed_remember(source_other, source_returned);
    source_task_handle_alias = source_returned == source_other;
    source_returned = AppBase::remember_shared(source_other);
    source_static_handle_alias = source_returned == AppBase::shared_peer;
    source_object.fixed_handles[0] = source_other;
    source_fixed_handle_alias =
        source_object.fixed_handles[0] == source_other;
    source_object.dynamic_handles = new[2];
    source_object.dynamic_handles[1] = source_other;
    source_dynamic_handle_alias =
        source_object.dynamic_handles[1] == source_other;
    source_object.queued_handles.push_back(source_other);
    source_queued_handle_alias =
        source_object.queued_handles[0] == source_other;
    source_queue_size = source_object.queued_handles.size();
    source_returned = source_object.queued_handles.pop_front();
    source_queue_pop_alias = source_returned == source_other;
    source_object.associative_handles[7] = source_other;
    source_associative_handle_alias =
        source_object.associative_handles[7] == source_other;
    source_waiter = new;
    source_result = source_object.transfer(
        2, source_prior, source_accumulator, source_alias);
    source_recursive = source_object.recurse(3);
    source_static_first = source_object.next_count();
    source_static_second = source_object.next_count();
    source_base_result = source_object.bump_base(2);
    source_task_accumulator = 1;
    source_object.delayed_update(
        4, source_task_observed, source_task_accumulator);
    source_waiter.wait_for_trigger(task_event_observed);
    delayed_handle(source_other, source_task_returned);
    source_module_task_handle_alias = source_task_returned == source_other;
    source_base_view = source_object;
    source_virtual_result = source_base_view.bump(1);
    source_static_result = AppBase::add_shared(3);
    AppBase::bump_shared(2, source_static_task_observed);
    source_static_property = AppDerived::shared;
    source_property = source_object.value;
    #3 $finish;
  end
  initial begin
    task_trigger = 1'b0;
    #2 task_trigger = 1'b1;
  end
  final begin
    source_final_seen = source_object != null;
  end
endmodule

module class_generated #(
    parameter int INITIAL = 1)(
    output logic ready,
    output int static_value);
  generate
    if (INITIAL >= 0) begin : leaf
      AppBase generated_object;
      initial begin
        generated_object = new(INITIAL);
        ready = generated_object.bump(0) == INITIAL;
        static_value = AppBase::add_shared(INITIAL);
      end
    end
  endgenerate
endmodule

module class_wrapper #(
    parameter int INITIAL = 1)(
    output logic ready,
    output int static_value);
  class_generated #(.INITIAL(INITIAL)) child(ready, static_value);
endmodule

module class_root_a;
  logic ready;
  int static_value;
  class_wrapper #(.INITIAL(4)) tree(ready, static_value);
  initial #2 $finish;
endmodule

module class_root_b;
  logic ready;
  int static_value;
  class_wrapper #(.INITIAL(7)) tree(ready, static_value);
endmodule
)";
}
sensitivity_source = directory / "sensitivity.sv";
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
vhdl_wait_source = directory / "vhdl_wait.vhd";
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
wildcard_source = directory / "wildcard.sv";
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
case_source = directory / "case.sv";
{
  std::ofstream output(case_source);
  output << R"(
module case_app;
  logic [1:0] selector;
  logic [1:0] result;
  logic casez_selector_wildcard;
  logic casez_x_literal;
  logic casez_item_wildcard;
  logic casex_selector_wildcard;
  logic casex_known_mismatch;
  always_comb case (selector)
    2'b00: result = 2'b00;
    2'b01, 2'b10: result = 2'b01;
    2'bx0: result = 2'b10;
    2'bz1: result = 2'b11;
    default: result = 2'b00;
  endcase
  initial begin
    casez (2'b0z)
      2'b00: casez_selector_wildcard = 1'b1;
      default: casez_selector_wildcard = 1'b0;
    endcase
    casez (2'b0x)
      2'b00: casez_x_literal = 1'b0;
      2'b0x: casez_x_literal = 1'b1;
      default: casez_x_literal = 1'b0;
    endcase
    casez (2'b00)
      2'b0?: casez_item_wildcard = 1'b1;
      default: casez_item_wildcard = 1'b0;
    endcase
    casex (2'b0x)
      2'b00: casex_selector_wildcard = 1'b1;
      default: casex_selector_wildcard = 1'b0;
    endcase
    casex (2'b1x)
      2'b0z: casex_known_mismatch = 1'b1;
      default: casex_known_mismatch = 1'b0;
    endcase
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
conditional_source = directory / "conditional.sv";
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
comparison_source = directory / "comparison.sv";
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
  logic case_eq;
  logic case_neq;
  logic wildcard_eq;
  logic wildcard_neq;
  always_comb begin
    neq = lhs != rhs;
    lt = lhs < rhs;
    le = lhs <= rhs;
    gt = lhs > rhs;
    ge = lhs >= rhs;
    logical_not = !lhs;
    case_eq = lhs === rhs;
    case_neq = lhs !== rhs;
    wildcard_eq = lhs ==? 4'b01?0;
    wildcard_neq = lhs !=? rhs;
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
logical_source = directory / "logical.sv";
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
  logic gate_buf;
  logic gate_buf_second;
  logic gate_not;
  logic gate_and;
  logic gate_nand;
  logic gate_or;
  logic gate_nor;
  logic gate_xor;
  logic gate_xnor;
  buf #1 (gate_buf, lhs[0]),
      second_buffer (gate_buf_second, lhs[1]);
  not gate_inverter (gate_not, lhs[0]);
  and (gate_and, lhs[0], rhs[0], lhs[1]);
  nand (gate_nand, lhs[0], rhs[0], lhs[1]);
  or (gate_or, lhs[0], rhs[0], lhs[1]);
  nor (gate_nor, lhs[0], rhs[0], lhs[1]);
  xor (gate_xor, lhs[0], rhs[0], lhs[1]);
  xnor (gate_xnor, lhs[0], rhs[0], lhs[1]);
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
arithmetic_source = directory / "arithmetic.sv";
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
select_concat_source =
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
vhdl_select_concat_source =
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
vhdl_signed_source =
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
  signal shifted_left : signed(7 downto 0);
  signal shifted_right : signed(7 downto 0);
  signal shifted_arithmetic : signed(7 downto 0);
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
    shifted_left <= lhs sll 1;
    shifted_right <= lhs srl 1;
    shifted_arithmetic <= lhs sra 1;
  end process;
end architecture;
)";
}
conditional_statement_source =
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
  logic [3:0] sequential_loop_result;
  logic [1:0] null_loop_result;
  logic [2:0] repeat_result;
  logic [2:0] runtime_loop_result;
  logic forever_clock;
  logic [3:0] static_continue_result;
  logic [3:0] runtime_control_result;
  logic [3:0] post_test_control_result;
  logic [3:0] post_test_once_result;
  logic wait_gate;
  logic wait_observed;
  logic constant_wait_result;
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
    sequential_loop_result = 4'b0000;
    null_loop_result = 2'b00;
    for (int lane = 0; lane < 4; lane++)
      sequential_loop_result[lane] = 1'b1;
    for (int lane = 3; lane >= 2; --lane)
      sequential_loop_result[lane] = 1'b0;
    for (int lane = 2; lane < 1; lane += 1)
      null_loop_result[0] = 1'b1;
    repeat_result = 3'b000;
    repeat (3) repeat_result = repeat_result + 1;
    repeat (0) repeat_result = 3'b111;
    runtime_loop_result = 3'b000;
    while (runtime_loop_result < 3)
      runtime_loop_result = runtime_loop_result + 1;
    static_continue_result = 4'b0000;
    repeat (3) begin
      static_continue_result = static_continue_result + 1;
      continue;
      static_continue_result = static_continue_result + 4;
    end
    runtime_control_result = 4'b0000;
    while (runtime_control_result < 5) begin
      runtime_control_result = runtime_control_result + 1;
      if (runtime_control_result == 2) continue;
      if (runtime_control_result == 5) break;
      runtime_control_result = runtime_control_result + 1;
    end
    post_test_control_result = 4'b0000;
    do begin
      post_test_control_result = post_test_control_result + 1;
      if (post_test_control_result == 1) continue;
      if (post_test_control_result == 4) break;
      post_test_control_result = post_test_control_result + 1;
    end while (post_test_control_result < 6);
    post_test_once_result = 4'b0000;
    do post_test_once_result = post_test_once_result + 1;
    while (1'b0);
    wait_gate = 1'b0;
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
    #1 begin
      selector = 4'b0010;
      wait_gate = 1'bx;
    end
    #1 begin
      selector = 4'b1000;
      wait_gate = 1'b1;
    end
    #1 $finish;
  end
  initial begin
    wait_observed = 1'b0;
    wait (wait_gate) wait_observed = 1'b1;
  end
  initial begin
    constant_wait_result = 1'b0;
    wait (1'b0);
    constant_wait_result = 1'b1;
  end
  initial begin
    forever_clock = 1'b0;
    forever #1 forever_clock = ~forever_clock;
  end
endmodule
)";
}
vhdl_conditional_statement_source =
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
  signal sequential_case_result : std_logic_vector(1 downto 0);
  signal sequential_loop_result : std_logic_vector(3 downto 0);
  signal null_loop_result : std_logic_vector(1 downto 0);
  signal runtime_while_result : boolean;
  signal static_control_result : boolean;
  signal runtime_control_result : boolean;
  signal nested_control_result : boolean;
  signal unconditional_loop_result : boolean;
  signal targeted_control_result : boolean;
  signal wait_gate : boolean;
  signal wait_observed : boolean;
  signal wait_timed : boolean;
  signal wait_constant_timeout : boolean;
  signal wait_permanent : boolean;
begin
  choose: process(trigger)
    variable assembled : std_logic_vector(3 downto 0) := "0000";
    variable untouched : std_logic_vector(1 downto 0) := "00";
    variable keep_going : boolean := true;
    variable while_result : boolean := false;
    variable static_control : boolean := false;
    variable runtime_control : boolean := false;
    variable nested_control : boolean := false;
    variable keep_controlling : boolean := true;
    variable skipped : boolean := false;
    variable unconditional_skipped : boolean := false;
    variable unconditional_result : boolean := false;
    variable targeted_control : boolean := false;
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
    case "10" is
      when "00" | "01" =>
        sequential_case_result <= "00";
      when "10" =>
        sequential_case_result <= "01";
      when others =>
        sequential_case_result <= "11";
    end case;
    for lane in 0 to 3 loop
      assembled(lane) := '1';
    end loop;
    for lane in 3 downto 2 loop
      assembled(lane) := '0';
    end loop;
    for lane in 2 to 1 loop
      untouched(0) := '1';
    end loop;
    while keep_going loop
      while_result := true;
      keep_going := false;
    end loop;
    for lane in 0 to 2 loop
      next when lane = 0;
      static_control := true;
      exit;
    end loop;
    while keep_controlling loop
      if not skipped then
        skipped := true;
        next;
      end if;
      runtime_control := true;
      exit;
    end loop;
    for outer in 0 to 1 loop
      for inner in 0 to 2 loop
        nested_control := true;
        exit;
      end loop;
    end loop;
    loop
      if not unconditional_skipped then
        unconditional_skipped := true;
        next;
      end if;
      unconditional_result := true;
      exit;
    end loop;
    outer_loop: for outer in 0 to 1 loop
      inner_loop: loop
        if outer = 0 then
          next outer_loop;
        end if;
        targeted_control := true;
        exit outer_loop;
      end loop inner_loop;
      targeted_control := false;
    end loop outer_loop;
    sequential_loop_result <= assembled;
    null_loop_result <= untouched;
    runtime_while_result <= while_result;
    static_control_result <= static_control;
    runtime_control_result <= runtime_control;
    nested_control_result <= nested_control;
    unconditional_loop_result <= unconditional_result;
    targeted_control_result <= targeted_control;
  end process;
  wait_driver: process
  begin
    wait for 1 ns;
    wait_gate <= true;
    wait for 2 ns;
    wait_gate <= false;
    wait until false;
  end process;
  wait_observer: process
  begin
    wait on wait_gate until false for 2 ns;
    wait_timed <= true;
    wait on wait_gate until not wait_gate for 2 ns;
    wait_observed <= true;
    wait until true for 1 ns;
    wait_constant_timeout <= true;
    wait until true;
    wait_permanent <= true;
  end process;
end architecture;
)";
}
}

}  // namespace fsim::test
