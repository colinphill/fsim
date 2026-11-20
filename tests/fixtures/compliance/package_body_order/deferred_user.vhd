-- SPDX-License-Identifier: Apache-2.0
use work.pbo_deferred.all;
entity pbo_deferred_user is end entity;
architecture a of pbo_deferred_user is
begin
  process begin report "d=" & integer'image(d); wait; end process;
end architecture;
