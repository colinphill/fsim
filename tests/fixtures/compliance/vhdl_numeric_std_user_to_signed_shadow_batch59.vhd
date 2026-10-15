-- fsim: top=vhdl_numeric_std_user_to_signed_shadow_batch59(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Control: a local homograph named to_signed keeps its own value semantics.
entity vhdl_numeric_std_user_to_signed_shadow_batch59 is
end entity;

library ieee;
use ieee.numeric_std.all;

architecture test of vhdl_numeric_std_user_to_signed_shadow_batch59 is
    function to_signed(value : integer; size : natural) return signed is
        variable result_value : signed(size - 1 downto 0);
    begin
        result_value := (others => '0');
        return result_value;
    end function;
    constant shadowed_value : signed(7 downto 0) := to_signed(1, 8);
begin
    check: process
    begin
        assert shadowed_value = "00000000"
            report "local to_signed homograph was replaced by numeric_std"
            severity failure;
        report "PASS" severity note;
        wait;
    end process;
end architecture;
