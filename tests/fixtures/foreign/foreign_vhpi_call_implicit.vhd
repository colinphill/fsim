-- SPDX-License-Identifier: Apache-2.0
-- Subprogram calls the simulation cycle makes (IEEE 1076-2019 14.7.3.2: the
-- resolution function of a resolved signal) and calls in a concurrent
-- statement (11.4, 11.6): the statements of the resolution function body are
-- executed by the cycle, not by a process, so no call object and no stack
-- frame stand for the call (19.10, Figure 35: a frame belongs to a subpCall),
-- and the variables of its body have no dynamically elaborated instance to
-- read (19.4.3). A function called by a concurrent assignment has a call
-- object and a frame, which a probe in its body reads.
entity top is
end entity;

architecture rtl of top is
  type int_array is array (natural range <>) of integer;
  function sum_i (v : int_array) return integer is
    variable total : integer := 0;
  begin
    for i in v'range loop
      total := total + v(i);
    end loop;
    return total;
  end function;
  function triple (x : integer) return integer is
    variable t : integer := x * 3;
  begin
    t := t + 1;
    return t;
  end function;
  signal b : sum_i integer := 0;
  signal a : integer := 2;
  signal y : integer;
begin
  y <= triple(a);
  d1 : b <= 1;
  d2 : b <= 2;
  stim : process is
  begin
    wait for 1 ns;
    a <= 5;
    wait;
  end process;
end architecture;
