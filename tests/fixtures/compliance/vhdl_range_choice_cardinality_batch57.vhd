-- fsim: top=vhdl_range_choice_cardinality_batch57(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity range_choice_pad is
    generic (width : positive);
    port (
        x : out bit_vector(31 downto 0);
        y : in bit_vector(width - 1 downto 0) );
end entity;

architecture test of range_choice_pad is
    signal sampled : bit_vector(width - 1 downto 0);
begin
    sampled <= y;
    x <= (31 downto width => '0') & sampled;
end architecture;

entity vhdl_range_choice_cardinality_batch57 is
end entity;

architecture test of vhdl_range_choice_cardinality_batch57 is
    type row_t is array (3 downto 0) of bit;
    type matrix_t is array (natural range <>) of row_t;
    signal input8 : bit_vector(7 downto 0) := X"55";
    signal padded8 : bit_vector(31 downto 0);
    signal input12 : bit_vector(11 downto 0) := X"ABC";
    signal padded12 : bit_vector(31 downto 0);
begin
    pad8 : entity work.range_choice_pad
        generic map (width => 8)
        port map (x => padded8, y => input8);
    pad12 : entity work.range_choice_pad
        generic map (width => 12)
        port map (x => padded12, y => input12);

    check : process is
        variable descending : bit_vector(7 downto 0);
        variable ascending : bit_vector(7 downto 0);
        variable a : bit_vector(11 downto 0);
        variable b : bit_vector(11 downto 0);
        variable joined : bit_vector(31 downto 0);
        variable mixed_scalar_choices : bit_vector(11 downto 0);
        variable rows : matrix_t(0 to 7);
    begin
        descending := (7 downto 4 => '0') & X"C";
        ascending := (4 to 7 => '0') & X"C";
        assert descending = X"0C" and ascending = X"0C"
            report "static ascending or descending range width was wrong"
            severity failure;

        -- Scalar-valued range choices do not choose an inferred aggregate
        -- direction. BIT_VECTOR inherits NATURAL's ascending direction,
        -- even though the target is descending and the choices mix directions.
        mixed_scalar_choices := (
            0 to 3 => '1',
            7 downto 4 => '0'
        ) & X"C";
        assert mixed_scalar_choices = X"F0C"
            report "scalar range-choice direction overrode the base index subtype"
            severity failure;

        a := X"FAE";
        b := X"182";
        joined := (15 downto 12 => a(11), 11 downto 0 => a)
            & (15 downto 12 => b(11), 11 downto 0 => b);
        assert joined = X"FFAE0182"
            report "array-valued range choice was counted per index"
            severity failure;

        -- Concatenation appends the right operand after the left operand.
        rows := (3 downto 0 => row_t'('1', '0', '1', '0'))
            & (3 downto 0 => row_t'('0', '1', '0', '1'));
        for index in 0 to 3 loop
            assert rows(index) = row_t'('1', '0', '1', '0')
                    and rows(index + 4) = row_t'('0', '1', '0', '1')
                report "array-valued elements were treated as array slices"
                severity failure;
        end loop;

        wait for 1 ns;
        assert padded8 = X"00000055"
            report "generic range cardinality failed for width 8"
            severity failure;
        assert padded12 = X"00000ABC"
            report "generic range cardinality failed for width 12"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
