-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.5: '1' is both a BIT and a CHARACTER literal, so p('1') has two interpretations.
-- Expected: FSIM-VHDL-TYPE-002.
-- fsim-standard: 2008
entity t02 is
end entity;
architecture a of t02 is
  procedure p(x : bit) is begin end procedure;
  procedure p(x : character) is begin end procedure;
begin
  process
  begin
    p('1');
    wait;
  end process;
end architecture;
