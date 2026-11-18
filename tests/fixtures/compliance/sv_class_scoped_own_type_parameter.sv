// fsim: top=sv_class_scoped_own_type_parameter
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25.1: in a parameterized class and in its out-of-block
// declarations (8.24), `C::T` with the unadorned class name refers to the
// class's own parameter T as though named inside the class; no
// specialization is implied. In Box#(string), Box#(Box::T) is therefore
// Box#(string): create returns one and peer holds one. In Box#(int) it is
// Box#(int).
class Box #(type T = int);
  T value;
  Box#(Box::T) peer;
  extern static function Box#(T) create(T value);
endclass

function Box#(Box::T) Box::create(T value);
  Box #(T) made = new();
  made.value = value;
  return made;
endfunction

module sv_class_scoped_own_type_parameter;
  initial begin
    Box #(string) text;
    Box #(int) number;
    text = Box#(string)::create("one");
    text.peer = Box#(string)::create("two");
    number = Box#(int)::create(42);
    number.peer = Box#(int)::create(-5);
    if (text.value == "one" && text.peer.value == "two"
        && number.value == 42 && number.peer.value == -5)
      $display("PASS");
    else
      $display("FAIL text=%s peer=%s number=%0d peer=%0d", text.value,
        text.peer.value, number.value, number.peer.value);
  end
endmodule
