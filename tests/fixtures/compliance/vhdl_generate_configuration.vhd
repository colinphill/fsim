-- fsim: top=generate_configuration_cfg std=2008
-- IEEE 1076-2008 3.4.2: a block configuration of a for-generate selects a
-- static discrete range of parameter values, with bounds given by package
-- constants and scalar attributes; 12.3: a component declaration in a block
-- hides the architecture's homograph; 5.2.2: SEVERITY_LEVEL ports and 'VAL.
package generate_configuration_pkg is
  constant zero : integer := 0;
  subtype index_t is integer range 0 to 3;
end package;

entity generate_configuration_cell is
  port (
    level_in  : in severity_level;
    level_out : out severity_level;
    tag_out   : out character
  );
end entity;

architecture plain of generate_configuration_cell is
begin
  level_out <= level_in;
  tag_out <= 'p';
end architecture;

architecture marked of generate_configuration_cell is
begin
  level_out <= severity_level'val(severity_level'pos(level_in) + 1);
  tag_out <= 'm';
end architecture;

use work.generate_configuration_pkg.all;
entity generate_configuration is
end entity;

architecture sim of generate_configuration is
  type levels_t is array (0 to 3) of severity_level;
  type tags_t is array (0 to 3) of character;
  signal levels : levels_t;
  signal results : levels_t;
  signal tags : tags_t;

  component generate_configuration_cell
    port (
      level_in  : in integer;
      level_out : out integer;
      tag_out   : out character
    );
  end component;
begin
  inner : block
    component generate_configuration_cell
      port (
        level_in  : in severity_level;
        level_out : out severity_level;
        tag_out   : out character
      );
    end component;
  begin
    g : for i in index_t generate
      cell : generate_configuration_cell
        port map (levels(i), results(i), tags(i));
    end generate;
  end block;

  process
  begin
    levels <= (note, warning, note, warning);
    wait for 1 ns;
    if tags = ('m', 'm', 'p', 'p')
      and results = (warning, error, note, warning) then
      report "PASS";
    else
      report "FAIL" severity error;
    end if;
    wait;
  end process;
end architecture;

use work.generate_configuration_pkg.all;
configuration generate_configuration_cfg of generate_configuration is
  for sim
    for inner
      for g(zero to index_t'low + 1)
        for cell : generate_configuration_cell
          use entity work.generate_configuration_cell(marked);
        end for;
      end for;
      for g(2 to index_t'high)
        for cell : generate_configuration_cell
          use entity work.generate_configuration_cell(plain);
        end for;
      end for;
    end for;
  end for;
end configuration;
