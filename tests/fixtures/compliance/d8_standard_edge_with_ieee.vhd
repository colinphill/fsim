-- fsim: top=d8_standard_edge_with_ieee(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.3, 12.5: RISING_EDGE and FALLING_EDGE of BIT and BOOLEAN
-- are declared in STANDARD; with IEEE.STD_LOGIC_1164 also visible, the call
-- on a BIT or BOOLEAN signal is decided by overload resolution and denotes
-- the STANDARD function, not the STD_ULOGIC one.
library ieee;
use ieee.std_logic_1164.all;

entity d8_standard_edge_with_ieee is
end entity;

architecture test of d8_standard_edge_with_ieee is
    signal sa : bit := '0';
    signal sb : boolean := true;
begin
    process
    begin
        sa <= '1';
        sb <= false;
        wait for 0 ns;
        assert rising_edge(sa) severity failure;
        assert not falling_edge(sa) severity failure;
        assert falling_edge(sb) severity failure;
        assert not rising_edge(sb) severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
