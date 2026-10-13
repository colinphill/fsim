-- fsim: top=vhdl_aggregate_assignment_targets(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 8.4 and 8.5: positional and named aggregate targets
-- distribute one record or array-valued right-hand side to their elements.
entity vhdl_aggregate_assignment_targets is
end entity;

architecture sim of vhdl_aggregate_assignment_targets is
  type pair_t is record
    first : bit;
    second : bit;
  end record;
  subtype word_t is bit_vector(1 downto 0);
  subtype char_pair_t is string(1 to 2);
  signal output_word : word_t := "00";
begin
  process
    variable calls : natural := 0;
    variable first_value, second_value : bit := '0';
    variable first_character, second_character : character := ' ';
    variable pair_value : pair_t;
    impure function next_pair return pair_t is
    begin
      calls := calls + 1;
      return pair_t'('1', '0');
    end function;
    impure function next_word return word_t is
    begin
      calls := calls + 1;
      return word_t'("10");
    end function;
  begin
    (first_value, second_value) := word_t'('0', '1');
    assert first_value = '0' and second_value = '1'
      report "FAIL qualified positional tuple" severity error;
    (first_value, second_value) := bit_vector'("10");
    assert first_value = '1' and second_value = '0'
      report "FAIL qualified string tuple" severity error;
    (first_character, second_character) := char_pair_t'('c', 'c');
    assert first_character = 'c' and second_character = 'c'
      report "FAIL qualified constrained STRING tuple" severity error;
    (first_value, second_value) := next_pair;
    if first_value /= '1' or second_value /= '0' or calls /= 1 then
      report "FAIL positional record target" severity error;
    end if;

    pair_value := pair_t'('0', '1');
    (pair_value.second, pair_value.first) := pair_value;
    if pair_value.first /= '1' or pair_value.second /= '0' or calls /= 1 then
      report "FAIL positional aggregate target overlap snapshot" severity error;
    end if;

    pair_value := pair_t'('0', '1');
    (first => pair_value.second, second => pair_value.first) := pair_value;
    if pair_value.first /= '1' or pair_value.second /= '0' or calls /= 1 then
      report "FAIL named aggregate target overlap snapshot" severity error;
    end if;

    (pair_value.second, pair_value.first) := next_pair;
    if pair_value.first /= '0' or pair_value.second /= '1' or calls /= 2 then
      report "FAIL record member target" severity error;
    end if;

    (second => first_value, first => second_value) := next_pair;
    if first_value /= '0' or second_value /= '1' or calls /= 3 then
      report "FAIL named record target" severity error;
    end if;

    (output_word(1), output_word(0)) <= next_word after 1 ns;
    wait for 2 ns;
    if output_word /= "10" or calls /= 4 then
      report "FAIL array signal target" severity error;
    else
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
