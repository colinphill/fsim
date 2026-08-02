// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <cassert>
#include <fstream>
#include <string_view>

namespace fsim::test {

void ApplicationTestFixture::create_mixed_language_sources() {
partial_group_source = directory / "partial_group.sv";
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
assertion_source = directory / "assertion.vhd";
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
provenance_source = directory / "provenance.sv";
unused_source = directory / "unused.sv";
write_provenance_source("top revision 1");
write_unused_source("unused revision 1");
parameter_child_source =
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
parameter_top_source =
    directory / "parameter_top.sv";
write_parameter_top(2);
vhdl_generic_entity_source =
    directory / "vhdl_generic_child_entity.vhd";
write_vhdl_generic_entity("interface revision 1");
vhdl_generic_architecture_source =
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
vhdl_generic_top_source =
    directory / "vhdl_generic_top.vhd";
write_vhdl_generic_top(2);
mixed_actual_sv_top_source =
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
mixed_actual_sv_child_source =
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
mixed_actual_vhdl_top_source =
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
generated_mixed_sv_top_source =
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
generated_loop_vhdl_source =
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
generated_loop_sv_top_source =
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
generated_case_sv_top_source =
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
generated_behavior_sv_source =
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
generated_behavior_vhdl_source =
    directory / "generated_behavior.vhd";
{
  std::ofstream output(generated_behavior_vhdl_source);
  output << R"(
package generated_math is
  generic (bias : natural := 0);
  constant selected_value : natural := bias + 1;
end package;

entity generated_behavior_vhdl is
  generic (
    enabled : boolean := true
  );
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of generated_behavior_vhdl is
  constant architecture_bias : natural := 5;
begin
  chosen: if enabled generate
    constant base_value : natural := architecture_bias;
    constant local_value : natural := base_value + 1;
    subtype generated_word_t is unsigned(3 downto 0);
    signal generated_value : generated_word_t;
    signal mapped_value : generated_word_t;
    function adjust(value : generated_word_t)
      return generated_word_t;
    function adjust(value : generated_word_t)
      return generated_word_t is
      constant local_increment : natural := 1;
      subtype local_word_t is generated_word_t;
      package function_math is new work.generated_math
        generic map (bias => 0);
      function bump(input_value : local_word_t)
        return local_word_t is
      begin
        return input_value + function_math.selected_value;
      end function bump;
      variable adjusted : local_word_t;
      alias adjusted_alias : local_word_t is adjusted;
    begin
      adjusted_alias := bump(value) + local_increment - 1;
      return adjusted_alias;
    end function adjust;
    procedure drive(
      variable target : out generated_word_t;
      value : generated_word_t);
    procedure drive(
      variable target : out generated_word_t;
      value : generated_word_t) is
      constant local_offset : natural := 0;
      subtype local_word_t is generated_word_t;
      procedure copy_with_offset(
        variable inner_target : out local_word_t;
        inner_value : local_word_t) is
      begin
        inner_target := inner_value + local_offset;
      end procedure copy_with_offset;
      variable driven : local_word_t;
      alias driven_alias : local_word_t is driven;
    begin
      copy_with_offset(driven_alias, adjust(value));
      target := driven_alias;
    end procedure drive;
    generic (amount : natural := 0)
    function shifted(value : generated_word_t)
      return generated_word_t is
      constant local_amount : natural := amount;
      package generic_math is new work.generated_math
        generic map (bias => 0);
      alias input_alias : generated_word_t is value;
    begin
      return adjust(input_alias) + local_amount
        + generic_math.selected_value - 1;
    end function shifted;
    function mapped_shift is new shifted
      generic map (amount => 0);
    generic (amount : natural := 0)
    procedure shifted_drive(
      variable target : out generated_word_t;
      value : generated_word_t) is
      alias target_alias : generated_word_t is target;
    begin
      drive(target_alias, value + amount);
    end procedure shifted_drive;
    procedure mapped_drive is new shifted_drive
      generic map (amount => 0);
    package selected_math is new work.generated_math
      generic map (bias => base_value);
  begin
    generated_value <= selected_math.selected_value;
    mapped_value <= mapped_shift(generated_value);
    worker: process(generated_value)
      constant local_offset : natural := 0;
      subtype local_word_t is generated_word_t;
      package process_math is new work.generated_math
        generic map (bias => 0);
      function process_adjust(input_value : local_word_t)
        return local_word_t is
      begin
        return input_value + process_math.selected_value;
      end function process_adjust;
      procedure process_drive(
        variable inner_target : out local_word_t;
        inner_value : local_word_t) is
      begin
        inner_target := process_adjust(inner_value);
      end procedure process_drive;
      variable process_value : local_word_t;
      alias process_alias : local_word_t is process_value;
    begin
      process_drive(process_alias, generated_value + local_offset);
      mapped_drive(observed, process_alias);
    end process;
  else generate
    observed <= 1;
end generate chosen;
end architecture;

entity generated_loop_declarations_vhdl is
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of generated_loop_declarations_vhdl is
  subtype architecture_word_t is unsigned(3 downto 0);
  signal architecture_value : architecture_word_t;
  alias architecture_alias : architecture_word_t is architecture_value;
  function architecture_adjust(value : architecture_word_t)
    return architecture_word_t is
    package local_math is new work.generated_math
      generic map (bias => 0);
    function bump(input_value : architecture_word_t)
      return architecture_word_t is
    begin
      return input_value + local_math.selected_value;
    end function bump;
    alias input_alias : architecture_word_t is value;
  begin
    return bump(input_alias);
  end function architecture_adjust;
begin
  lanes: for i in 2 to 2 generate
    constant local_value : natural := i + 4;
    package lane_math is new work.generated_math
      generic map (bias => i + 3);
    subtype lane_word_t is unsigned(i + 1 downto 0);
    signal generated_value : lane_word_t;
    alias generated_alias : lane_word_t is generated_value;
  begin
    generated_value <= lane_math.selected_value;
    architecture_alias <= generated_alias;
    observed <= architecture_adjust(architecture_alias);
  end generate lanes;
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
      constant base_value : natural := 8;
      type choice_bits_t is array (0 to base_value / 2 - 1) of bit;
      subtype selected_word_t is unsigned(3 downto 0);
      signal selected_value : selected_word_t;
    begin
      selected_value <= "1001";
      observed <= selected_value;
    empty_choice: when 4 to 3 =>
      observed <= 15;
    fallback: when others =>
      observed <= 3;
  end generate selection;
end architecture;
)";
}
generated_enum_behavior_vhdl_source =
    directory / "generated_enum_behavior.vhd";
{
  std::ofstream output(generated_enum_behavior_vhdl_source);
  output << R"(
package generated_choice_types is
  type state_t is (idle, ready, 'Z', done);
end package;
use work.generated_choice_types.all;
entity generated_enum_behavior_vhdl is
  generic (mode : state_t := 'Z');
  port (observed : out unsigned(3 downto 0));
end entity;
architecture rtl of generated_enum_behavior_vhdl is
begin
  selection: case mode generate
    idle_choice: when idle =>
      observed <= 1;
    selected: when ready | 'Z' to done =>
      signal selected_value : unsigned(3 downto 0);
    begin
      selected_value <= 9;
      observed <= selected_value;
    empty_range: when 'Z' downto done =>
      observed <= 15;
    fallback: when others =>
      observed <= 3;
  end generate selection;
end architecture;
)";
}
vhdl_statement_behavior_source =
    directory / "vhdl_statement_behavior.vhd";
{
  std::ofstream output(vhdl_statement_behavior_source);
  output << R"(
entity vhdl_statement_behavior is
  port (observed : out integer);
end entity;
architecture rtl of vhdl_statement_behavior is
begin
  worker: process
    procedure add_value(
      variable target : inout integer;
      amount : integer) is
    begin
      target := target + amount;
    end procedure add_value;
    variable local_value : integer := 1;
  begin
    choose_variable: with local_value select
      local_value := 2 when 1, 3 when others;
    initial_call: add_value(local_value, 1);
    iterations: for index in 0 to 2 loop
      branch: if index = 1 then
        branch_call: add_value(local_value, 4);
      else
        branch_null: null;
      end if branch;
      choice: case index is
        when 0 => first_call: add_value(local_value, 1);
        when 1 => middle_null: null;
        when others => final_call: add_value(local_value, 2);
      end case choice;
    end loop iterations;
    choose_signal: with local_value select
      observed <= local_value when 10, 0 when others;
    suspended: wait;
  end process worker;
end architecture;
)";
}
generated_static_behavior_sv_source =
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
generated_implicit_behavior_sv_source =
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
generated_block_behavior_vhdl_source =
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

entity generated_block_interface_vhdl is
  port (
    observed : out unsigned(3 downto 0);
    default_observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of generated_block_interface_vhdl is
  signal source_value : unsigned(3 downto 0);
begin
  source_value <= 5;
  interface_scope: block is
    generic (
      width : natural := 4;
      increment : natural := width - 2
    );
    generic map (open, increment => open);
    port (
      input_value : in unsigned(width - 1 downto 0);
      default_value : in unsigned(width - 1 downto 0) := "0011";
      output_value : out unsigned(width - 1 downto 0);
      unused_output : out unsigned(width - 1 downto 0)
    );
    port map (
      source_value,
      default_value => open,
      output_value => observed,
      unused_output => open
    );
  begin
    output_value <= input_value + increment;
    default_observed <= default_value;
  end block interface_scope;
end architecture;

package generated_block_math_vhdl is
  generic (bias : integer := 1);
  constant offset : integer := bias;
  function apply(value : integer) return integer;
end package;

package body generated_block_math_vhdl is
  function apply(value : integer) return integer is
  begin
    return value + offset;
  end function;
end package body;

entity generated_block_nonvalue_vhdl is
  port (observed : out integer);
end entity;

architecture rtl of generated_block_nonvalue_vhdl is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure publish(value : integer) is
  begin
    null;
  end procedure;
  package selected_math is new work.generated_block_math_vhdl
    generic map (bias => 3);
begin
  nonvalue_scope: block is
    generic (
      type item_t;
      function transform(value : item_t) return item_t;
      procedure observe(value : item_t) is publish;
      package api is new work.generated_block_math_vhdl
        generic map (<>));
    generic map (
      item_t => integer,
      transform => increment,
      observe => open,
      api => selected_math);
    port (output_value : out item_t);
    port map (output_value => observed);
  begin
    worker: process
      variable local_value : item_t;
    begin
      local_value := transform(api.apply(2));
      observe(local_value);
      output_value <= local_value;
      wait;
    end process;
  end block nonvalue_scope;
end architecture;

entity generated_guarded_behavior_vhdl is
  port (
    observed : out boolean
  );
end entity;

architecture rtl of generated_guarded_behavior_vhdl is
  signal enabled : boolean;
begin
  stimulus: process
  begin
    enabled <= false;
    wait for 1 ns;
    enabled <= true;
    wait;
  end process;
  guarded_scope: block (enabled) is
  begin
    observed <= guard;
  end block guarded_scope;
end architecture;
)";
}
}

}  // namespace fsim::test
