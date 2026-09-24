// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;
using namespace hierarchy_sv_parameters_detail;

namespace {

    const std::string* compiled_physical_source(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return nullptr;
        }
        const auto file = spans[source.value()].file;
        const auto& files = compiled.semantics.source_files();
        if (!file.valid() || file.value() >= files.size()) {
            return nullptr;
        }
        return &files[file.value()].physical_name;
    }

    frontend::SourceSpan compiled_source_span(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        frontend::SourceSpan result;
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return result;
        }
        const auto& span = spans[source.value()];
        result.source_name = span.logical_name;
        result.begin = {
            static_cast<std::size_t>(span.begin.offset),
            span.begin.line,
            span.begin.column,
        };
        result.end = {
            static_cast<std::size_t>(span.end.offset),
            span.end.line,
            span.end.column,
        };
        if (const auto* physical = compiled_physical_source(
                compiled, source)) {
            result.physical_source_name = *physical;
        }
        return result;
    }

    frontend::PortDirection compiled_port_direction(
        const semantic::sv::Direction direction) noexcept
    {
        switch (direction) {
        case semantic::sv::Direction::input:
            return frontend::PortDirection::Input;
        case semantic::sv::Direction::output:
            return frontend::PortDirection::Output;
        case semantic::sv::Direction::inout:
            return frontend::PortDirection::Inout;
        case semantic::sv::Direction::ref:
            return frontend::PortDirection::Ref;
        case semantic::sv::Direction::unknown:
            break;
        }
        return frontend::PortDirection::Unknown;
    }

    frontend::PortDirection compiled_port_direction(
        const semantic::sv::Declaration& declaration) noexcept
    {
        const auto direction = compiled_port_direction(declaration.direction);
        if (direction != frontend::PortDirection::Unknown
            || declaration.form != semantic::sv::DeclarationForm::port) {
            return direction;
        }
        const auto interface_port = !declaration.interface_type.empty()
            || (declaration.type
                && declaration.type->target.spelling == "interface");
        return interface_port
            ? frontend::PortDirection::Inout
            : frontend::PortDirection::Unknown;
    }

    std::string_view compiled_systemverilog_library(
        const semantic::sv::Unit& unit)
    {
        return unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
    }

} // namespace

bool HierarchyBuilder::materialize_compiled_systemverilog_declaration(
    const semantic::sv::Unit& unit,
    SystemVerilogHirMaterialization& materialization,
    const semantic::sv::Declaration& declaration,
    const std::string& path,
    const semantic::sv::UnconnectedDrive unconnected_drive,
    std::vector<PendingVirtualInterfaceInitializer>&
        pending_virtual_interface_initializers,
    const SystemVerilogPackedTypeResolver& packed_type_resolver,
    const SystemVerilogPackedDefaultResolver& packed_default_resolver,
    const SystemVerilogPackedFallbackResolver& packed_fallback_resolver)
{
    const auto whole_alias_signal = [&](
                                        const SystemVerilogHirMaterialization& value,
                                        const std::string_view name) -> std::optional<SignalId> {
        const auto& signals = value.signals;
        if (const auto direct = signals.find(std::string { name });
            direct != signals.end()) {
            return direct->second;
        }
        const auto group = std::ranges::find_if(
            value.alias_plan.whole_groups,
            [&](const auto& candidate) {
                return std::ranges::find(candidate, name)
                    != candidate.end();
            });
        if (group == value.alias_plan.whole_groups.end()) {
            return std::nullopt;
        }
        for (const auto& alias : *group) {
            if (const auto found = signals.find(alias);
                found != signals.end()) {
                return found->second;
            }
        }
        return std::nullopt;
    };
    const auto coverage_owner
        = std::string { compiled_systemverilog_library(unit) }
        + "." + unit.name;
    const auto is_covergroup_object
        = [&](const semantic::sv::Declaration& value) {
              const auto& instances
                  = compiled_->systemverilog_hir.covergroup_instances();
              return std::ranges::any_of(
                  instances,
                  [&](const semantic::sv::CovergroupInstance& instance) {
                      return !instance.class_member_template
                          && instance.owner_identity == coverage_owner
                          && instance.name == value.name
                          && instance.source == value.source;
                  });
          };
    const auto compiled_interface_type = [&](
                                             const std::string_view name,
                                             const std::string_view library) -> const semantic::sv::Unit* {
        const auto found = compiled_->find_unit(
            semantic::UnitKind::systemverilog_interface,
            library, name);
        return found && found->systemverilog != nullptr
            ? found->systemverilog
            : nullptr;
    };
    auto& signals = materialization.signals;
    auto& string_objects = materialization.string_objects;
    auto& container_objects = materialization.container_objects;
    auto& read_only_strings = materialization.read_only_strings;
    auto& read_only_containers
        = materialization.read_only_containers;
    auto& read_only_signals = materialization.read_only_signals;
    auto& declared_signal_names
        = materialization.declared_signal_names;
    const auto& working_specialization
        = *materialization.specialization;
    const auto& materialized_path = materialization.path;
    using Form = semantic::sv::DeclarationForm;
    const auto net_type_for = [](const semantic::sv::Declaration& value,
                                  const semantic::sv::TypeReference& type) {
        const auto explicit_net_type = [](
                                           const semantic::sv::TypeReference& candidate) {
            auto* current = &candidate;
            while (true) {
                if (!current->systemverilog_net_type.empty()) {
                    return std::string_view {
                        current->systemverilog_net_type
                    };
                }
                const auto spelling
                    = std::string_view { current->target.spelling };
                if (spelling == "wire" || spelling == "tri"
                    || spelling == "tri0" || spelling == "tri1"
                    || spelling == "wand" || spelling == "triand"
                    || spelling == "wor" || spelling == "trior"
                    || spelling == "trireg" || spelling == "uwire"
                    || spelling == "supply0"
                    || spelling == "supply1") {
                    return spelling;
                }
                if (current->container_element_types.size() != 1U) {
                    return std::string_view { };
                }
                current = &current->container_element_types.front();
            }
        };
        const auto declared = explicit_net_type(*value.type);
        const auto effective = explicit_net_type(type);
        return !declared.empty() ? declared
            : !effective.empty() ? effective
            : value.form == Form::net
            ? std::string_view { value.type->target.spelling }
            : std::string_view { };
    };
    const auto configure_net_signal = [](
                                          Signal& signal,
                                          const semantic::sv::Declaration& value,
                                          const std::string_view net_type) {
        if (net_type == "trireg") {
            signal.charge_strength = value.charge_strength
                ? static_cast<StrengthRank>(*value.charge_strength)
                : StrengthRank::medium;
            if (value.charge_decay) {
                signal.charge_decay
                    = value.charge_decay->primary.magnitude;
            }
        }
        if (net_type == "tri0" || net_type == "tri1") {
            signal.implicit_driver = net_type == "tri0"
                ? Logic4::zero
                : Logic4::one;
        } else if (net_type == "supply0"
            || net_type == "supply1") {
            signal.implicit_driver = net_type == "supply0"
                ? Logic4::zero
                : Logic4::one;
            signal.implicit_drive_strength = {
                StrengthRank::supply, StrengthRank::supply
            };
        }
    };
    const auto net_initial_for = [](
                                     const frontend::ValueDomain domain,
                                     const std::string_view net_type) {
        if (net_type == "tri0" || net_type == "supply0") {
            return Logic4::zero;
        }
        if (net_type == "tri1" || net_type == "supply1") {
            return Logic4::one;
        }
        return domain == frontend::ValueDomain::Logic4
            ? Logic4::z
            : Logic4::zero;
    };
    const auto register_resolution = [&](const SignalId id) {
        if (!declaration.type
                ->systemverilog_resolution_function.empty()) {
            resolver_by_signal_.insert_or_assign(
                id,
                declaration.type
                    ->systemverilog_resolution_function);
        }
    };
    if (declaration.form != Form::port
        && declaration.form != Form::net
        && declaration.form != Form::variable) {
        return true;
    }
    if (declaration.type
        && declaration.type->virtual_interface) {
        const auto* interface = compiled_interface_type(
            declaration.type->interface_type,
            compiled_systemverilog_library(unit));
        if (interface == nullptr) {
            report(
                "FSIM-ELAB-SVIFACE-003",
                "virtual interface '" + materialized_path + "."
                    + declaration.name + "' requires unknown type '"
                    + declaration.type->interface_type + "'",
                compiled_source_span(
                    *compiled_, declaration.source));
        } else if (!declaration.type->interface_modport.empty()
            && std::ranges::none_of(
                interface->modports,
                [&](const semantic::sv::Modport& modport) {
                    return modport.name
                        == declaration.type->interface_modport;
                })) {
            report(
                "FSIM-ELAB-SVIFACE-004",
                "virtual interface '" + materialized_path + "."
                    + declaration.name + "' selects unknown modport '"
                    + declaration.type->interface_modport
                    + "' on interface type '"
                    + declaration.type->interface_type + "'",
                compiled_source_span(
                    *compiled_, declaration.source));
        }
    }
    // Covergroup objects are executable coverage inventory, not
    // packed simulation signals. Their declaration and mutable state
    // remain owned by the compiled HIR coverage handoff.
    if (is_covergroup_object(declaration)) {
        return true;
    }
    if (compiled_systemverilog_string_declaration(declaration)) {
        return materialize_compiled_systemverilog_string_declaration(
            declaration,
            working_specialization,
            materialized_path,
            string_objects,
            read_only_strings,
            compiled_port_direction(declaration),
            compiled_source_span(*compiled_, declaration.source));
    }
    const auto executable_container_type = declaration.type
        ? hierarchy_sv_type_layout_detail::
              systemverilog_container_type(
                  working_specialization, *declaration.type)
        : std::nullopt;
    if (declaration.type
        && (declaration.type->container_form
            || executable_container_type)) {
        const auto full_name
            = materialized_path + "." + declaration.name;
        if (declaration.form == Form::port) {
            const auto alias = container_objects.find(
                declaration.name);
            if (alias != container_objects.end()) {
                const auto info = std::ranges::find_if(
                    design_.container_object_info_.rbegin(),
                    design_.container_object_info_.rend(),
                    [&](const ContainerObjectInfo& candidate) {
                        return candidate.id == alias->second;
                    });
                if (info
                    == design_.container_object_info_.rend()) {
                    report(
                        "FSIM-ELAB-HIR-001",
                        "compiled container port association for '"
                            + full_name
                            + "' has an invalid object",
                        compiled_source_span(
                            *compiled_, declaration.source));
                    return false;
                }
                container_objects.insert_or_assign(
                    full_name, alias->second);
                design_.container_by_name_.insert_or_assign(
                    full_name, alias->second);
                if (design_.roots_.size() == 1U
                    && materialized_path == active_root_) {
                    design_.container_by_name_.insert_or_assign(
                        declaration.name, alias->second);
                }
                if (compiled_port_direction(declaration)
                    == frontend::PortDirection::Input) {
                    read_only_containers.emplace(
                        declaration.name);
                    read_only_containers.emplace(full_name);
                }
                return true;
            }
        }
        if (declaration.initializer) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled container declaration '"
                    + declaration.name
                    + "' has an unsupported initializer",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        if (declaration.type->container_form
                == semantic::sv::TypeForm::associative_array
            && declaration.type->associative_index) {
            semantic::sv::TypeReference index_reference;
            index_reference.target
                = *declaration.type->associative_index;
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            const auto index_type
                = type_resolver.underlying_systemverilog_type(
                                   index_reference,
                                   working_specialization.scope())
                      .value_or(index_reference);
            const auto spelling = index_type.target.spelling.empty()
                ? declaration.type->associative_index->spelling
                : index_type.target.spelling;
            const auto form = index_type.value_form.value_or(
                semantic::sv::TypeForm::unresolved);
            const bool nonintegral_scalar
                = spelling == "shortreal" || spelling == "real"
                || spelling == "realtime" || spelling == "chandle";
            const bool invalid_form
                = form == semantic::sv::TypeForm::unpacked_structure
                || form == semantic::sv::TypeForm::tagged_union
                || form == semantic::sv::TypeForm::unpacked_union
                || form == semantic::sv::TypeForm::class_handle
                || index_type.container_form.has_value();
            if (nonintegral_scalar || invalid_form) {
                report(
                    "FSIM-ELAB-SVCONTAINER-013",
                    "associative-array indices require a resolved string "
                    "or integral type with an executable width",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return true;
            }
        }
        auto type = executable_container_type;
        if (!type) {
            if (declaration.type->container_form
                == semantic::sv::TypeForm::static_array) {
                report(
                    "FSIM-ELAB-SVCONTAINER-020",
                    "static unpacked-array bounds and storage must be "
                    "locally constant, 32-bit, and within the "
                    "per-container owning-storage budget",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return true;
            }
            report(
                "FSIM-ELAB-HIR-001",
                "compiled container declaration '"
                    + declaration.name
                    + "' has no executable container layout",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        const auto index = design_.container_objects_.size();
        const auto id = static_cast<ContainerObjectId>(index);
        if (static_cast<std::size_t>(id) != index) {
            throw std::length_error(
                "too many elaborated container objects");
        }
        design_.container_object_info_.push_back(
            ContainerObjectInfo {
                id,
                full_name,
                *type,
                compiled_source_span(
                    *compiled_, declaration.source),
                declaration.form == Form::port,
                compiled_port_direction(declaration),
                std::nullopt,
            });
        design_.container_objects_.push_back(ContainerObject {
            full_name,
            default_container_value(*type),
            std::nullopt,
        });
        materialization.container_objects.emplace(
            declaration.name, id);
        materialization.container_objects.emplace(full_name, id);
        design_.container_by_name_.emplace(full_name, id);
        if (design_.roots_.size() == 1U
            && materialized_path == active_root_) {
            design_.container_by_name_.emplace(
                declaration.name, id);
        }
        const auto declared_net_type = net_type_for(
            declaration, *declaration.type);
        const bool net_bridge
            = declaration.form == Form::net
            || !declared_net_type.empty();
        const bool variable_bridge
            = !net_bridge
            && (declaration.form == Form::variable
                || declaration.form == Form::port);
        const auto bridge_width
            = (net_bridge || variable_bridge)
                && type->fixed
                && (type->element_kind
                        == ContainerElementKind::Packed
                    || type->element_kind
                        == ContainerElementKind::Scalar)
            ? container_signal_bridge_width(*type)
            : std::nullopt;
        constexpr auto maximum_bridge_width
            = maximum_container_storage_bytes * 8U;
        if (bridge_width
            && *bridge_width <= maximum_bridge_width) {
            if (design_.signals_.size()
                > std::numeric_limits<SignalId>::max()) {
                report(
                    "FSIM-ELAB-011",
                    "the design has too many signals for dense 32-bit IDs",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return false;
            }
            const auto signal_id = static_cast<SignalId>(
                design_.signals_.size());
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            const auto effective_type
                = type_resolver.underlying_systemverilog_type(
                                   *declaration.type, declaration.scope)
                      .value_or(*declaration.type);
            const auto domain = type->two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            const auto net_type = net_bridge
                ? net_type_for(declaration, effective_type)
                : std::string_view { };
            SignalInfo info;
            info.id = signal_id;
            info.name = full_name;
            info.width = *bridge_width;
            info.type_name = declaration.type->target.spelling;
            info.source_domain = domain;
            info.systemverilog_net_type = net_type;
            info.is_signed = type->signed_elements;
            info.declaration_span = compiled_source_span(
                *compiled_, declaration.source);
            const auto initial_value = net_bridge
                ? net_initial_for(domain, net_type)
                : domain == frontend::ValueDomain::Logic4
                ? Logic4::x
                : Logic4::zero;
            Signal signal {
                full_name,
                PackedLogic4(*bridge_width, initial_value),
                ResolutionKind::none,
                value_kind(domain),
                std::nullopt,
                { StrengthRank::pull, StrengthRank::pull },
                std::nullopt,
                std::nullopt,
                frontend::SystemVerilogScalarKind::None,
            };
            if (net_bridge) {
                configure_net_signal(signal, declaration, net_type);
            }
            design_.signal_info_.push_back(std::move(info));
            design_.signals_.push_back(std::move(signal));
            declared_signal_names.insert(declaration.name);
            signals.insert_or_assign(
                declaration.name, signal_id);
            signals.insert_or_assign(full_name, signal_id);
            design_.signal_by_name_.insert_or_assign(
                full_name, signal_id);
            if (design_.roots_.size() == 1U) {
                design_.signal_by_name_.insert_or_assign(
                    declaration.name, signal_id);
                const auto root_prefix = active_root_ + ".";
                if (materialized_path.starts_with(root_prefix)) {
                    design_.signal_by_name_.insert_or_assign(
                        materialized_path.substr(root_prefix.size())
                            + "." + declaration.name,
                        signal_id);
                }
            }
            if (net_bridge) {
                register_resolution(signal_id);
            }
            const bool alias_writable = net_bridge
                || compiled_port_direction(declaration)
                    != frontend::PortDirection::Input;
            design_.container_signal_aliases_.push_back(
                ContainerSignalAlias {
                    id, signal_id, true, alias_writable });
        }
        if (compiled_port_direction(declaration)
            == frontend::PortDirection::Input) {
            materialization.read_only_containers.emplace(
                declaration.name);
            materialization.read_only_containers.emplace(full_name);
        }
        return true;
    }
    const auto executable_width
        = hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
            working_specialization, declaration);
    if (!executable_width) {
        report(
            "FSIM-ELAB-TYPE-001",
            "signal '" + declaration.name
                + "' has a type that the packed simulation runtime "
                  "cannot represent",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    if (declared_signal_names.contains(declaration.name)) {
        report(
            "FSIM-ELAB-HIR-001",
            "duplicate compiled signal declaration '"
                + declaration.name + "'",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    const auto width = *executable_width;
    const semantic::CompiledDesignResolver type_resolver {
        working_specialization
    };
    const auto effective_type
        = type_resolver.underlying_systemverilog_type(
                           *declaration.type, declaration.scope)
              .value_or(*declaration.type);
    const auto full_name
        = materialized_path + "." + declaration.name;
    if (const auto alias = whole_alias_signal(
            materialization, declaration.name)) {
        if (*alias >= design_.signal_info_.size()
            || design_.signal_info_[*alias].width != width) {
            const auto actual_width
                = *alias < design_.signal_info_.size()
                ? std::to_string(
                      design_.signal_info_[*alias].width)
                : std::string { "unavailable" };
            report(
                "FSIM-ELAB-HIR-001",
                "compiled port association for '" + full_name
                    + "' has an incompatible signal width "
                      "(formal "
                    + std::to_string(width)
                    + ", actual " + actual_width + ")",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        signals.insert_or_assign(declaration.name, *alias);
        signals.insert_or_assign(full_name, *alias);
        design_.signal_by_name_.insert_or_assign(full_name, *alias);
        if (design_.roots_.size() == 1U
            && materialized_path == active_root_) {
            design_.signal_by_name_.insert_or_assign(
                declaration.name, *alias);
        }
        if (compiled_port_direction(declaration)
            == frontend::PortDirection::Input) {
            read_only_signals.insert(*alias);
        }
        return true;
    }
    if (design_.signals_.size()
        > std::numeric_limits<SignalId>::max()) {
        report(
            "FSIM-ELAB-011",
            "the design has too many signals for dense 32-bit IDs",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    const auto id = static_cast<SignalId>(
        design_.signals_.size());
    if (materialized_path == path
        && signals.contains(declaration.name)) {
        report(
            "FSIM-ELAB-HIR-001",
            "duplicate compiled signal declaration '"
                + declaration.name + "'",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    const auto domain = (declaration.type->four_state
                            || effective_type.four_state)
        ? frontend::ValueDomain::Logic4
        : frontend::ValueDomain::Bit2;
    const bool event_variable
        = declaration.type->target.spelling == "event"
        || effective_type.target.spelling == "event";
    auto direction = compiled_port_direction(declaration);
    SignalInfo info;
    info.id = id;
    info.name = full_name;
    info.width = width;
    info.type_name = event_variable
        ? "event"
        : declaration.type->target.spelling;
    info.source_domain = domain;
    auto scalar_kind = compiled_systemverilog_scalar_kind(
        effective_type.target.spelling);
    if (scalar_kind
        == frontend::SystemVerilogScalarKind::None) {
        scalar_kind = compiled_systemverilog_scalar_kind(
            declaration.type->target.spelling);
    }
    if (declaration.type->virtual_interface
        || !declaration.interface_type.empty()
        || declaration.type->target.spelling == "interface") {
        scalar_kind = frontend::SystemVerilogScalarKind::Chandle;
    }
    info.systemverilog_scalar = scalar_kind;
    const auto net_type = net_type_for(
        declaration, effective_type);
    info.systemverilog_net_type = net_type;
    info.is_signed = declaration.type->signed_value
        || effective_type.signed_value;
    if (declaration.type->container_form
            != semantic::sv::TypeForm::static_array
        && effective_type.packed_range) {
        const auto& range = *effective_type.packed_range;
        const auto left = range.left_expression
            ? working_specialization.evaluate_integral_expression(
                  *range.left_expression)
            : range.left;
        const auto right = range.right_expression
            ? working_specialization.evaluate_integral_expression(
                  *range.right_expression)
            : range.right;
        if (left && right) {
            info.packed_range = frontend::PackedRange {
                *left,
                *right,
                range.descending,
            };
        }
    }
    const auto packed_type = packed_type_resolver(
        working_specialization, *declaration.type);
    if (packed_type) {
        info.packed_members = packed_type->packed_members;
    }
    info.is_port = declaration.form == Form::port;
    info.direction = direction;
    info.declaration_span = compiled_source_span(
        *compiled_, declaration.source);

    const auto four_state
        = domain == frontend::ValueDomain::Logic4;
    auto initial = declaration.form == Form::net
        ? net_initial_for(domain, net_type)
        : four_state
        ? Logic4::x
        : Logic4::zero;
    if (event_variable) {
        initial = Logic4::zero;
    }
    if (declaration.form == Form::port
        && direction == frontend::PortDirection::Input) {
        if (unconnected_drive
            == semantic::sv::UnconnectedDrive::pull_zero) {
            initial = Logic4::zero;
        } else if (unconnected_drive
            == semantic::sv::UnconnectedDrive::pull_one) {
            initial = Logic4::one;
        }
    }
    auto initial_value = scalar_kind
                != frontend::SystemVerilogScalarKind::None
            && scalar_kind
                != frontend::SystemVerilogScalarKind::Time
        ? runtime::encode_systemverilog_scalar_payload(
              runtime::SystemVerilogScalarValue {
                  scalar_kind, 0U })
              .value
        : PackedLogic4(width, initial);
    const bool forced_unconnected_value
        = declaration.form == Form::port
        && direction == frontend::PortDirection::Input
        && unconnected_drive
            != semantic::sv::UnconnectedDrive::none;
    if (packed_type && declaration.form != Form::net
        && !event_variable && !forced_unconnected_value) {
        initial_value = packed_default_resolver(
            working_specialization,
            *declaration.type,
            width)
                            .value_or(
                                packed_fallback_resolver(
                                    *packed_type, width));
    }
    if (declaration.initializer) {
        const auto value
            = working_specialization.evaluate_integral_expression(
                *declaration.initializer);
        if (value && width <= 64U
            && !declaration.type->virtual_interface) {
            initial_value = PackedLogic4::from_aval_bval(
                width, static_cast<std::uint64_t>(*value), 0U);
        } else if (declaration.type->virtual_interface) {
            const auto expression
                = working_specialization.find_expression(
                    *declaration.initializer);
            std::string actual_name;
            if (expression
                && expression->systemverilog != nullptr) {
                const auto& record = *expression->systemverilog;
                if (record.kind
                    == semantic::sv::ExpressionKind::name) {
                    actual_name = record.text;
                    if (record.referenced_name
                        && record.referenced_name->selected) {
                        const auto selected
                            = working_specialization
                                  .find_declaration(
                                      *record.referenced_name
                                          ->selected);
                        if (selected
                            && selected->systemverilog != nullptr) {
                            actual_name
                                = selected->systemverilog->name;
                        }
                    }
                } else if (record.kind
                        == semantic::sv::ExpressionKind::index
                    && record.operands.size() == 2U) {
                    const auto base
                        = working_specialization.find_expression(
                            record.operands.front());
                    const auto index = working_specialization
                                           .evaluate_integral_expression(
                                               record.operands.back());
                    if (base && base->systemverilog != nullptr
                        && index) {
                        actual_name = base->systemverilog->text
                            + "[" + std::to_string(*index) + "]";
                    }
                } else if (record.kind
                    == semantic::sv::ExpressionKind::class_null) {
                    initial_value = PackedLogic4::from_aval_bval(
                        width, 0U, 0U);
                }
            }
            if (!actual_name.empty()) {
                std::optional<semantic::sv::TypeReference>
                    source_type;
                if (expression
                    && expression->systemverilog != nullptr
                    && expression->systemverilog->referenced_name
                    && expression->systemverilog
                        ->referenced_name->selected) {
                    const auto selected
                        = working_specialization.find_declaration(
                            *expression->systemverilog
                                ->referenced_name->selected);
                    if (selected
                        && selected->systemverilog != nullptr
                        && selected->systemverilog->type
                        && selected->systemverilog->type
                            ->virtual_interface) {
                        source_type
                            = *selected->systemverilog->type;
                    }
                }
                pending_virtual_interface_initializers.push_back({
                    id,
                    materialized_path,
                    std::move(actual_name),
                    *declaration.type,
                    std::move(source_type),
                    declaration.source,
                    &working_specialization,
                });
                initial_value = PackedLogic4::from_aval_bval(
                    width, 0U, 0U);
            } else if (!expression
                || expression->systemverilog == nullptr
                || expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::class_null) {
                report(
                    "FSIM-ELAB-SVIFACE-002",
                    "virtual-interface initializer for '"
                        + full_name
                        + "' must name an interface instance or "
                          "virtual-interface value, or null",
                    compiled_source_span(
                        *compiled_, declaration.source));
            }
        } else {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled initializer for '" + declaration.name
                    + "' is not a bounded integral value",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
    }
    runtime::simir::Signal signal {
        full_name,
        std::move(initial_value),
        ResolutionKind::none,
        value_kind(domain),
        std::nullopt,
        { StrengthRank::pull, StrengthRank::pull },
        std::nullopt,
        std::nullopt,
        scalar_kind,
    };
    signal.event_variable = event_variable;
    configure_net_signal(signal, declaration, net_type);
    design_.signal_info_.push_back(std::move(info));
    design_.signals_.push_back(std::move(signal));
    declared_signal_names.insert(declaration.name);
    if (materialized_path == path) {
        signals.emplace(declaration.name, id);
    } else {
        signals.insert_or_assign(declaration.name, id);
    }
    signals.emplace(full_name, id);
    design_.signal_by_name_.emplace(full_name, id);
    if (design_.roots_.size() == 1U) {
        design_.signal_by_name_.emplace(declaration.name, id);
        const auto root_prefix = active_root_ + ".";
        if (materialized_path.starts_with(root_prefix)) {
            design_.signal_by_name_.emplace(
                materialized_path.substr(root_prefix.size())
                    + "." + declaration.name,
                id);
        }
    }
    register_resolution(id);
    if (direction == frontend::PortDirection::Input) {
        read_only_signals.insert(id);
    }
    return true;
}

} // namespace fsim::elaboration
