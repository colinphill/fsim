-- fsim-expect: FSIM-ELAB-VHASSIGN-001 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- A designated record value is not an access value; allocating it requires `new`.
entity access_object_from_designated_record is
end entity;

architecture test of access_object_from_designated_record is
    type cell;
    type link is access cell;
    type cell is record
        value : integer;
        succ : link;
        pred : link;
    end record;
begin
    check : process is
        variable null_link : link := null;
        variable allocated_link : link := new cell'(0, null, null);
        variable copied_link : link := allocated_link;
        variable missing_allocator : link := cell'(0, null, null);
    begin
        wait;
    end process;
end architecture;
