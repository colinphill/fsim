// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaborator_internal.hpp"

#include "fsim/frontend/output_format.hpp"
#include "fsim/runtime/output_format.hpp"

#include <set>

namespace fsim::elaboration::elaboration_detail {

using Value = SystemVerilogConstantValue;

struct ConstantFunctionEffect {
    frontend::AssertionSeverity severity {
        frontend::AssertionSeverity::Error };
    std::string code;
    std::string message;
    frontend::SourceSpan span;
};

struct ConstantCallCacheEntry {
    Value value;
    std::vector<ConstantFunctionEffect> effects;
    std::unordered_set<std::string> delivered_sites;
};

using CallCache = std::unordered_map<std::string, ConstantCallCacheEntry>;

extern thread_local CallCache* active_fold_call_cache;
extern thread_local std::vector<std::unique_ptr<CallCache>>
    active_elaboration_call_caches;
extern thread_local std::vector<Diagnostic>*
    active_constant_function_diagnostics;
extern thread_local std::vector<frontend::Diagnostic>*
    active_constant_function_messages;

void append_key_component(
    std::string& key,
    std::string_view component);

struct CallableBehaviorIdentity {
    std::string digest;
    std::set<std::string> identifiers;
};

enum class Flow {
    normal,
    returned,
    broken,
    continued,
    failed,
};

[[nodiscard]] bool callable_name_matches(
    std::string_view declared,
    std::string_view referenced,
    frontend::Language language);

[[nodiscard]] CallableBehaviorIdentity callable_behavior_identity(
    const frontend::FunctionDeclaration& function);

class ConstantFunctionEvaluator final {

public:
    ConstantFunctionEvaluator(
        const std::vector<frontend::FunctionDeclaration>& functions,
        const SystemVerilogConstantEnvironment& globals,
        const ConstantEnvironment& fallback);

    std::optional<Value> evaluate(
        const Expression& expression,
        std::string& error);

    bool evaluate_elaboration_report(
        const Statement& statement,
        std::string& error);

private:
    struct FixedArrayLayout {
        const frontend::Type* element_type { };
        std::int64_t left { };
        std::int64_t right { };
        std::uint32_t element_width { };
        std::uint32_t total_width { };
    };

    [[nodiscard]] std::optional<FixedArrayLayout>
    fixed_array_layout(
        const frontend::Type& type,
        const SystemVerilogConstantEnvironment& environment) const;

    [[nodiscard]] static std::optional<std::uint32_t>
    fixed_array_offset(
        const FixedArrayLayout& layout,
        const std::int64_t index);

    std::optional<Value> evaluate_expression(
        const Expression& expression,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error,
        const frontend::Type* expected_type = nullptr);

    struct ConstantPatternMatch {
        bool matched { };
        std::unordered_map<std::string, Value> bindings;
    };

    std::optional<ConstantPatternMatch> evaluate_case_pattern(
        const Expression& pattern,
        const Value& selector,
        const frontend::Type* type,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error);

    std::optional<Value> converted(
        const Expression& expression,
        const frontend::Type& type,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error);

    static std::string result_alias(
        const std::string_view name);

    static bool unconstrained_vhdl_builtin_array(
        const frontend::Type& type);

    std::optional<Value> evaluate_call(
        const frontend::FunctionDeclaration& declared_function,
        const Expression& call,
        const SystemVerilogConstantEnvironment& caller,
        const frontend::Type* expected_type,
        std::string& error);

    Flow execute_statements(
        const std::vector<Statement>& statements,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error);

    Flow execute_statement(
        const Statement& statement,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error);

    [[nodiscard]] bool format_report(
        const Statement& statement,
        const SystemVerilogConstantEnvironment& environment,
        std::string& message,
        std::string& error);

    [[nodiscard]] bool emit_effect(
        const ConstantFunctionEffect& effect,
        std::string& error);

    const std::vector<frontend::FunctionDeclaration>& functions_;

    const SystemVerilogConstantEnvironment& globals_;

    const ConstantEnvironment& fallback_;

    CallCache owned_call_cache_;

    CallCache& call_cache_;

    std::unordered_map<
        const frontend::FunctionDeclaration*,
        CallableBehaviorIdentity>
        callable_behaviors_;

    std::vector<const frontend::FunctionDeclaration*> call_stack_;

    std::vector<std::string> call_site_stack_;

    std::vector<const std::unordered_map<
        std::string, const frontend::Type*>*>
        type_scopes_;

    std::vector<ConstantFunctionEffect> effects_;

    std::vector<Diagnostic>* diagnostics_ { };

    std::vector<frontend::Diagnostic>* messages_ { };
};

} // namespace fsim::elaboration::elaboration_detail
