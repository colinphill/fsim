-- fsim-expect: FSIM-FE-VHDECL-004 std=1993 after=d7_procedure_pkg.vhd
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 4.7: a package body gives a subprogram body for each
-- subprogram declaration of its package; decided when the body is analyzed.
package body d7_procedure_pkg is
end package body;
