// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {

namespace {

std::string normalized_library(const DesignUnit& unit) {
    return unit.library.empty() ? "work" : unit.library;
}

bool vhdl_name_equal(
    const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const unsigned char lhs, const unsigned char rhs) {
            return std::tolower(lhs) == std::tolower(rhs);
        });
}

} // namespace



const DesignUnit* choose_unit(
    const frontend::ParsedDesign& parsed, const std::string& requested) {
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VerilogModule && unit.name == requested) {
            return &unit;
        }
    }
    for (const auto& unit : parsed.units) {
        if (unit.kind
                == frontend::UnitKind::VhdlConfiguration
            && unit.name == requested) {
            return &unit;
        }
    }
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VhdlArchitecture
            && unit.primary_name == requested) {
            return &unit;
        }
    }
    return nullptr;
}



std::optional<TargetSpec> parse_target(const std::string_view spelling) {
    const auto colon = spelling.find(':');
    if (colon == std::string_view::npos || colon == 0
        || colon + 1 == spelling.size()) {
        return std::nullopt;
    }
    TargetSpec result;
    result.language = std::string{spelling.substr(0, colon)};
    auto remainder = spelling.substr(colon + 1);
    if (const auto dot = remainder.rfind('.'); dot != std::string_view::npos) {
        result.library = std::string{remainder.substr(0, dot)};
        remainder.remove_prefix(dot + 1);
    }
    if (const auto open = remainder.find('('); open != std::string_view::npos) {
        if (remainder.back() != ')' || open + 1 == remainder.size() - 1) {
            return std::nullopt;
        }
        result.architecture =
            std::string{remainder.substr(open + 1, remainder.size() - open - 2)};
        remainder = remainder.substr(0, open);
    }
    if (remainder.empty()) {
        return std::nullopt;
    }
    result.unit = std::string{remainder};
    return result;
}



const DesignUnit* find_vhdl_entity(
    const frontend::ParsedDesign& parsed, const DesignUnit& architecture) {
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VhdlEntity
            && unit.name == architecture.primary_name
            && (unit.library.empty() || architecture.library.empty()
                || unit.library == architecture.library)) {
            return &unit;
        }
    }
    return nullptr;
}



const std::vector<frontend::SignalDeclaration>* unit_ports(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& unit) {
    if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        if (!unit.ports.empty()) {
            return &unit.ports;
        }
        const auto* entity = find_vhdl_entity(parsed, unit);
        return entity == nullptr ? nullptr : &entity->ports;
    }
    return &unit.ports;
}



const DesignUnit* choose_bound_unit(
    const frontend::ParsedDesign& parsed,
    const TargetSpec& target) {
    if (target.language == "sv" || target.language == "verilog") {
        for (const auto& unit : parsed.units) {
            if (unit.kind == frontend::UnitKind::VerilogModule
                && unit.name == target.unit
                && (target.library.empty() || unit.library.empty()
                    || unit.library == target.library)) {
                return &unit;
            }
        }
        return nullptr;
    }
    if (target.language == "vhdl") {
        for (const auto& unit : parsed.units) {
            if (unit.kind == frontend::UnitKind::VhdlArchitecture
                && unit.primary_name == target.unit
                && (target.library.empty() || unit.library.empty()
                    || unit.library == target.library)
                && (!target.architecture
                    || unit.name == *target.architecture)) {
                return &unit;
            }
        }
    }
    return nullptr;
}



const DesignUnit* choose_top_unit(
    const frontend::ParsedDesign& parsed,
    const std::string_view spelling) {
    if (spelling.find(':') != std::string_view::npos) {
        const auto target = parse_target(spelling);
        if (!target) {
            return nullptr;
        }
        if (target->language == "vhdl"
            && !target->architecture) {
            for (const auto& unit : parsed.units) {
                const auto library =
                    unit.library.empty()
                        ? std::string_view{"work"}
                        : std::string_view{unit.library};
                if (unit.kind
                        == frontend::UnitKind::VhdlConfiguration
                    && unit.name == target->unit
                    && (target->library.empty()
                        || library == target->library)) {
                    return &unit;
                }
            }
        }
        return choose_bound_unit(parsed, *target);
    }
    return choose_unit(parsed, simple_top_name(spelling));
}



std::vector<UnitResolutionCandidate> resolve_unit_candidates(
    const frontend::ParsedDesign& parsed,
    const std::string_view library,
    const std::string_view name,
    const bool include_vhdl_configurations,
    const bool include_udp_declarations) {
    const auto requested_library =
        library.empty() ? std::string_view{"work"} : library;
    std::vector<UnitResolutionCandidate> result;
    for (const auto& unit : parsed.units) {
        if (normalized_library(unit) != requested_library) {
            continue;
        }
        const bool verilog_module =
            unit.kind == frontend::UnitKind::VerilogModule
            && unit.name == name;
        const bool systemverilog_interface =
            unit.kind == frontend::UnitKind::SystemVerilogInterface
            && unit.name == name;
        const bool vhdl_architecture =
            unit.kind == frontend::UnitKind::VhdlArchitecture
            && vhdl_name_equal(unit.primary_name, name);
        const bool vhdl_configuration =
            include_vhdl_configurations
            && unit.kind == frontend::UnitKind::VhdlConfiguration
            && vhdl_name_equal(unit.name, name);
        if (!verilog_module && !systemverilog_interface
            && !vhdl_architecture
            && !vhdl_configuration) {
            continue;
        }
        result.push_back({&unit, std::nullopt, unit_identity(unit)});
    }
    if (include_udp_declarations) {
        for (const auto& udp : parsed.udp_declarations) {
            const auto udp_library = udp.library.empty()
                ? std::string_view{"work"}
                : std::string_view{udp.library};
            if (udp_library != requested_library || udp.name != name) {
                continue;
            }
            result.push_back({
                nullptr,
                std::nullopt,
                "udp:" + std::string{udp_library} + "." + udp.name,
                &udp});
        }
    }
    std::stable_sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return left.identity < right.identity;
        });
    return result;
}

std::vector<std::string> effective_search_scope(
    const std::string_view parent_library,
    const std::span<const std::string> search_libraries) {
    std::vector<std::string> result;
    result.reserve(search_libraries.size() + 1);
    const auto append = [&](const std::string_view library) {
        const auto normalized = library.empty()
            ? std::string{"work"} : std::string{library};
        if (std::ranges::find(result, normalized) == result.end()) {
            result.push_back(normalized);
        }
    };
    append(parent_library);
    for (const auto& library : search_libraries) {
        append(library);
    }
    return result;
}

bool has_logical_library(
    const frontend::ParsedDesign& parsed,
    const std::span<const SystemCFactoryCandidate> systemc_candidates,
    const std::span<const std::string> systemc_libraries,
    const std::string_view library) {
    return std::ranges::any_of(
               parsed.units,
               [&](const DesignUnit& unit) {
                   return normalized_library(unit) == library;
               })
        || std::ranges::any_of(
               parsed.udp_declarations,
               [&](const frontend::VerilogUdpDeclaration& udp) {
                   return (udp.library.empty()
                               ? std::string_view{"work"}
                               : std::string_view{udp.library})
                       == library;
               })
        || std::ranges::any_of(
               systemc_candidates,
               [&](const SystemCFactoryCandidate& candidate) {
                   return candidate.library == library;
               })
        || std::ranges::find(systemc_libraries, library)
            != systemc_libraries.end();
}



std::string format_resolution_candidates(
    const std::span<const UnitResolutionCandidate> candidates) {
    std::ostringstream output;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << candidates[index].identity;
    }
    return output.str();
}



const DesignUnit* choose_same_language_instance(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& parent,
    const std::string_view name) {
    if (parent.language == frontend::Language::Vhdl2008) {
        auto primary = name;
        std::optional<std::string_view> architecture;
        if (const auto open = primary.find('(');
            open != std::string_view::npos && primary.back() == ')') {
            architecture = primary.substr(
                open + 1, primary.size() - open - 2);
            primary = primary.substr(0, open);
        }
        std::optional<std::string_view> selected_library;
        if (const auto dot = primary.rfind('.');
            dot != std::string_view::npos) {
            selected_library = primary.substr(0, dot);
            primary.remove_prefix(dot + 1);
        }
        const auto desired_library =
            selected_library.value_or(parent.library);
        for (const auto& unit : parsed.units) {
            if (unit.kind == frontend::UnitKind::VhdlArchitecture
                && unit.primary_name == primary
                && (desired_library.empty() || unit.library.empty()
                    || unit.library == desired_library)
                && (!architecture || unit.name == *architecture)) {
                return &unit;
            }
        }
        return nullptr;
    }
    for (const auto& unit : parsed.units) {
        if ((unit.kind == frontend::UnitKind::VerilogModule
             || unit.kind
                 == frontend::UnitKind::SystemVerilogInterface)
            && unit.language == parent.language && unit.name == name) {
            if (!unit.library.empty() && !parent.library.empty()
                && unit.library != parent.library) {
                continue;
            }
            return &unit;
        }
    }
    return nullptr;
}



std::string unit_identity(const DesignUnit& unit) {
    const auto library =
        unit.library.empty()
            ? std::string_view{"work"}
            : std::string_view{unit.library};
    if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        return "vhdl:" + std::string{library} + "." + unit.primary_name
            + "(" + unit.name + ")";
    }
    if (unit.kind
            == frontend::UnitKind::VhdlConfiguration) {
        return "vhdl:" + std::string{library}
            + ".configuration(" + unit.name + ")";
    }
    if (unit.kind == frontend::UnitKind::SystemVerilogInterface) {
        return "sv:" + std::string{library} + ".interface(" + unit.name
            + ")";
    }
    return "sv:" + std::string{library} + "." + unit.name;
}

} // namespace fsim::elaboration::elaboration_detail
