// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_fsm_hints.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageFsmHintError;

    enum class AttributeKind : std::uint8_t {
        CurrentState,
        NextState,
        LegalStates,
    };

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    std::string_view trim_ascii(const std::string_view value) noexcept
    {
        std::size_t first = 0U;
        while (first < value.size()
            && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
            ++first;
        }
        auto last = value.size();
        while (last > first
            && std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
            --last;
        }
        return value.substr(first, last - first);
    }

    std::string vhdl_name(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return value;
    }

    std::optional<AttributeKind> attribute_kind(
        const std::string_view name) noexcept
    {
        if (name == "fsm_current_state") {
            return AttributeKind::CurrentState;
        }
        if (name == "fsm_next_state") {
            return AttributeKind::NextState;
        }
        if (name == "fsm_legal_states") {
            return AttributeKind::LegalStates;
        }
        return std::nullopt;
    }

    bool valid_vhdl_standard(const frontend::VhdlStandard standard) noexcept
    {
        switch (standard) {
        case frontend::VhdlStandard::Vhdl1987:
        case frontend::VhdlStandard::Vhdl1993:
        case frontend::VhdlStandard::Vhdl2000:
        case frontend::VhdlStandard::Vhdl2002:
        case frontend::VhdlStandard::Vhdl2008:
            return true;
        }
        return false;
    }

    bool valid_name(
        const std::string_view name, const CoverageFsmHintLimits limits) noexcept
    {
        return !name.empty() && name.size() <= limits.maximum_name_bytes
            && !invalid_text(name);
    }

    std::optional<std::vector<std::string>> split_legal_states(
        const std::string_view value, const bool vhdl,
        const CoverageFsmHintLimits limits, std::size_t& total_states)
    {
        std::vector<std::string> states;
        std::set<std::string, std::less<>> unique;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const auto comma = value.find(',', begin);
            const auto end = comma == std::string_view::npos
                ? value.size()
                : comma;
            auto state = std::string { trim_ascii(value.substr(begin, end - begin)) };
            if (vhdl) {
                state = vhdl_name(std::move(state));
            }
            if (!valid_name(state, limits) || !unique.emplace(state).second
                || total_states >= limits.maximum_legal_states) {
                return std::nullopt;
            }
            ++total_states;
            states.push_back(std::move(state));
            if (comma == std::string_view::npos) {
                break;
            }
            begin = comma + 1U;
        }
        if (states.size() < 2U) {
            return std::nullopt;
        }
        std::ranges::sort(states);
        return states;
    }

} // namespace

CoverageFsmHintResult make_coverage_fsm_hints(
    const frontend::DesignUnit& unit,
    const std::span<const project::CoverageFsmHintEntry> manifest_hints,
    const CoverageFsmHintLimits limits) noexcept
{
    CoverageFsmHintResult result;
    const auto reject = [&](const Error error, const std::size_t index) {
        result.hints.reset();
        result.error = error;
        result.input_index = index;
        return result;
    };

    try {
        if (unit.vhdl_attributes.size() > limits.maximum_attributes
            || manifest_hints.size() > limits.maximum_manifest_hints) {
            return reject(Error::ResourceLimit, 0U);
        }
        if (!unit.vhdl_attributes.empty()
            && (unit.language != frontend::Language::Vhdl2008
                || !valid_vhdl_standard(unit.vhdl_standard))) {
            return reject(Error::InvalidVhdlStandard, 0U);
        }

        std::map<std::string, bool, std::less<>> declarations;
        for (std::size_t index = 0U; index < unit.vhdl_attributes.size(); ++index) {
            const auto& attribute = unit.vhdl_attributes[index];
            const auto kind = attribute_kind(attribute.name);
            if (!kind || attribute.specification) {
                continue;
            }
            if (attribute.type.domain != frontend::ValueDomain::String
                || !declarations.emplace(attribute.name, true).second) {
                return reject(Error::InvalidAttributeDeclaration, index);
            }
        }

        std::vector<CoverageFsmHint> hints;
        std::size_t entity_name_count = 0U;
        std::size_t total_states = 0U;
        for (std::size_t index = 0U; index < unit.vhdl_attributes.size(); ++index) {
            const auto& attribute = unit.vhdl_attributes[index];
            const auto kind = attribute_kind(attribute.name);
            if (!kind || !attribute.specification) {
                continue;
            }
            if (attribute.entity_names.size()
                > limits.maximum_attribute_entity_names
                    - entity_name_count) {
                return reject(Error::ResourceLimit, index);
            }
            if (attribute.value.decoded_string
                && attribute.value.decoded_string->size()
                    > limits.maximum_value_bytes) {
                return reject(Error::ResourceLimit, index);
            }
            if (!declarations.contains(attribute.name)
                || (attribute.entity_class != "signal"
                    && attribute.entity_class != "variable")
                || !attribute.value.decoded_string) {
                return reject(Error::InvalidAttributeSpecification, index);
            }
            entity_name_count += attribute.entity_names.size();
            const auto value = trim_ascii(*attribute.value.decoded_string);
            if (value.empty() || invalid_text(value)) {
                return reject(Error::InvalidAttributeSpecification, index);
            }
            for (const auto& raw_entity : attribute.entity_names) {
                auto entity = vhdl_name(raw_entity);
                if (entity.size() > limits.maximum_name_bytes
                    || hints.size() >= limits.maximum_hints) {
                    return reject(Error::ResourceLimit, index);
                }
                if (!valid_name(entity, limits)) {
                    return reject(Error::InvalidAttributeSpecification, index);
                }
                CoverageFsmHint hint;
                hint.origin = CoverageFsmHintOrigin::VhdlSource;
                hint.current_state = std::move(entity);
                switch (*kind) {
                case AttributeKind::CurrentState: {
                    const auto marker = vhdl_name(std::string { value });
                    if (marker != "true" && marker != "false") {
                        return reject(
                            Error::InvalidAttributeSpecification, index);
                    }
                    if (marker == "false") {
                        continue;
                    }
                    hint.marks_current_state = true;
                    break;
                }
                case AttributeKind::NextState: {
                    auto next = vhdl_name(std::string { value });
                    if (!valid_name(next, limits)) {
                        return reject(
                            Error::InvalidAttributeSpecification, index);
                    }
                    hint.next_state = std::move(next);
                    break;
                }
                case AttributeKind::LegalStates:
                    if (static_cast<std::size_t>(
                            std::ranges::count(value, ','))
                            + 1U
                        > limits.maximum_legal_states - total_states) {
                        return reject(Error::ResourceLimit, index);
                    }
                    hint.legal_states = split_legal_states(
                        value, true, limits, total_states);
                    if (!hint.legal_states) {
                        return reject(
                            Error::InvalidAttributeSpecification, index);
                    }
                    break;
                }
                hints.push_back(std::move(hint));
            }
        }

        for (std::size_t index = 0U; index < manifest_hints.size(); ++index) {
            const auto& input = manifest_hints[index];
            if (hints.size() >= limits.maximum_hints
                || input.instance.size() > limits.maximum_instance_bytes
                || input.current_state.size() > limits.maximum_name_bytes
                || (input.next_state
                    && input.next_state->size()
                        > limits.maximum_name_bytes)) {
                return reject(Error::ResourceLimit, index);
            }
            if (input.instance.empty() || invalid_text(input.instance)
                || !valid_name(input.current_state, limits)
                || (input.next_state
                    && !valid_name(*input.next_state, limits))) {
                return reject(Error::InvalidManifestHint, index);
            }
            CoverageFsmHint hint;
            hint.origin = CoverageFsmHintOrigin::Manifest;
            hint.instance = input.instance;
            hint.current_state = input.current_state;
            hint.marks_current_state = true;
            hint.next_state = input.next_state;
            if (input.legal_states) {
                if (input.legal_states->size() < 2U
                    || input.legal_states->size()
                        > limits.maximum_legal_states - total_states) {
                    return reject(Error::InvalidManifestHint, index);
                }
                std::set<std::string, std::less<>> unique;
                hint.legal_states.emplace();
                hint.legal_states->reserve(input.legal_states->size());
                for (const auto& state : *input.legal_states) {
                    if (!valid_name(state, limits)
                        || !unique.emplace(state).second) {
                        return reject(Error::InvalidManifestHint, index);
                    }
                    hint.legal_states->push_back(state);
                }
                total_states += hint.legal_states->size();
                std::ranges::sort(*hint.legal_states);
            }
            hints.push_back(std::move(hint));
        }

        result.hints = std::move(hints);
        result.error = Error::None;
        result.input_index = 0U;
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit, result.input_index);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit, result.input_index);
    }
}

} // namespace fsim::elaboration
