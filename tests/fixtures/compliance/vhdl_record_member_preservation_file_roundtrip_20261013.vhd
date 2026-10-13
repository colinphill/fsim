-- fsim: top=vhdl_record_member_preservation_file_roundtrip(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- A STRING record member assignment must preserve earlier fields, and a
-- typed file must round-trip the whole record including NUL and spaces.
entity vhdl_record_member_preservation_file_roundtrip is
end entity;

architecture sim of vhdl_record_member_preservation_file_roundtrip is
  subtype fixed_text is string(1 to 4);

  type record_t is record
    number : natural;
    text   : fixed_text;
  end record;
  type record_file is file of record_t;
begin
  test : process
    file output_file : record_file;
    file input_file : record_file;
    variable value : record_t;
    variable with_nul : record_t :=
      (number => 3, text => (character'val(0), ' ', ' ', 'X'));
    variable actual : record_t;
  begin
    file_open(output_file, "record-member-roundtrip.dat", write_mode);
    value.number := 1;
    value.text := "  X ";
    assert value.number = 1
      report "STRING member assignment changed another record field"
      severity failure;
    write(output_file, value);
    value.number := 2;
    value.text := " A  ";
    assert value.number = 2
      report "STRING member assignment changed another record field"
      severity failure;
    write(output_file, value);
    write(output_file, with_nul);
    file_close(output_file);

    file_open(input_file, "record-member-roundtrip.dat", read_mode);
    read(input_file, actual);
    assert actual.number = 1
      report "typed-file record number did not round-trip"
      severity failure;
    assert actual.text = "  X "
      report "typed-file record STRING with leading and trailing spaces did not round-trip"
      severity failure;
    read(input_file, actual);
    assert actual.number = 2
      report "typed-file record number did not round-trip"
      severity failure;
    assert actual.text = " A  "
      report "typed-file record STRING with leading and trailing spaces did not round-trip"
      severity failure;
    read(input_file, actual);
    assert actual.number = 3
      report "typed-file record number did not round-trip"
      severity failure;
    assert actual.text = (character'val(0), ' ', ' ', 'X')
      report "typed-file record STRING with NUL and spaces did not round-trip"
      severity failure;
    assert endfile(input_file)
      report "typed-file record file has an unexpected extra element"
      severity failure;
    file_close(input_file);
    report "PASS";
    wait;
  end process;
end architecture;
