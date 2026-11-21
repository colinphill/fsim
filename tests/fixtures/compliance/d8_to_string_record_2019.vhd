-- fsim: top=d8_to_string_record_2019(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.3.3.3, 5.3.2.4: TO_STRING is implicitly declared for a
-- representable record type and for a representable array of any
-- dimensionality.
entity d8_to_string_record_2019 is
end entity;

architecture test of d8_to_string_record_2019 is
    type rec_t is record
        a : integer;
    end record;
    type matrix_t is array (0 to 1, 0 to 1) of integer;
begin
    p1 : process is
        variable r : rec_t := ( a => 5 );
        variable m : matrix_t := ( (1, 2), (3, 4) );
        constant sr : string := to_string(r);
        constant sm : string := to_string(m);
    begin
        assert sr'length > 0 severity failure;
        assert sm'length > 0 severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
