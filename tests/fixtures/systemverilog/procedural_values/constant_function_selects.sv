// SPDX-License-Identifier: Apache-2.0
// A function call in a select index runs when the select is evaluated
// (IEEE 1800-2023 11.5.1, 13.4.3): a function that reads a variable
// declared outside it is not a constant function, so its call is never
// folded to that variable's initial value; a compound assignment evaluates
// its target's index once (11.4.1). A constant function's call in a
// continuous assignment's select is a constant expression (10.3), so the
// two assignments drive distinct bits of the variable (6.5).
int x = 0;

function int identity(input int v);
  return v;
endfunction

module top;
  int arr[5];
  bit [4:0] a_in;
  bit [4:0] a_out;

  function int bump();
    x += 1;
    return 1;
  endfunction

  function int read_x();
    return x;
  endfunction

  assign a_out[identity(3)] = a_in[identity(3)];
  assign a_out[1] = a_in[1];

  initial begin
    arr[bump()] = 5;
    $display("x %0d arr1 %0d", x, arr[1]);
    arr[bump()] += 2;
    $display("x %0d arr1 %0d", x, arr[1]);
    x = 3;
    arr[read_x()] = 9;
    $display("arr3 %0d arr0 %0d", arr[3], arr[0]);
    a_in = 5'b01010;
    #1 $display("a_out %b", a_out);
  end
endmodule
