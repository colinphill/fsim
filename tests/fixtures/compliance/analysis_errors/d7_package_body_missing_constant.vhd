-- fsim-expect: FSIM-FE-VHDECL-001 std=1993 after=d7_deferred_pkg.vhd
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 4.8: a package body gives the full declaration of each
-- deferred constant of its package; decided when the body is analyzed.
package body d7_deferred_pkg is
end package body;
