-- fsim: top=typed_file_character_array_read_roundtrip_batch56(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Character-array typed-file images contain adjacent bytes with no field
-- separators between characters; each WRITE remains a separate file element.
entity typed_file_character_array_read_roundtrip_batch56 is
end entity;

architecture sim of typed_file_character_array_read_roundtrip_batch56 is
  type character_array is array (natural range <>) of character;
  type character_array_file is file of character_array;
begin
  test : process
    file output_file : character_array_file;
    file input_file : character_array_file;
    variable source_first : character_array(0 to 3) := ('1', 'a', '$', '+');
    variable source_second : character_array(1 to 3) := ('M', 'n', '?');
    variable actual_first : character_array(0 to 3);
    variable actual_second : character_array(1 to 3);
    variable length : natural;
  begin
    file_open(output_file, "typed-file-character-array.dat", write_mode);
    write(output_file, source_first);
    write(output_file, source_second);
    file_close(output_file);

    file_open(input_file, "typed-file-character-array.dat", read_mode);
    read(input_file, actual_first, length);
    assert length = 4
      report "first character-array typed-file length did not round-trip"
      severity failure;
    assert actual_first = source_first
      report "first character-array typed-file value did not round-trip"
      severity failure;

    read(input_file, actual_second, length);
    assert length = 3
      report "second character-array typed-file length did not round-trip"
      severity failure;
    assert actual_second = source_second
      report "second character-array typed-file value did not round-trip"
      severity failure;
    assert endfile(input_file)
      report "character-array typed file has an unexpected extra element"
      severity failure;

    file_close(input_file);
    report "PASS";
    wait;
  end process;
end architecture;
