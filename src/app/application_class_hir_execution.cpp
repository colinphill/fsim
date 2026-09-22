// SPDX-License-Identifier: Apache-2.0
#include "application_class_hir_execution.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <ranges>

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
    const auto width = decimal(
        std::string_view { spelling }.substr(0U, apostrophe));
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
    const auto found = std::ranges::find(
        hir_.declarations(), id, &semantic::sv::Declaration::id);
    return found == hir_.declarations().end() ? nullptr : &*found;
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
    if (requested == nullptr || requested->pure)
        return nullptr;
    if (!virtual_dispatch || !requested->virtual_slot)
        return requested;
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
    return selected;
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
        const auto found = environment.find(value->text);
        if (found != environment.end())
            return found->second;
    }
    return std::nullopt;
}

std::optional<runtime::PackedLogic4>
SystemVerilogClassHirExecution::evaluate_packed(
    const semantic::ExpressionId id,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment)
{
    const auto* value = expression(id);
    if (value == nullptr)
        return std::nullopt;
    using semantic::sv::ExpressionKind;
    if (value->kind == ExpressionKind::name) {
        if (value->text == "this" || value->text == "super") {
            return runtime::PackedLogic4::from_aval_bval(64U, handle, 0U);
        }
        const auto found = environment.find(value->text);
        return found == environment.end()
            ? std::nullopt
            : std::optional { found->second };
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
        return runtime::PackedLogic4::from_aval_bval(
            64U, static_cast<std::uint64_t>(integer), 0U);
    }
    if (value->kind == ExpressionKind::boolean_literal) {
        if (value->text == "true" || value->text == "1")
            return runtime::PackedLogic4::from_aval_bval(1U, 1U, 0U);
        if (value->text == "false" || value->text == "0")
            return runtime::PackedLogic4::from_aval_bval(1U, 0U, 0U);
        return std::nullopt;
    }
    if (value->kind == ExpressionKind::logic_literal)
        return logic_literal(value->text);
    if (value->kind == ExpressionKind::class_null
        || value->text == "@sv-null") {
        return runtime::PackedLogic4::from_aval_bval(64U, 0U, 0U);
    }
    constexpr std::string_view property_prefix { "@sv-property:" };
    if (value->kind == ExpressionKind::class_property
        || value->text.starts_with(property_prefix)) {
        return read_property_(
            handle, marker_payload(*value, property_prefix));
    }
    constexpr std::string_view static_property_prefix {
        "@sv-static-property:"
    };
    if (value->kind == ExpressionKind::class_static_property
        || value->text.starts_with(static_property_prefix)) {
        return read_static_property_(
            marker_payload(*value, static_property_prefix));
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
        if (!receiver || receiver->low_word().bval != 0U)
            return std::nullopt;
        std::vector<runtime::PackedLogic4> actuals;
        for (const auto operand : value->operands | std::views::drop(1)) {
            const auto actual = evaluate_packed(operand, handle, environment);
            if (!actual)
                return std::nullopt;
            actuals.push_back(*actual);
        }
        std::vector<std::string> names(actuals.size());
        if (value->argument_names.size() == value->operands.size()) {
            std::ranges::copy(
                value->argument_names | std::views::drop(1), names.begin());
        }
        std::vector<std::string> string_actuals(actuals.size());
        std::vector<std::uint8_t> directions;
        const auto identity = marker_payload(*value,
            virtual_dispatch ? method_prefix : base_method_prefix);
        auto result = invoke_function_(receiver->low_word().aval, identity,
            actuals, string_actuals, names, directions, virtual_dispatch);
        return result;
    }
    constexpr std::string_view static_method_prefix {
        "@sv-static-method:"
    };
    if (value->kind == ExpressionKind::class_static_method_call
        || value->text.starts_with(static_method_prefix)) {
        std::vector<runtime::PackedLogic4> actuals;
        for (const auto operand : value->operands) {
            const auto actual = evaluate_packed(operand, handle, environment);
            if (!actual)
                return std::nullopt;
            actuals.push_back(*actual);
        }
        auto names = value->argument_names;
        if (names.empty())
            names.resize(actuals.size());
        std::vector<std::string> string_actuals(actuals.size());
        return invoke_static_function_(
            marker_payload(*value, static_method_prefix), actuals,
            string_actuals, names, { });
    }
    if (value->kind == ExpressionKind::unary
        && value->operands.size() == 1U) {
        const auto operand = evaluate_packed(
            value->operands.front(), handle, environment);
        if (!operand)
            return std::nullopt;
        const auto word = operand->low_word();
        if (word.bval != 0U)
            return runtime::PackedLogic4(64U, runtime::Logic4::x);
        if (value->text == "+")
            return operand;
        if (value->text == "-") {
            return runtime::PackedLogic4::from_aval_bval(
                64U, std::uint64_t { 0 } - word.aval, 0U);
        }
        if (value->text == "~")
            return runtime::PackedLogic4::from_aval_bval(64U, ~word.aval, 0U);
        if (value->text == "!") {
            return runtime::PackedLogic4::from_aval_bval(
                1U, word.aval == 0U ? 1U : 0U, 0U);
        }
        return std::nullopt;
    }
    if (value->kind != ExpressionKind::binary
        || value->operands.size() != 2U) {
        return std::nullopt;
    }
    const auto left = evaluate_packed(value->operands[0], handle, environment);
    const auto right = evaluate_packed(value->operands[1], handle, environment);
    if (!left || !right)
        return std::nullopt;
    const auto lhs_word = left->low_word();
    const auto rhs_word = right->low_word();
    if (lhs_word.bval != 0U || rhs_word.bval != 0U)
        return runtime::PackedLogic4(64U, runtime::Logic4::x);
    const auto lhs = lhs_word.aval;
    const auto rhs = rhs_word.aval;
    std::uint64_t result { };
    if (value->text == "+") result = lhs + rhs;
    else if (value->text == "-") result = lhs - rhs;
    else if (value->text == "*") result = lhs * rhs;
    else if (value->text == "/") {
        if (rhs == 0U) return std::nullopt;
        result = lhs / rhs;
    } else if (value->text == "%") {
        if (rhs == 0U) return std::nullopt;
        result = lhs % rhs;
    } else if (value->text == "&") result = lhs & rhs;
    else if (value->text == "|") result = lhs | rhs;
    else if (value->text == "^") result = lhs ^ rhs;
    else if (value->text == "<<") result = rhs < 64U ? lhs << rhs : 0U;
    else if (value->text == ">>") result = rhs < 64U ? lhs >> rhs : 0U;
    else if (value->text == "==" || value->text == "===") result = lhs == rhs;
    else if (value->text == "!=" || value->text == "!==") result = lhs != rhs;
    else if (value->text == "<") result = lhs < rhs;
    else if (value->text == "<=") result = lhs <= rhs;
    else if (value->text == ">") result = lhs > rhs;
    else if (value->text == ">=") result = lhs >= rhs;
    else return std::nullopt;
    return runtime::PackedLogic4::from_aval_bval(64U, result, 0U);
}

SystemVerilogClassHirExecution::PackedEnvironment
SystemVerilogClassHirExecution::bind_actuals(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> actual_names,
    std::vector<std::size_t>* const actual_formals)
{
    if (!actual_names.empty() && actual_names.size() != actuals.size()) {
        throw std::invalid_argument {
            "class method actual names do not align with values"
        };
    }
    PackedEnvironment environment;
    std::vector<bool> assigned(method.formals.size());
    std::size_t next_positional { };
    for (std::size_t index = 0U; index < actuals.size(); ++index) {
        const auto name = actual_names.empty()
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
                const auto* declaration = this->declaration(
                    method.formals[candidate]);
                if (declaration != nullptr && declaration->name == name) {
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
    const std::span<const std::size_t> actual_formals,
    const std::span<const std::string> string_actuals,
    StringEnvironment& environment) const
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
            continue;
        }
        const auto value = formal->initializer
            ? evaluate_string(*formal->initializer, environment)
            : std::nullopt;
        if (!value) {
            throw std::invalid_argument {
                "class method HIR string formal has no executable default"
            };
        }
        environment.emplace(formal->name, *value);
    }
    for (const auto local_id : method.local_declarations) {
        if (std::ranges::find(method.formals, local_id) != method.formals.end())
            continue;
        const auto* local = declaration(local_id);
        if (local == nullptr || !local->type || !is_string(*local->type))
            continue;
        const auto value = local->initializer
            ? evaluate_string(*local->initializer, environment)
            : std::optional<std::string> { std::string { } };
        if (!value) {
            throw std::invalid_argument {
                "class method HIR string local initializer is not executable"
            };
        }
        environment[local->name] = *value;
    }
}

SystemVerilogClassHirExecution::ExecutionResult
SystemVerilogClassHirExecution::execute(
    const std::span<const semantic::StatementId> statements,
    const runtime::SystemVerilogClassHandle handle,
    PackedEnvironment& environment,
    StringEnvironment& string_environment,
    const bool constructor)
{
    constexpr std::string_view base_prefix { "@sv-base-constructor:" };
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
                const auto string = string_environment.find(target->text);
                if (string != string_environment.end()) {
                    const auto value = evaluate_string(
                        *item->value, string_environment);
                    if (!value) {
                        throw std::invalid_argument {
                            "class HIR string assignment is not executable"
                        };
                    }
                    string->second = *value;
                    continue;
                }
            }
            const auto value = evaluate_packed(
                *item->value, handle, environment);
            if (!value) {
                throw std::invalid_argument {
                    "class HIR assignment expression is not executable"
                };
            }
            constexpr std::string_view property_prefix { "@sv-property:" };
            if (target->kind == semantic::sv::ExpressionKind::class_property
                || target->text.starts_with(property_prefix)) {
                write_property_(handle,
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
                    marker_payload(*target, static_prefix), *value);
                continue;
            }
            if (target->kind == semantic::sv::ExpressionKind::name) {
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
                return { true, runtime::PackedLogic4 { } };
            const auto value = evaluate_packed(
                *item->value, handle, environment);
            if (!value) {
                throw std::invalid_argument {
                    "class HIR return expression is not executable"
                };
            }
            return { true, *value };
        }
        if (item->kind == semantic::sv::StatementKind::block) {
            for (const auto declaration_id : item->declarations) {
                const auto* local = declaration(declaration_id);
                if (local == nullptr || !local->type)
                    continue;
                if (is_string(*local->type)) {
                    const auto value = local->initializer
                        ? evaluate_string(*local->initializer, string_environment)
                        : std::optional<std::string> { std::string { } };
                    if (!value)
                        throw std::invalid_argument { "class HIR block string initializer is not executable" };
                    string_environment[local->name] = *value;
                } else {
                    const auto local_width = width(*local->type);
                    const auto value = local->initializer
                        ? evaluate_packed(*local->initializer, handle, environment)
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
                string_environment, constructor);
            if (result.returned)
                return result;
            continue;
        }
        if (item->kind == semantic::sv::StatementKind::conditional) {
            if (!item->condition)
                throw std::invalid_argument { "class HIR condition is absent" };
            const auto condition = evaluate_packed(
                *item->condition, handle, environment);
            if (!condition || condition->low_word().bval != 0U) {
                throw std::invalid_argument {
                    "class HIR condition is not a known packed value"
                };
            }
            auto result = execute(
                condition->low_word().aval != 0U
                    ? std::span<const semantic::StatementId> { item->statements }
                    : std::span<const semantic::StatementId> { item->else_statements },
                handle, environment, string_environment, constructor);
            if (result.returned)
                return result;
            continue;
        }
        if (item->kind != semantic::sv::StatementKind::null_statement) {
            throw std::invalid_argument {
                "class HIR body contains an unsupported executable statement"
            };
        }
    }
    return { false, runtime::PackedLogic4 { } };
}

runtime::PackedLogic4 SystemVerilogClassHirExecution::invoke_function(
    const semantic::sv::SpecializedClassMethod& method,
    const runtime::SystemVerilogClassHandle handle,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names,
    const std::span<const std::uint8_t> actual_directions)
{
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
            && actual_directions.size() != actuals.size())) {
        throw std::invalid_argument { "class HIR actual metadata does not align" };
    }
    std::vector<std::size_t> widths;
    std::ranges::transform(actuals, std::back_inserter(widths),
        [](const runtime::PackedLogic4& value) { return value.width(); });
    std::vector<std::size_t> actual_formals;
    auto environment = bind_actuals(
        method, handle, actuals, actual_names, &actual_formals);
    StringEnvironment strings;
    initialize_strings(method, actual_formals, string_actuals, strings);
    const auto result = execute(
        method.statements, handle, environment, strings, false);
    const auto void_result = method.return_type
        && method.return_type->target.spelling == "void";
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
        if (is_string(method.formal_types[formal_index]))
            string_actuals[index] = strings.at(formal->name);
        else
            actuals[index] = resize_packed(environment.at(formal->name), widths[index]);
    }
    ++statistics_.hir_functions;
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
    initialize_strings(method, actual_formals, string_actuals, strings);
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
    const std::span<const std::string> actual_names)
{
    const auto constructor = std::ranges::find(
        selected.methods, semantic::sv::ClassMethodKind::constructor,
        &semantic::sv::SpecializedClassMethod::kind);
    PackedEnvironment environment;
    if (constructor != selected.methods.end()) {
        environment = bind_actuals(
            *constructor, handle, actuals, actual_names);
    } else if (!actuals.empty()) {
        throw std::invalid_argument { "implicit HIR constructor has actuals" };
    }
    std::vector<runtime::PackedLogic4> base_actuals;
    std::vector<std::string> base_names;
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
                const auto value = evaluate_packed(
                    *association.actual, handle, environment);
                if (!value)
                    throw std::invalid_argument { "base HIR constructor actual is not executable" };
                base_actuals.push_back(*value);
                base_names.push_back(association.formal.value_or(std::string { }));
            }
            break;
        }
    }
    if (selected.base) {
        const auto* base = specialization(selected.base->specialization_identity);
        if (base == nullptr)
            throw std::invalid_argument { "base HIR class specialization is unavailable" };
        invoke_constructor_impl(*base, handle, base_actuals, base_names);
    }
    if (constructor != selected.methods.end()) {
        StringEnvironment strings;
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
    const std::span<const std::string> actual_names)
{
    const auto* selected = specialization(specialization_identity);
    if (selected == nullptr) {
        selected = specialization(heap_.object(handle).dynamic_type);
    }
    if (selected == nullptr)
        return false;
    invoke_constructor_impl(*selected, handle, actuals, actual_names);
    return true;
}

SystemVerilogClassExecutionStatistics
SystemVerilogClassHirExecution::statistics() const noexcept
{
    return statistics_;
}

} // namespace fsim::app::application_detail
