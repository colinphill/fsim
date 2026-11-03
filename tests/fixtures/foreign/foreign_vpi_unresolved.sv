// SPDX-License-Identifier: Apache-2.0
// $late_task is registered only by a cbUnresolvedSystf callback
// (IEEE 1800-2023 38.36.2), at elaboration and again at simulation.
module top;
  initial #3 $late_task(7);
endmodule
