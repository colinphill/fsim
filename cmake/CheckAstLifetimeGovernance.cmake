# SPDX-License-Identifier: Apache-2.0

cmake_policy(SET CMP0007 NEW)

if(NOT DEFINED FSIM_SOURCE_DIR)
  message(FATAL_ERROR "FSIM_SOURCE_DIR is required")
endif()

set(FSIM_CONTRACT
  "${FSIM_SOURCE_DIR}/tests/feature_matrix/ast_lifetime_governance.tsv")
set(FSIM_APPLICATION_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/app/application.hpp")
set(FSIM_ELABORATOR_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/elaboration/elaborator.hpp")
set(FSIM_SPECIALIZATION_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/semantic/compiled_design_specialization.hpp")
set(FSIM_CACHE_HEADER
  "${FSIM_SOURCE_DIR}/include/fsim/compiler/object_cache.hpp")
set(FSIM_APPLICATION_INTERNAL
  "${FSIM_SOURCE_DIR}/src/app/application_internal.hpp")
set(FSIM_APPLICATION_CHECK
  "${FSIM_SOURCE_DIR}/src/app/application_check.cpp")
set(FSIM_APPLICATION_BUILD
  "${FSIM_SOURCE_DIR}/src/app/application_build.cpp")
set(FSIM_APPLICATION_OBJECT_PHASE
  "${FSIM_SOURCE_DIR}/src/app/application_phase_object.cpp")
set(FSIM_FRONTEND_DESIGN
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/design.hpp")
set(FSIM_FRONTEND_COVERAGE_STATE
  "${FSIM_SOURCE_DIR}/include/fsim/frontend/coverage_persistence.hpp")
set(FSIM_COMPILED_HIR_TEST
  "${FSIM_SOURCE_DIR}/tests/app/compiled_hir_cache_application_test.cpp")
set(FSIM_DIFFERENTIAL_TEST
  "${FSIM_SOURCE_DIR}/tests/app/mixed_conversion_application_test.cpp")
set(FSIM_TEST_BUILD "${FSIM_SOURCE_DIR}/tests/CMakeLists.txt")

foreach(FSIM_INPUT IN ITEMS
    "${FSIM_CONTRACT}"
    "${FSIM_APPLICATION_HEADER}"
    "${FSIM_ELABORATOR_HEADER}"
    "${FSIM_SPECIALIZATION_HEADER}"
    "${FSIM_CACHE_HEADER}"
    "${FSIM_APPLICATION_INTERNAL}"
    "${FSIM_APPLICATION_CHECK}"
    "${FSIM_APPLICATION_BUILD}"
    "${FSIM_APPLICATION_OBJECT_PHASE}"
    "${FSIM_FRONTEND_DESIGN}"
    "${FSIM_FRONTEND_COVERAGE_STATE}"
    "${FSIM_COMPILED_HIR_TEST}"
    "${FSIM_DIFFERENTIAL_TEST}"
    "${FSIM_TEST_BUILD}")
  if(NOT EXISTS "${FSIM_INPUT}")
    message(FATAL_ERROR
      "AST-lifetime governance input is missing: ${FSIM_INPUT}")
  endif()
endforeach()

file(READ "${FSIM_CONTRACT}" FSIM_CONTRACT_TEXT)
string(REPLACE "\r\n" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(REPLACE "\r" "\n" FSIM_CONTRACT_TEXT "${FSIM_CONTRACT_TEXT}")
string(SHA256 FSIM_CONTRACT_DIGEST "${FSIM_CONTRACT_TEXT}")
set(FSIM_EXPECTED_CONTRACT_DIGEST
  "ed52995ab8efcfb814f4c27065ba306b876712188bd75d64fbd0a2eb374dcf1b")
if(NOT FSIM_CONTRACT_DIGEST STREQUAL FSIM_EXPECTED_CONTRACT_DIGEST)
  message(FATAL_ERROR
    "AST-lifetime governance contract changed: expected "
    "${FSIM_EXPECTED_CONTRACT_DIGEST}, got ${FSIM_CONTRACT_DIGEST}")
endif()

file(STRINGS "${FSIM_CONTRACT}" FSIM_ROWS ENCODING UTF-8)
list(LENGTH FSIM_ROWS FSIM_ROW_COUNT)
if(NOT FSIM_ROW_COUNT EQUAL 39)
  message(FATAL_ERROR
    "AST-lifetime governance contract requires SPDX, header, and 37 scopes")
endif()
list(GET FSIM_ROWS 0 FSIM_SPDX)
list(GET FSIM_ROWS 1 FSIM_HEADER)
if(NOT FSIM_SPDX STREQUAL "# SPDX-License-Identifier: Apache-2.0"
   OR NOT FSIM_HEADER STREQUAL "scope\tpath\tkind")
  message(FATAL_ERROR
    "AST-lifetime governance contract header or SPDX policy changed")
endif()

set(FSIM_GOVERNED_SOURCES)
set(FSIM_COMPILE_LOCAL_SOURCES)
set(FSIM_SCOPE_NAMES)
foreach(FSIM_INDEX RANGE 2 38)
  list(GET FSIM_ROWS ${FSIM_INDEX} FSIM_ROW)
  string(REPLACE "\t" ";" FSIM_FIELDS "${FSIM_ROW}")
  list(LENGTH FSIM_FIELDS FSIM_FIELD_COUNT)
  if(NOT FSIM_FIELD_COUNT EQUAL 3)
    message(FATAL_ERROR
      "AST-lifetime governance row ${FSIM_INDEX} requires three fields")
  endif()
  list(GET FSIM_FIELDS 0 FSIM_SCOPE)
  list(GET FSIM_FIELDS 1 FSIM_PATH)
  list(GET FSIM_FIELDS 2 FSIM_KIND)
  if(FSIM_SCOPE STREQUAL "" OR FSIM_PATH STREQUAL "")
    message(FATAL_ERROR
      "AST-lifetime governance row ${FSIM_INDEX} has an empty field")
  endif()
  if(FSIM_PATH MATCHES "(^|/)\\.\\.?(/|$)" OR FSIM_PATH MATCHES "^/")
    message(FATAL_ERROR
      "AST-lifetime governance path is not source-relative: ${FSIM_PATH}")
  endif()
  list(FIND FSIM_SCOPE_NAMES "${FSIM_SCOPE}" FSIM_DUPLICATE_SCOPE)
  if(NOT FSIM_DUPLICATE_SCOPE EQUAL -1)
    message(FATAL_ERROR
      "duplicate AST-lifetime governance scope: ${FSIM_SCOPE}")
  endif()
  list(APPEND FSIM_SCOPE_NAMES "${FSIM_SCOPE}")

  set(FSIM_ABSOLUTE_PATH "${FSIM_SOURCE_DIR}/${FSIM_PATH}")
  if(FSIM_KIND STREQUAL "file")
    if(NOT EXISTS "${FSIM_ABSOLUTE_PATH}")
      message(FATAL_ERROR
        "AST-lifetime governed file is missing: ${FSIM_PATH}")
    endif()
    list(APPEND FSIM_GOVERNED_SOURCES "${FSIM_ABSOLUTE_PATH}")
  elseif(FSIM_KIND STREQUAL "compile-local-file")
    if(NOT EXISTS "${FSIM_ABSOLUTE_PATH}" OR
       NOT FSIM_PATH MATCHES "^src/app/[^/]+\\.(cpp|hpp)$")
      message(FATAL_ERROR
        "AST-lifetime compile-local file is missing or out of scope: ${FSIM_PATH}")
    endif()
    list(APPEND FSIM_COMPILE_LOCAL_SOURCES "${FSIM_ABSOLUTE_PATH}")
  elseif(FSIM_KIND STREQUAL "tree")
    if(NOT IS_DIRECTORY "${FSIM_ABSOLUTE_PATH}")
      message(FATAL_ERROR
        "AST-lifetime governed tree is missing: ${FSIM_PATH}")
    endif()
    file(GLOB_RECURSE FSIM_TREE_SOURCES
      LIST_DIRECTORIES FALSE
      "${FSIM_ABSOLUTE_PATH}/*.c"
      "${FSIM_ABSOLUTE_PATH}/*.cpp"
      "${FSIM_ABSOLUTE_PATH}/*.h"
      "${FSIM_ABSOLUTE_PATH}/*.hpp"
      "${FSIM_ABSOLUTE_PATH}/*.ixx"
      "${FSIM_ABSOLUTE_PATH}/*.tpp")
    if(NOT FSIM_TREE_SOURCES)
      message(FATAL_ERROR
        "AST-lifetime governed tree has no source files: ${FSIM_PATH}")
    endif()
    list(APPEND FSIM_GOVERNED_SOURCES ${FSIM_TREE_SOURCES})
  else()
    message(FATAL_ERROR
      "AST-lifetime governance row ${FSIM_INDEX} has invalid kind ${FSIM_KIND}")
  endif()
endforeach()
list(REMOVE_DUPLICATES FSIM_GOVERNED_SOURCES)
list(REMOVE_DUPLICATES FSIM_COMPILE_LOCAL_SOURCES)
list(REMOVE_ITEM FSIM_GOVERNED_SOURCES ${FSIM_COMPILE_LOCAL_SOURCES})

set(FSIM_STRUCTURAL_AST_TYPES
  ParsedDesign
  DesignUnit
  Expression
  Statement
  Process
  GenerateRegion
  GenerateBody
  Instance)

# Reject frontend result wrappers which own one of the structural types above.
# A post-compilation owner containing one of these wrappers would otherwise
# make structural syntax reachable without spelling its underlying AST type.
set(FSIM_STRUCTURAL_AST_OWNERS
  ParseResult
  Type
  TypeAliasDeclaration
  FunctionDeclaration
  ProcedureDeclaration
  SystemVerilogClassPropertyLayout
  SystemVerilogClassMethodProfile
  SystemVerilogClassSpecialization
  SystemVerilogClassSpecializationResult
  SystemVerilogCovergroupSampleCall
  SystemVerilogCovergroupSampleSyntax
  SystemVerilogCovergroupInstanceSyntax)

function(fsim_find_structural_ast_reference contents output_type)
  foreach(FSIM_AST_TYPE IN LISTS
      FSIM_STRUCTURAL_AST_TYPES FSIM_STRUCTURAL_AST_OWNERS)
    string(REGEX MATCH
      "frontend::${FSIM_AST_TYPE}([^A-Za-z0-9_]|$)"
      FSIM_AST_REFERENCE "${contents}")
    if(FSIM_AST_REFERENCE)
      set(${output_type} "${FSIM_AST_TYPE}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${output_type} "" PARENT_SCOPE)
endfunction()

foreach(FSIM_AST_TYPE IN LISTS FSIM_STRUCTURAL_AST_TYPES)
  fsim_find_structural_ast_reference(
    "struct Probe { frontend::${FSIM_AST_TYPE}* value; };"
    FSIM_PROBE_TYPE)
  if(NOT FSIM_PROBE_TYPE STREQUAL FSIM_AST_TYPE)
    message(FATAL_ERROR
      "AST-lifetime detector does not reject frontend::${FSIM_AST_TYPE}")
  endif()
endforeach()
foreach(FSIM_AST_OWNER IN LISTS FSIM_STRUCTURAL_AST_OWNERS)
  fsim_find_structural_ast_reference(
    "struct Probe { frontend::${FSIM_AST_OWNER} value; };"
    FSIM_PROBE_TYPE)
  if(NOT FSIM_PROBE_TYPE STREQUAL FSIM_AST_OWNER)
    message(FATAL_ERROR
      "AST-lifetime detector does not reject frontend::${FSIM_AST_OWNER}")
  endif()
endforeach()
fsim_find_structural_ast_reference(
  "frontend::ExpressionKind frontend::StatementKind frontend::ProcessKind "
  "frontend::InstanceKind"
  FSIM_LEAF_PROBE_TYPE)
if(FSIM_LEAF_PROBE_TYPE)
  message(FATAL_ERROR
    "AST-lifetime detector rejects permitted frontend leaf type "
    "frontend::${FSIM_LEAF_PROBE_TYPE}Kind")
endif()

# The frontend coverage execution adapter is permitted after compilation only
# while its instance record is leaf-only. Constructor/sample expressions live
# in the ParsedDesign sidecar and become HIR ExpressionIds before handoff.
file(READ "${FSIM_FRONTEND_DESIGN}" FSIM_FRONTEND_DESIGN_TEXT)
string(FIND "${FSIM_FRONTEND_DESIGN_TEXT}"
  "struct SystemVerilogCovergroupSampleCall"
  FSIM_LEGACY_COVERGROUP_SAMPLE_OFFSET)
if(NOT FSIM_LEGACY_COVERGROUP_SAMPLE_OFFSET EQUAL -1)
  message(FATAL_ERROR
    "frontend covergroup sample syntax regained a post-compilation name")
endif()
set(FSIM_COVERGROUP_INSTANCE_BEGIN
  "struct SystemVerilogCovergroupInstance {")
set(FSIM_COVERGROUP_INSTANCE_END
  "/// Compile-local structural syntax associated with one covergroup instance.")
string(FIND "${FSIM_FRONTEND_DESIGN_TEXT}"
  "${FSIM_COVERGROUP_INSTANCE_BEGIN}" FSIM_COVERGROUP_INSTANCE_BEGIN_OFFSET)
string(FIND "${FSIM_FRONTEND_DESIGN_TEXT}"
  "${FSIM_COVERGROUP_INSTANCE_END}" FSIM_COVERGROUP_INSTANCE_END_OFFSET)
if(FSIM_COVERGROUP_INSTANCE_BEGIN_OFFSET EQUAL -1 OR
   FSIM_COVERGROUP_INSTANCE_END_OFFSET EQUAL -1 OR
   NOT FSIM_COVERGROUP_INSTANCE_BEGIN_OFFSET LESS
       FSIM_COVERGROUP_INSTANCE_END_OFFSET)
  message(FATAL_ERROR
    "AST-lifetime coverage instance boundary is missing")
endif()
string(CONCAT FSIM_COVERGROUP_INSTANCE_LENGTH_EXPRESSION
  "${FSIM_COVERGROUP_INSTANCE_END_OFFSET} - "
  "${FSIM_COVERGROUP_INSTANCE_BEGIN_OFFSET}")
math(EXPR FSIM_COVERGROUP_INSTANCE_LENGTH
  "${FSIM_COVERGROUP_INSTANCE_LENGTH_EXPRESSION}")
string(SUBSTRING "${FSIM_FRONTEND_DESIGN_TEXT}"
  ${FSIM_COVERGROUP_INSTANCE_BEGIN_OFFSET}
  ${FSIM_COVERGROUP_INSTANCE_LENGTH}
  FSIM_COVERGROUP_INSTANCE_TEXT)
foreach(FSIM_FORBIDDEN_COVERGROUP_MEMBER IN ITEMS
    "Expression" "constructor_actuals" "sample_calls")
  string(FIND "${FSIM_COVERGROUP_INSTANCE_TEXT}"
    "${FSIM_FORBIDDEN_COVERGROUP_MEMBER}"
    FSIM_FORBIDDEN_COVERGROUP_MEMBER_OFFSET)
  if(NOT FSIM_FORBIDDEN_COVERGROUP_MEMBER_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "post-compilation covergroup instance owns forbidden syntax member: "
      "${FSIM_FORBIDDEN_COVERGROUP_MEMBER}")
  endif()
endforeach()
foreach(FSIM_COVERGROUP_SIDECAR_TOKEN IN ITEMS
    "struct SystemVerilogCovergroupSampleSyntax {"
    "struct SystemVerilogCovergroupInstanceSyntax {"
    "std::vector<Expression> constructor_actuals;"
    "std::vector<SystemVerilogCovergroupSampleSyntax> sample_calls;"
    "systemverilog_covergroup_instance_syntax;")
  string(FIND "${FSIM_FRONTEND_DESIGN_TEXT}"
    "${FSIM_COVERGROUP_SIDECAR_TOKEN}" FSIM_COVERGROUP_SIDECAR_TOKEN_OFFSET)
  if(FSIM_COVERGROUP_SIDECAR_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "compile-local covergroup syntax sidecar lost required boundary: "
      "${FSIM_COVERGROUP_SIDECAR_TOKEN}")
  endif()
endforeach()
file(READ "${FSIM_FRONTEND_COVERAGE_STATE}"
  FSIM_FRONTEND_COVERAGE_STATE_TEXT)
string(FIND "${FSIM_FRONTEND_COVERAGE_STATE_TEXT}"
  "struct SystemVerilogCoverageState {"
  FSIM_COVERAGE_STATE_BEGIN_OFFSET)
string(FIND "${FSIM_FRONTEND_COVERAGE_STATE_TEXT}"
  "[[nodiscard]] SystemVerilogCoverageState"
  FSIM_COVERAGE_STATE_END_OFFSET)
if(FSIM_COVERAGE_STATE_BEGIN_OFFSET EQUAL -1 OR
   FSIM_COVERAGE_STATE_END_OFFSET EQUAL -1 OR
   NOT FSIM_COVERAGE_STATE_BEGIN_OFFSET LESS FSIM_COVERAGE_STATE_END_OFFSET)
  message(FATAL_ERROR
    "AST-lifetime frontend coverage state boundary is missing")
endif()
string(CONCAT FSIM_COVERAGE_STATE_LENGTH_EXPRESSION
  "${FSIM_COVERAGE_STATE_END_OFFSET} - "
  "${FSIM_COVERAGE_STATE_BEGIN_OFFSET}")
math(EXPR FSIM_COVERAGE_STATE_LENGTH
  "${FSIM_COVERAGE_STATE_LENGTH_EXPRESSION}")
string(SUBSTRING "${FSIM_FRONTEND_COVERAGE_STATE_TEXT}"
  ${FSIM_COVERAGE_STATE_BEGIN_OFFSET}
  ${FSIM_COVERAGE_STATE_LENGTH}
  FSIM_COVERAGE_STATE_TEXT)
foreach(FSIM_FORBIDDEN_COVERAGE_STATE_TYPE IN ITEMS
    "ParsedDesign" "DesignUnit" "Expression" "Statement" "Process"
    "GenerateRegion" "GenerateBody" "InstanceSyntax" "SampleSyntax")
  string(FIND "${FSIM_COVERAGE_STATE_TEXT}"
    "${FSIM_FORBIDDEN_COVERAGE_STATE_TYPE}"
    FSIM_FORBIDDEN_COVERAGE_STATE_TYPE_OFFSET)
  if(NOT FSIM_FORBIDDEN_COVERAGE_STATE_TYPE_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "post-compilation frontend coverage state owns forbidden syntax type: "
      "${FSIM_FORBIDDEN_COVERAGE_STATE_TYPE}")
  endif()
endforeach()
string(FIND "${FSIM_COVERAGE_STATE_TEXT}"
  "std::vector<SystemVerilogCovergroupInstance> instances;"
  FSIM_COVERAGE_STATE_INSTANCE_OFFSET)
if(FSIM_COVERAGE_STATE_INSTANCE_OFFSET EQUAL -1)
  message(FATAL_ERROR
    "frontend coverage state lost its leaf-only instance inventory")
endif()

# Compile-local exceptions are explicit and authenticated. Each one must
# actually consume structural syntax; this prevents an unrelated application
# owner from being hidden in the exception set. Every other application source
# is scanned with the post-compilation owners below.
foreach(FSIM_SOURCE IN LISTS FSIM_COMPILE_LOCAL_SOURCES)
  file(READ "${FSIM_SOURCE}" FSIM_SOURCE_TEXT)
  string(REGEX REPLACE "[ \t\r\n]" "" FSIM_COMPACT_SOURCE
    "${FSIM_SOURCE_TEXT}")
  fsim_find_structural_ast_reference(
    "${FSIM_COMPACT_SOURCE}" FSIM_AST_TYPE)
  if(NOT FSIM_AST_TYPE)
    file(RELATIVE_PATH FSIM_RELATIVE_SOURCE
      "${FSIM_SOURCE_DIR}" "${FSIM_SOURCE}")
    message(FATAL_ERROR
      "AST-lifetime compile-local exception has no structural syntax input: "
      "${FSIM_RELATIVE_SOURCE}")
  endif()
endforeach()

foreach(FSIM_SOURCE IN LISTS FSIM_GOVERNED_SOURCES)
  file(READ "${FSIM_SOURCE}" FSIM_SOURCE_TEXT)
  string(REGEX REPLACE "[ \t\r\n]" "" FSIM_COMPACT_SOURCE
    "${FSIM_SOURCE_TEXT}")
  file(RELATIVE_PATH FSIM_RELATIVE_SOURCE
    "${FSIM_SOURCE_DIR}" "${FSIM_SOURCE}")
  fsim_find_structural_ast_reference(
    "${FSIM_COMPACT_SOURCE}" FSIM_AST_TYPE)
  if(FSIM_AST_TYPE)
    message(FATAL_ERROR
      "AST-lifetime boundary violation in ${FSIM_RELATIVE_SOURCE}: "
      "frontend::${FSIM_AST_TYPE}")
  endif()
  if(FSIM_COMPACT_SOURCE MATCHES
      "usingnamespace(::)?(fsim::)?frontend;")
    message(FATAL_ERROR
      "AST-lifetime boundary forbids a frontend using-directive in "
      "${FSIM_RELATIVE_SOURCE}")
  endif()
  if(FSIM_COMPACT_SOURCE MATCHES
      "namespace[A-Za-z_][A-Za-z0-9_]*=(::)?(fsim::)?frontend;")
    message(FATAL_ERROR
      "AST-lifetime boundary forbids a frontend namespace alias in "
      "${FSIM_RELATIVE_SOURCE}")
  endif()
endforeach()

function(fsim_require_ast_lifetime_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "AST-lifetime owner ${path} lost required boundary: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

function(fsim_reject_ast_lifetime_tokens path)
  file(READ "${path}" FSIM_CONTENTS)
  foreach(FSIM_TOKEN IN LISTS ARGN)
    string(FIND "${FSIM_CONTENTS}" "${FSIM_TOKEN}" FSIM_TOKEN_OFFSET)
    if(NOT FSIM_TOKEN_OFFSET EQUAL -1)
      message(FATAL_ERROR
        "AST-lifetime owner ${path} exposes forbidden token: ${FSIM_TOKEN}")
    endif()
  endforeach()
endfunction()

fsim_require_ast_lifetime_tokens("${FSIM_APPLICATION_HEADER}"
  "struct CheckedProject : semantic::CompiledDesign"
  "struct BuiltProject {"
  "std::optional<CheckedProject> check_project(")
fsim_reject_ast_lifetime_tokens("${FSIM_APPLICATION_HEADER}"
  "CompilationWorkspace")

fsim_require_ast_lifetime_tokens("${FSIM_ELABORATOR_HEADER}"
  "Parser-free compiled-HIR elaboration"
  "ElaborationResult elaborate("
  "const semantic::CompiledDesign& compiled")

fsim_require_ast_lifetime_tokens("${FSIM_SPECIALIZATION_HEADER}"
  "const CompiledDesign* design_ { };"
  "SpecializedHirOverlay specialization_;"
  "std::vector<sv::Expression> systemverilog_expressions_;"
  "std::vector<vhdl::Expression> vhdl_expressions_;"
  "std::vector<sv::Statement> systemverilog_statements_;"
  "std::vector<vhdl::Statement> vhdl_statements_;")

fsim_require_ast_lifetime_tokens("${FSIM_CACHE_HEADER}"
  "std::optional<std::vector<std::byte>> load("
  "std::span<const std::byte> payload")

fsim_require_ast_lifetime_tokens("${FSIM_APPLICATION_INTERNAL}"
  "struct CompilationWorkspace final : CheckedProject {"
  "frontend::ParsedDesign parsed;"
  "std::vector<semantic::CompiledDesign> mapped_compiled_designs;"
  "CheckedProject release_compiled_project("
  "CompilationWorkspace&& workspace);")
file(READ "${FSIM_APPLICATION_INTERNAL}" FSIM_APPLICATION_INTERNAL_TEXT)
string(REGEX REPLACE "[ \t\r\n]" "" FSIM_COMPACT_APPLICATION_INTERNAL
  "${FSIM_APPLICATION_INTERNAL_TEXT}")
if(FSIM_COMPACT_APPLICATION_INTERNAL MATCHES
    "configure_systemverilog_class_constraints\\([^;]*frontend::SystemVerilogClassSpecialization")
  message(FATAL_ERROR
    "AST-lifetime boundary forbids frontend class specialization in the "
    "runtime constraint interface")
endif()
if(FSIM_COMPACT_APPLICATION_INTERNAL MATCHES
    "systemverilog_randomize_callback\\(")
  message(FATAL_ERROR
    "AST-lifetime boundary forbids the syntax-backed randomize callback")
endif()

fsim_require_ast_lifetime_tokens("${FSIM_APPLICATION_CHECK}"
  "static std::optional<CompilationWorkspace> check_project_impl("
  "CompilationWorkspace checked;"
  "std::optional<CheckedProject> check_project("
  "auto workspace = check_project_impl(config, diagnostics);"
  "return release_compiled_project(std::move(*workspace));"
  "return std::move(static_cast<CheckedProject&>(workspace));")

fsim_require_ast_lifetime_tokens("${FSIM_APPLICATION_BUILD}"
  "std::optional<BuiltProject> build_checked_project("
  "release_compiled_project(std::move(*workspace))"
  "workspace.reset();"
  "auto elaborated = elaboration::elaborate("
  "auto design_ir = build_design_ir(")
file(READ "${FSIM_APPLICATION_BUILD}" FSIM_APPLICATION_BUILD_TEXT)
string(FIND "${FSIM_APPLICATION_BUILD_TEXT}"
  "release_compiled_project(std::move(*workspace))" FSIM_RELEASE_OFFSET)
string(FIND "${FSIM_APPLICATION_BUILD_TEXT}"
  "workspace.reset();" FSIM_DESTROY_OFFSET)
string(FIND "${FSIM_APPLICATION_BUILD_TEXT}"
  "auto elaborated = elaboration::elaborate(" FSIM_ELABORATE_OFFSET)
if(FSIM_RELEASE_OFFSET EQUAL -1 OR FSIM_DESTROY_OFFSET EQUAL -1 OR
   FSIM_ELABORATE_OFFSET EQUAL -1 OR
   NOT FSIM_RELEASE_OFFSET LESS FSIM_DESTROY_OFFSET OR
   NOT FSIM_DESTROY_OFFSET LESS FSIM_ELABORATE_OFFSET)
  message(FATAL_ERROR
    "AST-lifetime build handoff must release and destroy parser storage "
    "before elaboration")
endif()

fsim_require_ast_lifetime_tokens("${FSIM_APPLICATION_OBJECT_PHASE}"
  "std::optional<CheckedProject> load_objects("
  "return application_detail::release_compiled_project("
  "std::move(*workspace));")

fsim_require_ast_lifetime_tokens("${FSIM_COMPILED_HIR_TEST}"
  "HasSingularPublicElaborator<fsim::semantic::CompiledDesign>"
  "!HasSingularPublicElaborator<fsim::frontend::ParsedDesign>"
  "HasCovergroupConstructorActuals"
  "HasCovergroupSampleCalls"
  "fsim::runtime::SystemVerilogCoverageState::instances"
  "run_compiled_hir_lifetime_test("
  "run_vhdl_compiled_hir_lifetime_test("
  "run_split_vhdl_entity_architecture_object_test("
  "incompatible_link_diagnostics,"
  "\"FSIM-ART-VHDEP-001\","
  "assert(standard_mismatch.diagnostic_code"
  "assert(compatibility_mismatch.diagnostic_code"
  "== \"FSIM-FE-VHORDER-011\");"
  "for (const std::uint32_t jobs : { 1U, 2U, 4U, 8U })"
  "struct DeterminismSnapshot"
  "worker-count-checkout"
  "relocated-checkout-"
  "assert(snapshot == *worker_reference);"
  "assert(snapshot == *relocation_reference);"
  "assert_parser_independent_object(object, true);"
  "assert_parser_independent_library(library, \"work\", true);"
  "const std::string corrupt_bundle = \"FSIMCHIR\";"
  "recovered && !recovered->cache_hit")

fsim_require_ast_lifetime_tokens("${FSIM_DIFFERENTIAL_TEST}"
  "void assert_five_path_warning_equivalence("
  "compare_captures(direct, warm);"
  "compare_captures(direct, explicit_object);"
  "compare_captures(direct, mapped_library);"
  "compare_captures(direct, design_artifact);")
file(READ "${FSIM_DIFFERENTIAL_TEST}" FSIM_DIFFERENTIAL_TEST_TEXT)
string(REGEX REPLACE "[ \t\r\n]" "" FSIM_COMPACT_DIFFERENTIAL_TEST
  "${FSIM_DIFFERENTIAL_TEST_TEXT}")
string(REGEX MATCHALL "=capture_diagnostics\\("
  FSIM_DIAGNOSTIC_CAPTURES "${FSIM_COMPACT_DIFFERENTIAL_TEST}")
list(LENGTH FSIM_DIAGNOSTIC_CAPTURES FSIM_DIAGNOSTIC_CAPTURE_COUNT)
if(NOT FSIM_DIAGNOSTIC_CAPTURE_COUNT EQUAL 5)
  message(FATAL_ERROR
    "AST-lifetime differential must capture diagnostics from five paths")
endif()
string(CONCAT FSIM_NAMED_DIAGNOSTIC_CAPTURE_PATTERN
  "NamedDiagnosticCapture\\{\"(direct|warm-cache|object|"
  "mapped-library|design-artifact)\",&[A-Za-z_][A-Za-z0-9_]*\\}")
string(REGEX MATCHALL "${FSIM_NAMED_DIAGNOSTIC_CAPTURE_PATTERN}"
  FSIM_NAMED_DIAGNOSTIC_CAPTURES "${FSIM_COMPACT_DIFFERENTIAL_TEST}")
list(LENGTH FSIM_NAMED_DIAGNOSTIC_CAPTURES
  FSIM_NAMED_DIAGNOSTIC_CAPTURE_COUNT)
if(NOT FSIM_NAMED_DIAGNOSTIC_CAPTURE_COUNT EQUAL 5)
  message(FATAL_ERROR
    "AST-lifetime differential must compare the five named input paths")
endif()
foreach(FSIM_DIFFERENTIAL_PATH IN ITEMS
    direct warm-cache object mapped-library design-artifact)
  string(FIND "${FSIM_COMPACT_DIFFERENTIAL_TEST}"
    "NamedDiagnosticCapture{\"${FSIM_DIFFERENTIAL_PATH}\",&"
    FSIM_DIFFERENTIAL_PATH_OFFSET)
  if(FSIM_DIFFERENTIAL_PATH_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "AST-lifetime differential lost path ${FSIM_DIFFERENTIAL_PATH}")
  endif()
endforeach()
foreach(FSIM_DIFFERENTIAL_TOKEN IN ITEMS
    "conststd::array<NamedDiagnosticCapture,5>&captures"
    "constautoequivalent=*capture.diagnostics==expected;"
    "assert_five_path_warning_equivalence(diagnostics_by_path,\"top.sv\");"
    "=capture_diagnostics(design_publish_diagnostics);"
    "assert(design_project&&design_diagnostics.empty());")
  string(FIND "${FSIM_COMPACT_DIFFERENTIAL_TEST}"
    "${FSIM_DIFFERENTIAL_TOKEN}" FSIM_DIFFERENTIAL_TOKEN_OFFSET)
  if(FSIM_DIFFERENTIAL_TOKEN_OFFSET EQUAL -1)
    message(FATAL_ERROR
      "AST-lifetime differential lost required five-path structure: "
      "${FSIM_DIFFERENTIAL_TOKEN}")
  endif()
endforeach()

fsim_require_ast_lifetime_tokens("${FSIM_TEST_BUILD}"
  "NAME fsim.ast-lifetime-governance"
  "CheckAstLifetimeGovernance.cmake")

list(LENGTH FSIM_GOVERNED_SOURCES FSIM_SOURCE_COUNT)
list(LENGTH FSIM_SCOPE_NAMES FSIM_SCOPE_COUNT)
list(LENGTH FSIM_STRUCTURAL_AST_TYPES FSIM_AST_TYPE_COUNT)
list(LENGTH FSIM_STRUCTURAL_AST_OWNERS FSIM_AST_OWNER_COUNT)
list(LENGTH FSIM_COMPILE_LOCAL_SOURCES FSIM_COMPILE_LOCAL_COUNT)
message(STATUS
  "AST-lifetime and Change 19 governance passed: scopes=${FSIM_SCOPE_COUNT} "
  "sources=${FSIM_SOURCE_COUNT} structural-types=${FSIM_AST_TYPE_COUNT} "
  "structural-owners=${FSIM_AST_OWNER_COUNT} "
  "compile-local-files=${FSIM_COMPILE_LOCAL_COUNT} "
  "digest=${FSIM_CONTRACT_DIGEST}")
