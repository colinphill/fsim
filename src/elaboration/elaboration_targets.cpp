// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {

namespace {

std::string_view normalized_library(const std::string_view library) {
    return library.empty() ? std::string_view{"work"} : library;
}

std::string_view normalized_library(const semantic::sv::Unit& unit) {
    return normalized_library(unit.library);
}

std::string_view normalized_library(const semantic::vhdl::Unit& unit) {
    return normalized_library(unit.library);
}

std::string_view normalized_library(
    const semantic::sv::UdpDeclaration& udp) {
    return normalized_library(udp.library);
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

bool vhdl_library_equal(
    const std::string_view left, const std::string_view right) {
    return vhdl_name_equal(
        normalized_library(left), normalized_library(right));
}

semantic::CompiledUnitView unit_view(
    const semantic::CompiledDesign& compiled,
    const semantic::sv::Unit& unit) {
    if (const auto found = compiled.find_unit(unit.id);
        found && found->systemverilog == &unit) {
        return *found;
    }
    const auto identities = compiled.units();
    const auto* identity = unit.id.valid()
            && unit.id.value() < identities.size()
        ? &identities[unit.id.value()]
        : nullptr;
    const auto language = identity == nullptr
        ? semantic::Language::system_verilog
        : identity->language;
    return {language, identity, &unit, nullptr};
}

semantic::CompiledUnitView unit_view(
    const semantic::CompiledDesign& compiled,
    const semantic::vhdl::Unit& unit) {
    if (const auto found = compiled.find_unit(unit.id);
        found && found->vhdl == &unit) {
        return *found;
    }
    const auto identities = compiled.units();
    const auto* identity = unit.id.valid()
            && unit.id.value() < identities.size()
        ? &identities[unit.id.value()]
        : nullptr;
    return {
        semantic::Language::vhdl,
        identity,
        nullptr,
        &unit,
    };
}

enum class SystemVerilogLibraryMatch : std::uint8_t {
    any,
    exact_logical,
    empty_is_wildcard,
};

std::vector<semantic::CompiledUnitView>
systemverilog_unit_candidates(
    const semantic::CompiledDesign& compiled,
    const std::string_view name,
    const std::span<const semantic::sv::UnitKind> kinds,
    const bool include_external,
    const SystemVerilogLibraryMatch library_match,
    const std::string_view library = { })
{
    std::vector<semantic::CompiledUnitView> result;
    for (const auto& unit : compiled.systemverilog_units()) {
        if (unit.name != name
            || (!include_external && unit.external)
            || std::ranges::find(kinds, unit.kind) == kinds.end()) {
            continue;
        }
        const auto library_matches = [&] {
            switch (library_match) {
            case SystemVerilogLibraryMatch::any:
                return true;
            case SystemVerilogLibraryMatch::exact_logical:
                return normalized_library(unit)
                    == normalized_library(library);
            case SystemVerilogLibraryMatch::empty_is_wildcard:
                return library.empty() || unit.library.empty()
                    || unit.library == library;
            }
            return false;
        }();
        if (library_matches) {
            result.push_back(unit_view(compiled, unit));
        }
    }
    return result;
}

} // namespace

CompiledPortView::operator bool() const noexcept {
    return (systemverilog != nullptr) != (vhdl != nullptr)
        && (vhdl == nullptr) == (language != semantic::Language::vhdl);
}

std::string_view CompiledPortView::name() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->name;
    }
    return vhdl == nullptr ? std::string_view { } : vhdl->name;
}

semantic::DeclarationId CompiledPortView::declaration() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->id;
    }
    return vhdl == nullptr ? semantic::DeclarationId { } : vhdl->id;
}

CompiledPortMode CompiledPortView::mode() const noexcept {
    if (systemverilog != nullptr) {
        switch (systemverilog->direction) {
        case semantic::sv::Direction::unknown:
            return CompiledPortMode::unknown;
        case semantic::sv::Direction::input:
            return CompiledPortMode::input;
        case semantic::sv::Direction::output:
            return CompiledPortMode::output;
        case semantic::sv::Direction::inout:
            return CompiledPortMode::inout;
        case semantic::sv::Direction::ref:
            return CompiledPortMode::reference;
        }
    }
    if (vhdl != nullptr) {
        switch (vhdl->direction) {
        case semantic::vhdl::Direction::unknown:
            return CompiledPortMode::unknown;
        case semantic::vhdl::Direction::input:
            return CompiledPortMode::input;
        case semantic::vhdl::Direction::output:
            return CompiledPortMode::output;
        case semantic::vhdl::Direction::inout:
            return CompiledPortMode::inout;
        case semantic::vhdl::Direction::buffer:
            return CompiledPortMode::buffer;
        }
    }
    return CompiledPortMode::unknown;
}

semantic::SourceSpanId CompiledPortView::source() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->source;
    }
    return vhdl == nullptr ? semantic::SourceSpanId { } : vhdl->source;
}

semantic::OriginId CompiledPortView::origin() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->origin;
    }
    return vhdl == nullptr ? semantic::OriginId { } : vhdl->origin;
}

std::optional<semantic::ExpressionId>
CompiledPortView::default_expression() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->initializer;
    }
    return vhdl == nullptr
        ? std::optional<semantic::ExpressionId> { }
        : vhdl->initializer;
}

const semantic::sv::TypeReference*
CompiledPortView::systemverilog_type() const noexcept {
    return systemverilog == nullptr || !systemverilog->type
        ? nullptr : &*systemverilog->type;
}

const semantic::sv::PackedRange*
CompiledPortView::systemverilog_packed_range() const noexcept {
    const auto* type = systemverilog_type();
    return type == nullptr || !type->packed_range
        ? nullptr : &*type->packed_range;
}

const semantic::vhdl::SubtypeIndication*
CompiledPortView::vhdl_subtype() const noexcept {
    return vhdl == nullptr || !vhdl->subtype ? nullptr : &*vhdl->subtype;
}

std::span<const semantic::vhdl::RangeConstraint>
CompiledPortView::vhdl_constraints() const noexcept {
    const auto* subtype = vhdl_subtype();
    return subtype == nullptr
        ? std::span<const semantic::vhdl::RangeConstraint> { }
        : std::span<const semantic::vhdl::RangeConstraint> {
              subtype->constraints };
}

std::string_view CompiledPortView::interface_type() const noexcept {
    return systemverilog == nullptr
        ? std::string_view { } : systemverilog->interface_type;
}

std::string_view CompiledPortView::modport() const noexcept {
    return systemverilog == nullptr
        ? std::string_view { } : systemverilog->modport;
}

CompiledActualView::operator bool() const noexcept {
    return (systemverilog != nullptr) != (vhdl != nullptr);
}

std::optional<std::string_view>
CompiledActualView::formal() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->formal
            ? std::optional<std::string_view> { *systemverilog->formal }
            : std::nullopt;
    }
    return vhdl != nullptr && vhdl->formal
        ? std::optional<std::string_view> { vhdl->formal->spelling }
        : std::nullopt;
}

CompiledActualKind CompiledActualView::kind() const noexcept {
    if (systemverilog != nullptr) {
        switch (systemverilog->kind) {
        case semantic::sv::ActualKind::expression:
            return CompiledActualKind::expression;
        case semantic::sv::ActualKind::type:
            return CompiledActualKind::type;
        case semantic::sv::ActualKind::default_value:
            return CompiledActualKind::default_value;
        case semantic::sv::ActualKind::open:
            return CompiledActualKind::open;
        }
    }
    if (vhdl != nullptr) {
        switch (vhdl->kind) {
        case semantic::vhdl::AssociationKind::expression:
            return CompiledActualKind::expression;
        case semantic::vhdl::AssociationKind::type:
            return CompiledActualKind::type;
        case semantic::vhdl::AssociationKind::open:
            return CompiledActualKind::open;
        case semantic::vhdl::AssociationKind::default_box:
            return CompiledActualKind::default_box;
        }
    }
    return CompiledActualKind::expression;
}

std::optional<semantic::ExpressionId>
CompiledActualView::expression() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->expression;
    }
    return vhdl == nullptr
        ? std::optional<semantic::ExpressionId> { }
        : vhdl->expression;
}

const semantic::sv::TypeReference*
CompiledActualView::systemverilog_type() const noexcept {
    return systemverilog == nullptr || !systemverilog->type
        ? nullptr : &*systemverilog->type;
}

const semantic::vhdl::SubtypeIndication*
CompiledActualView::vhdl_subtype() const noexcept {
    return vhdl == nullptr || !vhdl->type ? nullptr : &*vhdl->type;
}

semantic::SourceSpanId CompiledActualView::source() const noexcept {
    if (systemverilog != nullptr) {
        return systemverilog->source;
    }
    return vhdl == nullptr ? semantic::SourceSpanId { } : vhdl->source;
}

std::vector<CompiledActualView> instance_parameters(
    const semantic::CompiledInstanceView instance)
{
    std::vector<CompiledActualView> result;
    if (instance.systemverilog != nullptr) {
        result.reserve(instance.systemverilog->parameters.size());
        for (const auto& actual : instance.systemverilog->parameters) {
            result.push_back({ &actual, nullptr });
        }
    } else if (instance.vhdl != nullptr) {
        result.reserve(instance.vhdl->generic_map.size());
        for (const auto& actual : instance.vhdl->generic_map) {
            result.push_back({ nullptr, &actual });
        }
    }
    return result;
}

std::vector<CompiledActualView> instance_ports(
    const semantic::CompiledInstanceView instance)
{
    std::vector<CompiledActualView> result;
    if (instance.systemverilog != nullptr) {
        result.reserve(instance.systemverilog->ports.size());
        for (const auto& actual : instance.systemverilog->ports) {
            result.push_back({ &actual, nullptr });
        }
    } else if (instance.vhdl != nullptr) {
        result.reserve(instance.vhdl->port_map.size());
        for (const auto& actual : instance.vhdl->port_map) {
            result.push_back({ nullptr, &actual });
        }
    }
    return result;
}

std::optional<semantic::CompiledUnitView> choose_unit(
    const semantic::CompiledDesign& compiled,
    const std::string& requested)
{
    static constexpr std::array primary_kinds {
        semantic::sv::UnitKind::module,
        semantic::sv::UnitKind::program,
    };
    auto candidates = systemverilog_unit_candidates(
        compiled, requested, primary_kinds, false,
        SystemVerilogLibraryMatch::any);
    if (!candidates.empty()) {
        return candidates.front();
    }
    static constexpr std::array configuration_kinds {
        semantic::sv::UnitKind::configuration,
    };
    candidates = systemverilog_unit_candidates(
        compiled, requested, configuration_kinds, true,
        SystemVerilogLibraryMatch::any);
    if (!candidates.empty()) {
        return candidates.front();
    }
    for (const auto& unit : compiled.vhdl_units()) {
        if (unit.kind == semantic::vhdl::UnitKind::configuration
            && vhdl_name_equal(unit.name, requested)) {
            return unit_view(compiled, unit);
        }
    }
    for (const auto& unit : compiled.vhdl_units()) {
        if (unit.kind == semantic::vhdl::UnitKind::architecture
            && vhdl_name_equal(unit.primary_name, requested)) {
            return unit_view(compiled, unit);
        }
    }
    return std::nullopt;
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



std::optional<std::vector<CompiledPortView>> unit_ports(
    const semantic::CompiledDesign& compiled,
    const semantic::CompiledUnitView unit) {
    if (!unit) {
        return std::nullopt;
    }

    std::vector<CompiledPortView> result;
    const auto append_systemverilog = [&result, &compiled, unit](
                                         const semantic::sv::Unit& owner) {
        result.reserve(owner.declarations.size());
        for (const auto id : owner.declarations) {
            const auto declaration = compiled.find_declaration(id);
            if (!declaration || declaration->systemverilog == nullptr
                || declaration->vhdl != nullptr) {
                return false;
            }
            if (declaration->systemverilog->form
                == semantic::sv::DeclarationForm::port) {
                result.push_back(
                    { unit.language, declaration->systemverilog, nullptr });
            }
        }
        return true;
    };
    if (unit.systemverilog != nullptr) {
        return append_systemverilog(*unit.systemverilog)
            ? std::optional<std::vector<CompiledPortView>> {
                  std::move(result) }
            : std::nullopt;
    }

    const auto append_vhdl = [&result, &compiled](
                                 const semantic::vhdl::Unit& owner) {
        result.reserve(owner.declarations.size());
        for (const auto id : owner.declarations) {
            const auto declaration = compiled.find_declaration(id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->systemverilog != nullptr) {
                return false;
            }
            if (declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::port) {
                result.push_back({
                    semantic::Language::vhdl,
                    nullptr,
                    declaration->vhdl,
                });
            }
        }
        return true;
    };
    if (unit.vhdl == nullptr || !append_vhdl(*unit.vhdl)) {
        return std::nullopt;
    }
    if (!result.empty()
        || unit.vhdl->kind != semantic::vhdl::UnitKind::architecture) {
        return result;
    }

    for (const auto& entity : compiled.vhdl_units()) {
        if (entity.kind != semantic::vhdl::UnitKind::entity
            || !vhdl_name_equal(entity.name, unit.vhdl->primary_name)
            || !vhdl_library_equal(entity.library, unit.vhdl->library)) {
            continue;
        }
        result.clear();
        return append_vhdl(entity)
            ? std::optional<std::vector<CompiledPortView>> {
                  std::move(result) }
            : std::nullopt;
    }
    return std::nullopt;
}

std::optional<semantic::CompiledUnitView> choose_bound_unit(
    const semantic::CompiledDesign& compiled,
    const TargetSpec& target)
{
    if (target.language == "sv" || target.language == "verilog") {
        static constexpr std::array verilog_kinds {
            semantic::sv::UnitKind::module,
        };
        static constexpr std::array systemverilog_kinds {
            semantic::sv::UnitKind::module,
            semantic::sv::UnitKind::program,
        };
        auto candidates = systemverilog_unit_candidates(
            compiled, target.unit,
            target.language == "sv"
                ? std::span<const semantic::sv::UnitKind> {
                      systemverilog_kinds }
                : std::span<const semantic::sv::UnitKind> { verilog_kinds },
            false, SystemVerilogLibraryMatch::empty_is_wildcard,
            target.library);
        if (!candidates.empty()) {
            return candidates.front();
        }
        if (target.language == "sv") {
            static constexpr std::array configuration_kinds {
                semantic::sv::UnitKind::configuration,
            };
            candidates = systemverilog_unit_candidates(
                compiled, target.unit, configuration_kinds, true,
                SystemVerilogLibraryMatch::empty_is_wildcard,
                target.library);
            if (!candidates.empty()) {
                return candidates.front();
            }
        }
        return std::nullopt;
    }
    if (target.language == "vhdl") {
        for (const auto& unit : compiled.vhdl_units()) {
            if (unit.kind == semantic::vhdl::UnitKind::architecture
                && vhdl_name_equal(unit.primary_name, target.unit)
                && (target.library.empty() || unit.library.empty()
                    || vhdl_name_equal(unit.library, target.library))
                && (!target.architecture
                    || vhdl_name_equal(
                        unit.name, *target.architecture))) {
                return unit_view(compiled, unit);
            }
        }
    }
    return std::nullopt;
}



std::optional<semantic::CompiledUnitView> choose_top_unit(
    const semantic::CompiledDesign& compiled,
    const std::string_view spelling) {
    if (spelling.find(':') != std::string_view::npos) {
        const auto target = parse_target(spelling);
        if (!target) {
            return std::nullopt;
        }
        if (target->language == "vhdl" && !target->architecture) {
            for (const auto& unit : compiled.vhdl_units()) {
                if (unit.kind == semantic::vhdl::UnitKind::configuration
                    && vhdl_name_equal(unit.name, target->unit)
                    && (target->library.empty()
                        || vhdl_library_equal(
                            unit.library, target->library))) {
                    return unit_view(compiled, unit);
                }
            }
        }
        return choose_bound_unit(compiled, *target);
    }
    return choose_unit(compiled, simple_top_name(spelling));
}


CompiledSystemVerilogUnitMatch find_exact_systemverilog_module(
    const semantic::CompiledDesign& compiled,
    const std::string_view library,
    const std::string_view name)
{
    CompiledSystemVerilogUnitMatch result;
    static constexpr std::array module_kinds {
        semantic::sv::UnitKind::module,
    };
    for (const auto candidate : systemverilog_unit_candidates(
             compiled, name, module_kinds, false,
             SystemVerilogLibraryMatch::exact_logical, library)) {
        if (result.unit) {
            result.ambiguous = true;
            break;
        }
        result.unit = candidate;
    }
    return result;
}



std::vector<UnitResolutionCandidate> resolve_unit_candidates(
    const semantic::CompiledDesign& compiled,
    const std::string_view library,
    const std::string_view name,
    const bool include_vhdl_configurations,
    const bool include_udp_declarations) {
    const auto requested_library = normalized_library(library);
    std::vector<UnitResolutionCandidate> result;
    const auto append_systemverilog = [&](
        const std::span<const semantic::sv::UnitKind> kinds,
        const bool include_external) {
        for (const auto view : systemverilog_unit_candidates(
                 compiled, name, kinds, include_external,
                 SystemVerilogLibraryMatch::exact_logical,
                 requested_library)) {
            UnitResolutionCandidate candidate;
            candidate.identity = unit_identity(view);
            candidate.compiled_unit = view;
            result.push_back(std::move(candidate));
        }
    };
    static constexpr std::array module_kinds {
        semantic::sv::UnitKind::module,
    };
    static constexpr std::array interface_kinds {
        semantic::sv::UnitKind::interface,
    };
    static constexpr std::array program_kinds {
        semantic::sv::UnitKind::program,
    };
    append_systemverilog(module_kinds, false);
    append_systemverilog(interface_kinds, false);
    append_systemverilog(program_kinds, false);
    if (include_vhdl_configurations) {
        static constexpr std::array configuration_kinds {
            semantic::sv::UnitKind::configuration,
        };
        append_systemverilog(configuration_kinds, true);
    }
    for (const auto& unit : compiled.vhdl_units()) {
        if (!vhdl_library_equal(unit.library, requested_library)) {
            continue;
        }
        const bool vhdl_architecture =
            unit.kind == semantic::vhdl::UnitKind::architecture
            && vhdl_name_equal(unit.primary_name, name);
        const bool vhdl_configuration =
            include_vhdl_configurations
            && unit.kind == semantic::vhdl::UnitKind::configuration
            && vhdl_name_equal(unit.name, name);
        if (!vhdl_architecture && !vhdl_configuration) {
            continue;
        }
        const auto view = unit_view(compiled, unit);
        UnitResolutionCandidate candidate;
        candidate.identity = unit_identity(view);
        candidate.compiled_unit = view;
        result.push_back(std::move(candidate));
    }
    if (include_udp_declarations) {
        for (const auto& udp : compiled.systemverilog_hir.udps()) {
            if (normalized_library(udp) != requested_library
                || udp.name != name) {
                continue;
            }
            UnitResolutionCandidate candidate;
            candidate.identity = "udp:"
                + std::string{normalized_library(udp)} + "." + udp.name;
            candidate.compiled_udp = &udp;
            result.push_back(std::move(candidate));
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
    const semantic::CompiledDesign& compiled,
    const std::span<const SystemCFactoryCandidate> systemc_candidates,
    const std::span<const std::string> systemc_libraries,
    const std::string_view library) {
    return std::ranges::any_of(
               compiled.systemverilog_units(),
               [&](const semantic::sv::Unit& unit) {
                   return normalized_library(unit) == library;
               })
        || std::ranges::any_of(
               compiled.vhdl_units(),
               [&](const semantic::vhdl::Unit& unit) {
                   return vhdl_library_equal(unit.library, library);
               })
        || std::ranges::any_of(
               compiled.systemverilog_hir.udps(),
               [&](const semantic::sv::UdpDeclaration& udp) {
                   return normalized_library(udp) == library;
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

std::optional<semantic::CompiledUnitView> choose_same_language_instance(
    const semantic::CompiledDesign& compiled,
    const semantic::CompiledUnitView parent,
    const std::string_view name) {
    if (parent.vhdl != nullptr) {
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
        const auto desired_library = selected_library.value_or(
            std::string_view{parent.vhdl->library});
        for (const auto& unit : compiled.vhdl_units()) {
            if (unit.kind == semantic::vhdl::UnitKind::architecture
                && vhdl_name_equal(unit.primary_name, primary)
                && (desired_library.empty() || unit.library.empty()
                    || vhdl_name_equal(unit.library, desired_library))
                && (!architecture
                    || vhdl_name_equal(unit.name, *architecture))) {
                return unit_view(compiled, unit);
            }
        }
        return std::nullopt;
    }
    if (parent.systemverilog == nullptr) {
        return std::nullopt;
    }
    static constexpr std::array instance_kinds {
        semantic::sv::UnitKind::module,
        semantic::sv::UnitKind::interface,
        semantic::sv::UnitKind::program,
    };
    for (const auto view : systemverilog_unit_candidates(
             compiled, name, instance_kinds, false,
             SystemVerilogLibraryMatch::empty_is_wildcard,
             parent.systemverilog->library)) {
        if (view.language == parent.language) {
            return view;
        }
    }
    return std::nullopt;
}

std::string unit_identity(const semantic::CompiledUnitView unit) {
    if (unit.vhdl != nullptr) {
        const auto library = normalized_library(*unit.vhdl);
        if (unit.vhdl->kind
            == semantic::vhdl::UnitKind::architecture) {
            return "vhdl:" + std::string{library} + "."
                + unit.vhdl->primary_name + "(" + unit.vhdl->name + ")";
        }
        if (unit.vhdl->kind
            == semantic::vhdl::UnitKind::configuration) {
            return "vhdl:" + std::string{library}
                + ".configuration(" + unit.vhdl->name + ")";
        }
        return "vhdl:" + std::string{library} + "."
            + unit.vhdl->name;
    }
    if (unit.systemverilog == nullptr) {
        return {};
    }
    const auto library = normalized_library(*unit.systemverilog);
    if (unit.systemverilog->kind == semantic::sv::UnitKind::interface) {
        return "sv:" + std::string{library} + ".interface("
            + unit.systemverilog->name + ")";
    }
    if (unit.systemverilog->kind == semantic::sv::UnitKind::program) {
        return "sv:" + std::string{library} + ".program("
            + unit.systemverilog->name + ")";
    }
    return "sv:" + std::string{library} + "."
        + unit.systemverilog->name;
}

} // namespace fsim::elaboration::elaboration_detail
