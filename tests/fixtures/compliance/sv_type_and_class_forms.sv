// fsim: top=sv_type_and_class_forms std=2017
// IEEE 1800-2017 20.6.2: $bits of a data type; 7.4.1: a packed array of a
// named vector type; 23.2.2.3: a net port of a named type; 13.3: a classic
// task argument typed by a later declaration; 8.6: class property writes,
// compound updates and constructors in a class declared in a module;
// 21.2.1.3: %e/%f/%g precision and their six-digit default; 21.3.1:
// $fopen returns zero for a file it cannot open.
typedef logic [3:0] nibble_t;
typedef nibble_t [2:0] triple_t;

module sv_type_and_class_forms_child(input wire nibble_t n, output nibble_t m);
  assign m = ~n;
endmodule

module sv_type_and_class_forms;
  class counter;
    int count;
    function new();
      count = 2;
    endfunction
    task add(int amount);
      count += amount;
    endtask
  endclass

  typedef enum integer { IDLE, BUSY } state_t;
  int failures = 0;
  triple_t t;
  nibble_t [1:0] pair;
  nibble_t n = 4'h3, m;
  counter c;
  string text;

  sv_type_and_class_forms_child child(n, m);

  task check(input [63:0] got, input [63:0] expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %h expected %h", what, got, expected);
      failures++;
    end
  endtask

  task classic;
    input state;
    state_t state;
    check($bits(state), 32, "classic argument data type");
    check(state, BUSY, "classic argument value");
  endtask

  initial begin
    integer fd;
    check($bits(int), 32, "$bits(int)");
    check($bits(integer), 32, "$bits(integer)");
    check($bits(byte), 8, "$bits(byte)");
    check($bits(logic [7:0]), 8, "$bits(logic [7:0])");
    check($bits(nibble_t), 4, "$bits(typedef)");
    t = 12'habc;
    check($bits(t), 12, "named packed array width");
    check(t[0], 4'hc, "named packed array element");
    check($bits(t[1]), 4, "named packed array element width");
    pair = 8'h5a;
    check(pair[1], 4'h5, "named packed declaration");
    c = new;
    c.add(5);
    check(c.count, 7, "module class compound update");
    classic(BUSY);
    text = $sformatf("%.2f %8.3f %e %g %f", 3.14159, 2.5, 3.14159, 3.14159,
        1.5);
    if (text != "3.14    2.500 3.141590e+00 3.14159 1.500000") begin
      $display("FAIL formats: '%s'", text);
      failures++;
    end
    fd = $fopen("no_such_directory/no_such_file.txt", "r");
    check(fd, 0, "$fopen failure");
    #1;
    check(m, 4'hc, "net port of a named type");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
