-- SPDX-License-Identifier: Apache-2.0
-- Prerequisite of d7_package_body_missing_constant: the package alone is
-- legal (IEEE 1076-2019 4.8: its body completes the deferred constant).
package d7_deferred_pkg is
  constant c : integer;
end package;
