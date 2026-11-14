-- SPDX-License-Identifier: Apache-2.0
--
-- The open kind of a file declaration and of FILE_OPEN is a static value of
-- FILE_OPEN_KIND (IEEE 1076-2019 6.4.2.5, 14.3): a constant holding
-- WRITE_MODE opens the file for writing as the literal does, and one holding
-- READ_MODE or APPEND_MODE likewise.
use std.textio.all;

entity top is
end entity;

architecture a of top is
  constant out_mode : file_open_kind := write_mode;
  constant in_mode : file_open_kind := read_mode;
  constant more_mode : file_open_kind := append_mode;
begin
  process
    file f : text open out_mode is "fok-out.txt";
    variable l : line;
    variable status : file_open_status;
  begin
    write(l, string'("first"));
    writeline(f, l);
    file_close(f);
    file_open(status, f, "fok-out.txt", more_mode);
    write(l, string'("second"));
    writeline(f, l);
    file_close(f);
    file_open(f, "fok-out.txt", in_mode);
    readline(f, l);
    report "fokind line 1 " & l.all;
    readline(f, l);
    report "fokind line 2 " & l.all;
    file_close(f);
    wait;
  end process;
end architecture;
