// SPDX-License-Identifier: Apache-2.0
// Interface tf decls (IEEE 1800-2023 37.6, 37.8, 25.7.3): the interface
// declares an `extern forkjoin` task that two modules export, an `extern`
// task and an `extern` function that one module exports. The forkjoin task
// iterates as the body of each exporting module (vpiForkJoinAcc); an `extern`
// prototype is its own object (vpiExternAcc). The iteration lists the
// functions of the interface before its tasks (the order its declarations are
// kept in), so the expected trace is twice, count (m1, m2), one.
interface bus_if;
  extern forkjoin task count();
  extern task one();
  extern function int twice(input int v);
  modport target(export count, one, twice);
  modport counter(export count);
endinterface

module mem(bus_if.target a);
  task a.count();
  endtask
  task a.one();
  endtask
  function int a.twice(input int v);
    return v * 2;
  endfunction
endmodule

module cnt(bus_if.counter a);
  task a.count();
  endtask
endmodule

module top;
  bus_if sb ();
  mem m1 (sb.target);
  cnt m2 (sb.counter);
  initial #1 $display("done");
endmodule
