-- fsim: top=formal_subelements(sim) std=1993
-- IEEE 1076-1993 4.3.2.2: individual association of formal subelements
-- (`d(7) => x, d(6 downto 0) => y`, `r.f => z`) for input ports; and the
-- VHDL-2008 slice form of an aggregate range choice, used by the parser to
-- combine the parts.
package formal_subelements_pkg is
  type pair_t is record
    high : bit;
    low  : bit_vector(2 downto 0);
  end record;
end package;

use work.formal_subelements_pkg.all;
entity formal_subelements_cell is
  port (
    d : in bit_vector(7 downto 0);
    p : in pair_t;
    q : out bit_vector(7 downto 0);
    s : out bit_vector(3 downto 0)
  );
end entity;

architecture rtl of formal_subelements_cell is
begin
  q <= d;
  s <= p.high & p.low;
end architecture;

use work.formal_subelements_pkg.all;
entity formal_subelements is
end entity;

architecture sim of formal_subelements is
  signal top_bit : bit := '1';
  signal rest : bit_vector(6 downto 0) := "0000101";
  signal flag : bit := '0';
  signal field : bit_vector(2 downto 0) := "110";
  signal word : bit_vector(7 downto 0);
  signal packed : bit_vector(3 downto 0);
begin
  cell : entity work.formal_subelements_cell
    port map (
      d(7) => top_bit,
      d(6 downto 0) => rest,
      p.high => flag,
      p.low => field,
      q => word,
      s => packed
    );

  process
  begin
    wait for 1 ns;
    if word = "10000101" and packed = "0110" then
      report "PASS";
    else
      report "FAIL" severity error;
    end if;
    wait;
  end process;
end architecture;
