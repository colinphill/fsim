-- SPDX-License-Identifier: Apache-2.0
-- vhpiLogicalNameP of a file declaration (IEEE 1076-2019 6.4.2.5, 19.4.3,
-- Figure 10): the file logical name is the string of the declaration, a
-- string literal or the value of the constant it names.
use std.textio.all;

entity top is
end entity;

architecture rtl of top is
  constant fname : string := "second.txt";
  file f1 : text open write_mode is "first.txt";
  file f2 : text open write_mode is fname;
begin
end architecture;
