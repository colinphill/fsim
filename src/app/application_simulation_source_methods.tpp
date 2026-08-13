// SPDX-License-Identifier: Apache-2.0
  [[nodiscard]] const frontend::SystemVerilogClassMethodProfile&
  source_method(
      const runtime::SystemVerilogClassHandle handle,
      const std::string_view canonical_identity,
      const bool virtual_dispatch) const {
    const auto& object = class_heap.object(handle);
    auto current = &class_specialization(object.specialization_identity);
    const frontend::SystemVerilogClassMethodProfile* requested{};
    while (current != nullptr) {
      const auto method = std::ranges::find(
          current->methods,
          canonical_identity,
          &frontend::SystemVerilogClassMethodProfile::canonical_identity);
      if (method != current->methods.end()) {
        requested = &*method;
        break;
      }
      current = current->base_specialization_identity.empty()
          ? nullptr
          : &class_specialization(current->base_specialization_identity);
    }
    if (requested == nullptr) {
      throw std::out_of_range{
          "SystemVerilog class method '" + std::string{canonical_identity}
          + "' is not available on the receiver"};
    }
    const auto* selected = requested;
    if (virtual_dispatch && requested->virtual_slot) {
      current = &class_specialization(object.specialization_identity);
      while (current != nullptr) {
        const auto override = std::ranges::find(
            current->methods,
            requested->virtual_slot,
            &frontend::SystemVerilogClassMethodProfile::virtual_slot);
        if (override != current->methods.end()) {
          if (override->profile_identity != requested->profile_identity) {
            throw std::invalid_argument{
                "virtual class method profile does not match its stable slot"};
          }
          selected = &*override;
          break;
        }
        current = current->base_specialization_identity.empty()
            ? nullptr
            : &class_specialization(current->base_specialization_identity);
      }
    }
    if (selected->is_pure) {
      throw std::invalid_argument{
          "pure SystemVerilog class method has no executable override"};
    }
    return *selected;
  }

  [[nodiscard]] const frontend::SystemVerilogClassMethodProfile*
  source_method_named(
      const runtime::SystemVerilogClassHandle handle,
      const std::string_view name) const {
    const auto& object = class_heap.object(handle);
    auto current = &class_specialization(object.specialization_identity);
    while (current != nullptr) {
      const auto method = std::ranges::find(
          current->methods,
          name,
          &frontend::SystemVerilogClassMethodProfile::name);
      if (method != current->methods.end()) {
        return &source_method(handle, method->canonical_identity, true);
      }
      current = current->base_specialization_identity.empty()
          ? nullptr
          : &class_specialization(current->base_specialization_identity);
    }
    return nullptr;
  }

  struct SourceFunctionResult {
    bool returned{};
    runtime::PackedLogic4 value;
  };

  [[nodiscard]] static std::optional<std::string>
  evaluate_source_string_expression(
      const frontend::Expression& expression,
      const SourceStringEnvironment& environment) {
    if (expression.kind == frontend::ExpressionKind::StringLiteral) {
      return expression.decoded_string.value_or(expression.text);
    }
    if (expression.kind == frontend::ExpressionKind::Identifier) {
      if (const auto found = environment.find(expression.text);
          found != environment.end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] SourceFunctionResult execute_source_function_statements(
      const std::span<const frontend::Statement> statements,
      const runtime::SystemVerilogClassHandle handle,
      ConstructorEnvironment& environment,
      SourceStringEnvironment& string_environment) {
    constexpr std::string_view property_prefix{"@sv-property:"};
    constexpr std::string_view static_property_prefix{"@sv-static-property:"};
    for (const auto& statement : statements) {
      if (statement.kind == frontend::StatementKind::Assignment) {
        if (statement.target.kind == frontend::ExpressionKind::Identifier) {
          if (const auto found = string_environment.find(statement.target.text);
              found != string_environment.end()) {
            const auto value = evaluate_source_string_expression(
                statement.value, string_environment);
            if (!value) {
              throw std::invalid_argument{
                  "class function string assignment expression is not "
                  "executable"};
            }
            found->second = *value;
            continue;
          }
        }
        const auto value = evaluate_constructor_expression(
            statement.value, handle, environment);
        if (!value) {
          throw std::invalid_argument{
              "class function assignment expression is not executable"};
        }
        if (statement.target.kind == frontend::ExpressionKind::Call
            && statement.target.text.starts_with(property_prefix)) {
          const auto identity = statement.target.text.substr(
              property_prefix.size());
          assign_property_value(
              class_heap.property(handle, identity), identity, *value);
          continue;
        }
        if (statement.target.kind == frontend::ExpressionKind::Call
            && statement.target.text.starts_with(static_property_prefix)) {
          const auto [owner, name] = static_property_parts(
              std::string_view{statement.target.text}.substr(
                  static_property_prefix.size()));
          assign_property_value(
              class_static_store.property(owner, name),
              statement.target.text.substr(static_property_prefix.size()),
              *value);
          continue;
        }
        if (statement.target.kind == frontend::ExpressionKind::Identifier) {
          const auto found = environment.find(statement.target.text);
          if (found != environment.end()) {
            found->second = resize_packed(*value, found->second.width());
            continue;
          }
        }
        throw std::invalid_argument{
            "class function assignment target is not executable"};
      }
      if (statement.kind == frontend::StatementKind::Return) {
        const auto value = evaluate_constructor_expression(
            statement.value, handle, environment);
        if (!value) {
          throw std::invalid_argument{
              "class function return expression is not executable"};
        }
        return {true, *value};
      }
      if (statement.kind == frontend::StatementKind::Block) {
        auto result = execute_source_function_statements(
            statement.statements, handle, environment, string_environment);
        if (result.returned) return result;
        continue;
      }
      if (statement.kind == frontend::StatementKind::If) {
        const auto condition = evaluate_constructor_expression(
            statement.condition, handle, environment);
        if (!condition || condition->low_word().bval != 0) {
          throw std::invalid_argument{
              "class function condition is not a known packed value"};
        }
        auto result = execute_source_function_statements(
            condition->low_word().aval != 0
                ? std::span<const frontend::Statement>{statement.statements}
                : std::span<const frontend::Statement>{statement.else_statements},
            handle,
            environment,
            string_environment);
        if (result.returned) return result;
        continue;
      }
      if (statement.kind != frontend::StatementKind::Null) {
        throw std::invalid_argument{
            "class function contains a non-executable statement"};
      }
    }
    return SourceFunctionResult{false, runtime::PackedLogic4{}};
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_source_profile(
      const frontend::SystemVerilogClassMethodProfile& method,
      const runtime::SystemVerilogClassHandle handle,
      std::vector<runtime::PackedLogic4>& actuals,
      std::vector<std::string>& string_actuals,
      const std::span<const std::string> actual_names,
      const std::span<const std::uint8_t> actual_directions) {
    constexpr std::size_t maximum_source_method_depth = 1024;
    if (source_method_depth_ >= maximum_source_method_depth) {
      throw std::overflow_error{
          "SystemVerilog class method recursion exhausted the guarded "
          "host call stack"};
    }
    ++source_method_depth_;
    struct DepthGuard {
      std::size_t& depth;
      ~DepthGuard() { --depth; }
    } depth_guard{source_method_depth_};

    if (method.kind != frontend::SystemVerilogClassMethodKind::Function) {
      throw std::invalid_argument{
          "source class function invocation selected non-function '"
          + method.canonical_identity + "'"};
    }
    if (string_actuals.size() != actuals.size()
        || (!actual_names.empty() && actual_names.size() != actuals.size())
        || (!actual_directions.empty()
            && actual_directions.size() != actuals.size())) {
      throw std::invalid_argument{
          "class function actual metadata does not align"};
    }
    std::vector<std::size_t> original_widths;
    original_widths.reserve(actuals.size());
    for (const auto& actual : actuals) {
      original_widths.push_back(actual.width());
    }
    std::vector<bool> assigned(method.arguments.size());
    std::vector<std::size_t> actual_formals;
    actual_formals.reserve(actuals.size());
    std::size_t next_positional{};
    for (std::size_t index = 0; index < actuals.size(); ++index) {
      const auto name = actual_names.empty()
          ? std::string_view{}
          : std::string_view{actual_names[index]};
      std::size_t formal{};
      if (name.empty()) {
        while (next_positional < assigned.size()
               && assigned[next_positional]) {
          ++next_positional;
        }
        formal = next_positional;
      } else {
        const auto found = std::ranges::find(
            method.arguments, name, &frontend::FunctionArgument::name);
        formal = found == method.arguments.end()
            ? method.arguments.size()
            : static_cast<std::size_t>(
                  std::distance(method.arguments.begin(), found));
      }
      if (formal >= method.arguments.size() || assigned[formal]) {
        throw std::invalid_argument{
            "class function actual does not select a unique formal"};
      }
      assigned[formal] = true;
      actual_formals.push_back(formal);
    }
    auto environment = bind_constructor_actuals(
        method, handle, actuals, actual_names);
    SourceStringEnvironment string_environment;
    for (std::size_t formal = 0; formal < method.arguments.size(); ++formal) {
      const auto& argument = method.arguments[formal];
      if (argument.type.domain != frontend::ValueDomain::String) continue;
      const auto actual = std::ranges::find(actual_formals, formal);
      if (actual != actual_formals.end()) {
        const auto index = static_cast<std::size_t>(
            std::distance(actual_formals.begin(), actual));
        string_environment.emplace(
            argument.name,
            argument.direction == frontend::PortDirection::Output
                ? std::string{}
                : string_actuals[index]);
        continue;
      }
      const auto value = argument.default_value
          ? evaluate_source_string_expression(
                *argument.default_value, string_environment)
          : std::nullopt;
      if (!value) {
        throw std::invalid_argument{
            "class function string formal '" + argument.name
            + "' has no executable actual or default"};
      }
      string_environment.emplace(argument.name, *value);
    }
    for (const auto& variable : method.variables) {
      if (variable.type.domain != frontend::ValueDomain::String) continue;
      const auto value = variable.initializer
          ? evaluate_source_string_expression(
                *variable.initializer, string_environment)
          : std::optional<std::string>{std::string{}};
      if (!value) {
        throw std::invalid_argument{
            "class function string local initializer for '" + variable.name
            + "' is not executable"};
      }
      string_environment[variable.name] = *value;
    }
    if (method.lifetime == frontend::SystemVerilogClassLifetime::Static) {
      auto& retained = source_static_locals_[method.canonical_identity];
      for (const auto& variable : method.variables) {
        if (const auto found = retained.find(variable.name);
            found != retained.end()) {
          environment[variable.name] = found->second;
        } else {
          retained[variable.name] = environment.at(variable.name);
        }
      }
      auto& retained_strings =
          source_static_string_locals_[method.canonical_identity];
      for (const auto& variable : method.variables) {
        if (variable.type.domain != frontend::ValueDomain::String) continue;
        if (const auto found = retained_strings.find(variable.name);
            found != retained_strings.end()) {
          string_environment[variable.name] = found->second;
        } else {
          retained_strings[variable.name] = string_environment.at(variable.name);
        }
      }
    }
    const auto result = execute_source_function_statements(
        method.statements, handle, environment, string_environment);
    const auto void_result = method.return_type.spelling == "void";
    if (!result.returned && !void_result) {
      throw std::invalid_argument{
          "class function completed without returning a value"};
    }
    if (method.lifetime == frontend::SystemVerilogClassLifetime::Static) {
      auto& retained = source_static_locals_.at(method.canonical_identity);
      for (const auto& variable : method.variables) {
        retained[variable.name] = environment.at(variable.name);
      }
      auto& retained_strings =
          source_static_string_locals_.at(method.canonical_identity);
      for (const auto& variable : method.variables) {
        if (variable.type.domain == frontend::ValueDomain::String) {
          retained_strings[variable.name] =
              string_environment.at(variable.name);
        }
      }
    }

    for (std::size_t index = 0; index < actuals.size(); ++index) {
      const auto formal = actual_formals[index];
      const auto direction = method.arguments[formal].direction;
      if (!actual_directions.empty()
          && actual_directions[index]
              != static_cast<std::uint8_t>(direction)) {
        throw std::invalid_argument{
            "class function actual direction metadata is inconsistent"};
      }
      if (direction != frontend::PortDirection::Input) {
        if (method.arguments[formal].type.domain
            == frontend::ValueDomain::String) {
          string_actuals[index] =
              string_environment.at(method.arguments[formal].name);
        } else {
          actuals[index] = resize_packed(
              environment.at(method.arguments[formal].name),
              original_widths[index]);
        }
      }
    }
    if (void_result) return runtime::PackedLogic4{};
    const auto width = method.return_type.width();
    if (!width || *width == 0
        || *width > std::numeric_limits<std::size_t>::max()) {
      throw std::invalid_argument{
          "class function return type has no executable packed width"};
    }
    return resize_packed(
        result.value, static_cast<std::size_t>(*width));
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_source_profile(
      const frontend::SystemVerilogClassMethodProfile& method,
      const runtime::SystemVerilogClassHandle handle,
      std::vector<runtime::PackedLogic4>& actuals,
      const std::span<const std::string> actual_names,
      const std::span<const std::uint8_t> actual_directions) {
    std::vector<std::string> string_actuals(actuals.size());
    return invoke_source_profile(
        method, handle, actuals, string_actuals, actual_names,
        actual_directions);
  }

  void invoke_source_task_profile(
      const frontend::SystemVerilogClassMethodProfile& method,
      const runtime::SystemVerilogClassHandle handle,
      std::vector<runtime::PackedLogic4>& actuals) {
    constexpr std::size_t maximum_source_method_depth = 1024;
    if (source_method_depth_ >= maximum_source_method_depth) {
      throw std::overflow_error{
          "SystemVerilog class task recursion exhausted the guarded host "
          "call stack"};
    }
    ++source_method_depth_;
    struct DepthGuard {
      std::size_t& depth;
      ~DepthGuard() { --depth; }
    } depth_guard{source_method_depth_};
    if (method.kind != frontend::SystemVerilogClassMethodKind::Task
        || method.is_static || method.arguments.size() != actuals.size()
        || std::ranges::any_of(method.arguments, [](const auto& argument) {
             return argument.direction != frontend::PortDirection::Input;
           })) {
      throw std::invalid_argument{
          "UVM phase task requires a nonstatic input-only task profile"};
    }
    auto environment = bind_constructor_actuals(
        method, handle, actuals, {});
    SourceStringEnvironment string_environment;
    for (const auto& variable : method.variables) {
      if (variable.type.domain != frontend::ValueDomain::String) continue;
      const auto value = variable.initializer
          ? evaluate_source_string_expression(
                *variable.initializer, string_environment)
          : std::optional<std::string>{std::string{}};
      if (!value) {
        throw std::invalid_argument{
            "class task string local initializer for '" + variable.name
            + "' is not executable"};
      }
      string_environment[variable.name] = *value;
    }
    if (method.lifetime == frontend::SystemVerilogClassLifetime::Static) {
      auto& retained = source_static_locals_[method.canonical_identity];
      for (const auto& variable : method.variables) {
        if (const auto found = retained.find(variable.name);
            found != retained.end()) {
          environment[variable.name] = found->second;
        } else {
          retained[variable.name] = environment.at(variable.name);
        }
      }
    }
    const auto result = execute_source_function_statements(
        method.statements, handle, environment, string_environment);
    if (result.returned) {
      throw std::invalid_argument{"SystemVerilog class task returned a value"};
    }
    if (method.lifetime == frontend::SystemVerilogClassLifetime::Static) {
      auto& retained = source_static_locals_.at(method.canonical_identity);
      for (const auto& variable : method.variables) {
        retained[variable.name] = environment.at(variable.name);
      }
    }
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_source_function(
      const runtime::SystemVerilogClassHandle handle,
      const std::string_view canonical_identity,
      std::vector<runtime::PackedLogic4>& actuals,
      std::vector<std::string>& string_actuals,
      const std::span<const std::string> actual_names,
      const std::span<const std::uint8_t> actual_directions,
      const bool virtual_dispatch) {
    const auto method_separator = canonical_identity.rfind("::");
    const auto method_name = method_separator == std::string_view::npos
        ? canonical_identity
        : canonical_identity.substr(method_separator + 2U);
    if (method_name == "uvm_report_info"
        && actuals.size() >= 3U && string_actuals.size() >= 3U) {
      runtime::SystemVerilogUvmReportRequest request;
      request.report_object = handle;
      request.severity = runtime::SystemVerilogUvmReportSeverity::Info;
      request.id = string_actuals[0];
      request.message = string_actuals[1];
      const auto verbosity = actuals[2].low_word();
      if (verbosity.bval != 0) {
        throw std::invalid_argument{
            "UVM report verbosity must be a known packed value"};
      }
      request.verbosity = static_cast<std::int32_t>(verbosity.aval);
      if (actuals.size() >= 4U && string_actuals.size() >= 4U) {
        request.filename = string_actuals[3];
      }
      if (actuals.size() >= 5U) {
        request.line = static_cast<std::uint32_t>(
            actuals[4].low_word().aval);
      }
      request.timestamp = interpreter->scheduler().now();
      (void)uvm_reports.report(request);
      return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    if (auto factory_result = invoke_systemverilog_uvm_factory_method(
            built.systemverilog_class_specializations,
            class_heap,
            uvm_factory,
            handle,
            canonical_identity,
            actuals)) {
      return std::move(*factory_result);
    }
    const auto uvm_base_method =
        canonical_identity.find("::uvm_object::") != std::string_view::npos;
    if (uvm_base_method && uvm_objects.contains(handle)) {
      if (method_name == "get_inst_id" && actuals.empty()) {
        return runtime::PackedLogic4::from_aval_bval(
            32, uvm_objects.instance_id(handle), 0);
      }
      if (method_name == "clone" && actuals.empty()) {
        if (uvm_components.contains(handle)) {
          return runtime::PackedLogic4::from_aval_bval(64, 0, 0);
        }
        return runtime::PackedLogic4::from_aval_bval(
            64, uvm_objects.clone(handle), 0);
      }
      if (method_name == "copy" && actuals.size() == 1U) {
        uvm_objects.copy(handle, actuals.front().low_word().aval);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
      }
      if (method_name == "compare" && !actuals.empty()) {
        return runtime::PackedLogic4::from_aval_bval(
            1,
            uvm_objects.compare(handle, actuals.front().low_word().aval)
                ? 1U : 0U,
            0);
      }
      if (method_name == "print") {
        (void)uvm_objects.print(handle);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
      }
      if (method_name == "record") {
        (void)uvm_objects.record(handle);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
      }
    }
    const auto uvm_component_base_method =
        canonical_identity.find("::uvm_component::")
        != std::string_view::npos;
    if (uvm_component_base_method && uvm_components.contains(handle)) {
      if (method_name == "get_parent" && actuals.empty()) {
        return runtime::PackedLogic4::from_aval_bval(
            64, uvm_components.parent(handle), 0);
      }
      if (method_name == "get_num_children" && actuals.empty()) {
        return runtime::PackedLogic4::from_aval_bval(
            32, uvm_components.children(handle).size(), 0);
      }
    }
    if (uvm_objects.contains(handle)) {
      if (method_name == "get_object_type" && actuals.empty()) {
        const auto wrapper = uvm_registry.wrapper_by_specialization(
            class_heap.object(handle).specialization_identity);
        if (wrapper != 0) {
          return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
        }
      }
    }
    const auto& method = source_method(
        handle, canonical_identity, virtual_dispatch);
    if (method.is_static) {
      throw std::invalid_argument{
          "instance class call selected a static function"};
    }
    return invoke_source_profile(
        method, handle, actuals, string_actuals, actual_names,
        actual_directions);
  }

  [[nodiscard]] const frontend::SystemVerilogClassMethodProfile&
  source_static_method(const std::string_view canonical_identity) const {
    const frontend::SystemVerilogClassMethodProfile* selected{};
    for (const auto& specialization :
         built.systemverilog_class_specializations) {
      const auto method = std::ranges::find(
          specialization.methods,
          canonical_identity,
          &frontend::SystemVerilogClassMethodProfile::canonical_identity);
      if (method == specialization.methods.end()) continue;
      if (selected != nullptr && selected != &*method) {
        throw std::invalid_argument{
            "class static method requires an unambiguous specialization"};
      }
      selected = &*method;
    }
    if (selected == nullptr || !selected->is_static || selected->is_pure) {
      throw std::invalid_argument{
          "class static method is not executable"};
    }
    return *selected;
  }

  [[nodiscard]] runtime::PackedLogic4 invoke_source_static_function(
      const std::string_view canonical_identity,
      std::vector<runtime::PackedLogic4>& actuals,
      std::vector<std::string>& string_actuals,
      const std::span<const std::string> actual_names,
      const std::span<const std::uint8_t> actual_directions) {
    constexpr std::string_view registry_create_prefix{
        "@uvm-registry-create:"};
    if (canonical_identity.starts_with(registry_create_prefix)) {
      if (actuals.empty() || string_actuals.empty()) {
        throw std::invalid_argument{
            "UVM registry create requires a string name actual"};
      }
      const auto wrapper = uvm_registry.unique_wrapper_by_declaration(
          canonical_identity.substr(registry_create_prefix.size()));
      if (wrapper == 0) {
        throw std::invalid_argument{
            "UVM registry create selected an unknown object type"};
      }
      return runtime::PackedLogic4::from_aval_bval(
          64,
          uvm_registry.create_object_by_type(
              wrapper, string_actuals.front()),
          0);
    }
    constexpr std::string_view config_set_prefix{"@uvm-config-db-set:"};
    constexpr std::string_view config_get_prefix{"@uvm-config-db-get:"};
    const auto config_set = canonical_identity.starts_with(config_set_prefix);
    const auto config_get = canonical_identity.starts_with(config_get_prefix);
    if (config_set || config_get) {
      if (actuals.size() != 4U || string_actuals.size() != 4U) {
        throw std::invalid_argument{
            "UVM config_db call requires context, instance, field, and value"};
      }
      const auto prefix = config_set ? config_set_prefix : config_get_prefix;
      const auto identity = canonical_identity.substr(prefix.size());
      runtime::SystemVerilogUvmResourceType type;
      type.identity = identity;
      if (identity == "string") {
        type.kind = runtime::SystemVerilogUvmResourceValueKind::String;
      } else if (identity.starts_with("class:")) {
        type.kind = runtime::SystemVerilogUvmResourceValueKind::Object;
        type.packed_width = 64U;
      } else {
        type.kind = runtime::SystemVerilogUvmResourceValueKind::Packed;
        const auto separator = identity.rfind(':');
        if (separator != std::string_view::npos) {
          std::from_chars(
              identity.data() + separator + 1U,
              identity.data() + identity.size(),
              type.packed_width);
        }
        if (type.packed_width == 0U) {
          type.packed_width = actuals[3].width();
        }
      }
      runtime::SystemVerilogUvmConfigContext context;
      const auto context_word = actuals[0].low_word();
      if (context_word.bval != 0) {
        throw std::invalid_argument{
            "UVM config_db context must be a known class handle"};
      }
      if (context_word.aval != 0 && uvm_components.contains(context_word.aval)) {
        const auto component = uvm_components.snapshot(context_word.aval);
        context.full_name = component.full_name;
        context.depth = component.depth;
      }
      if (config_set) {
        runtime::SystemVerilogUvmResourceValue value;
        if (type.kind == runtime::SystemVerilogUvmResourceValueKind::String) {
          value = string_actuals[3];
        } else if (
            type.kind == runtime::SystemVerilogUvmResourceValueKind::Object) {
          value = static_cast<runtime::SystemVerilogClassHandle>(
              actuals[3].low_word().aval);
        } else {
          value = resize_packed(actuals[3], type.packed_width);
        }
        (void)uvm_config_db.set(
            context, string_actuals[1], string_actuals[2],
            std::move(type), std::move(value),
            runtime::SystemVerilogUvmConfigPhase::Runtime);
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
      }
      const auto value = uvm_config_db.get(
          context, string_actuals[1], string_actuals[2], identity);
      if (!value) {
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
      }
      if (const auto packed = std::get_if<runtime::PackedLogic4>(&*value)) {
        actuals[3] = *packed;
      } else if (const auto text = std::get_if<std::string>(&*value)) {
        string_actuals[3] = *text;
      } else if (const auto object =
                     std::get_if<runtime::SystemVerilogClassHandle>(&*value)) {
        actuals[3] = runtime::PackedLogic4::from_aval_bval(64, *object, 0);
      }
      return runtime::PackedLogic4::from_aval_bval(1, 1, 0);
    }
    const auto method_separator = canonical_identity.rfind("::");
    const auto method_name = method_separator == std::string_view::npos
        ? canonical_identity
        : canonical_identity.substr(method_separator + 2U);
    if (method_name == "get_type" && actuals.empty()
        && method_separator != std::string_view::npos) {
      const auto wrapper = uvm_registry.unique_wrapper_by_declaration(
          canonical_identity.substr(0, method_separator));
      if (wrapper != 0) {
        return runtime::PackedLogic4::from_aval_bval(64, wrapper, 0);
      }
    }
    return invoke_source_profile(
        source_static_method(canonical_identity),
        0,
        actuals,
        string_actuals,
        actual_names,
        actual_directions);
  }

  void invoke_source_constructor(
      const frontend::SystemVerilogClassSpecialization& specialization,
      const runtime::SystemVerilogClassHandle handle,
      const std::span<const runtime::PackedLogic4> actuals,
      const std::span<const std::string> actual_names) {
    // The unmodified UVM library constructors bootstrap their own singleton
    // scheduler, report server, resource pool, and component hierarchy. Those
    // facilities are simulation-owned services in fsim, so executing the
    // upstream constructor bodies would duplicate state and recursively pull
    // the complete reference scheduler into each source allocation. The host
    // services initialize the corresponding object/component state after the
    // user-derived constructor returns.
    if (specialization.declaration_identity.find("uvm_pkg::")
        != std::string::npos) {
      return;
    }
    const auto constructor = std::ranges::find_if(
        specialization.methods,
        [](const auto& method) {
          return method.kind
              == frontend::SystemVerilogClassMethodKind::Constructor;
        });
    ConstructorEnvironment environment;
    if (constructor != specialization.methods.end()) {
      environment = bind_constructor_actuals(
          *constructor, handle, actuals, actual_names);
    } else if (!actuals.empty()) {
      throw std::invalid_argument{
          "implicit constructor does not accept actuals"};
    }

    std::vector<runtime::PackedLogic4> base_actuals;
    std::vector<std::string> base_actual_names;
    if (constructor != specialization.methods.end()) {
      constexpr std::string_view base_prefix{"@sv-base-constructor:"};
      const auto base_call = std::ranges::find_if(
          constructor->statements,
          [&](const auto& statement) {
            return statement.kind == frontend::StatementKind::TaskCall
                && statement.task_name.starts_with(base_prefix);
          });
      if (base_call != constructor->statements.end()) {
        const auto first_actual = !base_call->task_arguments.empty()
            && base_call->task_arguments.front().text == "super"
            ? std::size_t{1}
            : std::size_t{0};
        for (std::size_t index = first_actual;
             index < base_call->task_arguments.size(); ++index) {
          const auto value = evaluate_constructor_expression(
              base_call->task_arguments[index], handle, environment);
          if (!value) {
            throw std::invalid_argument{
                "base-constructor actual is not executable"};
          }
          base_actuals.push_back(*value);
          base_actual_names.push_back(
              index < base_call->task_argument_names.size()
                  ? base_call->task_argument_names[index]
                  : std::string{});
        }
      }
    }
    if (!specialization.base_specialization_identity.empty()) {
      invoke_source_constructor(
          class_specialization(specialization.base_specialization_identity),
          handle,
          base_actuals,
          base_actual_names);
    }
    if (constructor != specialization.methods.end()) {
      execute_constructor_statements(
          constructor->statements, handle, environment,
          specialization.declaration_identity.find("uvm_pkg::")
              != std::string::npos);
    }
  }
