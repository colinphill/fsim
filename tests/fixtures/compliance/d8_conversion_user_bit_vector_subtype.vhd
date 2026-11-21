-- fsim: top=d8_conversion_user_bit_vector_subtype(tb) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.6: a slice of an object of a user subtype of BIT_VECTOR
-- is an array of BIT, closely related to a NUMERIC_BIT UNSIGNED of the same
-- length.
library ieee;
use ieee.numeric_bit.all;

entity d8_conversion_user_bit_vector_subtype is
end entity;

architecture tb of d8_conversion_user_bit_vector_subtype is
    subtype t_vec is bit_vector(33 downto 0);
begin
    process
        variable x : t_vec := (33 downto 32 => "10", others => '0');
        variable n : natural;
    begin
        n := to_integer(unsigned(x(33 downto 32)));
        assert n = 2 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
