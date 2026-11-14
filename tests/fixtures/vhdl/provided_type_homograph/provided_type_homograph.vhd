-- SPDX-License-Identifier: Apache-2.0
--
-- Types the design declares with the simple names SFIXED, UFIXED and FLOAT
-- are not the types of IEEE.FIXED_PKG and IEEE.FLOAT_PKG (IEEE 1076-2019
-- 12.3, 12.4): they get no fixed-point or floating-point lowering, and the
-- operators the design declares for them run as written. The operators
-- below OR their operands element by element, which no arithmetic does.
entity top is
end entity;

architecture a of top is
  type sfixed is array (integer range <>) of bit;
  type ufixed is array (integer range <>) of bit;
  type float is array (integer range <>) of bit;

  subtype s4 is sfixed(3 downto 0);
  subtype u6 is ufixed(3 downto -2);
  subtype f4 is float(3 downto 0);

  function "+" (l, r : s4) return s4 is
    variable v : s4;
  begin
    for i in l'range loop
      v(i) := l(i) or r(i);
    end loop;
    return v;
  end function;

  function "+" (l, r : u6) return u6 is
    variable v : u6;
  begin
    for i in l'range loop
      v(i) := l(i) or r(i);
    end loop;
    return v;
  end function;

  function "+" (l, r : f4) return f4 is
    variable v : f4;
  begin
    for i in l'range loop
      v(i) := l(i) or r(i);
    end loop;
    return v;
  end function;

  signal sa : s4 := "0101";
  signal sb : s4 := "0011";
  signal ua : u6 := "010100";
  signal ub : u6 := "001010";
  signal fa : f4 := "1000";
  signal fb : f4 := "0001";
begin
  process
    variable s : s4;
    variable u : u6;
    variable f : f4;
  begin
    s := sa + sb;
    report "pthom sfixed "
      & bit'image(s(3)) & bit'image(s(2)) & bit'image(s(1)) & bit'image(s(0));
    u := ua + ub;
    report "pthom ufixed "
      & bit'image(u(3)) & bit'image(u(2)) & bit'image(u(1)) & bit'image(u(0))
      & bit'image(u(-1)) & bit'image(u(-2));
    f := fa + fb;
    report "pthom float "
      & bit'image(f(3)) & bit'image(f(2)) & bit'image(f(1)) & bit'image(f(0));
    wait;
  end process;
end architecture;
