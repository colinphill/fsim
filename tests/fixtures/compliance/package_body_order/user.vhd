-- SPDX-License-Identifier: Apache-2.0
-- Legal only with body_new (width = 8): analyzed while body_old is the
-- library's body, elaborated after body_new replaced it (13.5: the user
-- depends on the package declaration only).
use work.pbo_pkg.all;
entity pbo_user is end entity;
architecture a of pbo_user is
  signal s : bit_vector(width - 1 downto 0) := x"AB";
begin
  process begin
    report "width=" & integer'image(s'length) & " sel=" & integer'image(sel);
    wait;
  end process;
end architecture;
