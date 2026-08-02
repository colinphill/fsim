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

void require_boolean_adapters(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::size_t count) {
  const auto& conversions = design.boundary_conversions();
  assert(conversions.size() == count);
  for (const auto& conversion : conversions) {
    assert(
        conversion.kind
        == fsim::elaboration::BoundaryConversionKind::boolean_adapter);
    assert(conversion.formal_width == 1);
    assert(conversion.actual_width == 1);
    assert(conversion.process);
    assert(!conversion.formal_range);
    assert(!conversion.actual_range);
  }
}

void require_integer_adapters(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::size_t count) {
  const auto& conversions = design.boundary_conversions();
  assert(conversions.size() == count);
  for (const auto& conversion : conversions) {
    assert(
        conversion.kind
        == fsim::elaboration::BoundaryConversionKind::integer_adapter);
    assert(conversion.formal_width == 32);
    assert(conversion.actual_width == 32);
    assert(conversion.formal_signed);
    assert(conversion.actual_signed);
    assert(conversion.process);
  }
}

}  // namespace

void test_mixed_language_conversions() {
  const auto vhdl_parent = fsim::frontend::parse_text(
      "boolean_vhdl_parent.vhd",
      R"(
entity Boolean_Vhdl_Parent is
end entity;
architecture rtl of Boolean_Vhdl_Parent is
  signal Logic_Source : boolean;
  signal Bit_Source : boolean;
  signal Logic_Result : boolean;
  signal Bit_Result : boolean;
begin
  drive : process
  begin
    Logic_Source <= true;
    Bit_Source <= false;
    wait;
  end process;
  child : boolean_sv_child
    port map (
      from_boolean_logic => Logic_Source,
      from_boolean_bit => Bit_Source,
      to_boolean_logic => Logic_Result,
      to_boolean_bit => Bit_Result
    );
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto sv_child = fsim::frontend::parse_text(
      "boolean_sv_child.sv",
      R"(
module boolean_sv_child(
  input logic from_boolean_logic,
  input bit from_boolean_bit,
  output logic to_boolean_logic,
  output bit to_boolean_bit
);
  assign to_boolean_logic = from_boolean_logic;
  assign to_boolean_bit = from_boolean_bit;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(vhdl_parent.ok() && sv_child.ok());
  auto vhdl_parent_design = vhdl_parent.design;
  append_units(vhdl_parent_design, sv_child.design);
  const std::vector<fsim::elaboration::Binding> vhdl_bindings{{
      "boolean_vhdl_parent.child",
      "sv:work.boolean_sv_child",
      std::nullopt}};
  const auto elaborated_vhdl_parent = fsim::elaboration::elaborate(
      vhdl_parent_design,
      "vhdl:work.boolean_vhdl_parent(rtl)",
      vhdl_bindings);
  if (!elaborated_vhdl_parent.ok()) {
    for (const auto& diagnostic : elaborated_vhdl_parent.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_vhdl_parent.ok());
  require_boolean_adapters(*elaborated_vhdl_parent.design, 4);
  auto vhdl_interpreter =
      elaborated_vhdl_parent.design->create_interpreter();
  assert(
      vhdl_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto vhdl_logic_result =
      elaborated_vhdl_parent.design->find_signal("logic_result");
  const auto vhdl_bit_result =
      elaborated_vhdl_parent.design->find_signal("bit_result");
  assert(vhdl_logic_result && vhdl_bit_result);
  assert(
      vhdl_interpreter->signal_value(*vhdl_logic_result).to_msb_string()
      == "1");
  assert(
      vhdl_interpreter->signal_value(*vhdl_bit_result).to_msb_string()
      == "0");

  const auto sv_parent = fsim::frontend::parse_text(
      "boolean_sv_parent.sv",
      R"(
module boolean_sv_parent;
  logic logic_source;
  bit bit_source;
  logic logic_result;
  bit bit_result;
  assign logic_source = 1'b1;
  assign bit_source = 1'b0;
  boolean_vhdl_child child(
    .from_logic(logic_source),
    .from_bit(bit_source),
    .to_logic(logic_result),
    .to_bit(bit_result)
  );
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto vhdl_child = fsim::frontend::parse_text(
      "boolean_vhdl_child.vhd",
      R"(
entity Boolean_Vhdl_Child is
  port (
    From_Logic : in boolean;
    From_Bit : in boolean;
    To_Logic : out boolean;
    To_Bit : out boolean
  );
end entity;
architecture rtl of Boolean_Vhdl_Child is
begin
  To_Logic <= From_Logic;
  To_Bit <= From_Bit;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(sv_parent.ok() && vhdl_child.ok());
  auto sv_parent_design = sv_parent.design;
  append_units(sv_parent_design, vhdl_child.design);
  const std::vector<fsim::elaboration::Binding> sv_bindings{{
      "boolean_sv_parent.child",
      "vhdl:work.boolean_vhdl_child(rtl)",
      std::nullopt}};
  const auto elaborated_sv_parent = fsim::elaboration::elaborate(
      sv_parent_design,
      "sv:work.boolean_sv_parent",
      sv_bindings);
  if (!elaborated_sv_parent.ok()) {
    for (const auto& diagnostic : elaborated_sv_parent.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_sv_parent.ok());
  require_boolean_adapters(*elaborated_sv_parent.design, 4);
  auto sv_interpreter = elaborated_sv_parent.design->create_interpreter();
  assert(
      sv_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto sv_logic_result =
      elaborated_sv_parent.design->find_signal("logic_result");
  const auto sv_bit_result =
      elaborated_sv_parent.design->find_signal("bit_result");
  assert(sv_logic_result && sv_bit_result);
  assert(
      sv_interpreter->signal_value(*sv_logic_result).to_msb_string()
      == "1");
  assert(
      sv_interpreter->signal_value(*sv_bit_result).to_msb_string()
      == "0");

  const auto invalid_sv_parent = fsim::frontend::parse_text(
      "invalid_boolean_sv_parent.sv",
      R"(
module invalid_boolean_sv_parent;
  logic invalid_source;
  assign invalid_source = 1'b0;
  boolean_sink child(.value(invalid_source));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto boolean_sink = fsim::frontend::parse_text(
      "boolean_sink.vhd",
      R"(
entity Boolean_Sink is
  port (Value : in boolean);
end entity;
architecture rtl of Boolean_Sink is
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(invalid_sv_parent.ok() && boolean_sink.ok());
  auto invalid_design = invalid_sv_parent.design;
  append_units(invalid_design, boolean_sink.design);
  const std::vector<fsim::elaboration::Binding> invalid_bindings{{
      "invalid_boolean_sv_parent.child",
      "vhdl:work.boolean_sink(rtl)",
      std::nullopt}};
  const auto invalid_boolean = fsim::elaboration::elaborate(
      invalid_design,
      "sv:work.invalid_boolean_sv_parent",
      invalid_bindings);
  assert(invalid_boolean.ok());
  require_boolean_adapters(*invalid_boolean.design, 1);
  auto invalid_interpreter = invalid_boolean.design->create_interpreter();
  assert(
      invalid_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto invalid_source =
      invalid_boolean.design->find_signal("invalid_source");
  assert(invalid_source);
  invalid_interpreter->deposit_signal(
      *invalid_source,
      fsim::runtime::PackedLogic4::from_msb_string("X"));
  bool rejected_unknown = false;
  try {
    (void)invalid_interpreter->run();
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    rejected_unknown = std::string_view{error.what()}.find(
        "unknown or high-impedance") != std::string_view::npos;
  }
  assert(rejected_unknown);

  const auto integer_vhdl_parent = fsim::frontend::parse_text(
      "integer_vhdl_parent.vhd",
      R"(
entity Integer_Vhdl_Parent is
end entity;
architecture rtl of Integer_Vhdl_Parent is
  signal Source : integer range -4 to 4;
  signal Result : integer range -4 to 2;
begin
  Source <= -2;
  child : integer_sv_child
    port map (Value => Source, Result => Result);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto integer_sv_child = fsim::frontend::parse_text(
      "integer_sv_child.sv",
      R"(
module integer_sv_child(
  input bit signed [31:0] value,
  output logic signed [31:0] result
);
  assign result = value + 1;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(integer_vhdl_parent.ok() && integer_sv_child.ok());
  auto integer_vhdl_design = integer_vhdl_parent.design;
  append_units(integer_vhdl_design, integer_sv_child.design);
  const std::vector<fsim::elaboration::Binding> integer_vhdl_bindings{{
      "integer_vhdl_parent.child",
      "sv:work.integer_sv_child",
      std::nullopt}};
  const auto elaborated_integer_vhdl = fsim::elaboration::elaborate(
      integer_vhdl_design,
      "vhdl:work.integer_vhdl_parent(rtl)",
      integer_vhdl_bindings);
  if (!elaborated_integer_vhdl.ok()) {
    for (const auto& diagnostic : elaborated_integer_vhdl.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_integer_vhdl.ok());
  require_integer_adapters(*elaborated_integer_vhdl.design, 2);
  const auto& integer_vhdl_conversions =
      elaborated_integer_vhdl.design->boundary_conversions();
  const auto checked_vhdl_result = std::ranges::find(
      integer_vhdl_conversions,
      "integer_vhdl_parent.child.result",
      &fsim::elaboration::BoundaryConversionInfo::path);
  assert(checked_vhdl_result != integer_vhdl_conversions.end());
  assert(checked_vhdl_result->actual_integer_range);
  assert(checked_vhdl_result->actual_integer_range->left == -4);
  assert(checked_vhdl_result->actual_integer_range->right == 2);
  auto integer_vhdl_interpreter =
      elaborated_integer_vhdl.design->create_interpreter();
  assert(
      integer_vhdl_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto integer_vhdl_result =
      elaborated_integer_vhdl.design->find_signal("result");
  const auto integer_sv_result = elaborated_integer_vhdl.design->find_signal(
      "integer_vhdl_parent.child.result");
  assert(integer_vhdl_result && integer_sv_result);
  assert(*integer_vhdl_result != *integer_sv_result);
  assert(
      integer_vhdl_interpreter
          ->signal_value(*integer_vhdl_result)
          .to_msb_string()
      == "11111111111111111111111111111111");

  auto range_interpreter =
      elaborated_integer_vhdl.design->create_interpreter();
  assert(
      range_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  range_interpreter->deposit_signal(
      *integer_sv_result,
      fsim::runtime::PackedLogic4::from_msb_string(
          "00000000000000000000000000000011"));
  bool rejected_range = false;
  try {
    (void)range_interpreter->run();
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    rejected_range = std::string_view{error.what()}.find(
        "integer subtype range check failed") != std::string_view::npos;
  }
  assert(rejected_range);

  auto unknown_integer_interpreter =
      elaborated_integer_vhdl.design->create_interpreter();
  assert(
      unknown_integer_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  unknown_integer_interpreter->deposit_signal(
      *integer_sv_result,
      fsim::runtime::PackedLogic4(32, fsim::runtime::Logic4::x));
  bool rejected_integer_unknown = false;
  try {
    (void)unknown_integer_interpreter->run();
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    rejected_integer_unknown = std::string_view{error.what()}.find(
        "unknown or high-impedance") != std::string_view::npos;
  }
  assert(rejected_integer_unknown);

  const auto integer_sv_parent = fsim::frontend::parse_text(
      "integer_sv_parent.sv",
      R"(
module integer_sv_parent;
  logic signed [31:0] source;
  bit signed [31:0] result;
  assign source = -2;
  integer_vhdl_child child(.value(source), .result(result));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto integer_vhdl_child = fsim::frontend::parse_text(
      "integer_vhdl_child.vhd",
      R"(
entity Integer_Vhdl_Child is
  port (
    Value : in integer range -4 to 4;
    Result : out integer range -4 to 2
  );
end entity;
architecture rtl of Integer_Vhdl_Child is
begin
  Result <= Value + 1;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(integer_sv_parent.ok() && integer_vhdl_child.ok());
  auto integer_sv_design = integer_sv_parent.design;
  append_units(integer_sv_design, integer_vhdl_child.design);
  const std::vector<fsim::elaboration::Binding> integer_sv_bindings{{
      "integer_sv_parent.child",
      "vhdl:work.integer_vhdl_child(rtl)",
      std::nullopt}};
  const auto elaborated_integer_sv = fsim::elaboration::elaborate(
      integer_sv_design,
      "sv:work.integer_sv_parent",
      integer_sv_bindings);
  if (!elaborated_integer_sv.ok()) {
    for (const auto& diagnostic : elaborated_integer_sv.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_integer_sv.ok());
  require_integer_adapters(*elaborated_integer_sv.design, 2);
  auto integer_sv_interpreter =
      elaborated_integer_sv.design->create_interpreter();
  assert(
      integer_sv_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto integer_sv_parent_result =
      elaborated_integer_sv.design->find_signal("result");
  const auto integer_vhdl_child_result =
      elaborated_integer_sv.design->find_signal(
          "integer_sv_parent.child.result");
  assert(integer_sv_parent_result && integer_vhdl_child_result);
  assert(*integer_sv_parent_result != *integer_vhdl_child_result);
  assert(
      integer_sv_interpreter
          ->signal_value(*integer_sv_parent_result)
          .to_msb_string()
      == "11111111111111111111111111111111");

  const auto unsigned_sv_parent = fsim::frontend::parse_text(
      "unsigned_integer_sv_parent.sv",
      R"(
module unsigned_integer_sv_parent;
  bit [31:0] source;
  integer_vhdl_sink child(.value(source));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto integer_vhdl_sink = fsim::frontend::parse_text(
      "integer_vhdl_sink.vhd",
      R"(
entity Integer_Vhdl_Sink is
  port (Value : in integer);
end entity;
architecture rtl of Integer_Vhdl_Sink is
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(unsigned_sv_parent.ok() && integer_vhdl_sink.ok());
  auto unsigned_integer_design = unsigned_sv_parent.design;
  append_units(unsigned_integer_design, integer_vhdl_sink.design);
  const std::vector<fsim::elaboration::Binding>
      unsigned_integer_bindings{{
          "unsigned_integer_sv_parent.child",
          "vhdl:work.integer_vhdl_sink(rtl)",
          std::nullopt}};
  const auto rejected_unsigned_integer = fsim::elaboration::elaborate(
      unsigned_integer_design,
      "sv:work.unsigned_integer_sv_parent",
      unsigned_integer_bindings);
  assert(!rejected_unsigned_integer.ok());
  assert(has_diagnostic(
      rejected_unsigned_integer, "FSIM-ELAB-BIND-021"));
  assert(has_diagnostic(
      rejected_unsigned_integer, "FSIM-ELAB-BIND-051"));

  const auto bit_vhdl_parent = fsim::frontend::parse_text(
      "bit_vhdl_parent.vhd",
      R"(
entity Bit_Vhdl_Parent is
end entity;
architecture rtl of Bit_Vhdl_Parent is
  signal Source : bit_vector(20 downto 17);
  signal Scalar_Source : bit;
  signal Result : bit_vector(3 to 6);
  signal Scalar_Result : bit;
begin
  drive : process
  begin
    Source <= "1010";
    Scalar_Source <= '1';
    wait;
  end process;
  child : bit_sv_child
    port map (
      Data => Source,
      Scalar_Data => Scalar_Source,
      Result => Result,
      Scalar_Result => Scalar_Result
    );
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto bit_sv_child = fsim::frontend::parse_text(
      "bit_sv_child.sv",
      R"(
module bit_sv_child(
  input bit [1:4] data,
  input bit scalar_data,
  output bit [9:6] result,
  output bit scalar_result
);
  assign result = data;
  assign scalar_result = scalar_data;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(bit_vhdl_parent.ok() && bit_sv_child.ok());
  auto bit_vhdl_design = bit_vhdl_parent.design;
  append_units(bit_vhdl_design, bit_sv_child.design);
  const std::vector<fsim::elaboration::Binding> bit_vhdl_bindings{{
      "bit_vhdl_parent.child",
      "sv:work.bit_sv_child",
      std::nullopt}};
  const auto elaborated_bit_vhdl = fsim::elaboration::elaborate(
      bit_vhdl_design,
      "vhdl:work.bit_vhdl_parent(rtl)",
      bit_vhdl_bindings);
  if (!elaborated_bit_vhdl.ok()) {
    for (const auto& diagnostic : elaborated_bit_vhdl.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_bit_vhdl.ok());
  const auto& bit_vhdl_conversions =
      elaborated_bit_vhdl.design->boundary_conversions();
  assert(bit_vhdl_conversions.size() == 4);
  for (const auto& conversion : bit_vhdl_conversions) {
    assert(
        conversion.kind
        == fsim::elaboration::BoundaryConversionKind::ordinal_alias);
    assert(conversion.formal_signal == conversion.actual_signal);
    assert(!conversion.process);
    assert(
        conversion.formal_domain == fsim::frontend::ValueDomain::Bit2);
    assert(
        conversion.actual_domain == fsim::frontend::ValueDomain::Bit2);
  }
  assert(
      std::ranges::count_if(
          bit_vhdl_conversions,
          [](const auto& conversion) {
            return conversion.formal_range && conversion.actual_range;
          })
      == 2);
  assert(
      std::ranges::count_if(
          bit_vhdl_conversions,
          [](const auto& conversion) {
            return !conversion.formal_range && !conversion.actual_range;
          })
      == 2);
  auto bit_vhdl_interpreter =
      elaborated_bit_vhdl.design->create_interpreter();
  assert(
      bit_vhdl_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto bit_vhdl_result =
      elaborated_bit_vhdl.design->find_signal("result");
  const auto bit_vhdl_scalar_result =
      elaborated_bit_vhdl.design->find_signal("scalar_result");
  assert(bit_vhdl_result && bit_vhdl_scalar_result);
  assert(
      bit_vhdl_interpreter
          ->signal_value(*bit_vhdl_result)
          .to_msb_string()
      == "1010");
  assert(
      bit_vhdl_interpreter
          ->signal_value(*bit_vhdl_scalar_result)
          .to_msb_string()
      == "1");

  const auto bit_sv_parent = fsim::frontend::parse_text(
      "bit_sv_parent.sv",
      R"(
module bit_sv_parent;
  bit [9:6] source;
  bit scalar_source;
  bit [2:5] result;
  bit scalar_result;
  assign source = 4'b0101;
  assign scalar_source = 1'b1;
  bit_vhdl_child child(
    .data(source),
    .scalar_data(scalar_source),
    .result(result),
    .scalar_result(scalar_result)
  );
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto bit_vhdl_child = fsim::frontend::parse_text(
      "bit_vhdl_child.vhd",
      R"(
entity Bit_Vhdl_Child is
  port (
    Data : in bit_vector(20 to 23);
    Scalar_Data : in bit;
    Result : out bit_vector(7 downto 4);
    Scalar_Result : out bit
  );
end entity;
architecture rtl of Bit_Vhdl_Child is
begin
  Result <= Data;
  Scalar_Result <= Scalar_Data;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(bit_sv_parent.ok() && bit_vhdl_child.ok());
  auto bit_sv_design = bit_sv_parent.design;
  append_units(bit_sv_design, bit_vhdl_child.design);
  const std::vector<fsim::elaboration::Binding> bit_sv_bindings{{
      "bit_sv_parent.child",
      "vhdl:work.bit_vhdl_child(rtl)",
      std::nullopt}};
  const auto elaborated_bit_sv = fsim::elaboration::elaborate(
      bit_sv_design,
      "sv:work.bit_sv_parent",
      bit_sv_bindings);
  if (!elaborated_bit_sv.ok()) {
    for (const auto& diagnostic : elaborated_bit_sv.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_bit_sv.ok());
  const auto& bit_sv_conversions =
      elaborated_bit_sv.design->boundary_conversions();
  assert(bit_sv_conversions.size() == 4);
  assert(std::ranges::all_of(
      bit_sv_conversions,
      [](const auto& conversion) {
        return conversion.kind
                == fsim::elaboration::BoundaryConversionKind::ordinal_alias
            && conversion.formal_signal == conversion.actual_signal
            && !conversion.process;
      }));
  auto bit_sv_interpreter = elaborated_bit_sv.design->create_interpreter();
  assert(
      bit_sv_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto bit_sv_result =
      elaborated_bit_sv.design->find_signal("result");
  const auto bit_sv_scalar_result =
      elaborated_bit_sv.design->find_signal("scalar_result");
  assert(bit_sv_result && bit_sv_scalar_result);
  assert(
      bit_sv_interpreter->signal_value(*bit_sv_result).to_msb_string()
      == "0101");
  assert(
      bit_sv_interpreter
          ->signal_value(*bit_sv_scalar_result)
          .to_msb_string()
      == "1");

  const auto lossy_bit_vhdl_parent = fsim::frontend::parse_text(
      "lossy_bit_vhdl_parent.vhd",
      R"(
entity Lossy_Bit_Vhdl_Parent is
end entity;
architecture rtl of Lossy_Bit_Vhdl_Parent is
  signal Source : bit;
  signal Result : bit;
begin
  child : lossy_bit_sv_child
    port map (Data => Source, Result => Result);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto lossy_bit_sv_child = fsim::frontend::parse_text(
      "lossy_bit_sv_child.sv",
      R"(
module lossy_bit_sv_child(
  input logic data,
  output logic result
);
  assign result = data;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(lossy_bit_vhdl_parent.ok() && lossy_bit_sv_child.ok());
  auto lossy_bit_design = lossy_bit_vhdl_parent.design;
  append_units(lossy_bit_design, lossy_bit_sv_child.design);
  const std::vector<fsim::elaboration::Binding> lossy_bit_bindings{{
      "lossy_bit_vhdl_parent.child",
      "sv:work.lossy_bit_sv_child",
      std::nullopt}};
  const auto rejected_lossy_bits = fsim::elaboration::elaborate(
      lossy_bit_design,
      "vhdl:work.lossy_bit_vhdl_parent(rtl)",
      lossy_bit_bindings);
  assert(!rejected_lossy_bits.ok());
  assert(
      std::ranges::count_if(
          rejected_lossy_bits.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-BIND-022";
          })
      == 1);

  const auto lossy_bit_sv_parent = fsim::frontend::parse_text(
      "lossy_bit_sv_parent.sv",
      R"(
module lossy_bit_sv_parent;
  logic source;
  bit_vhdl_sink child(.data(source));
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto bit_vhdl_sink = fsim::frontend::parse_text(
      "bit_vhdl_sink.vhd",
      R"(
entity Bit_Vhdl_Sink is
  port (Data : in bit);
end entity;
architecture rtl of Bit_Vhdl_Sink is
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(lossy_bit_sv_parent.ok() && bit_vhdl_sink.ok());
  auto lossy_bit_reverse_design = lossy_bit_sv_parent.design;
  append_units(lossy_bit_reverse_design, bit_vhdl_sink.design);
  const std::vector<fsim::elaboration::Binding>
      lossy_bit_reverse_bindings{{
          "lossy_bit_sv_parent.child",
          "vhdl:work.bit_vhdl_sink(rtl)",
          std::nullopt}};
  const auto rejected_lossy_bit_input = fsim::elaboration::elaborate(
      lossy_bit_reverse_design,
      "sv:work.lossy_bit_sv_parent",
      lossy_bit_reverse_bindings);
  assert(!rejected_lossy_bit_input.ok());
  assert(has_diagnostic(
      rejected_lossy_bit_input, "FSIM-ELAB-BIND-022"));

  constexpr std::array logic9_collapse{
      std::pair{fsim::runtime::Logic9::u, fsim::runtime::Logic4::x},
      std::pair{fsim::runtime::Logic9::x, fsim::runtime::Logic4::x},
      std::pair{fsim::runtime::Logic9::zero, fsim::runtime::Logic4::zero},
      std::pair{fsim::runtime::Logic9::one, fsim::runtime::Logic4::one},
      std::pair{fsim::runtime::Logic9::z, fsim::runtime::Logic4::z},
      std::pair{fsim::runtime::Logic9::w, fsim::runtime::Logic4::x},
      std::pair{fsim::runtime::Logic9::l, fsim::runtime::Logic4::zero},
      std::pair{fsim::runtime::Logic9::h, fsim::runtime::Logic4::one},
      std::pair{
          fsim::runtime::Logic9::dont_care,
          fsim::runtime::Logic4::x}};
  for (const auto& [source, expected] : logic9_collapse) {
    assert(fsim::runtime::to_logic4(source) == expected);
  }
  constexpr std::array logic4_expansion{
      std::pair{fsim::runtime::Logic4::zero, fsim::runtime::Logic9::zero},
      std::pair{fsim::runtime::Logic4::one, fsim::runtime::Logic9::one},
      std::pair{fsim::runtime::Logic4::x, fsim::runtime::Logic9::x},
      std::pair{fsim::runtime::Logic4::z, fsim::runtime::Logic9::z}};
  for (const auto& [source, expected] : logic4_expansion) {
    assert(fsim::runtime::to_logic9(source) == expected);
  }

  const auto logic9_vhdl_parent = fsim::frontend::parse_text(
      "logic9_vhdl_parent.vhd",
      R"(
entity Logic9_Vhdl_Parent is
end entity;
architecture rtl of Logic9_Vhdl_Parent is
  signal Source : std_logic_vector(8 downto 0);
  signal Scalar_Source : std_ulogic;
  signal Result : std_ulogic_vector(20 to 28);
  signal Scalar_Result : std_logic;
begin
  drive : process
  begin
    Source <= "UX01ZWLH-";
    Scalar_Source <= 'H';
    wait;
  end process;
  child : logic4_sv_child
    port map (
      Data => Source,
      Scalar_Data => Scalar_Source,
      Result => Result,
      Scalar_Result => Scalar_Result
    );
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto logic4_sv_child = fsim::frontend::parse_text(
      "logic4_sv_child.sv",
      R"(
module logic4_sv_child(
  input logic [1:9] data,
  input logic scalar_data,
  output logic [9:1] result,
  output logic scalar_result
);
  assign result = data;
  assign scalar_result = scalar_data;
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(logic9_vhdl_parent.ok() && logic4_sv_child.ok());
  auto logic9_vhdl_design = logic9_vhdl_parent.design;
  append_units(logic9_vhdl_design, logic4_sv_child.design);
  const std::vector<fsim::elaboration::Binding> logic9_vhdl_bindings{{
      "logic9_vhdl_parent.child",
      "sv:work.logic4_sv_child",
      std::nullopt}};
  const auto elaborated_logic9_vhdl = fsim::elaboration::elaborate(
      logic9_vhdl_design,
      "vhdl:work.logic9_vhdl_parent(rtl)",
      logic9_vhdl_bindings);
  if (!elaborated_logic9_vhdl.ok()) {
    for (const auto& diagnostic : elaborated_logic9_vhdl.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_logic9_vhdl.ok());
  const auto& logic9_vhdl_conversions =
      elaborated_logic9_vhdl.design->boundary_conversions();
  assert(logic9_vhdl_conversions.size() == 4);
  assert(std::ranges::all_of(
      logic9_vhdl_conversions,
      [](const auto& conversion) {
        return conversion.kind
                == fsim::elaboration::BoundaryConversionKind::state_domain_alias
            && conversion.state_domain_changed
            && conversion.formal_signal == conversion.actual_signal
            && !conversion.process;
      }));
  auto logic9_vhdl_interpreter =
      elaborated_logic9_vhdl.design->create_interpreter();
  assert(
      logic9_vhdl_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto logic9_vhdl_result =
      elaborated_logic9_vhdl.design->find_signal("result");
  const auto logic9_vhdl_scalar_result =
      elaborated_logic9_vhdl.design->find_signal("scalar_result");
  assert(logic9_vhdl_result && logic9_vhdl_scalar_result);
  assert(
      logic9_vhdl_interpreter
          ->signal_value(*logic9_vhdl_result)
          .to_msb_string()
      == "XX01ZX01X");
  assert(
      logic9_vhdl_interpreter
          ->signal_value(*logic9_vhdl_scalar_result)
          .to_msb_string()
      == "1");

  const auto logic4_sv_parent = fsim::frontend::parse_text(
      "logic4_sv_parent.sv",
      R"(
module logic4_sv_parent;
  logic [3:0] source;
  logic scalar_source;
  logic [7:4] result;
  logic scalar_result;
  assign source = 4'b01xz;
  assign scalar_source = 1'bz;
  logic9_vhdl_child child(
    .data(source),
    .scalar_data(scalar_source),
    .result(result),
    .scalar_result(scalar_result)
  );
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  const auto logic9_vhdl_child = fsim::frontend::parse_text(
      "logic9_vhdl_child.vhd",
      R"(
entity Logic9_Vhdl_Child is
  port (
    Data : in std_ulogic_vector(20 to 23);
    Scalar_Data : in std_logic;
    Result : out std_logic_vector(7 downto 4);
    Scalar_Result : out std_ulogic
  );
end entity;
architecture rtl of Logic9_Vhdl_Child is
begin
  Result <= Data;
  Scalar_Result <= Scalar_Data;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(logic4_sv_parent.ok() && logic9_vhdl_child.ok());
  auto logic4_sv_design = logic4_sv_parent.design;
  append_units(logic4_sv_design, logic9_vhdl_child.design);
  const std::vector<fsim::elaboration::Binding> logic4_sv_bindings{{
      "logic4_sv_parent.child",
      "vhdl:work.logic9_vhdl_child(rtl)",
      std::nullopt}};
  const auto elaborated_logic4_sv = fsim::elaboration::elaborate(
      logic4_sv_design,
      "sv:work.logic4_sv_parent",
      logic4_sv_bindings);
  if (!elaborated_logic4_sv.ok()) {
    for (const auto& diagnostic : elaborated_logic4_sv.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(elaborated_logic4_sv.ok());
  const auto& logic4_sv_conversions =
      elaborated_logic4_sv.design->boundary_conversions();
  assert(logic4_sv_conversions.size() == 4);
  assert(std::ranges::all_of(
      logic4_sv_conversions,
      [](const auto& conversion) {
        return conversion.kind
                == fsim::elaboration::BoundaryConversionKind::state_domain_alias
            && conversion.state_domain_changed
            && conversion.formal_signal == conversion.actual_signal
            && !conversion.process;
      }));
  auto logic4_sv_interpreter =
      elaborated_logic4_sv.design->create_interpreter();
  assert(
      logic4_sv_interpreter->run().status
      == fsim::runtime::RunStatus::completed);
  const auto logic4_sv_result =
      elaborated_logic4_sv.design->find_signal("result");
  const auto logic4_sv_scalar_result =
      elaborated_logic4_sv.design->find_signal("scalar_result");
  assert(logic4_sv_result && logic4_sv_scalar_result);
  assert(
      logic4_sv_interpreter
          ->signal_value(*logic4_sv_result)
          .to_msb_string()
      == "01XZ");
  assert(
      logic4_sv_interpreter
          ->signal_value(*logic4_sv_scalar_result)
          .to_msb_string()
      == "Z");

  const auto lossy_logic9_vhdl_parent = fsim::frontend::parse_text(
      "lossy_logic9_vhdl_parent.vhd",
      R"(
entity Lossy_Logic9_Vhdl_Parent is
end entity;
architecture rtl of Lossy_Logic9_Vhdl_Parent is
  signal Source : std_logic;
begin
  child : bit_sv_sink port map (Data => Source);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  const auto bit_sv_sink = fsim::frontend::parse_text(
      "bit_sv_sink.sv",
      R"(
module bit_sv_sink(input bit data);
endmodule
)",
      fsim::frontend::Language::SystemVerilog2017);
  assert(lossy_logic9_vhdl_parent.ok() && bit_sv_sink.ok());
  auto lossy_logic9_design = lossy_logic9_vhdl_parent.design;
  append_units(lossy_logic9_design, bit_sv_sink.design);
  const std::vector<fsim::elaboration::Binding> lossy_logic9_bindings{{
      "lossy_logic9_vhdl_parent.child",
      "sv:work.bit_sv_sink",
      std::nullopt}};
  const auto rejected_lossy_logic9 = fsim::elaboration::elaborate(
      lossy_logic9_design,
      "vhdl:work.lossy_logic9_vhdl_parent(rtl)",
      lossy_logic9_bindings);
  assert(!rejected_lossy_logic9.ok());
  assert(has_diagnostic(
      rejected_lossy_logic9, "FSIM-ELAB-BIND-022"));
}

}  // namespace fsim::tests::elaboration
