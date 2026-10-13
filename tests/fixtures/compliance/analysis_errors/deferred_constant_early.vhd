-- fsim-expect: FSIM-VHDL-SEM-130 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2008 4.8: a deferred constant is not named in a constant
-- declaration before its full declaration.
package deferred_constant_early is
  constant first : integer;
  constant second : integer;
end package;

package body deferred_constant_early is
  constant first : integer := second;
  constant second : integer := 1;
end package body;
