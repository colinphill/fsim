# SPDX-License-Identifier: Apache-2.0
# Workspace configuration: every fsim command run in this directory applies
# these defaults first (see docs/tcl.md). An option on the command line wins.
fsim::config set compile.sv.standard 2023
fsim::config set compile.code_coverage true
fsim::config set elaborate.code_coverage true
fsim::config set simulate.max_deltas 1000
