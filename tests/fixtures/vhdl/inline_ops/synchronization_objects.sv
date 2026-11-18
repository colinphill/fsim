// SPDX-License-Identifier: Apache-2.0
//
// Mailboxes, semaphores and automatic events (IEEE 1800-2023 15.4, 15.3,
// 15.5) in compiled member code: the handles are 64-bit registers, get and
// peek take the element width, num() and the try_* results are ints.
//   put 7, 9; num = 2; try_peek = 1 reads 7; get 7; try_get 9 (1);
//   try_get on the empty mailbox = 0; num = 0.
//   semaphore of 2 keys: try_get(2) = 1, try_get(1) = 0, put 1, try_get(1) = 1.
//   an automatic event triggered in this time step: triggered = 1.
module top;
  task automatic run();
    mailbox #(logic [11:0]) box = new();
    semaphore keys = new(2);
    event done;
    logic [11:0] value;
    int status;
    box.put(12'd7);
    box.put(12'd9);
    $display("inl mailbox num=%0d", box.num());
    status = box.try_peek(value);
    $display("inl peek %0d %0d", status, value);
    box.get(value);
    $display("inl get %0d", value);
    status = box.try_get(value);
    $display("inl try_get %0d %0d", status, value);
    status = box.try_get(value);
    $display("inl empty %0d num=%0d", status, box.num());
    $display("inl keys %0d %0d", keys.try_get(2), keys.try_get(1));
    keys.put(1);
    $display("inl keys %0d", keys.try_get(1));
    -> done;
    $display("inl event %0d", done.triggered);
  endtask
  initial run();
endmodule
