// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_ports_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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
                if (interface_formal) {
                    // Retain selected nested-interface members after the
                    // declaration chosen for the root name. An escaped root
                    // keeps the full written path because its identifier can
                    // contain the same punctuation as a path separator.
                    const auto suffix_start
                        = actual_expression.text.find_first_of(".[");
                    const auto escaped_start
                        = actual_expression.text.find('\\');
                    if (escaped_start != std::string::npos
                        && (suffix_start == std::string::npos
                            || escaped_start < suffix_start)) {
                        actual_name = actual_expression.text;
                    } else if (suffix_start != std::string::npos) {
                        actual_name += actual_expression.text.substr(
                            suffix_start);
                    }
                }
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

    std::string_view explicit_systemverilog_net_type(
        const semantic::sv::TypeReference& type) noexcept
    {
        auto* current = &type;
        while (true) {
            if (!current->systemverilog_net_type.empty()) {
                return current->systemverilog_net_type;
            }
            const auto spelling
                = std::string_view { current->target.spelling };
            if (semantic::sv::is_builtin_net_kind(spelling)) {
                return spelling;
            }
            if (current->container_element_types.size() != 1U) {
                return { };
            }
            current = &current->container_element_types.front();
        }
    }

    bool is_simple_variable_output_port(
        const semantic::sv::Declaration& declaration,
        const semantic::sv::TypeReference& effective_type) noexcept
    {
        if (!declaration.type
            || declaration.type->value_form
                != semantic::sv::TypeForm::packed_integral
            || effective_type.value_form
                != semantic::sv::TypeForm::packed_integral
            || declaration.type->virtual_interface
            || effective_type.virtual_interface
            || declaration.type->container_form
            || effective_type.container_form
            || !declaration.type->unpacked_dimensions.empty()
            || !effective_type.unpacked_dimensions.empty()
            || !declaration.type->systemverilog_resolution_function.empty()
            || !effective_type.systemverilog_resolution_function.empty()
            || !explicit_systemverilog_net_type(*declaration.type).empty()
            || !explicit_systemverilog_net_type(effective_type).empty()) {
            return false;
        }
        return frontend::systemverilog_scalar_kind(
                   declaration.type->target.spelling)
                == frontend::SystemVerilogScalarKind::None
            && frontend::systemverilog_scalar_kind(
                   effective_type.target.spelling)
                == frontend::SystemVerilogScalarKind::None;
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
                    return index_distance(type.index_left, type.index_right)
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
                        const auto supported_element_kind = [](
                            const ContainerElementKind kind) {
                            return kind == ContainerElementKind::Packed
                                || kind == ContainerElementKind::Scalar;
                        };
                        const bool ordinally_compatible
                            = expected->fixed && selected_type.fixed
                            && !expected->queue && !selected_type.queue
                            && !expected->associative
                            && !selected_type.associative
                            && expected->dimensions.size() == 1U
                            && selected_type.dimensions.size() == 1U
                            && supported_element_kind(
                                expected->element_kind)
                            && element_count(selected_type)
                                == element_count(*expected)
                            && same_element_profile(
                                selected_type, *expected);
                        if (!ordinally_compatible) {
                            report(
                                "FSIM-ELAB-SVPORT-007",
                                "whole-container port actuals require an exact container type match",
                                compiled_source_span(
                                    *compiled_, binding.source));
                            return false;
                        }

                        // A whole-array port connection preserves array
                        // declaration order even when the actual and formal
                        // use different index bounds or directions. Reuse the
                        // existing ordinal view so the child sees its formal
                        // range while reads and writes map across the actual's
                        // complete declared range.
                        slice_alias = ContainerSliceAlias {
                            actual->second,
                            selected_type.index_left,
                            selected_type.index_right,
                        };
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
        if (interface_port && formal_declaration.type
            && !formal_declaration.type->unpacked_dimensions.empty()
            && !formal_declaration.type->container_form) {
            // An interface port array binds element by element, left to
            // right, to the connected interface array (IEEE 1800-2017
            // 25.5).
            const auto& dimensions
                = formal_declaration.type->unpacked_dimensions;
            const auto bound = [&](const std::optional<std::int64_t>& value,
                                   const std::optional<semantic::ExpressionId>&
                                       expression)
                -> std::optional<std::int64_t> {
                if (value) {
                    return value;
                }
                return expression
                    ? child_interface_specialization
                          ->evaluate_integral_expression(*expression)
                    : std::nullopt;
            };
            // Element suffixes (`[i][j]`) of every dimension, left to
            // right, outermost first.
            const auto expand = [](std::vector<std::string>& suffixes,
                                   const std::int64_t left,
                                   const std::int64_t right) {
                std::vector<std::string> expanded;
                for (const auto& prefix : suffixes) {
                    for (auto index = left;; index += left > right ? -1 : 1) {
                        expanded.push_back(
                            prefix + "[" + std::to_string(index) + "]");
                        if (index == right) {
                            break;
                        }
                    }
                }
                suffixes = std::move(expanded);
            };
            std::vector<std::string> formal_suffixes { std::string { } };
            for (const auto& dimension : dimensions) {
                const auto left
                    = bound(dimension.left, dimension.left_expression);
                const auto right
                    = bound(dimension.right, dimension.right_expression);
                if (!left || !right
                    || (*left > *right ? *left - *right : *right - *left)
                        >= (std::int64_t { 1 } << 16)) {
                    formal_suffixes.clear();
                    break;
                }
                expand(formal_suffixes, *left, *right);
            }
            // The actual's elements: an interface instance array of this
            // unit, or an interface port array forwarded into it.
            std::optional<std::vector<std::string>> actual_suffixes;
            std::string actual_base;
            for (const auto instance_id : unit.instances) {
                const auto candidate = compiled_->find_instance(instance_id);
                if (candidate && candidate->systemverilog != nullptr
                    && candidate->systemverilog->name == actual_name
                    && (!candidate->systemverilog->array_indices.empty()
                        || candidate->systemverilog->array_left)) {
                    std::vector<std::string> suffixes;
                    const auto inner = compiled_instance_array_indices(
                        *candidate->systemverilog, working_specialization);
                    for (const auto& outer : compiled_instance_outer_indices(
                             *candidate->systemverilog,
                             working_specialization)) {
                        std::string prefix;
                        for (const auto index : outer) {
                            prefix += "[" + std::to_string(index) + "]";
                        }
                        for (const auto index : inner) {
                            suffixes.push_back(
                                prefix + "[" + std::to_string(index) + "]");
                        }
                    }
                    actual_suffixes = std::move(suffixes);
                    break;
                }
            }
            for (auto lexical = std::string { working_path };;) {
                const auto candidate = lexical + "." + actual_name;
                if (!actual_suffixes) {
                    if (const auto forwarded
                        = systemverilog_interface_array_indices_.find(
                            candidate);
                        forwarded
                        != systemverilog_interface_array_indices_.end()) {
                        actual_suffixes = forwarded->second;
                    }
                }
                if (actual_suffixes && !actual_suffixes->empty()
                    && systemverilog_interface_handles_.contains(
                        candidate + actual_suffixes->front())) {
                    actual_base = candidate;
                    break;
                }
                if (lexical.find('.') == std::string::npos) {
                    break;
                }
                lexical.resize(lexical.rfind('.'));
            }
            if (formal_suffixes.empty() || !actual_suffixes
                || actual_base.empty()
                || actual_suffixes->size() != formal_suffixes.size()) {
                report(
                    "FSIM-ELAB-SVIFACE-002",
                    "interface port array '" + formal_declaration.name
                        + "' requires an interface array actual of the "
                          "same size; '"
                        + actual_name + "' is not one",
                    compiled_source_span(*compiled_, binding.source));
                child_ports_valid = false;
                continue;
            }
            for (std::size_t position = 0;
                position < formal_suffixes.size(); ++position) {
                auto element = formal_declaration;
                element.name += formal_suffixes[position];
                if (!forward_interface_port(element,
                        actual_base + (*actual_suffixes)[position])) {
                    child_ports_valid = false;
                }
            }
            systemverilog_interface_array_indices_.insert_or_assign(
                child_path + "." + formal_declaration.name,
                std::move(formal_suffixes));
            continue;
        }
        auto interface_path
            = interface_port
            ? resolved_interface_path(actual_name)
            : std::nullopt;
        // An actual may select a modport of an interface instance
        // (`.port(bus.initiator)`, IEEE 1800-2017 25.5).
        auto viewed_formal = formal_declaration;
        if (interface_port && !interface_path) {
            const std::string_view actual_view { actual_name };
            const auto dot = actual_view.rfind('.');
            if (dot != std::string_view::npos) {
                const auto modport = std::string {
                    actual_view.substr(dot + 1U) };
                if (auto instance_path = resolved_interface_path(
                        actual_view.substr(0U, dot));
                    instance_path
                    && (viewed_formal.modport.empty()
                        || viewed_formal.modport == modport)) {
                    interface_path = std::move(instance_path);
                    viewed_formal.modport = modport;
                }
            }
        }
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
                viewed_formal, *interface_path)) {
            child_ports_valid = false;
            continue;
        }
        const auto actual = working_signals.find(actual_name);
        std::optional<SignalId> actual_signal;
        std::optional<SignalId> variable_output_actual_net;
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
        const auto actual_expression = binding.expression
            ? working_specialization.find_expression(
                  *binding.expression)
            : std::nullopt;
        const bool direct_name_actual
            = actual_expression
            && actual_expression->systemverilog != nullptr
            && actual_expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name;
        struct ScalarArrayOutputElement {
            ContainerObjectId object { };
            std::int32_t index { };
        };
        std::optional<ScalarArrayOutputElement>
            scalar_array_output_element;
        if (direction == frontend::PortDirection::Output
            && direct_name_actual
            && context.array_position
            && formal_declaration.type && !formal_container_type) {
            const auto actual_container
                = working_containers.find(actual_name);
            if (actual_container != working_containers.end()) {
                const auto actual_info = std::ranges::find_if(
                    design_.container_object_info_.rbegin(),
                    design_.container_object_info_.rend(),
                    [&](const ContainerObjectInfo& candidate) {
                        return candidate.id == actual_container->second;
                    });
                if (actual_info
                        == design_.container_object_info_.rend()
                    || actual_container->second
                        >= design_.container_objects_.size()) {
                    report(
                        "FSIM-ELAB-SVPORT-006",
                        "container connection object '" + actual_name
                            + "' has no elaborated type",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                if (working_read_only_containers.contains(actual_name)
                    || working_read_only_containers.contains(
                        actual_info->name)) {
                    report(
                        "FSIM-ELAB-SVPORT-009",
                        "an input container port cannot be connected to a "
                        "descendant output or inout port",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto& actual_type = actual_info->type;
                const auto& actual_object
                    = design_.container_objects_[actual_container->second];
                const auto element_count = [](const ContainerType& type) {
                    return index_distance(type.index_left, type.index_right)
                        + 1U;
                };
                const bool supported_array
                    = actual_type.fixed && !actual_type.queue
                    && !actual_type.associative
                    && !actual_type.string_indices
                    && !actual_type.aggregate_value
                    && actual_type.element_kind
                        == ContainerElementKind::Packed
                    && actual_type.element_width != 0U
                    && actual_type.dimensions.size() == 1U
                    && actual_type.index_left
                        == actual_type.dimensions.front().first
                    && actual_type.index_right
                        == actual_type.dimensions.front().second
                    && actual_object.initial_value.type == actual_type;
                if (!supported_array) {
                    report(
                        "FSIM-ELAB-SVPORT-007",
                        "an instance-array scalar output requires a fixed "
                        "one-dimensional packed array actual",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto [ordinal, instance_count]
                    = *context.array_position;
                const auto actual_count = element_count(actual_type);
                if (actual_count != instance_count
                    || ordinal >= actual_count
                    || actual_object.initial_value.elements.size()
                        != actual_count) {
                    report(
                        "FSIM-ELAB-SVPORT-007",
                        "an instance-array scalar output requires one actual "
                        "array element per instance",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                // Reuse the type-layout resolver for the formal scalar's
                // width, domain, signedness, and nominal identity.
                semantic::sv::TypeReference formal_element_array;
                formal_element_array.container_form
                    = semantic::sv::TypeForm::static_array;
                semantic::sv::PackedRange formal_element_range;
                formal_element_range.left = 0;
                formal_element_range.right = 0;
                formal_element_array.unpacked_dimensions.push_back(
                    std::move(formal_element_range));
                formal_element_array.container_element_types.push_back(
                    *formal_declaration.type);
                const auto formal_element_profile
                    = hierarchy_sv_type_layout_detail::
                        systemverilog_container_type(
                            *child_interface_specialization,
                            formal_element_array);
                // IEEE 1800-2017 6.22.2(c) makes packed arrays, structures,
                // and unions equivalent when their representation matches.
                // Keep enum identity strict because it is not in that rule.
                const auto is_packed_aggregate_nominal =
                    [](const std::string_view nominal_type,
                        const semantic::SpecializedHirUnit& specialization) {
                        const auto type
                            = semantic::sv::nominal_identity_type(
                                nominal_type);
                        const auto definition = type
                            ? specialization.find_type(*type)
                            : std::nullopt;
                        if (!definition
                            || definition->systemverilog == nullptr) {
                            return false;
                        }
                        const auto form = definition->systemverilog->form;
                        return form
                                   == semantic::sv::TypeForm::packed_structure
                            || form
                                   == semantic::sv::TypeForm::packed_union;
                    };
                const auto same_element_profile =
                    [&is_packed_aggregate_nominal,
                        &working_specialization,
                        &child_interface_specialization](
                        const ContainerType& left,
                        const ContainerType& right) {
                        if (left.element_kind != right.element_kind
                            || left.scalar_kind != right.scalar_kind
                            || left.element_width != right.element_width
                            || left.two_state != right.two_state
                            || left.signed_elements
                                != right.signed_elements) {
                            return false;
                        }
                        if (left.element_nominal_type
                            == right.element_nominal_type) {
                            return true;
                        }
                        if (!left.element_nominal_type.empty()
                            && !is_packed_aggregate_nominal(
                                left.element_nominal_type,
                                working_specialization)) {
                            return false;
                        }
                        return right.element_nominal_type.empty()
                            || is_packed_aggregate_nominal(
                                right.element_nominal_type,
                                *child_interface_specialization);
                    };
                if (!formal_element_profile
                    || formal_element_profile->element_kind
                        != ContainerElementKind::Packed
                    || !same_element_profile(
                        actual_type, *formal_element_profile)) {
                    report(
                        "FSIM-ELAB-SVPORT-007",
                        "an instance-array scalar output requires an "
                        "identical packed element profile",
                        compiled_source_span(
                            *compiled_, binding.source));
                    return false;
                }
                const auto has_signal_backing = [&] {
                    auto object = actual_container->second;
                    for (std::size_t depth = 0U;
                        depth < design_.container_objects_.size();
                        ++depth) {
                        if (std::ranges::any_of(
                                design_.container_element_signal_aliases_,
                                [object](const ContainerElementSignalAlias& alias) {
                                    return alias.object == object;
                                })
                            || std::ranges::any_of(
                                design_.container_aggregate_signal_aliases_,
                                [object](const ContainerAggregateSignalAlias& alias) {
                                    return alias.object == object;
                                })) {
                            return true;
                        }
                        const auto direct_alias = std::ranges::find_if(
                            design_.container_signal_aliases_,
                            [object](const ContainerSignalAlias& alias) {
                                return alias.object == object;
                            });
                        if (direct_alias
                            != design_.container_signal_aliases_.end()) {
                            if (!direct_alias->writable
                                || direct_alias->signal
                                    >= design_.signal_info_.size()
                                || direct_alias->signal
                                    >= design_.signals_.size()) {
                                return true;
                            }
                            const auto& proxy_info
                                = design_.signal_info_[direct_alias->signal];
                            const auto& proxy_signal
                                = design_.signals_[direct_alias->signal];
                            // A variable array's aggregate bridge is writable
                            // through WriteContainerObjectElement, which
                            // commits only that element's packed slice. Net
                            // or resolved proxies still need element drivers.
                            return !proxy_info.systemverilog_net_type.empty()
                                || proxy_signal.resolution
                                    != ResolutionKind::none;
                        }
                        const auto& container_object
                            = design_.container_objects_[object];
                        if (!container_object.slice_alias) {
                            return false;
                        }
                        object = container_object.slice_alias->object;
                        if (object >= design_.container_objects_.size()) {
                            return true;
                        }
                    }
                    return true;
                };
                // This bridge writes variable-array storage only. A
                // signal-backed array needs per-element driver regions.
                if (!has_signal_backing()) {
                    const auto left
                        = static_cast<std::int64_t>(
                            actual_type.index_left);
                    const auto ordinal_offset
                        = static_cast<std::int64_t>(ordinal);
                    const auto index = actual_type.index_left
                            >= actual_type.index_right
                        ? left - ordinal_offset
                        : left + ordinal_offset;
                    if (index
                            < std::numeric_limits<std::int32_t>::min()
                        || index
                            > std::numeric_limits<std::int32_t>::max()) {
                        report(
                            "FSIM-ELAB-SVPORT-007",
                            "an instance-array output element index is out of range",
                            compiled_source_span(
                                *compiled_, binding.source));
                        return false;
                    }
                    scalar_array_output_element
                        = ScalarArrayOutputElement {
                            actual_container->second,
                            static_cast<std::int32_t>(index),
                        };
                }
            }
        }
        if (direction == frontend::PortDirection::Output
            && direct_name_actual && actual_signal
            && formal_declaration.type
            && *actual_signal < design_.signal_info_.size()
            && *actual_signal < design_.signals_.size()) {
            const semantic::CompiledDesignResolver type_resolver {
                *child_interface_specialization
            };
            const auto effective_type
                = type_resolver.underlying_systemverilog_type(
                                   *formal_declaration.type,
                                   formal_declaration.scope)
                      .value_or(*formal_declaration.type);
            const auto& actual_info
                = design_.signal_info_[*actual_signal];
            const auto actual_native_resolution
                = native_resolution(actual_info);
            const auto registered_user_nettype
                = systemverilog_resolution_kinds_.contains(
                    actual_info.systemverilog_net_type);
            const auto actual_explicit_resolution
                = actual_info.systemverilog_net_type.empty()
                || actual_native_resolution != ResolutionKind::none
                || !registered_user_nettype
                ? std::optional<ResolutionKind> { }
                : explicit_resolution(*actual_signal);
            const auto supported_user_resolved_net
                = actual_explicit_resolution
                && *actual_explicit_resolution
                    == ResolutionKind::sv_user_first;
            // The unresolved uwire form remains single-driver checked by
            // validate_process_drivers(); it does not acquire a resolver.
            const auto single_driver_uwire
                = actual_info.systemverilog_net_type == "uwire";
            const auto formal_width
                = hierarchy_sv_type_layout_detail::
                    systemverilog_declaration_width(
                        *child_interface_specialization,
                        formal_declaration);
            const auto formal_domain
                = formal_declaration.type->four_state
                        || effective_type.four_state
                    ? frontend::ValueDomain::Logic4
                    : frontend::ValueDomain::Bit2;
            if (is_simple_variable_output_port(
                    formal_declaration, effective_type)
                && (actual_native_resolution != ResolutionKind::none
                    || supported_user_resolved_net
                    || single_driver_uwire)
                && !actual_info.systemverilog_net_type.empty()
                && formal_width && *formal_width != 0U
                && *formal_width == actual_info.width
                && (formal_domain == actual_info.source_domain
                    || (formal_domain == frontend::ValueDomain::Bit2
                        && actual_info.source_domain
                            == frontend::ValueDomain::Logic4))) {
                // An output variable has its own initialized state. The
                // implicit port connection drives the resolved actual; it
                // does not alias the variable's storage onto that net.
                variable_output_actual_net = *actual_signal;
                actual_signal.reset();
            }
        }
        // A port whose actual has a different packed width cannot share the
        // actual's storage; it connects through an adapter that pads or
        // truncates like a continuous assignment (IEEE 1800-2017 23.3.3).
        if (actual_signal && direct_name_actual && !interface_port
            && formal_declaration.type
            && (direction == frontend::PortDirection::Input
                || direction == frontend::PortDirection::Output)
            && *actual_signal < design_.signal_info_.size()) {
            const auto formal_width
                = hierarchy_sv_type_layout_detail::
                    systemverilog_declaration_width(
                        *child_interface_specialization,
                        formal_declaration);
            if (formal_width && *formal_width != 0U
                && *formal_width
                    != design_.signal_info_[*actual_signal].width) {
                actual_signal.reset();
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
                    = frontend::systemverilog_scalar_kind(
                        effective_type.target.spelling);
                if (info.systemverilog_scalar
                    == frontend::SystemVerilogScalarKind::None) {
                    info.systemverilog_scalar
                        = frontend::systemverilog_scalar_kind(
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
                auto initial_value = PackedLogic4(
                    *width,
                    domain == frontend::ValueDomain::Logic4
                        ? Logic4::x
                        : Logic4::zero);
                if (variable_output_actual_net
                    && formal_declaration.initializer) {
                    const auto initial
                        = child_interface_specialization
                              ->evaluate_integral_expression(
                                  *formal_declaration.initializer);
                    if (!initial || *width > 64U) {
                        report(
                            "FSIM-ELAB-HIR-001",
                            "compiled initializer for '"
                                + formal_declaration.name
                                + "' is not a bounded integral value",
                            compiled_source_span(
                                *compiled_, formal_declaration.source));
                        return false;
                    }
                    initial_value = PackedLogic4::from_aval_bval(
                        *width,
                        static_cast<std::uint64_t>(*initial),
                        0U);
                }
                design_.signal_info_.push_back(std::move(info));
                design_.signals_.push_back(
                    runtime::simir::Signal {
                        adapter_name,
                        std::move(initial_value),
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
                    &systemverilog_interface_handles_, &systemverilog_interface_types_);
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
                    design_.append_process_record(
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
                // An inout port whose actual is a constant select of a net
                // (`bus[0]`) is a bidirectional connection: a switch joins
                // the formal net to the selected bits, so drivers on either
                // side resolve together (IEEE 1800-2017 23.3.3.3). Copying
                // each way instead would hold an unknown value forever.
                const auto inout_actual
                    = direction == frontend::PortDirection::Inout
                        && domain == frontend::ValueDomain::Logic4
                    ? actual_lowerer.hir_inout_actual(*binding.expression)
                    : std::nullopt;
                const auto joinable = [&] {
                    if (!inout_actual) {
                        return false;
                    }
                    std::size_t total { };
                    for (const auto& piece : *inout_actual) {
                        if (piece.signal >= design_.signals_.size()
                            || native_resolution(
                                   design_.signal_info_[piece.signal])
                                != ResolutionKind::sv_wire
                            || piece.offset + piece.width
                                > design_.signals_[piece.signal]
                                      .initial_value.width()) {
                            return false;
                        }
                        total += piece.width;
                    }
                    return total == *width;
                }();
                if (joinable) {
                    design_.signal_info_[id].systemverilog_net_type
                        = "wire";
                    auto& formal_signal = design_.signals_[id];
                    formal_signal.resolution = ResolutionKind::sv_wire;
                    formal_signal.initial_value
                        = PackedLogic4(*width, Logic4::z);
                    std::size_t ordinal { };
                    for (const auto& piece : *inout_actual) {
                        const auto process_index = design_.process_count();
                        const auto process_id
                            = static_cast<ProcessId>(process_index);
                        if (static_cast<std::size_t>(process_id)
                            != process_index) {
                            throw std::length_error {
                                "too many SimIR processes" };
                        }
                        Process process;
                        process.id = process_id;
                        process.name = adapter_name + ".$inout_"
                            + std::to_string(ordinal++);
                        process.scheduling_domain
                            = ProcessSchedulingDomain::systemverilog;
                        process.switch_source = piece.signal;
                        process.switch_target = id;
                        process.switch_source_offset = piece.offset;
                        process.switch_target_offset = piece.formal_offset;
                        process.switch_width = piece.width;
                        process.switch_bidirectional = true;
                        process.initialize = false;
                        process.operations.emplace_back(Halt { });
                        child_boundary_processes.push_back(process.id);
                        design_.append_process_record(std::move(process));
                    }
                    actual_signal = id;
                    adapted = false;
                }
                // An instance of an instance array connects to its own
                // slice of an actual as wide as all the instances' ports,
                // the leftmost instance to the leftmost bits (23.3.3.5).
                std::optional<std::pair<std::uint32_t, std::uint32_t>>
                    actual_slice;
                if (!joinable && !scalar_array_output_element
                    && context.array_position
                    && context.array_position->second > 1U) {
                    const auto [ordinal, count] = *context.array_position;
                    const auto natural = actual_lowerer.hir_actual_width(
                        *binding.expression);
                    if (natural && *width != 0U
                        && *natural == *width * count
                        && *natural <= std::numeric_limits<std::uint32_t>::max()) {
                        actual_slice = std::pair {
                            static_cast<std::uint32_t>(
                                (count - 1U - ordinal) * *width),
                            static_cast<std::uint32_t>(*width),
                        };
                    }
                }
                if (scalar_array_output_element) {
                    // A scalar output of an instance array writes the
                    // matching declared-order element of an unpacked
                    // variable actual. Writing through a slice view lets
                    // the existing ContainerSliceAlias path propagate the
                    // element to the connected parent array.
                    Process element_output;
                    const auto process_index = design_.process_count();
                    element_output.id = static_cast<ProcessId>(process_index);
                    if (static_cast<std::size_t>(element_output.id)
                        != process_index) {
                        throw std::length_error {
                            "too many SimIR processes" };
                    }
                    element_output.name
                        = adapter_name + ".$array_element_output";
                    element_output.language_standard = unit.standard;
                    element_output.compatibility_profile
                        = unit.compatibility_profile;
                    element_output.scheduling_domain
                        = ProcessSchedulingDomain::systemverilog;
                    element_output.static_sensitivity.push_back({
                        id, EdgeKind::any
                    });
                    element_output.operations.emplace_back(
                        ReadSignal { 0U, id });
                    element_output.operations.emplace_back(LoadConstant {
                        1U,
                        PackedLogic4::from_aval_bval(
                            32U,
                            static_cast<std::uint32_t>(
                                scalar_array_output_element->index),
                            0U),
                    });
                    element_output.operations.emplace_back(
                        WriteContainerObjectElement {
                            scalar_array_output_element->object,
                            1U,
                            0U,
                            true,
                            false,
                            false,
                            std::nullopt,
                            std::nullopt,
                            { },
                        });
                    element_output.operations.emplace_back(
                        WaitSensitivity { });
                    element_output.operations.emplace_back(Jump { 0U });
                    element_output.register_count = 2U;
                    element_output.register_value_kinds.push_back(
                        design_.signals_[id].value_kind);
                    element_output.register_value_kinds.push_back(
                        ValueKind::logic4);
                    adapted = append_adapter(
                        std::optional<Process> {
                            std::move(element_output) });
                    if (adapted) {
                        actual_signal = id;
                    }
                }
                if (!joinable && accepts_input) {
                    adapted = append_adapter(
                        actual_lowerer.lower_hir_input_actual(
                            *binding.expression,
                            id,
                            source_language,
                            working_path,
                            concurrent_order++,
                            std::nullopt,
                            actual_slice));
                }
                if (adapted && produces_output && !actual_signal) {
                    adapted = append_adapter(
                        actual_lowerer.lower_hir_output_actual(
                            id,
                            *binding.expression,
                            source_language,
                            working_path,
                            concurrent_order++,
                            actual_slice));
                }
                if (adapted && !actual_signal) {
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
                = frontend::systemverilog_scalar_kind(
                    effective_type.target.spelling);
            if (formal_scalar
                == frontend::SystemVerilogScalarKind::None) {
                formal_scalar
                    = frontend::systemverilog_scalar_kind(
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
        if (!interface_port && variable_output_actual_net
            && parent_signal != working_signals.end()
            && parent_signal->second == *variable_output_actual_net) {
            note_boundary_driver(
                *variable_output_actual_net,
                external_binding,
                child_path + "." + formal_declaration.name,
                compiled_source_span(*compiled_, binding.source));
        } else if (!interface_port
            && (direction == frontend::PortDirection::Output
                || direction == frontend::PortDirection::Inout)
            && parent_signal != working_signals.end()
            && parent_signal->second == *actual_signal) {
            note_boundary_driver(
                *actual_signal,
                external_binding,
                child_path + "." + formal_declaration.name,
                compiled_source_span(*compiled_, binding.source));
        }
        // A pull port (`inout tri0 z`) merged with a plain wire or tri
        // actual makes the merged net that pull type (IEEE 1800-2017
        // 23.3.3.7, Table 23-1).
        if (!interface_port && formal_declaration.type
            && *actual_signal < design_.signals_.size()
            && *actual_signal < design_.signal_info_.size()) {
            const auto& formal_net
                = formal_declaration.type->systemverilog_net_type.empty()
                ? formal_declaration.type->target.spelling
                : formal_declaration.type->systemverilog_net_type;
            auto& merged = design_.signals_[*actual_signal];
            auto& merged_info = design_.signal_info_[*actual_signal];
            if ((formal_net == "tri0" || formal_net == "tri1")
                && !merged.implicit_driver
                && (merged_info.systemverilog_net_type == "wire"
                    || merged_info.systemverilog_net_type == "tri")) {
                const auto pull = formal_net == "tri0"
                    ? Logic4::zero : Logic4::one;
                merged.implicit_driver = pull;
                merged.initial_value = PackedLogic4 {
                    merged.initial_value.width(), pull };
                merged_info.systemverilog_net_type = formal_net;
            }
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
