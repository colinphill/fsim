// fsim: top=sv_dynamic_queue_value_copy std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_dynamic_queue_value_copy;
  typedef int integer_queue[$];
  typedef int integer_array[];
  typedef logic [7:0] byte_array[];
  typedef logic [23:0] packed_bytes;
  typedef bit [23:0] two_state_bytes;
  typedef logic [63:0] packed_integers;
  int calls;
  function byte_array byte_result();
    byte_array bytes;
    calls++;
    bytes = '{8'h12, 8'hxz, 8'h34};
    return bytes;
  endfunction
  function automatic integer_queue queue_result();
    return '{11, 12};
  endfunction
  function int associative_sum();
    int entries[int];
    int total;
    entries[3] = 3;
    entries[6] = 6;
    total = 0;
    foreach (entries[index]) total += entries[index];
    return total;
  endfunction
  function automatic integer_array copy_array(input integer_array values);
    values[0] = 21;
    return values;
  endfunction
  function integer_array increment_array(input integer_array values);
    integer_array result_values;
    result_values = new[values.size()];
    for (int i = 0, j = 0; i < values.size(); ++i) begin
      result_values[i] = values[j] + 1;
      ++j;
    end
    return result_values;
  endfunction
  task automatic take_array(input integer_array values);
    if (values.size() != 2 || values[0] != 4 || values[1] != 5)
      $fatal(1, "queue actual did not initialize dynamic array task input");
    values[0] = 22;
  endtask
  initial begin
    int dynamic_values[];
    int queue_values[$];
    int bounded_values[$:1];
    string dynamic_strings[];
    string queue_strings[$];
    packed_bytes cast_result;
    dynamic_values = '{1, 2, 3};
    queue_values = dynamic_values;
    dynamic_values[0] = 9;
    if (queue_values.size() != 3 || queue_values[0] != 1)
      $fatal(1, "dynamic-to-queue copy is not independent");
    bounded_values = dynamic_values;
    if (bounded_values.size() != 2 || bounded_values[0] != 9 || bounded_values[1] != 2)
      $fatal(1, "destination queue bound was not applied");
    dynamic_values = queue_values;
    queue_values[1] = 8;
    if (dynamic_values.size() != 3 || dynamic_values[1] != 2)
      $fatal(1, "queue-to-dynamic copy is not independent");
    queue_values.delete();
    dynamic_values = queue_values;
    if (dynamic_values.size() != 0)
      $fatal(1, "empty queue did not clear dynamic array");
    queue_values = '{4, 5};
    queue_values = dynamic_values;
    if (queue_values.size() != 0)
      $fatal(1, "empty dynamic array did not clear queue");
    dynamic_strings = '{"one", "two"};
    queue_strings = dynamic_strings;
    dynamic_strings[0] = "changed";
    if (queue_strings[0] != "one" || queue_strings[1] != "two")
      $fatal(1, "string queue copy is not independent");
    dynamic_strings = queue_strings;
    if (dynamic_strings.size() != 2 || dynamic_strings[0] != "one")
      $fatal(1, "string dynamic-array copy differs");
    dynamic_values = queue_result();
    if (dynamic_values.size() != 2 || dynamic_values[0] != 11
        || dynamic_values[1] != 12)
      $fatal(1, "queue function result did not copy into dynamic array");
    queue_values = '{4, 5};
    dynamic_values = copy_array(queue_values);
    take_array(queue_values);
    if (queue_values[0] != 4 || dynamic_values[0] != 21
        || dynamic_values[1] != 5)
      $fatal(1, "input array copy mutated its queue actual");
    dynamic_values = increment_array(dynamic_values);
    dynamic_values = increment_array(dynamic_values);
    if (dynamic_values.size() != 2 || dynamic_values[0] != 23
        || dynamic_values[1] != 7)
      $fatal(1, "static function loop variables did not initialize per call");
    if (associative_sum() != 9 || associative_sum() != 9)
      $fatal(1, "static function foreach loop variables did not initialize");
    cast_result = packed_bytes'(byte_result());
    if (cast_result !== 24'h12xz34 || calls != 1)
      $fatal(1, "dynamic array bit-stream cast order, state or call count");
    if (two_state_bytes'(byte_result()) !== 24'h120034 || calls != 2)
      $fatal(1, "bit-stream cast did not apply destination state conversion");
    if (packed_integers'(queue_values) !== 64'h0000000400000005)
      $fatal(1, "queue bit-stream cast did not put index zero in the MSBs");
    $display("PASS");
  end
endmodule
