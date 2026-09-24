// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_internal.hpp"

namespace fsim::app {
namespace codec_detail {

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_SV_HIR) \
    || defined(FSIM_DESIGN_ARTIFACT_CODEC_VHDL_HIR)
    template <typename Id>
    bool valid_vhdl_hir_id(const Id id, const semantic::Model& semantics)
    {
        if (!id.valid()) {
            return true;
        }
        if constexpr (std::same_as<Id, semantic::SourceFileId>) {
            return id.value() < semantics.source_files().size();
        } else if constexpr (std::same_as<Id, semantic::ExpansionId>) {
            return id.value() < semantics.expansions().size();
        } else if constexpr (std::same_as<Id, semantic::SourceSpanId>) {
            return id.value() < semantics.source_spans().size();
        } else if constexpr (std::same_as<Id, semantic::OriginId>) {
            return id.value() < semantics.origins().size();
        } else if constexpr (std::same_as<Id, semantic::ScopeId>) {
            return id.value() < semantics.scopes().size();
        } else if constexpr (std::same_as<Id, semantic::UnitId>) {
            return id.value() < semantics.units().size();
        } else if constexpr (std::same_as<Id, semantic::DeclarationId>) {
            return id.value() < semantics.declarations().size();
        } else if constexpr (std::same_as<Id, semantic::TypeId>) {
            return id.value() < semantics.types().size();
        } else if constexpr (std::same_as<Id, semantic::ValueId>) {
            return id.value() < semantics.values().size();
        } else if constexpr (std::same_as<Id, semantic::ExpressionId>) {
            return id.value() < semantics.expression_identities().size();
        } else if constexpr (std::same_as<Id, semantic::StatementId>) {
            return id.value() < semantics.statement_identities().size();
        } else if constexpr (std::same_as<Id, semantic::ProcessId>) {
            return id.value() < semantics.process_identities().size();
        } else if constexpr (std::same_as<Id, semantic::InstanceId>) {
            return id.value() < semantics.instances().size();
        } else {
            return true;
        }
    }

    template <typename T>
    bool valid_vhdl_hir_links(
        const T& value,
        const semantic::Model& semantics,
        const std::size_t depth = 0)
    {
        if (depth > kMaximumNesting) {
            return false;
        }
        using Value = std::remove_cv_t<T>;
        if constexpr (requires { value.generated_text; }) {
            if (!semantic::sv::generated_text_kind_valid(
                    value.generated_text)) {
                return false;
            }
        }
        if constexpr (requires { value.output_generated_text; }) {
            if (!semantic::sv::generated_text_kind_valid(
                    value.output_generated_text)) {
                return false;
            }
        }
        if constexpr (std::same_as<Value, semantic::sv::SourceToken>) {
            return value.source.valid()
                && value.source.value() < semantics.source_spans().size()
                && value.kind <= static_cast<std::uint16_t>(
                    frontend::TokenKind::EndOfFile)
                && (value.generated_text
                        == semantic::sv::GeneratedTextKind::none
                    || value.kind == static_cast<std::uint16_t>(
                        frontend::TokenKind::StringLiteral));
        } else if constexpr (
            std::same_as<Value, semantic::sv::GeneratedTextKind>) {
            return semantic::sv::generated_text_kind_valid(value);
        } else if constexpr (DenseId<Value>) {
            return valid_vhdl_hir_id(value, semantics);
        } else if constexpr (std::is_enum_v<Value>) {
            return valid_archive_enum(value);
        } else if constexpr (
            std::is_arithmetic_v<Value> || std::same_as<Value, std::string>
            || std::same_as<Value, std::filesystem::path>
            || std::same_as<Value, runtime::PackedLogic4>) {
            return true;
        } else if constexpr (IsVector<Value>::value
            || IsSpan<Value>::value || IsArray<Value>::value) {
            return std::ranges::all_of(value, [&](const auto& item) {
                return valid_vhdl_hir_links(item, semantics, depth + 1U);
            });
        } else if constexpr (IsOptional<Value>::value) {
            return !value
                || valid_vhdl_hir_links(*value, semantics, depth + 1U);
        } else if constexpr (IsVariant<Value>::value) {
            return std::visit(
                [&](const auto& item) {
                    return valid_vhdl_hir_links(item, semantics, depth + 1U);
                },
                value);
        } else if constexpr (IsPair<Value>::value) {
            return valid_vhdl_hir_links(value.first, semantics, depth + 1U)
                && valid_vhdl_hir_links(value.second, semantics, depth + 1U);
        } else if constexpr (IsSharedPtr<Value>::value) {
            return !value
                || valid_vhdl_hir_links(*value, semantics, depth + 1U);
        } else if constexpr (StorageWrapper<Value>) {
            return valid_vhdl_hir_links(value.storage, semantics, depth + 1U);
        } else if constexpr (requires { archive_fields(value); }) {
            bool valid = true;
            std::apply(
                [&](const auto&... fields) {
                    ((valid = valid
                             && valid_vhdl_hir_links(
                                 fields, semantics, depth + 1U)),
                        ...);
                },
                archive_fields(value));
            return valid;
        } else if constexpr (std::is_aggregate_v<Value>) {
            bool valid = true;
            boost::pfr::for_each_field(value, [&](const auto& field) {
                valid = valid
                    && valid_vhdl_hir_links(field, semantics, depth + 1U);
            });
            return valid;
        } else {
            return false;
        }
    }

    template <typename Records, typename Projection>
    bool unique_required_ids(const Records& records, Projection projection)
    {
        std::set<std::uint32_t> identities;
        return std::ranges::all_of(records, [&](const auto& record) {
            const auto id = std::invoke(projection, record);
            return id.valid() && identities.insert(id.value()).second;
        });
    }

    template <typename Records>
    auto validation_span(const Records& records)
    {
        using Record = typename Records::value_type;
        if constexpr (requires { records.source(); }) {
            return records.source();
        } else {
            return std::span<const Record> {
                records.data(), records.size() };
        }
    }

#endif

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_VHDL_HIR)
    template <typename State>
    bool valid_vhdl_hir_state(
        const State& state,
        const semantic::Model& semantics)
    {
        return valid_vhdl_hir_links(state, semantics)
            && semantic::vhdl::top_level_hir_collections_well_formed(
                semantics,
                { state.declarations, state.overload_sets,
                    state.instances })
            && unique_required_ids(state.units, &semantic::vhdl::Unit::id)
            && unique_required_ids(
                state.declarations, &semantic::vhdl::Declaration::id)
            && unique_required_ids(state.types, &semantic::vhdl::TypeDefinition::id)
            && unique_required_ids(
                state.expressions, &semantic::vhdl::Expression::id)
            && unique_required_ids(
                state.statements, &semantic::vhdl::Statement::id)
            && unique_required_ids(state.processes, &semantic::vhdl::Process::id);
    }

#endif

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_SV_HIR)
    template <typename State>
    std::string systemverilog_hir_state_error(
        const State& state,
        const semantic::Model& semantics)
    {
        const auto invalid_record = [&](const auto& records,
                                        const std::string_view name)
            -> std::string {
            for (std::size_t index { }; index < records.size(); ++index) {
                if (!valid_vhdl_hir_links(records[index], semantics)) {
                    if constexpr (std::same_as<
                                      std::remove_cvref_t<decltype(
                                          records[index])>,
                                      semantic::sv::Declaration>) {
                        std::size_t field_index { };
                        std::optional<std::size_t> invalid_field;
                        boost::pfr::for_each_field(
                            records[index], [&](const auto& field) {
                                if (!invalid_field
                                    && !valid_vhdl_hir_links(
                                        field, semantics, 1U)) {
                                    invalid_field = field_index;
                                }
                                ++field_index;
                        });
                        if (invalid_field) {
                            constexpr std::array declaration_field_names {
                                "id", "scope", "form", "name", "source",
                                "origin", "declared_type", "declared_value",
                                "type", "default_type", "initializer",
                                "delay", "drive_zero", "drive_one",
                                "charge_strength", "charge_decay",
                                "direction", "const_reference",
                                "static_reference", "interface_type",
                                "modport", "lifetime", "nested_scope",
                                "callable", "children", "statements",
                            };
                            const auto field_name
                                = *invalid_field
                                      < declaration_field_names.size()
                                ? declaration_field_names[*invalid_field]
                                : "unknown";
                            return std::string { name } + " record "
                                + std::to_string(index) + " field "
                                + std::to_string(*invalid_field) + " ("
                                + field_name + ")"
                                + " has invalid links, enums, or origins";
                        }
                    }
                    return std::string { name } + " record "
                        + std::to_string(index)
                        + " has invalid links, enums, or origins";
                }
            }
            return { };
        };
        for (const auto& error : {
                 invalid_record(state.units, "unit"),
                 invalid_record(state.declarations, "declaration"),
                 invalid_record(state.types, "type"),
                 invalid_record(state.expressions, "expression"),
                 invalid_record(state.statements, "statement"),
                 invalid_record(state.processes, "process"),
                 invalid_record(state.classes, "class"),
                 invalid_record(state.instances, "instance"),
                 invalid_record(state.udps, "UDP"),
                 invalid_record(state.dpi_declarations, "DPI"),
                 invalid_record(
                     state.covergroup_instances, "covergroup instance") }) {
            if (!error.empty()) {
                return error;
            }
        }
        if (!semantic::sv::top_level_hir_collections_well_formed(
                semantics,
                { validation_span(state.instances),
                    validation_span(state.udps),
                    validation_span(state.dpi_declarations),
                    validation_span(state.covergroup_instances) })) {
            return "top-level instance, UDP, DPI, or coverage records are invalid";
        }
        if (auto error = semantic::sv::class_hir_error(
                semantics, validation_span(state.units),
                validation_span(state.declarations),
                validation_span(state.statements),
                validation_span(state.classes));
            !error.empty()) {
            return error;
        }
        if (!unique_required_ids(state.units, &semantic::sv::Unit::id)) {
            return "unit IDs are missing or duplicated";
        }
        if (!unique_required_ids(
                state.declarations, &semantic::sv::Declaration::id)) {
            return "declaration IDs are missing or duplicated";
        }
        if (!unique_required_ids(
                state.types, &semantic::sv::TypeDefinition::id)) {
            return "type IDs are missing or duplicated";
        }
        if (!unique_required_ids(
                state.expressions, &semantic::sv::Expression::id)) {
            return "expression IDs are missing or duplicated";
        }
        if (!unique_required_ids(
                state.statements, &semantic::sv::Statement::id)) {
            return "statement IDs are missing or duplicated";
        }
        if (!unique_required_ids(
                state.processes, &semantic::sv::Process::id)) {
            return "process IDs are missing or duplicated";
        }
        return { };
    }

    template <typename State>
    bool valid_systemverilog_hir_state(
        const State& state,
        const semantic::Model& semantics)
    {
        return systemverilog_hir_state_error(state, semantics).empty();
    }

#endif


} // namespace codec_detail

using namespace codec_detail;

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_SV_HIR)
namespace codec_detail {

std::optional<std::string> serialize_systemverilog_constraint_hir_state(
    const SystemVerilogConstraintHirView& state,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    if (const auto error = systemverilog_hir_state_error(
            state, semantics);
        !error.empty()) {
        diagnostics.error(
            std::string { kCode },
            "SystemVerilog HIR state is structurally invalid: " + error);
        return std::nullopt;
    }
    return serialize(
        "FSIMSVCH", kSystemVerilogConstraintHirStateSchema,
        state, diagnostics);
}

} // namespace codec_detail

std::optional<std::string> serialize_systemverilog_constraint_hir_state(
    const semantic::sv::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    const SystemVerilogConstraintHirView state {
        hir.units(),
        hir.declarations(),
        hir.types(),
        hir.expressions(),
        hir.statements(),
        hir.processes(),
        hir.classes(),
        hir.instances(),
        hir.udps(),
        hir.dpi_declarations(),
        hir.covergroup_instances()
    };
    return codec_detail::serialize_systemverilog_constraint_hir_state(
        state, semantics, diagnostics);
}

std::optional<semantic::sv::Hir>
deserialize_systemverilog_constraint_hir_state(
    const std::string_view bytes,
    std::string source_name,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    return translate_decode_allocation_failure<semantic::sv::Hir>(
        source_name, diagnostics, [&] {
            DecodeBudget budget;
            return deserialize_systemverilog_constraint_hir_state_with_budget(
                bytes, source_name, semantics, diagnostics, budget);
        });
}

namespace codec_detail {

std::optional<semantic::sv::Hir>
deserialize_systemverilog_constraint_hir_state_with_budget(
    const std::string_view bytes,
    std::string source_name,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics,
    DecodeBudget& budget)
{
    auto state = deserialize<SystemVerilogConstraintHirState>(
        "FSIMSVCH", kSystemVerilogConstraintHirStateSchema,
        bytes, std::move(source_name), diagnostics, &budget);
    if (!state || !valid_systemverilog_hir_state(*state, semantics)) {
        if (state && !diagnostics.has_error()) {
            diagnostics.error(
                std::string { kCode },
                "SystemVerilog HIR state is structurally invalid");
        }
        return std::nullopt;
    }
    semantic::sv::Hir hir;
    hir.mutable_units() = std::move(state->units);
    hir.mutable_declarations() = std::move(state->declarations);
    hir.mutable_types() = std::move(state->types);
    hir.mutable_expressions() = std::move(state->expressions);
    hir.mutable_statements() = std::move(state->statements);
    hir.mutable_processes() = std::move(state->processes);
    hir.mutable_classes() = std::move(state->classes);
    hir.mutable_instances() = std::move(state->instances);
    hir.mutable_udps() = std::move(state->udps);
    hir.mutable_dpi_declarations() = std::move(state->dpi_declarations);
    hir.mutable_covergroup_instances()
        = std::move(state->covergroup_instances);
    return hir;
}

} // namespace codec_detail

#endif

#if defined(FSIM_DESIGN_ARTIFACT_CODEC_VHDL_HIR)
namespace codec_detail {

std::optional<std::string> serialize_vhdl_hir_state(
    const VhdlHirView& state,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    if (!valid_vhdl_hir_state(state, semantics)) {
        diagnostics.error(
            std::string { kCode },
            "VHDL/PSL HIR state is structurally inconsistent with semantic state");
        return std::nullopt;
    }
    return serialize("FSIMVHIR", kVhdlHirStateSchema, state, diagnostics);
}

} // namespace codec_detail

std::optional<std::string> serialize_vhdl_hir_state(
    const semantic::vhdl::Hir& hir,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    const VhdlHirView state {
        hir.units(),
        hir.declarations(),
        hir.types(),
        hir.overload_sets(),
        hir.expressions(),
        hir.statements(),
        hir.processes(),
        hir.instances()
    };
    return codec_detail::serialize_vhdl_hir_state(
        state, semantics, diagnostics);
}

std::optional<semantic::vhdl::Hir> deserialize_vhdl_hir_state(
    const std::string_view bytes,
    std::string source_name,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics)
{
    return translate_decode_allocation_failure<semantic::vhdl::Hir>(
        source_name, diagnostics, [&] {
            DecodeBudget budget;
            return deserialize_vhdl_hir_state_with_budget(
                bytes, source_name, semantics, diagnostics, budget);
        });
}

namespace codec_detail {

std::optional<semantic::vhdl::Hir> deserialize_vhdl_hir_state_with_budget(
    const std::string_view bytes,
    std::string source_name,
    const semantic::Model& semantics,
    diagnostic::Engine& diagnostics,
    DecodeBudget& budget)
{
    auto state = deserialize<VhdlHirState>(
        "FSIMVHIR", kVhdlHirStateSchema, bytes, std::move(source_name),
        diagnostics, &budget);
    if (!state || !valid_vhdl_hir_state(*state, semantics)) {
        if (state && !diagnostics.has_error()) {
            diagnostics.error(
                std::string { kCode },
                "VHDL/PSL HIR state is structurally inconsistent with semantic state");
        }
        return std::nullopt;
    }
    semantic::vhdl::Hir hir;
    hir.mutable_units() = std::move(state->units);
    hir.mutable_declarations() = std::move(state->declarations);
    hir.mutable_types() = std::move(state->types);
    hir.mutable_overload_sets() = std::move(state->overload_sets);
    hir.mutable_expressions() = std::move(state->expressions);
    hir.mutable_statements() = std::move(state->statements);
    hir.mutable_processes() = std::move(state->processes);
    hir.mutable_instances() = std::move(state->instances);
    return hir;
}

} // namespace codec_detail

#endif

std::optional<std::string> serialize_compiled_hir_bundle(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics)
{
    if (!design.valid()) {
        diagnostics.error(
            std::string { kCode },
            "compiled HIR bundle is structurally invalid");
        return std::nullopt;
    }
    auto semantic_state = serialize_validated_semantic_state(
        design.semantics, diagnostics);
    auto systemverilog_hir_state
        = serialize_systemverilog_constraint_hir_state(
            design.systemverilog_hir, design.semantics, diagnostics);
    auto vhdl_hir_state = serialize_vhdl_hir_state(
        design.vhdl_hir, design.semantics, diagnostics);
    if (!semantic_state || !systemverilog_hir_state || !vhdl_hir_state) {
        return std::nullopt;
    }
    CompiledHirBundleState state {
        std::move(*semantic_state),
        std::move(*systemverilog_hir_state),
        std::move(*vhdl_hir_state),
        { design.dependencies().begin(), design.dependencies().end() },
        { design.references().begin(), design.references().end() },
    };
    return serialize(
        "FSIMCHIR", kCompiledHirBundleSchema, state, diagnostics);
}

namespace application_detail {

std::optional<std::string>
serialize_cache_compiled_hir_bundle_with_projected_sources(
    const semantic::CompiledDesign& design,
    semantic::Model projected_semantics,
    const std::span<const semantic::CompiledDependency>
        projected_dependencies,
    const codec_detail::SystemVerilogConstraintHirView& projected_systemverilog,
    const std::span<const semantic::vhdl::Unit> projected_vhdl_units,
    diagnostic::Engine& diagnostics)
{
    if (!design.valid()) {
        diagnostics.error(
            std::string { kCode },
            "compiled HIR bundle is structurally invalid");
        return std::nullopt;
    }
    auto semantic_state = codec_detail::serialize_validated_semantic_state(
        projected_semantics, diagnostics);
    auto systemverilog_hir_state
        = codec_detail::serialize_systemverilog_constraint_hir_state(
            projected_systemverilog, projected_semantics, diagnostics);
    const VhdlHirView vhdl_view {
        projected_vhdl_units,
        design.vhdl_hir.declarations(),
        design.vhdl_hir.types(),
        design.vhdl_hir.overload_sets(),
        design.vhdl_hir.expressions(),
        design.vhdl_hir.statements(),
        design.vhdl_hir.processes(),
        design.vhdl_hir.instances()
    };
    auto vhdl_hir_state = codec_detail::serialize_vhdl_hir_state(
        vhdl_view, projected_semantics, diagnostics);
    if (!semantic_state || !systemverilog_hir_state || !vhdl_hir_state) {
        return std::nullopt;
    }
    const codec_detail::CompiledHirBundleView state {
        std::move(*semantic_state),
        std::move(*systemverilog_hir_state),
        std::move(*vhdl_hir_state),
        projected_dependencies,
        design.references()
    };
    return codec_detail::serialize(
        "FSIMCHIR", kCompiledHirBundleSchema,
        state, diagnostics);
}

} // namespace application_detail

std::optional<semantic::CompiledDesign> deserialize_compiled_hir_bundle(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    return translate_decode_allocation_failure<semantic::CompiledDesign>(
        source_name, diagnostics, [&] {
            DecodeBudget budget;
            return deserialize_compiled_hir_bundle_with_budget(
                bytes, source_name, diagnostics, budget);
        });
}

namespace codec_detail {

std::optional<semantic::CompiledDesign>
deserialize_compiled_hir_bundle_with_budget(
    const std::string_view bytes,
    const std::string& source_name,
    diagnostic::Engine& diagnostics,
    DecodeBudget& budget)
{
    auto state = deserialize<CompiledHirBundleState>(
        "FSIMCHIR", kCompiledHirBundleSchema, bytes,
        source_name, diagnostics, &budget, "compiled-HIR bundle");
    if (!state) {
        return std::nullopt;
    }
    auto semantics = deserialize_semantic_state_with_budget(
        state->semantic_state, source_name + ":semantics", diagnostics,
        budget);
    if (!semantics) {
        return std::nullopt;
    }
    auto systemverilog
        = deserialize_systemverilog_constraint_hir_state_with_budget(
        state->systemverilog_hir_state,
        source_name + ":systemverilog-hir", *semantics, diagnostics, budget);
    auto vhdl = deserialize_vhdl_hir_state_with_budget(
        state->vhdl_hir_state,
        source_name + ":vhdl-hir", *semantics, diagnostics, budget);
    if (!systemverilog || !vhdl) {
        return std::nullopt;
    }
    semantic::CompiledDesign design {
        std::move(*semantics),
        std::move(*systemverilog),
        std::move(*vhdl),
        std::move(state->dependencies),
        std::move(state->references),
    };
    if (!design.valid()) {
        diagnostics.error(
            std::string { kCode },
            "compiled HIR bundle is structurally invalid");
        return std::nullopt;
    }
    return design;
}

} // namespace codec_detail

} // namespace fsim::app
