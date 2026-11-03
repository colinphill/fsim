-- SPDX-License-Identifier: Apache-2.0
-- Statement, sensitivity and subprogram call callbacks (IEEE 1076-2019 21.3).
entity top is
end entity;

architecture rtl of top is
  signal clk : bit := '0';
begin
  clock : process is
  begin
    for k in 1 to 3 loop
      clk <= not clk;
      wait for 5 ns;
    end loop;
    wait;
  end process;

  watch : process (clk) is
    variable n : integer := 0;
  begin
    n := n + 1;
    case n is
      when 2 =>
        n := n + 10;
      when others =>
        null;
    end case;
  end process;

  counter : process is
    variable i : integer := 0;
  begin
    while i < 2 loop
      i := i + 1;
    end loop;
    wait;
  end process;

  -- vhpiCbStartOfSubpCall and vhpiCbEndOfSubpCall (21.3.4.5, 21.3.4.6).
  calls : process is
    procedure bump(variable x : inout integer) is
    begin
      x := x + 1;
    end procedure;
    variable c : integer := 0;
  begin
    bump(c);
    wait for 7 ns;
    bump(c);
    wait;
  end process;

  -- Function calls, subprogram bodies, elsif and else branches (21.3.4).
  fn : process is
    function inc(x : integer) return integer is
    begin
      return x + 1;
    end function;
    variable v : integer := 0;
  begin
    for n in 1 to 2 loop
      v := inc(v + n);
      if v > 5 then
        null;
      elsif v > 3 then
        null;
      else
        null;
      end if;
    end loop;
    wait;
  end process;

  -- A concurrent procedure call, and a signal for the sensitivity set.
  blk : block is
    signal sg : bit := '0';
    procedure tick(constant d : in integer) is
    begin
      null;
    end procedure;
  begin
    cc : tick(2);
    toggle : process is
    begin
      wait for 7 ns;
      sg <= '1';
      wait;
    end process;
  end block;
end architecture;
