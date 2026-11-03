-- SPDX-License-Identifier: Apache-2.0
library ieee;
use ieee.std_logic_1164.all;

entity top is
end entity;

architecture a of top is
  signal clk : std_logic := '0';
  signal a : integer := 0;
  signal b : integer := 0;
  signal s : integer := 0;
  signal t : integer := 0;
  signal u : integer := 0;
  signal v : std_logic_vector(3 downto 0) := "0000";
begin
  -- A labeled process with a sensitivity list and declarations.
  clock : process (clk) is
    variable count : integer := 0;
    constant limit : integer := 3;
  begin
    if clk = '1' then
      count := count + 1;
      a <= count;
    elsif count = 0 then
      a <= 0;
    else
      null;
    end if;
    case count is
      when 0 => b <= 1;
      when 1 | 2 => b <= 2;
      when others => b <= 3 after 1 ns;
    end case;
    for i in 0 to 3 loop
      v(i) <= '1';
      exit when i = 2;
    end loop;
    lp : while count < limit loop
      count := count + 1;
      next lp when count = 2;
    end loop lp;
  end process clock;

  -- Unlabeled concurrent statements have the implicit labels _P0, _P1, ...
  s <= a;

  process is
  begin
    wait;
  end process;

  t <= 1 when a = 0 else 2;

  with a select u <= 1 when 0, 2 when others;

  chk : assert a >= 0 report "negative" severity error;

  -- A process with wait statements.
  stimulus : process is
  begin
    wait for 5 ns;
    clk <= '1';
    wait on clk until clk = '0' for 10 ns;
    assert a >= 0 report "negative" severity warning;
    report "done";
    wait;
  end process stimulus;

  blk : block is
    signal w : integer := 0;
  begin
    w <= s;
  end block blk;

  gen : for i in 0 to 1 generate
    p : process (clk) is
    begin
      null;
    end process p;
  end generate gen;

  -- Expression objects, subprogram calls and a protected shared variable.
  ext : block is
    type counter_t is protected
      procedure bump;
    end protected counter_t;
    type counter_t is protected body
      variable count : integer := 0;
      procedure bump is
      begin
        count := count + 1;
      end procedure bump;
    end protected body counter_t;
    shared variable ctr : counter_t;
    signal q : integer := 0;
    signal q2 : integer := 0;
    signal q3 : integer := 0;
    signal bits : std_logic_vector(1 downto 0) := "00";
    function twice(n : integer) return integer is
    begin
      return n * 2;
    end function twice;
    procedure poke(signal s : out integer; v : integer) is
    begin
      s <= v;
    end procedure poke;
  begin
    drive : process is
      variable r : integer := 0;
    begin
      poke(q, 3);
      r := twice(r) + 1;
      bits <= (1 => '1', 0 => '0');
      wait;
    end process drive;

    q2 <= twice(2);

    pc : poke(q3, 5);
  end block ext;

  -- Subprogram bodies, expression classes and process variables (19.8,
  -- 19.10, 22.5.2); the plug-in walks this block by name.
  sub : block is
    signal sig : integer := 0;
    signal sig2 : integer := 0;
    function add1(n : integer) return integer is
      variable t : integer := n;
    begin
      t := t + 1;
      return t;
    end function add1;
    procedure note(constant d : in integer; signal s : out integer;
        variable v : inout integer) is
      variable w : integer := 0;
    begin
      if d > 100 then
        w := 1;
      else if d > 50 then
        w := 2;
      end if;
      end if;
      s <= d;
    end procedure note;
    procedure drive(signal s : out integer; constant d : in integer) is
    begin
      s <= d;
    end procedure drive;
    component cmp is
      generic (g : integer := 3);
      port (pin : in std_logic; pout : out std_logic);
    end component cmp;
  begin
    expr : process is
      variable v : std_logic_vector(3 downto 0) := "0101";
      variable u : std_logic_vector(1 downto 0) := "00";
      variable k : integer := 2;
      variable r : integer := 0;
    begin
      r := add1(k);
      note(k, sig, r);
      r := integer(real(k));
      r := integer'(k);
      u := (1 => '1', 0 => '0');
      if clk'event and clk'last_value = '0' then
        v(k) := '1';
      elsif r > 1 then
        u := v(k downto 1);
      end if;
      report integer'image(k);
      wait;
    end process expr;

    cp : drive(sig2, 7);
  end block sub;
end architecture;
