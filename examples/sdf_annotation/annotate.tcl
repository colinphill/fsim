# SPDX-License-Identifier: Apache-2.0

if {$argc != 3} {
  error "usage: annotate.tcl SDF ROOT CELL_GLOB"
}

lassign $argv sdf_path root cell_glob
set summary [fsim::sdf configure $sdf_path $root $cell_glob typ 256]
puts "SDF identity: [dict get $summary identity]"
puts "SDF inputs: [dict get $summary inputs]"
puts "SDF report entries: [dict get $summary report_entries]"
foreach entry [fsim::sdf report] {
  puts $entry
}
