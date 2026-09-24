// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace fsim::elaboration {

bool HierarchyBuilder::materialize_compiled_systemverilog_string_declaration(
    const semantic::sv::Declaration& declaration,
    const semantic::SpecializedHirUnit& working_specialization,
    const std::string& materialized_path,
    StringMap& string_objects,
    ReadOnlyStringSet& read_only_strings,
    const frontend::PortDirection direction,
    const frontend::SourceSpan& declaration_span)
{
    const auto full_name = materialized_path + "." + declaration.name;
    if (const auto alias = string_objects.find(declaration.name);
        alias != string_objects.end()) {
        if (static_cast<std::size_t>(alias->second)
            >= design_.string_objects_.size()) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled string port association for '" + full_name
                    + "' has an invalid object",
                declaration_span);
            return false;
        }
        string_objects.insert_or_assign(full_name, alias->second);
        design_.string_by_name_.insert_or_assign(full_name, alias->second);
        if (design_.roots_.size() == 1U
            && materialized_path == active_root_) {
            design_.string_by_name_.insert_or_assign(
                declaration.name, alias->second);
        }
        design_.string_object_info_.push_back(StringObjectInfo {
            alias->second,
            full_name,
            declaration_span,
            true,
            direction,
        });
        if (direction == frontend::PortDirection::Input) {
            read_only_strings.insert(alias->second);
        }
        return true;
    }

    std::string initial_value;
    if (declaration.initializer) {
        std::unordered_set<std::uint32_t> visiting;
        const auto evaluate_string = [&](const auto& self,
                                         const semantic::ExpressionId expression_id)
            -> std::optional<std::string> {
            if (!visiting.insert(expression_id.value()).second) {
                return std::nullopt;
            }
            const auto erase = [&] {
                visiting.erase(expression_id.value());
            };
            const auto expression
                = working_specialization.find_expression(expression_id);
            if (!expression || expression->systemverilog == nullptr) {
                erase();
                return std::nullopt;
            }
            const auto& source = *expression->systemverilog;
            if (source.kind
                    == semantic::sv::ExpressionKind::string_literal
                && source.decoded_string) {
                auto result = *source.decoded_string;
                erase();
                return result;
            }
            if (source.kind
                == semantic::sv::ExpressionKind::concatenation) {
                std::string result;
                for (const auto operand : source.operands) {
                    const auto part = self(self, operand);
                    if (!part) {
                        erase();
                        return std::nullopt;
                    }
                    result += *part;
                }
                erase();
                return result;
            }
            if (source.kind == semantic::sv::ExpressionKind::name
                && source.referenced_name
                && source.referenced_name->selected
                && source.referenced_name->selected->valid()) {
                const auto selected = working_specialization.find_declaration(
                    *source.referenced_name->selected);
                const auto result = selected
                        && selected->systemverilog != nullptr
                        && selected->systemverilog->initializer
                    ? self(self, *selected->systemverilog->initializer)
                    : std::nullopt;
                erase();
                return result;
            }
            erase();
            return std::nullopt;
        };
        const auto evaluated = evaluate_string(
            evaluate_string, *declaration.initializer);
        if (!evaluated) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled string declaration '" + declaration.name
                    + "' has a non-static initializer",
                declaration_span);
            return false;
        }
        initial_value = *evaluated;
    }
    if (initial_value.size() > maximum_string_bytes) {
        report(
            "FSIM-ELAB-SVSTRING-007",
            "module string initializer exceeds the 4096-byte limit",
            declaration_span);
        return false;
    }

    const auto index = design_.string_objects_.size();
    const auto id = static_cast<StringObjectId>(index);
    if (static_cast<std::size_t>(id) != index) {
        throw std::length_error("too many elaborated string objects");
    }
    design_.string_object_info_.push_back(StringObjectInfo {
        id,
        full_name,
        declaration_span,
        declaration.form == semantic::sv::DeclarationForm::port,
        direction,
    });
    design_.string_objects_.push_back(
        StringObject { full_name, std::move(initial_value) });
    string_objects.emplace(declaration.name, id);
    string_objects.emplace(full_name, id);
    design_.string_by_name_.emplace(full_name, id);
    if (design_.roots_.size() == 1U
        && materialized_path == active_root_) {
        design_.string_by_name_.emplace(declaration.name, id);
    }
    if (direction == frontend::PortDirection::Input) {
        read_only_strings.insert(id);
    }
    return true;
}

} // namespace fsim::elaboration
