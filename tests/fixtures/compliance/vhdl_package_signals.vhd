-- fsim: top=package_signals(sim) std=2008
-- IEEE 1076-2008 4.7 and 6.4.2.3: signals declared in a package are single
-- design-wide objects, written by one instance and read by others; 12.4:
-- use clauses in package, entity and architecture declarative regions;
-- 3.2.3: an entity declarative signal.
package package_signals_types is
  subtype count_t is integer range 0 to 100;
end package;

package package_signals_pkg is
  use work.package_signals_types.all;
  signal shared_count : count_t := 3;
  signal ready : bit;
  signal level : severity_level := note;
end package;

entity package_signals_writer is
end entity;

architecture rtl of package_signals_writer is
  use work.package_signals_pkg.all;
begin
  process
  begin
    wait for 1 ns;
    shared_count <= 7;
    level <= error;
    ready <= '1';
    wait;
  end process;
end architecture;

entity package_signals is
  signal local_flag : boolean := false;
end entity;

architecture sim of package_signals is
  use work.package_signals_pkg.all;
  signal seen : natural := 0;
begin
  writer : entity work.package_signals_writer;

  -- A concurrent statement sensitive to a package signal.
  local_flag <= ready = '1';

  process (ready)
  begin
    if ready = '1' then
      seen <= shared_count;
    end if;
  end process;

  process
    variable failures : natural := 0;
  begin
    if shared_count /= 3 or ready /= '0' then
      report "FAIL initial values" severity error;
      failures := failures + 1;
    end if;
    wait on ready;
    wait for 0 ns;
    if shared_count /= 7 or level /= error or not local_flag then
      report "FAIL written values" severity error;
      failures := failures + 1;
    end if;
    wait for 0 ns;
    if seen /= 7 then
      report "FAIL process sensitive to a package signal" severity error;
      failures := failures + 1;
    end if;
    if severity_level'image(level) /= "error"
      or severity_level'pos(level) /= 2 then
      report "FAIL severity_level attributes" severity error;
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
