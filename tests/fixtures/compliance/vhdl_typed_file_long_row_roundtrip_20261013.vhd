-- fsim: top=typed_file_long_row_roundtrip(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- The single typed-file element produces a row larger than the generic
-- SystemVerilog string limit while every serialized integer remains short.
entity typed_file_long_row_roundtrip is
end entity;

architecture sim of typed_file_long_row_roundtrip is
  type integer_vector is array (integer range <>) of integer;
  type vector_file is file of integer_vector;
  constant expected : integer_vector(0 to 699) := (others => 123456);
  signal passed : bit := '0';
begin
  test : process
    file output_file : vector_file;
    file input_file : vector_file;
    variable actual : integer_vector(0 to 699);
    variable element_count : natural;
  begin
    file_open(output_file, "typed-file-long-row.dat", write_mode);
    write(output_file, expected);
    file_close(output_file);

    file_open(input_file, "typed-file-long-row.dat", read_mode);
    read(input_file, actual, element_count);
    if element_count = expected'length
      and actual = expected and endfile(input_file) then
      passed <= '1';
      report "PASS";
    else
      report "FAIL: long typed-file row round trip" severity failure;
    end if;
    file_close(input_file);
    wait;
  end process;
end architecture;
