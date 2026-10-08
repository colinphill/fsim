-- fsim: top=enum_index_arrays(sim) std=2008
-- IEEE 1076-2008 5.3.2: arrays indexed by enumeration and BOOLEAN types.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package enum_index_pkg is
  type device_t is (DEV_ROM, DEV_GPIO, DEV_UART, DEV_TIMER, DEV_DMA);
  type rec_t is record
    addr : std_ulogic_vector(7 downto 0);
    stb  : std_ulogic;
  end record;
  type rec_array_t is array (device_t) of rec_t;
  type flag_array_t is array (device_t) of std_ulogic;
  type word_array_t is array (device_t) of unsigned(7 downto 0);
  type count_array_t is array (device_t range DEV_GPIO to DEV_TIMER) of integer;
  type table_t is array (device_t, boolean) of natural;
  type bool_map_t is array (boolean) of std_ulogic;
  type open_t is array (device_t range <>) of natural;

  constant base_c : word_array_t := (
    DEV_ROM => x"00", DEV_GPIO => x"10", DEV_UART => x"20",
    DEV_TIMER => x"30", DEV_DMA => x"40");
  constant positional_c : word_array_t := (x"01", x"02", x"03", x"04", x"05");

  function weight(arr : word_array_t) return natural;
end package;

package body enum_index_pkg is
  function weight(arr : word_array_t) return natural is
    variable sum_v : natural := 0;
  begin
    for d in arr'range loop
      sum_v := sum_v + to_integer(arr(d));
    end loop;
    return sum_v;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.enum_index_pkg.all;

entity enum_index_leaf is
  port (
    clk  : in  std_ulogic;
    req  : in  rec_t;
    irq  : out std_ulogic
  );
end entity;

architecture rtl of enum_index_leaf is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      irq <= req.stb and req.addr(0);
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.enum_index_pkg.all;

entity enum_index_arrays is
end entity;

architecture sim of enum_index_arrays is
  signal clk   : std_ulogic := '0';
  signal reqs  : rec_array_t;
  signal flags : flag_array_t := (others => '0');
  signal words : word_array_t := base_c;
  signal sel   : device_t := DEV_ROM;
  signal done  : boolean := false;
begin
  clk <= not clk after 5 ns when not done;

  gen_leaf : for d in device_t generate
    leaf : entity work.enum_index_leaf
      port map (clk => clk, req => reqs(d), irq => flags(d));
  end generate;

  driver : process (clk)
  begin
    if rising_edge(clk) then
      for d in device_t loop
        reqs(d).stb <= '0';
        reqs(d).addr <= (others => '0');
      end loop;
      reqs(sel).stb <= '1';
      reqs(sel).addr <= std_ulogic_vector(words(sel) + 1);
      words(sel) <= words(sel) + 2;
    end if;
  end process;

  stim : process
    variable counts   : count_array_t := (others => 0);
    variable table    : table_t := (others => (others => 0));
    variable bools    : bool_map_t := (false => '0', true => '1');
    variable open_v   : open_t(DEV_UART to DEV_DMA) := (others => 7);
    variable idx      : device_t;
    variable total    : natural := 0;
    variable failures : natural := 0;

    procedure check(cond : boolean; msg : string) is
    begin
      if not cond then
        report "FAIL: " & msg severity error;
        failures := failures + 1;
      end if;
    end procedure;
  begin
    -- static attributes
    check(word_array_t'length = 5, "length");
    check(count_array_t'length = 3, "range length");
    check(count_array_t'left = DEV_GPIO, "range left");
    check(count_array_t'right = DEV_TIMER, "range right");
    check(words'left = DEV_ROM and words'right = DEV_DMA, "signal bounds");
    check(words'low = DEV_ROM and words'high = DEV_DMA, "signal low/high");
    check(words'ascending, "ascending");
    check(open_v'length = 3 and open_v'left = DEV_UART, "open bounds");
    check(table_t'length(1) = 5 and table_t'length(2) = 2, "2d length");

    -- constant indexing and aggregates
    check(base_c(DEV_UART) = x"20", "named aggregate");
    check(positional_c(DEV_TIMER) = x"04", "positional aggregate");
    check(bools(true) = '1' and bools(false) = '0', "boolean index");
    check(weight(base_c) = 16#a0#, "function over enum range");

    -- dynamic indexing
    for d in device_t loop
      idx := d;
      total := total + to_integer(base_c(idx));
    end loop;
    check(total = 16#a0#, "dynamic sum");
    idx := device_t'val(3);
    check(base_c(idx) = x"30", "val index");
    for d in count_array_t'range loop
      counts(d) := device_t'pos(d) * 10;
    end loop;
    check(counts(DEV_GPIO) = 10 and counts(DEV_UART) = 20
          and counts(DEV_TIMER) = 30, "subrange loop");
    for d in device_t loop
      for b in boolean loop
        table(d, b) := device_t'pos(d) * 2 + boolean'pos(b);
      end loop;
    end loop;
    check(table(DEV_DMA, true) = 9 and table(DEV_GPIO, false) = 2, "2d index");
    for d in open_v'reverse_range loop
      open_v(d) := open_v(d) + device_t'pos(d);
    end loop;
    check(open_v(DEV_UART) = 9 and open_v(DEV_DMA) = 11, "open index");

    -- signal behaviour through hierarchy
    wait until rising_edge(clk);
    for step in 0 to 9 loop
      sel <= device_t'val(step mod 5);
      wait until rising_edge(clk);
    end loop;
    wait until rising_edge(clk);
    wait until rising_edge(clk);
    check(words(DEV_ROM) = x"06", "signal words rom: " & integer'image(to_integer(words(DEV_ROM))));
    check(words(DEV_DMA) = x"46", "signal words dma: " & integer'image(to_integer(words(DEV_DMA))));
    check(flags = flag_array_t'(DEV_DMA => '1', others => '0'), "flags dma");
    check(flags(sel) = '1' and flags(DEV_ROM) = '0', "flags index");
    report "words(" & device_t'image(sel) & ")=" & integer'image(to_integer(words(sel)));

    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    done <= true;
    wait;
  end process;
end architecture;
