// fsim: top=sv_enum_methods std=2017
// IEEE 1800-2017 6.19.5: enumeration methods first, last, next(N),
// prev(N), num and name, with or without parentheses, on typedef and
// anonymous enumerations; 6.19.2: literal ranges name[N] and name[N:M];
// 6.19: a type-name base with a packed dimension; 8.23: enumeration
// literals declared in a class are class constants; 11.4.11: a conditional
// with an unsigned alternative is unsigned.
class palette;
  typedef enum logic [2:0] { RED = 1, GREEN = 3, BLUE } color_t;
  enum integer { LOW = 10, HIGH } level;
  color_t color;
  function new();
    color = GREEN;
    level = HIGH;
  endfunction
endclass

module sv_enum_methods;
  typedef enum logic [2:0] { A = 1, B = 3, C = 6 } abc_t;
  typedef bit bit_t;
  abc_t e;
  enum bit [15:0] { X = 65535, Y = 65534, Z = 65533 } eu;
  enum { ADD = 10, SUB[3], JMP[6:8] } op;
  enum bit_t [7:0] { W0, W1 } wide;
  enum shortint { S1 = -1, S2 = -2 } es;
  int unsigned u = 10;
  int failures = 0;
  palette p;

  task check(input [63:0] got, input [63:0] expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %h expected %h", what, got, expected);
      failures++;
    end
  endtask

  initial begin
    int z;
    e = B;
    check(e.first, A, "first");
    check(e.last(), C, "last");
    check(e.next, C, "next");
    check(e.next(2), A, "next wraps");
    check(e.prev(), A, "prev");
    check(e.num, 3, "num");
    if (e.name != "B") begin
      $display("FAIL name");
      failures++;
    end
    e = abc_t'(2);
    if (e.name() != "") begin
      $display("FAIL name of a non-member");
      failures++;
    end
    eu = Y;
    check(eu.next, Z, "anonymous next");
    check(eu.num(), 3, "anonymous num");
    check(SUB0, 11, "literal range start");
    check(SUB2, 13, "literal range end");
    check(JMP8, 16, "literal range bounds");
    check(op.num, 7, "range literal count");
    check($bits(wide), 8, "type-name base with a dimension");
    es = S2;
    z = u ? es.first : u;
    check(z, 65535, "unsigned conditional alternative");
    p = new;
    check(p.color, palette::GREEN, "class enum literal in a method");
    check(p.level, 11, "class anonymous enum literal");
    check(p.BLUE, 4, "class enum literal through a handle");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
