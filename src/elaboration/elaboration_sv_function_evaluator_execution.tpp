// SPDX-License-Identifier: Apache-2.0

// Internal ConstantFunctionEvaluator fragment included by
// elaboration_sv_functions.cpp.

    std::optional<Value> evaluate_call(
        const frontend::FunctionDeclaration& declared_function,
        const Expression& call,
        const SystemVerilogConstantEnvironment& caller,
        const frontend::Type* expected_type,
        std::string& error) {
        std::optional<frontend::FunctionDeclaration> specialized_function;
        if (declared_function.language == frontend::Language::Vhdl2008) {
            specialized_function.emplace(declared_function);
        }
        const auto& function = specialized_function
            ? *specialized_function : declared_function;
        if (!function.automatic
            && function.language != frontend::Language::Verilog2005) {
            error = "static or implicit-lifetime function '"
                + function.name + "' is not a constant function";
            return std::nullopt;
        }
        std::vector<const Expression*> actuals(
            function.arguments.size());
        const bool have_names = !call.call_argument_names.empty();
        if (have_names
            && call.call_argument_names.size()
                != call.operands.size()) {
            error = "constant function association metadata is inconsistent";
            return std::nullopt;
        }
        bool named_seen = false;
        std::size_t positional = 0;
        for (std::size_t index = 0;
             index < call.operands.size(); ++index) {
            const auto& name = have_names
                ? call.call_argument_names[index]
                : std::string{};
            std::size_t formal_index = 0;
            if (name.empty()) {
                if (named_seen || positional >= actuals.size()) {
                    error = "invalid positional constant function argument";
                    return std::nullopt;
                }
                formal_index = positional++;
            } else {
                named_seen = true;
                const auto found = std::ranges::find(
                    function.arguments,
                    name,
                    &frontend::FunctionArgument::name);
                if (found == function.arguments.end()) {
                    error = "unknown named constant function argument '"
                        + name + "'";
                    return std::nullopt;
                }
                formal_index = static_cast<std::size_t>(
                    std::distance(function.arguments.begin(), found));
            }
            if (actuals[formal_index] != nullptr) {
                error = "duplicate constant function argument association";
                return std::nullopt;
            }
            if (call.operands[index].valid()) {
                actuals[formal_index] = &call.operands[index];
            }
        }
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            const auto& formal = function.arguments[index];
            if (formal.direction != frontend::PortDirection::Input
                || formal.reference) {
                error = "constant functions require input value arguments";
                return std::nullopt;
            }
            if (actuals[index] == nullptr && formal.default_value) {
                actuals[index] = &*formal.default_value;
            }
            if (actuals[index] == nullptr) {
                error = "constant function argument '" + formal.name
                    + "' has no actual or default value";
                return std::nullopt;
            }
        }
        if (function.language == frontend::Language::Vhdl2008) {
            auto& mutable_function = *specialized_function;
            const auto simple_type_name = [](const std::string_view spelling) {
              const auto separator = spelling.find_last_of('.');
              return spelling.substr(
                  separator == std::string_view::npos
                      ? 0U : separator + 1U);
            };
            const auto unconstrained_builtin_array =
                [&](const frontend::Type& type) {
                  const auto name = simple_type_name(type.spelling);
                  return !type.packed_range && !type.vhdl_array
                      && (name == "bit_vector"
                          || name == "std_logic_vector"
                          || name == "std_ulogic_vector"
                          || name == "signed"
                          || name == "unsigned");
                };
            ConstantEnvironment formal_environment;
            ConstantDomainEnvironment formal_domains;
            for (std::size_t index = 0;
                 index < mutable_function.arguments.size(); ++index) {
                auto& formal = mutable_function.arguments[index];
                const auto actual = evaluate_expression(
                    *actuals[index], caller, error);
                if (!actual) {
                    error = "constant function argument '" + formal.name
                        + "': " + error;
                    return std::nullopt;
                }
                if (unconstrained_builtin_array(formal.type)) {
                    formal.type.packed_range = frontend::PackedRange{
                        static_cast<std::int64_t>(actual->width - 1U),
                        0,
                        true};
                    formal.type.domain = actual->domain;
                    formal.type.is_signed = actual->is_signed;
                }
                if (formal.type.domain
                    == frontend::ValueDomain::Integer) {
                    if (const auto integer = actual->integer_value()) {
                        formal_environment.insert_or_assign(
                            formal.name, *integer);
                        formal_domains.insert_or_assign(
                            formal.name,
                            ConstantTypeInfo{
                                frontend::ValueDomain::Integer,
                                false,
                                {}});
                    }
                }
            }
            if (unconstrained_builtin_array(mutable_function.return_type)
                && expected_type != nullptr
                && expected_type->width().value_or(0U) != 0U) {
                mutable_function.return_type = *expected_type;
            }
            std::vector<Diagnostic> ignored_diagnostics;
            for (auto& constant : mutable_function.constants) {
                substitute_parameters(
                    constant.type,
                    formal_environment,
                    formal_domains,
                    ignored_diagnostics,
                    frontend::Language::Vhdl2008);
                substitute_parameters(
                    constant.default_value,
                    formal_environment,
                    formal_domains,
                    frontend::Language::Vhdl2008);
                std::string ignored;
                if (const auto value = evaluate_constant_expression(
                        constant.default_value,
                        formal_environment,
                        ignored)) {
                    formal_environment.insert_or_assign(
                        constant.name, *value);
                    formal_domains.insert_or_assign(
                        constant.name,
                        ConstantTypeInfo{
                            constant.type.domain,
                            false,
                            constant.type.nominal_type});
                }
            }
            for (auto& alias : mutable_function.type_aliases) {
                substitute_parameters(
                    alias.type,
                    formal_environment,
                    formal_domains,
                    ignored_diagnostics,
                    frontend::Language::Vhdl2008);
            }
            for (auto& variable : mutable_function.variables) {
                substitute_parameters(
                    variable,
                    formal_environment,
                    formal_domains,
                    ignored_diagnostics,
                    frontend::Language::Vhdl2008);
            }
            substitute_parameters(
                mutable_function.return_type,
                formal_environment,
                formal_domains,
                ignored_diagnostics,
                frontend::Language::Vhdl2008);
            substitute_parameters(
                mutable_function.statements,
                formal_environment,
                formal_domains,
                ignored_diagnostics,
                frontend::Language::Vhdl2008);
        }
        std::vector<Value> converted_arguments;
        converted_arguments.reserve(function.arguments.size());
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            auto value = converted(
                *actuals[index],
                function.arguments[index].type,
                caller,
                error);
            if (!value) {
                error =
                    "constant function argument '"
                    + function.arguments[index].name + "': "
                    + error;
                return std::nullopt;
            }
            converted_arguments.push_back(std::move(*value));
        }
        if (std::ranges::any_of(
                call_stack_, [&](const auto* active) {
                    return active->name == function.name
                        && active->arguments.size()
                            == function.arguments.size();
                })) {
            error =
                "recursive constant function call involving '"
                + function.name + "'";
            return std::nullopt;
        }

        std::optional<std::string> cache_key{std::in_place};
        const auto append_cache_component =
            [&](const std::string_view component) {
              append_key_component(*cache_key, component);
            };
        auto behavior = callable_behaviors_.find(&declared_function);
        if (behavior == callable_behaviors_.end()) {
            behavior = callable_behaviors_.emplace(
                &declared_function,
                callable_behavior_identity(declared_function)).first;
        }
        append_cache_component(behavior->second.digest);
        std::set<std::string> referenced_identifiers
            = behavior->second.identifiers;
        std::set<std::string> dependency_digests;
        std::unordered_set<const frontend::FunctionDeclaration*> visited {
            &declared_function
        };
        std::vector<const frontend::FunctionDeclaration*> pending {
            &declared_function
        };
        while (!pending.empty()) {
            const auto* dependency = pending.back();
            pending.pop_back();
            const auto dependency_behavior
                = dependency == &declared_function
                ? behavior->second
                : callable_behavior_identity(*dependency);
            referenced_identifiers.insert(
                dependency_behavior.identifiers.begin(),
                dependency_behavior.identifiers.end());
            if (dependency != &declared_function) {
                dependency_digests.insert(dependency_behavior.digest);
            }
            for (const auto& identifier :
                 dependency_behavior.identifiers) {
                const auto consider = [&](const auto& candidate) {
                    if (callable_name_matches(
                            candidate.name, identifier,
                            candidate.language)
                        && visited.insert(&candidate).second) {
                        pending.push_back(&candidate);
                    }
                };
                for (const auto& candidate : functions_) {
                    consider(candidate);
                }
                for (const auto* active : call_stack_) {
                    for (const auto& candidate : active->functions) {
                        consider(candidate);
                    }
                }
            }
        }
        for (const auto& digest : dependency_digests) {
            append_cache_component("callee");
            append_cache_component(digest);
        }
        for (const auto& identifier : referenced_identifiers) {
            if (const auto found = caller.find(identifier);
                found != caller.end()) {
                append_cache_component(identifier);
                append_cache_component(found->second.canonical());
                continue;
            }
            if (const auto found = globals_.find(identifier);
                found != globals_.end()) {
                append_cache_component(identifier);
                append_cache_component(found->second.canonical());
                continue;
            }
            if (const auto found = fallback_.find(identifier);
                found != fallback_.end()) {
                append_cache_component(identifier);
                append_cache_component(std::to_string(found->second));
            }
        }
        append_cache_component(
            std::to_string(static_cast<unsigned>(
                declared_function.language)));
        append_cache_component(declared_function.name);
        append_cache_component(
            frontend::physical_source(declared_function.span));
        append_cache_component(std::to_string(
            declared_function.span.begin.offset));
        append_cache_component(std::to_string(
            declared_function.span.end.offset));
        append_cache_component(std::to_string(
            declared_function.arguments.size()));
        if (function.language == frontend::Language::Vhdl2008) {
            append_cache_component("vhdl-call-v1");
        }
        append_cache_component(
            vhdl_type_identity(function.return_type));
        for (const auto& argument : function.arguments) {
            append_cache_component(
                vhdl_type_identity(argument.type));
        }
        for (const auto& value : converted_arguments) {
            append_cache_component(value.canonical());
        }
        const auto cached = call_cache_.find(*cache_key);
        if (cached != call_cache_.end()) {
            auto result = cached->second;
            result.source = call.span;
            return result;
        }

        auto environment = globals_;
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            environment.insert_or_assign(
                function.arguments[index].name,
                std::move(converted_arguments[index]));
        }
        call_stack_.push_back(&function);
        struct Pop {
            std::vector<const frontend::FunctionDeclaration*>& stack;
            ~Pop() { stack.pop_back(); }
        } pop{call_stack_};

        std::unordered_map<std::string, const frontend::Type*> types;
        types.emplace(function.name, &function.return_type);
        types.emplace(
            result_alias(function.name), &function.return_type);
        for (const auto& argument : function.arguments) {
            types.emplace(argument.name, &argument.type);
        }
        const auto collect_declarations =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> void {
              for (const auto& statement : statements) {
                  for (const auto& declaration :
                       statement.declarations) {
                      types.insert_or_assign(
                          declaration.name, &declaration.type);
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
            types.insert_or_assign(
                variable.name, &variable.type);
        }
        for (const auto& constant : function.constants) {
            types.insert_or_assign(
                constant.name, &constant.type);
        }
        collect_declarations(
            collect_declarations, function.statements);
        type_scopes_.push_back(&types);
        struct PopTypes {
            std::vector<const std::unordered_map<
                std::string, const frontend::Type*>*>& stack;
            ~PopTypes() { stack.pop_back(); }
        } pop_types { type_scopes_ };

        const auto initialize =
            [&](const frontend::VariableDeclaration& variable)
                -> bool {
              const auto layout = fixed_array_layout(
                  variable.type, environment);
              const auto declared_width = variable.type.width();
              const auto width = layout
                  ? static_cast<std::uint64_t>(layout->total_width)
                  : static_cast<std::uint64_t>(
                        declared_width.value_or(32));
              if (width == 0
                  || width > std::numeric_limits<std::uint32_t>::max()) {
                  error = "constant function variable '" + variable.name
                      + "' has an invalid zero or oversized packed width";
                  return false;
              }
              Value value{
                  0,
                  0,
                  0,
                  static_cast<std::uint32_t>(width),
                  layout ? false : variable.type.is_signed,
                  false,
                  variable.span};
              if (variable.initializer) {
                  bool initialized = false;
                  if (layout
                      && variable.initializer->kind
                          == ExpressionKind::Aggregate
                      && variable.initializer->operands.size() == 1U
                      && variable.initializer->aggregate_choices.size()
                          == 1U
                      && variable.initializer->aggregate_choices.front()
                          == "others") {
                      const auto element = converted(
                          variable.initializer->operands.front(),
                          *layout->element_type,
                          environment,
                          error);
                      if (!element
                          || element->width != layout->element_width) {
                          return false;
                      }
                      for (std::uint32_t offset = 0;
                           offset < layout->total_width;
                           offset += layout->element_width) {
                          for (std::uint32_t bit = 0;
                               bit < layout->element_width; ++bit) {
                              const auto state =
                                  element->packed.get_logic9(bit);
                              if (value.packed.is_logic9()) {
                                  value.packed.set_logic9(
                                      offset + bit, state);
                              } else {
                                  value.packed.set(
                                      offset + bit,
                                      runtime::to_logic4(state));
                              }
                          }
                      }
                      value.refresh_low_word_mirrors();
                      initialized = true;
                  }
                  if (!initialized) {
                      const auto initial = converted(
                          *variable.initializer,
                          variable.type,
                          environment,
                          error);
                      if (!initial) {
                          return false;
                      }
                      value = *initial;
                  }
              }
              environment.insert_or_assign(
                  variable.name, std::move(value));
              return true;
            };
        for (const auto& constant : function.constants) {
            frontend::VariableDeclaration local_constant;
            local_constant.name = constant.name;
            local_constant.type = constant.type;
            local_constant.initializer = constant.default_value;
            local_constant.span = constant.span;
            if (!initialize(local_constant)) {
                error = "constant function local constant '"
                    + constant.name + "': " + error;
                return std::nullopt;
            }
        }
        for (const auto& variable : function.variables) {
            if (!initialize(variable)) {
                return std::nullopt;
            }
        }
        const auto initialize_nested =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> bool {
              for (const auto& statement : statements) {
                  for (const auto& declaration :
                       statement.declarations) {
                      if (!initialize(declaration)) {
                          return false;
                      }
                  }
                  if (!self(self, statement.statements)
                      || !self(self, statement.else_statements)) {
                      return false;
                  }
                  for (const auto& alternative :
                       statement.case_alternatives) {
                      if (!self(self, alternative.statements)) {
                          return false;
                      }
                  }
              }
              return true;
            };
        if (!initialize_nested(
                initialize_nested, function.statements)) {
            return std::nullopt;
        }

        std::optional<Value> result;
        const auto flow = execute_statements(
            function.statements,
            environment,
            types,
            result,
            error);
        if (flow == Flow::failed) {
            error = "constant function '" + function.name
                + "': " + error;
            return std::nullopt;
        }
        if (!result) {
            const auto named = environment.find(function.name);
            const auto alias =
                environment.find(result_alias(function.name));
            if (named != environment.end()) {
                result = named->second;
            } else if (alias != environment.end()) {
                result = alias->second;
            }
        }
        if (!result) {
            error =
                "constant function '" + function.name
                + "' did not assign a result";
            return std::nullopt;
        }
        if (function.language == frontend::Language::Vhdl2008
            && unconstrained_vhdl_builtin_array(
                function.return_type)) {
            call_cache_.try_emplace(
                std::move(*cache_key), *result);
            return result;
        }
        auto converted_result = convert_systemverilog_parameter_value(
            *result, function.return_type, error);
        if (converted_result && cache_key) {
            call_cache_.try_emplace(
                std::move(*cache_key), *converted_result);
        }
        return converted_result;
    }

    Flow execute_statements(
        const std::vector<Statement>& statements,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error) {
        for (const auto& statement : statements) {
            const auto flow = execute_statement(
                statement, environment, types, result, error);
            if (flow != Flow::normal) {
                if (flow == Flow::failed) {
                    error = "line "
                        + std::to_string(statement.span.begin.line)
                        + ": " + error;
                }
                return flow;
            }
        }
        return Flow::normal;
    }

    Flow execute_statement(
        const Statement& statement,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error) {
        if (statement.kind == StatementKind::Assignment) {
            const Expression* root = &statement.target;
            while ((root->kind == ExpressionKind::Index
                    || root->kind == ExpressionKind::Slice)
                   && !root->operands.empty()) {
                root = &root->operands.front();
            }
            if (root->kind != ExpressionKind::Identifier) {
                error =
                    "constant function assignment target is not a local "
                    "packed variable or selection";
                return Flow::failed;
            }
            const auto type = types.find(root->text);
            if (type == types.end()) {
                error =
                    "constant function assignment target '"
                    + root->text + "' is not local";
                return Flow::failed;
            }
            std::optional<Value> value;
            if (statement.target.kind == ExpressionKind::Identifier) {
                value = converted(
                    statement.value, *type->second, environment, error);
            } else if ((statement.target.kind == ExpressionKind::Index
                        && statement.target.operands.size() == 2U)
                       || (statement.target.kind == ExpressionKind::Slice
                           && statement.target.operands.size() == 3U)) {
                const auto base = environment.find(root->text);
                const auto layout = type != types.end()
                    ? fixed_array_layout(*type->second, environment)
                    : std::nullopt;
                std::optional<std::uint32_t> array_offset;
                if (layout
                    && statement.target.kind == ExpressionKind::Index) {
                    const auto index = evaluate_expression(
                        statement.target.operands[1], environment, error);
                    const auto integer = index
                        ? index->integer_value() : std::nullopt;
                    array_offset = integer
                        ? fixed_array_offset(*layout, *integer)
                        : std::nullopt;
                    if (!array_offset) {
                        error = "constant function fixed-array assignment "
                                "index is outside the declared range";
                        return Flow::failed;
                    }
                }
                const auto selected = layout
                    ? std::optional<Value> { Value {
                          0,
                          0,
                          0,
                          layout->element_width,
                          layout->element_type->is_signed,
                          false,
                          statement.target.span } }
                    : base != environment.end()
                    ? evaluate_expression(
                          statement.target, environment, error)
                    : std::nullopt;
                if (base == environment.end()) {
                    error =
                        "constant function selected assignment target '"
                        + root->text + "' has no current value";
                }
                if (!selected) {
                    return Flow::failed;
                }
                frontend::Type selected_type{
                    base->second.domain,
                    base->second.domain == frontend::ValueDomain::Bit2
                        ? "bit" : "logic",
                    frontend::PackedRange{
                        static_cast<std::int64_t>(selected->width - 1U),
                        0,
                        true},
                    false};
                const auto replacement = converted(
                    statement.value,
                    layout ? *layout->element_type : selected_type,
                    environment,
                    error);
                if (!replacement) {
                    return Flow::failed;
                }
                std::vector<std::uint32_t> offsets;
                offsets.reserve(selected->width);
                if (layout) {
                    for (std::uint32_t bit = 0;
                         bit < layout->element_width; ++bit) {
                        offsets.push_back(*array_offset + bit);
                    }
                } else if (statement.target.kind == ExpressionKind::Index) {
                    const auto index = evaluate_expression(
                        statement.target.operands[1], environment, error);
                    const auto position =
                        index ? index->integer_value() : std::nullopt;
                    if (!position || *position < 0
                        || static_cast<std::uint64_t>(*position)
                            >= base->second.width) {
                        error =
                            "constant function bit-select assignment is "
                            "outside the packed target";
                        return Flow::failed;
                    }
                    offsets.push_back(
                        static_cast<std::uint32_t>(*position));
                } else {
                    const auto first = evaluate_expression(
                        statement.target.operands[1], environment, error);
                    const auto second = first
                        ? evaluate_expression(
                              statement.target.operands[2],
                              environment, error)
                        : std::nullopt;
                    const auto first_index =
                        first ? first->integer_value() : std::nullopt;
                    const auto second_index =
                        second ? second->integer_value() : std::nullopt;
                    if (!first_index || !second_index) {
                        error =
                            "constant function part-select assignment "
                            "bounds must be known integers";
                        return Flow::failed;
                    }
                    for (std::uint32_t bit = 0;
                         bit < selected->width;
                         ++bit) {
                        bool add = true;
                        auto anchor = *second_index;
                        auto offset = bit;
                        if (statement.target.text == "+:") {
                            anchor = *first_index;
                        } else if (statement.target.text == "-:") {
                            anchor = *first_index;
                            offset = selected->width - bit - 1U;
                            add = false;
                        } else if (*first_index < *second_index) {
                            add = false;
                        }
                        if (anchor < 0) {
                            error =
                                "constant function part-select assignment "
                                "is outside the packed target";
                            return Flow::failed;
                        }
                        const auto unsigned_anchor =
                            static_cast<std::uint64_t>(anchor);
                        if ((!add && unsigned_anchor < offset)
                            || (add && unsigned_anchor
                                > std::numeric_limits<std::uint64_t>::max()
                                    - offset)) {
                            error =
                                "constant function part-select assignment "
                                "is outside the packed target";
                            return Flow::failed;
                        }
                        const auto source = add
                            ? unsigned_anchor + offset
                            : unsigned_anchor - offset;
                        if (source >= base->second.width) {
                            error =
                                "constant function part-select assignment "
                                "is outside the packed target";
                            return Flow::failed;
                        }
                        offsets.push_back(
                            static_cast<std::uint32_t>(source));
                    }
                }
                auto& updated = base->second;
                for (std::uint32_t bit = 0;
                     bit < offsets.size();
                     ++bit) {
                    const auto state = replacement->packed.get_logic9(bit);
                    if (updated.packed.is_logic9()) {
                        updated.packed.set_logic9(offsets[bit], state);
                    } else {
                        updated.packed.set(
                            offsets[bit], runtime::to_logic4(state));
                    }
                }
                updated.refresh_low_word_mirrors();
                updated.source = statement.target.span;
                if (root->text == call_stack_.back()->name
                    || root->text
                        == result_alias(call_stack_.back()->name)) {
                    result = updated;
                }
                return Flow::normal;
            } else {
                error =
                    "constant functions support one packed bit/part-select "
                    "assignment level";
                return Flow::failed;
            }
            if (!value) {
                return Flow::failed;
            }
            environment.insert_or_assign(
                root->text, *value);
            if (root->text == call_stack_.back()->name
                || root->text == result_alias(call_stack_.back()->name)) {
                result = *value;
            }
            return Flow::normal;
        }
        if (statement.kind == StatementKind::Return) {
            const auto& return_type = call_stack_.back()->return_type;
            const auto value = call_stack_.back()->language
                        == frontend::Language::Vhdl2008
                    && unconstrained_vhdl_builtin_array(return_type)
                ? evaluate_expression(
                      statement.value, environment, error)
                : converted(
                      statement.value,
                      return_type,
                      environment,
                      error);
            if (!value) {
                return Flow::failed;
            }
            result = *value;
            return Flow::returned;
        }
        if (statement.kind == StatementKind::Block) {
            return execute_statements(
                statement.statements,
                environment,
                types,
                result,
                error);
        }
        if (statement.kind == StatementKind::If) {
            const auto condition = evaluate_expression(
                statement.condition, environment, error);
            if (!condition) {
                return Flow::failed;
            }
            const auto truth = condition->truth_value();
            if (!truth) {
                error =
                    "constant function condition contains X or Z";
                return Flow::failed;
            }
            return execute_statements(
                *truth
                    ? statement.statements
                    : statement.else_statements,
                environment,
                types,
                result,
                error);
        }
        if (statement.kind == StatementKind::Case) {
            const auto selector = evaluate_expression(
                statement.condition, environment, error);
            if (!selector) {
                return Flow::failed;
            }
            const frontend::Type* selector_type = nullptr;
            if (statement.condition.kind == ExpressionKind::Identifier) {
                const auto found = types.find(statement.condition.text);
                if (found != types.end()) {
                    selector_type = found->second;
                }
            }
            const frontend::CaseAlternative* selected = nullptr;
            const frontend::CaseAlternative* fallback = nullptr;
            struct ShadowedBinding {
                std::string name;
                std::optional<Value> previous;
            };
            std::vector<ShadowedBinding> selected_bindings;
            for (const auto& alternative :
                 statement.case_alternatives) {
                if (alternative.is_default) {
                    fallback = &alternative;
                    continue;
                }
                if (statement.case_match_kind
                    == frontend::CaseMatchKind::Matches) {
                    if (alternative.choices.size() != 1) {
                        error =
                            "constant function case matches item requires "
                            "exactly one pattern";
                        return Flow::failed;
                    }
                    const auto& choice = alternative.choices.front();
                    const Expression* pattern = &choice;
                    const Expression* guard = nullptr;
                    if (choice.kind == ExpressionKind::Call
                        && choice.text == "@match-guard") {
                        if (choice.operands.size() != 2U) {
                            error = "constant function guarded case matches "
                                    "requires one pattern and one guard";
                            return Flow::failed;
                        }
                        pattern = &choice.operands[0];
                        guard = &choice.operands[1];
                    }
                    const auto matched = evaluate_case_pattern(
                        *pattern,
                        *selector,
                        selector_type,
                        environment,
                        error);
                    if (!matched) {
                        return Flow::failed;
                    }
                    if (!matched->matched) {
                        continue;
                    }
                    std::vector<ShadowedBinding> bindings;
                    for (const auto& [name, value] : matched->bindings) {
                        const auto previous = environment.find(name);
                        bindings.push_back(ShadowedBinding {
                            name,
                            previous != environment.end()
                                ? std::optional<Value> { previous->second }
                                : std::nullopt });
                        environment.insert_or_assign(name, value);
                    }
                    bool pattern_matched = true;
                    if (guard != nullptr) {
                        const auto guarded = evaluate_expression(
                            *guard, environment, error);
                        if (!guarded) {
                            return Flow::failed;
                        }
                        pattern_matched = guarded->truth_value().value_or(false);
                    }
                    if (pattern_matched) {
                        selected = &alternative;
                        selected_bindings = std::move(bindings);
                        break;
                    }
                    for (const auto& binding : bindings) {
                        if (binding.previous) {
                            environment.insert_or_assign(
                                binding.name, *binding.previous);
                        } else {
                            environment.erase(binding.name);
                        }
                    }
                    continue;
                }
                if (statement.case_match_kind
                    == frontend::CaseMatchKind::Inside) {
                    Expression membership{
                        ExpressionKind::Call,
                        "inside",
                        {selector->expression(statement.condition.span)},
                        alternative.span};
                    membership.operands.insert(
                        membership.operands.end(),
                        alternative.choices.begin(),
                        alternative.choices.end());
                    const auto matched = evaluate_expression(
                        membership, environment, error);
                    if (!matched) {
                        return Flow::failed;
                    }
                    if (matched->truth_value().value_or(false)) {
                        selected = &alternative;
                        break;
                    }
                    continue;
                }
                for (const auto& choice : alternative.choices) {
                    const auto value = evaluate_expression(
                        choice, environment, error);
                    if (!value) {
                        return Flow::failed;
                    }
                    if (value->width == selector->width
                        && value->packed == selector->packed) {
                        selected = &alternative;
                        break;
                    }
                }
                if (selected != nullptr) {
                    break;
                }
            }
            selected = selected != nullptr ? selected : fallback;
            if (selected == nullptr) {
                return Flow::normal;
            }
            const auto flow = execute_statements(
                selected->statements,
                environment,
                types,
                result,
                error);
            for (const auto& binding : selected_bindings) {
                if (binding.previous) {
                    environment.insert_or_assign(
                        binding.name, *binding.previous);
                } else {
                    environment.erase(binding.name);
                }
            }
            return flow;
        }
        if (statement.kind == StatementKind::Loop) {
            constexpr std::size_t maximum_iterations = 1'000'000;
            std::size_t iteration = 0;
            if (statement.loop_runtime) {
                if (statement.target.valid()
                    && statement.loop_initial.valid()) {
                    Statement initializer;
                    initializer.kind = StatementKind::Assignment;
                    initializer.target = statement.target;
                    initializer.value = statement.loop_initial;
                    const auto flow = execute_statement(
                        initializer,
                        environment,
                        types,
                        result,
                        error);
                    if (flow != Flow::normal) {
                        return flow;
                    }
                }
                for (;;) {
                    if (!statement.loop_post_test) {
                        const auto condition = evaluate_expression(
                            statement.condition,
                            environment,
                            error);
                        if (!condition) {
                            return Flow::failed;
                        }
                        const auto truth = condition->truth_value();
                        if (!truth) {
                            error =
                                "constant function loop condition "
                                "contains X or Z";
                            return Flow::failed;
                        }
                        if (!*truth) {
                            return Flow::normal;
                        }
                    }
                    if (iteration++ == maximum_iterations) {
                        error =
                            "constant function loop exceeds 1,000,000 "
                            "iterations";
                        return Flow::failed;
                    }
                    const auto flow = execute_statements(
                        statement.statements,
                        environment,
                        types,
                        result,
                        error);
                    if (flow == Flow::returned
                        || flow == Flow::failed) {
                        return flow;
                    }
                    if (flow == Flow::broken) {
                        return Flow::normal;
                    }
                    if (statement.loop_update_target.valid()) {
                        Statement update;
                        update.kind = StatementKind::Assignment;
                        update.target = statement.loop_update_target;
                        update.value = statement.value;
                        const auto update_flow = execute_statement(
                            update,
                            environment,
                            types,
                            result,
                            error);
                        if (update_flow != Flow::normal) {
                            return update_flow;
                        }
                    }
                    if (statement.loop_post_test) {
                        const auto condition = evaluate_expression(
                            statement.condition,
                            environment,
                            error);
                        if (!condition) {
                            return Flow::failed;
                        }
                        const auto truth = condition->truth_value();
                        if (!truth || !*truth) {
                            return truth
                                ? Flow::normal
                                : Flow::failed;
                        }
                    }
                }
            }
            const auto initial = evaluate_expression(
                statement.loop_initial, environment, error);
            const auto limit = initial
                ? evaluate_expression(
                      statement.loop_limit, environment, error)
                : std::nullopt;
            if (!initial || !limit
                || !initial->integer_value()
                || !limit->integer_value()) {
                if (error.empty()) {
                    error =
                        "constant function loop bounds must be known "
                        "integers";
                }
                return Flow::failed;
            }
            auto value = *initial->integer_value();
            const auto final = *limit->integer_value();
            const auto in_range = [&]() {
                return statement.loop_descending
                    ? (statement.loop_limit_exclusive
                           ? value > final : value >= final)
                    : (statement.loop_limit_exclusive
                           ? value < final : value <= final);
            };
            while (in_range()) {
                if (iteration++ == maximum_iterations) {
                    error =
                        "constant function loop exceeds 1,000,000 "
                        "iterations";
                    return Flow::failed;
                }
                if (!statement.loop_variable.empty()) {
                    environment.insert_or_assign(
                        statement.loop_variable,
                        Value{
                            static_cast<std::uint64_t>(value),
                            0,
                            0,
                            32,
                            true,
                            false,
                            statement.span});
                }
                const auto flow = execute_statements(
                    statement.statements,
                    environment,
                    types,
                    result,
                    error);
                if (flow == Flow::returned
                    || flow == Flow::failed) {
                    return flow;
                }
                if (flow == Flow::broken) {
                    return Flow::normal;
                }
                value += statement.loop_descending ? -1 : 1;
            }
            return Flow::normal;
        }
        if (statement.kind == StatementKind::Break) {
            return Flow::broken;
        }
        if (statement.kind == StatementKind::Continue) {
            return Flow::continued;
        }
        if (statement.kind == StatementKind::Null) {
            return Flow::normal;
        }
        error =
            "statement is not permitted in a constant function";
        return Flow::failed;
    }

    const std::vector<frontend::FunctionDeclaration>& functions_;
    const SystemVerilogConstantEnvironment& globals_;
    const ConstantEnvironment& fallback_;
    CallCache owned_call_cache_;
    CallCache& call_cache_;
    std::unordered_map<
        const frontend::FunctionDeclaration*,
        CallableBehaviorIdentity> callable_behaviors_;
    std::vector<const frontend::FunctionDeclaration*> call_stack_;
    std::vector<const std::unordered_map<
        std::string, const frontend::Type*>*> type_scopes_;
};
