-- fsim: top=vhdl_time_unit_names_batch58(test) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_time_unit_names_batch58 is end;
architecture test of vhdl_time_unit_names_batch58 is
begin
  process
  begin
    assert time'pos(fs) = 1 and ps = 1000 fs and ns = 1000 ps
      report "bare TIME units differ from equivalent physical literals"
      severity failure;
    assert time'image(fs) = "1 fs" and time'image(ps) = "1000 fs"
      report "bare TIME units use the wrong runtime scale" severity failure;
    report "PASS";
    wait;
  end process;
end;
