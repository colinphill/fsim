// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    // Bounds on one inferred state machine and on the statements of its
    // case arms searched for transitions.
    constexpr std::size_t kCoverageFsmMaximumStates = 64U;
    constexpr std::size_t kCoverageFsmMaximumStatements = 4096U;

    std::string lower_ascii(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return text;
    }

} // namespace

void elaboration_detail::merge_coverage_fsm_machine(
    std::vector<CoverageFsmMachine>& machines, CoverageFsmMachine machine)
{
    const auto existing = std::ranges::find_if(machines,
        [&](const CoverageFsmMachine& item) {
            return item.signal == machine.signal;
        });
    if (existing == machines.end()) {
        machines.push_back(std::move(machine));
        return;
    }
    if (existing->values != machine.values
        || existing->declaration != machine.declaration) {
        return;
    }
    std::set<CoverageFsmTransition> merged(
        existing->transitions.begin(), existing->transitions.end());
    merged.insert(machine.transitions.begin(), machine.transitions.end());
    existing->transitions.assign(merged.begin(), merged.end());
}

const std::optional<std::vector<CoverageFsmHint>>&
Lowerer::coverage_fsm_hints()
{
    const auto unit = specialized_hir_unit_->unit();
    auto key = std::pair { unit.value(), hir_instance_path() };
    if (const auto found = coverage_fsm_hints_.find(key);
        found != coverage_fsm_hints_.end()) {
        return found->second;
    }
    auto result = coverage_hir_fsm_hints(specialized_hir_unit_->design(),
        unit, coverage_hir_context_->fsm_hints, key.second);
    if (!result.ok()) {
        const auto found = specialized_hir_unit_->design().find_unit(unit);
        const auto source = !found ? semantic::SourceSpanId { }
            : found->systemverilog != nullptr ? found->systemverilog->source
            : found->vhdl != nullptr          ? found->vhdl->source
                                              : semantic::SourceSpanId { };
        report(std::string { kCoverageFsmHintsDiagnostic },
            result.error == CoverageFsmHintError::InvalidManifestHint
                ? "invalid FSM coverage hint in the project manifest"
                : "invalid FSM coverage hint attribute",
            hir_source_span(source));
    }
    return coverage_fsm_hints_.emplace(std::move(key), std::move(result.hints))
        .first->second;
}

void Lowerer::record_hir_coverage_fsm(const semantic::ExpressionId selector,
    const std::span<const HirCoverageFsmArm> arms)
{
    if (coverage_fsm_drafts_ == nullptr) {
        return;
    }
    const auto& unit = *specialized_hir_unit_;
    // The object a plain name denotes; a select or call is not a state
    // object, a state assignment, or a state choice.
    const auto named = [&](const semantic::ExpressionId id)
        -> std::optional<semantic::DeclarationId> {
        const auto expression = unit.find_expression(id);
        if (!expression) {
            return std::nullopt;
        }
        const bool name = expression->systemverilog != nullptr
            ? expression->systemverilog->kind
                == semantic::sv::ExpressionKind::name
            : expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name;
        return name ? hir_referenced_declaration(id) : std::nullopt;
    };
    const auto object = named(selector);
    const auto declaration = object ? unit.find_declaration(*object)
                                    : std::nullopt;
    if (!declaration) {
        return;
    }
    const bool vhdl = declaration->vhdl != nullptr;
    // Names compare as the language does: VHDL identifiers ignore case.
    const auto normalized = [&](const std::string_view text) {
        auto name = std::string { text };
        return vhdl ? lower_ascii(std::move(name)) : name;
    };
    const auto declared_name = [&](const semantic::DeclarationId id) {
        const auto found = unit.find_declaration(id);
        return !found ? std::string { }
            : found->systemverilog != nullptr
            ? normalized(found->systemverilog->name)
            : found->vhdl != nullptr ? normalized(found->vhdl->name)
                                     : std::string { };
    };
    const auto expression_name = [&](const semantic::ExpressionId id) {
        const auto expression = unit.find_expression(id);
        return !expression ? std::string { }
            : expression->systemverilog != nullptr
            ? normalized(expression->systemverilog->text)
            : expression->vhdl != nullptr ? normalized(expression->vhdl->text)
                                          : std::string { };
    };
    // A VHDL name binds through its declaration; the state object is a
    // whole signal, not a selected part of one.
    std::optional<SignalId> signal;
    if (!vhdl) {
        signal = hir_direct_signal(selector);
    } else if (const auto binding = hir_runtime_binding(
                   *object, hir_process_scope_, true);
        binding && binding->kind == HirRuntimeBindingKind::signal
        && !binding->signal_selection) {
        signal = binding->signal;
    }
    if (!signal || *signal >= design_.signal_info_.size()) {
        return;
    }
    const auto source = vhdl ? declaration->vhdl->source
                             : declaration->systemverilog->source;

    // The hints that name this object: it is a state object, it has a
    // next-state partner, or only some of its values are states.
    const auto& hints = coverage_fsm_hints();
    if (!hints) {
        return;
    }
    const auto object_name = declared_name(*object);
    bool marked { };
    std::optional<std::string> next_state;
    std::optional<std::vector<std::string>> legal_states;
    for (const auto& hint : *hints) {
        if (normalized(hint.current_state) != object_name) {
            continue;
        }
        marked = marked || hint.marks_current_state;
        if (hint.next_state) {
            next_state = normalized(*hint.next_state);
        }
        if (hint.legal_states) {
            legal_states = hint.legal_states;
        }
    }
    const auto reject = [&](const std::string& reason) {
        report(std::string { kCoverageFsmHintsDiagnostic },
            "FSM coverage hint for '" + object_name + "' " + reason,
            hir_source_span(source));
    };

    // States by name and value. An enumeration's states are its literals
    // (a VHDL literal's runtime value is its position); a hinted object of
    // another type has the named constants its case arms choose.
    std::vector<std::string> names;
    std::size_t width = design_.signal_info_[*signal].width;
    std::vector<std::pair<semantic::DeclarationId, std::uint64_t>> literals;
    std::optional<HirEnumerationMethod> profile;
    std::optional<semantic::TypeId> vhdl_enumeration;
    if (!vhdl) {
        profile = hir_systemverilog_enumeration_profile({ }, *object);
        if (profile) {
            names = profile->names;
            width = profile->width;
        }
    } else if (declaration->vhdl->subtype
        && (vhdl_enumeration
            = hir_vhdl_enumeration_root(*declaration->vhdl->subtype))) {
        const auto type = unit.find_type(*vhdl_enumeration);
        if (!type || type->vhdl == nullptr) {
            return;
        }
        std::set<std::string> spellings;
        for (const auto& literal : type->vhdl->enumeration_literals) {
            // A character type (BIT, STD_ULOGIC, CHARACTER) has values,
            // not states.
            if (literal.spelling.starts_with('\'')) {
                return;
            }
            spellings.insert(lower_ascii(literal.spelling));
            names.push_back(lower_ascii(literal.spelling));
            literals.emplace_back(literal.declaration, literal.ordinal);
        }
        if (spellings == std::set<std::string> { "false", "true" }) {
            return;
        }
    }
    const bool enumeration = profile || vhdl_enumeration;
    if (!enumeration && !marked && !next_state && !legal_states) {
        return;
    }
    if (width == 0U || width > 64U) {
        return;
    }
    const auto mask = width == 64U ? ~std::uint64_t { 0 }
                                   : (std::uint64_t { 1 } << width) - 1U;
    // The value of a VHDL string of '0' and '1' elements, or of a
    // constant initialized by one: an element of any other value is in no
    // state, as the case statement compares elements exactly.
    const auto vhdl_bits = [&](semantic::ExpressionId id)
        -> std::optional<std::uint64_t> {
        for (;;) {
            const auto expression = unit.find_expression(id);
            if (!expression || expression->vhdl == nullptr) {
                return std::nullopt;
            }
            if (const auto& text = expression->vhdl->decoded_string) {
                if (text->size() != width) {
                    return std::nullopt;
                }
                std::uint64_t value { };
                for (const auto element : *text) {
                    if (element != '0' && element != '1') {
                        return std::nullopt;
                    }
                    value = (value << 1U) | (element == '1' ? 1U : 0U);
                }
                return value;
            }
            const auto referenced = named(id);
            const auto constant = referenced
                ? unit.find_declaration(*referenced)
                : std::nullopt;
            if (!constant || constant->vhdl == nullptr
                || constant->vhdl->form
                    != semantic::vhdl::DeclarationForm::constant
                || !constant->vhdl->initializer) {
                return std::nullopt;
            }
            id = *constant->vhdl->initializer;
        }
    };
    // The value a state name, choice or assigned value denotes.
    const auto value_of = [&](const semantic::ExpressionId id)
        -> std::optional<std::uint64_t> {
        if (vhdl && !vhdl_enumeration) {
            if (const auto bits = vhdl_bits(id)) {
                return bits;
            }
        }
        const auto referenced = named(id);
        if (!referenced) {
            return std::nullopt;
        }
        if (vhdl_enumeration) {
            const auto found = std::ranges::find(literals, *referenced,
                &std::pair<semantic::DeclarationId, std::uint64_t>::first);
            return found == literals.end() ? std::nullopt
                                           : std::optional { found->second };
        }
        const auto value = hir_constant_integer(id);
        return value ? std::optional { static_cast<std::uint64_t>(*value)
                           & mask }
                     : std::nullopt;
    };
    std::vector<std::uint64_t> values;
    if (profile) {
        values = profile->values;
    } else if (vhdl_enumeration) {
        for (const auto& literal : literals) {
            values.push_back(literal.second);
        }
    } else {
        for (const auto& arm : arms) {
            for (const auto choice : arm.choices) {
                const auto value = value_of(choice);
                if (!value) {
                    continue;
                }
                const auto name = expression_name(choice);
                const auto found = std::ranges::find(names, name);
                if (found == names.end()) {
                    names.push_back(name);
                    values.push_back(*value);
                } else if (values[static_cast<std::size_t>(
                               found - names.begin())]
                    != *value) {
                    return;
                }
            }
        }
    }
    // Only the legal states are states; a value outside them is in none.
    if (legal_states) {
        std::vector<std::string> legal_names;
        std::vector<std::uint64_t> legal_values;
        for (const auto& state : *legal_states) {
            const auto name = normalized(state);
            const auto found = std::ranges::find(names, name);
            if (found == names.end()) {
                reject("names legal state '" + name
                    + "', which is neither a literal of its type nor a "
                      "choice of its case statement");
                return;
            }
            legal_names.push_back(name);
            legal_values.push_back(
                values[static_cast<std::size_t>(found - names.begin())]);
        }
        names = std::move(legal_names);
        values = std::move(legal_values);
    }
    if (std::set<std::uint64_t> { values.begin(), values.end() }.size()
        != values.size()) {
        reject("names states with the same value");
        return;
    }

    CoverageFsmMachine machine;
    machine.signal = *signal;
    machine.declaration = source;
    machine.name = object_name;
    machine.values = values;
    const auto count = machine.values.size();
    if (count < 2U || count > kCoverageFsmMaximumStates) {
        return;
    }

    // The state a choice or an assigned value names.
    const auto state_of = [&](const semantic::ExpressionId id)
        -> std::optional<std::uint32_t> {
        const auto value = value_of(id);
        const auto found = value ? std::ranges::find(machine.values, *value)
                                 : machine.values.end();
        return found == machine.values.end()
            ? std::nullopt
            : std::optional { static_cast<std::uint32_t>(
                  found - machine.values.begin()) };
    };
    // Whether an assignment target is the state object, its hinted
    // next-state object, or another object of its enumeration type.
    const auto partner = [&](const semantic::ExpressionId id) {
        const auto referenced = named(id);
        if (!referenced) {
            return false;
        }
        if (*referenced == *object
            || (next_state && declared_name(*referenced) == *next_state)) {
            return true;
        }
        const auto other = unit.find_declaration(*referenced);
        if (!other) {
            return false;
        }
        if (profile) {
            if (other->systemverilog == nullptr) {
                return false;
            }
            const auto other_profile = hir_systemverilog_enumeration_profile(
                { }, *referenced);
            return other_profile && other_profile->names == profile->names
                && other_profile->values == profile->values;
        }
        return vhdl_enumeration && other->vhdl != nullptr
            && other->vhdl->subtype
            && hir_vhdl_enumeration_root(*other->vhdl->subtype)
            == vhdl_enumeration;
    };

    // Each arm's states; an earlier arm takes a state first, and a default
    // arm takes the states no other arm names.
    std::vector<bool> chosen(count);
    std::vector<std::vector<std::uint32_t>> arm_states(arms.size());
    for (std::size_t index = 0U; index < arms.size(); ++index) {
        if (arms[index].is_default) {
            continue;
        }
        for (const auto choice : arms[index].choices) {
            // Every choice of an unrestricted enumeration names a state;
            // otherwise a choice may name a value that is in no state.
            const auto state = state_of(choice);
            if (!state && enumeration && !legal_states) {
                return;
            }
            if (!state) {
                continue;
            }
            if (!chosen[*state]) {
                chosen[*state] = true;
                arm_states[index].push_back(*state);
            }
        }
    }
    for (std::size_t index = 0U; index < arms.size(); ++index) {
        if (!arms[index].is_default) {
            continue;
        }
        for (std::uint32_t state = 0U; state < count; ++state) {
            if (!chosen[state]) {
                chosen[state] = true;
                arm_states[index].push_back(state);
            }
        }
    }

    std::set<CoverageFsmTransition> transitions;
    std::size_t searched { };
    for (std::size_t index = 0U; index < arms.size(); ++index) {
        std::vector<std::uint32_t> targets;
        std::vector<semantic::StatementId> pending(
            arms[index].statements.rbegin(), arms[index].statements.rend());
        const auto push = [&](const std::span<const semantic::StatementId> ids) {
            pending.insert(pending.end(), ids.rbegin(), ids.rend());
        };
        while (!pending.empty()) {
            if (++searched > kCoverageFsmMaximumStatements) {
                return;
            }
            const auto statement = unit.find_statement(pending.back());
            pending.pop_back();
            if (!statement) {
                return;
            }
            std::optional<semantic::ExpressionId> target;
            std::vector<semantic::ExpressionId> assigned;
            if (statement->systemverilog != nullptr) {
                const auto& item = *statement->systemverilog;
                if (item.kind == semantic::sv::StatementKind::assignment
                    && item.update_kind == semantic::sv::UpdateKind::none
                    && item.value) {
                    target = item.target;
                    assigned.push_back(*item.value);
                }
                push(item.statements);
                push(item.else_statements);
                for (const auto& alternative : item.case_alternatives) {
                    push(alternative.statements);
                }
            } else if (statement->vhdl != nullptr) {
                const auto& item = *statement->vhdl;
                if (item.kind == semantic::vhdl::StatementKind::signal_assignment
                    || item.kind
                        == semantic::vhdl::StatementKind::variable_assignment) {
                    target = item.target;
                    if (item.waveform.empty() && item.value) {
                        assigned.push_back(*item.value);
                    }
                    for (const auto& element : item.waveform) {
                        assigned.push_back(element.value);
                    }
                }
                push(item.statements);
                push(item.else_statements);
                for (const auto& alternative : item.alternatives) {
                    push(alternative.statements);
                }
            }
            if (!target || !partner(*target)) {
                continue;
            }
            // Either outcome of a conditional operator is assigned.
            while (!assigned.empty()) {
                const auto value = assigned.back();
                assigned.pop_back();
                const auto expression = unit.find_expression(value);
                if (expression && expression->systemverilog != nullptr
                    && expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::call
                    && expression->systemverilog->text == "?:"
                    && expression->systemverilog->operands.size() == 3U) {
                    assigned.push_back(expression->systemverilog->operands[2]);
                    assigned.push_back(expression->systemverilog->operands[1]);
                    continue;
                }
                if (const auto state = state_of(value)) {
                    targets.push_back(*state);
                }
            }
        }
        for (const auto from : arm_states[index]) {
            for (const auto to : targets) {
                if (from != to) {
                    transitions.insert({ from, to });
                }
            }
        }
    }
    machine.transitions.assign(transitions.begin(), transitions.end());

    merge_coverage_fsm_machine(
        (*coverage_fsm_drafts_)[process_.id], std::move(machine));
}

} // namespace fsim::elaboration
