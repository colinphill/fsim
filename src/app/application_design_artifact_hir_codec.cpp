// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_internal.hpp"

#include "fsim/semantic/compiled_design_linker.hpp"

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
                                "array_parameter", "array_local_parameter",
                                "local_type_parameter",
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
        for (std::size_t index { }; index < state.statements.size(); ++index) {
            const auto& statement = state.statements[index];
            if (statement.imports.empty()) {
                continue;
            }
            if (statement.kind != semantic::sv::StatementKind::block
                || !statement.nested_scope) {
                return "block imports are attached to a statement without a "
                       "nested block scope";
            }
            for (const auto& import : statement.imports) {
                if (import.package.spelling.empty()
                    || import.wildcard == import.member.has_value()
                    || (import.member
                        && import.member->spelling.empty())) {
                    return "block imports contain an invalid package/member "
                           "selection";
                }
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
        for (const auto& unit : state.units) {
            if (!unit.port_slots) {
                continue;
            }
            std::unordered_map<std::string_view, std::size_t> slot_counts;
            for (const auto& slot : *unit.port_slots) {
                if (slot) {
                    ++slot_counts[*slot];
                }
            }
            for (const auto& [name, count] : slot_counts) {
                if (name.empty() || count != 1U) {
                    return "unit port slots contain an empty or duplicate "
                           "port name";
                }
            }
            std::unordered_map<std::string_view, std::size_t> declared_ports;
            for (const auto id : unit.declarations) {
                const auto declaration = std::ranges::find(
                    state.declarations, id,
                    &semantic::sv::Declaration::id);
                if (declaration != state.declarations.end()
                    && (*declaration).scope == unit.scope
                    && (*declaration).form
                        == semantic::sv::DeclarationForm::port) {
                    ++declared_ports[(*declaration).name];
                }
            }
            if (declared_ports.size() != slot_counts.size()) {
                return "unit port slots do not name every port declaration";
            }
            for (const auto& [name, count] : declared_ports) {
                const auto slot = slot_counts.find(name);
                if (count != 1U || slot == slot_counts.end()
                    || slot->second != 1U) {
                    return "unit port slots disagree with port declarations";
                }
            }
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
    // A trusted load skips the structural checks the producer ran before
    // serializing the state (FSIM_VERIFY_ARTIFACT=1 keeps them).
    if (!state
        || (!semantic::TrustedCompiledHirLoad::active()
            && !valid_systemverilog_hir_state(*state, semantics))) {
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
    // Trusted loads skip the structural walk; verify mode keeps it.
    if (!state
        || (!elaboration::detail::TrustedArtifactLoad::active()
            && !valid_vhdl_hir_state(*state, semantics))) {
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

namespace codec_detail {

// Writes the FSIMCHIR bundle in one pass: each inner state goes straight into
// the outer buffer behind a patched u64 length (the layout of a u64-prefixed
// string), followed by the dependencies and references.
std::optional<std::string> serialize_compiled_hir_bundle_parts(
    const semantic::Model& semantics,
    const SystemVerilogConstraintHirView& systemverilog,
    const VhdlHirView& vhdl,
    const std::span<const semantic::CompiledDependency> dependencies,
    const std::span<const semantic::CompiledReference> references,
    diagnostic::Engine& diagnostics,
    const bool check_states)
{
    const auto records = model_records_view(semantics);
    constexpr std::string_view magic = "FSIMCHIR";
    std::size_t capacity = add_serialization_capacity(
        add_serialization_capacity(magic.size(), sizeof(std::uint32_t)),
        add_serialization_capacity(
            serialization_capacity_hint(dependencies),
            serialization_capacity_hint(references)));
    for (const auto hint : { serialization_capacity_hint(records),
             serialization_capacity_hint(systemverilog),
             serialization_capacity_hint(vhdl) }) {
        capacity = add_serialization_capacity(
            capacity, add_serialization_capacity(
                          sizeof(std::uint64_t) + 8U + sizeof(std::uint32_t),
                          hint));
    }
    Writer writer { capacity };
    writer.raw(magic);
    writer.write(kCompiledHirBundleSchema);
    const auto write_inner = [&](const std::string_view inner_magic,
                                 const std::uint32_t schema,
                                 const auto& value) {
        const auto at = writer.begin_length_prefixed();
        writer.raw(inner_magic);
        writer.write(schema);
        writer.write(value);
        if (!writer.failure().empty()) {
            diagnostics.error(std::string { kCode }, writer.failure());
            return false;
        }
        writer.end_length_prefixed(at);
        return true;
    };
    bool ok = portable_semantics(semantics.source_files(), diagnostics)
        && write_inner("FSIMSEM1", kSemanticStateSchema, records);
    if (const auto error = check_states
            ? systemverilog_hir_state_error(systemverilog, semantics)
            : std::string { };
        !error.empty()) {
        diagnostics.error(
            std::string { kCode },
            "SystemVerilog HIR state is structurally invalid: " + error);
        ok = false;
    } else if (ok) {
        ok = write_inner("FSIMSVCH", kSystemVerilogConstraintHirStateSchema,
            systemverilog);
    }
    if (check_states && !valid_vhdl_hir_state(vhdl, semantics)) {
        diagnostics.error(
            std::string { kCode },
            "VHDL/PSL HIR state is structurally inconsistent with semantic state");
        ok = false;
    } else if (ok) {
        ok = write_inner("FSIMVHIR", kVhdlHirStateSchema, vhdl);
    }
    if (!ok) {
        return std::nullopt;
    }
    writer.write(dependencies);
    writer.write(references);
    if (!writer.failure().empty()) {
        diagnostics.error(std::string { kCode }, writer.failure());
        return std::nullopt;
    }
    return std::move(writer).finish();
}

namespace {

std::optional<std::string> serialize_design_hir_bundle(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics,
    const bool check)
{
    if (check && !design.valid()) {
        diagnostics.error(
            std::string { kCode },
            "compiled HIR bundle is structurally invalid");
        return std::nullopt;
    }
    const SystemVerilogConstraintHirView systemverilog {
        design.systemverilog_hir.units(),
        design.systemverilog_hir.declarations(),
        design.systemverilog_hir.types(),
        design.systemverilog_hir.expressions(),
        design.systemverilog_hir.statements(),
        design.systemverilog_hir.processes(),
        design.systemverilog_hir.classes(),
        design.systemverilog_hir.instances(),
        design.systemverilog_hir.udps(),
        design.systemverilog_hir.dpi_declarations(),
        design.systemverilog_hir.covergroup_instances()
    };
    const VhdlHirView vhdl {
        design.vhdl_hir.units(),
        design.vhdl_hir.declarations(),
        design.vhdl_hir.types(),
        design.vhdl_hir.overload_sets(),
        design.vhdl_hir.expressions(),
        design.vhdl_hir.statements(),
        design.vhdl_hir.processes(),
        design.vhdl_hir.instances()
    };
    return serialize_compiled_hir_bundle_parts(
        design.semantics, systemverilog, vhdl, design.dependencies(),
        design.references(), diagnostics, check);
}

} // namespace

} // namespace codec_detail

std::optional<std::string> serialize_compiled_hir_bundle(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics)
{
    return codec_detail::serialize_design_hir_bundle(
        design, diagnostics, true);
}

std::optional<std::string> serialize_elaborated_compiled_hir_bundle(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics)
{
    // Elaboration consumed validated inputs; the walks are a verify-mode net.
    return codec_detail::serialize_design_hir_bundle(design, diagnostics,
        elaboration::detail::TrustedArtifactLoad::verify_requested());
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
    return codec_detail::serialize_compiled_hir_bundle_parts(
        projected_semantics, projected_systemverilog, vhdl_view,
        projected_dependencies, design.references(), diagnostics);
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
    // Layout written by serialize_compiled_hir_bundle_parts; the three inner
    // states are read as views of the input instead of copies.
    Reader reader { bytes, &budget };
    std::uint32_t schema { };
    std::string_view semantic_state;
    std::string_view systemverilog_hir_state;
    std::string_view vhdl_hir_state;
    CompiledHirBundleDecoded state;
    if (!reader.raw("FSIMCHIR") || !reader.read(schema)
        || schema != kCompiledHirBundleSchema
        || !reader.string_slice(semantic_state)
        || !reader.string_slice(systemverilog_hir_state)
        || !reader.string_slice(vhdl_hir_state)
        || !reader.read(state.dependencies)
        || !reader.read(state.references) || reader.remaining() != 0) {
        auto message = reader.failure();
        if (message.empty() && schema != kCompiledHirBundleSchema) {
            message = diagnostic::unsupported_artifact_identity(
                "design state FSIMCHIR",
                "schema " + std::to_string(schema),
                "schema " + std::to_string(kCompiledHirBundleSchema),
                "compiled-HIR bundle");
        } else if (message.empty()) {
            message = "design state contains trailing bytes";
        }
        diagnostics.error(
            std::string { kCode }, std::move(message),
            { std::string { source_name }, { 1, 1, 0 }, { 1, 1, 0 } });
        return std::nullopt;
    }
    auto semantics = deserialize_semantic_state_with_budget(
        semantic_state, source_name + ":semantics", diagnostics, budget);
    if (!semantics) {
        return std::nullopt;
    }
    auto systemverilog
        = deserialize_systemverilog_constraint_hir_state_with_budget(
        systemverilog_hir_state,
        source_name + ":systemverilog-hir", *semantics, diagnostics, budget);
    auto vhdl = deserialize_vhdl_hir_state_with_budget(
        vhdl_hir_state,
        source_name + ":vhdl-hir", *semantics, diagnostics, budget);
    if (!systemverilog || !vhdl) {
        return std::nullopt;
    }
    semantic::CompiledDesign design {
        std::move(*semantics),
        std::move(*systemverilog),
        std::move(*vhdl),
        std::move(state.dependencies),
        std::move(state.references),
    };
    if (!elaboration::detail::TrustedArtifactLoad::active()
        && !design.valid()) {
        diagnostics.error(
            std::string { kCode },
            "compiled HIR bundle is structurally invalid");
        return std::nullopt;
    }
    return design;
}

} // namespace codec_detail

} // namespace fsim::app
