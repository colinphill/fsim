// SPDX-License-Identifier: Apache-2.0
// Same base name as lib_b/sysfunc_dir.sv, a call at the same line and column:
// a signed $random here (IEEE 1800-2023 38.15).
module md_a;
    int x;
    initial begin
        x = $random;
    end
endmodule
