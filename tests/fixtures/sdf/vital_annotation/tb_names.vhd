-- SPDX-License-Identifier: Apache-2.0
--
-- The cell vnames reports the timing generics that names_30.sdf annotates.
-- The expected values follow IEEE 1076.4-2000 5.2.6 and 5.2.7:
--  * Table 1 fills tr01..trz0 from 1, 2, 3 or 6 delays (3 delays v1 v2 v3 give
--    v1 v2 v3 v1 v3 v2); a list of 4 or 5 is read with trailing empty values,
--    and an empty value `()` leaves the generic's value of 9 ns;
--  * a VitalDelayType01ZX generic that a list of at most 6 delays fills takes
--    the transitions through X as the selection of a VitalDelayType01Z delay
--    does (9.2, Table 7): tr0x = min(tr01, tr0z), trx1 = max(tr01, trz1),
--    tr1x = min(tr10, tr1z), trx0 = max(tr10, trz0), trxz = max(tr0z, tr1z),
--    trzx = min(trz1, trz0);
--  * conditions and edges become the suffixes of 5.2.7.2.1 and 5.2.7.3.2, and
--    a timing check with two ports ends in the condition and edge of the first
--    port or `noedge`, then those of the second.
entity tb_names is end entity;

architecture t of tb_names is
  component vnames end component;
begin
  n1 : vnames;
end architecture;
