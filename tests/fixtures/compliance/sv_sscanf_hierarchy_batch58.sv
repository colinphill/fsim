// fsim: top=sv_sscanf_hierarchy_batch58 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 21.3.4.3, Table 21-8: %m returns the current path and reads no source data.
module sv_sscanf_hierarchy_batch58;
  logic [8*64-1:0] packed_path;
  string string_path;
  string task_path;
  int matched, value, task_matched;

  task automatic capture_task_path(output string observed_path,
                                   output int scan_result);
    scan_result = $sscanf("Z", "%mZ", observed_path);
  endtask

  initial begin : named_scope
    matched = $sscanf("", "%m", packed_path);
    if (matched != 1
        || packed_path != "sv_sscanf_hierarchy_batch58.named_scope")
      $fatal(1, "packed %m path or empty-source conversion");

    matched = $sscanf("N7", "%mN%d", string_path, value);
    if (matched != 2
        || string_path != "sv_sscanf_hierarchy_batch58.named_scope"
        || value != 7)
      $fatal(1, "string %m path or source consumption");

    capture_task_path(task_path, task_matched);
    if (task_matched != 1
        || task_path != "sv_sscanf_hierarchy_batch58.capture_task_path")
      $fatal(1, "subprogram %m path or literal after conversion");
    $display("PASS");
  end
endmodule
