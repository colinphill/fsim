-- fsim: top=vhdl_nested_open_array_aggregate_batch59(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Exercise bounds and packed widths for open arrays with array-valued elements.
entity vhdl_nested_open_array_aggregate_batch59 is
end entity;

architecture test of vhdl_nested_open_array_aggregate_batch59 is
    type row_t is array (natural range <>) of integer;
    type rows_t is array (natural range <>) of row_t;
    type bit_rows_t is array (natural range <>) of bit_vector;
    type string_rows_t is array (natural range <>) of string;
    type matrix_t is array (natural range <>, natural range <>) of integer;

    function make_bit_rows return bit_rows_t is
    begin
        return (X"4455", X"6677");
    end function;

    procedure check_bit_rows(constant value : in bit_rows_t) is
    begin
        assert value'left = 0 and value'right = 1 and value'ascending
            report "open bit-vector rows lost their outer index shape"
            severity failure;
        assert value(0)'left = 0 and value(0)'right = 15
                and value(0)'ascending
            report "function-returned bit-string row bounds were not positional"
            severity failure;
        assert value(0) = X"4455" and value(1) = X"6677"
            report "open bit-vector row widths or values were lost"
            severity failure;
    end procedure;

    signal rows : rows_t(0 to 1)(5 to 6) := (
        0 => (5 => 11, 6 => 12),
        1 => (5 => 21, 6 => 22)
    );
    -- Signal objects must constrain their array subelements (1076-2008 5.3.2.2).
    -- The function result and procedure formal above retain open row bounds.
    signal bit_rows : bit_rows_t(0 to 1)(15 downto 0) := (
        0 => X"4455",
        1 => X"6677"
    );
    signal strings : string_rows_t(0 to 1)(1 to 4) := (
        0 => "ABCD",
        1 => "WXYZ"
    );
    signal matrix : matrix_t(0 to 1, 5 to 6) := (
        (5 => 31, 6 => 32),
        (5 => 41, 6 => 42)
    );
begin
    check: process is
    begin
        rows <= (0 => (5 => 11, 6 => 12), 1 => (5 => 21, 6 => 22));
        bit_rows <= (X"4455", X"6677");
        strings <= (0 => "ABCD", 1 => "WXYZ");
        matrix <= ((5 => 31, 6 => 32), (5 => 41, 6 => 42));
        check_bit_rows(make_bit_rows);
        wait for 1 ns;
        assert rows = (0 => (5 => 11, 6 => 12), 1 => (5 => 21, 6 => 22))
            report "nested open array bounds or row values were lost"
            severity failure;
        assert bit_rows = (0 => X"4455", 1 => X"6677")
            report "open bit-vector element widths were not inferred from rows"
            severity failure;
        assert strings = (0 => "ABCD", 1 => "WXYZ")
            report "open string element widths were not contextually retained"
            severity failure;
        assert matrix = ((5 => 31, 6 => 32), (5 => 41, 6 => 42))
            report "multidimensional named row choices lost their bounds"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
