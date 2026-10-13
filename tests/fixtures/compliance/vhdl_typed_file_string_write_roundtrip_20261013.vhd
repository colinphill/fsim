-- fsim: top=typed_file_string_write_roundtrip(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- A file of constrained STRING elements uses typed-file element records;
-- each WRITE must remain a separate readable element.
entity typed_file_string_write_roundtrip is
end entity;

architecture sim of typed_file_string_write_roundtrip is
  subtype fixed_string is string(1 to 3);
  type string_file is file of fixed_string;
  signal passed : bit := '0';
begin
  test : process
    file output_file : string_file;
    file input_file : string_file;
    variable actual : fixed_string;
  begin
    file_open(output_file, "typed-file-string.dat", write_mode);
    write(output_file, "abc");
    write(output_file, "xyz");
    file_close(output_file);

    file_open(input_file, "typed-file-string.dat", read_mode);
    read(input_file, actual);
    if actual = "abc" then
      read(input_file, actual);
      if actual = "xyz" and endfile(input_file) then
        passed <= '1';
        report "PASS";
      else
        report "FAIL: second typed string element" severity failure;
      end if;
    else
      report "FAIL: first typed string element" severity failure;
    end if;
    file_close(input_file);
    wait;
  end process;
end architecture;
