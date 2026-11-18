// fsim: top=sv_class_static_vif_member_specialization
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25.1, 8.9, 25.9: `C#(actuals)::p.m` selects member m of
// the interface instance that static property p of the specialization
// C#(actuals) designates. A static property inherited from a base
// specialization is read through the derived one (8.13), and each
// specialization of Tagged has its own handle (8.25):
//   DerivedStore#(virtual counter_if)::handle designates first (count 3),
//   Tagged#(1)::handle second (count 7), Tagged#(2)::handle first (3).
interface counter_if ();
  int count = 0;
endinterface

class Store #(type T = int);
  static T handle;
  static function void set(input T value);
    handle = value;
  endfunction
endclass

class DerivedStore #(type T = int) extends Store #(T);
endclass

class Tagged #(int N = 0);
  static virtual counter_if handle;
endclass

module sv_class_static_vif_member_specialization;
  counter_if first ();
  counter_if second ();
  int derived, one, two;
  initial begin
    first.count = 3;
    second.count = 7;
    DerivedStore#(virtual counter_if)::set(first);
    Tagged#(1)::handle = second;
    Tagged#(2)::handle = first;
    derived = DerivedStore#(virtual counter_if)::handle.count;
    one = Tagged#(1)::handle.count;
    two = Tagged#(2)::handle.count;
    if (derived == 3 && one == 7 && two == 3)
      $display("PASS");
    else
      $display("FAIL derived=%0d one=%0d two=%0d", derived, one, two);
  end
endmodule
