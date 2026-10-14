// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_attribute_closure() {
  const auto range_direction_retention = fsim::frontend::parse_text(
      "vhdl_integer_range_direction_retention.vhd",
      R"(
entity Range_Direction_Retention is end entity;
architecture rtl of range_direction_retention is
  constant Ascending_Source : bit_vector(5 to 5) := "0";
  constant Descending_Source : bit_vector(5 downto 5) := "1";
  subtype Ascending_Index_T is integer range Ascending_Source'range;
  subtype Descending_Index_T is integer range Descending_Source'range;
begin
  wait_process : process
  begin
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(range_direction_retention.ok());
  const auto frontend_index = [&](const std::string_view name)
      -> const fsim::frontend::TypeAliasDeclaration* {
    for (const auto& unit : range_direction_retention.design.units) {
      const auto found = std::ranges::find_if(
          unit.type_aliases,
          [&](const auto& alias) { return alias.name == name; });
      if (found != unit.type_aliases.end()) {
        return &*found;
      }
    }
    return nullptr;
  };
  for (const auto name : { "ascending_index_t", "descending_index_t" }) {
    const auto* alias = frontend_index(name);
    assert(alias != nullptr && alias->type.integer_range_expression
        && alias->type.integer_range_expression->direction_from_bounds);
  }
  const auto range_compiled = compile_test_design(
      range_direction_retention.design);
  assert(range_compiled.valid());
  for (const auto name : { "ascending_index_t", "descending_index_t" }) {
    const auto found = std::ranges::find_if(
        range_compiled.vhdl_hir.types(),
        [&](const auto& type) { return type.name == name; });
    assert(found != range_compiled.vhdl_hir.types().end()
        && found->scalar_range
        && found->scalar_range->direction_from_bounds
        && found->scalar_range->left_expression
        && found->scalar_range->right_expression);
  }

  const auto parsed = fsim::frontend::parse_text(
      "vhdl_attribute_closure.vhd",
      R"(
entity Attribute_Closure is
end entity;

architecture rtl of attribute_closure is
  subtype Count_T is integer range -2 to 2;
  type Mode_T is (Idle, Ready, Busy);
  subtype Reverse_Mode_T is Mode_T range Busy downto Idle;
  type Matrix_T is array (2 downto 1, -1 to 1) of bit;
  type Null_Matrix_T is array (1 to 0, 3 downto 2) of bit;
  type Holder_T is record
    Matrix : Matrix_T;
    Mode : Reverse_Mode_T;
  end record;

  constant Static_Total : integer :=
    Matrix_T'length(1) + Matrix_T'length(2) + Count_T'high;
  signal Holder : Holder_T;
  signal Scalar_Left : integer;
  signal Scalar_Right : integer;
  signal Scalar_Length : integer;
  signal Scalar_Position : integer;
  signal Scalar_Value : integer;
  signal Scalar_Successor : integer;
  signal Scalar_Predecessor : integer;
  signal Scalar_Leftof : integer;
  signal Scalar_Rightof : integer;
  signal Scalar_Ascending : boolean;
  signal Bit_Right : bit;
  signal Bit_Position : integer;
  signal Boolean_Successor : boolean;
  signal Dimension_Left : integer;
  signal Dimension_Right : integer;
  signal Dimension_Low : integer;
  signal Dimension_High : integer;
  signal Dimension_Length : integer;
  signal Dimension_Ascending : boolean;
  signal Nested_Length : integer;
  signal Null_Length : integer;
  signal Static_Result : integer;
  signal Loop_Count : integer;
begin
  drive : process
    variable Local_Holder : Holder_T;
    variable Count : integer := 0;
  begin
    Scalar_Left <= Count_T'left;
    Scalar_Right <= Count_T'right;
    Scalar_Length <= Count_T'length;
    Scalar_Position <= Count_T'pos(1);
    Scalar_Value <= Count_T'val(-1);
    Scalar_Successor <= Count_T'succ(-2);
    Scalar_Predecessor <= Count_T'pred(2);
    Scalar_Leftof <= Count_T'leftof(0);
    Scalar_Rightof <= Count_T'rightof(0);
    Scalar_Ascending <= Count_T'ascending;
    Bit_Right <= bit'right;
    Bit_Position <= bit'pos('1');
    Boolean_Successor <= boolean'succ(false);
    Dimension_Left <= Matrix_T'left(1);
    Dimension_Right <= Matrix_T'right(2);
    Dimension_Low <= Matrix_T'low(2);
    Dimension_High <= Matrix_T'high(2);
    Dimension_Length <= Matrix_T'length(2);
    Dimension_Ascending <= Matrix_T'ascending(2);
    Nested_Length <= Holder.Matrix'length(2)
      + Local_Holder.Matrix'length(1);
    Null_Length <= Null_Matrix_T'length(1);
    Static_Result <= Static_Total;
    for Index in Count_T'range loop
      Count := Count + 1;
    end loop;
    for Index in Matrix_T'reverse_range(2) loop
      Count := Count + 1;
    end loop;
    for Item in Reverse_Mode_T'range loop
      Count := Count + 1;
    end loop;
    for Index in Holder.Matrix'range(2) loop
      Count := Count + 1;
    end loop;
    Loop_Count <= Count;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2019);
  assert(parsed.ok());
  const auto elaborated = compile_and_elaborate(
      parsed.design, "vhdl:work.attribute_closure(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message
                << " at " << diagnostic.span.begin.line << ':'
                << diagnostic.span.begin.column << '\n';
    }
  }
  assert(elaborated.ok());
  auto interpreter = elaborated.design->create_interpreter();
  const auto run = interpreter->run();
  assert(run.status == fsim::runtime::RunStatus::completed);
  const auto value = [&](const std::string_view name) {
    const auto signal = elaborated.design->find_signal(name);
    assert(signal);
    return static_cast<std::int32_t>(
        interpreter->signal_value(*signal).low_word().aval);
  };
  assert(
      value("scalar_left") == -2 && value("scalar_right") == 2
      && value("scalar_length") == 5
      && value("scalar_position") == 1 && value("scalar_value") == -1
      && value("scalar_successor") == -1
      && value("scalar_predecessor") == 1
      && value("scalar_leftof") == -1 && value("scalar_rightof") == 1
      && value("scalar_ascending") == 1 && value("bit_right") == 1
      && value("bit_position") == 1 && value("boolean_successor") == 1
      && value("dimension_left") == 2 && value("dimension_right") == 1
      && value("dimension_low") == -1 && value("dimension_high") == 1
      && value("dimension_length") == 3
      && value("dimension_ascending") == 1
      && value("nested_length") == 5 && value("null_length") == 0
      && value("static_result") == 7 && value("loop_count") == 14);

  const auto physical_positions = fsim::frontend::parse_text(
      "vhdl_physical_scalar_positions.vhd",
      R"(
entity Physical_Scalar_Positions is end entity;
architecture rtl of physical_scalar_positions is
  type Distance_T is range 0 to 100 units
    tick;
    double = 2 tick;
    octuple = 8 tick;
  end units Distance_T;
  signal Real_Value : Distance_T;
  signal Literal_Position : integer;
  signal Object_Position : integer;
  signal Time_Position : integer;
  signal Integer_Value : Distance_T;
begin
  drive : process
    variable Current : Distance_T := 3 double;
  begin
    Real_Value <= 0.5 octuple;
    Literal_Position <= Distance_T'pos(0.5 octuple);
    Object_Position <= Distance_T'pos(Current);
    Time_Position <= time'pos(fs);
    Integer_Value <= Distance_T'val(5);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(physical_positions.ok());
  const auto physical_positions_result = compile_and_elaborate(
      physical_positions.design,
      "vhdl:work.physical_scalar_positions(rtl)");
  if (!physical_positions_result.ok()) {
    for (const auto& diagnostic : physical_positions_result.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(physical_positions_result.ok());
  auto physical_interpreter
      = physical_positions_result.design->create_interpreter();
  const auto physical_run = physical_interpreter->run();
  assert(physical_run.status == fsim::runtime::RunStatus::completed);
  const auto physical_value = [&](const std::string_view name) {
    const auto signal = physical_positions_result.design->find_signal(name);
    assert(signal);
    return static_cast<std::int32_t>(
        physical_interpreter->signal_value(*signal).low_word().aval);
  };
  assert(physical_value("real_value") == 4
      && physical_value("literal_position") == 4
      && physical_value("object_position") == 6
      && physical_value("time_position") == 1
      && physical_value("integer_value") == 5);

  const auto repeated_physical_units = fsim::frontend::parse_text(
      "vhdl_physical_unit_scope_positions.vhd",
      R"(
entity First_Physical_Unit_Scope is end entity;
architecture rtl of first_physical_unit_scope is
  type Scale_T is range 0 to 20 units
    tick;
    double = 2 tick;
  end units Scale_T;
  signal Position : integer;
begin
  drive : process
  begin
    Position <= Scale_T'pos(1 double);
    wait;
  end process;
end architecture;

entity Second_Physical_Unit_Scope is end entity;
architecture rtl of second_physical_unit_scope is
  type Scale_T is range 0 to 20 units
    tick;
    double = 3 tick;
  end units Scale_T;
  signal Position : integer;
begin
  drive : process
  begin
    Position <= Scale_T'pos(1 double);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(repeated_physical_units.ok());
  const auto scoped_physical_value = [&](const std::string_view top) {
    const auto compiled = compile_and_elaborate(
        repeated_physical_units.design, top);
    assert(compiled.ok());
    auto interpreter = compiled.design->create_interpreter();
    assert(interpreter->run().status == fsim::runtime::RunStatus::completed);
    const auto signal = compiled.design->find_signal("position");
    assert(signal);
    return static_cast<std::int32_t>(
        interpreter->signal_value(*signal).low_word().aval);
  };
  assert(scoped_physical_value(
             "vhdl:work.first_physical_unit_scope(rtl)") == 2
      && scoped_physical_value(
             "vhdl:work.second_physical_unit_scope(rtl)") == 3);

  const auto wrong_physical_position = fsim::frontend::parse_text(
      "vhdl_physical_scalar_position_wrong_type.vhd",
      R"(
entity Physical_Scalar_Position_Wrong_Type is end entity;
architecture rtl of physical_scalar_position_wrong_type is
  type First_T is range 0 to 10 units
    first_tick;
  end units First_T;
  type Second_T is range 0 to 10 units
    second_tick;
  end units Second_T;
  signal Result : integer;
begin
  Result <= First_T'pos(1 second_tick);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(wrong_physical_position.ok());
  const auto wrong_physical_position_result = compile_and_elaborate(
      wrong_physical_position.design,
      "vhdl:work.physical_scalar_position_wrong_type(rtl)");
  assert(!wrong_physical_position_result.ok()
      && has_diagnostic(
          wrong_physical_position_result,
          "FSIM-ELAB-VHSCALARATTR-001"));

  const auto shadowed_time_position = fsim::frontend::parse_text(
      "vhdl_physical_scalar_position_shadowed_time.vhd",
      R"(
entity Physical_Scalar_Position_Shadowed_Time is end entity;
architecture rtl of physical_scalar_position_shadowed_time is
  type time is range 0 to 10 units
    local_tick;
  end units time;
  signal Result : integer;
begin
  Result <= time'pos(fs);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(shadowed_time_position.ok());
  const auto shadowed_time_position_result = compile_and_elaborate(
      shadowed_time_position.design,
      "vhdl:work.physical_scalar_position_shadowed_time(rtl)");
  assert(!shadowed_time_position_result.ok()
      && has_diagnostic(
          shadowed_time_position_result,
          "FSIM-ELAB-VHSCALARATTR-001"));

  const auto wrong_physical_value = fsim::frontend::parse_text(
      "vhdl_physical_scalar_value_wrong_type.vhd",
      R"(
entity Physical_Scalar_Value_Wrong_Type is end entity;
architecture rtl of physical_scalar_value_wrong_type is
  type First_T is range 0 to 10 units
    first_tick;
  end units First_T;
  type Second_T is range 0 to 10 units
    second_tick;
  end units Second_T;
  signal Result : First_T;
begin
  Result <= First_T'val(1 second_tick);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(wrong_physical_value.ok());
  const auto wrong_physical_value_result = compile_and_elaborate(
      wrong_physical_value.design,
      "vhdl:work.physical_scalar_value_wrong_type(rtl)");
  assert(!wrong_physical_value_result.ok()
      && has_diagnostic(
          wrong_physical_value_result,
          "FSIM-ELAB-VHSCALARATTR-001"));

  const auto reject = [](
      const std::string_view filename,
      const std::string_view declarations,
      const std::string_view expression,
      const std::string_view code) {
    const auto source = std::string{
        "entity Invalid is end entity;\n"
        "architecture rtl of invalid is\n"}
        + std::string{declarations}
        + "\n  signal Result : integer;\n"
          "begin\n  Result <= "
        + std::string{expression}
        + ";\nend architecture;\n";
    const auto invalid = fsim::frontend::parse_text(
        filename, source, fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(invalid.ok());
    const auto result = compile_and_elaborate(
        invalid.design, "vhdl:work.invalid(rtl)");
    if (result.ok() || !has_diagnostic(result, code)) {
      std::cerr << filename << " expected " << code << '\n';
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(!result.ok() && has_diagnostic(result, code));
    const auto found = std::ranges::find_if(
        result.diagnostics,
        [&](const auto& diagnostic) { return diagnostic.code == code; });
    assert(
        found != result.diagnostics.end()
        && found->span.source_name == filename && !found->span.empty());
  };

  constexpr std::string_view matrix =
      "  type Matrix_T is array (0 to 1, 3 downto 1) of bit;";
  reject(
      "vhdl_attribute_dimension_outside.vhd",
      matrix,
      "Matrix_T'length(3)",
      "FSIM-ELAB-VHARRAYATTR-002");
  reject(
      "vhdl_attribute_unconstrained.vhd",
      "  type Open_T is array (natural range <>) of bit;",
      "Open_T'length",
      "FSIM-ELAB-VHARRAYATTR-001");
  reject(
      "vhdl_attribute_dynamic_dimension.vhd",
      "  type Matrix_T is array (0 to 1, 3 downto 1) of bit;\n"
      "  signal Dimension : integer;",
      "Matrix_T'length(Dimension)",
      "FSIM-ELAB-VHARRAYATTR-002");
  reject(
      "vhdl_attribute_scalar_dimension.vhd",
      "  subtype Count_T is integer range 0 to 3;",
      "Count_T'left(1)",
      "FSIM-ELAB-VHSCALARATTR-001");
  reject(
      "vhdl_attribute_scalar_successor.vhd",
      "  subtype Count_T is integer range 0 to 3;",
      "Count_T'succ(3)",
      "FSIM-ELAB-VHSCALARATTR-002");
  reject(
      "vhdl_attribute_scalar_object.vhd",
      "  subtype Count_T is integer range 0 to 3;\n"
      "  signal Count : Count_T;",
      "Count'left",
      "FSIM-ELAB-VHSCALARATTR-001");
  reject(
      "vhdl_attribute_scalar_range_expression.vhd",
      "  subtype Count_T is integer range 0 to 3;",
      "Count_T'range",
      "FSIM-ELAB-VHSCALARATTR-003");
  reject(
      "vhdl_attribute_array_position.vhd",
      matrix,
      "Matrix_T'pos(0)",
      "FSIM-ELAB-VHARRAYATTR-001");
  reject(
      "vhdl_attribute_record_prefix.vhd",
      "  type Holder_T is record Value : bit; end record;\n"
      "  signal Holder : Holder_T;",
      "Holder'length",
      "FSIM-ELAB-VHARRAYATTR-001");

  const auto runtime_invalid = fsim::frontend::parse_text(
      "vhdl_attribute_dynamic_successor.vhd",
      R"(
entity Invalid is end entity;
architecture rtl of invalid is
  subtype Count_T is integer range 0 to 2;
  signal Result : integer;
begin
  drive : process
    variable Current : Count_T := 2;
  begin
    Result <= Count_T'succ(Current);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(runtime_invalid.ok());
  const auto runtime_design = compile_and_elaborate(
      runtime_invalid.design, "vhdl:work.invalid(rtl)");
  assert(runtime_design.ok());
  bool checked = false;
  try {
    auto runtime = runtime_design.design->create_interpreter();
    static_cast<void>(runtime->run());
  } catch (const std::exception& error) {
    checked = std::string_view{error.what()}.find(
        "VHDL integer subtype range check failed")
        != std::string_view::npos;
  }
  assert(checked);

  constexpr std::array older_standards{
      fsim::frontend::VhdlStandard::Vhdl1987,
      fsim::frontend::VhdlStandard::Vhdl1993,
      fsim::frontend::VhdlStandard::Vhdl2000,
      fsim::frontend::VhdlStandard::Vhdl2002,
      fsim::frontend::VhdlStandard::Vhdl2008};
  constexpr std::string_view scalar_length_source = R"(
entity Legacy_Scalar_Attribute is end entity;
architecture rtl of Legacy_Scalar_Attribute is
  subtype Count_T is integer range 0 to 3;
  signal Result : integer;
begin
  Result <= Count_T'length;
end architecture;
)";
  for (const auto standard : older_standards) {
    const auto legacy = fsim::frontend::parse_text(
        "vhdl-legacy-scalar-length.vhd", scalar_length_source,
        fsim::frontend::Language::Vhdl2008, standard);
    assert(legacy.ok() && legacy.design.vhdl_profile_compatible);
    const auto rejected = compile_and_elaborate(
        legacy.design,
        "vhdl:work.legacy_scalar_attribute(rtl)");
    assert(
        !rejected.ok()
        && has_diagnostic(rejected, "FSIM-ELAB-VHATTR-011"));
  }

  const auto recovered = fsim::frontend::parse_text(
      "vhdl2008-recovered-object-shorthand.vhd", R"(
entity Recovered is end entity;
architecture rtl of Recovered is
  type State_T is (Idle, Busy);
  signal State : State_T;
  signal Position : integer;
begin
  Position <= State'pos;
end architecture;
)", fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2008);
  assert(!recovered.ok() && !recovered.design.vhdl_profile_compatible);
  const auto recovered_rejected = compile_and_elaborate(
      recovered.design, "vhdl:work.recovered(rtl)");
  assert(
      !recovered_rejected.ok()
      && has_diagnostic(
          recovered_rejected, "FSIM-ELAB-VHPROFILE-001"));
}

}  // namespace fsim::tests::elaboration
