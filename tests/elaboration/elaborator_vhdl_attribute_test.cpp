// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_attribute_closure() {
  const auto range_direction_retention = fsim::frontend::parse_text(
      "vhdl_integer_range_direction_retention.vhd",
      R"(
entity Range_Direction_Retention is end entity;
architecture rtl of range_direction_retention is
  type Ascending_Singleton_Vector_T is array (5 to 5) of bit;
  type Descending_Singleton_Vector_T is array (5 downto 5) of bit;
  type Ascending_Null_Vector_T is array (5 to 4) of bit;
  type Descending_Null_Vector_T is array (4 downto 5) of bit;
  constant Ascending_Source : bit_vector(5 to 5) := "0";
  constant Descending_Source : bit_vector(5 downto 5) := "1";
  subtype Ascending_Index_T is integer range Ascending_Source'range;
  subtype Descending_Index_T is integer range Descending_Source'range;
  subtype Ascending_Null_Index_T is integer
    range Ascending_Null_Vector_T'range;
  subtype Descending_Null_Index_T is integer
    range Descending_Null_Vector_T'range;
  subtype Type_Ascending_Singleton_Index_T is integer
    range Ascending_Singleton_Vector_T'range;
  subtype Type_Descending_Singleton_Index_T is integer
    range Descending_Singleton_Vector_T'range;
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
  for (const auto name : { "ascending_index_t", "descending_index_t",
           "ascending_null_index_t", "descending_null_index_t",
           "type_ascending_singleton_index_t",
           "type_descending_singleton_index_t" }) {
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

  const auto attribute_range_controls = fsim::frontend::parse_text(
      "vhdl_attribute_range_direction_controls.vhd",
      R"(
entity Attribute_Range_Direction_Controls is end entity;
architecture rtl of attribute_range_direction_controls is
  signal Ascending_Singleton_Source : bit_vector(5 to 5);
  signal Descending_Singleton_Source : bit_vector(5 downto 5);
  signal Ascending_Singleton_Target : bit_vector(5 to 5);
  signal Descending_Singleton_Target : bit_vector(5 downto 5);
  signal Ascending_Null_Source : bit_vector(5 to 4);
  signal Descending_Null_Source : bit_vector(4 downto 5);
  signal Ascending_Null_Target : bit_vector(5 to 4);
  signal Descending_Null_Target : bit_vector(4 downto 5);
begin
  Ascending_Singleton_Target <=
    Ascending_Singleton_Source(Ascending_Singleton_Source'range);
  Descending_Singleton_Target <=
    Descending_Singleton_Source(Descending_Singleton_Source'range);
  Ascending_Null_Target <=
    Ascending_Null_Source(Ascending_Null_Source'range);
  Descending_Null_Target <=
    Descending_Null_Source(Descending_Null_Source'range);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(attribute_range_controls.ok());
  const auto attribute_range_controls_design = compile_and_elaborate(
      attribute_range_controls.design,
      "vhdl:work.attribute_range_direction_controls(rtl)");
  assert(attribute_range_controls_design.ok());

  const auto predefined_object_subtype = fsim::frontend::parse_text(
      "vhdl_attribute_predefined_object_subtype.vhd",
      R"(
package custom_arrays is
  type integer_vector is array (natural range <>) of bit;
end package;

entity Predefined_Object_Subtype is end entity;
architecture rtl of predefined_object_subtype is
  procedure copy_bit_vector(value : in bit_vector) is
    variable bit_vector_result : value'subtype;
  begin
    null;
  end procedure;

  procedure copy_boolean_vector(value : in boolean_vector) is
    variable boolean_vector_result : value'subtype;
  begin
    null;
  end procedure;

  procedure copy_integer(value : in integer_vector) is
    variable integer_result : value'subtype;
  begin
    null;
  end procedure;

  procedure copy_real_vector(value : in real_vector) is
    variable real_vector_result : value'subtype;
  begin
    null;
  end procedure;

  procedure copy_time_vector(value : in time_vector) is
    variable time_vector_result : value'subtype;
  begin
    null;
  end procedure;

  procedure copy_string(value : in string) is
    variable string_result : value'subtype;
  begin
    null;
  end procedure;

  procedure copy_custom(value : in custom_arrays.integer_vector) is
    variable custom_result : value'subtype;
  begin
    null;
  end procedure;
begin
end architecture;

entity Local_Array_Shadow is end entity;
architecture rtl of local_array_shadow is
  type integer_vector is array (natural range <>) of bit;
  procedure copy_local(value : in integer_vector) is
    variable local_result : value'subtype;
  begin
    local_result := value;
  end procedure;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(predefined_object_subtype.ok());
  auto predefined_object_subtype_design = compile_test_design(
      predefined_object_subtype.design);
  const auto projected_subtype = [&](const std::string_view name) {
    const auto& declarations =
        predefined_object_subtype_design.vhdl_hir.declarations();
    const auto found = std::ranges::find_if(
        declarations, [&](const auto& declaration) {
          return declaration.name == name;
        });
    assert(found != declarations.end() && found->subtype);
    return &*found->subtype;
  };
  const auto has_projected_array_bounds = [](const auto& subtype) {
    return subtype.constraints.size() == 1U
        && subtype.constraints.front().kind
            == fsim::semantic::vhdl::RangeKind::array_index
        && subtype.constraints.front().left_expression
        && subtype.constraints.front().right_expression;
  };
  const auto assert_builtin_array_result = [&](const std::string_view name,
                                               const auto identity) {
    const auto* subtype = projected_subtype(name);
    assert(subtype->builtin_type == identity
        && !subtype->type_mark.target.valid()
        && has_projected_array_bounds(*subtype));
  };
  using BuiltinType = fsim::semantic::vhdl::BuiltinTypeIdentity;
  assert_builtin_array_result("bit_vector_result",
      BuiltinType::std_standard_bit_vector);
  assert_builtin_array_result("boolean_vector_result",
      BuiltinType::std_standard_boolean_vector);
  assert_builtin_array_result("integer_result",
      BuiltinType::std_standard_integer_vector);
  assert_builtin_array_result("real_vector_result",
      BuiltinType::std_standard_real_vector);
  assert_builtin_array_result("time_vector_result",
      BuiltinType::std_standard_time_vector);
  assert_builtin_array_result("string_result",
      BuiltinType::std_standard_string);
  const auto* custom_result = projected_subtype("custom_result");
  assert(custom_result->type_mark.target.valid()
      && custom_result->builtin_type
          == fsim::semantic::vhdl::BuiltinTypeIdentity::none
      && has_projected_array_bounds(*custom_result));
  const auto* local_result = projected_subtype("local_result");
  assert(local_result->type_mark.target.valid()
      && local_result->builtin_type
          == fsim::semantic::vhdl::BuiltinTypeIdentity::none
      && has_projected_array_bounds(*local_result));

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

  const auto range_context = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_context.vhd",
      R"(
entity Array_Range_Attribute_Context is end entity;
architecture rtl of array_range_attribute_context is
  signal Descending_Data : bit_vector(5 downto 2) := "1010";
  signal Descending_Expected : bit_vector(5 downto 2) := "1010";
  signal Ascending_Data : bit_vector(2 to 5) := "0101";
  signal Ascending_Expected : bit_vector(2 to 5) := "0101";

  procedure inspect(
    input : in bit_vector;
    reference : in bit_vector;
    matches : out boolean;
    first : out integer;
    last : out integer;
    elements : out integer) is
    variable count : integer := 0;
  begin
    matches := input(reference'range) = reference;
    first := reference'range'left;
    last := reference'range'right;
    for index in input'range loop
      count := count + 1;
    end loop;
    elements := count;
  end procedure;

  procedure clear(signal target : inout bit_vector) is
  begin
    target(target'range) <= (others => '0');
  end procedure;
begin
  drive : process
    variable matches : boolean;
    variable first : integer;
    variable last : integer;
    variable elements : integer;
  begin
    inspect(Descending_Data, Descending_Expected, matches, first, last,
      elements);
    assert matches and first = 5 and last = 2 and elements = 4
      report "descending array range context failed" severity failure;
    inspect(Ascending_Data, Ascending_Expected, matches, first, last,
      elements);
    assert matches and first = 2 and last = 5 and elements = 4
      report "ascending array range context failed" severity failure;
    clear(Descending_Data);
    clear(Ascending_Data);
    wait for 0 ns;
    assert Descending_Data = "0000" and Ascending_Data = "0000"
      report "writable range-selected array targets failed" severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(range_context.ok());
  const auto range_context_design = compile_and_elaborate(
      range_context.design,
      "vhdl:work.array_range_attribute_context(rtl)");
  if (!range_context_design.ok()) {
    for (const auto& diagnostic : range_context_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(range_context_design.ok());
  auto range_context_runtime
      = range_context_design.design->create_interpreter();
  assert(range_context_runtime->run().status
      == fsim::runtime::RunStatus::completed);

  const auto shared_index_range = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_shared_index_type.vhd",
      R"(
entity Shared_Index_Range is end entity;
architecture rtl of shared_index_range is
  type Index_T is range 0 to 3;
  type First_Array_T is array (Index_T range <>) of bit;
  type Second_Array_T is array (Index_T range <>) of bit;
  signal First_Array : First_Array_T(0 to 3) := (others => '1');
  signal Second_Array : Second_Array_T(0 to 3) := (others => '0');
  signal Result : First_Array_T(0 to 3) := (others => '0');
begin
  Result <= First_Array(Second_Array'range);
  check : process
  begin
    wait for 0 ns;
    assert Result = First_Array
      report "same-index-type range from another array type failed"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(shared_index_range.ok());
  const auto shared_index_range_design = compile_and_elaborate(
      shared_index_range.design,
      "vhdl:work.shared_index_range(rtl)");
  if (!shared_index_range_design.ok()) {
    for (const auto& diagnostic : shared_index_range_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(shared_index_range_design.ok());
  auto shared_index_range_runtime
      = shared_index_range_design.design->create_interpreter();
  assert(shared_index_range_runtime->run().status
      == fsim::runtime::RunStatus::completed);

  const auto enumeration_index_bounds = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_enumeration_bounds.vhd",
      R"(
entity Enumeration_Index_Bounds is end entity;
architecture rtl of enumeration_index_bounds is
  type Index_T is (First_Index, Middle_Index, Last_Index);
  type Enum_Array_T is array (Index_T range <>) of bit;
  signal Data : Enum_Array_T(Middle_Index to Last_Index)
    := (Middle_Index => '0', Last_Index => '1');
begin
  check : process
    variable first_index : Index_T;
    variable last_index : Index_T;
  begin
    first_index := Data'low;
    last_index := Data'high;
    assert first_index = Middle_Index and last_index = Last_Index
      report "enumeration array low/high lost their nominal type"
      severity failure;
    first_index := Data'range'left;
    last_index := Data'range'right;
    assert first_index = Middle_Index and last_index = Last_Index
      report "enumeration index range bounds lost their nominal type"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(enumeration_index_bounds.ok());
  const auto enumeration_index_bounds_design = compile_and_elaborate(
      enumeration_index_bounds.design,
      "vhdl:work.enumeration_index_bounds(rtl)");
  if (!enumeration_index_bounds_design.ok()) {
    for (const auto& diagnostic
        : enumeration_index_bounds_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(enumeration_index_bounds_design.ok());
  auto enumeration_index_bounds_runtime
      = enumeration_index_bounds_design.design->create_interpreter();
  assert(enumeration_index_bounds_runtime->run().status
      == fsim::runtime::RunStatus::completed);

  const auto shared_index_subtype_range = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_shared_index_subtype.vhd",
      R"(
entity Shared_Index_Subtype is end entity;
architecture rtl of shared_index_subtype is
  type Index_T is range 0 to 7;
  subtype Index_Alias_T is Index_T range 0 to 7;
  type Base_Array_T is array (Index_T range <>) of bit;
  type Alias_Array_T is array (Index_Alias_T range <>) of bit;
  signal Base_Data : Base_Array_T(3 downto 0)
    := (others => '1');
  signal Alias_Data : Alias_Array_T(3 downto 0)
    := (others => '0');
  signal Result : Base_Array_T(3 downto 0) := (others => '0');
begin
  Result <= Base_Data(Alias_Data'range);
  check : process
  begin
    wait for 0 ns;
    assert Result = Base_Data
      report "same-root index subtype alias range failed"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(shared_index_subtype_range.ok());
  const auto shared_index_subtype_range_design = compile_and_elaborate(
      shared_index_subtype_range.design,
      "vhdl:work.shared_index_subtype(rtl)");
  if (!shared_index_subtype_range_design.ok()) {
    for (const auto& diagnostic
        : shared_index_subtype_range_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(shared_index_subtype_range_design.ok());
  auto shared_index_subtype_range_runtime
      = shared_index_subtype_range_design.design->create_interpreter();
  assert(shared_index_subtype_range_runtime->run().status
      == fsim::runtime::RunStatus::completed);

  const auto wide_integer_array_bounds = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_wide_integer_bounds.vhd",
      R"(
entity Wide_Integer_Array_Bounds is end entity;
architecture rtl of wide_integer_array_bounds is
  type Wide_Array_T is array (integer range <>) of bit;
  signal Data : Wide_Array_T(2147483648 to 2147483649)
    := (others => '1');
begin
  check : process
  begin
    assert Data'left = 2147483648 and Data'right = 2147483649
      and Data'low = 2147483648 and Data'high = 2147483649
      report "array bounds were narrowed to a 32-bit INTEGER"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2019);
  assert(wide_integer_array_bounds.ok());
  const auto wide_integer_array_bounds_design = compile_and_elaborate(
      wide_integer_array_bounds.design,
      "vhdl:work.wide_integer_array_bounds(rtl)");
  if (!wide_integer_array_bounds_design.ok()) {
    for (const auto& diagnostic : wide_integer_array_bounds_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(wide_integer_array_bounds_design.ok());
  auto wide_integer_array_bounds_runtime
      = wide_integer_array_bounds_design.design->create_interpreter();
  assert(wide_integer_array_bounds_runtime->run().status
      == fsim::runtime::RunStatus::completed);

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
      "vhdl_attribute_array_range_scalar_context.vhd",
      "  type Bits_T is array (natural range <>) of bit;\n"
      "  signal Bits : Bits_T(3 downto 0);",
      "Bits'range",
      "FSIM-ELAB-VHARRAYATTR-003");
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

  const auto wrong_index_range = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_wrong_index_type.vhd",
      R"(
entity Wrong_Index_Range is end entity;
architecture rtl of wrong_index_range is
  type First_Index_T is range 0 to 3;
  type Second_Index_T is range 0 to 3;
  type First_Array_T is array (First_Index_T range <>) of bit;
  type Second_Array_T is array (Second_Index_T range <>) of bit;
  signal First_Array : First_Array_T(0 to 3);
  signal Second_Array : Second_Array_T(0 to 3);
  signal Result : First_Array_T(0 to 3);
begin
  Result <= First_Array(Second_Array'range);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(wrong_index_range.ok());
  const auto wrong_index_range_result = compile_and_elaborate(
      wrong_index_range.design, "vhdl:work.wrong_index_range(rtl)");
  assert(!wrong_index_range_result.ok()
      && has_diagnostic(
          wrong_index_range_result, "FSIM-ELAB-VHARRAYSEL-006"));

  const auto wrong_direction_range = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_wrong_direction.vhd",
      R"(
entity Wrong_Direction_Range is end entity;
architecture rtl of wrong_direction_range is
  signal Forward : bit_vector(0 to 3);
  signal Reverse : bit_vector(3 downto 0);
  signal Result : bit_vector(0 to 3);
begin
  Result <= Forward(Reverse'range);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(wrong_direction_range.ok());
  const auto wrong_direction_range_result = compile_and_elaborate(
      wrong_direction_range.design,
      "vhdl:work.wrong_direction_range(rtl)");
  assert(!wrong_direction_range_result.ok()
      && has_diagnostic(
          wrong_direction_range_result, "FSIM-ELAB-VHARRAYSEL-004"));

  const auto singleton_wrong_direction = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_singleton_wrong_direction.vhd",
      R"(
entity Singleton_Wrong_Direction is end entity;
architecture rtl of singleton_wrong_direction is
  signal Forward : bit_vector(0 to 3);
  signal Singleton_Reverse : bit_vector(3 downto 3);
  signal Result : bit_vector(0 to 3);
begin
  Result <= Forward(Singleton_Reverse'range);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(singleton_wrong_direction.ok());
  const auto singleton_wrong_direction_result = compile_and_elaborate(
      singleton_wrong_direction.design,
      "vhdl:work.singleton_wrong_direction(rtl)");
  assert(!singleton_wrong_direction_result.ok()
      && has_diagnostic(
          singleton_wrong_direction_result,
          "FSIM-ELAB-VHARRAYSEL-004"));

  const auto null_out_of_bounds_range = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_null_out_of_bounds.vhd",
      R"(
entity Null_Out_Of_Bounds_Range is end entity;
architecture rtl of null_out_of_bounds_range is
  signal Forward : bit_vector(0 to 3);
  signal Empty : bit_vector(4 to 3);
  signal Result : bit_vector(0 to 3);
begin
  Result <= Forward(Empty'range);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(null_out_of_bounds_range.ok());
  const auto null_out_of_bounds_range_result = compile_and_elaborate(
      null_out_of_bounds_range.design,
      "vhdl:work.null_out_of_bounds_range(rtl)");
  assert(!has_diagnostic(
      null_out_of_bounds_range_result,
      "FSIM-ELAB-VHARRAYSEL-004"));

  const auto null_wrong_direction = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_null_wrong_direction.vhd",
      R"(
entity Null_Wrong_Direction is end entity;
architecture rtl of null_wrong_direction is
  signal Forward : bit_vector(0 to 3);
  signal Empty_Reverse : bit_vector(3 downto 4);
  signal Result : bit_vector(0 to 3);
begin
  Result <= Forward(Empty_Reverse'range);
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(null_wrong_direction.ok());
  const auto null_wrong_direction_result = compile_and_elaborate(
      null_wrong_direction.design,
      "vhdl:work.null_wrong_direction(rtl)");
  assert(!null_wrong_direction_result.ok()
      && has_diagnostic(
          null_wrong_direction_result,
          "FSIM-ELAB-VHARRAYSEL-004"));

  const auto wide_vhdl_2019_range_attribute = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_wide_2019.vhd",
      R"(
entity Wide_Range_Attribute_2019 is end entity;
architecture rtl of wide_range_attribute_2019 is
  type Wide_Index_T is range 3000000000 to 3000000001;
  type Wide_Bits_T is array (Wide_Index_T range <>) of bit;
  signal Data : Wide_Bits_T(3000000000 to 3000000001)
    := (others => '1');
begin
  check : process
    variable first : integer;
  begin
    first := Data'range'left;
    assert first = 3000000000
      report "VHDL-2019 wide range attribute was narrowed"
      severity failure;
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008,
      fsim::frontend::VhdlStandard::Vhdl2019);
  assert(wide_vhdl_2019_range_attribute.ok());
  const auto wide_vhdl_2019_range_attribute_design = compile_and_elaborate(
      wide_vhdl_2019_range_attribute.design,
      "vhdl:work.wide_range_attribute_2019(rtl)");
  if (!wide_vhdl_2019_range_attribute_design.ok()) {
    for (const auto& diagnostic
         : wide_vhdl_2019_range_attribute_design.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(wide_vhdl_2019_range_attribute_design.ok());
  auto wide_vhdl_2019_range_attribute_runtime
      = wide_vhdl_2019_range_attribute_design.design->create_interpreter();
  assert(wide_vhdl_2019_range_attribute_runtime->run().status
      == fsim::runtime::RunStatus::completed);

  const auto nested_range_selection = fsim::frontend::parse_text(
      "vhdl_array_range_attribute_nested_selection.vhd",
      R"(
entity Nested_Range_Selection is end entity;
architecture rtl of nested_range_selection is
  type Matrix_T is array (0 to 1, 0 to 1) of bit;
  signal Matrix : Matrix_T;
begin
  drive : process
    variable Selected : bit;
  begin
    Selected := Matrix(Matrix'range);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(nested_range_selection.ok());
  const auto nested_range_selection_result = compile_and_elaborate(
      nested_range_selection.design,
      "vhdl:work.nested_range_selection(rtl)");
  assert(!nested_range_selection_result.ok()
      && has_diagnostic(
          nested_range_selection_result, "FSIM-ELAB-VHARRAYSEL-004"));

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
