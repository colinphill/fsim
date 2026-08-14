# SPDX-License-Identifier: Apache-2.0
set summary [fsim::sdf configure mixed.sdf mixed_top {mixed_top.u_vital} typ 32]
puts "generation=[dict get $summary generation]"
puts "identity=[dict get $summary semantic_identity]"
