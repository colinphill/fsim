-- fsim: top=nested_package_instances(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.9: a package instance declared in a generic package is
-- elaborated once per instance of that package, with its generics. Names
-- through both instances (g1.nb.x) and through the inner one from the
-- outer package's body (nb.x) denote the enclosing instance's copy.
package inner_pkg is
  generic (m : integer);
  constant bc : integer := m * 3;
  signal bs : integer := m;
  function bf(x : integer) return integer;
  impure function read_bs return integer;
end package inner_pkg;

package body inner_pkg is
  function bf(x : integer) return integer is
  begin
    return x + m;
  end function;

  impure function read_bs return integer is
  begin
    return bs;
  end function;
end package body inner_pkg;

package outer_pkg is
  generic (n : integer);
  constant c : integer := n * 100;
  package nb is new work.inner_pkg generic map (m => n + 1);
  function f(x : integer) return integer;
  impure function nested_bs return integer;
end package outer_pkg;

package body outer_pkg is
  function f(x : integer) return integer is
  begin
    return n + x + nb.bc + nb.bf(1);
  end function;

  impure function nested_bs return integer is
  begin
    return nb.bs + nb.read_bs;
  end function;
end package body outer_pkg;

entity nested_package_instances is
end entity;

architecture sim of nested_package_instances is
  package g1 is new work.outer_pkg generic map (n => 1);
  package g2 is new work.outer_pkg generic map (n => 5);
begin
  process
  begin
    -- g1.nb has m = 2, g2.nb has m = 6.
    assert g1.c = 100 and g2.c = 500 report "FAIL c" severity failure;
    assert g1.nb.bc = 6 and g2.nb.bc = 18
      report "FAIL nb.bc " & integer'image(g1.nb.bc) & " "
        & integer'image(g2.nb.bc) severity failure;
    assert g1.nb.bf(0) = 2 and g2.nb.bf(0) = 6
      report "FAIL nb.bf" severity failure;
    -- f: n + x + nb.bc + nb.bf(1)
    assert g1.f(10) = 1 + 10 + 6 + 3 and g2.f(10) = 5 + 10 + 18 + 7
      report "FAIL f " & integer'image(g1.f(10)) & " "
        & integer'image(g2.f(10)) severity failure;
    assert g1.nb.bs = 2 and g2.nb.bs = 6
      report "FAIL nb.bs initial" severity failure;
    g1.nb.bs <= 77;
    wait for 1 ns;
    assert g1.nb.bs = 77 and g2.nb.bs = 6
      report "FAIL nb.bs " & integer'image(g1.nb.bs) & " "
        & integer'image(g2.nb.bs) severity failure;
    assert g1.nb.read_bs = 77 and g2.nb.read_bs = 6
      report "FAIL read_bs" severity failure;
    assert g1.nested_bs = 154 and g2.nested_bs = 12
      report "FAIL nested_bs " & integer'image(g1.nested_bs) & " "
        & integer'image(g2.nested_bs) severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
