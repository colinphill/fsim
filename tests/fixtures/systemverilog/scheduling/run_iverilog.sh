#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/fsim-sv-scheduling.XXXXXX")
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM

command -v iverilog >/dev/null 2>&1 || {
    echo "iverilog is required for the external scheduling oracle" >&2
    exit 127
}
command -v vvp >/dev/null 2>&1 || {
    echo "vvp is required for the external scheduling oracle" >&2
    exit 127
}

for case_name in \
    continuous_active \
    multidimensional_fixed_read \
    fixed_array_write \
    implicit_port_active \
    inactive_nested_delta \
    active_inactive_nba \
    nba_batch_before_active \
    derived_clock_nba \
    strobe_end_of_slot \
    monitor_once_per_slot
do
    iverilog -g2012 -s "$case_name" \
        -o "$temporary_dir/$case_name.vvp" \
        "$fixture_dir/$case_name.sv"
    vvp "$temporary_dir/$case_name.vvp" \
        >"$temporary_dir/$case_name.out"
    diff -u \
        "$fixture_dir/expected/$case_name.out" \
        "$temporary_dir/$case_name.out"
done

for case_name in \
    continuous_active_verilog \
    implicit_port_active_verilog \
    active_inactive_nba_verilog \
    strobe_end_of_slot_verilog
do
    iverilog -g2005 -s "$case_name" \
        -o "$temporary_dir/$case_name.vvp" \
        "$fixture_dir/$case_name.v"
    vvp "$temporary_dir/$case_name.vvp" \
        >"$temporary_dir/$case_name.out"
    diff -u \
        "$fixture_dir/expected/$case_name.out" \
        "$temporary_dir/$case_name.out"
done

echo "Icarus scheduling witnesses passed"
