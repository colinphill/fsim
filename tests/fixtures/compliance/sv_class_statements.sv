// fsim: top=sv_class_statements std=2017
// IEEE 1800-2017 13.4.1: class function methods called as statements and
// discarded with void'(); 8.9: a static property reached through an object
// handle; 11.4.12.2: string replication.
class counter;
  static int shared = 24;
  int value;
  function void set(int v);
    value = v;
  endfunction
  function int get();
    return value;
  endfunction
  function int bump();
    value = value + 1;
    return value;
  endfunction
endclass

module sv_class_statements;
  counter a, b;
  string text;
  int failures = 0;

  initial begin
    a = new;
    b = new;
    a.set(5);
    void'(a.bump());
    if (a.get() != 6) begin
      $display("FAIL: method statements value=%0d", a.get());
      failures++;
    end
    a.shared = 12;
    if (b.shared != 12 || counter::shared != 12) begin
      $display("FAIL: static property through a handle");
      failures++;
    end
    text = {3{"ab"}};
    if (text != "ababab") begin
      $display("FAIL: string replication %s", text);
      failures++;
    end
    if (failures == 0)
      $display("PASS");
    $finish;
  end
endmodule
