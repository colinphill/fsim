-- SPDX-License-Identifier: Apache-2.0
-- Subprogram call stack frames (IEEE 1076-2019 14.6, 19.10).
entity top is
end entity;

architecture rtl of top is
begin
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
  begin
    down(3);
    wait;
  end process;
end architecture;
