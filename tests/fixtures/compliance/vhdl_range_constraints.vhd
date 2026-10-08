-- fsim: top=range_constraints(sim) std=1993
-- IEEE 1076-1993 3.2.1.1 and 4.2: index constraints given by a range
-- attribute, a discrete subtype name or `T range L to R`; attributes and
-- locals of unconstrained formals inside subprograms; the optional reserved
-- word COMPONENT in a component instantiation (9.6).
entity range_leaf is
  port (d : in bit_vector(0 to 5); q : out natural);
end entity;

architecture rtl of range_leaf is
begin
  process (d)
    variable count : natural;
  begin
    count := 0;
    for i in d'range loop
      if d(i) = '1' then
        count := count + 1;
      end if;
    end loop;
    q <= count + d'length;
  end process;
end architecture;

entity range_constraints is
end entity;

architecture sim of range_constraints is
  function reverse(vec : bit_vector) return bit_vector is
    variable result : bit_vector(vec'range);
  begin
    for i in vec'range loop
      result(i) := vec(vec'left + vec'right - i);
    end loop;
    return result;
  end function;

  function rotate(vec : bit_vector) return bit_vector is
    variable tmp : bit_vector(vec'reverse_range);
    variable result : bit_vector(vec'range);
  begin
    tmp := vec;
    for i in tmp'range loop
      result(i) := tmp(i);
    end loop;
    assert tmp'left = vec'right report "reverse_range left" severity error;
    return result;
  end function;

  function ones(vec : bit_vector) return natural is
    variable n : natural := 0;
  begin
    for i in vec'reverse_range loop
      if vec(i) = '1' then
        n := n + 1;
      end if;
    end loop;
    return n;
  end function;

  subtype nibble_index is integer range 7 downto 4;
  type bit_memory is array (integer range <>) of bit;
  subtype upper_nibble is bit_memory(nibble_index);
  subtype window is bit_memory(integer range 2 to 5);

  component range_leaf is
    port (d : in bit_vector(0 to 5); q : out natural);
  end component;

  signal up   : bit_vector(0 to 3) := "1000";
  signal down : bit_vector(3 downto 0) := "1000";
  signal wide : bit_vector(0 to 5) := "101101";
  signal leaf_q : natural;
begin
  leaf : component range_leaf port map (d => wide, q => leaf_q);

  process
    variable m : upper_nibble;
    variable w : window;
    variable copy : bit_vector(up'range);
    variable failures : natural := 0;
  begin
    if reverse(up) /= "0001" then
      report "FAIL: reverse ascending" severity error;
      failures := failures + 1;
    end if;
    if reverse(down) /= "0001" then
      report "FAIL: reverse descending" severity error;
      failures := failures + 1;
    end if;
    if rotate(down) /= "0001" then
      report "FAIL: reverse_range copy" severity error;
      failures := failures + 1;
    end if;
    if ones(wide) /= 4 then
      report "FAIL: reverse_range loop" severity error;
      failures := failures + 1;
    end if;
    if m'left /= 7 or m'right /= 4 or m'length /= 4 then
      report "FAIL: subtype name constraint" severity error;
      failures := failures + 1;
    end if;
    if w'left /= 2 or w'right /= 5 or not w'ascending then
      report "FAIL: type range constraint" severity error;
      failures := failures + 1;
    end if;
    copy := up;
    if not copy'ascending or copy(0) /= '1' then
      report "FAIL: signal range constraint" severity error;
      failures := failures + 1;
    end if;
    wait for 1 ns;
    if leaf_q /= 10 then
      report "FAIL: component instance" severity error;
      failures := failures + 1;
    end if;
    if failures = 0 then
      report "PASS";
    else
      report "FAIL count " & integer'image(failures) severity error;
    end if;
    wait;
  end process;
end architecture;
