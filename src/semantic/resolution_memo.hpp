// SPDX-License-Identifier: Apache-2.0
//
// Resolutions and constant values of one SpecializedHirUnit that read only
// the compiled design and the unit's overlay. The unit owns them
// (SpecializedHirUnit::resolution_memo): copies share them, and replace()
// and select_generate() detach them. The iterations of one generate body
// also share a family memo (SpecializedHirUnit::iteration_family_memo) for
// the resolutions their generate parameters' values cannot change.
#pragma once

#include "fsim/semantic/compiled_design_resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::semantic::detail {

struct ResolutionMemo {
    /// The family memos of the generate iterations derived from this unit
    /// (SpecializedHirUnit::with_hierarchy_identities), by the names of
    /// their generate parameters.
    std::map<std::vector<std::string>, std::shared_ptr<ResolutionMemo>>
        iteration_families;

    /// In a family memo: by use scope, the spellings through which a
    /// resolution there can see which iteration it is in.
    std::unordered_map<std::uint32_t, std::vector<std::string>>
        iteration_variant_names;

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

/// The spellings through which a resolution at `use_scope` in a generate
/// iteration of `family` can see which iteration it is in: every generate
/// parameter, and every declaration of the iterative generate bodies around
/// `use_scope` (IEEE 1076-2008 11.8, 14.5.3). A declaration outside those
/// bodies cannot name one inside them, and names resolve by spelling, so a
/// resolution none of whose identifiers is spelled like these reads the same
/// in every iteration.
[[nodiscard]] const std::vector<std::string>& iteration_variant_names(
    const SpecializedHirUnit& unit, ScopeId use_scope,
    ResolutionMemo& family);

/// Whether an identifier token of `text` is spelled like one of `names`
/// (ignoring case); an extended identifier always is.
[[nodiscard]] bool spelling_iteration_variant(
    std::string_view text, std::span<const std::string> names);

/// Whether no identifier of a VHDL expression tree is spelled like one of
/// `names` (iteration_variant_names).
[[nodiscard]] bool vhdl_expression_iteration_independent(
    const SpecializedHirUnit& unit, ExpressionId expression,
    std::span<const std::string> names, std::size_t depth = 0U);

} // namespace fsim::semantic::detail
