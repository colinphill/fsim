// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_vhdl_callable_overloads() {
    const auto unconstrained_result = frontend::parse_text(
        "vhdl_unconstrained_function_result.vhd",
        R"(
entity unconstrained_function_result is
  generic (width : integer := 8);
  port (
    input_value : in bit_vector(width - 1 downto 0);
    output_value : out bit_vector(width - 1 downto 0));
end entity;
architecture rtl of unconstrained_function_result is
  function pass_through(
      value : bit_vector; count : integer) return bit_vector is
    variable result : bit_vector(count - 1 downto 0) := value;
  begin
    return result;
  end function;
begin
  output_value <= pass_through(input_value, width);
end architecture;
)",
        frontend::Language::Vhdl2008);
    assert(unconstrained_result.ok());
    const auto unconstrained_elaboration = fsim::elaboration::elaborate(
        unconstrained_result.design,
        "vhdl:work.unconstrained_function_result(rtl)");
    assert(unconstrained_elaboration.ok());

  const auto parsed = fsim::frontend::parse_text(
      "vhdl-callable-overloads.vhd",
      R"(
package overload_pkg is
  function choose(value : integer) return integer;
  function choose(value : boolean) return boolean;
  procedure assign_value(
    source : in integer;
    variable target : out integer);
  procedure assign_value(
    source : in boolean;
    variable target : out boolean);
end package;

package body overload_pkg is
  function choose(value : integer) return integer is
  begin
    return value + 10;
  end function;
  function choose(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure assign_value(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 20;
  end procedure;
  procedure assign_value(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := not source;
  end procedure;
end package body;

package overload_template is
  generic (bias : integer := 1);
  function transform(value : integer) return integer;
  function transform(value : boolean) return boolean;
  procedure publish(
    source : in integer;
    variable target : out integer);
  procedure publish(
    source : in boolean;
    variable target : out boolean);
end package;

package body overload_template is
  function transform(value : integer) return integer is
  begin
    return value + bias;
  end function;
  function transform(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure publish(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + bias;
  end procedure;
  procedure publish(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := not source;
  end procedure;
end package body;

context overload_context is
  library work;
  use work.overload_pkg.all;
end context;

entity callable_overload_top is
  port (
    integer_input : in integer;
    boolean_input : in boolean;
    package_integer : out integer;
    package_boolean : out boolean;
    local_integer : out integer;
    local_boolean : out boolean;
    selected_integer : out integer;
    selected_boolean : out boolean;
    package_proc_integer : out integer;
    package_proc_boolean : out boolean;
    local_proc_integer : out integer;
    local_proc_boolean : out boolean;
    contextual_integer : out integer;
    contextual_boolean : out boolean;
    named_integer : out integer;
    named_boolean : out boolean;
    defaulted_integer : out integer;
    nested_integer : out integer;
    nested_boolean : out boolean);
end entity;

use work.overload_pkg.all;
architecture rtl of callable_overload_top is
  function choose_local(value : integer) return integer is
  begin
    return value + 30;
  end function;
  function choose_local(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure assign_local(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 40;
  end procedure;
  procedure assign_local(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := not source;
  end procedure;
  function contextual(value : integer) return integer is
  begin
    return value + 50;
  end function;
  function contextual(value : integer) return boolean is
  begin
    return true;
  end function;
  function named_select(
    number : integer;
    flag : boolean := false) return integer is
  begin
    return number + 60;
  end function;
  function named_select(
    flag : boolean;
    number : integer := 0) return boolean is
  begin
    return flag;
  end function;
  function inner(value : integer) return integer is
  begin
    return value + 70;
  end function;
  function inner(value : boolean) return boolean is
  begin
    return not value;
  end function;
  function outer(value : integer) return integer is
  begin
    return value + 80;
  end function;
  function outer(value : boolean) return boolean is
  begin
    return not value;
  end function;
begin
  package_integer <= choose(integer_input);
  package_boolean <= choose(boolean_input);
  local_integer <= choose_local(integer_input);
  local_boolean <= choose_local(boolean_input);
  selected_integer <= overload_pkg.choose(integer_input);
  selected_boolean <= work.overload_pkg.choose(boolean_input);
  contextual_integer <= contextual(integer_input);
  contextual_boolean <= contextual(integer_input);
  named_integer <= named_select(number => integer_input);
  named_boolean <= named_select(flag => boolean_input);
  defaulted_integer <= named_select(integer_input);
  nested_integer <= outer(inner(integer_input));
  nested_boolean <= outer(inner(boolean_input));
  process(integer_input, boolean_input)
  begin
    assign_value(integer_input, package_proc_integer);
    assign_value(boolean_input, package_proc_boolean);
    assign_local(integer_input, local_proc_integer);
    assign_local(boolean_input, local_proc_boolean);
    overload_pkg.assign_value(integer_input, package_proc_integer);
    work.overload_pkg.assign_value(
      boolean_input, package_proc_boolean);
  end process;
end architecture;

context work.overload_context;
entity context_overload_top is
  port (
    integer_input : in integer;
    boolean_input : in boolean;
    integer_result : out integer;
    boolean_result : out boolean);
end entity;
context work.overload_context;
architecture rtl of context_overload_top is
begin
  integer_result <= choose(integer_input);
  boolean_result <= choose(boolean_input);
end architecture;

entity generic_overload_top is
  port (
    integer_input : in integer;
    boolean_input : in boolean;
    integer_result : out integer;
    boolean_result : out boolean;
    integer_procedure : out integer;
    boolean_procedure : out boolean);
end entity;
architecture rtl of generic_overload_top is
  package local_ops is new work.overload_template
    generic map (bias => 3);
begin
  integer_result <= local_ops.transform(integer_input);
  boolean_result <= local_ops.transform(boolean_input);
  process(integer_input, boolean_input)
  begin
    local_ops.publish(integer_input, integer_procedure);
    local_ops.publish(boolean_input, boolean_procedure);
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(parsed.ok());
  const auto elaborated = fsim::elaboration::elaborate(
      parsed.design,
      "vhdl:work.callable_overload_top(rtl)");
  if (!elaborated.ok()) {
    for (const auto& diagnostic : elaborated.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(elaborated.ok());

  const auto context_result = fsim::elaboration::elaborate(
      parsed.design,
      "vhdl:work.context_overload_top(rtl)");
  if (!context_result.ok()) {
    for (const auto& diagnostic : context_result.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(context_result.ok());

  const auto generic_result = fsim::elaboration::elaborate(
      parsed.design,
      "vhdl:work.generic_overload_top(rtl)");
  if (!generic_result.ok()) {
    for (const auto& diagnostic : generic_result.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(generic_result.ok());

  const auto cross_package = fsim::frontend::parse_text(
      "vhdl-cross-package-overloads.vhd",
      R"(
package integer_ops is
  function adjust(value : integer) return integer;
  procedure update_value(
    source : in integer;
    variable target : out integer);
end package;
package body integer_ops is
  function adjust(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure update_value(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 2;
  end procedure;
end package body;

package boolean_ops is
  function adjust(value : boolean) return boolean;
  procedure update_value(
    source : in boolean;
    variable target : out boolean);
end package;
package body boolean_ops is
  function adjust(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure update_value(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := not source;
  end procedure;
end package body;

entity cross_package_overloads is
  port (
    integer_input : in integer;
    boolean_input : in boolean;
    integer_result : out integer;
    boolean_result : out boolean;
    integer_procedure : out integer;
    boolean_procedure : out boolean);
end entity;
use work.integer_ops.all;
use work.boolean_ops.all;
architecture rtl of cross_package_overloads is
begin
  integer_result <= adjust(integer_input);
  boolean_result <= adjust(boolean_input);
  process(integer_input, boolean_input)
  begin
    update_value(integer_input, integer_procedure);
    update_value(boolean_input, boolean_procedure);
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(cross_package.ok());
  const auto cross_package_result = fsim::elaboration::elaborate(
      cross_package.design,
      "vhdl:work.cross_package_overloads(rtl)");
  if (!cross_package_result.ok()) {
    for (const auto& diagnostic : cross_package_result.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(cross_package_result.ok());

  const auto hiding = fsim::frontend::parse_text(
      "vhdl-callable-overload-hiding.vhd",
      R"(
package imported_ops is
  function hidden(value : integer) return integer;
  function hidden(value : boolean) return boolean;
  procedure hidden_store(
    source : in integer;
    variable target : out integer);
  procedure hidden_store(
    source : in boolean;
    variable target : out boolean);
end package;
package body imported_ops is
  function hidden(value : integer) return integer is
  begin
    return value + 1;
  end function;
  function hidden(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure hidden_store(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 1;
  end procedure;
  procedure hidden_store(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := not source;
  end procedure;
end package body;

entity overload_hiding is
end entity;
use work.imported_ops.all;
architecture rtl of overload_hiding is
  function hidden(value : integer) return integer is
  begin
    return value + 2;
  end function;
  procedure hidden_store(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 2;
  end procedure;
  signal source : integer;
  signal result : integer;
  signal stored : integer;
begin
  result <= hidden(source);
  process(source)
  begin
    hidden_store(source, stored);
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(hiding.ok());
  const auto hiding_result = fsim::elaboration::elaborate(
      hiding.design, "vhdl:work.overload_hiding(rtl)");
  if (!hiding_result.ok()) {
    for (const auto& diagnostic : hiding_result.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(hiding_result.ok());

  const auto imported_homographs = fsim::frontend::parse_text(
      "vhdl-imported-callable-homographs.vhd",
      R"(
package first_ops is
  function same(value : integer) return integer;
  procedure same_store(
    source : in integer;
    variable target : out integer);
end package;
package body first_ops is
  function same(value : integer) return integer is
  begin
    return value;
  end function;
  procedure same_store(
    source : in integer;
    variable target : out integer) is
  begin
    target := source;
  end procedure;
end package body;
package second_ops is
  function same(value : integer) return integer;
  procedure same_store(
    source : in integer;
    variable target : out integer);
end package;
package body second_ops is
  function same(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure same_store(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 1;
  end procedure;
end package body;
entity imported_homographs is
end entity;
use work.first_ops.all;
use work.second_ops.all;
architecture rtl of imported_homographs is
  signal source : integer;
  signal result : integer;
  signal stored : integer;
begin
  result <= same(source);
  process(source)
  begin
    same_store(source, stored);
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(imported_homographs.ok());
  const auto imported_homograph_result =
      fsim::elaboration::elaborate(
          imported_homographs.design,
          "vhdl:work.imported_homographs(rtl)");
  assert(!imported_homograph_result.ok());
  assert(has_diagnostic(
      imported_homograph_result, "FSIM-ELAB-VHOVER-001"));
  assert(has_diagnostic(
      imported_homograph_result, "FSIM-ELAB-VHOVER-004"));
  assert(!has_diagnostic(
      imported_homograph_result, "FSIM-ELAB-VHOVER-003"));
  assert(!has_diagnostic(
      imported_homograph_result, "FSIM-ELAB-VHOVER-006"));

  const auto nonconforming_bodies = fsim::frontend::parse_text(
      "vhdl-nonconforming-package-bodies.vhd",
      R"(
package broken_ops is
  function broken(value : integer) return integer;
  procedure broken_store(value : in integer);
end package;
package body broken_ops is
  function broken(value : integer) return boolean is
  begin
    return true;
  end function;
  procedure broken_store(value : in boolean) is
  begin
    null;
  end procedure;
end package body;
entity broken_body_top is
end entity;
use work.broken_ops.all;
architecture rtl of broken_body_top is
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(nonconforming_bodies.ok());
  const auto nonconforming_body_result =
      fsim::elaboration::elaborate(
          nonconforming_bodies.design,
          "vhdl:work.broken_body_top(rtl)");
  assert(!nonconforming_body_result.ok());
  assert(has_diagnostic(
      nonconforming_body_result, "FSIM-ELAB-VHLEGAL-001"));
  assert(has_diagnostic(
      nonconforming_body_result, "FSIM-ELAB-VHLEGAL-002"));
  assert(has_diagnostic(
      nonconforming_body_result, "FSIM-ELAB-VHLEGAL-003"));
  assert(has_diagnostic(
      nonconforming_body_result, "FSIM-ELAB-VHLEGAL-004"));

  const auto deferred_constants = fsim::frontend::parse_text(
      "vhdl-deferred-package-constants.vhd",
      R"(
package missing_constant is
  constant value : integer;
end package;

package mismatched_constant is
  constant value : integer;
end package;
package body mismatched_constant is
  constant value : boolean := true;
end package body;

package redeclared_constant is
  constant value : integer := 1;
end package;
package body redeclared_constant is
  constant value : integer := 2;
end package body;

entity missing_constant_top is end entity;
use work.missing_constant.all;
architecture rtl of missing_constant_top is begin end architecture;

entity mismatched_constant_top is end entity;
use work.mismatched_constant.all;
architecture rtl of mismatched_constant_top is begin end architecture;

entity redeclared_constant_top is end entity;
use work.redeclared_constant.all;
architecture rtl of redeclared_constant_top is begin end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(deferred_constants.ok());
  const auto missing_constant_result = fsim::elaboration::elaborate(
      deferred_constants.design,
      "vhdl:work.missing_constant_top(rtl)");
  const auto mismatched_constant_result = fsim::elaboration::elaborate(
      deferred_constants.design,
      "vhdl:work.mismatched_constant_top(rtl)");
  const auto redeclared_constant_result = fsim::elaboration::elaborate(
      deferred_constants.design,
      "vhdl:work.redeclared_constant_top(rtl)");
  assert(has_diagnostic(
      missing_constant_result, "FSIM-ELAB-VHLEGAL-010"));
  assert(has_diagnostic(
      mismatched_constant_result, "FSIM-ELAB-VHLEGAL-011"));
  assert(has_diagnostic(
      redeclared_constant_result, "FSIM-ELAB-VHLEGAL-012"));

  const auto purity = fsim::frontend::parse_text(
      "vhdl-pure-function-legality.vhd",
      R"(
entity pure_legality is
end entity;
architecture rtl of pure_legality is
  signal shared_value : integer;
  signal first_result : integer;
  signal second_result : integer;
  procedure observe is
  begin
    null;
  end procedure;
  function reads_signal return integer is
  begin
    return shared_value;
  end function;
  function calls_procedure return integer is
  begin
    observe;
    return 0;
  end function;
begin
  first_result <= reads_signal;
  second_result <= calls_procedure;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!purity.ok()) {
    for (const auto& diagnostic : purity.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(purity.ok());
  const auto purity_result = fsim::elaboration::elaborate(
      purity.design, "vhdl:work.pure_legality(rtl)");
  assert(!purity_result.ok());
  assert(has_diagnostic(
      purity_result, "FSIM-ELAB-VHLEGAL-005"));
  assert(has_diagnostic(
      purity_result, "FSIM-ELAB-VHLEGAL-006"));

  const auto default_legality = fsim::frontend::parse_text(
      "vhdl-subprogram-default-legality.vhd",
      R"(
entity default_legality is
end entity;
architecture rtl of default_legality is
  signal result : boolean;
  function invalid_default(
    value : integer := true) return boolean is
  begin
    return true;
  end function;
  procedure invalid_procedure_default(
    value : in integer := false) is
  begin
    null;
  end procedure;
begin
  result <= invalid_default();
  process(result)
  begin
    invalid_procedure_default;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(default_legality.ok());
  const auto default_legality_result =
      fsim::elaboration::elaborate(
          default_legality.design,
          "vhdl:work.default_legality(rtl)");
  assert(!default_legality_result.ok());
  assert(has_diagnostic(
      default_legality_result, "FSIM-ELAB-VHLEGAL-007"));
  assert(has_diagnostic(
      default_legality_result, "FSIM-ELAB-VHLEGAL-008"));

  const auto resolution_legality = fsim::frontend::parse_text(
      "vhdl-resolution-function-legality.vhd",
      R"(
entity resolution_legality is
end entity;
architecture rtl of resolution_legality is
  type first_pair is array (0 to 1) of bit;
  type second_pair is array (0 to 1) of bit;
  function wrong_profile(value : bit) return bit is
  begin
    return value;
  end function;
  function unsupported(values : first_pair) return bit is
  begin
    return values(0) xor values(1);
  end function;
  function ambiguous(values : first_pair) return bit is
  begin
    return values(0) or values(1);
  end function;
  function ambiguous(values : second_pair) return bit is
  begin
    return values(0) and values(1);
  end function;
  subtype missing_bit is absent bit;
  subtype wrong_bit is wrong_profile bit;
  subtype unsupported_bit is unsupported bit;
  subtype ambiguous_bit is ambiguous bit;
  signal missing_value : missing_bit;
  signal wrong_value : wrong_bit;
  signal unsupported_value : unsupported_bit;
  signal ambiguous_value : ambiguous_bit;
begin
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(resolution_legality.ok());
  const auto resolution_legality_result =
      fsim::elaboration::elaborate(
          resolution_legality.design,
          "vhdl:work.resolution_legality(rtl)");
  assert(!resolution_legality_result.ok());
  assert(has_diagnostic(
      resolution_legality_result,
      "FSIM-ELAB-VHRESOLVE-001"));
  assert(has_diagnostic(
      resolution_legality_result,
      "FSIM-ELAB-VHRESOLVE-002"));
  assert(has_diagnostic(
      resolution_legality_result,
      "FSIM-ELAB-VHRESOLVE-003"));
  assert(has_diagnostic(
      resolution_legality_result,
      "FSIM-ELAB-VHRESOLVE-004"));

  const auto no_match = fsim::frontend::parse_text(
      "vhdl-callable-overload-no-match.vhd",
      R"(
entity callable_no_match is
end entity;
architecture rtl of callable_no_match is
  function convert(value : integer) return integer is
  begin
    return value;
  end function;
  procedure store(
    source : in integer;
    variable target : out integer) is
  begin
    target := source;
  end procedure;
  signal boolean_value : boolean;
  signal integer_value : integer;
begin
  integer_value <= convert(boolean_value);
  process(boolean_value)
  begin
    store(boolean_value, integer_value);
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(no_match.ok());
  const auto no_match_result = fsim::elaboration::elaborate(
      no_match.design, "vhdl:work.callable_no_match(rtl)");
  assert(!no_match_result.ok());
  assert(has_diagnostic(
      no_match_result, "FSIM-ELAB-VHOVER-002"));
  assert(has_diagnostic(
      no_match_result, "FSIM-ELAB-VHOVER-005"));

  const auto ambiguous = fsim::frontend::parse_text(
      "vhdl-callable-overload-ambiguous.vhd",
      R"(
entity callable_ambiguous is
end entity;
architecture rtl of callable_ambiguous is
  type first_kind is (shared, first_only);
  type second_kind is (shared, second_only);
  function inspect(value : first_kind) return integer is
  begin
    return 1;
  end function;
  function inspect(value : second_kind) return integer is
  begin
    return 2;
  end function;
  procedure observe(source : in first_kind) is
  begin
    null;
  end procedure;
  procedure observe(source : in second_kind) is
  begin
    null;
  end procedure;
  signal result : integer;
begin
  result <= inspect(shared);
  process
  begin
    observe(shared);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(ambiguous.ok());
  const auto ambiguous_result = fsim::elaboration::elaborate(
      ambiguous.design, "vhdl:work.callable_ambiguous(rtl)");
  assert(!ambiguous_result.ok());
  assert(has_diagnostic(
      ambiguous_result, "FSIM-ELAB-VHOVER-001"));
  assert(has_diagnostic(
      ambiguous_result, "FSIM-ELAB-VHOVER-004"));

  const auto duplicate = fsim::frontend::parse_text(
      "vhdl-callable-overload-duplicate.vhd",
      R"(
entity callable_duplicate is
end entity;
architecture rtl of callable_duplicate is
  function repeated(value : integer) return integer is
  begin
    return value;
  end function;
  function repeated(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure repeated_proc(value : in integer) is
  begin
    null;
  end procedure;
  procedure repeated_proc(value : in integer) is
  begin
    null;
  end procedure;
begin
  process
  begin
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(duplicate.ok());
  const auto duplicate_result = fsim::elaboration::elaborate(
      duplicate.design, "vhdl:work.callable_duplicate(rtl)");
  assert(!duplicate_result.ok());
  assert(has_diagnostic(
      duplicate_result, "FSIM-ELAB-VHOVER-003"));
  assert(has_diagnostic(
      duplicate_result, "FSIM-ELAB-VHOVER-006"));

  const auto name_legality = fsim::frontend::parse_text(
      "vhdl-overload-name-legality.vhd",
      R"(
entity overload_name_legality is
end entity;
architecture rtl of overload_name_legality is
  type first_bits is array (0 to 1) of bit;
  type second_bits is array (0 to 1) of bit;
  function only_function(value : integer) return integer is
  begin
    return value;
  end function;
  procedure only_procedure(value : in integer) is
  begin
    null;
  end procedure;
  function aggregate_choice(value : first_bits) return integer is
  begin
    return 1;
  end function;
  function aggregate_choice(value : second_bits) return integer is
  begin
    return 2;
  end function;
  signal missing_result : integer;
  signal context_result : integer;
  signal aggregate_result : integer;
begin
  missing_result <= missing_function(1);
  context_result <= only_procedure(1);
  aggregate_result <= aggregate_choice((others => '1'));
  process
  begin
    missing_procedure(1);
    only_function(1);
    wait;
  end process;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  assert(name_legality.ok());
  const auto name_legality_result = fsim::elaboration::elaborate(
      name_legality.design,
      "vhdl:work.overload_name_legality(rtl)");
  assert(!name_legality_result.ok());
  assert(has_diagnostic(
      name_legality_result, "FSIM-ELAB-VHNAME-001"));
  assert(has_diagnostic(
      name_legality_result, "FSIM-ELAB-VHPROC-014"));
  assert(has_diagnostic(
      name_legality_result, "FSIM-ELAB-VHOVER-001"));

  const auto static_legality = fsim::frontend::parse_text(
      "vhdl-static-overload-legality.vhd",
      R"(
package static_legality_pkg is
  subtype small_t is integer range 1 to 2;
  constant outside_range : integer := small_t(3);
  constant overflowed : integer :=
    2147483647 + 1;
end package;
entity static_overload_legality is
end entity;
use work.static_legality_pkg.all;
architecture rtl of static_overload_legality is
  signal first_result : integer;
  signal second_result : integer;
begin
  first_result <= outside_range;
  second_result <= overflowed;
end architecture;
)",
      fsim::frontend::Language::Vhdl2008);
  if (!static_legality.ok()) {
    for (const auto& diagnostic : static_legality.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(static_legality.ok());
  const auto static_legality_result = fsim::elaboration::elaborate(
      static_legality.design,
      "vhdl:work.static_overload_legality(rtl)");
  assert(!static_legality_result.ok());
  assert(has_diagnostic(
      static_legality_result, "FSIM-ELAB-VHSTATIC-002"));
  assert(has_diagnostic(
      static_legality_result, "FSIM-ELAB-PKG-006"));
}

}  // namespace fsim::tests::elaboration
