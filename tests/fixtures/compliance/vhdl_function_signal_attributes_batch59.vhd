-- fsim: top=vhdl_function_signal_attributes_batch59(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Signal-class function formals retain the identity and bounds of each actual.
entity vhdl_function_signal_attributes_batch59 is
end entity;

architecture sim of vhdl_function_signal_attributes_batch59 is
  signal first_value : bit_vector(0 to 2) := "000";
  signal second_value : bit_vector(0 to 2) := "000";
  signal descending_value : bit_vector(3 downto 1) := "000";

  function event_now(signal value : in bit_vector) return boolean is
  begin
    return value'event;
  end function;

  function previous_value(signal value : in bit_vector) return bit_vector is
  begin
    return value'last_value;
  end function;

  function left_bound(signal value : in bit_vector) return integer is
  begin
    return value'left;
  end function;

  function right_bound(signal value : in bit_vector) return integer is
  begin
    return value'right;
  end function;
begin
  first_value <= "101" after 1 ns;
  second_value <= "011" after 1 ns, "110" after 2 ns;
  descending_value <= "111" after 1 ns;

  check : process
    variable failures : natural := 0;
  begin
    wait for 1 ns;
    if not event_now(first_value)
      or not event_now(second_value)
      or not event_now(descending_value) then
      failures := failures + 1;
    end if;
    if previous_value(first_value) /= "000"
      or previous_value(second_value) /= "000"
      or previous_value(descending_value) /= "000" then
      failures := failures + 1;
    end if;
    if left_bound(first_value) /= 0
      or right_bound(first_value) /= 2
      or left_bound(descending_value) /= 3
      or right_bound(descending_value) /= 1 then
      failures := failures + 1;
    end if;

    wait for 1 ns;
    if event_now(first_value)
      or not event_now(second_value)
      or event_now(descending_value) then
      failures := failures + 1;
    end if;
    -- The two ascending actuals have the same width and range. Their
    -- different histories prove that the cached function frame names each
    -- exact signal, while the descending actual checks range transfer.
    if previous_value(first_value) /= "000"
      or previous_value(second_value) /= "011"
      or previous_value(descending_value) /= "000" then
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
