-- fsim: top=environment_tee_and_flush(sim) std=2008
-- IEEE 1076-2008 16.5: STOP and FINISH made visible by a use clause of
-- STD.ENV; 16.4: TEE writes a line to a file and to OUTPUT; 5.5.2: FLUSH
-- of an open file, after which a second file object reads what was
-- written.
use std.env.all;
use std.textio.all;

entity environment_tee_and_flush is
end entity;

architecture sim of environment_tee_and_flush is
  type int_file is file of integer;
begin
  process
    file text_out : text;
    file values_out, values_in : int_file;
    variable l : line;
    variable value : integer;
    variable failures : natural := 0;
  begin
    file_open(text_out, "environment_tee.txt", write_mode);
    write(l, string'("tee line"));
    tee(text_out, l);
    if l'length /= 0 then
      failures := failures + 1;
    end if;
    file_close(text_out);
    file_open(text_out, "environment_tee.txt", read_mode);
    readline(text_out, l);
    if l.all /= "tee line" then
      failures := failures + 1;
    end if;
    file_close(text_out);

    file_open(values_out, "environment_flush.bin", write_mode);
    write(values_out, 42);
    flush(values_out);
    file_open(values_in, "environment_flush.bin", read_mode);
    read(values_in, value);
    if value /= 42 then
      failures := failures + 1;
    end if;
    file_close(values_in);
    file_close(values_out);

    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    finish;
    report "FAIL after finish" severity error;
    wait;
  end process;
end architecture;
