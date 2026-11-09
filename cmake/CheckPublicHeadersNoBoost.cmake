# SPDX-License-Identifier: Apache-2.0

# Public headers (include/) must not include Boost. Boost.PFR is a private
# compile-time dependency (CMakeLists.txt, fsim_boost_pfr_headers): a build
# without a system Boost fetches it and gives its include path only to the
# targets that link fsim_boost_pfr_headers, so a public header that includes
# it breaks every consumer that does not.

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE FSIM_PUBLIC_HEADERS
  "${FSIM_SOURCE_DIR}/include/*.h"
  "${FSIM_SOURCE_DIR}/include/*.hpp")
set(FSIM_OFFENDERS "")
foreach(FSIM_HEADER IN LISTS FSIM_PUBLIC_HEADERS)
  file(STRINGS "${FSIM_HEADER}" FSIM_BOOST_LINES
    REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]boost/")
  if(FSIM_BOOST_LINES)
    file(RELATIVE_PATH FSIM_RELATIVE "${FSIM_SOURCE_DIR}" "${FSIM_HEADER}")
    list(APPEND FSIM_OFFENDERS "${FSIM_RELATIVE}")
  endif()
endforeach()
if(FSIM_OFFENDERS)
  list(JOIN FSIM_OFFENDERS "\n  " FSIM_TEXT)
  message(FATAL_ERROR "public headers include Boost:\n  ${FSIM_TEXT}")
endif()
