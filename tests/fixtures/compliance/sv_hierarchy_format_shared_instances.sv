// fsim: top=sv_hierarchy_format_shared_instances std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 21.2.1.6: %m prints the hierarchical name of the scope that
// calls the task, so each instance of one module prints its own path, also
// where the instances share one parameterization or come from a generate loop.
module sv_hierarchy_format_leaf(input logic go, input int fd);
  always @(posedge go) $fwrite(fd, "%m\n");
endmodule

module sv_hierarchy_format_shared_instances;
  logic [3:0] go;
  int fd;
  int reader;
  int length;
  string line;
  string expected[4];

  sv_hierarchy_format_leaf a(go[0], fd);
  sv_hierarchy_format_leaf b(go[1], fd);
  for (genvar i = 0; i < 2; ++i) begin : g
    sv_hierarchy_format_leaf leaf(go[2 + i], fd);
  end

  initial begin
    expected[0] = "sv_hierarchy_format_shared_instances.a";
    expected[1] = "sv_hierarchy_format_shared_instances.b";
    expected[2] = "sv_hierarchy_format_shared_instances.g[0].leaf";
    expected[3] = "sv_hierarchy_format_shared_instances.g[1].leaf";
    go = '0;
    fd = $fopen("sv_hierarchy_format_shared_instances.txt", "w");
    for (int i = 0; i < 4; ++i) begin
      #1 go[i] = 1'b1;
    end
    #1 $fclose(fd);
    reader = $fopen("sv_hierarchy_format_shared_instances.txt", "r");
    for (int i = 0; i < 4; ++i) begin
      length = $fgets(line, reader);
      if (length == 0 || line != {expected[i], "\n"})
        $fatal(1, "instance %0d printed %s", i, line);
    end
    $fclose(reader);
    $display("PASS");
  end
endmodule
