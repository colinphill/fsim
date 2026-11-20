-- SPDX-License-Identifier: Apache-2.0
-- Prerequisite of d7_package_body_missing_procedure: the package alone is
-- legal (IEEE 1076-2019 4.7: its body gives the subprogram body).
package d7_procedure_pkg is
  procedure p(x : integer);
end package;
