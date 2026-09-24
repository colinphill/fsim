// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_ports_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::elaboration::hierarchy_sv_ports_detail {

PortActualResult normalize_port_actual(
    const semantic::SpecializedHirAssociationBinding& binding,
    const semantic::sv::Declaration& formal,
    const semantic::SpecializedHirUnit& specialization,
    const std::string_view child_path)
{
    if (binding.kind == semantic::SpecializedHirAssociationKind::open
        || (binding.kind
                == semantic::SpecializedHirAssociationKind::default_value
            && !binding.actual_declaration)) {
        return { PortActualStatus::unconnected, { }, std::nullopt };
    }

    const auto fatal = [](std::string message) {
        return PortActualResult {
            PortActualStatus::fatal,
            { },
            PortActualDiagnostic {
                "FSIM-ELAB-HIR-001", std::move(message) },
        };
    };
    if (binding.kind
        != semantic::SpecializedHirAssociationKind::expression) {
        return fatal(
            "compiled port '" + formal.name
            + "' requires a simple signal-name actual");
    }

    std::string actual_name;
    if (binding.expression) {
        const auto expression
            = specialization.find_expression(*binding.expression);
        if (!expression || expression->systemverilog == nullptr) {
            return fatal(
                "compiled port '" + formal.name
                + "' requires a simple signal-name actual");
        }
        const auto& actual_expression = *expression->systemverilog;
        const bool interface_formal = !formal.interface_type.empty()
            || (formal.type
                && formal.type->target.spelling == "interface");
        if (actual_expression.kind
            == semantic::sv::ExpressionKind::name) {
            actual_name = actual_expression.text;
        } else if (interface_formal
            && actual_expression.kind
                == semantic::sv::ExpressionKind::index
            && actual_expression.operands.size() == 2U) {
            const auto base = specialization.find_expression(
                actual_expression.operands.front());
            const auto index = specialization.evaluate_integral_expression(
                actual_expression.operands.back());
            if (base && base->systemverilog != nullptr && index) {
                actual_name = base->systemverilog->text + "["
                    + std::to_string(*index) + "]";
            }
        }
        if (actual_name.empty() && interface_formal) {
            return {
                PortActualStatus::invalid_child_port,
                { },
                PortActualDiagnostic {
                    "FSIM-ELAB-SVIFACE-001",
                    "interface port '" + std::string { child_path } + "."
                        + formal.name
                        + "' requires a whole interface-instance actual",
                },
            };
        }
        if (actual_expression.kind
                == semantic::sv::ExpressionKind::name
            && actual_expression.referenced_name
            && actual_expression.referenced_name->selected) {
            const auto declaration = specialization.find_declaration(
                *actual_expression.referenced_name->selected);
            if (declaration && declaration->systemverilog != nullptr) {
                actual_name = declaration->systemverilog->name;
            }
        }
    } else if (binding.actual_declaration) {
        const auto declaration = specialization.find_declaration(
            *binding.actual_declaration);
        if (!declaration || declaration->systemverilog == nullptr) {
            return fatal(
                "compiled port '" + formal.name
                + "' references an unavailable wildcard declaration actual");
        }
        using ActualForm = semantic::sv::DeclarationForm;
        const auto actual_form = declaration->systemverilog->form;
        if (actual_form != ActualForm::port
            && actual_form != ActualForm::net
            && actual_form != ActualForm::variable) {
            return fatal(
                "compiled port '" + formal.name
                + "' requires a simple signal-name actual");
        }
        actual_name = declaration->systemverilog->name;
    } else {
        return fatal(
            "compiled port '" + formal.name
            + "' requires a simple signal-name actual");
    }

    return { PortActualStatus::ready, std::move(actual_name), std::nullopt };
}

} // namespace fsim::elaboration::hierarchy_sv_ports_detail

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;
using namespace hierarchy_sv_parameters_detail;

namespace {

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
        const auto& files = compiled.semantics.source_files();
        if (span.file.valid() && span.file.value() < files.size()) {
            result.physical_source_name = files[span.file.value()].physical_name;
        }
        return result;
    }

    frontend::PortDirection compiled_port_direction(
        const semantic::sv::Declaration& declaration) noexcept
    {
        frontend::PortDirection direction = frontend::PortDirection::Unknown;
        switch (declaration.direction) {
        case semantic::sv::Direction::input:
            direction = frontend::PortDirection::Input;
            break;
        case semantic::sv::Direction::output:
            direction = frontend::PortDirection::Output;
            break;
        case semantic::sv::Direction::inout:
            direction = frontend::PortDirection::Inout;
            break;
        case semantic::sv::Direction::ref:
            direction = frontend::PortDirection::Ref;
            break;
        case semantic::sv::Direction::unknown:
            break;
        }
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

} // namespace

bool HierarchyBuilder::bind_compiled_systemverilog_ports(
    const CompiledSystemVerilogPortBindingContext& context)
{
    const auto& unit = context.unit;
    const auto& record = context.record;
    const auto& child = context.child;
    const auto& working_specialization = context.working_specialization;
    const auto& child_interface_specialization
        = context.child_interface_specialization;
    const auto& port_bindings = context.port_bindings;
    const auto& child_path = context.child_path;
    const auto working_path = context.working_path;
    const auto& working_signals = context.working_signals;
    const auto& working_read_only_signals
        = context.working_read_only_signals;
    const auto& working_strings = context.working_strings;
    const auto& working_read_only_strings
        = context.working_read_only_strings;
    const auto& working_containers = context.working_containers;
    const auto& working_read_only_containers
        = context.working_read_only_containers;
    const auto* external_binding = context.external_binding;
    const auto source_language = context.source_language;
    auto& concurrent_order = context.concurrent_order;
    const auto specialize_interface = context.specialize_interface;
    auto& child_aliases = context.child_aliases;
    auto& child_string_aliases = context.child_string_aliases;
    auto& child_container_aliases = context.child_container_aliases;
    auto& child_boundary_processes = context.child_boundary_processes;
    auto& child_ports_valid = context.child_ports_valid;

    const auto resolved_interface_path = [&](const std::string_view actual_name)
        -> std::optional<std::string> {
        auto candidate = std::string { working_path } + "."
            + std::string { actual_name };
        auto handle = systemverilog_interface_handles_.find(
            candidate);
        auto lexical_path = std::string { working_path };
        while (handle == systemverilog_interface_handles_.end()
            && lexical_path.find('.') != std::string::npos) {
            lexical_path.resize(lexical_path.rfind('.'));
            candidate = lexical_path + "."
                + std::string { actual_name };
            handle = systemverilog_interface_handles_.find(
                candidate);
        }
        if (handle == systemverilog_interface_handles_.end()) {
            return std::nullopt;
        }
        return candidate;
    };
    const auto forward_interface_port = [&](
                                            const semantic::sv::Declaration& formal,
                                            const std::string& source_path) -> bool {
        std::vector<CompiledSystemVerilogInterfaceDiagnostic> diagnostics;
        const auto forwarded
            = forward_compiled_systemverilog_interface_port(
                formal, *child->systemverilog,
                *child_interface_specialization,
                source_path, child_path, diagnostics,
                specialize_interface,
                child_aliases);
        for (const auto& diagnostic : diagnostics) {
            report(
                diagnostic.code,
                diagnostic.message,
                compiled_source_span(*compiled_, diagnostic.source));
        }
        return forwarded;
    };
    for (const auto& binding : port_bindings.bindings) {
        const auto formal = compiled_->find_declaration(
            binding.formal);
        if (!formal || formal->systemverilog == nullptr
            || formal->systemverilog->form
                != semantic::sv::DeclarationForm::port) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled port association for '" + record.name
                    + "' has no port declaration",
                compiled_source_span(*compiled_, binding.source));
            return false;
        }
        const auto& formal_declaration = *formal->systemverilog;
        const auto direction = compiled_port_direction(
            formal_declaration);
        const auto port_actual = hierarchy_sv_ports_detail::normalize_port_actual(
            binding, formal_declaration,
            working_specialization, child_path);
        if (port_actual.status
            == hierarchy_sv_ports_detail::PortActualStatus::unconnected) {
            // SystemVerilog permits an unconnected input. Leaving the
            // child formal unaliased preserves its local X, Z, two-state,
            // or declaration-initializer value.
            continue;
        }
        if (port_actual.diagnostic) {
            report(
                port_actual.diagnostic->code,
                port_actual.diagnostic->message,
                compiled_source_span(*compiled_, binding.source));
        }
        if (port_actual.status
            == hierarchy_sv_ports_detail::PortActualStatus::fatal) {
            return false;
        }
        if (port_actual.status
            == hierarchy_sv_ports_detail::PortActualStatus::
                invalid_child_port) {
            child_ports_valid = false;
            continue;
        }
        auto actual_name = port_actual.actual_name;
        if (port_actual.status
            != hierarchy_sv_ports_detail::PortActualStatus::ready) {
            return false;
        }
        const auto formal_container_type = formal_declaration.type
            ? hierarchy_sv_type_layout_detail::
                  systemverilog_container_type(
                      *child_interface_specialization,
                      *formal_declaration.type)
            : std::nullopt;
        if (formal_container_type) {
            const auto bound = [&]() -> bool {
                const auto& expected = formal_container_type;
                const auto actual_expression = binding.expression
                    ? working_specialization.find_expression(
                          *binding.expression)
                    : std::nullopt;
                if (!expected || !actual_expression
                    || actual_expression->systemverilog == nullptr) {
                    report(
                        "FSIM-ELAB-SVPORT-005",
                        "container port actuals must be direct whole-container objects or static-array slices",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto& expression
                    = *actual_expression->systemverilog;
                const bool sliced = expression.kind
                    == semantic::sv::ExpressionKind::slice;
                semantic::ExpressionId base_id
                    = binding.expression.value();
                if (sliced) {
                    if ((expression.text != ":"
                            && expression.text != "+:"
                            && expression.text != "-:")
                        || expression.operands.size() != 3U) {
                        report(
                            "FSIM-ELAB-SVPORT-005",
                            "container port slice actuals must be direct left:right or indexed selections of static-array objects",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    base_id = expression.operands.front();
                } else if (expression.kind
                    != semantic::sv::ExpressionKind::name) {
                    report(
                        "FSIM-ELAB-SVPORT-005",
                        "container port actuals must be direct whole-container objects or static-array slices",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto base_expression
                    = working_specialization.find_expression(base_id);
                if (!base_expression
                    || base_expression->systemverilog == nullptr
                    || base_expression->systemverilog->kind
                        != semantic::sv::ExpressionKind::name) {
                    report(
                        "FSIM-ELAB-SVPORT-005",
                        "container port slice actuals must select a direct static-array object",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                actual_name = base_expression->systemverilog->text;
                if (base_expression->systemverilog->referenced_name
                    && base_expression->systemverilog
                        ->referenced_name->selected) {
                    const auto declaration
                        = working_specialization.find_declaration(
                            *base_expression->systemverilog
                                ->referenced_name->selected);
                    if (declaration
                        && declaration->systemverilog != nullptr) {
                        actual_name
                            = declaration->systemverilog->name;
                    }
                }
                const auto actual = working_containers.find(
                    actual_name);
                if (actual == working_containers.end()) {
                    report(
                        "FSIM-ELAB-SVPORT-006",
                        "unknown container connection object '"
                            + actual_name + "' on instance '"
                            + child_path + "'",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto actual_info = std::ranges::find_if(
                    design_.container_object_info_.rbegin(),
                    design_.container_object_info_.rend(),
                    [&](const ContainerObjectInfo& candidate) {
                        return candidate.id == actual->second;
                    });
                if (actual_info
                    == design_.container_object_info_.rend()) {
                    report(
                        "FSIM-ELAB-SVPORT-006",
                        "container connection object '" + actual_name
                            + "' has no elaborated type",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto actual_info_name = actual_info->name;
                auto selected_type = actual_info->type;
                std::optional<ContainerSliceAlias> slice_alias;
                std::optional<std::pair<std::int32_t, std::int32_t>>
                    driver_interval;
                const auto element_count = [](const ContainerType& type) {
                    return static_cast<std::uint64_t>(
                               type.index_left >= type.index_right
                                   ? static_cast<std::int64_t>(
                                         type.index_left)
                                       - type.index_right
                                   : static_cast<std::int64_t>(
                                         type.index_right)
                                       - type.index_left)
                        + 1U;
                };
                const auto same_element_profile = [](
                                                      const ContainerType& left,
                                                      const ContainerType& right) {
                    return left.element_kind == right.element_kind
                        && left.scalar_kind == right.scalar_kind
                        && left.element_width == right.element_width
                        && left.two_state == right.two_state
                        && left.signed_elements
                        == right.signed_elements
                        && left.element_nominal_type
                        == right.element_nominal_type;
                };
                if (sliced) {
                    if (!expected->fixed || !selected_type.fixed
                        || expected->dimensions.size() != 1U
                        || selected_type.dimensions.size() != 1U) {
                        report(
                            "FSIM-ELAB-SVPORT-005",
                            "container port slice actuals require fixed one-dimensional static arrays",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    const auto first = working_specialization
                                           .evaluate_integral_expression(
                                               expression.operands[1]);
                    const auto second = working_specialization
                                            .evaluate_integral_expression(
                                                expression.operands[2]);
                    if (!first || !second
                        || *first
                            < std::numeric_limits<std::int32_t>::min()
                        || *first
                            > std::numeric_limits<std::int32_t>::max()
                        || *second
                            < std::numeric_limits<std::int32_t>::min()
                        || *second
                            > std::numeric_limits<std::int32_t>::max()) {
                        report(
                            "FSIM-ELAB-SVSLICE-002",
                            "static-array slice port bounds must be locally constant known signed 32-bit values",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    auto left = *first;
                    auto right = *second;
                    const bool actual_descending
                        = selected_type.index_left
                        >= selected_type.index_right;
                    if (expression.text != ":") {
                        if (*second <= 0) {
                            report(
                                "FSIM-ELAB-SVSLICE-002",
                                "a static-array indexed slice port width must be positive",
                                compiled_source_span(
                                    *compiled_, binding.source));
                            return false;
                        }
                        const auto distance = *second - 1;
                        const auto lower = expression.text == "+:"
                            ? *first
                            : *first - distance;
                        const auto upper = expression.text == "+:"
                            ? *first + distance
                            : *first;
                        if (actual_descending) {
                            left = upper;
                            right = lower;
                        } else {
                            left = lower;
                            right = upper;
                        }
                    }
                    const auto low = std::min(
                        selected_type.index_left,
                        selected_type.index_right);
                    const auto high = std::max(
                        selected_type.index_left,
                        selected_type.index_right);
                    const bool selected_descending = left >= right;
                    if ((left != right
                            && actual_descending
                                != selected_descending)
                        || left < low || left > high
                        || right < low || right > high) {
                        report(
                            "FSIM-ELAB-SVSLICE-003",
                            "a static-array slice port actual must preserve its declared direction and remain in range",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    selected_type.index_left
                        = static_cast<std::int32_t>(left);
                    selected_type.index_right
                        = static_cast<std::int32_t>(right);
                    selected_type.dimensions.front() = {
                        selected_type.index_left,
                        selected_type.index_right,
                    };
                    if (element_count(selected_type)
                            != element_count(*expected)
                        || !same_element_profile(
                            selected_type, *expected)) {
                        report(
                            "FSIM-ELAB-SVPORT-007",
                            "static-array slice port actuals require equal element counts and identical element profiles",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    slice_alias = ContainerSliceAlias {
                        actual->second,
                        selected_type.index_left,
                        selected_type.index_right,
                    };
                    driver_interval = std::pair {
                        std::min(selected_type.index_left,
                            selected_type.index_right),
                        std::max(selected_type.index_left,
                            selected_type.index_right),
                    };
                } else {
                    if (selected_type != *expected) {
                        report(
                            "FSIM-ELAB-SVPORT-007",
                            "whole-container port actuals require an exact container type match",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    if (selected_type.fixed) {
                        driver_interval = std::pair {
                            std::min(selected_type.index_left,
                                selected_type.index_right),
                            std::max(selected_type.index_left,
                                selected_type.index_right),
                        };
                    }
                }
                const bool writable
                    = direction == frontend::PortDirection::Output
                    || direction == frontend::PortDirection::Inout;
                if (writable
                    && (working_read_only_containers.contains(
                            actual_name)
                        || working_read_only_containers.contains(
                            actual_info_name))) {
                    report(
                        "FSIM-ELAB-SVPORT-009",
                        "an input container port cannot be connected to a descendant output or inout port",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                auto connected_object = actual->second;
                if (slice_alias) {
                    const auto index
                        = design_.container_objects_.size();
                    connected_object
                        = static_cast<ContainerObjectId>(index);
                    if (static_cast<std::size_t>(connected_object)
                        != index) {
                        throw std::length_error(
                            "too many elaborated container objects");
                    }
                    const auto full_name = child_path + "."
                        + formal_declaration.name;
                    design_.container_object_info_.push_back(
                        ContainerObjectInfo {
                            connected_object,
                            full_name,
                            *expected,
                            compiled_source_span(
                                *compiled_, binding.source),
                            true,
                            direction,
                            slice_alias,
                        });
                    design_.container_objects_.push_back(
                        ContainerObject {
                            full_name,
                            default_container_value(*expected),
                            slice_alias,
                        });
                }
                if (writable) {
                    auto& drivers = container_boundary_driver_paths_[actual->second];
                    const auto nested_with = [](
                                                 const std::string_view left,
                                                 const std::string_view right) {
                        const auto left_prefix
                            = std::string { left } + ".";
                        const auto right_prefix
                            = std::string { right } + ".";
                        return left == right
                            || left.starts_with(right_prefix)
                            || right.starts_with(left_prefix);
                    };
                    const auto overlaps = [&](
                                              const ContainerBoundaryDriver& driver) {
                        return !driver_interval
                            || !driver.selected_interval
                            || (driver_interval->first
                                    <= driver.selected_interval->second
                                && driver.selected_interval->first
                                    <= driver_interval->second);
                    };
                    if (std::ranges::any_of(
                            drivers,
                            [&](const ContainerBoundaryDriver& driver) {
                                return overlaps(driver)
                                    && !nested_with(
                                        child_path, driver.path);
                            })) {
                        report(
                            "FSIM-ELAB-SVPORT-008",
                            "container object '" + actual_info_name
                                + "' has overlapping output/inout module container-port drivers",
                            compiled_source_span(
                                *compiled_, binding.source));
                    }
                    drivers.push_back(ContainerBoundaryDriver {
                        child_path, driver_interval });
                }
                if (!child_container_aliases.emplace(
                                                formal_declaration.name,
                                                connected_object)
                        .second) {
                    report(
                        "FSIM-ELAB-HIR-001",
                        "duplicate compiled container port binding for '"
                            + formal_declaration.name + "'",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                return true;
            }();
            if (!bound) {
                child_ports_valid = false;
            }
            continue;
        }
        const bool string_port = formal_declaration.type
            && (formal_declaration.type->value_form
                    == semantic::sv::TypeForm::string
                || formal_declaration.type->target.spelling
                    == "string");
        if (string_port) {
            const auto actual = working_strings.find(actual_name);
            if (actual == working_strings.end()) {
                report(
                    "FSIM-ELAB-SVPORT-010",
                    "unknown mutable string port actual '"
                        + actual_name + "' on instance '"
                        + child_path + "'",
                    compiled_source_span(
                        *compiled_, binding.source));
                return false;
            }
            const bool writable
                = direction == frontend::PortDirection::Output
                || direction == frontend::PortDirection::Inout;
            if (writable
                && working_read_only_strings.contains(
                    actual->second)) {
                report(
                    "FSIM-ELAB-SVPORT-011",
                    "an input mutable string port cannot be "
                    "connected to a descendant output or inout "
                    "port",
                    compiled_source_span(
                        *compiled_, binding.source));
                return false;
            }
            if (writable) {
                auto& drivers
                    = string_boundary_driver_paths_[actual->second];
                const auto nested_with = [](
                                             const std::string_view left,
                                             const std::string_view right) {
                    const auto left_prefix
                        = std::string { left } + ".";
                    const auto right_prefix
                        = std::string { right } + ".";
                    return left == right
                        || left.starts_with(right_prefix)
                        || right.starts_with(left_prefix);
                };
                if (std::ranges::any_of(drivers,
                        [&](const std::string& driver) {
                            return !nested_with(
                                child_path, driver);
                        })) {
                    report(
                        "FSIM-ELAB-SVPORT-012",
                        "a mutable string object has conflicting "
                        "output/inout module port drivers",
                        compiled_source_span(
                            *compiled_, binding.source));
                }
                if (std::ranges::find(drivers, child_path)
                    == drivers.end()) {
                    drivers.push_back(child_path);
                }
            }
            if (!child_string_aliases.emplace(
                                         formal_declaration.name,
                                         actual->second)
                    .second) {
                report(
                    "FSIM-ELAB-HIR-001",
                    "duplicate compiled string port binding for '"
                        + formal_declaration.name + "'",
                    compiled_source_span(
                        *compiled_, binding.source));
                return false;
            }
            continue;
        }
        const auto interface_port
            = !formal_declaration.interface_type.empty()
            || (formal_declaration.type
                && formal_declaration.type->target.spelling
                    == "interface");
        const auto interface_path
            = interface_port
            ? resolved_interface_path(actual_name)
            : std::nullopt;
        if (interface_port && !interface_path) {
            report(
                "FSIM-ELAB-SVIFACE-002",
                "interface actual '" + actual_name
                    + "' must name an earlier interface instance",
                compiled_source_span(*compiled_, binding.source));
            child_ports_valid = false;
            continue;
        }
        if (interface_path
            && !forward_interface_port(
                formal_declaration, *interface_path)) {
            child_ports_valid = false;
            continue;
        }
        const auto actual = working_signals.find(actual_name);
        std::optional<SignalId> actual_signal;
        if (actual != working_signals.end()) {
            actual_signal = actual->second;
        } else if (interface_path) {
            auto handle = systemverilog_interface_handles_.find(
                *interface_path);
            if (handle
                    != systemverilog_interface_handles_.end()
                && design_.signals_.size()
                    <= std::numeric_limits<SignalId>::max()) {
                const auto id = static_cast<SignalId>(
                    design_.signals_.size());
                const auto full_name = child_path + "."
                    + formal_declaration.name;
                SignalInfo info;
                info.id = id;
                info.name = full_name;
                info.width = 64U;
                info.type_name
                    = formal_declaration.interface_type;
                info.source_domain
                    = frontend::ValueDomain::Bit2;
                info.systemverilog_scalar
                    = frontend::SystemVerilogScalarKind::Chandle;
                info.is_port = true;
                info.direction = compiled_port_direction(
                    formal_declaration);
                info.declaration_span = compiled_source_span(
                    *compiled_, formal_declaration.source);
                design_.signal_info_.push_back(std::move(info));
                design_.signals_.push_back(
                    runtime::simir::Signal {
                        full_name,
                        PackedLogic4::from_aval_bval(
                            64U, handle->second, 0U),
                        ResolutionKind::none,
                        ValueKind::logic4,
                        std::nullopt,
                        { StrengthRank::pull,
                            StrengthRank::pull },
                        std::nullopt,
                        std::nullopt,
                        frontend::SystemVerilogScalarKind::Chandle,
                    });
                design_.signal_by_name_.insert_or_assign(
                    full_name, id);
                actual_signal = id;
            }
        }
        if (!actual_signal && binding.expression
            && !interface_port && formal_declaration.type) {
            const auto width = hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                *child_interface_specialization,
                formal_declaration);
            const semantic::CompiledDesignResolver type_resolver {
                *child_interface_specialization
            };
            const auto effective_type
                = type_resolver.underlying_systemverilog_type(
                                   *formal_declaration.type,
                                   formal_declaration.scope)
                      .value_or(*formal_declaration.type);
            if (width && *width != 0U
                && design_.signals_.size()
                    <= std::numeric_limits<SignalId>::max()) {
                const auto id = static_cast<SignalId>(
                    design_.signals_.size());
                const auto adapter_name = child_path
                    + ".$actual_" + formal_declaration.name;
                const auto domain
                    = formal_declaration.type->four_state
                        || effective_type.four_state
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2;
                SignalInfo info;
                info.id = id;
                info.name = adapter_name;
                info.width = *width;
                info.type_name
                    = formal_declaration.type->target.spelling;
                info.source_domain = domain;
                info.systemverilog_scalar
                    = compiled_systemverilog_scalar_kind(
                        effective_type.target.spelling);
                if (info.systemverilog_scalar
                    == frontend::SystemVerilogScalarKind::None) {
                    info.systemverilog_scalar
                        = compiled_systemverilog_scalar_kind(
                            formal_declaration.type
                                ->target.spelling);
                }
                const auto adapter_scalar
                    = info.systemverilog_scalar;
                info.is_signed
                    = formal_declaration.type->signed_value
                    || effective_type.signed_value;
                if (effective_type.packed_range) {
                    const auto& range
                        = *effective_type.packed_range;
                    const auto left = range.left_expression
                        ? child_interface_specialization
                              ->evaluate_integral_expression(
                                  *range.left_expression)
                        : range.left;
                    const auto right = range.right_expression
                        ? child_interface_specialization
                              ->evaluate_integral_expression(
                                  *range.right_expression)
                        : range.right;
                    if (left && right) {
                        info.packed_range = frontend::PackedRange {
                            *left, *right, range.descending
                        };
                    }
                }
                info.declaration_span = compiled_source_span(
                    *compiled_, binding.source);
                design_.signal_info_.push_back(std::move(info));
                design_.signals_.push_back(
                    runtime::simir::Signal {
                        adapter_name,
                        PackedLogic4(
                            *width,
                            domain == frontend::ValueDomain::Logic4
                                ? Logic4::x
                                : Logic4::zero),
                        ResolutionKind::none,
                        value_kind(domain),
                        std::nullopt,
                        { StrengthRank::pull,
                            StrengthRank::pull },
                        std::nullopt,
                        std::nullopt,
                        adapter_scalar,
                    });
                design_.signal_by_name_.emplace(adapter_name, id);
                Lowerer actual_lowerer {
                    design_,
                    working_signals,
                    working_read_only_signals,
                    working_strings,
                    working_read_only_strings,
                    working_containers,
                    working_read_only_containers,
                    diagnostics_,
                };
                actual_lowerer.set_specialized_hir_unit(
                    &working_specialization);
                actual_lowerer.set_systemverilog_interface_handles(
                    &systemverilog_interface_handles_);
                const auto append_adapter = [&](std::optional<Process> lowered) {
                    if (!lowered) {
                        return false;
                    }
                    lowered->language_standard = unit.standard;
                    lowered->compatibility_profile
                        = unit.compatibility_profile;
                    canonicalize_process_operations(*lowered);
                    child_boundary_processes.push_back(
                        lowered->id);
                    design_.processes_.push_back(
                        std::move(*lowered));
                    return true;
                };
                const auto accepts_input
                    = direction == frontend::PortDirection::Input
                    || direction
                        == frontend::PortDirection::Inout;
                const auto produces_output
                    = direction == frontend::PortDirection::Output
                    || direction
                        == frontend::PortDirection::Inout;
                bool adapted = true;
                if (accepts_input) {
                    adapted = append_adapter(
                        actual_lowerer.lower_hir_input_actual(
                            *binding.expression,
                            id,
                            source_language,
                            working_path,
                            concurrent_order++));
                }
                if (adapted && produces_output) {
                    adapted = append_adapter(
                        actual_lowerer.lower_hir_output_actual(
                            id,
                            *binding.expression,
                            source_language,
                            working_path,
                            concurrent_order++));
                }
                if (adapted) {
                    actual_signal = id;
                }
            }
        }
        if (!actual_signal) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled port actual '" + actual_name
                    + "' is not a signal in '"
                    + std::string { working_path } + "'",
                compiled_source_span(*compiled_, binding.source));
            return false;
        }
        if (formal_declaration.type
            && *actual_signal < design_.signal_info_.size()) {
            const semantic::CompiledDesignResolver type_resolver {
                *child_interface_specialization
            };
            const auto effective_type
                = type_resolver.underlying_systemverilog_type(
                                   *formal_declaration.type,
                                   formal_declaration.scope)
                      .value_or(*formal_declaration.type);
            auto formal_scalar
                = compiled_systemverilog_scalar_kind(
                    effective_type.target.spelling);
            if (formal_scalar
                == frontend::SystemVerilogScalarKind::None) {
                formal_scalar
                    = compiled_systemverilog_scalar_kind(
                        formal_declaration.type->target.spelling);
            }
            if (interface_port) {
                formal_scalar
                    = frontend::SystemVerilogScalarKind::Chandle;
            }
            const auto actual_scalar
                = design_.signal_info_[*actual_signal]
                      .systemverilog_scalar;
            if (formal_scalar != actual_scalar
                && (formal_scalar
                        != frontend::SystemVerilogScalarKind::None
                    || actual_scalar
                        != frontend::SystemVerilogScalarKind::None)) {
                report(
                    "FSIM-ELAB-BIND-019",
                    "incompatible SystemVerilog scalar types on '"
                        + child_path + "."
                        + formal_declaration.name + "'",
                    compiled_source_span(
                        *compiled_, binding.source));
                return false;
            }
        }
        const auto parent_signal = working_signals.find(actual_name);
        if ((direction == frontend::PortDirection::Output
                || direction == frontend::PortDirection::Inout)
            && parent_signal != working_signals.end()
            && parent_signal->second == *actual_signal) {
            note_boundary_driver(
                *actual_signal,
                external_binding,
                child_path + "." + formal_declaration.name,
                compiled_source_span(*compiled_, binding.source));
        }
        if (!child_aliases.emplace(
                              formal_declaration.name, *actual_signal)
                .second) {
            report(
                "FSIM-ELAB-HIR-001",
                "duplicate compiled port binding for '"
                    + formal_declaration.name + "'",
                compiled_source_span(*compiled_, binding.source));
            return false;
        }
    }

    return true;
}

} // namespace fsim::elaboration
