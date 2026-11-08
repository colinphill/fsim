-- SPDX-License-Identifier: Apache-2.0
-- Subprogram call stack frames (IEEE 1076-2019 14.6, 19.10).
entity top is
end entity;

architecture rtl of top is
  signal sa : bit;
  signal so : bit;
begin
  sa <= '1';
  main : process is
    function leaf(x : integer) return integer is
    begin
      return x + 1;
    end function;
    procedure down(n : integer) is
      variable depth : integer := n * 10;
    begin
      depth := depth + 1;
      if n > 1 then
        down(n - 1);
      else
        depth := leaf(n);
      end if;
    end procedure;
    procedure drive(signal a : in bit; signal o : out bit) is
    begin
      o <= a;
    end procedure;
  begin
    down(3);
    drive(sa, so);
    wait;
  end process;

  -- A function's signal parameter is of mode in and denotes its actual for
  -- the call (IEEE 1076-2019 4.2.2.3, 19.12.2.2 c).
  fp : process is
    function sample(signal s : in bit) return bit is
    begin
      return s;
    end function;
    variable v : bit;
  begin
    wait for 1 ns;
    v := sample(sa);
    wait;
  end process;
end architecture;
