# SPDX-License-Identifier: Apache-2.0

function(fsim_source_package_is_excluded relative_path output_variable)
  set(FSIM_EXCLUDED FALSE)
  foreach(FSIM_RULE IN LISTS FSIM_SOURCE_PACKAGE_EXCLUSION_RULES)
    string(REPLACE "|" ";" FSIM_FIELDS "${FSIM_RULE}")
    list(GET FSIM_FIELDS 0 FSIM_KIND)
    list(GET FSIM_FIELDS 1 FSIM_VALUE)
    if(FSIM_KIND STREQUAL "root")
      if(relative_path STREQUAL FSIM_VALUE)
        set(FSIM_EXCLUDED TRUE)
      endif()
    elseif(FSIM_KIND STREQUAL "root-prefix")
      string(FIND "${relative_path}" "${FSIM_VALUE}" FSIM_PREFIX_INDEX)
      if(FSIM_PREFIX_INDEX EQUAL 0)
        set(FSIM_EXCLUDED TRUE)
      endif()
    elseif(FSIM_KIND STREQUAL "segment")
      string(FIND "/${relative_path}/" "/${FSIM_VALUE}/" FSIM_SEGMENT_INDEX)
      if(NOT FSIM_SEGMENT_INDEX EQUAL -1)
        set(FSIM_EXCLUDED TRUE)
      endif()
    elseif(FSIM_KIND STREQUAL "suffix")
      string(LENGTH "${relative_path}" FSIM_PATH_LENGTH)
      string(LENGTH "${FSIM_VALUE}" FSIM_VALUE_LENGTH)
      if(FSIM_PATH_LENGTH GREATER_EQUAL FSIM_VALUE_LENGTH)
        math(EXPR FSIM_SUFFIX_START "${FSIM_PATH_LENGTH} - ${FSIM_VALUE_LENGTH}")
        string(SUBSTRING
          "${relative_path}" ${FSIM_SUFFIX_START} ${FSIM_VALUE_LENGTH}
          FSIM_SUFFIX)
        if(FSIM_SUFFIX STREQUAL FSIM_VALUE)
          set(FSIM_EXCLUDED TRUE)
        endif()
      endif()
    else()
      message(FATAL_ERROR "unknown source-package exclusion kind: ${FSIM_KIND}")
    endif()
    if(FSIM_EXCLUDED)
      break()
    endif()
  endforeach()
  set(${output_variable} "${FSIM_EXCLUDED}" PARENT_SCOPE)
endfunction()

function(fsim_load_source_package_exclusions source_dir)
  set(FSIM_EXCLUSION_FILE
      "${source_dir}/packaging/source-package-exclusions.txt")
  if(NOT EXISTS "${FSIM_EXCLUSION_FILE}")
    message(FATAL_ERROR
      "source-package exclusion manifest is missing: ${FSIM_EXCLUSION_FILE}")
  endif()
  file(STRINGS "${FSIM_EXCLUSION_FILE}" FSIM_EXCLUSION_LINES ENCODING UTF-8)
  set(FSIM_RULES)
  foreach(FSIM_LINE IN LISTS FSIM_EXCLUSION_LINES)
    if(FSIM_LINE STREQUAL "" OR FSIM_LINE MATCHES "^#")
      continue()
    endif()
    string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_LINE}")
    list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
    if(NOT FSIM_FIELD_COUNT EQUAL 3)
      message(FATAL_ERROR
        "malformed source-package exclusion row: ${FSIM_LINE}")
    endif()
    list(GET FSIM_FIELDS 0 FSIM_KIND)
    list(GET FSIM_FIELDS 1 FSIM_VALUE)
    list(GET FSIM_FIELDS 2 FSIM_REASON)
    if(NOT FSIM_KIND MATCHES "^(root|root-prefix|segment|suffix)$"
       OR FSIM_VALUE STREQUAL "" OR FSIM_REASON STREQUAL "")
      message(FATAL_ERROR
        "invalid source-package exclusion row: ${FSIM_LINE}")
    endif()
    list(APPEND FSIM_RULES "${FSIM_KIND}|${FSIM_VALUE}|${FSIM_REASON}")
  endforeach()
  list(LENGTH FSIM_RULES FSIM_RULE_COUNT)
  if(NOT FSIM_RULE_COUNT EQUAL 23)
    message(FATAL_ERROR
      "source-package exclusion count changed: expected 23, found ${FSIM_RULE_COUNT}")
  endif()
  set(FSIM_SOURCE_PACKAGE_EXCLUSION_RULES "${FSIM_RULES}" PARENT_SCOPE)
endfunction()

function(fsim_collect_source_package_files source_dir output_variable)
  fsim_load_source_package_exclusions("${source_dir}")
  set(FSIM_SOURCE_PACKAGE_EXCLUSION_RULES
      "${FSIM_SOURCE_PACKAGE_EXCLUSION_RULES}")

  set(FSIM_TOP_FILES
      .clang-format
      .gitattributes
      .gitignore
      CMakeLists.txt
      CMakePresets.json
      LICENSE
      README.md)
  set(FSIM_OWNED_ROOTS
      .github
      cmake
      docs
      examples
      include
      packaging
      scripts
      src
      tests
      third_party)

  file(GLOB FSIM_TOP_ENTRIES RELATIVE "${source_dir}"
       "${source_dir}/*" "${source_dir}/.*")
  foreach(FSIM_ENTRY IN LISTS FSIM_TOP_ENTRIES)
    if(FSIM_ENTRY STREQUAL "." OR FSIM_ENTRY STREQUAL "..")
      continue()
    endif()
    if(FSIM_ENTRY IN_LIST FSIM_TOP_FILES OR FSIM_ENTRY IN_LIST FSIM_OWNED_ROOTS)
      continue()
    endif()
    fsim_source_package_is_excluded("${FSIM_ENTRY}" FSIM_EXCLUDED)
    if(NOT FSIM_EXCLUDED)
      message(FATAL_ERROR
        "unexpected top-level source-package entry: ${FSIM_ENTRY}")
    endif()
  endforeach()

  set(FSIM_FILES)
  foreach(FSIM_FILE IN LISTS FSIM_TOP_FILES)
    if(NOT EXISTS "${source_dir}/${FSIM_FILE}")
      message(FATAL_ERROR "required source-package file is missing: ${FSIM_FILE}")
    endif()
    list(APPEND FSIM_FILES "${FSIM_FILE}")
  endforeach()
  foreach(FSIM_ROOT IN LISTS FSIM_OWNED_ROOTS)
    if(NOT IS_DIRECTORY "${source_dir}/${FSIM_ROOT}")
      message(FATAL_ERROR "required source-package root is missing: ${FSIM_ROOT}")
    endif()
    file(GLOB_RECURSE FSIM_ROOT_FILES LIST_DIRECTORIES FALSE
         RELATIVE "${source_dir}" "${source_dir}/${FSIM_ROOT}/*")
    foreach(FSIM_FILE IN LISTS FSIM_ROOT_FILES)
      fsim_source_package_is_excluded("${FSIM_FILE}" FSIM_EXCLUDED)
      if(NOT FSIM_EXCLUDED)
        if(IS_SYMLINK "${source_dir}/${FSIM_FILE}")
          message(FATAL_ERROR
            "source-package entries must not be symbolic links: ${FSIM_FILE}")
        endif()
        list(APPEND FSIM_FILES "${FSIM_FILE}")
      endif()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES FSIM_FILES)
  list(SORT FSIM_FILES)
  set(${output_variable} "${FSIM_FILES}" PARENT_SCOPE)
endfunction()
