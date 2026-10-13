-- fsim: top=vhdl_procedure_local_file_lifetime_batch56(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 4.3, 14.3: local file objects open and close per call.
library std;
use std.textio.all;

entity vhdl_procedure_local_file_lifetime_batch56 is
end entity;

architecture sim of vhdl_procedure_local_file_lifetime_batch56 is
  procedure write_record is
    file output_file : text open write_mode is "procedure-local-file.dat";
    variable output_line : line;
  begin
    write(output_line, string'("local"));
    writeline(output_file, output_line);
    return;
    assert false
      report "procedure-local file close epilogue was bypassed"
      severity failure;
  end procedure;

  procedure check_record is
    file input_file : text open read_mode is "procedure-local-file.dat";
    variable input_line : line;
    variable value : string(1 to 5);
    variable good : boolean;
  begin
    readline(input_file, input_line);
    read(input_line, value, good);
    assert good and value = "local"
      report "procedure-local file read returned the wrong value"
      severity failure;
    assert endfile(input_file)
      report "procedure-local file did not reach EOF after one line"
      severity failure;
  end procedure;
begin
  check : process
  begin
    write_record;
    check_record;
    write_record;
    check_record;
    report "PASS";
    wait;
  end process;
end architecture;
