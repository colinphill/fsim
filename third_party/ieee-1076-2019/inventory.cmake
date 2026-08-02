# SPDX-License-Identifier: Apache-2.0

set(
  FSIM_IEEE_1076_2019_UPSTREAM
  "https://gitlab.com/IEEE-P1076/packages.git"
)
set(
  FSIM_IEEE_1076_2019_COMMIT
  "16a012320947d378611cc7457f64ed76cb52bac4"
)
set(FSIM_IEEE_1076_2019_TAG "1076-2019")

# Each entry is library|relative source|review stage. The order is the only
# supported analysis order: dependencies and declarations precede consumers
# and bodies. `predefined` entries are inventoried but are not ordinary parsed
# sources. Batch 120 advances the remaining stages independently.
set(
  FSIM_IEEE_1076_2019_SOURCES
  "std|std/standard.vhdl|predefined"
  "std|std/textio.vhdl|textio"
  "std|std/env.vhdl|environment"
  "std|std/reflection.vhdl|reflection"
  "ieee|ieee/std_logic_1164.vhdl|logic"
  "ieee|ieee/std_logic_1164-body.vhdl|logic"
  "ieee|ieee/std_logic_textio.vhdl|logic-utility"
  "ieee|ieee/numeric_bit.vhdl|numeric"
  "ieee|ieee/numeric_bit-body.vhdl|numeric"
  "ieee|ieee/numeric_bit_unsigned.vhdl|numeric"
  "ieee|ieee/numeric_bit_unsigned-body.vhdl|numeric"
  "ieee|ieee/numeric_std.vhdl|numeric"
  "ieee|ieee/numeric_std-body.vhdl|numeric"
  "ieee|ieee/numeric_std_unsigned.vhdl|numeric"
  "ieee|ieee/numeric_std_unsigned-body.vhdl|numeric"
  "ieee|ieee/math_real.vhdl|fixed-float-dependency"
  "ieee|ieee/math_real-body.vhdl|fixed-float-dependency"
  "ieee|ieee/math_complex.vhdl|fixed-float-dependency"
  "ieee|ieee/math_complex-body.vhdl|fixed-float-dependency"
  "ieee|ieee/fixed_float_types.vhdl|fixed-float-types"
  "ieee|ieee/fixed_generic_pkg.vhdl|fixed"
  "ieee|ieee/fixed_generic_pkg-body.vhdl|fixed"
  "ieee|ieee/fixed_pkg.vhdl|fixed"
  "ieee|ieee/float_generic_pkg.vhdl|float"
  "ieee|ieee/float_generic_pkg-body.vhdl|float"
  "ieee|ieee/float_pkg.vhdl|float"
)
