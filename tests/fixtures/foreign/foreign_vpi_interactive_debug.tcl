# SPDX-License-Identifier: Apache-2.0
# The debugger session as interactive mode (IEEE 1800-2023 38.36.3):
# `scope` fires cbInteractiveScopeChange, the run that continues leaves
# interactive mode (cbExitInteractive), the $stop returns the session to
# its prompt (cbEnterInteractive), and the plug-in's vpi_control
# (vpiSetInteractiveScope) moves the session's scope.
fsim::debug scope top.c
fsim::debug continue
# The plug-in selected top last, while the run was resumed.
if {[dict get [fsim::debug status] scope] ne "top"} {
    error "vpi_control did not change the debugger scope"
}
fsim::debug scope top.c
if {[dict get [fsim::debug status] scope] ne "top.c"} {
    error "the scope command did not change the debugger scope"
}
fsim::debug continue
