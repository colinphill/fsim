-- SPDX-License-Identifier: Apache-2.0
--
-- The "=" of a generic type is an operation of the subtype its actual
-- denotes (IEEE 1076-2019 5.8.2, 6.5.3.1, 14.3.3.3). COLORS declares its
-- own "=" for COLOR, which hides COLOR's predefined equality (12.3), so
-- COMPARE's instances for COLOR and for its subtype WARM call it; COLOR's
-- predefined "/=" is no homograph of it and stays. SHAPE has no explicit
-- "=", so its instance uses the predefined equality (9.2.3). COMPARE itself
-- makes no package visible: the instances do not depend on what the
-- instantiating unit's use clauses make visible (4.9), and the design runs
-- with and without TOP's `use work.colors.all;`, giving the same reports.
-- The same holds through a generic package whose actual is a generic type
-- of an enclosing generic package (NESTED), for the generic types of an
-- entity and of a block (14.3.3.3), and for a constant folded at
-- elaboration (TOKENS' "=" makes TOKEN values never equal). OWN_COMPARE
-- declares its own "=" for its generic type, which hides the generic type's
-- operator, and with it COLORS' "=", in its instances (12.3).
package colors is
  type color is (red, green, blue);
  function "=" (l, r : color) return boolean;
  subtype warm is color range red to green;
end package;

package body colors is
  function "=" (l, r : color) return boolean is
  begin
    report "gtop user = called";
    return false;
  end function;
end package body;

package tokens is
  type token is (open_token, close_token);
  function "=" (l, r : token) return boolean;
end package;

package body tokens is
  function "=" (l, r : token) return boolean is
  begin
    return false;
  end function;
end package body;

package shapes is
  type shape is (circle, square);
end package;

package compare is
  generic (type g);
  function equal (l, r : g) return boolean;
  function differ (l, r : g) return boolean;
end package;

package body compare is
  function equal (l, r : g) return boolean is
  begin
    return l = r;
  end function;

  function differ (l, r : g) return boolean is
  begin
    return l /= r;
  end function;
end package body;

package own_compare is
  generic (type g);
  function "=" (l, r : g) return boolean;
  function equal (l, r : g) return boolean;
end package;

package body own_compare is
  function "=" (l, r : g) return boolean is
  begin
    report "gtop own = called";
    return true;
  end function;

  function equal (l, r : g) return boolean is
  begin
    return l = r;
  end function;
end package body;

package nested is
  generic (type h);
  package inner is new work.compare generic map (g => h);
  function equal (l, r : h) return boolean;
end package;

package body nested is
  function equal (l, r : h) return boolean is
  begin
    return inner.equal(l, r);
  end function;
end package body;

entity same is
  generic (type g; left, right : g);
end entity;

architecture test of same is
begin
  process
  begin
    wait for 2 ns;
    report "gtop entity equal " & boolean'image(left = right);
    wait;
  end process;
end architecture;

entity top is
end entity;

use work.colors.all;
architecture test of top is
  package color_compare is new work.compare
    generic map (g => work.colors.color);
  package warm_compare is new work.compare
    generic map (g => work.colors.warm);
  package shape_compare is new work.compare
    generic map (g => work.shapes.shape);
  package color_nested is new work.nested
    generic map (h => work.colors.color);
  package shape_nested is new work.nested
    generic map (h => work.shapes.shape);
  package color_own_compare is new work.own_compare
    generic map (g => work.colors.color);
  package token_compare is new work.compare
    generic map (g => work.tokens.token);
  constant token_folded : boolean := token_compare.equal(
    work.tokens.open_token, work.tokens.open_token);
begin
  process
  begin
    report "gtop color equal "
      & boolean'image(color_compare.equal(work.colors.red, work.colors.red));
    report "gtop color differ "
      & boolean'image(color_compare.differ(work.colors.red, work.colors.red));
    report "gtop warm equal "
      & boolean'image(warm_compare.equal(work.colors.green, work.colors.green));
    report "gtop shape equal "
      & boolean'image(
        shape_compare.equal(work.shapes.circle, work.shapes.circle));
    report "gtop shape differ "
      & boolean'image(
        shape_compare.differ(work.shapes.circle, work.shapes.square));
    report "gtop nested color equal "
      & boolean'image(color_nested.equal(work.colors.blue, work.colors.blue));
    report "gtop nested shape equal "
      & boolean'image(
        shape_nested.equal(work.shapes.square, work.shapes.square));
    report "gtop token folded " & boolean'image(token_folded);
    report "gtop own equal "
      & boolean'image(
        color_own_compare.equal(work.colors.red, work.colors.green));
    wait;
  end process;

  entity_generic : entity work.same
    generic map (
      g => work.colors.color,
      left => work.colors.green,
      right => work.colors.green);

  block_generic : block is
    generic (type b; left, right : b);
    generic map (
      b => work.colors.color,
      left => work.colors.red,
      right => work.colors.red);
  begin
    process
    begin
      wait for 1 ns;
      report "gtop block equal " & boolean'image(left = right);
      wait;
    end process;
  end block;
end architecture;
