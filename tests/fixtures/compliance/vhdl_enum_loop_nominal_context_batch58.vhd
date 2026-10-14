-- fsim: top=vhdl_enum_loop_nominal_context_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Scoped enum identities resolve a common overloaded discrete-range type and type-prefix attributes.
entity vhdl_enum_loop_nominal_context_batch58 is
end entity;

architecture sim of vhdl_enum_loop_nominal_context_batch58 is
  type enum_left is (a, b);
  type enum_common is (b, c);
  type enum_right is (c, d);
begin
  test : process
    type colors is (red, green, blue);
    type color_pointer is access colors;
    variable pointer : color_pointer := new colors'(blue);
    variable count : integer := 0;
    variable encoded : integer := 0;
  begin
    for item in c downto b loop
      count := count + 1;
      encoded := encoded * 10 + enum_common'pos(item) + 1;
    end loop;
    assert count = 2 and encoded = 21
      report "overloaded range did not use the unique common enum type"
      severity failure;

    count := 0;
    for item in colors'succ(colors'low) to colors'high loop
      count := count + 1;
    end loop;
    assert count = 2
      report "enum type-prefix attributes did not form an ascending range"
      severity failure;

    count := 0;
    for item in colors'pred(colors'high) downto colors'low loop
      count := count + 1;
    end loop;
    assert count = 2
      report "enum type-prefix attributes did not form a descending range"
      severity failure;

    assert colors'pos(colors'high) = 2
        and colors'succ(colors'low) = green
        and colors'pred(pointer.all) = green
      report "enum attributes lost their nominal type or access value"
      severity failure;

    report "PASS"
      severity note;
    wait;
  end process;
end architecture;
