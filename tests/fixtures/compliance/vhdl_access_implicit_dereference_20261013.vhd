-- fsim: top=vhdl_access_implicit_dereference_20261013(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
package vhdl_access_scalar_aliases_20261013 is
    type scalar_value is range -100 to 100;
    type scalar_pointer is access scalar_value;
    subtype scalar_pointer_alias is scalar_pointer;
end package;

entity vhdl_access_implicit_dereference_20261013 is
end entity;

use work.vhdl_access_scalar_aliases_20261013.all;

architecture sim of vhdl_access_implicit_dereference_20261013 is
begin
    test : process
        type node;
        type node_pointer is access node;
        type node_pointer_vector is array (natural range <>) of node_pointer;
        type node is record
            link : node_pointer;
            marker : integer;
        end record;

        procedure capture_marker(
            variable item : inout node_pointer;
            variable observed : out integer) is
        begin
            observed := item.marker;
        end procedure;

        type first_word_pointer is access bit_vector(0 to 3);
        type second_word_pointer is access bit_vector(0 to 3);
        type byte_word_pointer is access bit_vector(0 to 7);
        type unconstrained_byte_pointer is access bit_vector;

        variable first_node : node_pointer := new node;
        variable second_node : node_pointer := new node;
        variable first_word : first_word_pointer
            := new bit_vector'("1010");
        variable second_word : second_word_pointer
            := new bit_vector'("0110");
        variable byte_word : byte_word_pointer
            := new bit_vector'("00000000");
        variable unconstrained_byte : unconstrained_byte_pointer
            := new bit_vector(0 to 7);
        variable assigned_word : first_word_pointer;
        variable observed_marker : integer := 0;
        variable external_scalar_pointer :
            work.vhdl_access_scalar_aliases_20261013.scalar_pointer_alias
            := new work.vhdl_access_scalar_aliases_20261013.scalar_value'(37);
    begin
        first_node.marker := 17;
        second_node.marker := 29;
        first_node.link := second_node;

        capture_marker(first_node, observed_marker);
        assert observed_marker = 17
            report "access-typed formal lost implicit member dereference"
            severity failure;
        assert external_scalar_pointer.all = 37
            report "cross-package access subtype alias lost explicit .all"
            severity failure;

        assert first_node.marker = 17 and second_node.marker = 29
            report "implicit access-member reads mixed distinct heap objects"
            severity failure;
        assert first_node.all.marker = 17 and second_node.all.marker = 29
            report "explicit dereference member read lost heap root or offset"
            severity failure;
        assert first_node.link = second_node
            report "implicit access-member write lost the stored handle"
            severity failure;

        (first_node, second_node) := node_pointer_vector'(
            second_node, first_node);
        assert first_node.marker = 29 and second_node.marker = 17
            report "aggregate assignment did not snapshot access handles"
            severity failure;

        assert first_word.all = "1010"
            report "allocator did not inherit its bounded access context"
            severity failure;
        assert byte_word.all = "00000000"
            report "same-length BIT_VECTOR access equality failed"
            severity failure;
        assert unconstrained_byte.all = "00000000"
            report "unconstrained BIT_VECTOR access equality failed"
            severity failure;
        assert byte_word.all /= "0000"
            report "BIT_VECTOR access equality missed a length mismatch"
            severity failure;
        assert first_word.all(0 to 1) = "10"
            report "slice selection through an access object failed"
            severity failure;
        assert second_word(2) = '1'
            report "index selection through an access object failed"
            severity failure;

        assigned_word := new bit_vector'("1100");
        assert assigned_word.all = "1100"
            report "assignment allocator did not inherit access context"
            severity failure;

        deallocate(first_node);
        deallocate(second_node);
        deallocate(first_word);
        deallocate(second_word);
        deallocate(byte_word);
        deallocate(unconstrained_byte);
        deallocate(assigned_word);
        deallocate(external_scalar_pointer);
        assert first_node = null and second_node = null
            and first_word = null and second_word = null
            and byte_word = null and unconstrained_byte = null
            and assigned_word = null
            and external_scalar_pointer = null
            report "deallocation did not null every access object"
            severity failure;
        report "PASS";
        wait;
    end process;
end architecture;
