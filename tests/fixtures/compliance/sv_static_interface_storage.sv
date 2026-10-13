// fsim: top=sv_static_interface_storage std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.21, 13.3, 25.9: static function results belong to
// the interface instance selected by a virtual interface handle.
interface static_result_bus;
  typedef int queue_t[$];
  function string letters();
    letters = {letters, "x"};
  endfunction
  function queue_t history();
    history.push_back(7);
  endfunction
endinterface

module sv_static_interface_storage;
  static_result_bus a();
  static_result_bus b();
  virtual static_result_bus handle;
  string first, second, other;
  int first_queue[$], second_queue[$], other_queue[$];
  initial begin
    handle = a;
    first = handle.letters();
    first_queue = handle.history();
    handle = b;
    other = handle.letters();
    other_queue = handle.history();
    handle = a;
    second = handle.letters();
    second_queue = handle.history();
    if (first != "x" || second != "xx" || other != "x"
        || first_queue.size() != 1 || second_queue.size() != 2
        || other_queue.size() != 1 || second_queue[1] != 7)
      $fatal(1, "virtual interface static result storage");
    $display("PASS");
    $finish;
  end
endmodule
