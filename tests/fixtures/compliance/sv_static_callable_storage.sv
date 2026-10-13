// fsim: top=sv_static_callable_storage std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.21, 13.3: static locals belong to the instance,
// retain their values across callers, and have hierarchical names.
module static_storage_child;
  function int count();
    static int calls = 10;
    calls++;
    return calls;
  endfunction

  function automatic int automatic_count();
    int calls = 20;
    calls++;
    return calls;
  endfunction

  function automatic int retained_count();
    static int calls = 30;
    calls++;
    return calls;
  endfunction
  function bit [7:0] serial();
    serial++;
  endfunction
  typedef int queue_t[$];
  function automatic int queue_count();
    static int values[$] = '{7};
    values.push_back(1);
    return values.size();
  endfunction
  function string letters();
    letters = {letters, "x"};
  endfunction
  function queue_t history();
    history.push_back(1);
  endfunction
  function queue_t local_history();
    local_history.push_back(3);
  endfunction
  function bit [79:0] wide_count();
    static bit [79:0] value = 80'h1234_5678_9abc_def0_0000;
    value++;
    return value;
  endfunction
  function logic [71:0] four_state();
    static logic [71:0] value = 72'hx1_z2_3456_789a_bcde_f0;
    return value;
  endfunction
  task append(output string text, output queue_t items);
    text = {text, "a"};
    items.push_back(2);
  endtask

  int first, second, retained_first, retained_second;
  int queue_first, queue_second;
  queue_t local_first, local_second;
  initial begin
    first = count();
    retained_first = retained_count();
    queue_first = queue_count();
    local_first = local_history();
    local_second = local_history();
    if (local_first.size() != 1 || local_second.size() != 2
        || local_first[0] != 3 || local_second[1] != 3)
      $fatal(1, "lexical static queue function invocation and copy");
    if (wide_count() != 80'h1234_5678_9abc_def0_0001
        || wide_count() != 80'h1234_5678_9abc_def0_0002
        || four_state() !== 72'hx1_z2_3456_789a_bcde_f0)
      $fatal(1, "wide four-state static initializers");
  end
  initial begin
    #1;
    second = count();
    retained_second = retained_count();
    queue_second = queue_count();
  end
endmodule

module automatic default_automatic_storage;
  int total;
  initial begin : repeated
    repeat (2) begin : body
      int value = 3;
      total += value;
      value = 5;
    end
  end
endmodule

module sv_static_callable_storage;
  static_storage_child a();
  static_storage_child b();
  default_automatic_storage c();
  for (genvar index = 0; index < 2; index++) begin : lanes
    event trigger;
    int triggers;
    always @trigger triggers++;
    initial begin : worker
      static int value = index;
      #2;
      value++;
    end
  end
  task record();
    static int value = 4;
    value++;
  endtask
  task retained_output(output bit [7:0] value);
    value++;
  endtask
  task delayed(inout int value);
    #2;
    value++;
  endtask
  task automatic isolated(inout int value);
    #2;
    value++;
  endtask
  int shared_a = 10, shared_b = 20;
  int isolated_a = 10, isolated_b = 20;
  bit [7:0] output_first, output_second;
  string letters_first, letters_second;
  int history_first[$], history_second[$];
  string output_text;
  int output_items[$];
  initial delayed(shared_a);
  initial begin #1; delayed(shared_b); end
  initial isolated(isolated_a);
  initial begin #1; isolated(isolated_b); end
  initial begin : named
    static int value = 6;
    #2;
    if (value != 9) $fatal(1, "named block storage");
  end
  int retained_loop_total;
  initial begin : repeated
    repeat (2) begin : body
      static int value = 2;
      retained_loop_total += value;
      value = 9;
    end
  end
  int initialization_count;
  function automatic int seed();
    initialization_count++;
    return 5;
  endfunction
  function int explicit_automatic();
    automatic int value = seed();
    return value++;
  endfunction
  function automatic int separate_blocks();
    int total = 0;
    begin
      int value = 1;
      total += value;
    end
    begin
      int value = 2;
      total += value;
    end
    begin
      int value = 3;
      total += value;
    end
    return total;
  endfunction
  initial begin
    #1;
    named.value = 9;
    record.value = 7;
    record();
    if (record.value != 8) $fatal(1, "task hierarchical storage");
    retained_output(output_first);
    retained_output(output_second);
    if (output_first != 1 || output_second != 2)
      $fatal(1, "static output formal retains its value");
    #2;
    if (a.first != 11 || a.second != 12 || b.second != 12)
      $fatal(1, "static local shared between processes");
    if (a.retained_first != 31 || a.retained_second != 32)
      $fatal(1, "explicit static local of automatic function");
    a.count.calls = 50;
    if (b.count.calls != 12) $fatal(1, "instance isolation");
    if (a.automatic_count() != 21 || a.automatic_count() != 21)
      $fatal(1, "automatic local lifetime");
    if (a.count() != 51 || b.count() != 13)
      $fatal(1, "hierarchical calls use instance storage");
    #1;
    if (shared_a != 21 || shared_b != 22 || delayed.value != 22)
      $fatal(1, "overlapping calls share static formals");
    if (isolated_a != 11 || isolated_b != 21)
      $fatal(1, "overlapping automatic calls keep separate formals");
    if (lanes[0].worker.value != 1 || lanes[1].worker.value != 2)
      $fatal(1, "generated static block storage");
    if (retained_loop_total != 11)
      $fatal(1, "static initializer runs once across block entries");
    if (explicit_automatic() != 5 || explicit_automatic() != 5
        || initialization_count != 2)
      $fatal(1, "automatic initializer runs once per call");
    if (a.serial() != 1 || a.serial() != 2 || b.serial() != 1)
      $fatal(1, "static function result storage");
    if (a.queue_first != 2 || a.queue_second != 3 || b.queue_second != 3
        || a.queue_count() != 4 || a.queue_count() != 5 || b.queue_count() != 4)
      $fatal(1, "static queue shared across call sites and processes");
    letters_first = a.letters();
    letters_second = a.letters();
    history_first = a.history();
    history_second = a.history();
    if (letters_first != "x" || letters_second != "xx" || b.letters() != "x"
        || history_first.size() != 1 || history_second.size() != 2)
      $fatal(1, "static string and queue function results");
    a.append(output_text, output_items);
    a.append(output_text, output_items);
    if (output_text != "aa" || output_items.size() != 2
        || a.append.text != "aa" || a.append.items.size() != 2)
      $fatal(1, "static string and queue output formals");
    if (separate_blocks() != 6)
      $fatal(1, "automatic locals in separate unnamed blocks");
    if (c.total != 6)
      $fatal(1, "module default automatic lifetime");
    ->lanes[0].trigger;
    ->lanes[1].trigger;
    #1;
    if (lanes[0].triggers != 1 || lanes[1].triggers != 1)
      $fatal(1, "indexed hierarchical event trigger");
    $display("PASS");
  end
endmodule
