// fsim-expect-run: top=sv_typeless_mailbox_get_mismatch std=2017
// fsim-expect-text: mailbox message type is not equivalent to the type of the message variable
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 15.4.5: a get from a typeless mailbox whose oldest message
// has a type not equivalent to the type of the get's variable is a
// run-time error. Here the message is an int and the variable a string.
module sv_typeless_mailbox_get_mismatch;
  mailbox box = new;
  initial begin
    string s;
    box.put(1);
    box.get(s);
    $display("reached");
  end
endmodule
