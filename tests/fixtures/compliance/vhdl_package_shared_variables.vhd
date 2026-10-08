-- fsim: top=package_shared_variables(sim) std=2008
-- IEEE 1076-2008 4.7, 5.6.2 and 6.4.2.4: a shared variable of a protected
-- type declared in a package (here its body) is one object shared by every
-- design unit that calls the package's subprograms.
package scoreboard_pkg is
  procedure record_pass(constant name : in string);
  procedure record_fail(constant name : in string);
  impure function passes return natural;
  impure function failures return natural;
end package;

package body scoreboard_pkg is
  type counters_t is protected
    procedure add_pass;
    procedure add_fail;
    impure function pass_count return natural;
    impure function fail_count return natural;
  end protected;

  type counters_t is protected body
    variable pass_total : natural := 0;
    variable fail_total : natural := 0;
    procedure add_pass is
    begin
      pass_total := pass_total + 1;
    end procedure;
    procedure add_fail is
    begin
      fail_total := fail_total + 1;
    end procedure;
    impure function pass_count return natural is
    begin
      return pass_total;
    end function;
    impure function fail_count return natural is
    begin
      return fail_total;
    end function;
  end protected body;

  shared variable counters : counters_t;

  procedure record_pass(constant name : in string) is
  begin
    counters.add_pass;
  end procedure;

  procedure record_fail(constant name : in string) is
  begin
    counters.add_fail;
    report "check failed: " & name severity note;
  end procedure;

  impure function passes return natural is
  begin
    return counters.pass_count;
  end function;

  impure function failures return natural is
  begin
    return counters.fail_count;
  end function;
end package body;

use work.scoreboard_pkg.all;

entity shared_checker is
  generic (count : natural);
end entity;

architecture sim of shared_checker is
begin
  process
  begin
    for i in 1 to count loop
      record_pass("leaf");
      wait for 1 ns;
    end loop;
    wait;
  end process;
end architecture;

use work.scoreboard_pkg.all;

entity package_shared_variables is
end entity;

architecture sim of package_shared_variables is
begin
  left_checker : entity work.shared_checker generic map (count => 3);
  right_checker : entity work.shared_checker generic map (count => 4);

  process
  begin
    record_pass("top");
    wait for 10 ns;
    if passes = 8 and failures = 0 then
      report "PASS";
    else
      report "FAIL passes=" & integer'image(passes)
        & " failures=" & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
