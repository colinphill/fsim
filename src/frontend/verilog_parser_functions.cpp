// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace fsim::frontend {

FunctionDeclaration VerilogParser::parse_function(
    const Token& start,
    const bool prototype,
    const bool default_automatic) {
  FunctionDeclaration function;
  function.language = language_;
  if (match_keyword("automatic")) {
    (void)require_standard(
        "an automatic function",
        StandardRevision::Verilog2001,
        previous(),
        "FSIM-SV-PARSE-347");
    function.automatic = true;
    function.lifetime_explicit = true;
  } else if (match_keyword("static")) {
    (void)require_standard(
        "an explicitly static function",
        StandardRevision::SystemVerilog2005,
        previous(),
        "FSIM-SV-PARSE-347");
    function.lifetime_explicit = true;
  } else {
    function.automatic = default_automatic || unit_default_automatic_;
    function.lifetime_explicit = false;
  }
  const LifetimeScope lifetime_scope { automatic_callable_, function.automatic };

  // A user-defined return type is followed by the function name and then
  // '('. The general declaration lookahead deliberately treats that shape
  // as a possible instance, but inside a function header it is unambiguous.
  const bool constructor = keyword("new");
  bool qualified_constructor = false;
  std::size_t qualified_constructor_offset = 0;
  while (at(TokenKind::Identifier, qualified_constructor_offset)
         && at(TokenKind::Scope, qualified_constructor_offset + 1U)) {
    if (keyword("new", qualified_constructor_offset + 2U)) {
      qualified_constructor = true;
      break;
    }
    if (!at(TokenKind::Identifier, qualified_constructor_offset + 2U)) {
      break;
    }
    qualified_constructor_offset += 2U;
  }
  const bool void_result = keyword("void");
  const bool builtin_return_type =
      keyword("string") || keyword("byte")
      || keyword("shortint") || keyword("longint")
      || keyword("shortreal") || keyword("real")
      || keyword("realtime") || keyword("chandle")
      || keyword("process")
      || keyword("time") || keyword("integer") || keyword("int")
      || keyword("logic") || keyword("reg") || keyword("bit")
      || keyword("signed") || keyword("unsigned")
      || at(TokenKind::LeftBracket);
  if (constructor) {
    const auto name = advance();
    function.name = name.text;
    function.return_type.spelling = "constructor";
  } else if (qualified_constructor) {
    function.return_type.spelling = "constructor";
    function.name = expect_identifier("constructor owner").text;
    while (match(TokenKind::Scope)) {
      function.name += "::";
      if (keyword("new")) {
        function.name += advance().text;
        break;
      }
      function.name +=
          expect_identifier("selected constructor owner").text;
    }
  } else {
    if (void_result) {
      const auto result = advance();
      function.return_type.spelling = result.text;
      function.return_type.named_type_span = result.span;
    } else if (keyword("enum")) {
      // An anonymous enumeration return type declares its literals in the
      // enclosing scope (IEEE 1800-2017 6.19, 13.4).
      std::vector<EnumLiteralDeclaration> literals;
      function.return_type = parse_systemverilog_enum_type(&literals);
      member_enumerations_.emplace_back(
          function.return_type, std::move(literals));
    } else {
      function.return_type =
          !builtin_return_type && at(TokenKind::Identifier)
              && at(TokenKind::Identifier, 1)
              ? parse_named_type()
              : parse_parameter_type();
      // A function without a return type returns a 1-bit logic value
      // (IEEE 1800-2017 13.4), not a parameter's implicit integer.
      if (function.return_type.spelling == "implicit"
          && !function.return_type.packed_range
          && !function.return_type.packed_range_expression) {
        function.return_type.spelling = "logic";
        function.return_type.domain = ValueDomain::Logic4;
        function.return_type.is_signed = false;
      }
    }
    const auto name = expect_identifier("function name");
    function.name = name.text;
    while (match(TokenKind::Scope)) {
      function.name += "::";
      function.name += expect_identifier("selected function name").text;
    }
  }
  const ImplicitNetDiscard method_scope { implicit_net_references_,
      implicit_net_references_.size(),
      function.name.find("::") != std::string::npos };
  (void)parse_optional_container_dimension(
      function.return_type);

  auto saved_names = std::move(current_procedural_names_);
  auto saved_types = std::move(current_procedural_types_);
  auto saved_arguments = std::move(current_function_arguments_);
  auto saved_function_name = std::move(current_function_name_);
  const bool saved_in_function = in_function_;
  const bool saved_function_returns_void = current_function_returns_void_;
  current_procedural_names_.clear();
  current_procedural_types_.clear();
  current_function_arguments_.clear();
  current_function_name_ = function.name;
  in_function_ = true;
  current_function_returns_void_ = void_result;
  current_procedural_names_.insert(function.name);

  std::vector<std::string> classic_header_arguments;
  if (match(TokenKind::LeftParen)) {
    const auto header_open = previous();
    const bool classic_header =
        at(TokenKind::Identifier)
        && (at(TokenKind::Comma, 1)
            || at(TokenKind::RightParen, 1));
    if (classic_header) {
      std::unordered_set<std::string> names;
      do {
        const auto argument_name =
            expect_identifier("classic function argument name");
        if (!names.insert(argument_name.text).second
            || argument_name.text == function.name) {
          error(
              argument_name,
              "FSIM-SV-SEM-058",
              "duplicate or conflicting function argument '"
                  + argument_name.text + "'");
        }
        classic_header_arguments.push_back(argument_name.text);
        if (language_ == Language::SystemVerilog2017) {
          // Without a body declaration the name is an ANSI formal of
          // implicit type `input logic` (IEEE 1800-2017 13.3, 13.4).
          current_procedural_names_.insert(argument_name.text);
        }
      } while (match(TokenKind::Comma));
    } else {
    (void)require_standard(
        "an ANSI function header",
        StandardRevision::Verilog2001,
        header_open,
        "FSIM-SV-PARSE-347");
    for (auto& formal : parse_ansi_callable_formals(
             "function", [&](const Token& argument_name) {
               if (!current_function_arguments_.insert(
                       argument_name.text).second
                   || argument_name.text == function.name) {
                 error(
                     argument_name,
                     "FSIM-SV-SEM-058",
                     "duplicate or conflicting function argument '"
                         + argument_name.text + "'");
               }
               current_procedural_names_.insert(argument_name.text);
             })) {
      auto& argument = function.arguments.emplace_back(FunctionArgument{
          formal.name.text,
          std::move(formal.type),
          formal.direction,
          formal.name.span});
      argument.reference = formal.reference;
      argument.const_reference = formal.const_reference;
      argument.static_reference = formal.static_reference;
      argument.default_value = std::move(formal.default_value);
    }
    }
    expect(
        TokenKind::RightParen,
        "')' after function arguments",
        "FSIM-SV-PARSE-140");
  }
  expect(
      TokenKind::Semicolon,
      "';' after function header",
      "FSIM-SV-PARSE-141");

  if (prototype) {
    function.defined = false;
    function.span = span_from(start, previous());
    current_procedural_names_ = std::move(saved_names);
    current_procedural_types_ = std::move(saved_types);
    current_function_arguments_ = std::move(saved_arguments);
    current_function_name_ = std::move(saved_function_name);
    in_function_ = saved_in_function;
    current_function_returns_void_ = saved_function_returns_void;
    return function;
  }

  Statement body;
  body.kind = StatementKind::Block;
  DesignUnit local_declarations;
  while (!at_end() && !keyword("endfunction")) {
    const auto before = position();
    if (parse_local_type_parameter(local_declarations)) {
    } else if (match_keyword("typedef")) {
      const auto alias_count = local_declarations.type_aliases.size();
      parse_typedef(local_declarations, previous());
      if (local_declarations.type_aliases.size() > alias_count) {
        const auto& alias = local_declarations.type_aliases.back();
        current_procedural_types_.insert_or_assign(alias.name, alias.type);
      }
    } else if (is_direction_keyword() || keyword("ref")
               || keyword("const")) {
      const auto declaration_start = current();
      const bool const_reference = match_keyword("const");
      if (const_reference && !keyword("ref")) {
        error(
            declaration_start,
            "FSIM-SV-SEM-248",
            "const is permitted only before a ref formal");
      }
      const auto direction_token = advance();
      const bool reference = direction_token.text == "ref";
      const bool static_reference = reference && match_keyword("static");
      if (static_reference) {
        (void)require_standard(
            "a ref static function formal",
            StandardRevision::SystemVerilog2023,
            previous(),
            "FSIM-SV-PARSE-368");
      }
      const auto direction = reference
          ? const_reference ? PortDirection::Input : PortDirection::Inout
          : direction_token.text == "output"
              ? PortDirection::Output
          : direction_token.text == "inout"
              ? PortDirection::Inout
              : PortDirection::Input;
      Type type;
      if (at(TokenKind::Identifier)
          && (at(TokenKind::Comma, 1)
              || at(TokenKind::Semicolon, 1)
              || at(TokenKind::Assign, 1))) {
        type = Type{ValueDomain::Integer, "implicit", std::nullopt, true};
      } else {
        type = parse_parameter_type();
      }
      do {
        const auto argument_name =
            expect_identifier("classic function argument name");
        if (!current_function_arguments_.insert(
                argument_name.text).second
            || argument_name.text == function.name) {
          error(
              argument_name,
              "FSIM-SV-SEM-058",
              "duplicate or conflicting function argument '"
                  + argument_name.text + "'");
        }
        current_procedural_names_.insert(argument_name.text);
        auto argument_type = type;
        (void)parse_optional_container_dimension(argument_type);
        current_procedural_types_.insert_or_assign(
            argument_name.text, argument_type);
        function.arguments.push_back(FunctionArgument{
            argument_name.text,
            std::move(argument_type),
            direction,
            argument_name.span});
        function.arguments.back().reference = reference;
        function.arguments.back().const_reference = const_reference;
        function.arguments.back().static_reference = static_reference;
        if (match(TokenKind::Assign)) {
          function.arguments.back().default_value = parse_expression();
        }
      } while (match(TokenKind::Comma));
      expect(
          TokenKind::Semicolon,
          "';' after classic function argument declaration",
          "FSIM-SV-PARSE-197");
    } else if (is_block_declaration_start() || keyword("parameter")
        || keyword("localparam")) {
      parse_procedural_declaration(body);
    } else if (auto statement = parse_statement()) {
      body.statements.push_back(std::move(*statement));
    }
    if (position() == before) {
      advance();
    }
  }
  expect_keyword(
      "endfunction", false, "FSIM-SV-PARSE-142");
  if (match(TokenKind::Colon)) {
    const auto end_name = (constructor || qualified_constructor)
            && keyword("new")
        ? advance()
        : expect_identifier("function name after endfunction");
    const auto separator = function.name.rfind("::");
    const auto expected_name = separator == std::string::npos
        ? std::string_view{function.name}
        : std::string_view{function.name}.substr(separator + 2U);
    if (end_name.text != expected_name) {
      error(
          end_name,
          "FSIM-SV-SEM-059",
          "function end name does not match '"
              + function.name + "'");
    }
  }

  function.type_aliases = std::move(local_declarations.type_aliases);
  function.variables = std::move(body.declarations);
  function.statements = std::move(body.statements);
  // A classic formal declared without a type takes the type of a later
  // variable declaration of the same name, as `input x; real x;`
  // (IEEE 1364-2005 10.3.1 port declarations with a data type).
  std::erase_if(function.variables, [&](const VariableDeclaration& variable) {
    const auto argument = std::ranges::find(
        function.arguments, variable.name, &FunctionArgument::name);
    // `input [7:0] x; reg [7:0] x;` completes the same formal when the
    // declared ranges agree (IEEE 1364-2005 12.3.3).
    const bool same_range = argument != function.arguments.end()
        && argument->type.packed_range && variable.type.packed_range
        && argument->type.packed_range->width()
            == variable.type.packed_range->width()
        && (variable.type.spelling == "reg"
            || variable.type.spelling == "logic");
    if (argument == function.arguments.end()
        || (argument->type.spelling != "implicit" && !same_range)
        || variable.initializer) {
      return false;
    }
    argument->type = variable.type;
    return true;
  });
  // A port declaration without a data type that no body declaration
  // completes is a one-bit logic (IEEE 1364-2005 10.3.1, IEEE 1800-2017
  // 13.3).
  for (auto& argument : function.arguments) {
    if (argument.type.spelling == "implicit") {
      argument.type = default_verilog_type();
      argument.type.spelling = "logic";
    }
  }
  if (!classic_header_arguments.empty()) {
    std::vector<FunctionArgument> ordered;
    ordered.reserve(classic_header_arguments.size());
    std::size_t declared_in_body { };
    for (const auto& argument_name : classic_header_arguments) {
      const auto found = std::ranges::find(
          function.arguments,
          argument_name,
          &FunctionArgument::name);
      if (found == function.arguments.end()
          && language_ == Language::SystemVerilog2017) {
        auto type = default_verilog_type();
        type.spelling = "logic";
        ordered.push_back(FunctionArgument{
            argument_name, std::move(type), PortDirection::Input,
            function.span});
      } else if (found == function.arguments.end()) {
        error(
            start,
            "FSIM-SV-SEM-058",
            "classic function argument '" + argument_name
                + "' has no body declaration");
      } else {
        ordered.push_back(std::move(*found));
        ++declared_in_body;
      }
    }
    if (declared_in_body != function.arguments.size()) {
      error(
          start,
          "FSIM-SV-SEM-058",
          "classic function body declares an argument absent from its "
          "header list");
    }
    function.arguments = std::move(ordered);
  }
  function.span = span_from(start, previous());
  // IEEE 1364-2005 10.4.1: a Verilog function declares at least one input.
  // SystemVerilog lifts the restriction (IEEE 1800-2017 13.4).
  if (language_ == Language::Verilog2005
      && std::ranges::none_of(function.arguments,
          [](const FunctionArgument& argument) {
            return argument.direction == PortDirection::Input;
          })) {
    error(start, "FSIM-SV-SEM-422",
        "a Verilog function must declare at least one input");
  }
  if (!constructor && !qualified_constructor
      && function.return_type.spelling != "void") {
    validate_function_body(function, start);
  }

  current_procedural_names_ = std::move(saved_names);
  current_procedural_types_ = std::move(saved_types);
  current_function_arguments_ = std::move(saved_arguments);
  current_function_name_ = std::move(saved_function_name);
  in_function_ = saved_in_function;
  current_function_returns_void_ = saved_function_returns_void;
  return function;
}

void VerilogParser::validate_function_body(
    const FunctionDeclaration& function,
    const Token& start) {
  const auto result_separator = function.name.rfind("::");
  const auto result_name = result_separator == std::string::npos
      ? function.name
      : function.name.substr(result_separator + 2U);
  std::unordered_set<std::string> locals;
  std::unordered_set<std::string> const_references;
  locals.insert(result_name);
  for (const auto& argument : function.arguments) {
    locals.insert(argument.name);
    if (argument.const_reference) {
      const_references.insert(argument.name);
    }
    if (argument.default_value
        && (argument.direction != PortDirection::Input
            || argument.reference)) {
      error(
          start,
          "FSIM-SV-SEM-095",
          "default function arguments require input value formals");
    }
    if (argument.reference && !function.automatic) {
      error(
          start,
          "FSIM-SV-SEM-096",
          "ref function arguments require automatic lifetime");
    }
  }

  const auto collect_declarations =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          for (const auto& declaration : statement.declarations) {
            locals.insert(declaration.name);
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  for (const auto& variable : function.variables) {
    if (!locals.insert(variable.name).second) {
      error(
          start,
          "FSIM-SV-SEM-060",
          "duplicate or conflicting function local '"
              + variable.name + "'");
    }
  }
  collect_declarations(
      collect_declarations, function.statements);
  const auto inspect =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          if (statement.kind == StatementKind::Assignment) {
            // Every leaf of a concatenation or streaming target
            // (`{hi, lo} = v;`, `{<<8{b}} = v;`) is a variable lvalue (IEEE
            // 1800-2017 10.4.1, 11.4.14.3). A stream's first operand is its
            // slice size.
            const auto stream = [](const Expression& expression) {
              return expression.kind == ExpressionKind::Call
                  && (expression.text == "@stream-left"
                      || expression.text == "@stream-right");
            };
            std::vector<const Expression*> leaves { &statement.target };
            for (std::size_t index = 0; index < leaves.size(); ++index) {
              const auto& composite = *leaves[index];
              if (composite.kind == ExpressionKind::Concatenation
                  || stream(composite)) {
                for (std::size_t operand = stream(composite) ? 1U : 0U;
                     operand < composite.operands.size(); ++operand) {
                  leaves.push_back(&composite.operands[operand]);
                }
              }
            }
            for (const auto* leaf : leaves) {
            if (leaf->kind == ExpressionKind::Concatenation
                || stream(*leaf)) {
              continue;
            }
            const Expression* root = leaf;
            while ((root->kind == ExpressionKind::Index
                    || root->kind == ExpressionKind::Slice)
                   && !root->operands.empty()) {
              root = &root->operands.front();
            }
            const bool selected_member_root =
                root->kind == ExpressionKind::Call
                && root->text.starts_with("@sv-select:")
                && root->operands.size() == 1U;
            if (root->kind != ExpressionKind::Identifier
                && !selected_member_root) {
              error(
                  start,
                  "FSIM-SV-SEM-061",
                  "a function assignment target must have an identifier "
                  "root");
            } else if (const_references.contains(root->text)) {
              error(
                  start,
                  "FSIM-SV-SEM-249",
                  "a const ref function formal is read-only");
            }
            }
            if (statement.assignment_kind
                    != AssignmentKind::Blocking
                || statement.procedural_assignment_control
                    != ProceduralAssignmentControl::None) {
              error(
                  start,
                  "FSIM-SV-SEM-063",
                  "function assignments must be blocking and time-free");
            }
          }
          const bool forbidden = statement.kind == StatementKind::Delay
              || statement.kind == StatementKind::WaitOn
              || statement.kind == StatementKind::WaitUntil
              || statement.kind == StatementKind::WaitOrder
              || (statement.kind == StatementKind::EventTrigger
                  && (statement.delay
                      || statement.assignment_kind
                          == AssignmentKind::NonBlocking))
              || (statement.kind == StatementKind::Fork
                  && statement.fork_join_kind != ForkJoinKind::None)
              || statement.kind == StatementKind::WaitFork
              || statement.kind == StatementKind::DisableFork;
          // IEEE 1800-2009 13.4.4 introduced fork...join_none background
          // processes in functions.
          if (statement.kind == StatementKind::Fork
              && statement.fork_join_kind == ForkJoinKind::None
              && standard_revision_ != StandardRevision::SystemVerilog2009
              && standard_revision_ != StandardRevision::SystemVerilog2012
              && standard_revision_ != StandardRevision::SystemVerilog2017
              && standard_revision_
                  != StandardRevision::SystemVerilog2023) {
            error(
                start,
                "FSIM-SV-SEM-246",
                "a function may spawn fork...join_none background "
                "processes only in SystemVerilog-2009 or later");
          }
          if (forbidden) {
            error(
                start,
                "FSIM-SV-SEM-064",
                "functions cannot contain timing controls, event or task "
                "statements");
          }
          if (statement.kind == StatementKind::Fork
              && statement.fork_join_kind == ForkJoinKind::None) {
            // SystemVerilog-2023 gives the spawned background processes task
            // statement semantics. Their timing controls do not make the
            // calling function itself time-consuming.
            continue;
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  inspect(inspect, function.statements);
}

// The ANSI formal list of a function or task header after its '(' (IEEE
// 1800-2017 13.3, 13.4). A formal without a direction inherits the
// previous one's direction and, without a data type, its type. `declare`
// checks and records each formal name.
std::vector<VerilogParser::AnsiCallableFormal>
VerilogParser::parse_ansi_callable_formals(
    const std::string_view kind,
    const std::function<void(const Token&)>& declare) {
  std::vector<AnsiCallableFormal> formals;
  Type inherited_type;
  PortDirection inherited_direction { PortDirection::Input };
  bool inherited_reference = false;
  bool inherited_const_reference = false;
  bool inherited_static_reference = false;
  bool have_inherited_formal = false;
  while (!at_end() && !at(TokenKind::RightParen)) {
    auto direction = inherited_direction;
    bool explicit_direction = false;
    bool reference = inherited_reference;
    bool const_reference = inherited_const_reference;
    bool static_reference = inherited_static_reference;
    const bool const_qualifier = match_keyword("const");
    if (is_direction_keyword()) {
      if (const_qualifier) {
        error(
            previous(),
            "FSIM-SV-SEM-248",
            "const is permitted only before a ref formal");
      }
      direction = parse_direction();
      reference = false;
      const_reference = false;
      static_reference = false;
      explicit_direction = true;
    } else if (match_keyword("ref")) {
      explicit_direction = true;
      direction = const_qualifier
          ? PortDirection::Input
          : PortDirection::Inout;
      reference = true;
      const_reference = const_qualifier;
      static_reference = match_keyword("static");
      if (static_reference) {
        (void)require_standard(
            "a ref static " + std::string { kind } + " formal",
            StandardRevision::SystemVerilog2023,
            previous(),
            "FSIM-SV-PARSE-368");
      }
    } else if (const_qualifier) {
      error(
          previous(),
          "FSIM-SV-SEM-248",
          "const is permitted only before a ref formal");
    }

    if (explicit_direction) {
      inherited_direction = direction;
      inherited_reference = reference;
      inherited_const_reference = const_reference;
      inherited_static_reference = static_reference;
    } else if (!have_inherited_formal) {
      direction = PortDirection::Input;
      reference = false;
      const_reference = false;
      static_reference = false;
    }

    // `var` marks a variable formal; without a data type it is logic
    // (IEEE 1800-2017 13.3, 23.2.2.3).
    const bool var_formal = language_ == Language::SystemVerilog2017
        && match_keyword("var");
    const bool implicit_var_formal = var_formal
        && at(TokenKind::Identifier)
        && !keyword_reserved(keyword_set_, current().text)
        && !is_named_type_reference_start();
    const bool explicit_type =
        keyword("string") || keyword("byte")
        || keyword("shortint") || keyword("longint")
        || keyword("time") || keyword("shortreal")
        || keyword("real") || keyword("realtime")
        || keyword("chandle") || keyword("process")
        || keyword("integer")
        || keyword("int") || keyword("logic")
        || keyword("reg") || keyword("bit")
        || keyword("signed") || keyword("unsigned")
        || at(TokenKind::LeftBracket)
        || is_named_type_reference_start();
    if (language_ != Language::SystemVerilog2017 && !explicit_direction
        && (explicit_type || !have_inherited_formal)) {
      // IEEE 1364-2005 10.3.1: each Verilog function port declaration
      // names its direction; only SystemVerilog defaults it.
      error(current(), "FSIM-SV-SEM-395",
          "a Verilog function or task port declaration requires a "
          "direction");
    }
    Type type;
    if (language_ == Language::SystemVerilog2017 && keyword("virtual")) {
      // A virtual interface formal (IEEE 1800-2017 25.9).
      type = parse_virtual_interface_type(advance());
      inherited_type = type;
    } else if (implicit_var_formal
        || (explicit_direction && !explicit_type
            && at(TokenKind::Identifier)
            && (at(TokenKind::LeftBracket, 1) || at(TokenKind::Comma, 1)
                || at(TokenKind::RightParen, 1)
                || at(TokenKind::Assign, 1)))) {
      // A formal with no data type is `logic`, also as the element type of
      // an unpacked array formal (IEEE 1800-2017 13.3).
      type = default_verilog_type();
      type.spelling = "logic";
      inherited_type = type;
    } else if (var_formal || explicit_direction || explicit_type
        || !have_inherited_formal) {
      type = parse_parameter_type();
      inherited_type = type;
    } else {
      type = inherited_type;
    }
    have_inherited_formal = true;
    const auto argument_name = expect_identifier(
        std::string { kind } + " argument name");
    declare(argument_name);
    (void)parse_optional_container_dimension(type);
    current_procedural_types_.insert_or_assign(argument_name.text, type);
    auto& formal = formals.emplace_back();
    formal.name = argument_name;
    formal.type = std::move(type);
    formal.direction = direction;
    formal.reference = reference;
    formal.const_reference = const_reference;
    formal.static_reference = static_reference;
    if (match(TokenKind::Assign)) {
      formal.default_value = parse_expression();
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  return formals;
}

TaskDeclaration VerilogParser::parse_task(
    const Token& start,
    const bool prototype,
    const bool default_automatic) {
  TaskDeclaration task;
  std::unordered_set<std::string> explicit_task_arguments;
  if (match_keyword("automatic")) {
    (void)require_standard(
        "an automatic task",
        StandardRevision::Verilog2001,
        previous(),
        "FSIM-SV-PARSE-347");
    task.automatic = true;
    task.lifetime_explicit = true;
  } else if (match_keyword("static")) {
    (void)require_standard(
        "an explicitly static task",
        StandardRevision::SystemVerilog2005,
        previous(),
        "FSIM-SV-PARSE-347");
    task.lifetime_explicit = true;
  } else {
    task.automatic = default_automatic || unit_default_automatic_;
    task.lifetime_explicit = false;
  }
  const LifetimeScope lifetime_scope { automatic_callable_, task.automatic };

  const auto name = expect_identifier("task name");
  task.name = name.text;
  while (match(TokenKind::Scope)) {
    task.name += "::";
    task.name += expect_identifier("selected task name").text;
  }
  const ImplicitNetDiscard method_scope { implicit_net_references_,
      implicit_net_references_.size(),
      task.name.find("::") != std::string::npos };

  auto saved_names = std::move(current_procedural_names_);
  auto saved_types = std::move(current_procedural_types_);
  const bool saved_in_task = in_task_;
  current_procedural_names_.clear();
  current_procedural_types_.clear();
  in_task_ = true;

  std::vector<std::string> classic_header_arguments;
  if (match(TokenKind::LeftParen)) {
    const auto header_open = previous();
    const bool classic_header =
        at(TokenKind::Identifier)
        && (at(TokenKind::Comma, 1)
            || at(TokenKind::RightParen, 1));
    if (classic_header) {
      std::unordered_set<std::string> names;
      do {
        const auto argument_name =
            expect_identifier("classic task argument name");
        if (!names.insert(argument_name.text).second) {
          error(
              argument_name,
              "FSIM-SV-SEM-067",
              "duplicate task argument '" + argument_name.text + "'");
        }
        classic_header_arguments.push_back(argument_name.text);
        if (language_ == Language::SystemVerilog2017) {
          // Without a body declaration the name is an ANSI formal of
          // implicit type `input logic` (IEEE 1800-2017 13.3).
          current_procedural_names_.insert(argument_name.text);
        }
      } while (match(TokenKind::Comma));
    } else {
    (void)require_standard(
        "an ANSI task header",
        StandardRevision::Verilog2001,
        header_open,
        "FSIM-SV-PARSE-347");
    for (auto& formal : parse_ansi_callable_formals(
             "task", [&](const Token& argument_name) {
               if (!current_procedural_names_.insert(
                       argument_name.text).second) {
                 error(argument_name, "FSIM-SV-SEM-067",
                       "duplicate task argument '" + argument_name.text
                           + "'");
               }
             })) {
      auto& argument = task.arguments.emplace_back(
          formal.name.text, std::move(formal.type), formal.direction,
          formal.name.span);
      argument.reference = formal.reference;
      argument.const_reference = formal.const_reference;
      argument.static_reference = formal.static_reference;
      argument.default_value = std::move(formal.default_value);
    }
    }
    expect(TokenKind::RightParen, "')' after task arguments",
           "FSIM-SV-PARSE-143");
  }
  expect(TokenKind::Semicolon, "';' after task header", "FSIM-SV-PARSE-144");

  if (prototype) {
    task.span = span_from(start, previous());
    current_procedural_names_ = std::move(saved_names);
    current_procedural_types_ = std::move(saved_types);
    in_task_ = saved_in_task;
    return task;
  }

  Statement body;
  body.kind = StatementKind::Block;
  DesignUnit local_declarations;
  while (!at_end() && !keyword("endtask")) {
    const auto before = position();
    if (parse_local_type_parameter(local_declarations)) {
    } else if (match_keyword("typedef")) {
      const auto alias_count = local_declarations.type_aliases.size();
      parse_typedef(local_declarations, previous());
      if (local_declarations.type_aliases.size() > alias_count) {
        const auto& alias = local_declarations.type_aliases.back();
        current_procedural_types_.insert_or_assign(alias.name, alias.type);
      }
    } else if (is_direction_keyword() || keyword("ref")
               || keyword("const")) {
      const auto declaration_start = current();
      const bool const_reference = match_keyword("const");
      if (const_reference && !keyword("ref")) {
        error(
            declaration_start,
            "FSIM-SV-SEM-248",
            "const is permitted only before a ref formal");
      }
      const auto direction_token = advance();
      const bool reference = direction_token.text == "ref";
      const bool static_reference = reference && match_keyword("static");
      if (static_reference) {
        (void)require_standard(
            "a ref static task formal",
            StandardRevision::SystemVerilog2023,
            previous(),
            "FSIM-SV-PARSE-368");
      }
      const auto direction = reference
          ? const_reference ? PortDirection::Input : PortDirection::Inout
          : direction_token.text == "output"
              ? PortDirection::Output
          : direction_token.text == "inout"
              ? PortDirection::Inout
              : PortDirection::Input;
      Type type;
      bool explicit_argument_type = false;
      if (at(TokenKind::Identifier)
          && (at(TokenKind::Comma, 1)
              || at(TokenKind::Semicolon, 1)
              || at(TokenKind::Assign, 1))) {
        type = Type{ValueDomain::Integer, "implicit", std::nullopt, true};
      } else {
        explicit_argument_type = !at(TokenKind::LeftBracket)
            && !keyword("signed") && !keyword("unsigned");
        type = parse_parameter_type();
      }
      do {
        const auto argument_name =
            expect_identifier("classic task argument name");
        // `T x; input x;` declares one argument of type T (13.3).
        const bool typed_earlier = std::ranges::any_of(
            body.declarations, [&](const VariableDeclaration& variable) {
                return variable.name == argument_name.text;
            });
        const bool repeated_direction = std::ranges::any_of(
            task.arguments, [&](const TaskArgument& argument) {
                return argument.name == argument_name.text;
            });
        if (typed_earlier && explicit_argument_type) {
          error(argument_name, "FSIM-SV-SEM-396",
              "task argument '" + argument_name.text
                  + "' declared with a data type cannot be declared again");
        }
        if (explicit_argument_type) {
          explicit_task_arguments.insert(argument_name.text);
        }
        // A classic header name is declared by this body declaration.
        const bool header_argument = std::ranges::find(
            classic_header_arguments, argument_name.text)
            != classic_header_arguments.end();
        if ((!current_procedural_names_.insert(
                argument_name.text).second && !typed_earlier
                && !header_argument)
            || repeated_direction) {
          error(
              argument_name,
              "FSIM-SV-SEM-067",
              "duplicate task argument '" + argument_name.text + "'");
        }
        auto argument_type = type;
        (void)parse_optional_container_dimension(argument_type);
        current_procedural_types_.insert_or_assign(
            argument_name.text, argument_type);
        task.arguments.push_back(TaskArgument{
            argument_name.text,
            std::move(argument_type),
            direction,
            argument_name.span});
        task.arguments.back().reference = reference;
        task.arguments.back().const_reference = const_reference;
        task.arguments.back().static_reference = static_reference;
        if (match(TokenKind::Assign)) {
          task.arguments.back().default_value = parse_expression();
        }
      } while (match(TokenKind::Comma));
      expect(
          TokenKind::Semicolon,
          "';' after classic task argument declaration",
          "FSIM-SV-PARSE-198");
    } else if (is_block_declaration_start() || keyword("parameter")
        || keyword("localparam")) {
      parse_procedural_declaration(body);
    } else if (auto statement = parse_statement()) {
      body.statements.push_back(std::move(*statement));
    }
    if (position() == before) {
      advance();
    }
  }
  expect_keyword("endtask", false, "FSIM-SV-PARSE-145");
  if (match(TokenKind::Colon)) {
    const auto end_name = expect_identifier("task name after endtask");
    const auto separator = task.name.rfind("::");
    const auto expected_name = separator == std::string::npos
        ? std::string_view{task.name}
        : std::string_view{task.name}.substr(separator + 2U);
    if (end_name.text != expected_name) {
      error(end_name, "FSIM-SV-SEM-068",
            "task end name does not match '" + task.name + "'");
    }
  }

  task.type_aliases = std::move(local_declarations.type_aliases);
  task.variables = std::move(body.declarations);
  // A body declaration of a classic argument's name gives that argument
  // its data type (`input x; T x;`, IEEE 1800-2017 13.3) rather than
  // declaring a second object.
  for (auto& argument : task.arguments) {
    const auto variable = std::ranges::find(
        task.variables, argument.name, &VariableDeclaration::name);
    if (variable == task.variables.end() || variable->initializer) {
      continue;
    }
    check_port_redeclaration(argument.type,
        explicit_task_arguments.contains(argument.name), variable->type,
        Token { TokenKind::Identifier, argument.name, variable->span, { } },
        "task argument");
    argument.type = variable->type;
    task.variables.erase(variable);
  }
  // A port declaration without a data type that no body declaration
  // completes is a one-bit logic (IEEE 1364-2005 10.2.1, IEEE 1800-2017
  // 13.3).
  for (auto& argument : task.arguments) {
    if (argument.type.spelling == "implicit") {
      argument.type = default_verilog_type();
      argument.type.spelling = "logic";
    }
  }
  task.statements = std::move(body.statements);
  if (!classic_header_arguments.empty()) {
    std::vector<TaskArgument> ordered;
    ordered.reserve(classic_header_arguments.size());
    std::size_t declared_in_body { };
    for (const auto& argument_name : classic_header_arguments) {
      const auto found = std::ranges::find(
          task.arguments,
          argument_name,
          &TaskArgument::name);
      if (found == task.arguments.end()
          && language_ == Language::SystemVerilog2017) {
        auto type = default_verilog_type();
        type.spelling = "logic";
        ordered.push_back(TaskArgument{
            argument_name, std::move(type), PortDirection::Input,
            task.span});
      } else if (found == task.arguments.end()) {
        error(
            start,
            "FSIM-SV-SEM-067",
            "classic task argument '" + argument_name
                + "' has no body declaration");
      } else {
        ordered.push_back(std::move(*found));
        ++declared_in_body;
      }
    }
    if (declared_in_body != task.arguments.size()) {
      error(
          start,
          "FSIM-SV-SEM-067",
          "classic task body declares an argument absent from its header "
          "list");
    }
    task.arguments = std::move(ordered);
  }
  task.span = span_from(start, previous());
  validate_task_body(task, start);

  current_procedural_names_ = std::move(saved_names);
  current_procedural_types_ = std::move(saved_types);
  in_task_ = saved_in_task;
  return task;
}

void VerilogParser::validate_task_body(
    const TaskDeclaration& task,
    const Token& start) {
  std::unordered_set<std::string> names;
  std::unordered_set<std::string> const_references;
  for (const auto& argument : task.arguments) {
    names.insert(argument.name);
    if (argument.const_reference) {
      const_references.insert(argument.name);
    }
    // An input, inout or ref formal may have a default (IEEE 1800-2017
    // 13.5.3); a ref or inout default names the variable it refers to.
    if (argument.default_value
        && argument.direction == PortDirection::Output) {
      error(
          start,
          "FSIM-SV-SEM-097",
          "default task arguments require input value formals");
    }
    if (argument.reference && !task.automatic) {
      error(
          start,
          "FSIM-SV-SEM-098",
          "ref task arguments require automatic lifetime");
    }
  }
  for (const auto& variable : task.variables) {
    if (!names.insert(variable.name).second) {
      error(start, "FSIM-SV-SEM-069",
            "duplicate or conflicting task local '" + variable.name + "'");
    }
  }
  // An automatic variable cannot be written by a nonblocking assignment,
  // named in its intra-assignment event, or used by a procedural
  // continuous assignment or force (IEEE 1800-2017 6.21).
  if (task.automatic) {
    const auto root_name = [](const auto& self, const Expression& value)
        -> std::string {
      if (value.kind == ExpressionKind::Identifier) {
        return value.text.substr(0U, value.text.find_first_of(".["));
      }
      if ((value.kind == ExpressionKind::Index
              || value.kind == ExpressionKind::Slice)
          && !value.operands.empty()) {
        return self(self, value.operands.front());
      }
      return { };
    };
    const auto mentions = [&](const auto& self, const Expression& value,
                              const std::unordered_set<std::string>& automatic)
        -> std::optional<std::string> {
      if (value.kind == ExpressionKind::Identifier) {
        const auto name = value.text.substr(0U, value.text.find_first_of(".["));
        if (automatic.contains(name)) {
          return name;
        }
      }
      for (const auto& operand : value.operands) {
        if (const auto found = self(self, operand, automatic)) {
          return found;
        }
      }
      return std::nullopt;
    };
    const auto check = [&](const auto& self,
                           const auto& statements,
                           std::unordered_set<std::string> automatic) -> void {
      for (const auto& statement : statements) {
        auto scoped = automatic;
        for (const auto& declaration : statement.declarations) {
          if (declaration.systemverilog_static) {
            scoped.erase(declaration.name);
          } else {
            scoped.insert(declaration.name);
          }
        }
        const auto report = [&](const std::string& name,
                                const std::string& use) {
          error(start, "FSIM-SV-SEM-397",
              "automatic variable '" + name + "' cannot be " + use);
        };
        const auto target = root_name(root_name, statement.target);
        if (statement.kind == StatementKind::Assignment
            && statement.assignment_kind == AssignmentKind::NonBlocking) {
          if (scoped.contains(target)) {
            report(target, "written by a nonblocking assignment");
          }
          for (const auto& sensitivity : statement.sensitivities) {
            const auto name = sensitivity.signal.substr(
                0U, sensitivity.signal.find_first_of(".["));
            if (scoped.contains(name)) {
              report(name, "named in a nonblocking assignment's event");
            } else if (const auto found = mentions(
                           mentions, sensitivity.expression, scoped)) {
              report(*found, "named in a nonblocking assignment's event");
            }
          }
        } else if (statement.kind == StatementKind::ProceduralAssign
            || statement.kind == StatementKind::Force) {
          if (scoped.contains(target)) {
            report(target, "the target of a procedural continuous "
                           "assignment or force");
          } else if (const auto found = mentions(
                         mentions, statement.value, scoped)) {
            report(*found, "used by a procedural continuous assignment "
                           "or force");
          }
        } else if ((statement.kind == StatementKind::Deassign
                       || statement.kind == StatementKind::Release)
            && scoped.contains(target)) {
          report(target, "deassigned or released");
        }
        self(self, statement.statements, scoped);
        self(self, statement.else_statements, scoped);
        for (const auto& alternative : statement.case_alternatives) {
          self(self, alternative.statements, scoped);
        }
      }
    };
    // A local explicitly declared `static` is not automatic (6.21).
    auto automatic_names = names;
    for (const auto& variable : task.variables) {
      if (variable.systemverilog_static) {
        automatic_names.erase(variable.name);
      }
    }
    check(check, task.statements, automatic_names);
  }
  const auto inspect =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          if (statement.kind == StatementKind::Assignment) {
            const Expression* root = &statement.target;
            while ((root->kind == ExpressionKind::Index
                    || root->kind == ExpressionKind::Slice)
                   && !root->operands.empty()) {
              root = &root->operands.front();
            }
            if (root->kind == ExpressionKind::Identifier
                && const_references.contains(root->text)) {
              error(
                  start,
                  "FSIM-SV-SEM-249",
                  "a const ref task formal is read-only");
            }
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative : statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  inspect(inspect, task.statements);
}

}  // namespace fsim::frontend

namespace fsim::frontend {

// `localparam type T = logic [7:0];` in a subroutine is a local type
// declaration (IEEE 1800-2017 6.20.3, A.2.8 block_item_declaration).
bool VerilogParser::parse_local_type_parameter(DesignUnit& local_declarations)
{
  if (!(keyword("localparam") || keyword("parameter")) || !keyword("type", 1)
      || !at(TokenKind::Identifier, 2) || !at(TokenKind::Assign, 3)) {
    return false;
  }
  const auto start = advance();
  (void)advance();
  const auto name = advance();
  (void)advance();
  auto type = parse_parameter_type();
  expect(TokenKind::Semicolon, "';' after local type parameter",
      "FSIM-SV-PARSE-197");
  packed_typedef_types_.insert_or_assign(name.text, type);
  current_procedural_types_.insert_or_assign(name.text, type);
  local_declarations.type_aliases.push_back({ name.text,
      std::move(type),
      cover(start.span, previous().span),
      { },
      TypeDeclarationKind::SystemVerilogTypedef,
      { },
      { },
      { },
      false });
  return true;
}

}  // namespace fsim::frontend
