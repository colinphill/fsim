-- fsim: top=vhdl_batch57_array_segment_context_20261013(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_batch57_array_segment_context_20261013 is
end entity;

architecture test of vhdl_batch57_array_segment_context_20261013 is
    type byte_rows is array (2 downto 0) of bit_vector(7 downto 0);
    type integer_array_t is array (positive range <>) of integer;
    function has_integer_vector_shape(value : integer_vector)
        return boolean is
    begin
        return value'left = 0 and value'right = 1 and value'ascending
            and value(0) = 1 and value(1) = 2;
    end function;
begin
    check: process is
        procedure check_shadowed_builtin is
            type integer_vector is array (integer range <>) of integer;
            variable shadowed_builtin : integer_vector(1 to 4);
        begin
            shadowed_builtin :=
                (integer_vector'(11, 12), integer_vector'(13, 14));
            assert shadowed_builtin(1) = 11
                    and shadowed_builtin(2) = 12
                    and shadowed_builtin(3) = 13
                    and shadowed_builtin(4) = 14
                report "qualified shadowing array segments lost their nominal type"
                severity failure;
            assert shadowed_builtin = (11, 12, 13, 14)
                report "a local integer_vector shadow lost comparison context"
                severity failure;
            shadowed_builtin := integer_vector'(
                integer_vector'(21, 22), integer_vector'(23, 24));
            assert shadowed_builtin(1) = 21
                    and shadowed_builtin(2) = 22
                    and shadowed_builtin(3) = 23
                    and shadowed_builtin(4) = 24
                report "nested local qualified array segments lost their shape"
                severity failure;
        end procedure;
        variable flat : integer_vector(1 to 4);
        variable flat_six : integer_vector(1 to 6);
        variable prefix : integer_vector(1 to 2);
        variable rows : byte_rows := (X"03", X"02", X"01");
        variable byte : bit_vector(7 downto 0) := X"ff";
        variable segmented : integer_array_t(1 to 4);
    begin
        prefix := (5, 6);
        flat := (prefix, 9, 8);
        assert flat = (5, 6, 9, 8)
            report "typed positional array segment lost its elements"
            severity failure;

        flat := (integer_vector'(1, 2), integer_vector'(3, 4));
        assert flat = (1, 2, 3, 4)
            report "qualified predefined integer_vector segments lost their shape"
            severity failure;
        assert has_integer_vector_shape(integer_vector'(1, 2))
            report "qualified integer_vector bounds or direction were lost"
            severity failure;

        flat_six := (9, 8, (integer_vector'((1, 2), (3, 4))));
        assert flat_six = (9, 8, 1, 2, 3, 4)
            report "nested qualified positional segments lost their cardinality"
            severity failure;

        check_shadowed_builtin;

        flat := (9, 8, (1, 2));
        assert flat = (9, 8, 1, 2)
            report "untyped positional nested aggregate was not flattened"
            severity failure;

        assert flat /= (prefix, 9)
            report "different positional aggregate lengths compared equal"
            severity failure;

        segmented := (1, 2, 3, 4);
        assert integer_array_t'(segmented(1 to 2), segmented(3 to 4))
            = integer_array_t'(1, 2, 3, 4)
            report "unconstrained aggregate slice segments lost their shape"
            severity failure;

        rows := (rows(1 downto 0), byte);
        assert rows = (X"02", X"01", X"ff")
            report "array-valued segment order was not preserved"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
