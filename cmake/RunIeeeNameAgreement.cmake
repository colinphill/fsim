# SPDX-License-Identifier: Apache-2.0

# Runs the projection-names tool and compares its output with the names the
# derived per-standard IEEE package sources declare
# (scripts/check_ieee_derived_sources.py names).
foreach(required IN ITEMS FSIM_NAMES_TOOL FSIM_PYTHON FSIM_SOURCE_DIR
    FSIM_WORK_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${FSIM_WORK_DIR}")
execute_process(
  COMMAND "${FSIM_NAMES_TOOL}" "${FSIM_WORK_DIR}/probe" "${FSIM_WORK_DIR}/names.txt"
  RESULT_VARIABLE tool_result
  OUTPUT_VARIABLE tool_output
  ERROR_VARIABLE tool_output
)
if(NOT tool_result EQUAL 0)
  message(FATAL_ERROR "the projection-names tool failed: ${tool_output}")
endif()
execute_process(
  COMMAND "${FSIM_PYTHON}" -I
    "${FSIM_SOURCE_DIR}/scripts/check_ieee_derived_sources.py" names
    "${FSIM_WORK_DIR}/names.txt"
    "${FSIM_SOURCE_DIR}/third_party/ieee-derived"
    "${FSIM_SOURCE_DIR}/scripts/vhdl_ieee_name_differences.tsv"
    "${FSIM_SOURCE_DIR}/scripts/vhdl_intrinsic_revisions.tsv"
  RESULT_VARIABLE check_result
  OUTPUT_VARIABLE check_output
  ERROR_VARIABLE check_output
)
message("${check_output}")
if(NOT check_result EQUAL 0)
  message(FATAL_ERROR "IEEE name agreement check failed")
endif()
