-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  subtype nibble is std_logic_vector(3 downto 0);
  type pair is record
    a : nibble;
    b : integer;
  end record;
  -- Foreign functions with array and record results (IEEE 1076-2019
  -- 22.5.5): their bodies are not executed.
  function mk_nibble(n : integer) return nibble is
  begin
    return "0000";
  end function;
  attribute foreign of mk_nibble : function is "VHPI vals nibble";
  function mk_grow(n : integer) return std_logic_vector is
  begin
    return "";
  end function;
  attribute foreign of mk_grow : function is "VHPI vals grow";
  function mk_text(n : integer) return string is
  begin
    return "";
  end function;
  attribute foreign of mk_text : function is "VHPI vals text";
  function mk_pair(n : integer) return pair is
  begin
    return (a => "0000", b => 0);
  end function;
  attribute foreign of mk_pair : function is "VHPI vals pair";
  signal nib : nibble;
  signal grown : std_logic_vector(7 downto 0);
  signal txt : string(1 to 5);
  signal p : pair;
  signal d1, d2, d3 : std_logic := '0';
begin
  main : process
    type int_ptr is access integer;
    variable ptr : int_ptr;
  begin
    ptr := new integer'(5);
    nib <= mk_nibble(1);
    grown <= mk_grow(8);
    txt <= mk_text(5);
    p <= mk_pair(1);
    wait for 1 ns;
    d1 <= '1';
    wait;
  end process;
  d2 <= d1;
  d3 <= d2;
end architecture;
