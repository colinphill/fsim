// SPDX-License-Identifier: Apache-2.0
// Included inside namespace fsim::elaboration::elaboration_detail.

[[nodiscard]] runtime::simir::ValueKind value_kind(
    const frontend::ValueDomain domain) noexcept;
std::optional<std::int64_t> evaluate_constant_expression(
    const Expression& expression,
    const ConstantEnvironment& environment,
    std::string& error);
struct LoweredLiteral {
  PackedLogic4 value;
  frontend::ValueDomain domain{frontend::ValueDomain::Bit2};
};

std::string simple_top_name(std::string_view top);

std::optional<std::uint64_t> unsigned_decimal(std::string_view text);

std::optional<std::int64_t> constant_index(
    const Expression& expression);

std::uint64_t index_distance(
    std::int64_t lhs,
    std::int64_t rhs) noexcept;

PackedLogic4 unsigned_value(std::uint64_t value, std::size_t width);

PackedLogic4 integer_value(std::int64_t value);

PackedLogic4 default_packed_value(
    const frontend::Type& type,
    std::size_t width);

std::optional<VerilogSpecifyTerminalInfo>
resolve_verilog_specify_selection(
    const frontend::Expression& expression,
    runtime::simir::SignalId signal,
    const SignalInfo& info,
    const ConstantEnvironment& environment);

std::optional<std::int64_t> vhdl_enumeration_ordinal(
    const Expression& expression,
    const frontend::Type& type);

const frontend::Type* vhdl_enumeration_type_mark(
    const DesignUnit& unit,
    std::string_view name);

const frontend::Type* vhdl_object_type(
    const DesignUnit& unit,
    std::string_view name);

struct FoldedEnumerationAttribute {
  std::int64_t value{};
  bool enumeration_result{};
  bool boolean_result{};
};

std::optional<FoldedEnumerationAttribute>
evaluate_vhdl_enumeration_attribute(
    const Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error);

std::optional<LoweredLiteral> literal_value(
    const Expression& expression,
    std::size_t expected_width,
    frontend::Language language);

std::optional<PackedLogic4> static_vhdl_value(
    const Expression& expression,
    const frontend::Type& type,
    std::string& error);

std::optional<std::int64_t> vhdl_physical_literal_value(
    const Expression&,
    const frontend::Type&,
    std::string& error);
struct ConstantTypeInfo {
  frontend::ValueDomain domain{frontend::ValueDomain::Unknown};
  bool vhdl_enumeration{};
  std::string nominal_type;
  std::optional<Expression> vhdl_composite_value;
  ConstantTypeInfo();
  ConstantTypeInfo(frontend::ValueDomain value);
  ConstantTypeInfo(
      frontend::ValueDomain value,
      bool enumeration,
      std::string nominal = {});
};

using ConstantDomainEnvironment =
    std::unordered_map<std::string, ConstantTypeInfo>;
