// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <string_view>

namespace fsim::tests::elaboration {

void test_vhdl_pure_packed_function_fold()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-pure-packed-function-fold.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package packed_fold_pkg is
  subtype nibble_t is std_logic_vector(3 downto 0);
  function encode_inner(value : integer) return std_logic_vector;
  function encode(value : integer) return std_logic_vector;
  function counted_bits(count : integer; width : integer)
    return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package body packed_fold_pkg is
  function count_unsigned(count : integer; width : integer)
    return unsigned is
    variable bits : unsigned(width - 1 downto 0) :=
      to_unsigned(1, width);
  begin
    for i in 0 to count - 1 loop
      bits := bits sll 1;
    end loop;
    return bits;
  end function;
  function counted_bits(count : integer; width : integer)
    return std_logic_vector is
  begin
    return std_logic_vector(count_unsigned(count, width));
  end function;
  function encode_inner(value : integer) return std_logic_vector is
  begin
    if value = 9 then
      return "1011";
    end if;
    return "0100";
  end function;
  function encode(value : integer) return std_logic_vector is
  begin
    return encode_inner(value);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity packed_function_fold is
  generic (WIDTH : integer := 4);
end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of packed_function_fold is
  constant GEN : work.packed_fold_pkg.nibble_t :=
    work.packed_fold_pkg.encode(9);
  constant COUNTED : std_logic_vector(WIDTH - 1 downto 0) :=
    work.packed_fold_pkg.counted_bits(3, WIDTH);
  signal upper_bits : std_logic_vector(1 downto 0);
  signal lower_bits : std_logic_vector(1 downto 0);
  signal dynamic_source : integer := 8;
  signal dynamic_bits : work.packed_fold_pkg.nibble_t;
  signal counted_output : std_logic_vector(WIDTH - 1 downto 0);
begin
  upper_bits <= GEN(3 downto 2);
  lower_bits <= GEN(1 downto 0);
  counted_output <= COUNTED;

  update_source : process
  begin
    wait for 1 ns;
    dynamic_source <= 9;
    wait;
  end process;

  observe_source : process (dynamic_source)
  begin
    dynamic_bits <= work.packed_fold_pkg.encode(dynamic_source);
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "vhdl:work.packed_function_fold(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    bool loaded_upper { };
    bool loaded_lower { };
    for (const auto& process : elaborated.design->processes()) {
        for (const auto& operation : process.operations) {
            const auto* values = fsim::runtime::simir::operation_group_if<
                fsim::runtime::simir::ValueOperationGroup>(&operation);
            const auto* constant = values == nullptr
                ? nullptr
                : std::get_if<fsim::runtime::simir::LoadConstant>(
                      &values->storage);
            if (constant == nullptr || constant->value.width() != 2U
                || !constant->value.is_logic9()) {
                continue;
            }
            loaded_upper |= constant->value.to_msb_string() == "10";
            loaded_lower |= constant->value.to_msb_string() == "11";
        }
    }
    assert(loaded_upper && loaded_lower);

    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    const auto upper = elaborated.design->find_signal("upper_bits");
    const auto lower = elaborated.design->find_signal("lower_bits");
    const auto dynamic = elaborated.design->find_signal("dynamic_bits");
    const auto counted = elaborated.design->find_signal("counted_output");
    assert(upper && lower && dynamic && counted);
    const auto dynamic_type = std::ranges::find(
        elaborated.design->signals(), *dynamic,
        &fsim::elaboration::SignalInfo::id);
    assert(dynamic_type != elaborated.design->signals().end());
    assert(dynamic_type->width == 4U);
    assert(dynamic_type->source_domain
        == fsim::frontend::ValueDomain::Logic9);
    assert(dynamic_type->packed_range);
    assert(dynamic_type->packed_range->left == 3);
    assert(dynamic_type->packed_range->right == 0);
    assert(dynamic_type->packed_range->descending);
    assert(interpreter->signal_value(*upper).to_msb_string() == "10");
    assert(interpreter->signal_value(*lower).to_msb_string() == "11");
    assert(interpreter->signal_value(*dynamic).to_msb_string() == "1011");
    assert(interpreter->signal_value(*counted).to_msb_string() == "1000");
}

void test_vhdl_local_integer_array_callable_generic_fold()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-local-integer-array-callable-fold.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package local_integer_array_pkg is
  subtype local_integer_t is integer;
  function integer_array_tail(n : integer) return std_logic_vector;
  function alias_array_tail(n : integer) return std_logic_vector;
  function constrained_array_tail(n : integer) return std_logic_vector;
  function unknown_array_tail(n : integer) return std_logic_vector;
  function integer_array_pattern(entry_count : integer)
    return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package body local_integer_array_pkg is
  function integer_array_tail(n : integer) return std_logic_vector is
    type int_arr is array (natural range <>) of integer;
    variable values : int_arr(0 to n - 1) := (others => 0);
    variable xu : unsigned(n downto 0) := to_unsigned(8, n + 1);
  begin
    for i in 0 to n - 1 loop
      values(i) := to_integer(xu(n - 1 downto 0));
    end loop;
    return std_logic_vector(to_unsigned(values(n - 1), 8));
  end function;

  function alias_array_tail(n : integer) return std_logic_vector is
    type alias_arr is array (natural range <>) of local_integer_t;
    variable values : alias_arr(0 to n - 1) := (others => 0);
  begin
    for i in 0 to n - 1 loop
      values(i) := i + 1;
    end loop;
    return std_logic_vector(to_unsigned(values(n - 1), 8));
  end function;

  function constrained_array_tail(n : integer) return std_logic_vector is
    type constrained_arr is array (natural range <>)
      of integer range 0 to 15;
    variable values : constrained_arr(0 to n - 1) := (others => 0);
  begin
    for i in 0 to n - 1 loop
      values(i) := i + 1;
    end loop;
    return std_logic_vector(to_unsigned(values(n - 1), 8));
  end function;

  function unknown_array_tail(n : integer) return std_logic_vector is
    type int_arr is array (natural range <>) of integer;
    variable values : int_arr(0 to n - 1) := (others => 0);
    variable xu : unsigned(n downto 0) := unsigned("XXXXXXXXX");
  begin
    for i in 0 to n - 1 loop
      values(i) := to_integer(xu(n - 1 downto 0));
    end loop;
    return std_logic_vector(to_unsigned(values(n - 1), 8));
  end function;

  function integer_array_pattern(entry_count : integer)
    return std_logic_vector is
    type int_arr is array (natural range <>) of integer;
    variable values : int_arr(0 to entry_count - 1) := (others => 0);
    variable result_value : std_logic_vector(entry_count * 8 - 1 downto 0)
      := (others => '0');
  begin
    for ii in 0 to entry_count - 1 loop
      values(ii) := (ii * 3 + 5) mod 256;
      result_value((ii + 1) * 8 - 1 downto ii * 8) :=
        std_logic_vector(to_unsigned(values(ii), 8));
    end loop;
    return result_value;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package local_integer_user_overload_pkg is
  function to_integer(value : unsigned) return integer;
  function tail_with_user_overload(n : integer)
    return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package body local_integer_user_overload_pkg is
  function to_integer(value : unsigned) return integer is
  begin
    return 8 / 0;
  end function;

  function tail_with_user_overload(n : integer)
    return std_logic_vector is
    variable value : unsigned(n - 1 downto 0) := to_unsigned(8, n);
  begin
    return std_logic_vector(to_unsigned(to_integer(value), 8));
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package local_integer_user_type_shadow_pkg is
  function tail_with_user_type_shadow(n : integer)
    return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package body local_integer_user_type_shadow_pkg is
  function tail_with_user_type_shadow(n : integer)
    return std_logic_vector is
    type unsigned is array (natural range <>) of std_logic;
    variable value : unsigned(n - 1 downto 0) := (others => '0');
  begin
    return std_logic_vector(to_unsigned(to_integer(value), 8));
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_array_leaf is
  generic (init : std_logic_vector(7 downto 0));
end entity;
architecture rtl of local_integer_array_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_pattern_leaf is
  generic (init : std_logic_vector(23 downto 0));
end entity;
architecture rtl of local_integer_pattern_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_pattern8_leaf is
  generic (init : std_logic_vector(2047 downto 0));
end entity;
architecture rtl of local_integer_pattern8_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_pattern8_unconstrained_leaf is
  generic (init : std_logic_vector := (0 downto 0 => '0'));
end entity;
architecture rtl of local_integer_pattern8_unconstrained_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_pattern8_mismatch_leaf is
  generic (init : std_logic_vector(2046 downto 0));
end entity;
architecture rtl of local_integer_pattern8_mismatch_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_array_fold is
end entity;
architecture rtl of local_integer_array_fold is
  constant init : std_logic_vector(7 downto 0) := integer_array_tail(8);
begin
  child: entity work.local_integer_array_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_alias_array_rejected is
end entity;
architecture rtl of local_integer_alias_array_rejected is
  constant init : std_logic_vector(7 downto 0) := alias_array_tail(8);
begin
  child: entity work.local_integer_array_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_constrained_array_rejected is
end entity;
architecture rtl of local_integer_constrained_array_rejected is
  constant init : std_logic_vector(7 downto 0)
    := constrained_array_tail(8);
begin
  child: entity work.local_integer_array_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_unknown_slice_rejected is
end entity;
architecture rtl of local_integer_unknown_slice_rejected is
  constant init : std_logic_vector(7 downto 0) := unknown_array_tail(8);
begin
  child: entity work.local_integer_array_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_user_overload_rejected is end entity;
architecture rtl of local_integer_user_overload_rejected is
  constant init : std_logic_vector(7 downto 0)
    := work.local_integer_user_overload_pkg.tail_with_user_overload(8);
begin
  child: entity work.local_integer_array_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity local_integer_user_type_shadow_rejected is end entity;
architecture rtl of local_integer_user_type_shadow_rejected is
  constant init : std_logic_vector(7 downto 0)
    := work.local_integer_user_type_shadow_pkg.tail_with_user_type_shadow(8);
begin
  child: entity work.local_integer_array_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_pattern_fold is end entity;
architecture rtl of local_integer_pattern_fold is
  constant init : std_logic_vector(23 downto 0) := integer_array_pattern(3);
begin
  child: entity work.local_integer_pattern_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_pattern8_fold is end entity;
architecture rtl of local_integer_pattern8_fold is
  constant init : std_logic_vector(2047 downto 0)
    := integer_array_pattern(256);
begin
  child: entity work.local_integer_pattern8_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_pattern8_unconstrained_fold is end entity;
architecture rtl of local_integer_pattern8_unconstrained_fold is
  constant init : std_logic_vector(2047 downto 0)
    := integer_array_pattern(256);
begin
  child: entity work.local_integer_pattern8_unconstrained_leaf(rtl)
    generic map (init => init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.local_integer_array_pkg.all;
entity local_integer_pattern8_constrained_mismatch is end entity;
architecture rtl of local_integer_pattern8_constrained_mismatch is
  constant init : std_logic_vector(2047 downto 0)
    := integer_array_pattern(256);
begin
  child: entity work.local_integer_pattern8_mismatch_leaf(rtl)
    generic map (init => init);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());

    auto compiled = compile_test_design(parsed.design);
    assert(compiled.valid());
    const auto direct_array = std::ranges::find_if(
        compiled.vhdl_hir.types(), [](const auto& type) {
            return type.name == "int_arr";
        });
    const auto alias_array = std::ranges::find_if(
        compiled.vhdl_hir.types(), [](const auto& type) {
            return type.name == "alias_arr";
        });
    const auto constrained_array = std::ranges::find_if(
        compiled.vhdl_hir.types(), [](const auto& type) {
            return type.name == "constrained_arr";
        });
    assert(direct_array != compiled.vhdl_hir.types().end());
    assert(alias_array != compiled.vhdl_hir.types().end());
    assert(constrained_array != compiled.vhdl_hir.types().end());
    assert(direct_array->element_subtype
        && direct_array->element_subtype->type_mark.spelling == "integer"
        && !direct_array->element_subtype->type_mark.target.valid()
        && direct_array->element_subtype->domain
            == fsim::semantic::vhdl::ValueDomain::integer
        && direct_array->element_subtype->signed_value
        && direct_array->element_subtype->integer_storage_width == 32U
        && direct_array->element_subtype->constraints.size() == 1U
        && direct_array->element_subtype->constraints.front().kind
            == fsim::semantic::vhdl::RangeKind::integer
        && direct_array->element_subtype->constraints.front().left
            == std::numeric_limits<std::int32_t>::min()
        && direct_array->element_subtype->constraints.front().right
            == std::numeric_limits<std::int32_t>::max());
    assert(alias_array->element_subtype
        && alias_array->element_subtype->type_mark.target.valid());
    const auto alias_element_type = compiled.find_type(
        alias_array->element_subtype->type_mark.target);
    assert(alias_element_type && alias_element_type->vhdl != nullptr
        && alias_element_type->vhdl->name == "local_integer_t");
    assert(constrained_array->element_subtype
        && constrained_array->element_subtype->type_mark.spelling
            == "integer"
        && !constrained_array->element_subtype->type_mark.target.valid()
        && constrained_array->element_subtype->constraints.size() == 1U
        && constrained_array->element_subtype->constraints.front().kind
            == fsim::semantic::vhdl::RangeKind::integer
        && constrained_array->element_subtype->constraints.front().left == 0
        && constrained_array->element_subtype->constraints.front().right
            == 15);

    std::vector<fsim::semantic::CompiledDesign> inputs;
    inputs.push_back(std::move(compiled));
    const auto linked = fsim::semantic::link_compiled_designs(
        std::move(inputs));
    assert(linked.ok());

    const auto folded = fsim::elaboration::elaborate(
        *linked.design, "vhdl:work.local_integer_array_fold(rtl)");
    if (!folded.ok()) {
        for (const auto& diagnostic : folded.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(folded.ok());
    const auto child = std::ranges::find_if(
        folded.design->specializations(), [](const auto& specialization) {
            return specialization.instance == "local_integer_array_fold.child";
        });
    assert(child != folded.design->specializations().end());
    const auto packed_init = std::ranges::find_if(
        child->parameter_identity_values, [](const auto& parameter) {
            return parameter.first == "init";
        });
    assert(packed_init != child->parameter_identity_values.end());
    assert(packed_init->second.find(";value=00001000")
        != std::string::npos);

    const auto pattern_fold = fsim::elaboration::elaborate(
        *linked.design, "vhdl:work.local_integer_pattern_fold(rtl)");
    assert(pattern_fold.ok());
    const auto pattern_child = std::ranges::find_if(
        pattern_fold.design->specializations(), [](const auto& specialization) {
            return specialization.instance
                == "local_integer_pattern_fold.child";
        });
    assert(pattern_child != pattern_fold.design->specializations().end());
    const auto pattern_init = std::ranges::find_if(
        pattern_child->parameter_identity_values, [](const auto& parameter) {
            return parameter.first == "init";
        });
    assert(pattern_init != pattern_child->parameter_identity_values.end());
    assert(pattern_init->second.find(";value=000010110000100000000101")
        != std::string::npos);

    const auto pattern8_fold = fsim::elaboration::elaborate(
        *linked.design, "vhdl:work.local_integer_pattern8_fold(rtl)");
    if (!pattern8_fold.ok()) {
        for (const auto& diagnostic : pattern8_fold.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(pattern8_fold.ok());
    const auto pattern8_child = std::ranges::find_if(
        pattern8_fold.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "local_integer_pattern8_fold.child";
        });
    assert(pattern8_child
        != pattern8_fold.design->specializations().end());
    const auto pattern8_init = std::ranges::find_if(
        pattern8_child->parameter_identity_values,
        [](const auto& parameter) {
            return parameter.first == "init";
        });
    assert(pattern8_init
        != pattern8_child->parameter_identity_values.end());
    constexpr std::string_view packed_value_marker { ";value=" };
    const auto packed_value_marker_offset
        = pattern8_init->second.find(packed_value_marker);
    assert(packed_value_marker_offset != std::string::npos);
    const auto packed_value_begin
        = packed_value_marker_offset + packed_value_marker.size();
    const auto packed_value_end = pattern8_init->second.find(
        ';', packed_value_begin);
    const auto packed_value = packed_value_end == std::string::npos
        ? pattern8_init->second.substr(packed_value_begin)
        : pattern8_init->second.substr(
              packed_value_begin,
              packed_value_end - packed_value_begin);
    assert(packed_value.size() == 2048U);
    constexpr std::array<std::size_t, 8U> table_indexes {
        1U, 2U, 3U, 7U, 127U, 128U, 254U, 255U
    };
    constexpr std::array<std::string_view, 8U> expected_table_bytes {
        "00001000", "00001011", "00001110", "00011010",
        "10000010", "10000101", "11111111", "00000010"
    };
    for (std::size_t sample { };
        sample < table_indexes.size(); ++sample) {
        const auto offset = 2048U - (table_indexes[sample] + 1U) * 8U;
        assert(packed_value.substr(offset, 8U)
            == expected_table_bytes[sample]);
    }

    const auto unconstrained_pattern8_fold = fsim::elaboration::elaborate(
        *linked.design,
        "vhdl:work.local_integer_pattern8_unconstrained_fold(rtl)");
    assert(unconstrained_pattern8_fold.ok());
    const auto unconstrained_pattern8_child = std::ranges::find_if(
        unconstrained_pattern8_fold.design->specializations(),
        [](const auto& specialization) {
            return specialization.instance
                == "local_integer_pattern8_unconstrained_fold.child";
        });
    assert(unconstrained_pattern8_child
        != unconstrained_pattern8_fold.design->specializations().end());
    const auto unconstrained_pattern8_init = std::ranges::find_if(
        unconstrained_pattern8_child->parameter_identity_values,
        [](const auto& parameter) {
            return parameter.first == "init";
        });
    assert(unconstrained_pattern8_init
        != unconstrained_pattern8_child->parameter_identity_values.end());
    const auto unconstrained_value_marker_offset
        = unconstrained_pattern8_init->second.find(packed_value_marker);
    assert(unconstrained_value_marker_offset != std::string::npos);
    const auto unconstrained_value_begin
        = unconstrained_value_marker_offset + packed_value_marker.size();
    const auto unconstrained_value_end =
        unconstrained_pattern8_init->second.find(
            ';', unconstrained_value_begin);
    const auto unconstrained_packed_value
        = unconstrained_value_end == std::string::npos
        ? unconstrained_pattern8_init->second.substr(
              unconstrained_value_begin)
        : unconstrained_pattern8_init->second.substr(
              unconstrained_value_begin,
              unconstrained_value_end - unconstrained_value_begin);
    assert(unconstrained_packed_value.size() == 2048U);
    for (std::size_t sample { };
        sample < table_indexes.size(); ++sample) {
        const auto offset = 2048U - (table_indexes[sample] + 1U) * 8U;
        assert(unconstrained_packed_value.substr(offset, 8U)
            == expected_table_bytes[sample]);
    }

    const auto rejected = fsim::elaboration::elaborate(
        *linked.design,
        "vhdl:work.local_integer_alias_array_rejected(rtl)");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-GENERIC-004"));

    const auto rejected_constrained = fsim::elaboration::elaborate(
        *linked.design,
        "vhdl:work.local_integer_constrained_array_rejected(rtl)");
    assert(!rejected_constrained.ok());
    assert(has_diagnostic(
        rejected_constrained, "FSIM-ELAB-GENERIC-004"));

    const auto rejected_unknown = fsim::elaboration::elaborate(
        *linked.design,
        "vhdl:work.local_integer_unknown_slice_rejected(rtl)");
    assert(!rejected_unknown.ok());
    assert(has_diagnostic(rejected_unknown, "FSIM-ELAB-GENERIC-004"));

    const auto find_package_call = [&](const std::string_view package_name)
        -> const fsim::semantic::vhdl::Expression& {
        const auto expression = std::ranges::find_if(
            linked.design->vhdl_hir.expressions(), [&](const auto& candidate) {
                if (candidate.kind
                        != fsim::semantic::vhdl::ExpressionKind::call
                    || !candidate.referenced_name
                    || candidate.referenced_name->spelling != "to_integer") {
                    return false;
                }
                const auto scope = std::ranges::find(
                    linked.design->semantics.scopes(), candidate.scope,
                    &fsim::semantic::Scope::id);
                const auto owner = scope
                        != linked.design->semantics.scopes().end()
                    ? linked.design->find_unit(scope->unit)
                    : std::nullopt;
                return owner && owner->vhdl != nullptr
                    && owner->vhdl->kind
                        == fsim::semantic::vhdl::UnitKind::package
                    && owner->vhdl->name == package_name;
            });
        assert(expression != linked.design->vhdl_hir.expressions().end());
        return *expression;
    };
    const auto& user_overload_call = find_package_call(
        "local_integer_user_overload_pkg");
    assert(user_overload_call.referenced_name);
    const auto user_overload_scope = std::ranges::find(
        linked.design->semantics.scopes(), user_overload_call.scope,
        &fsim::semantic::Scope::id);
    assert(user_overload_scope != linked.design->semantics.scopes().end());
    const fsim::semantic::CompiledDesignResolver user_overload_resolver {
        *linked.design, user_overload_scope->unit
    };
    const auto user_overload_candidates
        = user_overload_resolver.resolve_vhdl_callables(
            *user_overload_call.referenced_name, user_overload_call.scope);
    assert(user_overload_candidates.status
        == fsim::semantic::CompiledResolutionStatus::unique);
    assert(user_overload_candidates.candidates.size() == 1U);
    const auto user_overload = linked.design->find_declaration(
        user_overload_candidates.candidates.front().body);
    assert(user_overload && user_overload->vhdl != nullptr
        && user_overload->vhdl->callable
        && user_overload->vhdl->callable->pure);
    const auto user_overload_owner_scope = std::ranges::find(
        linked.design->semantics.scopes(), user_overload->vhdl->scope,
        &fsim::semantic::Scope::id);
    assert(user_overload_owner_scope
        != linked.design->semantics.scopes().end());
    const auto user_overload_owner
        = linked.design->find_unit(user_overload_owner_scope->unit);
    assert(user_overload_owner && user_overload_owner->identity != nullptr
        && user_overload_owner->identity->kind
            == fsim::semantic::UnitKind::vhdl_package
        && (user_overload_owner->identity->library.empty()
            || user_overload_owner->identity->library == "work")
        && user_overload_owner->vhdl != nullptr
        && user_overload_owner->vhdl->name
            == "local_integer_user_overload_pkg");
    assert(user_overload_resolver.vhdl_builtin_package_member_imported(
        "ieee", "numeric_std", "to_integer", user_overload_call.scope));

    const auto& user_type_shadow_call = find_package_call(
        "local_integer_user_type_shadow_pkg");
    const auto user_type_shadow_scope = std::ranges::find(
        linked.design->semantics.scopes(), user_type_shadow_call.scope,
        &fsim::semantic::Scope::id);
    assert(user_type_shadow_scope != linked.design->semantics.scopes().end());
    const fsim::semantic::CompiledDesignResolver user_type_shadow_resolver {
        *linked.design, user_type_shadow_scope->unit
    };
    const auto user_unsigned = user_type_shadow_resolver.resolve_vhdl_named_type(
        "unsigned", user_type_shadow_call.scope);
    assert(user_unsigned);
    const auto user_unsigned_type = linked.design->find_type(*user_unsigned);
    assert(user_unsigned_type && user_unsigned_type->vhdl != nullptr
        && user_unsigned_type->vhdl->name == "unsigned"
        && user_unsigned_type->vhdl->form
            == fsim::semantic::vhdl::TypeForm::array);
    fsim::semantic::vhdl::Name user_type_name;
    user_type_name.spelling = "unsigned";
    user_type_name.canonical = "unsigned";
    const auto visible_user_types = user_type_shadow_resolver.resolve_vhdl(
        user_type_name, user_type_shadow_call.scope,
        [](const auto& declaration) {
            if (declaration.vhdl == nullptr) {
                return false;
            }
            const auto form = declaration.vhdl->form;
            return form == fsim::semantic::vhdl::DeclarationForm::type
                || form == fsim::semantic::vhdl::DeclarationForm::subtype
                || form
                    == fsim::semantic::vhdl::DeclarationForm::generic_type
                || form == fsim::semantic::vhdl::DeclarationForm::alias;
        });
    assert(visible_user_types.status
        == fsim::semantic::CompiledResolutionStatus::unique);
    assert(visible_user_types.candidates.size() == 1U);
    assert(visible_user_types.candidates.front()
        == user_unsigned_type->vhdl->declaration);
    assert(user_type_shadow_resolver.vhdl_builtin_package_member_imported(
        "ieee", "numeric_std", "unsigned", user_type_shadow_call.scope));

    const auto rejected_user_overload = fsim::elaboration::elaborate(
        *linked.design,
        "vhdl:work.local_integer_user_overload_rejected(rtl)");
    assert(!rejected_user_overload.ok());
    assert(has_diagnostic(
        rejected_user_overload, "FSIM-ELAB-GENERIC-004"));

    const auto rejected_user_type_shadow = fsim::elaboration::elaborate(
        *linked.design,
        "vhdl:work.local_integer_user_type_shadow_rejected(rtl)");
    assert(!rejected_user_type_shadow.ok());
    assert(has_diagnostic(
        rejected_user_type_shadow, "FSIM-ELAB-GENERIC-004"));

    const auto rejected_constrained_width_mismatch
        = fsim::elaboration::elaborate(
            *linked.design,
        "vhdl:work.local_integer_pattern8_constrained_mismatch(rtl)");
    assert(!rejected_constrained_width_mismatch.ok());
    assert(has_diagnostic(
        rejected_constrained_width_mismatch, "FSIM-ELAB-GENERIC-004"));
}

void test_vhdl_constant_initializer_preserves_mismatched_packed_bounds()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-constant-initializer-mismatched-packed-bounds.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package mismatched_fold_pkg is
  function encode(value : integer) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
package body mismatched_fold_pkg is
  function encode(value : integer) return std_logic_vector is
  begin
    return "10110";
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity mismatched_constant_fold is end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of mismatched_constant_fold is
  constant GEN : std_logic_vector(5 downto 1) :=
    work.mismatched_fold_pkg.encode(9);
  signal result_bits : std_logic_vector(5 downto 1);
begin
  result_bits <= GEN;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "vhdl:work.mismatched_constant_fold(rtl)");
    assert(elaborated.ok());

    // The function result uses bounds 4 downto 0 while GEN uses 5 downto 1.
    // Check the observable array value without depending on whether the
    // elaborator realizes this static initializer through folding or calls.
    const auto result = elaborated.design->find_signal("result_bits");
    assert(result);
    const auto result_type = std::ranges::find(
        elaborated.design->signals(), *result,
        &fsim::elaboration::SignalInfo::id);
    assert(result_type != elaborated.design->signals().end());
    assert(result_type->width == 5U);
    assert(result_type->packed_range);
    assert(result_type->packed_range->left == 5);
    assert(result_type->packed_range->right == 1);
    assert(result_type->packed_range->descending);

    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*result).to_msb_string() == "10110");
}

void test_vhdl_packed_array_signal_initializer()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-packed-array-signal-initializer.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package rom_initializer_pkg is
  type rom_t is array (3 to 4) of std_logic_vector(7 downto 0);
  function init_rom return rom_t;
end package;

library ieee;
use ieee.std_logic_1164.all;
package body rom_initializer_pkg is
  function init_rom return rom_t is
    variable result_rom : rom_t := (others => (others => '0'));
  begin
    result_rom(3) := "10100101";
    result_rom(4) := "00111100";
    return result_rom;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity packed_array_initializer is end entity;

library ieee;
use ieee.std_logic_1164.all;
use work.rom_initializer_pkg.all;
architecture rtl of packed_array_initializer is
  constant saved_rom : rom_t := init_rom;
  signal rom : rom_t := init_rom;
  signal rom_from_constant : rom_t := saved_rom;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "vhdl:work.packed_array_initializer(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    for (const auto signal_name : { "rom", "rom_from_constant" }) {
        const auto signal = elaborated.design->find_signal(signal_name);
        assert(signal);
        const auto signal_info = std::ranges::find(
            elaborated.design->signals(), *signal,
            &fsim::elaboration::SignalInfo::id);
        assert(signal_info != elaborated.design->signals().end());
        assert(signal_info->width == 16U);
        assert(signal_info->vhdl_array);
        assert(signal_info->vhdl_array->dimensions.size() == 1U);
        assert(signal_info->vhdl_array->dimensions.front().range);
        assert(signal_info->vhdl_array->dimensions.front().range->left == 3);
        assert(signal_info->vhdl_array->dimensions.front().range->right == 4);
    }
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    const auto rom = elaborated.design->find_signal("rom");
    const auto rom_from_constant
        = elaborated.design->find_signal("rom_from_constant");
    assert(rom && rom_from_constant);
    constexpr auto expected = "1010010100111100";
    assert(interpreter->signal_value(*rom).to_msb_string() == expected);
    assert(interpreter->signal_value(*rom_from_constant).to_msb_string()
        == expected);

    const auto distinct_nominal = fsim::frontend::parse_text(
        "vhdl-packed-array-signal-initializer-nominal-negative.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package distinct_rom_pkg is
  type rom_t is array (3 to 4) of std_logic_vector(7 downto 0);
  type other_rom_t is array (3 to 4) of std_logic_vector(7 downto 0);
  function init_rom return rom_t;
end package;
library ieee;
use ieee.std_logic_1164.all;
package body distinct_rom_pkg is
  function init_rom return rom_t is
    variable result_rom : rom_t := (others => (others => '0'));
  begin
    result_rom(3) := "10100101";
    result_rom(4) := "00111100";
    return result_rom;
  end function;
end package body;
entity distinct_rom_initializer is end entity;
use work.distinct_rom_pkg.all;
architecture rtl of distinct_rom_initializer is
  signal rom : other_rom_t := init_rom;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(distinct_nominal.ok());
    const auto rejected_nominal = compile_and_elaborate(
        distinct_nominal.design,
        "vhdl:work.distinct_rom_initializer(rtl)");
    assert(!rejected_nominal.ok());
    assert(has_diagnostic(rejected_nominal, "FSIM-ELAB-HIR-001"));

    const auto constrained_mismatch = fsim::frontend::parse_text(
        "vhdl-packed-array-signal-initializer-bounds-negative.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package constrained_rom_pkg is
  type rom_t is array (integer range <>) of std_logic_vector(7 downto 0);
  subtype good_rom_t is rom_t(3 to 4);
  subtype bad_rom_t is rom_t(3 to 5);
  function init_rom return good_rom_t;
end package;
library ieee;
use ieee.std_logic_1164.all;
package body constrained_rom_pkg is
  function init_rom return good_rom_t is
    variable result_rom : good_rom_t := (others => (others => '0'));
  begin
    result_rom(3) := "10100101";
    result_rom(4) := "00111100";
    return result_rom;
  end function;
end package body;
entity constrained_rom_initializer is end entity;
use work.constrained_rom_pkg.all;
architecture rtl of constrained_rom_initializer is
  signal rom : bad_rom_t := init_rom;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(constrained_mismatch.ok());
    const auto rejected_bounds = compile_and_elaborate(
        constrained_mismatch.design,
        "vhdl:work.constrained_rom_initializer(rtl)");
    assert(!rejected_bounds.ok());
    assert(has_diagnostic(rejected_bounds, "FSIM-ELAB-HIR-001"));

    const auto wrong_element_width = fsim::frontend::parse_text(
        "vhdl-packed-array-signal-initializer-width-negative.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package wrong_width_rom_pkg is
  type rom_t is array (3 to 4) of std_logic_vector(7 downto 0);
  function init_rom return rom_t;
end package;
library ieee;
use ieee.std_logic_1164.all;
package body wrong_width_rom_pkg is
  function init_rom return rom_t is
    variable result_rom : rom_t := (others => (others => '0'));
  begin
    result_rom := (others => "101001010");
    return result_rom;
  end function;
end package body;
entity wrong_width_rom_initializer is end entity;
use work.wrong_width_rom_pkg.all;
architecture rtl of wrong_width_rom_initializer is
  signal rom : rom_t := init_rom;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(wrong_element_width.ok());
    const auto rejected_element_width = compile_and_elaborate(
        wrong_element_width.design,
        "vhdl:work.wrong_width_rom_initializer(rtl)");
    assert(!rejected_element_width.ok());
    assert(has_diagnostic(
        rejected_element_width, "FSIM-ELAB-HIR-001"));
}

void test_vhdl_packed_generic_value_reaches_nested_callable()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-packed-generic-value-binding.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
entity packed_generic_rom is
  generic (
    width : positive := 4;
    depth : positive := 2;
    init : std_logic_vector := "00000000"
  );
end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of packed_generic_rom is
  type rom_t is array (0 to depth - 1) of
    std_logic_vector(width - 1 downto 0);
  function init_rom return rom_t is
    variable result_rom : rom_t := (others => (others => '0'));
  begin
    for i in 0 to depth - 1 loop
      result_rom(i) := init((i + 1) * width - 1 downto i * width);
    end loop;
    return result_rom;
  end function;
  signal rom : rom_t := init_rom;
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity packed_generic_binding_parent is end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of packed_generic_binding_parent is
  constant first_init : std_logic_vector(7 downto 0) := "10100101";
  constant second_init : std_logic_vector(7 downto 0) := "00111100";
begin
  u_first : entity work.packed_generic_rom(rtl)
    generic map (width => 4, depth => 2, init => first_init);
  u_second : entity work.packed_generic_rom(rtl)
    generic map (width => 4, depth => 2, init => second_init);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity packed_generic_binding_bad_parent is end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of packed_generic_binding_bad_parent is
  constant short_init : std_logic_vector(6 downto 0) := "0000000";
begin
  u_bad : entity work.packed_generic_rom(rtl)
    generic map (width => 4, depth => 2, init => short_init);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());

    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.packed_generic_binding_parent(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    const auto first = elaborated.design->find_signal(
        "packed_generic_binding_parent.u_first.rom");
    const auto second = elaborated.design->find_signal(
        "packed_generic_binding_parent.u_second.rom");
    assert(first && second && *first != *second);
    assert(interpreter->signal_value(*first).to_msb_string() == "01011010");
    assert(interpreter->signal_value(*second).to_msb_string() == "11000011");

    const auto rejected = compile_and_elaborate(
        parsed.design,
        "vhdl:work.packed_generic_binding_bad_parent(rtl)");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-HIR-001"));
}

void test_vhdl_packed_bitwise_evaluator_preserves_operator_range()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-packed-bitwise-operator-range.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package packed_bitwise_pkg is
  type unsigned_values_t is array (natural range <>) of unsigned(5 to 8);
  function logic_and(left_value, right_value : std_logic_vector)
    return std_logic_vector;
  function logic_or(left_value, right_value : std_logic_vector)
    return std_logic_vector;
  function logic_nand(left_value, right_value : std_logic_vector)
    return std_logic_vector;
  function logic_nor(left_value, right_value : std_logic_vector)
    return std_logic_vector;
  function logic_xor(left_value, right_value : std_logic_vector)
    return std_logic_vector;
  function logic_xnor(left_value, right_value : std_logic_vector)
    return std_logic_vector;
  function rebound_unsigned(value : unsigned) return unsigned;
  function numeric_and_at(values : unsigned_values_t;
                          index_value : integer;
                          right_value : unsigned)
    return unsigned;
  function numeric_or_at(values : unsigned_values_t;
                         index_value : integer;
                         right_value : unsigned)
    return unsigned;
  function numeric_nand_at(values : unsigned_values_t;
                           index_value : integer;
                           right_value : unsigned)
    return unsigned;
  function numeric_nor_at(values : unsigned_values_t;
                          index_value : integer;
                          right_value : unsigned)
    return unsigned;
  function numeric_xor_at(values : unsigned_values_t;
                          index_value : integer;
                          right_value : unsigned)
    return unsigned;
  function numeric_xnor_at(values : unsigned_values_t;
                           index_value : integer;
                           right_value : unsigned)
    return unsigned;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package body packed_bitwise_pkg is
  function logic_and(left_value, right_value : std_logic_vector)
    return std_logic_vector is
  begin
    return left_value and right_value;
  end function;
  function logic_or(left_value, right_value : std_logic_vector)
    return std_logic_vector is
  begin
    return left_value or right_value;
  end function;
  function logic_nand(left_value, right_value : std_logic_vector)
    return std_logic_vector is
  begin
    return left_value nand right_value;
  end function;
  function logic_nor(left_value, right_value : std_logic_vector)
    return std_logic_vector is
  begin
    return left_value nor right_value;
  end function;
  function logic_xor(left_value, right_value : std_logic_vector)
    return std_logic_vector is
  begin
    return left_value xor right_value;
  end function;
  function logic_xnor(left_value, right_value : std_logic_vector)
    return std_logic_vector is
  begin
    return left_value xnor right_value;
  end function;
  function rebound_unsigned(value : unsigned) return unsigned is
    variable result_value : unsigned(40 downto 37);
  begin
    result_value := value;
    return result_value;
  end function;
  function numeric_and_at(values : unsigned_values_t;
                          index_value : integer;
                          right_value : unsigned)
    return unsigned is
  begin
    return values(index_value) and rebound_unsigned(right_value);
  end function;
  function numeric_or_at(values : unsigned_values_t;
                         index_value : integer;
                         right_value : unsigned)
    return unsigned is
  begin
    return values(index_value) or rebound_unsigned(right_value);
  end function;
  function numeric_nand_at(values : unsigned_values_t;
                           index_value : integer;
                           right_value : unsigned)
    return unsigned is
  begin
    return values(index_value) nand rebound_unsigned(right_value);
  end function;
  function numeric_nor_at(values : unsigned_values_t;
                          index_value : integer;
                          right_value : unsigned)
    return unsigned is
  begin
    return values(index_value) nor rebound_unsigned(right_value);
  end function;
  function numeric_xor_at(values : unsigned_values_t;
                          index_value : integer;
                          right_value : unsigned)
    return unsigned is
  begin
    return values(index_value) xor rebound_unsigned(right_value);
  end function;
  function numeric_xnor_at(values : unsigned_values_t;
                           index_value : integer;
                           right_value : unsigned)
    return unsigned is
  begin
    return values(index_value) xnor rebound_unsigned(right_value);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.packed_bitwise_pkg.all;
entity packed_bitwise_operator_parent is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.packed_bitwise_pkg.all;
architecture rtl of packed_bitwise_operator_parent is
  constant logic_a : std_logic_vector(5 to 8) := "1010";
  constant logic_b : std_logic_vector(40 downto 37) := "0011";
  constant unsigned_values : unsigned_values_t(0 to 0)
    := (0 => "1010");
  constant unsigned_b : unsigned(40 downto 37) := "0011";
  constant numeric_plain_a : unsigned(5 to 8) := "1010";
  constant numeric_plain_b : unsigned(40 downto 37) := "0011";
  constant numeric_plain_and_result : unsigned
    := numeric_plain_a and numeric_plain_b;
  constant numeric_plain_or_result : unsigned
    := numeric_plain_a or numeric_plain_b;
  constant numeric_plain_nand_result : unsigned
    := numeric_plain_a nand numeric_plain_b;
  constant numeric_plain_nor_result : unsigned
    := numeric_plain_a nor numeric_plain_b;
  constant numeric_plain_xor_result : unsigned
    := numeric_plain_a xor numeric_plain_b;
  constant numeric_plain_xnor_result : unsigned
    := numeric_plain_a xnor numeric_plain_b;
  constant logic_and_result : std_logic_vector
    := logic_and(logic_a, logic_b);
  constant logic_or_result : std_logic_vector
    := logic_or(logic_a, logic_b);
  constant logic_nand_result : std_logic_vector
    := logic_nand(logic_a, logic_b);
  constant logic_nor_result : std_logic_vector
    := logic_nor(logic_a, logic_b);
  constant logic_xor_result : std_logic_vector
    := logic_xor(logic_a, logic_b);
  constant logic_xnor_result : std_logic_vector
    := logic_xnor(logic_a, logic_b);
  constant numeric_and_result : unsigned
    := numeric_and_at(unsigned_values, 0, unsigned_b);
  constant numeric_or_result : unsigned
    := numeric_or_at(unsigned_values, 0, unsigned_b);
  constant numeric_nand_result : unsigned
    := numeric_nand_at(unsigned_values, 0, unsigned_b);
  constant numeric_nor_result : unsigned
    := numeric_nor_at(unsigned_values, 0, unsigned_b);
  constant numeric_xor_result : unsigned
    := numeric_xor_at(unsigned_values, 0, unsigned_b);
  constant numeric_xnor_result : unsigned
    := numeric_xnor_at(unsigned_values, 0, unsigned_b);
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    std::vector<fsim::semantic::CompiledDesign> inputs;
    inputs.push_back(compile_test_design(parsed.design));
    auto linked = fsim::semantic::link_compiled_designs(std::move(inputs));
    assert(linked.ok());
    auto& design = *linked.design;
    const auto architecture = design.find_unit(
        fsim::semantic::UnitKind::vhdl_architecture,
        "work", "packed_bitwise_operator_parent", "rtl");
    assert(architecture && architecture->vhdl != nullptr);

    constexpr std::array<std::string_view, 6U> operators {
        "and", "or", "nand", "nor", "xor", "xnor"
    };
    for (const auto operation : operators) {
        const auto expression = std::ranges::find_if(
            design.vhdl_hir.expressions(), [&](const auto& candidate) {
                if (candidate.kind
                        != fsim::semantic::vhdl::ExpressionKind::binary
                    || candidate.text != operation
                    || candidate.operands.size() != 2U) {
                    return false;
                }
                const auto left = design.find_expression(
                    candidate.operands.front());
                const auto right = design.find_expression(
                    candidate.operands.back());
                return left && left->vhdl != nullptr
                    && left->vhdl->kind
                        == fsim::semantic::vhdl::ExpressionKind::index
                    && right && right->vhdl != nullptr
                    && right->vhdl->kind
                        == fsim::semantic::vhdl::ExpressionKind::call;
            });
        assert(expression != design.vhdl_hir.expressions().end());
        assert(expression->builtin_operator
            == fsim::semantic::vhdl::BuiltinOperatorIdentity::none);
        assert(!expression->referenced_name
            || (!expression->referenced_name->selected
                && expression->referenced_name->overloads.empty()));
    }
    const auto package = design.find_unit(
        fsim::semantic::UnitKind::vhdl_package,
        "work", "packed_bitwise_pkg");
    assert(package && package->identity != nullptr);
    const auto package_imports
        = design.vhdl_linked_imports(package->identity->id);
    assert(package_imports
        && std::ranges::any_of(*package_imports,
            [](const fsim::semantic::CompiledVhdlImport& imported) {
                return imported.library == "ieee"
                    && imported.package == "numeric_std"
                    && imported.member == "all";
            }));

    const auto initializer = [&](const std::string_view name) {
        const auto declaration = std::ranges::find_if(
            design.vhdl_hir.declarations(), [&](const auto& candidate) {
                return candidate.name == name;
            });
        assert(declaration != design.vhdl_hir.declarations().end());
        assert(declaration->initializer);
        return *declaration->initializer;
    };
    const std::array<fsim::semantic::SpecializedHirActualIdentity, 0>
        actuals { };
    const auto specialized = fsim::semantic::make_specialized_hir_unit(
        design, architecture->identity->id, actuals);
    assert(specialized);

    constexpr std::array<std::string_view, 6U> expected_bits {
        "0010", "1011", "1101", "0100", "1001", "0110"
    };
    const auto assert_result = [&](const std::string_view name,
                                   const std::string_view bits,
                                   const std::int64_t left,
                                   const std::int64_t right) {
        const auto value = specialized->evaluate_vhdl_constant_expression(
            initializer(name));
        const auto packed = value
            ? std::get_if<fsim::semantic::SpecializedHirVhdlPackedValue>(
                  &*value)
            : nullptr;
        if (!packed || packed->bits != bits
            || packed->left_bound != left || packed->right_bound != right) {
            std::cerr << "packed bitwise constant '" << name
                      << "' expected bits=" << bits << " range=" << left
                      << " to " << right;
            if (!value) {
                std::cerr << " actual=<unresolved>\n";
            } else if (packed) {
                std::cerr << " actual bits=" << packed->bits << " range="
                          << packed->left_bound << " to "
                          << packed->right_bound << '\n';
            } else {
                std::cerr << " actual kind=" << value->index();
                if (const auto integer = std::get_if<std::int64_t>(&*value)) {
                    std::cerr << " integer=" << *integer;
                }
                std::cerr << '\n';
            }
        }
        assert(packed && packed->bits == bits);
        assert(packed->left_bound == left && packed->right_bound == right);
    };
    constexpr std::array<std::string_view, 6U> names {
        "and", "or", "nand", "nor", "xor", "xnor"
    };
    for (std::size_t index { }; index < operators.size(); ++index) {
        assert_result(std::string("logic_") + std::string(names[index])
                + "_result",
            expected_bits[index], 1, 4);
        assert_result(std::string("numeric_plain_")
                + std::string(names[index])
                + "_result",
            expected_bits[index], 3, 0);
    }

}

void test_vhdl_pure_call_updates_local_packed_array_in_place()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-local-packed-array-constant.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package local_packed_array_pkg is
  function build_pattern(count : positive) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package body local_packed_array_pkg is
  function build_pattern(count : positive) return std_logic_vector is
    type words_t is array (natural range <>) of unsigned(3 downto 0);
    variable words : words_t(0 to count - 1);
    variable result : std_logic_vector(count * 4 - 1 downto 0)
      := (others => '0');
  begin
    for i in 0 to count - 1 loop
      words(i) := to_unsigned(i mod 16, 4);
    end loop;
    for i in 0 to count - 1 loop
      result((i + 1) * 4 - 1 downto i * 4)
        := std_logic_vector(words(i));
    end loop;
    return result;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.local_packed_array_pkg.all;
entity local_packed_array_parent is end entity;
architecture rtl of local_packed_array_parent is
  constant pattern : std_logic_vector(2047 downto 0)
    := build_pattern(512);
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    std::vector<fsim::semantic::CompiledDesign> inputs;
    inputs.push_back(compile_test_design(parsed.design));
    const auto linked = fsim::semantic::link_compiled_designs(
        std::move(inputs));
    assert(linked.ok());
    const auto architecture = linked.design->find_unit(
        fsim::semantic::UnitKind::vhdl_architecture,
        "work", "local_packed_array_parent", "rtl");
    assert(architecture && architecture->identity != nullptr);
    const auto pattern = std::ranges::find_if(
        linked.design->vhdl_hir.declarations(), [](const auto& declaration) {
            return declaration.name == "pattern";
        });
    assert(pattern != linked.design->vhdl_hir.declarations().end());
    assert(pattern->initializer);
    const std::array<fsim::semantic::SpecializedHirActualIdentity, 0>
        actuals { };
    const auto specialized = fsim::semantic::make_specialized_hir_unit(
        *linked.design, architecture->identity->id, actuals);
    assert(specialized);
    const auto value = specialized->evaluate_vhdl_constant_expression(
        *pattern->initializer);
    const auto packed = value
        ? std::get_if<fsim::semantic::SpecializedHirVhdlPackedValue>(
              &*value)
        : nullptr;
    assert(packed && packed->left_bound == 2047
        && packed->right_bound == 0 && packed->bits.size() == 2048U);
    for (std::size_t index { }; index < 512U; ++index) {
        const auto nibble = std::bitset<4> { index % 16U }.to_string();
        assert(packed->bits.substr((511U - index) * 4U, 4U) == nibble);
    }
}

void test_vhdl_pure_call_respects_package_generic_binding()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-pure-call-package-generic-binding.vhd",
        R"(
package generic_math is
  generic (bias : integer := 1);
  constant offset : integer := bias;
  function apply(value : integer) return integer;
end package;

package body generic_math is
  function apply(value : integer) return integer is
  begin
    return value + offset;
  end function;
end package body;

entity package_generic_call_fold is
  port (result_value : out integer);
end entity;

architecture rtl of package_generic_call_fold is
  package selected_math is new work.generic_math
    generic map (bias => 3);

  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
begin
  worker : process
    variable local_value : integer;
  begin
    local_value := increment(selected_math.apply(2));
    result_value <= local_value;
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "vhdl:work.package_generic_call_fold(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto result = elaborated.design->find_signal("result_value");
    assert(result);
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    const auto value = interpreter->signal_value(*result).low_word();
    assert(value.aval == 6 && value.bval == 0);
}

void test_vhdl_callable_formal_preserves_actual_packed_range()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-callable-formal-preserves-actual-packed-range.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package formal_range_pkg is
  function get_window(
    value : std_logic_vector;
    index_value : integer;
    width_value : integer
  ) return std_logic_vector;
  function get_low(value : std_logic_vector) return integer;
  function get_chunk(
    value : std_logic_vector;
    index_value : integer;
    width_value : integer
  ) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
package body formal_range_pkg is
  function get_window(
    value : std_logic_vector;
    index_value : integer;
    width_value : integer
  ) return std_logic_vector is
    variable result_value : std_logic_vector(width_value - 1 downto 0);
  begin
    for j in 0 to width_value - 1 loop
      result_value(j) := value(
        value'low + index_value * width_value + j);
    end loop;
    return result_value;
  end function;
  function get_low(value : std_logic_vector) return integer is
  begin
    return value'low;
  end function;
  function get_chunk(
    value : std_logic_vector;
    index_value : integer;
    width_value : integer
  ) return std_logic_vector is
  begin
    return value((index_value + 1) * width_value - 1
      downto index_value * width_value);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity callable_formal_packed_range is end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of callable_formal_packed_range is
  signal source_a : std_logic_vector(5 to 12) := "10110011";
  signal source_b : std_logic_vector(20 to 27) := "01101010";
  signal source_c : std_logic_vector(63 downto 0) := x"1234567890ABCDEF";
  signal source_d : std_logic_vector(31 downto 0) := x"89ABCDEF";
  signal index_value : integer := 1;
  signal window_a : std_logic_vector(3 downto 0);
  signal window_b : std_logic_vector(3 downto 0);
  signal chunk_value_a : std_logic_vector(7 downto 0);
  signal chunk_value_b : std_logic_vector(7 downto 0);
  signal low_a : integer;
  signal low_b : integer;
  signal low_not : integer;
  signal low_and : integer;
begin
  worker : process (source_a, source_b, source_c, source_d, index_value)
  begin
    window_a <= work.formal_range_pkg.get_window(
      source_a, index_value, 4);
    window_b <= work.formal_range_pkg.get_window(
      source_b, index_value, 4);
    low_a <= work.formal_range_pkg.get_low(source_a);
    low_b <= work.formal_range_pkg.get_low(source_b);
    low_not <= work.formal_range_pkg.get_low(not source_a);
    low_and <= work.formal_range_pkg.get_low(
      source_a and not source_b);
    chunk_value_a <= work.formal_range_pkg.get_chunk(source_c, 2, 8);
    chunk_value_b <= work.formal_range_pkg.get_chunk(source_d, 1, 8);
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    std::vector<fsim::semantic::CompiledDesign> inputs;
    inputs.push_back(compile_test_design(parsed.design));
    const auto linked = fsim::semantic::link_compiled_designs(
        std::move(inputs));
    assert(linked.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        *linked.design, "vhdl:work.callable_formal_packed_range(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto signal_value = [&](const std::string_view name) {
        const auto signal = elaborated.design->find_signal(name);
        assert(signal);
        return *signal;
    };
    const auto window_a = signal_value("window_a");
    const auto window_b = signal_value("window_b");
    const auto low_a = signal_value("low_a");
    const auto low_b = signal_value("low_b");
    const auto low_not = signal_value("low_not");
    const auto low_and = signal_value("low_and");
    const auto chunk_a = signal_value("chunk_value_a");
    const auto chunk_b = signal_value("chunk_value_b");
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(window_a).to_msb_string() == "1100");
    assert(interpreter->signal_value(window_b).to_msb_string() == "0101");
    assert(interpreter->signal_value(low_a).low_word().aval == 5);
    assert(interpreter->signal_value(low_b).low_word().aval == 20);
    assert(interpreter->signal_value(low_not).low_word().aval == 1);
    assert(interpreter->signal_value(low_and).low_word().aval == 1);
    assert(interpreter->signal_value(chunk_a).to_msb_string() == "10101011");
    assert(interpreter->signal_value(chunk_b).to_msb_string() == "11001101");
}

void test_vhdl_generated_iterator_is_integer_actual()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-generated-iterator-integer-actual.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package iterator_slice_pkg is
  function get_slice(
    value : std_logic_vector;
    index_value : integer
  ) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
package body iterator_slice_pkg is
  function get_slice(
    value : std_logic_vector;
    index_value : integer
  ) return std_logic_vector is
  begin
    return value(
      value'low + index_value * 4 + 3 downto
      value'low + index_value * 4);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity generated_iterator_integer_actual is end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of generated_iterator_integer_actual is
  signal source_value : std_logic_vector(15 downto 0) := x"1234";
  signal result_value : std_logic_vector(3 downto 0);
begin
  slice_gen : for gi in 0 to 0 generate
    result_value <= work.iterator_slice_pkg.get_slice(source_value, gi);
  end generate;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.generated_iterator_integer_actual(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());
    const auto result = elaborated.design->find_signal("result_value");
    assert(result);
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*result).to_msb_string() == "0100");
}

void test_vhdl_noninteger_named_constant_is_not_generate_iterator()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-noninteger-named-constant-not-generate-iterator.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
package iterator_negative_pkg is
  type lane_t is (lane_zero, lane_one);
  function get_slice(
    value : std_logic_vector;
    index_value : integer
  ) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;
package body iterator_negative_pkg is
  function get_slice(
    value : std_logic_vector;
    index_value : integer
  ) return std_logic_vector is
  begin
    return value(3 downto 0);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
entity noninteger_named_constant is end entity;

library ieee;
use ieee.std_logic_1164.all;
architecture rtl of noninteger_named_constant is
  signal source_value : std_logic_vector(7 downto 0) := x"A5";
  constant gi : work.iterator_negative_pkg.lane_t :=
    work.iterator_negative_pkg.lane_zero;
  signal result_value : std_logic_vector(3 downto 0);
begin
  result_value <= work.iterator_negative_pkg.get_slice(source_value, gi);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design, "vhdl:work.noninteger_named_constant(rtl)");
    assert(!elaborated.ok());
    assert(std::ranges::any_of(
        elaborated.diagnostics,
        [](const fsim::elaboration::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHOVER-002";
        }));
}

void test_vhdl_parsed_operator_builtin_provenance()
{
    const auto compile_linked = [](const std::string_view source_name,
                                   const std::string_view source_text) {
        auto parsed = fsim::frontend::parse_text(
            source_name, source_text, fsim::frontend::Language::Vhdl2008);
        assert(parsed.ok());
        std::vector<fsim::semantic::CompiledDesign> inputs;
        inputs.push_back(compile_test_design(std::move(parsed.design)));
        auto linked = fsim::semantic::link_compiled_designs(
            std::move(inputs));
        assert(linked.ok());
        return std::move(*linked.design);
    };
    const auto operator_expression = [](
        const fsim::semantic::CompiledDesign& design,
        const fsim::semantic::vhdl::ExpressionKind kind,
        const std::string_view spelling)
        -> const fsim::semantic::vhdl::Expression& {
        const auto found = std::ranges::find_if(
            design.vhdl_hir.expressions(), [&](const auto& expression) {
                return expression.kind == kind
                    && expression.text == spelling;
            });
        assert(found != design.vhdl_hir.expressions().end());
        return *found;
    };
    const auto architecture_unit = [](
        const fsim::semantic::CompiledDesign& design)
        -> const fsim::semantic::vhdl::Unit& {
        const auto found = std::ranges::find_if(
            design.vhdl_hir.units(), [](const auto& unit) {
                return unit.kind
                    == fsim::semantic::vhdl::UnitKind::architecture;
            });
        assert(found != design.vhdl_hir.units().end());
        return *found;
    };
    constexpr std::string_view positive_source = R"(
library ieee;
use ieee.std_logic_1164.all;
entity parsed_logic_operator_identity is end entity;
library ieee;
use ieee.std_logic_1164.all;
architecture rtl of parsed_logic_operator_identity is
  signal source_a : std_logic_vector(5 to 12);
  signal source_b : std_logic_vector(20 to 27);
  signal result_value : std_logic_vector(7 downto 0);
begin
  worker : process (source_a, source_b)
  begin
    result_value <= source_a and not source_b;
  end process;
end architecture;
)";
    auto positive = compile_linked(
        "parsed-logic-operator-identity.vhd", positive_source);
    const auto& positive_architecture = architecture_unit(positive);
    const auto positive_imports = positive.vhdl_linked_imports(
        positive_architecture.id);
    assert(positive_imports
        && std::ranges::any_of(*positive_imports,
            [](const fsim::semantic::CompiledVhdlImport& imported) {
                return imported.library == "ieee"
                    && imported.package == "std_logic_1164"
                    && imported.member == "all";
            }));
    assert(operator_expression(positive,
               fsim::semantic::vhdl::ExpressionKind::unary, "not")
               .builtin_operator
        == fsim::semantic::vhdl::BuiltinOperatorIdentity::
            ieee_std_logic_1164_not);
    assert(operator_expression(positive,
               fsim::semantic::vhdl::ExpressionKind::binary, "and")
               .builtin_operator
        == fsim::semantic::vhdl::BuiltinOperatorIdentity::
            ieee_std_logic_1164_and);

    constexpr std::string_view overload_source = R"(
library ieee;
use ieee.std_logic_1164.all;
package user_logic_ops is
  function "and" (left_value, right_value : std_logic_vector)
    return std_logic_vector;
end package;
library ieee;
use ieee.std_logic_1164.all;
entity parsed_logic_operator_overload is end entity;
library ieee;
use ieee.std_logic_1164.all;
use work.user_logic_ops.all;
architecture rtl of parsed_logic_operator_overload is
  signal source_a : std_logic_vector(5 to 12);
  signal source_b : std_logic_vector(20 to 27);
  signal result_value : std_logic_vector(7 downto 0);
begin
  worker : process (source_a, source_b)
  begin
    result_value <= source_a and not source_b;
  end process;
end architecture;
)";
    auto overloaded = compile_linked(
        "parsed-logic-operator-overload.vhd", overload_source);
    const auto& overloaded_architecture = architecture_unit(overloaded);
    const auto overloaded_imports = overloaded.vhdl_linked_imports(
        overloaded_architecture.id);
    assert(overloaded_imports
        && std::ranges::any_of(*overloaded_imports,
            [](const fsim::semantic::CompiledVhdlImport& imported) {
                return imported.library == "work"
                    && imported.package == "user_logic_ops"
                    && imported.member == "all";
            }));
    const auto& overloaded_and = operator_expression(overloaded,
        fsim::semantic::vhdl::ExpressionKind::binary, "and");
    assert(overloaded_and.builtin_operator
        == fsim::semantic::vhdl::BuiltinOperatorIdentity::none);
    fsim::semantic::vhdl::Name overload_name;
    overload_name.spelling = "and";
    overload_name.canonical = "and";
    const fsim::semantic::CompiledDesignResolver resolver {
        overloaded, overloaded_architecture.id
    };
    const auto overload_candidates = resolver.resolve_vhdl(
        overload_name, overloaded_and.scope);
    assert(overload_candidates.status
        != fsim::semantic::CompiledResolutionStatus::not_found);
    assert(!overload_candidates.candidates.empty());
}

void test_vhdl_unconstrained_unsigned_conversion_preserves_operand_width()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-unconstrained-unsigned-conversion-source-width.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity unsigned_conversion_source_width is end entity;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of unsigned_conversion_source_width is
  signal parity_even : std_logic_vector(3 downto 0) := "1001";
  signal result_value : std_logic := '0';
begin
  worker : process (parity_even)
  begin
    if unsigned(parity_even) > 8 then
      result_value <= '1';
    else
      result_value <= '0';
    end if;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.unsigned_conversion_source_width(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto result = elaborated.design->find_signal("result_value");
    assert(result);
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*result).to_msb_string() == "1");
}

void test_vhdl_static_width_formal_specializes_callable_frame()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-static-width-formal-specialization.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity static_width_formal_specialization is end entity;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of static_width_formal_specialization is
  signal source_value : integer := 22;
  signal narrow_result : integer;
  signal wide_result : integer;

  function width_probe(width_value : natural; source_value : integer)
    return integer is
    variable prim_low : std_logic_vector(width_value downto 0) :=
      std_logic_vector(to_unsigned(
        source_value mod (2 ** width_value), width_value + 1));
  begin
    return to_integer(unsigned(prim_low(width_value - 1 downto 0)));
  end function;
begin
  worker : process (source_value)
  begin
    narrow_result <= width_probe(4, source_value);
    wide_result <= width_probe(8, source_value);
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.static_width_formal_specialization(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto narrow = elaborated.design->find_signal("narrow_result");
    const auto wide = elaborated.design->find_signal("wide_result");
    assert(narrow && wide);
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    const auto narrow_value = interpreter->signal_value(*narrow).low_word();
    const auto wide_value = interpreter->signal_value(*wide).low_word();
    assert(narrow_value.aval == 6 && narrow_value.bval == 0);
    assert(wide_value.aval == 22 && wide_value.bval == 0);
}

void test_vhdl_dynamic_width_formal_remains_unresolved()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-dynamic-width-formal-remains-unresolved.vhd",
        R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dynamic_width_formal_remains_unresolved is end entity;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of dynamic_width_formal_remains_unresolved is
  signal width_value : integer := 4;
  signal result_value : integer;

  function width_probe(width_value : natural; source_value : integer)
    return integer is
    variable prim_low : std_logic_vector(width_value downto 0) :=
      std_logic_vector(to_unsigned(
        source_value mod (2 ** width_value), width_value + 1));
  begin
    return to_integer(unsigned(prim_low(width_value - 1 downto 0)));
  end function;
begin
  worker : process (width_value)
  begin
    result_value <= width_probe(width_value, 22);
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.dynamic_width_formal_remains_unresolved(rtl)");
    assert(!elaborated.ok());
    assert(std::ranges::any_of(
        elaborated.diagnostics,
        [](const fsim::elaboration::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHNUM-002";
        }));
}

void test_vhdl_dynamic_exponent_formal_remains_unresolved()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-dynamic-exponent-formal-remains-unresolved.vhd",
        R"(
entity dynamic_exponent_formal_remains_unresolved is end entity;

architecture rtl of dynamic_exponent_formal_remains_unresolved is
  signal exponent_value : natural := 4;
  signal result_value : integer;

  function bounded_power(base_value : integer; power_value : natural)
    return integer is
  begin
    return base_value ** power_value;
  end function;
begin
  worker : process (exponent_value)
  begin
    result_value <= bounded_power(2, exponent_value);
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.dynamic_exponent_formal_remains_unresolved(rtl)");
    assert(!elaborated.ok());
    assert(std::ranges::any_of(
        elaborated.diagnostics,
        [](const fsim::elaboration::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-091";
        }));
}

void test_vhdl_dynamic_integer_for_loop_bounds()
{
    const auto parsed = fsim::frontend::parse_text(
        "vhdl-dynamic-integer-for-loop-bounds.vhd",
        R"(
entity dynamic_integer_for_loop_bounds is end entity;
architecture rtl of dynamic_integer_for_loop_bounds is
  signal captured_input : integer := 3;
  signal empty_input : integer := -1;
  signal descending_input : integer := 3;
  signal control_input : integer := 6;
  signal captured_result : integer;
  signal empty_result : integer;
  signal descending_result : integer;
  signal control_result : integer;

  function captured_sum(bound_value : integer) return integer is
    variable upper_bound : integer := bound_value;
    variable total : integer := 0;
  begin
    for j in 0 to upper_bound loop
      total := total + j;
      upper_bound := upper_bound - 1;
    end loop;
    return total;
  end function;

  function descending_sum(bound_value : integer) return integer is
    variable total : integer := 0;
  begin
    for j in bound_value downto 1 loop
      total := total + j;
    end loop;
    return total;
  end function;

  function controlled_sum(bound_value : integer) return integer is
    variable total : integer := 0;
  begin
    for j in 1 to bound_value loop
      if j = 2 then
        next;
      end if;
      if j = 5 then
        exit;
      end if;
      total := total + j;
    end loop;
    return total;
  end function;
begin
  worker : process (
    captured_input, empty_input, descending_input, control_input)
  begin
    captured_result <= captured_sum(captured_input);
    empty_result <= captured_sum(empty_input);
    descending_result <= descending_sum(descending_input);
    control_result <= controlled_sum(control_input);
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.dynamic_integer_for_loop_bounds(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto captured
        = elaborated.design->find_signal("captured_result");
    const auto empty = elaborated.design->find_signal("empty_result");
    const auto descending
        = elaborated.design->find_signal("descending_result");
    const auto controlled
        = elaborated.design->find_signal("control_result");
    assert(captured && empty && descending && controlled);
    auto interpreter = elaborated.design->create_interpreter();
    assert(interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    assert(interpreter->signal_value(*captured).low_word().aval == 6U);
    assert(interpreter->signal_value(*empty).low_word().aval == 0U);
    assert(interpreter->signal_value(*descending).low_word().aval == 6U);
    assert(interpreter->signal_value(*controlled).low_word().aval == 8U);
}

void test_vhdl_runtime_for_loop_iteration_cap()
{
    const auto make_design = [](const std::string_view source_name,
                                 const std::string_view bound_value) {
        return fsim::frontend::parse_text(
            source_name,
            "entity runtime_for_loop_iteration_cap is end entity;\n"
            "architecture rtl of runtime_for_loop_iteration_cap is\n"
            "  signal bound_value : integer := "
                + std::string { bound_value } + ";\n"
            "  signal result_value : integer;\n"
            "  function bounded_empty_loop(limit_value : integer) "
            "return integer is\n"
            "  begin\n"
            "    for j in 0 to limit_value loop\n"
            "      null;\n"
            "    end loop;\n"
            "    return limit_value;\n"
            "  end function;\n"
            "begin\n"
            "  worker : process (bound_value)\n"
            "  begin\n"
            "    result_value <= bounded_empty_loop(bound_value);\n"
            "  end process;\n"
            "end architecture;\n",
            fsim::frontend::Language::Vhdl2008);
    };

    const auto exact_limit = make_design(
        "vhdl-runtime-for-loop-exact-limit.vhd", "999999");
    assert(exact_limit.ok());
    const auto exact_elaborated = compile_and_elaborate(
        exact_limit.design,
        "vhdl:work.runtime_for_loop_iteration_cap(rtl)");
    if (!exact_elaborated.ok()) {
        for (const auto& diagnostic : exact_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(exact_elaborated.ok());
    const auto exact_result
        = exact_elaborated.design->find_signal("result_value");
    assert(exact_result);
    auto exact_interpreter
        = exact_elaborated.design->create_interpreter();
    assert(exact_interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    assert(exact_interpreter->signal_value(*exact_result)
               .low_word()
               .aval
        == 999999U);

    const auto over_limit = make_design(
        "vhdl-runtime-for-loop-over-limit.vhd", "1000000");
    assert(over_limit.ok());
    const auto over_elaborated = compile_and_elaborate(
        over_limit.design,
        "vhdl:work.runtime_for_loop_iteration_cap(rtl)");
    assert(over_elaborated.ok());
    auto over_interpreter = over_elaborated.design->create_interpreter();
    bool saw_iteration_limit = false;
    try {
        (void)over_interpreter->run();
    } catch (const fsim::runtime::simir::AssertionError& error) {
        saw_iteration_limit = std::string_view { error.what() }.find(
                                  "one-million-iteration limit")
            != std::string_view::npos;
    }
    assert(saw_iteration_limit);
}

void test_vhdl_interface_function_generics()
{
    test_vhdl_pure_packed_function_fold();
    test_vhdl_local_integer_array_callable_generic_fold();
    test_vhdl_constant_initializer_preserves_mismatched_packed_bounds();
    test_vhdl_packed_array_signal_initializer();
    test_vhdl_packed_generic_value_reaches_nested_callable();
    test_vhdl_packed_bitwise_evaluator_preserves_operator_range();
    test_vhdl_pure_call_updates_local_packed_array_in_place();
    test_vhdl_pure_call_respects_package_generic_binding();
    test_vhdl_parsed_operator_builtin_provenance();
    test_vhdl_callable_formal_preserves_actual_packed_range();
    test_vhdl_generated_iterator_is_integer_actual();
    test_vhdl_noninteger_named_constant_is_not_generate_iterator();
    test_vhdl_unconstrained_unsigned_conversion_preserves_operand_width();
    test_vhdl_static_width_formal_specializes_callable_frame();
    test_vhdl_dynamic_width_formal_remains_unresolved();
    test_vhdl_dynamic_exponent_formal_remains_unresolved();
    test_vhdl_dynamic_integer_for_loop_bounds();
    test_vhdl_runtime_for_loop_iteration_cap();
    const auto parsed = fsim::frontend::parse_text(
        "interface-function-generics.vhd",
        R"(
package function_pkg is
  function package_increment(value : integer) return integer;
end package;
package body function_pkg is
  function package_increment(value : integer) return integer is
  begin
    return value + 2;
  end function;
end package body;

entity function_leaf is
  generic (
    function transform(value : integer) return integer;
    Width : positive := transform(3));
  port (
    input_value : in integer;
    output_value : out integer);
end entity;

architecture rtl of function_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity boxed_leaf is
  generic (
    function transform(value : integer) return integer is <>);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;

architecture rtl of boxed_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity function_wrapper is
  generic (
    function forwarded(value : integer) return integer);
end entity;

architecture rtl of function_wrapper is
  signal input_value : integer;
  signal output_value : integer;
begin
  nested: entity work.function_leaf(rtl)
    generic map (transform => forwarded)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity package_wrapper is
end entity;

use work.function_pkg.all;
architecture rtl of package_wrapper is
  signal input_value : integer;
  signal output_value : integer;
begin
  nested: entity work.function_leaf(rtl)
    generic map (package_increment)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity function_top is
end entity;

architecture rtl of function_top is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  signal direct_input : integer;
  signal direct_output : integer;
  signal boxed_input : integer;
  signal boxed_output : integer;
begin
  direct_instance: entity work.function_leaf(rtl)
    generic map (increment)
    port map (
      input_value => direct_input,
      output_value => direct_output);
  nested_instance: entity work.function_wrapper(rtl)
    generic map (forwarded => increment)
    port map ();
  boxed_instance: entity work.boxed_leaf(rtl)
    port map (
      input_value => boxed_input,
      output_value => boxed_output);
  package_instance: entity work.package_wrapper(rtl)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    const auto elaborated = compile_and_elaborate(
        parsed.design,
        "vhdl:work.function_top(rtl)");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(!has_diagnostic(elaborated, "FSIM-ELAB-GENERIC-005"));
    assert(!has_diagnostic(elaborated, "FSIM-ELAB-HIR-001"));
    assert(elaborated.ok());
    assert(elaborated.design->specializations().size() == 7);

    const auto find_specialization =
        [&](const std::string_view instance) {
            return std::ranges::find_if(
                elaborated.design->specializations(),
                [&](const auto& specialization) {
                    return specialization.instance == instance;
                });
        };
    const auto direct = find_specialization("function_top.direct_instance");
    const auto wrapper = find_specialization("function_top.nested_instance");
    const auto nested = find_specialization(
        "function_top.nested_instance.nested");
    const auto boxed = find_specialization("function_top.boxed_instance");
    const auto package_actual = find_specialization(
        "function_top.package_instance.nested");
    assert(direct != elaborated.design->specializations().end());
    assert(wrapper != elaborated.design->specializations().end());
    assert(nested != elaborated.design->specializations().end());
    assert(boxed != elaborated.design->specializations().end());
    assert(
        package_actual
        != elaborated.design->specializations().end());
    assert(direct->parameter_values.size() == 2);
    assert(direct->parameter_values.front().first == "transform");
    assert(wrapper->parameter_values.size() == 1);
    assert(wrapper->parameter_values.front().first == "forwarded");
    assert(nested->parameter_values.size() == 2);
    assert(nested->parameter_values.front().first == "transform");
    assert(boxed->parameter_values.size() == 1);
    assert(boxed->parameter_values.front().first == "transform");
    assert(package_actual->parameter_values.size() == 2);
    assert(
        package_actual->parameter_values.front().first
        == "transform");

    const auto missing = fsim::frontend::parse_text(
        "missing-function-actual.vhd",
        R"(
entity missing is
  generic (
    function required(value : integer) return integer);
end entity;
architecture rtl of missing is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(missing.ok());
    const auto missing_result = compile_and_elaborate(
        missing.design, "vhdl:work.missing(rtl)");
    assert(!missing_result.ok());
    assert(has_diagnostic(
        missing_result, "FSIM-ELAB-VHFUNC-004"));

    const auto invalid = fsim::frontend::parse_text(
        "invalid-function-actuals.vhd",
        R"(
entity required_leaf is
  generic (
    function selected(value : integer) return integer);
end entity;
architecture rtl of required_leaf is
begin
end architecture;

entity default_leaf is
  generic (
    function selected(value : integer) return integer is <>);
end entity;
architecture rtl of default_leaf is
begin
end architecture;

entity invalid_top is
end entity;
architecture rtl of invalid_top is
  impure function impure_actual(value : integer) return integer is
  begin
    return value;
  end function;
  function wrong_profile(value : bit) return bit is
  begin
    return value;
  end function;
  function declared(value : integer) return integer;
  function first_match(value : integer) return integer is
  begin
    return value;
  end function;
  function second_match(value : integer) return integer is
  begin
    return value + 1;
  end function;
begin
  expression_actual: entity work.required_leaf(rtl)
    generic map (selected => 1)
    port map ();
  invisible_actual: entity work.required_leaf(rtl)
    generic map (selected => absent)
    port map ();
  profile_actual: entity work.required_leaf(rtl)
    generic map (selected => wrong_profile)
    port map ();
  impure_binding: entity work.required_leaf(rtl)
    generic map (selected => impure_actual)
    port map ();
  undefined_binding: entity work.required_leaf(rtl)
    generic map (selected => declared)
    port map ();
  ambiguous_default: entity work.default_leaf(rtl)
    port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid.ok());
    const auto invalid_result = compile_and_elaborate(
        invalid.design, "vhdl:work.invalid_top(rtl)");
    assert(!invalid_result.ok());
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-003"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-005"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-007"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-008"));
    assert(has_diagnostic(
        invalid_result, "FSIM-ELAB-VHFUNC-006"));

    auto cross_language_parent = fsim::frontend::parse_text(
        "cross-language-function.sv",
        R"(
module cross_language_function;
  function automatic int increment(input int value);
    return value + 1;
  endfunction
  foreign_target #(.selected(increment)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    auto cross_language_child = fsim::frontend::parse_text(
        "cross-language-function.vhd",
        R"(
entity foreign_target is
  generic (
    function selected(value : integer) return integer);
end entity;
architecture rtl of foreign_target is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(
        cross_language_parent.ok()
        && cross_language_child.ok());
    for (auto& unit : cross_language_child.design.units) {
        cross_language_parent.design.units.push_back(
            std::move(unit));
    }
    const std::vector<fsim::elaboration::Binding>
        cross_language_binding { { "cross_language_function.child",
            "vhdl:work.foreign_target(rtl)",
            std::nullopt } };
    const auto cross_language_result = compile_and_elaborate(
        cross_language_parent.design,
        "sv:work.cross_language_function",
        cross_language_binding);
    assert(!cross_language_result.ok());
    assert(has_diagnostic(
        cross_language_result, "FSIM-ELAB-VHFUNC-002"));

    const auto recursive = fsim::frontend::parse_text(
        "recursive-function-actual.vhd",
        R"(
entity recursive_leaf is
  generic (
    function selected(value : integer) return integer);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of recursive_leaf is
begin
  output_value <= selected(input_value);
end architecture;

entity recursive_top is
end entity;
architecture rtl of recursive_top is
  function recurse(value : integer) return integer is
  begin
    return recurse(value);
  end function;
  signal input_value : integer;
  signal output_value : integer;
begin
  child: entity work.recursive_leaf(rtl)
    generic map (recurse)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(recursive.ok());
    const auto recursive_result = compile_and_elaborate(
        recursive.design, "vhdl:work.recursive_top(rtl)");
    assert(recursive_result.ok());

    const auto loop_shadow = fsim::frontend::parse_text(
        "pure-loop-shadow.vhd",
        R"(
entity pure_loop_shadow is
end entity;
architecture rtl of pure_loop_shadow is
  signal lane : integer;
  signal result : bit;
  function accumulate(seed : bit) return bit is
    variable total : bit := seed;
  begin
    for lane in 1 to 2 loop
      total := not total;
    end loop;
    return total;
  end function;
begin
  result <= accumulate('0');
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(loop_shadow.ok());
    const auto loop_shadow_result = compile_and_elaborate(
        loop_shadow.design, "vhdl:work.pure_loop_shadow(rtl)");
    for (const auto& diagnostic : loop_shadow_result.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
    }
    assert(loop_shadow_result.ok());

    const auto contextual_result = fsim::frontend::parse_text(
        "vhdl-2019-contextual-result.vhd",
        R"(
entity contextual_result is
end entity;

architecture rtl of contextual_result is
  function fill(value : bit) return result_t of bit_vector is
    variable answer : result_t := (others => value);
    variable result_length : integer := result_t'length;
  begin
    if result_length = answer'length then
      return answer;
    end if;
    return (others => not value);
  end function;
  signal narrow : bit_vector(3 downto 0) := (others => '0');
  signal wide : bit_vector(7 downto 0) := (others => '1');
begin
  exercise : process
  begin
    narrow <= fill('1');
    wide <= fill('0');
    wait;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    if (!contextual_result.ok()) {
        for (const auto& diagnostic : contextual_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(contextual_result.ok());
    const auto& contextual_function =
        contextual_result.design.units.back().functions.front();
    assert(contextual_function.vhdl_return_identifier == "result_t");
    assert(contextual_function.type_aliases.front().name == "result_t");
    const auto contextual_elaboration = compile_and_elaborate(
        contextual_result.design,
        "vhdl:work.contextual_result(rtl)");
    if (!contextual_elaboration.ok()) {
        for (const auto& diagnostic : contextual_elaboration.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(contextual_elaboration.ok());
    auto contextual_interpreter =
        contextual_elaboration.design->create_interpreter();
    assert(contextual_interpreter->run().status
           == fsim::runtime::RunStatus::completed);
    const auto narrow = contextual_elaboration.design->find_signal("narrow");
    const auto wide = contextual_elaboration.design->find_signal("wide");
    assert(narrow && wide);
    assert(contextual_interpreter->signal_value(*narrow).to_msb_string()
           == "1111");
    assert(contextual_interpreter->signal_value(*wide).to_msb_string()
           == "00000000");

    const auto legacy_contextual_result = fsim::frontend::parse_text(
        "vhdl-2008-contextual-result.vhd",
        R"(
entity legacy_contextual_result is end entity;
architecture rtl of legacy_contextual_result is
  function fill(value : bit) return result_t of bit_vector is
  begin
    return (others => value);
  end function;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2008);
    assert(!legacy_contextual_result.ok());
    assert(std::ranges::any_of(
        legacy_contextual_result.diagnostics,
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-FE-VHSTD-003";
        }));

    const auto unconstrained_context = fsim::frontend::parse_text(
        "vhdl-2019-unconstrained-result.vhd",
        R"(
entity unconstrained_result is end entity;
architecture rtl of unconstrained_result is
  function fill(value : bit) return result_t of bit_vector is
  begin
    return (others => value);
  end function;
  function first(value : bit_vector) return bit is
  begin
    return value(value'left);
  end function;
  signal observed : bit;
begin
  observed <= first(fill('1'));
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(unconstrained_context.ok());
    const auto rejected_unconstrained = compile_and_elaborate(
        unconstrained_context.design,
        "vhdl:work.unconstrained_result(rtl)");
    assert(!rejected_unconstrained.ok());
    assert(has_diagnostic(
        rejected_unconstrained, "FSIM-ELAB-VHRESULT-001"));

    const auto duplicate_result = fsim::frontend::parse_text(
        "vhdl-2019-duplicate-result.vhd",
        R"(
entity duplicate_result is end entity;
architecture rtl of duplicate_result is
  function fill(value : bit) return result_t of bit_vector is
    subtype result_t is bit_vector(1 downto 0);
  begin
    return (others => value);
  end function;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(!duplicate_result.ok());
    assert(std::ranges::any_of(
        duplicate_result.diagnostics,
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-VHDL-SEM-036";
        }));

    const auto conflicting_result = fsim::frontend::parse_text(
        "vhdl-2019-conflicting-result.vhd",
        R"(
entity conflicting_result is end entity;
architecture rtl of conflicting_result is
  function fill(result_t : bit) return result_t of bit_vector is
  begin
    return (others => result_t);
  end function;
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(!conflicting_result.ok());
    assert(std::ranges::any_of(
        conflicting_result.diagnostics,
        [](const auto& diagnostic) {
          return diagnostic.code == "FSIM-VHDL-SEM-112";
        }));

    const auto negative_mod = fsim::frontend::parse_text(
        "negative-mod-constant-function.vhd",
        R"(
package negative_mod_pkg is
  function normalize_mod(value : integer) return integer;
  function normalize_rem(value : integer) return integer;
end package;
package body negative_mod_pkg is
  function normalize_mod(value : integer) return integer is
  begin
    return value mod 255;
  end function;
  function normalize_rem(value : integer) return integer is
  begin
    return value rem 255;
  end function;
end package body;

use work.negative_mod_pkg.all;
entity negative_mod_constant_function is
  generic (
    mod_value : integer := normalize_mod(-1);
    rem_value : integer := normalize_rem(-1));
end entity;
architecture rtl of negative_mod_constant_function is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(negative_mod.ok());
    const auto negative_mod_result = compile_and_elaborate(
        negative_mod.design,
        "vhdl:work.negative_mod_constant_function(rtl)");
    for (const auto& diagnostic : negative_mod_result.diagnostics) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
    }
    assert(negative_mod_result.ok());
    const auto& negative_mod_specialization =
        negative_mod_result.design->specializations().front();
    assert(negative_mod_specialization.parameter_values.size() == 2);
    assert(negative_mod_specialization.parameter_values[0].first
           == "mod_value");
    assert(negative_mod_specialization.parameter_values[0].second
           == "254");
    assert(negative_mod_specialization.parameter_values[1].first
           == "rem_value");
    assert(negative_mod_specialization.parameter_values[1].second
           == "-1");
}

} // namespace fsim::tests::elaboration
