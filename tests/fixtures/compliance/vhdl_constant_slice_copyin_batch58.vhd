-- fsim: top=vhdl_constant_slice_copyin_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_constant_slice_copyin_batch58 is end entity;
architecture sim of vhdl_constant_slice_copyin_batch58 is
  subtype bit_vector_4 is bit_vector(0 to 3);
  subtype bit_vector_8 is bit_vector(0 to 7);
  constant source : bit_vector_8 := B"1010_1100";

  procedure check_slice(constant value : in bit_vector_4;
                       constant expected : in bit_vector_4) is
  begin
    assert value = expected
      report "constant slice copy-in changed the selected elements"
      severity failure;
  end procedure;
begin
  test : process
  begin
    check_slice(source(0 to 3), B"1010");
    check_slice(source(4 to 7), B"1100");
    report "PASS" severity note;
    wait;
  end process;
end architecture;
