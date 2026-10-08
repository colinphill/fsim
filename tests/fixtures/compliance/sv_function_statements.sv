// fsim: top=sv_function_statements std=2017
// IEEE 1800-2017 13.4.1: a void function called as a statement, and a
// non-void function result discarded with void'(); 6.5: a variable written
// by procedural statements in several processes takes the last write.
module sv_function_statements;
  int calls = 0;
  int last;
  logic [3:0] lanes = 4'b0000;
  int failures = 0;

  function void bump(int amount);
    calls += amount;
  endfunction

  function int record(int value);
    last = value;
    return value * 2;
  endfunction

  initial begin
    bump(2);
    bump(3);
    void'(record(7));
    if (calls != 5) begin
      $display("FAIL: void function statements calls=%0d", calls);
      failures++;
    end
    if (last != 7) begin
      $display("FAIL: discarded function result last=%0d", last);
      failures++;
    end
  end

  initial #1 lanes[0] = 1'b1;
  initial #2 lanes[3:2] = 2'b11;
  initial #3 last = 11;

  initial begin
    #4;
    if (lanes !== 4'b1101 || last != 11) begin
      $display("FAIL: procedural writers lanes=%b last=%0d", lanes, last);
      failures++;
    end
    if (failures == 0)
      $display("PASS");
    $finish;
  end
endmodule
