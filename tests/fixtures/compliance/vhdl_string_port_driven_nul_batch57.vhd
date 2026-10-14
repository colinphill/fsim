-- fsim: top=vhdl_string_port_driven_nul_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A child STRING port must read the live mapped signal value, including NUL,
-- after the parent drives it; unconstrained formals retain the actual bounds.
entity vhdl_string_port_driven_child_batch57 is
  port (value : in string);
end entity;

architecture verify of vhdl_string_port_driven_child_batch57 is
  procedure check_text(
    actual : in string;
    expected_first : in character;
    expected_last : in character;
    result : out boolean) is
  begin
    result := actual'left = 3
      and actual'right = 2
      and actual'length = 2
      and actual(actual'left) = expected_first
      and actual(actual'right) = expected_last;
  end procedure;

  procedure check_empty(actual : in string; result : out boolean) is
  begin
    result := actual'left = 3
      and actual'right = 4
      and actual'length = 0;
  end procedure;
begin
  check : process
    variable result : boolean;
  begin
    if value'length = 0 then
      check_empty(value, result);
      assert result
        report "FAIL null STRING bounds changed through procedure formal"
        severity failure;
      assert value'left = 3 and value'right = 4
        report "FAIL child null STRING port lost its exact bounds"
        severity failure;
      assert value'ascending = false and value'length = 0
        report "FAIL child null STRING port lost direction or length"
        severity failure;
    else
      wait for 2 ns;
      check_text(value, character'val(0), 'A', result);
      assert result
        report "FAIL unconstrained STRING formal lost the driven NUL value or bounds"
        severity failure;
      assert value'left = 3 and value'right = 2
        report "FAIL child STRING port lost descending bounds"
        severity failure;
      assert value'ascending = false and value'length = 2
        report "FAIL child STRING port lost direction or length"
        severity failure;
      assert value(3) = character'val(0) and value(2) = 'A'
        report "FAIL child STRING port did not read driven NUL bytes"
        severity failure;
    end if;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;

entity vhdl_string_port_driven_nul_batch57 is
end entity;

architecture sim of vhdl_string_port_driven_nul_batch57 is
  signal driven : string(3 downto 2) := "xy";
  signal empty_driven : string(3 downto 4);
  component vhdl_string_port_driven_child_batch57 is
    port (value : in string);
  end component;
begin
  reader : vhdl_string_port_driven_child_batch57 port map (
    value => driven);
  empty_reader : vhdl_string_port_driven_child_batch57 port map (
    value => empty_driven);

  drive : process
  begin
    wait for 1 ns;
    driven(3) <= character'val(0);
    driven(2) <= 'A';
    wait;
  end process;
end architecture;
