-- fsim: top=vhdl_entity_generic_actual_range_batch58_20261013(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
package generic_actual_range_pkg is
    type enable_vec_t is array (integer range <>) of boolean;
    type word_vec_t is array (integer range <>) of bit_vector(1 downto 0);
end package;

use work.generic_actual_range_pkg.all;
entity generic_actual_range_worker is
    generic (
        G_ENABLES : enable_vec_t;
        G_VALUES : word_vec_t(G_ENABLES'range)
            := (others => (others => '0'));
        G_SCALAR : integer := 7
    );
    port (values : out word_vec_t);
end entity;

use work.generic_actual_range_pkg.all;
architecture rtl of generic_actual_range_worker is
begin
    assert G_SCALAR = 7
        report "a targetless predefined scalar generic lost its default"
        severity failure;
    assert G_VALUES'left = G_ENABLES'left
        and G_VALUES'right = G_ENABLES'right
        and G_VALUES'ascending = G_ENABLES'ascending
        and G_VALUES'length = G_ENABLES'length
        report "a dependent generic default lost the earlier generic's range"
        severity failure;
    values <= G_VALUES;
end architecture;

use work.generic_actual_range_pkg.all;
entity generic_actual_range_null_worker is
    generic (
        G_ENABLES : enable_vec_t;
        G_VALUES : word_vec_t(G_ENABLES'range)
            := (others => (others => '0'));
        G_EXPECT_LEFT : integer;
        G_EXPECT_RIGHT : integer;
        G_EXPECT_ASCENDING : boolean
    );
end entity;

use work.generic_actual_range_pkg.all;
architecture check_null_generic_range of generic_actual_range_null_worker is
begin
    assert G_ENABLES'left = G_EXPECT_LEFT
        and G_ENABLES'right = G_EXPECT_RIGHT
        and G_ENABLES'ascending = G_EXPECT_ASCENDING
        and G_ENABLES'length = 0
        report "a null generic actual lost its bounds or direction"
        severity failure;
    assert G_VALUES'left = G_EXPECT_LEFT
        and G_VALUES'right = G_EXPECT_RIGHT
        and G_VALUES'ascending = G_EXPECT_ASCENDING
        and G_VALUES'length = 0
        report "a null dependent generic default lost its bounds or direction"
        severity failure;
end architecture;

use work.generic_actual_range_pkg.all;
entity vhdl_entity_generic_actual_range_batch58_20261013 is
end entity;

use work.generic_actual_range_pkg.all;
architecture sim of vhdl_entity_generic_actual_range_batch58_20261013 is
    signal targetless_integer : integer := 7;
    constant enable_ascending : enable_vec_t(2 to 3)
        := (true, false);
    constant enable_descending : enable_vec_t(9 downto 7)
        := (true, false, true);
    constant enable_override : enable_vec_t(5 downto 4)
        := (false, true);
    constant enable_null : enable_vec_t(3 downto 4)
        := (others => false);
    constant enable_null_ascending : enable_vec_t(4 to 3)
        := (others => false);
    constant explicit_values : word_vec_t(5 downto 4)
        := ("10", "01");
    signal ascending_values : word_vec_t(2 to 3);
    signal descending_values : word_vec_t(9 downto 7);
    signal override_values : word_vec_t(5 downto 4);
begin
    ascending_default : entity work.generic_actual_range_worker
        generic map (G_ENABLES => enable_ascending)
        port map (values => ascending_values);

    descending_default : entity work.generic_actual_range_worker
        generic map (G_ENABLES => enable_descending)
        port map (values => descending_values);

    descending_override : entity work.generic_actual_range_worker
        generic map (
            G_ENABLES => enable_override,
            G_VALUES => explicit_values
        )
        port map (values => override_values);

    null_default : entity work.generic_actual_range_null_worker
        generic map (
            G_ENABLES => enable_null,
            G_EXPECT_LEFT => 3,
            G_EXPECT_RIGHT => 4,
            G_EXPECT_ASCENDING => false
        );

    null_ascending_default : entity work.generic_actual_range_null_worker
        generic map (
            G_ENABLES => enable_null_ascending,
            G_EXPECT_LEFT => 4,
            G_EXPECT_RIGHT => 3,
            G_EXPECT_ASCENDING => true
        );

    check : process is
    begin
        wait for 1 ns;
        assert targetless_integer = 7
            report "a targetless predefined scalar subtype was not retained"
            severity failure;
        assert ascending_values'left = 2
            and ascending_values'right = 3
            and ascending_values'ascending
            and ascending_values(2) = "00"
            and ascending_values(3) = "00"
            report "ascending generic actual did not size the default"
            severity failure;
        assert descending_values'left = 9
            and descending_values'right = 7
            and not descending_values'ascending
            and descending_values(9) = "00"
            and descending_values(8) = "00"
            and descending_values(7) = "00"
            report "descending generic actual did not size the default"
            severity failure;
        assert override_values'left = 5
            and override_values'right = 4
            and not override_values'ascending
            and override_values(5) = "10"
            and override_values(4) = "01"
            report "an explicit dependent generic actual was not preserved"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
