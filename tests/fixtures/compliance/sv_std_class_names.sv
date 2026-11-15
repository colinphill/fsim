// fsim: top=sv_std_class_names std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 26.7: the std package is wildcard imported into every
// compilation unit, so a type the design declares or imports under the name
// of a std class hides that class where it is visible, and nowhere else.
// process is no reserved word (Annex B): it names a std class and can be
// declared as any other identifier.
package sv_std_class_names_pkg;
  class mailbox;
    int items[$];
    function void put_user(int value);
      items.push_back(value);
    endfunction
    function int count();
      return items.size();
    endfunction
  endclass

  class process;
    static function int self();
      return 42;
    endfunction
  endclass
endpackage

// Design declarations hide std::semaphore, std::mailbox, std::process and
// std::randomize.
module sv_std_class_names_user;
  import sv_std_class_names_pkg::*;
  typedef int semaphore;
  semaphore counter = 5;
  mailbox box;
  int seen;
  int selfish;
  int bumped;

  // A subroutine of this name hides std::randomize (18.12).
  function automatic int randomize(input int value);
    return value + 1;
  endfunction

  initial begin
    box = new;
    box.put_user(3);
    box.put_user(4);
    seen = box.count();
    counter++;
    selfish = process::self();
    bumped = randomize(1);
  end
endmodule

class sv_std_class_names_holder #(int N = 0);
  static int n = N;
endclass

// A variable named process, and a value parameter named mailbox used as a
// parameter actual.
module sv_std_class_names_variable;
  localparam int mailbox = 3;
  int process;
  int held;

  initial begin
    process = 11;
    held = sv_std_class_names_holder #(mailbox)::n;
  end
endmodule

module sv_std_class_names;
  int failures;
  semaphore key = new(1);
  mailbox #(int) box = new;
  process handles[2];
  std::process qualified;

  sv_std_class_names_user user ();
  sv_std_class_names_variable variable ();

  task automatic check(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d, want %0d", what, got, want);
      failures++;
    end
  endtask

  initial begin
    int value;
    handles[0] = process::self();
    qualified = std::process::self();
    if (handles[0] != qualified) begin
      $display("FAIL process::self and std::process::self differ");
      failures++;
    end
    check("process status", handles[0].status(), process::RUNNING);
    check("std process state", std::process::FINISHED, 0);
    box.put(9);
    box.get(value);
    check("std mailbox", value, 9);
    key.get(1);
    key.put(1);
    check("std semaphore try_get", key.try_get(1), 1);
    #1;
    check("user typedef semaphore", user.counter, 6);
    check("user class mailbox", user.seen, 2);
    check("user class process", user.selfish, 42);
    check("user function randomize", user.bumped, 2);
    check("variable named process", variable.process, 11);
    check("value parameter named mailbox", variable.held, 3);
    if (failures == 0) $display("PASS");
  end
endmodule
