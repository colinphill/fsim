-- fsim: top=vhdl_access_object_initializer_controls_batch57(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Legal access initial values: null, allocation, and copying preserve sharing.
entity vhdl_access_object_initializer_controls_batch57 is
end entity;

architecture test of vhdl_access_object_initializer_controls_batch57 is
    type node;
    type node_ptr is access node;
    type node is record
        payload : integer;
        next_node : node_ptr;
    end record;
begin
    exercise : process is
        variable empty_ptr : node_ptr := null;
        variable allocated_ptr : node_ptr := new node'(
            payload => 7, next_node => null);
        variable copied_ptr : node_ptr := allocated_ptr;
    begin
        assert empty_ptr = null
            report "null access initialization changed value"
            severity failure;
        assert allocated_ptr /= null and copied_ptr = allocated_ptr
            report "allocator or access-copy initialization failed"
            severity failure;
        assert allocated_ptr.all.payload = 7
            report "allocated designated record payload is wrong"
            severity failure;
        copied_ptr.all := (payload => 19, next_node => null);
        assert allocated_ptr.all.payload = 19
            report "copied access value does not share its designated record"
            severity failure;
        empty_ptr := copied_ptr;
        assert empty_ptr = allocated_ptr
            report "access assignment did not preserve pointer identity"
            severity failure;
        empty_ptr := null;
        assert empty_ptr = null and allocated_ptr /= null
                and copied_ptr.all.payload = 19
            report "null assignment changed another access value"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
