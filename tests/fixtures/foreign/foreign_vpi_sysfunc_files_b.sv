// SPDX-License-Identifier: Apache-2.0
// The second file of foreign_vpi_sysfunc_files_a.sv: the call is at the same
// line and column as there, and unsigned.
module mb;
    bit [31:0] y;
    initial begin
        y = $urandom;
    end
endmodule
