// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_fsm_inference.hpp"

#include "fsim/frontend/source.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::elaboration {
namespace {

    using Error = CoverageFsmInferenceError;
    using IssueKind = CoverageFsmDescriptionIssueKind;
    using IssueOrigin = CoverageFsmDescriptionOrigin;
    using IssueSubject = CoverageFsmDescriptionSubject;

    struct Candidate {
        const frontend::Type* type { };
        const frontend::SourceSpan* span { };
        bool duplicate { };
        std::optional<std::vector<std::string>> case_states;
        bool case_ambiguous { };
        bool case_incomplete { };
        std::set<std::string, std::less<>> next_state_candidates;
        std::set<std::string, std::less<>> pragma_next_state_candidates;
        std::optional<std::vector<std::string>> pragma_legal_states;
        bool pragma_current_state { };
        bool pragma_ambiguous { };
        std::set<std::string, std::less<>> vhdl_hint_next_state_candidates;
        std::set<std::string, std::less<>> manifest_hint_next_state_candidates;
        std::optional<std::vector<std::string>> vhdl_hint_legal_states;
        std::optional<std::vector<std::string>> manifest_hint_legal_states;
        bool vhdl_source_hint_current_state { };
        bool manifest_hint_current_state { };
        bool vhdl_hint_ambiguous { };
        bool manifest_hint_ambiguous { };
        bool referenced_as_next_state { };
    };

    struct ExplicitDescription {
        std::set<std::string, std::less<>> next_state_candidates;
        std::optional<std::vector<std::string>> legal_states;
        bool pragma_evidence { };
        bool vhdl_source_hint_evidence { };
        bool manifest_hint_evidence { };
        bool ambiguous { };

        [[nodiscard]] bool current_state_evidence() const noexcept
        {
            return pragma_evidence || vhdl_source_hint_evidence
                || manifest_hint_evidence;
        }
    };

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>(
                (bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    void update_string(support::Sha256& hash, const std::string_view value) noexcept
    {
        update_u64(hash, value.size());
        hash.update(value);
    }

    std::uint64_t digest_word(
        const support::Sha256::Digest& digest, const std::size_t first) noexcept
    {
        std::uint64_t value { };
        for (std::size_t index = first; index < first + 8U; ++index) {
            value = (value << 8U) | digest[index];
        }
        return value;
    }

    runtime::CodeCoveragePointId object_identity(
        const runtime::CodeCoveragePointId source_point,
        const CoverageInstanceIdentity instance,
        const std::string_view path, const std::string_view role) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageFsmInferenceSchema);
        update_string(hash, role);
        update_u64(hash, source_point.high);
        update_u64(hash, source_point.low);
        update_u64(hash, instance.high);
        update_u64(hash, instance.low);
        update_string(hash, path);
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    runtime::CodeCoveragePointId legal_state_set_identity(
        const runtime::CodeCoveragePointId current,
        const std::span<const runtime::CodeCoveragePointId> states) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageFsmInferenceSchema);
        update_string(hash, "legal-state-set");
        update_u64(hash, current.high);
        update_u64(hash, current.low);
        update_u64(hash, states.size());
        for (const auto& state : states) {
            update_u64(hash, state.high);
            update_u64(hash, state.low);
        }
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    runtime::CodeCoveragePointId state_identity(
        const runtime::CodeCoveragePointId object,
        const std::string_view name, const std::size_t ordinal) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageFsmInferenceSchema);
        update_string(hash, "state");
        update_u64(hash, object.high);
        update_u64(hash, object.low);
        update_u64(hash, ordinal);
        update_string(hash, name);
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    bool invalid_text(const std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
    }

    bool valid_standard(const frontend::VhdlStandard standard) noexcept
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

    bool valid_systemverilog_pragma_standard(
        const frontend::StandardRevision standard) noexcept
    {
        switch (standard) {
        case frontend::StandardRevision::SystemVerilog2005:
        case frontend::StandardRevision::SystemVerilog2009:
        case frontend::StandardRevision::SystemVerilog2012:
        case frontend::StandardRevision::SystemVerilog2017:
            return true;
        case frontend::StandardRevision::Vhdl1987:
        case frontend::StandardRevision::Vhdl1993:
        case frontend::StandardRevision::Vhdl2000:
        case frontend::StandardRevision::Vhdl2002:
        case frontend::StandardRevision::Vhdl2008:
        case frontend::StandardRevision::Verilog1995:
        case frontend::StandardRevision::Verilog2001:
        case frontend::StandardRevision::Verilog2001NoConfig:
        case frontend::StandardRevision::Verilog2005:
            return false;
        }
        return false;
    }

    std::optional<frontend::CodeCoverageLanguage> coverage_language(
        const frontend::Language language) noexcept
    {
        switch (language) {
        case frontend::Language::Vhdl2008:
            return frontend::CodeCoverageLanguage::Vhdl;
        case frontend::Language::Verilog2005:
            return frontend::CodeCoverageLanguage::Verilog;
        case frontend::Language::SystemVerilog2017:
            return frontend::CodeCoverageLanguage::SystemVerilog;
        }
        return std::nullopt;
    }

    bool valid_unit_kind(const frontend::DesignUnit& unit) noexcept
    {
        if (unit.language == frontend::Language::Vhdl2008) {
            return unit.kind == frontend::UnitKind::VhdlArchitecture;
        }
        return unit.kind == frontend::UnitKind::VerilogModule
            || unit.kind == frontend::UnitKind::SystemVerilogInterface
            || unit.kind == frontend::UnitKind::SystemVerilogProgram;
    }

    std::string unit_identity(const frontend::DesignUnit& unit)
    {
        const auto library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
            return "vhdl:" + std::string { library } + "." + unit.primary_name
                + "(" + unit.name + ")";
        }
        if (unit.kind == frontend::UnitKind::SystemVerilogInterface) {
            return "sv:" + std::string { library } + ".interface(" + unit.name
                + ")";
        }
        if (unit.kind == frontend::UnitKind::SystemVerilogProgram) {
            return "sv:" + std::string { library } + ".program(" + unit.name
                + ")";
        }
        return "sv:" + std::string { library } + "." + unit.name;
    }

    std::string normalized_name(
        std::string value, const frontend::Language language)
    {
        if (language == frontend::Language::Vhdl2008) {
            std::ranges::transform(value, value.begin(), [](const char character) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            });
        }
        return value;
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

    std::optional<std::string> pragma_string(
        const frontend::SystemVerilogFsmPragmaSpecification& specification,
        const std::size_t maximum_bytes)
    {
        if (!specification.value
            || specification.value->kind
                != frontend::ExpressionKind::StringLiteral
            || !specification.value->decoded_string) {
            return std::nullopt;
        }
        const auto trimmed = trim_ascii(*specification.value->decoded_string);
        if (trimmed.empty() || trimmed.size() > maximum_bytes
            || invalid_text(trimmed)) {
            return std::nullopt;
        }
        return std::string { trimmed };
    }

    std::optional<std::vector<std::string>> pragma_legal_states(
        const std::string_view value, const std::size_t maximum_name_bytes)
    {
        std::vector<std::string> states;
        std::set<std::string> unique;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const auto comma = value.find(',', begin);
            const auto end = comma == std::string_view::npos
                ? value.size()
                : comma;
            const auto state = trim_ascii(value.substr(begin, end - begin));
            if (state.empty() || state.size() > maximum_name_bytes
                || invalid_text(state) || !unique.emplace(state).second) {
                return std::nullopt;
            }
            states.emplace_back(state);
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

    std::string_view unqualified_name(const std::string_view name) noexcept
    {
        const auto package = name.rfind("::");
        const auto selected = name.rfind('.');
        const auto offset = std::max(
            package == std::string_view::npos ? std::size_t { 0U } : package + 2U,
            selected == std::string_view::npos ? std::size_t { 0U } : selected + 1U);
        return name.substr(offset);
    }

    std::optional<std::string> choice_name(
        const frontend::Expression& expression,
        const frontend::Language language)
    {
        switch (expression.kind) {
        case frontend::ExpressionKind::Identifier:
        case frontend::ExpressionKind::IntegerLiteral:
        case frontend::ExpressionKind::BooleanLiteral:
        case frontend::ExpressionKind::LogicLiteral:
            if (expression.text.empty() || invalid_text(expression.text)) {
                return std::nullopt;
            }
            return normalized_name(expression.text, language);
        case frontend::ExpressionKind::Unary:
            if (expression.operands.size() == 1U
                && (expression.text == "+" || expression.text == "-")) {
                const auto operand = choice_name(expression.operands.front(), language);
                if (operand) {
                    return expression.text + *operand;
                }
            }
            return std::nullopt;
        case frontend::ExpressionKind::Invalid:
        case frontend::ExpressionKind::StringLiteral:
        case frontend::ExpressionKind::Update:
        case frontend::ExpressionKind::Binary:
        case frontend::ExpressionKind::Call:
        case frontend::ExpressionKind::Index:
        case frontend::ExpressionKind::Slice:
        case frontend::ExpressionKind::Aggregate:
        case frontend::ExpressionKind::Concatenation:
        case frontend::ExpressionKind::Replication:
        case frontend::ExpressionKind::DefaultChoice:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::vector<std::string> enum_states(const frontend::Type& type,
        const frontend::DesignUnit& unit)
    {
        if (!type.enumeration_literals.empty()) {
            auto states = type.enumeration_literals;
            for (auto& state : states) {
                state = normalized_name(std::move(state), unit.language);
            }
            return states;
        }
        if (!type.named_type.empty()) {
            const auto wanted = normalized_name(
                std::string { unqualified_name(type.named_type) }, unit.language);
            const auto alias = std::ranges::find_if(unit.type_aliases,
                [&](const auto& candidate) {
                    return normalized_name(candidate.name, unit.language) == wanted
                        && !candidate.enum_literals.empty();
                });
            if (alias != unit.type_aliases.end()) {
                std::vector<std::string> states;
                states.reserve(alias->enum_literals.size());
                for (const auto& literal : alias->enum_literals) {
                    states.push_back(
                        normalized_name(literal.name, unit.language));
                }
                return states;
            }
        }
        if (type.systemverilog_enumeration_values.size() >= 2U) {
            std::vector<std::string> states;
            states.reserve(type.systemverilog_enumeration_values.size());
            for (std::size_t index = 0U;
                index < type.systemverilog_enumeration_values.size(); ++index) {
                const auto value = choice_name(
                    type.systemverilog_enumeration_values[index], unit.language);
                if (!value) {
                    return { };
                }
                states.push_back("enum#" + std::to_string(index) + "=" + *value);
            }
            return states;
        }
        return { };
    }

    bool state_type(const frontend::Type& type) noexcept
    {
        return type.domain == frontend::ValueDomain::Bit2
            || type.domain == frontend::ValueDomain::Logic4
            || type.domain == frontend::ValueDomain::Logic9
            || type.domain == frontend::ValueDomain::Boolean
            || type.domain == frontend::ValueDomain::Integer;
    }

    bool compatible_state_types(const frontend::Type& current,
        const frontend::Type& next, const frontend::DesignUnit& unit)
    {
        const auto current_states = enum_states(current, unit);
        const auto next_states = enum_states(next, unit);
        if (!current_states.empty() || !next_states.empty()) {
            return current_states.size() >= 2U && current_states == next_states;
        }
        if (!current.nominal_type.empty() || !next.nominal_type.empty()) {
            return !current.nominal_type.empty()
                && current.nominal_type == next.nominal_type;
        }
        if (!current.named_type.empty() || !next.named_type.empty()) {
            return !current.named_type.empty()
                && normalized_name(current.named_type, unit.language)
                == normalized_name(next.named_type, unit.language);
        }
        const auto current_width = current.width();
        const auto next_width = next.width();
        return state_type(current) && state_type(next)
            && current.domain == next.domain && current.is_signed == next.is_signed
            && current_width.has_value() && current_width == next_width;
    }

} // namespace

CoverageFsmInferenceResult make_coverage_fsm_inference(
    const frontend::DesignUnit& unit,
    const std::span<const frontend::SignalDeclaration> ports,
    const CoverageInventoryOwner& owner,
    const std::span<const VerilogCoverageSource> sources,
    const std::span<const CoverageFsmHint> hints,
    const CoverageFsmInferenceLimits limits) noexcept
{
    CoverageFsmInferenceResult result;
    const auto reject = [&](const Error error,
                            const std::size_t object = 0U) {
        result.inference.reset();
        result.error = error;
        result.object_index = object;
        return result;
    };

    try {
        const auto language = coverage_language(unit.language);
        if (!language || owner.language != unit.language) {
            return reject(Error::InvalidLanguage);
        }
        if (unit.language == frontend::Language::Vhdl2008
            && !valid_standard(unit.vhdl_standard)) {
            return reject(Error::InvalidStandard);
        }
        if (!valid_unit_kind(unit)) {
            return reject(Error::InvalidUnitKind);
        }
        const auto library = unit.library.empty()
            ? std::string_view { "work" }
            : std::string_view { unit.library };
        if (owner.instance.empty() || invalid_text(owner.instance)
            || owner.source.empty() || owner.library != library
            || owner.unit != unit_identity(unit)) {
            return reject(Error::InstanceOwnerMismatch);
        }
        const auto instance = make_coverage_instance_identity(
            { owner.instance, owner.language, owner.library, owner.unit,
                owner.parameter_identities },
            limits.instance_identity);
        if (!instance.ok()) {
            return reject(Error::InvalidInstanceIdentity);
        }
        std::vector<CoverageFsmDescriptionIssueCandidate> issue_candidates;
        const auto add_issue
            = [&](const IssueKind kind, const IssueSubject subject,
                  const std::string_view object,
                  std::vector<IssueOrigin> origins) {
                  if (issue_candidates.size()
                      >= limits.description_validation.maximum_candidates) {
                      return false;
                  }
                  issue_candidates.push_back({ kind, subject,
                      *instance.identity, std::string(owner.instance),
                      std::string(object), std::move(origins) });
                  return true;
              };
        if (sources.size() > limits.maximum_sources) {
            return reject(Error::ResourceLimit);
        }
        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        std::set<std::string> source_identities;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(Error::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(Error::InvalidSourceIdentity);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(Error::DuplicateSourceName);
            }
            if (!source_identities.emplace(
                                      frontend::code_coverage_source_identity_hex(source.identity))
                    .second) {
                return reject(Error::DuplicateSourceIdentity);
            }
        }

        std::map<std::string, Candidate, std::less<>> candidates;
        std::size_t object_count = 0U;
        const auto add_candidate = [&](const std::string& name,
                                       const frontend::Type& type,
                                       const frontend::SourceSpan& span) {
            ++object_count;
            Candidate candidate;
            candidate.type = &type;
            candidate.span = &span;
            auto [entry, inserted] = candidates.emplace(
                normalized_name(name, unit.language),
                std::move(candidate));
            if (!inserted) {
                entry->second.duplicate = true;
            }
        };
        for (const auto& port : ports) {
            if (port.direction == frontend::PortDirection::Output
                || port.direction == frontend::PortDirection::Buffer) {
                add_candidate(port.name, port.type, port.span);
            }
        }
        for (const auto& signal : unit.signals) {
            add_candidate(signal.name, signal.type, signal.span);
        }
        for (const auto& variable : unit.variables) {
            add_candidate(variable.name, variable.type, variable.span);
        }
        if (object_count > limits.maximum_objects) {
            return reject(Error::ResourceLimit);
        }

        if (unit.systemverilog_fsm_pragmas.size()
            > limits.maximum_pragmas) {
            return reject(Error::ResourceLimit);
        }
        if (!unit.systemverilog_fsm_pragmas.empty()
            && (unit.language
                    != frontend::Language::SystemVerilog2017
                || !valid_systemverilog_pragma_standard(
                    unit.standard_revision))) {
            return reject(Error::InvalidSystemVerilogPragma);
        }
        std::size_t pragma_specification_count = 0U;
        for (const auto& pragma : unit.systemverilog_fsm_pragmas) {
            if (pragma.specifications.size()
                > limits.maximum_pragma_specifications
                    - pragma_specification_count) {
                return reject(Error::ResourceLimit);
            }
            pragma_specification_count += pragma.specifications.size();
            std::optional<std::string> current_name;
            std::optional<std::string> next_name;
            std::optional<std::vector<std::string>> legal_states;
            for (const auto& specification : pragma.specifications) {
                if (specification.value
                    && specification.value->decoded_string
                    && specification.value->decoded_string->size()
                        > limits.maximum_pragma_value_bytes) {
                    return reject(Error::ResourceLimit);
                }
                const auto value = pragma_string(
                    specification, limits.maximum_pragma_value_bytes);
                if (!value) {
                    return reject(Error::InvalidSystemVerilogPragma);
                }
                switch (specification.kind) {
                case frontend::SystemVerilogFsmPragmaKind::CurrentState:
                    if (current_name) {
                        return reject(Error::InvalidSystemVerilogPragma);
                    }
                    current_name = *value;
                    break;
                case frontend::SystemVerilogFsmPragmaKind::NextState:
                    if (next_name) {
                        return reject(Error::InvalidSystemVerilogPragma);
                    }
                    next_name = *value;
                    break;
                case frontend::SystemVerilogFsmPragmaKind::LegalStates:
                    if (legal_states) {
                        return reject(Error::InvalidSystemVerilogPragma);
                    }
                    legal_states = pragma_legal_states(
                        *value, limits.maximum_state_name_bytes);
                    if (!legal_states) {
                        return reject(Error::InvalidSystemVerilogPragma);
                    }
                    std::ranges::sort(*legal_states);
                    break;
                }
            }
            if (!current_name) {
                return reject(Error::InvalidSystemVerilogPragma);
            }
            *current_name = normalized_name(
                std::move(*current_name), unit.language);
            auto current = candidates.find(*current_name);
            if (current == candidates.end() || current->second.duplicate
                || current->second.type == nullptr) {
                return reject(Error::InvalidSystemVerilogPragma);
            }
            current->second.pragma_current_state = true;
            if (legal_states) {
                if (!current->second.pragma_legal_states) {
                    current->second.pragma_legal_states
                        = std::move(*legal_states);
                } else if (*current->second.pragma_legal_states
                    != *legal_states) {
                    current->second.pragma_ambiguous = true;
                    if (!add_issue(IssueKind::Conflicting,
                            IssueSubject::LegalStates, *current_name,
                            { IssueOrigin::SystemVerilogPragma })) {
                        return reject(Error::ResourceLimit);
                    }
                }
            }
            if (next_name) {
                *next_name = normalized_name(
                    std::move(*next_name), unit.language);
                const auto next = candidates.find(*next_name);
                if (*next_name == *current_name
                    || next == candidates.end() || next->second.duplicate
                    || next->second.type == nullptr
                    || !compatible_state_types(*current->second.type,
                        *next->second.type, unit)) {
                    return reject(Error::InvalidSystemVerilogPragma);
                }
                current->second.pragma_next_state_candidates.emplace(
                    *next_name);
                next->second.referenced_as_next_state = true;
            }
        }

        if (hints.size() > limits.maximum_hints) {
            return reject(Error::ResourceLimit);
        }
        std::size_t hint_legal_state_count = 0U;
        for (std::size_t index = 0U; index < hints.size(); ++index) {
            const auto& hint = hints[index];
            if (hint.current_state.empty()
                || invalid_text(hint.current_state)) {
                return reject(Error::InvalidFsmHint, index);
            }
            if (hint.current_state.size() > limits.maximum_hint_name_bytes) {
                return reject(Error::ResourceLimit, index);
            }
            bool applies = true;
            switch (hint.origin) {
            case CoverageFsmHintOrigin::VhdlSource:
                if (unit.language != frontend::Language::Vhdl2008
                    || !hint.instance.empty()) {
                    return reject(Error::InvalidFsmHint, index);
                }
                break;
            case CoverageFsmHintOrigin::Manifest:
                if (hint.instance.empty() || invalid_text(hint.instance)) {
                    return reject(Error::InvalidFsmHint, index);
                }
                if (hint.instance.size()
                    > limits.maximum_hint_instance_bytes) {
                    return reject(Error::ResourceLimit, index);
                }
                applies = hint.instance == owner.instance;
                break;
            }
            if (!hint.marks_current_state && !hint.next_state
                && !hint.legal_states) {
                return reject(Error::InvalidFsmHint, index);
            }
            std::optional<std::string> next_name;
            if (hint.next_state) {
                if (hint.next_state->empty()
                    || invalid_text(*hint.next_state)) {
                    return reject(Error::InvalidFsmHint, index);
                }
                if (hint.next_state->size()
                    > limits.maximum_hint_name_bytes) {
                    return reject(Error::ResourceLimit, index);
                }
                next_name = normalized_name(
                    *hint.next_state, unit.language);
            }
            std::optional<std::vector<std::string>> legal_states;
            if (hint.legal_states) {
                if (hint.legal_states->size() < 2U
                    || hint.legal_states->size()
                        > limits.maximum_hint_legal_states
                            - hint_legal_state_count) {
                    return reject(Error::ResourceLimit, index);
                }
                legal_states.emplace();
                legal_states->reserve(hint.legal_states->size());
                std::set<std::string, std::less<>> unique;
                for (const auto& state : *hint.legal_states) {
                    if (state.empty() || invalid_text(state)) {
                        return reject(Error::InvalidFsmHint, index);
                    }
                    if (state.size() > limits.maximum_hint_name_bytes) {
                        return reject(Error::ResourceLimit, index);
                    }
                    auto normalized = normalized_name(state, unit.language);
                    if (!unique.emplace(normalized).second) {
                        return reject(Error::InvalidFsmHint, index);
                    }
                    legal_states->push_back(std::move(normalized));
                }
                hint_legal_state_count += legal_states->size();
                std::ranges::sort(*legal_states);
            }
            if (!applies) {
                continue;
            }
            const auto current_name = normalized_name(
                hint.current_state, unit.language);
            auto current = candidates.find(current_name);
            if (current == candidates.end() || current->second.duplicate
                || current->second.type == nullptr) {
                return reject(Error::InvalidFsmHint, index);
            }
            auto& evidence = current->second;
            const bool vhdl_source
                = hint.origin == CoverageFsmHintOrigin::VhdlSource;
            if (hint.marks_current_state) {
                if (vhdl_source) {
                    evidence.vhdl_source_hint_current_state = true;
                } else {
                    evidence.manifest_hint_current_state = true;
                }
            }
            if (next_name) {
                const auto next = candidates.find(*next_name);
                if (*next_name == current_name
                    || next == candidates.end() || next->second.duplicate
                    || next->second.type == nullptr
                    || !compatible_state_types(*current->second.type,
                        *next->second.type, unit)) {
                    return reject(Error::InvalidFsmHint, index);
                }
                if (vhdl_source) {
                    evidence.vhdl_hint_next_state_candidates.emplace(
                        *next_name);
                } else {
                    evidence.manifest_hint_next_state_candidates.emplace(
                        *next_name);
                }
                next->second.referenced_as_next_state = true;
            }
            if (legal_states) {
                auto& retained = vhdl_source
                    ? evidence.vhdl_hint_legal_states
                    : evidence.manifest_hint_legal_states;
                auto& ambiguous = vhdl_source
                    ? evidence.vhdl_hint_ambiguous
                    : evidence.manifest_hint_ambiguous;
                if (!retained) {
                    retained = std::move(*legal_states);
                } else if (*retained != *legal_states) {
                    ambiguous = true;
                    const auto origin = vhdl_source
                        ? IssueOrigin::VhdlSource
                        : IssueOrigin::Manifest;
                    if (!add_issue(IssueKind::Conflicting,
                            IssueSubject::LegalStates, current_name,
                            { origin })) {
                        return reject(Error::ResourceLimit, index);
                    }
                }
            }
        }

        std::size_t case_count = 0U;
        std::size_t choice_count = 0U;
        std::size_t assignment_count = 0U;
        std::function<Error(std::span<const frontend::Statement>,
            const std::set<std::string, std::less<>>&, std::size_t)>
            scan_statements;
        scan_statements = [&](const std::span<const frontend::Statement> statements,
                              const std::set<std::string, std::less<>>& shadows,
                              const std::size_t depth) -> Error {
            if (depth > limits.maximum_statement_depth) {
                return Error::ResourceLimit;
            }
            for (const auto& statement : statements) {
                auto nested_shadows = shadows;
                for (const auto& declaration : statement.declarations) {
                    nested_shadows.emplace(
                        normalized_name(declaration.name, unit.language));
                }
                if (statement.kind == frontend::StatementKind::Assignment) {
                    ++assignment_count;
                    if (assignment_count > limits.maximum_assignments) {
                        return Error::ResourceLimit;
                    }
                    if (statement.target.kind
                            == frontend::ExpressionKind::Identifier
                        && statement.value.kind
                            == frontend::ExpressionKind::Identifier) {
                        const auto target_name = normalized_name(
                            statement.target.text, unit.language);
                        const auto next_name = normalized_name(
                            statement.value.text, unit.language);
                        if (target_name != next_name
                            && !nested_shadows.contains(target_name)
                            && !nested_shadows.contains(next_name)) {
                            auto current = candidates.find(target_name);
                            auto next = candidates.find(next_name);
                            if (current != candidates.end()
                                && next != candidates.end()
                                && !current->second.duplicate
                                && !next->second.duplicate
                                && current->second.type != nullptr
                                && next->second.type != nullptr
                                && compatible_state_types(
                                    *current->second.type, *next->second.type,
                                    unit)) {
                                current->second.next_state_candidates.emplace(
                                    next_name);
                                next->second.referenced_as_next_state = true;
                            }
                        }
                    }
                }
                if (statement.kind == frontend::StatementKind::Case) {
                    ++case_count;
                    if (case_count > limits.maximum_cases) {
                        return Error::ResourceLimit;
                    }
                    for (const auto& alternative :
                        statement.case_alternatives) {
                        if (alternative.choices.size()
                            > limits.maximum_case_choices - choice_count) {
                            return Error::ResourceLimit;
                        }
                        choice_count += alternative.choices.size();
                    }
                    if (statement.case_match_kind
                            == frontend::CaseMatchKind::Exact
                        && statement.condition.kind
                            == frontend::ExpressionKind::Identifier
                        && !nested_shadows.contains(normalized_name(
                            statement.condition.text, unit.language))) {
                        const auto candidate = candidates.find(normalized_name(
                            statement.condition.text, unit.language));
                        if (candidate != candidates.end()
                            && !candidate->second.duplicate) {
                            std::vector<std::string> states;
                            bool complete = true;
                            for (const auto& alternative :
                                statement.case_alternatives) {
                                if (alternative.is_default) {
                                    continue;
                                }
                                for (const auto& choice : alternative.choices) {
                                    const auto name
                                        = choice_name(choice, unit.language);
                                    if (!name) {
                                        complete = false;
                                        break;
                                    }
                                    states.push_back(*name);
                                }
                                if (!complete) {
                                    break;
                                }
                            }
                            std::ranges::sort(states);
                            states.erase(std::unique(states.begin(), states.end()),
                                states.end());
                            if (complete && states.size() >= 2U) {
                                auto& evidence = candidate->second;
                                if (!evidence.case_states) {
                                    evidence.case_states = std::move(states);
                                } else if (*evidence.case_states != states) {
                                    evidence.case_ambiguous = true;
                                    if (!add_issue(IssueKind::Conflicting,
                                            IssueSubject::LegalStates,
                                            normalized_name(
                                                statement.condition.text,
                                                unit.language),
                                            { IssueOrigin::Case })) {
                                        return Error::ResourceLimit;
                                    }
                                }
                            } else {
                                candidate->second.case_incomplete = true;
                            }
                        }
                    }
                }
                if (const auto error = scan_statements(statement.statements,
                        nested_shadows, depth + 1U);
                    error != Error::None) {
                    return error;
                }
                if (const auto error = scan_statements(statement.else_statements,
                        nested_shadows, depth + 1U);
                    error != Error::None) {
                    return error;
                }
                if (const auto error = scan_statements(statement.loop_updates,
                        nested_shadows, depth + 1U);
                    error != Error::None) {
                    return error;
                }
                for (const auto& alternative : statement.case_alternatives) {
                    if (const auto error = scan_statements(
                            alternative.statements, nested_shadows, depth + 1U);
                        error != Error::None) {
                        return error;
                    }
                }
            }
            return Error::None;
        };
        for (const auto& process : unit.processes) {
            std::set<std::string, std::less<>> shadows;
            for (const auto& variable : process.variables) {
                shadows.emplace(
                    normalized_name(variable.name, unit.language));
            }
            if (const auto error = scan_statements(
                    process.statements, shadows, 1U);
                error != Error::None) {
                return reject(error);
            }
        }

        CoverageFsmInference inference;
        inference.instance_identity = *instance.identity;
        inference.specialization = owner.specialization;
        inference.instance = owner.instance;
        std::set<std::string> current_paths;
        std::set<std::string> next_paths;
        std::set<std::pair<std::uint64_t, std::uint64_t>> object_ids;
        std::set<std::pair<std::uint64_t, std::uint64_t>> state_ids;
        std::set<std::pair<std::uint64_t, std::uint64_t>> legal_set_ids;
        std::map<std::string, ExplicitDescription, std::less<>> descriptions;
        std::map<std::string, std::size_t, std::less<>> next_state_owner_count;
        const auto explicit_origins = [](const Candidate& candidate) {
            std::vector<IssueOrigin> origins;
            if (candidate.pragma_current_state
                || !candidate.pragma_next_state_candidates.empty()
                || candidate.pragma_legal_states) {
                origins.push_back(IssueOrigin::SystemVerilogPragma);
            }
            if (candidate.vhdl_source_hint_current_state
                || !candidate.vhdl_hint_next_state_candidates.empty()
                || candidate.vhdl_hint_legal_states) {
                origins.push_back(IssueOrigin::VhdlSource);
            }
            if (candidate.manifest_hint_current_state
                || !candidate.manifest_hint_next_state_candidates.empty()
                || candidate.manifest_hint_legal_states) {
                origins.push_back(IssueOrigin::Manifest);
            }
            return origins;
        };
        for (const auto& [candidate_name, candidate] : candidates) {
            if (candidate.duplicate && candidate.type != nullptr
                && enum_states(*candidate.type, unit).size() >= 2U
                && !add_issue(IssueKind::Ambiguous,
                    IssueSubject::CurrentState, candidate_name,
                    { IssueOrigin::Enum })) {
                return reject(Error::ResourceLimit);
            }
            ExplicitDescription description;
            description.pragma_evidence = candidate.pragma_current_state
                && !candidate.pragma_ambiguous;
            description.vhdl_source_hint_evidence
                = candidate.vhdl_source_hint_current_state
                && !candidate.vhdl_hint_ambiguous;
            description.manifest_hint_evidence
                = candidate.manifest_hint_current_state
                && !candidate.manifest_hint_ambiguous;
            const auto merge_legal = [&](const bool enabled,
                                         const auto& legal_states) {
                if (!enabled || !legal_states) {
                    return;
                }
                if (!description.legal_states) {
                    description.legal_states = *legal_states;
                } else if (*description.legal_states != *legal_states) {
                    description.ambiguous = true;
                }
            };
            merge_legal(description.pragma_evidence,
                candidate.pragma_legal_states);
            merge_legal(description.vhdl_source_hint_evidence,
                candidate.vhdl_hint_legal_states);
            merge_legal(description.manifest_hint_evidence,
                candidate.manifest_hint_legal_states);
            if (description.ambiguous) {
                if (!add_issue(IssueKind::Conflicting,
                        IssueSubject::LegalStates, candidate_name,
                        explicit_origins(candidate))) {
                    return reject(Error::ResourceLimit);
                }
                description.pragma_evidence = false;
                description.vhdl_source_hint_evidence = false;
                description.manifest_hint_evidence = false;
                description.legal_states.reset();
            } else {
                if (description.pragma_evidence) {
                    description.next_state_candidates.insert(
                        candidate.pragma_next_state_candidates.begin(),
                        candidate.pragma_next_state_candidates.end());
                }
                if (description.vhdl_source_hint_evidence) {
                    description.next_state_candidates.insert(
                        candidate.vhdl_hint_next_state_candidates.begin(),
                        candidate.vhdl_hint_next_state_candidates.end());
                }
                if (description.manifest_hint_evidence) {
                    description.next_state_candidates.insert(
                        candidate.manifest_hint_next_state_candidates.begin(),
                        candidate.manifest_hint_next_state_candidates.end());
                }
            }
            const bool has_explicit_next
                = !description.next_state_candidates.empty();
            if (has_explicit_next
                && description.next_state_candidates.size() > 1U) {
                if (!add_issue(IssueKind::Conflicting,
                        IssueSubject::NextState, candidate_name,
                        explicit_origins(candidate))) {
                    return reject(Error::ResourceLimit);
                }
            }
            if (has_explicit_next
                && std::ranges::any_of(candidate.next_state_candidates,
                    [&](const auto& next_name) {
                        return !description.next_state_candidates.contains(
                            next_name);
                    })) {
                auto origins = explicit_origins(candidate);
                origins.push_back(IssueOrigin::Assignment);
                if (!add_issue(IssueKind::Conflicting,
                        IssueSubject::NextState, candidate_name,
                        std::move(origins))) {
                    return reject(Error::ResourceLimit);
                }
            }
            if (description.next_state_candidates.empty()) {
                description.next_state_candidates
                    = candidate.next_state_candidates;
                if (description.next_state_candidates.size() > 1U
                    && !add_issue(IssueKind::Ambiguous,
                        IssueSubject::NextState, candidate_name,
                        { IssueOrigin::Assignment })) {
                    return reject(Error::ResourceLimit);
                }
            }
            for (const auto& next_name : description.next_state_candidates) {
                ++next_state_owner_count[next_name];
            }
            descriptions.emplace(candidate_name, std::move(description));
        }
        std::size_t total_states = 0U;
        std::size_t candidate_index = 0U;
        for (auto& [name, candidate] : candidates) {
            const auto current_index = candidate_index++;
            if (candidate.duplicate || candidate.type == nullptr
                || candidate.span == nullptr) {
                continue;
            }
            auto states = enum_states(*candidate.type, unit);
            const bool enum_evidence = states.size() >= 2U;
            if (enum_evidence && candidate.case_states
                && !candidate.case_ambiguous) {
                auto enum_names = states;
                std::ranges::sort(enum_names);
                if (enum_names != *candidate.case_states) {
                    const bool case_is_subset = std::ranges::all_of(
                        *candidate.case_states, [&](const auto& state) {
                            return std::ranges::binary_search(
                                enum_names, state);
                        });
                    const auto kind = case_is_subset
                        ? IssueKind::Incomplete
                        : IssueKind::Conflicting;
                    if (!add_issue(kind, IssueSubject::LegalStates, name,
                            { IssueOrigin::Enum, IssueOrigin::Case })) {
                        return reject(Error::ResourceLimit, current_index);
                    }
                }
            }
            const bool case_evidence = candidate.case_states.has_value()
                && !candidate.case_ambiguous;
            const auto description = descriptions.find(name);
            if (description == descriptions.end()) {
                return reject(Error::InvalidFsmHint, current_index);
            }
            const bool pragma_evidence
                = description->second.pragma_evidence;
            const bool vhdl_source_hint_evidence
                = description->second.vhdl_source_hint_evidence;
            const bool manifest_hint_evidence
                = description->second.manifest_hint_evidence;
            const bool explicit_current_state_evidence
                = description->second.current_state_evidence();
            if (candidate.case_incomplete
                && (enum_evidence || explicit_current_state_evidence)) {
                auto origins = explicit_origins(candidate);
                if (enum_evidence) {
                    origins.push_back(IssueOrigin::Enum);
                }
                origins.push_back(IssueOrigin::Case);
                if (!add_issue(IssueKind::Incomplete,
                        IssueSubject::LegalStates, name,
                        std::move(origins))) {
                    return reject(Error::ResourceLimit, current_index);
                }
            }
            if (candidate.referenced_as_next_state
                && candidate.next_state_candidates.empty()
                && candidate.pragma_next_state_candidates.empty()
                && candidate.vhdl_hint_next_state_candidates.empty()
                && candidate.manifest_hint_next_state_candidates.empty()
                && !case_evidence && !explicit_current_state_evidence) {
                continue;
            }
            if (!enum_evidence && !state_type(*candidate.type)
                && !explicit_current_state_evidence) {
                continue;
            }
            if (!enum_evidence && !case_evidence
                && (!explicit_current_state_evidence
                    || !description->second.legal_states)) {
                if (explicit_current_state_evidence
                    && !add_issue(IssueKind::Incomplete,
                        IssueSubject::LegalStates, name,
                        explicit_origins(candidate))) {
                    return reject(Error::ResourceLimit, current_index);
                }
                continue;
            }
            if (!enum_evidence) {
                if (case_evidence) {
                    states = *candidate.case_states;
                } else {
                    states = *description->second.legal_states;
                }
            }
            std::set<std::string> unique_states;
            for (const auto& state : states) {
                if (state.empty() || invalid_text(state)) {
                    return reject(Error::InvalidStateName, current_index);
                }
                if (state.size() > limits.maximum_state_name_bytes) {
                    return reject(Error::ResourceLimit, current_index);
                }
                if (!unique_states.emplace(state).second) {
                    return reject(Error::InvalidStateName, current_index);
                }
            }
            if (states.size() > limits.maximum_states - total_states) {
                return reject(Error::ResourceLimit, current_index);
            }
            total_states += states.size();
            if (name.empty()) {
                return reject(Error::EmptyObjectName, current_index);
            }
            if (invalid_text(name)) {
                return reject(Error::InvalidObjectName, current_index);
            }
            if (owner.instance.size() >= limits.maximum_hierarchy_bytes
                || name.size()
                    >= limits.maximum_hierarchy_bytes - owner.instance.size()) {
                return reject(Error::ResourceLimit, current_index);
            }
            auto path = std::string(owner.instance);
            path += ".";
            path += name;
            if (path.size() > limits.maximum_hierarchy_bytes) {
                return reject(Error::ResourceLimit, current_index);
            }
            if (!current_paths.emplace(path).second) {
                return reject(Error::DuplicateObjectPath, current_index);
            }
            const auto physical = frontend::physical_source(*candidate.span);
            const auto source = source_by_name.find(physical);
            if (source == source_by_name.end()) {
                return reject(Error::UnknownObjectSource, current_index);
            }
            if (physical != owner.source
                && std::ranges::find(owner.source_dependencies, physical)
                    == owner.source_dependencies.end()) {
                return reject(
                    Error::ObjectSourceOwnershipMismatch, current_index);
            }
            const frontend::CodeCoverageSourceSpan span {
                static_cast<std::uint64_t>(candidate.span->begin.offset),
                static_cast<std::uint64_t>(candidate.span->end.offset),
            };
            const auto source_point
                = frontend::make_code_coverage_point_identity(
                    sources[source->second].identity, *language,
                    frontend::CodeCoverageConstructKind::FsmCurrentStateObject,
                    span);
            if (!source_point.ok()) {
                return reject(Error::InvalidObjectSpan, current_index);
            }
            if (candidate.span->begin.line == 0U
                || candidate.span->begin.line > limits.maximum_line_number) {
                return reject(Error::InvalidObjectLine, current_index);
            }
            const auto point = object_identity(
                *source_point.identity, *instance.identity, path,
                "current-state-object");
            if (!runtime::is_code_coverage_identity_valid(point)
                || !object_ids.emplace(point.high, point.low).second) {
                return reject(Error::DuplicateObjectIdentity, current_index);
            }
            CoverageFsmCurrentStateObject object;
            object.source_point = *source_point.identity;
            object.point = point;
            object.instance_identity = *instance.identity;
            object.specialization = owner.specialization;
            object.language = *language;
            object.hierarchy_path = path;
            object.enum_evidence = enum_evidence;
            object.case_evidence = case_evidence;
            object.pragma_evidence = pragma_evidence;
            object.vhdl_source_hint_evidence
                = vhdl_source_hint_evidence;
            object.manifest_hint_evidence = manifest_hint_evidence;
            object.source_index = source->second;
            object.span = span;
            object.line = candidate.span->begin.line;
            object.states.reserve(states.size());
            for (std::size_t index = 0U; index < states.size(); ++index) {
                const auto id = state_identity(point, states[index], index);
                if (!runtime::is_code_coverage_identity_valid(id)
                    || !state_ids.emplace(id.high, id.low).second) {
                    return reject(Error::DuplicateStateIdentity, current_index);
                }
                object.states.push_back(
                    CoverageFsmState { id, std::move(states[index]), index });
            }

            if (inference.legal_state_sets.size()
                >= limits.maximum_legal_state_sets) {
                return reject(Error::ResourceLimit, current_index);
            }
            CoverageFsmLegalStateSet legal;
            legal.current_state_object = point;
            legal.enum_evidence = enum_evidence;
            if (case_evidence) {
                std::vector<std::string> legal_names;
                legal_names.reserve(object.states.size());
                for (const auto& state : object.states) {
                    legal_names.push_back(state.name);
                }
                std::ranges::sort(legal_names);
                legal.case_evidence
                    = legal_names == *candidate.case_states;
            }
            legal.pragma_evidence = pragma_evidence
                && candidate.pragma_legal_states
                && description->second.legal_states
                    == candidate.pragma_legal_states;
            legal.vhdl_source_hint_evidence
                = vhdl_source_hint_evidence
                && candidate.vhdl_hint_legal_states
                && description->second.legal_states
                    == candidate.vhdl_hint_legal_states;
            legal.manifest_hint_evidence = manifest_hint_evidence
                && candidate.manifest_hint_legal_states
                && description->second.legal_states
                    == candidate.manifest_hint_legal_states;
            if (description->second.legal_states) {
                const std::set<std::string, std::less<>> wanted {
                    description->second.legal_states->begin(),
                    description->second.legal_states->end()
                };
                legal.states.reserve(wanted.size());
                for (const auto& state : object.states) {
                    if (wanted.contains(state.name)) {
                        legal.states.push_back(state.id);
                    }
                }
                if (legal.states.size() != wanted.size()) {
                    const auto error = legal.vhdl_source_hint_evidence
                            || legal.manifest_hint_evidence
                        ? Error::InvalidFsmHint
                        : Error::InvalidSystemVerilogPragma;
                    return reject(error, current_index);
                }
            } else {
                legal.states.reserve(object.states.size());
                for (const auto& state : object.states) {
                    legal.states.push_back(state.id);
                }
            }
            legal.point = legal_state_set_identity(point,
                std::span<const runtime::CodeCoveragePointId> {
                    legal.states });
            if (!runtime::is_code_coverage_identity_valid(legal.point)
                || !legal_set_ids.emplace(
                                     legal.point.high, legal.point.low)
                    .second) {
                return reject(
                    Error::DuplicateLegalStateSetIdentity, current_index);
            }
            inference.legal_state_sets.push_back(std::move(legal));

            const auto& selected_next_candidates
                = description->second.next_state_candidates;
            if (selected_next_candidates.size() == 1U) {
                const auto& next_name
                    = *selected_next_candidates.begin();
                const auto owners = next_state_owner_count.find(next_name);
                const auto next = candidates.find(next_name);
                if (owners != next_state_owner_count.end()
                    && owners->second > 1U) {
                    auto origins = explicit_origins(candidate);
                    if (origins.empty()) {
                        origins.push_back(IssueOrigin::Assignment);
                    }
                    if (!add_issue(IssueKind::Ambiguous,
                            IssueSubject::NextState, name,
                            std::move(origins))) {
                        return reject(Error::ResourceLimit, current_index);
                    }
                }
                if (owners != next_state_owner_count.end()
                    && owners->second == 1U && next != candidates.end()
                    && !next->second.duplicate
                    && next->second.type != nullptr
                    && next->second.span != nullptr) {
                    if (inference.next_state_objects.size()
                        >= limits.maximum_next_state_objects) {
                        return reject(Error::ResourceLimit, current_index);
                    }
                    if (owner.instance.size()
                            >= limits.maximum_hierarchy_bytes
                        || next_name.size()
                            >= limits.maximum_hierarchy_bytes
                                - owner.instance.size()) {
                        return reject(Error::ResourceLimit, current_index);
                    }
                    auto next_path = std::string(owner.instance);
                    next_path += ".";
                    next_path += next_name;
                    if (next_path.size() > limits.maximum_hierarchy_bytes) {
                        return reject(Error::ResourceLimit, current_index);
                    }
                    if (!next_paths.emplace(next_path).second) {
                        return reject(
                            Error::DuplicateNextStatePath, current_index);
                    }
                    const auto next_physical
                        = frontend::physical_source(*next->second.span);
                    const auto next_source
                        = source_by_name.find(next_physical);
                    if (next_source == source_by_name.end()) {
                        return reject(
                            Error::UnknownObjectSource, current_index);
                    }
                    if (next_physical != owner.source
                        && std::ranges::find(owner.source_dependencies,
                               next_physical)
                            == owner.source_dependencies.end()) {
                        return reject(Error::ObjectSourceOwnershipMismatch,
                            current_index);
                    }
                    const frontend::CodeCoverageSourceSpan next_span {
                        static_cast<std::uint64_t>(
                            next->second.span->begin.offset),
                        static_cast<std::uint64_t>(
                            next->second.span->end.offset),
                    };
                    const auto next_source_point
                        = frontend::make_code_coverage_point_identity(
                            sources[next_source->second].identity, *language,
                            frontend::CodeCoverageConstructKind::FsmNextStateObject,
                            next_span);
                    if (!next_source_point.ok()) {
                        return reject(
                            Error::InvalidObjectSpan, current_index);
                    }
                    if (next->second.span->begin.line == 0U
                        || next->second.span->begin.line
                            > limits.maximum_line_number) {
                        return reject(
                            Error::InvalidObjectLine, current_index);
                    }
                    const auto next_point = object_identity(
                        *next_source_point.identity, *instance.identity,
                        next_path, "next-state-object");
                    if (!runtime::is_code_coverage_identity_valid(next_point)
                        || !object_ids.emplace(
                                          next_point.high, next_point.low)
                            .second) {
                        return reject(
                            Error::DuplicateNextStateIdentity, current_index);
                    }
                    CoverageFsmNextStateObject next_object;
                    next_object.source_point = *next_source_point.identity;
                    next_object.point = next_point;
                    next_object.current_state_object = point;
                    next_object.instance_identity = *instance.identity;
                    next_object.specialization = owner.specialization;
                    next_object.language = *language;
                    next_object.hierarchy_path = std::move(next_path);
                    next_object.source_index = next_source->second;
                    next_object.span = next_span;
                    next_object.line = next->second.span->begin.line;
                    next_object.assignment_evidence
                        = candidate.next_state_candidates.contains(next_name);
                    next_object.pragma_evidence
                        = pragma_evidence
                        && candidate.pragma_next_state_candidates.contains(
                            next_name);
                    next_object.vhdl_source_hint_evidence
                        = candidate.vhdl_hint_next_state_candidates.contains(
                              next_name)
                        && vhdl_source_hint_evidence;
                    next_object.manifest_hint_evidence
                        = candidate.manifest_hint_next_state_candidates.contains(
                              next_name)
                        && manifest_hint_evidence;
                    inference.next_state_objects.push_back(
                        std::move(next_object));
                }
            }
            inference.current_state_objects.push_back(std::move(object));
        }
        const auto validation = make_coverage_fsm_description_diagnostics(
            issue_candidates, limits.description_validation);
        if (!validation.ok()) {
            const auto error
                = validation.error
                    == CoverageFsmDescriptionValidationError::ResourceLimit
                ? Error::ResourceLimit
                : Error::InvalidDescriptionDiagnostic;
            return reject(error, validation.candidate_index);
        }
        inference.description_diagnostics
            = std::move(*validation.diagnostics);
        result.inference = std::move(inference);
        result.error = Error::None;
        result.object_index = 0U;
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit, result.object_index);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit, result.object_index);
    }
}

} // namespace fsim::elaboration
