// fsim: top=sv_sync_method_receivers std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 15.3, 15.4, 13.4.1: semaphore and mailbox methods called
// as statements and in expressions, on a module variable, on a class
// property named in a method and through an object handle, and with the
// result of a function method discarded (void'); a user class whose
// methods are named put and get keeps its own methods.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_sync_method_receivers;
  class user;
    int value;
    task put(int x);
      value = x;
    endtask
    function int get();
      return value + 1;
    endfunction
  endclass

  class holder;
    mailbox #(int) box = new;
    semaphore lock = new(1);
    task send(int x);
      box.put(x);
    endtask
    task take(output int x);
      lock.get();
      box.get(x);
      lock.put();
    endtask
  endclass

  semaphore keys = new(2);
  int failures;

  initial begin
    user object = new;
    holder link = new;
    int got;
    int ok;

    object.put(5);
    `CHECK("user class methods", object.get() == 6)
    keys.get(2);
    ok = keys.try_get(1);
    `CHECK("semaphore exhausted", ok == 0)
    keys.put(2);
    ok = keys.try_get();
    `CHECK("semaphore default key count", ok == 1)
    link.send(7);
    link.take(got);
    `CHECK("property receivers in methods", got == 7)
    link.box.put(8);
    link.box.get(got);
    `CHECK("property receiver through a handle", got == 8)
    ok = link.box.try_put(3);
    `CHECK("try_put through a handle", ok > 0 && link.box.num() == 1)
    void'(link.box.try_put(4));
    `CHECK("discarded try_put", link.box.num() == 2)
    void'(keys.try_get());
    ok = keys.try_get(2);
    `CHECK("discarded try_get", ok == 0)

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
