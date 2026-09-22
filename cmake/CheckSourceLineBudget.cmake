# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

message(
  STATUS
  "The physical source-line ceiling is disabled; checking translation-unit "
  "structure only"
)

include("${FSIM_SOURCE_DIR}/cmake/CheckTranslationUnitStructure.cmake")
