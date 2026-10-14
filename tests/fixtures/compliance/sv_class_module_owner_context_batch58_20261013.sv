// fsim: top=sv_class_module_owner_context_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
module b58_owner_context_mod #(parameter int X = 3);
  localparam int OWNER_LOCAL = X + 7;

  class owner_bound;
    int constructed;
    int seen_owner;
    function new;
      constructed = OWNER_LOCAL;
    endfunction
    function int value;
      return 100 * X + constructed;
    endfunction
    function int owner_local_value;
      return OWNER_LOCAL;
    endfunction
    function int owner_leaf;
      return 100 * X + constructed;
    endfunction
    function int owner_local_leaf;
      return OWNER_LOCAL;
    endfunction
    function int transitive_value;
      return owner_leaf();
    endfunction
    function int transitive_owner_local_value;
      return owner_local_leaf();
    endfunction
    function void record_owner;
      seen_owner = 100 * X + OWNER_LOCAL;
    endfunction
    function void record_transitive_owner;
      seen_owner = transitive_value();
    endfunction
    function int seen_owner_value;
      return seen_owner;
    endfunction
  endclass

  class local_shadow;
    int X = 99;
    function int value;
      return X;
    endfunction
    function int local_value;
      int X;
      X = 77;
      return X;
    endfunction
  endclass

  owner_bound original = new;
  owner_bound copy;
  owner_bound forked;
  local_shadow shadow = new;

  initial begin
    copy = original;
    fork
      begin
        forked = new;
      end
    join
  end
endmodule

module sv_class_module_owner_context_batch58;
  b58_owner_context_mod #(.X(10)) first();
  b58_owner_context_mod #(.X(20)) second();
  b58_owner_context_mod defaulted();

  initial begin
    #1;
    first.original.record_owner();
    first.copy.record_owner();
    first.forked.record_owner();
    second.original.record_owner();
    second.copy.record_owner();
    second.forked.record_owner();
    defaulted.original.record_owner();
    defaulted.forked.record_owner();
    if (first.original.seen_owner_value() != 1017
        || first.copy.seen_owner_value() != 1017
        || first.forked.seen_owner_value() != 1017
        || second.original.seen_owner_value() != 2027
        || second.copy.seen_owner_value() != 2027
        || second.forked.seen_owner_value() != 2027
        || defaulted.original.seen_owner_value() != 310
        || defaulted.forked.seen_owner_value() != 310) begin
      $fatal(1, "statement-form owner read used the wrong specialization");
    end
    first.original.record_transitive_owner();
    first.copy.record_transitive_owner();
    first.forked.record_transitive_owner();
    second.original.record_transitive_owner();
    second.copy.record_transitive_owner();
    second.forked.record_transitive_owner();
    defaulted.original.record_transitive_owner();
    defaulted.forked.record_transitive_owner();
    if (first.original.value() != 1017
        || first.copy.value() != 1017
        || second.original.value() != 2027
        || second.copy.value() != 2027
        || defaulted.original.value() != 310
        || first.forked.value() != 1017
        || second.forked.value() != 2027
        || defaulted.forked.value() != 310
        || first.original.owner_local_value() != 17
        || second.original.owner_local_value() != 27
        || defaulted.original.owner_local_value() != 10
        || first.original.transitive_value() != 1017
        || second.original.transitive_value() != 2027
        || defaulted.original.transitive_value() != 310
        || first.forked.transitive_value() != 1017
        || second.forked.transitive_value() != 2027
        || defaulted.forked.transitive_value() != 310
        || first.original.transitive_owner_local_value() != 17
        || second.original.transitive_owner_local_value() != 27
        || defaulted.original.transitive_owner_local_value() != 10
        || first.original.seen_owner_value() != 1017
        || first.copy.seen_owner_value() != 1017
        || first.forked.seen_owner_value() != 1017
        || second.original.seen_owner_value() != 2027
        || second.copy.seen_owner_value() != 2027
        || second.forked.seen_owner_value() != 2027
        || defaulted.original.seen_owner_value() != 310
        || defaulted.forked.seen_owner_value() != 310
        || first.forked.owner_local_value() != 17
        || second.forked.owner_local_value() != 27
        || defaulted.forked.owner_local_value() != 10
        || first.shadow.value() != 99
        || second.shadow.value() != 99
        || first.shadow.local_value() != 77
        || second.shadow.local_value() != 77) begin
      $fatal(1, "class handle lost its exact enclosing specialization");
    end
    $display("PASS");
  end
endmodule
