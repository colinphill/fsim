#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
vivado_bin=${VIVADO_BIN:-/opt/eda/AMD/2025.2.1/Vivado/bin}
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/fsim-sv-scheduling-vivado.XXXXXX")

if [ "${KEEP_WORKSPACE:-0}" != 1 ]; then
    trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM
else
    echo "Vivado scheduling raw logs: $temporary_dir" >&2
fi

for tool_name in xvlog xelab xsim; do
    if [ ! -x "$vivado_bin/$tool_name" ]; then
        echo "Vivado executable not found: $vivado_bin/$tool_name" >&2
        exit 127
    fi
done

for case_name in \
    a2_chain_fork_merge \
    multidimensional_fixed_read \
    fixed_array_write \
    a2_active_nba_boundary \
    a2_derived_clock_reader \
    continuous_active \
    continuous_active_verilog \
    ordered_wave_native \
    implicit_port_active \
    implicit_port_active_verilog \
    inactive_nested_delta \
    active_inactive_nba \
    active_inactive_nba_verilog \
    nba_batch_before_active \
    derived_clock_nba \
    strobe_end_of_slot \
    strobe_end_of_slot_verilog \
    monitor_once_per_slot \
    reactive_reinactive_re_nba \
    named_event_triggered_nba; do
    case_dir="$temporary_dir/$case_name"
    mkdir -p "$case_dir/work"
    (
        cd "$case_dir"
        case "$case_name" in
        continuous_active_verilog|implicit_port_active_verilog|\
        active_inactive_nba_verilog|strobe_end_of_slot_verilog)
            "$vivado_bin/xvlog" -work work \
                -log compile.log "$fixture_dir/$case_name.v" \
                >compile.stdout 2>&1 ;;
        *)
            "$vivado_bin/xvlog" --sv -work work \
                -log compile.log "$fixture_dir/$case_name.sv" \
                >compile.stdout 2>&1 ;;
        esac
        "$vivado_bin/xelab" -mt off -debug off \
            "$case_name" -s "${case_name}_snapshot" -log elaborate.log \
            >elaborate.stdout 2>&1
        "$vivado_bin/xsim" "${case_name}_snapshot" -R \
            -log simulate.log >simulate.stdout 2>&1
    )

    sed -E \
        -e '/^\*{6} xsim /d' \
        -e '/^ *\*{4} /d' \
        -e '/^ *\*{2} Copyright /d' \
        -e '/^source xsim\.dir\//d' \
        -e '/^# xsim /d' \
        -e '/^Time resolution is /d' \
        -e '/^run -all$/d' \
        -e '/^\$finish called at time : /d' \
        -e '/^exit$/d' \
        -e '/^INFO: xsimkernel Simulation Memory Usage:/d' \
        -e '/^INFO: \[Common 17-206\] Exiting xsim at /d' \
        -e '/^[[:space:]]*$/d' \
        "$case_dir/simulate.stdout" >"$case_dir/actual.out"

    diff -u \
        "$fixture_dir/expected/$case_name.out" \
        "$case_dir/actual.out"
    echo "Vivado scheduling witness passed: $case_name"
done

echo "Vivado scheduling witnesses passed"
