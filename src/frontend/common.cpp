// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/source.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace fsim::frontend {

namespace {

struct SourceNameBucket {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<const std::string>> values;
};

std::array<SourceNameBucket, 64>& source_name_buckets()
{
    static std::array<SourceNameBucket, 64> buckets;
    return buckets;
}

} // namespace

std::shared_ptr<const std::string> SourceName::intern(std::string value)
{
    if (value.empty()) {
        return { };
    }
    auto& buckets = source_name_buckets();
    auto& bucket = buckets[std::hash<std::string_view> { }(value)
        % buckets.size()];
    const std::lock_guard lock { bucket.mutex };
    if (const auto found = bucket.values.find(value);
        found != bucket.values.end()) {
        if (auto existing = found->second.lock()) {
            return existing;
        }
    }
    auto result = std::make_shared<const std::string>(std::move(value));
    bucket.values.insert_or_assign(*result, result);
    return result;
}

SourceName::SourceName(std::string value)
    : value_ { intern(std::move(value)) }
{
}

SourceName::SourceName(const std::string_view value)
    : SourceName(std::string { value })
{
}

SourceName::SourceName(const char* const value)
    : SourceName(value == nullptr ? std::string { } : std::string { value })
{
}

SourceName& SourceName::operator=(std::string value)
{
    value_ = intern(std::move(value));
    return *this;
}

SourceName& SourceName::operator=(const std::string_view value)
{
    return *this = std::string { value };
}

SourceName& SourceName::operator=(const char* const value)
{
    return *this = value == nullptr ? std::string { } : std::string { value };
}

const std::string& SourceName::str() const noexcept
{
    static const std::string empty;
    return value_ ? *value_ : empty;
}

SourceSpan cover(const SourceSpan& first, const SourceSpan& last)
{
    SourceSpan result = first;
    result.end = last.end;
    if (result.source_name.empty()) {
        result.source_name = last.source_name;
    }
    if (result.expansion_stack.empty()) {
        result.expansion_stack = last.expansion_stack;
    }
    return result;
}

std::string_view physical_source(const SourceSpan& span) noexcept
{
    return span.physical_source_name.empty()
        ? std::string_view { span.source_name }
        : std::string_view { span.physical_source_name };
}

const char* to_string(DiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case DiagnosticSeverity::Note:
        return "note";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

std::string format_diagnostic(const Diagnostic& diagnostic)
{
    std::ostringstream output;
    output << diagnostic.span.source_name << ':' << diagnostic.span.begin.line
           << ':' << diagnostic.span.begin.column << ": "
           << to_string(diagnostic.severity) << '[' << diagnostic.code
           << "]: " << diagnostic.message;
    for (const auto& expansion : diagnostic.expansion_stack) {
        output << "\n  note: " << expansion;
    }
    return output.str();
}

bool has_errors(const std::vector<Diagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::Error;
        });
}

std::uint64_t PackedRange::width() const noexcept
{
    if ((descending && left < right) || (!descending && left > right)) {
        return 0;
    }
    const auto unsigned_left = static_cast<std::uint64_t>(left);
    const auto unsigned_right = static_cast<std::uint64_t>(right);
    const auto distance = left >= right ? unsigned_left - unsigned_right
                                        : unsigned_right - unsigned_left;
    if (distance == std::numeric_limits<std::uint64_t>::max()) {
        return 0;
    }
    return distance + 1;
}

std::optional<std::uint64_t> PackedMember::width() const noexcept
{
    if (!nested_types.empty()) {
        return nested_types.front().width();
    }
    if (packed_range) {
        return packed_range->width();
    }
    if (packed_range_expression) {
        return std::nullopt;
    }
    switch (domain) {
    case ValueDomain::Bit2:
    case ValueDomain::Logic4:
    case ValueDomain::Logic9:
    case ValueDomain::Boolean:
        return 1;
    case ValueDomain::Integer:
        return 32;
    case ValueDomain::String:
    case ValueDomain::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> Type::width() const noexcept
{
    if (systemverilog_scalar == SystemVerilogScalarKind::ShortReal) {
        return 32;
    }
    if (systemverilog_scalar == SystemVerilogScalarKind::Real
        || systemverilog_scalar == SystemVerilogScalarKind::Realtime
        || systemverilog_scalar == SystemVerilogScalarKind::Time
        || systemverilog_scalar == SystemVerilogScalarKind::Chandle) {
        return 64;
    }
    if (!systemverilog_class_declaration.empty()) {
        return 64;
    }
    if (vhdl_file) {
        return 32;
    }
    if (vhdl_array && vhdl_array->flat_width) {
        return vhdl_array->flat_width;
    }
    if (packed_range) {
        return packed_range->width();
    }
    if (packed_range_expression) {
        return std::nullopt;
    }
    if (!systemverilog_packed_dimensions.empty()) {
        return std::nullopt;
    }
    if (vhdl_array) {
        return std::nullopt;
    }
    if (!packed_members.empty()) {
        return std::nullopt;
    }
    switch (domain) {
    case ValueDomain::Bit2:
    case ValueDomain::Logic4:
    case ValueDomain::Logic9:
    case ValueDomain::Boolean:
        return 1;
    case ValueDomain::Integer:
        return 32;
    case ValueDomain::String:
    case ValueDomain::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

namespace {

    bool same_expression_shape(const Expression& left, const Expression& right)
    {
        if (left.kind != right.kind || left.text != right.text || left.nominal_type != right.nominal_type || left.aggregate_choices != right.aggregate_choices || left.operands.size() != right.operands.size() || left.aggregate_choice_expressions.size() != right.aggregate_choice_expressions.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.operands.size(); ++index) {
            if (!same_expression_shape(left.operands[index], right.operands[index])) {
                return false;
            }
        }
        for (std::size_t index = 0;
            index < left.aggregate_choice_expressions.size(); ++index) {
            const auto& left_choices = left.aggregate_choice_expressions[index];
            const auto& right_choices = right.aggregate_choice_expressions[index];
            if (left_choices.size() != right_choices.size()) {
                return false;
            }
            for (std::size_t choice = 0; choice < left_choices.size(); ++choice) {
                if (!same_expression_shape(left_choices[choice], right_choices[choice])) {
                    return false;
                }
            }
        }
        return true;
    }

    bool same_packed_range(const std::optional<PackedRange>& left,
        const std::optional<PackedRange>& right)
    {
        return left.has_value() == right.has_value() && (!left || (left->left == right->left && left->right == right->right && left->descending == right->descending));
    }

    bool same_packed_range_expression(
        const std::optional<PackedRangeExpression>& left,
        const std::optional<PackedRangeExpression>& right)
    {
        return left.has_value() == right.has_value() && (!left || (left->descending == right->descending && same_expression_shape(left->left, right->left) && same_expression_shape(left->right, right->right)));
    }

    bool same_integer_range(const std::optional<IntegerRange>& left,
        const std::optional<IntegerRange>& right)
    {
        return left.has_value() == right.has_value() && (!left || (left->left == right->left && left->right == right->right && left->descending == right->descending));
    }

    bool same_integer_range_expression(
        const std::optional<IntegerRangeExpression>& left,
        const std::optional<IntegerRangeExpression>& right)
    {
        return left.has_value() == right.has_value() && (!left || (left->descending == right->descending && same_expression_shape(left->left, right->left) && same_expression_shape(left->right, right->right)));
    }

    bool same_discrete_range_expression(
        const std::optional<DiscreteRangeExpression>& left,
        const std::optional<DiscreteRangeExpression>& right)
    {
        return left.has_value() == right.has_value() && (!left || (left->descending == right->descending && same_expression_shape(left->left, right->left) && same_expression_shape(left->right, right->right)));
    }

} // namespace

bool vhdl_subtype_indications_conform(const Type& left, const Type& right)
{
    if (left.domain != right.domain || left.spelling != right.spelling || left.named_type != right.named_type || left.nominal_type != right.nominal_type || left.is_signed != right.is_signed || left.vhdl_resolution_function != right.vhdl_resolution_function || !same_packed_range(left.packed_range, right.packed_range) || !same_packed_range_expression(left.packed_range_expression, right.packed_range_expression) || !same_integer_range(left.integer_range, right.integer_range) || !same_integer_range_expression(left.integer_range_expression, right.integer_range_expression) || !same_discrete_range_expression(left.discrete_range_expression, right.discrete_range_expression) || left.vhdl_array_constraints.size() != right.vhdl_array_constraints.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.vhdl_array_constraints.size();
        ++index) {
        const auto& left_constraint = left.vhdl_array_constraints[index];
        const auto& right_constraint = right.vhdl_array_constraints[index];
        if (left_constraint.descending != right_constraint.descending || !same_expression_shape(left_constraint.left, right_constraint.left) || !same_expression_shape(left_constraint.right, right_constraint.right)) {
            return false;
        }
    }
    return true;
}

const DesignUnit* ParsedDesign::find(UnitKind kind,
    std::string_view name) const noexcept
{
    const auto found = std::find_if(units.begin(), units.end(), [&](const DesignUnit& unit) {
        return unit.kind == kind && unit.name == name;
    });
    return found == units.end() ? nullptr : &*found;
}

} // namespace fsim::frontend
