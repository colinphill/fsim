// SPDX-License-Identifier: Apache-2.0
// The typespec of a task or function formal and result declared with a
// typedef follows the name lookup of IEEE 1800-2023 26.3 (explicit and
// wildcard imports, a local declaration hiding a wildcard-imported name),
// seen through vpiTypespec (37.41, 37.25).
package pa;
    typedef logic [3:0] wa_t;
endpackage

package pb;
    typedef logic [5:0] eb_t;
    typedef logic [6:0] unused_t;
endpackage

package pc;
    typedef logic [7:0] sh_t;
endpackage

// Both declare dup_t; importing both with a wildcard is legal while the
// name is not referenced, and no formal here uses it.
package px;
    typedef logic [8:0] dup_t;
endpackage

package py;
    typedef logic [9:0] dup_t;
endpackage

module top;
    import pa::*;
    import pb::eb_t;
    import pc::*;
    import px::*;
    import py::*;
    typedef logic [1:0] sh_t;

    function automatic wa_t wild(input wa_t a);
        return a;
    endfunction
    function automatic eb_t expl(input eb_t b);
        return b;
    endfunction
    function automatic sh_t local_t(input sh_t c);
        return c;
    endfunction
endmodule
