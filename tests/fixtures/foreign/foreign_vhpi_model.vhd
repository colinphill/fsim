-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  type color is (red, green, blue);
  constant k : integer := 7;
  signal v : std_logic_vector(7 downto 0) := "10110010";
  signal c : color := green;
  signal r : real := 1.5;
  alias hi : std_logic_vector(3 downto 0) is v(7 downto 4);
  -- Declarations without a value (IEEE 1076-2019 19.4, Figure 14).
  function twice(x : integer) return integer is
  begin
    return 2 * x;
  end function;
  function twice(x : real) return real is
  begin
    return 2.0 * x;
  end function;
  procedure nop is
  begin
  end procedure;
  component leaf is
    port (a : in std_logic);
  end component;
  attribute note : string;
  attribute note of v : signal is "data";
  group pair is (signal, signal);
  group vr : pair (v, r);
begin
  v <= "10110010";
  c <= green;
  r <= 1.5;
end architecture;
