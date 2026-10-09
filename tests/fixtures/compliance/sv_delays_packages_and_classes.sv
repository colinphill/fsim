// fsim: top=sv_delays_packages_and_classes std=2017
// IEEE 1800-2017 10.3.3 and 10.4.2: continuous and nonblocking assignments
// whose delay is a run-time expression; 11.5.1: a partially out-of-range
// part-select writes only the bits inside the range; 10.4.2: nonblocking
// part-select, delayed, and event-controlled writes to memory elements;
// 26.2-26.6: package variables shared through explicit, wildcard, and
// exported imports and by class scope; 8.5 and 8.7: a class string property
// read and written from a module, and a class handle initialized by new;
// 6.7.1: a net declaration assignment on a tri1 net.
package sv_delays_values;
  integer counter = 123;
endpackage

package sv_delays_reexport;
  import sv_delays_values::counter;
  export sv_delays_values::counter;
endpackage

module sv_delays_bumper;
  import sv_delays_values::*;
  initial #1 counter = counter + 1;
endmodule

module sv_delays_packages_and_classes;
  import sv_delays_reexport::counter;

  class labelled;
    string text = "start";
    int value;
    function new();
      value = 9;
    endfunction
  endclass

  labelled item = new;
  reg [7:0] period = 4;
  reg drive = 0;
  wire delayed_drive;
  assign #(period) delayed_drive = drive;
  tri1 [3:0] pulled = 4'h6;

  reg [3:0] vec;
  reg [7:0] mem [0:1];
  reg late;
  integer amount = 3;
  event go;
  int failures = 0;

  sv_delays_bumper bumper();

  task check(input int got, input int expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %0d expected %0d", what, got, expected);
      failures++;
    end
  endtask

  initial begin
    vec = 4'h0;
    vec[4:-1] = 6'b101010;
    check(vec, 4'b0101, "partially out-of-range part-select");
    vec[5:4] = 2'b11;
    check(vec, 4'b0101, "wholly out-of-range part-select");
    mem[0] = 8'h00;
    mem[1] = 8'h00;
    #10;
    // t = 10
    mem[0][3:0] <= 4'hc;
    mem[1] <= #2 8'h5a;
    late <= #(amount) 1'b1;
    mem[0][7:4] <= @(go) 4'h3;
    #1;
    // t = 11
    check(mem[0], 8'h0c, "nonblocking memory element part-select");
    check(mem[1], 8'h00, "delayed memory element write pending");
    -> go;
    #1;
    // t = 12 is the delayed update's own time step; look at t = 13.
    #1;
    check(mem[0], 8'h3c, "event-controlled memory element write");
    check(mem[1], 8'h5a, "delayed memory element write");
    check(delayed_drive, 0, "continuous run-time delay initial value");
    drive = 1;
    #3;
    // t = 16, the new value arrives at 17
    check(late, 1, "nonblocking run-time delay");
    check(delayed_drive, 0, "continuous run-time delay pending");
    #2;
    check(delayed_drive, 1, "continuous run-time delay");
    check(counter, 124, "exported package variable");
    check(sv_delays_values::counter, 124, "package-scoped variable");
    check(item.value, 9, "class handle initializer");
    if (item.text != "start") begin
      $display("FAIL class string property read: %s", item.text);
      failures++;
    end
    item.text = {item.text, "ed"};
    if (item.text != "started") begin
      $display("FAIL class string property write: %s", item.text);
      failures++;
    end
    check(pulled, 4'h6, "tri1 net declaration assignment");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
