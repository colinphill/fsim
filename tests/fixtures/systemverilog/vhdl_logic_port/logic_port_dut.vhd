-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

-- Standard-logic functions on in ports. Driven from SystemVerilog, the ports
-- alias four-state nets; the functions see the std_ulogic values 'X', '0',
-- '1' and 'Z' exactly as they would from a VHDL driver.
entity logic_port_dut is
    port (
        clk : in std_logic;
        v : in std_logic_vector(3 downto 0));
end entity;

architecture rtl of logic_port_dut is
    signal b : bit;
    signal r : std_logic;
    signal bv : bit_vector(3 downto 0);
begin
    b <= to_bit(clk);
    r <= and v;
    bv <= to_bitvector(v);
    process (b, clk, v, r, bv)
    begin
        report "b=" & bit'image(b) & " is_x=" & boolean'image(is_x(clk))
            & " x01=" & std_logic'image(to_x01(clk))
            & " ux01=" & std_logic'image(to_ux01(v(3)))
            & " and=" & std_logic'image(r)
            & " bv0=" & bit'image(bv(0)) & " bv3=" & bit'image(bv(3))
            -- Elements of the functions' results, whose index ranges are
            -- S'LENGTH-1 downto 0 (TO_BITVECTOR) and 1 to S'LENGTH (TO_X01).
            & " i0=" & bit'image(to_bitvector(v)(0))
            & " i3=" & bit'image(to_bitvector(v)(3))
            & " m1=" & std_logic'image(to_x01(v)(1))
            & " m4=" & std_logic'image(to_x01(v)(4));
    end process;
end architecture;
