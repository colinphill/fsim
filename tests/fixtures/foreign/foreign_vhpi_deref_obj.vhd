-- SPDX-License-Identifier: Apache-2.0
-- vhpiDerefObj (IEEE 1076-2019 19.8, Figures 7 and 25): a variable of an
-- access type has a derefObj, the object `ptr.all`, of the designated
-- subtype. Its names are errors in the design hierarchy model (19.4.3) and
-- it is no locally static name, so its value cannot be read (22.3). A
-- variable of a type that is not an access type has none.
entity top is
end entity;

architecture rtl of top is
begin
  main : process
    type int_ptr is access integer;
    variable ptr : int_ptr;
    variable count : integer := 3;
  begin
    ptr := new integer'(5);
    wait;
  end process;
end architecture;
