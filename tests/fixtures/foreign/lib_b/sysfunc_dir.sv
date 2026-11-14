// SPDX-License-Identifier: Apache-2.0
// Same base name as lib_a/sysfunc_dir.sv, a call at the same line and column:
// an unsigned $urandom here.
module md_b;
    bit [31:0] y;
    initial begin
        y = $urandom;
    end
endmodule
