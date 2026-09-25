// SPDX-License-Identifier: Apache-2.0
#include "application_compiled_environment_sv.hpp"

#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <string_view>

namespace fsim::app::application_detail {
namespace {

namespace sv = semantic::sv;

std::string_view effective_library(std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

diagnostic::SourceSpan source_span(const frontend::SourceSpan& source)
{
    const auto position = [](const frontend::SourceLocation& input) {
        const auto clamp = [](std::size_t value) {
            return static_cast<std::uint32_t>(std::min<std::size_t>(value,
                std::numeric_limits<std::uint32_t>::max()));
        };
        return diagnostic::SourcePosition { clamp(input.line), clamp(input.column),
            static_cast<std::uint64_t>(input.offset) };
    };
    return { source.source_name.str(),
        position(source.begin), position(source.end) };
}

/// A lookup result, never a reconstructed syntax type. Source types are owned
/// by this request and imported types remain owned by the compiled design.
struct ClassTypeView {
    const frontend::Type* source { };
    const sv::TypeReference* compiled { };
    std::string identity;
};

struct SourceScope {
    const frontend::DesignUnit* unit { };
    std::string library { "work" };
    std::string class_identity;
    std::map<std::string, ClassTypeView, std::less<>> objects;
    std::map<std::string, const frontend::Type*, std::less<>> aliases;
    std::set<std::string, std::less<>> local_classes;
};

class SourceEnvironment final {
public:
    SourceEnvironment(const semantic::CompiledDesign& imported,
        diagnostic::Engine& diagnostics)
        : imported_(imported)
        , diagnostics_(diagnostics)
        , resolver_(imported, { })
    {
    }

    bool prepare(frontend::ParsedDesign& parsed)
    {
        for (auto& unit : parsed.units) {
            if (unit.language != frontend::Language::SystemVerilog2017)
                continue;
            const auto prefix = std::string { effective_library(unit.library) }
                + "::" + unit.name;
            for (auto& declaration : unit.systemverilog_classes)
                collect_class(declaration, prefix);
        }
        for (auto& declaration : parsed.systemverilog_classes) {
            auto prefix = std::string { effective_library(declaration.library) }
                + "::$unit";
            if (!declaration.compilation_unit_identity.empty())
                prefix += "@" + declaration.compilation_unit_identity;
            collect_class(declaration, prefix);
        }
        for (auto& unit : parsed.units) {
            if (unit.language != frontend::Language::SystemVerilog2017)
                continue;
            SourceScope scope;
            scope.unit = &unit;
            scope.library = effective_library(unit.library);
            for (const auto& declaration : unit.systemverilog_classes)
                scope.local_classes.insert(declaration.name);
            for (const auto& declaration : parsed.systemverilog_classes) {
                if (effective_library(declaration.library) == scope.library
                    && declaration.compilation_unit_identity == unit.compilation_unit_identity)
                    scope.local_classes.insert(declaration.name);
            }
            check_profiles(unit);
            declarations(unit.parameters, scope);
            aliases(unit.type_aliases, scope);
            declarations(unit.ports, scope);
            declarations(unit.signals, scope);
            declarations(unit.variables, scope);
            for (auto& declaration : unit.systemverilog_classes)
                prepare_class(declaration, scope);
            for (auto& function : unit.functions)
                callable(function, scope);
            for (auto& task : unit.tasks)
                callable(task, scope);
            for (auto& process : unit.processes)
                prepare_process(process, scope);
            statements(unit.concurrent_statements, scope);
            for (auto& region : unit.generate_regions)
                generate(region, scope);
        }
        for (auto& declaration : parsed.systemverilog_classes) {
            SourceScope scope;
            scope.library = effective_library(declaration.library);
            prepare_class(declaration, scope);
        }
        return !diagnostics_.has_error();
    }

private:
    void error(std::string code, std::string message,
        const frontend::SourceSpan& source)
    {
        diagnostics_.error(std::move(code), std::move(message),
            source_span(source));
    }

    const sv::Unit* package(std::string_view library,
        std::string_view name, const frontend::SourceSpan& source)
    {
        if (const auto selected = resolver_.find_systemverilog_package(
                library, name))
            return selected->systemverilog;
        const sv::Unit* selected { };
        for (const auto& candidate : imported_.systemverilog_units()) {
            if (candidate.kind != sv::UnitKind::package
                || candidate.name != name)
                continue;
            if (selected != nullptr) {
                error("FSIM-SV-PACKAGE-001", "compiled package '"
                    + std::string { name } + "' is ambiguous between libraries '"
                    + selected->library + "' and '" + candidate.library + "'",
                    source);
                return nullptr;
            }
            selected = &candidate;
        }
        return selected;
    }

    void check_profile(const sv::Unit& provider,
        const frontend::DesignUnit& consumer,
        const frontend::SourceSpan& source)
    {
        if (provider.standard != frontend::revision_string(consumer.standard_revision)
            || provider.compatibility_profile != consumer.verilog_compatibility_profile) {
            error("FSIM-FE-STANDARD-003", "compiled SystemVerilog package '"
                + provider.library + "::" + provider.name
                + "' uses an incompatible standard or compatibility profile", source);
        }
    }

    void check_profiles(const frontend::DesignUnit& unit)
    {
        for (const auto& import : unit.systemverilog_imports) {
            const auto* provider = package(effective_library(unit.library),
                import.package, import.span);
            if (provider == nullptr)
                continue;
            check_profile(*provider, unit, import.span);
        }
    }

    template<typename Lookup>
    auto visible(std::string_view spelling, const SourceScope& scope,
        const frontend::SourceSpan& source, Lookup&& lookup)
    {
        using Result = decltype(lookup(std::string_view { },
            std::string_view { }, std::string_view { }));
        Result result;
        const auto append = [&](std::string_view package_name,
                                std::string_view member) {
            const auto* selected = package(scope.library, package_name, source);
            if (selected == nullptr)
                return;
            if (scope.unit != nullptr)
                check_profile(*selected, *scope.unit, source);
            auto found = lookup(selected->library, selected->name, member);
            for (const auto candidate : found) {
                if (std::ranges::find(result, candidate) == result.end())
                    result.push_back(candidate);
            }
        };
        const auto separator = spelling.rfind("::");
        if (separator != std::string_view::npos) {
            append(spelling.substr(0, separator), spelling.substr(separator + 2U));
            return result;
        }
        if (scope.unit == nullptr)
            return result;
        bool explicit_import { };
        for (const auto& import : scope.unit->systemverilog_imports) {
            if (import.name == spelling) {
                explicit_import = true;
                append(import.package, spelling);
            }
        }
        if (explicit_import)
            return result;
        for (const auto& import : scope.unit->systemverilog_imports) {
            if (import.name.empty())
                append(import.package, spelling);
        }
        return result;
    }

    const sv::ClassDeclaration* named_class(std::string_view spelling,
        const SourceScope& scope, const frontend::SourceSpan& source)
    {
        if (const auto found = std::ranges::find(imported_.systemverilog_hir.classes(),
                spelling, &sv::ClassDeclaration::canonical_identity);
            found != imported_.systemverilog_hir.classes().end())
            return &*found;
        if (scope.local_classes.contains(spelling))
            return nullptr;
        auto found = visible(spelling, scope, source,
            [&](auto library, auto package_name, auto member) {
                return resolver_.resolve_systemverilog_package_class(
                    library, package_name, member).candidates;
            });
        if (found.size() > 1U) {
            error("FSIM-SV-CLASS-004", "compiled class '"
                + std::string { spelling } + "' is ambiguous in import scope", source);
            return nullptr;
        }
        return found.empty() ? nullptr : found.front();
    }

    const sv::Declaration* named_declaration(std::string_view spelling,
        const SourceScope& scope, const frontend::SourceSpan& source)
    {
        const auto found = visible(spelling, scope, source,
            [&](auto library, auto package_name, auto member) {
                return resolver_.resolve_systemverilog_package_member(
                    library, package_name, member).candidates;
            });
        if (found.size() != 1U)
            return nullptr;
        const auto declaration = imported_.find_declaration(found.front());
        return declaration ? declaration->systemverilog : nullptr;
    }

    const frontend::SystemVerilogClassDeclaration* source_class(
        std::string_view spelling, const SourceScope& scope) const
    {
        const auto find = [&](const std::string& identity) {
            const auto selected = source_classes_.find(identity);
            return selected == source_classes_.end() ? nullptr : selected->second;
        };
        if (const auto* exact = find(std::string { spelling }))
            return exact;
        if (spelling.find("::") != std::string_view::npos)
            return find(scope.library + "::" + std::string { spelling });
        auto lexical = scope.class_identity;
        while (!lexical.empty()) {
            if (const auto* selected = find(lexical + "::" + std::string { spelling }))
                return selected;
            const auto separator = lexical.rfind("::");
            if (separator == std::string::npos)
                break;
            lexical.resize(separator);
        }
        if (scope.unit != nullptr) {
            if (const auto* selected = find(scope.library + "::" + scope.unit->name
                    + "::" + std::string { spelling }))
                return selected;
            auto unit_scope = scope.library + "::$unit";
            if (!scope.unit->compilation_unit_identity.empty())
                unit_scope += "@" + scope.unit->compilation_unit_identity;
            if (const auto* selected = find(unit_scope + "::" + std::string { spelling }))
                return selected;
        }
        return nullptr;
    }

    ClassTypeView compiled_type(const sv::TypeReference& type) const
    {
        std::set<std::uint32_t> active;
        const auto* current = &type;
        while (current->class_identity.empty() && current->target.target.valid()
            && active.insert(current->target.target.value()).second) {
            const auto definition = imported_.find_type(current->target.target);
            if (!definition || definition->systemverilog == nullptr)
                break;
            current = &definition->systemverilog->base;
        }
        return { nullptr, current, current->class_identity };
    }

    ClassTypeView source_type(frontend::Type& type, const SourceScope& scope)
    {
        for (auto& member : type.packed_members) {
            for (auto& nested : member.nested_types)
                source_type(nested, scope);
        }
        if (type.systemverilog_container) {
            for (auto& element : type.systemverilog_container->element_types)
                source_type(element, scope);
            if (type.systemverilog_container->associative_index_type)
                source_type(*type.systemverilog_container->associative_index_type, scope);
        }
        for (auto& actual : type.systemverilog_class_parameter_actuals) {
            if (actual.type_actual)
                source_type(*actual.type_actual, scope);
            expression(actual.value, scope);
        }
        if (type.packed_range_expression) {
            expression(type.packed_range_expression->left, scope);
            expression(type.packed_range_expression->right, scope);
        }
        if (type.named_type.empty() || type.systemverilog_virtual_interface)
            return { &type, nullptr, type.systemverilog_class_declaration };
        if (const auto alias = scope.aliases.find(type.named_type);
            alias != scope.aliases.end()) {
            if (alias->second != &type) {
                type.systemverilog_class_declaration
                    = alias->second->systemverilog_class_declaration;
            }
        } else if (const auto* source_declaration = source_class(type.named_type, scope)) {
            type.systemverilog_class_name = type.named_type;
            type.systemverilog_class_declaration = source_declaration->canonical_identity;
        } else if (const auto* class_declaration = named_class(type.named_type, scope,
                       type.named_type_span)) {
            type.systemverilog_class_name = type.named_type;
            type.systemverilog_class_declaration = class_declaration->canonical_identity;
        } else if (const auto* named = named_declaration(type.named_type,
                       scope, type.named_type_span)) {
            if (named->type)
                type.systemverilog_class_declaration = compiled_type(*named->type).identity;
            if (named->declared_type) {
                const auto definition = imported_.find_type(*named->declared_type);
                if (definition && definition->systemverilog != nullptr)
                    type.systemverilog_class_declaration
                        = compiled_type(definition->systemverilog->base).identity;
            }
        }
        return { &type, nullptr, type.systemverilog_class_declaration };
    }

    template<typename Declarations>
    void declarations(Declarations& input, SourceScope& scope)
    {
        for (auto& declaration : input) {
            const auto type = source_type(declaration.type, scope);
            scope.objects.insert_or_assign(declaration.name, type);
            if constexpr (requires { declaration.initializer; }) {
                if (declaration.initializer)
                    expression(*declaration.initializer, scope, type);
            }
            if constexpr (requires { declaration.default_value; }) {
                if constexpr (requires { *declaration.default_value; }) {
                    if (declaration.default_value)
                        expression(*declaration.default_value, scope, type);
                } else {
                    expression(declaration.default_value, scope, type);
                }
            }
            if constexpr (requires { declaration.default_type; }) {
                if (declaration.default_type)
                    source_type(*declaration.default_type, scope);
                if (declaration.kind == frontend::ParameterKind::Type)
                    scope.aliases.insert_or_assign(declaration.name, &declaration.type);
            }
        }
    }

    template<typename Aliases>
    void aliases(Aliases& input, SourceScope& scope)
    {
        for (auto& alias : input) {
            source_type(alias.type, scope);
            scope.aliases.insert_or_assign(alias.name, &alias.type);
        }
    }

    void collect_class(frontend::SystemVerilogClassDeclaration& declaration,
        const std::string& prefix)
    {
        declaration.canonical_identity = prefix + "::" + declaration.name;
        source_classes_.emplace(declaration.canonical_identity, &declaration);
        for (auto& nested : declaration.nested_classes)
            collect_class(nested, declaration.canonical_identity);
    }

    void prepare_class(frontend::SystemVerilogClassDeclaration& declaration,
        SourceScope scope)
    {
        scope.class_identity = declaration.canonical_identity;
        const auto relation = [&](frontend::SystemVerilogClassBase& base) {
            if (const auto* selected = named_class(base.name, scope, base.span))
                base.declaration_identity = selected->canonical_identity;
            for (auto& actual : base.parameter_actuals) {
                if (actual.type_actual)
                    source_type(*actual.type_actual, scope);
                expression(actual.value, scope);
            }
        };
        if (declaration.base)
            relation(*declaration.base);
        for (auto& base : declaration.extended_interfaces)
            relation(base);
        for (auto& base : declaration.implemented_interfaces)
            relation(base);
        declarations(declaration.parameters, scope);
        aliases(declaration.type_aliases, scope);
        for (auto& property : declaration.properties) {
            auto& input = property.declaration;
            const auto type = source_type(input.type, scope);
            scope.objects.insert_or_assign(input.name, type);
            if (input.initializer)
                expression(*input.initializer, scope, type);
        }
        for (auto& method : declaration.methods)
            callable(method, scope);
        for (auto& nested : declaration.nested_classes)
            prepare_class(nested, scope);
    }

    template<typename Callable>
    void callable(Callable& input, SourceScope scope)
    {
        ClassTypeView result;
        if constexpr (requires { input.return_type; })
            result = source_type(input.return_type, scope);
        declarations(input.arguments, scope);
        if constexpr (requires { input.constants; })
            declarations(input.constants, scope);
        aliases(input.type_aliases, scope);
        declarations(input.variables, scope);
        if constexpr (requires { input.functions; }) {
            for (auto& function : input.functions)
                callable(function, scope);
        }
        statements(input.statements, scope, result);
    }

    void prepare_process(frontend::Process& process, SourceScope scope)
    {
        declarations(process.constants, scope);
        aliases(process.type_aliases, scope);
        declarations(process.variables, scope);
        for (auto& function : process.functions)
            callable(function, scope);
        for (auto& sensitivity : process.sensitivities)
            expression(sensitivity.expression, scope);
        statements(process.statements, scope);
    }

    void generate(frontend::GenerateRegion& region, const SourceScope& scope)
    {
        expression(region.initial, scope);
        expression(region.condition, scope);
        expression(region.iteration, scope);
        generate_body(region.then_body, scope);
        generate_body(region.else_body, scope);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                expression(choice.left, scope);
                if (choice.right)
                    expression(*choice.right, scope);
            }
            generate_body(alternative.body, scope);
        }
    }

    void generate_body(frontend::GenerateBody& body, SourceScope scope)
    {
        declarations(body.constants, scope);
        aliases(body.type_aliases, scope);
        declarations(body.signals, scope);
        declarations(body.variables, scope);
        for (auto& function : body.functions)
            callable(function, scope);
        for (auto& task : body.tasks)
            callable(task, scope);
        for (auto& declaration : body.systemverilog_classes)
            prepare_class(declaration, scope);
        for (auto& process : body.processes)
            prepare_process(process, scope);
        statements(body.concurrent_statements, scope);
        for (auto& region : body.generate_regions)
            generate(region, scope);
    }

    template<typename Visitor>
    void class_chain(std::string identity, Visitor&& visitor) const
    {
        std::set<std::string> visited;
        while (!identity.empty() && visited.insert(identity).second) {
            const auto source = source_classes_.find(identity);
            if (source != source_classes_.end()) {
                if (!visitor(*source->second))
                    return;
                identity = source->second->base
                    ? source->second->base->declaration_identity : std::string { };
                continue;
            }
            const auto found = std::ranges::find(imported_.systemverilog_hir.classes(),
                identity, &sv::ClassDeclaration::canonical_identity);
            if (found == imported_.systemverilog_hir.classes().end()
                || !visitor(*found))
                return;
            identity = found->base_declaration_identity;
        }
    }

    const sv::ClassProperty* property(std::string_view identity,
        std::string_view name) const
    {
        const sv::ClassProperty* result { };
        class_chain(std::string { identity }, [&](const auto& owner) {
            for (const auto& item : owner.properties) {
                if constexpr (requires { item.canonical_identity; }) {
                    if (item.name == name) {
                        result = &item;
                        return false;
                    }
                } else if (item.declaration.name == name) {
                    return false;
                }
            }
            return true;
        });
        return result;
    }

    const sv::ClassMethod* method(std::string_view identity,
        std::string_view name) const
    {
        const sv::ClassMethod* result { };
        class_chain(std::string { identity }, [&](const auto& owner) {
            for (const auto& item : owner.methods) {
                if (item.name != name)
                    continue;
                if constexpr (requires { item.declaration; })
                    result = &item;
                return false;
            }
            return true;
        });
        return result;
    }

    bool accessible(sv::ClassVisibility visibility, std::string_view owner,
        const SourceScope& scope, const frontend::SourceSpan& source)
    {
        if (visibility == sv::ClassVisibility::public_access
            || scope.class_identity == owner)
            return true;
        bool derived { };
        if (visibility == sv::ClassVisibility::protected_access) {
            class_chain(scope.class_identity, [&](const auto& declaration) {
                derived = declaration.canonical_identity == owner;
                return !derived;
            });
        }
        if (!derived)
            error("FSIM-SV-CLASS-021", "compiled class member is not visible in this scope", source);
        return derived;
    }

    static void annotate(frontend::Expression& output, const ClassTypeView& type)
    {
        if (!type.identity.empty())
            output.nominal_type = type.identity;
        if (type.compiled != nullptr) {
            output.call_result_width = type.compiled->executable_width.value_or(0U);
            output.call_result_signed = type.compiled->signed_value;
            output.call_result_domain = type.compiled->value_form == sv::TypeForm::string
                ? frontend::ValueDomain::String
                : type.compiled->four_state ? frontend::ValueDomain::Logic4
                                           : frontend::ValueDomain::Bit2;
        }
    }

    bool validate_call(const sv::ClassMethod& method, std::size_t count,
        const frontend::SourceSpan& source)
    {
        const auto declaration = imported_.find_declaration(method.declaration);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->callable)
            return false;
        const auto& profile = *declaration->systemverilog->callable;
        std::size_t required { };
        for (const auto formal : profile.formals) {
            const auto parameter = imported_.find_declaration(formal);
            if (parameter && parameter->systemverilog != nullptr
                && !parameter->systemverilog->initializer)
                ++required;
        }
        if (count >= required && count <= profile.formals.size())
            return true;
        error("FSIM-SV-CLASS-013", "compiled method '" + method.canonical_identity
            + "' has no matching argument profile", source);
        return false;
    }

    ClassTypeView method_result(const sv::ClassMethod& method) const
    {
        const auto declaration = imported_.find_declaration(method.declaration);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->callable)
            return { };
        return compiled_type(declaration->systemverilog->callable->return_type);
    }

    ClassTypeView bind_property(frontend::Expression& output,
        frontend::Expression receiver, const ClassTypeView& receiver_type,
        std::string_view name, const SourceScope& scope, bool static_access)
    {
        const auto* selected = property(receiver_type.identity, name);
        if (selected == nullptr)
            return { };
        if (!accessible(selected->visibility, selected->owner_identity, scope, output.span))
            return { };
        if (static_access && !selected->static_storage) {
            error("FSIM-SV-CLASS-012", "instance property requires a class receiver", output.span);
            return { };
        }
        output.kind = frontend::ExpressionKind::Call;
        output.text = std::string { selected->static_storage
                ? "@sv-static-property:" : "@sv-property:" }
            + selected->canonical_identity;
        output.operands.clear();
        if (!selected->static_storage)
            output.operands.push_back(std::move(receiver));
        auto result = compiled_type(selected->type);
        annotate(output, result);
        return result;
    }

    ClassTypeView bind_method(frontend::Expression& output,
        const ClassTypeView& receiver, std::string_view name,
        const SourceScope& scope, bool static_access)
    {
        const auto* selected = method(receiver.identity, name);
        if (selected == nullptr)
            return { };
        if (!accessible(selected->visibility, selected->owner_identity, scope, output.span))
            return { };
        if (static_access && !selected->static_method) {
            error("FSIM-SV-CLASS-013", "instance method requires a class receiver", output.span);
            return { };
        }
        const auto receiver_count = static_access ? 0U : 1U;
        if (!validate_call(*selected, output.operands.size() - receiver_count, output.span))
            return { };
        output.text = std::string { selected->static_method
                ? "@sv-static-method:" : "@sv-method:" }
            + selected->canonical_identity;
        if (selected->static_method && !static_access)
            output.operands.erase(output.operands.begin());
        auto result = method_result(*selected);
        annotate(output, result);
        return result;
    }

    ClassTypeView expression(frontend::Expression& input, const SourceScope& scope,
        const ClassTypeView& expected = { })
    {
        if (!input.valid())
            return { };
        if (input.text == "@sv-new" && !expected.identity.empty()) {
            if (const auto* owner = named_class(expected.identity, scope, input.span)) {
                if (owner->virtual_class || owner->interface_class)
                    error("FSIM-SV-CLASS-014", "cannot construct a virtual or interface class", input.span);
                if (const auto* selected = method(owner->canonical_identity, "new")) {
                    (void)accessible(selected->visibility, selected->owner_identity,
                        scope, input.span);
                    (void)validate_call(*selected, input.operands.size(), input.span);
                } else if (!input.operands.empty()) {
                    error("FSIM-SV-CLASS-013", "implicit class constructor takes no arguments", input.span);
                }
                input.text += ":" + owner->canonical_identity;
                input.nominal_type = owner->canonical_identity;
                for (auto& operand : input.operands)
                    expression(operand, scope);
                return expected;
            }
        }
        if (input.kind == frontend::ExpressionKind::Identifier) {
            if (const auto object = scope.objects.find(input.text);
                object != scope.objects.end()) {
                annotate(input, object->second);
                return object->second;
            }
            if (input.text == "this")
                return { nullptr, nullptr, scope.class_identity };
            if (input.text == "super") {
                if (const auto owner = source_classes_.find(scope.class_identity);
                    owner != source_classes_.end() && owner->second->base)
                    return { nullptr, nullptr, owner->second->base->declaration_identity };
            }
            const auto dot = input.text.rfind('.');
            if (dot != std::string::npos) {
                // Split the locally parsed selected name, preserving its source
                // span. No syntax is manufactured from a compiled declaration.
                frontend::Expression receiver { frontend::ExpressionKind::Identifier,
                    input.text.substr(0, dot), { }, input.span };
                const auto type = expression(receiver, scope);
                if (!type.identity.empty()) {
                    const auto member = input.text.substr(dot + 1U);
                    return bind_property(input, std::move(receiver), type, member, scope, false);
                }
            }
            const auto separator = input.text.rfind("::");
            if (separator != std::string::npos) {
                if (const auto* owner = named_class(std::string_view { input.text }.substr(0, separator),
                        scope, input.span)) {
                    const auto member = input.text.substr(separator + 2U);
                    return bind_property(input, { },
                        { nullptr, nullptr, owner->canonical_identity }, member, scope, true);
                }
            }
            if (!scope.class_identity.empty()) {
                frontend::Expression receiver { frontend::ExpressionKind::Identifier,
                    "this", { }, input.span };
                const auto member = input.text;
                auto result = bind_property(input, std::move(receiver),
                    { nullptr, nullptr, scope.class_identity }, member, scope, false);
                if (result.compiled != nullptr)
                    return result;
            }
            if (const auto* declaration = named_declaration(input.text, scope, input.span)) {
                if (declaration->type)
                    return compiled_type(*declaration->type);
            }
            return { };
        }
        std::vector<ClassTypeView> operands;
        for (auto& operand : input.operands)
            operands.push_back(expression(operand, scope));
        if (input.kind != frontend::ExpressionKind::Call)
            return { };
        if (input.text.starts_with('.') && !operands.empty()
            && !operands.front().identity.empty()) {
            const auto member = input.text.substr(1U);
            return bind_method(input, operands.front(), member, scope, false);
        }
        if (input.text.starts_with("@sv-select:") && operands.size() == 1U
            && !operands.front().identity.empty()) {
            const auto member = input.text.substr(std::string_view { "@sv-select:" }.size());
            auto receiver = input.operands.front();
            return bind_property(input, std::move(receiver), operands.front(), member, scope, false);
        }
        const auto separator = input.text.rfind("::");
        if (separator != std::string::npos) {
            if (const auto* owner = named_class(std::string_view { input.text }.substr(0, separator),
                    scope, input.span)) {
                const auto member = input.text.substr(separator + 2U);
                return bind_method(input, { nullptr, nullptr, owner->canonical_identity },
                    member, scope, true);
            }
        }
        if (!scope.class_identity.empty() && !input.text.starts_with('@')) {
            if (const auto* selected = method(scope.class_identity, input.text)) {
                const auto member = input.text;
                if (!selected->static_method) {
                    frontend::Expression receiver { frontend::ExpressionKind::Identifier,
                        "this", { }, input.span };
                    receiver.nominal_type = scope.class_identity;
                    input.operands.insert(input.operands.begin(), std::move(receiver));
                    input.call_argument_names.insert(input.call_argument_names.begin(), std::string { });
                }
                return bind_method(input, { nullptr, nullptr, scope.class_identity },
                    member, scope, selected->static_method);
            }
        }
        if (const auto* declaration = named_declaration(input.text, scope, input.span)) {
            if (declaration->callable)
                return compiled_type(declaration->callable->return_type);
        }
        return { };
    }

    void task(frontend::Statement& input, const SourceScope& scope)
    {
        const auto dot = input.task_name.rfind('.');
        const auto separator = input.task_name.rfind("::");
        const sv::ClassMethod* selected { };
        frontend::Expression receiver;
        bool static_access = dot == std::string::npos;
        if (!static_access) {
            receiver = { frontend::ExpressionKind::Identifier,
                input.task_name.substr(0, dot), { }, input.span };
            const auto type = expression(receiver, scope);
            if (!type.identity.empty())
                selected = method(type.identity, std::string_view { input.task_name }.substr(dot + 1U));
        } else if (separator != std::string::npos) {
            if (const auto* owner = named_class(std::string_view { input.task_name }.substr(0, separator),
                    scope, input.span))
                selected = method(owner->canonical_identity,
                    std::string_view { input.task_name }.substr(separator + 2U));
        } else if (!scope.class_identity.empty()) {
            selected = method(scope.class_identity, input.task_name);
            if (selected != nullptr && !selected->static_method) {
                static_access = false;
                receiver = { frontend::ExpressionKind::Identifier,
                    "this", { }, input.span };
                receiver.nominal_type = scope.class_identity;
            }
        }
        if (selected == nullptr)
            return;
        if (!accessible(selected->visibility, selected->owner_identity, scope, input.span)
            || !validate_call(*selected, input.task_arguments.size(), input.span))
            return;
        if (static_access && !selected->static_method) {
            error("FSIM-SV-CLASS-013", "instance task requires a class receiver", input.span);
            return;
        }
        input.task_name = std::string { selected->static_method
                ? "@sv-static-task:" : "@sv-task:" }
            + selected->canonical_identity;
        if (!selected->static_method) {
            input.task_arguments.insert(input.task_arguments.begin(), std::move(receiver));
            input.task_argument_names.insert(input.task_argument_names.begin(), std::string { });
        }
    }

    template<typename Statements>
    void statements(Statements& input, const SourceScope& scope,
        const ClassTypeView& returned = { })
    {
        for (auto& statement : input)
            prepare_statement(statement, scope, returned);
    }

    void prepare_statement(frontend::Statement& input, SourceScope scope,
        const ClassTypeView& returned)
    {
        declarations(input.constants, scope);
        aliases(input.type_aliases, scope);
        declarations(input.declarations, scope);
        const auto target = expression(input.target, scope);
        expression(input.value, scope,
            input.kind == frontend::StatementKind::Return ? returned : target);
        expression(input.condition, scope);
        expression(input.loop_initial, scope);
        expression(input.loop_limit, scope);
        expression(input.loop_update_target, scope);
        expression(input.file_handle, scope);
        for (auto& argument : input.task_arguments)
            expression(argument, scope);
        for (auto& output : input.output_values)
            expression(output.value, scope);
        for (auto& sensitivity : input.sensitivities)
            expression(sensitivity.expression, scope);
        if (input.kind == frontend::StatementKind::TaskCall)
            task(input, scope);
        statements(input.loop_updates, scope, returned);
        statements(input.statements, scope, returned);
        statements(input.else_statements, scope, returned);
        for (auto& alternative : input.case_alternatives) {
            for (auto& choice : alternative.choices)
                expression(choice, scope);
            statements(alternative.statements, scope, returned);
        }
        for (auto& function : input.functions)
            callable(function, scope);
    }

    const semantic::CompiledDesign& imported_;
    diagnostic::Engine& diagnostics_;
    semantic::CompiledDesignResolver resolver_;
    std::map<std::string, const frontend::SystemVerilogClassDeclaration*, std::less<>> source_classes_;
};

class LinkedEnvironment final {
public:
    LinkedEnvironment(semantic::CompiledDesign& design,
        diagnostic::Engine& diagnostics)
        : design_(design)
        , diagnostics_(diagnostics)
    {
    }

    bool resolve()
    {
        auto& hir = design_.mutable_systemverilog();
        auto& units = hir.mutable_units();
        auto& declarations = hir.mutable_declarations();
        auto& types = hir.mutable_types();
        auto& classes = hir.mutable_classes();
        auto& expressions = hir.mutable_expressions();
        auto& statements = hir.mutable_statements();
        // No owning collection is resized during this pass. Indexed identities
        // remain valid while we annotate names and type references in place.
        design_.refresh_lookup_indexes();
        for (auto& unit : units) {
            for (auto& import : unit.imports)
                bind_package(import.package.spelling, unit, import.source, true);
            for (auto& item : unit.exports) {
                if (item.package.spelling != "*")
                    bind_package(item.package.spelling, unit, item.source, true);
            }
        }
        for (auto& declaration : declarations) {
            if (declaration.type)
                bind_type(*declaration.type, declaration.scope);
            if (declaration.default_type)
                bind_type(*declaration.default_type, declaration.scope);
            if (declaration.callable)
                bind_type(declaration.callable->return_type, declaration.scope);
        }
        for (auto& type : types) {
            const auto declaration = design_.find_declaration(type.declaration);
            if (!declaration || declaration->systemverilog == nullptr)
                continue;
            const auto scope = declaration->systemverilog->scope;
            bind_type(type.base, scope);
            for (auto& member : type.members)
                bind_type(member.type, scope);
            if (type.container && type.container->associative_index)
                bind_type(*type.container->associative_index, scope);
        }
        for (auto& declaration : classes) {
            for (auto& parameter : declaration.parameters) {
                if (parameter.type)
                    bind_type(*parameter.type, declaration.scope);
                if (parameter.default_type)
                    bind_type(*parameter.default_type, declaration.scope);
            }
            for (auto& property : declaration.properties)
                bind_type(property.type, declaration.scope);
            const auto relation = [&](sv::ClassRelation& base) {
                for (auto& actual : base.actuals) {
                    if (actual.type)
                        bind_type(*actual.type, declaration.scope);
                }
            };
            if (declaration.base)
                relation(*declaration.base);
            for (auto& base : declaration.extended_interfaces)
                relation(base);
            for (auto& base : declaration.implemented_interfaces)
                relation(base);
        }
        for (auto& expression : expressions) {
            if (expression.referenced_name)
                bind_name(*expression.referenced_name, expression.scope);
        }
        for (auto& statement : statements)
            bind_name(statement.task, statement.scope);
        design_.refresh_lookup_indexes();
        return !diagnostics_.has_error();
    }

private:
    diagnostic::SourceSpan source_span(semantic::SourceSpanId id) const
    {
        if (!id.valid() || id.value() >= design_.semantics.source_spans().size())
            return { };
        const auto& span = design_.semantics.source_spans()[id.value()];
        diagnostic::SourceSpan result;
        result.path = span.logical_name;
        if (span.file.valid() && span.file.value() < design_.semantics.source_files().size())
            result.path = result.path.empty()
                ? design_.semantics.source_files()[span.file.value()].physical_name : result.path;
        result.begin = { span.begin.line, span.begin.column, span.begin.offset };
        result.end = { span.end.line, span.end.column, span.end.offset };
        return result;
    }

    const sv::Unit* scope_unit(semantic::ScopeId scope) const
    {
        if (!scope.valid() || scope.value() >= design_.semantics.scopes().size())
            return nullptr;
        const auto unit = design_.find_unit(design_.semantics.scopes()[scope.value()].unit);
        return unit ? unit->systemverilog : nullptr;
    }

    const sv::Unit* package(const sv::Unit& owner, std::string_view spelling,
        semantic::SourceSpanId source, bool required)
    {
        semantic::CompiledDesignResolver resolver { design_, owner.id };
        if (const auto provider = resolver.find_systemverilog_package(owner.library, spelling))
            return provider->systemverilog;
        if (spelling.find("::") != std::string_view::npos) {
            if (required)
                diagnostics_.error("FSIM-SV-PACKAGE-002", "compiled package '"
                    + std::string { spelling } + "' was not found", source_span(source));
            return nullptr;
        }
        const sv::Unit* selected { };
        for (const auto& candidate : design_.systemverilog_units()) {
            if (candidate.kind != sv::UnitKind::package || candidate.name != spelling)
                continue;
            if (selected != nullptr) {
                diagnostics_.error("FSIM-SV-PACKAGE-001", "compiled package '"
                    + std::string { spelling } + "' is ambiguous between libraries '"
                    + selected->library + "' and '" + candidate.library + "'",
                    source_span(source));
                return nullptr;
            }
            selected = &candidate;
        }
        if (selected == nullptr && required)
            diagnostics_.error("FSIM-SV-PACKAGE-002", "compiled package '"
                + std::string { spelling } + "' was not found", source_span(source));
        return selected;
    }

    void bind_package(std::string& spelling, const sv::Unit& owner,
        semantic::SourceSpanId source, bool required)
    {
        const auto* provider = package(owner, spelling, source, required);
        if (provider == nullptr)
            return;
        if (provider->standard != owner.standard
            || provider->compatibility_profile != owner.compatibility_profile) {
            diagnostics_.error("FSIM-FE-STANDARD-003", "compiled SystemVerilog package '"
                + provider->library + "::" + provider->name
                + "' uses an incompatible standard or compatibility profile", source_span(source));
        }
        spelling = std::string { effective_library(provider->library) }
            + "::" + provider->name;
    }

    void qualify_member(std::string& spelling, const sv::Unit& owner,
        semantic::SourceSpanId source)
    {
        const auto separator = spelling.rfind("::");
        if (separator == std::string::npos)
            return;
        auto qualifier = spelling.substr(0, separator);
        const auto* provider = package(owner, qualifier, source, false);
        if (provider == nullptr)
            return;
        bind_package(qualifier, owner, source, false);
        spelling = qualifier + spelling.substr(separator);
    }

    void bind_name(sv::Name& name, semantic::ScopeId scope)
    {
        const auto* unit = scope_unit(scope);
        if (unit == nullptr || name.spelling.empty() || name.spelling.starts_with('@'))
            return;
        qualify_member(name.spelling, *unit, name.source);
        semantic::CompiledDesignResolver resolver { design_, unit->id };
        const auto resolved = resolver.resolve_systemverilog_name(name, scope, { }, false);
        if (resolved.status == semantic::CompiledResolutionStatus::ambiguous) {
            diagnostics_.error("FSIM-SV-PACKAGE-003", "name '" + name.spelling
                + "' is ambiguous in package import scope", source_span(name.source));
            return;
        }
        if (resolved.status == semantic::CompiledResolutionStatus::unique) {
            name.selected = resolved.unique();
            name.overloads = resolved.candidates;
        }
    }

    void bind_type(sv::TypeReference& type, semantic::ScopeId scope)
    {
        const auto* unit = scope_unit(scope);
        if (unit == nullptr)
            return;
        qualify_member(type.target.spelling, *unit, type.target.source);
        semantic::CompiledDesignResolver resolver { design_, unit->id };
        if (!type.target.target.valid() && !type.target.spelling.empty()) {
            const auto resolved = resolver.resolve_systemverilog_named_type(type.target.spelling, scope, false);
            if (resolved.status == semantic::CompiledResolutionStatus::ambiguous) {
                diagnostics_.error("FSIM-SV-PACKAGE-003", "type '" + type.target.spelling
                    + "' is ambiguous in package import scope", source_span(type.target.source));
            } else if (const auto id = resolved.unique()) {
                const auto declaration = design_.find_declaration(*id);
                if (declaration && declaration->systemverilog != nullptr
                    && declaration->systemverilog->declared_type)
                    type.target.target = *declaration->systemverilog->declared_type;
            }
        }
        if (type.class_identity.empty()) {
            const auto resolved = resolver.resolve_systemverilog_class(type.target.spelling, scope, false);
            if (const auto* declaration = resolved.unique()) {
                type.class_identity = declaration->canonical_identity;
                type.value_form = sv::TypeForm::class_handle;
                type.executable_width = 64U;
                type.four_state = false;
            }
        }
        for (auto& nested : type.container_element_types)
            bind_type(nested, scope);
        for (auto& actual : type.interface_parameter_actuals) {
            if (actual.type)
                bind_type(*actual.type, scope);
        }
        if (type.associative_index) {
            sv::TypeReference index;
            index.target = *type.associative_index;
            bind_type(index, scope);
            type.associative_index = index.target;
        }
    }

    semantic::CompiledDesign& design_;
    diagnostic::Engine& diagnostics_;
};

} // namespace

bool prepare_compiled_systemverilog_environment(frontend::ParsedDesign& parsed,
    const semantic::CompiledDesign& imported, diagnostic::Engine& diagnostics)
{
    return SourceEnvironment { imported, diagnostics }.prepare(parsed);
}

bool resolve_compiled_systemverilog_environment(semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics)
{
    return LinkedEnvironment { design, diagnostics }.resolve();
}

} // namespace fsim::app::application_detail
