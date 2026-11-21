-- fsim: top=d8_construct_label_prefix(test) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 8.3: an expanded name whose prefix is the label of a
-- process, block or generate statement is allowed within that construct.
entity d8_construct_label_prefix is
end entity;

architecture test of d8_construct_label_prefix is
  signal s : integer := 0;
begin
  P1 : process
    variable v : integer := 1;
  begin
    P1.v := 5;
    s <= P1.v;
    wait for 1 ns;
    assert s = 5 severity failure;
    report "PASS";
    wait;
  end process;

  B1 : block
    constant d : integer := 3;
    signal t : integer := B1.d;
  begin
    process
    begin
      wait for 1 ns;
      assert t = 3 severity failure;
      wait;
    end process;
  end block;

  G1 : if true generate
    constant c : integer := 4;
    signal u : integer := G1.c;
  begin
    process
    begin
      wait for 1 ns;
      assert u = 4 severity failure;
      wait;
    end process;
  end generate;
end architecture;
