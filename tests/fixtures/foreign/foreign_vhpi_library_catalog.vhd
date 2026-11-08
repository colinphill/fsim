-- SPDX-License-Identifier: Apache-2.0
-- vhpiDesignUnits of a library lists every design unit analyzed into it
-- (IEEE 1076-2019 19.4.5, Figure 4), not only the units the elaborated design
-- uses: `spare`, `spare_a` and `spare_pk` are analyzed with `top` but nothing
-- instantiates or uses them.
entity spare is
end entity;

architecture spare_a of spare is
begin
end architecture;

package spare_pk is
  constant k : integer := 1;
end package;

package body spare_pk is
end package body;

entity top is
end entity;

architecture t of top is
begin
end architecture;
