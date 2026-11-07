-- SPDX-License-Identifier: Apache-2.0
--
-- A TEXTIO read of std_logic values from a line (IEEE 1076-2019 16.4,
-- IEEE 1164 READ for STD_ULOGIC and STD_ULOGIC_VECTOR): READ skips leading
-- white space and takes the next character as the value, so "H 1WL-" reads
-- 'H', then the four characters '1', 'W', 'L', '-' (leftmost element first).
library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;

entity top is
end entity;

architecture sim of top is
begin
  reader : process
    variable text : line;
    variable bit_value : std_logic;
    variable vector : std_logic_vector(3 downto 0);
    variable ok : boolean;
  begin
    write(text, string'("H 1WL-"));
    read(text, bit_value, ok);
    report "weak ok=" & boolean'image(ok) & ";";
    report "weak bit=" & std_logic'image(bit_value) & ";";
    read(text, vector, ok);
    report "weak ok=" & boolean'image(ok) & ";";
    report "weak vector=" & to_string(vector) & ";";
    std.env.finish;
  end process;
end architecture;
