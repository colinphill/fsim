-- fsim-expect: FSIM-FE-VHCONFIG-002 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- The block specification denotes an existing architecture, but not the
-- architecture selected by this component binding.
entity and2g is
end and2g;

architecture behavior of and2g is
begin
end behavior;

entity full_adder is
end full_adder;

architecture structural of full_adder is
  component and2
  end component;
begin
  C1: and2;
end structural;

architecture alternate of full_adder is
  component and2
  end component;
begin
  C1: and2;
end alternate;

entity configuration_subject is
end configuration_subject;

architecture top of configuration_subject is
  component adder
  end component;
begin
  A1: adder;
end top;

configuration configuration_subject_cfg of configuration_subject is
  for top
    for A1: adder use entity work.full_adder(structural);
      for alternate
        for C1: and2 use entity work.and2g(behavior);
        end for;
      end for;
    end for;
  end for;
end configuration_subject_cfg;
