-- SPDX-License-Identifier: Apache-2.0
-- A package function called from two processes of one architecture (IEEE
-- 1076-2019 14.6 b): the body the second process runs is the first process's
-- recording, replayed, and each call object of either process reads the
-- formal and the variable of the frame its own site runs. In the second
-- process two sites run the frame of variable actuals and one runs the frame
-- of a literal first actual.
package fpkg is
  function g(a : integer; b : integer) return integer;
end package;

package body fpkg is
  function g(a : integer; b : integer) return integer is
    variable t : integer := a * 10;
  begin
    t := t + b;
    return t;
  end function;
end package body;

use work.fpkg.all;

entity top is
end entity;

architecture rtl of top is
begin
  first : process is
    variable r : integer;
    procedure q(n : integer) is
    begin
      r := g(n, 4);
    end procedure;
  begin
    q(3);
    wait;
  end process;

  second : process is
    variable r : integer;
    procedure q(n : integer) is
    begin
      r := g(n, 5);
      r := g(n + 1, 6);
      r := g(2, n);
    end procedure;
  begin
    wait for 1 ns;
    q(7);
    wait;
  end process;
end architecture;
