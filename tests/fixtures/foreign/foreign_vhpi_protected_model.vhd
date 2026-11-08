-- SPDX-License-Identifier: Apache-2.0
-- Protected types in the information model (IEEE 1076-2019 5.6, 19.4.3,
-- Figures 5, 15 and 21): the shared variable `ctr` has the protected type
-- declaration as its type (class protectedTypeDecl), the declaration and
-- its body lead to each other (vhpiProtectedTypeBody, vhpiProtectedTypeDecl),
-- the methods are the declarations of the type declaration, and the
-- variable's elaboration is a protectedTypeInst region named like it.
entity top is
end entity;

architecture rtl of top is
  type counter_t is protected
    procedure bump;
  end protected counter_t;
  type counter_t is protected body
    variable count : integer := 0;
    procedure bump is
    begin
      count := count + 1;
    end procedure bump;
  end protected body counter_t;
  shared variable ctr : counter_t;
  signal s : bit;
begin
end architecture;
