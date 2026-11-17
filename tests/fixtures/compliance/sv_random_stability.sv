// fsim: top=sv_random_stability std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 18.14.1, 18.13.4, 18.13.5: an object created with new is
// seeded with the next random value of the creating thread and nothing
// else, so two classes of one layout created from one thread state draw the
// same values; a forked thread is seeded with the next value of its parent;
// every initialization random number generator is seeded with the default
// seed, so two instances of one module seed their static processes and the
// objects of their static initializers alike.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
class sv_random_stability_a;
  rand int x;
  rand bit [7:0] y;
  constraint k { x inside {[1:100]}; }
endclass
class sv_random_stability_b;
  rand int x;
  rand bit [7:0] y;
  constraint k { x inside {[1:100]}; }
endclass
module sv_random_stability_leaf;
  sv_random_stability_a held = new;
  int drawn;
  int solved;
  initial begin
    drawn = $urandom;
    void'(held.randomize());
    solved = held.x;
  end
endmodule
module sv_random_stability;
  sv_random_stability_leaf u0(), u1();
  int failures;

  initial begin
    process self_process;
    string state;
    sv_random_stability_a a;
    sv_random_stability_b b;
    int first;
    int second;
    self_process = process::self();

    state = self_process.get_randstate();
    first = $urandom;
    self_process.set_randstate(state);
    second = $urandom;
    `CHECK("restored thread state", first == second)

    state = self_process.get_randstate();
    a = new;
    self_process.set_randstate(state);
    b = new;
    void'(a.randomize());
    void'(b.randomize());
    `CHECK("object seed is the thread's next value", a.x == b.x && a.y == b.y)

    state = self_process.get_randstate();
    fork first = $urandom; join
    self_process.set_randstate(state);
    fork second = $urandom; join
    `CHECK("forked thread seeded from its parent", first == second)

    #1;
    `CHECK("instances seed their static processes alike",
      u0.drawn == u1.drawn)
    `CHECK("instances seed their static initializers' objects alike",
      u0.solved == u1.solved && u0.solved >= 1 && u0.solved <= 100)

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
