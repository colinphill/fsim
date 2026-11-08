// SPDX-License-Identifier: Apache-2.0
// Compiled without that switch: the compatibility profiles differ, so the
// import is refused with FSIM-FE-STANDARD-003.
module tb_profile;
  import psz::*;
  initial $display("MIX profile %0d", N);
endmodule
