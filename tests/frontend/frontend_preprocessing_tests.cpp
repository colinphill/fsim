// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[maybe_unused]] std::filesystem::path
make_test_directory(std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("fsim-" + std::string{name} + "-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(const std::filesystem::path &path,
                                 const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(output.good(), "frontend test fixture must be writable");
}

} // namespace

void test_systemverilog_preprocessor() {
  const auto directory = make_test_directory("verilog-preprocessor");
  const auto include_directory = directory / "include";
  const auto root = directory / "root.sv";
  write_text(include_directory / "nested" / "values.svh",
             R"(`define FROM_NESTED 4'b1010
)");
  write_text(include_directory / "definitions.svh",
             R"(`include <nested/values.svh>
`define JOIN(left,right) left``right
`define RANGE [3:0]
`define MESSAGE(value) `"value`"
`define DEFAULTED(value=4'b0011) value
`define ASSIGN_VALUES \
    value = `FROM_NESTED; \
    command_value = `FROM_COMMAND;
)");
  write_text(root,
             R"(`define HEADER "definitions.svh"
`include `HEADER
`define NESTED_NAME values.svh
`include <nested/`NESTED_NAME>
`define SELECT_EXPECTED
`undef SELECT_EXPECTED
`ifndef SELECT_EXPECTED
`define SELECT_EXPECTED
`endif
`ifdef MISSING
module inactive_bad(;
`elsif SELECT_EXPECTED
`timescale 1ns/1ps
module `JOIN(pre,processed)(
  output logic `RANGE value,
  output logic [3:0] command_value
);
  logic [7:0] source_line;
  logic [3:0] default_value;
  initial begin
    `ASSIGN_VALUES
    default_value = `DEFAULTED();
    source_line = `__LINE__;
    assert (1'b1) else $error(`MESSAGE(preprocessed));
    assert (1'b1) else $error(`__FILE__);
  end
endmodule
`else
module also_inactive_bad(;
`endif
)");

  PreprocessorOptions options;
  options.include_directories = {include_directory};
  options.defines = {"FROM_COMMAND=4'b0101"};
  auto preprocessed =
      preprocess_verilog_file(root, Language::SystemVerilog2017, options);
  require(preprocessed.ok(),
          "includes, command definitions, conditionals, function macros, and "
          "token concatenation must preprocess");
  require(preprocessed.dependencies.size() == 3,
          "root and two transitive include snapshots");
  require(preprocessed.dependencies[1].path.filename() == "definitions.svh" &&
              preprocessed.dependencies[2].path.filename() == "values.svh",
          "dependencies retain deterministic first-use order");

  const auto parsed = parse_verilog(std::move(preprocessed.lexed), true);
  require(parsed.ok(), "preprocessed SystemVerilog must parse");
  require(parsed.design.units.size() == 1 &&
              parsed.design.units.front().name == "preprocessed",
          "conditional selection and token concatenation");
  const auto &unit = parsed.design.units.front();
  require(unit.time_unit == "1ns" && unit.time_precision == "1ps",
          "timescale directive survives preprocessing");
  require(unit.ports.size() == 2 && unit.ports.front().type.width() ==
                                        std::optional<std::uint64_t>{4},
          "object macro expands into a packed range");
  require(unit.processes.size() == 1 &&
              unit.processes.front().statements.size() == 6 &&
              unit.processes.front().statements[0].value.text == "4'b1010" &&
              unit.processes.front().statements[1].value.text == "4'b0101",
          "transitive and command-line macro values reach the AST");
  require(unit.processes.front().statements[2].value.text == "4'b0011",
          "omitted function-macro argument uses its default");
  require(unit.processes.front().statements[3].value.kind ==
              ExpressionKind::IntegerLiteral,
          "`__LINE__ expands to an integer literal");
  require(unit.processes.front().statements[4].else_statements.size() == 1 &&
              unit.processes.front()
                      .statements[4]
                      .else_statements.front()
                      .output_text == "preprocessed",
          "SystemVerilog macro stringification");
  require(std::filesystem::path{unit.processes.front()
                                    .statements[5]
                                    .else_statements.front()
                                    .output_text}
                  .filename() == "root.sv",
          "`__FILE__ expands to the normalized source name");

  const auto compilation_first = directory / "shared-first.sv";
  const auto compilation_second = directory / "shared-second.sv";
  write_text(compilation_first,
             R"(`timescale 10ns/1ns
`define SHARED_VALUE 4'b1100
`ifdef ENABLE_SHARED_UNIT
)");
  write_text(compilation_second,
             R"(module shared_compilation_unit;
  logic [3:0] value;
  initial begin
    value = `SHARED_VALUE;
    #1 $finish;
  end
endmodule
`endif
)");
  PreprocessorOptions shared_options;
  shared_options.defines = {"ENABLE_SHARED_UNIT=1"};
  auto shared = preprocess_verilog_compilation_unit(
      {compilation_first, compilation_second}, Language::SystemVerilog2017,
      shared_options);
  require(shared.ok() && shared.roots.size() == 2 && shared.inputs.size() == 2,
          "ordered roots form one exact preprocessing compilation unit");
  const auto shared_parsed = parse_verilog(std::move(shared.lexed), true);
  require(shared_parsed.ok() && shared_parsed.design.units.size() == 1 &&
              shared_parsed.design.units.front().name ==
                  "shared_compilation_unit" &&
              shared_parsed.design.units.front().time_unit == "10ns" &&
              shared_parsed.design.units.front()
                      .processes.front()
                      .statements.front()
                      .value.text == "4'b1100",
          "macro, conditional, and timescale state persist across roots");

  const auto bad_root = directory / "bad.sv";
  write_text(bad_root,
             R"(`define BAD_ASSIGN target = )
module bad;
  logic target;
  initial begin
    `BAD_ASSIGN;
  end
endmodule
)");
  auto bad_preprocessed =
      preprocess_verilog_file(bad_root, Language::SystemVerilog2017);
  const auto bad = parse_verilog(std::move(bad_preprocessed.lexed), true);
  require(!bad.ok(), "invalid macro expansion must be rejected");
  require(std::any_of(bad.diagnostics.begin(), bad.diagnostics.end(),
                      [](const Diagnostic &diagnostic) {
                        return !diagnostic.expansion_stack.empty() &&
                               diagnostic.expansion_stack.front().find(
                                   "macro `BAD_ASSIGN'") != std::string::npos;
                      }),
          "parser diagnostics retain macro definition/invocation ancestry");

  const auto bad_include = include_directory / "bad.svh";
  write_text(bad_include, "module included_bad(;\n");
  const auto include_error_root = directory / "include-error.sv";
  write_text(include_error_root, "`include \"bad.svh\"\n");
  auto include_error_preprocessed = preprocess_verilog_file(
      include_error_root, Language::SystemVerilog2017, options);
  const auto include_error =
      parse_verilog(std::move(include_error_preprocessed.lexed), true);
  require(!include_error.ok() &&
              std::any_of(include_error.diagnostics.begin(),
                          include_error.diagnostics.end(),
                          [](const Diagnostic &diagnostic) {
                            return !diagnostic.expansion_stack.empty() &&
                                   diagnostic.expansion_stack.front().find(
                                       "included '") != std::string::npos;
                          }),
          "diagnostics retain nested include ancestry");

  const auto missing_root = directory / "missing.sv";
  write_text(missing_root, "`include \"absent.svh\"\n");
  const auto missing = preprocess_verilog_file(
      missing_root, Language::SystemVerilog2017, options);
  require(!missing.ok() && std::any_of(missing.lexed.diagnostics.begin(),
                                       missing.lexed.diagnostics.end(),
                                       [](const Diagnostic &diagnostic) {
                                         return diagnostic.code ==
                                                "FSIM-SV-PP-022";
                                       }),
          "missing include receives a targeted diagnostic");

  const auto directive_error_root = directory / "directive-errors.sv";
  write_text(directive_error_root,
             R"(`define TWO(first,second) first
`TWO(one)
`define EMPTY
`include `EMPTY
`resetall
`pragma protect
`ifdef LEFT_OPEN
)");
  const auto directive_errors = preprocess_verilog_file(
      directive_error_root, Language::SystemVerilog2017);
  const auto has_preprocessor_code = [&](const std::string_view code) {
      return std::any_of(
          directive_errors.lexed.diagnostics.begin(),
          directive_errors.lexed.diagnostics.end(),
          [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
  };
  require(!directive_errors.ok() && has_preprocessor_code("FSIM-SV-PP-030") && has_preprocessor_code("FSIM-SV-PP-020") && has_preprocessor_code("FSIM-SV-PP-011") && has_preprocessor_code("FSIM-SV-PP-002"),
      "macro arity, malformed protect pragma, and open conditional diagnostics");

  auto ignored_and_plaintext_pragmas = preprocess_verilog(
      SourceText { "pragma-policy.sv",
          R"(`pragma vendor_extension option = enabled
`pragma protect begin
module visible_plaintext;
endmodule
`pragma protect end
)" },
      Language::SystemVerilog2017);
  auto ignored_and_plaintext_parsed = parse_verilog(
      std::move(ignored_and_plaintext_pragmas.lexed), true);
  require(
      ignored_and_plaintext_parsed.ok() && ignored_and_plaintext_parsed.design.units.size() == 1 && ignored_and_plaintext_parsed.design.units.front().name == "visible_plaintext",
      "unknown pragmas have no effect and plaintext protect markers compile");

  auto encrypted_envelope = preprocess_verilog(
      SourceText { "protected-envelope.sv",
          R"(module visible_before;
endmodule
`pragma protect begin_protected
`pragma protect data_method = "aes128-cbc"
`pragma protect data_block
encrypted payload tokens 12345
`pragma protect end_protected
module visible_after;
endmodule
)" },
      Language::SystemVerilog2017);
  const bool has_protected_code = std::ranges::any_of(
      encrypted_envelope.lexed.diagnostics,
      [](const Diagnostic& diagnostic) {
          return diagnostic.code == "FSIM-SV-PP-011";
      });
  auto encrypted_parsed = parse_verilog(
      std::move(encrypted_envelope.lexed), true);
  require(
      has_protected_code && encrypted_parsed.design.units.size() == 2 && encrypted_parsed.design.units[0].name == "visible_before" && encrypted_parsed.design.units[1].name == "visible_after",
      "encrypted envelopes are contained and require a decryption provider");

  const auto cycle_a = directory / "cycle-a.svh";
  const auto cycle_b = directory / "cycle-b.svh";
  write_text(cycle_a, "`include \"cycle-b.svh\"\n");
  write_text(cycle_b, "`include \"cycle-a.svh\"\n");
  const auto cycle = preprocess_verilog_file(cycle_a, Language::SystemVerilog2017);
  require(!cycle.ok() && std::any_of(cycle.lexed.diagnostics.begin(), cycle.lexed.diagnostics.end(), [](const Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SV-PP-005";
  }),
      "recursive include receives a targeted diagnostic");
  const auto empty_compilation_unit = preprocess_verilog_compilation_unit({ }, Language::SystemVerilog2017);
  require(!empty_compilation_unit.ok() && std::any_of(empty_compilation_unit.lexed.diagnostics.begin(), empty_compilation_unit.lexed.diagnostics.end(), [](const Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SV-PP-031";
  }),
      "an empty preprocessing compilation unit is rejected");

  auto reset_macros = preprocess_verilog(SourceText { "undefineall.sv",
                                             R"(`define SAME(value=1) value
`define SAME(value=1) value
`define CLEARED 1
`undefineall
`ifndef CLEARED
module macros_cleared;
endmodule
`endif
)" },
      Language::SystemVerilog2017);
  auto reset_parsed = parse_verilog(std::move(reset_macros.lexed), true);
  require(reset_parsed.ok() && reset_parsed.design.units.size() == 1 && reset_parsed.design.units.front().name == "macros_cleared",
      "identical redefinition is legal and `undefineall clears macros");

  const auto conflicting_redefinition = preprocess_verilog(
      SourceText { "macro-redefinition.sv", "`define VALUE 1\n`define VALUE 2\n" },
      Language::SystemVerilog2017);
  require(!conflicting_redefinition.ok() && std::ranges::any_of(conflicting_redefinition.lexed.diagnostics, [](const Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SV-PP-044" && !diagnostic.expansion_stack.empty();
  }),
      "a conflicting macro redefinition retains the prior location");

  const auto cross_close = directory / "cross-close.svh";
  const auto cross_open = directory / "cross-open.svh";
  const auto cross_root = directory / "cross-root.sv";
  write_text(cross_close, "`endif\n");
  write_text(cross_open, "`ifdef OPEN_IN_INCLUDE\n");
  write_text(cross_root,
      R"(`define FLAG
`ifdef FLAG
`include "cross-close.svh"
`include "cross-open.svh"
`endif
)");
  const auto cross_boundary = preprocess_verilog_file(cross_root, Language::SystemVerilog2017);
  require(
      cross_boundary.ok(),
      "textual includes may open and close conditional branches across files");

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
}

void test_systemverilog_uvm_macro_surface()
{
    const auto directory = make_test_directory("uvm-macro-surface");
    const auto include = directory / "uvm_like_macros.svh";
    const auto root = directory / "root.sv";
    write_text(include,
        R"(`ifndef FSIM_UVM_LIKE_MACROS_SVH
`define FSIM_UVM_LIKE_MACROS_SVH
`define UVM_FORWARD(first, second) localparam int first = second;
`define UVM_DECLARE(SFX, FORWARDED) \
module uvm_generated``SFX; \
  // A comment-ending continuation is used by both governed UVM kits. \
  `UVM_FORWARD FORWARDED \
`ifdef FSIM_UVM_ALTERNATE localparam int selected = 1; \
`else localparam int selected = 2; \
`endif \
  initial $display(`"uvm_generated``SFX FORWARDED`"); \
endmodule
`endif
)");
    write_text(root,
        R"(`include "uvm_like_macros.svh"
`include "uvm_like_macros.svh"
`define FSIM_UVM_SUFFIX _item
`UVM_DECLARE(`FSIM_UVM_SUFFIX, (forwarded_value, 7))
)");
    PreprocessorOptions options;
    options.include_directories = { directory };
    auto preprocessed = preprocess_verilog_file(root, Language::SystemVerilog2017, options);
    require(preprocessed.ok(),
        "UVM-style guarded, nested, forwarded, conditional, pasted, and "
        "stringified macros must preprocess");
    const auto has_token = [&](const TokenKind kind,
                               const std::string_view text) {
        return std::ranges::any_of(
            preprocessed.lexed.tokens, [&](const Token& token) {
                return token.kind == kind && token.text == text;
            });
    };
    require(has_token(TokenKind::Identifier, "uvm_generated_item") && has_token(TokenKind::Identifier, "forwarded_value") && has_token(TokenKind::Number, "7") && has_token(TokenKind::Number, "2"),
        "nested suffix expansion, tuple forwarding, and replacement "
        "conditional selection must reach generated declarations");
    require(
        std::ranges::any_of(preprocessed.lexed.tokens,
            [](const Token& token) {
                return token.kind == TokenKind::StringLiteral && token.text.find("uvm_generated_item") != std::string::npos && token.text.find("forwarded_value") != std::string::npos && token.text.find('`') == std::string::npos;
            }),
        "composed special strings must substitute parameters and remove paste "
        "markers");
    const auto parsed = parse_verilog(std::move(preprocessed.lexed), true);
    require(parsed.ok() && parsed.design.units.size() == 1 && parsed.design.units.front().name == "uvm_generated_item",
        "UVM-style macro expansion must generate one parseable declaration");

    const auto punctuation_paste = preprocess_verilog(SourceText { "uvm-punctuation-paste.sv",
                                                          "`define UVM_MEMBER(BASE) BASE``.value\n"
                                                          "`UVM_MEMBER(resource_queue)\n" },
        Language::SystemVerilog2017);
    require(
        punctuation_paste.ok() && punctuation_paste.lexed.tokens.size() == 4 && punctuation_paste.lexed.tokens[0].text == "resource_queue" && punctuation_paste.lexed.tokens[1].kind == TokenKind::Dot && punctuation_paste.lexed.tokens[2].text == "value",
        "a paste marker may delimit a macro argument before punctuation without "
        "forming one lexical token");

    const auto malformed = preprocess_verilog(SourceText { "uvm-macro-errors.sv",
                                                  R"(`define BAD_CONDITIONAL \
`ifdef \
module bad_conditional; endmodule
`BAD_CONDITIONAL
`define BAD_OPEN \
`ifdef NEVER_DEFINED \
module bad_open; endmodule
`BAD_OPEN
`define BAD_STRING(ARG) `"ARG``"
`BAD_STRING(value)
)" },
        Language::SystemVerilog2017);
    const auto has_code = [&](const std::string_view code) {
        return std::ranges::any_of(
            malformed.lexed.diagnostics,
            [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    };
    require(
        !malformed.ok() && has_code("FSIM-SV-PP-049") && has_code("FSIM-SV-PP-050") && has_code("FSIM-SV-PP-051"),
        "malformed replacement conditionals and special strings receive exact "
        "cataloged diagnostics");

#if defined(FSIM_TEST_UVM_1_2_SOURCE_DIR) &&                                   \
    defined(FSIM_TEST_UVM_2020_3_1_SOURCE_DIR)
  const std::array<std::filesystem::path, 2> upstream_roots{
      std::filesystem::path{FSIM_TEST_UVM_1_2_SOURCE_DIR} / "src",
      std::filesystem::path{FSIM_TEST_UVM_2020_3_1_SOURCE_DIR} / "src"};
  for (std::size_t release = 0; release < upstream_roots.size(); ++release) {
    const auto upstream_probe =
        directory / ("upstream-" + std::to_string(release) + ".sv");
    write_text(upstream_probe,
               R"(`include "uvm_macros.svh"
class fsim_uvm_macro_item;
  int integral;
  real real_value;
  string text;
  fsim_uvm_macro_item child;
  int dynamic_values[];
  int queue_values[$];
  int keyed_values[string];
  `uvm_object_utils_begin(fsim_uvm_macro_item)
    `uvm_field_int(integral, UVM_ALL_ON)
    `uvm_field_real(real_value, UVM_DEFAULT)
    `uvm_field_string(text, UVM_DEFAULT)
    `uvm_field_object(child, UVM_REFERENCE)
    `uvm_field_array_int(dynamic_values, UVM_DEFAULT)
    `uvm_field_queue_int(queue_values, UVM_DEFAULT)
    `uvm_field_aa_int_string(keyed_values, UVM_DEFAULT)
  `uvm_object_utils_end
endclass
class fsim_uvm_macro_component;
  `uvm_component_utils(fsim_uvm_macro_component)
  `uvm_register_cb(fsim_uvm_macro_component, fsim_uvm_macro_callback)
endclass
class fsim_uvm_explicit_object_registry;
  `uvm_object_registry(fsim_uvm_explicit_object_registry,
                       "fsim_uvm_explicit_object_registry")
endclass
class fsim_uvm_explicit_component_registry;
  `uvm_component_registry(fsim_uvm_explicit_component_registry,
                          "fsim_uvm_explicit_component_registry")
endclass
`uvm_analysis_imp_decl(_fsim)
function void fsim_uvm_legacy_macro_calls;
  fsim_uvm_macro_item request;
  `uvm_create(request)
  `uvm_do(request)
  `uvm_do_pri_with(request, 7, { integral > 0; })
  `uvm_send(request)
  `uvm_rand_send_with(request, { integral < 32; })
  `uvm_do_callbacks(fsim_uvm_macro_component,
                    fsim_uvm_macro_callback, invoked())
  `uvm_info("LEGACY_INFO", "message", UVM_LOW)
  `uvm_warning("LEGACY_WARNING", "message")
  `uvm_error("LEGACY_ERROR", "message")
  `uvm_fatal("LEGACY_FATAL", "message")
endfunction
)");
    PreprocessorOptions upstream_options;
    upstream_options.include_directories = {upstream_roots[release]};
    const auto upstream = preprocess_verilog_file(
        upstream_probe, Language::SystemVerilog2017, upstream_options);
    const auto identifier_count = [&](const std::string_view spelling) {
      return std::ranges::count_if(
          upstream.lexed.tokens, [&](const Token &token) {
            return token.kind == TokenKind::Identifier &&
                   token.text == spelling;
          });
    };
    const std::array required_identifiers{
        std::string_view{"uvm_analysis_imp_fsim"},
        std::string_view{"m_register_cb_fsim_uvm_macro_callback"},
        std::string_view{"uvm_callback_iter"},
        std::string_view{"create_item"},
        std::string_view{"start_item"},
        std::string_view{"finish_item"},
        std::string_view{"uvm_report_info"},
        std::string_view{"uvm_report_warning"},
        std::string_view{"uvm_report_error"},
        std::string_view{"uvm_report_fatal"}};
    const auto field_helper =
        release == 0 ? std::string_view{"__m_uvm_field_automation"}
                     : std::string_view{"__m_uvm_execute_field_op"};
    const auto required_present =
        std::ranges::all_of(required_identifiers, [&](const auto spelling) {
          return identifier_count(spelling) > 0;
        });
    const auto macro_free =
        std::ranges::none_of(upstream.lexed.tokens, [](const Token &token) {
          return token.text.find('`') != std::string::npos;
        });
    std::string observed{
        " ok=" + std::to_string(upstream.ok()) +
        " required=" + std::to_string(required_present) +
        " macro_free=" + std::to_string(macro_free) +
        " type_id=" + std::to_string(identifier_count("type_id")) +
        " get_type=" + std::to_string(identifier_count("get_type")) +
        " get_object_type=" +
        std::to_string(identifier_count("get_object_type")) +
        " create_item=" + std::to_string(identifier_count("create_item")) +
        " start_item=" + std::to_string(identifier_count("start_item")) +
        " finish_item=" + std::to_string(identifier_count("finish_item"))};
    observed += " field_helper=";
    observed += std::to_string(identifier_count(field_helper));
    for (const auto &diagnostic : upstream.lexed.diagnostics) {
      observed += " ";
      observed += diagnostic.code;
    }
    for (const auto spelling : required_identifiers) {
      observed += " ";
      observed += spelling;
      observed += "=";
      observed += std::to_string(identifier_count(spelling));
    }
    require(
        upstream.ok() && required_present &&
            identifier_count(field_helper) > 0 &&
            identifier_count("type_id") >= 4 &&
            identifier_count("get_type") >= 4 &&
            identifier_count("get_object_type") >= 4 &&
            identifier_count("create_item") >= 3 &&
            identifier_count("start_item") >= 4 &&
            identifier_count("finish_item") >= 4 && macro_free,
        "the exact governed UVM 1.2 legacy field/object/component/registry, "
        "sequence, callback, TLM declaration, and report macro families must "
        "expand to their expected utility and call signatures without source "
        "modification;" +
            observed);
    const auto upstream_package =
        preprocess_verilog_file(upstream_roots[release] / "uvm_pkg.sv",
                                Language::SystemVerilog2017, upstream_options);
    require(upstream_package.ok(),
            "the complete exact governed UVM package must preprocess without "
            "modification");
    const auto package_identifier_count = [&](const std::string_view spelling) {
      return std::ranges::count_if(
          upstream_package.lexed.tokens, [&](const Token &token) {
            return token.kind == TokenKind::Identifier &&
                   token.text == spelling;
          });
    };
    const auto version_probe =
        directory / ("upstream-version-" + std::to_string(release) + ".sv");
    if (release == 0) {
      write_text(version_probe,
                 R"(`include "macros/uvm_version_defines.svh"
`UVM_MAJOR_REV
`UVM_MINOR_REV
`UVM_VERSION_STRING
`ifdef UVM_VERSION_1_2
uvm_version_1_2_enabled
`endif
)");
    } else {
      write_text(version_probe,
                 R"(`include "macros/uvm_version_defines.svh"
`UVM_VERSION
`UVM_MAJOR_REV
`UVM_MINOR_REV
`UVM_VERSION_STRING
`ifdef UVM_VERSION_POST_2020_2_0
uvm_version_post_2020_2_0_enabled
`endif
`ifdef UVM_POST_VERSION_1_2
uvm_post_version_1_2_enabled
`endif
)");
    }
    const auto version = preprocess_verilog_file(
        version_probe, Language::SystemVerilog2017, upstream_options);
    const auto version_has_token = [&](const std::string_view spelling) {
      return std::ranges::any_of(version.lexed.tokens, [&](const Token &token) {
        return token.text == spelling;
      });
    };
    require(
        version.ok() &&
            (release == 0
                 ? version_has_token("1") && version_has_token("2") &&
                       version_has_token("\"UVM-1.2\"") &&
                       version_has_token("uvm_version_1_2_enabled")
                 : version_has_token("2020") && version_has_token("3.0") &&
                       version_has_token("uvm_pkg") &&
                       version_has_token("UVM_VERSION_STRING") &&
                       version_has_token("uvm_version_post_2020_2_0_enabled") &&
                       version_has_token("uvm_post_version_1_2_enabled")),
        "the exact governed release must expose its own version numbers, "
        "string mapping, and compatibility ladder");
    if (release == 0) {
      const std::array legacy_package_aliases{
          std::string_view{"uvm_top"}, std::string_view{"uvm_test_done"},
          std::string_view{"global_stop_request"},
          std::string_view{"set_global_timeout"},
          std::string_view{"set_global_stop_timeout"}};
      require(std::ranges::all_of(legacy_package_aliases,
                                  [&](const auto spelling) {
                                    return package_identifier_count(spelling) >
                                           0;
                                  }),
              "the exact governed UVM 1.2 package must retain its deprecated "
              "root, test-done, stop-request, and timeout aliases");

      const auto deprecated_probe = directory / "upstream-deprecated-0.sv";
      write_text(deprecated_probe,
                 R"(`include "uvm_macros.svh"
class fsim_uvm_deprecated_sequencer;
  `uvm_sequencer_utils(fsim_uvm_deprecated_sequencer)
endclass
class fsim_uvm_deprecated_sequence;
  `uvm_sequence_utils(fsim_uvm_deprecated_sequence,
                      fsim_uvm_deprecated_sequencer)
endclass
)");
      const auto deprecated = preprocess_verilog_file(
          deprecated_probe, Language::SystemVerilog2017, upstream_options);
      const auto deprecated_identifier_count =
          [&](const std::string_view spelling) {
            return std::ranges::count_if(
                deprecated.lexed.tokens, [&](const Token &token) {
                  return token.kind == TokenKind::Identifier &&
                         token.text == spelling;
                });
          };
      const std::array deprecated_generated_identifiers{
          std::string_view{"add_typewide_sequence"},
          std::string_view{"remove_typewide_sequence"},
          std::string_view{"uvm_update_sequence_lib"},
          std::string_view{"is_registered_with_sequencer"},
          std::string_view{"m_set_p_sequencer"},
          std::string_view{"type_id"}};
      require(deprecated.ok() &&
                  std::ranges::all_of(deprecated_generated_identifiers,
                                      [&](const auto spelling) {
                                        return deprecated_identifier_count(
                                                   spelling) > 0;
                                      }) &&
                  std::ranges::none_of(deprecated.lexed.tokens,
                                       [](const Token &token) {
                                         return token.text.find('`') !=
                                                std::string::npos;
                                       }),
              "the exact governed UVM 1.2 deprecated sequence and sequencer "
              "registration aliases must expand to their complete generated "
              "method and registry surface");

      auto no_deprecated_options = upstream_options;
      no_deprecated_options.defines = {"UVM_NO_DEPRECATED=1"};
      const auto disabled_deprecated = preprocess_verilog_file(
          deprecated_probe, Language::SystemVerilog2017, no_deprecated_options);
      require(!disabled_deprecated.ok() &&
                  std::ranges::any_of(disabled_deprecated.lexed.diagnostics,
                                      [](const Diagnostic &diagnostic) {
                                        return diagnostic.code ==
                                               "FSIM-SV-PP-028";
                                      }),
              "UVM_NO_DEPRECATED must remove the governed UVM 1.2 registration "
              "aliases with the exact undefined-macro diagnostic");

      const auto invalid_arity_probe = directory / "upstream-bad-arity-0.sv";
      write_text(invalid_arity_probe,
                 "`include \"uvm_macros.svh\"\n"
                 "`uvm_info(\"LEGACY\", \"missing verbosity\")\n");
      const auto invalid_arity = preprocess_verilog_file(
          invalid_arity_probe, Language::SystemVerilog2017, upstream_options);
      require(!invalid_arity.ok() &&
                  std::ranges::any_of(invalid_arity.lexed.diagnostics,
                                      [](const Diagnostic &diagnostic) {
                                        return diagnostic.code ==
                                               "FSIM-SV-PP-030";
                                      }),
              "an exact governed UVM 1.2 report-macro arity error must retain "
              "FSIM-SV-PP-030");

      auto shallow_options = upstream_options;
      shallow_options.maximum_macro_expansion_depth = 1;
      const auto shallow = preprocess_verilog_file(
          upstream_probe, Language::SystemVerilog2017, shallow_options);
      require(!shallow.ok() && std::ranges::any_of(
                                   shallow.lexed.diagnostics,
                                   [](const Diagnostic &diagnostic) {
                                     return diagnostic.code == "FSIM-SV-PP-029";
                                   }),
              "an exact governed UVM 1.2 nested expansion over its configured "
              "limit must retain FSIM-SV-PP-029");
    } else {
      const std::array ieee_2020_names{std::string_view{"uvm_policy"},
                                       std::string_view{"uvm_field_op"},
                                       std::string_view{"uvm_copier"},
                                       std::string_view{"uvm_revision_string"}};
      require(std::ranges::all_of(ieee_2020_names,
                                  [&](const auto spelling) {
                                    return package_identifier_count(spelling) >
                                           0;
                                  }),
              "the exact governed UVM 2020-3.1 package must retain its IEEE "
              "policy, field-operation, copier, and revision names");
      const std::array removed_package_aliases{
          std::string_view{"uvm_test_done"},
          std::string_view{"global_stop_request"},
          std::string_view{"set_global_timeout"},
          std::string_view{"set_global_stop_timeout"}};
      require(std::ranges::none_of(removed_package_aliases,
                                   [&](const auto spelling) {
                                     return package_identifier_count(spelling) >
                                            0;
                                   }),
              "the exact governed UVM 2020-3.1 package must not retain removed "
              "UVM 1.2 test-done, stop-request, or timeout aliases");

      const auto removed_macro_probe =
          directory / "upstream-removed-deprecated-1.sv";
      write_text(removed_macro_probe,
                 "`include \"uvm_macros.svh\"\n"
                 "`uvm_sequencer_utils(fsim_removed_sequencer)\n"
                 "`uvm_sequence_utils(fsim_removed_sequence, "
                 "fsim_removed_sequencer)\n");
      const auto removed_macros = preprocess_verilog_file(
          removed_macro_probe, Language::SystemVerilog2017, upstream_options);
      require(!removed_macros.ok() &&
                  std::ranges::count_if(removed_macros.lexed.diagnostics,
                                        [](const Diagnostic &diagnostic) {
                                          return diagnostic.code ==
                                                 "FSIM-SV-PP-028";
                                        }) == 2,
              "the exact governed UVM 2020-3.1 macros must reject both removed "
              "sequence and sequencer registration aliases");
    }
  }
#endif

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
}

void test_systemverilog_line_directive() {
  const auto remapped =
      preprocess_verilog(SourceText{"physical.sv",
                                    R"(`line 100 "C:\\rtl\\logical.sv" 0
`define LOGICAL_TOKEN remapped_token
`line 200 "C:\\rtl\\logical\"name.sv" 2
`LOGICAL_TOKEN
`__FILE__
`__LINE__
)"},
                         Language::SystemVerilog2017);
  require(remapped.ok(), "valid `line directives must preprocess");
  const auto token_named =
      [&](const std::string_view spelling) -> const Token * {
    const auto found =
        std::ranges::find(remapped.lexed.tokens, spelling, &Token::text);
    return found == remapped.lexed.tokens.end() ? nullptr : &*found;
  };
  const auto *macro_token = token_named("remapped_token");
  const auto *file_token = token_named("\"C:\\\\rtl\\\\logical\\\"name.sv\"");
  const auto *line_token = token_named("202");
  require(macro_token != nullptr &&
              macro_token->span.source_name == R"(C:\rtl\logical"name.sv)" &&
              std::filesystem::path{physical_source(macro_token->span)}
                      .filename() == "physical.sv" &&
              macro_token->span.begin.line == 200,
          "macro invocations use the active logical source and line");
  require(
      !macro_token->expansion_stack.empty() &&
          macro_token->expansion_stack.front().find(
              R"(defined at C:\rtl\logical.sv:100)") != std::string::npos &&
          macro_token->expansion_stack.front().find(
              R"(expanded at C:\rtl\logical"name.sv:200)") != std::string::npos,
      "macro ancestry retains remapped definition and invocation locations");
  require(file_token != nullptr &&
              file_token->span.source_name == R"(C:\rtl\logical"name.sv)" &&
              file_token->span.begin.line == 201,
          "`__FILE__ is safely re-escaped from the logical source name");
  require(line_token != nullptr &&
              line_token->span.source_name == R"(C:\rtl\logical"name.sv)" &&
              line_token->span.begin.line == 202,
          "`__LINE__ uses the active logical line");

  const auto inactive = preprocess_verilog(SourceText{"inactive-line.sv",
                                                      R"(`ifdef NEVER
`line 900 "ignored.sv" 0
`endif
physical_token
)"},
                                           Language::SystemVerilog2017);
  const auto inactive_token =
      std::ranges::find(inactive.lexed.tokens, "physical_token", &Token::text);
  require(inactive.ok() && inactive_token != inactive.lexed.tokens.end() &&
              inactive_token->span.source_name.find("inactive-line.sv") !=
                  std::string::npos &&
              inactive_token->span.begin.line == 4,
          "inactive `line directives do not alter source provenance");

  const auto directory = make_test_directory("verilog-line");
  const auto parent = directory / "parent.sv";
  const auto child = directory / "child.svh";
  write_text(child,
             R"(`line 700 "logical-child.svh" 1
child_token
)");
  write_text(parent,
             R"(`line 50 "logical-parent.sv" 0
parent_before
`include "child.svh"
parent_after
)");
  const auto included =
      preprocess_verilog_file(parent, Language::SystemVerilog2017);
  const auto included_token =
      [&](const std::string_view spelling) -> const Token * {
    const auto found =
        std::ranges::find(included.lexed.tokens, spelling, &Token::text);
    return found == included.lexed.tokens.end() ? nullptr : &*found;
  };
  const auto *before = included_token("parent_before");
  const auto *child_value = included_token("child_token");
  const auto *after = included_token("parent_after");
  require(included.ok() && before != nullptr && child_value != nullptr &&
              after != nullptr &&
              before->span.source_name == "logical-parent.sv" &&
              before->span.begin.line == 50 &&
              child_value->span.source_name == "logical-child.svh" &&
              child_value->span.begin.line == 700 &&
              after->span.source_name == "logical-parent.sv" &&
              after->span.begin.line == 52,
          "include-local `line state does not leak into its parent");

  const auto second = directory / "second.sv";
  write_text(second, "second_root_token\n");
  const auto roots = preprocess_verilog_compilation_unit(
      {parent, second}, Language::SystemVerilog2017);
  const auto second_token =
      std::ranges::find(roots.lexed.tokens, "second_root_token", &Token::text);
  require(roots.ok() && second_token != roots.lexed.tokens.end() &&
              second_token->span.source_name ==
                  std::filesystem::weakly_canonical(second).generic_string() &&
              second_token->span.begin.line == 1,
          "`line state resets at each compilation-unit root");

  const auto malformed = preprocess_verilog(SourceText{"malformed-line.sv",
                                                       R"(`ifdef NEVER
`line 0 broken 9 extra
`endif
`line
`line 0 "zero.sv" 0
`line 4294967296 "overflow.sv" 0
`line 1 not_a_string 0
`line 1 "bad\q.sv" 0
`line 1 "bad-level.sv" 3
)"},
                                            Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        malformed.lexed.diagnostics,
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(!malformed.ok() && has_code("FSIM-SV-PP-039") &&
              has_code("FSIM-SV-PP-040") && has_code("FSIM-SV-PP-041") &&
              has_code("FSIM-SV-PP-042") && has_code("FSIM-SV-PP-043"),
          "malformed `line fields receive targeted diagnostics");
  require(std::ranges::none_of(malformed.lexed.diagnostics,
                               [](const Diagnostic &diagnostic) {
                                 return diagnostic.span.source_name ==
                                        "ignored.sv";
                               }),
          "malformed inactive `line directives are ignored");

  auto parser_error = preprocess_verilog(
      SourceText{"parser-physical.sv",
                 "`line 900 \"parser-logical.sv\" 0\nmodule broken(;\n"},
      Language::SystemVerilog2017);
  const auto parsed_error = parse_verilog(std::move(parser_error.lexed), true);
  require(!parsed_error.ok() &&
              std::ranges::any_of(parsed_error.diagnostics,
                                  [](const Diagnostic &diagnostic) {
                                    return diagnostic.span.source_name ==
                                               "parser-logical.sv" &&
                                           diagnostic.span.begin.line == 900;
                                  }),
          "parser diagnostics retain logical `line provenance");

  const auto lexical_error = preprocess_verilog(
      SourceText{"lexer-physical.sv",
                 "`line 300 \"lexer-logical.sv\" 0\n\"unterminated\n"},
      Language::SystemVerilog2017);
  require(!lexical_error.ok() &&
              std::ranges::any_of(lexical_error.lexed.diagnostics,
                                  [](const Diagnostic &diagnostic) {
                                    return diagnostic.span.source_name ==
                                               "lexer-logical.sv" &&
                                           diagnostic.span.begin.line == 300;
                                  }),
          "lexer diagnostics after `line are remapped");

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
}

void test_vhdl_vertical_slice() {
  constexpr std::string_view source = R"(
library ieee;
use ieee.std_logic_1164.all;
context work.shared;

entity Counter is
  port (
    clk : in std_logic;
    q   : out std_logic_vector(7 downto 0)
  );
end entity Counter;

architecture RTL of Counter is
  signal count : std_logic_vector(7 downto 0);
begin
  q <= count;
  Direct_Child: entity Work.Child(Gates)
    port map (clk, Q => q);
  Component_Child: Child
    port map (clk, Result => count);

  update: process(clk)
  begin
    if rising_edge(clk) then
      count <= count + 1;
    else
      null;
    end if;
  end process update;
end architecture RTL;
)";

  const auto result = parse_text("counter.vhd", source, Language::Vhdl2008);
  require(result.ok(), "VHDL vertical slice must parse without errors");
  require(result.design.units.size() == 2,
          "VHDL entity and architecture must remain distinct units");

  const auto *entity = result.design.find(UnitKind::VhdlEntity, "counter");
  require(entity != nullptr, "VHDL names must be canonicalized");
  require(entity->vhdl_context.size() == 3,
          "VHDL context items must attach to the following unit");
  require(entity->vhdl_context[0].kind == VhdlContextItemKind::LibraryClause &&
              entity->vhdl_context[0].selected_names ==
                  std::vector<std::string>{"ieee"},
          "library clause representation");
  require(entity->vhdl_context[1].kind == VhdlContextItemKind::UseClause &&
              entity->vhdl_context[1].selected_names ==
                  std::vector<std::string>{"ieee.std_logic_1164.all"},
          "use clause representation");
  require(entity->vhdl_context[2].kind ==
                  VhdlContextItemKind::ContextReference &&
              entity->vhdl_context[2].selected_names ==
                  std::vector<std::string>{"work.shared"},
          "context reference representation");
  require(entity->ports.size() == 2, "entity port count");
  require(entity->ports[1].type.width() == 8, "VHDL vector width");
  require(entity->ports[1].type.domain == ValueDomain::Logic9,
          "std_logic_vector must retain nine-state domain");

  const auto *architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture != nullptr, "architecture lookup");
  require(architecture->vhdl_context.empty(),
          "a context clause applies only to its following library unit");
  require(architecture->primary_name == "counter",
          "architecture must identify its entity");
  require(architecture->signals.size() == 1, "architecture signal count");
  require(architecture->concurrent_statements.size() == 1,
          "concurrent assignment count");
  require(architecture->instances.size() == 2, "VHDL instance count");
  const auto &direct_instance = architecture->instances[0];
  require(direct_instance.name == "direct_child",
          "VHDL instance names must be canonicalized");
  require(direct_instance.unit_name == "work.child(gates)",
          "direct entity library, unit, and architecture");
  require(direct_instance.connections.size() == 2,
          "direct entity connection count");
  require(!direct_instance.connections[0].port,
          "VHDL positional actual remains positional");
  require(direct_instance.connections[0].value.kind ==
                  ExpressionKind::Identifier &&
              direct_instance.connections[0].value.text == "clk",
          "VHDL positional actual identifier");
  require(direct_instance.connections[1].port ==
                  std::optional<std::string>{"q"} &&
              direct_instance.connections[1].value.text == "q",
          "VHDL positional-then-named connection");
  require(direct_instance.span.source_name == "counter.vhd" &&
              direct_instance.span.begin.line > 1 &&
              direct_instance.connections[0].span.begin.line > 1,
          "VHDL instance and connection source spans");
  const auto &component_instance = architecture->instances[1];
  require(component_instance.name == "component_child" &&
              component_instance.unit_name == "child",
          "component-style VHDL instance");
  require(component_instance.connections.size() == 2 &&
              component_instance.connections[1].port ==
                  std::optional<std::string>{"result"} &&
              component_instance.connections[1].value.text == "count",
          "component-style named and positional connections");
  require(architecture->processes.size() == 1, "process count");
  const auto &process = architecture->processes.front();
  require(process.name == "update", "process label");
  require(process.sensitivities.size() == 1, "VHDL sensitivity count");
  require(process.sensitivities.front().edge == EdgeKind::Any,
          "an edge guard with an else branch must retain any-edge sensitivity");
  require(process.statements.size() == 1 &&
              process.statements.front().kind == StatementKind::If,
          "VHDL if statement");
  require(process.statements.front().statements.front().assignment_kind ==
              AssignmentKind::VhdlSignal,
          "VHDL sequential signal assignment");
}

void test_vhdl_falling_edge_guard() {
  const auto result = parse_text("falling_edge.vhd",
                                 R"(
entity falling_edge is
  port (clk : in std_logic; hit : out std_logic);
end entity;

architecture rtl of falling_edge is
begin
  capture: process(clk)
  begin
    if falling_edge(clk) then
      hit <= '1';
    end if;
  end process;
end architecture;
)",
                                 Language::Vhdl2008);
  require(result.ok(), "VHDL falling_edge guard must parse");
  const auto *architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture && architecture->processes.size() == 1,
          "VHDL falling_edge process");
  const auto &process = architecture->processes.front();
  require(process.sensitivities.size() == 1 &&
              process.sensitivities.front().signal == "clk" &&
              process.sensitivities.front().edge == EdgeKind::Negative,
          "sole VHDL falling_edge guard must refine static sensitivity");
}

void test_vhdl_instance_diagnostics() {
  constexpr std::string_view source = R"(
entity top is
  port (clk : in std_logic);
end entity;

architecture rtl of top is
  signal data : std_logic_vector(1 downto 0);
begin
  with_generic: entity work.child(rtl)
    generic map (Width => 2)
    port map (clk);
  indexed_actual: child
    port map (input => data(0), output => open);
  qualified_actual: child
    port map (
      input => std_logic_vector'(data),
      output => open);
end architecture;
)";

  const auto result = parse_text("instances.vhd", source, Language::Vhdl2008);
  require(result.ok(), "VHDL indexed and open port actuals must parse");
  const auto *architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture != nullptr && architecture->instances.size() == 3,
          "port associations must retain both instances");
  require(architecture->instances[0].parameter_overrides.size() == 1 &&
              architecture->instances[0].parameter_overrides[0].name ==
                  std::optional<std::string>{"width"} &&
              architecture->instances[0].parameter_overrides[0].value.text ==
                  "2",
          "named generic maps must remain in the instance HIR");
  require(architecture->instances[1].connections.size() == 2 &&
              architecture->instances[1].connections[0].value.kind ==
                  ExpressionKind::Call &&
              architecture->instances[1].connections[1].kind ==
                  PortActualKind::Open,
          "indexed actual and explicit open state retained");
  require(architecture->instances[2].connections.size() == 2 &&
              architecture->instances[2].connections[0].value.kind ==
                  ExpressionKind::Call &&
              architecture->instances[2].connections[0].value.text ==
                  "@vhdl-qualified:std_logic_vector" &&
              architecture->instances[2].connections[0].value.operands.size() ==
                  1 &&
              architecture->instances[2]
                      .connections[0]
                      .value.operands.front()
                      .text == "data",
          "qualified input actual retains its explicit type mark");

  const auto invalid = parse_text("invalid-port-associations.vhd",
                                  R"(
entity invalid_port_top is
end entity;
architecture rtl of invalid_port_top is
  signal data : bit;
begin
  child: entity work.invalid_port_leaf(rtl)
    port map (
      first_value => data,
      first_value => data,
      data);
end architecture;
)",
                                  Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(!invalid.ok() && has_code("FSIM-VHDL-SEM-078") &&
              has_code("FSIM-VHDL-SEM-079"),
          "duplicate and named-then-positional port maps are rejected");
}

void test_vhdl_generics() {
  const auto result = parse_text("generics.vhd",
                                 R"(
entity generic_child is
  generic (
    type Element_T;
    constant Width : in positive := 8;
    Depth : positive := 8;
    Enabled : boolean := true;
    Required : integer
  );
  port (
    data : out bit_vector(Width - 1 downto 0)
  );
end entity;

architecture rtl of generic_child is
  signal local_data : bit_vector(Width - 1 downto 0);
begin
  data <= local_data;
end architecture;

entity generic_top is
end entity;

architecture rtl of generic_top is
  signal named_data : bit_vector(3 downto 0);
  signal mixed_data : bit_vector(1 downto 0);
  signal ranged_data : bit_vector(0 downto 0);
  signal constrained_data : bit_vector(0 downto 0);
begin
  named_child: entity work.generic_child(rtl)
    generic map (
      Element_T => bit,
      Width => 4,
      Required => 2
    )
    port map (data => named_data);
  mixed_child: entity work.generic_child(rtl)
    generic map (
      bit,
      2,
      Enabled => false,
      Required => 1
    )
    port map (data => mixed_data);
  ranged_type_child: entity work.generic_child(rtl)
    generic map (
      integer range 2 to 7,
      Required => 2
    )
    port map (data => ranged_data);
  constrained_type_child: entity work.generic_child(rtl)
    generic map (
      bit_vector(3 downto 0),
      Required => 2
    )
    port map (data => constrained_data);
  open_default_child: entity work.generic_child(rtl)
    generic map (
      Element_T => bit,
      Width => open,
      Enabled => open,
      Required => 2
    )
    port map (data => named_data);
end architecture;
)",
                                 Language::Vhdl2008);
  require(result.ok(), "bounded VHDL generic declarations and maps must parse");
  const auto *child = result.design.find(UnitKind::VhdlEntity, "generic_child");
  require(child != nullptr && child->parameters.size() == 5,
          "VHDL generics are retained in declaration order");
  require(
      child->parameters[0].name == "element_t" &&
          child->parameters[0].kind == ParameterKind::Type &&
          child->parameters[0].default_value.kind == ExpressionKind::Invalid &&
          child->parameters[1].name == "width" &&
          child->parameters[1].object_class == InterfaceObjectClass::Constant &&
          child->parameters[1].direction == PortDirection::Input &&
          child->parameters[1].type.spelling == "positive" &&
          child->parameters[1].default_value.text == "8" &&
          child->parameters[2].name == "depth" &&
          child->parameters[3].type.domain == ValueDomain::Boolean &&
          child->parameters[3].default_value.kind ==
              ExpressionKind::BooleanLiteral &&
          child->parameters[4].default_value.kind == ExpressionKind::Invalid,
      "type formals, typed defaults, and required value generics");
  require(child->ports.size() == 1 && !child->ports[0].type.packed_range &&
              child->ports[0].type.packed_range_expression &&
              child->ports[0].type.packed_range_expression->descending ==
                  std::optional<bool>{true},
          "VHDL symbolic ranges retain explicit direction");
  const auto top_architecture =
      std::find_if(result.design.units.begin(), result.design.units.end(),
                   [](const DesignUnit &unit) {
                     return unit.kind == UnitKind::VhdlArchitecture &&
                            unit.primary_name == "generic_top";
                   });
  require(top_architecture != result.design.units.end() &&
              top_architecture->instances.size() == 5 &&
              top_architecture->instances[0].parameter_overrides.size() == 3 &&
              top_architecture->instances[0].parameter_overrides[0].name ==
                  std::optional<std::string>{"element_t"} &&
              !top_architecture->instances[1].parameter_overrides[0].name &&
              top_architecture->instances[1].parameter_overrides[2].name ==
                  std::optional<std::string>{"enabled"},
          "named and positional-then-named generic maps are represented");
  require(
      top_architecture->instances[4].parameter_overrides.size() == 4 &&
          top_architecture->instances[4].parameter_overrides[1].default_box &&
          top_architecture->instances[4].parameter_overrides[2].default_box,
      "open value-generic actuals retain default selection in HIR");
  const auto &ranged_actual =
      top_architecture->instances[2].parameter_overrides[0];
  require(ranged_actual.type_value &&
              ranged_actual.type_value->spelling == "integer" &&
              ranged_actual.type_value->integer_range_expression &&
              !ranged_actual.type_value->integer_range_expression->descending &&
              ranged_actual.type_value->integer_range_expression->span
                      .source_name == "generics.vhd" &&
              ranged_actual.value.kind == ExpressionKind::Invalid,
          "unambiguous VHDL range subtype actuals retain typed HIR");
  const auto &constrained_actual =
      top_architecture->instances[3].parameter_overrides[0];
  require(!constrained_actual.type_value &&
              constrained_actual.value.kind == ExpressionKind::Slice &&
              constrained_actual.value.text == "downto" &&
              constrained_actual.value.operands.size() == 3 &&
              constrained_actual.value.operands[0].text == "bit_vector",
          "ambiguous parenthesized subtype actuals retain slice HIR for "
          "formal-aware elaboration");

  const auto wide_generic = parse_text(
      "wide-generic.vhd",
      R"(
entity wide_generic is
  generic (Value : bit_vector(256 downto 0));
end entity;
architecture rtl of wide_generic is begin end architecture;
)",
      Language::Vhdl2008);
  require(
      wide_generic.ok()
          && wide_generic.design.units.front().parameters.size() == 1
          && wide_generic.design.units.front().parameters.front().type.width()
              == 257,
      "VHDL packed value generics have no host-word width cap");

  const auto invalid = parse_text("invalid-generics.vhd",
                                  R"(
entity invalid_generic is
  generic (
    type Classified is range <>;
    type Defaulted := integer;
    Clash : integer := 1;
    Clash : integer := 2;
    String_Value : string;
    variable Bad_Class : integer;
    Bad_Mode : out integer
  );
  type Classified is (First, Second);
  port (Clash : in bit);
end entity;
architecture rtl of invalid_generic is
begin
end architecture;
entity invalid_top is
end entity;
architecture rtl of invalid_top is
begin
  bad: entity work.invalid_generic(rtl)
    generic map (
      Clash => 1,
      Clash => 2,
      3
    )
    port map ();
end architecture;
)",
                                  Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(), invalid.diagnostics.end(),
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(!invalid.ok() && has_code("FSIM-VHDL-SEM-013") &&
              has_code("FSIM-VHDL-SEM-014") && has_code("FSIM-VHDL-SEM-015") &&
              has_code("FSIM-VHDL-SEM-016") && has_code("FSIM-VHDL-SEM-036") &&
              has_code("FSIM-VHDL-SEM-076") && has_code("FSIM-VHDL-SEM-077") &&
              has_code("FSIM-VHDL-UNSUPPORTED-018") &&
              has_code("FSIM-VHDL-UNSUPPORTED-028"),
          "VHDL generic diagnostics are stable and targeted");
}

void test_vhdl_record_types() {
  const auto parsed = parse_text("record_types.vhd",
                                 R"(
entity record_types is
end entity;

architecture rtl of record_types is
  type Packet_T is record
    Data, Shadow : std_logic_vector(7 downto 0);
    Valid : boolean;
    Parity : bit;
  end record packet_t;
  signal source, result : PACKET_T;
begin
  copy_fields : process
    variable Local_Value : packet_t;
  begin
    local_value.data := source.DATA;
    local_value.shadow(3 downto 0) :=
      source.shadow(7 downto 4);
    result.data <= local_value.data;
    result.valid <= local_value.valid;
    wait;
  end process;
end architecture;
)",
                                 Language::Vhdl2008);
  require(parsed.ok(), "bounded VHDL record declarations must parse");
  require(parsed.design.units.size() == 2,
          "record fixture has an entity and architecture");
  const auto &architecture = parsed.design.units.back();
  require(architecture.type_aliases.size() == 1,
          "architecture retains its record type declaration");
  const auto &alias = architecture.type_aliases.front();
  require(alias.name == "packet_t" &&
              alias.type.packed_aggregate == PackedAggregateKind::Struct &&
              alias.type.domain == ValueDomain::Logic9 &&
              alias.type.width() == 18 && alias.type.packed_members.size() == 4,
          "record type retains flattened width, domain, and elements");
  require(alias.type.packed_members[0].name == "data" &&
              alias.type.packed_members[0].lsb_offset == 10 &&
              alias.type.packed_members[1].name == "shadow" &&
              alias.type.packed_members[1].lsb_offset == 2 &&
              alias.type.packed_members[2].name == "valid" &&
              alias.type.packed_members[2].lsb_offset == 1 &&
              alias.type.packed_members[3].name == "parity" &&
              alias.type.packed_members[3].lsb_offset == 0,
          "record elements use declaration-order flattened offsets");
  require(
      architecture.signals.size() == 2 &&
          architecture.signals[0].type.named_type == "packet_t" &&
          architecture.processes.size() == 1 &&
          architecture.processes.front().variables.size() == 1 &&
          architecture.processes.front().variables.front().type.named_type ==
              "packet_t",
      "record-typed signals and variables retain the local named type");
  const auto &statements = architecture.processes.front().statements;
  require(statements.size() == 5 &&
              statements[0].target.text == "local_value.data" &&
              statements[0].value.text == "source.data" &&
              statements[1].target.kind == ExpressionKind::Slice &&
              statements[1].target.operands.front().text ==
                  "local_value.shadow" &&
              statements[1].value.kind == ExpressionKind::Slice &&
              statements[1].value.operands.front().text == "source.shadow" &&
              statements[2].target.text == "result.data" &&
              statements[3].target.text == "result.valid",
          "record element selections are canonical in reads and writes");

  const auto packaged = parse_text("package_records.vhd",
                                   R"(
package Packet_Types is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
end package;

use work.packet_types.all;
entity Packet_Endpoint is
  port (
    Source : in packet_t;
    Result : out work.packet_types.packet_t
  );
end entity;

use work.packet_types.all;
architecture rtl of packet_endpoint is
  signal Direct_Value : packet_types.packet_t;
begin
  copy_value : process(source)
    variable Local_Value : PACKET_T;
  begin
    local_value := source;
    direct_value <= local_value;
    result <= direct_value;
  end process;
end architecture;
)",
                                   Language::Vhdl2008);
  require(packaged.ok(),
          "package records and consumer named-type references must parse");
  require(packaged.design.units.size() == 3 &&
              packaged.design.units[0].kind == UnitKind::VhdlPackage &&
              packaged.design.units[0].type_aliases.size() == 1 &&
              packaged.design.units[0].type_aliases.front().name ==
                  "packet_t" &&
              packaged.design.units[0].type_aliases.front().type.width() == 5,
          "a package retains its bounded record declaration");
  const auto &packaged_entity = packaged.design.units[1];
  const auto &packaged_architecture = packaged.design.units[2];
  require(
      packaged_entity.ports.size() == 2 &&
          packaged_entity.ports[0].type.named_type == "packet_t" &&
          packaged_entity.ports[1].type.named_type ==
              "work.packet_types.packet_t" &&
          packaged_architecture.signals.size() == 1 &&
          packaged_architecture.signals[0].type.named_type ==
              "packet_types.packet_t" &&
          packaged_architecture.processes[0].variables[0].type.named_type ==
              "packet_t",
      "package record references retain bare and selected names for semantics");

  const auto aggregates = parse_text("record_aggregates.vhd",
                                     R"(
entity record_aggregates is
end entity;

architecture rtl of record_aggregates is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal Result : packet_t;
  signal Grouped : boolean;
begin
  aggregate_forms : process
    variable Named_Value : packet_t :=
      (Valid => true, Data => "ULH-");
    variable Positional_Value : packet_t :=
      ("10Z-", false);
  begin
    named_value := (Data => "01LH", others => false);
    positional_value := ("ZZZZ", true);
    result <= (Data => "1010", Valid => true);
    grouped <= (true);
    wait;
  end process;
end architecture;
)",
                                     Language::Vhdl2008);
  require(aggregates.ok(),
          "VHDL positional, named, and others record aggregates must parse");
  const auto &aggregate_process =
      aggregates.design.units.back().processes.front();
  require(aggregate_process.variables.size() == 2 &&
              aggregate_process.variables[0].initializer &&
              aggregate_process.variables[1].initializer,
          "record aggregate variable initializers are retained");
  const auto &named_initializer = *aggregate_process.variables[0].initializer;
  const auto &positional_initializer =
      *aggregate_process.variables[1].initializer;
  require(named_initializer.kind == ExpressionKind::Aggregate &&
              named_initializer.operands.size() == 2 &&
              named_initializer.aggregate_choices ==
                  std::vector<std::string>({"valid", "data"}) &&
              named_initializer.operands[0].kind ==
                  ExpressionKind::BooleanLiteral &&
              named_initializer.operands[1].kind ==
                  ExpressionKind::StringLiteral,
          "named record aggregate associations preserve canonical choices and "
          "source order");
  require(positional_initializer.kind == ExpressionKind::Aggregate &&
              positional_initializer.operands.size() == 2 &&
              positional_initializer.aggregate_choices ==
                  std::vector<std::string>({"", ""}),
          "positional record aggregate associations remain distinct from named "
          "choices");
  const auto &aggregate_statements = aggregate_process.statements;
  require(
      aggregate_statements.size() == 5 &&
          aggregate_statements[0].value.kind == ExpressionKind::Aggregate &&
          aggregate_statements[0].value.aggregate_choices ==
              std::vector<std::string>({"data", "others"}) &&
          aggregate_statements[1].value.kind == ExpressionKind::Aggregate &&
          aggregate_statements[2].value.kind == ExpressionKind::Aggregate &&
          aggregate_statements[3].value.kind == ExpressionKind::BooleanLiteral,
      "aggregate assignments retain choices while single parentheses remain "
      "ordinary grouping");

  const auto invalid_aggregates = parse_text("invalid_record_aggregates.vhd",
                                             R"(
entity invalid_record_aggregates is
end entity;
architecture rtl of invalid_record_aggregates is
  type Packet_T is record
    Data : bit_vector(1 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal Result : packet_t;
begin
  invalid_forms : process
  begin
    result <= (Data => "00", false);
    result <= (others => false, Data => "00");
    result <= (others => false, others => true);
    result <= (true => false, Data => "00");
    result <= (Data => , Valid => true);
    wait;
  end process;
end architecture;
)",
                                             Language::Vhdl2008);
  const auto has_aggregate_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid_aggregates.diagnostics,
        [&](const auto &diagnostic) { return diagnostic.code == code; });
  };
  require(!invalid_aggregates.ok() &&
              has_aggregate_code("FSIM-VHDL-PARSE-132") &&
              has_aggregate_code("FSIM-VHDL-PARSE-133") &&
              has_aggregate_code("FSIM-VHDL-SEM-038") &&
              has_aggregate_code("FSIM-VHDL-SEM-039"),
          "record aggregate association syntax failures have stable targeted "
          "diagnostics");

  const auto invalid = parse_text("invalid_records.vhd",
                                  R"(
entity invalid_records is
end entity;
architecture rtl of invalid_records is
  type empty_t is record
  end record empty_t;
  type duplicate_t is record
    Item, ITEM : bit;
  end record wrong_name;
  type duplicate_t is record
    count : integer;
    nested : empty_t;
  end record duplicate_t;
begin
end architecture;
)",
                                  Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const auto &diagnostic) { return diagnostic.code == code; });
  };
  require(has_code("FSIM-VHDL-PARSE-130") && has_code("FSIM-VHDL-SEM-035") &&
              has_code("FSIM-VHDL-SEM-036") && has_code("FSIM-VHDL-SEM-037"),
          "record declaration failures have targeted stable diagnostics");
}

void test_vhdl_select_and_concatenation_expressions() {
  const auto result = parse_text("select_concat.vhd",
                                 R"(
entity select_concat is
  port (
    descending : in std_logic_vector(7 downto 4);
    ascending : in std_logic_vector(2 to 5);
    selected : out std_logic;
    part : out std_logic_vector(1 downto 0);
    joined : out std_logic_vector(5 downto 0)
  );
end entity;

architecture rtl of select_concat is
begin
  observe: process(descending, ascending)
  begin
    selected <= descending(5);
    part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
    descending(4) <= selected;
    ascending(4 to 5) <= part;
  end process;
end architecture;
)",
                                 Language::Vhdl2008);
  require(result.ok(),
          "VHDL indexed, slice, and concatenation expressions must parse");
  const auto *entity =
      result.design.find(UnitKind::VhdlEntity, "select_concat");
  require(entity != nullptr && entity->ports[0].type.packed_range &&
              entity->ports[0].type.packed_range->descending &&
              entity->ports[1].type.packed_range &&
              !entity->ports[1].type.packed_range->descending,
          "VHDL port range directions");
  const auto *architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture != nullptr && architecture->processes.size() == 1 &&
              architecture->processes.front().statements.size() == 5,
          "VHDL select/concatenation process");
  const auto &statements = architecture->processes.front().statements;
  require(
      statements[0].value.kind == ExpressionKind::Call &&
          statements[0].value.text == "descending" &&
          statements[0].value.operands.size() == 1,
      "ambiguous VHDL indexed-name syntax retained for semantic resolution");
  require(statements[1].value.kind == ExpressionKind::Slice &&
              statements[1].value.text == "to" &&
              statements[1].value.operands.size() == 3,
          "VHDL ascending slice expression");
  require(statements[2].value.kind == ExpressionKind::Binary &&
              statements[2].value.text == "&" &&
              statements[2].value.operands[0].kind == ExpressionKind::Binary,
          "left-associated VHDL concatenation expression");
  require(statements[3].target.kind == ExpressionKind::Index &&
              statements[3].target.operands.size() == 2 &&
              statements[3].target.operands[1].text == "4" &&
              statements[4].target.kind == ExpressionKind::Slice &&
              statements[4].target.text == "to",
          "VHDL selected assignment target nodes");
}

void test_signed_type_and_expression_nodes() {
  const auto vhdl = parse_text("signed_ops.vhd",
                               R"(
entity signed_ops is
  port (
    lhs : in signed(7 downto 0);
    rhs : in signed(7 downto 0);
    quotient : out signed(7 downto 0);
    remainder : out signed(7 downto 0);
    modulo : out signed(7 downto 0);
    shifted_left : out signed(7 downto 0);
    shifted_right : out signed(7 downto 0);
    shifted_arithmetic : out signed(7 downto 0);
    shifted_arithmetic_left : out signed(7 downto 0);
    rotated_left : out signed(7 downto 0);
    rotated_right : out signed(7 downto 0);
    absolute : out signed(7 downto 0)
  );
end entity;
architecture rtl of signed_ops is
begin
  quotient <= lhs / rhs;
  remainder <= lhs rem rhs;
  modulo <= lhs mod rhs;
  shifted_left <= lhs sll 1;
  shifted_right <= lhs srl 1;
  shifted_arithmetic <= lhs sra 1;
  shifted_arithmetic_left <= lhs sla 1;
  rotated_left <= lhs rol 1;
  rotated_right <= lhs ror 1;
  absolute <= abs lhs;
end architecture;
)",
                               Language::Vhdl2008);
  require(vhdl.ok(), "VHDL signed arithmetic source must parse");
  const auto *entity = vhdl.design.find(UnitKind::VhdlEntity, "signed_ops");
  const auto *architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(entity != nullptr && entity->ports.size() == 12 &&
              std::ranges::all_of(entity->ports,
                                  [](const SignalDeclaration &port) {
                                    return port.type.is_signed;
                                  }),
          "VHDL signed subtype metadata");
  require(
      architecture != nullptr &&
          architecture->concurrent_statements.size() == 10 &&
          architecture->concurrent_statements[0].value.text == "/" &&
          architecture->concurrent_statements[1].value.text == "rem" &&
          architecture->concurrent_statements[2].value.text == "mod" &&
          architecture->concurrent_statements[3].value.text == "sll" &&
          architecture->concurrent_statements[4].value.text == "srl" &&
          architecture->concurrent_statements[5].value.text == "sra" &&
          architecture->concurrent_statements[6].value.text == "sla" &&
          architecture->concurrent_statements[7].value.text == "rol" &&
          architecture->concurrent_statements[8].value.text == "ror" &&
          architecture->concurrent_statements[9].value.kind ==
              ExpressionKind::Unary &&
          architecture->concurrent_statements[9].value.text == "abs",
      "VHDL signed arithmetic, shift, rotate, and absolute expression nodes");

  const auto systemverilog = parse_text("signed_ops.sv",
                                        R"(
module signed_ops;
  logic signed [7:0] lhs;
  logic signed [7:0] rhs;
  logic unsigned [7:0] mixed;
  logic signed [7:0] quotient;
  logic comparison;
  logic cast_comparison;
  logic [7:0] cast_shift;
  logic unknown_present;
  logic [31:0] packed_width;
  logic signed [31:0] left_bound;
  logic signed [31:0] right_bound;
  logic signed [31:0] low_bound;
  logic signed [31:0] high_bound;
  logic signed [31:0] packed_size;
  logic signed [31:0] packed_increment;
  logic signed [31:0] dimensions;
  logic signed [31:0] unpacked_dimensions;
  logic one_hot;
  logic one_hot_or_zero;
  logic signed [31:0] one_count;
  logic signed [31:0] selected_count;
  always_comb begin
    quotient = lhs / rhs;
    comparison = lhs < rhs;
    cast_comparison = $signed(mixed) < rhs;
    cast_shift = $unsigned(lhs) >>> 1;
    unknown_present = $isunknown(mixed);
    packed_width = $bits({lhs, mixed});
    left_bound = $left(mixed);
    right_bound = $right(mixed);
    low_bound = $low(mixed);
    high_bound = $high(mixed);
    packed_size = $size(mixed, 1);
    packed_increment = $increment(mixed);
    dimensions = $dimensions(mixed);
    unpacked_dimensions = $unpacked_dimensions(mixed);
    one_hot = $onehot(mixed);
    one_hot_or_zero = $onehot0(mixed);
    one_count = $countones(mixed);
    selected_count = $countbits(mixed, 1'b0, 1'bx);
  end
endmodule
)",
                                        Language::SystemVerilog2017);
  require(systemverilog.ok(),
          "SystemVerilog signed arithmetic source must parse");
  const auto &unit = systemverilog.design.units.front();
  require(unit.signals.size() == 21 && unit.signals[0].type.is_signed &&
              unit.signals[1].type.is_signed && !unit.signals[2].type.is_signed,
          "SystemVerilog explicit signedness metadata");
  require(
      unit.processes.size() == 1 &&
          unit.processes.front().statements.size() == 18 &&
          unit.processes.front().statements[0].value.text == "/" &&
          unit.processes.front().statements[1].value.text == "<" &&
          unit.processes.front().statements[2].value.text == "<" &&
          unit.processes.front().statements[2].value.operands.front().kind ==
              ExpressionKind::Call &&
          unit.processes.front().statements[2].value.operands.front().text ==
              "$signed" &&
          unit.processes.front().statements[3].value.text == ">>>" &&
          unit.processes.front().statements[3].value.operands.front().text ==
              "$unsigned" &&
          unit.processes.front().statements[4].value.kind ==
              ExpressionKind::Call &&
          unit.processes.front().statements[4].value.text == "$isunknown" &&
          unit.processes.front().statements[5].value.kind ==
              ExpressionKind::Call &&
          unit.processes.front().statements[5].value.text == "$bits" &&
          unit.processes.front().statements[5].value.operands.front().kind ==
              ExpressionKind::Concatenation &&
          unit.processes.front().statements[6].value.text == "$left" &&
          unit.processes.front().statements[7].value.text == "$right" &&
          unit.processes.front().statements[8].value.text == "$low" &&
          unit.processes.front().statements[9].value.text == "$high" &&
          unit.processes.front().statements[10].value.text == "$size" &&
          unit.processes.front().statements[10].value.operands.size() == 2 &&
          unit.processes.front().statements[11].value.text == "$increment" &&
          unit.processes.front().statements[12].value.text == "$dimensions" &&
          unit.processes.front().statements[13].value.text ==
              "$unpacked_dimensions" &&
          unit.processes.front().statements[14].value.text == "$onehot" &&
          unit.processes.front().statements[15].value.text == "$onehot0" &&
          unit.processes.front().statements[16].value.text == "$countones" &&
          unit.processes.front().statements[17].value.text == "$countbits" &&
          unit.processes.front().statements[17].value.operands.size() == 3,
      "SystemVerilog signed arithmetic, casts, and system-function nodes");
}

} // namespace fsim::tests::frontend
