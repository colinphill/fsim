// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_hir_points.hpp"

#include "fsim/support/ctype_whitespace.hpp"

#include <algorithm>
#include <cctype>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageFsmHintError;

    std::string lower_ascii(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return text;
    }

    bool valid_name(const std::string_view name) noexcept
    {
        return !name.empty() && name.find('\0') == std::string_view::npos;
    }

    // A comma-separated list of at least two distinct state names.
    std::optional<std::vector<std::string>> split_legal_states(
        const std::string_view value, const bool vhdl)
    {
        std::vector<std::string> states;
        std::set<std::string, std::less<>> unique;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const auto comma = value.find(',', begin);
            const auto end = comma == std::string_view::npos ? value.size()
                                                             : comma;
            auto state = std::string { support::trim_ctype_whitespace(
                value.substr(begin, end - begin)) };
            if (vhdl) {
                state = lower_ascii(std::move(state));
            }
            if (!valid_name(state) || !unique.emplace(state).second) {
                return std::nullopt;
            }
            states.push_back(std::move(state));
            if (comma == std::string_view::npos) {
                break;
            }
            begin = comma + 1U;
        }
        if (states.size() < 2U) {
            return std::nullopt;
        }
        return states;
    }

} // namespace

CoverageFsmHintResult coverage_hir_fsm_hints(
    const semantic::CompiledDesign& compiled, const semantic::UnitId unit_id,
    const std::span<const project::CoverageFsmHintEntry> manifest,
    const std::string_view instance) noexcept
{
    CoverageFsmHintResult result;
    const auto reject = [&](const Error error, const std::size_t index) {
        result.hints.reset();
        result.error = error;
        result.input_index = index;
        return result;
    };
    try {
        std::vector<CoverageFsmHint> hints;
        const auto unit = compiled.find_unit(unit_id);
        if (unit && unit->systemverilog != nullptr) {
            const auto& pragmas = unit->systemverilog->fsm_pragmas;
            for (std::size_t index = 0U; index < pragmas.size(); ++index) {
                const auto& pragma = pragmas[index];
                if (!pragma.well_formed || !pragma.current_state) {
                    return reject(Error::InvalidAttributeSpecification, index);
                }
                CoverageFsmHint hint;
                hint.current_state = std::string {
                    support::trim_ctype_whitespace(*pragma.current_state) };
                hint.marks_current_state = true;
                if (!valid_name(hint.current_state)) {
                    return reject(Error::InvalidAttributeSpecification, index);
                }
                if (pragma.next_state) {
                    hint.next_state = std::string {
                        support::trim_ctype_whitespace(*pragma.next_state) };
                    if (!valid_name(*hint.next_state)
                        || *hint.next_state == hint.current_state) {
                        return reject(
                            Error::InvalidAttributeSpecification, index);
                    }
                }
                if (pragma.legal_states) {
                    hint.legal_states
                        = split_legal_states(*pragma.legal_states, false);
                    if (!hint.legal_states) {
                        return reject(
                            Error::InvalidAttributeSpecification, index);
                    }
                }
                hints.push_back(std::move(hint));
            }
        } else if (unit && unit->vhdl != nullptr) {
            // `attribute fsm_current_state of s : signal is "true";` and
            // its fsm_next_state and fsm_legal_states companions, in the
            // architecture or its entity.
            std::vector<const semantic::vhdl::Unit*> scopes { unit->vhdl };
            if (const auto entity = compiled.vhdl_primary_unit(*unit->vhdl)) {
                scopes.push_back(*entity);
            }
            std::size_t index = 0U;
            for (const auto* scope : scopes) {
                for (const auto id : scope->declarations) {
                    const auto found = compiled.find_declaration(id);
                    if (!found || found->vhdl == nullptr
                        || found->vhdl->form
                            != semantic::vhdl::DeclarationForm::
                                attribute_specification
                        || !found->vhdl->attribute) {
                        continue;
                    }
                    const auto attribute = lower_ascii(found->vhdl->name);
                    const bool current = attribute == "fsm_current_state";
                    const bool next = attribute == "fsm_next_state";
                    if (!current && !next && attribute != "fsm_legal_states") {
                        continue;
                    }
                    ++index;
                    const auto& profile = *found->vhdl->attribute;
                    const auto value = profile.value
                        ? compiled.find_expression(*profile.value)
                        : std::nullopt;
                    const auto entity_class = lower_ascii(profile.entity_class);
                    if (!value || value->vhdl == nullptr
                        || !value->vhdl->decoded_string
                        || (entity_class != "signal"
                            && entity_class != "variable")) {
                        return reject(
                            Error::InvalidAttributeSpecification, index);
                    }
                    const auto text = lower_ascii(std::string {
                        support::trim_ctype_whitespace(
                            *value->vhdl->decoded_string) });
                    for (const auto& name : profile.entity_names) {
                        CoverageFsmHint hint;
                        hint.current_state
                            = lower_ascii(std::string { name.text() });
                        if (!valid_name(hint.current_state)) {
                            return reject(
                                Error::InvalidAttributeSpecification, index);
                        }
                        if (current) {
                            if (text != "true" && text != "false") {
                                return reject(
                                    Error::InvalidAttributeSpecification,
                                    index);
                            }
                            if (text == "false") {
                                continue;
                            }
                            hint.marks_current_state = true;
                        } else if (next) {
                            if (!valid_name(text)) {
                                return reject(
                                    Error::InvalidAttributeSpecification,
                                    index);
                            }
                            hint.next_state = text;
                        } else {
                            hint.legal_states = split_legal_states(text, true);
                            if (!hint.legal_states) {
                                return reject(
                                    Error::InvalidAttributeSpecification,
                                    index);
                            }
                        }
                        hints.push_back(std::move(hint));
                    }
                }
            }
        }
        const bool vhdl = unit && unit->vhdl != nullptr;
        for (std::size_t index = 0U; index < manifest.size(); ++index) {
            const auto& input = manifest[index];
            if (input.instance.empty() || !valid_name(input.current_state)
                || (input.next_state && !valid_name(*input.next_state))) {
                return reject(Error::InvalidManifestHint, index);
            }
            if (input.instance != instance) {
                continue;
            }
            CoverageFsmHint hint;
            hint.origin = CoverageFsmHintOrigin::Manifest;
            hint.instance = input.instance;
            hint.current_state = vhdl ? lower_ascii(input.current_state)
                                      : input.current_state;
            hint.marks_current_state = true;
            if (input.next_state) {
                hint.next_state = vhdl ? lower_ascii(*input.next_state)
                                       : *input.next_state;
            }
            if (input.legal_states) {
                std::set<std::string, std::less<>> unique;
                auto& states = hint.legal_states.emplace();
                for (const auto& state : *input.legal_states) {
                    auto name = vhdl ? lower_ascii(state) : state;
                    if (!valid_name(name) || !unique.emplace(name).second) {
                        return reject(Error::InvalidManifestHint, index);
                    }
                    states.push_back(std::move(name));
                }
                if (states.size() < 2U) {
                    return reject(Error::InvalidManifestHint, index);
                }
            }
            hints.push_back(std::move(hint));
        }
        result.hints = std::move(hints);
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit, 0U);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit, 0U);
    }
}

} // namespace fsim::elaboration
