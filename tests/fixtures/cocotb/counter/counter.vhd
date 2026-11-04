-- SPDX-License-Identifier: Apache-2.0
-- A counter with an enable of a generic level and a generic width, driven
-- through VHPI. A for-generate holds instances whose generic actual uses the
-- generate parameter; they are reached by name inside the generate.
library ieee;
use ieee.std_logic_1164.all;

entity tap is
  generic (CODE : std_logic_vector);
  port (o : out std_logic);
end entity;

architecture rtl of tap is
begin
  o <= CODE(CODE'right);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity counter is
  generic (WIDTH : positive := 4; ENABLE_HIGH : boolean := true);
  port (
    clk    : in  std_logic;
    rst    : in  std_logic;
    enable : in  std_logic;
    count  : out std_logic_vector(WIDTH - 1 downto 0)
  );
end entity;

architecture rtl of counter is
  signal value : unsigned(WIDTH - 1 downto 0) := (others => '0');
  signal taps : std_logic_vector(1 downto 0);
begin
  gen_tap : for i in 1 downto 0 generate
    inst_tap : entity work.tap
      generic map (CODE => std_logic_vector(to_unsigned(i, 2)))
      port map (o => taps(i));
  end generate gen_tap;
  process (clk) is
  begin
    if rising_edge(clk) then
      if rst = '1' then
        value <= (others => '0');
      elsif (enable = '1') = ENABLE_HIGH then
        value <= value + 1;
      end if;
    end if;
  end process;
  count <= std_logic_vector(value);
end architecture;
