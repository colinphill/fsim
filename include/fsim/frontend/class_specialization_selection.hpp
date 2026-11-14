// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::frontend {

/// A class specialization read from compiled HIR (IEEE 1800-2023 8.25): the
/// class by declaration identity with the literal or class-type actuals of
/// one specialization. Consumers outside the frontend describe the
/// specialization here and obtain the frontend selection type below.
struct SystemVerilogClassSelection {
    struct Actual {
        std::optional<std::string> formal;
        // A class-type actual, or else the literal below.
        std::shared_ptr<SystemVerilogClassSelection> type;
        ExpressionKind literal_kind { ExpressionKind::IntegerLiteral };
        std::string literal_text;
        std::optional<std::string> decoded_string;
    };
    std::string name;
    std::string declaration_identity;
    std::vector<Actual> actuals;
};

[[nodiscard]] std::vector<SystemVerilogClassTypeActual>
systemverilog_class_specialization_actuals(
    const SystemVerilogClassSelection& specialization,
    const SourceSpan& span);

[[nodiscard]] std::shared_ptr<const Type> systemverilog_class_specialization_owner(
    const SystemVerilogClassSelection& specialization,
    const SourceSpan& span);

}  // namespace fsim::frontend
