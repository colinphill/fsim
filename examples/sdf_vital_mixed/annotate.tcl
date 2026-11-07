# SPDX-License-Identifier: Apache-2.0
# The timing generics of a VITAL cell are constants of elaboration, so a Tcl
# session cannot annotate them (IEEE 1076.4-2000 5.1.1); `fsim elaborate --sdf`
# does. The session reports it.
if {[catch {fsim::sdf configure mixed.sdf mixed_top {mixed_top.u_vital.u_core} typ 32} message]} {
  puts "VITAL annotation is refused: $message"
}
