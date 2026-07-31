// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

int main() {
  fsim::tests::elaboration::test_systemverilog_container_lowering();
  fsim::tests::elaboration::test_systemverilog_static_slice_calls();
  fsim::tests::elaboration::test_systemverilog_static_slice_ordering();
}
