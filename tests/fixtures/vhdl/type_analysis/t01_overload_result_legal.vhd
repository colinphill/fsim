-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.5: overloading is resolved by the result type; each context selects one F.
-- Expected: success.
-- fsim-standard: 2008
entity t01 is
end entity;
architecture a of t01 is
  function f return integer is begin return 1; end function;
  function f return boolean is begin return true; end function;
  constant i : integer := f;
  constant b : boolean := f;
begin
end architecture;
