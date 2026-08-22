// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_toggle_selection.hpp"

#include "fsim/elaboration/verilog_toggle_inventory.hpp"
#include "fsim/elaboration/vhdl_toggle_inventory.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageToggleSelectionError;
    using Reason = CoverageToggleExclusionReason;

    struct PendingExclusion {
        runtime::CodeCoveragePointId source_point;
        runtime::CodeCoveragePointId id;
        frontend::CodeCoverageLanguage language {
            frontend::CodeCoverageLanguage::SystemVerilog
        };
        std::string hierarchy_path;
        std::vector<Reason> reasons;
        CoverageToggleExcludedShape shape;
        std::size_t source_index { };
        frontend::CodeCoverageSourceSpan span;
        std::uint64_t line { };
    };

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>(
                (bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    void update_string(support::Sha256& hash, const std::string_view value) noexcept
    {
        update_u64(hash, value.size());
        hash.update(value);
    }

    std::uint64_t digest_word(
        const support::Sha256::Digest& digest, const std::size_t first) noexcept
    {
        std::uint64_t value { };
        for (std::size_t index = first; index < first + 8U; ++index) {
            value = (value << 8U) | digest[index];
        }
        return value;
    }

    runtime::CodeCoveragePointId exclusion_identity(
        const runtime::CodeCoveragePointId source_point,
        const CoverageInstanceIdentity instance,
        const std::string_view hierarchy_path,
        const std::span<const Reason> reasons) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageToggleSelectionSchema);
        update_u64(hash, source_point.high);
        update_u64(hash, source_point.low);
        update_u64(hash, instance.high);
        update_u64(hash, instance.low);
        update_string(hash, hierarchy_path);
        update_u64(hash, reasons.size());
        for (const auto reason : reasons) {
            update_u64(hash, static_cast<std::uint8_t>(reason));
        }
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    std::optional<frontend::CodeCoverageLanguage> coverage_language(
        const frontend::Language language) noexcept
    {
        switch (language) {
        case frontend::Language::Vhdl2008:
            return frontend::CodeCoverageLanguage::Vhdl;
        case frontend::Language::Verilog2005:
            return frontend::CodeCoverageLanguage::Verilog;
        case frontend::Language::SystemVerilog2017:
            return frontend::CodeCoverageLanguage::SystemVerilog;
        }
        return std::nullopt;
    }

    bool valid_unit_kind(const frontend::DesignUnit& unit) noexcept
    {
        switch (unit.kind) {
        case frontend::UnitKind::VerilogModule:
        case frontend::UnitKind::SystemVerilogInterface:
        case frontend::UnitKind::SystemVerilogProgram:
            return unit.language == frontend::Language::Verilog2005
                || unit.language == frontend::Language::SystemVerilog2017;
        case frontend::UnitKind::VhdlArchitecture:
            return unit.language == frontend::Language::Vhdl2008;
        case frontend::UnitKind::VhdlEntity:
        case frontend::UnitKind::VhdlConfiguration:
        case frontend::UnitKind::VhdlPackage:
        case frontend::UnitKind::VhdlContext:
        case frontend::UnitKind::SystemVerilogPackage:
        case frontend::UnitKind::VhdlPslVerificationUnit:
        case frontend::UnitKind::SystemVerilogConfiguration:
        case frontend::UnitKind::SystemVerilogBind:
            return false;
        }
        return false;
    }

    std::string unit_identity(const frontend::DesignUnit& unit)
    {
        const auto library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
            return "vhdl:" + std::string { library } + "." + unit.primary_name
                + "(" + unit.name + ")";
        }
        if (unit.kind == frontend::UnitKind::SystemVerilogInterface) {
            return "sv:" + std::string { library } + ".interface(" + unit.name
                + ")";
        }
        if (unit.kind == frontend::UnitKind::SystemVerilogProgram) {
            return "sv:" + std::string { library } + ".program(" + unit.name
                + ")";
        }
        return "sv:" + std::string { library } + "." + unit.name;
    }

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    std::vector<Reason> container_reasons(
        const frontend::Type& type, const bool retained_variable)
    {
        std::vector<Reason> reasons;
        if (type.systemverilog_container) {
            if (type.systemverilog_container->kind
                == frontend::SystemVerilogContainerKind::StaticArray) {
                reasons.push_back(Reason::Memory);
            }
            reasons.push_back(Reason::Array);
        }
        if (type.vhdl_array && !is_vhdl_toggle_type(type)) {
            if (retained_variable) {
                reasons.push_back(Reason::Memory);
            }
            reasons.push_back(Reason::Array);
        }
        return reasons;
    }

    std::optional<std::int64_t> integer_literal(
        const frontend::Expression& expression) noexcept
    {
        if (expression.kind == frontend::ExpressionKind::Unary
            && expression.operands.size() == 1U
            && (expression.text == "+" || expression.text == "-")) {
            const auto magnitude = integer_literal(expression.operands.front());
            if (!magnitude) {
                return std::nullopt;
            }
            if (expression.text == "+") {
                return magnitude;
            }
            if (*magnitude == std::numeric_limits<std::int64_t>::min()) {
                return std::nullopt;
            }
            return -*magnitude;
        }
        if (expression.kind != frontend::ExpressionKind::IntegerLiteral
            || expression.text.empty()) {
            return std::nullopt;
        }
        auto text = std::string_view { expression.text };
        if (text.front() == '+') {
            text.remove_prefix(1U);
        }
        std::int64_t value { };
        const auto [end, error]
            = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc { } || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    CoverageToggleExcludedShape excluded_shape(const frontend::Type& type)
    {
        using Kind = CoverageToggleExcludedContainerKind;
        CoverageToggleExcludedShape shape;
        if (type.systemverilog_container) {
            const auto& container = *type.systemverilog_container;
            switch (container.kind) {
            case frontend::SystemVerilogContainerKind::StaticArray:
                shape.kind = Kind::StaticArray;
                break;
            case frontend::SystemVerilogContainerKind::DynamicArray:
                shape.kind = Kind::DynamicArray;
                break;
            case frontend::SystemVerilogContainerKind::Queue:
                shape.kind = Kind::Queue;
                break;
            case frontend::SystemVerilogContainerKind::AssociativeArray:
                shape.kind = Kind::AssociativeArray;
                shape.string_index = container.associative_index_type
                    && container.associative_index_type->domain
                        == frontend::ValueDomain::String;
                break;
            }
            if (container.element_types.size() == 1U) {
                const auto& element = container.element_types.front();
                if (is_verilog_toggle_type(element)) {
                    shape.element_width = element.width().value_or(0U);
                }
            }
            if (shape.kind == Kind::StaticArray) {
                if (!container.static_range_expressions.empty()) {
                    for (const auto& dimension :
                        container.static_range_expressions) {
                        const auto left = integer_literal(dimension.left);
                        const auto right = integer_literal(dimension.right);
                        shape.dimensions.push_back({ left.value_or(0),
                            right.value_or(0), left.has_value() && right.has_value() });
                    }
                } else if (container.static_range) {
                    shape.dimensions.push_back({ container.static_range->left,
                        container.static_range->right, true });
                }
            }
            return shape;
        }
        if (type.vhdl_array) {
            shape.kind = Kind::VhdlArray;
            const auto& array = *type.vhdl_array;
            if (array.element_types.size() == 1U) {
                const auto& element = array.element_types.front();
                if (is_vhdl_toggle_type(element)) {
                    shape.element_width = element.width().value_or(0U);
                }
            }
            for (const auto& dimension : array.dimensions) {
                shape.dimensions.push_back(dimension.range
                        ? CoverageToggleExcludedDimension {
                              dimension.range->left, dimension.range->right, true }
                        : CoverageToggleExcludedDimension { });
            }
        }
        return shape;
    }

    std::string scope_component(const std::string_view family,
        const std::string_view name, const frontend::SourceSpan& span)
    {
        return "$" + std::string { family } + "("
            + (name.empty() ? std::string { "anonymous" } : std::string { name })
            + "@" + std::to_string(span.begin.offset) + ")";
    }

    bool exclusion_less(
        const PendingExclusion& left, const PendingExclusion& right) noexcept
    {
        return std::tie(left.hierarchy_path, left.reasons,
                   left.source_point.high, left.source_point.low)
            < std::tie(right.hierarchy_path, right.reasons,
                right.source_point.high, right.source_point.low);
    }

} // namespace

std::string_view coverage_toggle_exclusion_reason_name(
    const CoverageToggleExclusionReason reason) noexcept
{
    switch (reason) {
    case Reason::AutomaticLocal:
        return "automatic-local";
    case Reason::ProceduralLocal:
        return "procedural-local";
    case Reason::Memory:
        return "memory";
    case Reason::Array:
        return "array";
    }
    return "unknown";
}

CoverageToggleSelectionResult make_default_coverage_toggle_selection(
    const frontend::DesignUnit& unit,
    const std::span<const frontend::SignalDeclaration> ports,
    const CoverageInventoryOwner& owner,
    const std::span<const VerilogCoverageSource> sources,
    const CoverageToggleSelectionLimits limits) noexcept
{
    CoverageToggleSelectionResult result;
    const auto reject = [&](const Error error,
                            const std::size_t declaration = 0U) {
        result.selection.reset();
        result.error = error;
        result.declaration_index = declaration;
        return result;
    };

    try {
        const auto language = coverage_language(unit.language);
        if (!language || owner.language != unit.language) {
            return reject(Error::InvalidLanguage);
        }
        if (!valid_unit_kind(unit)) {
            return reject(Error::InvalidUnitKind);
        }
        const auto library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        if (owner.instance.empty() || owner.source.empty()
            || owner.library.empty() || owner.unit.empty()
            || owner.unit != unit_identity(unit) || owner.library != library
            || invalid_text(owner.instance)) {
            return reject(Error::InstanceOwnerMismatch);
        }
        const auto instance = make_coverage_instance_identity(
            { owner.instance, owner.language, owner.library, owner.unit,
                owner.parameter_identities },
            limits.instance_identity);
        if (!instance.ok()) {
            return reject(Error::InvalidInstanceIdentity);
        }
        if (sources.size() > limits.maximum_sources) {
            return reject(Error::ResourceLimit);
        }
        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        std::set<std::string> source_identities;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(Error::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(Error::InvalidSourceIdentity);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(Error::DuplicateSourceName);
            }
            if (!source_identities.emplace(
                                      frontend::code_coverage_source_identity_hex(
                                          source.identity))
                    .second) {
                return reject(Error::DuplicateSourceIdentity);
            }
        }

        std::vector<PendingExclusion> pending;
        std::set<std::string> paths;
        std::set<std::pair<std::uint64_t, std::uint64_t>> ids;
        std::size_t declarations = 0U;
        std::size_t reason_count = 0U;

        const auto add = [&](const frontend::VariableDeclaration& declaration,
                             const std::string_view scope,
                             std::vector<Reason> reasons,
                             CoverageToggleExcludedShape shape) -> Error {
            const auto declaration_index = declarations++;
            if (declarations > limits.maximum_declarations) {
                result.declaration_index = declaration_index;
                return Error::ResourceLimit;
            }
            std::ranges::sort(reasons);
            reasons.erase(std::unique(reasons.begin(), reasons.end()),
                reasons.end());
            if (reasons.empty()) {
                return Error::None;
            }
            if (declaration.name.empty()) {
                result.declaration_index = declaration_index;
                return Error::EmptyObjectName;
            }
            if (invalid_text(declaration.name)
                || declaration.name.size() > limits.maximum_name_bytes) {
                result.declaration_index = declaration_index;
                return declaration.name.size() > limits.maximum_name_bytes
                    ? Error::ResourceLimit
                    : Error::InvalidObjectName;
            }
            if (scope.size() >= limits.maximum_hierarchy_bytes
                || declaration.name.size()
                    >= limits.maximum_hierarchy_bytes - scope.size()) {
                result.declaration_index = declaration_index;
                return Error::ResourceLimit;
            }
            std::string path { scope };
            path.push_back('.');
            path.append(declaration.name);
            if (invalid_text(path)) {
                result.declaration_index = declaration_index;
                return Error::InvalidObjectName;
            }
            if (path.size() > limits.maximum_hierarchy_bytes) {
                result.declaration_index = declaration_index;
                return Error::ResourceLimit;
            }
            if (!paths.emplace(path).second) {
                result.declaration_index = declaration_index;
                return Error::DuplicateObjectPath;
            }
            const auto physical = frontend::physical_source(declaration.span);
            const auto source = source_by_name.find(physical);
            if (source == source_by_name.end()) {
                result.declaration_index = declaration_index;
                return Error::UnknownObjectSource;
            }
            if (physical != owner.source
                && std::ranges::find(owner.source_dependencies, physical)
                    == owner.source_dependencies.end()) {
                result.declaration_index = declaration_index;
                return Error::ObjectSourceOwnershipMismatch;
            }
            const frontend::CodeCoverageSourceSpan span {
                static_cast<std::uint64_t>(declaration.span.begin.offset),
                static_cast<std::uint64_t>(declaration.span.end.offset),
            };
            const auto source_point
                = frontend::make_code_coverage_point_identity(
                    sources[source->second].identity, *language,
                    frontend::CodeCoverageConstructKind::ToggleObject, span);
            if (!source_point.ok()) {
                result.declaration_index = declaration_index;
                return Error::InvalidObjectSpan;
            }
            if (declaration.span.begin.line == 0U
                || declaration.span.begin.line > limits.maximum_line_number) {
                result.declaration_index = declaration_index;
                return Error::InvalidObjectLine;
            }
            if (pending.size() >= limits.maximum_exclusions
                || reasons.size() > limits.maximum_reasons - reason_count) {
                result.declaration_index = declaration_index;
                return Error::ResourceLimit;
            }
            reason_count += reasons.size();
            const auto id = exclusion_identity(*source_point.identity,
                *instance.identity, path, reasons);
            if (!runtime::is_code_coverage_identity_valid(id)
                || !ids.emplace(id.high, id.low).second) {
                result.declaration_index = declaration_index;
                return Error::DuplicateExclusionIdentity;
            }
            pending.push_back(PendingExclusion { *source_point.identity, id,
                *language, std::move(path), std::move(reasons),
                std::move(shape), source->second, span,
                static_cast<std::uint64_t>(declaration.span.begin.line) });
            return Error::None;
        };

        const auto add_signal = [&](const frontend::SignalDeclaration& signal,
                                    const std::string_view scope) -> Error {
            frontend::VariableDeclaration declaration;
            declaration.name = signal.name;
            declaration.type = signal.type;
            declaration.span = signal.span;
            return add(declaration, scope,
                container_reasons(signal.type, false),
                excluded_shape(signal.type));
        };
        const auto add_variable = [&](const frontend::VariableDeclaration& variable,
                                      const std::string_view scope,
                                      const std::optional<Reason> local) -> Error {
            auto reasons = container_reasons(variable.type,
                variable.vhdl_shared);
            if (local) {
                reasons.push_back(*local);
            }
            return add(variable, scope, std::move(reasons),
                excluded_shape(variable.type));
        };

        for (const auto& port : ports) {
            if (const auto error = add_signal(port, owner.instance);
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
        }
        for (const auto& signal : unit.signals) {
            if (const auto error = add_signal(signal, owner.instance);
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
        }
        for (const auto& variable : unit.variables) {
            if (const auto error = add_variable(variable, owner.instance, { });
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
        }

        std::function<Error(std::span<const frontend::Statement>,
            const std::string&, std::size_t)>
            scan_statements;
        scan_statements = [&](const std::span<const frontend::Statement> statements,
                              const std::string& scope,
                              const std::size_t depth) -> Error {
            if (depth > limits.maximum_scope_depth) {
                return Error::ResourceLimit;
            }
            for (const auto& statement : statements) {
                const auto block_scope = scope + "."
                    + scope_component("block", statement.label, statement.span);
                for (const auto& variable : statement.declarations) {
                    if (const auto error = add_variable(variable, block_scope,
                            Reason::ProceduralLocal);
                        error != Error::None) {
                        return error;
                    }
                }
                if (const auto error = scan_statements(statement.statements,
                        block_scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
                if (const auto error = scan_statements(statement.else_statements,
                        block_scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
                if (const auto error = scan_statements(statement.loop_updates,
                        block_scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
                for (const auto& alternative : statement.case_alternatives) {
                    if (const auto error = scan_statements(
                            alternative.statements, block_scope, depth + 1U);
                        error != Error::None) {
                        return error;
                    }
                }
            }
            return Error::None;
        };

        std::function<Error(const frontend::FunctionDeclaration&,
            const std::string&, std::size_t)>
            scan_function;
        std::function<Error(const frontend::ProcedureDeclaration&,
            const std::string&, std::size_t)>
            scan_procedure;
        scan_function = [&](const frontend::FunctionDeclaration& function,
                            const std::string& parent,
                            const std::size_t depth) -> Error {
            if (depth > limits.maximum_scope_depth) {
                return Error::ResourceLimit;
            }
            const auto scope = parent + "."
                + scope_component("function", function.name, function.span);
            const bool automatic = function.language
                    == frontend::Language::Vhdl2008
                || function.automatic;
            for (const auto& variable : function.variables) {
                if (const auto error = add_variable(variable, scope,
                        automatic ? std::optional { Reason::AutomaticLocal }
                                  : std::nullopt);
                    error != Error::None) {
                    return error;
                }
            }
            for (const auto& nested : function.functions) {
                if (const auto error = scan_function(nested, scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
            }
            for (const auto& nested : function.procedures) {
                if (const auto error = scan_procedure(nested, scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
            }
            return scan_statements(function.statements, scope, depth + 1U);
        };
        scan_procedure = [&](const frontend::ProcedureDeclaration& procedure,
                             const std::string& parent,
                             const std::size_t depth) -> Error {
            if (depth > limits.maximum_scope_depth) {
                return Error::ResourceLimit;
            }
            const auto scope = parent + "."
                + scope_component("procedure", procedure.name, procedure.span);
            for (const auto& variable : procedure.variables) {
                if (const auto error = add_variable(variable, scope,
                        Reason::AutomaticLocal);
                    error != Error::None) {
                    return error;
                }
            }
            for (const auto& nested : procedure.functions) {
                if (const auto error = scan_function(nested, scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
            }
            for (const auto& nested : procedure.procedures) {
                if (const auto error = scan_procedure(nested, scope, depth + 1U);
                    error != Error::None) {
                    return error;
                }
            }
            return scan_statements(procedure.statements, scope, depth + 1U);
        };

        for (const auto& process : unit.processes) {
            const auto scope = std::string { owner.instance } + "."
                + scope_component("process", process.name, process.span);
            for (const auto& variable : process.variables) {
                if (const auto error = add_variable(variable, scope,
                        Reason::ProceduralLocal);
                    error != Error::None) {
                    return reject(error, result.declaration_index);
                }
            }
            if (const auto error = scan_statements(
                    process.statements, scope, 1U);
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
            for (const auto& function : process.functions) {
                if (const auto error = scan_function(function, scope, 1U);
                    error != Error::None) {
                    return reject(error, result.declaration_index);
                }
            }
            for (const auto& procedure : process.procedures) {
                if (const auto error = scan_procedure(procedure, scope, 1U);
                    error != Error::None) {
                    return reject(error, result.declaration_index);
                }
            }
        }
        for (const auto& function : unit.functions) {
            if (const auto error = scan_function(function,
                    std::string { owner.instance }, 1U);
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
        }
        for (const auto& task : unit.tasks) {
            const auto scope = std::string { owner.instance } + "."
                + scope_component("task", task.name, task.span);
            for (const auto& variable : task.variables) {
                if (const auto error = add_variable(variable, scope,
                        task.automatic
                            ? std::optional { Reason::AutomaticLocal }
                            : std::nullopt);
                    error != Error::None) {
                    return reject(error, result.declaration_index);
                }
            }
            if (const auto error = scan_statements(task.statements, scope, 1U);
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
        }
        for (const auto& procedure : unit.procedures) {
            if (const auto error = scan_procedure(procedure,
                    std::string { owner.instance }, 1U);
                error != Error::None) {
                return reject(error, result.declaration_index);
            }
        }

        std::ranges::sort(pending, exclusion_less);
        CoverageToggleSelection selection;
        selection.instance_identity = *instance.identity;
        selection.specialization = owner.specialization;
        selection.instance = owner.instance;
        selection.exclusions.reserve(pending.size());
        for (auto& exclusion : pending) {
            selection.exclusions.push_back(CoverageToggleExclusion {
                exclusion.source_point, exclusion.id, *instance.identity,
                owner.specialization, exclusion.language,
                std::move(exclusion.hierarchy_path),
                std::move(exclusion.reasons), std::move(exclusion.shape),
                exclusion.source_index,
                exclusion.span, exclusion.line });
        }
        result.selection = std::move(selection);
        result.error = Error::None;
        result.declaration_index = 0U;
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit, result.declaration_index);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit, result.declaration_index);
    }
}

} // namespace fsim::elaboration
