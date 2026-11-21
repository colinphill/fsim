-- fsim: top=d8_alias_of_alias_signature(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 6.6.3: an alias of a subprogram name denotes the
-- subprogram its signature selects. TO_HEX_STRING is itself a set of aliases
-- (STD_LOGIC_1164, NUMERIC_STD), so the signature of the local alias must
-- select through every alias on the way, and the call h(foo) must resolve.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity d8_alias_of_alias_signature is
end entity;

architecture test of d8_alias_of_alias_signature is
    subtype sv is std_ulogic_vector;
    signal foo : std_ulogic_vector(7 downto 0) := x"ab";
begin
    process
        alias h is to_hex_string [sv return string];
    begin
        assert h(foo) = "AB" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
