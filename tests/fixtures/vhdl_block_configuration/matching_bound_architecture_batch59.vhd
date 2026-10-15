-- fsim-compile-only: std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 1.3.1: the nested block configuration names the exact
-- architecture selected by its fully bound component configuration.
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

entity configured_root is
end configured_root;

architecture top of configured_root is
  component adder
  end component;
begin
  A1: adder;
end top;

configuration configured_root_cfg of configured_root is
  for top
    for A1: adder use entity work.full_adder(structural);
      for structural
        for C1: and2 use entity work.and2g(behavior);
        end for;
      end for;
    end for;
  end for;
end configured_root_cfg;
