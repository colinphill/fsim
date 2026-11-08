// SPDX-License-Identifier: Apache-2.0
// The library binding properties of instances a configuration controls
// (IEEE 1800-2023 33.7): every instance below the configured design reports
// the configuration as vpiConfig, "library.cell" with the library the
// configuration was compiled into.
module leaf;
endmodule

module mid;
  leaf l0();
endmodule

module top;
  mid m();
  initial #1 $finish;
endmodule

config cfg;
  design work.top;
  default liblist work;
endconfig
