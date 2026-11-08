-- SPDX-License-Identifier: Apache-2.0
-- A concurrent procedure call whose procedure body is declared apart, in the
-- package body (IEEE 1076-2019 4.8): vhpiCbStmt on the call triggers before
-- the first statement of that body (21.3.4.2), which the callback reports.
package pk is
  procedure mark(constant d : in integer);
end package;

package body pk is
  procedure mark(constant d : in integer) is
  begin
    null;
  end procedure;
end package body;

use work.pk.all;
entity top is
end entity;

architecture t of top is
begin
  cc : mark(3);
end architecture;
