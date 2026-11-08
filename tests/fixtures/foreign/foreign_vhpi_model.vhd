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
  type volts is range -1.0 to 2.5;
  subtype unit_volts is volts range 0.0 to 1.0;
  subtype resolved_bit is resolved std_ulogic;
  type quad is array (0 to 3) of std_logic;
  type int_file is file of integer;
  file fi : int_file;
  procedure drive(signal s : out std_logic; signal t : out bit) is
  begin
    null;
  end procedure;
  -- vhpiLogicalNameP of this file declaration is its literal "vhpi_model_out.dat"
  -- (6.4.2.5, 19.4.3 Figure 10). vhpiEntityClassP of the attribute
  -- specification of NOTE above is vhpiSignalEC = 1010 (7.2, 23.10).
  file fo : int_file open write_mode is "vhpi_model_out.dat";
begin
  v <= "10110010";
  c <= green;
  r <= 1.5;
end architecture;
