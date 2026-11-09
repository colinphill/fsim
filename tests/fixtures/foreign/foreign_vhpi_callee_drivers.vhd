-- SPDX-License-Identifier: Apache-2.0
-- A process that calls a procedure holds a driver for each signal the
-- procedure's body assigns directly, and for those the procedures it calls
-- assign (IEEE 1076-2019 14.7.2); a formal parameter of the procedure that
-- has the name of a signal in the caller's scope is no such signal. The
-- signals out signal parameters are associated with are drivers of the
-- caller too (4.2.2.3). `direct` assigns `a`, `outer` calls it and assigns
-- its formal `s`, the actual of which is `c` (the architecture's own signal
-- `s` is not driven).
entity top is
end entity;

architecture t of top is
  signal a, c, s : bit;
  procedure direct is
  begin
    a <= '1';
  end procedure;
  procedure outer(signal s : out bit) is
  begin
    direct;
    s <= '1';
  end procedure;
begin
  p1 : process is
  begin
    outer(c);
    wait;
  end process;
  p2 : process is
  begin
    wait;
  end process;
end architecture;
