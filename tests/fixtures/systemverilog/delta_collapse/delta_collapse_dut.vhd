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
