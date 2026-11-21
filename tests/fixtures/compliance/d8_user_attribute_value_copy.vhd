-- fsim: top=d8_user_attribute_value_copy(a) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 7.2 and 14.1: the value of a user-defined attribute is the
-- expression of its attribute specification, evaluated where the
-- specification is written (12.3, 12.4): the names in it denote what is
-- visible in the package, whatever the context clause of the user says.
package d8_attr_pack is
  type level is (low, mid, high);
  constant base : integer := 20;
  attribute lvl : level;
  attribute num : integer;
  attribute lvl of d8_attr_pack : package is high;
  attribute num of d8_attr_pack : package is base + 1;
end package;

use work.d8_attr_pack.all;

entity d8_user_attribute_value_copy is
end entity;

architecture a of d8_user_attribute_value_copy is
  constant l : level := d8_attr_pack'lvl;
  constant n : integer := d8_attr_pack'num;
begin
  process
  begin
    assert l = high and n = 21 severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
