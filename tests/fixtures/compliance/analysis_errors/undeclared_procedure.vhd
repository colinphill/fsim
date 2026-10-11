-- fsim-expect: FSIM-ELAB-VHPROC-014 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 10.7: a procedure call names a visible procedure.
entity undeclared_procedure is
end entity;

architecture a of undeclared_procedure is
begin
  process
  begin
    missing_procedure;
    wait;
  end process;
end architecture;
