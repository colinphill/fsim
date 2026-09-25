#!/usr/bin/env sh
# SPDX-License-Identifier: Apache-2.0
set -eu

fsim_example_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fsim_example_bin=${FSIM_BIN:-${fsim_example_directory}/../../build/dev/fsim}
case "${fsim_example_bin}" in
  /*|[A-Za-z]:/*) ;;
  *) fsim_example_bin=${PWD}/${fsim_example_bin} ;;
esac
cd "${fsim_example_directory}"

"${fsim_example_bin}" compile --library models logic_stage.vhd
"${fsim_example_bin}" compile --library work three_language_tb.sv
"${fsim_example_bin}" systemc compile --library models mixed_bridge.cpp
"${fsim_example_bin}" systemc link --library models
"${fsim_example_bin}" elaborate work.three_language_tb --search-library models
"${fsim_example_bin}" debug <<'FSIM_COMMANDS'
scopes
scope u_bridge
where
scopes
signals
scope ..
scope u_vhdl
where
signals
scope ..
break signal observed == 1
trace clear
trace add stimulus
trace add observed
continue
show observed
where
continue
quit
FSIM_COMMANDS
