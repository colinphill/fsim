// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
namespace {

constexpr std::size_t maximum_hir_let_expansion_depth = 1'024U;

} // namespace

std::optional<semantic::ExpressionId> Lowerer::hir_let_actual(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr || hir_let_frames_.empty()) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr
        || expression->systemverilog->kind
            != semantic::sv::ExpressionKind::name) {
        return std::nullopt;
    }
    const auto name = std::string_view { expression->systemverilog->text };
    for (auto frame = hir_let_frames_.rbegin();
        frame != hir_let_frames_.rend(); ++frame) {
        const auto found = std::ranges::find(
            frame->bindings, name, &HirLetBinding::name);
        if (found != frame->bindings.end()) {
            // The bound actual is evaluated in the invocation's outer
            // context. In particular, `let same(value) = value` called as
            // `same(value)` must not bind the actual identifier to itself.
            if (found->actual == expression_id) {
                continue;
            }
            return found->actual;
        }
    }
    return std::nullopt;
}

const semantic::sv::LetDeclaration* Lowerer::hir_let_declaration(
    const semantic::ExpressionId expression_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return nullptr;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return nullptr;
    }
    const auto& source = *expression->systemverilog;
    const auto call = source.kind == semantic::sv::ExpressionKind::call;
    const auto identifier = source.kind == semantic::sv::ExpressionKind::name;
    if (!call && !identifier) {
        return nullptr;
    }

    const semantic::CompiledDesignResolver resolver {
        *specialized_hir_unit_, hir_generic_binding_frames_
    };
    const auto declaration = resolver.resolve_systemverilog_let(
        source.text, source.scope).unique();
    return declaration != nullptr
            && (call || declaration->ports.empty())
        ? declaration
        : nullptr;
}

std::optional<std::vector<Lowerer::HirLetBinding>>
Lowerer::bind_hir_let_actuals(
    const semantic::ExpressionId expression_id,
    const semantic::sv::LetDeclaration& declaration) const
{
    if (specialized_hir_unit_ == nullptr) {
        return std::nullopt;
    }
    const auto expression = specialized_hir_unit_->find_expression(
        expression_id);
    if (!expression || expression->systemverilog == nullptr) {
        return std::nullopt;
    }
    const auto& call = *expression->systemverilog;
    if (call.kind == semantic::sv::ExpressionKind::name) {
        return declaration.ports.empty()
            ? std::optional<std::vector<HirLetBinding>> {
                  std::vector<HirLetBinding> { }
              }
            : std::nullopt;
    }
    if (call.kind != semantic::sv::ExpressionKind::call
        || (!call.argument_names.empty()
            && call.argument_names.size() != call.operands.size())) {
        return std::nullopt;
    }

    std::vector<HirLetBinding> result;
    result.reserve(declaration.ports.size());
    std::size_t positional { };
    bool saw_named { };
    for (std::size_t index = 0U; index < call.operands.size(); ++index) {
        const auto argument_name = call.argument_names.empty()
            ? std::string_view { }
            : std::string_view { call.argument_names[index] };
        const semantic::sv::LetPort* formal { };
        if (argument_name.empty()) {
            if (saw_named || positional >= declaration.ports.size()) {
                return std::nullopt;
            }
            formal = &declaration.ports[positional++];
        } else {
            saw_named = true;
            const auto found = std::ranges::find(
                declaration.ports, argument_name,
                &semantic::sv::LetPort::name);
            if (found == declaration.ports.end()) {
                return std::nullopt;
            }
            formal = &*found;
        }
        if (std::ranges::find(
                result, formal->name, &HirLetBinding::name)
            != result.end()) {
            return std::nullopt;
        }
        result.push_back({ formal->name, call.operands[index] });
    }
    for (const auto& formal : declaration.ports) {
        if (std::ranges::find(
                result, formal.name, &HirLetBinding::name)
            != result.end()) {
            continue;
        }
        if (!formal.default_value) {
            return std::nullopt;
        }
        result.push_back({ formal.name, *formal.default_value });
    }
    return result;
}

bool Lowerer::push_hir_let_frame(
    const semantic::ExpressionId expression_id,
    const semantic::sv::LetDeclaration& declaration) const
{
    if (hir_let_frames_.size() >= maximum_hir_let_expansion_depth
        || std::ranges::any_of(
            hir_let_frames_,
            [&](const HirLetFrame& frame) {
                return frame.declaration == &declaration;
            })) {
        return false;
    }
    auto bindings = bind_hir_let_actuals(expression_id, declaration);
    if (!bindings) {
        return false;
    }
    hir_let_frames_.push_back({ &declaration, std::move(*bindings) });
    return true;
}

} // namespace fsim::elaboration
