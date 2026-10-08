-- fsim: top=composite_constants(sim) std=2008
-- IEEE 1076-2008 6.4.2.2 and 9.3.3: composite constants and signals whose
-- elements are integers, enumerations, vectors or records, declared in
-- packages and architectures, read with static and dynamic indices.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package composite_constants_pkg is
  type opcode_t is (op_nop, op_add, op_sub, op_jump);
  type cost_table_t is array (opcode_t) of natural;
  type rom_t is array (0 to 7) of unsigned(7 downto 0);
  type entry_t is record
    code  : opcode_t;
    width : natural;
    mask  : std_ulogic_vector(3 downto 0);
  end record;
  type entry_table_t is array (natural range <>) of entry_t;

  constant cost_c : cost_table_t := (op_nop => 1, op_jump => 4, others => 2);
  constant rom_c : rom_t := (
    x"11", x"22", x"33", x"44", x"55", x"66", x"77", x"88");
  constant entries_c : entry_table_t(0 to 2) := (
    (op_add, 8, "0011"), (op_sub, 16, "1100"), (op_jump, 32, "1010"));
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.composite_constants_pkg.all;

entity composite_constants is
end entity;

architecture sim of composite_constants is
  type grid_t is array (0 to 2, 0 to 3) of integer;
  constant grid_c : grid_t := (
    (0, 1, 2, 3), (10, 11, 12, 13), (20, 21, 22, 23));
  type squares_t is array (1 to 6) of integer;
  constant squares_c : squares_t := (1, 4, 9, 16, 25, 36);
  type state_t is (idle, fetch, decode, execute);
  type next_t is array (state_t) of state_t;
  constant next_c : next_t := (idle => fetch, fetch => decode,
                               decode => execute, execute => idle);

  signal addr  : unsigned(2 downto 0) := (others => '0');
  signal data  : unsigned(7 downto 0);
  signal state : state_t := idle;
  signal clk   : std_ulogic := '0';
  signal done  : boolean := false;
begin
  clk <= not clk after 5 ns when not done;
  data <= rom_c(to_integer(addr));

  fsm : process (clk)
  begin
    if rising_edge(clk) then
      state <= next_c(state);
      addr <= addr + 1;
    end if;
  end process;

  stim : process
    variable failures : natural := 0;
    variable total    : natural := 0;
    variable index    : natural;
    variable op       : opcode_t;

    procedure check(cond : boolean; msg : string) is
    begin
      if not cond then
        report "FAIL: " & msg severity error;
        failures := failures + 1;
      end if;
    end procedure;
  begin
    check(cost_c(op_nop) = 1 and cost_c(op_add) = 2
          and cost_c(op_jump) = 4, "enum-indexed package constant");
    for o in opcode_t loop
      total := total + cost_c(o);
    end loop;
    check(total = 9, "loop over enum-indexed constant");
    op := op_sub;
    check(cost_c(op) = 2, "dynamic enum index");
    check(rom_c(3) = x"44", "static rom index");
    index := 6;
    check(rom_c(index) = x"77", "dynamic rom index");
    check(grid_c(2, 1) = 21 and grid_c(1, 3) = 13, "2-d constant");
    total := 0;
    for i in squares_c'range loop
      total := total + squares_c(i);
    end loop;
    check(total = 91, "integer constant sum");
    check(entries_c(1).width = 16, "record element");
    check(entries_c(2).mask = "1010", "record vector element");
    check(entries_c(0).code = op_add, "record enum element");
    index := 2;
    check(entries_c(index).width = 32, "dynamic record index");
    check(next_c(next_c(idle)) = decode, "enum-valued table");

    wait until rising_edge(clk);
    wait for 1 ns;
    check(state = fetch, "fsm through constant table");
    check(data = x"22", "rom read in concurrent assignment");
    for step in 1 to 6 loop
      wait until rising_edge(clk);
    end loop;
    wait for 1 ns;
    check(state = execute, "fsm wraps");
    check(data = x"88", "rom read after wrap");
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    done <= true;
    wait;
  end process;
end architecture;
