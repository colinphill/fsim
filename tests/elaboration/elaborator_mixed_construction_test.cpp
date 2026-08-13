// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {
namespace {

void append_units(
    fsim::frontend::ParsedDesign& destination,
    const fsim::frontend::ParsedDesign& source) {
  destination.units.insert(
      destination.units.end(), source.units.begin(), source.units.end());
}

const fsim::elaboration::SpecializationInfo& require_specialization(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view instance) {
  const auto found = std::ranges::find_if(
      design.specializations(),
      [&](const auto& candidate) {
        return candidate.instance == instance;
      });
  assert(found != design.specializations().end());
  return *found;
}

}  // namespace

void test_mixed_language_construction() {
  const auto sv_parent = fsim::frontend::parse_text(
      "sv_to_vhdl_construction.sv",
      R"(
module sv_to_vhdl_construction;
  logic [3:0] named_q;
  logic named_enabled;
  logic signed [31:0] named_offset;
  logic [3:0] positional_q;
  logic positional_enabled;
  logic signed [31:0] positional_offset;
  logic [3:0] default_q;
  logic default_enabled;
  logic signed [31:0] default_offset;

  vhdl_parameter_bound #(
    .WIDTH(4),
    .ENABLED(1'b1),
    .OFFSET(-3),
    .PATTERN(4'b1010)
  ) named_child(
    .q(named_q),
    .enabled_result(named_enabled),
    .offset_result(named_offset)
  );
  vhdl_parameter_bound #(
    4, 1'b0, 2, 4'b0101
  ) positional_child(
    .q(positional_q),
    .enabled_result(positional_enabled),
    .offset_result(positional_offset)
  );
  vhdl_parameter_bound #(
    .WIDTH(4)
  ) default_child(
    .q(default_q),
    .enabled_result(default_enabled),
    .offset_result(default_offset)
  );
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto vhdl_child = fsim::frontend::parse_text(
      "vhdl_parameter_bound.vhd",
      R"(
entity Vhdl_Parameter_Bound is
  generic (
    Width : positive := 2;
    Enabled : boolean := false;
    Offset : integer range -8 to 8 := 1;
    Pattern : bit_vector(3 downto 0) := "0011";
    Last : integer := Width - 1
  );
  port (
    Q : out bit_vector(Last downto 0);
    Enabled_Result : out bit;
    Offset_Result : out integer
  );
end entity;

architecture rtl of Vhdl_Parameter_Bound is
begin
  Q <= Pattern;
  Enabled_Result <= '1' when Enabled else '0';
  Offset_Result <= Offset;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!sv_parent.ok()) {
    for (const auto& diagnostic : sv_parent.diagnostics) {
      std::cerr << diagnostic.message << '\n';
    }
  }
  if (!vhdl_child.ok()) {
    for (const auto& diagnostic : vhdl_child.diagnostics) {
      std::cerr << diagnostic.message << '\n';
    }
  }
  assert(sv_parent.ok() && vhdl_child.ok());
  auto design = sv_parent.design;
  append_units(design, vhdl_child.design);
  const std::vector<fsim::elaboration::Binding> bindings{
      {"sv_to_vhdl_construction.named_child",
       "vhdl:work.vhdl_parameter_bound(rtl)", std::nullopt},
      {"sv_to_vhdl_construction.positional_child",
       "vhdl:work.vhdl_parameter_bound(rtl)", std::nullopt},
      {"sv_to_vhdl_construction.default_child",
       "vhdl:work.vhdl_parameter_bound(rtl)", std::nullopt},
  };
  const auto elaborated = fsim::elaboration::elaborate(
      design, "sv:work.sv_to_vhdl_construction", bindings);
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());

  const auto& named = require_specialization(
      *elaborated.design, "sv_to_vhdl_construction.named_child");
  const auto& positional = require_specialization(
      *elaborated.design, "sv_to_vhdl_construction.positional_child");
  const auto& defaults = require_specialization(
      *elaborated.design, "sv_to_vhdl_construction.default_child");
  assert((named.parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"width", "4"}, {"enabled", "true"}, {"offset", "-3"},
          {"pattern", "1010"}, {"last", "3"}}));
  assert((positional.parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"width", "4"}, {"enabled", "false"}, {"offset", "2"},
          {"pattern", "0101"}, {"last", "3"}}));
  assert((defaults.parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"width", "4"}, {"enabled", "false"}, {"offset", "1"},
          {"pattern", "0011"}, {"last", "3"}}));
  for (const auto* specialization : {&named, &positional, &defaults}) {
    assert(std::ranges::all_of(
        specialization->parameter_identity_values,
        [](const auto& value) {
          return value.second.starts_with("vhdlconst-v1;")
              || value.second.starts_with("vhdlcomposite-v1;");
        }));
  }

  auto interpreter = elaborated.design->create_interpreter();
  assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
  const auto require_value = [&](const std::string_view name) {
    const auto signal = elaborated.design->find_signal(name);
    assert(signal);
    return interpreter->signal_value(*signal).to_msb_string();
  };
  assert(require_value("named_q") == "1010");
  assert(require_value("named_enabled") == "1");
  assert(require_value("named_offset")
      == "11111111111111111111111111111101");
  assert(require_value("positional_q") == "0101");
  assert(require_value("positional_enabled") == "0");
  assert(require_value("positional_offset")
      == "00000000000000000000000000000010");
  assert(require_value("default_q") == "0011");
  assert(require_value("default_enabled") == "0");
  assert(require_value("default_offset")
      == "00000000000000000000000000000001");

  auto invalid_parent = sv_parent.design;
  auto& invalid_instances = invalid_parent.units.front().instances;
  invalid_instances.front().parameter_overrides[1].value.text = "2";
  append_units(invalid_parent, vhdl_child.design);
  const auto rejected_boolean = fsim::elaboration::elaborate(
      invalid_parent, "sv:work.sv_to_vhdl_construction", bindings);
  assert(!rejected_boolean.ok());
  assert(has_diagnostic(rejected_boolean, "FSIM-ELAB-GENERIC-008"));

  const auto vhdl_parent = fsim::frontend::parse_text(
      "vhdl_to_sv_construction.vhd",
      R"(
entity Vhdl_To_Sv_Construction is
end entity;
architecture rtl of Vhdl_To_Sv_Construction is
  signal Named_Q : bit_vector(3 downto 0);
  signal Named_Pattern : bit_vector(3 downto 0);
  signal Named_Signed : bit_vector(7 downto 0);
  signal Named_Enabled : bit;
  signal Named_Generated : bit;
  signal Positional_Q : bit_vector(3 downto 0);
  signal Positional_Pattern : bit_vector(3 downto 0);
  signal Positional_Signed : bit_vector(7 downto 0);
  signal Positional_Enabled : bit;
  signal Positional_Generated : bit;
  signal Default_Q : bit_vector(3 downto 0);
  signal Default_Pattern : bit_vector(3 downto 0);
  signal Default_Signed : bit_vector(7 downto 0);
  signal Default_Enabled : bit;
  signal Default_Generated : bit;
begin
  Named_Child : entity work.sv_typed_bound(rtl)
    generic map (
      width => 4,
      pattern => 18,
      signed_value => -3,
      enabled => 1,
      label => "go")
    port map (
      q => Named_Q, pattern_result => Named_Pattern,
      signed_result => Named_Signed, enabled_result => Named_Enabled,
      generated_result => Named_Generated);
  Positional_Child : entity work.sv_typed_bound(rtl)
    generic map (4, 5, 2, 0, "hi")
    port map (
      q => Positional_Q, pattern_result => Positional_Pattern,
      signed_result => Positional_Signed,
      enabled_result => Positional_Enabled,
      generated_result => Positional_Generated);
  Default_Child : entity work.sv_typed_bound(rtl)
    generic map (width => 4)
    port map (
      q => Default_Q, pattern_result => Default_Pattern,
      signed_result => Default_Signed, enabled_result => Default_Enabled,
      generated_result => Default_Generated);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto sv_child = fsim::frontend::parse_text(
      "sv_typed_bound.sv",
      R"(
module sv_typed_bound #(
  parameter int WIDTH = 2,
  parameter bit [3:0] PATTERN = 4'b0011,
  parameter bit signed [7:0] SIGNED_VALUE = -1,
  parameter bit ENABLED = 0,
  parameter string LABEL = "base",
  localparam int LAST = WIDTH - 1
) (
  output bit [LAST:0] q,
  output bit [3:0] pattern_result,
  output bit signed [7:0] signed_result,
  output bit enabled_result,
  output bit generated_result
);
  initial begin
    q = PATTERN;
    pattern_result = PATTERN;
    signed_result = SIGNED_VALUE;
    enabled_result = ENABLED;
  end
  generate
    if (ENABLED) begin : selected
      initial generated_result = 1'b1;
    end else begin : fallback
      initial generated_result = 1'b0;
    end
  endgenerate
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(vhdl_parent.ok() && sv_child.ok());
  auto reverse_design = vhdl_parent.design;
  append_units(reverse_design, sv_child.design);
  const std::vector<fsim::elaboration::Binding> reverse_bindings{
      {"vhdl_to_sv_construction.named_child", "sv:work.sv_typed_bound",
       std::nullopt},
      {"vhdl_to_sv_construction.positional_child", "sv:work.sv_typed_bound",
       std::nullopt},
      {"vhdl_to_sv_construction.default_child", "sv:work.sv_typed_bound",
       std::nullopt},
  };
  const auto reverse = fsim::elaboration::elaborate(
      reverse_design, "vhdl:work.vhdl_to_sv_construction(rtl)",
      reverse_bindings);
  if (!reverse.ok()) {
    for (const auto& diagnostic : reverse.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(reverse.ok());
  const auto& reverse_named = require_specialization(
      *reverse.design, "vhdl_to_sv_construction.named_child");
  const auto& reverse_positional = require_specialization(
      *reverse.design, "vhdl_to_sv_construction.positional_child");
  const auto& reverse_defaults = require_specialization(
      *reverse.design, "vhdl_to_sv_construction.default_child");
  assert((reverse_named.parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"WIDTH", "4"}, {"PATTERN", "2"}, {"SIGNED_VALUE", "-3"},
          {"ENABLED", "1"}, {"LABEL", "\"go\""}, {"LAST", "3"}}));
  assert((reverse_positional.parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"WIDTH", "4"}, {"PATTERN", "5"}, {"SIGNED_VALUE", "2"},
          {"ENABLED", "0"}, {"LABEL", "\"hi\""}, {"LAST", "3"}}));
  assert((reverse_defaults.parameter_values
      == std::vector<std::pair<std::string, std::string>>{
          {"WIDTH", "4"}, {"PATTERN", "3"}, {"SIGNED_VALUE", "-1"},
          {"ENABLED", "0"}, {"LABEL", "\"base\""}, {"LAST", "3"}}));
  for (const auto* specialization : {
           &reverse_named, &reverse_positional, &reverse_defaults}) {
    assert(std::ranges::all_of(
        specialization->parameter_identity_values,
        [](const auto& value) {
          return value.second.starts_with("svconst-v3:b=0:")
              || value.second.starts_with("svstring-v1;");
        }));
  }
  auto reverse_interpreter = reverse.design->create_interpreter();
  assert(
      reverse_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto require_reverse_value = [&](const std::string_view name) {
    const auto signal = reverse.design->find_signal(name);
    assert(signal);
    return reverse_interpreter->signal_value(*signal).to_msb_string();
  };
  assert(require_reverse_value("named_q") == "0010");
  assert(require_reverse_value("named_pattern") == "0010");
  assert(require_reverse_value("named_signed") == "11111101");
  assert(require_reverse_value("named_enabled") == "1");
  assert(require_reverse_value("named_generated") == "1");
  assert(require_reverse_value("positional_q") == "0101");
  assert(require_reverse_value("positional_generated") == "0");
  assert(require_reverse_value("default_q") == "0011");
  assert(require_reverse_value("default_signed") == "11111111");
  assert(require_reverse_value("default_generated") == "0");

  const auto ambiguous_sv_child = fsim::frontend::parse_text(
      "ambiguous_sv_parameter.sv",
      R"(
module ambiguous_sv_parameter #(
  parameter WIDTH = 1,
  parameter width = 2
) ();
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto ambiguous_vhdl_parent = fsim::frontend::parse_text(
      "ambiguous_vhdl_parent.vhd",
      R"(
entity Ambiguous_Vhdl_Parent is
end entity;
architecture rtl of Ambiguous_Vhdl_Parent is
begin
  Child : entity work.ambiguous_bound(rtl)
    generic map (Width => 3);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(ambiguous_sv_child.ok() && ambiguous_vhdl_parent.ok());
  auto ambiguous_design = ambiguous_vhdl_parent.design;
  append_units(ambiguous_design, ambiguous_sv_child.design);
  const std::vector<fsim::elaboration::Binding> ambiguous_bindings{{
      "ambiguous_vhdl_parent.child", "sv:work.ambiguous_sv_parameter",
      std::nullopt}};
  const auto ambiguous = fsim::elaboration::elaborate(
      ambiguous_design, "vhdl:work.ambiguous_vhdl_parent(rtl)",
      ambiguous_bindings);
  assert(!ambiguous.ok());
  assert(has_diagnostic(ambiguous, "FSIM-ELAB-PARAM-009"));

  TestSystemCFactoryProvider systemc_provider;
  systemc_provider.parameters = {
      {"INTEGER_VALUE", FSIM_SC_CONSTRUCTION_INTEGER, std::nullopt},
      {"NATURAL_VALUE", FSIM_SC_CONSTRUCTION_NATURAL, std::nullopt},
      {"POSITIVE_VALUE", FSIM_SC_CONSTRUCTION_POSITIVE, std::nullopt},
      {"BOOLEAN_VALUE", FSIM_SC_CONSTRUCTION_BOOLEAN, std::nullopt},
      {"BIT_VALUE", FSIM_SC_CONSTRUCTION_BIT, std::nullopt},
  };
  systemc_provider.prototype.target = "systemc:models.scalar";
  const auto systemc_sv_parent = fsim::frontend::parse_text(
      "systemc_scalar_sv_parent.sv",
      R"(
module systemc_scalar_sv_parent;
  systemc_scalar_bound #(
    .INTEGER_VALUE(-3), .NATURAL_VALUE(0), .POSITIVE_VALUE(2),
    .BOOLEAN_VALUE(1'b1), .BIT_VALUE(1'b0)
  ) child();
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(systemc_sv_parent.ok());
  const std::vector<fsim::elaboration::Binding> systemc_sv_binding{{
      "systemc_scalar_sv_parent.child", "systemc:models.scalar",
      std::nullopt}};
  const auto systemc_from_sv = fsim::elaboration::elaborate(
      systemc_sv_parent.design, "sv:work.systemc_scalar_sv_parent",
      systemc_sv_binding,
      std::span<const fsim::elaboration::SystemCInstanceDescription>{},
      &systemc_provider);
  assert(systemc_from_sv.ok());
  assert((systemc_provider.last_values
      == std::vector<std::pair<std::string, std::int64_t>>{
          {"INTEGER_VALUE", -3}, {"NATURAL_VALUE", 0},
          {"POSITIVE_VALUE", 2}, {"BOOLEAN_VALUE", 1},
          {"BIT_VALUE", 0}}));
  assert((systemc_from_sv.design->systemc_instances().front()
      .construction_identity_values
      == std::vector<std::pair<std::string, std::string>>{
          {"INTEGER_VALUE", "systemcconst-v1:type=0;value=-3"},
          {"NATURAL_VALUE", "systemcconst-v1:type=1;value=0"},
          {"POSITIVE_VALUE", "systemcconst-v1:type=2;value=2"},
          {"BOOLEAN_VALUE", "systemcconst-v1:type=3;value=1"},
          {"BIT_VALUE", "systemcconst-v1:type=4;value=0"}}));

  const auto systemc_vhdl_parent = fsim::frontend::parse_text(
      "systemc_scalar_vhdl_parent.vhd",
      R"(
entity Systemc_Scalar_Vhdl_Parent is
end entity;
architecture rtl of Systemc_Scalar_Vhdl_Parent is
begin
  Child : entity work.systemc_scalar_bound(rtl)
    generic map (
      Integer_Value => -4, Natural_Value => 1, Positive_Value => 3,
      Boolean_Value => true, Bit_Value => '1');
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(systemc_vhdl_parent.ok());
  const std::vector<fsim::elaboration::Binding> systemc_vhdl_binding{{
      "systemc_scalar_vhdl_parent.child", "systemc:models.scalar",
      std::nullopt}};
  const auto systemc_from_vhdl = fsim::elaboration::elaborate(
      systemc_vhdl_parent.design,
      "vhdl:work.systemc_scalar_vhdl_parent(rtl)",
      systemc_vhdl_binding,
      std::span<const fsim::elaboration::SystemCInstanceDescription>{},
      &systemc_provider);
  assert(systemc_from_vhdl.ok());
  assert((systemc_provider.last_values
      == std::vector<std::pair<std::string, std::int64_t>>{
          {"INTEGER_VALUE", -4}, {"NATURAL_VALUE", 1},
          {"POSITIVE_VALUE", 3}, {"BOOLEAN_VALUE", 1},
          {"BIT_VALUE", 1}}));
  assert(std::ranges::all_of(
      systemc_from_vhdl.design->systemc_instances().front()
          .construction_identity_values,
      [](const auto& value) {
        return value.second.starts_with("systemcconst-v1:");
      }));
}

}  // namespace fsim::tests::elaboration
