-- SPDX-License-Identifier: Apache-2.0
--
-- A type conversion to INTEGER of an alias of an external name whose type is
-- a user-defined integer type (IEEE 1076-2019 6.6.3, 8.7, 9.3.6): integer
-- types are closely related, and an alias of an external name has the type of
-- the external name's subtype indication.
--   w and the leaf's port are 7 (the port's driver starts at its default 7, the
--   one source of w, 14.7.3.2, 14.7.5.2); the three
--   conversions have the same value:
--     integer(w) = integer(wv) = integer(pv) = 7
package conv_alias_pkg is
  type t1 is range -1000 to 1000;
end package;

use work.conv_alias_pkg.all;

entity conv_alias_leaf is
  port (p : inout t1 := 7);
end entity;

architecture rtl of conv_alias_leaf is
begin
end architecture;

use work.conv_alias_pkg.all;

entity top is
end entity;

architecture sim of top is
  signal w : t1 := 7;
  alias pv is << signal .top.u.p : t1 >>;
  alias wv is << signal .top.w : t1 >>;
begin
  u : entity work.conv_alias_leaf port map (p => w);

  check : process
  begin
    report "wide e=" & integer'image(integer(w));
    report "wide f=" & integer'image(integer(wv));
    report "wide g=" & integer'image(integer(pv));
    wait;
  end process;
end architecture;
