-- fsim: top=vhdl_time_unit_names_coarse_batch58(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_time_unit_names_coarse_batch58 is end;
architecture test of vhdl_time_unit_names_coarse_batch58 is
  function shadowed_unit return integer is
    variable ps : integer := 7;
  begin
    return ps;
  end function;
begin
  process
  begin
    assert ps = 1 ps and ns = 1000 ps
      report "bare TIME units differ at a coarser project resolution"
      severity failure;
    assert time'image(ps) = "1000 fs" and shadowed_unit = 7
      report "TIME normalization changed an image or a shadowing declaration"
      severity failure;
    report "PASS";
    wait;
  end process;
end;
