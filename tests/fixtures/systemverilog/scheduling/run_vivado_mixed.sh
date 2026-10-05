#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
vivado_bin=${VIVADO_BIN:-/opt/eda/AMD/2025.2.1/Vivado/bin}
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/fsim-sv-vhdl-scheduling.XXXXXX")

if [ "${KEEP_WORKSPACE:-0}" != 1 ]; then
    trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM
else
    echo "Vivado mixed-language scheduling raw logs: $temporary_dir" >&2
fi

for tool_name in xvlog xvhdl xelab xsim; do
    if [ ! -x "$vivado_bin/$tool_name" ]; then
        echo "Vivado executable not found: $vivado_bin/$tool_name" >&2
        exit 127
    fi
done

case_dir="$temporary_dir/mixed_domain_boundary"
mkdir -p "$case_dir/work"
(
    cd "$case_dir"
    "$vivado_bin/xvhdl" -2008 -work work \
        -log compile-vhdl.log "$fixture_dir/mixed_language_leaf.vhd" \
        >compile-vhdl.stdout 2>&1
    "$vivado_bin/xvlog" --sv -work work \
        -log compile-sv.log "$fixture_dir/mixed_domain_boundary.sv" \
        >compile-sv.stdout 2>&1
    "$vivado_bin/xelab" -mt off -debug off \
        mixed_domain_boundary -s mixed_domain_boundary_snapshot \
        -log elaborate.log >elaborate.stdout 2>&1
    "$vivado_bin/xsim" mixed_domain_boundary_snapshot -R \
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
    "$fixture_dir/expected/mixed_domain_boundary.out" \
    "$case_dir/actual.out"
echo "Vivado mixed-language scheduling witness passed: mixed_domain_boundary"
