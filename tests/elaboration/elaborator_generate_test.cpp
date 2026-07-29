// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

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
      localparam logic [64:0] WIDE_VALUE = 0;
    end
  endgenerate
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto generated_vhdl = fsim::frontend::parse_text(
        "generated-mixed.vhd",
        R"(
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
      child: entity work.generated_case_foreign(rtl)
        generic map (
          value => 7
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
begin
  chosen: if enabled generate
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= 6;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  else generate
    observed <= 1;
  end generate chosen;
end architecture;

entity generated_vhdl_loop_behavior is
end entity;
architecture rtl of generated_vhdl_loop_behavior is
begin
  lanes: for i in 0 to 2 generate
    constant local_value : natural := i + 4;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= local_value;
  end generate lanes;
end architecture;

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

entity generated_vhdl_bad_constant is
end entity;
architecture rtl of generated_vhdl_bad_constant is
begin
  invalid_scope: block
    constant bad_value : positive := 0;
  begin
  end block invalid_scope;
end architecture;
)",
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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
    assert(generated_vhdl_case_q);
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

    const auto generated_vhdl_overlapping_ranges =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_overlapping_ranges(rtl)");
    assert(!generated_vhdl_overlapping_ranges.ok());
    assert(has_diagnostic(
        generated_vhdl_overlapping_ranges,
        "FSIM-ELAB-GEN-010"));

    const auto generated_vhdl_bad_range =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_bad_range(rtl)");
    assert(!generated_vhdl_bad_range.ok());
    assert(has_diagnostic(
        generated_vhdl_bad_range,
        "FSIM-ELAB-GEN-009"));

    const auto generated_sv_behavior =
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_behavior(rtl)");
    assert(generated_vhdl_behavior.ok());
    const auto generated_vhdl_observed =
        generated_vhdl_behavior.design->find_signal("observed");
    const auto generated_vhdl_local =
        generated_vhdl_behavior.design->find_signal(
            "chosen.generated_value");
    assert(generated_vhdl_observed && generated_vhdl_local);
    assert(generated_vhdl_behavior.design->processes().size() == 2);
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
        == "0111");

    const auto generated_sv_loop_behavior =
        fsim::elaboration::elaborate(
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

    const auto generated_vhdl_loop_behavior =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_loop_behavior(rtl)");
    assert(generated_vhdl_loop_behavior.ok());
    assert(
        generated_vhdl_loop_behavior.design->processes().size()
        == 3);
    auto generated_vhdl_loop_behavior_interpreter =
        generated_vhdl_loop_behavior.design->create_interpreter();
    assert(
        generated_vhdl_loop_behavior_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    const std::array<std::string_view, 3>
        generated_vhdl_loop_behavior_values{
            "0100", "0101", "0110"};
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
    }

    const auto generated_sv_implicit_behavior =
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
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

    const auto generated_vhdl_block_behavior =
        fsim::elaboration::elaborate(
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

    const auto generated_sv_bad_constant =
        fsim::elaboration::elaborate(
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
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_sv_wide_constant");
    assert(!generated_sv_wide_constant.ok());
    assert(std::any_of(
        generated_sv_wide_constant.diagnostics.begin(),
        generated_sv_wide_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-012";
        }));

    const auto generated_vhdl_bad_constant =
        fsim::elaboration::elaborate(
            generated_design,
            "vhdl:work.generated_vhdl_bad_constant(rtl)");
    assert(!generated_vhdl_bad_constant.ok());
    assert(std::any_of(
        generated_vhdl_bad_constant.diagnostics.begin(),
        generated_vhdl_bad_constant.diagnostics.end(),
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-GEN-012";
        }));

    auto unevaluable_generate_design = generated_design;
    const auto unevaluable_unit = std::find_if(
        unevaluable_generate_design.units.begin(),
        unevaluable_generate_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_true";
        });
    assert(unevaluable_unit != unevaluable_generate_design.units.end());
    assert(!unevaluable_unit->generate_regions.empty());
    unevaluable_unit->generate_regions.front().condition.text =
        "MISSING_GENERATE_CONSTANT";
    const auto unevaluable_generate =
        fsim::elaboration::elaborate(
            unevaluable_generate_design,
            "sv:work.generated_sv_true");
    assert(!unevaluable_generate.ok());
    assert(has_diagnostic(
        unevaluable_generate, "FSIM-ELAB-GEN-001"));

    const auto generated_loop_unit =
        [](fsim::frontend::ParsedDesign& design)
        -> fsim::frontend::DesignUnit& {
          const auto found = std::find_if(
              design.units.begin(),
              design.units.end(),
              [](const auto& unit) {
                return unit.name == "generated_sv_loop";
              });
          assert(found != design.units.end());
          assert(!found->generate_regions.empty());
          return *found;
        };
    auto invalid_loop_initial_design = generated_design;
    auto& invalid_loop_initial =
        generated_loop_unit(invalid_loop_initial_design)
            .generate_regions.front()
            .initial;
    invalid_loop_initial.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_initial.text = "MISSING_LOOP_INITIAL";
    invalid_loop_initial.operands.clear();
    const auto invalid_loop_initial_result =
        fsim::elaboration::elaborate(
            invalid_loop_initial_design,
            "sv:work.generated_sv_loop");
    assert(!invalid_loop_initial_result.ok());
    assert(has_diagnostic(
        invalid_loop_initial_result, "FSIM-ELAB-GEN-002"));

    auto invalid_loop_condition_design = generated_design;
    auto& invalid_loop_condition =
        generated_loop_unit(invalid_loop_condition_design)
            .generate_regions.front()
            .condition;
    invalid_loop_condition.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_condition.text = "MISSING_LOOP_CONDITION";
    invalid_loop_condition.operands.clear();
    const auto invalid_loop_condition_result =
        fsim::elaboration::elaborate(
            invalid_loop_condition_design,
            "sv:work.generated_sv_loop");
    assert(!invalid_loop_condition_result.ok());
    assert(has_diagnostic(
        invalid_loop_condition_result, "FSIM-ELAB-GEN-003"));

    const std::vector<fsim::elaboration::Binding>
        first_generated_loop_binding{
            {"generated_sv_loop.lanes[0].child",
             "vhdl:work.generated_vhdl_internal_leaf(rtl)",
             std::nullopt},
        };
    auto stalled_loop_design = generated_design;
    auto& stalled_iteration =
        generated_loop_unit(stalled_loop_design)
            .generate_regions.front()
            .iteration;
    stalled_iteration.kind =
        fsim::frontend::ExpressionKind::Identifier;
    stalled_iteration.text = "i";
    stalled_iteration.operands.clear();
    const auto stalled_loop =
        fsim::elaboration::elaborate(
            stalled_loop_design,
            "sv:work.generated_sv_loop",
            first_generated_loop_binding);
    assert(!stalled_loop.ok());
    assert(has_diagnostic(stalled_loop, "FSIM-ELAB-GEN-006"));

    auto invalid_loop_iteration_design = generated_design;
    auto& invalid_loop_iteration =
        generated_loop_unit(invalid_loop_iteration_design)
            .generate_regions.front()
            .iteration;
    invalid_loop_iteration.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_loop_iteration.text = "MISSING_LOOP_ITERATION";
    invalid_loop_iteration.operands.clear();
    const auto invalid_loop_iteration_result =
        fsim::elaboration::elaborate(
            invalid_loop_iteration_design,
            "sv:work.generated_sv_loop",
            first_generated_loop_binding);
    assert(!invalid_loop_iteration_result.ok());
    assert(has_diagnostic(
        invalid_loop_iteration_result, "FSIM-ELAB-GEN-005"));

    const auto shadowed_loop =
        fsim::elaboration::elaborate(
            generated_design,
            "sv:work.generated_shadow_loop");
    assert(!shadowed_loop.ok());
    assert(has_diagnostic(
        shadowed_loop, "FSIM-ELAB-GEN-007"));

    const auto generated_case_unit =
        [](fsim::frontend::ParsedDesign& design)
        -> fsim::frontend::DesignUnit& {
          const auto found = std::find_if(
              design.units.begin(),
              design.units.end(),
              [](const auto& unit) {
                return unit.name
                    == "generated_sv_case_selected";
              });
          assert(found != design.units.end());
          assert(!found->generate_regions.empty());
          return *found;
        };
    auto invalid_case_selector_design = generated_design;
    auto& invalid_case_selector =
        generated_case_unit(invalid_case_selector_design)
            .generate_regions.front()
            .condition;
    invalid_case_selector.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_case_selector.text = "MISSING_CASE_SELECTOR";
    invalid_case_selector.operands.clear();
    const auto invalid_case_selector_result =
        fsim::elaboration::elaborate(
            invalid_case_selector_design,
            "sv:work.generated_sv_case_selected");
    assert(!invalid_case_selector_result.ok());
    assert(has_diagnostic(
        invalid_case_selector_result, "FSIM-ELAB-GEN-008"));

    auto invalid_case_choice_design = generated_design;
    auto& invalid_case_choice =
        generated_case_unit(invalid_case_choice_design)
            .generate_regions.front()
            .alternatives.front()
            .choices.front();
    invalid_case_choice.left.kind =
        fsim::frontend::ExpressionKind::Identifier;
    invalid_case_choice.left.text = "MISSING_CASE_CHOICE";
    invalid_case_choice.left.operands.clear();
    const auto invalid_case_choice_result =
        fsim::elaboration::elaborate(
            invalid_case_choice_design,
            "sv:work.generated_sv_case_selected");
    assert(!invalid_case_choice_result.ok());
    assert(has_diagnostic(
        invalid_case_choice_result, "FSIM-ELAB-GEN-009"));

    auto overlapping_case_design = generated_design;
    auto& overlapping_choice =
        generated_case_unit(overlapping_case_design)
            .generate_regions.front()
            .alternatives.front()
            .choices.front();
    overlapping_choice.left.text = "2";
    const auto overlapping_case =
        fsim::elaboration::elaborate(
            overlapping_case_design,
            "sv:work.generated_sv_case_selected");
    assert(!overlapping_case.ok());
    assert(has_diagnostic(
        overlapping_case, "FSIM-ELAB-GEN-010"));

    auto unmatched_case_design = generated_design;
    const auto unmatched_case_unit = std::find_if(
        unmatched_case_design.units.begin(),
        unmatched_case_design.units.end(),
        [](const auto& unit) {
          return unit.name == "generated_sv_case_default";
        });
    assert(unmatched_case_unit != unmatched_case_design.units.end());
    auto& unmatched_alternatives =
        unmatched_case_unit->generate_regions.front().alternatives;
    unmatched_alternatives.erase(
        std::remove_if(
            unmatched_alternatives.begin(),
            unmatched_alternatives.end(),
            [](const auto& alternative) {
              return alternative.is_default;
            }),
        unmatched_alternatives.end());
    const auto unmatched_case =
        fsim::elaboration::elaborate(
            unmatched_case_design,
            "sv:work.generated_sv_case_default");
    assert(unmatched_case.ok());
    assert(unmatched_case.design->specializations().size() == 1);
}

} // namespace fsim::tests::elaboration

