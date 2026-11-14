-- SPDX-License-Identifier: Apache-2.0
--
-- A type declared in the architecture that is named BIT_VECTOR hides the
-- predefined STD.STANDARD.BIT_VECTOR (IEEE 1076-2019 12.3, 12.4): it is
-- another type, whose slices, elements, aggregates and record fields behave
-- as its own declaration says, beside the predefined type named by an
-- expanded name.
entity top is
end entity;

architecture a of top is
  type bit_vector is array (natural range <>) of bit;
  type rec is record
    f : bit_vector(1 downto 0);
  end record;
  signal s : bit_vector(3 downto 0) := "0111";
  signal t : std.standard.bit_vector(3 downto 0) := "1000";
  constant r : rec := (f => "01");
  constant c : bit_vector(1 downto 0) := (others => '1');
  constant d : bit_vector(1 downto 0) := (1 => '1', 0 => '0');
  constant e : std.standard.bit_vector(1 downto 0) := (others => '0');
  function width(x : bit_vector) return integer is
  begin
    return x'length;
  end function;
begin
  process
  begin
    report "bvhom s(1) " & bit'image(s(1));
    report "bvhom t(3) " & bit'image(t(3));
    report "bvhom r.f(0) " & bit'image(r.f(0));
    report "bvhom r.f(1) " & bit'image(r.f(1));
    report "bvhom c(0) " & bit'image(c(0));
    report "bvhom d(1) " & bit'image(d(1));
    report "bvhom d(0) " & bit'image(d(0));
    report "bvhom e(1) " & bit'image(e(1));
    report "bvhom slice s " & bit'image(s(2 downto 1)(1));
    report "bvhom slice t " & bit'image(t(2 downto 1)(1));
    report "bvhom width " & integer'image(width(s));
    wait;
  end process;
end architecture;
