// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 3.12.1, 6.21, 8.9: the classes of a compilation unit's
// $unit scope belong to the design with the unit; their static properties
// are initialized before any initial procedure starts whether or not the
// design names the class, so the initializers of reg_a and reg_b, which
// nothing refers to, each register once with K.
class K;
  static int hits;
  static function int hit();
    hits++;
    return hits;
  endfunction
endclass

class reg_a;
  static int registered = K::hit();
endclass

class reg_b;
  static int registered = K::hit();
endclass

module top;
  initial begin
    if (K::hits == 2)
      $display("PASS");
    else
      $display("FAIL hits=%0d", K::hits);
  end
endmodule
