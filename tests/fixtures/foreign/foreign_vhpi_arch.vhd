-- SPDX-License-Identifier: Apache-2.0
-- A foreign architecture (IEEE 1076-2019 20.2.4.2): the VHPI program that
-- registers library archlib, model buf, implements it.
entity foreign_buf is
  port (a : in bit; y : out bit; v : out bit_vector(1 downto 0));
end entity;

architecture model of foreign_buf is
  attribute foreign of model : architecture is "VHPI archlib buf";
  signal state : bit := '0';
begin
end architecture;

entity top is
end entity;

architecture t of top is
  signal a : bit := '0';
  signal y : bit;
  signal v : bit_vector(1 downto 0);
  signal z : bit;
begin
  u1 : entity work.foreign_buf port map (a => a, y => y, v => v);
  -- The foreign model's output drives the surrounding VHDL.
  z <= y;
  stim : process
  begin
    wait for 10 ns;
    a <= '1';
    wait for 10 ns;
    a <= '0';
    wait;
  end process;
end architecture;
