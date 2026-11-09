-- SPDX-License-Identifier: Apache-2.0
-- The formal part of a port association may select an element of a record
-- port, an element of an array port, or a chain of them (IEEE 1076-2019
-- 6.5.7.1, 6.5.7.3); each association element of a port map has the part of
-- the port its formal part designates as vhpiFormal, an object of class
-- selectedName or indexedName, and its own actual (19.12.2.2).
package p is
  type rec_t is record
    f : bit_vector(1 downto 0);
    g : bit;
  end record;
  type mat_t is array (0 to 1) of bit_vector(1 downto 0);
  type outer_t is record
    h : rec_t;
    k : bit;
  end record;
end package;

use work.p.all;
entity leaf is
  port (rr : in rec_t; mm : in mat_t; oo : in outer_t);
end entity;

architecture l of leaf is
begin
end architecture;

use work.p.all;
entity top is
end entity;

architecture t of top is
  signal a, b, c, d, e, x, y : bit;
  signal f2, v2 : bit_vector(1 downto 0);
begin
  u : entity work.leaf
    port map (rr.f(1) => a, rr.f(0) => b, rr.g => c,
              mm(0)(1) => d, mm(0)(0) => e, mm(1) => f2,
              oo.h.f => v2, oo.h.g => x, oo.k => y);
end architecture;
