-- SPDX-License-Identifier: Apache-2.0
-- VHDL delta collapsing witnesses, driven by SystemVerilog testbenches
-- (whose clock and stimulus threads change signals only before a time
-- step's first delta). In each design `a` reaches a reader along two paths
-- of different delta depth, so the reference schedule passes through an
-- intermediate input combination that a collapsed reader would never see.

library ieee;
use ieee.std_logic_1164.all;

-- A non-fatal assertion that fails only on the intermediate combination:
-- the reference reports it, so its process must not be collapsed.
entity dc_glitch_assert is
    port (clk, a : in std_logic; q : out std_logic);
end entity;

architecture rtl of dc_glitch_assert is
    signal b, d, e, x : std_logic := '0';
    signal c : std_logic := '1';
begin
    b <= a;
    c <= not b;
    d <= b;
    e <= not c;
    process (d, e)
    begin
        x <= d xor e;
        assert (d xor e) = '0' report "glitch" severity note;
    end process;
    process (clk)
    begin
        if rising_edge(clk) then
            q <= x;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- A range check whose value counts bits selected from a signal in a loop:
-- on the intermediate combination two bits are set and the check fails
-- (fatally) in the reference, so its process must not be collapsed. The
-- vector is wider than 64 bits, so the kernel selects its bits from the
-- slot in place (a selection that reads a slot, not a register).
entity dc_glitch_count is
    port (clk, a : in std_logic; q : out std_logic);
end entity;

architecture rtl of dc_glitch_count is
    signal b, d, e, n : std_logic := '0';
    signal c : std_logic := '1';
    signal vec : std_logic_vector(71 downto 0) := (70 => '1', others => '0');
begin
    b <= a;
    c <= not b;
    d <= b;
    e <= not c;
    vec <= d & (not e) & (69 downto 0 => '0');
    process (vec)
        variable count : integer range 0 to 1;
    begin
        count := 0;
        for i in vec'range loop
            if vec(i) = '1' then
                count := count + 1;
            end if;
        end loop;
        if count = 1 then
            n <= '1';
        else
            n <= '0';
        end if;
    end process;
    process (clk)
    begin
        if rising_edge(clk) then
            q <= n;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- GF(2^4) multiplication by a function with constant-bound loops and
-- integer index arithmetic: pure, so it may be collapsed.
entity dc_static_cell is
    port (
        a, b : in std_logic_vector(3 downto 0);
        p : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of dc_static_cell is
    function mul(x, y : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable r : std_logic_vector(6 downto 0) := (others => '0');
        variable s : std_logic_vector(6 downto 0);
        variable t : std_logic;
    begin
        for k in 0 to 6 loop
            t := '0';
            for i in 0 to 3 loop
                if i <= k and k - i < 4 then
                    t := t xor (x(i) and y(k - i));
                end if;
            end loop;
            r(k) := t;
        end loop;
        -- x^4 = x + 1
        for i in 6 downto 4 loop
            s := (others => '0');
            s(i) := '1';
            s(i - 3) := '1';
            s(i - 4) := '1';
            if r(i) = '1' then
                r := r xor s;
            end if;
        end loop;
        return r(3 downto 0);
    end function;
begin
    p <= mul(a, b);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Four cells whose products drive elements of one array (port drivers),
-- packed into one vector by slice drivers and registered on clk.
entity dc_static is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(15 downto 0));
end entity;

architecture rtl of dc_static is
    type products_t is array (0 to 3) of std_logic_vector(3 downto 0);
    signal products : products_t;
    signal rotated : std_logic_vector(3 downto 0);
    signal packed : std_logic_vector(15 downto 0);
begin
    rotated <= a(2 downto 0) & a(3);
    u0 : entity work.dc_static_cell port map (a => a, b => rotated, p => products(0));
    u1 : entity work.dc_static_cell port map (a => rotated, b => a, p => products(1));
    u2 : entity work.dc_static_cell port map (a => a, b => a, p => products(2));
    u3 : entity work.dc_static_cell port map (a => rotated, b => rotated, p => products(3));
    pack : for j in 0 to 3 generate
        packed(4 * j + 3 downto 4 * j) <= products(j);
    end generate;
    process (clk)
    begin
        if rising_edge(clk) then
            q <= packed;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- A data-dependent loop nested in a static one: its iteration count
-- follows intermediate inputs, so its process must not be collapsed.
entity dc_nested_loop is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of dc_nested_loop is
    function lead(x : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable v, r : std_logic_vector(3 downto 0);
    begin
        r := (others => '0');
        for k in 0 to 1 loop
            v := x;
            while v(3) = '0' and v /= "0000" loop
                v := v(2 downto 0) & '0';
            end loop;
            r := r xor v;
        end loop;
        return r;
    end function;
    signal p : std_logic_vector(3 downto 0);
begin
    p <= lead(a);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= p;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- The intermediate combination reaches an output port: the SystemVerilog
-- testbench counts every event on it, so the glitch must stay visible.
entity dc_observe_glitch is
    port (clk, a : in std_logic; q, r : out std_logic);
end entity;

-- k1 and k2 are an unobserved neighbour of the observed chain, registered
-- into r: they may be collapsed.
architecture rtl of dc_observe_glitch is
    signal b, d, e, k1, k2 : std_logic := '0';
    signal c : std_logic := '1';
begin
    b <= a;
    c <= not b;
    d <= b;
    e <= not c;
    q <= d xor e;
    k1 <= not a;
    k2 <= k1 xor a;
    process (clk)
    begin
        if rising_edge(clk) then
            r <= k2;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- 'EVENT and 'LAST_VALUE of an internal signal that pulses for one delta
-- on each input change: the count of its events is registered on clk.
entity dc_attributes is
    port (
        clk, a : in std_logic;
        q : out std_logic_vector(7 downto 0);
        r : out std_logic_vector(1 downto 0));
end entity;

architecture rtl of dc_attributes is
    signal b, c, x, n1, n2 : std_logic := '0';
    signal count : unsigned(7 downto 0) := (others => '0');
begin
    b <= a;
    c <= b;
    x <= b xor c;
    -- A collapsible neighbour of the pulse, registered into r.
    n1 <= not a;
    n2 <= n1 and a;
    process (x)
    begin
        if x'event and x'last_value = '0' then
            count <= count + 1;
        end if;
    end process;
    process (clk)
    begin
        if rising_edge(clk) then
            q <= std_logic_vector(count);
        end if;
    end process;
    process (clk)
    begin
        if rising_edge(clk) then
            r <= n1 & n2;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Eight GF(2^4) cells in a chain, each multiplying the previous product
-- by the input: pure, so every cell may be collapsed, eight levels deep.
-- (Each stage is its own signal: the planner's reader rule compares whole
-- signals, so stages of one array would read their own output.)
entity dc_chain is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of dc_chain is
    signal s0, s1, s2, s3, s4, s5, s6, s7, s8 : std_logic_vector(3 downto 0);
begin
    s0 <= a(0) & a(3 downto 1);
    c1 : entity work.dc_static_cell port map (a => s0, b => a, p => s1);
    c2 : entity work.dc_static_cell port map (a => s1, b => a, p => s2);
    c3 : entity work.dc_static_cell port map (a => s2, b => a, p => s3);
    c4 : entity work.dc_static_cell port map (a => s3, b => a, p => s4);
    c5 : entity work.dc_static_cell port map (a => s4, b => a, p => s5);
    c6 : entity work.dc_static_cell port map (a => s5, b => a, p => s6);
    c7 : entity work.dc_static_cell port map (a => s6, b => a, p => s7);
    c8 : entity work.dc_static_cell port map (a => s7, b => a, p => s8);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= s8;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Two inputs the testbench changes from different regions of one time
-- step (a blocking in the active region, b nonblocking in every other
-- step): a note reports each value their exclusive or takes.
entity dc_regions is
    port (
        clk, a, b : in std_logic;
        q, r : out std_logic);
end entity;

architecture rtl of dc_regions is
    signal x, y, z : std_logic := '0';
begin
    x <= a xor b;
    -- A collapsible neighbour of the reported signal, registered into r.
    y <= a and b;
    z <= y or not a;
    process (x)
    begin
        report "x=" & std_logic'image(x) severity note;
    end process;
    process (clk)
    begin
        if rising_edge(clk) then
            q <= x;
            r <= z;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- A selected assignment and a case statement in one pure cone, registered
-- on clk: every member may be collapsed.
entity dc_case is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_case is
    signal s, t : std_logic_vector(3 downto 0);
begin
    with a select s <=
        "0001" when "0000",
        "0010" when "0001",
        "0100" when "0011",
        a xor "1010" when others;
    process (s)
    begin
        case s is
            when "0001" | "0010" => t <= "1111";
            when "0100" => t <= s(2 downto 0) & '1';
            when others => t <= not s;
        end case;
    end process;
    process (clk)
    begin
        if rising_edge(clk) then
            q <= s & t;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- numeric_std addition, subtraction and resizing on static widths.
entity dc_numeric is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_numeric is
    signal x, y, z : unsigned(7 downto 0);
begin
    x <= resize(unsigned(a), 8) + 3;
    y <= x + x;
    z <= y - resize(unsigned(a), 8);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= std_logic_vector(z);
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- A record signal whose fields have separate drivers (disjoint slices of
-- one signal) and a record computed from it.
entity dc_record is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_record is
    type pair_t is record
        lo, hi : std_logic_vector(3 downto 0);
    end record;
    signal p, r : pair_t;
begin
    p.lo <= a;
    p.hi <= not a(0) & a(3 downto 1);
    r <= (lo => p.hi xor p.lo, hi => p.lo and p.hi);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= r.hi & r.lo;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Fan-out and fan-in across levels: y and z read signals of different
-- depth from a, so their inputs change in different deltas.
entity dc_diamond is
    port (
        clk : in std_logic;
        a : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_diamond is
    signal x1, x2, x3, y, z : std_logic_vector(3 downto 0);
begin
    x1 <= a(2 downto 0) & a(3);
    x2 <= a xor x1;
    x3 <= x2 and not a;
    y <= x1 xor x3;
    z <= x3 or (a and x2);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= y & z;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- A counter incremented under a data condition inside a static loop
-- (popcount): bounded by the trip count, so its integer arithmetic and the
-- ranged signal's check cannot fail on any input, glitch or not.
entity dc_popcount is
    port (
        clk : in std_logic;
        a : in std_logic_vector(23 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_popcount is
    function popcount(v : std_logic_vector) return integer is
        variable c : integer := 0;
    begin
        for i in v'range loop
            if v(i) = '1' then c := c + 1; end if;
        end loop;
        return c;
    end function;
    signal x : std_logic_vector(23 downto 0);
    signal n : integer range 0 to 24;
begin
    x <= a xor x"a5c3e1";
    n <= popcount(x);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= std_logic_vector(to_unsigned(n, 8));
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Slices assembled bit by bit and byte by byte in static loops: dynamic
-- part inserts whose bases are loop indices, into a narrow and into a wide
-- (80-bit) target.
entity dc_slice is
    port (
        clk : in std_logic;
        a : in std_logic_vector(11 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_slice is
    function get_slice(v : std_logic_vector; idx : integer; w : integer)
        return std_logic_vector is
        variable r : std_logic_vector(w-1 downto 0);
    begin
        for j in 0 to w-1 loop
            r(j) := v(v'low + idx*w + j);
        end loop;
        return r;
    end function;
    function spread(v : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable r : std_logic_vector(79 downto 0) := (others => '0');
    begin
        for j in 0 to 9 loop
            r(j*8+7 downto j*8) := v & v;
        end loop;
        return r;
    end function;
    signal x : std_logic_vector(11 downto 0);
    signal s : std_logic_vector(3 downto 0);
    signal w : std_logic_vector(79 downto 0);
begin
    x <= not a;
    s <= get_slice(x, 1, 4);
    w <= spread(s);
    process (clk)
    begin
        if rising_edge(clk) then
            q <= w(43 downto 36);
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Known-value guard: to_integer's metavalue assertion under a cone fed by
-- a register (a first-delta writer), and under one fed by the testbench
-- input (a delay-only thread). With known sources a cone is collapsed;
-- while a source holds a metavalue it runs in the delta schedule, and the
-- warnings match the reference.
entity dc_guard is
    port (
        clk : in std_logic;
        a : in std_logic_vector(5 downto 0);
        q : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of dc_guard is
    signal r : std_logic_vector(5 downto 0);
    signal s : std_logic_vector(5 downto 0);
    signal i : integer range 0 to 63;
    signal j : integer range 0 to 7;
begin
    s <= r xor "101010";
    i <= to_integer(unsigned(s));
    j <= to_integer(unsigned(a(2 downto 0)));
    process (clk)
    begin
        if rising_edge(clk) then
            r <= a;
            q <= std_logic_vector(to_unsigned((i * 3 + j) mod 256, 8));
        end if;
    end process;
end architecture;
