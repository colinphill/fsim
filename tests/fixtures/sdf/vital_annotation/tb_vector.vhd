-- SPDX-License-Identifier: Apache-2.0
--
-- vvec: vector delay generics and the row-dominant index mapping of IEEE
-- 1076.4-2000 5.2.7.3.3. With g1 = 0 and g2 = 3 (0 to 3) the direction
-- (g1 - g2) / abs(g1 - g2) is -1.
--  * Two bus ports, a generic larger than the first port (5.2.7.3.3.2):
--    A (0 to 1) -> Y (1 to 2), i2 = 1, j1 = 1, j2 = 2.
--    A[0] Y[1]: x = 3 + (abs(1 - 2) + abs(0 - 1) * 2) * -1 = 0.
--    A[1] Y[2]: x = 3 + (abs(2 - 2) + abs(1 - 1) * 2) * -1 = 3.
--    A[0:1] Y[1:2] to Z (1 to 2): the four combinations give x = 0, 1, 2, 3.
--  * Two bus ports and a generic as large as the first port:
--    DATAIN (0 to 3) -> DATAOUT (1 to 4), j2 = 4: DATAIN[0] DATAOUT[1] gives
--    x = 3 + abs(1 - 4) * -1 = 0, and DATAIN[3] DATAOUT[4] gives x = 3.
--  * One bus port (5.2.7.3.3.1), tipd_A (0 to 1), g2 = 1, j2 = 1:
--    PORT A[1] gives x = 1 + abs(1 - 1) * -1 = 1 and PORT A[0] gives x = 0.
-- vretain: RETAIN values by Table 2 of 5.2.6.
--  * r1 v1 on a 01Z generic: (5, 5, r1, 5, r1, 5) = (5, 5, 1, 5, 1, 5).
--  * r1 r2 v1 v2: (v1, v2, r1, v1, r2, v2) = (5, 6, 1, 5, 2, 6).
--  * r1 r2 r3 v1 v2 v3 on a 01ZX generic: the delays fill tr01..trz0 as
--    (4, 5, 6, 4, 6, 5), r1 r2 r3 go to tr0x, tr1x and trzx, and trx1, trx0
--    and trxz are 4, 5 and 6 (the selection of a 01Z delay, Table 7).
--  * r1 v1 v2 v3: as above with r1 in tr0x, tr1x and trzx.
library ieee;
use ieee.std_logic_1164.all;

entity tb_vector is end entity;

architecture t of tb_vector is
  component vvec
    port (A : in std_logic_vector (0 to 1); Y : out std_logic_vector (1 to 2);
          Z : out std_logic_vector (1 to 2);
          DATAIN : in std_logic_vector (0 to 3);
          DATAOUT : out std_logic_vector (1 to 4));
  end component;
  component vretain end component;
begin
  v1 : vvec port map (A => "00", Y => open, Z => open, DATAIN => "0000", DATAOUT => open);
  r1 : vretain;
end architecture;
