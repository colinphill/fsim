-- fsim-expect: FSIM-ELAB-VHAGG-003 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 9.3.3.2: a named choice of a record aggregate is an element of the record type.
entity d7_record_aggregate_unknown_element is
end entity;

architecture a of d7_record_aggregate_unknown_element is
begin
  process
    type pair is record
      a : integer;
      b : integer;
    end record;
    variable p : pair;
  begin
    p := (a => 1, c => 2);
    wait;
  end process;
end architecture;
