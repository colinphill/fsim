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

IntegerRange vhdl_predefined_integer_range(
    const VhdlStandard standard,
    const std::string_view subtype)
{
    const auto first = standard >= VhdlStandard::Vhdl2019
        ? std::numeric_limits<std::int64_t>::min()
        : static_cast<std::int64_t>(
              std::numeric_limits<std::int32_t>::min());
    const auto last = standard >= VhdlStandard::Vhdl2019
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(
              std::numeric_limits<std::int32_t>::max());
    return IntegerRange {
        subtype == "integer" ? first : subtype == "natural" ? 0 : 1,
        last,
        false
    };
}

Type vhdl_predefined_integer_type(
    const VhdlStandard standard,
    const std::string_view subtype)
{
    Type type;
    type.domain = ValueDomain::Integer;
    type.spelling = subtype;
    type.is_signed = true;
    type.integer_range = vhdl_predefined_integer_range(standard, subtype);
    type.vhdl_integer_storage_width =
        vhdl_predefined_integer_storage_width(standard);
    return type;
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
        return vhdl_integer_storage_width != 0
            ? std::optional<std::uint64_t> {
                  vhdl_integer_storage_width }
            : std::optional<std::uint64_t> { 32 };
    case ValueDomain::String:
    case ValueDomain::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<SystemVerilogIntegralTypeDescriptor>
systemverilog_integral_type_descriptor(
    const std::string_view spelling) noexcept
{
    using Descriptor = SystemVerilogIntegralTypeDescriptor;
    using Entry = std::pair<std::string_view, Descriptor>;
    static constexpr std::array<Entry, 9> descriptors { {
        { "bit", { ValueDomain::Bit2, 1U, false, false,
                     SystemVerilogScalarKind::None } },
        { "logic", { ValueDomain::Logic4, 1U, false, false,
                       SystemVerilogScalarKind::None } },
        { "reg", { ValueDomain::Logic4, 1U, false, false,
                     SystemVerilogScalarKind::None } },
        { "byte", { ValueDomain::Bit2, 8U, true, true,
                      SystemVerilogScalarKind::None } },
        { "shortint", { ValueDomain::Bit2, 16U, true, true,
                          SystemVerilogScalarKind::None } },
        { "int", { ValueDomain::Bit2, 32U, true, true,
                     SystemVerilogScalarKind::None } },
        { "longint", { ValueDomain::Bit2, 64U, true, true,
                         SystemVerilogScalarKind::None } },
        { "integer", { ValueDomain::Logic4, 32U, true, true,
                         SystemVerilogScalarKind::None } },
        { "time", { ValueDomain::Logic4, 64U, true, false,
                      SystemVerilogScalarKind::Time } },
    } };
    for (const auto& [name, descriptor] : descriptors) {
        if (spelling == name) {
            return descriptor;
        }
    }
    return std::nullopt;
}

bool apply_systemverilog_integral_type(
    Type& type, const std::string_view spelling)
{
    const auto descriptor = systemverilog_integral_type_descriptor(spelling);
    if (!descriptor) {
        return false;
    }
    type.spelling = spelling;
    type.domain = descriptor->domain;
    type.is_signed = descriptor->default_signed;
    type.systemverilog_scalar = descriptor->scalar_kind;
    type.packed_range = descriptor->fixed_width
        ? std::optional<PackedRange> { PackedRange {
              static_cast<std::int64_t>(descriptor->default_width) - 1,
              0,
              true } }
        : std::nullopt;
    return true;
}

bool is_systemverilog_simple_integral_type(const Type& type) noexcept
{
    const bool builtin =
        systemverilog_integral_type_descriptor(type.spelling).has_value();
    const bool integral_domain = type.domain == ValueDomain::Bit2
        || type.domain == ValueDomain::Logic4;
    const bool integral_scalar =
        type.systemverilog_scalar == SystemVerilogScalarKind::None
        || type.systemverilog_scalar == SystemVerilogScalarKind::Time;
    return builtin && integral_domain && integral_scalar
        && type.named_type.empty() && type.nominal_type.empty()
        && type.enumeration_literals.empty()
        && type.systemverilog_enumeration_values.empty()
        && type.packed_aggregate == PackedAggregateKind::None
        && type.packed_members.empty()
        && type.systemverilog_packed_dimensions.empty()
        && !type.systemverilog_container
        && type.systemverilog_class_declaration.empty()
        && !type.systemverilog_virtual_interface
        && type.systemverilog_interface_type.empty()
        && !type.vhdl_array && !type.vhdl_access && !type.vhdl_file
        && !type.vhdl_physical && !type.vhdl_protected
        && !type.vhdl_unspecified && type.width().has_value();
}

bool systemverilog_integral_types_equivalent(
    const Type& left, const Type& right) noexcept
{
    if (!is_systemverilog_simple_integral_type(left)
        || !is_systemverilog_simple_integral_type(right)) {
        return false;
    }
    return left.domain == right.domain
        && left.is_signed == right.is_signed
        && left.width() == right.width();
}

namespace {

[[nodiscard]] bool same_systemverilog_expression_shape(
    const Expression& left, const Expression& right) noexcept
{
    if (left.kind != right.kind || left.text != right.text
        || left.operands.size() != right.operands.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.operands.size(); ++index) {
        if (!same_systemverilog_expression_shape(
                left.operands[index], right.operands[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_systemverilog_range(
    const std::optional<PackedRange>& left,
    const std::optional<PackedRange>& right) noexcept
{
    return (!left && !right)
        || (left && right && left->left == right->left
            && left->right == right->right
            && left->descending == right->descending);
}

[[nodiscard]] bool same_systemverilog_range_expression(
    const PackedRangeExpression& left,
    const PackedRangeExpression& right) noexcept
{
    return same_systemverilog_expression_shape(left.left, right.left)
        && same_systemverilog_expression_shape(left.right, right.right)
        && left.descending == right.descending;
}

[[nodiscard]] bool same_systemverilog_optional_range_expression(
    const std::optional<PackedRangeExpression>& left,
    const std::optional<PackedRangeExpression>& right) noexcept
{
    return (!left && !right)
        || (left && right
            && same_systemverilog_range_expression(*left, *right));
}

[[nodiscard]] bool is_systemverilog_nominal_type(
    const Type& type) noexcept
{
    return !type.nominal_type.empty()
        && (type.packed_aggregate != PackedAggregateKind::None
            || !type.enumeration_literals.empty());
}

} // namespace

bool systemverilog_types_equivalent(
    const Type& left, const Type& right) noexcept
{
    const bool left_nominal = is_systemverilog_nominal_type(left);
    const bool right_nominal = is_systemverilog_nominal_type(right);
    if (left_nominal || right_nominal) {
        return left_nominal && right_nominal
            && left.nominal_type == right.nominal_type;
    }
    const bool left_integral = is_systemverilog_simple_integral_type(left);
    const bool right_integral = is_systemverilog_simple_integral_type(right);
    if (left_integral || right_integral) {
        return systemverilog_integral_types_equivalent(left, right);
    }
    const bool left_unresolved = left.domain == ValueDomain::Unknown
        && !left.named_type.empty() && !left.width();
    const bool right_unresolved = right.domain == ValueDomain::Unknown
        && !right.named_type.empty() && !right.width();
    if (left_unresolved || right_unresolved) {
        return left_unresolved && right_unresolved
            && left.named_type == right.named_type;
    }
    if (left.domain != right.domain
        || left.systemverilog_scalar != right.systemverilog_scalar
        || left.is_signed != right.is_signed
        || left.packed_aggregate != right.packed_aggregate
        || !same_systemverilog_range(left.packed_range, right.packed_range)
        || !same_systemverilog_optional_range_expression(
            left.packed_range_expression,
            right.packed_range_expression)
        || left.systemverilog_virtual_interface
            != right.systemverilog_virtual_interface
        || left.systemverilog_interface_type
            != right.systemverilog_interface_type
        || left.systemverilog_interface_modport
            != right.systemverilog_interface_modport
        || left.systemverilog_class_declaration
            != right.systemverilog_class_declaration
        || left.systemverilog_class_parameter_actuals.size()
            != right.systemverilog_class_parameter_actuals.size()
        || left.enumeration_literals != right.enumeration_literals
        || left.systemverilog_enumeration_values.size()
            != right.systemverilog_enumeration_values.size()
        || left.systemverilog_packed_dimensions.size()
            != right.systemverilog_packed_dimensions.size()
        || left.packed_members.size() != right.packed_members.size()
        || left.systemverilog_container.has_value()
            != right.systemverilog_container.has_value()
        || static_cast<bool>(left.vhdl_array)
            != static_cast<bool>(right.vhdl_array)
        || static_cast<bool>(left.vhdl_access)
            != static_cast<bool>(right.vhdl_access)
        || static_cast<bool>(left.vhdl_file)
            != static_cast<bool>(right.vhdl_file)
        || static_cast<bool>(left.vhdl_physical)
            != static_cast<bool>(right.vhdl_physical)
        || static_cast<bool>(left.vhdl_protected)
            != static_cast<bool>(right.vhdl_protected)
        || static_cast<bool>(left.vhdl_unspecified)
            != static_cast<bool>(right.vhdl_unspecified)) {
        return false;
    }
    for (std::size_t index = 0;
        index < left.systemverilog_enumeration_values.size(); ++index) {
        if (!same_systemverilog_expression_shape(
                left.systemverilog_enumeration_values[index],
                right.systemverilog_enumeration_values[index])) {
            return false;
        }
    }
    for (std::size_t index = 0;
        index < left.systemverilog_class_parameter_actuals.size(); ++index) {
        const auto& left_actual =
            left.systemverilog_class_parameter_actuals[index];
        const auto& right_actual =
            right.systemverilog_class_parameter_actuals[index];
        if (left_actual.name != right_actual.name
            || static_cast<bool>(left_actual.type_actual)
                != static_cast<bool>(right_actual.type_actual)) {
            return false;
        }
        if (left_actual.type_actual) {
            if (!systemverilog_types_equivalent(
                    *left_actual.type_actual, *right_actual.type_actual)) {
                return false;
            }
        } else if (!same_systemverilog_expression_shape(
                       left_actual.value, right_actual.value)) {
            return false;
        }
    }
    for (std::size_t index = 0;
        index < left.systemverilog_packed_dimensions.size(); ++index) {
        if (!same_systemverilog_range_expression(
                left.systemverilog_packed_dimensions[index],
                right.systemverilog_packed_dimensions[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.packed_members.size(); ++index) {
        const auto& left_member = left.packed_members[index];
        const auto& right_member = right.packed_members[index];
        if (left_member.name != right_member.name
            || left_member.domain != right_member.domain
            || left_member.is_signed != right_member.is_signed
            || left_member.lsb_offset != right_member.lsb_offset
            || !same_systemverilog_range(
                left_member.packed_range, right_member.packed_range)
            || !same_systemverilog_optional_range_expression(
                left_member.packed_range_expression,
                right_member.packed_range_expression)
            || left_member.nested_types.size()
                != right_member.nested_types.size()) {
            return false;
        }
        for (std::size_t nested = 0;
            nested < left_member.nested_types.size(); ++nested) {
            if (!systemverilog_types_equivalent(
                    left_member.nested_types[nested],
                    right_member.nested_types[nested])) {
                return false;
            }
        }
    }
    if (!left.systemverilog_container) {
        return true;
    }
    const auto& left_container = *left.systemverilog_container;
    const auto& right_container = *right.systemverilog_container;
    if (left_container.kind != right_container.kind
        || !same_systemverilog_range(
            left_container.static_range,
            right_container.static_range)
        || left_container.static_range_expressions.size()
            != right_container.static_range_expressions.size()
        || left_container.element_types.size()
            != right_container.element_types.size()
        || static_cast<bool>(left_container.associative_index_type)
            != static_cast<bool>(right_container.associative_index_type)) {
        return false;
    }
    if (left_container.associative_index_type
        && !systemverilog_types_equivalent(
            *left_container.associative_index_type,
            *right_container.associative_index_type)) {
        return false;
    }
    for (std::size_t index = 0;
        index < left_container.static_range_expressions.size(); ++index) {
        if (!same_systemverilog_range_expression(
                left_container.static_range_expressions[index],
                right_container.static_range_expressions[index])) {
            return false;
        }
    }
    for (std::size_t index = 0;
        index < left_container.element_types.size(); ++index) {
        if (!systemverilog_types_equivalent(
                left_container.element_types[index],
                right_container.element_types[index])) {
            return false;
        }
    }
    return true;
}

namespace {

std::string_view vhdl_simple_type_name(const std::string_view spelling) {
    const auto separator = spelling.find_last_of('.');
    return spelling.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
}

bool vhdl_array_type(const Type& type) {
    const auto name = vhdl_simple_type_name(type.spelling);
    return type.vhdl_array.has_value()
        || name == "bit_vector"
        || name == "std_logic_vector"
        || name == "std_ulogic_vector"
        || name == "signed"
        || name == "unsigned"
        || name == "string";
}

bool vhdl_physical_type(const Type& type) {
    return type.vhdl_physical.has_value()
        || type.nominal_type == "@builtin:time";
}

bool vhdl_floating_type(const Type& type) {
    return vhdl_simple_type_name(type.spelling) == "real";
}

bool vhdl_discrete_type(const Type& type) {
    if (vhdl_array_type(type) || vhdl_physical_type(type)
        || vhdl_floating_type(type) || type.vhdl_access || type.vhdl_file
        || type.vhdl_protected) {
        return false;
    }
    return type.domain == ValueDomain::Integer
        || type.domain == ValueDomain::Boolean
        || type.domain == ValueDomain::Bit2
        || type.domain == ValueDomain::Logic4
        || type.domain == ValueDomain::Logic9
        || !type.enumeration_literals.empty();
}

bool vhdl_unspecified_component_accepts(
    const Type& formal, const Type& actual) {
    if (formal.vhdl_unspecified) {
        return vhdl_unspecified_type_accepts(formal, actual);
    }
    const auto formal_name = vhdl_simple_type_name(formal.spelling);
    const auto actual_name = vhdl_simple_type_name(actual.spelling);
    if (!formal.named_type.empty() || !formal.nominal_type.empty()) {
        return (!formal.nominal_type.empty()
                   && formal.nominal_type == actual.nominal_type)
            || formal_name == actual_name;
    }
    return formal.domain == actual.domain
        && (formal_name.empty() || formal_name == actual_name);
}

} // namespace

bool vhdl_unspecified_type_accepts(const Type& formal, const Type& actual) {
    if (!formal.vhdl_unspecified || actual.vhdl_unspecified) {
        return false;
    }
    const auto& profile = *formal.vhdl_unspecified;
    const bool array = vhdl_array_type(actual);
    const bool physical = vhdl_physical_type(actual);
    const bool floating = vhdl_floating_type(actual);
    const bool discrete = vhdl_discrete_type(actual);
    switch (profile.type_class) {
    case VhdlUnspecifiedTypeClass::Private:
        return !actual.vhdl_file && !actual.vhdl_protected;
    case VhdlUnspecifiedTypeClass::Scalar:
        return discrete || physical || floating;
    case VhdlUnspecifiedTypeClass::Discrete:
        return discrete;
    case VhdlUnspecifiedTypeClass::Integer:
        return actual.domain == ValueDomain::Integer && !physical;
    case VhdlUnspecifiedTypeClass::Physical:
        return physical;
    case VhdlUnspecifiedTypeClass::Floating:
        return floating;
    case VhdlUnspecifiedTypeClass::Access:
        if (!actual.vhdl_access) {
            return false;
        }
        return profile.component_types.empty()
            || (!actual.vhdl_access->designated_types.empty()
                && vhdl_unspecified_component_accepts(
                    profile.component_types.front(),
                    actual.vhdl_access->designated_types.front()));
    case VhdlUnspecifiedTypeClass::File:
        if (!actual.vhdl_file) {
            return false;
        }
        return profile.component_types.empty()
            || (!actual.vhdl_file->element_types.empty()
                && vhdl_unspecified_component_accepts(
                    profile.component_types.front(),
                    actual.vhdl_file->element_types.front()));
    case VhdlUnspecifiedTypeClass::Array: {
        if (!array) {
            return false;
        }
        const auto actual_dimensions = actual.vhdl_array
            ? actual.vhdl_array->dimensions.size() : 1U;
        if (profile.array_index_count != actual_dimensions
            || profile.component_types.size()
                != profile.array_index_count + 1U) {
            return false;
        }
        if (actual.vhdl_array) {
            for (std::size_t index = 0;
                 index < profile.array_index_count; ++index) {
                Type actual_index;
                actual_index.spelling =
                    actual.vhdl_array->dimensions[index].index_subtype;
                if (actual_index.spelling == "integer"
                    || actual_index.spelling == "natural"
                    || actual_index.spelling == "positive") {
                    actual_index.domain = ValueDomain::Integer;
                }
                if (!vhdl_unspecified_component_accepts(
                        profile.component_types[index], actual_index)) {
                    return false;
                }
            }
        } else if (!vhdl_unspecified_component_accepts(
                       profile.component_types.front(),
                       Type{ValueDomain::Integer, "natural", std::nullopt,
                            true})) {
            return false;
        }
        Type actual_element;
        if (actual.vhdl_array
            && !actual.vhdl_array->element_types.empty()) {
            actual_element = actual.vhdl_array->element_types.front();
        } else {
            const auto name = vhdl_simple_type_name(actual.spelling);
            actual_element.spelling = name == "string" ? "character"
                : name == "bit_vector" ? "bit" : "std_logic";
            actual_element.domain = name == "bit_vector"
                ? ValueDomain::Bit2 : ValueDomain::Logic9;
        }
        return vhdl_unspecified_component_accepts(
            profile.component_types.back(), actual_element);
    }
    case VhdlUnspecifiedTypeClass::None:
        return false;
    }
    return false;
}

std::string vhdl_inferred_type_identity(const Type& type) {
    if (!type.nominal_type.empty()) {
        return "nominal:" + type.nominal_type;
    }
    std::string result = "structural:" + type.spelling + ":"
        + std::to_string(static_cast<unsigned>(type.domain)) + ":"
        + (type.is_signed ? "signed" : "unsigned");
    if (const auto width = type.width()) {
        result += ":width=" + std::to_string(*width);
    } else {
        result += ":width=?";
    }
    if (type.vhdl_array) {
        result += ":dimensions="
            + std::to_string(type.vhdl_array->dimensions.size());
        for (const auto& dimension : type.vhdl_array->dimensions) {
            result += ":index=" + dimension.index_subtype;
        }
    }
    if (type.vhdl_access) {
        result += ":access";
    }
    if (type.vhdl_file) {
        result += ":file";
    }
    if (type.vhdl_physical) {
        result += ":physical";
    }
    return result;
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

bool vhdl_base_type_profiles_match(const Type& left, const Type& right)
{
    if (left.vhdl_unspecified || right.vhdl_unspecified) {
        return left.vhdl_unspecified && right.vhdl_unspecified
            && vhdl_inferred_type_identity(left)
                == vhdl_inferred_type_identity(right);
    }
    if (!left.nominal_type.empty() || !right.nominal_type.empty()) {
        return !left.nominal_type.empty() && !right.nominal_type.empty()
            && left.nominal_type == right.nominal_type;
    }
    if (!left.named_type.empty() || !right.named_type.empty()) {
        return left.named_type == right.named_type;
    }
    if (left.domain != right.domain) {
        return false;
    }
    if (left.domain == ValueDomain::Integer
        || left.domain == ValueDomain::Boolean
        || left.domain == ValueDomain::String) {
        return true;
    }
    const auto simple_name = [](const std::string_view spelling) {
        const auto separator = spelling.find_last_of('.');
        return spelling.substr(separator == std::string_view::npos
                ? 0U : separator + 1U);
    };
    const auto left_name = simple_name(left.spelling);
    const auto right_name = simple_name(right.spelling);
    const bool standard_logic_base =
        (left_name == "std_logic" || left_name == "std_ulogic")
        && (right_name == "std_logic" || right_name == "std_ulogic");
    return standard_logic_base
        || (left_name == right_name && left.is_signed == right.is_signed);
}

bool vhdl_parameter_type_profiles_match(const Type& left, const Type& right)
{
    return left.vhdl_unspecified || right.vhdl_unspecified
        || vhdl_base_type_profiles_match(left, right);
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
