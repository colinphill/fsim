-- fsim: top=d8_range_record_subtype(a) std=2019
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 5.3.3.2 and 16.2.3: the subtype `R'record` of a range
-- attribute is an implicit record type with elements left, right and
-- direction. The implicit declaration is in force at the subtype
-- declaration that names it (12.3).
entity d8_range_record_subtype is
end entity;

architecture a of d8_range_record_subtype is
  subtype rr is natural'range'record;
  constant r : rr := (left => 0, right => integer'high, direction => ascending);
begin
  process
  begin
    assert r.left = 0 and r.right = integer'high severity failure;
    assert r.direction = ascending severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
