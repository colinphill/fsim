-- SPDX-License-Identifier: Apache-2.0
--
-- A use clause makes declarations visible only in the declarative region of
-- the design unit it is in (IEEE 1076-2019 12.4, 12.3): a package that a
-- design unit uses keeps its own visibility when the design unit's
-- elaboration folds the package's declarations. USER_PKG sees SYM_A only;
-- TOP also uses SYM_B and IEEE.VITAL_MEMORY, whose types declare the same
-- literals (GREEN, 'Z', and VITAL's '1', 'X', 'Z'), so they must not make
-- USER_PKG's literals ambiguous or select another type's literal. The design
-- runs with and without TOP's two extra use clauses, and must give the same
-- reports.
package sym_a is
  type sym_a_t is (red, green, blue, 'Z');
end package;

package sym_b is
  type sym_b_t is ('Z', green, yellow, black, white);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.sym_a.all;
package user_pkg is
  constant green_pos : integer := sym_a_t'pos(green);
  subtype green_to_z is sym_a_t range green to 'Z';
  constant high_pos : integer := green_to_z'pos(green_to_z'high);
  constant z_const : sym_a_t := 'Z';
  constant z_pos : integer := sym_a_t'pos(z_const);
  constant a_green : sym_a_t := green;
  type by_sym is array (green_to_z) of integer;
  function sym_length return integer;
  function is_green(s : sym_a_t) return boolean;
  function strength(v : std_ulogic_vector) return std_ulogic_vector;
end package;

package body user_pkg is
  function sym_length return integer is
  begin
    return by_sym'length;
  end function;

  function is_green(s : sym_a_t) return boolean is
  begin
    case s is
      when green => return true;
      when others => return false;
    end case;
  end function;

  function strength(v : std_ulogic_vector) return std_ulogic_vector is
  begin
    return To_X01Z(v);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_memory.all;
use work.sym_b.all;
use work.user_pkg.all;

entity top is
end entity;

architecture test of top is
  constant folded_length : integer := sym_length;
  constant folded_green : boolean := is_green(a_green);
begin
  process
  begin
    report "pkguse green_pos " & integer'image(green_pos);
    report "pkguse high_pos " & integer'image(high_pos);
    report "pkguse z_pos " & integer'image(z_pos);
    report "pkguse sym_length " & integer'image(sym_length);
    report "pkguse folded_length " & integer'image(folded_length);
    report "pkguse is_green " & boolean'image(is_green(a_green));
    report "pkguse folded_green " & boolean'image(folded_green);
    report "pkguse strength " & to_string(strength(std_ulogic_vector'("01ZHL-")));
    report "pkguse hstring " & to_hstring(std_ulogic_vector'("01Z1ZZZZ"));
    report "pkguse resolved " & std_ulogic'image(resolved(std_ulogic_vector'("Z1")));
    wait;
  end process;
end architecture;
