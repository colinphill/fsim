-- fsim-expect: FSIM-ELAB-VHPROC-022 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 4.2.2.3, 6.5.7.1: the actual of a formal of class signal is a static signal name.
entity d7_signal_formal_variable_actual is
end entity;

architecture a of d7_signal_formal_variable_actual is
begin
  process
    variable v : bit := '0';
    procedure drive(signal s : out bit) is
    begin
      s <= '1';
    end procedure;
  begin
    drive(v);
    wait;
  end process;
end architecture;
