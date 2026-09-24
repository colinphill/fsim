// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include <string>
#include <unordered_set>

namespace fsim::tests::elaboration {

void test_generate_slice_and_case_closure(
    const fsim::frontend::ParsedDesign& generated_design);

void test_generate_elaboration() {
auto generated_sv = fsim::frontend::parse_text(
        "generated-mixed.sv",
        R"(
module generated_sv_leaf #(
  parameter VALUE = 3
) (
  output logic [3:0] q
);
  initial q = VALUE;
endmodule

module generated_sv_internal_leaf #(
  parameter VALUE = 0
);
  logic [3:0] q;
  initial q = VALUE;
endmodule

module generated_sv_true #(
  parameter ENABLED = 1
) (
  output logic [3:0] q
);
  generate
    if (ENABLED) begin : foreign_branch
      for (genvar j = 0; j < 1; j = j + 1) begin : nested_lane
        generated_foreign #(.VALUE(j + 9)) child(.q(q));
      end
    end else begin : local_branch
      if (1) begin : nested_branch
        generated_sv_leaf #(.VALUE(3)) child(.q(q));
      end else begin : unused_nested_branch
        generated_sv_leaf #(.VALUE(4)) child(.q(q));
      end
    end
  endgenerate
endmodule

module generated_sv_false #(
  parameter ENABLED = 0
) (
  output logic [3:0] q
);
  generate
    if (ENABLED) begin : foreign_branch
      generated_foreign #(.VALUE(9)) child(.q(q));
    end else begin : local_branch
      if (1) begin : nested_branch
        generated_sv_leaf #(.VALUE(3)) child(.q(q));
      end else begin : unused_nested_branch
        generated_sv_leaf #(.VALUE(4)) child(.q(q));
      end
    end
  endgenerate
endmodule

module generated_sv_loop #(
  parameter COUNT = 3
);
  genvar i;
  generate
    for (i = 0; i < COUNT; i++) begin : lanes
      generated_vhdl_loop_bound #(.VALUE(i + 5)) child();
    end
  endgenerate
endmodule

module generated_shadow_loop;
  generate
    for (genvar i = 0; i < 1; i = i + 1) begin : outer
      for (genvar i = 0; i < 1; i = i + 1) begin : inner
        generated_sv_internal_leaf child();
      end
    end
  endgenerate
endmodule

module generated_sv_case_selected #(
  parameter MODE = 2
);
  generate
    case (MODE)
      0: begin : zero
        generated_sv_internal_leaf #(.VALUE(1)) child();
      end
      1, 2: begin : selected
        generated_case_foreign #(.VALUE(8)) child();
      end
      default: begin : fallback
        generated_sv_internal_leaf #(.VALUE(4)) child();
      end
    endcase
  endgenerate
endmodule

module generated_sv_case_default #(
  parameter MODE = 9
);
  generate
    case (MODE)
      0: begin : zero
        generated_sv_internal_leaf #(.VALUE(1)) child();
      end
      default: begin : fallback
        generated_sv_internal_leaf #(.VALUE(4)) child();
      end
    endcase
  endgenerate
endmodule

module generated_sv_behavior #(
  parameter ENABLED = 1
) (
  output logic [3:0] observed
);
  generate
    if (ENABLED) begin : selected
      logic [3:0] generated_value;
      assign generated_value = 4'd5;
      always_comb observed = generated_value + 1;
    end else begin : fallback
      assign observed = 4'd1;
    end
  endgenerate
endmodule

module generated_sv_loop_behavior #(parameter COUNT = 3);
  genvar i;
  generate
    for (i = COUNT - 1; i >= 0; i--) begin : lane
      localparam int LOCAL_VALUE = i + 1;
      logic [3:0] generated_value;
      initial generated_value = LOCAL_VALUE;
    end
  endgenerate
endmodule

module generated_sv_typed #(parameter COUNT = 2);
  generate
    for (genvar i = 0; i < COUNT; i++) begin
      localparam int WIDTH = i + 2;
      typedef logic [WIDTH-1:0] word_t;
      word_t value;
      initial value = WIDTH;
    end
  endgenerate
endmodule

module generated_sv_implicit_behavior #(
  parameter ENABLED = 1
) (
  output logic [3:0] observed
);
  if (ENABLED) begin : implicit_scope
    localparam int BASE_VALUE = 5;
    parameter int GENERATED_VALUE = BASE_VALUE + 1;
    logic [3:0] generated_value;
    assign generated_value = GENERATED_VALUE;
    always_comb observed = generated_value + 1;
  end
endmodule

module generated_sv_direct_behavior (
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

module generated_sv_scope_visibility (
  output logic [3:0] nested_parent_visible,
  output logic [3:0] shadowed_value,
  output logic [3:0] left_value,
  output logic [3:0] right_value
);
  localparam logic [3:0] scope_value = 4'd1;
  generate
    if (1) begin : outer_scope
      localparam logic [3:0] scope_value = 4'd2;
      logic [3:0] parent_value;
      assign parent_value = 4'd10;

      if (1) begin : nested_scope
        localparam logic [3:0] scope_value = 4'd3;
        logic [3:0] nested_value;
        assign nested_value = parent_value;
        assign nested_parent_visible = nested_value;
        assign shadowed_value = scope_value;
      end

      if (1) begin : left_scope
        localparam logic [3:0] sibling_value = 4'd4;
        logic [3:0] sibling_local;
        assign sibling_local = sibling_value;
        assign left_value = sibling_local;
      end

      if (1) begin : right_scope
        localparam logic [3:0] sibling_value = 4'd8;
        logic [3:0] sibling_local;
        assign sibling_local = sibling_value;
        assign right_value = sibling_local;
      end
    end
  endgenerate
endmodule

module generated_sv_bad_constant;
  generate
    if (1) begin : selected
      localparam int BAD_VALUE = 1 / 0;
    end
  endgenerate
endmodule

module generated_sv_wide_constant;
  generate
    if (1) begin : selected
      localparam logic [16777216:0] WIDE_VALUE = 0;
    end
  endgenerate
endmodule

module generated_sv_wide_function_constant;
  function automatic logic [95:0] build_table(input int index);
    build_table = 96'h123456789abcdef012345678 + index;
  endfunction
  generate
    for (genvar lane = 0; lane < 2; lane++) begin : each
      localparam logic [95:0] TABLE = build_table(lane);
      logic [7:0] observed;
      assign observed = TABLE[lane * 8 +: 8];
    end
  endgenerate
endmodule

module generated_sv_zero_replication;
  function automatic logic [1:0] alpha;
    alpha = {{0{1'b0}}, 2'b10};
  endfunction
  if (alpha() == 2'b10) begin : selected
    logic observed;
    assign observed = 1'b1;
  end
endmodule

module generated_sv_inactive_constant_function;
  function automatic logic [4095:0] build_table(input int seed);
    integer index;
    begin
      build_table = '0;
      for (index = 0; index < 512; index++)
        build_table[index * 8 +: 8] = seed + index;
    end
  endfunction
  generate
    if (0) begin : inactive
      generated_sv_internal_leaf #(.VALUE(build_table(1))) child();
    end else begin : selected
      generated_sv_internal_leaf #(.VALUE(7)) child();
    end
  endgenerate
endmodule

module generated_sv_local_function_after_genvar #(
  parameter int BIAS = 3
) (
  output logic [15:0] observed
);
  generate
    for (genvar lane = 0; lane < 2; lane++) begin : each
      function automatic logic [7:0] build_value(input int seed);
        build_value = BIAS + seed;
      endfunction
      assign observed[lane * 8 +: 8] = build_value(lane);
    end
  endgenerate
endmodule

module generated_sv_full_slice_bank(
  output logic [7:0] observed
);
  generate
    for (genvar lane = 0; lane < 8; lane++) begin : each
      assign observed[lane] = lane < 4;
    end
  endgenerate
endmodule

module generated_sv_partial_slice_bank(
  output logic [8:0] observed
);
  generate
    for (genvar lane = 0; lane < 8; lane++) begin : each
      assign observed[lane] = lane < 4;
    end
  endgenerate
endmodule

module generated_sv_wide_slice_bank(
  output logic [1023:0] observed
);
  generate
    for (genvar lane = 0; lane < 128; lane++) begin : each
      assign observed[lane * 8 +: 8] = lane;
    end
  endgenerate
endmodule

)",
        fsim::frontend::Language::SystemVerilog2017);
    std::string generated_vhdl_source = R"(
package generated_math is
  generic (bias : natural := 0);
  constant selected_value : natural := bias + 1;
end package;

entity generated_vhdl_leaf is
  generic (
    value : natural := 1
  );
  port (
    q : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_leaf is
begin
  q <= value;
end architecture;

entity generated_vhdl_internal_leaf is
  generic (
    value : natural := 0
  );
end entity;
architecture rtl of generated_vhdl_internal_leaf is
  signal q : unsigned(3 downto 0);
begin
  q <= value;
end architecture;

entity generated_vhdl_top is
  generic (
    enabled : boolean := true
  );
  port (
    q : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_top is
begin
  selection: if enabled generate
    nested: if enabled generate
      child: entity work.generated_foreign(rtl)
        generic map (
          value => 6
        )
        port map (
          q => q
        );
    else generate
      child: entity work.generated_vhdl_leaf(rtl)
        generic map (
          value => 7
        )
        port map (
          q => q
        );
    end generate nested;
  else generate
    child: entity work.generated_vhdl_leaf(rtl)
      generic map (
        value => 2
      )
      port map (
        q => q
      );
  end generate selection;
end architecture;

entity generated_vhdl_loop_top is
  generic (
    count : positive := 3
  );
end entity;
architecture rtl of generated_vhdl_loop_top is
begin
  lanes: for i in count - 1 downto 0 generate
    child: entity work.generated_sv_loop_bound(rtl)
      generic map (
        value => i + 4
      )
      port map ();
  end generate lanes;
end architecture;

entity generated_vhdl_case_top is
  generic (
    mode : integer := 6
  );
end entity;
architecture rtl of generated_vhdl_case_top is
begin
  selection: case mode generate
    zero: when 0 =>
      child: entity work.generated_vhdl_internal_leaf(rtl)
        generic map (
          value => 1
        )
        port map ();
    selected: when 1 to 2 | 7 downto 5 =>
      constant base_value : natural := 6;
      signal selected_value : unsigned(3 downto 0);
    begin
      selected_value <= "0111";
      child: entity work.generated_case_foreign(rtl)
        generic map (
          value => base_value + 1
        )
        port map ();
    empty_choice: when 3 to 1 =>
      child: entity work.generated_vhdl_internal_leaf(rtl)
        generic map (
          value => 15
        )
        port map ();
    fallback: when others =>
      child: entity work.generated_vhdl_internal_leaf(rtl)
        generic map (
          value => 3
        )
        port map ();
  end generate selection;
end architecture;
package generated_choice_types is
  type state_t is (idle, ready, 'Z', done);
  type foreign_state_t is (cold, hot);
  constant foreign_choice : foreign_state_t := hot;
end package;
use work.generated_choice_types.all;
entity generated_vhdl_enum_case is
  generic (mode : state_t := 'Z');
  port (observed : out unsigned(3 downto 0));
end entity;
architecture rtl of generated_vhdl_enum_case is
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

use work.generated_choice_types.all;
entity generated_vhdl_enum_overlap is generic (mode : state_t := 'Z'); end;
architecture rtl of generated_vhdl_enum_overlap is
begin
  selection: case mode generate
    first_choice: when ready to 'Z' =>
    second_choice: when 'Z' to done =>
  end generate selection;
end architecture;
use work.generated_choice_types.all;
entity generated_vhdl_enum_bad_choice is generic (mode : state_t := 'Z'); end;
architecture rtl of generated_vhdl_enum_bad_choice is
begin
  selection: case mode generate
    invalid_choice: when missing_state =>
  end generate selection;
end architecture;
use work.generated_choice_types.all;
entity generated_vhdl_enum_wrong_domain is generic (mode : state_t := 'Z'); end;
architecture rtl of generated_vhdl_enum_wrong_domain is
begin
  selection: case mode generate
    invalid_choice: when foreign_choice =>
  end generate selection;
end architecture;
entity generated_vhdl_overlapping_ranges is
end entity;
architecture rtl of generated_vhdl_overlapping_ranges is
begin
  selection: case 2 generate
    first_choice: when 0 to 2 =>
    second_choice: when 2 to 4 =>
  end generate selection;
end architecture;

entity generated_vhdl_bad_range is
end entity;
architecture rtl of generated_vhdl_bad_range is
begin
  selection: case 2 generate
    invalid_choice: when 0 to missing_bound =>
  end generate selection;
end architecture;

entity generated_vhdl_behavior is
  generic (
    enabled : boolean := true
  );
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_behavior is
  constant architecture_bias : natural := 5;
begin
  chosen: if enabled generate
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
      generic map (bias => architecture_bias);
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

entity generated_vhdl_loop_behavior is
end entity;
architecture rtl of generated_vhdl_loop_behavior is
  subtype architecture_word_t is unsigned(3 downto 0);
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
  lanes: for i in 0 to 2 generate
    type lane_bits_t is array (0 to i + 1) of bit;
    constant local_value : natural := i + 4;
    package lane_math is new work.generated_math
      generic map (bias => i + 3);
    signal raw_value : unsigned(3 downto 0);
    signal generated_value : unsigned(3 downto 0);
    alias generated_alias : unsigned(3 downto 0) is generated_value;
    signal typed_value : lane_bits_t;
  begin
    raw_value <= lane_math.selected_value;
    generated_alias <= architecture_adjust(raw_value);
end generate lanes;
end architecture;
)";
    generated_vhdl_source += R"(
entity generated_vhdl_scope_visibility is
  port (
    nested_parent_visible : out bit_vector(3 downto 0);
    shadowed_value : out bit_vector(3 downto 0);
    left_value : out bit_vector(3 downto 0);
    right_value : out bit_vector(3 downto 0));
end entity;
architecture rtl of generated_vhdl_scope_visibility is
  constant scope_value : bit_vector(3 downto 0) := "0001";
begin
  outer_scope : if true generate
    constant scope_value : bit_vector(3 downto 0) := "0010";
    signal parent_value : bit_vector(3 downto 0);
  begin
    parent_value <= "1010";

    nested_scope : if true generate
      constant scope_value : bit_vector(3 downto 0) := "0011";
      signal nested_value : bit_vector(3 downto 0);
    begin
      nested_value <= parent_value;
      nested_parent_visible <= nested_value;
      shadowed_value <= scope_value;
    end generate nested_scope;

    left_scope : if true generate
      constant sibling_value : bit_vector(3 downto 0) := "0100";
      signal sibling_local : bit_vector(3 downto 0);
    begin
      sibling_local <= sibling_value;
      left_value <= sibling_local;
    end generate left_scope;

    right_scope : if true generate
      constant sibling_value : bit_vector(3 downto 0) := "1000";
      signal sibling_local : bit_vector(3 downto 0);
    begin
      sibling_local <= sibling_value;
      right_value <= sibling_local;
    end generate right_scope;
  end generate outer_scope;
end architecture;
)";
    generated_vhdl_source += R"(
entity generated_vhdl_slice_port_leaf is
  port (
    slice_value : in bit_vector(1 downto 0);
    captured_value : out bit_vector(1 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_slice_port_leaf is
begin
  captured_value <= slice_value;
end architecture;

entity generated_vhdl_iterative_slice_actual is
end entity;
architecture rtl of generated_vhdl_iterative_slice_actual is
  constant M : natural := 2;
  constant GEN : bit_vector(7 downto 0) := "11001001";
begin
  glane: for gj in 0 to 3 generate
    signal captured_value : bit_vector(1 downto 0);
  begin
    child: entity work.generated_vhdl_slice_port_leaf(rtl)
      port map (
        slice_value => GEN((gj + 1) * M - 1 downto gj * M),
        captured_value => captured_value
      );
  end generate glane;
end architecture;
)";
    generated_vhdl_source += R"(
entity generated_vhdl_block_behavior is
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_block_behavior is
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

entity generated_vhdl_block_interface is
  port (
    source_value : in unsigned(3 downto 0);
    result_value : out unsigned(3 downto 0);
    default_result : out unsigned(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_block_interface is
begin
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
      output_value => result_value,
      unused_output => open
    );
  begin
    output_value <= input_value + increment;
    default_result <= default_value;
  end block interface_scope;
end architecture;

package generated_vhdl_block_math is
  generic (bias : integer := 1);
  constant offset : integer := bias;
  function apply(value : integer) return integer;
end package;
package body generated_vhdl_block_math is
  function apply(value : integer) return integer is
  begin
    return value + offset;
  end function;
end package body;

entity generated_vhdl_block_package_leaf is
  generic (
    package api is new work.generated_vhdl_block_math
      generic map (<>));
end entity;
architecture rtl of generated_vhdl_block_package_leaf is
begin
  worker: process
    variable local_value : integer;
  begin
    local_value := api.offset;
    wait;
  end process;
end architecture;

entity generated_vhdl_block_nonvalue is
  port (result_value : out integer);
end entity;
architecture rtl of generated_vhdl_block_nonvalue is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure observe(value : integer) is
  begin
    null;
  end procedure;
  package selected_math is new work.generated_vhdl_block_math
    generic map (bias => 3);
begin
  nonvalue_scope: block is
    generic (
      type item_t;
      function transform(value : item_t) return item_t;
      procedure publish(value : item_t) is observe;
      package api is new work.generated_vhdl_block_math
        generic map (<>));
    generic map (
      item_t => integer,
      transform => increment,
      publish => open,
      api => selected_math);
    port (output_value : out item_t);
    port map (output_value => result_value);
  begin
    worker: process
      variable local_value : item_t;
    begin
      local_value := transform(api.apply(2));
      publish(local_value);
      output_value <= local_value;
      wait;
    end process;
    child: entity work.generated_vhdl_block_package_leaf(rtl)
      generic map (api => api)
      port map ();
  end block nonvalue_scope;
end architecture;

package generated_vhdl_other_block_math is
  generic (bias : integer := 1);
  constant offset : integer := bias;
end package;

entity generated_vhdl_bad_block_nonvalue is
end entity;
architecture rtl of generated_vhdl_bad_block_nonvalue is
  function wrong_function(value : boolean) return boolean is
  begin
    return value;
  end function;
  procedure wrong_procedure(value : boolean) is
  begin
    null;
  end procedure;
  package other_math is new work.generated_vhdl_other_block_math
    generic map (bias => 3);
begin
  missing_type_scope: block is
    generic (type item_t);
  begin
  end block missing_type_scope;

  wrong_function_scope: block is
    generic (
      type item_t;
      function transform(value : item_t) return item_t);
    generic map (
      item_t => integer,
      transform => wrong_function);
  begin
  end block wrong_function_scope;

  wrong_procedure_scope: block is
    generic (
      type item_t;
      procedure publish(value : item_t));
    generic map (
      item_t => integer,
      publish => wrong_procedure);
  begin
  end block wrong_procedure_scope;

  wrong_package_scope: block is
    generic (
      package api is new work.generated_vhdl_block_math
        generic map (<>));
    generic map (api => other_math);
  begin
  end block wrong_package_scope;
end architecture;

entity generated_vhdl_bad_block_generic is
end entity;
architecture rtl of generated_vhdl_bad_block_generic is
begin
  invalid_scope: block is
    generic (required_value : natural);
    generic map (unknown_value => 1);
  begin
  end block invalid_scope;
end architecture;

entity generated_vhdl_bad_block_port is
  port (source_value : in unsigned(7 downto 0));
end entity;
architecture rtl of generated_vhdl_bad_block_port is
begin
  invalid_scope: block is
    port (
      required_input : in unsigned(3 downto 0);
      output_value : out unsigned(3 downto 0)
    );
    port map (required_input => open, output_value => source_value + 1);
  begin
    required_input <= 0;
  end block invalid_scope;
end architecture;

entity generated_vhdl_bad_block_profile is
  port (source_value : in unsigned(7 downto 0));
end entity;
architecture rtl of generated_vhdl_bad_block_profile is
begin
  invalid_scope: block is
    port (input_value : in unsigned(0 to 3));
    port map (input_value => source_value);
  begin
  end block invalid_scope;
end architecture;

entity generated_vhdl_guarded_behavior is
  port (
    enabled : in boolean;
    observed : out boolean
  );
end entity;
architecture rtl of generated_vhdl_guarded_behavior is
begin
  guarded_scope: block (enabled) is
  begin
    observed <= guard;
  end block guarded_scope;
end architecture;

entity generated_vhdl_bad_guard is
end entity;
architecture rtl of generated_vhdl_bad_guard is
begin
  guarded_scope: block (1) is
  begin
  end block guarded_scope;
end architecture;

entity generated_vhdl_bad_constant is
end entity;
architecture rtl of generated_vhdl_bad_constant is
begin
  invalid_scope: block
    constant bad_value : positive := 0;
  begin
  end block invalid_scope;
end architecture;

entity generated_vhdl_packed_array_constant is
  port (
    index_value : in integer range 3 to 4;
    selected_value : out bit_vector(3 downto 0)
  );
end entity;
architecture rtl of generated_vhdl_packed_array_constant is
  type rom_t is array (3 to 4) of bit_vector(3 downto 0);
  function build_rom return rom_t is
    variable rom_value : rom_t := (others => "0101");
  begin
    rom_value(3) := "1010";
    return rom_value;
  end function build_rom;
begin
  selected: if true generate
    constant rom : rom_t := build_rom;
  begin
    selected_value <= rom(index_value);
  end generate selected;
end architecture;

entity generated_vhdl_bad_packed_array_constant is
end entity;
architecture rtl of generated_vhdl_bad_packed_array_constant is
  type rom_t is array (0 to 1) of bit_vector(3 downto 0);
  function build_rom return rom_t is
    variable rom_value : rom_t;
  begin
    return rom_value;
  end function build_rom;
begin
  selected: if true generate
    constant rom : rom_t := build_rom;
  begin
  end generate selected;
end architecture;

entity generated_vhdl_forward_generated_type is
end entity;
architecture rtl of generated_vhdl_forward_generated_type is
begin
  selected: if true generate
    signal early_value : later_t;
    subtype later_t is bit;
  begin
  end generate selected;
end architecture;

entity generated_vhdl_duplicate_callable is
  port (observed : out integer);
end entity;
architecture rtl of generated_vhdl_duplicate_callable is
begin
  selected: if true generate
    function adjust(value : integer) return integer is
    begin
      return value + 1;
    end function adjust;
    function adjust(value : integer) return integer is
    begin
      return value + 2;
    end function adjust;
    procedure drive(variable value : out integer) is
    begin
      value := 1;
    end procedure drive;
    procedure drive(variable value : out integer) is
    begin
      value := 2;
    end procedure drive;
  begin
    worker: process
    begin
      observed <= adjust(0);
      drive(observed);
      wait;
    end process;
  end generate selected;
end architecture;

entity generated_vhdl_forward_callable is
  port (observed : out integer);
end entity;
architecture rtl of generated_vhdl_forward_callable is
begin
  selected: if true generate
    function early(value : integer) return integer is
    begin
      return later(value);
    end function early;
    function later(value : integer) return integer is
    begin
      return value + 1;
    end function later;
  begin
    observed <= early(1);
  end generate selected;
end architecture;

entity generated_vhdl_forward_generic_callable is
  port (observed : out integer);
end entity;
architecture rtl of generated_vhdl_forward_generic_callable is
begin
  selected: if true generate
    function mapped is new later
      generic map (amount => 1);
    generic (amount : natural := 0)
    function later(value : integer) return integer is
    begin
      return value + amount;
    end function later;
  begin
    observed <= mapped(1);
  end generate selected;
end architecture;
)";
    auto generated_vhdl = fsim::frontend::parse_text(
        "generated-mixed.vhd", generated_vhdl_source,
        fsim::frontend::Language::Vhdl2008);
    assert(generated_sv.ok());
    assert(generated_vhdl.ok());
    fsim::frontend::ParsedDesign generated_design =
        std::move(generated_sv.design);
    generated_design.units.insert(
        generated_design.units.end(),
        std::make_move_iterator(
            generated_vhdl.design.units.begin()),
        std::make_move_iterator(
            generated_vhdl.design.units.end()));

    const std::vector<fsim::elaboration::Binding>
        generated_sv_binding{
            {"generated_sv_true.foreign_branch.nested_lane[0].child",
             "vhdl:work.generated_vhdl_leaf(rtl)",
             std::nullopt},
        };
    const auto generated_sv_true =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_true",
            generated_sv_binding);
    assert(generated_sv_true.ok());
    assert(
        generated_sv_true.design->specializations().size() == 2);
    assert(
        generated_sv_true.design->specializations()[1].instance
        == "generated_sv_true.foreign_branch.nested_lane[0].child");
    const auto generated_sv_true_q =
        generated_sv_true.design->find_signal("q");
    assert(generated_sv_true_q);
    auto generated_sv_true_interpreter =
        generated_sv_true.design->create_interpreter();
    assert(
        generated_sv_true_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_true_interpreter
            ->signal_value(*generated_sv_true_q)
            .to_msb_string()
        == "1001");
    const auto generated_sv_false =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_false");
    assert(generated_sv_false.ok());
    assert(
        generated_sv_false.design->specializations()[1].instance
        == "generated_sv_false.local_branch.nested_branch.child");
    assert(
        !generated_sv_false.design->find_signal(
            "generated_sv_false.foreign_branch.child.q"));
    const auto generated_sv_false_q =
        generated_sv_false.design->find_signal("q");
    assert(generated_sv_false_q);
    auto generated_sv_false_interpreter =
        generated_sv_false.design->create_interpreter();
    assert(
        generated_sv_false_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_false_interpreter
            ->signal_value(*generated_sv_false_q)
            .to_msb_string()
        == "0011");
    const std::vector<fsim::elaboration::Binding>
        generated_vhdl_binding{
            {"generated_vhdl_top.selection.nested.child",
             "sv:work.generated_sv_leaf",
             std::nullopt},
        };
    const auto generated_vhdl_top =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_top(rtl)",
            generated_vhdl_binding);
    assert(generated_vhdl_top.ok());
    assert(
        generated_vhdl_top.design->specializations()[1].instance
        == "generated_vhdl_top.selection.nested.child");
    assert((
        generated_vhdl_top.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "6"}}));
    const auto generated_vhdl_q =
        generated_vhdl_top.design->find_signal("q");
    assert(generated_vhdl_q);
    auto generated_vhdl_interpreter =
        generated_vhdl_top.design->create_interpreter();
    assert(
        generated_vhdl_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_interpreter
            ->signal_value(*generated_vhdl_q)
            .to_msb_string()
        == "0110");

    const std::vector<fsim::elaboration::Binding>
        generated_sv_loop_bindings{
            {"generated_sv_loop.lanes[0].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
            {"generated_sv_loop.lanes[1].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
            {"generated_sv_loop.lanes[2].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    const auto generated_sv_loop =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_loop",
            generated_sv_loop_bindings);
    assert(generated_sv_loop.ok());
    assert(generated_sv_loop.design->specializations().size() == 4);
    for (std::size_t index = 0; index < 3; ++index) {
      const auto path =
          "generated_sv_loop.lanes[" + std::to_string(index)
          + "].child";
      const auto& specialization =
          generated_sv_loop.design->specializations()[index + 1];
      assert(specialization.instance == path);
      assert((
          specialization.parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"value", std::to_string(index + 5)}}));
    }
    auto generated_sv_loop_interpreter =
        generated_sv_loop.design->create_interpreter();
    assert(
        generated_sv_loop_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3> sv_loop_values{
        "0101", "0110", "0111"};
    for (std::size_t index = 0; index < 3; ++index) {
      const auto signal = generated_sv_loop.design->find_signal(
          "generated_sv_loop.lanes[" + std::to_string(index)
          + "].child.q");
      assert(signal);
      assert(
          generated_sv_loop_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == sv_loop_values[index]);
    }
    auto empty_generated_loop_design = generated_design;
    const auto empty_loop_unit = std::find_if(
        empty_generated_loop_design.units.begin(),
        empty_generated_loop_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_loop";
        });
    assert(empty_loop_unit != empty_generated_loop_design.units.end());
    assert(!empty_loop_unit->parameters.empty());
    empty_loop_unit->parameters.front().default_value.text = "0";
    const auto empty_generated_loop =
        compile_and_elaborate(
            empty_generated_loop_design,
            "sv:work.generated_sv_loop");
    assert(empty_generated_loop.ok());
    assert(empty_generated_loop.design->specializations().size() == 1);

    const std::vector<fsim::elaboration::Binding>
        generated_vhdl_loop_bindings{
            {"generated_vhdl_loop_top.lanes[2].child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
            {"generated_vhdl_loop_top.lanes[1].child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
            {"generated_vhdl_loop_top.lanes[0].child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
        };
    const auto generated_vhdl_loop =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_loop_top(rtl)",
            generated_vhdl_loop_bindings);
    assert(generated_vhdl_loop.ok());
    assert(generated_vhdl_loop.design->specializations().size() == 4);
    const std::array<std::size_t, 3> descending_indices{2, 1, 0};
    for (std::size_t ordinal = 0;
         ordinal < descending_indices.size();
         ++ordinal) {
      const auto index = descending_indices[ordinal];
      const auto path =
          "generated_vhdl_loop_top.lanes["
          + std::to_string(index) + "].child";
      const auto& specialization =
          generated_vhdl_loop.design->specializations()[ordinal + 1];
      assert(specialization.instance == path);
      assert((
          specialization.parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"VALUE", std::to_string(index + 4)}}));
    }
    auto generated_vhdl_loop_interpreter =
        generated_vhdl_loop.design->create_interpreter();
    assert(
        generated_vhdl_loop_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3> vhdl_loop_values{
        "0100", "0101", "0110"};
    for (const auto index : descending_indices) {
      const auto signal = generated_vhdl_loop.design->find_signal(
          "generated_vhdl_loop_top.lanes["
          + std::to_string(index) + "].child.q");
      assert(signal);
      assert(
          generated_vhdl_loop_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == vhdl_loop_values[index]);
    }

    const std::vector<fsim::elaboration::Binding>
        generated_sv_case_binding{
            {"generated_sv_case_selected.selected.child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    const auto generated_sv_case =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_case_selected",
            generated_sv_case_binding);
    assert(generated_sv_case.ok());
    assert(generated_sv_case.design->specializations().size() == 2);
    assert(
        generated_sv_case.design->specializations()[1].instance
        == "generated_sv_case_selected.selected.child");
    assert((
        generated_sv_case.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"value", "8"}}));
    const auto generated_sv_case_q =
        generated_sv_case.design->find_signal(
            "generated_sv_case_selected.selected.child.q");
    assert(generated_sv_case_q);
    auto generated_sv_case_interpreter =
        generated_sv_case.design->create_interpreter();
    assert(
        generated_sv_case_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_case_interpreter
            ->signal_value(*generated_sv_case_q)
            .to_msb_string()
        == "1000");

    const auto generated_sv_case_default =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_case_default");
    assert(generated_sv_case_default.ok());
    assert(
        generated_sv_case_default.design
            ->specializations()[1]
            .instance
        == "generated_sv_case_default.fallback.child");
    const auto generated_sv_case_default_q =
        generated_sv_case_default.design->find_signal(
            "generated_sv_case_default.fallback.child.q");
    assert(generated_sv_case_default_q);
    auto generated_sv_case_default_interpreter =
        generated_sv_case_default.design->create_interpreter();
    assert(
        generated_sv_case_default_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_case_default_interpreter
            ->signal_value(*generated_sv_case_default_q)
            .to_msb_string()
        == "0100");

    const std::vector<fsim::elaboration::Binding>
        generated_vhdl_case_binding{
            {"generated_vhdl_case_top.selected.child",
             "sv:work.generated_sv_internal_leaf",
             std::nullopt},
        };
    const auto generated_vhdl_case =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_case_top(rtl)",
            generated_vhdl_case_binding);
    assert(generated_vhdl_case.ok());
    assert(generated_vhdl_case.design->specializations().size() == 2);
    assert(
        generated_vhdl_case.design->specializations()[1].instance
        == "generated_vhdl_case_top.selected.child");
    assert((
        generated_vhdl_case.design->specializations()[1]
            .parameter_values
        == std::vector<std::pair<std::string, std::string>>{
            {"VALUE", "7"}}));
    const auto generated_vhdl_case_q =
        generated_vhdl_case.design->find_signal(
            "generated_vhdl_case_top.selected.child.q");
    const auto generated_vhdl_case_local =
        generated_vhdl_case.design->find_signal(
            "generated_vhdl_case_top.selected.selected_value");
    assert(generated_vhdl_case_q && generated_vhdl_case_local);
    auto generated_vhdl_case_interpreter =
        generated_vhdl_case.design->create_interpreter();
    assert(
        generated_vhdl_case_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_case_interpreter
            ->signal_value(*generated_vhdl_case_q)
            .to_msb_string()
        == "0111");
    assert(
        generated_vhdl_case_interpreter
            ->signal_value(*generated_vhdl_case_local)
            .to_msb_string()
        == "0111");

    const auto generated_vhdl_enum_case =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_enum_case(rtl)");
    assert(generated_vhdl_enum_case.ok());
    const auto generated_vhdl_enum_observed =
        generated_vhdl_enum_case.design->find_signal("observed");
    const auto generated_vhdl_enum_local =
        generated_vhdl_enum_case.design->find_signal(
            "selected.selected_value");
    assert(generated_vhdl_enum_observed && generated_vhdl_enum_local);
    auto generated_vhdl_enum_interpreter =
        generated_vhdl_enum_case.design->create_interpreter();
    assert(
        generated_vhdl_enum_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_enum_interpreter
            ->signal_value(*generated_vhdl_enum_observed)
            .to_msb_string()
        == "1001");
    assert(
        generated_vhdl_enum_interpreter
            ->signal_value(*generated_vhdl_enum_local)
            .to_msb_string()
        == "1001");
    assert(
        generated_vhdl_enum_case.design->processes().size() == 2);

    const auto generated_vhdl_enum_overlap =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_enum_overlap(rtl)");
    assert(!generated_vhdl_enum_overlap.ok());
    assert(has_diagnostic(
        generated_vhdl_enum_overlap,
        "FSIM-ELAB-GEN-010"));

    const auto generated_vhdl_enum_bad_choice =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_enum_bad_choice(rtl)");
    assert(!generated_vhdl_enum_bad_choice.ok());
    assert(has_diagnostic(
        generated_vhdl_enum_bad_choice,
        "FSIM-ELAB-GEN-009"));

    const auto generated_vhdl_enum_wrong_domain =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_enum_wrong_domain(rtl)");
    assert(!generated_vhdl_enum_wrong_domain.ok());
    assert(has_diagnostic(
        generated_vhdl_enum_wrong_domain,
        "FSIM-ELAB-GEN-009"));

    const auto generated_vhdl_overlapping_ranges =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_overlapping_ranges(rtl)");
    assert(!generated_vhdl_overlapping_ranges.ok());
    assert(has_diagnostic(
        generated_vhdl_overlapping_ranges,
        "FSIM-ELAB-GEN-010"));

    const auto generated_vhdl_bad_range =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_bad_range(rtl)");
    assert(!generated_vhdl_bad_range.ok());
    assert(has_diagnostic(
        generated_vhdl_bad_range,
        "FSIM-ELAB-GEN-009"));

    const auto generated_sv_behavior =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_behavior");
    assert(generated_sv_behavior.ok());
    const auto generated_sv_observed =
        generated_sv_behavior.design->find_signal("observed");
    const auto generated_sv_local =
        generated_sv_behavior.design->find_signal(
            "selected.generated_value");
    assert(generated_sv_observed && generated_sv_local);
    assert(generated_sv_behavior.design->processes().size() == 2);
    auto generated_sv_behavior_interpreter =
        generated_sv_behavior.design->create_interpreter();
    assert(
        generated_sv_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_behavior_interpreter
            ->signal_value(*generated_sv_local)
            .to_msb_string()
        == "0101");
    assert(
        generated_sv_behavior_interpreter
            ->signal_value(*generated_sv_observed)
            .to_msb_string()
        == "0110");

    const auto generated_vhdl_behavior =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_behavior(rtl)");
    assert(generated_vhdl_behavior.ok());
    const auto generated_vhdl_observed =
        generated_vhdl_behavior.design->find_signal("observed");
    const auto generated_vhdl_local =
        generated_vhdl_behavior.design->find_signal(
            "chosen.generated_value");
    const auto generated_vhdl_mapped =
        generated_vhdl_behavior.design->find_signal(
            "chosen.mapped_value");
    assert(
        generated_vhdl_observed && generated_vhdl_local
        && generated_vhdl_mapped);
    assert(generated_vhdl_behavior.design->processes().size() == 3);
    auto generated_vhdl_behavior_interpreter =
        generated_vhdl_behavior.design->create_interpreter();
    assert(
        generated_vhdl_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_behavior_interpreter
            ->signal_value(*generated_vhdl_local)
            .to_msb_string()
        == "0110");
    assert(
        generated_vhdl_behavior_interpreter
            ->signal_value(*generated_vhdl_observed)
            .to_msb_string()
        == "1000");
    assert(
        generated_vhdl_behavior_interpreter
            ->signal_value(*generated_vhdl_mapped)
            .to_msb_string()
        == "0111");

    const auto generated_vhdl_packed_array_constant
        = compile_and_elaborate(generated_design,
            "vhdl:work.generated_vhdl_packed_array_constant(rtl)");
    if (!generated_vhdl_packed_array_constant.ok()) {
      const auto& diagnostics
          = generated_vhdl_packed_array_constant.diagnostics;
      const auto count = diagnostics.size() < 8U
          ? diagnostics.size()
          : 8U;
      for (std::size_t index = 0U; index < count; ++index) {
        std::cerr << diagnostics[index].code << ": "
                  << diagnostics[index].message << '\n';
      }
      if (diagnostics.size() > count) {
        std::cerr << "additional generated packed-array diagnostics: "
                  << diagnostics.size() - count << '\n';
      }
    }
    assert(generated_vhdl_packed_array_constant.ok());
    const auto generated_vhdl_rom_index =
        generated_vhdl_packed_array_constant.design->find_signal(
            "index_value");
    const auto generated_vhdl_rom_selected =
        generated_vhdl_packed_array_constant.design->find_signal(
            "selected_value");
    assert(generated_vhdl_rom_index && generated_vhdl_rom_selected);
    auto generated_vhdl_rom_first_interpreter
        = generated_vhdl_packed_array_constant.design->create_interpreter();
    generated_vhdl_rom_first_interpreter->deposit_signal(
        *generated_vhdl_rom_index,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000000000000000000000000000011"));
    assert(
        generated_vhdl_rom_first_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_rom_first_interpreter
            ->signal_value(*generated_vhdl_rom_selected)
            .to_msb_string()
        == "1010");

    auto generated_vhdl_rom_second_interpreter
        = generated_vhdl_packed_array_constant.design->create_interpreter();
    generated_vhdl_rom_second_interpreter->deposit_signal(
        *generated_vhdl_rom_index,
        fsim::runtime::PackedLogic4::from_msb_string(
            "00000000000000000000000000000100"));
    assert(
        generated_vhdl_rom_second_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_rom_second_interpreter
            ->signal_value(*generated_vhdl_rom_selected)
            .to_msb_string()
        == "0101");

    const auto generated_vhdl_bad_packed_array_constant
        = compile_and_elaborate(generated_design,
            "vhdl:work.generated_vhdl_bad_packed_array_constant(rtl)");
    assert(!generated_vhdl_bad_packed_array_constant.ok());
    assert(has_diagnostic(
        generated_vhdl_bad_packed_array_constant,
        "FSIM-ELAB-GEN-011"));

    const auto generated_sv_loop_behavior =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_loop_behavior");
    assert(generated_sv_loop_behavior.ok());
    assert(
        generated_sv_loop_behavior.design->processes().size() == 3);
    auto generated_sv_loop_behavior_interpreter =
        generated_sv_loop_behavior.design->create_interpreter();
    assert(
        generated_sv_loop_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3>
        generated_sv_loop_behavior_values{
            "0001", "0010", "0011"};
    for (std::size_t index = 0; index < 3; ++index) {
      const auto signal =
          generated_sv_loop_behavior.design->find_signal(
              "lane[" + std::to_string(index)
              + "].generated_value");
      assert(signal);
      assert(
          generated_sv_loop_behavior_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == generated_sv_loop_behavior_values[index]);
    }

    const auto generated_sv_typed =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_typed");
    for (const auto& diagnostic : generated_sv_typed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
    assert(generated_sv_typed.ok());
    auto generated_sv_typed_interpreter =
        generated_sv_typed.design->create_interpreter();
    assert(
        generated_sv_typed_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto typed_zero =
        generated_sv_typed.design->find_signal(
            "genblk1[0].value");
    const auto typed_one =
        generated_sv_typed.design->find_signal(
            "genblk1[1].value");
    assert(typed_zero && typed_one);
    assert(
        generated_sv_typed_interpreter
            ->signal_value(*typed_zero).to_msb_string()
        == "10");
    assert(
        generated_sv_typed_interpreter
            ->signal_value(*typed_one).to_msb_string()
        == "011");

    const auto generated_vhdl_loop_behavior =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_loop_behavior(rtl)");
    for (const auto& diagnostic :
         generated_vhdl_loop_behavior.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
    assert(generated_vhdl_loop_behavior.ok());
    assert(
        generated_vhdl_loop_behavior.design->processes().size()
        == 6);
    auto generated_vhdl_loop_behavior_interpreter =
        generated_vhdl_loop_behavior.design->create_interpreter();
    assert(
        generated_vhdl_loop_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3>
        generated_vhdl_loop_behavior_values{
            "0101", "0110", "0111"};
    std::unordered_set<std::string> generated_vhdl_type_identities;
    for (std::size_t index = 0; index < 3; ++index) {
      const auto signal =
          generated_vhdl_loop_behavior.design->find_signal(
              "lanes[" + std::to_string(index)
              + "].generated_value");
      assert(signal);
      assert(
          generated_vhdl_loop_behavior_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == generated_vhdl_loop_behavior_values[index]);
      const auto typed_signal =
          generated_vhdl_loop_behavior.design->find_signal(
              "lanes[" + std::to_string(index)
              + "].typed_value");
      assert(typed_signal);
      const auto& typed_info =
          generated_vhdl_loop_behavior.design
              ->signals().at(*typed_signal);
      assert(typed_info.width == index + 2);
      assert(
          typed_info.nominal_type.find(
              "generated-mixed.vhd")
              != std::string::npos
          && typed_info.nominal_type.find("lane_bits_t")
              != std::string::npos
          && typed_info.nominal_type.find(
                 "lanes[" + std::to_string(index) + "]")
              != std::string::npos);
      assert(
          generated_vhdl_type_identities.insert(
              typed_info.nominal_type).second);
    }

    const auto generated_vhdl_iterative_slice_actual =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_iterative_slice_actual(rtl)");
    for (const auto& diagnostic :
         generated_vhdl_iterative_slice_actual.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
    assert(generated_vhdl_iterative_slice_actual.ok());
    auto generated_vhdl_iterative_slice_interpreter =
        generated_vhdl_iterative_slice_actual.design
            ->create_interpreter();
    assert(
        generated_vhdl_iterative_slice_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 4>
        generated_vhdl_iterative_slice_values{
            "01", "10", "00", "11"};
    for (std::size_t index = 0; index <
         generated_vhdl_iterative_slice_values.size(); ++index) {
      const auto signal =
          generated_vhdl_iterative_slice_actual.design->find_signal(
              "glane[" + std::to_string(index)
              + "].captured_value");
      assert(signal);
      assert(
          generated_vhdl_iterative_slice_interpreter
              ->signal_value(*signal)
              .to_msb_string()
          == generated_vhdl_iterative_slice_values[index]);
    }

    const auto generated_sv_implicit_behavior =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_implicit_behavior");
    assert(generated_sv_implicit_behavior.ok());
    const auto generated_sv_implicit_local =
        generated_sv_implicit_behavior.design->find_signal(
            "implicit_scope.generated_value");
    const auto generated_sv_implicit_observed =
        generated_sv_implicit_behavior.design->find_signal(
            "observed");
    assert(
        generated_sv_implicit_local
        && generated_sv_implicit_observed);
    assert(
        generated_sv_implicit_behavior.design->processes().size()
        == 2);
    auto generated_sv_implicit_interpreter =
        generated_sv_implicit_behavior.design->create_interpreter();
    assert(
        generated_sv_implicit_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_implicit_interpreter
            ->signal_value(*generated_sv_implicit_local)
            .to_msb_string()
        == "0110");
    assert(
        generated_sv_implicit_interpreter
            ->signal_value(*generated_sv_implicit_observed)
            .to_msb_string()
        == "0111");

    const auto generated_sv_direct_behavior =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_direct_behavior");
    assert(generated_sv_direct_behavior.ok());
    const auto generated_sv_direct =
        generated_sv_direct_behavior.design->find_signal(
            "direct_value");
    const auto generated_sv_nested =
        generated_sv_direct_behavior.design->find_signal(
            "named_scope.nested_value");
    const auto generated_sv_direct_observed =
        generated_sv_direct_behavior.design->find_signal(
            "observed");
    assert(
        generated_sv_direct && generated_sv_nested
        && generated_sv_direct_observed);
    assert(
        generated_sv_direct_behavior.design->processes().size()
        == 3);
    auto generated_sv_direct_interpreter =
        generated_sv_direct_behavior.design->create_interpreter();
    assert(
        generated_sv_direct_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_sv_direct_interpreter
            ->signal_value(*generated_sv_direct)
            .to_msb_string()
        == "0010");
    assert(
        generated_sv_direct_interpreter
            ->signal_value(*generated_sv_nested)
            .to_msb_string()
        == "0011");
    assert(
        generated_sv_direct_interpreter
            ->signal_value(*generated_sv_direct_observed)
            .to_msb_string()
        == "0100");

    const auto generated_sv_scope_visibility = compile_and_elaborate(
        generated_design,
        "sv:work.generated_sv_scope_visibility");
    assert(generated_sv_scope_visibility.ok());
    const auto generated_sv_nested_local =
        generated_sv_scope_visibility.design->find_signal(
            "outer_scope.nested_scope.nested_value");
    const auto generated_sv_left_local =
        generated_sv_scope_visibility.design->find_signal(
            "outer_scope.left_scope.sibling_local");
    const auto generated_sv_right_local =
        generated_sv_scope_visibility.design->find_signal(
            "outer_scope.right_scope.sibling_local");
    assert(
        generated_sv_nested_local && generated_sv_left_local
        && generated_sv_right_local);
    assert(*generated_sv_left_local != *generated_sv_right_local);
    auto generated_sv_scope_interpreter =
        generated_sv_scope_visibility.design->create_interpreter();
    assert(
        generated_sv_scope_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto generated_sv_value = [&](const std::string_view name) {
        const auto signal =
            generated_sv_scope_visibility.design->find_signal(name);
        assert(signal);
        return generated_sv_scope_interpreter
            ->signal_value(*signal).to_msb_string();
    };
    assert(generated_sv_value("outer_scope.nested_scope.nested_value")
        == "1010");
    assert(generated_sv_value("nested_parent_visible") == "1010");
    assert(generated_sv_value("shadowed_value") == "0011");
    assert(generated_sv_value("left_value") == "0100");
    assert(generated_sv_value("right_value") == "1000");

    const auto generated_vhdl_block_behavior =
        compile_and_elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_block_behavior(rtl)");
    assert(generated_vhdl_block_behavior.ok());
    const auto generated_vhdl_block_local =
        generated_vhdl_block_behavior.design->find_signal(
            "static_scope.generated_value");
    const auto generated_vhdl_block_observed =
        generated_vhdl_block_behavior.design->find_signal(
            "observed");
    assert(
        generated_vhdl_block_local
        && generated_vhdl_block_observed);
    assert(
        generated_vhdl_block_behavior.design->processes().size()
        == 2);
    auto generated_vhdl_block_interpreter =
        generated_vhdl_block_behavior.design->create_interpreter();
    assert(
        generated_vhdl_block_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_vhdl_block_interpreter
            ->signal_value(*generated_vhdl_block_local)
            .to_msb_string()
        == "0111");
    assert(
        generated_vhdl_block_interpreter
            ->signal_value(*generated_vhdl_block_observed)
            .to_msb_string()
        == "1000");

    const auto generated_vhdl_scope_visibility = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_scope_visibility(rtl)");
    assert(generated_vhdl_scope_visibility.ok());
    const auto generated_vhdl_nested_local =
        generated_vhdl_scope_visibility.design->find_signal(
            "outer_scope.nested_scope.nested_value");
    const auto generated_vhdl_left_local =
        generated_vhdl_scope_visibility.design->find_signal(
            "outer_scope.left_scope.sibling_local");
    const auto generated_vhdl_right_local =
        generated_vhdl_scope_visibility.design->find_signal(
            "outer_scope.right_scope.sibling_local");
    assert(
        generated_vhdl_nested_local && generated_vhdl_left_local
        && generated_vhdl_right_local);
    assert(*generated_vhdl_left_local != *generated_vhdl_right_local);
    auto generated_vhdl_scope_interpreter =
        generated_vhdl_scope_visibility.design->create_interpreter();
    assert(
        generated_vhdl_scope_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto generated_vhdl_value = [&](const std::string_view name) {
        const auto signal =
            generated_vhdl_scope_visibility.design->find_signal(name);
        assert(signal);
        return generated_vhdl_scope_interpreter
            ->signal_value(*signal).to_msb_string();
    };
    assert(generated_vhdl_value(
        "outer_scope.nested_scope.nested_value") == "1010");
    assert(generated_vhdl_value("nested_parent_visible") == "1010");
    assert(generated_vhdl_value("shadowed_value") == "0011");
    assert(generated_vhdl_value("left_value") == "0100");
    assert(generated_vhdl_value("right_value") == "1000");

    constexpr auto nested_scope_count = 16U;
    constexpr auto sibling_scope_count = 12U;
    std::string scope_overlay_source = R"(
module generated_sv_scope_overlay_stress (
  output logic [7:0] deepest_shadow,
  output logic [7:0] middle_shadow,
  output logic [7:0] root_visible,
  output logic [11:0] sibling_bits
);
  localparam logic [7:0] shadow = 8'd1;
  localparam logic [7:0] root_visible_value = 8'd165;
  generate
)";
    for (auto depth = 0U; depth < nested_scope_count; ++depth) {
        scope_overlay_source += "if (1) begin : depth_"
            + std::to_string(depth) + "\n";
        scope_overlay_source += "  localparam logic [7:0] shadow = 8'd"
            + std::to_string(depth + 2U) + ";\n";
        if (depth + 1U == nested_scope_count / 2U) {
            scope_overlay_source += "  assign middle_shadow = shadow;\n";
        }
    }
    scope_overlay_source += R"(
  assign deepest_shadow = shadow;
  assign root_visible = root_visible_value;
)";
    for (auto depth = nested_scope_count; depth > 0U; --depth) {
        scope_overlay_source += "end\n";
    }
    for (auto sibling = 0U; sibling < sibling_scope_count; ++sibling) {
        scope_overlay_source += "if (1) begin : sibling_"
            + std::to_string(sibling) + "\n";
        scope_overlay_source += "  localparam logic sibling_value = 1'b";
        scope_overlay_source += (sibling % 2U == 0U) ? '1' : '0';
        scope_overlay_source += ";\n";
        scope_overlay_source += R"(
  logic sibling_local;
  assign sibling_local = sibling_value;
)";
        scope_overlay_source += "  assign sibling_bits["
            + std::to_string(sibling) + "] = sibling_local;\nend\n";
    }
    scope_overlay_source += R"(
  endgenerate
endmodule
)";
    const auto scope_overlay_parsed = fsim::frontend::parse_text(
        "generated-sv-scope-overlay-stress.sv",
        scope_overlay_source,
        fsim::frontend::Language::SystemVerilog2017);
    assert(scope_overlay_parsed.ok());
    const auto scope_overlay = compile_and_elaborate(
        scope_overlay_parsed.design,
        "sv:work.generated_sv_scope_overlay_stress");
    assert(scope_overlay.ok());
    auto scope_overlay_interpreter
        = scope_overlay.design->create_interpreter();
    assert(scope_overlay_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto scope_overlay_value = [&](const std::string_view name) {
        const auto signal = scope_overlay.design->find_signal(
            "generated_sv_scope_overlay_stress." + std::string { name });
        assert(signal);
        return scope_overlay_interpreter
            ->signal_value(*signal)
            .to_msb_string();
    };
    std::string expected_sibling_bits;
    for (auto sibling = sibling_scope_count; sibling > 0U; --sibling) {
        expected_sibling_bits += (sibling - 1U) % 2U == 0U ? '1' : '0';
    }
    assert(scope_overlay_value("deepest_shadow") == "00010001");
    assert(scope_overlay_value("middle_shadow") == "00001001");
    assert(scope_overlay_value("root_visible") == "10100101");
    assert(scope_overlay_value("sibling_bits") == expected_sibling_bits);

    const auto block_interface = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_block_interface(rtl)");
    for (const auto& diagnostic : block_interface.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
    assert(block_interface.ok());
    const auto source_value =
        block_interface.design->find_signal("source_value");
    const auto input_alias = block_interface.design->find_signal(
        "interface_scope.input_value");
    const auto result_value =
        block_interface.design->find_signal("result_value");
    const auto output_alias = block_interface.design->find_signal(
        "interface_scope.output_value");
    const auto default_value = block_interface.design->find_signal(
        "interface_scope.default_value");
    const auto unused_output = block_interface.design->find_signal(
        "interface_scope.unused_output");
    const auto default_result =
        block_interface.design->find_signal("default_result");
    assert(source_value);
    assert(input_alias && *source_value == *input_alias);
    assert(result_value);
    assert(output_alias && *result_value == *output_alias);
    assert(default_value);
    assert(unused_output);
    assert(default_result);
    auto block_interpreter = block_interface.design->create_interpreter();
    block_interpreter->deposit_signal(
        *source_value,
        fsim::runtime::PackedLogic4::from_msb_string("0101"));
    assert(
        block_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        block_interpreter->signal_value(*result_value).to_msb_string()
        == "0111");
    assert(
        block_interpreter->signal_value(*default_result).to_msb_string()
        == "0011");

    const auto block_nonvalue = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_block_nonvalue(rtl)");
    for (const auto& diagnostic : block_nonvalue.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
    assert(block_nonvalue.ok());
    const auto block_nonvalue_result =
        block_nonvalue.design->find_signal("result_value");
    assert(block_nonvalue_result);
    auto block_nonvalue_interpreter =
        block_nonvalue.design->create_interpreter();
    assert(
        block_nonvalue_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const auto block_nonvalue_word =
        block_nonvalue_interpreter
            ->signal_value(*block_nonvalue_result)
            .low_word();
    assert(
        block_nonvalue_word.aval == 6
        && block_nonvalue_word.bval == 0);
    const auto& block_nonvalue_specialization =
        block_nonvalue.design->specializations().front();
    for (const auto generic : {
             "item_t", "transform", "publish", "api"}) {
      assert(std::ranges::any_of(
          block_nonvalue_specialization.parameter_identity_values,
          [&](const auto& identity) {
            return identity.first
                == "__block:nonvalue_scope:"
                    + std::string{generic};
      }));
    }
    const auto block_package_child = std::ranges::find_if(
        block_nonvalue.design->specializations(),
        [](const auto& specialization) {
          return specialization.instance
              == "generated_vhdl_block_nonvalue.nonvalue_scope.child";
        });
    assert(
        block_package_child
        != block_nonvalue.design->specializations().end());
    assert(std::ranges::any_of(
        block_package_child->parameter_identity_values,
        [](const auto& identity) {
          return identity.first == "api"
              && identity.second.find("bias=3")
                  != std::string::npos;
        }));

    const auto bad_block_nonvalue = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_bad_block_nonvalue(rtl)");
    assert(!bad_block_nonvalue.ok());
    for (const auto code : {
             "FSIM-ELAB-GENTYPE-001",
             "FSIM-ELAB-VHFUNC-005",
             "FSIM-ELAB-VHPROC-005",
             "FSIM-ELAB-VHPKG-007"}) {
      assert(has_diagnostic(bad_block_nonvalue, code));
    }

    const auto bad_block_generic = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_bad_block_generic(rtl)");
    assert(!bad_block_generic.ok());
    assert(has_diagnostic(
        bad_block_generic, "FSIM-ELAB-VHBLOCK-001"));
    const auto bad_block_port = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_bad_block_port(rtl)");
    assert(!bad_block_port.ok());
    assert(has_diagnostic(
        bad_block_port, "FSIM-ELAB-VHBLOCK-002"));
    const auto bad_block_profile = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_bad_block_profile(rtl)");
    assert(!bad_block_profile.ok());
    assert(has_diagnostic(
        bad_block_profile, "FSIM-ELAB-VHBLOCK-003"));

    const auto guarded = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_guarded_behavior(rtl)");
    assert(guarded.ok());
    const auto guarded_enabled =
        guarded.design->find_signal("enabled");
    const auto guarded_implicit =
        guarded.design->find_signal("guarded_scope.guard");
    const auto guarded_observed =
        guarded.design->find_signal("observed");
    assert(guarded_enabled && guarded_implicit && guarded_observed);
    assert(guarded.design->processes().size() == 2);
    auto guarded_interpreter = guarded.design->create_interpreter();
    for (const auto& [value, expected] :
         std::array{
             std::pair{"0", "0"},
             std::pair{"1", "1"}}) {
        guarded_interpreter->deposit_signal(
            *guarded_enabled,
            fsim::runtime::PackedLogic4::from_msb_string(value));
        assert(
            guarded_interpreter->run().status
            == fsim::runtime::RunStatus::completed);
        assert(
            guarded_interpreter->signal_value(*guarded_implicit)
                .to_msb_string()
            == expected);
        assert(
            guarded_interpreter->signal_value(*guarded_observed)
                .to_msb_string()
            == expected);
    }

    const auto bad_guard = compile_and_elaborate(
        generated_design,
        "vhdl:work.generated_vhdl_bad_guard(rtl)");
    assert(!bad_guard.ok());
    assert(has_diagnostic(bad_guard, "FSIM-ELAB-GEN-013"));

    const auto generated_sv_bad_constant =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_bad_constant");
    assert(!generated_sv_bad_constant.ok());
    assert(std::any_of(
        generated_sv_bad_constant.diagnostics.begin(),
        generated_sv_bad_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-011";
        }));

    const auto generated_sv_wide_constant =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_wide_constant");
    assert(!generated_sv_wide_constant.ok());
    assert(std::any_of(
        generated_sv_wide_constant.diagnostics.begin(),
        generated_sv_wide_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-012";
        }));

    const auto generated_sv_wide_function_constant =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_wide_function_constant");
    assert(generated_sv_wide_function_constant.ok());

    const auto generated_sv_zero_replication =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_zero_replication");
    assert(generated_sv_zero_replication.ok());

    const auto generated_sv_inactive_constant_function =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_inactive_constant_function");
    assert(generated_sv_inactive_constant_function.ok());
    assert(
        generated_sv_inactive_constant_function.design
            ->specializations()[1]
            .instance
        == "generated_sv_inactive_constant_function.selected.child");

    const auto generated_sv_local_function_after_genvar =
        compile_and_elaborate(
            generated_design,
            "sv:work.generated_sv_local_function_after_genvar");
    assert(generated_sv_local_function_after_genvar.ok());
    assert(std::ranges::none_of(
        generated_sv_local_function_after_genvar.design->processes(),
        [](const auto& process) {
          return std::ranges::any_of(
              process.operations,
              [](const auto& operation) {
                return fsim::runtime::simir::operation_holds<
                    fsim::runtime::simir::Call>(operation);
              });
        }));
    const auto generated_function_observed =
        generated_sv_local_function_after_genvar.design->find_signal(
            "observed");
    assert(generated_function_observed);
    auto generated_function_interpreter =
        generated_sv_local_function_after_genvar.design
            ->create_interpreter();
    assert(
        generated_function_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        generated_function_interpreter
            ->signal_value(*generated_function_observed)
            .to_msb_string()
        == "0000010000000011");

    test_generate_slice_and_case_closure(generated_design);
}

} // namespace fsim::tests::elaboration
