-- SPDX-License-Identifier: Apache-2.0
-- Driver transaction and value-change callbacks (IEEE 1076-2019 21.3.2.2,
-- 21.3.2.5). A driver is one scalar subelement's (14.7.2): an assignment to
-- part of a composite signal is a transaction of the drivers of that part
-- only, and a driver whose value does not change has a transaction but no
-- value change (14.7.3.1). Two processes drive a resolved vector, one
-- through a procedure's signal parameter; one drives a record array by
-- parts; one projects transport waveforms of unchanged values on
-- unresolved signals.
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  type rec is record
    f : std_logic;
    g : std_logic_vector(1 downto 0);
  end record;
  type recarr is array (0 to 1) of rec;
  signal r : recarr := (('0', "00"), ('0', "00"));
  signal v : std_logic_vector(3 downto 0) := "0000";
  signal t : bit_vector(1 downto 0) := "00";
  signal i : integer := 0;
  procedure setp (signal s : out std_logic_vector; x : in std_logic_vector) is
  begin
    s <= x;
  end procedure;
begin
  pr : process is
  begin
    wait for 1 ns;
    r(1) <= ('1', "10");
    r(0).g <= "01";
    wait for 1 ns;
    r <= (('1', "01"), ('1', "10"));
    wait;
  end process;
  pv1 : process is
  begin
    v <= "ZZZZ";
    wait for 1 ns;
    setp(v, "1Z0Z");
    wait for 1 ns;
    v(1 downto 0) <= "11" after 500 ps;
    wait;
  end process;
  pv2 : process is
  begin
    v <= "ZZZZ";
    wait for 1500 ps;
    v <= transport "ZZZ0" after 1 ns, "ZZZ1" after 2 ns;
    wait;
  end process;
  pt : process is
  begin
    t <= "01" after 1 ns, "10" after 2 ns, "10" after 3 ns;
    i <= 5 after 1 ns, 5 after 2 ns, 7 after 3 ns;
    wait;
  end process;
end architecture;
