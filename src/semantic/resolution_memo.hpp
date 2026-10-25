// SPDX-License-Identifier: Apache-2.0
//
// Resolutions and constant values of one SpecializedHirUnit that read only
// the compiled design and the unit's overlay. The unit owns them
// (SpecializedHirUnit::resolution_memo): copies share them, and replace()
// and select_generate() detach them.
#pragma once

#include "fsim/semantic/compiled_design_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::semantic::detail {

struct ResolutionMemo {
    struct SpellingHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(
            const std::string_view spelling) const noexcept
        {
            return std::hash<std::string_view> { }(spelling);
        }
    };

    struct TypeIdentity {
        std::vector<CompiledBindingFrame> binding_frames;
        CompiledVhdlTypeIdentityResolution result;
    };

    /// Binding contexts kept for one spelling in one scope.
    static constexpr std::size_t maximum_type_identity_contexts { 16U };

    /// CompiledDesignResolver::resolve_vhdl_type_identity results, by owner
    /// scope and spelling, for each binding context.
    std::unordered_map<std::uint32_t,
        std::unordered_map<std::string, std::vector<TypeIdentity>,
            SpellingHash, std::equal_to<>>>
        type_identities;

    struct EffectiveSubtype {
        ScopeId scope;
        vhdl::SubtypeIndication input;
        std::optional<vhdl::SubtypeIndication> result;
    };

    /// Subtypes kept in all.
    static constexpr std::size_t maximum_effective_subtypes { 65536U };

    /// CompiledDesignResolver::effective_vhdl_subtype results outside any
    /// binding frame, by a hash of the use scope and type mark.
    std::unordered_map<std::size_t, std::vector<EffectiveSubtype>>
        effective_subtypes;
    std::size_t effective_subtype_count { };

    /// Values of expressions evaluated without bindings
    /// (SpecializedHirUnit::evaluate_integral_expression), by expression;
    /// empty for an expression found not to be constant.
    std::unordered_map<std::uint32_t, std::optional<std::int64_t>>
        integral_values;

    /// The kinds of declaration constant evaluation resolves an
    /// expression's name to.
    enum class NamedDeclaration : std::uint8_t {
        evaluable_value,
        value_object,
        typed_value,
        attribute_prefix,
        alias,
        function,
        type,
    };
    [[nodiscard]] static std::uint64_t named_key(
        const NamedDeclaration kind, const ExpressionId expression) noexcept
    {
        return (static_cast<std::uint64_t>(kind) << 32U)
            | expression.value();
    }
    /// The declarations named by expressions in constant evaluation, by
    /// kind and expression.
    std::unordered_map<std::uint64_t, std::optional<DeclarationId>>
        named_declarations;
};

} // namespace fsim::semantic::detail
