#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
vivado_bin=${VIVADO_BIN:-/opt/eda/AMD/2025.2.1/Vivado/bin}
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/fsim-sv-inertial-scheduling.XXXXXX")

if [ "${KEEP_WORKSPACE:-0}" != 1 ]; then
    trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM
else
    echo "Vivado inertial scheduling raw logs: $temporary_dir" >&2
fi

for tool_name in xvlog xelab xsim; do
    if [ ! -x "$vivado_bin/$tool_name" ]; then
        echo "Vivado executable not found: $vivado_bin/$tool_name" >&2
        exit 127
    fi
done

case_dir="$temporary_dir/inertial_maturation_order"
mkdir -p "$case_dir/work"
(
    cd "$case_dir"
    "$vivado_bin/xvlog" --sv -work work \
        -log compile.log "$fixture_dir/inertial_maturation_order.sv" \
        >compile.stdout 2>&1
    "$vivado_bin/xelab" -mt off -debug off \
        inertial_maturation_order -s inertial_maturation_order_snapshot \
        -log elaborate.log >elaborate.stdout 2>&1
    "$vivado_bin/xsim" inertial_maturation_order_snapshot -R \
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
    "$fixture_dir/expected/inertial_maturation_order.out" \
    "$case_dir/actual.out"
echo "Vivado inertial scheduling witness passed: inertial_maturation_order"
