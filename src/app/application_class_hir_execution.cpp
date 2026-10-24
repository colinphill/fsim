// SPDX-License-Identifier: Apache-2.0
#include "application_class_hir_execution.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/support/uvm_identity.hpp"
#include "../elaboration/hir_output_conversion.hpp"

#include "fsim/frontend/output_format.hpp"
#include "fsim/runtime/output_format.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::app::application_detail {
namespace {

constexpr std::size_t maximum_class_call_depth = 1024U;
constexpr std::size_t maximum_literal_width = 16U * 1024U * 1024U;

[[nodiscard]] std::optional<std::uint64_t> decimal(
    const std::string_view text)
{
    std::uint64_t value { };
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value, 10);
    return parsed.ec == std::errc { }
            && parsed.ptr == text.data() + text.size()
        ? std::optional { value }
        : std::nullopt;
}

[[nodiscard]] std::optional<runtime::PackedLogic4> logic_literal(
    std::string spelling)
{
    spelling.erase(
        std::remove(spelling.begin(), spelling.end(), '_'), spelling.end());
    const auto apostrophe = spelling.find('\'');
    if (apostrophe == std::string::npos)
        return std::nullopt;
    auto suffix = std::string_view { spelling }.substr(apostrophe + 1U);
    if (apostrophe == 0U && suffix.size() == 1U) {
        const auto digit = static_cast<char>(std::tolower(
            static_cast<unsigned char>(suffix.front())));
        const auto value = digit == '0' ? runtime::Logic4::zero
            : digit == '1'             ? runtime::Logic4::one
            : digit == 'x'             ? runtime::Logic4::x
            : digit == 'z' || digit == '?' ? runtime::Logic4::z
                                           : runtime::Logic4::x;
        if (digit != '0' && digit != '1' && digit != 'x'
            && digit != 'z' && digit != '?') {
            return std::nullopt;
        }
        return runtime::PackedLogic4(1U, value);
    }
    // An unsized based literal (`'b0101`) has at least 32 bits (IEEE
    // 1800-2017 5.7.1), more when its digits need them.
    const auto width = apostrophe == 0U
        ? std::optional<std::uint64_t> { std::max<std::uint64_t>(32U,
              static_cast<std::uint64_t>(suffix.size()) * 4U) }
        : decimal(std::string_view { spelling }.substr(0U, apostrophe));
    if (!width || *width == 0U || *width > maximum_literal_width)
        return std::nullopt;
    if (!suffix.empty()
        && (suffix.front() == 's' || suffix.front() == 'S')) {
        suffix.remove_prefix(1U);
    }
    if (suffix.size() < 2U)
        return std::nullopt;
    const auto base = static_cast<char>(std::tolower(
        static_cast<unsigned char>(suffix.front())));
    suffix.remove_prefix(1U);
    if (base == 'd') {
        const auto value = decimal(suffix);
        if (!value)
            return std::nullopt;
        auto result = runtime::PackedLogic4(
            static_cast<std::size_t>(*width), runtime::Logic4::zero);
        for (std::size_t bit = 0U;
             bit < std::min<std::size_t>(64U, result.width()); ++bit) {
            if ((*value & (UINT64_C(1) << bit)) != 0U)
                result.set(bit, runtime::Logic4::one);
        }
        return result;
    }
    const auto digit_width = base == 'b' ? 1U : base == 'o' ? 3U
        : base == 'h' ? 4U : 0U;
    if (digit_width == 0U)
        return std::nullopt;
    constexpr std::string_view binary_digits {
        "0000000100100011010001010110011110001001101010111100110111101111"
    };
    std::string bits;
    bits.reserve(suffix.size() * digit_width);
    for (const auto source_digit : suffix) {
        const auto digit = static_cast<char>(std::toupper(
            static_cast<unsigned char>(source_digit)));
        if (digit == 'X' || digit == 'Z' || digit == '?') {
            bits.append(digit_width, digit == '?' ? 'Z' : digit);
            continue;
        }
        const auto numeric = digit >= '0' && digit <= '9'
            ? static_cast<unsigned>(digit - '0')
            : digit >= 'A' && digit <= 'F'
            ? static_cast<unsigned>(digit - 'A' + 10)
            : 16U;
        if (numeric >= (1U << digit_width))
            return std::nullopt;
        bits.append(binary_digits.substr(
            numeric * 4U + 4U - digit_width, digit_width));
    }
    const auto result_width = static_cast<std::size_t>(*width);
    if (bits.size() > result_width)
        bits.erase(0U, bits.size() - result_width);
    else if (bits.size() < result_width)
        bits.insert(0U, result_width - bits.size(), '0');
    try {
        return runtime::PackedLogic4::from_msb_string(bits);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::string_view marker_payload(
    const semantic::sv::Expression& expression,
    const std::string_view marker)
{
    if (!expression.class_member_identity.empty())
        return expression.class_member_identity;
    return expression.text.starts_with(marker)
        ? std::string_view { expression.text }.substr(marker.size())
        : std::string_view { };
}

[[nodiscard]] const semantic::sv::SpecializedClassProperty*
static_class_property(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const std::string_view identity,
    const std::optional<semantic::DeclarationId> declaration_id)
{
    if (identity.empty() && !declaration_id)
        return nullptr;

    const semantic::sv::SpecializedClassProperty* selected { };
    for (const auto& specialization : specializations) {
        for (const auto& candidate : specialization.properties) {
            if (!candidate.static_storage
                || (declaration_id
                    && candidate.declaration != *declaration_id)
                || (!identity.empty()
                    && candidate.canonical_identity != identity)) {
                continue;
            }
            if (selected != nullptr
                && (selected->declaration != candidate.declaration
                    || selected->canonical_identity
                        != candidate.canonical_identity
                    || selected->type != candidate.type)) {
                return nullptr;
            }
            selected = &candidate;
        }
    }
    return selected;
}

[[nodiscard]] std::optional<runtime::PackedLogic4> cast_integral_index(
    const runtime::PackedLogic4& source,
    const std::size_t target_width,
    const bool source_signed,
    const bool target_four_state)
{
    if (source.width() == 0U || target_width == 0U || target_width > 64U)
        return std::nullopt;

    const auto convert_state = [target_four_state](const runtime::Logic4 state) {
        if (!target_four_state
            && (state == runtime::Logic4::x || state == runtime::Logic4::z)) {
            return runtime::Logic4::zero;
        }
        return state;
    };
    auto result = runtime::PackedLogic4(target_width, runtime::Logic4::zero);
    const auto copied_width = std::min(source.width(), target_width);
    for (std::size_t bit = 0U; bit < copied_width; ++bit)
        result.set(bit, convert_state(source.get(bit)));

    if (source_signed && target_width > source.width()) {
        const auto extension = convert_state(source.get(source.width() - 1U));
        for (std::size_t bit = source.width(); bit < target_width; ++bit)
            result.set(bit, extension);
    }
    return result;
}

[[nodiscard]] bool packed_has_unknown(
    const runtime::PackedLogic4& value)
{
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        const auto state = value.get(bit);
        if (state == runtime::Logic4::x || state == runtime::Logic4::z)
            return true;
    }
    return false;
}

[[nodiscard]] std::optional<runtime::PackedLogic4> resize_expression_operand(
    const runtime::PackedLogic4& value,
    const std::size_t width,
    const bool sign_extend)
{
    if (value.width() == 0U || width == 0U || value.is_logic9())
        return std::nullopt;
    auto result = runtime::PackedLogic4(width, runtime::Logic4::zero);
    const auto copied = std::min(width, value.width());
    for (std::size_t bit = 0U; bit < copied; ++bit)
        result.set(bit, value.get(bit));
    if (sign_extend && width > value.width()) {
        const auto extension = value.get(value.width() - 1U);
        for (std::size_t bit = value.width(); bit < width; ++bit)
            result.set(bit, extension);
    }
    return result;
}

// The two's complement of a fully known value.
[[nodiscard]] runtime::PackedLogic4 negate_known_packed(
    const runtime::PackedLogic4& value)
{
    return runtime::simir::binary_value(
        runtime::simir::BinaryOperator::subtract_unsigned,
        runtime::PackedLogic4(value.width(), runtime::Logic4::zero), value);
}

[[nodiscard]] std::optional<runtime::PackedLogic4> evaluate_binary_packed(
    const std::string_view operation,
    const runtime::PackedLogic4& lhs,
    const runtime::PackedLogic4& rhs,
    const bool signed_operation,
    const bool power_base_signed)
{
    using runtime::Logic4;
    using runtime::PackedLogic4;
    const bool shift = operation == "<<" || operation == "<<<"
        || operation == ">>" || operation == ">>>";
    const bool power = operation == "**";
    if (lhs.width() == 0U
        || (!shift && !power && lhs.width() != rhs.width())
        || rhs.width() == 0U || lhs.is_logic9() || rhs.is_logic9()) {
        return std::nullopt;
    }
    if (shift) {
        using runtime::simir::ShiftOperator;
        const auto shift_operation = operation == "<<" || operation == "<<<"
            ? ShiftOperator::logical_left
            : operation == ">>>" && signed_operation
            ? ShiftOperator::arithmetic_right
            : ShiftOperator::logical_right;
        return runtime::simir::shift_value(shift_operation, lhs, rhs, false);
    }

    using runtime::simir::BinaryOperator;
    auto binary = std::optional<BinaryOperator> { };
    if (operation == "&") binary = BinaryOperator::bit_and;
    else if (operation == "|") binary = BinaryOperator::bit_or;
    else if (operation == "^") binary = BinaryOperator::bit_xor;
    else if (operation == "+") binary = signed_operation
            ? BinaryOperator::add_signed : BinaryOperator::add_unsigned;
    else if (operation == "-") binary = signed_operation
            ? BinaryOperator::subtract_signed : BinaryOperator::subtract_unsigned;
    else if (operation == "*") binary = signed_operation
            ? BinaryOperator::multiply_signed : BinaryOperator::multiply_unsigned;
    else if (operation == "**") binary = signed_operation
            ? BinaryOperator::power_signed : BinaryOperator::power_unsigned;
    else if (operation == "/") binary = signed_operation
            ? BinaryOperator::divide_signed : BinaryOperator::divide_unsigned;
    else if (operation == "%") binary = signed_operation
            ? BinaryOperator::remainder_signed : BinaryOperator::modulo_unsigned;
    else if (operation == "==") binary = BinaryOperator::equal;
    else if (operation == "!=") binary = BinaryOperator::not_equal;
    else if (operation == "===") binary = BinaryOperator::case_equal;
    else if (operation == "!==") binary = BinaryOperator::case_equal;
    else if (operation == "~^" || operation == "^~")
        binary = BinaryOperator::bit_xor;
    if (!binary)
        return std::nullopt;
    auto result = [&]() -> std::optional<PackedLogic4> {
        if (!power || lhs.width() == rhs.width())
            return runtime::simir::binary_value(*binary, lhs, rhs);

        // The helper requires equal widths. Keep the exponent's signedness
        // for its signed power mode and the base's signedness for extension;
        // discard helper-only high result bits after evaluation.
        const auto work_width = std::max(lhs.width(), rhs.width());
        const auto work_lhs = resize_expression_operand(
            lhs, work_width, power_base_signed);
        const auto work_rhs = resize_expression_operand(
            rhs, work_width, signed_operation);
        if (!work_lhs || !work_rhs)
            return std::nullopt;
        const auto widened = runtime::simir::binary_value(
            *binary, *work_lhs, *work_rhs);
        return resize_expression_operand(widened, lhs.width(), false);
    }();
    if (!result)
        return std::nullopt;
    if (operation == "!==")
        result->set(0U, runtime::logic_not(result->get(0U)));
    if (operation == "~^" || operation == "^~") {
        for (std::size_t bit = 0U; bit < result->width(); ++bit)
            result->set(bit, runtime::logic_not(result->get(bit)));
    }
    return result;
}

[[nodiscard]] bool is_integral_associative_index(
    const semantic::sv::AssociativeIndexTypeReference& index_type,
    const semantic::sv::Hir& hir)
{
    using semantic::sv::TypeForm;
    if (index_type.container_form
        || !index_type.unpacked_dimensions.empty()
        || index_type.value_form == TypeForm::string
        || index_type.value_form == TypeForm::class_handle
        || index_type.virtual_interface) {
        return false;
    }
    if (index_type.value_form == TypeForm::packed_integral
        || index_type.value_form == TypeForm::enumeration
        || index_type.value_form == TypeForm::packed_structure
        || index_type.value_form == TypeForm::packed_union) {
        return true;
    }

    constexpr std::array<std::string_view, 9> integral_names {
        "bit", "logic", "reg", "byte", "shortint", "int",
        "integer", "longint", "time"
    };
    auto target = index_type.target;
    std::vector<semantic::TypeId> visited;
    for (std::size_t depth = 0U; depth < 64U; ++depth) {
        if (std::ranges::find(integral_names, target.spelling)
            != integral_names.end()) {
            return true;
        }
        if (!target.target.valid()
            || std::ranges::find(visited, target.target) != visited.end()) {
            return false;
        }
        visited.push_back(target.target);
        const auto* definition = hir.find_type(target.target);
        if (definition == nullptr)
            return false;
        if (definition->form == TypeForm::enumeration
            || definition->form == TypeForm::packed_integral
            || definition->form == TypeForm::packed_structure
            || definition->form == TypeForm::packed_union) {
            return true;
        }
        if (definition->form != TypeForm::alias)
            return false;
        target = definition->base.target;
    }
    return false;
}

// Points `slot` at `value` for one scope and restores it on exit.
template <typename T>
class ScopedSlot {
public:
    ScopedSlot(T*& slot, T* value) noexcept
        : slot_ { slot }, saved_ { std::exchange(slot, value) }
    {
    }
    ScopedSlot(const ScopedSlot&) = delete;
    ScopedSlot& operator=(const ScopedSlot&) = delete;
    ~ScopedSlot() { slot_ = saved_; }

private:
    T*& slot_;
    T* saved_;
};

// Sets `slot` to `value` for one scope and restores it on exit.
template <typename T>
class ScopedValue {
public:
    ScopedValue(T& slot, T value)
        : slot_ { slot }, saved_ { std::exchange(slot, std::move(value)) }
    {
    }
    ScopedValue(const ScopedValue&) = delete;
    ScopedValue& operator=(const ScopedValue&) = delete;
    ~ScopedValue() { slot_ = std::move(saved_); }

private:
    T& slot_;
    T saved_;
};

// Restores one map entry (or its absence) on scope exit.
template <typename Map>
class ScopedMapEntry {
public:
    ScopedMapEntry(Map& map, typename Map::key_type key)
        : map_ { map }, key_ { std::move(key) }
    {
        if (const auto found = map_.find(key_); found != map_.end()) {
            saved_ = found->second;
        }
    }
    ScopedMapEntry(const ScopedMapEntry&) = delete;
    ScopedMapEntry& operator=(const ScopedMapEntry&) = delete;
    ~ScopedMapEntry()
    {
        if (saved_) {
            map_.insert_or_assign(key_, std::move(*saved_));
        } else {
            map_.erase(key_);
        }
    }

private:
    Map& map_;
    typename Map::key_type key_;
    std::optional<typename Map::mapped_type> saved_;
};

// Pops the frame a call pushed onto `frames`, when it pushed one.
template <typename Frames>
class PendingFrameGuard {
public:
    PendingFrameGuard(Frames& frames, const bool active) noexcept
        : frames_ { frames }, active_ { active }
    {
    }
    PendingFrameGuard(const PendingFrameGuard&) = delete;
    PendingFrameGuard& operator=(const PendingFrameGuard&) = delete;
    ~PendingFrameGuard()
    {
        if (active_) {
            frames_.pop_back();
        }
    }

private:
    Frames& frames_;
    bool active_;
};

// Reports from `report_process` (when given) and names `hierarchy` as the
// active scope while a method runs; restores both on exit.
class ExecutionContextGuard {
public:
    ExecutionContextGuard(
        std::optional<runtime::simir::ProcessId>& report_slot,
        const std::optional<runtime::simir::ProcessId> report_process,
        std::string& hierarchy_slot, std::string hierarchy)
        : report_slot_ { report_slot }, previous_report_ { report_slot },
          hierarchy_slot_ { hierarchy_slot },
          previous_hierarchy_ {
              std::exchange(hierarchy_slot, std::move(hierarchy)) }
    {
        if (report_process) {
            report_slot = report_process;
        }
    }
    ExecutionContextGuard(const ExecutionContextGuard&) = delete;
    ExecutionContextGuard& operator=(const ExecutionContextGuard&) = delete;
    ~ExecutionContextGuard()
    {
        report_slot_ = previous_report_;
        hierarchy_slot_ = std::move(previous_hierarchy_);
    }

private:
    std::optional<runtime::simir::ProcessId>& report_slot_;
    std::optional<runtime::simir::ProcessId> previous_report_;
    std::string& hierarchy_slot_;
    std::string previous_hierarchy_;
};

} // namespace

SystemVerilogClassHirExecution::SystemVerilogClassHirExecution(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const semantic::sv::Hir& hir,
    const runtime::SystemVerilogClassHeap& heap)
    : specializations_(specializations)
    , hir_(hir)
    , heap_(heap)
{
}

void SystemVerilogClassHirExecution::set_runtime_services(
    FunctionInvoker function,
    StaticFunctionInvoker static_function,
    PropertyRead property_read,
    PropertyWrite property_write,
    StaticPropertyRead static_property_read,
    StaticPropertyWrite static_property_write)
{
    invoke_function_ = std::move(function);
    invoke_static_function_ = std::move(static_function);
    read_property_ = std::move(property_read);
    write_property_ = std::move(property_write);
    read_static_property_ = std::move(static_property_read);
    write_static_property_ = std::move(static_property_write);
}

const semantic::sv::ClassSpecialization*
SystemVerilogClassHirExecution::specialization(
    const std::string_view identity) const noexcept
{
    const auto found = std::ranges::find(
        specializations_, identity,
        &semantic::sv::ClassSpecialization::specialization_identity);
    if (found != specializations_.end())
        return &*found;
    const semantic::sv::ClassSpecialization* selected { };
    for (const auto& candidate : specializations_) {
        if (candidate.declaration_identity != identity)
            continue;
        if (selected != nullptr)
            return nullptr;
        selected = &candidate;
    }
    return selected;
}

const semantic::sv::Declaration*
SystemVerilogClassHirExecution::declaration(
    const semantic::DeclarationId id) const noexcept
{
    return hir_.find_declaration(id);
}

const semantic::sv::TypeDefinition*
SystemVerilogClassHirExecution::type_definition(
    const semantic::TypeId id) const noexcept
{
    return hir_.find_type(id);
}

const semantic::sv::Expression*
SystemVerilogClassHirExecution::expression(
    const semantic::ExpressionId id) const noexcept
{
    const auto found = std::ranges::find(
        hir_.expressions(), id, &semantic::sv::Expression::id);
    return found == hir_.expressions().end() ? nullptr : &*found;
}

const semantic::sv::Statement*
SystemVerilogClassHirExecution::statement(
    const semantic::StatementId id) const noexcept
{
    const auto found = std::ranges::find(
        hir_.statements(), id, &semantic::sv::Statement::id);
    return found == hir_.statements().end() ? nullptr : &*found;
}

const semantic::sv::SpecializedClassProperty*
SystemVerilogClassHirExecution::instance_property(
    const runtime::SystemVerilogClassHandle handle,
    const semantic::DeclarationId declaration_id) const
{
    if (!heap_.contains(handle))
        return nullptr;
    auto current = specialization(heap_.object(handle).specialization_identity);
    while (current != nullptr) {
        const auto found = std::ranges::find(
            current->properties, declaration_id,
            &semantic::sv::SpecializedClassProperty::declaration);
        if (found != current->properties.end())
            return found->static_storage ? nullptr : &*found;
        current = current->base
            ? specialization(current->base->specialization_identity)
            : nullptr;
    }
    return nullptr;
}

const semantic::sv::SpecializedClassProperty*
SystemVerilogClassHirExecution::instance_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view canonical_identity) const
{
    if (canonical_identity.empty() || !heap_.contains(handle))
        return nullptr;
    auto current = specialization(heap_.object(handle).specialization_identity);
    while (current != nullptr) {
        const auto found = std::ranges::find(
            current->properties, canonical_identity,
            &semantic::sv::SpecializedClassProperty::canonical_identity);
        if (found != current->properties.end())
            return found->static_storage ? nullptr : &*found;
        current = current->base
            ? specialization(current->base->specialization_identity)
            : nullptr;
    }
    return nullptr;
}

const SystemVerilogClassHirExecution::HirLookupIndex&
SystemVerilogClassHirExecution::hir_index() const
{
    if (hir_index_ && hir_index_->revision == hir_.revision()) {
        return *hir_index_;
    }
    auto& index = hir_index_.emplace();
    index.revision = hir_.revision();
    for (const auto& unit : hir_.units()) {
        if (unit.kind == semantic::sv::UnitKind::package) {
            index.package_scopes.insert(unit.scope.value());
            for (const auto declaration : unit.declarations) {
                auto& packages
                    = index.packages_by_declaration[declaration.value()];
                if (packages.empty() || packages.back() != &unit) {
                    packages.push_back(&unit);
                }
            }
        } else if (unit.kind == semantic::sv::UnitKind::module) {
            index.modules_by_name[unit.name].push_back(&unit);
        }
    }
    for (const auto& candidate : hir_.classes()) {
        for (const auto& parameter : candidate.parameters) {
            index.class_parameters.insert(parameter.declaration.value());
            if (!parameter.type_parameter) {
                index.class_value_parameters.insert(
                    parameter.declaration.value());
            }
        }
        for (const auto member : candidate.member_declarations) {
            index.class_members.insert(member.value());
        }
        index.class_scopes.insert(candidate.scope.value());
    }
    for (const auto& candidate : hir_.declarations()) {
        if (candidate.form
            == semantic::sv::DeclarationForm::enumeration_literal) {
            index.enumeration_literals_by_scope[candidate.scope.value()]
                .push_back(&candidate);
        } else if (candidate.form
            == semantic::sv::DeclarationForm::typedef_declaration) {
            index.typedefs_by_name[candidate.name].push_back(&candidate);
        }
    }
    for (const auto& definition : hir_.types()) {
        for (const auto& literal : definition.enumeration_literals) {
            auto& types = index.types_by_enumeration_literal[literal.name];
            if (types.empty() || types.back() != &definition) {
                types.push_back(&definition);
            }
        }
    }
    return index;
}

bool SystemVerilogClassHirExecution::is_package_member(
    const semantic::sv::Declaration& declaration) const
{
    const auto& index = hir_index();
    const auto found
        = index.packages_by_declaration.find(declaration.id.value());
    return found != index.packages_by_declaration.end()
        && std::ranges::any_of(found->second, [&](const auto* unit) {
               return unit->scope == declaration.scope;
           });
}

bool SystemVerilogClassHirExecution::is_package_global(
    const semantic::DeclarationId id) const
{
    const auto* selected = declaration(id);
    if (selected == nullptr
        || (selected->form != semantic::sv::DeclarationForm::variable
            && selected->form != semantic::sv::DeclarationForm::net)) {
        return false;
    }
    return is_package_member(*selected);
}

bool SystemVerilogClassHirExecution::is_package_constant(
    const semantic::DeclarationId id) const
{
    const auto* selected = declaration(id);
    if (selected == nullptr
        || (selected->form != semantic::sv::DeclarationForm::parameter
            && selected->form
                != semantic::sv::DeclarationForm::local_parameter
            && selected->form
                != semantic::sv::DeclarationForm::enumeration_literal)) {
        return false;
    }
    const auto& index = hir_index();
    return index.packages_by_declaration.contains(id.value())
        || (selected->form
                == semantic::sv::DeclarationForm::enumeration_literal
            && index.package_scopes.contains(selected->scope.value()));
}

const SystemVerilogClassHirExecution::VariableReference*
SystemVerilogClassHirExecution::active_reference(
    const semantic::DeclarationId id) const noexcept
{
    const auto found = active_references_.find(id.value());
    return found == active_references_.end() ? nullptr : &found->second;
}

std::optional<SystemVerilogClassHirExecution::VariableReference>
SystemVerilogClassHirExecution::variable_reference(
    const semantic::ExpressionId actual_id,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& string_environment) const
{
    const auto* actual = expression(actual_id);
    if (actual == nullptr)
        return std::nullopt;
    constexpr std::string_view property_prefix { "@sv-property:" };
    if (actual->kind == semantic::sv::ExpressionKind::class_property
        || actual->text.starts_with(property_prefix)) {
        if (actual->operands.size() > 1U)
            return std::nullopt;
        if (actual->operands.size() == 1U) {
            const auto* receiver = expression(actual->operands.front());
            if (receiver == nullptr
                || receiver->kind != semantic::sv::ExpressionKind::name
                || receiver->text != "this") {
                return std::nullopt;
            }
        }
        const auto* property = instance_property(
            handle, marker_payload(*actual, property_prefix));
        if (property == nullptr) {
            return std::nullopt;
        }
        const auto identity = property->canonical_identity;
        VariableReference reference;
        reference.string_value = is_string(property->type);
        if (reference.string_value) {
            if (!read_string_property_ || !write_string_property_)
                return std::nullopt;
            reference.read_string = [this, handle, identity] {
                return read_string_property_(handle, identity);
            };
            reference.write_string = [this, handle, identity](
                                         const std::string_view value) {
                return write_string_property_(handle, identity, value);
            };
        } else {
            if (!read_property_ || !write_property_)
                return std::nullopt;
            reference.read_packed = [this, handle, identity] {
                return read_property_(handle, identity);
            };
            reference.write_packed = [this, handle, identity](
                                         const runtime::PackedLogic4& value) {
                write_property_(handle, identity, value);
                return true;
            };
        }
        return reference;
    }
    if (actual->kind != semantic::sv::ExpressionKind::name
        || !actual->referenced_name
        || !actual->referenced_name->selected) {
        return std::nullopt;
    }
    const auto declaration_id = *actual->referenced_name->selected;
    if (const auto* reference = active_reference(declaration_id))
        return *reference;
    const auto* selected = declaration(declaration_id);
    if (selected == nullptr || !selected->type)
        return std::nullopt;
    if (is_package_global(declaration_id)) {
        VariableReference reference;
        reference.string_value = is_string(*selected->type);
        if (reference.string_value) {
            if (!read_package_string_ || !write_package_string_)
                return std::nullopt;
            if (!read_package_string_(declaration_id))
                return std::nullopt;
            reference.read_string = [this, declaration_id] {
                return read_package_string_(declaration_id);
            };
            reference.write_string = [this, declaration_id](
                                         const std::string_view value) {
                return write_package_string_(declaration_id, value);
            };
        } else {
            if (!read_package_packed_ || !write_package_packed_)
                return std::nullopt;
            if (!read_package_packed_(declaration_id))
                return std::nullopt;
            reference.read_packed = [this, declaration_id] {
                return read_package_packed_(declaration_id);
            };
            reference.write_packed = [this, declaration_id](
                                         const runtime::PackedLogic4& value) {
                return write_package_packed_(declaration_id, value);
            };
        }
        return reference;
    }
    if (selected->form != semantic::sv::DeclarationForm::variable)
        return std::nullopt;
    VariableReference reference;
    const auto name = selected->name;
    if (is_string(*selected->type)) {
        const auto found = string_environment.find(name);
        auto* bindings = string_bindings_;
        if (found != string_environment.end() && bindings != nullptr
            && bindings->contains(declaration_id.value())) {
            reference.string_value = true;
            reference.read_string = [bindings, declaration_id]()
                -> std::optional<std::string> {
                const auto current = bindings->find(declaration_id.value());
                return current == bindings->end()
                    ? std::nullopt : std::optional { current->second };
            };
            reference.write_string = [bindings, &string_environment, name,
                                         declaration_id](
                                         const std::string_view value) {
                const auto current = string_environment.find(name);
                const auto binding = bindings->find(declaration_id.value());
                if (current == string_environment.end()
                    || binding == bindings->end())
                    return false;
                current->second = value;
                binding->second = value;
                return true;
            };
            return reference;
        }
        const auto* property = instance_property(handle, declaration_id);
        if (property == nullptr || !is_string(property->type)
            || !read_string_property_ || !write_string_property_
            || !read_string_property_(handle, property->canonical_identity)) {
            return std::nullopt;
        }
        const auto identity = property->canonical_identity;
        reference.string_value = true;
        reference.read_string = [this, handle, identity] {
            return read_string_property_(handle, identity);
        };
        reference.write_string = [this, handle, identity](
                                     const std::string_view value) {
            return write_string_property_(handle, identity, value);
        };
        return reference;
    }
    const auto* property = instance_property(handle, declaration_id);
    if (property != nullptr && !is_string(property->type)
        && read_property_ && write_property_) {
        const auto identity = property->canonical_identity;
        reference.read_packed = [this, handle, identity] {
            return read_property_(handle, identity);
        };
        reference.write_packed = [this, handle, identity](
                                     const runtime::PackedLogic4& value) {
            write_property_(handle, identity, value);
            return true;
        };
        return reference;
    }
    const auto found = environment.find(name);
    if (found == environment.end())
        return std::nullopt;
    reference.read_packed = [&environment, name]()
        -> std::optional<runtime::PackedLogic4> {
        const auto current = environment.find(name);
        return current == environment.end()
            ? std::nullopt : std::optional { current->second };
    };
    reference.write_packed = [&environment, name](
                                 const runtime::PackedLogic4& value) {
        const auto current = environment.find(name);
        if (current == environment.end())
            return false;
        current->second = resize_packed(value, current->second.width());
        return true;
    };
    return reference;
}

std::vector<std::size_t>
SystemVerilogClassHirExecution::map_actual_formals(
    const semantic::sv::SpecializedClassMethod& method,
    const std::span<const std::string> actual_names,
    const std::size_t actual_count) const
{
    if ((!actual_names.empty() && actual_names.size() != actual_count)
        || actual_count > method.formals.size()) {
        throw std::invalid_argument {
            "class method actual names do not align with values"
        };
    }
    std::vector<std::size_t> result;
    result.reserve(actual_count);
    std::vector<bool> assigned(method.formals.size());
    std::size_t next_positional { };
    for (std::size_t index = 0U; index < actual_count; ++index) {
        const std::string_view name = actual_names.empty()
            ? std::string_view { }
            : std::string_view { actual_names[index] };
        std::size_t formal = method.formals.size();
        if (name.empty()) {
            while (next_positional < assigned.size()
                && assigned[next_positional]) {
                ++next_positional;
            }
            formal = next_positional;
        } else {
            for (std::size_t candidate = 0U;
                 candidate < method.formals.size(); ++candidate) {
                const auto* formal_declaration
                    = declaration(method.formals[candidate]);
                if (formal_declaration != nullptr
                    && formal_declaration->name == name) {
                    formal = candidate;
                    break;
                }
            }
        }
        if (formal >= method.formals.size() || assigned[formal]) {
            throw std::invalid_argument {
                "class method actual cannot select a unique HIR formal"
            };
        }
        assigned[formal] = true;
        result.push_back(formal);
    }
    return result;
}

bool SystemVerilogClassHirExecution::push_package_queue_back(
    const semantic::ExpressionId receiver_id,
    const semantic::ExpressionId actual_id,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& string_environment)
{
    const auto* receiver = expression(receiver_id);
    if (receiver == nullptr
        || receiver->kind != semantic::sv::ExpressionKind::name
        || !receiver->referenced_name
        || !receiver->referenced_name->selected
        || !is_package_global(*receiver->referenced_name->selected)
        || !read_package_container_ || !write_package_container_) {
        return false;
    }
    const auto declaration_id = *receiver->referenced_name->selected;
    const auto initial = read_package_container_(declaration_id);
    if (!initial || !initial->type.queue)
        return false;
    const auto type = initial->type;

    std::optional<std::string> text;
    std::optional<runtime::PackedLogic4> packed;
    if (type.element_kind == runtime::simir::ContainerElementKind::String) {
        text = evaluate_string(actual_id, string_environment, handle);
        if (!text)
            return false;
    } else if (type.element_kind
            == runtime::simir::ContainerElementKind::Packed
        || type.element_kind
            == runtime::simir::ContainerElementKind::Scalar) {
        packed = evaluate_packed(actual_id, handle, environment);
        if (!packed)
            return false;
    } else {
        return false;
    }

    // Evaluate the argument before taking the value that will be deposited:
    // an argument can call code that mutates this same package queue.
    auto contents = read_package_container_(declaration_id);
    if (!contents || contents->type != type)
        return false;
    const auto size = runtime::simir::container_value_size(*contents);
    if (type.maximum_elements && size >= *type.maximum_elements)
        return true;
    if (size >= runtime::simir::maximum_container_elements(type)) {
        throw std::length_error {
            "queue exceeds the per-container owning-storage budget"
        };
    }
    if (text) {
        contents->string_elements.push_back(std::move(*text));
    } else {
        contents->elements.push_back(
            resize_packed(*packed, type.element_width));
    }
    return write_package_container_(declaration_id, std::move(*contents));
}

std::size_t SystemVerilogClassHirExecution::width(
    const semantic::sv::TypeReference& type,
    const std::size_t fallback) const noexcept
{
    if (!type.executable_width || *type.executable_width == 0U
        || *type.executable_width
            > std::numeric_limits<std::size_t>::max()) {
        return fallback;
    }
    return static_cast<std::size_t>(*type.executable_width);
}

bool SystemVerilogClassHirExecution::is_string(
    const semantic::sv::TypeReference& type) noexcept
{
    return type.value_form == semantic::sv::TypeForm::string;
}

bool SystemVerilogClassHirExecution::is_input(
    const semantic::sv::Direction direction) noexcept
{
    return direction == semantic::sv::Direction::input
        || direction == semantic::sv::Direction::unknown;
}

std::uint8_t SystemVerilogClassHirExecution::direction_code(
    const semantic::sv::Direction direction) noexcept
{
    return static_cast<std::uint8_t>(direction);
}

runtime::PackedLogic4 SystemVerilogClassHirExecution::resize_packed(
    const runtime::PackedLogic4& value, const std::size_t width)
{
    if (value.width() == width)
        return value;
    auto result = runtime::PackedLogic4(width, runtime::Logic4::zero);
    for (std::size_t bit = 0U; bit < std::min(width, value.width()); ++bit) {
        if (value.is_logic9())
            result.set_logic9(bit, value.get_logic9(bit));
        else
            result.set(bit, value.get(bit));
    }
    return result;
}

const semantic::sv::SpecializedClassMethod*
SystemVerilogClassHirExecution::method(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view canonical_identity,
    const bool virtual_dispatch) const
{
    const auto& object = heap_.object(handle);
    auto current = specialization(object.specialization_identity);
    const semantic::sv::SpecializedClassMethod* requested { };
    while (current != nullptr) {
        const auto found = std::ranges::find(
            current->methods, canonical_identity,
            &semantic::sv::SpecializedClassMethod::canonical_identity);
        if (found != current->methods.end()) {
            requested = &*found;
            break;
        }
        current = current->base
            ? specialization(current->base->specialization_identity)
            : nullptr;
    }
    if (requested == nullptr && virtual_dispatch) {
        // A method of an implemented interface class (IEEE 1800-2017 8.26)
        // is not inherited through the base chain; the object's class
        // provides it under the same name.
        const auto separator = canonical_identity.rfind("::");
        const auto name = separator == std::string_view::npos
            ? canonical_identity
            : canonical_identity.substr(separator + 2U);
        current = specialization(object.specialization_identity);
        while (current != nullptr && requested == nullptr) {
            for (const auto& candidate : current->methods) {
                if (candidate.name == name && !candidate.pure
                    && !candidate.static_method) {
                    requested = &candidate;
                    break;
                }
            }
            current = current->base
                ? specialization(current->base->specialization_identity)
                : nullptr;
        }
        return requested;
    }
    if (requested == nullptr)
        return nullptr;
    // A pure virtual prototype is reached only through its override.
    if (!virtual_dispatch || !requested->virtual_slot)
        return requested->pure ? nullptr : requested;
    const semantic::sv::SpecializedClassMethod* selected = requested;
    current = specialization(object.specialization_identity);
    while (current != nullptr) {
        const auto override = std::ranges::find(
            current->methods, requested->virtual_slot,
            &semantic::sv::SpecializedClassMethod::virtual_slot);
        if (override != current->methods.end()) {
            if (override->declared_profile_identity
                != requested->declared_profile_identity) {
                throw std::invalid_argument {
                    "virtual HIR class method profile does not match its slot"
                };
            }
            selected = &*override;
            break;
        }
        current = current->base
            ? specialization(current->base->specialization_identity)
            : nullptr;
    }
    return selected->pure ? nullptr : selected;
}

const semantic::sv::SpecializedClassMethod*
SystemVerilogClassHirExecution::method_named(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view name) const
{
    const auto& object = heap_.object(handle);
    auto current = specialization(object.specialization_identity);
    const semantic::sv::SpecializedClassMethod* selected { };
    while (current != nullptr) {
        const auto found = std::ranges::find(
            current->methods, name,
            &semantic::sv::SpecializedClassMethod::name);
        if (found != current->methods.end()) {
            selected = &*found;
            break;
        }
        current = current->base
            ? specialization(current->base->specialization_identity)
            : nullptr;
    }
    return selected == nullptr
        ? nullptr
        : method(handle, selected->canonical_identity, true);
}

const semantic::sv::SpecializedClassMethod*
SystemVerilogClassHirExecution::static_method(
    const std::string_view canonical_identity) const
{
    const semantic::sv::SpecializedClassMethod* selected { };
    for (const auto& specialization : specializations_) {
        const auto found = std::ranges::find(
            specialization.methods, canonical_identity,
            &semantic::sv::SpecializedClassMethod::canonical_identity);
        if (found == specialization.methods.end())
            continue;
        if (selected != nullptr
            && (selected->declaration != found->declaration
                || selected->owner_identity != found->owner_identity
                || selected->callable_identity != found->callable_identity
                || selected->formals != found->formals
                || selected->local_declarations != found->local_declarations
                || selected->statements != found->statements
                || selected->kind != found->kind
                || selected->static_method != found->static_method
                || selected->pure != found->pure
                || selected->defined != found->defined)) {
            return nullptr;
        }
        selected = &*found;
    }
    return selected != nullptr && selected->static_method && !selected->pure
        ? selected
        : nullptr;
}

std::optional<std::string>
SystemVerilogClassHirExecution::evaluate_string(
    const semantic::ExpressionId id,
    const StringEnvironment& environment) const
{
    const auto* value = expression(id);
    if (value == nullptr)
        return std::nullopt;
    if (value->kind == semantic::sv::ExpressionKind::string_literal)
        return value->decoded_string.value_or(value->text);
    if (value->kind == semantic::sv::ExpressionKind::name) {
        if (value->referenced_name
            && value->referenced_name->selected) {
            const auto selected = *value->referenced_name->selected;
            if (const auto* reference = active_reference(selected)) {
                return reference->string_value && reference->read_string
                    ? reference->read_string() : std::nullopt;
            }
            if (is_package_global(selected)) {
                return read_package_string_
                    ? read_package_string_(selected) : std::nullopt;
            }
            if (is_package_constant(selected)) {
                const auto* constant = declaration(selected);
                if (constant == nullptr
                    || (constant->form
                            != semantic::sv::DeclarationForm::parameter
                        && constant->form
                            != semantic::sv::DeclarationForm::local_parameter)
                    || !constant->initializer) {
                    return std::nullopt;
                }
                const auto evaluate_package_string_constant = [this](
                    auto&& self,
                    const semantic::ExpressionId expression_id,
                    std::vector<semantic::DeclarationId>& active_constants,
                    const std::size_t depth)
                    -> std::optional<std::string> {
                    if (depth >= 64U) {
                        return std::nullopt;
                    }
                    const auto* initializer = expression(expression_id);
                    if (initializer == nullptr) {
                        return std::nullopt;
                    }
                    if (initializer->kind
                        == semantic::sv::ExpressionKind::string_literal) {
                        return initializer->decoded_string.value_or(
                            initializer->text);
                    }
                    if (initializer->kind
                            == semantic::sv::ExpressionKind::concatenation
                        && !initializer->operands.empty()) {
                        std::string result;
                        for (const auto operand : initializer->operands) {
                            const auto part = self(self, operand,
                                active_constants, depth + 1U);
                            if (!part) {
                                return std::nullopt;
                            }
                            result += *part;
                        }
                        return result;
                    }
                    if (initializer->kind
                            != semantic::sv::ExpressionKind::name
                        || !initializer->referenced_name
                        || !initializer->referenced_name->selected) {
                        return std::nullopt;
                    }
                    const auto dependency
                        = *initializer->referenced_name->selected;
                    const auto* dependency_declaration
                        = declaration(dependency);
                    if (!is_package_constant(dependency)
                        || dependency_declaration == nullptr
                        || (dependency_declaration->form
                                != semantic::sv::DeclarationForm::parameter
                            && dependency_declaration->form
                                != semantic::sv::DeclarationForm::local_parameter)
                        || !dependency_declaration->initializer
                        || std::ranges::find(
                               active_constants, dependency)
                            != active_constants.end()) {
                        return std::nullopt;
                    }
                    active_constants.push_back(dependency);
                    const auto result = self(self,
                        *dependency_declaration->initializer,
                        active_constants, depth + 1U);
                    active_constants.pop_back();
                    return result;
                };
                std::vector<semantic::DeclarationId> active_constants {
                    selected
                };
                return evaluate_package_string_constant(
                    evaluate_package_string_constant,
                    *constant->initializer, active_constants, 0U);
            }
            if (string_bindings_ != nullptr) {
                const auto found = string_bindings_->find(selected.value());
                if (found != string_bindings_->end())
                    return found->second;
            }
            // A selected declaration must be read by its declaration ID.
            // Falling through to the spelling-keyed frame could return a
            // same-named local or formal from another lexical scope.
            return std::nullopt;
        }
        const auto found = environment.find(value->text);
        if (found != environment.end())
            return found->second;
    }
    return std::nullopt;
}

std::optional<std::string>
SystemVerilogClassHirExecution::evaluate_string(
    const semantic::ExpressionId id,
    StringEnvironment& environment,
    const runtime::SystemVerilogClassHandle handle)
{
    if (packed_environment_ != nullptr) {
        if (auto aggregate = evaluate_bound_call_value(
                id, handle, *packed_environment_);
            aggregate && aggregate->kind == CallValue::Kind::string) {
            return aggregate->text;
        }
    }
    if (auto value = evaluate_string(id, environment)) {
        return value;
    }
    const auto* value = expression(id);
    if (value != nullptr
        && value->kind == semantic::sv::ExpressionKind::name
        && value->referenced_name
        && value->referenced_name->selected) {
        const auto* property = instance_property(
            handle, *value->referenced_name->selected);
        return property != nullptr && is_string(property->type)
                && read_string_property_
            ? read_string_property_(handle, property->canonical_identity)
            : std::nullopt;
    }
    constexpr std::string_view property_prefix { "@sv-property:" };
    if (value != nullptr && read_string_property_
        && (value->kind == semantic::sv::ExpressionKind::class_property
            || value->text.starts_with(property_prefix))
        && value->operands.size() <= 1U) {
        auto owner = handle;
        if (value->operands.size() == 1U) {
            // Only `this` is evaluated here; other receivers need the
            // packed evaluator's environment.
            const auto* receiver = expression(value->operands.front());
            if (receiver == nullptr
                || receiver->kind != semantic::sv::ExpressionKind::name
                || receiver->text != "this") {
                return std::nullopt;
            }
        }
        const auto* property = instance_property(
            owner, marker_payload(*value, property_prefix));
        return property != nullptr && is_string(property->type)
            ? read_string_property_(owner, property->canonical_identity)
            : std::nullopt;
    }
    if (value != nullptr
        && value->kind == semantic::sv::ExpressionKind::call
        && value->text == "?:" && value->operands.size() == 3U
        && packed_environment_ != nullptr) {
        return evaluate_returned_string(
            id, handle, *packed_environment_, environment);
    }
    // `{a, "x"}`: a concatenation of strings (IEEE 1800-2017 11.4.12.2).
    if (value != nullptr
        && value->kind == semantic::sv::ExpressionKind::concatenation
        && !value->operands.empty()) {
        std::string text;
        for (const auto operand : value->operands) {
            const auto part = evaluate_string(operand, environment, handle);
            if (!part) {
                return std::nullopt;
            }
            text += *part;
        }
        return text;
    }
    if (value != nullptr
        && (value->kind == semantic::sv::ExpressionKind::call
            || value->kind == semantic::sv::ExpressionKind::class_method_call
            || value->kind == semantic::sv::ExpressionKind::class_static_method_call
            || value->text.starts_with("@sv-method:")
            || value->text.starts_with("@sv-base-method:")
            || value->text.starts_with("@sv-static-method:"))) {
        return evaluate_string_call(id, handle, environment);
    }
    return std::nullopt;
}

std::optional<runtime::PackedLogic4>
SystemVerilogClassHirExecution::invoke_package_function(
    const semantic::sv::Expression& call,
    const runtime::SystemVerilogClassHandle caller_handle,
    PackedEnvironment& environment,
    StringEnvironment& string_environment,
    std::optional<std::string>* const string_result)
{
    if (call.kind != semantic::sv::ExpressionKind::call
        || !call.referenced_name || !call.referenced_name->selected) {
        return std::nullopt;
    }
    const auto* selected = declaration(*call.referenced_name->selected);
    if (selected == nullptr
        || selected->form != semantic::sv::DeclarationForm::function
        || !selected->callable || !selected->callable->function) {
        return std::nullopt;
    }
    const semantic::sv::Unit* package { };
    const auto& packages = hir_index().packages_by_declaration;
    if (const auto found = packages.find(selected->id.value());
        found != packages.end()) {
        for (const auto* unit : found->second) {
            if (unit->scope != selected->scope) {
                continue;
            }
            if (package != nullptr) {
                return std::nullopt;
            }
            package = unit;
        }
    }
    if (package == nullptr) {
        return std::nullopt;
    }

    semantic::sv::SpecializedClassMethod method;
    method.declaration = selected->id;
    method.name = selected->name;
    method.canonical_identity = package->name + "::" + selected->name;
    method.owner_identity = package->name;
    method.kind = semantic::sv::ClassMethodKind::function;
    method.return_type = selected->callable->return_type;
    method.formals = selected->callable->formals;
    method.local_declarations = selected->children;
    method.statements = selected->statements;
    method.defined = true;
    method.formal_types.reserve(method.formals.size());
    for (const auto formal_id : method.formals) {
        const auto* formal = declaration(formal_id);
        if (formal == nullptr || !formal->type) {
            return std::nullopt;
        }
        method.formal_types.push_back(*formal->type);
    }
    const bool returns_string
        = method.return_type && is_string(*method.return_type);
    if (returns_string && string_result == nullptr)
        return std::nullopt;
    if (returns_string && std::ranges::any_of(
            method.local_declarations, [this](const auto local_id) {
                const auto* local = declaration(local_id);
                return local != nullptr && local->type
                    && local->lifetime
                        == semantic::sv::Lifetime::static_lifetime
                    && is_string(*local->type);
            })) {
        return std::nullopt;
    }

    std::vector<semantic::ExpressionId> actual_expressions;
    std::vector<std::string> actual_names;
    actual_expressions.reserve(call.operands.size());
    actual_names.reserve(call.operands.size());
    if (!call.call_arguments.empty()) {
        std::vector<bool> associated(method.formals.size());
        std::size_t next_positional { };
        for (const auto& association : call.call_arguments) {
            std::size_t formal_index = method.formals.size();
            const std::string_view formal_name = association.formal
                ? std::string_view { *association.formal }
                : std::string_view { };
            if (!formal_name.empty()) {
                for (std::size_t index = 0U;
                     index < method.formals.size(); ++index) {
                    const auto* formal = declaration(method.formals[index]);
                    if (formal != nullptr && formal->name == formal_name) {
                        formal_index = index;
                        break;
                    }
                }
            } else {
                while (next_positional < associated.size()
                    && associated[next_positional]) {
                    ++next_positional;
                }
                formal_index = next_positional;
                if (next_positional < associated.size())
                    ++next_positional;
            }
            if (formal_index >= method.formals.size()
                || associated[formal_index]) {
                throw std::invalid_argument {
                    "package call association cannot select a unique HIR formal"
                };
            }
            associated[formal_index] = true;
            if (!association.actual)
                continue;
            const auto* formal = declaration(method.formals[formal_index]);
            if (formal == nullptr)
                return std::nullopt;
            actual_expressions.push_back(*association.actual);
            // Normalize positional slots to formal names so a preceding
            // explicit omission cannot shift a later actual in the frame.
            actual_names.push_back(formal->name);
        }
    } else {
        for (std::size_t index = 0U; index < call.operands.size(); ++index) {
            actual_expressions.push_back(call.operands[index]);
            if (call.argument_names.size() == call.operands.size()) {
                actual_names.push_back(call.argument_names[index]);
            } else {
                actual_names.emplace_back();
            }
        }
    }
    const auto actual_formal_indices = map_actual_formals(
        method, actual_names, actual_expressions.size());
    std::vector<runtime::PackedLogic4> actuals;
    std::vector<std::string> string_actuals;
    std::vector<std::optional<VariableReference>> actual_references;
    actuals.reserve(call.operands.size());
    string_actuals.reserve(call.operands.size());
    actual_references.reserve(actual_expressions.size());
    for (std::size_t index = 0U; index < actual_expressions.size(); ++index) {
        const auto* formal = declaration(
            method.formals[actual_formal_indices[index]]);
        if (formal == nullptr || !formal->type)
            return std::nullopt;
        const auto reference = is_input(formal->direction)
            ? std::optional<VariableReference> { }
            : variable_reference(actual_expressions[index], caller_handle,
                  environment, string_environment);
        if (formal->direction == semantic::sv::Direction::ref
            || formal->direction == semantic::sv::Direction::output
            || formal->direction == semantic::sv::Direction::inout) {
            if (!reference || reference->string_value
                    != is_string(*formal->type)) {
                throw std::invalid_argument {
                    "package function output actual is not an exact writable variable"
                };
            }
        }
        actual_references.push_back(reference);
        if (formal->direction == semantic::sv::Direction::output) {
            actuals.emplace_back(width(*formal->type), runtime::Logic4::x);
            string_actuals.emplace_back();
            continue;
        }
        if (is_string(*formal->type)) {
            const auto text = evaluate_string(
                actual_expressions[index], string_environment, caller_handle);
            if (!text)
                return std::nullopt;
            actuals.emplace_back(64U, runtime::Logic4::zero);
            string_actuals.push_back(*text);
            continue;
        }
        const auto packed = evaluate_packed(
            actual_expressions[index], caller_handle, environment,
            is_input(formal->direction)
                ? std::optional<std::size_t> {
                      width(*formal->type, 0U) }
                : std::nullopt);
        if (!packed)
            return std::nullopt;
        actuals.push_back(*packed);
        string_actuals.emplace_back();
    }
    auto invocation_names = actual_names;
    if (returns_string) {
        actuals.emplace_back(64U, runtime::Logic4::zero);
        string_actuals.emplace_back();
        invocation_names.emplace_back("@result-string");
    }
    std::vector<std::size_t> bound_actual_formals;
    const auto result = invoke_function_impl(method, 0U, actuals,
        string_actuals, invocation_names, { }, actual_references,
        &bound_actual_formals);
    if (returns_string) {
        if (string_actuals.empty())
            throw std::logic_error {
                "package string result channel was not returned"
            };
        *string_result = string_actuals.back();
        string_actuals.pop_back();
        actuals.pop_back();
    }
    for (std::size_t index = 0U; index < actuals.size(); ++index) {
        const auto* formal = declaration(
            method.formals[bound_actual_formals[index]]);
        if (formal == nullptr)
            throw std::invalid_argument {
                "package function formal disappeared during copy-out"
            };
        if (formal->direction == semantic::sv::Direction::ref
            || is_input(formal->direction)) {
            continue;
        }
        const auto& reference = actual_references[index];
        if (!reference) {
            throw std::invalid_argument {
                "package function output actual lost its exact variable identity"
            };
        }
        const bool written = reference->string_value
            ? reference->write_string
                && reference->write_string(string_actuals[index])
            : reference->write_packed
                && reference->write_packed(actuals[index]);
        if (!written) {
            throw std::invalid_argument {
                "package function output actual could not be written back"
            };
        }
    }
    return result;
}

std::optional<runtime::PackedLogic4>
SystemVerilogClassHirExecution::evaluate_class_array_query(
    const semantic::ExpressionId array_id,
    const std::string_view query,
    const std::size_t dimension,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment)
{
    using semantic::sv::ExpressionKind;
    using semantic::sv::TypeForm;
    const auto unknown_query = [] {
        return std::optional<runtime::PackedLogic4> {
            runtime::PackedLogic4(32U, runtime::Logic4::x)
        };
    };

    // Typed aggregate input actuals are carried in a class-call frame rather
    // than the packed formal environment. Array-query calls on an open-array
    // formal therefore read the bound aggregate snapshot directly.
    const auto* queried_expression = expression(array_id);
    if (queried_expression != nullptr
        && queried_expression->kind == ExpressionKind::name) {
        auto actual = evaluate_bound_call_value(array_id, handle, environment);
        if (actual && actual->kind == CallValue::Kind::array) {
            const auto* actual_definition
                = call_aggregate_definition(actual->type);
            if (actual->type.container_form == TypeForm::associative_array
                || (actual_definition != nullptr
                    && actual_definition->form
                        == TypeForm::associative_array)) {
                return std::nullopt;
            }
            const auto size = actual->elements.size();
            if (size == 0U) {
                if (actual->left != 0 || actual->right != -1)
                    return std::nullopt;
            } else {
                const auto distance
                    = support::index_distance(actual->left, actual->right);
                if (distance == std::numeric_limits<std::uint64_t>::max()
                    || !std::in_range<std::size_t>(distance + 1U)
                    || static_cast<std::size_t>(distance + 1U) != size) {
                    return std::nullopt;
                }
            }
            if (dimension == 0U)
                return unknown_query();

            struct QueryDimension {
                std::optional<std::int64_t> left;
                std::optional<std::int64_t> right;
            };
            std::vector<QueryDimension> dimensions {
                QueryDimension { actual->left, actual->right }
            };
            const auto range_bound = [&](const auto& literal, const auto& id) {
                return evaluate_range_bound(literal, id, handle, environment);
            };
            const auto append_range = [&](const semantic::sv::PackedRange& range) {
                dimensions.push_back({
                    range_bound(range.left, range.left_expression),
                    range_bound(range.right, range.right_expression)
                });
            };
            struct ResolvedQueryType {
                const semantic::sv::TypeDefinition* definition { };
                semantic::sv::TypeReference terminal;
            };
            const auto resolve_query_type = [this](
                                                   const semantic::sv::TypeReference& type)
                -> std::optional<ResolvedQueryType> {
                auto current = type;
                std::vector<semantic::TypeId> alias_path;
                for (std::size_t depth { }; depth < 64U; ++depth) {
                    if (!current.target.target.valid()) {
                        return ResolvedQueryType { nullptr,
                            std::move(current) };
                    }
                    if (std::ranges::find(alias_path,
                            current.target.target) != alias_path.end()) {
                        return std::nullopt;
                    }
                    alias_path.push_back(current.target.target);
                    const auto* found = type_definition(current.target.target);
                    if (found == nullptr)
                        return std::nullopt;
                    if (found->form != TypeForm::alias) {
                        return ResolvedQueryType { found,
                            std::move(current) };
                    }
                    current = found->base;
                }
                return std::nullopt;
            };
            const auto append_type_dimensions = [&](auto&& self,
                                                        const semantic::sv::TypeReference& type,
                                                        const bool captured_outer,
                                                        const std::size_t depth) -> bool {
                if (depth >= 64U)
                    return false;
                const auto resolved = resolve_query_type(type);
                if (!resolved)
                    return false;
                const auto* definition = resolved->definition;

                auto form = type.container_form;
                if (!form && definition != nullptr
                    && (definition->form == TypeForm::static_array
                        || definition->form == TypeForm::dynamic_array
                        || definition->form == TypeForm::queue
                        || definition->form == TypeForm::associative_array)) {
                    form = definition->form;
                }
                if (form == TypeForm::associative_array)
                    return false;

                auto ranges = type.unpacked_dimensions;
                if (ranges.empty() && form == TypeForm::static_array
                    && definition != nullptr && definition->container) {
                    ranges = definition->container->static_dimensions;
                }
                const bool array_type = form == TypeForm::static_array
                    || form == TypeForm::dynamic_array
                    || form == TypeForm::queue
                    || !ranges.empty();
                if (array_type) {
                    std::size_t range_index { };
                    if (form == TypeForm::dynamic_array
                        || form == TypeForm::queue) {
                        if (!captured_outer)
                            dimensions.push_back({ });
                    } else if (ranges.empty()) {
                        if (!captured_outer)
                            dimensions.push_back({ });
                    } else if (captured_outer) {
                        range_index = 1U;
                    }
                    for (; range_index < ranges.size(); ++range_index)
                        append_range(ranges[range_index]);

                    // The parser copies a named element TypeReference from an
                    // unpacked array declaration and clears only its container.
                    // Consequently, outer and element references can share a
                    // TypeId. The packed shape belongs to the scalar element,
                    // so collect it at that leaf rather than counting the
                    // same typedef once at both array and element levels.
                    const auto element = call_array_element_type(type);
                    return element
                        && self(self, *element, false, depth + 1U);
                }

                const bool unpacked_aggregate = definition != nullptr
                    ? definition->form == TypeForm::unpacked_structure
                        || definition->form == TypeForm::unpacked_union
                    : resolved->terminal.value_form
                            == TypeForm::unpacked_structure
                        || resolved->terminal.value_form
                            == TypeForm::unpacked_union;
                if (unpacked_aggregate) {
                    // An unpacked aggregate's base describes its member-type
                    // representation. It is not a packed dimension of the
                    // aggregate object, even when that base has a packed range.
                    return true;
                }
                const auto has_packed_shape = [](
                    const semantic::sv::TypeReference& reference) {
                    return !reference.packed_dimensions.empty()
                        || reference.packed_range.has_value();
                };
                const semantic::sv::TypeReference* packed_shape = nullptr;
                if (has_packed_shape(type)) {
                    packed_shape = &type;
                } else if (has_packed_shape(resolved->terminal)) {
                    // A typedef can terminate in an inline builtin reference
                    // (for example `typedef logic [1:0][7:0] word_t`). Such a
                    // terminal has no TypeId or TypeDefinition, but its shape
                    // is still the authoritative packed-dimension source.
                    packed_shape = &resolved->terminal;
                } else if (definition != nullptr
                    && has_packed_shape(definition->base)) {
                    packed_shape = &definition->base;
                }
                if (packed_shape != nullptr) {
                    if (!packed_shape->packed_dimensions.empty()) {
                        for (const auto& range : packed_shape->packed_dimensions)
                            append_range(range);
                    } else {
                        append_range(*packed_shape->packed_range);
                    }
                }
                return true;
            };
            if (!append_type_dimensions(append_type_dimensions,
                    actual->type, true, 0U)) {
                return std::nullopt;
            }
            if (dimension == 0U || dimension > dimensions.size())
                return unknown_query();
            const auto& selected = dimensions[dimension - 1U];
            if (!selected.left || !selected.right)
                return std::nullopt;
            const auto left = *selected.left;
            const auto right = *selected.right;
            const auto difference = support::index_distance(left, right);
            if (difference == std::numeric_limits<std::uint64_t>::max())
                return std::nullopt;
            const auto dimension_size = dimension == 1U
                ? size : difference + 1U;

            const auto signed_answer = [](const std::int64_t answer) {
                return runtime::PackedLogic4::from_aval_bval(
                    32U, static_cast<std::uint32_t>(answer), 0U);
            };
            if (query == "$size") {
                if (!std::in_range<std::uint32_t>(dimension_size))
                    return std::nullopt;
                return runtime::PackedLogic4::from_aval_bval(
                    32U, static_cast<std::uint32_t>(dimension_size), 0U);
            }
            if (query == "$left")
                return signed_answer(left);
            if (query == "$right")
                return signed_answer(right);
            const auto increment = dimension_size == 0U
                ? std::int64_t { -1 }
                : left >= right ? std::int64_t { 1 }
                                : std::int64_t { -1 };
            if (query == "$low")
                return signed_answer(increment == 1 ? right : left);
            if (query == "$high")
                return signed_answer(increment == 1 ? left : right);
            if (query == "$increment")
                return signed_answer(increment);
        }
    }

    constexpr std::string_view container_property_prefix {
        "@sv-container-property:"
    };
    constexpr std::string_view property_prefix { "@sv-property:" };
    constexpr std::string_view selected_prefix { "@sv-select:" };
    constexpr std::string_view static_container_property_prefix {
        "@sv-static-container-property:"
    };
    constexpr std::string_view static_property_prefix {
        "@sv-static-property:"
    };
    constexpr std::string_view indexed_container_prefix {
        "@sv-container-index:"
    };
    struct QueryTarget {
        semantic::sv::TypeReference type;
        const runtime::SystemVerilogClassPropertyValue* storage { };
        bool direct_container_property { };
        std::optional<runtime::SystemVerilogClassPropertyValue>
            owned_storage;
    };

    const auto consume_index_type = [&](semantic::sv::TypeReference& type,
                                        const std::int64_t index,
                                        const runtime::SystemVerilogClassPropertyValue*
                                            storage) {
        std::vector<semantic::TypeId> alias_path;
        auto resolved = type;
        const semantic::sv::TypeDefinition* definition = nullptr;
        auto form = resolved.container_form;
        for (;;) {
            definition = resolved.target.target.valid()
                ? type_definition(resolved.target.target)
                : nullptr;
            if (form || !resolved.unpacked_dimensions.empty()
                || definition == nullptr
                || definition->form != TypeForm::alias) {
                break;
            }
            if (std::ranges::find(alias_path, definition->id)
                != alias_path.end()) {
                return false;
            }
            alias_path.push_back(definition->id);
            resolved = definition->base;
            form = resolved.container_form;
        }
        if (!form && !resolved.unpacked_dimensions.empty())
            form = TypeForm::static_array;
        if (!form && definition != nullptr)
            form = definition->form;
        auto ranges = resolved.unpacked_dimensions;
        if (ranges.empty() && form == TypeForm::static_array
            && definition != nullptr && definition->container) {
            ranges = definition->container->static_dimensions;
        }
        if (form == TypeForm::static_array) {
            if (ranges.empty())
                return false;
            const auto& range = ranges.front();
            const auto bound = [&](const auto& literal, const auto& id) {
                return evaluate_range_bound(literal, id, handle, environment);
            };
            const auto left = bound(range.left, range.left_expression);
            const auto right = bound(range.right, range.right_expression);
            if (!left || !right || index < std::min(*left, *right)
                || index > std::max(*left, *right)) {
                return false;
            }
            if (ranges.size() > 1U) {
                resolved.unpacked_dimensions = std::move(ranges);
                resolved.unpacked_dimensions.erase(
                    resolved.unpacked_dimensions.begin());
                resolved.container_form = TypeForm::static_array;
                type = std::move(resolved);
                return true;
            }
            if (resolved.container_element_types.size() == 1U) {
                type = resolved.container_element_types.front();
                return true;
            }
            if (definition != nullptr
                && definition->form == TypeForm::static_array) {
                type = definition->base;
                return true;
            }
            if (resolved.unpacked_dimensions.size() == 1U
                || resolved.container_form == TypeForm::static_array) {
                resolved.container_form.reset();
                resolved.unpacked_dimensions.clear();
                type = std::move(resolved);
                return true;
            }
            return false;
        }
        if (form == TypeForm::dynamic_array || form == TypeForm::queue) {
            const auto expected_kind = form == TypeForm::dynamic_array
                ? runtime::SystemVerilogClassContainerKind::DynamicArray
                : runtime::SystemVerilogClassContainerKind::Queue;
            if (storage == nullptr || !storage->handle_container
                || storage->handle_container->kind() != expected_kind
                || index < 0 || !std::in_range<std::size_t>(index)
                || static_cast<std::size_t>(index)
                    >= storage->handle_container->size()) {
                return false;
            }
            semantic::sv::TypeReference element_type;
            if (resolved.container_element_types.size() == 1U) {
                element_type = resolved.container_element_types.front();
            } else if (definition != nullptr
                && form && definition->form == *form) {
                element_type = definition->base;
            } else {
                return false;
            }
            if (ranges.empty()) {
                type = element_type;
            } else {
                type = semantic::sv::TypeReference { };
                type.container_form = TypeForm::static_array;
                type.unpacked_dimensions = std::move(ranges);
                type.container_element_types.push_back(element_type);
            }
            return true;
        }
        return false;
    };

    const auto target_for_expression = [&](auto&& self,
                                           const semantic::ExpressionId id,
                                           std::vector<semantic::ExpressionId>
                                               active)
        -> std::optional<QueryTarget> {
        if (std::ranges::find(active, id) != active.end())
            return std::nullopt;
        active.push_back(id);
        const auto* source = expression(id);
        if (source == nullptr)
            return std::nullopt;

        if (source->kind == ExpressionKind::index
            && source->operands.size() == 2U) {
            auto base = self(self, source->operands.front(), active);
            const auto index = evaluate_packed(
                source->operands.back(), handle, environment);
            const auto index_value = index
                ? index->known_signed_value()
                : std::nullopt;
            const auto* storage = base
                ? base->storage != nullptr
                    ? base->storage
                    : base->owned_storage ? &*base->owned_storage
                                          : nullptr
                : nullptr;
            if (!base || !index_value
                || !consume_index_type(base->type, *index_value, storage)) {
                return std::nullopt;
            }
            base->direct_container_property = false;
            return base;
        }

        if (source->kind == ExpressionKind::call
            && source->text.starts_with(indexed_container_prefix)) {
            if (source->operands.size() < 2U)
                return std::nullopt;
            const auto receiver = evaluate_packed(
                source->operands.front(), handle, environment);
            if (!receiver || receiver->low_word().bval != 0U)
                return std::nullopt;
            const auto owner = receiver->low_word().aval;
            const auto identity = marker_payload(
                *source, indexed_container_prefix);
            const auto* property = instance_property(owner, identity);
            if (property == nullptr)
                return std::nullopt;
            auto type = property->type;
            const auto& storage = heap_.property(
                owner, property->canonical_identity);
            for (const auto index_id :
                source->operands | std::views::drop(1)) {
                const auto index = evaluate_packed(
                    index_id, handle, environment);
                const auto index_value = index
                    ? index->known_signed_value()
                    : std::nullopt;
                if (!index_value
                    || !consume_index_type(type, *index_value, &storage)) {
                    return std::nullopt;
                }
            }
            return QueryTarget {
                std::move(type), nullptr, false, std::nullopt
            };
        }

        const bool static_container_property
            = source->text.starts_with(static_container_property_prefix);
        const bool static_property_expression
            = static_container_property
            || source->text.starts_with(static_property_prefix)
            || source->kind == ExpressionKind::class_static_property;
        if (static_property_expression) {
            const auto identity = marker_payload(
                *source,
                static_container_property
                    ? static_container_property_prefix
                    : static_property_prefix);
            const auto* property = static_class_property(
                specializations_, identity,
                source->referenced_name
                    ? source->referenced_name->selected
                    : std::nullopt);
            if (property == nullptr || !read_static_property_value_)
                return std::nullopt;
            auto storage = read_static_property_value_(
                property->canonical_identity);
            if (!storage)
                return std::nullopt;
            return QueryTarget {
                property->type,
                nullptr,
                true,
                std::move(storage)
            };
        }

        if (source->kind == ExpressionKind::call
            && source->text.starts_with(selected_prefix)
            && source->operands.size() == 1U) {
            auto base = self(self, source->operands.front(), active);
            if (!base)
                return std::nullopt;
            const auto selected_name
                = std::string_view { source->text }.substr(
                    selected_prefix.size());
            if (selected_name.empty()
                || selected_name.find_first_of(".:[]")
                    != std::string_view::npos) {
                return std::nullopt;
            }

            auto type = base->type;
            std::vector<semantic::TypeId> visited;
            for (;;) {
                if (!type.target.target.valid()
                    || std::ranges::find(visited, type.target.target)
                        != visited.end()) {
                    return std::nullopt;
                }
                visited.push_back(type.target.target);
                const auto* definition = type_definition(type.target.target);
                if (definition == nullptr)
                    return std::nullopt;
                if (definition->form == TypeForm::alias) {
                    type = definition->base;
                    continue;
                }
                if (definition->form != TypeForm::packed_structure
                    && definition->form != TypeForm::packed_union
                    && definition->form != TypeForm::unpacked_structure
                    && definition->form != TypeForm::unpacked_union
                    && definition->form != TypeForm::tagged_union) {
                    return std::nullopt;
                }
                const semantic::sv::PackedMember* selected_member { };
                for (const auto& member : definition->members) {
                    if (member.name != selected_name)
                        continue;
                    if (selected_member != nullptr)
                        return std::nullopt;
                    selected_member = &member;
                }
                if (selected_member == nullptr)
                    return std::nullopt;
                base->type = selected_member->type;
                base->direct_container_property = false;
                base->storage = nullptr;
                base->owned_storage.reset();
                return base;
            }
            return std::nullopt;
        }

        // In a class method, a value-container property is deliberately
        // retained as a selected name by class-expression resolution. Resolve
        // that exact declaration here so its declared unpacked dimensions
        // remain available to array-query calls such as $size(a, 3). This
        // path supplies type metadata only; it does not read container values
        // from the class heap.
        if (source->kind == ExpressionKind::name
            && source->referenced_name
            && source->referenced_name->selected) {
            const auto* selected_property = instance_property(
                handle, *source->referenced_name->selected);
            if (selected_property == nullptr
                || (!selected_property->type.container_form
                    && selected_property->type.unpacked_dimensions.empty()
                    && selected_property->type.container_element_types.empty())) {
                return std::nullopt;
            }
            return QueryTarget {
                selected_property->type, nullptr, false, std::nullopt
            };
        }

        const bool direct_container_property
            = source->text.starts_with(container_property_prefix);
        const bool instance_property_expression
            = direct_container_property
            || source->text.starts_with(property_prefix)
            || source->kind == ExpressionKind::class_property;
        if (!instance_property_expression)
            return std::nullopt;
        if (source->operands.size() > 1U)
            return std::nullopt;

        auto owner = handle;
        if (!source->operands.empty()) {
            const auto receiver = evaluate_packed(
                source->operands.front(), handle, environment);
            if (!receiver || receiver->low_word().bval != 0U)
                return std::nullopt;
            owner = receiver->low_word().aval;
        }
        if (owner == 0U || !heap_.contains(owner))
            return std::nullopt;

        const auto property_identity = direct_container_property
            ? std::string_view { source->text }.substr(
                  container_property_prefix.size())
            : marker_payload(*source, property_prefix);
        const auto* property = instance_property(owner, property_identity);
        if (property == nullptr)
            return std::nullopt;

        const auto& storage = heap_.property(
            owner, property->canonical_identity);
        return QueryTarget {
            property->type,
            &storage,
            true,
            std::nullopt
        };
    };

    const auto target = target_for_expression(
        target_for_expression, array_id, { });
    if (!target)
        return std::nullopt;
    if (dimension == 0U)
        return unknown_query();

    using IndexTypeReference
        = semantic::sv::AssociativeIndexTypeReference;
    struct QueryDimension {
        std::optional<semantic::sv::PackedRange> range;
        std::optional<TypeForm> variable_form;
        std::optional<IndexTypeReference> associative_index;
    };
    std::vector<QueryDimension> dimensions;
    std::vector<std::optional<semantic::sv::PackedRange>> unpacked_dimensions;
    semantic::sv::TypeReference leaf_type = target->type;
    const auto is_container_form = [](const TypeForm form) {
        return form == TypeForm::static_array
            || form == TypeForm::dynamic_array
            || form == TypeForm::queue
            || form == TypeForm::associative_array;
    };
    const auto integral_type = [&](const IndexTypeReference& reference) {
        return is_integral_associative_index(reference, hir_);
    };
    const auto bound = [&](const auto& literal, const auto& id) {
        return evaluate_range_bound(literal, id, handle, environment);
    };
    const auto extent = [&](const semantic::sv::PackedRange& range)
        -> std::optional<std::uint64_t> {
        const auto left = bound(range.left, range.left_expression);
        const auto right = bound(range.right, range.right_expression);
        if (!left || !right)
            return std::nullopt;
        const auto distance = support::index_distance(*left, *right);
        if (distance == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        return distance + 1U;
    };
    const auto packed_integral_width = [&](auto&& self,
                                           const semantic::sv::TypeReference& reference,
                                           std::vector<semantic::TypeId> visited)
        -> std::optional<std::uint64_t> {
        if (reference.executable_width && *reference.executable_width > 0U)
            return reference.executable_width;
        if (!reference.packed_dimensions.empty()) {
            std::uint64_t width { 1U };
            for (const auto& range : reference.packed_dimensions) {
                const auto range_width = extent(range);
                if (!range_width || *range_width == 0U
                    || *range_width
                        > std::numeric_limits<std::uint64_t>::max() / width) {
                    return std::nullopt;
                }
                width *= *range_width;
            }
            return width;
        }
        if (reference.packed_range) {
            return extent(*reference.packed_range);
        }
        const auto builtin = reference.target.spelling;
        if (builtin == "bit" || builtin == "logic" || builtin == "reg")
            return 1U;
        if (builtin == "byte")
            return 8U;
        if (builtin == "shortint")
            return 16U;
        if (builtin == "int" || builtin == "integer")
            return 32U;
        if (builtin == "longint" || builtin == "time")
            return 64U;

        const auto type_id = reference.target.target;
        if (!type_id.valid()
            || std::ranges::find(visited, type_id) != visited.end()) {
            return std::nullopt;
        }
        visited.push_back(type_id);
        const auto* definition = type_definition(type_id);
        if (definition == nullptr)
            return std::nullopt;
        if (definition->form == TypeForm::enumeration
            && !definition->base.target.target.valid()
            && definition->base.target.spelling.empty()
            && !definition->base.executable_width
            && definition->base.packed_dimensions.empty()
            && !definition->base.packed_range) {
            return 32U;
        }
        if (definition->form == TypeForm::alias
            || definition->form == TypeForm::enumeration
            || definition->form == TypeForm::type_parameter
            || definition->form == TypeForm::packed_integral) {
            return self(self, definition->base, std::move(visited));
        }
        if (definition->form != TypeForm::packed_structure
            && definition->form != TypeForm::packed_union) {
            return std::nullopt;
        }
        std::uint64_t aggregate_width { };
        for (const auto& member : definition->members) {
            const auto member_width = self(self, member.type, visited);
            if (!member_width || *member_width == 0U
                || (definition->form == TypeForm::packed_structure
                    && *member_width
                        > std::numeric_limits<std::uint64_t>::max()
                            - aggregate_width)) {
                return std::nullopt;
            }
            if (definition->form == TypeForm::packed_structure) {
                aggregate_width += *member_width;
            } else {
                aggregate_width = std::max(aggregate_width, *member_width);
            }
        }
        return aggregate_width == 0U
            ? std::nullopt
            : std::optional { aggregate_width };
    };
    const auto append_type = [&](auto&& self,
                                 const semantic::sv::TypeReference& reference,
                                 std::vector<semantic::TypeId> type_path)
        -> bool {
        const auto* type_definition = reference.target.target.valid()
            ? this->type_definition(reference.target.target)
            : nullptr;
        if (type_definition != nullptr) {
            if (std::ranges::find(type_path, type_definition->id)
                != type_path.end()) {
                return false;
            }
            type_path.push_back(type_definition->id);
        }
        auto form = reference.container_form;
        auto ranges = reference.unpacked_dimensions;
        auto associative_index = reference.associative_index_metadata;
        if (!associative_index && reference.associative_index) {
            IndexTypeReference metadata;
            metadata.target = *reference.associative_index;
            associative_index = metadata;
        }
        if (!form && type_definition != nullptr
            && is_container_form(type_definition->form)) {
            form = type_definition->form;
        }
        if (ranges.empty() && form == TypeForm::static_array
            && type_definition != nullptr && type_definition->container) {
            ranges = type_definition->container->static_dimensions;
        }
        if (!associative_index && type_definition != nullptr
            && type_definition->container
            && type_definition->container->associative_index) {
            associative_index =
                semantic::sv::make_associative_index_metadata(
                    *type_definition->container->associative_index);
        }

        if (form == TypeForm::static_array) {
            if (ranges.empty())
                return false;
            for (const auto& range : ranges) {
                dimensions.push_back({ range, std::nullopt, std::nullopt });
                unpacked_dimensions.push_back(range);
            }
        } else if (form == TypeForm::dynamic_array
            || form == TypeForm::queue
            || form == TypeForm::associative_array) {
            // Fixed suffix dimensions are stored on this variable container.
            dimensions.push_back({
                std::nullopt,
                form,
                form == TypeForm::associative_array
                    ? associative_index
                    : std::nullopt,
            });
            unpacked_dimensions.push_back(std::nullopt);
            for (const auto& range : ranges) {
                dimensions.push_back({ range, std::nullopt, std::nullopt });
                unpacked_dimensions.push_back(range);
            }
        } else {
            for (const auto& range : ranges) {
                dimensions.push_back({ range, std::nullopt, std::nullopt });
                unpacked_dimensions.push_back(range);
            }
        }

        if (!reference.container_element_types.empty()) {
            if (reference.container_element_types.size() != 1U)
                return false;
            return self(self, reference.container_element_types.front(),
                { });
        }
        if (type_definition != nullptr
            && type_definition->form == TypeForm::alias) {
            return self(self, type_definition->base,
                std::move(type_path));
        }
        if (type_definition != nullptr
            && is_container_form(type_definition->form)) {
            return self(self, type_definition->base,
                std::move(type_path));
        }

        const bool unpacked_aggregate = type_definition != nullptr
            ? type_definition->form == TypeForm::unpacked_structure
                || type_definition->form == TypeForm::unpacked_union
            : reference.value_form == TypeForm::unpacked_structure
                || reference.value_form == TypeForm::unpacked_union;
        if (unpacked_aggregate)
            return true;

        if (!reference.packed_dimensions.empty()) {
            for (const auto& range : reference.packed_dimensions) {
                dimensions.push_back({ range, std::nullopt, std::nullopt });
            }
        } else if (reference.packed_range) {
            dimensions.push_back({ *reference.packed_range,
                std::nullopt, std::nullopt });
        } else if (reference.value_form == TypeForm::packed_integral
            || reference.value_form == TypeForm::enumeration
            || reference.value_form == TypeForm::packed_structure
            || reference.value_form == TypeForm::packed_union
            || integral_type(
                semantic::sv::make_associative_index_metadata(reference))) {
            const auto width = packed_integral_width(
                packed_integral_width, reference, { });
            if (!width || *width == 0U
                || *width > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
                return false;
            }
            dimensions.push_back({ semantic::sv::PackedRange {
                                       static_cast<std::int64_t>(*width - 1U), 0,
                                       std::nullopt, std::nullopt, true, { } },
                std::nullopt, std::nullopt });
        }
        return true;
    };
    if (!append_type(append_type, leaf_type, { }))
        return std::nullopt;
    if (dimension > dimensions.size())
        return unknown_query();

    const auto packed_signed_answer = [](const std::int64_t answer) {
        return runtime::PackedLogic4::from_aval_bval(
            32U, static_cast<std::uint32_t>(answer), 0U);
    };
    const auto packed_unsigned_answer = [](const std::uint64_t answer) {
        return runtime::PackedLogic4::from_aval_bval(32U, answer, 0U);
    };
    const auto& selected_dimension = dimensions[dimension - 1U];
    if (selected_dimension.variable_form) {
        const auto form = *selected_dimension.variable_form;
        if (dimension != 1U) {
            throw std::invalid_argument {
                "class array query selects a variable-sized dimension other than dimension 1"
            };
        }
        const auto* storage = target->storage != nullptr
            ? target->storage
            : target->owned_storage ? &*target->owned_storage
                                    : nullptr;
        if (!target->direct_container_property || storage == nullptr
            || !storage->handle_container) {
            return std::nullopt;
        }
        if (form == TypeForm::associative_array
            && (!selected_dimension.associative_index
                || !integral_type(*selected_dimension.associative_index))) {
            return std::nullopt;
        }
        const auto expected_kind = form == TypeForm::dynamic_array
            ? runtime::SystemVerilogClassContainerKind::DynamicArray
            : form == TypeForm::queue
            ? runtime::SystemVerilogClassContainerKind::Queue
            : form == TypeForm::associative_array
            ? runtime::SystemVerilogClassContainerKind::AssociativeArray
            : runtime::SystemVerilogClassContainerKind::FixedArray;
        auto& container = *storage->handle_container;
        if (container.kind() != expected_kind)
            return std::nullopt;
        const auto size = container.size();
        if (query == "$size") {
            if (!std::in_range<std::uint32_t>(size))
                return std::nullopt;
            return packed_unsigned_answer(size);
        }
        if (form == TypeForm::associative_array) {
            if (query == "$left")
                return packed_signed_answer(0);
            if (query == "$increment")
                return packed_signed_answer(-1);

            using SystemVerilogTypeReference = semantic::sv::TypeReference;
            struct IntegralIndexDomain {
                std::uint64_t width { };
                bool signed_value { };
            };
            const auto builtin_domain = [](const std::string_view spelling)
                -> std::optional<IntegralIndexDomain> {
                if (spelling == "bit" || spelling == "logic"
                    || spelling == "reg") {
                    return IntegralIndexDomain { 1U, false };
                }
                if (spelling == "byte")
                    return IntegralIndexDomain { 8U, true };
                if (spelling == "shortint")
                    return IntegralIndexDomain { 16U, true };
                if (spelling == "int" || spelling == "integer")
                    return IntegralIndexDomain { 32U, true };
                if (spelling == "longint")
                    return IntegralIndexDomain { 64U, true };
                if (spelling == "time")
                    return IntegralIndexDomain { 64U, false };
                return std::nullopt;
            };
            const auto systemverilog_domain = [&](
                                                  auto&& self,
                                                  const SystemVerilogTypeReference& reference,
                                                  std::vector<semantic::TypeId> visited)
                -> std::optional<IntegralIndexDomain> {
                if (reference.executable_width
                    && *reference.executable_width > 0U
                    && *reference.executable_width <= 64U) {
                    return IntegralIndexDomain {
                        *reference.executable_width,
                        reference.signed_value
                    };
                }
                const auto& ranges = reference.packed_dimensions;
                std::uint64_t width { 1U };
                if (!ranges.empty()) {
                    for (const auto& range : ranges) {
                        const auto range_width = extent(range);
                        if (!range_width || *range_width == 0U
                            || *range_width > 64U / width) {
                            return std::nullopt;
                        }
                        width *= *range_width;
                    }
                    return IntegralIndexDomain { width,
                        reference.signed_value };
                }
                if (reference.packed_range) {
                    const auto range_width = extent(*reference.packed_range);
                    if (range_width && *range_width > 0U
                        && *range_width <= 64U) {
                        return IntegralIndexDomain { *range_width,
                            reference.signed_value };
                    }
                    return std::nullopt;
                }
                const auto builtin = builtin_domain(
                    reference.target.spelling);
                if (builtin) {
                    return IntegralIndexDomain { builtin->width,
                        reference.signed_value };
                }
                const auto type_id = reference.target.target;
                if (!type_id.valid()
                    || std::ranges::find(visited, type_id) != visited.end()) {
                    return std::nullopt;
                }
                visited.push_back(type_id);
                const auto* definition = type_definition(type_id);
                if (definition == nullptr)
                    return std::nullopt;
                if (definition->form == TypeForm::enumeration
                    && !definition->base.target.target.valid()
                    && definition->base.target.spelling.empty()
                    && !definition->base.executable_width
                    && definition->base.packed_dimensions.empty()
                    && !definition->base.packed_range) {
                    return IntegralIndexDomain { 32U, true };
                }
                if (definition->form == TypeForm::alias
                    || definition->form == TypeForm::enumeration
                    || definition->form == TypeForm::type_parameter
                    || definition->form == TypeForm::packed_integral) {
                    return self(self, definition->base, std::move(visited));
                }
                if (definition->form != TypeForm::packed_structure
                    && definition->form != TypeForm::packed_union) {
                    return std::nullopt;
                }
                std::uint64_t aggregate_width { };
                for (const auto& member : definition->members) {
                    const auto member_domain = self(
                        self, member.type, visited);
                    if (!member_domain || member_domain->width == 0U
                        || (definition->form == TypeForm::packed_structure
                            && member_domain->width
                                > 64U - aggregate_width)) {
                        return std::nullopt;
                    }
                    if (definition->form == TypeForm::packed_structure) {
                        aggregate_width += member_domain->width;
                    } else {
                        aggregate_width = std::max(
                            aggregate_width, member_domain->width);
                    }
                }
                if (aggregate_width == 0U || aggregate_width > 64U)
                    return std::nullopt;
                return IntegralIndexDomain {
                    aggregate_width, reference.signed_value
                };
            };
            const auto associative_index_domain = [&](
                                                      const IndexTypeReference& index_type)
                -> std::optional<IntegralIndexDomain> {
                const auto reference
                    = semantic::sv::type_reference_from_associative_index_metadata(
                        index_type);
                return systemverilog_domain(
                    systemverilog_domain, reference, { });
            };
            const auto index_domain = associative_index_domain(
                *selected_dimension.associative_index);
            if (!index_domain || index_domain->width == 0U
                || index_domain->width > 64U) {
                return std::nullopt;
            }
            if (query == "$right") {
                std::uint64_t maximum { };
                if (index_domain->signed_value) {
                    maximum = index_domain->width == 64U
                        ? static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())
                        : (UINT64_C(1) << (index_domain->width - 1U)) - 1U;
                } else {
                    maximum = index_domain->width == 64U
                        ? std::numeric_limits<std::uint64_t>::max()
                        : (UINT64_C(1) << index_domain->width) - 1U;
                }
                return packed_unsigned_answer(maximum);
            }
            if (query != "$low" && query != "$high")
                return std::nullopt;
            if (container.keyed_values().empty())
                return unknown_query();
            const auto numeric_key = [&](const std::string_view key)
                -> std::optional<std::uint64_t> {
                if (const auto unsigned_value = decimal(key))
                    return unsigned_value;
                if (!index_domain->signed_value)
                    return std::nullopt;
                std::int64_t signed_value { };
                const auto parsed = std::from_chars(
                    key.data(), key.data() + key.size(), signed_value, 10);
                return parsed.ec == std::errc { }
                        && parsed.ptr == key.data() + key.size()
                    ? std::optional { static_cast<std::uint64_t>(signed_value) }
                    : std::nullopt;
            };
            const auto mask = index_domain->width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (UINT64_C(1) << index_domain->width) - 1U;
            std::optional<std::uint64_t> selected_raw;
            std::optional<std::int64_t> selected_signed;
            for (const auto& [key, value] : container.keyed_values()) {
                (void)value;
                const auto parsed = numeric_key(key);
                if (!parsed)
                    return std::nullopt;
                const auto raw = *parsed & mask;
                std::int64_t signed_value { };
                if (index_domain->signed_value) {
                    const auto sign_bit = UINT64_C(1)
                        << (index_domain->width - 1U);
                    signed_value = (raw & sign_bit) == 0U
                        ? static_cast<std::int64_t>(raw)
                        : -1 - static_cast<std::int64_t>(mask ^ raw);
                }
                bool replace = !selected_raw;
                if (selected_raw) {
                    if (index_domain->signed_value) {
                        replace = query == "$low"
                            ? signed_value < *selected_signed
                            : signed_value > *selected_signed;
                    } else {
                        replace = query == "$low"
                            ? raw < *selected_raw
                            : raw > *selected_raw;
                    }
                }
                if (replace) {
                    selected_raw = raw;
                    selected_signed = signed_value;
                }
            }
            return index_domain->signed_value
                ? packed_signed_answer(*selected_signed)
                : packed_unsigned_answer(*selected_raw);
        }

        if (query == "$left" || query == "$low")
            return packed_signed_answer(0);
        if (query == "$increment")
            return packed_signed_answer(-1);
        if (query == "$right" || query == "$high") {
            const auto right = size == 0U
                ? std::int64_t { -1 }
                : static_cast<std::int64_t>(size - 1U);
            return packed_signed_answer(right);
        }
        return std::nullopt;
    }
    if (!selected_dimension.range)
        return std::nullopt;

    const auto selected_left = bound(
        selected_dimension.range->left,
        selected_dimension.range->left_expression);
    const auto selected_right = bound(
        selected_dimension.range->right,
        selected_dimension.range->right_expression);
    if (!selected_left || !selected_right)
        return std::nullopt;
    const auto selected_size = [&]() -> std::optional<std::uint64_t> {
        const auto distance
            = support::index_distance(*selected_left, *selected_right);
        if (distance == std::numeric_limits<std::uint64_t>::max())
            return std::nullopt;
        return distance + 1U;
    };

    if (target->direct_container_property && target->storage != nullptr
        && target->storage->handle_container) {
        const auto kind = target->storage->handle_container->kind();
        if (unpacked_dimensions.empty())
            return std::nullopt;
        if (!unpacked_dimensions.front()) {
            if (dimensions.empty() || !dimensions.front().variable_form)
                return std::nullopt;
            const auto outer_form = *dimensions.front().variable_form;
            const auto expected_kind = outer_form == TypeForm::dynamic_array
                ? runtime::SystemVerilogClassContainerKind::DynamicArray
                : outer_form == TypeForm::queue
                ? runtime::SystemVerilogClassContainerKind::Queue
                : runtime::SystemVerilogClassContainerKind::AssociativeArray;
            if (kind != expected_kind)
                return std::nullopt;
        } else if (kind
                != runtime::SystemVerilogClassContainerKind::FixedArray
            && kind
                != runtime::SystemVerilogClassContainerKind::UnpackedAggregate) {
            return std::nullopt;
        } else {
            const bool has_variable_unpacked_dimension
                = std::ranges::any_of(unpacked_dimensions,
                    [](const auto& range) { return !range; });
            if (!has_variable_unpacked_dimension) {
                std::size_t total_unpacked_elements { 1U };
                for (const auto& range : unpacked_dimensions) {
                    const auto dimension_size = extent(*range);
                    if (!dimension_size
                        || !std::in_range<std::size_t>(*dimension_size)
                        || *dimension_size
                            > std::numeric_limits<std::size_t>::max()
                                / total_unpacked_elements) {
                        return std::nullopt;
                    }
                    total_unpacked_elements *= static_cast<std::size_t>(
                        *dimension_size);
                }
                if (target->storage->handle_container->size()
                    != total_unpacked_elements) {
                    return std::nullopt;
                }
            }
        }
    }

    if (query == "$size") {
        const auto size = selected_size();
        if (!size || !std::in_range<std::uint32_t>(*size))
            return std::nullopt;
        return packed_unsigned_answer(*size);
    }
    if (query == "$left")
        return packed_signed_answer(*selected_left);
    if (query == "$right")
        return packed_signed_answer(*selected_right);
    const bool increment_positive = *selected_left >= *selected_right;
    if (query == "$increment")
        return packed_signed_answer(increment_positive ? 1 : -1);
    if (query == "$low")
        return packed_signed_answer(
            std::min(*selected_left, *selected_right));
    if (query == "$high")
        return packed_signed_answer(
            std::max(*selected_left, *selected_right));
    return std::nullopt;
}

std::optional<runtime::PackedLogic4>
SystemVerilogClassHirExecution::evaluate_class_handle_index(
    const semantic::sv::Expression& source,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment)
{
    using semantic::sv::ExpressionKind;
    using semantic::sv::TypeForm;
    constexpr std::string_view indexed_container_prefix {
        "@sv-container-index:"
    };
    constexpr std::string_view static_container_prefix {
        "@sv-static-container-property:"
    };

    const semantic::sv::TypeReference* type { };
    const runtime::SystemVerilogClassPropertyValue* storage { };
    std::optional<runtime::SystemVerilogClassPropertyValue> static_storage;
    std::vector<semantic::ExpressionId> indices;

    if (source.kind == ExpressionKind::call
        && source.text.starts_with(indexed_container_prefix)
        && source.operands.size() >= 2U) {
        const auto receiver = evaluate_packed(
            source.operands.front(), handle, environment);
        if (!receiver || receiver->low_word().bval != 0U)
            return std::nullopt;
        const auto owner = receiver->low_word().aval;
        if (owner == 0U || !heap_.contains(owner))
            return std::nullopt;
        const auto identity = marker_payload(
            source, indexed_container_prefix);
        const auto* property = instance_property(owner, identity);
        if (property == nullptr)
            return std::nullopt;
        type = &property->type;
        storage = &heap_.property(owner, property->canonical_identity);
        indices.assign(source.operands.begin() + 1,
            source.operands.end());
    } else if (source.kind == ExpressionKind::index
        && source.operands.size() == 2U) {
        const auto* base = expression(source.operands.front());
        if (base == nullptr
            || !base->text.starts_with(static_container_prefix)) {
            return std::nullopt;
        }
        const auto identity = marker_payload(*base, static_container_prefix);
        const auto declaration_id = base->referenced_name
            ? base->referenced_name->selected
            : std::nullopt;
        const auto* property = static_class_property(
                specializations_, identity, declaration_id);
        if (property == nullptr || !read_static_property_value_)
            return std::nullopt;
        static_storage = read_static_property_value_(
            property->canonical_identity);
        if (!static_storage)
            return std::nullopt;
        type = &property->type;
        storage = &*static_storage;
        indices.push_back(source.operands.back());
    } else {
        return std::nullopt;
    }

    if (type == nullptr || storage == nullptr || !storage->handle_container)
        return std::nullopt;

    const auto& container = *storage->handle_container;
    const auto form = type->container_form;
    if (form == TypeForm::static_array) {
        auto ranges = type->unpacked_dimensions;
        if (ranges.empty() && type->target.target.valid()) {
            const auto* definition = type_definition(type->target.target);
            if (definition != nullptr
                && definition->container) {
                ranges = definition->container->static_dimensions;
            }
        }
        if (ranges.size() != indices.size()
            || container.kind()
                != runtime::SystemVerilogClassContainerKind::FixedArray) {
            return std::nullopt;
        }
        std::size_t offset { };
        const auto range_bound = [&](const auto& literal, const auto& id) {
            return evaluate_range_bound(literal, id, handle, environment);
        };
        for (std::size_t dimension { }; dimension < ranges.size();
            ++dimension) {
            const auto& range = ranges[dimension];
            const auto left = range_bound(range.left, range.left_expression);
            const auto right = range_bound(range.right, range.right_expression);
            const auto index_value = evaluate_packed(
                indices[dimension], handle, environment);
            const auto index = index_value
                ? index_value->known_signed_value()
                : std::nullopt;
            if (!left || !right || !index
                || *index < std::min(*left, *right)
                || *index > std::max(*left, *right)) {
                return std::nullopt;
            }
            const auto distance = support::index_distance(*left, *right);
            if (distance == std::numeric_limits<std::uint64_t>::max()
                || distance + 1U
                    > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            const auto extent = static_cast<std::size_t>(distance + 1U);
            const auto ordinal = static_cast<std::uint64_t>(
                *left >= *right
                    ? static_cast<std::uint64_t>(*left)
                        - static_cast<std::uint64_t>(*index)
                    : static_cast<std::uint64_t>(*index)
                        - static_cast<std::uint64_t>(*left));
            if (ordinal >= extent)
                return std::nullopt;
            if (extent == 0U
                || offset > (std::numeric_limits<std::size_t>::max()
                                - static_cast<std::size_t>(ordinal))
                        / extent) {
                return std::nullopt;
            }
            offset = offset * extent
                + static_cast<std::size_t>(ordinal);
        }
        if (offset >= container.size())
            return std::nullopt;
        return runtime::PackedLogic4::from_aval_bval(
            64U, container.at(offset), 0U);
    }

    if ((form != TypeForm::dynamic_array && form != TypeForm::queue)
        || indices.size() != 1U
        || container.kind()
            != (form == TypeForm::dynamic_array
                    ? runtime::SystemVerilogClassContainerKind::DynamicArray
                    : runtime::SystemVerilogClassContainerKind::Queue)) {
        return std::nullopt;
    }
    const auto index_value = evaluate_packed(
        indices.front(), handle, environment);
    const auto index = index_value
        ? index_value->known_signed_value()
        : std::nullopt;
    if (!index || *index < 0
        || !std::in_range<std::size_t>(*index)
        || static_cast<std::size_t>(*index) >= container.size()) {
        return std::nullopt;
    }
    return runtime::PackedLogic4::from_aval_bval(
        64U, container.at(static_cast<std::size_t>(*index)), 0U);
}

const semantic::sv::TypeDefinition*
SystemVerilogClassHirExecution::call_aggregate_definition(
    const semantic::sv::TypeReference& type) const
{
    auto current = type;
    std::vector<semantic::TypeId> visited;
    for (std::size_t depth = 0U; depth < 64U; ++depth) {
        if (!current.target.target.valid()
            || std::ranges::find(visited, current.target.target)
                != visited.end()) {
            return nullptr;
        }
        visited.push_back(current.target.target);
        const auto* found = type_definition(current.target.target);
        if (found == nullptr)
            return nullptr;
        if (found->form != semantic::sv::TypeForm::alias)
            return found;
        current = found->base;
    }
    return nullptr;
}

std::optional<semantic::sv::TypeReference>
SystemVerilogClassHirExecution::call_array_element_type(
    const semantic::sv::TypeReference& type) const
{
    if (type.container_element_types.size() == 1U)
        return type.container_element_types.front();
    if (type.container_element_types.size() > 1U)
        return std::nullopt;
    const auto* definition = call_aggregate_definition(type);
    if (definition != nullptr && definition->container)
        return definition->base;
    return std::nullopt;
}

bool SystemVerilogClassHirExecution::class_call_type_assignable(
    const std::string_view actual_identity,
    const std::string_view formal_identity) const
{
    if (actual_identity.empty() || formal_identity.empty())
        return false;
    std::vector<std::string> pending { std::string { actual_identity } };
    std::set<std::string, std::less<>> visited;
    while (!pending.empty()) {
        auto current_identity = std::move(pending.back());
        pending.pop_back();
        if (current_identity == formal_identity)
            return true;
        if (!visited.insert(current_identity).second)
            continue;
        const auto* current = specialization(current_identity);
        if (current == nullptr)
            continue;
        if (current->base)
            pending.push_back(current->base->declaration_identity);
        for (const auto& interface_type : current->interfaces)
            pending.push_back(interface_type.declaration_identity);
    }
    return false;
}

bool SystemVerilogClassHirExecution::is_typed_call_value(
    const semantic::sv::TypeReference& type) const
{
    if (type.container_form)
        return *type.container_form != semantic::sv::TypeForm::static_array;
    if (!type.unpacked_dimensions.empty())
        return false;
    const auto* definition = call_aggregate_definition(type);
    return definition != nullptr
        && definition->form == semantic::sv::TypeForm::unpacked_structure;
}

bool SystemVerilogClassHirExecution::scalar_call_type_compatible(
    const semantic::sv::TypeReference& actual,
    const semantic::sv::TypeReference& formal) const
{
    // This helper is used for scalar leaves inside typed arrays/structures,
    // where the caller needs effective type equivalence. Do not discard
    // declarator shape while following aliases: unpacked-array argument
    // compatibility depends on its element type, and a packed extension at
    // the use site remains part of that element type.
    const auto retain_declarator = [](
        semantic::sv::TypeReference replacement,
        const semantic::sv::TypeReference& use) {
        if (use.packed_range)
            replacement.packed_range = use.packed_range;
        if (use.executable_width)
            replacement.executable_width = use.executable_width;
        replacement.signed_value
            = replacement.signed_value || use.signed_value;
        replacement.four_state
            = replacement.four_state || use.four_state;
        return replacement;
    };
    const auto unalias = [this, &retain_declarator](
                             semantic::sv::TypeReference type)
        -> std::optional<semantic::sv::TypeReference> {
        std::vector<semantic::TypeId> visited;
        for (std::size_t depth = 0U; depth < 64U; ++depth) {
            if (!type.target.target.valid())
                return type;
            const auto* definition = type_definition(type.target.target);
            if (definition == nullptr
                || definition->form != semantic::sv::TypeForm::alias) {
                return type;
            }
            if (std::ranges::find(visited, definition->id) != visited.end())
                return std::nullopt;
            visited.push_back(definition->id);
            type = retain_declarator(definition->base, type);
        }
        return std::nullopt;
    };
    const auto actual_type = unalias(actual);
    const auto formal_type = unalias(formal);
    if (!actual_type || !formal_type)
        return false;

    // PackedRange::operator== also compares source spans and expression IDs.
    // For fully resolved literal ranges, compare only the effective bounds and
    // direction; source provenance does not change the scalar type shape.
    // Keep expression identity for unresolved ranges, where no resolved
    // bounds establish equivalence.
    const auto same_packed_range_shape = [](
        const semantic::sv::PackedRange& left,
        const semantic::sv::PackedRange& right) {
        if (left.left != right.left || left.right != right.right
            || left.descending != right.descending) {
            return false;
        }
        if (left.left && left.right)
            return true;
        return left.left_expression == right.left_expression
            && left.right_expression == right.right_expression;
    };
    const auto same_optional_packed_range = [&](
        const std::optional<semantic::sv::PackedRange>& left,
        const std::optional<semantic::sv::PackedRange>& right) {
        if (left.has_value() != right.has_value())
            return false;
        return !left || same_packed_range_shape(*left, *right);
    };
    const auto same_packed_dimensions = [&](
        const std::vector<semantic::sv::PackedRange>& left,
        const std::vector<semantic::sv::PackedRange>& right) {
        if (left.size() != right.size())
            return false;
        for (std::size_t index { }; index < left.size(); ++index) {
            if (!same_packed_range_shape(left[index], right[index]))
                return false;
        }
        return true;
    };

    const auto* actual_definition = call_aggregate_definition(*actual_type);
    const auto* formal_definition = call_aggregate_definition(*formal_type);
    const auto nominal_form = [](const semantic::sv::TypeForm form) {
        using semantic::sv::TypeForm;
        return form == TypeForm::enumeration
            || form == TypeForm::packed_structure
            || form == TypeForm::packed_union
            || form == TypeForm::tagged_union
            || form == TypeForm::unpacked_structure
            || form == TypeForm::unpacked_union;
    };
    if ((actual_definition != nullptr
            && nominal_form(actual_definition->form))
        || (formal_definition != nullptr
            && nominal_form(formal_definition->form))) {
        return actual_definition != nullptr && formal_definition != nullptr
            && actual_definition->form == formal_definition->form
            && actual_definition->id == formal_definition->id;
    }
    if (actual_type->value_form != formal_type->value_form
        || actual_type->class_identity != formal_type->class_identity
        || actual_type->signed_value != formal_type->signed_value
        || actual_type->four_state != formal_type->four_state
        || !same_optional_packed_range(actual_type->packed_range,
            formal_type->packed_range)
        || !same_packed_dimensions(actual_type->packed_dimensions,
            formal_type->packed_dimensions)
        || actual_type->executable_width != formal_type->executable_width) {
        return false;
    }
    return actual_type->value_form.has_value()
        || (actual_type->target.target.valid()
            && actual_type->target.target == formal_type->target.target);
}

std::optional<std::pair<std::int64_t, std::int64_t>>
SystemVerilogClassHirExecution::call_array_bounds(
    const semantic::sv::TypeReference& type,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment)
{
    auto ranges = type.unpacked_dimensions;
    if (ranges.empty()) {
        const auto* definition = call_aggregate_definition(type);
        if (definition != nullptr && definition->container)
            ranges = definition->container->static_dimensions;
    }
    if (ranges.size() != 1U)
        return std::nullopt;
    const auto bound = [&](const auto& literal, const auto& id) {
        return evaluate_range_bound(literal, id, handle, environment);
    };
    const auto left = bound(ranges.front().left,
        ranges.front().left_expression);
    const auto right = bound(ranges.front().right,
        ranges.front().right_expression);
    if (!left || !right)
        return std::nullopt;
    return std::pair { *left, *right };
}

void SystemVerilogClassHirExecution::validate_typed_actual(
    const CallValue& actual,
    const semantic::sv::TypeReference& formal_type) const
{
    const auto* formal_definition = call_aggregate_definition(formal_type);
    const bool formal_array = formal_type.container_form
        || !formal_type.unpacked_dimensions.empty()
        || (formal_definition != nullptr
            && formal_definition->form == semantic::sv::TypeForm::static_array);
    if (formal_array) {
        const auto expected_element = call_array_element_type(formal_type);
        if (!expected_element || actual.kind != CallValue::Kind::array) {
            throw std::invalid_argument {
                "class HIR aggregate actual does not match an array formal"
            };
        }
        const auto array_form = [this](
                                    const semantic::sv::TypeReference& type)
            -> std::optional<semantic::sv::TypeForm> {
            if (type.container_form)
                return type.container_form;
            if (!type.unpacked_dimensions.empty())
                return semantic::sv::TypeForm::static_array;
            const auto* definition = call_aggregate_definition(type);
            if (definition != nullptr
                && (definition->form
                        == semantic::sv::TypeForm::static_array
                    || definition->form
                        == semantic::sv::TypeForm::dynamic_array
                    || definition->form == semantic::sv::TypeForm::queue
                    || definition->form
                        == semantic::sv::TypeForm::associative_array)) {
                return definition->form;
            }
            return std::nullopt;
        };
        const auto compatible_type = [this, &array_form](
                                         auto&& self,
                                         const semantic::sv::TypeReference& source,
                                         const semantic::sv::TypeReference& target,
                                         const std::size_t depth) -> bool {
            if (depth >= 64U)
                return false;
            const auto source_form = array_form(source);
            const auto target_form = array_form(target);
            if (source_form || target_form) {
                if (!source_form || !target_form)
                    return false;
                const auto source_element = call_array_element_type(source);
                const auto target_element = call_array_element_type(target);
                return source_element && target_element
                    && self(self, *source_element, *target_element,
                        depth + 1U);
            }
            if (!source.class_identity.empty()
                || !target.class_identity.empty()) {
                return !source.class_identity.empty()
                    && !target.class_identity.empty()
                    && class_call_type_assignable(
                        source.class_identity, target.class_identity);
            }
            if (is_string(source) || is_string(target))
                return is_string(source) && is_string(target);
            return scalar_call_type_compatible(source, target);
        };
        if (actual.elements.empty()) {
            const auto actual_element = call_array_element_type(actual.type);
            if (!actual_element
                || !compatible_type(compatible_type, *actual_element,
                    *expected_element, 0U)) {
                throw std::invalid_argument {
                    "class HIR empty array actual has an incompatible element type"
                };
            }
        }
        if (actual.left == 0 && actual.right == -1 && actual.elements.empty())
            return;
        const auto distance
            = support::index_distance(actual.left, actual.right);
        if (distance == std::numeric_limits<std::uint64_t>::max()
            || !std::in_range<std::size_t>(distance + 1U)
            || actual.elements.size()
                != static_cast<std::size_t>(distance + 1U)) {
            throw std::invalid_argument {
                "class HIR array actual bounds do not match its elements"
            };
        }
        for (const auto& element : actual.elements) {
            validate_typed_actual(element, *expected_element);
        }
        return;
    }

    if (!formal_type.class_identity.empty()) {
        if (actual.kind != CallValue::Kind::packed || !actual.packed
            || actual.packed->width() != 64U
            || actual.packed->low_word().bval != 0U
            || !class_call_type_assignable(
                actual.type.class_identity, formal_type.class_identity)) {
            throw std::invalid_argument {
                "class HIR handle actual has an incompatible static type or is not known"
            };
        }
        const auto class_handle = actual.packed->low_word().aval;
        if (class_handle != 0U) {
            if (!heap_.contains(class_handle)) {
                throw std::invalid_argument {
                    "class HIR handle actual is stale"
                };
            }
            (void)heap_.checked_cast(class_handle, formal_type.class_identity);
        }
        return;
    }

    if (formal_definition != nullptr
        && formal_definition->form
            == semantic::sv::TypeForm::unpacked_structure) {
        const auto* actual_definition
            = call_aggregate_definition(actual.type);
        if (actual.kind != CallValue::Kind::structure
            || actual_definition == nullptr
            || actual_definition->id != formal_definition->id) {
            throw std::invalid_argument {
                "class HIR structure actual has an incompatible nominal type"
            };
        }
        if (actual.members.size() != formal_definition->members.size()) {
            throw std::invalid_argument {
                "class HIR structure actual is missing a member"
            };
        }
        for (const auto& formal_member : formal_definition->members) {
            const auto actual_member = std::ranges::find(
                actual.members, formal_member.name,
                [](const auto& entry) -> const std::string& {
                    return entry.first;
                });
            if (actual_member == actual.members.end()) {
                throw std::invalid_argument {
                    "class HIR structure actual is missing a member"
                };
            }
            if (!actual_member->second) {
                throw std::invalid_argument {
                    "class HIR structure actual has an absent member value"
                };
            }
            validate_typed_actual(*actual_member->second, formal_member.type);
        }
        return;
    }
    if (is_string(formal_type)) {
        if (actual.kind != CallValue::Kind::string
            || !is_string(actual.type)) {
            throw std::invalid_argument {
                "class HIR scalar actual is not a string"
            };
        }
        return;
    }
    if (actual.kind != CallValue::Kind::packed || !actual.packed
        || !scalar_call_type_compatible(actual.type, formal_type)) {
        throw std::invalid_argument {
            "class HIR scalar actual has an incompatible packed type"
        };
    }
}

std::optional<SystemVerilogClassHirExecution::CallValue>
SystemVerilogClassHirExecution::evaluate_typed_actual(
    const semantic::ExpressionId id,
    const semantic::sv::TypeReference& expected_type,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& string_environment)
{
    using semantic::sv::ExpressionKind;
    using semantic::sv::TypeForm;
    if (auto bound = evaluate_bound_call_value(id, handle, environment);
        bound && (bound->kind == CallValue::Kind::array
            || bound->kind == CallValue::Kind::structure)) {
        validate_typed_actual(*bound, expected_type);
        return bound;
    }
    const auto* source = expression(id);
    if (source == nullptr)
        return std::nullopt;
    const auto is_fixed_array = [this](
                                    const semantic::sv::TypeReference& type) {
        if (type.container_form == TypeForm::static_array
            || !type.unpacked_dimensions.empty()) {
            return true;
        }
        const auto* definition = call_aggregate_definition(type);
        return definition != nullptr
            && definition->form == TypeForm::static_array;
    };
    const auto array_form = [this](
                                const semantic::sv::TypeReference& type)
        -> std::optional<TypeForm> {
        if (type.container_form)
            return type.container_form;
        if (!type.unpacked_dimensions.empty())
            return TypeForm::static_array;
        const auto* definition = call_aggregate_definition(type);
        if (definition != nullptr
            && (definition->form == TypeForm::static_array
                || definition->form == TypeForm::dynamic_array
                || definition->form == TypeForm::queue
                || definition->form == TypeForm::associative_array)) {
            return definition->form;
        }
        return std::nullopt;
    };
    const auto nominal_identity = [this](
                                      const semantic::sv::TypeReference& type) {
        const auto* definition = call_aggregate_definition(type);
        return definition != nullptr
                && semantic::sv::has_nominal_type_identity(definition->form)
            ? semantic::sv::nominal_type_identity(definition->id)
            : std::string { };
    };
    const auto scalar_kind = [this](
                                 const semantic::sv::TypeReference& type) {
        auto current = type;
        std::vector<semantic::TypeId> visited;
        for (std::size_t depth { }; depth < 64U; ++depth) {
            using Scalar = runtime::SystemVerilogScalarKind;
            if (const auto kind = frontend::systemverilog_scalar_kind(
                    current.target.spelling);
                kind != Scalar::None) {
                return kind;
            }
            if (!current.target.target.valid()
                || std::ranges::find(visited, current.target.target)
                    != visited.end()) {
                return Scalar::None;
            }
            visited.push_back(current.target.target);
            const auto* definition = type_definition(current.target.target);
            if (definition == nullptr
                || definition->form != TypeForm::alias) {
                return Scalar::None;
            }
            current = definition->base;
        }
        return runtime::SystemVerilogScalarKind::None;
    };
    const auto effective_scalar_type = [this](
        const semantic::sv::TypeReference& type)
        -> std::optional<semantic::sv::TypeReference> {
        // Keep the declarator's effective packed shape while following a
        // typedef, matching CompiledDesignResolver's
        // retain_systemverilog_declarator behavior. The runtime container
        // profile consumes executable_width, so retain that precomputed
        // flattened width as well as the packed range and state/sign rules.
        const auto retain_declarator = [](
            semantic::sv::TypeReference replacement,
            const semantic::sv::TypeReference& use) {
            if (use.packed_range)
                replacement.packed_range = use.packed_range;
            if (use.executable_width)
                replacement.executable_width = use.executable_width;
            replacement.signed_value
                = replacement.signed_value || use.signed_value;
            replacement.four_state
                = replacement.four_state || use.four_state;
            return replacement;
        };
        auto current = type;
        std::vector<semantic::TypeId> visited;
        for (std::size_t depth { }; depth < 64U; ++depth) {
            if (!current.class_identity.empty()
                || current.value_form == TypeForm::class_handle) {
                return current;
            }
            if (!current.target.target.valid())
                return current;
            if (std::ranges::find(visited, current.target.target)
                != visited.end()) {
                return std::nullopt;
            }
            visited.push_back(current.target.target);
            const auto* definition = type_definition(current.target.target);
            if (definition == nullptr)
                return std::nullopt;
            if (definition->form != TypeForm::alias)
                return current;
            current = retain_declarator(definition->base, current);
        }
        return std::nullopt;
    };
    std::function<bool(const runtime::simir::ContainerType&,
        const semantic::sv::TypeReference&, std::size_t)>
        profile_matches_container;
    std::function<bool(const runtime::simir::ContainerType&,
        const semantic::sv::TypeReference&, std::size_t)>
        profile_matches_element;
    profile_matches_container = [&](
        const runtime::simir::ContainerType& profile,
        const semantic::sv::TypeReference& type,
        const std::size_t depth) {
        if (depth >= 64U)
            return false;
        const auto form = array_form(type);
        if (!form || *form == TypeForm::associative_array)
            return false;
        if (*form == TypeForm::dynamic_array) {
            if (profile.fixed || profile.queue || profile.associative)
                return false;
        } else if (*form == TypeForm::queue) {
            if (profile.fixed || !profile.queue || profile.associative)
                return false;
        } else if (!profile.fixed || profile.queue || profile.associative) {
            return false;
        }
        const auto element = call_array_element_type(type);
        return element
            && profile_matches_element(profile, *element, depth + 1U);
    };
    profile_matches_element = [&](
        const runtime::simir::ContainerType& profile,
        const semantic::sv::TypeReference& type,
        const std::size_t depth) {
        using ElementKind = runtime::simir::ContainerElementKind;
        if (depth >= 64U)
            return false;
        if (array_form(type)) {
            return profile.element_kind == ElementKind::Container
                && profile.element_types.size() == 1U
                && profile_matches_container(
                    profile.element_types.front(), type, depth + 1U);
        }
        const auto* definition = call_aggregate_definition(type);
        if (definition != nullptr
            && (definition->form == TypeForm::unpacked_structure
                || definition->form == TypeForm::unpacked_union)) {
            if (profile.element_kind != ElementKind::Aggregate
                || profile.union_aggregate
                    != (definition->form == TypeForm::unpacked_union)
                || profile.element_nominal_type != nominal_identity(type)
                || profile.member_names.size() != definition->members.size()
                || profile.element_types.size() != definition->members.size()) {
                return false;
            }
            for (std::size_t index { };
                 index < definition->members.size(); ++index) {
                if (profile.member_names[index]
                        != definition->members[index].name
                    || !profile_matches_element(profile.element_types[index],
                        definition->members[index].type, depth + 1U)) {
                    return false;
                }
            }
            return true;
        }
        const auto effective_type = effective_scalar_type(type);
        if (!effective_type)
            return false;
        if (is_string(*effective_type)) {
            return profile.element_kind == ElementKind::String
                && profile.element_width == 0U
                && profile.scalar_kind
                    == runtime::SystemVerilogScalarKind::None;
        }
        const auto scalar = scalar_kind(*effective_type);
        if (scalar == runtime::SystemVerilogScalarKind::None) {
            if (profile.element_kind != ElementKind::Packed
                || profile.scalar_kind
                    != runtime::SystemVerilogScalarKind::None) {
                return false;
            }
        } else if (profile.element_kind != ElementKind::Scalar
            || profile.scalar_kind != scalar) {
            return false;
        }
        if (!effective_type->class_identity.empty()
            && profile.element_width != 64U) {
            return false;
        }
        if (effective_type->executable_width
            && profile.element_width != *effective_type->executable_width) {
            return false;
        }
        if (profile.element_width == 0U
            || profile.signed_elements != effective_type->signed_value
            || profile.two_state != !effective_type->four_state) {
            return false;
        }
        const auto nominal = nominal_identity(type);
        return nominal.empty() || profile.element_nominal_type == nominal;
    };
    const auto storage_shape_matches = [](
        const runtime::simir::ContainerValue& value,
        const std::size_t count) {
        using ElementKind = runtime::simir::ContainerElementKind;
        if (!value.keys.empty() || !value.string_keys.empty())
            return false;
        switch (value.type.element_kind) {
        case ElementKind::Packed:
        case ElementKind::Scalar:
            return value.elements.size() == count
                && value.string_elements.empty()
                && value.nested_elements.empty();
        case ElementKind::String:
            return value.string_elements.size() == count
                && value.elements.empty()
                && value.nested_elements.empty();
        case ElementKind::Container:
        case ElementKind::Aggregate:
            return value.nested_elements.size() == count
                && value.elements.empty()
                && value.string_elements.empty();
        }
        return false;
    };
    std::function<std::optional<CallValue>(
        const runtime::simir::ContainerValue&,
        const semantic::sv::TypeReference&, std::size_t)> capture_container_value;
    capture_container_value = [&](
        const runtime::simir::ContainerValue& value,
        const semantic::sv::TypeReference& type,
        const std::size_t depth) -> std::optional<CallValue> {
        using ElementKind = runtime::simir::ContainerElementKind;
        if (depth >= 64U)
            return std::nullopt;
        if (const auto form = array_form(type)) {
            if (*form == TypeForm::associative_array
                || !profile_matches_container(value.type, type, depth)
                || value.type.associative) {
                return std::nullopt;
            }
            const auto count = runtime::simir::container_value_size(value);
            if (!storage_shape_matches(value, count))
                return std::nullopt;
            CallValue array;
            array.kind = CallValue::Kind::array;
            array.type = type;
            if (*form == TypeForm::dynamic_array || *form == TypeForm::queue) {
                if (count != 0U
                    && !std::in_range<std::int64_t>(count - 1U)) {
                    return std::nullopt;
                }
                array.left = 0;
                array.right = count == 0U
                    ? -1 : static_cast<std::int64_t>(count - 1U);
            } else {
                auto bounds = call_array_bounds(type, handle, environment);
                if (!bounds && value.type.dimensions.size() == 1U) {
                    bounds = std::pair {
                        static_cast<std::int64_t>(
                            value.type.dimensions.front().first),
                        static_cast<std::int64_t>(
                            value.type.dimensions.front().second)
                    };
                }
                if (!bounds)
                    return std::nullopt;
                const auto distance
                    = support::index_distance(bounds->first, bounds->second);
                if (distance == std::numeric_limits<std::uint64_t>::max()
                    || !std::in_range<std::size_t>(distance + 1U)
                    || count != static_cast<std::size_t>(distance + 1U)) {
                    return std::nullopt;
                }
                array.left = bounds->first;
                array.right = bounds->second;
            }
            const auto element_type = call_array_element_type(type);
            if (!element_type)
                return std::nullopt;
            array.elements.reserve(count);
            for (std::size_t index { }; index < count; ++index) {
                CallValue element;
                element.type = *element_type;
                switch (value.type.element_kind) {
                case ElementKind::Packed:
                case ElementKind::Scalar:
                    if (value.elements[index].width()
                        != value.type.element_width) {
                        return std::nullopt;
                    }
                    element.kind = CallValue::Kind::packed;
                    element.packed = value.elements[index];
                    break;
                case ElementKind::String:
                    element.kind = CallValue::Kind::string;
                    element.text = value.string_elements[index];
                    break;
                case ElementKind::Container:
                case ElementKind::Aggregate: {
                    auto nested = capture_container_value(
                        value.nested_elements[index], *element_type,
                        depth + 1U);
                    if (!nested)
                        return std::nullopt;
                    element = std::move(*nested);
                    break;
                }
                }
                validate_typed_actual(element, *element_type);
                array.elements.push_back(std::move(element));
            }
            validate_typed_actual(array, type);
            return array;
        }

        const auto count = runtime::simir::container_value_size(value);
        if (!storage_shape_matches(value, count)
            || !profile_matches_element(value.type, type, depth)) {
            return std::nullopt;
        }
        if (is_string(type)) {
            if (count != 1U)
                return std::nullopt;
            CallValue text;
            text.kind = CallValue::Kind::string;
            text.type = type;
            text.text = value.string_elements.front();
            validate_typed_actual(text, type);
            return text;
        }
        const auto* definition = call_aggregate_definition(type);
        if (definition != nullptr
            && definition->form == TypeForm::unpacked_structure) {
            if (!value.type.aggregate_value
                || count != definition->members.size()) {
                return std::nullopt;
            }
            CallValue structure;
            structure.kind = CallValue::Kind::structure;
            structure.type = type;
            structure.members.reserve(definition->members.size());
            for (std::size_t index { };
                 index < definition->members.size(); ++index) {
                const auto& member = definition->members[index];
                auto field = capture_container_value(
                    value.nested_elements[index], member.type, depth + 1U);
                if (!field)
                    return std::nullopt;
                structure.members.emplace_back(member.name,
                    std::make_shared<CallValue>(std::move(*field)));
            }
            validate_typed_actual(structure, type);
            return structure;
        }
        if ((value.type.element_kind != ElementKind::Packed
                && value.type.element_kind != ElementKind::Scalar)
            || count != 1U
            || value.elements.front().width()
                != value.type.element_width) {
            return std::nullopt;
        }
        CallValue packed;
        packed.kind = CallValue::Kind::packed;
        packed.type = type;
        packed.packed = value.elements.front();
        validate_typed_actual(packed, type);
        return packed;
    };
    const auto capture_dynamic_property = [&](
        const runtime::SystemVerilogClassHandle owner,
        const semantic::sv::SpecializedClassProperty& property)
        -> std::optional<CallValue> {
        const auto form = array_form(property.type);
        if (!read_class_property_container_ || !form
            || *form != TypeForm::dynamic_array) {
            return std::nullopt;
        }
        const auto stored = read_class_property_container_(
            owner, property.canonical_identity);
        CallValue actual;
        if (stored) {
            auto captured = capture_container_value(*stored,
                property.type, 0U);
            if (!captured || captured->kind != CallValue::Kind::array)
                return std::nullopt;
            actual = std::move(*captured);
        } else {
            // An uninitialized dynamic array has zero elements. Preserve its
            // declared element type so empty actuals still receive the same
            // nominal and scalar compatibility checks as nonempty arrays.
            actual.kind = CallValue::Kind::array;
            actual.type = property.type;
            actual.left = 0;
            actual.right = -1;
        }
        validate_typed_actual(actual, expected_type);
        return actual;
    };

    const auto evaluate_value = [&](auto&& self,
                                    const semantic::ExpressionId value_id,
                                    const semantic::sv::TypeReference& type)
        -> std::optional<CallValue> {
        const auto* value = expression(value_id);
        if (value == nullptr)
            return std::nullopt;
        const bool array_type = type.container_form.has_value()
            || !type.unpacked_dimensions.empty();
        if (array_type) {
            const auto element_type = call_array_element_type(type);
            if (!element_type)
                return std::nullopt;
            CallValue array;
            array.kind = CallValue::Kind::array;
            array.type = type;

            constexpr std::string_view container_prefix {
                "@sv-container-property:"
            };
            if (value->kind == ExpressionKind::name
                && value->referenced_name
                && value->referenced_name->selected) {
                auto owner = handle;
                if (value->operands.size() > 1U)
                    return std::nullopt;
                if (value->operands.size() == 1U) {
                    const auto receiver = evaluate_packed(
                        value->operands.front(), handle, environment);
                    if (!receiver || receiver->low_word().bval != 0U)
                        return std::nullopt;
                    owner = receiver->low_word().aval;
                }
                if (owner == 0U || !heap_.contains(owner))
                    return std::nullopt;
                const auto* property = instance_property(
                    owner, *value->referenced_name->selected);
                if (property != nullptr) {
                    if (const auto form = array_form(property->type);
                        form && *form == TypeForm::dynamic_array) {
                        return capture_dynamic_property(owner, *property);
                    }
                    const auto& storage = heap_.property(
                        owner, property->canonical_identity);
                    const auto bounds = call_array_bounds(
                        property->type, owner, environment);
                    const auto actual_element
                        = call_array_element_type(property->type);
                    if (!bounds || !actual_element
                        || !is_fixed_array(property->type)
                        || !storage.handle_container
                        || storage.handle_container->kind()
                            != runtime::SystemVerilogClassContainerKind::FixedArray) {
                        return std::nullopt;
                    }
                    const auto distance = support::index_distance(
                        bounds->first, bounds->second);
                    if (distance == std::numeric_limits<std::uint64_t>::max()
                        || !std::in_range<std::size_t>(distance + 1U)
                        || storage.handle_container->size()
                            != static_cast<std::size_t>(distance + 1U)) {
                        throw std::invalid_argument {
                            "class HIR fixed-array property storage does not match its declared bounds"
                        };
                    }
                    array.type = property->type;
                    array.left = bounds->first;
                    array.right = bounds->second;
                    for (std::size_t index = 0U;
                         index < storage.handle_container->size(); ++index) {
                        CallValue element;
                        element.type = *actual_element;
                        element.packed = runtime::PackedLogic4::from_aval_bval(
                            64U, storage.handle_container->at(index), 0U);
                        array.elements.push_back(std::move(element));
                    }
                    validate_typed_actual(array, expected_type);
                    return array;
                }
            }
            if (value->text.starts_with(container_prefix)) {
                auto owner = handle;
                if (value->operands.size() > 1U)
                    return std::nullopt;
                if (value->operands.size() == 1U) {
                    const auto receiver = evaluate_packed(
                        value->operands.front(), handle, environment);
                    if (!receiver || receiver->low_word().bval != 0U)
                        return std::nullopt;
                    owner = receiver->low_word().aval;
                }
                const auto* property = instance_property(
                    owner, marker_payload(*value, container_prefix));
                if (property == nullptr || !heap_.contains(owner))
                    return std::nullopt;
                if (const auto form = array_form(property->type);
                    form && *form == TypeForm::dynamic_array) {
                    return capture_dynamic_property(owner, *property);
                }
                const auto& storage = heap_.property(
                    owner, property->canonical_identity);
                const auto bounds = call_array_bounds(
                    property->type, owner, environment);
                const auto actual_element
                    = call_array_element_type(property->type);
                if (!bounds || !actual_element
                    || !is_fixed_array(property->type)
                    || !storage.handle_container
                    || storage.handle_container->kind()
                        != runtime::SystemVerilogClassContainerKind::FixedArray) {
                    return std::nullopt;
                }
                const auto distance
                    = support::index_distance(bounds->first, bounds->second);
                if (distance == std::numeric_limits<std::uint64_t>::max()
                    || !std::in_range<std::size_t>(distance + 1U)
                    || storage.handle_container->size()
                        != static_cast<std::size_t>(distance + 1U)) {
                    throw std::invalid_argument {
                        "class HIR fixed-array property storage does not match its declared bounds"
                    };
                }
                array.type = property->type;
                array.left = bounds->first;
                array.right = bounds->second;
                for (std::size_t index = 0U;
                     index < storage.handle_container->size(); ++index) {
                    CallValue element;
                    element.type = *actual_element;
                    element.packed = runtime::PackedLogic4::from_aval_bval(
                        64U, storage.handle_container->at(index), 0U);
                    array.elements.push_back(std::move(element));
                }
                validate_typed_actual(array, expected_type);
                return array;
            }
            if (value->kind != ExpressionKind::assignment_pattern)
                return std::nullopt;
            array.left = 0;
            array.right = value->associations.empty()
                ? -1
                : static_cast<std::int64_t>(value->associations.size() - 1U);
            array.elements.reserve(value->associations.size());
            for (const auto& association : value->associations) {
                if (!association.choices.empty())
                    return std::nullopt;
                auto element = self(self, association.value, *element_type);
                if (!element)
                    return std::nullopt;
                array.elements.push_back(std::move(*element));
            }
            validate_typed_actual(array, expected_type);
            return array;
        }

        const auto* definition = call_aggregate_definition(type);
        if (definition != nullptr
            && definition->form == TypeForm::unpacked_structure) {
            if (value->kind != ExpressionKind::assignment_pattern)
                return std::nullopt;
            if (!value->nominal_type.empty()) {
                if (!value->nominal_type_target) {
                    throw std::invalid_argument {
                        "class HIR typed structure pattern has no resolved nominal type"
                    };
                }
                semantic::sv::TypeReference actual_type;
                actual_type.target.target = *value->nominal_type_target;
                actual_type.target.spelling = value->nominal_type;
                const auto* actual_definition
                    = call_aggregate_definition(actual_type);
                if (actual_definition == nullptr
                    || actual_definition->form
                        != TypeForm::unpacked_structure
                    || actual_definition->id != definition->id) {
                    throw std::invalid_argument {
                        "class HIR typed structure pattern is incompatible with its formal type"
                    };
                }
            }
            CallValue structure;
            structure.kind = CallValue::Kind::structure;
            structure.type = type;
            std::vector<std::optional<CallValue>> fields(
                definition->members.size());
            std::size_t next_positional { };
            for (const auto& association : value->associations) {
                std::size_t member_index = definition->members.size();
                if (association.choices.empty()) {
                    while (next_positional < fields.size()
                        && fields[next_positional]) {
                        ++next_positional;
                    }
                    member_index = next_positional++;
                } else {
                    if (association.choices.size() != 1U)
                        return std::nullopt;
                    const auto* choice = expression(
                        association.choices.front());
                    if (choice == nullptr)
                        return std::nullopt;
                    const auto& name = association.choice_spelling.empty()
                        ? choice->text : association.choice_spelling;
                    for (std::size_t index = 0U;
                         index < definition->members.size(); ++index) {
                        if (definition->members[index].name == name) {
                            member_index = index;
                            break;
                        }
                    }
                }
                if (member_index >= definition->members.size()
                    || fields[member_index]) {
                    return std::nullopt;
                }
                auto field = self(self, association.value,
                    definition->members[member_index].type);
                if (!field)
                    return std::nullopt;
                fields[member_index] = std::move(*field);
            }
            if (std::ranges::any_of(fields,
                    [](const auto& field) { return !field; })) {
                return std::nullopt;
            }
            for (std::size_t index = 0U; index < fields.size(); ++index) {
                structure.members.emplace_back(
                    definition->members[index].name,
                    std::make_shared<CallValue>(
                        std::move(*fields[index])));
            }
            return structure;
        }

        CallValue scalar;
        scalar.type = type;
        if (!type.class_identity.empty()) {
            if (!value->nominal_type.empty()) {
                scalar.type.class_identity = value->nominal_type;
            } else if (value->text != "@sv-null") {
                return std::nullopt;
            }
        }
        if (is_string(type)) {
            const auto text = evaluate_string(value_id,
                string_environment, handle);
            if (!text)
                return std::nullopt;
            scalar.kind = CallValue::Kind::string;
            scalar.text = *text;
            return scalar;
        }
        const auto packed = evaluate_packed(value_id, handle, environment);
        if (!packed)
            return std::nullopt;
        scalar.kind = CallValue::Kind::packed;
        scalar.packed = *packed;
        return scalar;
    };

    auto actual = evaluate_value(evaluate_value, id, expected_type);
    if (!actual)
        return std::nullopt;
    validate_typed_actual(*actual, expected_type);
    return actual;
}

std::optional<SystemVerilogClassHirExecution::CallValue>
SystemVerilogClassHirExecution::evaluate_bound_call_value(
    const semantic::ExpressionId id,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment)
{
    if (call_values_ == nullptr)
        return std::nullopt;
    const auto* value = expression(id);
    if (value == nullptr)
        return std::nullopt;
    if (value->kind == semantic::sv::ExpressionKind::name
        && value->referenced_name
        && value->referenced_name->selected) {
        const auto found = call_values_->find(
            *value->referenced_name->selected);
        return found == call_values_->end()
            ? std::nullopt
            : std::optional<CallValue> { found->second };
    }
    if (value->kind == semantic::sv::ExpressionKind::index
        && value->operands.size() == 2U) {
        auto array = evaluate_bound_call_value(
            value->operands.front(), handle, environment);
        if (!array || array->kind != CallValue::Kind::array)
            return std::nullopt;
        // This HIR form stores the selected member in text and keeps the
        // array and index as its two operands (for example `index.path`).
        constexpr std::string_view indexed_member_prefix { "index." };
        const bool selects_member
            = value->text.starts_with(indexed_member_prefix);
        const semantic::sv::TypeDefinition* formal_structure { };
        const semantic::sv::PackedMember* formal_member { };
        std::string_view member_name;
        if (selects_member) {
            if (value->text.size() <= indexed_member_prefix.size())
                return std::nullopt;
            const auto element_type = call_array_element_type(array->type);
            formal_structure = element_type
                ? call_aggregate_definition(*element_type) : nullptr;
            if (formal_structure == nullptr
                || formal_structure->form
                    != semantic::sv::TypeForm::unpacked_structure) {
                return std::nullopt;
            }
            member_name = std::string_view { value->text }
                .substr(indexed_member_prefix.size());
            const auto member = std::ranges::find(
                formal_structure->members, member_name,
                &semantic::sv::PackedMember::name);
            if (member == formal_structure->members.end()
                || !is_string(member->type)) {
                return std::nullopt;
            }
            formal_member = &*member;
        }
        const auto index_value = evaluate_packed(
            value->operands.back(), handle, environment);
        const auto index = index_value
            ? index_value->known_signed_value() : std::nullopt;
        if (!index || *index < std::min(array->left, array->right)
            || *index > std::max(array->left, array->right)) {
            return std::nullopt;
        }
        const auto ordinal = array->left <= array->right
            ? static_cast<std::uint64_t>(*index)
                - static_cast<std::uint64_t>(array->left)
            : static_cast<std::uint64_t>(array->left)
                - static_cast<std::uint64_t>(*index);
        if (!std::in_range<std::size_t>(ordinal)
            || static_cast<std::size_t>(ordinal) >= array->elements.size()) {
            return std::nullopt;
        }
        auto element = array->elements[static_cast<std::size_t>(ordinal)];
        if (!selects_member)
            return element;
        if (element.kind != CallValue::Kind::structure) {
            return std::nullopt;
        }
        const auto* actual_structure
            = call_aggregate_definition(element.type);
        if (actual_structure == nullptr
            || actual_structure->form
                != semantic::sv::TypeForm::unpacked_structure
            || formal_structure == nullptr
            || actual_structure->id != formal_structure->id) {
            return std::nullopt;
        }
        const auto actual_member = std::ranges::find(
            element.members, member_name,
            [](const auto& entry) -> const std::string& {
                return entry.first;
            });
        if (formal_member == nullptr
            || actual_member == element.members.end()
            || !actual_member->second
            || actual_member->second->kind != CallValue::Kind::string
            || !is_string(actual_member->second->type)) {
            return std::nullopt;
        }
        return *actual_member->second;
    }
    constexpr std::string_view selected_prefix { "@sv-select:" };
    if (value->kind == semantic::sv::ExpressionKind::call
        && value->text.starts_with(selected_prefix)
        && value->operands.size() == 1U) {
        auto aggregate = evaluate_bound_call_value(
            value->operands.front(), handle, environment);
        if (!aggregate || aggregate->kind != CallValue::Kind::structure)
            return std::nullopt;
        const auto name = marker_payload(*value, selected_prefix);
        const auto member = std::ranges::find(
            aggregate->members, name,
            [](const auto& entry) -> const std::string& { return entry.first; });
        return member == aggregate->members.end() || !member->second
            ? std::nullopt
            : std::optional<CallValue> { *member->second };
    }
    return std::nullopt;
}

std::optional<runtime::PackedLogic4>
SystemVerilogClassHirExecution::evaluate_packed(
    const semantic::ExpressionId id,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    const std::optional<std::size_t> context_width,
    const std::optional<bool> context_signed)
{
    const auto* value = expression(id);
    if (value == nullptr)
        return std::nullopt;
    const auto apply_context = [context_width, context_signed](
                                   const runtime::PackedLogic4& packed,
                                   const bool signed_value,
                                   const bool inherit_parent_signed = false)
        -> std::optional<runtime::PackedLogic4> {
        if (!context_width)
            return packed;
        const auto result_width = std::max(packed.width(), *context_width);
        const auto extend_signed = inherit_parent_signed && context_signed
            ? *context_signed : signed_value;
        return resize_expression_operand(packed, result_width, extend_signed);
    };
    // A value parameter of the object's class takes the value of the
    // object's specialization (IEEE 1800-2017 8.25), not its default.
    if (handle != 0U && read_owner_parameter_
        && value->kind == semantic::sv::ExpressionKind::name
        && value->referenced_name && value->referenced_name->selected) {
        const auto selected = *value->referenced_name->selected;
        const bool class_parameter
            = hir_index().class_value_parameters.contains(selected.value());
        if (class_parameter) {
            const auto binding = read_owner_parameter_(handle, selected);
            if (binding.found && binding.value) {
                const auto* declared = declaration(selected);
                return apply_context(*binding.value,
                    declared != nullptr && declared->type
                        && declared->type->signed_value);
            }
        }
    }
    if (auto aggregate = evaluate_bound_call_value(id, handle, environment)) {
        if (aggregate->kind == CallValue::Kind::packed && aggregate->packed) {
            return apply_context(
                *aggregate->packed, aggregate->type.signed_value, true);
        }
        return std::nullopt;
    }
    using semantic::sv::ExpressionKind;
    if (value->kind == ExpressionKind::call
        && value->text.starts_with("@sv-container-index:")) {
        return evaluate_class_handle_index(*value, handle, environment);
    }
    // `T'(x)`: an integral cast to a built-in, sign or size target, or to a
    // typedef of a packed integral type (IEEE 1800-2017 6.24.1).
    if (constexpr auto cast_prefix = std::string_view { "@sv-cast:" };
        value->kind == ExpressionKind::call
        && value->text.starts_with(cast_prefix)
        && value->operands.size() == 1U) {
        const auto name = std::string_view { value->text }.substr(
            cast_prefix.size());
        const auto* operand_expression = expression(value->operands.front());
        auto operand = evaluate_packed(
            value->operands.front(), handle, environment);
        if (!operand || operand_expression == nullptr) {
            return std::nullopt;
        }
        const bool operand_signed = operand_expression->signed_value;
        std::optional<std::size_t> target_width;
        bool target_signed = operand_signed;
        if (name == "byte" || name == "shortint" || name == "int"
            || name == "longint" || name == "integer") {
            target_width = name == "byte" ? 8U
                : name == "shortint"      ? 16U
                : name == "longint"       ? 64U
                                          : 32U;
            target_signed = true;
        } else if (name == "bit" || name == "logic" || name == "reg") {
            target_width = 1U;
            target_signed = false;
        } else if (name == "signed" || name == "unsigned") {
            target_width = operand->width();
            target_signed = name == "signed";
        } else if (!name.empty()
            && std::ranges::all_of(name, [](const char c) {
                   return c >= '0' && c <= '9';
               })) {
            target_width = static_cast<std::size_t>(
                std::strtoull(std::string { name }.c_str(), nullptr, 10));
        } else if (const auto typedefs
                   = hir_index().typedefs_by_name.find(name);
                   typedefs != hir_index().typedefs_by_name.end()) {
            for (const auto* candidate : typedefs->second) {
                if (!candidate->type) {
                    continue;
                }
                const auto candidate_width = width(*candidate->type, 0U);
                if (candidate_width == 0U) {
                    continue;
                }
                target_width = candidate_width;
                target_signed = candidate->type->signed_value;
                break;
            }
        }
        if (!target_width || *target_width == 0U) {
            return std::nullopt;
        }
        const auto converted = resize_expression_operand(
            *operand, *target_width, operand_signed);
        if (!converted) {
            return std::nullopt;
        }
        return apply_context(*converted, target_signed);
    }
    if (value->kind == ExpressionKind::index) {
        const auto indexed = evaluate_class_handle_index(
            *value, handle, environment);
        if (indexed)
            return indexed;
    }
    const auto package_container_id = [&](
                                          const semantic::ExpressionId receiver_id)
        -> std::optional<semantic::DeclarationId> {
        const auto* receiver = expression(receiver_id);
        if (receiver == nullptr
            || receiver->kind != ExpressionKind::name
            || !receiver->referenced_name
            || !receiver->referenced_name->selected
            || !is_package_global(*receiver->referenced_name->selected)) {
            return std::nullopt;
        }
        return *receiver->referenced_name->selected;
    };
    // `++x`, `x--` of a local (IEEE 1800-2017 11.4.2).
    if (value->kind == ExpressionKind::update && value->operands.size() == 1U
        && (value->text == "pre++" || value->text == "pre--"
            || value->text == "post++" || value->text == "post--")) {
        const auto* target = expression(value->operands.front());
        if (target == nullptr || target->kind != ExpressionKind::name) {
            return std::nullopt;
        }
        std::optional<runtime::PackedLogic4> before_value;
        const auto selected = target->referenced_name
            && target->referenced_name->selected
            ? *target->referenced_name->selected
            : semantic::DeclarationId { };
        const auto* reference = target->referenced_name
                && target->referenced_name->selected
            ? active_reference(selected) : nullptr;
        if (reference != nullptr) {
            if (!reference->string_value && reference->read_packed)
                before_value = reference->read_packed();
        } else if (target->referenced_name
            && target->referenced_name->selected
            && is_package_global(selected)) {
            if (read_package_packed_)
                before_value = read_package_packed_(selected);
        } else if (const auto found = environment.find(target->text);
            found != environment.end()) {
            before_value = found->second;
        }
        if (!before_value || before_value->width() == 0U
            || before_value->width() > 64U) {
            return std::nullopt;
        }
        const auto before = *before_value;
        const auto word = before.low_word();
        const auto width = before.width();
        const auto mask = width == 64U ? ~std::uint64_t { 0 }
                                       : (std::uint64_t { 1 } << width) - 1U;
        const auto after = word.bval != 0U
            ? runtime::PackedLogic4(width, runtime::Logic4::x)
            : runtime::PackedLogic4::from_aval_bval(width,
                  (value->text.ends_with("++") ? word.aval + 1U
                                               : word.aval - 1U) & mask,
                  0U);
        const bool stored = reference != nullptr
            ? reference->write_packed && reference->write_packed(after)
            : target->referenced_name
                && target->referenced_name->selected
                && is_package_global(selected)
            ? write_package_packed_
                && write_package_packed_(selected, after)
            : [&] {
                  const auto found = environment.find(target->text);
                  if (found == environment.end())
                      return false;
                  found->second = after;
                  return true;
              }();
        if (!stored)
            return std::nullopt;
        const auto* target_declaration = target->referenced_name
                && target->referenced_name->selected
            ? declaration(*target->referenced_name->selected) : nullptr;
        const bool target_signed = target_declaration != nullptr
                && target_declaration->type
            ? target_declaration->type->signed_value
            : target->signed_value;
        return apply_context(
            value->text.starts_with("post") ? before : after,
            target_signed, true);
    }
    if (value->kind == ExpressionKind::name) {
        if (value->text == "this" || value->text == "super") {
            return apply_context(
                runtime::PackedLogic4::from_aval_bval(64U, handle, 0U),
                false);
        }
        const auto* selected_declaration = value->referenced_name
                && value->referenced_name->selected
            ? declaration(*value->referenced_name->selected) : nullptr;
        const bool selected_signed = selected_declaration != nullptr
                && selected_declaration->type
            ? selected_declaration->type->signed_value
            : value->signed_value;
        bool selected_package_enum_literal { };
        if (value->referenced_name
            && value->referenced_name->selected) {
            const auto selected = *value->referenced_name->selected;
            if (const auto* reference = active_reference(selected)) {
                if (reference->string_value || !reference->read_packed)
                    return std::nullopt;
                const auto packed = reference->read_packed();
                return packed ? apply_context(*packed, selected_signed, true)
                              : std::nullopt;
            }
            if (is_package_global(selected)) {
                if (!read_package_packed_)
                    return std::nullopt;
                const auto packed = read_package_packed_(selected);
                return packed ? apply_context(*packed, selected_signed, true)
                              : std::nullopt;
            }
            if (is_package_constant(selected)) {
                const auto* constant = selected_declaration;
                if (constant != nullptr
                    && constant->form
                        == semantic::sv::DeclarationForm::enumeration_literal) {
                    selected_package_enum_literal = true;
                } else {
                    if (constant == nullptr || !constant->initializer
                        || *constant->initializer == id
                        || constant_depth_ >= 64U) {
                        return std::nullopt;
                    }
                    ++constant_depth_;
                    const auto result = evaluate_packed(
                        *constant->initializer, handle, environment,
                        constant->type
                            ? std::optional<std::size_t> {
                                  width(*constant->type, 0U) }
                            : std::nullopt);
                    --constant_depth_;
                    return result
                        ? apply_context(*result, selected_signed, true)
                        : std::nullopt;
                }
            }
        }
        if (!selected_package_enum_literal) {
            const auto found = environment.find(value->text);
            if (found != environment.end())
                return apply_context(found->second, selected_signed, true);
        }
        bool owner_context_available { };
        if (handle != 0U && value->referenced_name
            && value->referenced_name->selected
            && read_owner_parameter_) {
            const auto binding = read_owner_parameter_(
                handle, *value->referenced_name->selected);
            owner_context_available = binding.context_available;
            if (binding.found && binding.value)
                return apply_context(
                    *binding.value, selected_signed, true);
        }
        // A class-scope constant or enumeration literal (IEEE 1800-2017
        // 8.25, 6.19) evaluates its declaration's constant initializer.
        const auto* constant = value->referenced_name
                && value->referenced_name->selected
            ? declaration(*value->referenced_name->selected)
            : nullptr;
        if (constant != nullptr && read_owner_parameter_
            && (constant->form
                    == semantic::sv::DeclarationForm::local_parameter
                || constant->form
                    == semantic::sv::DeclarationForm::parameter)) {
            const auto& index = hir_index();
            const bool class_constant
                = index.class_parameters.contains(constant->id.value())
                || index.class_members.contains(constant->id.value())
                || index.class_scopes.contains(constant->scope.value());
            const bool package_constant
                = hir_index().packages_by_declaration.contains(
                    constant->id.value());
            const bool owner_local_parameter
                = constant->form
                    == semantic::sv::DeclarationForm::local_parameter
                && owner_context_available;
            if (!class_constant && !package_constant
                && !owner_local_parameter) {
                // A module parameter's source default is not a substitute
                // for a missing actual, and a package/global allocation has
                // no module owner from which to specialize it.
                return std::nullopt;
            }
            // A module localparam is not an override. When its value was not
            // materialized in the owner map, the retained initializer below
            // evaluates against that exact owner's parameter bindings.
        }
        if (constant != nullptr && constant->initializer
            && *constant->initializer != id
            && (constant->form
                    == semantic::sv::DeclarationForm::local_parameter
                || constant->form == semantic::sv::DeclarationForm::parameter
                || constant->form
                    == semantic::sv::DeclarationForm::enumeration_literal)
            && constant_depth_ < 64U) {
            ++constant_depth_;
            auto result = evaluate_packed(
                *constant->initializer, handle, environment,
                constant->type
                    ? std::optional<std::size_t> {
                          width(*constant->type, 0U) }
                    : std::nullopt);
            --constant_depth_;
            return result ? apply_context(*result, selected_signed, true)
                          : std::nullopt;
        }
        // A hierarchical instance reference such as `test.A` must belong to
        // this object's exact owning module occurrence. A same-named module
        // definition elsewhere in the design is not enough.
        if (constant_depth_ < 64U) {
            std::optional<semantic::DeclarationId>
                qualified_literal_declaration;
            std::string_view literal_name { value->text };
            if (selected_package_enum_literal
                && value->referenced_name
                && value->referenced_name->selected) {
                qualified_literal_declaration
                    = *value->referenced_name->selected;
            }
            const auto dot = value->text.find('.');
            if (dot != std::string::npos
                && value->text.find('.', dot + 1U) == std::string::npos
                && value->text.find("::") == std::string::npos
                && handle != 0U && read_owner_parameter_) {
                const auto unit_name
                    = std::string_view { value->text }.substr(0U, dot);
                const auto selected_literal_name
                    = std::string_view { value->text }.substr(dot + 1U);
                bool ambiguous_owner_literal { };
                const auto& index = hir_index();
                const auto modules = index.modules_by_name.find(unit_name);
                for (const auto* module : modules == index.modules_by_name.end()
                         ? std::span<const semantic::sv::Unit* const> { }
                         : std::span { modules->second }) {
                    const auto& unit = *module;
                    const semantic::sv::Declaration*
                        unit_literal_declaration { };
                    bool ambiguous_declaration { };
                    const auto literals
                        = index.enumeration_literals_by_scope.find(
                            unit.scope.value());
                    for (const auto* candidate :
                        literals == index.enumeration_literals_by_scope.end()
                            ? std::span<const semantic::sv::Declaration* const> { }
                            : std::span { literals->second }) {
                        if (candidate->name != selected_literal_name) {
                            continue;
                        }
                        if (unit_literal_declaration != nullptr
                            && unit_literal_declaration->id != candidate->id) {
                            ambiguous_declaration = true;
                            break;
                        }
                        unit_literal_declaration = candidate;
                    }
                    if (ambiguous_declaration
                        || unit_literal_declaration == nullptr
                        || unit_literal_declaration->form
                            != semantic::sv::DeclarationForm::enumeration_literal) {
                        continue;
                    }
                    const auto owner = read_owner_parameter_(
                        handle, unit_literal_declaration->id);
                    const bool owner_matches
                        = owner.owner_unit && *owner.owner_unit == unit.id
                        && owner.owner_path == unit_name;
                    if (!owner_matches) {
                        continue;
                    }
                    if (qualified_literal_declaration
                        && *qualified_literal_declaration
                            != unit_literal_declaration->id) {
                        ambiguous_owner_literal = true;
                        break;
                    }
                    qualified_literal_declaration
                        = unit_literal_declaration->id;
                }
                if (!ambiguous_owner_literal
                    && qualified_literal_declaration) {
                    literal_name = selected_literal_name;
                } else {
                    qualified_literal_declaration.reset();
                }
            } else if (const auto separator = value->text.rfind("::");
                separator != std::string::npos) {
                // A package-qualified literal names the same literal.
                literal_name = std::string_view { value->text }.substr(
                    separator + 2U);
            }
            std::optional<runtime::PackedLogic4> literal_value;
            std::size_t matches { };
            // Only the types with a literal of this name can match.
            const auto& literal_types
                = hir_index().types_by_enumeration_literal;
            const auto named_types = literal_types.find(literal_name);
            for (const auto* type : named_types == literal_types.end()
                     ? std::span<const semantic::sv::TypeDefinition* const> { }
                     : std::span { named_types->second }) {
                const auto& definition = *type;
                if (qualified_literal_declaration
                    && std::ranges::none_of(
                        definition.enumeration_literals,
                        [&](const auto& literal) {
                            return literal.declaration
                                == *qualified_literal_declaration;
                        })) {
                    continue;
                }
                std::optional<runtime::PackedLogic4> previous;
                for (const auto& literal : definition.enumeration_literals) {
                    std::optional<runtime::PackedLogic4> current;
                    if (literal.value) {
                        ++constant_depth_;
                        PackedEnvironment none;
                        current = evaluate_packed(*literal.value, handle, none);
                        --constant_depth_;
                    } else if (!previous) {
                        current = runtime::PackedLogic4::from_aval_bval(
                            64U, 0U, 0U);
                    } else if (previous->low_word().bval == 0U) {
                        current = runtime::PackedLogic4::from_aval_bval(
                            64U, previous->low_word().aval + 1U, 0U);
                    }
                    previous = current;
                    if (literal.name == literal_name
                        && (!qualified_literal_declaration
                            || literal.declaration
                                == *qualified_literal_declaration)) {
                        // The same literal reached through several types
                        // (a typedef of the enumeration) has one value.
                        if (matches == 0U || literal_value != current) {
                            ++matches;
                        }
                        literal_value = current;
                    }
                }
            }
            if (matches == 1U && literal_value)
                return apply_context(*literal_value, selected_signed, true);
        }
        return std::nullopt;
    }
    if (value->kind == ExpressionKind::integer_literal) {
        std::int64_t integer { };
        const auto parsed = std::from_chars(
            value->text.data(), value->text.data() + value->text.size(),
            integer, 10);
        if (parsed.ec != std::errc { }
            || parsed.ptr != value->text.data() + value->text.size()) {
            return std::nullopt;
        }
        const auto literal_width
            = integer >= std::numeric_limits<std::int32_t>::min()
                    && integer <= std::numeric_limits<std::int32_t>::max()
            ? 32U : 64U;
        const auto literal = runtime::PackedLogic4::from_aval_bval(
            literal_width, static_cast<std::uint64_t>(integer), 0U);
        return apply_context(literal, true, true);
    }
    if (value->kind == ExpressionKind::boolean_literal) {
        if (value->text == "true" || value->text == "1")
            return apply_context(
                runtime::PackedLogic4::from_aval_bval(1U, 1U, 0U), false);
        if (value->text == "false" || value->text == "0")
            return apply_context(
                runtime::PackedLogic4::from_aval_bval(1U, 0U, 0U), false);
        return std::nullopt;
    }
    if (value->kind == ExpressionKind::logic_literal) {
        const auto literal = logic_literal(value->text);
        if (!literal)
            return std::nullopt;
        const bool unbased_unsized = value->text.size() == 2U
            && value->text.front() == '\'';
        if (unbased_unsized && context_width) {
            const auto state = literal->get(0U);
            return runtime::PackedLogic4(*context_width, state);
        }
        return apply_context(*literal, value->signed_value, true);
    }
    if (value->kind == ExpressionKind::class_null
        || value->text == "@sv-null") {
        return apply_context(
            runtime::PackedLogic4::from_aval_bval(64U, 0U, 0U), false);
    }
    constexpr std::string_view property_prefix { "@sv-property:" };
    if (value->kind == ExpressionKind::class_property
        || value->text.starts_with(property_prefix)) {
        auto owner = handle;
        if (!value->operands.empty()) {
            const auto receiver = evaluate_packed(
                value->operands.front(), handle, environment);
            if (!receiver || receiver->low_word().bval != 0U)
                return std::nullopt;
            owner = receiver->low_word().aval;
        }
        const auto identity = marker_payload(*value, property_prefix);
        const auto packed = read_property_(owner, identity);
        const auto* property = instance_property(owner, identity);
        return apply_context(
            packed, property != nullptr && property->type.signed_value, true);
    }
    constexpr std::string_view static_property_prefix {
        "@sv-static-property:"
    };
    if (value->kind == ExpressionKind::class_static_property
        || value->text.starts_with(static_property_prefix)) {
        const auto identity = marker_payload(*value, static_property_prefix);
        const auto packed = read_static_property_(identity, handle);
        const auto* property = static_class_property(
            specializations_, identity,
            value->referenced_name
                ? value->referenced_name->selected : std::nullopt);
        return apply_context(
            packed, property != nullptr && property->type.signed_value, true);
    }
    constexpr std::string_view method_prefix { "@sv-method:" };
    constexpr std::string_view base_method_prefix { "@sv-base-method:" };
    if ((value->kind == ExpressionKind::class_method_call
            || value->text.starts_with(method_prefix)
            || value->text.starts_with(base_method_prefix))
        && !value->operands.empty()) {
        const auto virtual_dispatch
            = !value->text.starts_with(base_method_prefix);
        const auto receiver = evaluate_packed(
            value->operands.front(), handle, environment);
        if (!receiver || receiver->low_word().bval != 0U) {
            return std::nullopt;
        }
        const auto identity = marker_payload(*value,
            virtual_dispatch ? method_prefix : base_method_prefix);
        // An inline constraint block needs the compiled call; the method
        // body is not interpreted without it.
        if (identity == "@builtin-randomize"
            && std::ranges::any_of(value->associations,
                [](const semantic::sv::AssignmentPatternAssociation& item) {
                    return item.choice_spelling == "@sv-inline-constraint";
                })) {
            return std::nullopt;
        }
        const auto receiver_handle = receiver->low_word().aval;
        const auto* selected_method = method(
            receiver_handle, identity, virtual_dispatch);
        const auto actual_count = value->operands.size() - 1U;
        std::vector<std::string> names(actual_count);
        if (value->argument_names.size() == value->operands.size()) {
            std::ranges::copy(
                value->argument_names | std::views::drop(1), names.begin());
        }
        std::vector<std::size_t> mapped_formals;
        if (selected_method != nullptr) {
            try {
                mapped_formals = map_actual_formals(
                    *selected_method, names, actual_count);
            } catch (const std::invalid_argument&) {
                // Let the configured dispatcher retain responsibility for
                // reporting malformed argument associations.
            }
        }
        std::vector<runtime::PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        CallValueEnvironment typed_actuals;
        StringEnvironment empty_strings;
        auto& current_strings = strings_ != nullptr
            ? *strings_ : empty_strings;
        for (std::size_t index = 0U; index < actual_count; ++index) {
            const auto operand = value->operands[index + 1U];
            const semantic::sv::Declaration* formal { };
            if (selected_method != nullptr
                && mapped_formals.size() == actual_count) {
                formal = declaration(selected_method->formals[
                    mapped_formals[index]]);
            }
            if (formal != nullptr && formal->type
                && is_input(formal->direction)
                && is_typed_call_value(*formal->type)) {
                auto typed = evaluate_typed_actual(operand, *formal->type,
                    handle, environment, current_strings);
                if (!typed) {
                    throw std::invalid_argument {
                        "class HIR typed array actual is not executable"
                    };
                }
                typed_actuals.emplace(formal->id, std::move(*typed));
                actuals.emplace_back(64U, runtime::Logic4::zero);
                string_actuals.emplace_back();
                continue;
            }
            // An actual is passed as both a packed value and text where it
            // has them; the callee takes the form its formal's type needs.
            const auto* source = expression(operand);
            auto text = source != nullptr
                    && (source->kind == ExpressionKind::string_literal
                        || (source->kind == ExpressionKind::name
                            && strings_ != nullptr
                            && strings_->contains(source->text)))
                ? evaluate_string(operand, current_strings, handle)
                : std::optional<std::string> { };
            const auto actual = text
                ? std::optional<runtime::PackedLogic4> { }
                : evaluate_packed(operand, handle, environment,
                      formal != nullptr && formal->type
                          && is_input(formal->direction)
                          ? std::optional<std::size_t> {
                                width(*formal->type, 0U) }
                          : std::nullopt);
            if (!actual && !text && strings_ != nullptr) {
                text = evaluate_string(operand, *strings_, handle);
            }
            if (!actual && !text) {
                return std::nullopt;
            }
            actuals.push_back(actual.value_or(
                runtime::PackedLogic4(64U, runtime::Logic4::zero)));
            string_actuals.push_back(text.value_or(std::string { }));
        }
        std::vector<std::uint8_t> directions;
        const bool typed_frame = !typed_actuals.empty();
        if (typed_frame) {
            pending_call_frames_.push_back({
                selected_method->canonical_identity, receiver_handle,
                std::move(typed_actuals), false });
        }
        const PendingFrameGuard pending_guard { pending_call_frames_,
            typed_frame };
        auto result = invoke_function_(receiver_handle, identity,
            actuals, string_actuals, names, directions, virtual_dispatch,
            ActualReferences { });
        if (typed_frame && !pending_call_frames_.back().consumed) {
            throw std::invalid_argument {
                "class HIR typed actual did not reach its selected method"
            };
        }
        return selected_method != nullptr && selected_method->return_type
            ? apply_context(result, selected_method->return_type->signed_value,
                  true)
            : std::optional<runtime::PackedLogic4> { result };
    }
    if ((value->kind == ExpressionKind::class_allocation
            || value->text.starts_with("@sv-new:"))
        && construct_ && !value->class_identity.empty()) {
        std::vector<runtime::PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        for (const auto operand : value->operands) {
            const auto* actual = expression(operand);
            const bool string_actual = actual != nullptr
                && (actual->kind == ExpressionKind::string_literal
                    || (actual->kind == ExpressionKind::name
                        && strings_ != nullptr
                        && strings_->contains(actual->text)));
            if (string_actual) {
                StringEnvironment empty_strings;
                auto& current_strings = strings_ != nullptr
                    ? *strings_ : empty_strings;
                auto text = evaluate_string(
                    operand, current_strings, handle);
                if (!text)
                    return std::nullopt;
                actuals.emplace_back(64U);
                string_actuals.push_back(std::move(*text));
                continue;
            }
            const auto packed = evaluate_packed(operand, handle, environment);
            if (!packed)
                return std::nullopt;
            actuals.push_back(*packed);
            string_actuals.emplace_back();
        }
        auto names = value->argument_names;
        names.resize(actuals.size());
        const auto object = construct_(
            value->class_identity, actuals, string_actuals, names, handle);
        return runtime::PackedLogic4::from_aval_bval(64U, object, 0U);
    }
    constexpr std::string_view static_method_prefix {
        "@sv-static-method:"
    };
    if (value->kind == ExpressionKind::call
        && (value->text == "$size" || value->text == "$left"
            || value->text == "$right" || value->text == "$low"
            || value->text == "$high" || value->text == "$increment")
        && (value->operands.size() == 1U
            || value->operands.size() == 2U)) {
        const auto declaration_id
            = package_container_id(value->operands.front());
        if (!declaration_id) {
            std::size_t query_dimension { 1U };
            if (value->operands.size() == 2U) {
                const auto dimension = evaluate_packed(
                    value->operands[1U], handle, environment);
                if (!dimension)
                    return std::nullopt;
                const auto known_dimension
                    = dimension->known_signed_value();
                if (!known_dimension)
                    return std::nullopt;
                if (*known_dimension <= 0) {
                    query_dimension = 0U;
                } else if (!std::in_range<std::size_t>(*known_dimension)) {
                    query_dimension = std::numeric_limits<std::size_t>::max();
                } else {
                    query_dimension
                        = static_cast<std::size_t>(*known_dimension);
                }
            }
            const auto result = evaluate_class_array_query(
                value->operands.front(), value->text, query_dimension,
                handle, environment);
            return result ? apply_context(*result, true, true)
                          : std::nullopt;
        }
        if (!read_package_container_) {
            return std::nullopt;
        }
        if (value->operands.size() == 2U) {
            const auto dimension = evaluate_packed(
                value->operands[1U], handle, environment);
            if (!dimension) {
                return std::nullopt;
            }
            const auto known_dimension = dimension->known_unsigned_value();
            if (!known_dimension || *known_dimension != 1U) {
                return std::nullopt;
            }
        }
        const auto container = read_package_container_(*declaration_id);
        if (container && container->type.queue) {
            const auto size = runtime::simir::container_value_size(*container);
            const auto right = size == 0U
                ? std::int64_t { -1 }
                : static_cast<std::int64_t>(size - 1U);
            const auto answer = value->text == "$size"
                ? static_cast<std::int64_t>(size)
                : value->text == "$left" || value->text == "$low"
                ? std::int64_t { 0 }
                : value->text == "$right" || value->text == "$high"
                ? right
                : std::int64_t { -1 };
            return apply_context(runtime::PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(answer), 0U), true, true);
        }
    }
    constexpr std::string_view container_method_prefix {
        "@sv-container-method:"
    };
    if (value->kind == ExpressionKind::call
        && (value->text.starts_with(container_method_prefix)
            || value->text.starts_with('.'))
        && !value->operands.empty()) {
        const auto operation = value->text.starts_with(
                container_method_prefix)
            ? std::string_view { value->text }.substr(
                  container_method_prefix.size())
            : std::string_view { value->text }.substr(1U);
        const auto separator = operation.find(':');
        const auto method_name = operation.substr(0U, separator);
        const auto declaration_id
            = package_container_id(value->operands.front());
        if (declaration_id && method_name == "push_back"
            && value->operands.size() == 2U) {
            StringEnvironment empty_strings;
            auto& current_strings = strings_ != nullptr
                ? *strings_ : empty_strings;
            if (push_package_queue_back(value->operands.front(),
                    value->operands[1U], handle, environment,
                    current_strings)) {
                return runtime::PackedLogic4::from_aval_bval(
                    1U, 0U, 0U);
            }
            return std::nullopt;
        }
        // size() and reductions without a with-clause of an object's array
        // of values (IEEE 1800-2017 7.5.1, 7.12.3).
        if (!declaration_id && separator != std::string_view::npos
            && value->operands.size() == 1U && read_class_property_container_
            && (method_name == "size" || method_name == "sum"
                || method_name == "product" || method_name == "and"
                || method_name == "or" || method_name == "xor")) {
            const auto receiver = evaluate_packed(
                value->operands.front(), handle, environment, 64U);
            if (!receiver || receiver->width() != 64U) {
                return std::nullopt;
            }
            const auto word = receiver->low_word();
            if (word.bval != 0U) {
                return std::nullopt;
            }
            const auto stored = read_class_property_container_(
                word.aval, std::string { operation.substr(separator + 1U) });
            if (!stored) {
                return std::nullopt;
            }
            if (method_name == "size") {
                return apply_context(runtime::PackedLogic4::from_aval_bval(
                    32U, runtime::simir::container_value_size(*stored), 0U),
                    true, true);
            }
            if (stored->type.element_kind
                    != runtime::simir::ContainerElementKind::Packed
                && stored->type.element_kind
                    != runtime::simir::ContainerElementKind::Scalar) {
                return std::nullopt;
            }
            using Reduction = runtime::simir::ContainerReductionOperator;
            const auto reduction = method_name == "sum" ? Reduction::sum
                : method_name == "product" ? Reduction::product
                : method_name == "and"     ? Reduction::bit_and
                : method_name == "or"      ? Reduction::bit_or
                                           : Reduction::bit_xor;
            return apply_context(runtime::simir::reduce_container_value(
                *stored, reduction), stored->type.signed_elements, true);
        }
        auto container = declaration_id && read_package_container_
            ? read_package_container_(*declaration_id)
            : std::nullopt;
        if (container && container->type.queue) {
            auto& contents = *container;
            if (method_name == "size" && value->operands.size() == 1U) {
                return apply_context(runtime::PackedLogic4::from_aval_bval(
                    32U,
                    runtime::simir::container_value_size(contents), 0U),
                    true, true);
            }
            if (method_name == "delete" && value->operands.size() == 1U) {
                if (!write_package_container_) {
                    return std::nullopt;
                }
                contents.elements.clear();
                contents.string_elements.clear();
                contents.nested_elements.clear();
                contents.keys.clear();
                contents.string_keys.clear();
                if (write_package_container_(*declaration_id,
                        std::move(contents))) {
                    return runtime::PackedLogic4::from_aval_bval(
                        1U, 0U, 0U);
                }
                return std::nullopt;
            }
        }
    }
    if (value->kind == ExpressionKind::class_static_method_call
        || value->text.starts_with(static_method_prefix)) {
        const auto identity = marker_payload(*value, static_method_prefix);
        const auto* selected_method = static_method(identity);
        auto names = value->argument_names;
        if (names.empty())
            names.resize(value->operands.size());
        std::vector<std::size_t> mapped_formals;
        if (selected_method != nullptr) {
            try {
                mapped_formals = map_actual_formals(
                    *selected_method, names, value->operands.size());
            } catch (const std::invalid_argument&) {
                // Keep the configured static dispatcher responsible for
                // malformed argument associations.
            }
        }
        std::vector<runtime::PackedLogic4> actuals;
        for (std::size_t index = 0U; index < value->operands.size(); ++index) {
            const auto operand = value->operands[index];
            const semantic::sv::Declaration* formal { };
            if (selected_method != nullptr
                && mapped_formals.size() == value->operands.size()) {
                formal = declaration(selected_method->formals[
                    mapped_formals[index]]);
            }
            const auto actual = evaluate_packed(
                operand, handle, environment,
                formal != nullptr && formal->type
                    && is_input(formal->direction)
                    ? std::optional<std::size_t> {
                          width(*formal->type, 0U) }
                    : std::nullopt);
            if (!actual)
                return std::nullopt;
            actuals.push_back(*actual);
        }
        std::vector<std::string> string_actuals(actuals.size());
        const auto result = invoke_static_function_(
            identity, actuals,
            string_actuals, names, { }, ActualReferences { });
        return selected_method != nullptr && selected_method->return_type
            ? apply_context(result, selected_method->return_type->signed_value,
                  true)
            : std::optional<runtime::PackedLogic4> { result };
    }
    if (value->kind == ExpressionKind::call
        && value->referenced_name
        && value->referenced_name->selected) {
        StringEnvironment empty_strings;
        auto& strings = strings_ != nullptr ? *strings_ : empty_strings;
        const auto result = invoke_package_function(
            *value, handle, environment, strings);
        const auto* selected = declaration(
            *value->referenced_name->selected);
        return result
            ? apply_context(*result,
                  selected != nullptr && selected->callable
                      ? selected->callable->return_type.signed_value
                      : value->signed_value,
                  true)
            : std::nullopt;
    }
    struct ExpressionShape {
        std::size_t width { };
        bool signed_value { };
    };
    const auto expression_type = [this, handle](
                                     auto&& self,
                                     const semantic::ExpressionId expression_id,
                                     const std::size_t depth)
        -> std::optional<semantic::sv::TypeReference> {
        if (depth >= 64U)
            return std::nullopt;
        const auto* selected_expression = expression(expression_id);
        if (selected_expression == nullptr)
            return std::nullopt;
        if (selected_expression->kind == ExpressionKind::name
            && selected_expression->referenced_name
            && selected_expression->referenced_name->selected) {
            const auto* selected = declaration(
                *selected_expression->referenced_name->selected);
            if (selected != nullptr && selected->type)
                return *selected->type;
        }
        if (selected_expression->kind == ExpressionKind::call
            && (selected_expression->text == "$size"
                || selected_expression->text == "$left"
                || selected_expression->text == "$right"
                || selected_expression->text == "$low"
                || selected_expression->text == "$high"
                || selected_expression->text == "$increment")) {
            semantic::sv::TypeReference query_result;
            query_result.value_form
                = semantic::sv::TypeForm::packed_integral;
            query_result.signed_value = true;
            query_result.executable_width = 32U;
            query_result.four_state = true;
            return query_result;
        }
        if (selected_expression->kind == ExpressionKind::class_property
            || selected_expression->text.starts_with("@sv-property:")) {
            if (selected_expression->operands.empty()) {
                const auto* property = instance_property(
                    handle, marker_payload(
                        *selected_expression, "@sv-property:"));
                if (property != nullptr)
                    return property->type;
            }
            if (selected_expression->operands.size() == 1U) {
                const auto* receiver
                    = expression(selected_expression->operands.front());
                if (receiver != nullptr
                    && receiver->kind == ExpressionKind::name
                    && receiver->text == "this") {
                    const auto* property = instance_property(
                        handle, marker_payload(
                            *selected_expression, "@sv-property:"));
                    if (property != nullptr)
                        return property->type;
                }
            }
        }
        if (selected_expression->kind
                == ExpressionKind::class_static_property
            || selected_expression->text.starts_with(
                "@sv-static-property:")) {
            const auto* property = static_class_property(
                specializations_, marker_payload(
                    *selected_expression, "@sv-static-property:"),
                selected_expression->referenced_name
                    ? selected_expression->referenced_name->selected
                    : std::nullopt);
            if (property != nullptr)
                return property->type;
        }
        constexpr std::string_view static_container_prefix {
            "@sv-static-container-property:"
        };
        if (selected_expression->text.starts_with(static_container_prefix)) {
            const auto* property = static_class_property(
                specializations_, marker_payload(
                    *selected_expression, static_container_prefix),
                selected_expression->referenced_name
                    ? selected_expression->referenced_name->selected
                    : std::nullopt);
            if (property != nullptr)
                return property->type;
        }
        if (selected_expression->kind == ExpressionKind::call
            && selected_expression->referenced_name
            && selected_expression->referenced_name->selected) {
            const auto* selected = declaration(
                *selected_expression->referenced_name->selected);
            if (selected != nullptr && selected->callable
                && selected->callable->function) {
                return selected->callable->return_type;
            }
        }
        constexpr std::string_view method_prefix { "@sv-method:" };
        constexpr std::string_view base_method_prefix {
            "@sv-base-method:"
        };
        constexpr std::string_view static_method_prefix {
            "@sv-static-method:"
        };
        const bool static_method_call
            = selected_expression->kind
                    == ExpressionKind::class_static_method_call
            || selected_expression->text.starts_with(static_method_prefix);
        const bool instance_method_call
            = selected_expression->kind == ExpressionKind::class_method_call
            || selected_expression->text.starts_with(method_prefix)
            || selected_expression->text.starts_with(base_method_prefix);
        if (static_method_call || instance_method_call) {
            const auto identity = marker_payload(
                *selected_expression, static_method_call
                    ? static_method_prefix
                    : selected_expression->text.starts_with(
                          base_method_prefix)
                    ? base_method_prefix : method_prefix);
            if (static_method_call) {
                const auto* selected = static_method(identity);
                if (selected != nullptr && selected->return_type)
                    return selected->return_type;
            } else {
                std::optional<semantic::sv::TypeReference> result_type;
                std::size_t result_width { };
                for (const auto& specialization : specializations_) {
                    for (const auto& method : specialization.methods) {
                        if (method.canonical_identity != identity
                            || method.static_method
                            || method.kind
                                != semantic::sv::ClassMethodKind::function
                            || !method.return_type) {
                            continue;
                        }
                        const auto candidate_width
                            = width(*method.return_type, 0U);
                        if (!result_type) {
                            result_type = *method.return_type;
                            result_width = candidate_width;
                            continue;
                        }
                        if (candidate_width != result_width
                            || method.return_type->signed_value
                                != result_type->signed_value) {
                            return std::nullopt;
                        }
                    }
                }
                if (result_type)
                    return result_type;
            }
        }
        if (selected_expression->kind != ExpressionKind::index
            || selected_expression->operands.empty()) {
            return std::nullopt;
        }
        auto base = self(
            self, selected_expression->operands.front(), depth + 1U);
        if (!base)
            return std::nullopt;
        auto element = call_array_element_type(*base);
        if (element)
            return element;
        semantic::sv::TypeReference bit;
        bit.executable_width = 1U;
        return bit;
    };
    const auto expression_shape = [this, &environment, &expression_type](
                                      auto&& self,
                                      const semantic::ExpressionId expression_id,
                                      const std::size_t depth)
        -> std::optional<ExpressionShape> {
        if (depth >= 64U)
            return std::nullopt;
        const auto* current = expression(expression_id);
        if (current == nullptr)
            return std::nullopt;
        if (const auto type = expression_type(
                expression_type, expression_id, depth + 1U)) {
            if (type->executable_width && *type->executable_width > 0U
                && std::in_range<std::size_t>(*type->executable_width)) {
                return ExpressionShape {
                    static_cast<std::size_t>(*type->executable_width),
                    type->signed_value };
            }
            if (type->packed_range && type->packed_range->left
                && type->packed_range->right) {
                const auto left = *type->packed_range->left;
                const auto right = *type->packed_range->right;
                const auto distance = support::index_distance(left, right);
                if (distance < std::numeric_limits<std::size_t>::max()) {
                    return ExpressionShape {
                        static_cast<std::size_t>(distance + 1U),
                        type->signed_value };
                }
            }
            const auto fallback_width = width(*type, 0U);
            if (fallback_width != 0U)
                return ExpressionShape { fallback_width, type->signed_value };
        }
        using semantic::sv::ExpressionKind;
        if (current->kind == ExpressionKind::integer_literal) {
            std::int64_t literal { };
            const auto parsed = std::from_chars(
                current->text.data(),
                current->text.data() + current->text.size(), literal, 10);
            if (parsed.ec != std::errc { }
                || parsed.ptr != current->text.data() + current->text.size()) {
                return std::nullopt;
            }
            return ExpressionShape {
                literal >= std::numeric_limits<std::int32_t>::min()
                        && literal <= std::numeric_limits<std::int32_t>::max()
                    ? 32U : 64U,
                true };
        }
        if (current->kind == ExpressionKind::logic_literal) {
            const auto literal = logic_literal(current->text);
            return literal
                ? std::optional<ExpressionShape> {
                      ExpressionShape { literal->width(), current->signed_value }
                  }
                : std::nullopt;
        }
        if (current->kind == ExpressionKind::boolean_literal)
            return ExpressionShape { 1U, false };
        if (current->kind == ExpressionKind::name) {
            const auto found = environment.find(current->text);
            if (found != environment.end())
                return ExpressionShape {
                    found->second.width(), current->signed_value };
        }
        if (current->kind == ExpressionKind::unary
            && current->operands.size() == 1U) {
            const auto operand
                = self(self, current->operands.front(), depth + 1U);
            if (!operand)
                return std::nullopt;
            if (current->text == "!" || current->text == "&"
                || current->text == "~&" || current->text == "|"
                || current->text == "~|" || current->text == "^"
                || current->text == "~^" || current->text == "^~") {
                return ExpressionShape { 1U, false };
            }
            return operand;
        }
        if (current->kind == ExpressionKind::binary
            && current->operands.size() == 2U) {
            const auto lhs
                = self(self, current->operands[0], depth + 1U);
            const auto rhs
                = self(self, current->operands[1], depth + 1U);
            if (!lhs || !rhs)
                return std::nullopt;
            const auto& operation = current->text;
            if (operation == "==" || operation == "!="
                || operation == "===" || operation == "!=="
                || operation == "<" || operation == "<="
                || operation == ">" || operation == ">="
                || operation == "&&" || operation == "||") {
                return ExpressionShape { 1U, false };
            }
            if (operation == "<<" || operation == "<<<"
                || operation == ">>" || operation == ">>>") {
                return lhs;
            }
            if (operation == "**") {
                return ExpressionShape { lhs->width, lhs->signed_value };
            }
            return ExpressionShape {
                std::max(lhs->width, rhs->width),
                lhs->signed_value && rhs->signed_value };
        }
        if (current->kind == ExpressionKind::call && current->text == "?:"
            && current->operands.size() == 3U) {
            const auto lhs = self(self, current->operands[1U], depth + 1U);
            const auto rhs = self(self, current->operands[2U], depth + 1U);
            if (lhs && rhs) {
                return ExpressionShape {
                    std::max(lhs->width, rhs->width),
                    lhs->signed_value && rhs->signed_value };
            }
        }
        return std::nullopt;
    };
    if (value->kind == ExpressionKind::unary
        && value->operands.size() == 1U) {
        const auto operand_id = value->operands.front();
        const bool logical_not = value->text == "!";
        const bool reduction = value->text == "&" || value->text == "~&"
            || value->text == "|" || value->text == "~|"
            || value->text == "^" || value->text == "~^"
            || value->text == "^~";
        const auto operand_shape = expression_shape(
            expression_shape, operand_id, 0U);
        const auto operand_context
            = logical_not || reduction ? std::nullopt : context_width;
        const auto operand_sign_context = context_width && context_signed
            ? context_signed
            : (operand_shape
                    ? std::optional<bool> { operand_shape->signed_value }
                    : std::nullopt);
        const auto operand = evaluate_packed(
            operand_id, handle, environment, operand_context,
            operand_sign_context);
        if (!operand)
            return std::nullopt;
        const auto* operand_expression = expression(operand_id);
        if (operand_expression == nullptr)
            return std::nullopt;
        const bool signed_operand = operand_shape
            ? operand_shape->signed_value
            : operand_expression->signed_value;
        if (logical_not) {
            bool any_one = false;
            bool unknown = false;
            for (std::size_t bit = 0U; bit < operand->width(); ++bit) {
                const auto state = operand->get(bit);
                any_one = any_one || state == runtime::Logic4::one;
                unknown = unknown
                    || (state != runtime::Logic4::zero
                        && state != runtime::Logic4::one);
            }
            return apply_context(runtime::PackedLogic4(1U,
                any_one ? runtime::Logic4::zero
                : unknown ? runtime::Logic4::x
                          : runtime::Logic4::one), false);
        }
        if (reduction) {
            auto state = value->text == "&" || value->text == "~&"
                ? runtime::Logic4::one
                : value->text == "|" || value->text == "~|"
                ? runtime::Logic4::zero : runtime::Logic4::zero;
            for (std::size_t bit = 0U; bit < operand->width(); ++bit) {
                state = value->text == "&" || value->text == "~&"
                    ? runtime::logic_and(state, operand->get(bit))
                    : value->text == "|" || value->text == "~|"
                    ? runtime::logic_or(state, operand->get(bit))
                    : runtime::logic_xor(state, operand->get(bit));
            }
            if (value->text == "~&" || value->text == "~|"
                || value->text == "~^" || value->text == "^~") {
                state = runtime::logic_not(state);
            }
            return apply_context(
                runtime::PackedLogic4(1U, state), false);
        }
        const auto result_width
            = std::max(operand->width(), context_width.value_or(0U));
        const auto resized = resize_expression_operand(
            *operand, result_width, signed_operand);
        if (!resized)
            return std::nullopt;
        if (value->text == "+")
            return *resized;
        if (value->text == "-") {
            return packed_has_unknown(*resized)
                ? std::optional<runtime::PackedLogic4> {
                      runtime::PackedLogic4(result_width, runtime::Logic4::x) }
                : std::optional<runtime::PackedLogic4> {
                      negate_known_packed(*resized) };
        }
        if (value->text == "~") {
            auto result = runtime::PackedLogic4(
                result_width, runtime::Logic4::zero);
            for (std::size_t bit = 0U; bit < result_width; ++bit)
                result.set(bit, runtime::logic_not(resized->get(bit)));
            return result;
        }
        return std::nullopt;
    }
    if (value->kind != ExpressionKind::binary
        || value->operands.size() != 2U) {
        return std::nullopt;
    }
    const auto is_string_operand = [this, handle](auto&& self,
                                           const semantic::ExpressionId id)
        -> bool {
        const auto* operand = expression(id);
        if (operand == nullptr)
            return false;
        if (operand->kind == ExpressionKind::string_literal)
            return true;
        if (operand->kind == ExpressionKind::name) {
            if (!operand->referenced_name
                || !operand->referenced_name->selected) {
                return strings_ != nullptr
                    && strings_->contains(operand->text);
            }
            const auto selected = *operand->referenced_name->selected;
            if (const auto* reference = active_reference(selected))
                return reference->string_value;
            if (is_package_global(selected)
                || is_package_constant(selected)) {
                const auto* selected_declaration = declaration(selected);
                return selected_declaration != nullptr
                    && selected_declaration->type
                    && is_string(*selected_declaration->type);
            }
            const auto* selected_declaration = declaration(selected);
            if (selected_declaration != nullptr
                && selected_declaration->form
                    == semantic::sv::DeclarationForm::variable
                && selected_declaration->lifetime
                    == semantic::sv::Lifetime::static_lifetime) {
                return false;
            }
            if (string_bindings_ != nullptr
                && string_bindings_->contains(selected.value())) {
                return true;
            }
            const auto* property = instance_property(handle, selected);
            return property != nullptr && is_string(property->type)
                && static_cast<bool>(read_string_property_);
        }
        if (operand->kind == ExpressionKind::concatenation) {
            return !operand->operands.empty()
                && std::ranges::all_of(operand->operands,
                    [&](const auto part) { return self(self, part); });
        }
        constexpr std::string_view property_prefix { "@sv-property:" };
        if (operand->kind == ExpressionKind::class_property
            || operand->text.starts_with(property_prefix)) {
            if (operand->operands.size() == 1U) {
                const auto* receiver = expression(operand->operands.front());
                if (receiver == nullptr || receiver->kind != ExpressionKind::name
                    || receiver->text != "this") {
                    return false;
                }
            } else if (!operand->operands.empty()) {
                return false;
            }
            const auto* property = instance_property(
                handle, marker_payload(*operand, property_prefix));
            return property != nullptr && is_string(property->type)
                && static_cast<bool>(read_string_property_);
        }
        if (operand->kind == ExpressionKind::call
            && operand->referenced_name
            && operand->referenced_name->selected) {
            const auto* selected
                = declaration(*operand->referenced_name->selected);
            const bool package_function
                = selected != nullptr && is_package_member(*selected);
            return package_function
                && selected->form
                    == semantic::sv::DeclarationForm::function
                && selected->callable && selected->callable->function
                && is_string(selected->callable->return_type);
        }
        constexpr std::string_view static_prefix {
            "@sv-static-method:"
        };
        constexpr std::string_view method_prefix { "@sv-method:" };
        constexpr std::string_view base_method_prefix {
            "@sv-base-method:"
        };
        const bool static_call
            = operand->kind == ExpressionKind::class_static_method_call
            || operand->text.starts_with(static_prefix);
        const bool instance_call
            = operand->kind == ExpressionKind::class_method_call
            || operand->text.starts_with(method_prefix)
            || operand->text.starts_with(base_method_prefix);
        if (!static_call && !instance_call)
            return false;
        const auto identity = marker_payload(*operand,
            static_call ? static_prefix
                : operand->text.starts_with(base_method_prefix)
                ? base_method_prefix : method_prefix);
        bool found { };
        for (const auto& specialization : specializations_) {
            for (const auto& method : specialization.methods) {
                if (method.canonical_identity != identity)
                    continue;
                if (method.kind != semantic::sv::ClassMethodKind::function
                    || method.static_method != static_call
                    || !method.defined
                    || method.external
                    || !method.return_type
                    || !is_string(*method.return_type)) {
                    return false;
                }
                found = true;
            }
        }
        return found;
    };
    const auto contains_static_string = [this](auto&& self,
                                                const semantic::ExpressionId id)
        -> bool {
        const auto* operand = expression(id);
        if (operand == nullptr)
            return false;
        if (operand->kind == ExpressionKind::name
            && operand->referenced_name
            && operand->referenced_name->selected) {
            const auto selected = *operand->referenced_name->selected;
            if (is_package_global(selected) || is_package_constant(selected))
                return false;
            const auto* declaration = this->declaration(selected);
            return declaration != nullptr
                && declaration->form
                    == semantic::sv::DeclarationForm::variable
                && declaration->type
                && declaration->lifetime
                    == semantic::sv::Lifetime::static_lifetime
                && is_string(*declaration->type);
        }
        return std::ranges::any_of(operand->operands,
            [&](const auto child) { return self(self, child); });
    };
    if ((value->text == "==" || value->text == "!=")
        && (contains_static_string(
                contains_static_string, value->operands[0])
            || contains_static_string(
                contains_static_string, value->operands[1]))) {
        // Static string locals need persistent storage shared across calls.
        // The packed fallback sees only the placeholder words used for
        // strings, so refusing this comparison is safer than comparing them.
        return std::nullopt;
    }
    if ((value->text == "==" || value->text == "!=")
        && is_string_operand(is_string_operand, value->operands[0])
        && is_string_operand(is_string_operand, value->operands[1])) {
        if (strings_ == nullptr)
            return std::nullopt;
        const auto left_string = evaluate_string(
            value->operands[0], *strings_, handle);
        const auto right_string = evaluate_string(
            value->operands[1], *strings_, handle);
        if (!left_string || !right_string)
            return std::nullopt;
        const bool equal = *left_string == *right_string;
        const bool result = value->text == "==" ? equal : !equal;
        return apply_context(runtime::PackedLogic4::from_aval_bval(
            1U, result ? 1U : 0U, 0U), false);
    }
    const auto lhs_shape = expression_shape(
        expression_shape, value->operands[0], 0U);
    const auto rhs_shape = expression_shape(
        expression_shape, value->operands[1], 0U);
    const bool relational = value->text == "<" || value->text == "<="
        || value->text == ">" || value->text == ">=";
    const bool logical = value->text == "&&" || value->text == "||";
    const bool shift = value->text == "<<" || value->text == "<<<"
        || value->text == ">>" || value->text == ">>>";
    const bool power = value->text == "**";
    const auto* lhs_expression = expression(value->operands[0]);
    const auto* rhs_expression = expression(value->operands[1]);
    if (lhs_expression == nullptr || rhs_expression == nullptr)
        return std::nullopt;
    const bool context_sized = !relational && !logical && !shift
        && value->text != "==" && value->text != "!="
        && value->text != "===" && value->text != "!==";
    const bool natural_signed_operation = shift
        ? (lhs_shape ? lhs_shape->signed_value
                     : lhs_expression->signed_value)
        : lhs_shape && rhs_shape
        ? lhs_shape->signed_value && rhs_shape->signed_value
        : lhs_expression->signed_value && rhs_expression->signed_value;
    const bool inherited_context_sign = context_sized
        && context_width.has_value() && context_signed.has_value();
    const bool power_base_naturally_signed = lhs_shape
        ? lhs_shape->signed_value : lhs_expression->signed_value;
    const bool power_base_signed = power_base_naturally_signed
        && !(context_width && context_signed && !*context_signed);
    const bool power_exponent_signed = rhs_shape
        ? rhs_shape->signed_value : rhs_expression->signed_value;
    const bool signed_operation = power ? power_exponent_signed
        : shift ? natural_signed_operation
        : inherited_context_sign ? *context_signed
                                 : natural_signed_operation;
    std::optional<std::size_t> relational_width;
    std::optional<bool> relational_signed;
    if (relational && lhs_shape && rhs_shape) {
        relational_width = std::max(lhs_shape->width, rhs_shape->width);
        relational_signed
            = lhs_shape->signed_value && rhs_shape->signed_value;
    }
    std::optional<std::size_t> operand_context;
    if (context_sized && (lhs_shape || rhs_shape || context_width)) {
        auto selected_width = lhs_shape ? lhs_shape->width : 0U;
        if (!power)
            selected_width = std::max(selected_width,
                rhs_shape ? rhs_shape->width : 0U);
        if (context_width)
            selected_width = std::max(selected_width, *context_width);
        if (selected_width != 0U)
            operand_context = selected_width;
    }
    std::optional<std::size_t> operand_value_context;
    std::optional<bool> operand_sign_context;
    if (relational) {
        operand_value_context = relational_width;
        operand_sign_context = relational_signed;
    } else if (power) {
        operand_value_context = operand_context;
        operand_sign_context = power_base_signed;
    } else if (!logical && !shift && context_sized) {
        operand_value_context = operand_context;
        operand_sign_context = signed_operation;
    }
    const auto left = evaluate_packed(
        value->operands[0], handle, environment,
        operand_value_context, operand_sign_context);
    // Logical operators short-circuit (IEEE 1800-2017 11.4.7).
    if ((value->text == "&&" || value->text == "||") && left) {
        const auto truth = [](const runtime::PackedLogic4& operand)
            -> std::optional<bool> {
            bool unknown { };
            for (std::size_t bit = 0U; bit < operand.width(); ++bit) {
                const auto state = operand.get(bit);
                if (state == runtime::Logic4::one)
                    return true;
                unknown = unknown || state != runtime::Logic4::zero;
            }
            return unknown ? std::nullopt : std::optional { false };
        };
        const auto lhs_truth = truth(*left);
        const bool conjunction = value->text == "&&";
        if (lhs_truth && *lhs_truth != conjunction) {
            return apply_context(runtime::PackedLogic4::from_aval_bval(
                1U, conjunction ? 0U : 1U, 0U), false);
        }
        const auto right_value = evaluate_packed(
            value->operands[1], handle, environment);
        if (!right_value)
            return std::nullopt;
        const auto rhs_truth = truth(*right_value);
        if (rhs_truth && *rhs_truth != conjunction) {
            return apply_context(runtime::PackedLogic4::from_aval_bval(
                1U, conjunction ? 0U : 1U, 0U), false);
        }
        if (!lhs_truth || !rhs_truth)
            return apply_context(
                runtime::PackedLogic4(1U, runtime::Logic4::x), false);
        return apply_context(runtime::PackedLogic4::from_aval_bval(
            1U, conjunction ? 1U : 0U, 0U), false);
    }
    const auto right = evaluate_packed(
        value->operands[1], handle, environment,
        power ? std::nullopt : operand_value_context,
        power ? std::nullopt : operand_sign_context);
    if (!left || !right)
        return std::nullopt;
    if (relational) {
        const auto* left_expression = expression(value->operands[0]);
        const auto* right_expression = expression(value->operands[1]);
        if (left_expression == nullptr || right_expression == nullptr
            || left->width() == 0U || right->width() == 0U) {
            return std::nullopt;
        }

        const auto relational_operand_type = [this, handle](
                                         auto&& self,
                                         const semantic::ExpressionId operand_id,
                                         const std::size_t depth)
            -> std::optional<semantic::sv::TypeReference> {
            if (depth >= 64U)
                return std::nullopt;
            const auto* operand = expression(operand_id);
            if (operand == nullptr)
                return std::nullopt;
            if (operand->kind == ExpressionKind::name
                && operand->referenced_name
                && operand->referenced_name->selected) {
                const auto* selected = declaration(
                    *operand->referenced_name->selected);
                if (selected != nullptr && selected->type)
                    return *selected->type;
            }
            if (operand->kind == ExpressionKind::class_property) {
                const auto* property = instance_property(
                    handle, operand->class_member_identity);
                if (property != nullptr)
                    return property->type;
            }
            if (operand->kind == ExpressionKind::index
                && !operand->operands.empty()) {
                auto base_type = self(
                    self, operand->operands.front(), depth + 1U);
                if (!base_type)
                    return std::nullopt;
                const auto* definition
                    = call_aggregate_definition(*base_type);
                const bool unpacked_array
                    = base_type->container_form.has_value()
                    || !base_type->unpacked_dimensions.empty()
                    || (definition != nullptr
                        && (definition->form
                                == semantic::sv::TypeForm::static_array
                            || definition->form
                                == semantic::sv::TypeForm::dynamic_array
                            || definition->form
                                == semantic::sv::TypeForm::queue
                            || definition->form
                                == semantic::sv::TypeForm::associative_array));
                if (unpacked_array) {
                    auto element = call_array_element_type(*base_type);
                    if (!element)
                        return std::nullopt;
                    constexpr std::string_view member_prefix { "index." };
                    if (operand->text.starts_with(member_prefix)) {
                        const auto* structure
                            = call_aggregate_definition(*element);
                        if (structure == nullptr
                            || structure->form
                                != semantic::sv::TypeForm::unpacked_structure) {
                            return std::nullopt;
                        }
                        const auto member_name
                            = std::string_view { operand->text }
                                  .substr(member_prefix.size());
                        const auto member = std::ranges::find(
                            structure->members, member_name,
                            [](const auto& candidate) -> std::string_view {
                                return candidate.name;
                            });
                        if (member == structure->members.end())
                            return std::nullopt;
                        return member->type;
                    }
                    return element;
                }

                // A packed bit-select has an unsigned integral result.
                semantic::sv::TypeReference selected_bit;
                selected_bit.executable_width = 1U;
                return selected_bit;
            }
            return std::nullopt;
        };
        const auto expression_signed = [this, &relational_operand_type](
                                           auto&& self,
                                           const semantic::ExpressionId operand_id,
                                           const std::size_t depth)
            -> std::optional<bool> {
            if (depth >= 64U)
                return std::nullopt;
            const auto* operand = expression(operand_id);
            if (operand == nullptr)
                return std::nullopt;

            if (operand->kind == ExpressionKind::name) {
                const auto type = relational_operand_type(
                    relational_operand_type, operand_id, depth + 1U);
                return type ? std::optional { type->signed_value }
                            : std::optional { operand->signed_value };
            }
            if (operand->kind == ExpressionKind::unary
                && operand->operands.size() == 1U) {
                if (operand->text == "!" || operand->text == "&"
                    || operand->text == "~&" || operand->text == "|"
                    || operand->text == "~|" || operand->text == "^"
                    || operand->text == "~^" || operand->text == "^~") {
                    return false;
                }
                return self(self, operand->operands.front(), depth + 1U);
            }
            if (operand->kind == ExpressionKind::update
                && !operand->operands.empty()) {
                return self(self, operand->operands.front(), depth + 1U);
            }
            if (operand->kind == ExpressionKind::binary
                && operand->operands.size() == 2U) {
                const auto& operation = operand->text;
                if (operation == "==" || operation == "!="
                    || operation == "===" || operation == "!=="
                    || operation == "<" || operation == "<="
                    || operation == ">" || operation == ">="
                    || operation == "&&" || operation == "||"
                    || operation == "inside" || operation == "matches") {
                    return false;
                }
                const auto lhs_signed = self(
                    self, operand->operands[0], depth + 1U);
                if (operation == "<<" || operation == "<<<"
                    || operation == ">>" || operation == ">>>"
                    || operation == "**") {
                    return lhs_signed;
                }
                const auto rhs_signed = self(
                    self, operand->operands[1], depth + 1U);
                if (lhs_signed && rhs_signed)
                    return *lhs_signed && *rhs_signed;
            }
            if (operand->kind == ExpressionKind::call
                && operand->text == "?:"
                && operand->operands.size() == 3U) {
                const auto true_signed = self(
                    self, operand->operands[1], depth + 1U);
                const auto false_signed = self(
                    self, operand->operands[2], depth + 1U);
                if (true_signed && false_signed)
                    return *true_signed && *false_signed;
            }
            if (operand->kind == ExpressionKind::index) {
                const auto type = relational_operand_type(
                    relational_operand_type, operand_id, depth + 1U);
                return type ? std::optional { type->signed_value }
                            : std::optional { false };
            }
            if (operand->kind == ExpressionKind::slice
                || operand->kind == ExpressionKind::concatenation
                || operand->kind == ExpressionKind::replication) {
                return false;
            }
        if (const auto type = relational_operand_type(
                relational_operand_type, operand_id, depth + 1U)) {
            return type->signed_value;
        }
            return operand->signed_value;
        };
        const auto lhs_signed = expression_signed(
            expression_signed, value->operands[0], 0U);
        const auto rhs_signed = expression_signed(
            expression_signed, value->operands[1], 0U);
        if (!lhs_signed || !rhs_signed)
            return std::nullopt;

        // Relational operands are signed only when both operands are signed.
        // They are extended to their common width using that operation
        // signedness before comparison (IEEE 1800-2017 11.8.1-11.8.2).
        const bool signed_comparison = *lhs_signed && *rhs_signed;
        // An X or Z bit in either operand makes the result unknown.
        const auto common_width = std::max(left->width(), right->width());
        const auto lhs_operand = resize_expression_operand(
            *left, common_width, signed_comparison);
        const auto rhs_operand = resize_expression_operand(
            *right, common_width, signed_comparison);
        if (!lhs_operand || !rhs_operand)
            return std::nullopt;
        using runtime::simir::BinaryOperator;
        const auto comparison = value->text == "<"
            ? (signed_comparison ? BinaryOperator::less_signed
                                 : BinaryOperator::less_unsigned)
            : value->text == "<="
            ? (signed_comparison ? BinaryOperator::less_equal_signed
                                 : BinaryOperator::less_equal_unsigned)
            : value->text == ">"
            ? (signed_comparison ? BinaryOperator::greater_signed
                                 : BinaryOperator::greater_unsigned)
            : (signed_comparison ? BinaryOperator::greater_equal_signed
                                 : BinaryOperator::greater_equal_unsigned);
        return apply_context(runtime::simir::binary_value(
                                 comparison, *lhs_operand, *rhs_operand),
            false);
    }
    const bool shift_operation = value->text == "<<"
        || value->text == "<<<" || value->text == ">>"
        || value->text == ">>>";
    const bool power_operation = value->text == "**";
    const auto operand_width = shift_operation
        ? lhs_shape ? lhs_shape->width : left->width()
        : power_operation ? left->width()
        : std::max({ left->width(), right->width(),
              operand_context.value_or(0U) });
    const auto normalized_left = resize_expression_operand(
        *left, operand_width, power_operation
            ? power_base_signed : signed_operation);
    const auto normalized_right = shift_operation || power_operation
        ? std::optional<runtime::PackedLogic4> { *right }
        : resize_expression_operand(*right, operand_width, signed_operation);
    if (!normalized_left || !normalized_right)
        return std::nullopt;
    if (power_operation && power_exponent_signed
        && normalized_right->width() > 0U
        && !packed_has_unknown(*normalized_right)
        && normalized_right->get(normalized_right->width() - 1U)
            == runtime::Logic4::one
        && !power_base_signed) {
        // For an unsigned base with a negative signed exponent, IEEE 1800
        // 11.4.3/Table 11-4 yields 1 for base 1, X for base 0, and 0 for
        // every other known base. The shared power helper's signed mode uses
        // the exponent sign bit, but its all-ones shortcut would otherwise
        // treat an unsigned all-ones base as signed -1.
        bool base_zero = true;
        bool base_one = true;
        for (std::size_t bit = 0U; bit < normalized_left->width(); ++bit) {
            const auto state = normalized_left->get(bit);
            if (state != runtime::Logic4::zero
                && state != runtime::Logic4::one) {
                const auto result = runtime::PackedLogic4(
                    operand_width, runtime::Logic4::x);
                return apply_context(result, false);
            }
            base_zero = base_zero && state == runtime::Logic4::zero;
            base_one = base_one
                && state == (bit == 0U
                    ? runtime::Logic4::one : runtime::Logic4::zero);
        }
        if (base_one) {
            auto result = runtime::PackedLogic4(
                operand_width, runtime::Logic4::zero);
            result.set(0U, runtime::Logic4::one);
            return apply_context(result, false);
        }
        if (base_zero) {
            const auto base_type = expression_type(
                expression_type, value->operands[0], 0U);
            const auto state = base_type && !base_type->four_state
                ? runtime::Logic4::zero : runtime::Logic4::x;
            return apply_context(runtime::PackedLogic4(operand_width, state),
                false);
        }
        return apply_context(runtime::PackedLogic4(
            operand_width, runtime::Logic4::zero), false);
    }
    const auto result = evaluate_binary_packed(
        value->text, *normalized_left, *normalized_right,
        signed_operation, power_base_signed);
    if (!result)
        return std::nullopt;
    const bool one_bit_result = value->text == "=="
        || value->text == "!=" || value->text == "==="
        || value->text == "!==";
    return apply_context(*result,
        !one_bit_result && (power_operation
            ? power_base_signed : signed_operation));
}

SystemVerilogClassHirExecution::PackedEnvironment
SystemVerilogClassHirExecution::bind_actuals(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> actual_names,
    std::vector<std::size_t>* const actual_formals)
{
    PackedEnvironment environment;
    std::vector<bool> assigned(method.formals.size());
    const auto mapped_formals = map_actual_formals(
        method, actual_names, actuals.size());
    for (std::size_t index = 0U; index < actuals.size(); ++index) {
        const auto formal = mapped_formals[index];
        const auto* formal_declaration = declaration(method.formals[formal]);
        if (formal_declaration == nullptr
            || formal >= method.formal_types.size()) {
            throw std::invalid_argument {
                "class method HIR formal is unavailable"
            };
        }
        assigned[formal] = true;
        if (actual_formals != nullptr)
            actual_formals->push_back(formal);
        const auto formal_width = width(
            method.formal_types[formal], actuals[index].width());
        environment.emplace(formal_declaration->name,
            formal_declaration->direction == semantic::sv::Direction::output
                ? runtime::PackedLogic4(formal_width, runtime::Logic4::x)
                : resize_packed(actuals[index], formal_width));
    }
    for (std::size_t index = 0U; index < method.formals.size(); ++index) {
        if (assigned[index])
            continue;
        const auto* formal = declaration(method.formals[index]);
        if (formal == nullptr || index >= method.formal_types.size()
            || !formal->initializer) {
            throw std::invalid_argument {
                "class method HIR formal has no actual or default"
            };
        }
        if (is_string(method.formal_types[index])) {
            environment.emplace(formal->name,
                runtime::PackedLogic4(64U, runtime::Logic4::zero));
            continue;
        }
        const auto value = evaluate_packed(
            *formal->initializer, handle, environment);
        if (!value) {
            throw std::invalid_argument {
                "class method HIR default is not executable"
            };
        }
        environment.emplace(formal->name,
            resize_packed(*value, width(method.formal_types[index],
                value->width())));
    }
    for (const auto local_id : method.local_declarations) {
        if (std::ranges::find(method.formals, local_id) != method.formals.end())
            continue;
        const auto* local = declaration(local_id);
        if (local == nullptr || local->form != semantic::sv::DeclarationForm::variable
            || !local->type) {
            continue;
        }
        if (is_string(*local->type)) {
            environment[local->name]
                = runtime::PackedLogic4(64U, runtime::Logic4::zero);
            continue;
        }
        // A static local keeps its value across calls (IEEE 1800-2017
        // 6.21).
        if (local->lifetime == semantic::sv::Lifetime::static_lifetime) {
            if (const auto kept = static_locals_.find(local_id.value());
                kept != static_locals_.end()) {
                environment[local->name] = kept->second;
                continue;
            }
        }
        const auto local_width = width(*local->type);
        const auto value = local->initializer
            ? evaluate_packed(*local->initializer, handle, environment)
            : std::optional<runtime::PackedLogic4> {
                  runtime::PackedLogic4(local_width, runtime::Logic4::x)
              };
        if (!value) {
            throw std::invalid_argument {
                "class method HIR local initializer is not executable"
            };
        }
        environment[local->name] = resize_packed(*value, local_width);
    }
    return environment;
}

void SystemVerilogClassHirExecution::initialize_strings(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const std::size_t> actual_formals,
    const std::span<const std::string> string_actuals,
    StringEnvironment& environment,
    StringBindingEnvironment& bindings)
{
    for (std::size_t formal_index = 0U;
         formal_index < method.formals.size(); ++formal_index) {
        if (formal_index >= method.formal_types.size()
            || !is_string(method.formal_types[formal_index])) {
            continue;
        }
        const auto* formal = declaration(method.formals[formal_index]);
        if (formal == nullptr)
            continue;
        const auto actual = std::ranges::find(
            actual_formals, formal_index);
        if (actual != actual_formals.end()) {
            const auto actual_index = static_cast<std::size_t>(
                std::distance(actual_formals.begin(), actual));
            environment.emplace(formal->name,
                formal->direction == semantic::sv::Direction::output
                    ? std::string { }
                    : string_actuals[actual_index]);
            bindings.insert_or_assign(formal->id.value(),
                formal->direction == semantic::sv::Direction::output
                    ? std::string { }
                    : string_actuals[actual_index]);
            continue;
        }
        const auto value = formal->initializer
            ? evaluate_string(*formal->initializer, environment, handle)
            : std::nullopt;
        if (!value) {
            throw std::invalid_argument {
                "class method HIR string formal has no executable default"
            };
        }
        environment.emplace(formal->name, *value);
        bindings.insert_or_assign(formal->id.value(), *value);
    }
    for (const auto local_id : method.local_declarations) {
        if (std::ranges::find(method.formals, local_id) != method.formals.end())
            continue;
        const auto* local = declaration(local_id);
        if (local == nullptr || !local->type || !is_string(*local->type))
            continue;
        const auto value = local->initializer
            ? evaluate_string(*local->initializer, environment, handle)
            : std::optional<std::string> { std::string { } };
        if (!value) {
            throw std::invalid_argument {
                "class method HIR string local initializer is not executable"
            };
        }
        environment[local->name] = *value;
        bindings.insert_or_assign(local_id.value(), *value);
    }
}

SystemVerilogClassHirExecution::ExecutionResult
SystemVerilogClassHirExecution::execute(
    const std::span<const semantic::StatementId> statements,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& string_environment,
    const bool constructor,
    const std::optional<std::size_t> return_context_width)
{
    constexpr std::string_view base_prefix { "@sv-base-constructor:" };
    const ScopedSlot string_scope { strings_, &string_environment };
    const auto assignment_context_width = [this, handle, &environment](
                                              const semantic::ExpressionId id)
        -> std::optional<std::size_t> {
        const auto selected_type = [this, handle](auto&& self,
                                      const semantic::ExpressionId expression_id,
                                      const std::size_t depth)
            -> std::optional<semantic::sv::TypeReference> {
            if (depth >= 64U)
                return std::nullopt;
            const auto* value = expression(expression_id);
            if (value == nullptr)
                return std::nullopt;
            if (value->kind == semantic::sv::ExpressionKind::name
                && value->referenced_name
                && value->referenced_name->selected) {
                const auto* selected = declaration(
                    *value->referenced_name->selected);
                if (selected != nullptr && selected->type)
                    return *selected->type;
            }
            constexpr std::string_view property_prefix { "@sv-property:" };
            if (value->kind == semantic::sv::ExpressionKind::class_property
                || value->text.starts_with(property_prefix)) {
                const auto* property = instance_property(
                    handle, marker_payload(*value, property_prefix));
                if (property != nullptr)
                    return property->type;
            }
            constexpr std::string_view container_index_prefix {
                "@sv-container-index:"
            };
            if (value->kind == semantic::sv::ExpressionKind::call
                && value->text.starts_with(container_index_prefix)
                && value->operands.size() >= 2U) {
                const auto* property = instance_property(
                    handle, marker_payload(*value, container_index_prefix));
                const auto element = property != nullptr
                    ? call_array_element_type(property->type)
                    : std::nullopt;
                if (element)
                    return element;
            }
            constexpr std::string_view static_property_prefix {
                "@sv-static-property:"
            };
            if (value->kind
                    == semantic::sv::ExpressionKind::class_static_property
                || value->text.starts_with(static_property_prefix)) {
                const auto* property = static_class_property(
                    specializations_,
                    marker_payload(*value, static_property_prefix),
                    value->referenced_name
                        ? value->referenced_name->selected
                        : std::nullopt);
                if (property != nullptr)
                    return property->type;
            }
            constexpr std::string_view static_container_prefix {
                "@sv-static-container-property:"
            };
            if (value->text.starts_with(static_container_prefix)) {
                const auto* property = static_class_property(
                    specializations_,
                    marker_payload(*value, static_container_prefix),
                    value->referenced_name
                        ? value->referenced_name->selected
                        : std::nullopt);
                if (property != nullptr)
                    return property->type;
            }
            if (value->kind == semantic::sv::ExpressionKind::index
                && !value->operands.empty()) {
                const auto base = self(
                    self, value->operands.front(), depth + 1U);
                if (!base)
                    return std::nullopt;
                if (const auto unpacked = call_array_element_type(*base))
                    return unpacked;
                semantic::sv::TypeReference bit;
                bit.value_form = semantic::sv::TypeForm::packed_integral;
                bit.executable_width = 1U;
                return bit;
            }
            constexpr std::string_view selected_prefix { "@sv-select:" };
            if (value->kind == semantic::sv::ExpressionKind::call
                && value->text.starts_with(selected_prefix)
                && value->operands.size() == 1U) {
                const auto* indexed = expression(value->operands.front());
                if (indexed == nullptr
                    || indexed->kind != semantic::sv::ExpressionKind::index
                    || indexed->operands.empty()) {
                    return std::nullopt;
                }
                const auto array = self(
                    self, indexed->operands.front(), depth + 1U);
                const auto element = array
                    ? call_array_element_type(*array) : std::nullopt;
                const auto* structure = element
                    ? call_aggregate_definition(*element) : nullptr;
                if (structure == nullptr
                    || structure->form
                        != semantic::sv::TypeForm::unpacked_structure) {
                    return std::nullopt;
                }
                const auto member = std::ranges::find(
                    structure->members,
                    marker_payload(*value, selected_prefix),
                    &semantic::sv::PackedMember::name);
                return member == structure->members.end()
                    ? std::nullopt
                    : std::optional<semantic::sv::TypeReference> {
                          member->type };
            }
            return std::nullopt;
        };
        if (const auto type = selected_type(selected_type, id, 0U)) {
            if (type->executable_width
                && *type->executable_width > 0U
                && std::in_range<std::size_t>(*type->executable_width)) {
                return static_cast<std::size_t>(*type->executable_width);
            }
            const auto type_width = width(*type, 0U);
            if (type_width != 0U)
                return type_width;
            if (type->packed_range && type->packed_range->left
                && type->packed_range->right) {
                const auto left = *type->packed_range->left;
                const auto right = *type->packed_range->right;
                const auto distance = support::index_distance(left, right);
                if (distance < std::numeric_limits<std::size_t>::max())
                    return static_cast<std::size_t>(distance + 1U);
            }
        }
        const auto* target = expression(id);
        if (target != nullptr
            && target->kind == semantic::sv::ExpressionKind::name) {
            const auto local = environment.find(target->text);
            if (local != environment.end())
                return local->second.width();
        }
        return std::nullopt;
    };
    for (const auto statement_id : statements) {
        const auto* item = statement(statement_id);
        if (item == nullptr) {
            throw std::invalid_argument { "class HIR statement is unavailable" };
        }
        if (constructor && item->kind == semantic::sv::StatementKind::task_call
            && item->task.spelling.starts_with(base_prefix)) {
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::assignment) {
            if (!item->target || !item->value)
                throw std::invalid_argument { "class HIR assignment is incomplete" };
            const auto* target = expression(*item->target);
            if (target == nullptr)
                throw std::invalid_argument { "class HIR assignment target is unavailable" };
            if (target->kind == semantic::sv::ExpressionKind::name) {
                std::optional<semantic::DeclarationId> selected;
                if (target->referenced_name
                    && target->referenced_name->selected) {
                    selected = *target->referenced_name->selected;
                }
                const bool selected_global = target->referenced_name
                    && target->referenced_name->selected
                    && is_package_global(
                        *target->referenced_name->selected);
                const auto* reference = target->referenced_name
                        && target->referenced_name->selected
                    ? active_reference(
                          *target->referenced_name->selected)
                    : nullptr;
                if (reference != nullptr) {
                    if (reference->string_value) {
                        const auto value = evaluate_string(
                            *item->value, string_environment, handle);
                        if (!value || !reference->write_string
                            || !reference->write_string(*value)) {
                            throw std::invalid_argument {
                                "class HIR string reference assignment is not executable"
                            };
                        }
                        continue;
                    }
                } else if (selected_global) {
                    const auto id = *target->referenced_name->selected;
                    if (read_package_string_
                        && read_package_string_(id)) {
                        const auto value = evaluate_string(
                            *item->value, string_environment, handle);
                        if (!value || !write_package_string_
                            || !write_package_string_(id, *value)) {
                            throw std::invalid_argument {
                                "class HIR package string assignment is not executable"
                            };
                        }
                        continue;
                    }
                } else if (selected && string_bindings_ != nullptr
                    && string_bindings_->contains(selected->value())) {
                    const auto value = evaluate_string(
                        *item->value, string_environment, handle);
                    if (!value) {
                        throw std::invalid_argument {
                            "class HIR string assignment is not executable"
                        };
                    }
                    string_bindings_->insert_or_assign(
                        selected->value(), *value);
                    if (const auto string = string_environment.find(target->text);
                        string != string_environment.end()) {
                        string->second = *value;
                    }
                    continue;
                } else if (selected) {
                    const auto* property = instance_property(handle, *selected);
                    if (property != nullptr && is_string(property->type)
                        && write_string_property_) {
                        const auto value = evaluate_string(
                            *item->value, string_environment, handle);
                        if (!value || !write_string_property_(
                                handle, property->canonical_identity, *value)) {
                            throw std::invalid_argument {
                                "class HIR string property assignment is not executable"
                            };
                        }
                        continue;
                    }
                } else if (!selected) {
                    const auto string = string_environment.find(target->text);
                    if (string != string_environment.end()) {
                        const auto value = evaluate_string(
                            *item->value, string_environment, handle);
                        if (!value) {
                            throw std::invalid_argument {
                                "class HIR string assignment is not executable"
                            };
                        }
                        string->second = *value;
                        continue;
                    }
                }
            }
            constexpr std::string_view property_prefix { "@sv-property:" };
            if (write_string_property_
                && (target->kind == semantic::sv::ExpressionKind::class_property
                    || target->text.starts_with(property_prefix))
                && target->operands.size() <= 1U) {
                auto owner = handle;
                if (target->operands.size() == 1U) {
                    const auto receiver = evaluate_packed(
                        target->operands.front(), handle, environment);
                    owner = receiver && receiver->low_word().bval == 0U
                        ? receiver->low_word().aval
                        : 0U;
                }
                const auto* property = instance_property(
                    owner, marker_payload(*target, property_prefix));
                if (property != nullptr && is_string(property->type)) {
                    const auto text = evaluate_string(
                        *item->value, string_environment, handle);
                    if (!text || !write_string_property_(
                            owner, property->canonical_identity, *text)) {
                        throw std::invalid_argument {
                            "class HIR string property assignment is not executable"
                        };
                    }
                    continue;
                }
            }
            constexpr std::string_view container_index_prefix {
                "@sv-container-index:"
            };
            if (target->kind == semantic::sv::ExpressionKind::call
                && target->text.starts_with(container_index_prefix)) {
                using semantic::sv::TypeForm;
                if (item->assignment_kind
                        != semantic::sv::AssignmentKind::blocking
                    || target->operands.size() != 2U) {
                    throw std::invalid_argument {
                        "class HIR associative handle assignment shape is not executable"
                    };
                }

                // Evaluate each side-effecting expression once. The language
                // leaves lvalue/RHS evaluation order unspecified here.
                const auto receiver = evaluate_packed(
                    target->operands[0], handle, environment);
                if (!receiver || receiver->width() != 64U
                    || receiver->low_word().bval != 0U) {
                    throw std::invalid_argument {
                        "class HIR associative handle receiver is not executable"
                    };
                }
                const auto owner = receiver->low_word().aval;
                if (owner == 0U || !heap_.contains(owner)) {
                    throw std::invalid_argument {
                        "class HIR associative handle receiver is null or stale"
                    };
                }
                const auto* property = instance_property(
                    owner, marker_payload(*target, container_index_prefix));
                if (property != nullptr
                    && property->type.container_form == TypeForm::static_array) {
                    const auto& element_type
                        = property->type.container_element_types;
                    if (element_type.size() != 1U
                        || (element_type.front().value_form
                                != TypeForm::class_handle
                            && element_type.front().class_identity.empty())) {
                        throw std::invalid_argument {
                            "class HIR fixed-array assignment requires a class-handle element"
                        };
                    }

                    auto ranges = property->type.unpacked_dimensions;
                    if (ranges.empty()
                        && property->type.target.target.valid()) {
                        const auto* definition
                            = type_definition(property->type.target.target);
                        if (definition != nullptr
                            && definition->container) {
                            ranges = definition->container->static_dimensions;
                        }
                    }
                    if (ranges.size() != 1U) {
                        throw std::invalid_argument {
                            "class HIR fixed-array assignment requires one resolved dimension"
                        };
                    }
                    const auto range_bound
                        = [&](const auto& literal, const auto& id) {
                              return evaluate_range_bound(
                                  literal, id, handle, environment);
                          };
                    const auto left = range_bound(
                        ranges.front().left,
                        ranges.front().left_expression);
                    const auto right = range_bound(
                        ranges.front().right,
                        ranges.front().right_expression);
                    if (!left || !right) {
                        throw std::invalid_argument {
                            "class HIR fixed-array assignment bounds are not executable"
                        };
                    }
                    const auto distance
                        = support::index_distance(*left, *right);
                    if (distance == std::numeric_limits<std::uint64_t>::max()
                        || !std::in_range<std::size_t>(distance + 1U)) {
                        throw std::invalid_argument {
                            "class HIR fixed-array assignment extent is not addressable"
                        };
                    }
                    const auto extent = static_cast<std::size_t>(distance + 1U);
                    const auto& storage = heap_.property(
                        owner, property->canonical_identity);
                    if (!storage.handle_container
                        || storage.handle_container->kind()
                            != runtime::SystemVerilogClassContainerKind::FixedArray
                        || storage.handle_container->size() != extent) {
                        throw std::invalid_argument {
                            "class HIR fixed-array assignment storage does not match its declared bounds"
                        };
                    }

                    const auto* index_expression = expression(
                        target->operands[1]);
                    if (index_expression == nullptr) {
                        throw std::invalid_argument {
                            "class HIR fixed-array index expression is unavailable"
                        };
                    }
                    if (index_expression->scalar_kind
                            == semantic::sv::ScalarKind::short_real
                        || index_expression->scalar_kind
                            == semantic::sv::ScalarKind::real
                        || index_expression->scalar_kind
                            == semantic::sv::ScalarKind::realtime) {
                        throw std::invalid_argument {
                            "class HIR fixed-array integral index cannot implicitly cast a real value"
                        };
                    }
                    const auto index_value = evaluate_packed(
                        target->operands[1], handle, environment);
                    if (!index_value) {
                        throw std::invalid_argument {
                            "class HIR fixed-array index is not executable"
                        };
                    }
                    std::optional<std::int64_t> index;
                    if (index_expression->signed_value) {
                        index = index_value->known_signed_value();
                    } else if (const auto unsigned_index
                        = index_value->known_unsigned_value()) {
                        if (*unsigned_index <= static_cast<std::uint64_t>(
                                std::numeric_limits<std::int64_t>::max())) {
                            index = static_cast<std::int64_t>(*unsigned_index);
                        }
                    }

                    // Evaluate the RHS once even when the fixed-array index is
                    // unknown or outside the declared bounds; those writes do
                    // not select an element.
                    const auto value = evaluate_packed(
                        *item->value, handle, environment);
                    if (!value || value->width() != 64U
                        || value->low_word().bval != 0U) {
                        throw std::invalid_argument {
                            "class HIR fixed-array assignment value is not a known class handle"
                        };
                    }
                    if (!index || *index < std::min(*left, *right)
                        || *index > std::max(*left, *right)) {
                        continue;
                    }
                    const auto ordinal = *left >= *right
                        ? static_cast<std::uint64_t>(*left)
                            - static_cast<std::uint64_t>(*index)
                        : static_cast<std::uint64_t>(*index)
                            - static_cast<std::uint64_t>(*left);
                    if (ordinal >= distance + 1U
                        || !std::in_range<std::size_t>(ordinal)) {
                        throw std::invalid_argument {
                            "class HIR fixed-array index offset is not addressable"
                        };
                    }
                    if (!write_class_handle_fixed_array_) {
                        throw std::invalid_argument {
                            "class HIR fixed-array handle storage is unavailable"
                        };
                    }
                    write_class_handle_fixed_array_(
                        owner,
                        property->canonical_identity,
                        static_cast<std::size_t>(ordinal),
                        value->low_word().aval);
                    continue;
                }
                if (property == nullptr
                    || property->type.container_form
                        != TypeForm::associative_array
                    || !property->type.associative_index_metadata
                    || property->type.container_element_types.size() != 1U
                    || (property->type.container_element_types.front().value_form
                            != TypeForm::class_handle
                        && property->type.container_element_types.front()
                                .class_identity.empty())
                    || !is_integral_associative_index(
                        *property->type.associative_index_metadata, hir_)) {
                    throw std::invalid_argument {
                        "class HIR indexed assignment requires an integral associative class-handle container"
                    };
                }
                const auto index_width
                    = property->type.associative_index_metadata->executable_width;
                if (!index_width || *index_width == 0U || *index_width > 64U) {
                    throw std::invalid_argument {
                        "class HIR associative index width is not executable"
                    };
                }

                const auto* index_expression = expression(target->operands[1]);
                if (index_expression == nullptr) {
                    throw std::invalid_argument {
                        "class HIR associative index expression is unavailable"
                    };
                }
                if (index_expression->scalar_kind
                        == semantic::sv::ScalarKind::short_real
                    || index_expression->scalar_kind
                        == semantic::sv::ScalarKind::real
                    || index_expression->scalar_kind
                        == semantic::sv::ScalarKind::realtime) {
                    throw std::invalid_argument {
                        "class HIR associative integral index cannot implicitly cast a real value"
                    };
                }
                const auto index = evaluate_packed(
                    target->operands[1], handle, environment);
                if (!index) {
                    throw std::invalid_argument {
                        "class HIR associative index is not executable"
                    };
                }
                const auto converted_index = cast_integral_index(
                    *index,
                    static_cast<std::size_t>(*index_width),
                    index_expression->signed_value,
                    property->type.associative_index_metadata->four_state);
                if (!converted_index) {
                    throw std::invalid_argument {
                        "class HIR associative index cannot be cast to its declared type"
                    };
                }

                const auto value = evaluate_packed(
                    *item->value, handle, environment, 64U);
                if (!value) {
                    throw std::invalid_argument {
                        "class HIR associative assignment value is not a class handle"
                    };
                }
                if (converted_index->low_word().bval != 0U) {
                    if (report_) {
                        report_(
                            report_process_,
                            "associative array write ignored because index contains X or Z",
                            runtime::simir::AssertionSeverity::warning,
                            item->source);
                    }
                    continue;
                }
                if (value->width() != 64U || value->low_word().bval != 0U) {
                    throw std::invalid_argument {
                        "class HIR associative assignment value is not a known class handle"
                    };
                }
                if (!write_class_handle_container_) {
                    throw std::invalid_argument {
                        "class HIR associative handle storage is unavailable"
                    };
                }
                const auto mask = *index_width == 64U
                    ? std::numeric_limits<std::uint64_t>::max()
                    : (UINT64_C(1) << *index_width) - 1U;
                write_class_handle_container_(
                    owner,
                    marker_payload(*target, container_index_prefix),
                    std::to_string(converted_index->low_word().aval & mask),
                    value->low_word().aval);
                continue;
            }
            const auto value = evaluate_packed(
                *item->value, handle, environment,
                assignment_context_width(*item->target));
            if (!value) {
                if (std::getenv("FSIM_CLASS_HIR_TRACE") != nullptr) {
                    const auto dump = [&](const auto& self,
                                          const semantic::ExpressionId id,
                                          const int depth) -> void {
                        const auto* node = expression(id);
                        if (node == nullptr || depth > 3) {
                            return;
                        }
                        std::cerr << "[fsim-class-hir] " << std::string(
                                         static_cast<std::size_t>(depth) * 2U,
                                         ' ')
                                  << "kind=" << static_cast<int>(node->kind)
                                  << " text=" << node->text << '\n';
                        for (const auto operand : node->operands) {
                            self(self, operand, depth + 1);
                        }
                    };
                    dump(dump, *item->value, 0);
                }
                throw std::invalid_argument {
                    "class HIR assignment expression is not executable"
                };
            }
            if (target->kind == semantic::sv::ExpressionKind::class_property
                || target->text.starts_with(property_prefix)) {
                auto owner = handle;
                if (!target->operands.empty()) {
                    const auto receiver = evaluate_packed(
                        target->operands.front(), handle, environment);
                    if (!receiver || receiver->low_word().bval != 0U) {
                        throw std::invalid_argument {
                            "class HIR property receiver is not a handle"
                        };
                    }
                    owner = receiver->low_word().aval;
                }
                write_property_(owner,
                    marker_payload(*target, property_prefix), *value);
                continue;
            }
            constexpr std::string_view static_prefix {
                "@sv-static-property:"
            };
            if (target->kind
                    == semantic::sv::ExpressionKind::class_static_property
                || target->text.starts_with(static_prefix)) {
                write_static_property_(
                    marker_payload(*target, static_prefix), *value, handle);
                continue;
            }
            if (target->kind == semantic::sv::ExpressionKind::name) {
                if (target->referenced_name
                    && target->referenced_name->selected) {
                    const auto id = *target->referenced_name->selected;
                    if (const auto* reference = active_reference(id)) {
                        if (reference->string_value
                            || !reference->write_packed
                            || !reference->write_packed(*value)) {
                            throw std::invalid_argument {
                                "class HIR reference assignment is not executable"
                            };
                        }
                        continue;
                    }
                    if (is_package_global(id)) {
                        if (write_package_packed_
                            && write_package_packed_(id, *value)) {
                            continue;
                        }
                        throw std::invalid_argument {
                            "class HIR package assignment has no exact runtime storage"
                        };
                    }
                }
                const auto local = environment.find(target->text);
                if (local != environment.end()) {
                    local->second = resize_packed(*value, local->second.width());
                    continue;
                }
            }
            throw std::invalid_argument {
                "class HIR assignment target is not executable"
            };
        }
        if (item->kind == semantic::sv::StatementKind::return_statement) {
            if (!item->value)
                return { true, runtime::PackedLogic4 { }, false, false, std::nullopt };
            if (string_return_) {
                auto text = evaluate_returned_string(
                    *item->value, handle, environment, string_environment);
                if (!text) {
                    throw std::invalid_argument {
                        "class HIR string return expression is not executable"
                    };
                }
                ExecutionResult returned;
                returned.returned = true;
                returned.text = std::move(*text);
                return returned;
            }
            const auto value = evaluate_packed(
                *item->value, handle, environment, return_context_width);
            if (!value) {
                throw std::invalid_argument {
                    "class HIR return expression is not executable"
                };
            }
            return { true, *value, false, false, std::nullopt };
        }
        if (item->kind == semantic::sv::StatementKind::block) {
            for (const auto declaration_id : item->declarations) {
                const auto* local = declaration(declaration_id);
                if (local == nullptr || !local->type)
                    continue;
                if (is_string(*local->type)) {
                    const auto value = local->initializer
                        ? evaluate_string(
                              *local->initializer, string_environment, handle)
                        : std::optional<std::string> { std::string { } };
                    if (!value)
                        throw std::invalid_argument { "class HIR block string initializer is not executable" };
                    string_environment[local->name] = *value;
                    if (string_bindings_ != nullptr) {
                        string_bindings_->insert_or_assign(
                            declaration_id.value(), *value);
                    }
                } else {
                    const auto local_width = width(*local->type);
                    const auto value = local->initializer
                        ? evaluate_packed(
                              *local->initializer, handle, environment,
                              local_width)
                        : std::optional<runtime::PackedLogic4> {
                              runtime::PackedLogic4(local_width, runtime::Logic4::x)
                          };
                    if (!value)
                        throw std::invalid_argument { "class HIR block initializer is not executable" };
                    environment[local->name]
                        = resize_packed(*value, local_width);
                }
            }
            auto result = execute(item->statements, handle, environment,
                string_environment, constructor, return_context_width);
            if (result.returned || result.broke || result.continued)
                return result;
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::conditional) {
            if (!item->condition)
                throw std::invalid_argument { "class HIR condition is absent" };
            const auto condition = evaluate_packed(
                *item->condition, handle, environment);
            if (!condition) {
                throw std::invalid_argument {
                    "class HIR condition is not a known packed value"
                };
            }
            bool condition_true { };
            for (std::size_t bit = 0U; bit < condition->width(); ++bit) {
                if (condition->get(bit) == runtime::Logic4::one) {
                    condition_true = true;
                    break;
                }
            }
            auto result = execute(
                condition_true
                    ? std::span<const semantic::StatementId> { item->statements }
                    : std::span<const semantic::StatementId> { item->else_statements },
                handle, environment, string_environment, constructor,
                return_context_width);
            if (result.returned || result.broke || result.continued)
                return result;
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::task_call) {
            const auto& spelling = item->task.spelling;
            constexpr std::string_view package_push_back_prefix {
                "@sv-container-push-back:"
            };
            if (spelling.starts_with(package_push_back_prefix)
                && item->task_arguments.size() == 2U
                && item->task_arguments[0].actual
                && item->task_arguments[1].actual
                && read_package_container_
                && write_package_container_) {
                if (!push_package_queue_back(
                        *item->task_arguments[0].actual,
                        *item->task_arguments[1].actual,
                        handle, environment, string_environment)) {
                    throw std::invalid_argument {
                        "package queue push_back is not executable"
                    };
                }
                continue;
            }
            if (item->task.selected) {
                const auto* selected = declaration(*item->task.selected);
                const bool package_owned
                    = selected != nullptr && is_package_member(*selected);
                const bool package_function = package_owned
                    && selected->form
                        == semantic::sv::DeclarationForm::function
                    && selected->callable
                    && selected->callable->function;
                if (package_function) {
                    semantic::sv::Expression call;
                    call.scope = item->scope;
                    call.kind = semantic::sv::ExpressionKind::call;
                    call.text = item->task.spelling;
                    call.referenced_name = item->task;
                    for (const auto& association : item->task_arguments) {
                        call.call_arguments.push_back({
                            association.formal, association.actual,
                            association.source });
                        if (association.actual)
                            call.operands.push_back(*association.actual);
                    }
                    StringEnvironment empty_strings;
                    auto& current_strings = strings_ != nullptr
                        ? *strings_ : empty_strings;
                    if (!invoke_package_function(call, handle,
                            environment, current_strings)) {
                        throw std::invalid_argument {
                            "selected package function call is not executable"
                        };
                    }
                    continue;
                }
            }
            if (spelling == "$stop" || spelling == "$finish"
                || spelling == "$fatal") {
                // A class method interpreted on the host cannot suspend the
                // simulation; reaching one ends it with an error.
                throw std::invalid_argument {
                    "class method executed " + spelling
                };
            }
            constexpr std::string_view task_prefix { "@sv-task:" };
            if (spelling.starts_with(task_prefix)
                && !item->task_arguments.empty()
                && item->task_arguments.front().actual) {
                const auto* receiver_expression
                    = expression(*item->task_arguments.front().actual);
                const auto receiver = evaluate_packed(
                    *item->task_arguments.front().actual, handle, environment);
                if (!receiver || receiver->low_word().bval != 0U) {
                    throw std::invalid_argument {
                        "class HIR method receiver is not a handle"
                    };
                }
                const bool virtual_dispatch = receiver_expression == nullptr
                    || receiver_expression->text != "super";
                const auto method_identity
                    = std::string_view { spelling }.substr(task_prefix.size());
                const auto* selected_method = method(
                    receiver->low_word().aval, method_identity,
                    virtual_dispatch);
                std::vector<runtime::PackedLogic4> actuals;
                std::vector<std::string> string_actuals;
                std::vector<std::string> names;
                std::vector<semantic::ExpressionId> actual_expressions;
                for (const auto& association :
                    item->task_arguments | std::views::drop(1)) {
                    if (!association.actual) {
                        throw std::invalid_argument {
                            "class HIR method argument is open"
                        };
                    }
                    names.push_back(
                        association.formal.value_or(std::string { }));
                    actual_expressions.push_back(*association.actual);
                }
                std::vector<std::size_t> mapped_formals;
                if (selected_method != nullptr) {
                    try {
                        mapped_formals = map_actual_formals(
                            *selected_method, names, actual_expressions.size());
                    } catch (const std::invalid_argument&) {
                        // Keep dispatch's original argument validation when
                        // the association list is not bindable here.
                    }
                }
                CallValueEnvironment typed_actuals;
                for (std::size_t index = 0U;
                     index < actual_expressions.size(); ++index) {
                    const auto actual_id = actual_expressions[index];
                    const semantic::sv::Declaration* formal { };
                    if (selected_method != nullptr
                        && mapped_formals.size() == actual_expressions.size()) {
                        formal = declaration(selected_method->formals[
                            mapped_formals[index]]);
                    }
                    if (formal != nullptr && formal->type
                        && is_input(formal->direction)
                        && is_typed_call_value(*formal->type)) {
                        auto typed = evaluate_typed_actual(actual_id,
                            *formal->type, handle, environment,
                            string_environment);
                        if (!typed) {
                            throw std::invalid_argument {
                                "class HIR typed array actual is not executable"
                            };
                        }
                        typed_actuals.emplace(formal->id,
                            std::move(*typed));
                        actuals.emplace_back(64U, runtime::Logic4::zero);
                        string_actuals.emplace_back();
                        continue;
                    }
                    // A string actual travels as text; the callee takes the
                    // form its formal's type needs.
                    const auto* source = expression(actual_id);
                    auto text = source != nullptr
                            && (source->kind
                                    == semantic::sv::ExpressionKind::string_literal
                                || (source->kind
                                        == semantic::sv::ExpressionKind::name
                                    && string_environment.contains(
                                        source->text)))
                        ? evaluate_string(
                              actual_id, string_environment, handle)
                        : std::optional<std::string> { };
                    const auto actual = text
                        ? std::optional<runtime::PackedLogic4> { }
                        : evaluate_packed(
                              actual_id, handle, environment,
                              formal != nullptr && formal->type
                                  && is_input(formal->direction)
                                  ? std::optional<std::size_t> {
                                        width(*formal->type, 0U) }
                                  : std::nullopt);
                    if (!actual && !text) {
                        text = evaluate_string(
                            actual_id, string_environment, handle);
                    }
                    if (!actual && !text) {
                        throw std::invalid_argument {
                            "class HIR method argument is not executable"
                        };
                    }
                    actuals.push_back(actual.value_or(
                        runtime::PackedLogic4(64U, runtime::Logic4::zero)));
                    string_actuals.push_back(text.value_or(std::string { }));
                }
                std::vector<std::uint8_t> directions;
                std::vector<std::optional<VariableReference>> actual_references;
                std::vector<std::size_t> actual_formals;
                if (selected_method != nullptr) {
                    actual_references.resize(actuals.size());
                    try {
                        actual_formals = map_actual_formals(
                            *selected_method, names, actuals.size());
                    } catch (const std::invalid_argument&) {
                        // Preserve the configured runtime dispatch. If it
                        // reaches this HIR method, the same association check
                        // will report the invalid call there.
                    }
                    if (actual_formals.size() == actuals.size()) {
                        try {
                            for (std::size_t index = 0U;
                                 index < actual_formals.size(); ++index) {
                                const auto* formal = declaration(
                                    selected_method->formals[
                                        actual_formals[index]]);
                                if (formal == nullptr || !formal->type
                                    || (formal->direction
                                            != semantic::sv::Direction::ref
                                        && formal->direction
                                            != semantic::sv::Direction::output
                                        && formal->direction
                                            != semantic::sv::Direction::inout)) {
                                    continue;
                                }
                                auto reference = variable_reference(
                                    actual_expressions[index], handle,
                                    environment, string_environment);
                                if (reference
                                    && reference->string_value
                                        == is_string(*formal->type)) {
                                    actual_references[index]
                                        = std::move(*reference);
                                }
                            }
                        } catch (const std::invalid_argument&) {
                            actual_references.assign(
                                actuals.size(), std::nullopt);
                        }
                    }
                }
                const bool typed_frame = !typed_actuals.empty();
                if (typed_frame) {
                    pending_call_frames_.push_back({
                        selected_method->canonical_identity,
                        receiver->low_word().aval,
                        std::move(typed_actuals), false });
                }
                const PendingFrameGuard pending_guard { pending_call_frames_,
                    typed_frame };
                (void)invoke_function_(receiver->low_word().aval,
                    method_identity,
                    actuals, string_actuals, names, directions,
                    virtual_dispatch, actual_references);
                if (typed_frame && !pending_call_frames_.back().consumed) {
                    throw std::invalid_argument {
                        "class HIR typed actual did not reach its selected method"
                    };
                }
                for (std::size_t index = 0U;
                     index < actual_formals.size(); ++index) {
                    const auto* formal = declaration(
                        selected_method->formals[actual_formals[index]]);
                    if (formal == nullptr)
                        throw std::invalid_argument {
                            "class HIR copy-out formal is unavailable"
                        };
                    if (formal->direction != semantic::sv::Direction::output
                        && formal->direction != semantic::sv::Direction::inout) {
                        continue;
                    }
                    const auto& reference = actual_references[index];
                    const bool written = reference
                        && (reference->string_value
                            ? reference->write_string
                                && reference->write_string(string_actuals[index])
                            : reference->write_packed
                                && reference->write_packed(actuals[index]));
                    if (!written) {
                        throw std::invalid_argument {
                            "class HIR output actual could not be copied back"
                        };
                    }
                }
                continue;
            }
        }
        if (item->kind == semantic::sv::StatementKind::container_method
            && item->value) {
            if (!evaluate_packed(*item->value, handle, environment)) {
                throw std::invalid_argument {
                    "class HIR call statement is not executable"
                };
            }
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::break_loop) {
            ExecutionResult result;
            result.broke = true;
            return result;
        }
        if (item->kind == semantic::sv::StatementKind::continue_loop) {
            ExecutionResult result;
            result.continued = true;
            return result;
        }
        if (item->kind == semantic::sv::StatementKind::loop) {
            const auto* loop_condition = item->condition
                ? expression(*item->condition) : nullptr;
            if (loop_condition != nullptr
                && loop_condition->kind
                    == semantic::sv::ExpressionKind::call
                && loop_condition->text == "@sv-foreach") {
                if (loop_condition->operands.size() > 2U) {
                    throw std::invalid_argument {
                        "class HIR multidimensional foreach is not executable"
                    };
                }
                if (loop_condition->operands.empty()) {
                    throw std::invalid_argument {
                        "class HIR foreach collection is absent"
                    };
                }
                auto collection = evaluate_bound_call_value(
                    loop_condition->operands.front(), handle, environment);
                if (!collection
                    || collection->kind != CallValue::Kind::array) {
                    throw std::invalid_argument {
                        "class HIR foreach collection is not an array actual"
                    };
                }
                auto index_name = item->loop_variable;
                if (index_name.empty() && loop_condition->operands.size() > 1U) {
                    const auto* index_expression = expression(
                        loop_condition->operands[1U]);
                    if (index_expression != nullptr)
                        index_name = index_expression->text;
                }
                if (index_name.empty()) {
                    throw std::invalid_argument {
                        "class HIR foreach index variable is absent"
                    };
                }
                const ScopedMapEntry index_guard { environment, index_name };
                auto index = collection->left;
                for (;;) {
                    environment.insert_or_assign(index_name,
                        runtime::PackedLogic4::from_aval_bval(
                            32U, static_cast<std::uint32_t>(index), 0U));
                    auto result = execute(item->statements, handle,
                        environment, string_environment, constructor,
                        return_context_width);
                    if (result.returned)
                        return result;
                    if (result.broke)
                        break;
                    if (index == collection->right)
                        break;
                    index += collection->left <= collection->right ? 1 : -1;
                }
                continue;
            }
            const auto known = [&](const semantic::ExpressionId id) {
                const auto value = evaluate_packed(id, handle, environment);
                if (!value || value->low_word().bval != 0U) {
                    throw std::invalid_argument {
                        "class HIR loop control is not a known packed value"
                    };
                }
                return *value;
            };
            const auto run_body = [&]() {
                return execute(item->statements, handle, environment,
                    string_environment, constructor, return_context_width);
            };
            constexpr std::uint64_t iteration_limit = 100'000'000U;
            if (item->loop_repeat) {
                if (!item->loop_limit) {
                    throw std::invalid_argument { "class HIR repeat has no count" };
                }
                const auto count = known(*item->loop_limit);
                const auto total = count.low_word().aval;
                bool stop = false;
                for (std::uint64_t index = 0; index < total && !stop; ++index) {
                    auto result = run_body();
                    if (result.returned)
                        return result;
                    stop = result.broke;
                }
                continue;
            }
            if (!item->loop_variable.empty() && item->loop_initial) {
                auto initial = known(*item->loop_initial);
                auto variable = environment.find(item->loop_variable);
                if (variable == environment.end()) {
                    variable = environment.emplace(item->loop_variable,
                        runtime::PackedLogic4 { 32U }).first;
                }
                variable->second = resize_packed(initial, variable->second.width());
            }
            const auto holds = [&]() {
                return !item->condition
                    || known(*item->condition).low_word().aval != 0U;
            };
            std::uint64_t iterations {};
            bool returned = false;
            ExecutionResult returned_result;
            while (item->loop_post_test || holds()) {
                if (++iterations > iteration_limit) {
                    throw std::invalid_argument { "class HIR loop does not terminate" };
                }
                auto result = run_body();
                if (result.returned) {
                    returned = true;
                    returned_result = std::move(result);
                    break;
                }
                if (result.broke)
                    break;
                if (item->loop_update_target && item->value) {
                    const auto* update_target = expression(*item->loop_update_target);
                    if (update_target != nullptr) {
                        auto variable = environment.find(update_target->text);
                        if (variable != environment.end()) {
                            variable->second = resize_packed(
                                known(*item->value), variable->second.width());
                        }
                    }
                }
                if (!item->loop_updates.empty()) {
                    auto update = execute(item->loop_updates, handle,
                        environment, string_environment, constructor,
                        return_context_width);
                    if (update.returned)
                        return update;
                }
                if (item->loop_post_test && !holds())
                    break;
            }
            if (returned)
                return returned_result;
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::selection
            && item->case_match == semantic::sv::CaseMatchKind::exact
            && item->condition) {
            // A case statement compares with === (IEEE 1800-2017 12.5).
            const auto selector = evaluate_packed(
                *item->condition, handle, environment);
            if (!selector) {
                throw std::invalid_argument {
                    "class HIR case expression is not executable"
                };
            }
            const auto matches = [&](const runtime::PackedLogic4& choice) {
                const auto width = std::max(selector->width(), choice.width());
                const auto left = resize_packed(*selector, width);
                const auto right = resize_packed(choice, width);
                for (std::size_t bit = 0; bit < width; ++bit) {
                    if (left.get(bit) != right.get(bit))
                        return false;
                }
                return true;
            };
            const semantic::sv::CaseAlternative* selected = nullptr;
            const semantic::sv::CaseAlternative* fallback = nullptr;
            for (const auto& alternative : item->case_alternatives) {
                if (alternative.is_default) {
                    fallback = &alternative;
                    continue;
                }
                for (const auto choice_id : alternative.choices) {
                    const auto choice = evaluate_packed(
                        choice_id, handle, environment);
                    if (!choice) {
                        throw std::invalid_argument {
                            "class HIR case item is not executable"
                        };
                    }
                    if (matches(*choice)) {
                        selected = &alternative;
                        break;
                    }
                }
                if (selected != nullptr)
                    break;
            }
            if (selected == nullptr)
                selected = fallback;
            if (selected != nullptr) {
                auto result = execute(selected->statements, handle,
                    environment, string_environment, constructor,
                    return_context_width);
                if (result.returned || result.broke || result.continued)
                    return result;
            }
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::display && output_) {
            write_display(*item, handle, environment, string_environment);
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::report) {
            auto message = item->output_text;
            if (item->value) {
                if (is_string_expression(
                        *item->value, handle, string_environment)) {
                    const auto evaluated = evaluate_string(
                        *item->value, string_environment, handle);
                    if (!evaluated) {
                        throw std::invalid_argument {
                            "class HIR string report message is not executable"
                        };
                    }
                    message = *evaluated;
                } else if (const auto packed = evaluate_packed(
                               *item->value, handle, environment)) {
                    const auto* source = expression(*item->value);
                    message = runtime::simir::make_formatted_output(
                        { }, { }, runtime::simir::OutputFormat::decimal,
                        *packed, source != nullptr && source->signed_value,
                        false, 0U, false, false);
                } else {
                    throw std::invalid_argument {
                        "class HIR report message is not executable"
                    };
                }
            }
            message = format_report(
                *item, message, handle, environment, string_environment);
            if (report_) {
                report_(report_process_, message,
                    elaboration::runtime_assertion_severity(
                        item->assertion_severity),
                    item->source);
            }
            if (item->assertion_severity
                == semantic::sv::AssertionSeverity::failure) {
                throw std::invalid_argument {
                    "class method executed $fatal"
                };
            }
            continue;
        }
        if (item->kind != semantic::sv::StatementKind::null_statement) {
            throw std::invalid_argument {
                "class HIR body contains an unsupported executable statement "
                "(kind "
                + std::to_string(static_cast<unsigned>(item->kind)) + ")"
            };
        }
    }
    return { false, runtime::PackedLogic4 { }, false, false, std::nullopt };
}

void SystemVerilogClassHirExecution::write_display(
    const semantic::sv::Statement& statement,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& strings)
{
    output_(format_display(statement, handle, environment, strings),
        statement.output_newline);
}

bool SystemVerilogClassHirExecution::is_string_expression(
    const semantic::ExpressionId id,
    const runtime::SystemVerilogClassHandle handle,
    const StringEnvironment& strings) const
{
    const auto* value = expression(id);
    if (value == nullptr) {
        return false;
    }
    using ExpressionKind = semantic::sv::ExpressionKind;
    if (value->kind == ExpressionKind::string_literal) {
        return true;
    }
    const auto call_array_element_for = [&](const semantic::ExpressionId base_id)
        -> std::optional<semantic::sv::TypeReference> {
        if (call_values_ == nullptr)
            return std::nullopt;
        const auto* base = expression(base_id);
        if (base == nullptr || base->kind != ExpressionKind::name
            || !base->referenced_name
            || !base->referenced_name->selected) {
            return std::nullopt;
        }
        const auto found = call_values_->find(
            *base->referenced_name->selected);
        return found == call_values_->end()
            ? std::nullopt
            : call_array_element_type(found->second.type);
    };
    if (value->kind == ExpressionKind::index
        && value->operands.size() == 2U) {
        const auto element = call_array_element_for(value->operands.front());
        constexpr std::string_view indexed_member_prefix { "index." };
        if (value->text.starts_with(indexed_member_prefix)) {
            if (!element || value->text.size() <= indexed_member_prefix.size()) {
                return false;
            }
            const auto* structure = call_aggregate_definition(*element);
            if (structure == nullptr
                || structure->form
                    != semantic::sv::TypeForm::unpacked_structure) {
                return false;
            }
            const auto member_name = std::string_view { value->text }
                .substr(indexed_member_prefix.size());
            const auto member = std::ranges::find(
                structure->members, member_name,
                &semantic::sv::PackedMember::name);
            return member != structure->members.end()
                && is_string(member->type);
        }
        return element && is_string(*element);
    }
    constexpr std::string_view selected_prefix { "@sv-select:" };
    if (value->kind == ExpressionKind::call
        && value->text.starts_with(selected_prefix)
        && value->operands.size() == 1U) {
        const auto* indexed = expression(value->operands.front());
        if (indexed != nullptr
            && indexed->kind == ExpressionKind::index
            && indexed->operands.size() == 2U) {
            const auto element = call_array_element_for(
                indexed->operands.front());
            const auto* structure = element
                ? call_aggregate_definition(*element) : nullptr;
            if (structure != nullptr
                && structure->form
                    == semantic::sv::TypeForm::unpacked_structure) {
                const auto member_name = marker_payload(
                    *value, selected_prefix);
                const auto member = std::ranges::find(
                    structure->members, member_name,
                    &semantic::sv::PackedMember::name);
                return member != structure->members.end()
                    && is_string(member->type);
            }
        }
    }
    if (value->kind == ExpressionKind::name) {
        if (value->referenced_name && value->referenced_name->selected) {
            const auto selected = *value->referenced_name->selected;
            if (const auto* reference = active_reference(selected)) {
                return reference->string_value;
            }
            const auto* selected_declaration = declaration(selected);
            return selected_declaration != nullptr
                && selected_declaration->type
                && is_string(*selected_declaration->type);
        }
        return strings.contains(value->text);
    }
    if (value->kind == ExpressionKind::concatenation) {
        return !value->operands.empty()
            && std::ranges::all_of(value->operands,
                [&](const auto operand) {
                    return is_string_expression(operand, handle, strings);
                });
    }
    if (value->kind == ExpressionKind::call && value->text == "?:"
        && value->operands.size() == 3U) {
        return is_string_expression(value->operands[1U], handle, strings)
            && is_string_expression(value->operands[2U], handle, strings);
    }
    constexpr std::string_view property_prefix { "@sv-property:" };
    if (value->kind == ExpressionKind::class_property
        || value->text.starts_with(property_prefix)) {
        const auto* property = instance_property(
            handle, marker_payload(*value, property_prefix));
        return property != nullptr && is_string(property->type);
    }
    constexpr std::string_view static_property_prefix {
        "@sv-static-property:"
    };
    if (value->kind == ExpressionKind::class_static_property
        || value->text.starts_with(static_property_prefix)) {
        const auto identity = marker_payload(*value, static_property_prefix);
        for (const auto& specialization : specializations_) {
            const auto found = std::ranges::find(
                specialization.properties, identity,
                &semantic::sv::SpecializedClassProperty::canonical_identity);
            if (found != specialization.properties.end()) {
                return found->static_storage && is_string(found->type);
            }
        }
        return false;
    }
    constexpr std::string_view static_method_prefix {
        "@sv-static-method:"
    };
    if (value->kind == ExpressionKind::class_static_method_call
        || value->text.starts_with(static_method_prefix)) {
        const auto* selected = static_method(
            marker_payload(*value, static_method_prefix));
        return selected != nullptr && selected->return_type
            && is_string(*selected->return_type);
    }
    constexpr std::string_view method_prefix { "@sv-method:" };
    constexpr std::string_view base_method_prefix {
        "@sv-base-method:"
    };
    if (value->kind == ExpressionKind::class_method_call
        || value->text.starts_with(method_prefix)
        || value->text.starts_with(base_method_prefix)) {
        const auto identity = marker_payload(*value,
            value->text.starts_with(base_method_prefix)
                ? base_method_prefix
                : method_prefix);
        bool found { };
        for (const auto& specialization : specializations_) {
            for (const auto& candidate : specialization.methods) {
                if (candidate.canonical_identity != identity) {
                    continue;
                }
                found = true;
                if (!candidate.return_type
                    || !is_string(*candidate.return_type)) {
                    return false;
                }
            }
        }
        return found;
    }
    if (value->kind == ExpressionKind::call && value->text == "$sformatf") {
        return true;
    }
    if (value->kind == ExpressionKind::call && value->referenced_name
        && value->referenced_name->selected) {
        const auto* selected = declaration(
            *value->referenced_name->selected);
        return selected != nullptr
            && selected->form == semantic::sv::DeclarationForm::function
            && selected->callable && selected->callable->function
            && is_string(selected->callable->return_type);
    }
    return false;
}

std::string SystemVerilogClassHirExecution::format_display(
    const semantic::sv::Statement& statement,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& strings)
{
    using Format = semantic::sv::OutputFormat;
    const auto format_one = [&](const std::optional<semantic::ExpressionId> value,
                                const Format format,
                                const bool suppress_leading_zero,
                                const std::uint32_t minimum_width,
                                const bool left_justify, const bool zero_pad,
                                const std::uint32_t precision) -> std::string {
        if (format == Format::hierarchy) {
            if (!active_hierarchy_.empty()) {
                return active_hierarchy_;
            }
            return handle == 0U
                ? std::string { }
                : heap_.object(handle).specialization_identity;
        }
        if (format == Format::time && !value) {
            const auto now = runtime::PackedLogic4::from_aval_bval(
                64U, now_ ? now_() : 0U, 0U);
            return runtime::simir::make_formatted_output({ }, { },
                runtime::simir::OutputFormat::decimal, now, false,
                suppress_leading_zero, minimum_width == 0U && !suppress_leading_zero
                    ? 20U : minimum_width,
                left_justify, zero_pad);
        }
        if (!value) {
            throw std::invalid_argument { "class HIR display value is absent" };
        }
        const auto* source = expression(*value);
        if (format == Format::pattern) {
            auto typed = evaluate_bound_call_value(
                *value, handle, environment);
            if (typed && typed->kind == CallValue::Kind::packed
                && typed->packed && !typed->type.class_identity.empty()) {
                const auto class_handle = typed->packed->low_word().aval;
                if (typed->packed->low_word().bval != 0U
                    || (class_handle != 0U && !heap_.contains(class_handle))) {
                    throw std::invalid_argument {
                        "class HIR pattern display handle is unknown or stale"
                    };
                }
                return class_handle == 0U
                    ? std::string { "null" }
                    : heap_.object(class_handle).dynamic_type;
            }
        }
        if (format == Format::string
            || (source != nullptr
                && source->kind == semantic::sv::ExpressionKind::string_literal)) {
            if (is_string_expression(*value, handle, strings)) {
                const auto text = evaluate_string(*value, strings, handle);
                if (!text) {
                    throw std::invalid_argument {
                        "class HIR string display value is not executable"
                    };
                }
                if (minimum_width <= text->size()) {
                    return *text;
                }
                const auto padding
                    = std::string(minimum_width - text->size(), ' ');
                return left_justify ? *text + padding : padding + *text;
            }
        }
        const auto packed = evaluate_packed(*value, handle, environment);
        if (!packed) {
            throw std::invalid_argument {
                "class HIR display value is not executable"
            };
        }
        if (format == Format::pattern || format == Format::strength
            || format == Format::hierarchy) {
            throw std::invalid_argument {
                "class HIR output conversion is unsupported"
            };
        }
        const auto runtime_format = elaboration::runtime_output_format(format);
        return runtime::simir::make_formatted_output({ }, { }, runtime_format,
            *packed, source != nullptr && source->signed_value,
            suppress_leading_zero, minimum_width, left_justify, zero_pad,
            frontend::SystemVerilogScalarKind::None, precision);
    };
    std::string text;
    if (!statement.output_values.empty()) {
        for (const auto& output : statement.output_values) {
            text += output.prefix;
            text += format_one(output.value, output.format,
                output.suppress_leading_zero, output.minimum_width,
                output.left_justify, output.zero_pad, output.precision);
        }
        text += statement.output_trailing_text;
    } else if (statement.output_format) {
        text = statement.output_prefix
            + format_one(statement.value, *statement.output_format,
                statement.output_suppress_leading_zero,
                statement.output_minimum_width, statement.output_left_justify,
                statement.output_zero_pad, statement.output_precision)
            + statement.output_suffix;
    } else {
        text = statement.output_text;
    }
    return text;
}

std::string SystemVerilogClassHirExecution::format_report(
    const semantic::sv::Statement& statement,
    const std::string_view format,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& strings)
{
    const auto parsed = frontend::parse_output_format(format);
    if (!parsed.valid) {
        throw std::invalid_argument {
            "class HIR report format is invalid"
        };
    }
    const auto consumes_value = [](const frontend::OutputFormat conversion) {
        return conversion != frontend::OutputFormat::Hierarchy
            && conversion != frontend::OutputFormat::Library;
    };
    const auto required_values = static_cast<std::size_t>(std::ranges::count_if(
        parsed.conversions, [&](const auto& conversion) {
            return consumes_value(conversion.format);
        }));
    if (statement.task_arguments.size() < required_values) {
        throw std::invalid_argument {
            "class HIR report format requires matching value arguments"
        };
    }
    semantic::sv::Statement display;
    display.output_text = parsed.trailing_text;
    std::size_t value_index { };
    for (const auto& conversion : parsed.conversions) {
        std::optional<semantic::ExpressionId> value;
        if (consumes_value(conversion.format)) {
            const auto& association = statement.task_arguments[value_index++];
            if (association.formal || !association.actual) {
                throw std::invalid_argument {
                    "class HIR report argument is not positional"
                };
            }
            value = association.actual;
        }
        display.output_values.push_back(semantic::sv::OutputValue {
            value,
            elaboration::hir_output_format(conversion.format),
            conversion.prefix,
            conversion.suppress_leading_zero,
            conversion.minimum_width,
            conversion.left_justify,
            conversion.zero_pad,
            conversion.precision,
        });
    }
    const auto first_extra = value_index;
    for (; value_index < statement.task_arguments.size(); ++value_index) {
        const auto& association = statement.task_arguments[value_index];
        if (association.formal || !association.actual) {
            throw std::invalid_argument {
                "class HIR report argument is not positional"
            };
        }
        display.output_values.push_back(semantic::sv::OutputValue {
            association.actual,
            semantic::sv::OutputFormat::decimal,
            value_index == first_extra ? parsed.trailing_text : std::string { },
        });
    }
    if (first_extra == value_index) {
        display.output_trailing_text = parsed.trailing_text;
    }
    return format_display(display, handle, environment, strings);
}

std::optional<std::string>
SystemVerilogClassHirExecution::evaluate_returned_string(
    const semantic::ExpressionId id,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& strings)
{
    const auto* value = expression(id);
    if (value != nullptr && value->kind == semantic::sv::ExpressionKind::call
        && value->text == "?:" && value->operands.size() == 3U) {
        const auto condition = evaluate_packed(
            value->operands[0], handle, environment);
        if (!condition) {
            return std::nullopt;
        }
        const auto word = condition->low_word();
        return evaluate_returned_string(
            (word.aval & ~word.bval) != 0U ? value->operands[1]
                                           : value->operands[2],
            handle, environment, strings);
    }
    return evaluate_string(id, strings, handle);
}

std::optional<std::string>
SystemVerilogClassHirExecution::evaluate_string_call(
    const semantic::ExpressionId id,
    const runtime::SystemVerilogClassHandle handle,
    StringEnvironment& strings)
{
    const auto* value = expression(id);
    if (value == nullptr || packed_environment_ == nullptr)
        return std::nullopt;
    if (value->kind == semantic::sv::ExpressionKind::call
        && value->text == "$sformatf" && !value->operands.empty()) {
        const auto format = evaluate_string(
            value->operands.front(), strings, handle);
        if (!format) {
            return std::nullopt;
        }
        semantic::sv::Statement formatting;
        for (const auto argument : value->operands | std::views::drop(1U)) {
            formatting.task_arguments.push_back({ std::nullopt, argument, { } });
        }
        return format_report(formatting, *format, handle,
            *packed_environment_, strings);
    }
    const auto contains_static_string = [this](auto&& self,
                                                const semantic::ExpressionId child)
        -> bool {
        const auto* operand = expression(child);
        if (operand == nullptr)
            return false;
        if (operand->kind == semantic::sv::ExpressionKind::name
            && operand->referenced_name
            && operand->referenced_name->selected) {
            const auto selected = *operand->referenced_name->selected;
            if (is_package_global(selected) || is_package_constant(selected))
                return false;
            const auto* local = declaration(selected);
            return local != nullptr
                && local->form == semantic::sv::DeclarationForm::variable
                && local->type
                && local->lifetime
                    == semantic::sv::Lifetime::static_lifetime
                && is_string(*local->type);
        }
        return std::ranges::any_of(operand->operands,
            [&](const auto nested) { return self(self, nested); });
    };
    if (std::ranges::any_of(value->operands,
            [&](const auto operand) {
                return contains_static_string(contains_static_string, operand);
            })) {
        return std::nullopt;
    }
    auto& environment = *packed_environment_;
    if (value->kind == semantic::sv::ExpressionKind::call
        && value->referenced_name && value->referenced_name->selected) {
        std::optional<std::string> result;
        const auto packed = invoke_package_function(
            *value, handle, environment, strings, &result);
        return packed ? result : std::nullopt;
    }

    constexpr std::string_view static_method_prefix {
        "@sv-static-method:"
    };
    constexpr std::string_view method_prefix { "@sv-method:" };
    constexpr std::string_view base_method_prefix { "@sv-base-method:" };
    const semantic::sv::SpecializedClassMethod* selected_method { };
    std::string_view canonical_identity;
    runtime::SystemVerilogClassHandle receiver_handle = handle;
    bool static_dispatch { };
    bool virtual_dispatch { true };
    std::size_t first_actual { };
    if (value->kind == semantic::sv::ExpressionKind::class_static_method_call
        || value->text.starts_with(static_method_prefix)) {
        static_dispatch = true;
        canonical_identity = marker_payload(*value, static_method_prefix);
        selected_method = static_method(canonical_identity);
    } else if ((value->kind
                    == semantic::sv::ExpressionKind::class_method_call
                || value->text.starts_with(method_prefix)
                || value->text.starts_with(base_method_prefix))
        && !value->operands.empty()) {
        virtual_dispatch = !value->text.starts_with(base_method_prefix);
        canonical_identity = marker_payload(*value,
            virtual_dispatch ? method_prefix : base_method_prefix);
        const auto receiver = evaluate_packed(
            value->operands.front(), handle, environment);
        if (!receiver || receiver->low_word().bval != 0U)
            return std::nullopt;
        receiver_handle = receiver->low_word().aval;
        selected_method = method(receiver_handle, canonical_identity,
            virtual_dispatch);
        first_actual = 1U;
    }
    if (selected_method == nullptr || !selected_method->defined
        || selected_method->external
        || !selected_method->return_type
        || !is_string(*selected_method->return_type)) {
        return std::nullopt;
    }
    if (std::ranges::any_of(
            selected_method->local_declarations, [this](const auto local_id) {
                const auto* local = declaration(local_id);
                return local != nullptr && local->type
                    && local->lifetime
                        == semantic::sv::Lifetime::static_lifetime
                    && is_string(*local->type);
            })) {
        return std::nullopt;
    }
    if (value->operands.size() < first_actual)
        return std::nullopt;
    const auto actual_count = value->operands.size() - first_actual;
    std::vector<std::string> actual_names(actual_count);
    if (value->argument_names.size() == value->operands.size()) {
        std::ranges::copy(
            value->argument_names | std::views::drop(first_actual),
            actual_names.begin());
    } else if (value->argument_names.size() == actual_count) {
        actual_names = value->argument_names;
    }
    const auto actual_formals = map_actual_formals(
        *selected_method, actual_names, actual_count);
    if (actual_count < selected_method->formals.size()
        && std::ranges::any_of(actual_names,
            [](const auto& name) { return name.empty(); })) {
        // Compact positional operands cannot distinguish trailing defaults
        // from an omitted positional slot before a later actual.
        return std::nullopt;
    }
    std::vector<runtime::PackedLogic4> actuals;
    std::vector<std::string> string_actuals;
    std::vector<std::optional<VariableReference>> actual_references;
    CallValueEnvironment typed_actuals;
    actuals.reserve(actual_count + 1U);
    string_actuals.reserve(actual_count + 1U);
    actual_references.reserve(actual_count);
    for (std::size_t index = 0U; index < actual_count; ++index) {
        const auto* formal = declaration(
            selected_method->formals[actual_formals[index]]);
        if (formal == nullptr || !formal->type)
            return std::nullopt;
        const auto actual_id = value->operands[first_actual + index];
        const auto reference = is_input(formal->direction)
            ? std::optional<VariableReference> { }
            : variable_reference(actual_id, handle, environment, strings);
        if (formal->direction == semantic::sv::Direction::ref
            || formal->direction == semantic::sv::Direction::output
            || formal->direction == semantic::sv::Direction::inout) {
            if (!reference || reference->string_value
                    != is_string(*formal->type)) {
                throw std::invalid_argument {
                    "class string-call output actual has no exact writable identity"
                };
            }
        }
        actual_references.push_back(reference);
        if (is_input(formal->direction)
            && is_typed_call_value(*formal->type)) {
            auto typed = evaluate_typed_actual(actual_id, *formal->type,
                handle, environment, strings);
            if (!typed) {
                throw std::invalid_argument {
                    "class string-call typed actual is not executable"
                };
            }
            typed_actuals.emplace(formal->id, std::move(*typed));
            actuals.emplace_back(64U, runtime::Logic4::zero);
            string_actuals.emplace_back();
        } else if (formal->direction == semantic::sv::Direction::output) {
            actuals.emplace_back(width(*formal->type), runtime::Logic4::x);
            string_actuals.emplace_back();
        } else if (is_string(*formal->type)) {
            const auto text = evaluate_string(actual_id, strings, handle);
            if (!text)
                return std::nullopt;
            actuals.emplace_back(64U, runtime::Logic4::zero);
            string_actuals.push_back(*text);
        } else {
            const auto packed = evaluate_packed(
                actual_id, handle, environment,
                is_input(formal->direction)
                    ? std::optional<std::size_t> {
                          width(*formal->type, 0U) }
                    : std::nullopt);
            if (!packed)
                return std::nullopt;
            actuals.push_back(*packed);
            string_actuals.emplace_back();
        }
    }
    auto invocation_names = actual_names;
    invocation_names.emplace_back("@result-string");
    actuals.emplace_back(64U, runtime::Logic4::zero);
    string_actuals.emplace_back();
    const bool typed_frame = !typed_actuals.empty();
    if (typed_frame) {
        pending_call_frames_.push_back({
            selected_method->canonical_identity,
            static_dispatch ? 0U : receiver_handle,
            std::move(typed_actuals), false });
    }
    const PendingFrameGuard pending_guard { pending_call_frames_, typed_frame };
    if (static_dispatch) {
        if (!invoke_static_function_)
            return std::nullopt;
        (void)invoke_static_function_(canonical_identity, actuals,
            string_actuals, invocation_names, {}, actual_references);
    } else {
        if (!invoke_function_)
            return std::nullopt;
        (void)invoke_function_(receiver_handle, canonical_identity, actuals,
            string_actuals, invocation_names, {}, virtual_dispatch,
            actual_references);
    }
    if (typed_frame && !pending_call_frames_.back().consumed) {
        throw std::invalid_argument {
            "class HIR typed actual did not reach its selected method"
        };
    }
    if (string_actuals.size() != actual_count + 1U)
        throw std::logic_error { "class string-call result channel is missing" };
    auto result = std::move(string_actuals.back());
    actuals.pop_back();
    string_actuals.pop_back();
    for (std::size_t index = 0U; index < actual_count; ++index) {
        const auto* formal = declaration(
            selected_method->formals[actual_formals[index]]);
        if (formal == nullptr)
            throw std::invalid_argument {
                "class string-call formal disappeared during copy-out"
            };
        if (formal->direction == semantic::sv::Direction::ref
            || is_input(formal->direction)) {
            continue;
        }
        const auto& reference = actual_references[index];
        const bool written = reference && (reference->string_value
            ? reference->write_string
                && reference->write_string(string_actuals[index])
            : reference->write_packed
                && reference->write_packed(actuals[index]));
        if (!written) {
            throw std::invalid_argument {
                "class string-call output actual could not be written back"
            };
        }
    }
    return result;
}

runtime::PackedLogic4 SystemVerilogClassHirExecution::invoke_function(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names_view,
    const std::span<const std::uint8_t> actual_directions_view,
    const ActualReferences actual_references,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    const ExecutionContextGuard context_guard { report_process_,
        report_process, active_hierarchy_, method.canonical_identity };
    return invoke_function_impl(method, handle, actuals, string_actuals,
        actual_names_view, actual_directions_view, actual_references, nullptr);
}

std::optional<std::int64_t>
SystemVerilogClassHirExecution::evaluate_range_bound(
    const std::optional<std::int64_t>& literal,
    const std::optional<semantic::ExpressionId>& expression,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment)
{
    if (literal)
        return literal;
    if (!expression)
        return std::nullopt;
    const auto value = evaluate_packed(*expression, handle, environment);
    return value ? value->known_signed_value() : std::nullopt;
}

runtime::PackedLogic4 SystemVerilogClassHirExecution::invoke_function_impl(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names_view,
    const std::span<const std::uint8_t> actual_directions_view,
    const std::span<const std::optional<VariableReference>> actual_references,
    std::vector<std::size_t>* const actual_formals_output)
{
    // A string result travels in a trailing hidden string actual, which the
    // caller copies back like an output string argument.
    constexpr std::string_view result_name { "@result-string" };
    const bool string_result = !actual_names_view.empty()
        && actual_names_view.back() == result_name
        && !actuals.empty() && string_actuals.size() == actuals.size();
    std::vector<std::string> names(
        actual_names_view.begin(), actual_names_view.end());
    std::vector<std::uint8_t> directions(
        actual_directions_view.begin(), actual_directions_view.end());
    if (string_result) {
        actuals.pop_back();
        string_actuals.pop_back();
        names.pop_back();
        if (!directions.empty()) {
            directions.pop_back();
        }
    }
    const std::span<const std::string> actual_names { names };
    const std::span<const std::uint8_t> actual_directions { directions };
    const ScopedValue string_return_guard { string_return_,
        method.return_type && is_string(*method.return_type) };

    if (depth_ >= maximum_class_call_depth)
        throw std::overflow_error { "SystemVerilog HIR class call depth exhausted" };
    ++depth_;
    struct Guard {
        std::size_t& depth;
        ~Guard() { --depth; }
    } guard { depth_ };
    if (method.kind != semantic::sv::ClassMethodKind::function) {
        throw std::invalid_argument {
            "HIR instance function invocation selected an incompatible method"
        };
    }
    if (string_actuals.size() != actuals.size()
        || (!actual_names.empty() && actual_names.size() != actuals.size())
        || (!actual_directions.empty()
            && actual_directions.size() != actuals.size())
        || (!actual_references.empty()
            && actual_references.size() != actuals.size())) {
        throw std::invalid_argument { "class HIR actual metadata does not align" };
    }
    std::vector<std::size_t> widths;
    std::ranges::transform(actuals, std::back_inserter(widths),
        [](const runtime::PackedLogic4& value) { return value.width(); });
    const auto mapped_actual_formals = map_actual_formals(
        method, actual_names, actuals.size());
    CallValueEnvironment typed_actuals;
    if (!pending_call_frames_.empty()) {
        auto& pending = pending_call_frames_.back();
        if (!pending.consumed
            && pending.method_identity == method.canonical_identity
            && pending.receiver == handle) {
            std::set<semantic::DeclarationId> supplied_formals;
            for (const auto formal_index : mapped_actual_formals)
                supplied_formals.insert(method.formals[formal_index]);
            for (const auto& [formal_id, actual] : pending.actuals) {
                const auto* formal = declaration(formal_id);
                if (formal == nullptr || !formal->type
                    || !is_input(formal->direction)
                    || !supplied_formals.contains(formal_id)) {
                    throw std::invalid_argument {
                        "class HIR aggregate actual does not match an input formal"
                    };
                }
                validate_typed_actual(actual, *formal->type);
            }
            typed_actuals = std::move(pending.actuals);
            pending.consumed = true;
        }
    }
    std::vector<std::pair<std::uint32_t,
        std::optional<VariableReference>>> previous_references;
    if (!actual_references.empty()) {
        for (std::size_t index = 0U; index < actuals.size(); ++index) {
            const auto formal_index = mapped_actual_formals[index];
            const auto* formal = declaration(method.formals[formal_index]);
            if (formal == nullptr)
                throw std::invalid_argument {
                    "class HIR formal is unavailable"
                };
            if (is_input(formal->direction))
                continue;
            if (!actual_references[index]) {
                throw std::invalid_argument {
                    "package function output actual has no exact variable identity"
                };
            }
            if (formal->direction != semantic::sv::Direction::ref)
                continue;
            const auto formal_id = method.formals[formal_index].value();
            const auto previous = active_references_.find(formal_id);
            previous_references.emplace_back(
                formal_id,
                previous == active_references_.end()
                    ? std::nullopt
                    : std::optional { previous->second });
            active_references_.insert_or_assign(
                formal_id, *actual_references[index]);
        }
    }
    struct ReferenceScope {
        std::map<std::uint32_t, VariableReference>& active;
        std::vector<std::pair<std::uint32_t,
            std::optional<VariableReference>>>& previous;
        ~ReferenceScope()
        {
            for (auto entry = previous.rbegin(); entry != previous.rend(); ++entry) {
                if (entry->second) {
                    active.insert_or_assign(entry->first, *entry->second);
                } else {
                    active.erase(entry->first);
                }
            }
        }
    } reference_scope { active_references_, previous_references };
    std::vector<std::size_t> actual_formals;
    auto environment = bind_actuals(
        method, handle, actuals, actual_names, &actual_formals);
    if (actual_formals != mapped_actual_formals)
        throw std::logic_error { "package actual/formal mapping changed" };
    for (const auto& [formal_id, actual] : typed_actuals) {
        (void)actual;
        const auto* formal = declaration(formal_id);
        if (formal == nullptr)
            throw std::invalid_argument {
                "class HIR aggregate formal disappeared during binding"
            };
        environment.erase(formal->name);
    }
    if (actual_formals_output != nullptr)
        *actual_formals_output = actual_formals;
    StringEnvironment strings;
    const ScopedSlot active_string_environment { strings_, &strings };
    StringBindingEnvironment string_bindings;
    const ScopedSlot active_string_bindings { string_bindings_,
        &string_bindings };
    const ScopedSlot active_packed_environment { packed_environment_,
        &environment };
    const ScopedSlot active_call_values { call_values_, &typed_actuals };
    initialize_strings(method, handle, actual_formals, string_actuals,
        strings, string_bindings);
    const auto void_result = method.return_type
        && method.return_type->target.spelling == "void";
    // A function's name is a variable holding its result when the body
    // ends without `return` (IEEE 1800-2017 13.4.1).
    const auto result_variable = !void_result && method.return_type
        && !environment.contains(method.name)
        && !is_string(*method.return_type);
    const bool string_result_variable
        = method.return_type && is_string(*method.return_type);
    if (result_variable) {
        environment.emplace(method.name,
            runtime::PackedLogic4 { width(*method.return_type, 32U) });
    }
    if (string_result_variable) {
        strings.emplace(method.name, std::string { });
        string_bindings.insert_or_assign(method.declaration.value(),
            std::string { });
    }
    auto result = execute(
        method.statements, handle, environment, strings, false,
        method.return_type
            ? std::optional<std::size_t> {
                  width(*method.return_type, 32U) }
            : std::nullopt);
    for (const auto local_id : method.local_declarations) {
        const auto* local = declaration(local_id);
        if (local != nullptr
            && local->lifetime == semantic::sv::Lifetime::static_lifetime) {
            if (const auto kept = environment.find(local->name);
                kept != environment.end()) {
                static_locals_.insert_or_assign(local_id.value(), kept->second);
            }
        }
    }
    if (!result.returned && result_variable) {
        result.returned = true;
        result.value = environment.at(method.name);
    }
    if (!result.returned && string_result_variable) {
        const auto value = string_bindings.find(method.declaration.value());
        if (value != string_bindings.end()) {
            result.returned = true;
            result.text = value->second;
        }
    }
    if (!result.returned && !void_result) {
        throw std::invalid_argument {
            "class HIR function completed without returning a value"
        };
    }
    for (std::size_t index = 0U; index < actuals.size(); ++index) {
        const auto formal_index = actual_formals[index];
        const auto* formal = declaration(method.formals[formal_index]);
        if (formal == nullptr)
            throw std::invalid_argument { "class HIR formal is unavailable" };
        if (!actual_directions.empty()
            && actual_directions[index]
                != direction_code(formal->direction)) {
            throw std::invalid_argument {
                "class HIR actual direction metadata is inconsistent"
            };
        }
        if (is_input(formal->direction))
            continue;
        if (formal->direction == semantic::sv::Direction::ref
            && index < actual_references.size()
            && actual_references[index]) {
            continue;
        }
        if (is_string(method.formal_types[formal_index])) {
            const auto value = string_bindings.find(formal->id.value());
            if (value == string_bindings.end()) {
                throw std::invalid_argument {
                    "class HIR string formal lost its declaration binding"
                };
            }
            string_actuals[index] = value->second;
        } else {
            actuals[index] = resize_packed(environment.at(formal->name), widths[index]);
        }
    }
    ++statistics_.hir_functions;
    if (string_result) {
        actuals.emplace_back(64U, runtime::Logic4::zero);
        string_actuals.push_back(result.text.value_or(std::string { }));
        return runtime::PackedLogic4(1U, runtime::Logic4::zero);
    }
    if (string_return_)
        return runtime::PackedLogic4(1U, runtime::Logic4::zero);
    if (void_result)
        return runtime::PackedLogic4 { };
    if (!method.return_type)
        throw std::invalid_argument { "class HIR function has no return type" };
    return resize_packed(result.value, width(*method.return_type,
        result.value.width()));
}

void SystemVerilogClassHirExecution::invoke_task(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    std::vector<runtime::PackedLogic4>& actuals)
{
    if (method.kind != semantic::sv::ClassMethodKind::task
        || method.static_method || method.formals.size() != actuals.size()) {
        throw std::invalid_argument {
            "UVM phase requires a nonstatic HIR task profile"
        };
    }
    for (const auto formal_id : method.formals) {
        const auto* formal = declaration(formal_id);
        if (formal == nullptr || !is_input(formal->direction)) {
            throw std::invalid_argument {
                "UVM phase HIR task requires input-only formals"
            };
        }
    }
    std::vector<std::size_t> actual_formals;
    auto environment = bind_actuals(method, handle, actuals, { }, &actual_formals);
    std::vector<std::string> string_actuals(actuals.size());
    StringEnvironment strings;
    const ScopedSlot active_string_environment { strings_, &strings };
    StringBindingEnvironment string_bindings;
    const ScopedSlot active_string_bindings { string_bindings_,
        &string_bindings };
    const ScopedSlot active_packed_environment { packed_environment_,
        &environment };
    initialize_strings(method, handle, actual_formals, string_actuals,
        strings, string_bindings);
    const auto result = execute(
        method.statements, handle, environment, strings, false);
    if (result.returned)
        throw std::invalid_argument { "SystemVerilog HIR class task returned" };
    ++statistics_.hir_tasks;
}

void SystemVerilogClassHirExecution::invoke_constructor_impl(
    const semantic::sv::ClassSpecialization& selected,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names)
{
    // A specialization also lists inherited methods; the class's own
    // constructor is the one declared by this class.
    auto constructor = std::ranges::find_if(
        selected.methods, [&](const semantic::sv::SpecializedClassMethod& method) {
            return method.kind == semantic::sv::ClassMethodKind::constructor
                && method.owner_identity == selected.declaration_identity;
        });
    if (constructor == selected.methods.end()) {
        constructor = std::ranges::find(
            selected.methods, semantic::sv::ClassMethodKind::constructor,
            &semantic::sv::SpecializedClassMethod::kind);
    }
    PackedEnvironment environment;
    StringEnvironment strings;
    const ScopedSlot active_string_environment { strings_, &strings };
    StringBindingEnvironment string_bindings;
    const ScopedSlot active_string_bindings { string_bindings_,
        &string_bindings };
    const ScopedSlot active_packed_environment { packed_environment_,
        &environment };
    if (constructor != selected.methods.end()) {
        std::vector<std::size_t> actual_formals;
        environment = bind_actuals(
            *constructor, handle, actuals, actual_names, &actual_formals);
        std::vector<std::string> padded_strings(
            string_actuals.begin(), string_actuals.end());
        padded_strings.resize(actual_formals.size());
        initialize_strings(
            *constructor, handle, actual_formals, padded_strings, strings,
            string_bindings);
    } else if (!actuals.empty()) {
        throw std::invalid_argument { "implicit HIR constructor has actuals" };
    }
    std::vector<runtime::PackedLogic4> base_actuals;
    std::vector<std::string> base_strings;
    std::vector<std::string> base_names;
    const auto* base = selected.base
        ? specialization(selected.base->specialization_identity)
        : nullptr;
    // A base constructor formal of type string takes its actual as text.
    const auto base_string_formal = [&](const std::size_t position,
                                        const std::string_view name) {
        if (base == nullptr) {
            return false;
        }
        auto method = std::ranges::find_if(base->methods,
            [&](const semantic::sv::SpecializedClassMethod& candidate) {
                return candidate.kind
                        == semantic::sv::ClassMethodKind::constructor
                    && candidate.owner_identity == base->declaration_identity;
            });
        if (method == base->methods.end()) {
            return false;
        }
        auto index = position;
        if (!name.empty()) {
            for (std::size_t formal = 0U; formal < method->formals.size();
                 ++formal) {
                const auto* item = declaration(method->formals[formal]);
                if (item != nullptr && item->name == name) {
                    index = formal;
                }
            }
        }
        return index < method->formal_types.size()
            && is_string(method->formal_types[index]);
    };
    constexpr std::string_view base_prefix { "@sv-base-constructor:" };
    if (constructor != selected.methods.end()) {
        for (const auto statement_id : constructor->statements) {
            const auto* item = statement(statement_id);
            if (item == nullptr
                || item->kind != semantic::sv::StatementKind::task_call
                || !item->task.spelling.starts_with(base_prefix)) {
                continue;
            }
            std::size_t first { };
            if (!item->task_arguments.empty()
                && item->task_arguments.front().actual) {
                const auto* expression = this->expression(
                    *item->task_arguments.front().actual);
                if (expression != nullptr && expression->text == "super")
                    first = 1U;
            }
            for (std::size_t index = first;
                 index < item->task_arguments.size(); ++index) {
                const auto& association = item->task_arguments[index];
                if (!association.actual)
                    throw std::invalid_argument { "open base HIR constructor actual" };
                const auto formal_name
                    = association.formal.value_or(std::string { });
                if (base_string_formal(index - first, formal_name)) {
                    const auto text = evaluate_string(
                        *association.actual, strings, handle);
                    if (!text)
                        throw std::invalid_argument { "base HIR constructor actual is not executable" };
                    base_actuals.emplace_back(64U, runtime::Logic4::zero);
                    base_strings.push_back(*text);
                    base_names.push_back(formal_name);
                    continue;
                }
                const auto value = evaluate_packed(
                    *association.actual, handle, environment);
                if (!value)
                    throw std::invalid_argument { "base HIR constructor actual is not executable" };
                base_actuals.push_back(*value);
                base_strings.emplace_back();
                base_names.push_back(association.formal.value_or(std::string { }));
            }
            break;
        }
    }
    // The UVM library's own constructors are replaced by the simulation's
    // UVM services, which initialize object and component state after the
    // user-derived constructor returns.
    if (selected.base
        && (base == nullptr
            || !support::is_uvm_library_identity(
                base->declaration_identity))) {
        if (base == nullptr)
            throw std::invalid_argument { "base HIR class specialization is unavailable" };
        invoke_constructor_impl(
            *base, handle, base_actuals, base_strings, base_names);
    }
    // Property initializers other than literals (which the object's
    // descriptor already holds) run after the base class's constructor
    // and before this class's constructor body (IEEE 1800-2017 8.7).
    for (const auto& property : selected.properties) {
        if (property.static_storage || !property.initializer
            || property.owner_identity != selected.declaration_identity
            || property.type.container_form) {
            continue;
        }
        const auto* initializer = expression(*property.initializer);
        using semantic::sv::ExpressionKind;
        if (initializer == nullptr
            || initializer->kind == ExpressionKind::integer_literal
            || initializer->kind == ExpressionKind::boolean_literal
            || initializer->kind == ExpressionKind::string_literal) {
            continue;
        }
        const auto identity = property.owner_identity + "::" + property.name;
        if (is_string(property.type)) {
            const auto text = evaluate_string(
                *property.initializer, strings, handle);
            if (text && write_string_property_)
                (void)write_string_property_(handle, identity, *text);
            continue;
        }
        PackedEnvironment initializer_environment;
        if (const auto value = evaluate_packed(
                *property.initializer, handle, initializer_environment)) {
            write_property_(handle, identity, *value);
        }
    }
    if (constructor != selected.methods.end()) {
        const auto result = execute(
            constructor->statements, handle, environment, strings, true);
        if (result.returned)
            throw std::invalid_argument { "SystemVerilog HIR constructor returned a value" };
    }
    ++statistics_.hir_constructors;
}

bool SystemVerilogClassHirExecution::invoke_constructor(
    const std::string_view specialization_identity,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals,
    const std::span<const std::string> actual_names,
    const std::optional<runtime::simir::ProcessId> report_process)
{
    const auto* selected = specialization(specialization_identity);
    if (selected == nullptr) {
        selected = specialization(heap_.object(handle).dynamic_type);
    }
    if (selected == nullptr)
        return false;
    const ExecutionContextGuard context_guard { report_process_,
        report_process, active_hierarchy_,
        std::string { specialization_identity } + "::new" };
    invoke_constructor_impl(
        *selected, handle, actuals, string_actuals, actual_names);
    return true;
}

SystemVerilogClassExecutionStatistics
SystemVerilogClassHirExecution::statistics() const noexcept
{
    return statistics_;
}

} // namespace fsim::app::application_detail
