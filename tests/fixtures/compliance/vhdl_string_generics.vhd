-- fsim: top=string_generics(sim) std=2008 generic=config=fast:x4 generic=width=12
-- IEEE 1076-2008 6.5.6.2: STRING generics, with and without defaults, bound
-- by a generic map or by a top-level override; procedure overloads that
-- differ only by array type (4.5.2); a protected type body with private
-- subprograms (5.6.3).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity string_leaf is
  generic (name : string := "unnamed");
  port (length_out : out natural);
end entity;

architecture rtl of string_leaf is
begin
  length_out <= name'length;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity string_generics is
  generic (config : string; width : natural := 4; verbose : boolean := false);
end entity;

architecture sim of string_generics is
  type tally_t is protected
    procedure add(constant amount : in natural);
    impure function total return natural;
  end protected;

  type tally_t is protected body
    variable sum : natural := 0;
    impure function doubled(constant amount : natural) return natural is
    begin
      return amount * 2;
    end function;
    procedure add(constant amount : in natural) is
    begin
      sum := sum + doubled(amount);
    end procedure;
    impure function total return natural is
    begin
      return sum;
    end function;
  end protected body;

  shared variable tally : tally_t;

  procedure classify(constant value : in std_ulogic_vector; variable kind : out natural) is
  begin
    kind := 1;
  end procedure;
  procedure classify(constant value : in unsigned; variable kind : out natural) is
  begin
    kind := 2;
  end procedure;
  procedure classify(constant value : in signed; variable kind : out natural) is
  begin
    kind := 3;
  end procedure;

  signal named_length, default_length : natural;
  signal vector : std_logic_vector(3 downto 0) := "1010";
  signal count : unsigned(3 downto 0) := "0011";
  signal offset : signed(3 downto 0) := "1111";
begin
  named : entity work.string_leaf generic map (name => "leaf-one")
    port map (length_out => named_length);
  defaulted : entity work.string_leaf port map (length_out => default_length);

  process
    variable kind : natural;
    variable failures : natural := 0;
  begin
    wait for 1 ns;
    if config /= "fast:x4" or config'length /= 7 then
      report "FAIL: STRING generic override " & config severity error;
      failures := failures + 1;
    end if;
    if width /= 12 or verbose then
      report "FAIL: scalar generic override" severity error;
      failures := failures + 1;
    end if;
    if named_length /= 8 or default_length /= 7 then
      report "FAIL: STRING generic map and default" severity error;
      failures := failures + 1;
    end if;
    classify(vector, kind);
    if kind /= 1 then
      report "FAIL: std_logic_vector overload" severity error;
      failures := failures + 1;
    end if;
    classify(count, kind);
    if kind /= 2 then
      report "FAIL: unsigned overload" severity error;
      failures := failures + 1;
    end if;
    classify(offset, kind);
    if kind /= 3 then
      report "FAIL: signed overload" severity error;
      failures := failures + 1;
    end if;
    tally.add(3);
    tally.add(4);
    if tally.total /= 14 then
      report "FAIL: protected private subprogram" severity error;
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
