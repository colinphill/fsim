// fsim: top=sv_patterns_and_aggregates std=2017
// IEEE 1800-2017 21.2.1.7: %p assignment patterns in $sformatf; 7.2: members
// of an unpacked structure variable, including string and real members;
// 10.9 and 6.8: container declaration initializers; 10.10: unpacked array
// concatenation into queues and dynamic arrays; 6.22.3: an integral value
// assigned to a packed structure; 6.7.1: a net of a packed structure type;
// 5.9: string literals and $random inside a concatenation; 12.5: case
// comparison signedness and real case items; 6.20.2: real parameters with
// integral initializers; 13.4.3: a recursive constant function; 7.4.2:
// C-style sizes in later unpacked dimensions.
module sv_patterns_and_aggregates;
  typedef struct { int a; logic [3:0] b; string s; real r; } record_t;
  typedef struct packed { logic [7:0] high; logic [7:0] low; } word_t;
  typedef enum { RED, GREEN } color_t;

  record_t rec;
  word_t word;
  wire struct packed { logic [3:0] x; logic [3:0] y; } pair = 8'h5a;
  int q[$] = '{1, -2, 3};
  int fixed_values[2][3] = '{'{1, 2, 3}, '{4, 5, 6}};
  logic [3:0] defaults[4] = '{default: 4'hf};
  string names[2] = '{"a", "b"};
  int dyn[];
  string text_queue[$];
  color_t color = GREEN;
  int failures = 0;

  localparam real half = 1;
  localparam integer limit = half * 4;

  function integer factorial(input integer n);
    if (n > 1) factorial = n * factorial(n - 1);
    else factorial = n;
  endfunction
  localparam integer six_factorial = factorial(6);

  function [2:0] classify(input signed [2:0] value);
    case (value)
      4'b1100: classify = 1;
      3'sb100: classify = 2;
      2'sb10:  classify = 3;
      default: classify = 4;
    endcase
  endfunction

  function [2:0] classify_real(input real value);
    case (value)
      1:    classify_real = 1;
      -1.0: classify_real = 2;
      default: classify_real = 3;
    endcase
  endfunction

  task check(input string got, input string expected, input string what);
    if (got != expected) begin
      $display("FAIL %s: got '%s' expected '%s'", what, got, expected);
      failures++;
    end
  endtask

  task check_int(input int got, input int expected, input string what);
    if (got != expected) begin
      $display("FAIL %s: got %0d expected %0d", what, got, expected);
      failures++;
    end
  endtask

  initial begin
    int value;
    rec.a = 3;
    rec.b = 4'ha;
    rec.s = "z";
    rec.r = 2;
    check($sformatf("%p", rec), "'{a:3,b:10,s:\"z\",r:2}", "structure pattern");
    check($sformatf("%0d %s %0g", rec.a, rec.s, rec.r), "3 z 2",
        "structure members");
    check($sformatf("%p", q), "'{1,-2,3}", "queue initializer");
    check($sformatf("%p", fixed_values), "'{'{1,2,3},'{4,5,6}}",
        "multidimensional initializer");
    check($sformatf("%p", defaults), "'{15,15,15,15}", "default initializer");
    check($sformatf("%p %p", names, color), "'{\"a\",\"b\"} GREEN",
        "string array and enumeration patterns");
    q = {q, 4};
    q = {0, q};
    dyn = {q, fixed_values[1][0]};
    text_queue = {"x", names[1]};
    check($sformatf("%p %p %p", q, dyn, text_queue),
        "'{0,1,-2,3,4} '{0,1,-2,3,4,4} '{\"x\",\"b\"}",
        "unpacked array concatenation");
    q = {};
    check_int(q.size(), 0, "empty concatenation");
    word = 16'haa55;
    check_int(word.high, 8'haa, "integral value to packed structure");
    #1;
    check_int(pair.y, 4'ha, "net of a packed structure type");
    value = {8'h01, "C"};
    check_int(value, 16'h0143, "string literal in a concatenation");
    value = {$random} % 1;
    check_int(value, 0, "$random in a concatenation");
    check_int(classify(3'sb100), 2, "unsigned case context");
    check_int(classify(3'sb010), 3, "unsigned case zero extension");
    check_int(classify_real(1.0), 1, "real case expression");
    check_int(classify_real(-1.0), 2, "real case item");
    check($sformatf("%0g %0d", half, limit), "1 4", "real parameter");
    check_int(six_factorial, 720, "recursive constant function");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
