-- fsim: top=d7_analysis_mode_legal(a) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Legal designs that compile-time analysis (gate BD D7) must accept:
-- rules decided by a generic's value or an actual belong to elaboration
-- (IEEE 1076-2019 9.4.3: generics are globally static), and STANDARD type
-- marks, attributes and enumeration literals are locally static (9.4.2).
entity d7_sized is
  generic (n : positive := 3; pattern : bit_vector);
  port (done : out boolean);
end entity;

architecture a of d7_sized is
  type cells is array (0 to n - 1) of bit;
  signal q : cells := ('1', '0', '1', '1');
begin
  process
    variable count : natural := 0;
  begin
    for i in pattern'range loop
      if pattern(i) = q(i - pattern'low) then
        count := count + 1;
      end if;
    end loop;
    done <= count = pattern'length;
    wait;
  end process;
end architecture;

entity d7_analysis_mode_legal is
end entity;

architecture a of d7_analysis_mode_legal is
  type by_character is array (character range 'a' to 'c') of bit;
  type by_level is array (severity_level) of bit;
  signal sized_done : boolean;
begin
  sized : entity work.d7_sized
    generic map (n => 4, pattern => "1011")
    port map (done => sized_done);

  process
    variable c : by_character;
    variable l : by_level;
    variable b : bit := '1';
    variable hits : natural := 0;
  begin
    c := (character'('a') => '1', 'b' to by_character'high => '0');
    l := (note => '1', warning => '0', error => '0', failure => '1');
    case b is
      when bit'('0') => null;
      when bit'('1') => hits := hits + 1;
    end case;
    wait for 1 ns;
    if c('a') = '1' and c('c') = '0' and l(failure) = '1' and hits = 1
        and sized_done then
      report "PASS";
    else
      report "FAIL" severity failure;
    end if;
    wait;
  end process;
end architecture;
