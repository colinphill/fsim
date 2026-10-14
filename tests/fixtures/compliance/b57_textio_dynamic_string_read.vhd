-- fsim: top=b57_textio_dynamic_string_read(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 16.4: READ into dynamically constrained and null STRINGs.
use std.textio.all;

entity b57_textio_dynamic_string_read is
end entity;

architecture sim of b57_textio_dynamic_string_read is
begin
  check : process is
    procedure check_length(
      size_value : natural;
      expected : string;
      remaining : string) is
      variable l : line;
      variable value : string(1 to size_value);
      variable good : boolean := false;
    begin
      l := new string'("abcdef");
      read(l, value, good);
      assert good
        report "READ rejected a STRING target with a run-time bound"
        severity failure;
      assert value = expected
        report "READ did not assign exactly the constrained characters"
        severity failure;
      assert l.all = remaining
        report "READ consumed characters beyond the target bound"
        severity failure;
      deallocate(l);
    end procedure;
    procedure check_short_read(size_value : natural) is
      variable l : line;
      variable value : string(1 to size_value);
      variable good : boolean := true;
    begin
      l := new string'("ab");
      read(l, value, good);
      assert not good
        report "READ accepted a LINE shorter than the dynamic target"
        severity failure;
      assert l.all = ""
        report "failed short READ did not consume its accepted prefix"
        severity failure;
      deallocate(l);
    end procedure;
    variable requested_length : natural := 0;
  begin
    requested_length := 3;
    check_length(requested_length, "abc", "def");
    requested_length := 6;
    check_length(requested_length, "abcdef", "");
    requested_length := 0;
    check_length(requested_length, "", "abcdef");
    requested_length := 3;
    check_short_read(requested_length);
    report "PASS";
    wait;
  end process;
end architecture;
