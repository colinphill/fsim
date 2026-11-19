-- SPDX-License-Identifier: Apache-2.0
-- TIME'VALUE and DELAY_LENGTH'VALUE (IEEE 1076-2019 16.2.2, 5.2.4.2): the
-- string is a TIME physical literal with optional leading and trailing
-- blanks; the result counts simulation ticks, and a value whose absolute
-- value is below the resolution limit is truncated to zero time units
-- (toward zero), never an error. A string that is not a TIME literal is an
-- error.

-- Resolution 1 ps: units of a ps and above are exact, finer ones truncate.
entity time_value_ps is
end entity;

architecture test of time_value_ps is
begin
  process
    variable t : time;
    variable d : delay_length;
  begin
    t := time'value("10 ns");
    report "a1 " & integer'image(t / 1 ps);
    t := time'value(" 1.5 us ");
    report "a2 " & integer'image(t / 1 ps);
    t := time'value("2 ps");
    report "a3 " & integer'image(t / 1 ps);
    d := delay_length'value("3 ns");
    report "a4 " & integer'image(d / 1 ps);
    t := time'value("1 fs");
    report "a5 " & integer'image(t / 1 ps);
    t := time'value("1500 fs");
    report "a6 " & integer'image(t / 1 ps);
    t := time'value("-1500 fs");
    report "a7 " & integer'image(t / 1 ps);
    t := time'value("999 fs");
    report "a8 " & integer'image(t / 1 ps);
    t := time'value("2 US");
    report "a9 " & integer'image(t / 1 ps);
    wait;
  end process;
end architecture;

-- A unit that TIME does not have.
entity time_value_bad is
end entity;

architecture test of time_value_bad is
begin
  process
    variable t : time;
  begin
    t := time'value("5 parsecs");
    report "not reached";
    wait;
  end process;
end architecture;

-- A string that is not a physical literal at all.
entity time_value_syntax is
end entity;

architecture test of time_value_syntax is
begin
  process
    variable t : time;
  begin
    t := time'value("ns ns");
    report "not reached";
    wait;
  end process;
end architecture;
