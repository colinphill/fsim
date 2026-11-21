-- fsim: top=d8_env_call_path_range(test) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 16.5.6.2, 10.10: `for i in p'range` over the CALL_PATH_VECTOR
-- an access value of STD.ENV designates (indexed upwards from 0) iterates its
-- elements; the access value is a STD.ENV container, not a shaped array.
entity d8_env_call_path_range is
end entity;

architecture test of d8_env_call_path_range is
    use std.env.all;
begin
    process
        variable p : call_path_vector_ptr;
        variable n : integer := 0;
    begin
        p := get_call_path;
        for idx in p'range loop
            n := n + 1;
        end loop;
        assert n >= 1 report "FAIL: call path is empty" severity failure;
        assert n = p'length report "FAIL: count" severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
