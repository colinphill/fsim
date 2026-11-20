// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_vhdl_analysis_stamp.hpp"

#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/semantic/compiled_design_vhdl_resolution.hpp"
#include "fsim/support/design_library.hpp"
#include "fsim/support/payload_checksum.hpp"
#include "fsim/support/vhdl_identifier.hpp"
#include "fsim/support/vhdl_standard_package_form.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Compile-time analysis results that links keep (gate BD, owner 11-19).
//
// A stamped unit's resolution results are authoritative: the resolution pass
// neither resets nor writes them (compiled_design_vhdl_resolution.cpp). A
// result that names a declaration of another library unit does not survive
// the projection of the unit into its own library object (the target's ID
// is not retained), so the stamp records it as an anchor: the target library
// unit as IEEE 1076-2019 13.1 and 13.2 name it (logical library, primary
// unit, secondary unit), the ordinal of the target in that unit's items, and
// a digest of those items. A link rebinds every anchor by position before
// the resolution pass runs; nothing inside the target is found by name.
//
// The compiler-served packages that precompiled standard library artifacts
// carry (standard_package_form artifact; gate BD D2) are analyzed from the
// standard's sources like user units: results that name their declarations
// are anchored, and the artifact units are stamped. A result that names a
// declaration of a package the compiler still projects (standard_package_form
// projection) or of no VHDL library unit gets no anchor: the stamp lists the
// slot in vhdl::Unit::recomputed and every resolution pass computes it
// again, as it did before the stamp existed.
namespace fsim::semantic {
namespace {

using vhdl::AnchoredSlot;
using vhdl::AnchorSpace;

[[nodiscard]] UnitId scope_unit(
    const CompiledDesign& design, const ScopeId scope) noexcept
{
    const auto* record = design.find_scope(scope);
    return record == nullptr ? UnitId { } : record->unit;
}

[[nodiscard]] const vhdl::Unit* vhdl_unit(
    const CompiledDesign& design, const UnitId id) noexcept
{
    if (!id.valid()) {
        return nullptr;
    }
    const auto view = design.find_unit(id);
    return view ? view->vhdl : nullptr;
}

// An architecture or a package body (13.1): named by its primary unit and
// its own name.
[[nodiscard]] bool secondary_unit(const vhdl::Unit& unit) noexcept
{
    return unit.kind == vhdl::UnitKind::architecture
        || (unit.kind == vhdl::UnitKind::package
            && !unit.primary_name.empty());
}

[[nodiscard]] vhdl::DeclarationAnchor unit_identity(const vhdl::Unit& unit)
{
    vhdl::DeclarationAnchor anchor;
    anchor.library = std::string { support::normalized_library(unit.library) };
    anchor.kind = unit.kind;
    if (secondary_unit(unit)) {
        anchor.primary = unit.primary_name;
        anchor.extended_primary = unit.extended_primary_name;
        anchor.secondary = unit.name;
        anchor.extended_secondary = unit.extended_name;
    } else {
        anchor.primary = unit.name;
        anchor.extended_primary = unit.extended_name;
    }
    return anchor;
}

// The lookup key of a library unit's identity: logical library names and
// basic identifiers are case-insensitive, extended identifiers are compared
// as written (15.4, 13.2).
[[nodiscard]] std::string identity_key(const vhdl::DeclarationAnchor& anchor)
{
    const auto identifier = [](const std::string_view name,
                                const bool extended) {
        return extended ? "\\" + std::string { name }
                        : support::ascii_lower(name);
    };
    std::string key;
    key.push_back(static_cast<char>('0' + static_cast<int>(anchor.kind)));
    key.push_back('\0');
    key += support::ascii_lower(anchor.library);
    key.push_back('\0');
    key += identifier(anchor.primary, anchor.extended_primary);
    key.push_back('\0');
    key += identifier(anchor.secondary, anchor.extended_secondary);
    return key;
}

[[nodiscard]] std::string describe(const vhdl::DeclarationAnchor& anchor)
{
    auto text = anchor.library + "." + anchor.primary;
    if (!anchor.secondary.empty()) {
        text += "(" + anchor.secondary + ")";
    }
    return text;
}

[[nodiscard]] std::string describe(const vhdl::Unit& unit)
{
    return describe(unit_identity(unit));
}

void ensure_lookup_indexes(CompiledDesign& design, const UnitId probe)
{
    if (design.vhdl_unit_items(probe) == nullptr) {
        design.refresh_lookup_indexes();
    }
}

[[nodiscard]] const CompiledVhdlUnitItems& unit_items(
    const CompiledDesign& design, const vhdl::Unit& unit)
{
    const auto* items = design.vhdl_unit_items(unit.id);
    if (items == nullptr) {
        throw std::logic_error {
            "VHDL analysis anchors: stale lookup indexes for unit "
            + describe(unit)
        };
    }
    return *items;
}

// The digest of the items an anchor indexes: the number and order of the
// unit's declarations and types, each declaration's form, name and source
// position, and each type's name. A unit reanalyzed with its declarations
// reordered, added or removed has another digest.
[[nodiscard]] std::string items_digest(const CompiledDesign& design,
    const vhdl::Unit& unit, const CompiledVhdlUnitItems& items)
{
    support::Xxh64 state;
    const auto number = [&](const std::uint64_t value) {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index { }; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(
                (value >> (8U * index)) & 0xffU);
        }
        state.update(bytes);
    };
    const auto text = [&](const std::string_view value) {
        number(value.size());
        state.update(value);
    };
    const auto& spans = design.semantics.source_spans();
    const auto& declarations = design.vhdl_hir.declarations();
    const auto& types = design.vhdl_hir.types();
    number(static_cast<std::uint64_t>(unit.kind));
    number(items.declarations.size());
    number(items.types.size());
    for (const auto position : items.declarations) {
        const auto& declaration = declarations.at(position);
        number(static_cast<std::uint64_t>(declaration.form));
        text(declaration.name);
        if (declaration.source.valid()
            && declaration.source.value() < spans.size()) {
            const auto& span = spans[declaration.source.value()];
            number(span.begin.offset);
            number(span.end.offset);
        } else {
            number(~std::uint64_t { });
            number(~std::uint64_t { });
        }
    }
    for (const auto position : items.types) {
        text(types.at(position).name);
    }
    return support::payload_checksum(state);
}

// Where a stored reference points, as the stamp records it.
struct Target {
    enum class Kind : std::uint8_t { same_unit, anchored, recomputed };
    Kind kind { Kind::same_unit };
    const vhdl::Unit* unit { };
    AnchorSpace space { AnchorSpace::declarations };
    std::uint32_t ordinal { };
};

class AnchorTargets final {
public:
    explicit AnchorTargets(const CompiledDesign& design) : design_ { design }
    {
    }

    [[nodiscard]] Target declaration(
        const DeclarationId id, const UnitId owner) const
    {
        const auto view = design_.find_declaration(id);
        if (!view || view->vhdl == nullptr) {
            throw std::logic_error {
                "VHDL analysis stamp: a resolved reference names declaration "
                + std::to_string(id.value()) + ", which the design lacks"
            };
        }
        const auto& declarations = design_.vhdl_hir.declarations();
        const auto position = static_cast<std::uint32_t>(
            view->vhdl - declarations.data());
        return classify(scope_unit(design_, view->vhdl->scope), owner,
            AnchorSpace::declarations, position);
    }

    [[nodiscard]] Target type(const TypeId id, const UnitId owner) const
    {
        const auto view = design_.find_type(id);
        if (!view || view->vhdl == nullptr) {
            throw std::logic_error {
                "VHDL analysis stamp: a resolved reference names type "
                + std::to_string(id.value()) + ", which the design lacks"
            };
        }
        // A type belongs to the unit of its declaration
        // (CompiledVhdlUnitItems); an anonymous type to none.
        const auto declared
            = design_.find_declaration(view->vhdl->declaration);
        const auto unit = declared && declared->vhdl != nullptr
            ? scope_unit(design_, declared->vhdl->scope)
            : UnitId { };
        const auto& types = design_.vhdl_hir.types();
        const auto position
            = static_cast<std::uint32_t>(view->vhdl - types.data());
        return classify(unit, owner, AnchorSpace::types, position);
    }

    [[nodiscard]] vhdl::DeclarationAnchor anchor(const Target& target)
    {
        auto result = unit_identity(*target.unit);
        result.space = target.space;
        result.ordinal = target.ordinal;
        auto [digest, inserted]
            = digests_.try_emplace(target.unit->id.value());
        if (inserted) {
            digest->second = items_digest(
                design_, *target.unit, unit_items(design_, *target.unit));
        }
        result.target_digest = digest->second;
        return result;
    }

private:
    [[nodiscard]] Target classify(const UnitId unit, const UnitId owner,
        const AnchorSpace space, const std::uint32_t position) const
    {
        if (unit.valid() && unit == owner) {
            return { };
        }
        const auto* target = vhdl_unit(design_, unit);
        if (target == nullptr
            || target->standard_package_form
                == support::VhdlStandardPackageForm::projection) {
            return { Target::Kind::recomputed };
        }
        const auto& items = unit_items(design_, *target);
        const auto& positions = space == AnchorSpace::declarations
            ? items.declarations
            : items.types;
        const auto found
            = std::ranges::lower_bound(positions, position);
        if (found == positions.end() || *found != position) {
            throw std::logic_error {
                "VHDL analysis stamp: a resolved target is not an item of "
                "its unit " + describe(*target)
            };
        }
        return { Target::Kind::anchored, target, space,
            static_cast<std::uint32_t>(found - positions.begin()) };
    }

    const CompiledDesign& design_;
    std::unordered_map<std::uint32_t, std::string> digests_;
};

struct UnitStamp {
    std::size_t position { };
    std::vector<vhdl::DeclarationAnchor> anchors;
    std::vector<vhdl::AnchoredReference> anchored;
    std::vector<vhdl::RecomputedSlot> recomputed;
};

[[nodiscard]] UnitStamp stamp_unit(const CompiledDesign& design,
    const std::size_t position, AnchorTargets& targets)
{
    const auto& unit = design.vhdl_hir.units()[position];
    const auto& items = unit_items(design, unit);
    UnitStamp stamp;
    stamp.position = position;
    std::map<std::tuple<std::uint32_t, AnchorSpace, std::uint32_t>,
        std::uint32_t>
        anchors;
    const auto add = [&](const AnchoredSlot slot, const std::uint32_t item,
                         const Target& target) {
        if (target.kind == Target::Kind::same_unit) {
            return;
        }
        if (target.kind == Target::Kind::recomputed) {
            stamp.recomputed.push_back({ slot, item });
            return;
        }
        const auto key = std::tuple { target.unit->id.value(), target.space,
            target.ordinal };
        auto [found, inserted] = anchors.try_emplace(
            key, static_cast<std::uint32_t>(stamp.anchors.size()));
        if (inserted) {
            stamp.anchors.push_back(targets.anchor(target));
        }
        stamp.anchored.push_back({ slot, item, found->second });
    };
    const auto& expressions = design.vhdl_hir.expressions();
    for (std::uint32_t item { }; item < items.expressions.size(); ++item) {
        const auto& expression = expressions.at(items.expressions[item]);
        if (expression.resolved_declaration) {
            add(AnchoredSlot::expression_resolved_declaration, item,
                targets.declaration(*expression.resolved_declaration, unit.id));
        }
        if (expression.resolved_type) {
            add(AnchoredSlot::expression_resolved_type, item,
                targets.type(*expression.resolved_type, unit.id));
        }
        if (expression.qualified_type_mark
            && expression.qualified_type_mark->selected) {
            add(AnchoredSlot::expression_qualified_mark_selected, item,
                targets.declaration(
                    *expression.qualified_type_mark->selected, unit.id));
        }
    }
    const auto& statements = design.vhdl_hir.statements();
    for (std::uint32_t item { }; item < items.statements.size(); ++item) {
        const auto& statement = statements.at(items.statements[item]);
        if (statement.resolved_procedure) {
            add(AnchoredSlot::statement_resolved_procedure, item,
                targets.declaration(*statement.resolved_procedure, unit.id));
        }
    }
    return stamp;
}

// The library units of a design by identity (13.1, 13.2).
class UnitDirectory final {
public:
    explicit UnitDirectory(const CompiledDesign& design)
    {
        for (const auto& unit : design.vhdl_hir.units()) {
            units_[identity_key(unit_identity(unit))].push_back(&unit);
        }
    }

    [[nodiscard]] std::span<const vhdl::Unit* const> find(
        const vhdl::DeclarationAnchor& anchor) const
    {
        const auto found = units_.find(identity_key(anchor));
        if (found == units_.end()) {
            return { };
        }
        return found->second;
    }

private:
    std::unordered_map<std::string, std::vector<const vhdl::Unit*>> units_;
};

struct AnchorWrite {
    AnchoredSlot slot { AnchoredSlot::expression_resolved_declaration };
    std::uint32_t position { };
    std::uint32_t target { };
};

[[nodiscard]] std::string slot_name(const AnchoredSlot slot)
{
    switch (slot) {
    case AnchoredSlot::expression_resolved_declaration:
        return "resolved_declaration";
    case AnchoredSlot::expression_resolved_type:
        return "resolved_type";
    case AnchoredSlot::expression_qualified_mark_selected:
        return "qualified_type_mark.selected";
    case AnchoredSlot::statement_resolved_procedure:
        return "resolved_procedure";
    }
    return "?";
}

// ---- Verifier ---------------------------------------------------------------

template <typename Id>
[[nodiscard]] std::string id_text(const std::optional<Id>& id)
{
    return id ? std::to_string(id->value()) : std::string { "-" };
}

[[nodiscard]] std::string declaration_text(const CompiledDesign& design,
    const std::optional<DeclarationId>& id)
{
    if (!id) {
        return "-";
    }
    auto text = std::to_string(id->value());
    if (const auto view = design.find_declaration(*id);
        view && view->vhdl != nullptr) {
        text += " (" + view->vhdl->name + ")";
    }
    return text;
}

[[nodiscard]] std::string provided_name(const vhdl::ProvidedType type)
{
    const auto name = vhdl_provided_type_name(type);
    return name.empty() ? std::string { "none" } : std::string { name };
}

[[nodiscard]] std::string provided_text(
    const std::optional<vhdl::ProvidedType>& type)
{
    return type ? provided_name(*type) : std::string { "-" };
}

[[nodiscard]] std::string qualified_text(
    const CompiledDesign& design, const std::optional<vhdl::Name>& name)
{
    if (!name) {
        return "-";
    }
    return "'" + std::string { name->text() } + "' "
        + declaration_text(design, name->selected);
}

// The provided-type stamps of every subtype indication the resolution pass
// stamps on a declaration, a type or an instance, in its order.
void collect(const std::optional<vhdl::SubtypeIndication>& subtype,
    std::vector<vhdl::ProvidedType>& into)
{
    if (subtype) {
        into.push_back(subtype->provided_type);
    }
}

[[nodiscard]] std::vector<vhdl::ProvidedType> provided_types(
    const vhdl::Declaration& declaration)
{
    std::vector<vhdl::ProvidedType> result;
    collect(declaration.subtype, result);
    collect(declaration.default_type, result);
    if (declaration.callable) {
        collect(declaration.callable->return_type, result);
    }
    if (declaration.attribute) {
        collect(declaration.attribute->subtype, result);
    }
    if (declaration.package) {
        for (const auto& association : declaration.package->generic_map) {
            collect(association.type, result);
        }
    }
    if (declaration.alias_signature) {
        for (const auto& parameter : declaration.alias_signature->parameters) {
            result.push_back(parameter.provided_type);
        }
        collect(declaration.alias_signature->result, result);
    }
    return result;
}

[[nodiscard]] std::vector<vhdl::ProvidedType> provided_types(
    const vhdl::TypeDefinition& type)
{
    std::vector<vhdl::ProvidedType> result;
    result.push_back(type.base.provided_type);
    collect(type.element_subtype, result);
    collect(type.designated_subtype, result);
    for (const auto& element : type.record_elements) {
        result.push_back(element.subtype.provided_type);
    }
    return result;
}

[[nodiscard]] std::vector<vhdl::ProvidedType> provided_types(
    const vhdl::Instance& instance)
{
    std::vector<vhdl::ProvidedType> result;
    for (const auto& association : instance.generic_map) {
        collect(association.type, result);
    }
    return result;
}

// Whether a stored list of provided types differs from a fresh one where
// the fresh resolution decided a type (none: undecided, not compared).
[[nodiscard]] bool decided_differences(
    const std::vector<vhdl::ProvidedType>& stored,
    const std::vector<vhdl::ProvidedType>& fresh)
{
    if (stored.size() != fresh.size()) {
        return true;
    }
    for (std::size_t index { }; index < fresh.size(); ++index) {
        if (fresh[index] != vhdl::ProvidedType::none
            && stored[index] != fresh[index]) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string provided_list(
    const std::vector<vhdl::ProvidedType>& types)
{
    std::string text { "[" };
    for (const auto type : types) {
        if (text.size() > 1U) {
            text += ", ";
        }
        text += provided_name(type);
    }
    return text + "]";
}

class Differences final {
public:
    void add(const vhdl::Unit& unit, const std::string& item,
        const std::string_view slot, const std::string& stored,
        const std::string& recomputed)
    {
        ++count_;
        if (count_ > limit) {
            return;
        }
        if (!text_.empty()) {
            text_ += "; ";
        }
        text_ += "unit " + describe(unit) + " " + item + " "
            + std::string { slot } + ": stored " + stored + ", recomputed "
            + recomputed;
    }

    [[nodiscard]] std::string finish() &&
    {
        if (count_ > limit) {
            text_ += "; and " + std::to_string(count_ - limit)
                + " more differences";
        }
        return std::move(text_);
    }

private:
    static constexpr std::size_t limit = 40U;
    std::string text_;
    std::size_t count_ { };
};

// The primary units of a design by library and name (13.1), as
// analysis_context_present looks them up.
[[nodiscard]] std::unordered_set<std::string> primary_unit_keys(
    const CompiledDesign& design)
{
    std::unordered_set<std::string> keys;
    for (const auto& unit : design.vhdl_hir.units()) {
        if (!secondary_unit(unit)) {
            keys.insert(
                support::ascii_lower(support::normalized_library(unit.library))
                + '\0'
                + (unit.extended_name ? "\\" + unit.name
                                      : support::ascii_lower(unit.name)));
        }
    }
    return keys;
}

// Whether a design holds every library unit a unit's analysis depends on
// (13.1, 13.5): its primary unit and the units that its own and its primary
// unit's use clauses and context references name by library. A unit
// compiled without them is not analyzed completely, and one object linked
// alone lacks them.
[[nodiscard]] bool analysis_context_present(const CompiledDesign& design,
    const vhdl::Unit& unit,
    const std::unordered_set<std::string>& primary_units)
{
    // As identity_key: extended identifiers as written, marked (15.4).
    const auto identifier = [](const std::string_view spelling) {
        return support::is_extended_vhdl_identifier(spelling)
            ? "\\" + support::decode_vhdl_identifier(spelling)
            : support::ascii_lower(spelling);
    };
    const auto* primary = &unit;
    if (secondary_unit(unit)) {
        const auto found = design.vhdl_primary_unit(unit);
        if (!found) {
            return false;
        }
        primary = *found;
    }
    for (const auto* owner : { &unit, primary }) {
        std::unordered_set<std::string> libraries;
        for (const auto& item : owner->context) {
            if (item.kind == vhdl::ContextKind::library_clause) {
                for (const auto& name : item.selected_names) {
                    libraries.insert(identifier(name.spelling));
                }
                continue;
            }
            for (const auto& name : item.selected_names) {
                const auto parts
                    = support::split_vhdl_selected_name(name.spelling);
                if (parts.size() < 2U) {
                    continue;
                }
                // WORK denotes the unit's own working library (13.2); a
                // prefix that is no library named by a library clause
                // selects inside a visible package instead.
                auto library = identifier(parts[0]);
                if (support::same_vhdl_identifier(parts[0], "work")) {
                    library = support::ascii_lower(
                        support::normalized_library(owner->library));
                } else if (!libraries.contains(library)) {
                    continue;
                }
                if (!primary_units.contains(
                        library + '\0' + identifier(parts[1]))) {
                    return false;
                }
            }
        }
    }
    return true;
}


} // namespace

void stamp_vhdl_analysis(
    CompiledDesign& design, const std::span<const UnitId> source_units)
{
    if (source_units.empty() || design.vhdl_hir.units().empty()) {
        return;
    }
    std::unordered_set<std::uint32_t> sources;
    for (const auto unit : source_units) {
        if (unit.valid()) {
            sources.insert(unit.value());
        }
    }
    std::vector<std::size_t> positions;
    const auto& units = design.vhdl_hir.units();
    // A unit compiled without its primary unit or a unit it uses is not
    // analyzed completely: it stays unstamped, and links resolve it.
    const auto primary_units = primary_unit_keys(design);
    for (std::size_t position { }; position < units.size(); ++position) {
        const auto& unit = units[position];
        if (!unit.analyzed
            && unit.standard_package_form
                != support::VhdlStandardPackageForm::projection
            && unit.id.valid() && sources.contains(unit.id.value())
            && analysis_context_present(design, unit, primary_units)) {
            positions.push_back(position);
        }
    }
    if (positions.empty()) {
        return;
    }
    ensure_lookup_indexes(design, units[positions.front()].id);
    std::vector<UnitStamp> stamps;
    stamps.reserve(positions.size());
    {
        AnchorTargets targets { design };
        for (const auto position : positions) {
            stamps.push_back(stamp_unit(design, position, targets));
        }
    }
    auto& stamped = design.vhdl_hir.mutable_units();
    for (auto& stamp : stamps) {
        auto& unit = stamped[stamp.position];
        unit.analyzed = true;
        unit.anchors = std::move(stamp.anchors);
        unit.anchored = std::move(stamp.anchored);
        unit.recomputed = std::move(stamp.recomputed);
    }
    // The stamp changes no field the lookup indexes read.
    design.refresh_lookup_indexes();
}

void unstamp_vhdl_units_anchored_to(CompiledDesign& design,
    const std::span<const std::string> omitted_packages)
{
    if (omitted_packages.empty()) {
        return;
    }
    std::unordered_set<std::string> omitted;
    for (const auto& package : omitted_packages) {
        omitted.insert(support::ascii_lower(package));
    }
    const auto anchored_to_omitted = [&](const vhdl::Unit& unit) {
        return unit.analyzed
            && std::ranges::any_of(unit.anchors,
                [&](const vhdl::DeclarationAnchor& anchor) {
                    return !anchor.extended_primary
                        && omitted.contains(
                            support::ascii_lower(
                                support::normalized_library(anchor.library))
                            + "." + support::ascii_lower(anchor.primary));
                });
    };
    if (std::ranges::none_of(design.vhdl_hir.units(), anchored_to_omitted)) {
        return;
    }
    for (auto& unit : design.vhdl_hir.mutable_units()) {
        if (!anchored_to_omitted(unit)) {
            continue;
        }
        unit.analyzed = false;
        unit.anchors.clear();
        unit.anchored.clear();
        unit.recomputed.clear();
    }
    design.refresh_lookup_indexes();
}

std::string rebind_vhdl_declaration_anchors(
    CompiledDesign& design, [[maybe_unused]] const AnchorRebind mode)
{
    const auto& units = design.vhdl_hir.units();
    const auto anchored = std::ranges::find_if(units,
        [](const vhdl::Unit& unit) {
            return unit.analyzed && !unit.anchors.empty();
        });
    if (anchored == units.end()) {
        return { };
    }
    ensure_lookup_indexes(design, anchored->id);
    const UnitDirectory directory { design };
    std::unordered_map<std::uint32_t, std::string> digests;
    std::vector<AnchorWrite> writes;
    // A stale or missing target is reachable from the objects a user links:
    // a diagnostic, not an internal error.
    std::string error;
    for (const auto& unit : units) {
        if (!unit.analyzed || unit.anchors.empty()) {
            continue;
        }
        const auto& owner = unit_items(design, unit);
        // By anchor: the target's ID, or none while the target unit is not
        // part of a projection.
        std::vector<std::optional<std::uint32_t>> resolved(unit.anchors.size());
        std::vector<bool> done(unit.anchors.size());
        const auto target_of = [&](const std::uint32_t index)
            -> std::optional<std::uint32_t> {
            if (done[index]) {
                return resolved[index];
            }
            done[index] = true;
            const auto& anchor = unit.anchors[index];
            const auto matches = directory.find(anchor);
            if (matches.empty()) {
                // Designs are linked in parts (one object, then the whole
                // design): a target unit outside this part leaves the slot
                // to the link that holds it; the anchor stays on the unit.
                // A design that lacks the unit altogether is rejected by
                // the dependency checks of 13.5 (FSIM-WS-002) and by the
                // elaborator's own library unit lookup.
                return std::nullopt;
            }
            if (matches.size() != 1U) {
                error = "VHDL unit " + describe(unit)
                    + " was analyzed against library unit " + describe(anchor)
                    + ", of which the linked design holds "
                    + std::to_string(matches.size()) + " units";
                return std::nullopt;
            }
            const auto& target = *matches.front();
            const auto& items = unit_items(design, target);
            auto [digest, inserted] = digests.try_emplace(target.id.value());
            if (inserted) {
                digest->second = items_digest(design, target, items);
            }
            if (digest->second != anchor.target_digest) {
                error = "VHDL unit " + describe(unit)
                    + " is obsolete (IEEE 1076-2019 13.5): library unit "
                    + describe(anchor)
                    + " was analyzed again since; analyze "
                    + describe(unit) + " again";
                return std::nullopt;
            }
            const auto& positions = anchor.space == AnchorSpace::declarations
                ? items.declarations
                : items.types;
            if (anchor.ordinal >= positions.size()) {
                throw std::logic_error {
                    "VHDL analysis anchors: ordinal "
                    + std::to_string(anchor.ordinal) + " is past the items of "
                    + describe(anchor)
                };
            }
            const auto position = positions[anchor.ordinal];
            resolved[index] = anchor.space == AnchorSpace::declarations
                ? design.vhdl_hir.declarations().at(position).id.value()
                : design.vhdl_hir.types().at(position).id.value();
            return resolved[index];
        };
        for (const auto& reference : unit.anchored) {
            if (reference.anchor >= unit.anchors.size()) {
                throw std::logic_error {
                    "VHDL analysis anchors: unit " + describe(unit)
                    + " names anchor " + std::to_string(reference.anchor)
                    + " of " + std::to_string(unit.anchors.size())
                };
            }
            const auto target = target_of(reference.anchor);
            if (!error.empty()) {
                return error;
            }
            if (!target) {
                continue;
            }
            const auto& positions
                = reference.slot == AnchoredSlot::statement_resolved_procedure
                ? owner.statements
                : owner.expressions;
            if (reference.item >= positions.size()) {
                throw std::logic_error {
                    "VHDL analysis anchors: unit " + describe(unit)
                    + " has no item " + std::to_string(reference.item)
                    + " for its " + slot_name(reference.slot)
                };
            }
            writes.push_back(
                { reference.slot, positions[reference.item], *target });
        }
    }
    if (writes.empty()) {
        return { };
    }
    auto& expressions = design.vhdl_hir.mutable_expressions();
    auto& statements = design.vhdl_hir.mutable_statements();
    for (const auto& write : writes) {
        switch (write.slot) {
        case AnchoredSlot::expression_resolved_declaration:
            expressions.at(write.position).resolved_declaration
                = DeclarationId::from_index(write.target);
            break;
        case AnchoredSlot::expression_resolved_type:
            expressions.at(write.position).resolved_type
                = TypeId::from_index(write.target);
            break;
        case AnchoredSlot::expression_qualified_mark_selected: {
            auto& mark = expressions.at(write.position).qualified_type_mark;
            if (!mark) {
                throw std::logic_error {
                    "VHDL analysis anchors: an anchored qualified type mark "
                    "is missing"
                };
            }
            mark->selected = DeclarationId::from_index(write.target);
            break;
        }
        case AnchoredSlot::statement_resolved_procedure:
            statements.at(write.position).resolved_procedure
                = DeclarationId::from_index(write.target);
            break;
        }
    }
    // The rebound slots are resolution results, which no index reads.
    design.refresh_lookup_indexes();
    return { };
}

std::string unbound_vhdl_anchor_error(const CompiledDesign& design)
{
    const auto& units = design.vhdl_hir.units();
    if (std::ranges::none_of(units, [](const vhdl::Unit& unit) {
            return unit.analyzed && !unit.anchors.empty();
        })) {
        return { };
    }
    const UnitDirectory directory { design };
    for (const auto& unit : units) {
        if (!unit.analyzed) {
            continue;
        }
        for (const auto& anchor : unit.anchors) {
            if (directory.find(anchor).empty()) {
                return "VHDL unit " + describe(unit)
                    + " was analyzed against library unit "
                    + describe(anchor)
                    + ", which the elaborated design lacks (IEEE 1076-2019 "
                      "13.5): compile it into the design's libraries, or "
                      "analyze "
                    + describe(unit) + " again";
            }
        }
    }
    return { };
}

std::string verify_stamped_vhdl_analysis(const CompiledDesign& design)
{
    const auto& units = design.vhdl_hir.units();
    // Compared: the stamped units whose analysis context (and anchors'
    // targets) this link holds; one object linked alone lacks them, and a
    // fresh resolution there cannot see what analysis saw.
    const UnitDirectory directory { design };
    const auto primary_units = primary_unit_keys(design);
    std::unordered_map<std::uint32_t, const vhdl::Unit*> stamped;
    for (const auto& unit : units) {
        if (unit.analyzed && unit.id.valid()
            && analysis_context_present(design, unit, primary_units)
            && std::ranges::all_of(unit.anchors,
                [&](const vhdl::DeclarationAnchor& anchor) {
                    return !directory.find(anchor).empty();
                })) {
            stamped.emplace(unit.id.value(), &unit);
        }
    }
    if (stamped.empty()) {
        return { };
    }
    CompiledDesign fresh { design };
    for (auto& unit : fresh.vhdl_hir.mutable_units()) {
        unit.analyzed = false;
    }
    try {
        if (!resolve_vhdl_expression_types(fresh)) {
            return "the fresh resolution could not refresh its lookup "
                   "indexes";
        }
    } catch (const std::exception& error) {
        return std::string { "the fresh resolution failed: " } + error.what();
    }
    const auto owner = [&](const ScopeId scope) -> const vhdl::Unit* {
        const auto unit = scope_unit(design, scope);
        if (!unit.valid()) {
            return nullptr;
        }
        const auto found = stamped.find(unit.value());
        return found == stamped.end() ? nullptr : found->second;
    };
    Differences differences;
    const auto& stored_expressions = design.vhdl_hir.expressions();
    const auto& fresh_expressions = fresh.vhdl_hir.expressions();
    if (stored_expressions.size() != fresh_expressions.size()) {
        return "the fresh resolution changed the number of expressions";
    }
    for (std::size_t index { }; index < stored_expressions.size(); ++index) {
        const auto& stored = stored_expressions[index];
        const auto& recomputed = fresh_expressions[index];
        const auto* unit = owner(stored.scope);
        if (unit == nullptr) {
            continue;
        }
        const auto item = "expression " + std::to_string(stored.id.value())
            + " '" + stored.text + "'";
        // A fresh link resolution sees less than analysis did where its
        // lookup is weaker (a generic package's protected methods, use
        // clauses of other units in the link; next-gate-found 11-19): an
        // undecided fresh result is not compared. A stored result that is
        // missing or differs is.
        if (recomputed.resolved_declaration
            && stored.resolved_declaration != recomputed.resolved_declaration) {
            differences.add(*unit, item, "resolved_declaration",
                declaration_text(design, stored.resolved_declaration),
                declaration_text(fresh, recomputed.resolved_declaration));
        }
        if (recomputed.resolved_type
            && stored.resolved_type != recomputed.resolved_type) {
            differences.add(*unit, item, "resolved_type",
                id_text(stored.resolved_type),
                id_text(recomputed.resolved_type));
        }
        if (!recomputed.resolved_builtin_type.empty()
            && stored.resolved_builtin_type
                != recomputed.resolved_builtin_type) {
            differences.add(*unit, item, "resolved_builtin_type",
                "'" + stored.resolved_builtin_type + "'",
                "'" + recomputed.resolved_builtin_type + "'");
        }
        if (!recomputed.ambiguous_literal
            && stored.ambiguous_literal != recomputed.ambiguous_literal) {
            differences.add(*unit, item, "ambiguous_literal",
                stored.ambiguous_literal ? "true" : "false",
                recomputed.ambiguous_literal ? "true" : "false");
        }
        if (recomputed.qualified_type_mark
            && stored.qualified_type_mark != recomputed.qualified_type_mark) {
            differences.add(*unit, item, "qualified_type_mark",
                qualified_text(design, stored.qualified_type_mark),
                qualified_text(fresh, recomputed.qualified_type_mark));
        }
        if (recomputed.denoted_provided_type
            && stored.denoted_provided_type
                != recomputed.denoted_provided_type) {
            differences.add(*unit, item, "denoted_provided_type",
                provided_text(stored.denoted_provided_type),
                provided_text(recomputed.denoted_provided_type));
        }
    }
    const auto& stored_statements = design.vhdl_hir.statements();
    const auto& fresh_statements = fresh.vhdl_hir.statements();
    for (std::size_t index { };
         index < stored_statements.size() && index < fresh_statements.size();
         ++index) {
        const auto& stored = stored_statements[index];
        const auto* unit = owner(stored.scope);
        if (unit != nullptr && fresh_statements[index].resolved_procedure
            && stored.resolved_procedure
                != fresh_statements[index].resolved_procedure) {
            differences.add(*unit,
                "statement " + std::to_string(stored.id.value()),
                "resolved_procedure",
                declaration_text(design, stored.resolved_procedure),
                declaration_text(
                    fresh, fresh_statements[index].resolved_procedure));
        }
    }
    const auto& stored_declarations = design.vhdl_hir.declarations();
    const auto& fresh_declarations = fresh.vhdl_hir.declarations();
    for (std::size_t index { }; index < stored_declarations.size()
         && index < fresh_declarations.size();
         ++index) {
        const auto& stored = stored_declarations[index];
        const auto* unit = owner(stored.scope);
        if (unit == nullptr) {
            continue;
        }
        const auto before = provided_types(stored);
        const auto after = provided_types(fresh_declarations[index]);
        if (decided_differences(before, after)) {
            differences.add(*unit,
                "declaration " + std::to_string(stored.id.value()) + " '"
                    + stored.name + "'",
                "provided_type", provided_list(before),
                provided_list(after));
        }
    }
    const auto& stored_types = design.vhdl_hir.types();
    const auto& fresh_types = fresh.vhdl_hir.types();
    for (std::size_t index { };
         index < stored_types.size() && index < fresh_types.size(); ++index) {
        const auto& stored = stored_types[index];
        const auto declaration = design.find_declaration(stored.declaration);
        const auto* unit = declaration && declaration->vhdl != nullptr
            ? owner(declaration->vhdl->scope)
            : nullptr;
        if (unit == nullptr) {
            continue;
        }
        const auto before = provided_types(stored);
        const auto after = provided_types(fresh_types[index]);
        if (decided_differences(before, after)) {
            differences.add(*unit,
                "type " + std::to_string(stored.id.value()) + " '"
                    + stored.name + "'",
                "provided_type", provided_list(before),
                provided_list(after));
        }
    }
    const auto& stored_instances = design.vhdl_hir.instances();
    const auto& fresh_instances = fresh.vhdl_hir.instances();
    for (std::size_t index { }; index < stored_instances.size()
         && index < fresh_instances.size();
         ++index) {
        const auto& stored = stored_instances[index];
        const auto* unit = owner(stored.scope);
        if (unit == nullptr) {
            continue;
        }
        const auto before = provided_types(stored);
        const auto after = provided_types(fresh_instances[index]);
        if (decided_differences(before, after)) {
            differences.add(*unit,
                "instance " + std::to_string(stored.id.value()),
                "provided_type", provided_list(before),
                provided_list(after));
        }
    }
    return std::move(differences).finish();
}

} // namespace fsim::semantic
