// fsim: top=sv_covergroup_instance_weight
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 19.11.3: with type_option.merge_instances false (the
// default), a covergroup type's coverage is the average of its instances'
// coverage weighted by each instance's option.weight: sum(Wi * Ii) / sum(Wi).
//   heavy (weight 3) covers 2 of 4 bins (50%), light (weight 1) all 4
//   (100%): type coverage (3 * 50 + 1 * 100) / 4 = 62.5. With light's weight
//   then set to 0 only heavy counts: 50.
module sv_covergroup_instance_weight;
  covergroup cg with function sample(input bit [1:0] value);
    value_point: coverpoint value {
      bins values[] = {0, 1, 2, 3};
    }
  endgroup
  cg heavy = new;
  cg light = new;
  real heavy_inst, light_inst, weighted, heavy_only;
  initial begin
    heavy.option.weight = 3;
    light.option.weight = 1;
    heavy.sample(0);
    heavy.sample(1);
    for (int v = 0; v < 4; v++) light.sample(v);
    heavy_inst = heavy.get_inst_coverage();
    light_inst = light.get_inst_coverage();
    weighted = cg::get_coverage();
    light.option.weight = 0;
    heavy_only = cg::get_coverage();
    if (heavy_inst == 50.0 && light_inst == 100.0 && weighted == 62.5
        && heavy_only == 50.0)
      $display("PASS");
    else
      $display("FAIL heavy=%f light=%f weighted=%f heavy_only=%f",
        heavy_inst, light_inst, weighted, heavy_only);
  end
endmodule
