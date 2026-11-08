-- SPDX-License-Identifier: Apache-2.0
-- A package body is a design unit of its own: the secondary unit of its
-- package declaration (IEEE 1076-2019 19.4.5, Figure 4). The units a design
-- unit depends on (vhpiDepUnits) and the declarations its use clauses make
-- visible (vhpiUses) are relations of Figure 4.
package pk is
  constant c : integer := 3;
  function f(x : integer) return integer;
end package;

package body pk is
  function f(x : integer) return integer is
  begin
    return x + c;
  end function;
end package body;

use work.pk.all;
-- Each attribute specification belongs to the design unit whose declarative
-- part holds it (7.2): `ea` to the entity declaration, `aa` to the
-- architecture body, so vhpiSpecs (Figure 33) of the entity is ea and of the
-- architecture is aa.
entity top is
  attribute ea : integer;
  attribute ea of top : entity is 1;
end entity;

use work.pk.all;
architecture t of top is
  signal s : integer := f(1);
  attribute aa : integer;
  attribute aa of s : signal is 2;
begin
end architecture;
