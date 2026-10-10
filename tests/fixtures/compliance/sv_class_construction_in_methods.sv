// fsim: top=sv_class_construction_in_methods std=2017
// IEEE 1800-2017 8.7: `new` inside class methods (a singleton built by a
// static function) and property initializers that are not literals, run
// after the base constructor; 8.9: a static class-handle property
// initialized by `new`; 8.25: a parameterized class named without actuals
// is its default specialization; 13.3: a class task with a delay called
// from a package task; 6.19: an enumeration literal of a typedef declared
// outside the class as a property initializer.
typedef enum bit [4:0] { FIRST = 1, SECOND } level_t;

package sv_construction_runner;
  class root;
    static root single;
    int runs;
    static function root get();
      if (single == null) single = new;
      return single;
    endfunction
    task run(int amount);
      #5;
      runs += amount;
    endtask
  endclass

  task run_twice();
    root top;
    top = root::get();
    top.run(1);
    top = root::get();
    top.run(2);
  endtask
endpackage

module sv_class_construction_in_methods;
  import sv_construction_runner::*;

  class counter;
    int value;
    function new(int start);
      value = start;
    endfunction
  endclass

  class holder;
    static counter shared = new(7);
    level_t level = SECOND;
    int doubled = 2 * 21;
  endclass

  class box #(type T = int);
    typedef box default_box;
    T content;
    function default_box make_default();
      default_box made = new;
      return made;
    endfunction
  endclass

  holder first, second;
  box #(bit) small_box;
  box made_box;
  int failures;

  initial begin
    first = new;
    second = new;
    if (holder::shared == null || holder::shared.value != 7) failures++;
    if (first.level != SECOND || first.doubled != 42) failures++;
    if (first.shared != second.shared) failures++;

    small_box = new;
    made_box = small_box.make_default();
    if (made_box == null || $bits(made_box.content) != 32) failures++;

    run_twice();
    if (root::get().runs != 3 || $time != 10) failures++;

    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
