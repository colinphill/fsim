// fsim: top=sv_declarations_and_calls std=2017
// IEEE 1800-2017 7.2: an unpacked structure variable local to a procedure,
// and bit- and part-selects of a structure member as assignment targets;
// 12.7.1: for loops with empty clauses; 12.7.2: a real repeat count rounds;
// 13.5.3: an omitted positional argument takes its default; 13.4.3: a
// formal hides a parameter of the same name in a constant function call;
// 6.7.1: a net of a user-defined type; 22.5.1: stringification keeps the
// spacing of its argument.
`define SV_DECL_PATH /usr/local/bin/
`define SV_DECL_STRING(x) `"x`"

module sv_declarations_and_calls;
  typedef struct { bit [3:0] enables; int count; } entry_t;
  typedef logic [7:0] byte_t;
  localparam width = 8;

  function automatic int combine(int high = 1, int low = 0);
    return (high << 16) | low;
  endfunction

  function [width-1:0] copy;
    input [width-1:0] width;
    copy = width;
  endfunction

  entry_t shared;
  wire byte_t typed_net;
  assign typed_net = 8'h5a;
  int i, loops, failures;

  initial begin
    entry_t local_entry;
    local_entry.enables = '0;
    local_entry.enables[0] = 1'b1;
    local_entry.enables[3:2] = 2'b11;
    local_entry.count = 5;
    shared = local_entry;
    shared.enables[1] = 1'b1;
    if (local_entry.enables != 4'b1101 || shared.enables != 4'b1111
        || shared.count != 5) failures++;

    i = 3;
    for (; i < 10; ++i) loops++;
    for (i = 0; i < 10;) i += 2;
    for (;;) begin loops++; if (loops == 10) break; end
    if (loops != 10 || i != 10) failures++;

    loops = 0;
    repeat (2.6) loops++;
    if (loops != 3) failures++;

    if (combine(, 2) != 32'h0001_0002 || combine(3) != 32'h0003_0000)
      failures++;
    if (copy(21) != 21) failures++;

    #1;
    if (typed_net != 8'h5a) failures++;
    if (`SV_DECL_STRING(`SV_DECL_PATH) != "/usr/local/bin/") failures++;

    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
