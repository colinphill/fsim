-- fsim: top=vhdl_lexical_replacement_characters(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_lexical_replacement_characters is
end entity;

architecture sim of vhdl_lexical_replacement_characters is
  subtype byte is bit_vector(0 to 7);

  constant ordinary_string : string :=
      "double ""quote""; percent %; pipe |; bang !";
  constant ordinary_percent_pair : string := "%%";
  constant empty_replacement_string : string := %%;
  constant one_percent : string := %%%%;
  constant replacement_string : string := %outer %% percent; pipe |; bang !%;

  constant ordinary_hex_bits : bit_vector := X"FF";
  constant replacement_hex_bits : bit_vector := X%FF%;
  constant ordinary_octal_bits : bit_vector := o"17";
  constant replacement_octal_bits : bit_vector := o%17%;
  constant ordinary_binary_bits : bit_vector := b"10_01";
  constant replacement_binary_bits : bit_vector := b%10_01%;

  constant pipe_choices : byte := byte'(0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 => '1');
  constant exclamation_choices : byte := byte'(0 ! 1 ! 2 ! 3 ! 4 ! 5 ! 6 ! 7 => '1');
begin
  test : process
  begin
    assert ordinary_string = "double ""quote""; percent %; pipe |; bang !"
      report "ordinary quote strings retain doubled quotes and their contents"
      severity failure;
    assert ordinary_percent_pair = "%%"
      report "percent signs inside ordinary quote strings are unchanged"
      severity failure;
    assert empty_replacement_string = ""
      report "replacement delimiters form an empty string"
      severity failure;
    assert one_percent = "%"
      report "a doubled replacement delimiter encodes one percent sign"
      severity failure;
    assert replacement_string = "outer % percent; pipe |; bang !"
      report "replacement strings decode only doubled percent signs"
      severity failure;

    assert ordinary_hex_bits = replacement_hex_bits
      report "percent delimiters preserve hexadecimal bit-string values"
      severity failure;
    assert ordinary_octal_bits = replacement_octal_bits
      report "percent delimiters preserve octal bit-string values"
      severity failure;
    assert ordinary_binary_bits = replacement_binary_bits
      report "percent delimiters preserve binary bit-string values"
      severity failure;
    assert pipe_choices = exclamation_choices
      report "exclamation marks replace vertical-bar aggregate delimiters"
      severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
