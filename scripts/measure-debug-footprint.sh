#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 BUILD_DIR [REPRESENTATIVE_EXECUTABLE]" >&2
  exit 2
fi

build_dir=$(realpath "$1")
representative=${2:-tests/fsim_application_tests_1}
if [[ ${representative} = /* ]]; then
  executable=${representative}
else
  executable=${build_dir}/${representative}
fi
archive=${build_dir}/libfsim_elaboration.a

for tool in ar du find readelf size stat; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "required tool '${tool}' was not found" >&2
    exit 2
  fi
done
for artifact in "${executable}" "${archive}"; do
  if [[ ! -f ${artifact} ]]; then
    echo "required artifact '${artifact}' was not found" >&2
    exit 2
  fi
done

tree_bytes=$(du -sb "${build_dir}" | awk '{print $1}')
executable_bytes=$(stat -c %s "${executable}")
loaded_bytes=$(
  size "${executable}" |
    awk 'NR == 2 {print $1 + $2 + $3}'
)
section_count=$(
  readelf -h "${executable}" |
    awk '/Number of section headers:/ {print $NF}'
)
debug_bytes=$(
  size -A "${executable}" |
    awk '$1 ~ /^\.debug_/ {sum += $2} END {printf "%.0f", sum}'
)

archive_bytes=$(stat -c %s "${archive}")
member_bytes=$(
  LC_ALL=C ar tv "${archive}" |
    awk '{sum += $3} END {printf "%.0f", sum}'
)
member_count=$(ar t "${archive}" | wc -l)
archive_index_bytes=$((archive_bytes - member_bytes - member_count * 60))

application_summary=$(
  find "${build_dir}/tests" -maxdepth 1 -type f -executable \
    -name 'fsim_application_tests*' -printf '%s\n' |
    awk '{sum += $1} END {printf "%d %.0f", NR, sum}'
)
read -r application_count application_bytes <<<"${application_summary}"

printf 'build_tree_bytes=%s\n' "${tree_bytes}"
printf 'representative_executable=%s\n' "${executable}"
printf 'representative_executable_bytes=%s\n' "${executable_bytes}"
printf 'representative_loaded_bytes=%s\n' "${loaded_bytes}"
printf 'representative_debug_bytes=%s\n' "${debug_bytes}"
printf 'representative_section_count=%s\n' "${section_count}"
printf 'elaboration_archive_bytes=%s\n' "${archive_bytes}"
printf 'elaboration_member_bytes=%s\n' "${member_bytes}"
printf 'elaboration_archive_index_bytes=%s\n' "${archive_index_bytes}"
printf 'application_executable_count=%s\n' "${application_count}"
printf 'application_executable_bytes=%s\n' "${application_bytes}"
