-- SPDX-License-Identifier: Apache-2.0
-- Forcing a variable a subprogram call elaborated (IEEE 1076-2019 14.6, 22.5.2):
-- the force lasts until that activation returns. A recursive procedure is
-- forced in its second activation: the activation it calls starts with
-- unforced variables, the forced one is forced again when that call returns,
-- and the next call of the procedure starts clean.
entity top is
end entity;

architecture rtl of top is
begin
  main : process is
    variable total : integer := 0;
    function leaf(x : integer) return integer is
    begin
      return x + 5;
    end function;
    procedure down(n : integer) is
      variable depth : integer := n * 10;
    begin
      depth := depth + 1;
      if n > 1 then
        down(n - 1);
      else
        depth := leaf(depth);
      end if;
      total := total + depth;
    end procedure;
  begin
    for k in 0 to 1 loop
      down(3 - 2 * k);
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;
