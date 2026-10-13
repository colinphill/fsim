// fsim: top=sv_local_queue_output_index std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 7.4.6 and 7.10.1: local queue writes at size append;
// invalid sequence writes and writes beyond a bounded queue are ignored.
class local_queue_item;
  int value;
endclass

module sv_local_queue_output_index;
  task automatic create_item(output local_queue_item item);
    item = new;
    item.value = 42;
  endtask

  initial begin
    local_queue_item items[$];
    int values[$];
    int bounded[$:0];
    int dynamic_values[];
    logic [127:0] wide_values[$];
    int long_index_values[$];
    longint index_value;
    logic [31:0] unknown_index = 'x;

    create_item(items[0]);
    if (items.size() != 1 || items[0].value != 42)
      $fatal(1, "output copy did not append to a local queue");
    values[0] = 7;
    values[1] = 9;
    values[-1] = 1;
    values[3] = 2;
    values[unknown_index] = 3;
    if (values.size() != 2 || values[0] != 7 || values[1] != 9)
      $fatal(1, "local queue indexed write rules differ");
    bounded[0] = 11;
    bounded[1] = 12;
    if (bounded.size() != 1 || bounded[0] != 11)
      $fatal(1, "local queue append exceeded its declared bound");
    dynamic_values = new[1];
    dynamic_values[0] = 13;
    dynamic_values[1] = 14;
    dynamic_values[-1] = 15;
    if (dynamic_values.size() != 1 || dynamic_values[0] != 13)
      $fatal(1, "invalid dynamic-array write changed its value");
    wide_values[0] = 128'h12345678_9abcdef0_13579bdf_2468ace0;
    wide_values[unknown_index] = '0;
    if (wide_values.size() != 1
        || wide_values[0] != 128'h12345678_9abcdef0_13579bdf_2468ace0)
      $fatal(1, "wide queue indexed write rules differ");
    index_value = 0;
    long_index_values[index_value] = 19;
    index_value = -1;
    long_index_values[index_value] = 20;
    index_value = 64'h100000000;
    long_index_values[index_value] = 21;
    if (long_index_values.size() != 1 || long_index_values[0] != 19)
      $fatal(1, "long index queue write rules differ");
    $display("PASS");
  end
endmodule
