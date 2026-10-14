-- fsim: top=vhdl_string_slice_null_outside_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- A null STRING slice outside the base range selects no bytes, but its
-- right-hand impure function is still evaluated exactly once.
entity vhdl_string_slice_null_outside_batch57 is
end entity;

architecture sim of vhdl_string_slice_null_outside_batch57 is
    type call_counter_t is protected
        procedure increment;
        impure function value return natural;
    end protected;

    type call_counter_t is protected body
        variable calls : natural := 0;

        procedure increment is
        begin
            calls := calls + 1;
        end procedure;

        impure function value return natural is
        begin
            return calls;
        end function;
    end protected body;

    shared variable rhs_calls : call_counter_t;

    impure function empty_rhs return string is
    begin
        rhs_calls.increment;
        return "";
    end function;
begin
    check : process is
        variable text : string(1 to 6) := "ABCDEF";
    begin
        text(9 to 8) := empty_rhs;

        assert rhs_calls.value = 1
            report "empty RHS for an out-of-range null slice was not evaluated once"
            severity failure;
        assert text = "ABCDEF"
            report "out-of-range null STRING slice changed the base value"
            severity failure;
        assert false report "PASS" severity note;
        wait;
    end process;
end architecture;
