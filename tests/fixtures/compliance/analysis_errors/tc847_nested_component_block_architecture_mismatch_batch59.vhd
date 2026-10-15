-- fsim-expect: FSIM-FE-VHCONFIG-002 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 1.3.1: a nested block configuration under a component
-- configuration must name the architecture selected by the binding.
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

entity c01s03b01x00p08n01i00847ent is
end c01s03b01x00p08n01i00847ent;

architecture c01s03b01x00p08n01i00847arch
    of c01s03b01x00p08n01i00847ent is
  component adder
  end component;
begin
  A1: adder;
end c01s03b01x00p08n01i00847arch;

configuration c01s03b01x00p08n01i00847cfg
    of c01s03b01x00p08n01i00847ent is
  for c01s03b01x00p08n01i00847arch
    for A1: adder use entity work.full_adder(structural);
      for bad_block_spec
        for C1: and2 use entity work.and2g(behavior);
        end for;
      end for;
    end for;
  end for;
end c01s03b01x00p08n01i00847cfg;
