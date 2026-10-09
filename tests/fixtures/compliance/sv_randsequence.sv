// fsim: top=sv_randsequence std=2017
// IEEE 1800-2017 18.17: randsequence with weights, if, case, repeat, rand
// join, break, return and production arguments; zero weights make every
// selection deterministic. xsim 2025.2 does not simulate randsequence; the
// expected sums follow 18.17.1-18.17.7.
module sv_randsequence;
  int failures = 0;

  function automatic int sequence_sum(input bit stop_early);
    int x = 0;
    int mode = 2;
    randsequence (main)
      main : first second third;
      first : { x = x + 1; } | { x = x + 1000; } := 0;
      second : if (stop_early) quit else more;
      more : repeat (3) add(10);
      quit : { break; };
      third : case (mode) 1 : add(100); 2 : add(200); default : add(400); endcase
        fourth;
      fourth : rand join add(1) add(2);
      void add(int y) : { x = x + y; if (y > 0) return; x = x + 5000; };
    endsequence
    return x;
  endfunction

  initial begin
    if (sequence_sum(1'b0) != 1 + 30 + 200 + 3) begin
      $display("FAIL randsequence: %0d", sequence_sum(1'b0));
      failures++;
    end
    if (sequence_sum(1'b1) != 1) begin
      $display("FAIL randsequence break: %0d", sequence_sum(1'b1));
      failures++;
    end
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
