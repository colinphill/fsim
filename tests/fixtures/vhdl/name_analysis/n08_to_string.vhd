-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 TO_STRING is predefined from VHDL-2008 on. Compiled with --standard 2008: success.
-- With --standard 1993 expected: FSIM-VHDL-NAME-001 at line 7.
entity n08 is
end entity;
architecture a of n08 is
  constant s : string := to_string(42);
begin
end architecture;
