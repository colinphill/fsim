// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;



    void HierarchyBuilder::resolve_named_types(
        DesignUnit& unit,
        const NamedTypeEnvironment& imported_types,
        const bool vhdl,
        const bool resolve_ports) {
        std::unordered_map<std::string, std::size_t> local_types;
        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            local_types.emplace(
                unit.type_aliases[index].name, index);
        }
        std::vector<unsigned char> states(
            unit.type_aliases.size(), 0);
        std::function<bool(frontend::Type&)> resolve_type;
        std::function<bool(std::size_t)> resolve_alias;
        const auto simple_type_name =
            [](const std::string_view spelling) {
                const auto separator =
                    spelling.find_last_of('.');
                return std::string{
                    spelling.substr(
                        separator == std::string_view::npos
                            ? 0
                            : separator + 1)};
            };
        const auto is_packed_array_type =
            [&](const frontend::Type& type) {
                const auto name =
                    simple_type_name(type.spelling);
                return type.vhdl_array.has_value()
                    || name == "bit_vector"
                    || name == "std_logic_vector"
                    || name == "std_ulogic_vector"
                    || name == "signed"
                    || name == "unsigned";
            };
        const auto constraint_span =
            [](const frontend::Type& type) {
                if (type.discrete_range_expression) {
                    return type.discrete_range_expression->span;
                }
                if (type.enumeration_range_expression) {
                    return type.enumeration_range_expression->span;
                }
                if (type.integer_range_expression) {
                    return type.integer_range_expression->span;
                }
                if (type.packed_range_expression) {
                    return type.packed_range_expression->span;
                }
                return type.named_type_span;
            };
        const auto validate_direct_constraints =
            [&](const frontend::Type& type) {
                bool valid = true;
                if (type.discrete_range_expression) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-001",
                        "a VHDL discrete range constraint requires a "
                        "named integer-family or enumeration base subtype",
                        constraint_span(type));
                    valid = false;
                }
                if (type.integer_range_expression
                    && type.domain
                        != frontend::ValueDomain::Integer) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-001",
                        "a VHDL range constraint requires an "
                        "integer-family base subtype",
                        constraint_span(type));
                    valid = false;
                }
                if (type.packed_range_expression
                    && (!is_packed_array_type(type)
                        || !type.packed_members.empty())) {
                    report(
                        "FSIM-ELAB-VHSUBTYPE-003",
                        "a VHDL packed index constraint requires an "
                        "unconstrained one-dimensional packed-array base",
                        constraint_span(type));
                    valid = false;
                }
                return valid;
            };
        const auto apply_derived_constraints =
            [&](frontend::Type base,
                const frontend::Type& derived)
                -> std::optional<frontend::Type> {
                const bool has_integer_constraint =
                    derived.integer_range_expression.has_value();
                const bool has_discrete_constraint =
                    derived.discrete_range_expression.has_value();
                const bool has_packed_constraint =
                    derived.packed_range_expression.has_value();
                if (has_integer_constraint) {
                    if (base.domain
                        != frontend::ValueDomain::Integer) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-001",
                            "a derived VHDL range constraint requires an "
                            "integer-family base subtype",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.integer_base_range =
                        base.integer_range;
                    base.integer_base_range_expression =
                        base.integer_range_expression;
                    base.integer_range =
                        derived.integer_range;
                    base.integer_range_expression =
                        derived.integer_range_expression;
                }
                if (has_discrete_constraint) {
                    const auto& range =
                        *derived.discrete_range_expression;
                    if (base.domain
                        == frontend::ValueDomain::Integer) {
                        base.integer_base_range =
                            base.integer_range;
                        base.integer_base_range_expression =
                            base.integer_range_expression;
                        base.integer_range.reset();
                        base.integer_range_expression =
                            frontend::IntegerRangeExpression{
                                range.left,
                                range.right,
                                range.span,
                                range.descending};
                    } else if (
                        !base.enumeration_literals.empty()) {
                        base.enumeration_base_range =
                            base.enumeration_range;
                        base.enumeration_base_range_expression =
                            base.enumeration_range_expression;
                        base.enumeration_range.reset();
                        base.enumeration_range_expression =
                            range;
                    } else {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-001",
                            "a derived VHDL discrete range constraint "
                            "requires an integer-family or enumeration "
                            "base subtype",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.discrete_range_expression.reset();
                }
                if (has_packed_constraint) {
                    if (!is_packed_array_type(base)
                        || !base.packed_members.empty()) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-003",
                            "a derived VHDL packed index constraint "
                            "requires an unconstrained one-dimensional "
                            "packed-array base",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    if (base.packed_range
                        || base.packed_range_expression) {
                        report(
                            "FSIM-ELAB-VHSUBTYPE-004",
                            "a constrained VHDL packed-array subtype cannot "
                            "be constrained again",
                            constraint_span(derived));
                        return std::nullopt;
                    }
                    base.packed_range =
                        derived.packed_range;
                    base.packed_range_expression =
                        derived.packed_range_expression;
                    if (base.vhdl_array) {
                        base.vhdl_array->unconstrained = false;
                    }
                }
                return base;
            };
        resolve_alias = [&](const std::size_t index) {
            if (states[index] == 2) {
                return true;
            }
            if (states[index] == 3) {
                return false;
            }
            if (states[index] == 1) {
                report(
                    vhdl
                        ? "FSIM-ELAB-VHTYPE-002"
                        : "FSIM-ELAB-SVTYPE-003",
                    std::string{
                        vhdl
                            ? "cyclic VHDL type declaration involving '"
                            : "cyclic SystemVerilog typedef involving '"}
                        + unit.type_aliases[index].name + "'",
                    unit.type_aliases[index].span);
                return false;
            }
            states[index] = 1;
            const bool resolved =
                resolve_type(unit.type_aliases[index].type);
            states[index] = resolved ? 2 : 3;
            return resolved;
        };
        resolve_type = [&](frontend::Type& type) {
            if (type.vhdl_array
                && !type.vhdl_array->element_named_type.empty()) {
                frontend::Type element;
                element.spelling =
                    type.vhdl_array->element_spelling;
                element.named_type =
                    type.vhdl_array->element_named_type;
                element.named_type_span =
                    type.vhdl_array->element_span;
                if (!resolve_type(element)) {
                    return false;
                }
                const auto width = element.width();
                if (!width || *width != 1
                    || element.vhdl_array
                    || !element.packed_members.empty()
                    || !element.enumeration_literals.empty()
                    || element.domain
                        == frontend::ValueDomain::Integer
                    || element.domain
                        == frontend::ValueDomain::Unknown) {
                    report(
                        "FSIM-ELAB-VHARRAY-001",
                        "VHDL array type '" + type.spelling
                            + "' requires a resolved scalar bit, Boolean, "
                              "std_logic, or std_ulogic element subtype",
                        type.vhdl_array->element_span);
                    return false;
                }
                type.vhdl_array->element_domain =
                    element.domain;
                type.vhdl_array->element_spelling =
                    element.spelling;
                type.vhdl_array->element_named_type.clear();
                type.domain = element.domain;
            }
            if (type.named_type.empty()) {
                return !vhdl
                    || validate_direct_constraints(type);
            }
            const auto name = type.named_type;
            const auto use_span = type.named_type_span;
            const auto derived = type;
            std::optional<frontend::Type> base;
            if (name.find("::") == std::string::npos) {
                if (const auto local = local_types.find(name);
                    local != local_types.end()) {
                    if (!resolve_alias(local->second)) {
                        return false;
                    }
                    base =
                        unit.type_aliases[local->second].type;
                }
            }
            if (!base) {
                const auto imported =
                    imported_types.find(name);
                if (imported == imported_types.end()) {
                    report(
                        vhdl
                            ? "FSIM-ELAB-VHTYPE-001"
                            : "FSIM-ELAB-SVTYPE-001",
                        std::string{
                            vhdl
                                ? "VHDL type '"
                                : "SystemVerilog type alias '"}
                            + name + "' is not visible in this unit",
                        use_span);
                    return false;
                }
                base = imported->second.type;
            }
            if (!vhdl) {
                type = std::move(*base);
                return true;
            }
            auto constrained =
                apply_derived_constraints(
                    std::move(*base), derived);
            if (!constrained) {
                return false;
            }
            type = std::move(*constrained);
            return true;
        };

        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            (void)resolve_alias(index);
        }

        const auto resolve_declaration =
            [&](auto& declaration) {
                (void)resolve_type(declaration.type);
            };
        std::function<void(std::vector<Statement>&)>
            resolve_statements;
        resolve_statements =
            [&](std::vector<Statement>& statements) {
                for (auto& statement : statements) {
                    for (auto& declaration :
                         statement.declarations) {
                        resolve_declaration(declaration);
                    }
                    resolve_statements(statement.statements);
                    resolve_statements(
                        statement.else_statements);
                    for (auto& alternative :
                         statement.case_alternatives) {
                        resolve_statements(
                            alternative.statements);
                    }
                }
            };
        std::function<void(frontend::GenerateBody&)>
            resolve_generate_body;
        std::function<void(
            std::vector<frontend::GenerateRegion>&)>
            resolve_generate_regions;
        resolve_generate_body =
            [&](frontend::GenerateBody& body) {
                for (auto& constant : body.constants) {
                    resolve_declaration(constant);
                }
                for (auto& signal : body.signals) {
                    resolve_declaration(signal);
                }
                for (auto& process : body.processes) {
                    for (auto& variable : process.variables) {
                        resolve_declaration(variable);
                    }
                    resolve_statements(process.statements);
                }
                resolve_generate_regions(
                    body.generate_regions);
            };
        resolve_generate_regions =
            [&](std::vector<frontend::GenerateRegion>&
                    regions) {
                for (auto& region : regions) {
                    resolve_generate_body(region.then_body);
                    resolve_generate_body(region.else_body);
                    for (auto& alternative :
                         region.alternatives) {
                        resolve_generate_body(
                            alternative.body);
                    }
                }
            };

        for (auto& parameter : unit.parameters) {
            resolve_declaration(parameter);
        }
        if (resolve_ports) {
            for (auto& port : unit.ports) {
                resolve_declaration(port);
            }
        }
        for (auto& signal : unit.signals) {
            resolve_declaration(signal);
        }
        for (auto& process : unit.processes) {
            for (auto& variable : process.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(process.statements);
        }
        resolve_generate_regions(unit.generate_regions);
    }



    DesignUnit HierarchyBuilder::effective_unit(const DesignUnit& selected) {
        auto result = selected;
        if (selected.kind
            == frontend::UnitKind::VerilogModule) {
            std::vector<const DesignUnit*> import_stack;
            NamedTypeEnvironment type_environment;
            import_systemverilog_package_items(
                result, import_stack, type_environment);
            import_qualified_systemverilog_package_items(
                result, import_stack, type_environment);
            resolve_named_types(
                result, type_environment);
            return result;
        }
        if (selected.kind
            != frontend::UnitKind::VhdlArchitecture) {
            return result;
        }
        const auto* entity = find_vhdl_entity(parsed_, selected);
        if (entity == nullptr) {
            return result;
        }
        result.parameters = entity->parameters;
        result.ports = entity->ports;
        for (const auto& generic : result.parameters) {
            if (std::any_of(
                    result.signals.begin(),
                    result.signals.end(),
                    [&](const frontend::SignalDeclaration& signal) {
                        return signal.name == generic.name;
                    })) {
                report(
                    "FSIM-ELAB-GENERIC-009",
                    "architecture object '" + generic.name
                        + "' conflicts with an entity generic",
                    generic.span);
            }
        }
        std::vector<frontend::VhdlContextItem> context =
            entity->vhdl_context;
        context.insert(
            context.end(),
            selected.vhdl_context.begin(),
            selected.vhdl_context.end());
        std::vector<frontend::VhdlContextItem> expanded_context;
        std::vector<const DesignUnit*> context_stack;
        const auto unit_library =
            result.library.empty()
                ? std::string{"work"}
                : result.library;
        expand_vhdl_context_references(
            result,
            context,
            expanded_context,
            context_stack,
            unit_library);
        std::vector<const DesignUnit*> import_stack;
        NamedTypeEnvironment type_environment;
        import_vhdl_package_constants(
            result,
            expanded_context,
            import_stack,
            type_environment);
        import_qualified_vhdl_package_constants(
            result, import_stack);
        import_qualified_vhdl_package_types(
            result, type_environment, import_stack);

        // Entity interfaces have their own declarative region. Resolve them
        // without exposing architecture-local type declarations, then merge
        // the typed ports back into the architecture specialization.
        // Generic and port clauses precede the entity declarative part in
        // VHDL. Resolve the interface without entity-local type declarations,
        // then resolve those declarations separately for architecture
        // visibility.
        auto effective_interface = *entity;
        effective_interface.type_aliases.clear();
        resolve_named_types(
            effective_interface, type_environment, true);
        for (const auto& generic :
             effective_interface.parameters) {
            const auto resolved = std::find_if(
                result.parameters.begin(),
                result.parameters.end(),
                [&](const auto& candidate) {
                    return candidate.name == generic.name
                        && candidate.span.source_name
                            == generic.span.source_name
                        && candidate.span.begin.offset
                            == generic.span.begin.offset;
                });
            if (resolved != result.parameters.end()) {
                resolved->type = generic.type;
            }
            if ((generic.type.packed_range
                 && generic.type.enumeration_literals.empty())
                || !generic.type.packed_members.empty()
                || (generic.type.domain
                    != frontend::ValueDomain::Integer
                && generic.type.domain
                    != frontend::ValueDomain::Boolean
                && generic.type.domain
                    != frontend::ValueDomain::Bit2)) {
                report(
                    "FSIM-ELAB-GENERIC-010",
                    "a bounded VHDL generic subtype must resolve to scalar "
                    "integer, Boolean, or bit",
                    generic.span);
            }
        }
        result.ports = std::move(effective_interface.ports);
        auto effective_entity_declarations = *entity;
        effective_entity_declarations.parameters.clear();
        effective_entity_declarations.ports.clear();
        resolve_named_types(
            effective_entity_declarations,
            type_environment,
            true,
            false);
        for (const auto& alias :
             effective_entity_declarations.type_aliases) {
            type_environment.insert_or_assign(
                alias.name,
                NamedTypeBinding{
                    alias.type,
                    (entity->library.empty()
                         ? std::string{"work"}
                         : entity->library)
                        + "." + entity->name});
        }
        resolve_named_types(
            result, type_environment, true, false);
        for (const auto& [name, binding] : type_environment) {
            if ((binding.type.enumeration_literals.empty()
                 && !binding.type.vhdl_array)
                || std::any_of(
                    result.type_aliases.begin(),
                    result.type_aliases.end(),
                    [&](const auto& alias) {
                        return alias.name == name;
                    })) {
                continue;
            }
            result.type_aliases.push_back(
                frontend::TypeAliasDeclaration{
                    name,
                    binding.type,
                    result.span,
                    {},
                    frontend::TypeDeclarationKind::Alias});
        }
        return result;
    }



    SpecializedUnit HierarchyBuilder::specialize_selected_unit(
        const DesignUnit& selected,
        const std::vector<frontend::ParameterOverride>& overrides,
        const ConstantEnvironment& parent_environment,
        const frontend::Language association_language) {
        return specialize_unit(
            effective_unit(selected),
            overrides,
            parent_environment,
            association_language,
            diagnostics_);
    }



    void HierarchyBuilder::finish() {
        validate_process_drivers();
        for (const auto& [path, binding] : bindings_) {
            (void)binding;
            if (!used_bindings_.contains(path)) {
                report(
                    "FSIM-ELAB-BIND-011",
                    "binding instance path '" + path
                        + "' was not found in the elaborated hierarchy",
                    {});
            }
        }
        for (const auto& [path, instance] : systemc_instances_) {
            (void)instance;
            if (!used_systemc_instances_.contains(path)) {
                report(
                    "FSIM-ELAB-BIND-033",
                    "constructed SystemC instance path '" + path
                        + "' was not reached from the elaborated hierarchy",
                    {});
            }
        }
    }



    ResolutionKind HierarchyBuilder::native_resolution(
        const SignalInfo& signal) {
        if (signal.type_name == "std_logic"
            || signal.type_name == "std_logic_vector") {
            return ResolutionKind::std_logic;
        }
        if (signal.type_name == "wire"
            || signal.type_name == "tri") {
            return ResolutionKind::sv_wire;
        }
        return ResolutionKind::none;
    }



    std::optional<ResolutionKind> HierarchyBuilder::explicit_resolution(
        const SignalId signal) {
        const auto found = resolver_by_signal_.find(signal);
        if (found == resolver_by_signal_.end()) {
            return std::nullopt;
        }
        if (found->second == "std_logic") {
            return ResolutionKind::std_logic;
        }
        if (found->second == "sv_wire") {
            return ResolutionKind::sv_wire;
        }
        report(
            "FSIM-ELAB-BIND-050",
            "unknown resolver '" + found->second
                + "'; expected \"std_logic\" or \"sv_wire\"",
            {});
        return ResolutionKind::none;
    }



    void HierarchyBuilder::set_resolution(
        const SignalId signal,
        const ResolutionKind resolution) {
        design_.signal_info_.at(signal).resolution = resolution;
        design_.signals_.at(signal).resolution = resolution;
    }



    void HierarchyBuilder::validate_process_drivers() {
        std::unordered_map<SignalId, std::vector<ProcessId>> drivers;
        for (const auto& process : design_.processes_) {
            std::set<SignalId> process_outputs;
            for (const auto& operation : process.operations) {
                if (const auto* blocking =
                        std::get_if<WriteBlocking>(&operation)) {
                    process_outputs.insert(blocking->signal);
                } else if (const auto* update =
                               std::get_if<WriteUpdate>(&operation)) {
                    process_outputs.insert(update->signal);
                } else if (const auto* delayed =
                               std::get_if<WriteAfter>(&operation)) {
                    process_outputs.insert(delayed->signal);
                } else if (const auto* inertial =
                               std::get_if<WriteInertial>(&operation)) {
                    process_outputs.insert(inertial->signal);
                } else if (const auto* projected =
                               std::get_if<WriteProjected>(&operation)) {
                    process_outputs.insert(projected->signal);
                } else if (const auto* waveform =
                               std::get_if<WriteProjectedWaveform>(
                                   &operation)) {
                    process_outputs.insert(waveform->signal);
                } else if (const auto* blocking_slice =
                               std::get_if<WriteBlockingSlice>(
                                   &operation)) {
                    process_outputs.insert(blocking_slice->signal);
                } else if (const auto* update_slice =
                               std::get_if<WriteUpdateSlice>(
                                   &operation)) {
                    process_outputs.insert(update_slice->signal);
                } else if (const auto* delayed_slice =
                               std::get_if<WriteAfterSlice>(
                                   &operation)) {
                    process_outputs.insert(delayed_slice->signal);
                } else if (const auto* inertial_slice =
                               std::get_if<WriteInertialSlice>(
                                   &operation)) {
                    process_outputs.insert(inertial_slice->signal);
                } else if (const auto* projected_slice =
                               std::get_if<WriteProjectedSlice>(
                                   &operation)) {
                    process_outputs.insert(projected_slice->signal);
                } else if (const auto* waveform_slice =
                               std::get_if<WriteProjectedWaveformSlice>(
                                   &operation)) {
                    process_outputs.insert(waveform_slice->signal);
                }
            }
            for (const auto signal : process_outputs) {
                drivers[signal].push_back(process.id);
            }
        }
        for (SignalId signal = 0;
             signal < design_.signal_info_.size();
             ++signal) {
            const auto selected = explicit_resolution(signal);
            set_resolution(
                signal,
                selected.value_or(
                    native_resolution(
                        design_.signal_info_.at(signal))));
        }
        for (const auto& [signal, processes] : drivers) {
            if (processes.size() <= 1
                || design_.signal_info_.at(signal).resolution
                    != ResolutionKind::none) {
                continue;
            }
            const auto& info = design_.signal_info_.at(signal);
            if (info.type_name == "wand"
                || info.type_name == "triand"
                || info.type_name == "wor"
                || info.type_name == "trior") {
                report(
                    "FSIM-ELAB-DRV-002",
                    "wired-AND/OR resolution for signal '"
                        + info.name + "' is not implemented",
                    {});
                continue;
            }
            report(
                "FSIM-ELAB-DRV-001",
                "unresolved variable '" + info.name
                    + "' has multiple process drivers",
                {});
        }
    }



    std::optional<SignalId> HierarchyBuilder::add_owned_signal(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        SignalMap& local) {
        if (const auto existing = local.find(declaration.name);
            existing != local.end()) {
            return existing->second;
        }
        if (declaration.type.domain == frontend::ValueDomain::Unknown) {
            report(
                "FSIM-ELAB-TYPE-001",
                "signal '" + declaration.name
                    + "' has a type that the packed simulation runtime "
                      "cannot represent",
                declaration.span);
            return std::nullopt;
        }
        if (declaration.type.vhdl_array
            && !declaration.type.width()) {
            report(
                "FSIM-ELAB-VHARRAY-005",
                "VHDL array object '" + declaration.name
                    + "' requires a concrete non-null index constraint",
                declaration.span);
            return std::nullopt;
        }
        const auto width = declaration.type.width().value_or(1);
        if (width == 0 || width > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-010",
                "signal '" + declaration.name + "' has an invalid width",
                declaration.span);
            return std::nullopt;
        }
        if (design_.signals_.size()
            > std::numeric_limits<SignalId>::max()) {
            report(
                "FSIM-ELAB-011",
                "the design has too many signals for dense 32-bit IDs",
                declaration.span);
            return std::nullopt;
        }
        const auto id = static_cast<SignalId>(design_.signals_.size());
        const auto full_name =
            std::string(path) + "." + declaration.name;
        local.emplace(declaration.name, id);
        local.emplace(full_name, id);
        design_.signal_by_name_.emplace(full_name, id);
        if (path == design_.top_) {
            design_.signal_by_name_.emplace(declaration.name, id);
        }
        design_.signal_info_.push_back({
            id,
            full_name,
            static_cast<std::size_t>(width),
            declaration.type.spelling,
            declaration.type.domain,
            declaration.type.is_signed,
            declaration.type.packed_range,
            declaration.type.vhdl_array,
            declaration.type.packed_members,
            declaration.type.integer_range,
            declaration.type.nominal_type,
            declaration.type.enumeration_literals,
            declaration.type.enumeration_range,
            declaration.is_port,
            declaration.direction,
            declaration.span});
        auto initial = Logic4::x;
        if (declaration.type.spelling == "event") {
            initial = Logic4::zero;
        } else if (declaration.type.spelling == "tri0") {
            initial = Logic4::zero;
        } else if (declaration.type.spelling == "tri1") {
            initial = Logic4::one;
        } else if (is_two_state_domain(declaration.type.domain)) {
            initial = Logic4::zero;
        } else if (
            declaration.type.domain == frontend::ValueDomain::Logic4
            && (declaration.type.spelling == "wire"
                || declaration.type.spelling == "tri"
                || declaration.type.spelling == "wand"
                || declaration.type.spelling == "triand"
                || declaration.type.spelling == "wor"
                || declaration.type.spelling == "trior"
                || declaration.type.spelling == "trireg"
                || declaration.type.spelling == "uwire")) {
            initial = Logic4::z;
        }
        auto initial_value =
            PackedLogic4(static_cast<std::size_t>(width), initial);
        if (!declaration.type.packed_members.empty()
            || !declaration.type.enumeration_literals.empty()
            || declaration.type.domain
                == frontend::ValueDomain::Logic9
            || declaration.type.domain
                == frontend::ValueDomain::Integer) {
            initial_value = default_packed_value(
                declaration.type, static_cast<std::size_t>(width));
        }
        design_.signals_.push_back(
            {
                full_name,
                std::move(initial_value),
                ResolutionKind::none,
                value_kind(declaration.type.domain)});
        return id;
    }



    const Binding* HierarchyBuilder::binding_for(const std::string& path) {
        const auto found = bindings_.find(path);
        if (found == bindings_.end()) {
            return nullptr;
        }
        used_bindings_.insert(path);
        return found->second;
    }



    const DesignUnit* HierarchyBuilder::bound_target(
        const frontend::Instance& instance,
        const DesignUnit& parent,
        const std::string& path,
        const Binding* binding) {
        if (binding == nullptr) {
            const auto* target = choose_same_language_instance(
                parsed_, parent, instance.unit_name);
            if (target == nullptr) {
                report(
                    "FSIM-ELAB-BIND-012",
                    "instance '" + path + "' names unit '"
                        + instance.unit_name
                        + "', which was not found in the same language; "
                          "an explicit cross-language binding is required",
                    instance.span);
            }
            return target;
        }
        const auto target = parse_target(binding->target);
        if (!target) {
            report(
                "FSIM-ELAB-BIND-013",
                "malformed binding target '" + binding->target + "'",
                instance.span);
            return nullptr;
        }
        if (target->language == "systemc") {
            report(
                "FSIM-ELAB-BIND-014",
                "SystemC factory hierarchy is not executable in this slice",
                instance.span);
            return nullptr;
        }
        if (target->language == "vhdl" && !target->architecture) {
            report(
                "FSIM-ELAB-BIND-016",
                "an explicit VHDL binding target must name an architecture, "
                "for example vhdl:work.entity(rtl)",
                instance.span);
            return nullptr;
        }
        const auto* selected = choose_bound_unit(parsed_, *target);
        if (selected == nullptr) {
            report(
                "FSIM-ELAB-BIND-015",
                "binding target '" + binding->target + "' was not found",
                instance.span);
        }
        return selected;
    }



    void HierarchyBuilder::validate_boundary_type(
        const frontend::SignalDeclaration& port,
        const SignalInfo& actual,
        const std::string& path,
        const frontend::SourceSpan& source,
        const bool cross_language) {
        const auto unsupported_domain =
            [](const frontend::ValueDomain domain) {
                return domain == frontend::ValueDomain::Unknown;
            };
        if (unsupported_domain(port.type.domain)
            || unsupported_domain(actual.source_domain)) {
            report(
                "FSIM-ELAB-BIND-019",
                "unsupported value domain on boundary '"
                    + path + "." + port.name + "'",
                source);
            return;
        }
        if (cross_language
            && (!port.type.packed_members.empty()
                || !actual.packed_members.empty())) {
            report(
                "FSIM-ELAB-BIND-049",
                "packed aggregate boundary '" + path + "."
                    + port.name
                    + "' requires a same-language scalar/vector wrapper",
                source);
            return;
        }
        const bool port_array =
            port.type.vhdl_array.has_value();
        const bool actual_array =
            actual.vhdl_array.has_value();
        if (cross_language
            && (port_array || actual_array)) {
            report(
                "FSIM-ELAB-BIND-055",
                "VHDL array boundary '" + path + "."
                    + port.name
                    + "' requires a same-language scalar/vector wrapper",
                source);
            return;
        }
        if (!cross_language
            && (port_array || actual_array)
            && (!port_array
                || !actual_array
                || port.type.nominal_type
                    != actual.nominal_type)) {
            report(
                "FSIM-ELAB-BIND-056",
                "VHDL array boundary '" + path + "."
                    + port.name
                    + "' requires the same nominal array type",
                source);
            return;
        }
        const bool port_enumeration =
            !port.type.enumeration_literals.empty();
        const bool actual_enumeration =
            !actual.enumeration_literals.empty();
        if (cross_language
            && (port_enumeration || actual_enumeration)) {
            report(
                "FSIM-ELAB-BIND-052",
                "VHDL enumeration boundary '" + path + "."
                    + port.name
                    + "' requires a same-language scalar/vector wrapper",
                source);
            return;
        }
        if (!cross_language
            && (port_enumeration || actual_enumeration)
            && (!port_enumeration
                || !actual_enumeration
                || port.type.nominal_type
                    != actual.nominal_type)) {
            report(
                "FSIM-ELAB-BIND-053",
                "VHDL enumeration boundary '" + path + "."
                    + port.name
                    + "' requires the same nominal enumeration type",
                source);
            return;
        }
        if (!cross_language
            && port_enumeration && actual_enumeration) {
            const auto bounds =
                [](const std::optional<
                       frontend::EnumerationRange>& range,
                   const std::size_t literal_count) {
                  if (!range) {
                      return std::pair{
                          std::int64_t{0},
                          static_cast<std::int64_t>(
                              literal_count - 1U)};
                  }
                  return std::pair{
                      std::min(range->left, range->right),
                      std::max(range->left, range->right)};
                };
            const auto port_bounds =
                bounds(
                    port.type.enumeration_range,
                    port.type.enumeration_literals.size());
            const auto actual_bounds =
                bounds(
                    actual.enumeration_range,
                    actual.enumeration_literals.size());
            const auto contains =
                [](const auto& outer, const auto& inner) {
                  return outer.first <= inner.first
                      && outer.second >= inner.second;
                };
            const bool compatible =
                port.direction
                        == frontend::PortDirection::Input
                    ? contains(port_bounds, actual_bounds)
                : port.direction
                          == frontend::PortDirection::Output
                    ? contains(actual_bounds, port_bounds)
                    : port_bounds == actual_bounds;
            if (!compatible) {
                report(
                    "FSIM-ELAB-BIND-054",
                    "enumeration subtype ranges on boundary '"
                        + path + "." + port.name
                        + "' cannot guarantee a range-safe alias",
                    source);
            }
        }
        const auto width = port.type.width().value_or(1);
        if (width != actual.width) {
            report(
                "FSIM-ELAB-BIND-020",
                "width mismatch on '" + path + "." + port.name + "': "
                    + std::to_string(width) + " versus "
                    + std::to_string(actual.width),
                source);
        }
        if (port.type.is_signed != actual.is_signed && width > 1) {
            report(
                "FSIM-ELAB-BIND-021",
                "signedness mismatch on '" + path + "." + port.name + "'",
                source);
        }
        if (port.type.domain == frontend::ValueDomain::Integer
            || actual.source_domain
                == frontend::ValueDomain::Integer) {
            const auto bounds =
                [](const std::optional<frontend::IntegerRange>& range) {
                    if (!range) {
                        return std::pair{
                            std::numeric_limits<std::int32_t>::min(),
                            std::numeric_limits<std::int32_t>::max()};
                    }
                    return std::pair{
                        static_cast<std::int32_t>(
                            std::min(range->left, range->right)),
                        static_cast<std::int32_t>(
                            std::max(range->left, range->right))};
                };
            const auto port_bounds =
                bounds(port.type.integer_range);
            const auto actual_bounds =
                bounds(actual.integer_range);
            const auto contains =
                [](const auto& outer, const auto& inner) {
                    return outer.first <= inner.first
                        && outer.second >= inner.second;
                };
            const bool compatible =
                port.direction == frontend::PortDirection::Input
                    ? contains(port_bounds, actual_bounds)
                    : port.direction
                              == frontend::PortDirection::Output
                        ? contains(actual_bounds, port_bounds)
                        : port_bounds == actual_bounds;
            if (!compatible) {
                report(
                    "FSIM-ELAB-BIND-051",
                    "integer subtype ranges on boundary '" + path + "."
                        + port.name
                        + "' cannot guarantee a range-safe alias",
                    source);
            }
        }
        const auto lossy_into_two_state =
            [](const frontend::ValueDomain destination,
               const frontend::ValueDomain source_domain) {
                return is_two_state_domain(destination)
                    && !is_two_state_domain(source_domain);
            };
        const bool lossy =
            port.direction == frontend::PortDirection::Output
                ? lossy_into_two_state(
                      actual.source_domain, port.type.domain)
                : lossy_into_two_state(
                      port.type.domain, actual.source_domain);
        if (lossy) {
            report(
                "FSIM-ELAB-BIND-022",
                "implicit lossy conversion into a 2-state boundary at '"
                    + path + "." + port.name + "' is forbidden",
                source);
        }
    }



    void HierarchyBuilder::note_boundary_driver(
        const SignalId signal,
        const Binding* binding,
        const std::string& path,
        const frontend::SourceSpan& source,
        const bool cross_language) {
        auto& count = boundary_driver_count_[signal];
        ++count;
        if (cross_language) {
            cross_language_boundary_signals_.insert(signal);
        }
        if (binding != nullptr && binding->resolver) {
            const auto [found, inserted] =
                resolver_by_signal_.emplace(signal, *binding->resolver);
            if (!inserted && found->second != *binding->resolver) {
                report(
                    "FSIM-ELAB-BIND-023",
                    "conflicting resolvers for boundary net '" + path + "'",
                    source);
            }
        }
        if (count > 1
            && !resolver_by_signal_.contains(signal)
            && (cross_language_boundary_signals_.contains(signal)
                || native_resolution(
                       design_.signal_info_.at(signal))
                    == ResolutionKind::none)) {
            report(
                "FSIM-ELAB-BIND-024",
                "multiple boundary drivers on '" + path
                    + "' require resolver = \"std_logic\" or \"sv_wire\"",
                source);
        }
    }



    HierarchyBuilder::SignalMap HierarchyBuilder::connect_ports(
        const frontend::Instance& instance,
        const std::vector<frontend::SignalDeclaration>& ports,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding,
        const bool cross_language) {
        SignalMap aliases;
        std::vector<bool> connected(ports.size());
        std::size_t positional = 0;
        for (const auto& connection : instance.connections) {
            std::size_t port_index = ports.size();
            if (connection.port) {
                const auto found = std::find_if(
                    ports.begin(), ports.end(),
                    [&](const frontend::SignalDeclaration& port) {
                        return port.name == *connection.port;
                    });
                if (found != ports.end()) {
                    port_index = static_cast<std::size_t>(
                        std::distance(ports.begin(), found));
                }
            } else {
                while (positional < ports.size() && connected[positional]) {
                    ++positional;
                }
                port_index = positional++;
            }
            if (port_index >= ports.size()) {
                report(
                    "FSIM-ELAB-BIND-025",
                    connection.port
                        ? "unknown port '" + *connection.port
                            + "' on instance '" + path + "'"
                        : "too many positional connections on instance '"
                            + path + "'",
                    connection.span);
                continue;
            }
            if (connected[port_index]) {
                report(
                    "FSIM-ELAB-BIND-026",
                    "port '" + ports[port_index].name
                        + "' is connected more than once on instance '"
                        + path + "'",
                    connection.span);
                continue;
            }
            connected[port_index] = true;
            if (connection.value.kind != frontend::ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-BIND-027",
                    "boundary connection actuals must be whole signals",
                    connection.value.span);
                continue;
            }
            const auto actual = parent_signals.find(connection.value.text);
            if (actual == parent_signals.end()) {
                report(
                    "FSIM-ELAB-BIND-028",
                    "unknown connection signal '" + connection.value.text
                        + "' on instance '" + path + "'",
                    connection.value.span);
                continue;
            }
            const auto& port = ports[port_index];
            const auto& actual_info = design_.signal_info_.at(actual->second);
            validate_boundary_type(
                port,
                actual_info,
                path,
                connection.span,
                cross_language);
            if (cross_language
                && port.direction == frontend::PortDirection::Inout) {
                if (binding == nullptr || !binding->resolver) {
                    report(
                        "FSIM-ELAB-BIND-030",
                        "cross-language inout '" + path + "." + port.name
                            + "' requires resolver = \"std_logic\" or "
                              "\"sv_wire\"",
                        connection.span);
                }
            }
            aliases.emplace(port.name, actual->second);
            aliases.emplace(path + "." + port.name, actual->second);
            design_.signal_by_name_.emplace(
                path + "." + port.name, actual->second);
            if (port.direction == frontend::PortDirection::Output
                || port.direction == frontend::PortDirection::Inout
                || port.direction == frontend::PortDirection::Buffer) {
                note_boundary_driver(
                    actual->second,
                    binding,
                    path,
                    connection.span,
                    cross_language);
            }
        }
        if (instance.unconnected_drive
            != frontend::VerilogUnconnectedDrive::None) {
            for (std::size_t port_index = 0;
                 port_index < ports.size(); ++port_index) {
                const auto& port = ports[port_index];
                if (connected[port_index]
                    || port.direction
                        != frontend::PortDirection::Input) {
                    continue;
                }
                auto pulled = port;
                pulled.type.spelling =
                    instance.unconnected_drive
                            == frontend::VerilogUnconnectedDrive::Pull0
                        ? "tri0"
                        : "tri1";
                (void)add_owned_signal(pulled, path, aliases);
            }
        }
        return aliases;
    }



    HierarchyBuilder::SignalMap HierarchyBuilder::connect_instance(
        const frontend::Instance& instance,
        const DesignUnit& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding,
        const bool cross_language) {
        const auto* ports = unit_ports(parsed_, target);
        if (ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + target.name
                    + "' has no matching entity",
                target.span);
            return {};
        }
        return connect_ports(
            instance,
            *ports,
            path,
            parent_signals,
            binding,
            cross_language);
    }



    frontend::SignalDeclaration HierarchyBuilder::external_port_declaration(
        const ExternalPort& port) {
        return {
            port.name,
            port.type,
            port.direction,
            true,
            {}};
    }



    frontend::SignalDeclaration HierarchyBuilder::foreign_port_declaration(
        const ForeignPort& port) {
        return {
            port.name,
            port.type,
            port.direction,
            true,
            {}};
    }



    std::pair<HierarchyBuilder::SignalMap, HierarchyBuilder::ObjectMap> HierarchyBuilder::connect_systemc_instance(
        const frontend::Instance& instance,
        const SystemCInstanceDescription& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding) {
        std::vector<frontend::SignalDeclaration> ports;
        ports.reserve(target.ports.size());
        for (const auto& port : target.ports) {
            ports.push_back(external_port_declaration(port));
        }
        auto aliases = connect_ports(
            instance,
            ports,
            path,
            parent_signals,
            binding,
            true);
        ObjectMap objects;
        for (const auto& port : target.ports) {
            if (const auto signal = aliases.find(port.name);
                signal != aliases.end()) {
                objects.emplace(port.handle, signal->second);
            }
        }
        return {std::move(aliases), std::move(objects)};
    }



    HierarchyBuilder::SignalMap HierarchyBuilder::connect_foreign_child(
        const ForeignChild& child,
        const DesignUnit& target,
        const std::string& path,
        const ObjectMap& objects) {
        SignalMap aliases;
        const auto* target_ports = unit_ports(parsed_, target);
        if (target_ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + target.name
                    + "' has no matching entity",
                target.span);
            return aliases;
        }
        std::unordered_set<std::string> connected;
        for (const auto& foreign_port : child.ports) {
            const auto formal = std::find_if(
                target_ports->begin(),
                target_ports->end(),
                [&](const frontend::SignalDeclaration& port) {
                    return port.name == foreign_port.name;
                });
            if (formal == target_ports->end()) {
                report(
                    "FSIM-ELAB-BIND-034",
                    "foreign child '" + path
                        + "' declares unknown target port '"
                        + foreign_port.name + "'",
                    {});
                continue;
            }
            if (!connected.insert(foreign_port.name).second) {
                report(
                    "FSIM-ELAB-BIND-035",
                    "foreign child port '" + path + "."
                        + foreign_port.name
                        + "' is connected more than once",
                    {});
                continue;
            }
            const auto actual = objects.find(foreign_port.object);
            if (actual == objects.end()) {
                report(
                    "FSIM-ELAB-BIND-036",
                    "foreign child port '" + path + "."
                        + foreign_port.name
                        + "' references an unknown SystemC object",
                    {});
                continue;
            }
            const auto placeholder =
                foreign_port_declaration(foreign_port);
            const auto& actual_info =
                design_.signal_info_.at(actual->second);
            validate_boundary_type(
                placeholder, actual_info, path, {}, true);
            validate_boundary_type(
                *formal, actual_info, path, {}, true);
            if (placeholder.direction != formal->direction) {
                report(
                    "FSIM-ELAB-BIND-037",
                    "foreign child port direction mismatch on '"
                        + path + "." + foreign_port.name + "'",
                    {});
            }
            aliases.emplace(formal->name, actual->second);
            aliases.emplace(
                path + "." + formal->name, actual->second);
            design_.signal_by_name_.emplace(
                path + "." + formal->name, actual->second);
            // The foreign child is an implementation detail of the enclosing
            // SystemC module. Its output reaches the parent through that
            // module's already-recorded boundary driver, so recording a
            // second boundary driver here would turn one hierarchical drive
            // path into a false multi-driver conflict.
        }
        return aliases;
    }

} // namespace fsim::elaboration
