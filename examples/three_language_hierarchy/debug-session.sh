#!/usr/bin/env sh
# SPDX-License-Identifier: Apache-2.0
set -eu

fsim_example_bin=${FSIM_BIN:-build/dev/fsim}

"${fsim_example_bin}" debug \
  -p examples/three_language_hierarchy/fsim.toml -j 8 <<'FSIM_COMMANDS'
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
