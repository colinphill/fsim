// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {
namespace {

namespace vh = semantic::vhdl;

[[nodiscard]] std::string canonical_vhdl_mode_view_name(std::string value)
{
    if (!value.empty() && value.front() != '\\' && value.front() != '\'') {
        std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        });
    }
    return value;
}

class ModeViewComposer final {
public:
    explicit ModeViewComposer(vh::Hir& hir)
        : hir_(hir)
        , states_(hir.declarations().size(), 0)
    {
    }

    void compose_all()
    {
        for (std::size_t index = 0;
             index < hir_.declarations().size(); ++index) {
            if (hir_.declarations()[index].mode_view) {
                (void)compose(index);
            }
        }
        bind_interfaces();
    }

private:
    [[nodiscard]] const vh::TypeDefinition* type_definition(
        const semantic::TypeId id) const
    {
        const auto found = std::ranges::find_if(
            hir_.types(),
            [&](const vh::TypeDefinition& type) {
                return type.id == id;
            });
        return found == hir_.types().end() ? nullptr : &*found;
    }

    [[nodiscard]] const vh::TypeDefinition* terminal_type(
        const vh::SubtypeIndication& subtype) const
    {
        auto id = subtype.type_mark.target;
        std::vector<semantic::TypeId> visited;
        while (id.valid()
               && std::ranges::find(visited, id) == visited.end()) {
            visited.push_back(id);
            const auto* definition = type_definition(id);
            if (definition == nullptr) {
                return nullptr;
            }
            if (definition->form != vh::TypeForm::subtype
                && definition->form != vh::TypeForm::alias) {
                return definition;
            }
            id = definition->base.type_mark.target;
        }
        return nullptr;
    }

    [[nodiscard]] bool compatible(
        const vh::SubtypeIndication& left,
        const vh::SubtypeIndication& right) const
    {
        const auto* left_type = terminal_type(left);
        const auto* right_type = terminal_type(right);
        return left_type != nullptr && right_type != nullptr
            && left_type->id == right_type->id;
    }

    [[nodiscard]] bool resolved_subtype(
        const vh::SubtypeIndication& subtype) const
    {
        auto current = &subtype;
        std::vector<semantic::TypeId> visited;
        while (current != nullptr) {
            if (!current->resolution_function.spelling.empty()
                || !current->resolution_function.canonical.empty()) {
                return true;
            }
            const auto id = current->type_mark.target;
            if (!id.valid()
                || std::ranges::find(visited, id) != visited.end()) {
                return false;
            }
            visited.push_back(id);
            const auto* definition = type_definition(id);
            if (definition == nullptr
                || (definition->form != vh::TypeForm::subtype
                    && definition->form != vh::TypeForm::alias)) {
                return false;
            }
            current = &definition->base;
        }
        return false;
    }

    [[nodiscard]] std::optional<std::size_t> declaration_index(
        const semantic::DeclarationId id) const
    {
        const auto found = std::ranges::find_if(
            hir_.declarations(),
            [&](const vh::Declaration& declaration) {
                return declaration.id == id;
            });
        if (found == hir_.declarations().end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(found - hir_.declarations().begin());
    }

    [[nodiscard]] const vh::Unit* unit_for_scope(
        const semantic::ScopeId scope) const
    {
        const auto found = std::ranges::find_if(
            hir_.units(),
            [&](const vh::Unit& unit) { return unit.scope == scope; });
        return found == hir_.units().end() ? nullptr : &*found;
    }

    [[nodiscard]] std::optional<semantic::ScopeId> parent_scope(
        const semantic::ScopeId scope) const
    {
        const auto found = std::ranges::find_if(
            hir_.declarations(),
            [&](const vh::Declaration& declaration) {
                return declaration.nested_scope
                    && *declaration.nested_scope == scope;
            });
        if (found == hir_.declarations().end()) {
            return std::nullopt;
        }
        return found->scope;
    }

    [[nodiscard]] const vh::Unit* owning_unit(
        semantic::ScopeId scope) const
    {
        std::vector<semantic::ScopeId> visited;
        while (std::ranges::find(visited, scope) == visited.end()) {
            visited.push_back(scope);
            if (const auto* unit = unit_for_scope(scope)) {
                return unit;
            }
            const auto parent = parent_scope(scope);
            if (!parent) {
                return nullptr;
            }
            scope = *parent;
        }
        return nullptr;
    }

    [[nodiscard]] const vh::Unit* package_unit(
        std::string_view library,
        std::string_view name) const
    {
        const auto found = std::ranges::find_if(
            hir_.units(),
            [&](const vh::Unit& unit) {
                const auto unit_library = unit.library.empty()
                    ? std::string_view { "work" }
                    : std::string_view { unit.library };
                return unit.kind == vh::UnitKind::package
                    && unit.primary_name.empty()
                    && unit_library == library
                    && canonical_vhdl_mode_view_name(unit.name)
                        == canonical_vhdl_mode_view_name(std::string { name });
            });
        return found == hir_.units().end() ? nullptr : &*found;
    }

    [[nodiscard]] const vh::Declaration* view_in_scope(
        const semantic::ScopeId scope,
        const std::string_view name) const
    {
        const auto canonical = canonical_vhdl_mode_view_name(
            std::string { name });
        const auto found = std::ranges::find_if(
            hir_.declarations(),
            [&](const vh::Declaration& declaration) {
                return declaration.scope == scope
                    && declaration.mode_view
                    && canonical_vhdl_mode_view_name(declaration.name)
                        == canonical;
            });
        return found == hir_.declarations().end() ? nullptr : &*found;
    }

    [[nodiscard]] const vh::TypeDefinition* type_in_scope(
        const semantic::ScopeId scope,
        const std::string_view name) const
    {
        const auto canonical = canonical_vhdl_mode_view_name(
            std::string { name });
        const auto found = std::ranges::find_if(
            hir_.types(),
            [&](const vh::TypeDefinition& type) {
                return type.name == canonical && [&] {
                    const auto declaration = declaration_index(
                        type.declaration);
                    return declaration
                        && hir_.declarations()[*declaration].scope == scope;
                }();
            });
        return found == hir_.types().end() ? nullptr : &*found;
    }

    [[nodiscard]] const vh::TypeDefinition* visible_type(
        const vh::Declaration& declaration,
        const std::string_view spelling) const
    {
        const auto* owner = owning_unit(declaration.scope);
        if (owner == nullptr) {
            return nullptr;
        }
        const auto canonical = canonical_vhdl_mode_view_name(
            std::string { spelling });
        auto lexical_scope = declaration.scope;
        std::vector<semantic::ScopeId> visited;
        while (std::ranges::find(visited, lexical_scope) == visited.end()) {
            visited.push_back(lexical_scope);
            if (const auto* local = type_in_scope(
                    lexical_scope, canonical)) {
                return local;
            }
            const auto parent = parent_scope(lexical_scope);
            if (!parent) {
                break;
            }
            lexical_scope = *parent;
        }
        const vh::TypeDefinition* selected = nullptr;
        for (const auto& item : owner->context) {
            if (item.kind != vh::ContextKind::use_clause) {
                continue;
            }
            for (const auto& imported : item.selected_names) {
                const auto& imported_name = imported.canonical;
                const auto first_dot = imported_name.find('.');
                const auto second_dot = first_dot == std::string::npos
                    ? std::string::npos
                    : imported_name.find('.', first_dot + 1);
                if (first_dot == std::string::npos
                    || second_dot == std::string::npos) {
                    continue;
                }
                const auto member = imported_name.substr(second_dot + 1);
                if (member != "all" && member != canonical) {
                    continue;
                }
                auto library = imported_name.substr(0, first_dot);
                if (library == "work") {
                    library = owner->library.empty()
                        ? std::string { "work" }
                        : owner->library;
                }
                const auto package = imported_name.substr(
                    first_dot + 1, second_dot - first_dot - 1);
                const auto* package_owner = package_unit(library, package);
                const auto* candidate = package_owner == nullptr
                    ? nullptr
                    : type_in_scope(package_owner->scope, canonical);
                if (candidate == nullptr) {
                    continue;
                }
                if (selected != nullptr && selected->id != candidate->id) {
                    return nullptr;
                }
                selected = candidate;
            }
        }
        return selected;
    }

    [[nodiscard]] const vh::Declaration* visible_view(
        const vh::Declaration& declaration,
        const vh::Name& reference) const
    {
        if (reference.selected) {
            const auto index = declaration_index(*reference.selected);
            if (index && hir_.declarations()[*index].mode_view) {
                return &hir_.declarations()[*index];
            }
        }
        const auto* owner = owning_unit(declaration.scope);
        if (owner == nullptr) {
            return nullptr;
        }
        std::vector<std::string> parts;
        std::size_t begin = 0;
        while (begin <= reference.canonical.size()) {
            const auto dot = reference.canonical.find('.', begin);
            parts.push_back(reference.canonical.substr(
                begin,
                dot == std::string::npos
                    ? std::string::npos
                    : dot - begin));
            if (dot == std::string::npos) {
                break;
            }
            begin = dot + 1;
        }
        const auto owner_library = owner->library.empty()
            ? std::string { "work" }
            : owner->library;
        const auto selected_view = [&](std::string library,
                                       const std::string_view package,
                                       const std::string_view name) {
            if (library == "work") {
                library = owner_library;
            }
            const auto* unit = package_unit(library, package);
            return unit == nullptr ? nullptr : view_in_scope(unit->scope, name);
        };
        if (parts.size() == 2) {
            return selected_view(owner_library, parts[0], parts[1]);
        }
        if (parts.size() == 3) {
            return selected_view(parts[0], parts[1], parts[2]);
        }
        if (parts.size() != 1) {
            return nullptr;
        }
        auto lexical_scope = declaration.scope;
        std::vector<semantic::ScopeId> visited;
        while (std::ranges::find(visited, lexical_scope) == visited.end()) {
            visited.push_back(lexical_scope);
            if (const auto* local = view_in_scope(
                    lexical_scope, parts.front())) {
                return local;
            }
            const auto parent = parent_scope(lexical_scope);
            if (!parent) {
                break;
            }
            lexical_scope = *parent;
        }
        const vh::Declaration* selected = nullptr;
        for (const auto& item : owner->context) {
            if (item.kind != vh::ContextKind::use_clause) {
                continue;
            }
            for (const auto& imported : item.selected_names) {
                std::vector<std::string> import_parts;
                std::size_t import_begin = 0;
                while (import_begin <= imported.canonical.size()) {
                    const auto dot = imported.canonical.find('.', import_begin);
                    import_parts.push_back(imported.canonical.substr(
                        import_begin,
                        dot == std::string::npos
                            ? std::string::npos
                            : dot - import_begin));
                    if (dot == std::string::npos) {
                        break;
                    }
                    import_begin = dot + 1;
                }
                if (import_parts.size() != 3
                    || (import_parts[2] != "all"
                        && import_parts[2] != parts.front())) {
                    continue;
                }
                const auto* candidate = selected_view(
                    import_parts[0], import_parts[1], parts.front());
                if (candidate == nullptr) {
                    continue;
                }
                if (selected != nullptr && selected->id != candidate->id) {
                    return nullptr;
                }
                selected = candidate;
            }
        }
        return selected;
    }

    void bind_interfaces()
    {
        for (auto& interface : hir_.mutable_declarations()) {
            if (!interface.interface_view) {
                continue;
            }
            auto& profile = *interface.interface_view;
            const auto* view = visible_view(interface, profile.view);
            if (view == nullptr || !view->mode_view) {
                profile.composition = vh::ModeViewCompositionState::invalid;
                continue;
            }
            profile.view.selected = view->id;
            profile.view.overloads = { view->id };
            profile.composition = view->mode_view->composition;
            if (profile.composition
                != vh::ModeViewCompositionState::complete) {
                continue;
            }
            profile.elements = view->mode_view->elements;
            if (!profile.explicit_subtype
                && profile.form == vh::ModeViewElementForm::record_view) {
                interface.subtype = view->mode_view->record_subtype;
            }
            if (!interface.subtype) {
                profile.composition = vh::ModeViewCompositionState::invalid;
                profile.elements.clear();
                continue;
            }
            bool compatible_subtype = false;
            const auto* interface_type = terminal_type(*interface.subtype);
            if (interface_type == nullptr) {
                interface_type = visible_type(
                    interface,
                    interface.subtype->type_mark.spelling);
            }
            if (profile.form == vh::ModeViewElementForm::record_view) {
                const auto* view_type = terminal_type(
                    view->mode_view->record_subtype);
                compatible_subtype = interface_type != nullptr
                    && view_type != nullptr
                    && interface_type->id == view_type->id;
            } else if (interface_type != nullptr
                       && interface_type->form == vh::TypeForm::array
                       && interface_type->element_subtype) {
                compatible_subtype = compatible(
                    *interface_type->element_subtype,
                    view->mode_view->record_subtype);
            }
            if (!compatible_subtype) {
                profile.composition = vh::ModeViewCompositionState::invalid;
                profile.elements.clear();
            }
        }
    }

    [[nodiscard]] bool structurally_applicable(
        const vh::ModeViewElement& element,
        const vh::SubtypeIndication& subtype) const
    {
        const auto* type = terminal_type(subtype);
        if (type == nullptr) {
            return false;
        }
        if (!element.referenced_view
            || !element.referenced_view->selected) {
            return false;
        }
        const auto referenced = declaration_index(
            *element.referenced_view->selected);
        if (!referenced
            || !hir_.declarations()[*referenced].mode_view) {
            return false;
        }
        const auto& view_subtype = hir_.declarations()[*referenced]
            .mode_view->record_subtype;
        if (element.form == vh::ModeViewElementForm::record_view) {
            return type->form == vh::TypeForm::record
                && compatible(subtype, view_subtype);
        }
        return element.form == vh::ModeViewElementForm::array_view
            && type->form == vh::TypeForm::array
            && type->element_subtype
            && compatible(*type->element_subtype, view_subtype);
    }

    [[nodiscard]] static vh::Direction converse_direction(
        const vh::Direction direction) noexcept
    {
        switch (direction) {
        case vh::Direction::input:
            return vh::Direction::output;
        case vh::Direction::output:
            return vh::Direction::input;
        case vh::Direction::inout:
            return vh::Direction::inout;
        case vh::Direction::buffer:
            return vh::Direction::input;
        default:
            return vh::Direction::unknown;
        }
    }

    static void apply_converse(std::vector<vh::ModeViewElement>& elements)
    {
        for (auto& element : elements) {
            if (element.form == vh::ModeViewElementForm::direction) {
                element.direction = converse_direction(element.direction);
            }
            apply_converse(element.elements);
        }
    }

    [[nodiscard]] vh::ModeViewCompositionState compose(
        const std::size_t index)
    {
        auto& declarations = hir_.mutable_declarations();
        auto& declaration = declarations[index];
        if (!declaration.mode_view) {
            return vh::ModeViewCompositionState::invalid;
        }
        auto& profile = *declaration.mode_view;
        if (states_[index] == 2) {
            return profile.composition;
        }
        if (states_[index] == 1) {
            profile.composition = vh::ModeViewCompositionState::recursive;
            return profile.composition;
        }

        states_[index] = 1;
        if (profile.converse_of) {
            const auto* referenced = visible_view(
                declaration, *profile.converse_of);
            const auto referenced_index = referenced == nullptr
                ? std::optional<std::size_t>{}
                : declaration_index(referenced->id);
            if (!referenced_index || *referenced_index == index
                || !declarations[*referenced_index].mode_view) {
                states_[index] = 2;
                profile.composition =
                    vh::ModeViewCompositionState::invalid;
                return profile.composition;
            }
            const auto nested = compose(*referenced_index);
            if (nested != vh::ModeViewCompositionState::complete) {
                states_[index] = 2;
                profile.composition = nested;
                return profile.composition;
            }
            const auto& source =
                *declarations[*referenced_index].mode_view;
            profile.record_subtype = source.record_subtype;
            profile.elements = source.elements;
            apply_converse(profile.elements);
            states_[index] = 2;
            profile.composition = vh::ModeViewCompositionState::complete;
            return profile.composition;
        }
        auto result = vh::ModeViewCompositionState::complete;
        const auto* record = terminal_type(profile.record_subtype);
        if (record == nullptr || record->form != vh::TypeForm::record
            || resolved_subtype(profile.record_subtype)
            || record->record_elements.size() != profile.elements.size()) {
            result = vh::ModeViewCompositionState::invalid;
        } else {
            for (auto& element : profile.elements) {
                const auto member = std::ranges::find_if(
                    record->record_elements,
                    [&](const vh::RecordElement& candidate) {
                        return canonical_vhdl_mode_view_name(candidate.name)
                            == element.element.canonical;
                    });
                if (member == record->record_elements.end()) {
                    result = vh::ModeViewCompositionState::invalid;
                    continue;
                }
                element.subtype = member->subtype;
                element.elements.clear();
                if (element.form == vh::ModeViewElementForm::direction) {
                    continue;
                }
                if (!structurally_applicable(element, member->subtype)
                    || !element.referenced_view
                    || !element.referenced_view->selected) {
                    result = vh::ModeViewCompositionState::invalid;
                    continue;
                }
                const auto referenced = declaration_index(
                    *element.referenced_view->selected);
                if (!referenced || !declarations[*referenced].mode_view) {
                    result = vh::ModeViewCompositionState::invalid;
                    continue;
                }
                const auto nested = compose(*referenced);
                if (nested == vh::ModeViewCompositionState::complete) {
                    element.elements =
                        declarations[*referenced].mode_view->elements;
                } else if (nested
                           == vh::ModeViewCompositionState::recursive) {
                    result = vh::ModeViewCompositionState::recursive;
                } else if (result
                           != vh::ModeViewCompositionState::recursive) {
                    result = vh::ModeViewCompositionState::invalid;
                }
            }
        }
        states_[index] = 2;
        profile.composition = result;
        return result;
    }

    vh::Hir& hir_;
    std::vector<unsigned char> states_;
};

} // namespace

void compose_vhdl_mode_views(vh::Hir& hir)
{
    ModeViewComposer composer { hir };
    composer.compose_all();
}

bool validate_vhdl_mode_view_hir(
    const vh::Hir& hir,
    diagnostic::Engine& diagnostics)
{
    bool valid = true;
    for (const auto& declaration : hir.declarations()) {
        if (declaration.mode_view
            && declaration.mode_view->composition
                != vh::ModeViewCompositionState::complete) {
            diagnostics.error(
                "FSIM-VHDL-SEM-110",
                "mode view '" + declaration.name
                    + "' must cover one unresolved record type with "
                      "compatible nested record or array element views");
            valid = false;
        }
        if (declaration.interface_view
            && declaration.interface_view->composition
                != vh::ModeViewCompositionState::complete) {
            diagnostics.error(
                "FSIM-VHDL-SEM-111",
                "view-based interface '" + declaration.name
                    + "' requires a record subtype compatible with its "
                      "view, or an array subtype with a compatible element");
            valid = false;
        }
    }
    return valid;
}

} // namespace fsim::app::application_detail
