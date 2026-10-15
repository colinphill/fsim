-- SPDX-License-Identifier: Apache-2.0
library masklib;
use work.visible_names.all;
entity explicit_library_mask_consumer is
end entity;

architecture test of explicit_library_mask_consumer is
begin
  check : process
    variable hidden : masklib;
  begin
    wait;
  end process;
end architecture;
