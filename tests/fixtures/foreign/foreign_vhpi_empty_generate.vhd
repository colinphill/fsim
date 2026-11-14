-- SPDX-License-Identifier: Apache-2.0
-- Generate statement occurrences that hold no signal, instance or process
-- (IEEE 1076-2019 14.5.3, 19.4.2): each value of a for generate statement's
-- range and the chosen alternative of an if or case generate statement is a
-- region, and a null range or a false condition without an else part makes
-- none.
entity top is
end entity;

architecture rtl of top is
  signal s : bit;
begin
  -- A null range: no occurrence.
  g : for i in 1 to 0 generate
    signal x : bit;
  begin
    x <= s;
  end generate;

  -- Two occurrences that hold a constant and a null-range generate only.
  h : for i in 0 to 1 generate
    constant c : integer := i + 10;
  begin
    e : for j in 3 downto 4 generate
      s <= '1';
    end generate;
  end generate;

  -- An if generate whose chosen body declares a constant only, and one
  -- whose condition is false.
  k : if true generate
    constant d : integer := 7;
  begin
  end generate;
  m : if false generate
    s <= '0';
  end generate;

  -- An if generate whose condition is false is its else alternative, an
  -- elsif alternative is one of the statement too (11.8), and a case
  -- generate statement is its chosen alternative.
  x : if false generate
    constant a : integer := 1;
  begin
  else generate
    constant b : integer := 2;
  begin
  end generate;
  y : if false generate
    constant a : integer := 1;
  begin
  elsif true generate
    constant e : integer := 3;
  begin
  else generate
    constant b : integer := 2;
  begin
  end generate;
  z : case 2 generate
    when 1 =>
      constant f : integer := 1;
    begin
    end;
    when 2 =>
      constant f : integer := 2;
    begin
    end;
    when others =>
  end generate;

  -- One occurrence holds a process through an if generate, the other an
  -- empty body.
  n : for i in 2 downto 1 generate
  begin
    q : if i = 2 generate
      p : process is
      begin
        wait;
      end process;
    end generate;
  end generate;
end architecture;
