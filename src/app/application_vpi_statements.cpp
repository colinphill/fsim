// SPDX-License-Identifier: Apache-2.0
#include "application_vpi_statements.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace fsim::app::application_detail {

namespace {

namespace sv = semantic::sv;
using runtime::Logic4;
using runtime::PackedLogic4;

// The statement list an implicit block holds: a process's, a callable's,
// a statement's then or else branch, or case item `aux - body_case`.
constexpr std::uint32_t body_process = 0U;
constexpr std::uint32_t body_callable = 1U;
constexpr std::uint32_t body_then = 2U;
constexpr std::uint32_t body_else = 3U;
constexpr std::uint32_t body_case = 4U;

// The operator of a unary, binary or update expression (IEEE 1800-2023
// 37.78 vpiOpType).
[[nodiscard]] std::optional<PLI_INT32> operator_type(
    const std::string_view text, const std::size_t operands)
{
    if (operands == 1U) {
        static constexpr std::pair<std::string_view, PLI_INT32> unary[] = {
            { "-", vpiMinusOp }, { "+", vpiPlusOp }, { "!", vpiNotOp },
            { "~", vpiBitNegOp }, { "&", vpiUnaryAndOp },
            { "~&", vpiUnaryNandOp }, { "|", vpiUnaryOrOp },
            { "~|", vpiUnaryNorOp }, { "^", vpiUnaryXorOp },
            { "~^", vpiUnaryXNorOp }, { "^~", vpiUnaryXNorOp },
        };
        for (const auto& [spelling, type] : unary) {
            if (text == spelling) {
                return type;
            }
        }
        return std::nullopt;
    }
    if (operands == 3U && text == "?:") {
        return vpiConditionOp;
    }
    static constexpr std::pair<std::string_view, PLI_INT32> binary[] = {
        { "+", vpiAddOp }, { "-", vpiSubOp }, { "*", vpiMultOp },
        { "/", vpiDivOp }, { "%", vpiModOp }, { "**", vpiPowerOp },
        { "<<", vpiLShiftOp }, { ">>", vpiRShiftOp },
        { "<<<", vpiArithLShiftOp }, { ">>>", vpiArithRShiftOp },
        { "==", vpiEqOp }, { "!=", vpiNeqOp }, { "===", vpiCaseEqOp },
        { "!==", vpiCaseNeqOp }, { "==?", vpiWildEqOp },
        { "!=?", vpiWildNeqOp }, { ">", vpiGtOp }, { ">=", vpiGeOp },
        { "<", vpiLtOp }, { "<=", vpiLeOp }, { "&&", vpiLogAndOp },
        { "||", vpiLogOrOp }, { "&", vpiBitAndOp }, { "|", vpiBitOrOp },
        { "^", vpiBitXorOp }, { "~^", vpiBitXNorOp }, { "^~", vpiBitXNorOp },
        { "->", vpiImplyOp }, { "inside", vpiInsideOp },
    };
    for (const auto& [spelling, type] : binary) {
        if (text == spelling) {
            return type;
        }
    }
    return std::nullopt;
}

[[nodiscard]] Logic4 digit_logic(const char digit) noexcept
{
    switch (digit) {
    case 'x':
    case 'X':
        return Logic4::x;
    case 'z':
    case 'Z':
    case '?':
        return Logic4::z;
    default:
        return digit == '1' ? Logic4::one : Logic4::zero;
    }
}

// An integer literal's value (IEEE 1800-2023 5.7.1): sized and unsized,
// based and decimal, with x and z digits; an unsized one is 32 bits.
[[nodiscard]] std::optional<VpiStatementValue> integer_value(
    std::string_view text)
{
    std::string digits;
    for (const auto character : text) {
        if (character != '_' && character != ' ') {
            digits.push_back(character);
        }
    }
    VpiStatementValue result;
    const auto quote = digits.find('\'');
    if (quote == std::string::npos) {
        std::uint64_t value { };
        const auto parsed = std::from_chars(
            digits.data(), digits.data() + digits.size(), value);
        if (parsed.ec != std::errc { } || parsed.ptr != digits.data() + digits.size()) {
            return std::nullopt;
        }
        result.is_signed = true;
        const auto width = value > 0xFFFF'FFFFU ? 64U : 32U;
        result.bits = PackedLogic4(width, Logic4::zero);
        for (std::size_t bit = 0U; bit < width; ++bit) {
            result.bits.set(bit, ((value >> bit) & 1U) != 0U ? Logic4::one
                                                             : Logic4::zero);
        }
        return result;
    }
    std::size_t width = 32U;
    bool sized = false;
    if (quote != 0U) {
        std::uint64_t size { };
        const auto parsed = std::from_chars(digits.data(),
            digits.data() + quote, size);
        if (parsed.ec != std::errc { } || size == 0U || size > 65536U) {
            return std::nullopt;
        }
        width = static_cast<std::size_t>(size);
        sized = true;
    }
    auto rest = std::string_view { digits }.substr(quote + 1U);
    if (!rest.empty() && (rest.front() == 's' || rest.front() == 'S')) {
        result.is_signed = true;
        rest.remove_prefix(1U);
    }
    if (rest.empty()) {
        return std::nullopt;
    }
    const auto base = static_cast<char>(
        std::tolower(static_cast<unsigned char>(rest.front())));
    // An unbased unsized literal fills its context (5.7.1); alone it is
    // one bit.
    if (base == '0' || base == '1' || base == 'x' || base == 'z') {
        result.bits = PackedLogic4(1U, digit_logic(base));
        return result;
    }
    rest.remove_prefix(1U);
    std::vector<Logic4> bits; // least significant first
    if (base == 'd') {
        if (rest.size() == 1U && digit_logic(rest.front()) != Logic4::zero
            && rest.front() != '0' && rest.front() != '1'
            && !std::isdigit(static_cast<unsigned char>(rest.front()))) {
            bits.assign(width, digit_logic(rest.front()));
        } else {
            std::uint64_t value { };
            const auto parsed = std::from_chars(
                rest.data(), rest.data() + rest.size(), value);
            if (parsed.ec != std::errc { }) {
                return std::nullopt;
            }
            for (std::size_t bit = 0U; bit < 64U; ++bit) {
                bits.push_back(((value >> bit) & 1U) != 0U ? Logic4::one
                                                           : Logic4::zero);
            }
        }
    } else {
        const auto per_digit = base == 'b' ? 1U
            : base == 'o'                 ? 3U
            : base == 'h'                 ? 4U
                                          : 0U;
        if (per_digit == 0U) {
            return std::nullopt;
        }
        for (auto digit = rest.rbegin(); digit != rest.rend(); ++digit) {
            const auto logic = digit_logic(*digit);
            if (logic == Logic4::x || logic == Logic4::z) {
                bits.insert(bits.end(), per_digit, logic);
                continue;
            }
            unsigned value { };
            const auto lower = static_cast<char>(
                std::tolower(static_cast<unsigned char>(*digit)));
            if (lower >= '0' && lower <= '9') {
                value = static_cast<unsigned>(lower - '0');
            } else if (lower >= 'a' && lower <= 'f') {
                value = static_cast<unsigned>(lower - 'a' + 10);
            } else {
                return std::nullopt;
            }
            for (unsigned bit = 0U; bit < per_digit; ++bit) {
                bits.push_back(((value >> bit) & 1U) != 0U ? Logic4::one
                                                           : Logic4::zero);
            }
        }
    }
    if (!sized && bits.size() > width) {
        width = bits.size();
    }
    // A leftmost x or z digit extends; other values zero-extend.
    const auto fill = !bits.empty()
            && (bits.back() == Logic4::x || bits.back() == Logic4::z)
        ? bits.back()
        : Logic4::zero;
    result.bits = PackedLogic4(width, fill);
    for (std::size_t bit = 0U; bit < width && bit < bits.size(); ++bit) {
        result.bits.set(bit, bits[bit]);
    }
    return result;
}

[[nodiscard]] std::string_view base_name(std::string_view name)
{
    const auto bracket = name.find('[');
    return bracket == std::string_view::npos ? name : name.substr(0U, bracket);
}

} // namespace

VpiStatementModel::VpiStatementModel(const semantic::sv::Hir& hir,
    const semantic::Model& semantics, Hooks hooks)
    : hir_ { hir }
    , semantics_ { semantics }
    , hooks_ { std::move(hooks) }
{
    for (const auto& record : hir_.statements()) {
        statements_.emplace(record.id.value(), &record);
    }
    for (const auto& record : hir_.processes()) {
        processes_.emplace(record.id.value(), &record);
    }
}

const sv::Statement* VpiStatementModel::statement(const std::uint32_t id) const
{
    const auto found = statements_.find(id);
    return found == statements_.end() ? nullptr : found->second;
}

const sv::Process* VpiStatementModel::process(const std::uint32_t id) const
{
    const auto found = processes_.find(id);
    return found == processes_.end() ? nullptr : found->second;
}

const sv::Expression* VpiStatementModel::expression(
    const std::uint32_t id) const
{
    return hir_.find_expression(semantic::ExpressionId::from_index(id));
}

fsim_vpi_handle_v1 VpiStatementModel::node(const Kind kind,
    const std::uint32_t id, const std::uint32_t aux,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent,
    const bool of_process)
{
    const auto key = std::tuple { kind, id, aux, scope, of_process };
    const auto [found, created] = index_.try_emplace(key, nodes_.size());
    if (created) {
        nodes_.push_back(Node { kind, id, aux, scope, parent, of_process });
    }
    return hooks_.handle(found->second);
}

// A named block is its scope object (IEEE 1800-2023 37.43); other
// statements are nodes.
fsim_vpi_handle_v1 VpiStatementModel::statement_handle(
    const semantic::StatementId id, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    const auto* const record = statement(id.value());
    if (record == nullptr) {
        return 0U;
    }
    if ((record->kind == sv::StatementKind::block
            || record->kind == sv::StatementKind::fork)
        && !record->label.empty()) {
        if (const auto named = hooks_.child_scope(scope, record->label)) {
            return named;
        }
    }
    return node(Kind::statement, id.value(), 0U, scope, parent);
}

// A name is the object it refers to (IEEE 1800-2023 37.78); one that
// refers to nothing published is a reference node.
fsim_vpi_handle_v1 VpiStatementModel::expression_handle(
    const semantic::ExpressionId id, const fsim_vpi_handle_v1 scope,
    const fsim_vpi_handle_v1 parent)
{
    const auto* const record = expression(id.value());
    if (record == nullptr) {
        return 0U;
    }
    if (record->kind == sv::ExpressionKind::name) {
        const auto& name = record->referenced_name
            ? record->referenced_name->spelling
            : record->text;
        if (const auto object = hooks_.resolve(scope, name)) {
            return object;
        }
        return node(Kind::reference, id.value(), 0U, scope, parent);
    }
    return node(Kind::expression, id.value(), 0U, scope, parent);
}

const sv::Statement* VpiStatementModel::find_block(
    const std::vector<semantic::StatementId>& statements,
    const std::string& label) const
{
    for (const auto id : statements) {
        const auto* const record = statement(id.value());
        if (record == nullptr) {
            continue;
        }
        if ((record->kind == sv::StatementKind::block
                || record->kind == sv::StatementKind::fork)
            && record->label == label) {
            return record;
        }
        // Statements nest without opening a named scope, except a named
        // block of their own.
        if ((record->kind == sv::StatementKind::block
                || record->kind == sv::StatementKind::fork)
            && !record->label.empty()) {
            continue;
        }
        if (const auto* found = find_block(record->statements, label)) {
            return found;
        }
        if (const auto* found = find_block(record->else_statements, label)) {
            return found;
        }
        for (const auto& alternative : record->case_alternatives) {
            if (const auto* found = find_block(alternative.statements, label)) {
                return found;
            }
        }
    }
    return nullptr;
}

std::optional<VpiStatementModel::Container> VpiStatementModel::locate(
    const VpiHirScope& scope)
{
    const auto unit = std::ranges::find(
        hir_.units(), semantic::UnitId::from_index(scope.unit), &sv::Unit::id);
    if (unit == hir_.units().end()) {
        return std::nullopt;
    }
    Container container;
    container.processes = unit->processes;
    container.concurrent = unit->concurrent_statements;
    container.declarations = unit->declarations;
    container.generates = &unit->generates;
    for (const auto& [step, name] : scope.steps) {
        switch (step) {
        case VpiHirScope::Step::generate: {
            if (container.generates == nullptr) {
                return std::nullopt;
            }
            const auto wanted = base_name(name);
            const sv::GenerateRegion* region = nullptr;
            // A generate block's region, or the region of the construct
            // whose branch it is.
            const std::function<void(
                const std::vector<sv::GenerateRegion>&)>
                search = [&](const std::vector<sv::GenerateRegion>& regions) {
                    for (const auto& candidate : regions) {
                        if (region != nullptr) {
                            return;
                        }
                        if (candidate.label == wanted
                            || candidate.alternative_label == wanted) {
                            region = &candidate;
                            return;
                        }
                        if (candidate.label.empty()) {
                            search(candidate.nested);
                        }
                    }
                };
            search(*container.generates);
            if (region == nullptr) {
                return std::nullopt;
            }
            container = Container { };
            container.processes = region->processes;
            container.concurrent = region->concurrent_statements;
            container.declarations = region->declarations;
            container.generates = &region->nested;
            break;
        }
        case VpiHirScope::Step::callable: {
            const sv::Declaration* callable = nullptr;
            for (const auto id : container.declarations) {
                const auto* const declaration = hir_.find_declaration(id);
                if (declaration != nullptr && declaration->name == name
                    && (declaration->form == sv::DeclarationForm::task
                        || declaration->form
                            == sv::DeclarationForm::function)) {
                    callable = declaration;
                    break;
                }
            }
            if (callable == nullptr) {
                return std::nullopt;
            }
            container = Container { };
            container.statements = callable->statements;
            container.callable = callable->id;
            break;
        }
        case VpiHirScope::Step::block: {
            const sv::Statement* block = find_block(container.statements, name);
            for (const auto id : container.processes) {
                if (block != nullptr) {
                    break;
                }
                if (const auto* const record = process(id.value())) {
                    block = find_block(record->statements, name);
                }
            }
            if (block == nullptr) {
                return std::nullopt;
            }
            container = Container { };
            container.statements = block->statements;
            break;
        }
        }
    }
    return container;
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiStatementModel::iterate_scope(
    const VpiHirScope& scope, const PLI_INT32 type)
{
    if (type != vpiProcess && type != vpiContAssign && type != vpiStmt) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container) {
        return std::nullopt;
    }
    std::vector<fsim_vpi_handle_v1> found;
    if (type == vpiProcess) {
        for (const auto id : container->processes) {
            const auto* const record = process(id.value());
            // Declaration initializers run as frontend-made processes
            // named with a '$'; they are not processes of the design.
            if (record != nullptr && !record->concurrent_assertion
                && !record->name.starts_with('$')) {
                found.push_back(node(Kind::process, id.value(), 0U,
                    scope.handle, scope.handle));
            }
        }
    } else if (type == vpiContAssign) {
        for (const auto id : container->concurrent) {
            const auto* const record = statement(id.value());
            if (record != nullptr
                && record->kind == sv::StatementKind::assignment
                && record->assignment_kind
                    == sv::AssignmentKind::continuous) {
                found.push_back(node(Kind::cont_assign, id.value(), 0U,
                    scope.handle, scope.handle));
            }
        }
    } else {
        for (const auto id : container->statements) {
            if (const auto handle
                = statement_handle(id, scope.handle, scope.handle)) {
                found.push_back(handle);
            }
        }
    }
    return found;
}

std::optional<fsim_vpi_handle_v1> VpiStatementModel::scope_relation(
    const VpiHirScope& scope, const PLI_INT32 type)
{
    if (type != vpiStmt || scope.steps.empty()
        || scope.steps.back().first != VpiHirScope::Step::callable) {
        return std::nullopt;
    }
    const auto container = locate(scope);
    if (!container || !container->callable) {
        return fsim_vpi_handle_v1 { };
    }
    return sequence(container->statements, container->callable->value(),
        body_callable, scope.handle, scope.handle);
}

std::vector<VpiStatementModel::ForceSite> VpiStatementModel::force_sites(
    const VpiHirScope& scope, const bool release,
    const std::function<bool(fsim_vpi_handle_v1)>& targets)
{
    std::vector<ForceSite> found;
    const auto container = locate(scope);
    if (!container) {
        return found;
    }
    // The object an assignment target names: a select's selected object.
    const auto target_object = [&](std::optional<semantic::ExpressionId> id,
                                   const fsim_vpi_handle_v1 within) {
        for (std::size_t depth = 0U; id && depth < 64U; ++depth) {
            const auto* const record = expression(id->value());
            if (record == nullptr) {
                return fsim_vpi_handle_v1 { };
            }
            if (record->kind == sv::ExpressionKind::name) {
                return hooks_.resolve(within,
                    record->referenced_name ? record->referenced_name->spelling
                                            : record->text);
            }
            if ((record->kind != sv::ExpressionKind::index
                    && record->kind != sv::ExpressionKind::slice)
                || record->operands.empty()) {
                return fsim_vpi_handle_v1 { };
            }
            id = record->operands.front();
        }
        return fsim_vpi_handle_v1 { };
    };
    std::uint32_t owner { };
    const std::function<void(const std::vector<semantic::StatementId>&,
        fsim_vpi_handle_v1, fsim_vpi_handle_v1)>
        walk = [&](const std::vector<semantic::StatementId>& statements,
                   const fsim_vpi_handle_v1 within,
                   const fsim_vpi_handle_v1 parent) {
            for (const auto id : statements) {
                const auto* const record = statement(id.value());
                if (record == nullptr) {
                    continue;
                }
                using Statement = sv::StatementKind;
                const auto reason = record->kind == Statement::force ? cbForce
                    : record->kind == Statement::procedural_assign ? cbAssign
                    : record->kind == Statement::release           ? cbRelease
                    : record->kind == Statement::deassign          ? cbDeassign
                                                                   : 0;
                const auto releases = reason == cbRelease || reason == cbDeassign;
                if (reason != 0 && releases == release
                    && targets(target_object(record->target, within))) {
                    found.push_back(ForceSite {
                        statement_handle(id, within, parent), reason, owner });
                    continue;
                }
                auto inner = within;
                if ((record->kind == Statement::block
                        || record->kind == Statement::fork)
                    && !record->label.empty()) {
                    if (const auto named
                        = hooks_.child_scope(within, record->label)) {
                        inner = named;
                    }
                }
                const auto self = statement_handle(id, within, parent);
                walk(record->statements, inner, self);
                walk(record->else_statements, inner, self);
                for (const auto& alternative : record->case_alternatives) {
                    walk(alternative.statements, inner, self);
                }
            }
        };
    for (const auto id : container->processes) {
        const auto* const record = process(id.value());
        if (record == nullptr || record->name.starts_with('$')) {
            continue;
        }
        owner = id.value();
        walk(record->statements, scope.handle,
            node(Kind::process, id.value(), 0U, scope.handle, scope.handle));
    }
    owner = 0U;
    for (const auto id : container->declarations) {
        const auto* const declaration = hir_.find_declaration(id);
        if (declaration == nullptr
            || (declaration->form != sv::DeclarationForm::task
                && declaration->form != sv::DeclarationForm::function)) {
            continue;
        }
        const auto callable = hooks_.child_scope(scope.handle, declaration->name);
        if (callable != 0U) {
            walk(declaration->statements, callable, callable);
        }
    }
    return found;
}

fsim_vpi_handle_v1 VpiStatementModel::sequence(
    const std::vector<semantic::StatementId>& statements,
    const std::uint32_t id, const std::uint32_t owner,
    const fsim_vpi_handle_v1 scope, const fsim_vpi_handle_v1 parent,
    const bool of_process)
{
    if (statements.empty()) {
        return 0U;
    }
    if (statements.size() == 1U) {
        return statement_handle(statements.front(), scope, parent);
    }
    return node(Kind::body, id, owner, scope, parent, of_process);
}

const std::vector<semantic::StatementId>* VpiStatementModel::body_statements(
    const Node& entry) const
{
    if (entry.aux == body_process) {
        const auto* const record = process(entry.id);
        return record == nullptr ? nullptr : &record->statements;
    }
    if (entry.aux == body_callable) {
        const auto* const record = hir_.find_declaration(
            semantic::DeclarationId::from_index(entry.id));
        return record == nullptr ? nullptr : &record->statements;
    }
    const auto* const record = statement(entry.id);
    if (record == nullptr) {
        return nullptr;
    }
    if (entry.aux == body_then) {
        return &record->statements;
    }
    if (entry.aux == body_else) {
        return &record->else_statements;
    }
    const auto alternative = entry.aux - body_case;
    return alternative < record->case_alternatives.size()
        ? &record->case_alternatives[alternative].statements
        : nullptr;
}

// The format string a display task was given, rebuilt from its parsed
// form (IEEE 1800-2023 21.2.1); none when it had only values.
std::optional<std::string> VpiStatementModel::format_text(
    const sv::Statement& record) const
{
    if (!system_task_name(record) || record.kind == sv::StatementKind::task_call) {
        return std::nullopt;
    }
    std::string text;
    const auto conversion = [&](const sv::OutputFormat format,
                                const bool left, const bool zero,
                                const bool suppress, const std::uint32_t width,
                                const std::uint32_t precision) {
        text += '%';
        if (left) {
            text += '-';
        }
        if (zero) {
            text += '0';
        }
        if (suppress) {
            text += '0';
        } else if (width != 0U) {
            text += std::to_string(width);
        }
        if (precision != std::numeric_limits<std::uint32_t>::max()) {
            text += '.' + std::to_string(precision);
        }
        static constexpr std::string_view letters = "bhodcsefgmtuzpvl";
        const auto index = static_cast<std::size_t>(format);
        text += index < letters.size() ? letters[index] : 'd';
    };
    bool formatted = !record.output_text.empty()
        || !record.output_trailing_text.empty();
    // One conversion of one value is kept on the statement itself.
    if (record.output_values.empty() && record.output_format) {
        text += record.output_prefix;
        conversion(*record.output_format, record.output_left_justify,
            record.output_zero_pad, record.output_suppress_leading_zero,
            record.output_minimum_width, record.output_precision);
        text += record.output_suffix;
        formatted = true;
    }
    for (const auto& output : record.output_values) {
        text += output.prefix;
        formatted = formatted || !output.prefix.empty()
            || output.suppress_leading_zero || output.minimum_width != 0U
            || output.left_justify || output.zero_pad
            || output.format != sv::OutputFormat::decimal;
        conversion(output.format, output.left_justify, output.zero_pad,
            output.suppress_leading_zero, output.minimum_width,
            output.precision);
    }
    text += record.output_text;
    text += record.output_trailing_text;
    if (!formatted) {
        return std::nullopt;
    }
    return text;
}

PLI_INT32 VpiStatementModel::statement_type(const sv::Statement& record) const
{
    using enum sv::StatementKind;
    switch (record.kind) {
    case assignment:
        return vpiAssignment;
    case force:
        return vpiForce;
    case release:
        return vpiRelease;
    case conditional:
        return record.else_statements.empty() ? vpiIf : vpiIfElse;
    case selection:
        return vpiCase;
    case loop:
        if (record.loop_repeat) {
            return vpiRepeat;
        }
        if (record.loop_post_test) {
            return vpiDoWhile;
        }
        if (!record.loop_variable.empty() || record.loop_initial) {
            return vpiFor;
        }
        if (const auto* const condition = record.condition
                ? expression(record.condition->value())
                : nullptr;
            condition == nullptr
            || (condition->kind == sv::ExpressionKind::integer_literal
                && condition->text == "1")) {
            return vpiForever;
        }
        return vpiWhile;
    case break_loop:
        return vpiBreak;
    case continue_loop:
        return vpiContinue;
    case return_statement:
        return vpiReturn;
    case task_call:
        return record.task.spelling.starts_with('$') ? vpiSysTaskCall
                                                     : vpiTaskCall;
    case assertion:
        return vpiImmediateAssert;
    case delay_control:
        return vpiDelayControl;
    case event_control:
        return vpiEventControl;
    case wait_statement:
        return vpiWait;
    case event_trigger:
        return vpiEventStmt;
    case fork:
        return record.label.empty() ? vpiFork : vpiNamedFork;
    case block:
        return record.label.empty() ? vpiBegin : vpiNamedBegin;
    case wait_fork:
        return vpiWaitFork;
    case disable_fork:
        return vpiDisableFork;
    case disable:
        return vpiDisable;
    case display:
    case file_close:
    case file_flush:
    case file_display:
    case memory_transfer:
    case monitor_control:
    case pause:
    case finish:
    case exit_program:
        return vpiSysTaskCall;
    case container_method:
        return vpiMethodTaskCall;
    case procedural_assign:
        return vpiAssignStmt;
    case deassign:
        return vpiDeassign;
    case wait_order:
        return vpiOrderedWait;
    case null_statement:
    case report:
        return vpiNullStmt;
    }
    return vpiNullStmt;
}

// The system task a folded statement calls (IEEE 1800-2023 21.2, 21.3,
// 20.2, 20.6); the frontend keeps its form, not its name.
std::optional<std::string> VpiStatementModel::system_task_name(
    const sv::Statement& record) const
{
    using enum sv::StatementKind;
    const auto display_name = [&](const std::string_view prefix) {
        const std::string_view verb = record.output_monitor ? "monitor"
            : record.output_postponed                      ? "strobe"
            : record.output_newline                        ? "display"
                                                           : "write";
        return std::string { "$" } + std::string { prefix }
            + std::string { verb };
    };
    switch (record.kind) {
    case task_call:
        return record.task.spelling;
    case display:
        return display_name("");
    case file_display:
        return display_name("f");
    case file_close:
        return std::string { "$fclose" };
    case file_flush:
        return std::string { "$fflush" };
    case memory_transfer:
        return std::string { record.memory_write ? "$writemem" : "$readmem" }
            + (record.memory_hex ? "h" : "b");
    case monitor_control:
        return std::string { record.monitor_enabled ? "$monitoron"
                                                    : "$monitoroff" };
    case pause:
        return std::string { "$stop" };
    case finish:
        return std::string { "$finish" };
    case exit_program:
        return std::string { "$exit" };
    default:
        return std::nullopt;
    }
}

PLI_INT32 VpiStatementModel::expression_type(
    const sv::Expression& record) const
{
    using enum sv::ExpressionKind;
    switch (record.kind) {
    case integer_literal:
    case boolean_literal:
    case logic_literal:
    case string_literal:
    case class_null:
        return vpiConstant;
    case call:
        if (sv::systemverilog_cast_value_operand(record)) {
            return vpiOperation;
        }
        return record.text.starts_with('$') ? vpiSysFuncCall : vpiFuncCall;
    case class_method_call:
    case class_static_method_call:
        return vpiMethodFuncCall;
    case index:
        return vpiBitSelect;
    case slice:
        return record.text == "+:" || record.text == "-:"
            ? vpiIndexedPartSelect
            : vpiPartSelect;
    default:
        return vpiOperation;
    }
}

PLI_INT32 VpiStatementModel::type(const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    switch (entry.kind) {
    case Kind::process: {
        const auto* const record = process(entry.id);
        if (record == nullptr) {
            return vpiAlways;
        }
        return record->kind == sv::ProcessKind::initial ? vpiInitial
            : record->kind == sv::ProcessKind::final   ? vpiFinal
                                                       : vpiAlways;
    }
    case Kind::process_control:
        return vpiEventControl;
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        return record == nullptr ? vpiNullStmt : statement_type(*record);
    }
    case Kind::assignment_control: {
        const auto* const record = statement(entry.id);
        return record != nullptr
                && record->assignment_control == sv::AssignmentControl::delay
            ? vpiDelayControl
            : vpiEventControl;
    }
    case Kind::case_item:
        return vpiCaseItem;
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        return record == nullptr ? vpiOperation : expression_type(*record);
    }
    case Kind::sensitivity_list:
    case Kind::sensitivity:
        return vpiOperation;
    case Kind::delay:
        return vpiConstant;
    case Kind::cont_assign:
        return vpiContAssign;
    case Kind::reference:
        return vpiRefObj;
    case Kind::body:
        return vpiBegin;
    case Kind::loop_init:
    case Kind::loop_step:
        return vpiAssignment;
    case Kind::format:
        return vpiConstant;
    }
    return vpiOperation;
}

std::optional<semantic::SourceSpanId> VpiStatementModel::source(
    const Node& entry) const
{
    switch (entry.kind) {
    case Kind::process:
    case Kind::process_control:
        if (const auto* const record = process(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::body: {
        const auto* const list = body_statements(entry);
        if (list == nullptr || list->empty()) {
            return std::nullopt;
        }
        if (const auto* const record = statement(list->front().value())) {
            return record->source;
        }
        return std::nullopt;
    }
    case Kind::statement:
    case Kind::assignment_control:
    case Kind::cont_assign:
    case Kind::delay:
    case Kind::loop_init:
    case Kind::loop_step:
    case Kind::format:
        if (const auto* const record = statement(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::case_item:
        if (const auto* const record = statement(entry.id);
            record != nullptr && entry.aux < record->case_alternatives.size()) {
            return record->case_alternatives[entry.aux].source;
        }
        return std::nullopt;
    case Kind::expression:
    case Kind::reference:
        if (const auto* const record = expression(entry.id)) {
            return record->source;
        }
        return std::nullopt;
    case Kind::sensitivity_list:
    case Kind::sensitivity: {
        const auto* const list = entry.of_process
            ? (process(entry.id) != nullptr ? &process(entry.id)->sensitivities
                                            : nullptr)
            : (statement(entry.id) != nullptr
                    ? &statement(entry.id)->sensitivities
                    : nullptr);
        if (list == nullptr || list->empty()) {
            return std::nullopt;
        }
        return (*list)[entry.kind == Kind::sensitivity
                && entry.aux < list->size()
            ? entry.aux
            : 0U]
            .source;
    }
    }
    return std::nullopt;
}

std::optional<std::int64_t> VpiStatementModel::property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = nodes_.at(serial);
    if (property == vpiLineNo || property == vpiColumn) {
        const auto span = source(entry);
        if (!span || !span->valid()
            || span->value() >= semantics_.source_spans().size()) {
            return std::nullopt;
        }
        const auto& location = semantics_.source_spans().at(span->value());
        return property == vpiLineNo
            ? static_cast<std::int64_t>(location.begin.line)
            : static_cast<std::int64_t>(location.begin.column);
    }
    switch (entry.kind) {
    case Kind::process:
        if (property == vpiAlwaysType) {
            const auto* const record = process(entry.id);
            if (record == nullptr || record->kind == sv::ProcessKind::initial
                || record->kind == sv::ProcessKind::final) {
                return std::nullopt;
            }
            return record->kind == sv::ProcessKind::always_comb ? vpiAlwaysComb
                : record->kind == sv::ProcessKind::always_ff    ? vpiAlwaysFF
                : record->kind == sv::ProcessKind::always_latch
                ? vpiAlwaysLatch
                : vpiAlways;
        }
        return std::nullopt;
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        switch (property) {
        case vpiBlocking:
            if (record->kind != sv::StatementKind::assignment) {
                return std::nullopt;
            }
            return record->assignment_kind == sv::AssignmentKind::nonblocking
                ? 0
                : 1;
        case vpiOpType:
            if (record->kind != sv::StatementKind::assignment) {
                return std::nullopt;
            }
            if (!record->update_operator.empty()) {
                auto spelling = std::string_view { record->update_operator };
                if (spelling.ends_with('=')) {
                    spelling.remove_suffix(1U);
                }
                if (const auto type = operator_type(spelling, 2U)) {
                    return *type;
                }
            }
            return vpiAssignmentOp;
        case vpiCaseType:
            if (record->kind != sv::StatementKind::selection) {
                return std::nullopt;
            }
            return record->case_match == sv::CaseMatchKind::wildcard_z
                ? vpiCaseZ
                : record->case_match == sv::CaseMatchKind::wildcard_xz
                ? vpiCaseX
                : vpiCaseExact;
        case vpiQualifier:
            if (record->kind != sv::StatementKind::selection
                && record->kind != sv::StatementKind::conditional) {
                return std::nullopt;
            }
            return record->case_qualifier == sv::CaseQualifier::priority
                ? vpiPriorityQualifier
                : record->case_qualifier == sv::CaseQualifier::none
                ? vpiNoQualifier
                : vpiUniqueQualifier;
        case vpiJoinType:
            if (record->kind != sv::StatementKind::fork) {
                return std::nullopt;
            }
            return record->fork_join == sv::ForkJoinKind::any ? vpiJoinAny
                : record->fork_join == sv::ForkJoinKind::none ? vpiJoinNone
                                                              : vpiJoin;
        default:
            return std::nullopt;
        }
    }
    case Kind::cont_assign: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        // IEEE 1800-2023 37.46 vpiStrength0/1: the 38.34 strength
        // constants.
        static constexpr PLI_INT32 strengths[] = { vpiHighZ, vpiSmallCharge,
            vpiMediumCharge, vpiWeakDrive, vpiLargeCharge, vpiPullDrive,
            vpiStrongDrive, vpiSupplyDrive };
        if (property == vpiStrength0 || property == vpiStrength1) {
            const auto& level = property == vpiStrength0 ? record->drive_zero
                                                         : record->drive_one;
            return level && *level < std::size(strengths)
                ? strengths[*level]
                : vpiStrongDrive;
        }
        if (property == vpiNetDeclAssign) {
            return 0;
        }
        return std::nullopt;
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto type = expression_type(*record);
        if (property == vpiOpType && type == vpiOperation) {
            using enum sv::ExpressionKind;
            switch (record->kind) {
            case concatenation:
                return vpiConcatOp;
            case replication:
                return vpiMultiConcatOp;
            case assignment_pattern:
                return vpiAssignmentPatternOp;
            case update:
                return record->text == "pre++" ? vpiPreIncOp
                    : record->text == "post++"     ? vpiPostIncOp
                    : record->text == "pre--"      ? vpiPreDecOp
                                                   : vpiPostDecOp;
            case call:
                return vpiCastOp;
            default:
                if (const auto op
                    = operator_type(record->text, record->operands.size())) {
                    return *op;
                }
                return vpiNullOp;
            }
        }
        if (property == vpiConstType && type == vpiConstant) {
            if (record->kind == sv::ExpressionKind::string_literal) {
                return vpiStringConst;
            }
            if (record->kind == sv::ExpressionKind::class_null) {
                return vpiNullConst;
            }
            if (record->decimal_literal) {
                return record->decimal_literal->kind
                        == sv::DecimalLiteralKind::time
                    ? vpiTimeConst
                    : vpiRealConst;
            }
            const auto quote = record->text.find('\'');
            if (quote == std::string::npos) {
                return vpiIntConst;
            }
            auto rest = std::string_view { record->text }.substr(quote + 1U);
            if (!rest.empty() && (rest.front() == 's' || rest.front() == 'S')) {
                rest.remove_prefix(1U);
            }
            const auto base = rest.empty() ? 'd'
                                           : static_cast<char>(std::tolower(
                                               static_cast<unsigned char>(
                                                   rest.front())));
            return base == 'b' ? vpiBinaryConst
                : base == 'o'  ? vpiOctConst
                : base == 'h'  ? vpiHexConst
                : base == 'd'  ? vpiDecConst
                               : vpiBinaryConst;
        }
        if (property == vpiSize && type == vpiConstant) {
            if (const auto constant = value(serial);
                constant && constant->kind == VpiStatementValue::Kind::bits) {
                return static_cast<std::int64_t>(constant->bits.width());
            }
            if (record->kind == sv::ExpressionKind::string_literal) {
                return static_cast<std::int64_t>(
                    8U * record->decoded_string.value_or(record->text).size());
            }
            return std::nullopt;
        }
        if (property == vpiIndexedPartSelectType
            && type == vpiIndexedPartSelect) {
            return record->text == "+:" ? vpiPosIndexed : vpiNegIndexed;
        }
        if (property == vpiConstantSelect
            && (type == vpiBitSelect || type == vpiPartSelect
                || type == vpiIndexedPartSelect)) {
            for (std::size_t index = 1U; index < record->operands.size();
                 ++index) {
                const auto* const operand
                    = expression(record->operands[index].value());
                if (operand == nullptr
                    || operand->kind != sv::ExpressionKind::integer_literal) {
                    return 0;
                }
            }
            return 1;
        }
        return std::nullopt;
    }
    case Kind::sensitivity_list:
        if (property == vpiOpType) {
            return vpiEventOrOp;
        }
        return std::nullopt;
    case Kind::sensitivity: {
        if (property != vpiOpType) {
            return std::nullopt;
        }
        const auto* const list = entry.of_process
            ? &process(entry.id)->sensitivities
            : &statement(entry.id)->sensitivities;
        const auto edge = entry.aux < list->size() ? (*list)[entry.aux].edge
                                                   : sv::EdgeKind::any;
        return edge == sv::EdgeKind::positive ? vpiPosedgeOp : vpiNegedgeOp;
    }
    case Kind::loop_init:
    case Kind::loop_step:
        if (property == vpiBlocking) {
            return 1;
        }
        if (property == vpiOpType) {
            return vpiAssignmentOp;
        }
        return std::nullopt;
    case Kind::format:
        if (property == vpiConstType) {
            return vpiStringConst;
        }
        if (property == vpiSize) {
            const auto* const record = statement(entry.id);
            const auto text = record ? format_text(*record) : std::nullopt;
            return text ? static_cast<std::int64_t>(8U * text->size())
                        : std::int64_t { };
        }
        return std::nullopt;
    case Kind::delay:
        if (property == vpiConstType) {
            return vpiDecConst;
        }
        if (property == vpiSize) {
            return 64;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<std::string> VpiStatementModel::string_property(
    const std::uint64_t serial, const PLI_INT32 property) const
{
    const auto& entry = nodes_.at(serial);
    if (property == vpiFile) {
        const auto span = source(entry);
        if (!span || !span->valid()
            || span->value() >= semantics_.source_spans().size()) {
            return std::nullopt;
        }
        const auto& location = semantics_.source_spans().at(span->value());
        if (!location.logical_name.empty()) {
            return location.logical_name;
        }
        if (location.file.valid()
            && location.file.value() < semantics_.source_files().size()) {
            return semantics_.source_files().at(location.file.value())
                .physical_name;
        }
        return std::nullopt;
    }
    switch (entry.kind) {
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        if (property == vpiName) {
            if (const auto name = system_task_name(*record)) {
                return name;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto type = expression_type(*record);
        if (property == vpiName
            && (type == vpiSysFuncCall || type == vpiFuncCall
                || type == vpiMethodFuncCall)) {
            return record->text;
        }
        if (property == vpiDecompile && type == vpiConstant) {
            return record->kind == sv::ExpressionKind::string_literal
                ? "\"" + record->decoded_string.value_or(record->text) + "\""
                : record->text;
        }
        return std::nullopt;
    }
    case Kind::reference: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return std::nullopt;
        }
        const auto& name = record->referenced_name
            ? record->referenced_name->spelling
            : record->text;
        if (property == vpiName) {
            return name;
        }
        if (property == vpiFullName) {
            return hooks_.full_name(entry.scope) + "." + name;
        }
        return std::nullopt;
    }
    case Kind::format:
        if (property == vpiDecompile) {
            if (const auto* const record = statement(entry.id)) {
                if (const auto text = format_text(*record)) {
                    return "\"" + *text + "\"";
                }
            }
        }
        return std::nullopt;
    case Kind::delay:
        if (property == vpiDecompile) {
            if (const auto* const record = statement(entry.id);
                record != nullptr && record->delay) {
                const auto& primary = record->delay->primary;
                return std::to_string(primary.magnitude)
                    + (primary.divisor > 1U
                            ? "/" + std::to_string(primary.divisor)
                            : std::string { })
                    + primary.unit;
            }
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<fsim_vpi_handle_v1> VpiStatementModel::relation(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = nodes_.at(serial);
    const auto self = hooks_.handle(serial);
    if (type == vpiScope) {
        return entry.scope;
    }
    if (type == vpiParent) {
        return entry.parent;
    }
    if (type == vpiModule) {
        return std::nullopt;
    }
    const auto expression_at = [&](const std::optional<semantic::ExpressionId>
                                       id) -> fsim_vpi_handle_v1 {
        return id ? expression_handle(*id, entry.scope, self) : 0U;
    };
    switch (entry.kind) {
    case Kind::process: {
        const auto* const record = process(entry.id);
        if (record == nullptr || type != vpiStmt) {
            return fsim_vpi_handle_v1 { };
        }
        // `always @(...) stmt`: the event control is the process's
        // statement (IEEE 1800-2023 37.44).
        if (!record->sensitivities.empty()
            && (record->kind == sv::ProcessKind::always
                || record->kind == sv::ProcessKind::always_ff
                || record->kind == sv::ProcessKind::always_latch)) {
            return node(Kind::process_control, entry.id, 0U, entry.scope, self,
                true);
        }
        return sequence(record->statements, entry.id, body_process,
            entry.scope, self, true);
    }
    case Kind::process_control: {
        const auto* const record = process(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        if (type == vpiStmt) {
            return sequence(record->statements, entry.id, body_process,
                entry.scope, self, true);
        }
        if (type == vpiCondition) {
            return record->sensitivities.size() == 1U
                ? node(Kind::sensitivity, entry.id, 0U, entry.scope, self, true)
                : node(Kind::sensitivity_list, entry.id, 0U, entry.scope, self,
                    true);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::statement:
    case Kind::cont_assign: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        using enum sv::StatementKind;
        // Only assignments have sides (IEEE 1800-2023 37.45-37.47); a
        // loop keeps its step there.
        const auto assigns = record->kind == assignment
            || record->kind == force || record->kind == procedural_assign;
        switch (type) {
        case vpiLhs:
            return assigns || record->kind == release || record->kind == deassign
                ? expression_at(record->target)
                : 0U;
        case vpiRhs:
            return assigns ? expression_at(record->value) : 0U;
        case vpiForInitStmt:
            return statement_type(*record) == vpiFor && record->loop_initial
                ? node(Kind::loop_init, entry.id, 0U, entry.scope, self)
                : 0U;
        case vpiForIncStmt:
            if (statement_type(*record) != vpiFor) {
                return fsim_vpi_handle_v1 { };
            }
            if (!record->loop_updates.empty()) {
                return statement_handle(
                    record->loop_updates.front(), entry.scope, self);
            }
            return record->target && record->value
                ? node(Kind::loop_step, entry.id, 0U, entry.scope, self)
                : 0U;
        case vpiCondition:
            if (record->kind == event_control) {
                if (record->sensitivities.empty()) {
                    return fsim_vpi_handle_v1 { };
                }
                return record->sensitivities.size() == 1U
                    ? node(Kind::sensitivity, entry.id, 0U, entry.scope, self)
                    : node(Kind::sensitivity_list, entry.id, 0U, entry.scope,
                        self);
            }
            if (record->kind == return_statement) {
                return expression_at(record->value);
            }
            // A repeat loop's condition is its count (IEEE 1800-2023
            // 37.75).
            if (record->kind == loop && record->loop_repeat) {
                return expression_at(record->loop_limit);
            }
            return expression_at(record->condition);
        case vpiExpr:
            if (record->kind == assertion) {
                return expression_at(record->condition);
            }
            if (record->kind == disable) {
                return hooks_.resolve(entry.scope, record->task.spelling);
            }
            return fsim_vpi_handle_v1 { };
        case vpiStmt:
            return sequence(
                record->statements, entry.id, body_then, entry.scope, self);
        case vpiElseStmt:
            return sequence(record->else_statements, entry.id, body_else,
                entry.scope, self);
        case vpiDelay:
            if (record->kind == delay_control || entry.kind == Kind::cont_assign) {
                if (!record->delay) {
                    return fsim_vpi_handle_v1 { };
                }
                if (record->delay->primary.expression) {
                    return expression_at(record->delay->primary.expression);
                }
                return node(Kind::delay, entry.id, 0U, entry.scope, self);
            }
            return fsim_vpi_handle_v1 { };
        case vpiDelayControl:
        case vpiEventControl:
            if (record->kind == assignment
                && record->assignment_control
                    == (type == vpiDelayControl ? sv::AssignmentControl::delay
                                                : sv::AssignmentControl::event)) {
                return node(Kind::assignment_control, entry.id, 0U, entry.scope,
                    self);
            }
            return fsim_vpi_handle_v1 { };
        case vpiNamedEvent:
            return record->kind == event_trigger ? expression_at(record->target)
                                                 : 0U;
        case vpiTask:
            if (record->kind == task_call
                && !record->task.spelling.starts_with('$')) {
                return hooks_.resolve(entry.scope, record->task.spelling);
            }
            return fsim_vpi_handle_v1 { };
        default:
            return fsim_vpi_handle_v1 { };
        }
    }
    case Kind::assignment_control: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        if (type == vpiDelay && record->delay) {
            if (record->delay->primary.expression) {
                return expression_at(record->delay->primary.expression);
            }
            return node(Kind::delay, entry.id, 0U, entry.scope, self);
        }
        if (type == vpiCondition && !record->sensitivities.empty()) {
            return record->sensitivities.size() == 1U
                ? node(Kind::sensitivity, entry.id, 0U, entry.scope, self)
                : node(Kind::sensitivity_list, entry.id, 0U, entry.scope, self);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::case_item: {
        const auto* const record = statement(entry.id);
        if (record == nullptr || entry.aux >= record->case_alternatives.size()
            || type != vpiStmt) {
            return fsim_vpi_handle_v1 { };
        }
        return sequence(record->case_alternatives[entry.aux].statements,
            entry.id, body_case + entry.aux, entry.scope, self);
    }
    case Kind::loop_init:
    case Kind::loop_step: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        if (type == vpiLhs) {
            const auto target = entry.kind == Kind::loop_init
                ? record->loop_update_target.has_value()
                    ? record->loop_update_target
                    : record->target
                : record->target;
            return expression_at(target);
        }
        if (type == vpiRhs) {
            return expression_at(entry.kind == Kind::loop_init
                    ? record->loop_initial
                    : record->value);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return fsim_vpi_handle_v1 { };
        }
        const auto kind = expression_type(*record);
        const auto operand = [&](const std::size_t index) {
            return index < record->operands.size()
                ? expression_handle(record->operands[index], entry.scope, self)
                : 0U;
        };
        // A select's parent is the object selected (IEEE 1800-2023
        // 37.19, 37.20).
        if (kind == vpiBitSelect || kind == vpiPartSelect
            || kind == vpiIndexedPartSelect) {
            if (type == vpiParent) {
                return operand(0U);
            }
            if (kind == vpiBitSelect && type == vpiIndex) {
                return operand(1U);
            }
            if (kind == vpiPartSelect
                && (type == vpiLeftRange || type == vpiRightRange)) {
                return operand(type == vpiLeftRange ? 1U : 2U);
            }
            if (kind == vpiIndexedPartSelect
                && (type == vpiBaseExpr || type == vpiWidthExpr)) {
                return operand(type == vpiBaseExpr ? 1U : 2U);
            }
            return fsim_vpi_handle_v1 { };
        }
        if ((kind == vpiFuncCall || kind == vpiMethodFuncCall)
            && type == vpiFunction) {
            return hooks_.resolve(entry.scope, record->text);
        }
        return fsim_vpi_handle_v1 { };
    }
    case Kind::sensitivity: {
        const auto* const list = entry.of_process
            ? (process(entry.id) != nullptr ? &process(entry.id)->sensitivities
                                            : nullptr)
            : (statement(entry.id) != nullptr
                    ? &statement(entry.id)->sensitivities
                    : nullptr);
        if (list == nullptr || entry.aux >= list->size()) {
            return fsim_vpi_handle_v1 { };
        }
        return fsim_vpi_handle_v1 { };
    }
    default:
        return fsim_vpi_handle_v1 { };
    }
}

std::optional<std::vector<fsim_vpi_handle_v1>> VpiStatementModel::iterate(
    const std::uint64_t serial, const PLI_INT32 type)
{
    const auto entry = nodes_.at(serial);
    const auto self = hooks_.handle(serial);
    std::vector<fsim_vpi_handle_v1> found;
    const auto sensitivities = [&]() -> const std::vector<sv::Sensitivity>* {
        if (entry.of_process) {
            const auto* const record = process(entry.id);
            return record == nullptr ? nullptr : &record->sensitivities;
        }
        const auto* const record = statement(entry.id);
        return record == nullptr ? nullptr : &record->sensitivities;
    };
    // A sensitivity: its expression, or for an edge the vpiPosedgeOp or
    // vpiNegedgeOp operation over it.
    const auto sensitivity_handle = [&](const std::size_t index) {
        const auto* const list = sensitivities();
        if (list == nullptr || index >= list->size()) {
            return fsim_vpi_handle_v1 { };
        }
        const auto& item = (*list)[index];
        if (item.edge != sv::EdgeKind::any) {
            return node(Kind::sensitivity, entry.id,
                static_cast<std::uint32_t>(index), entry.scope, self,
                entry.of_process);
        }
        if (item.expression) {
            return expression_handle(*item.expression, entry.scope, self);
        }
        return hooks_.resolve(entry.scope, item.signal);
    };
    switch (entry.kind) {
    case Kind::statement: {
        const auto* const record = statement(entry.id);
        if (record == nullptr) {
            return found;
        }
        using enum sv::StatementKind;
        if ((type == vpiForInitStmt || type == vpiForIncStmt)
            && statement_type(*record) == vpiFor) {
            if (const auto handle = relation(serial, type).value_or(0U)) {
                found.push_back(handle);
            }
            if (type == vpiForIncStmt) {
                for (std::size_t index = 1U;
                     index < record->loop_updates.size(); ++index) {
                    if (const auto handle = statement_handle(
                            record->loop_updates[index], entry.scope, self)) {
                        found.push_back(handle);
                    }
                }
            }
        } else if (type == vpiStmt
            && (record->kind == block || record->kind == fork)) {
            for (const auto id : record->statements) {
                if (const auto handle = statement_handle(id, entry.scope, self)) {
                    found.push_back(handle);
                }
            }
        } else if (type == vpiCaseItem && record->kind == selection) {
            for (std::size_t index = 0U;
                 index < record->case_alternatives.size(); ++index) {
                found.push_back(node(Kind::case_item, entry.id,
                    static_cast<std::uint32_t>(index), entry.scope, self));
            }
        } else if (type == vpiArgument) {
            if (record->kind == task_call) {
                for (const auto& argument : record->task_arguments) {
                    if (argument.actual) {
                        found.push_back(expression_handle(
                            *argument.actual, entry.scope, self));
                    }
                }
            } else if (system_task_name(*record)) {
                if (record->file_handle) {
                    found.push_back(expression_handle(
                        *record->file_handle, entry.scope, self));
                }
                if (format_text(*record)) {
                    found.push_back(
                        node(Kind::format, entry.id, 0U, entry.scope, self));
                }
                for (const auto& output : record->output_values) {
                    if (output.value) {
                        found.push_back(expression_handle(
                            *output.value, entry.scope, self));
                    }
                }
                if (record->output_values.empty() && record->value) {
                    found.push_back(
                        expression_handle(*record->value, entry.scope, self));
                }
                if (record->target) {
                    found.push_back(expression_handle(
                        *record->target, entry.scope, self));
                }
            }
        }
        return found;
    }
    case Kind::body:
        if (type == vpiStmt) {
            if (const auto* const list = body_statements(entry)) {
                for (const auto id : *list) {
                    if (const auto handle
                        = statement_handle(id, entry.scope, self)) {
                        found.push_back(handle);
                    }
                }
            }
        }
        return found;
    case Kind::case_item: {
        const auto* const record = statement(entry.id);
        if (record != nullptr && entry.aux < record->case_alternatives.size()
            && type == vpiExpr) {
            for (const auto choice :
                record->case_alternatives[entry.aux].choices) {
                found.push_back(expression_handle(choice, entry.scope, self));
            }
        }
        return found;
    }
    case Kind::expression: {
        const auto* const record = expression(entry.id);
        if (record == nullptr) {
            return found;
        }
        const auto kind = expression_type(*record);
        if ((type == vpiOperand && kind == vpiOperation)
            || (type == vpiArgument
                && (kind == vpiSysFuncCall || kind == vpiFuncCall
                    || kind == vpiMethodFuncCall))) {
            auto operands = std::span { record->operands };
            // A cast's operand is its value (6.24).
            if (const auto value = sv::systemverilog_cast_value_operand(*record);
                value && kind == vpiOperation) {
                found.push_back(expression_handle(*value, entry.scope, self));
                return found;
            }
            for (const auto operand : operands) {
                found.push_back(expression_handle(operand, entry.scope, self));
            }
        }
        return found;
    }
    case Kind::sensitivity_list:
        if (type == vpiOperand) {
            const auto* const list = sensitivities();
            for (std::size_t index = 0U; list != nullptr && index < list->size();
                 ++index) {
                if (const auto handle = sensitivity_handle(index)) {
                    found.push_back(handle);
                }
            }
        }
        return found;
    case Kind::sensitivity:
        if (type == vpiOperand) {
            const auto* const list = sensitivities();
            if (list != nullptr && entry.aux < list->size()) {
                const auto& item = (*list)[entry.aux];
                const auto operand = item.expression
                    ? expression_handle(*item.expression, entry.scope, self)
                    : hooks_.resolve(entry.scope, item.signal);
                if (operand != 0U) {
                    found.push_back(operand);
                }
            }
        }
        return found;
    default:
        return found;
    }
}

std::optional<VpiStatementValue> VpiStatementModel::value(
    const std::uint64_t serial) const
{
    const auto& entry = nodes_.at(serial);
    if (entry.kind == Kind::delay) {
        const auto* const record = statement(entry.id);
        if (record == nullptr || !record->delay) {
            return std::nullopt;
        }
        VpiStatementValue result;
        const auto& primary = record->delay->primary;
        if (primary.divisor > 1U) {
            result.kind = VpiStatementValue::Kind::real;
            result.real = static_cast<double>(primary.magnitude)
                / static_cast<double>(primary.divisor);
            return result;
        }
        result.bits = PackedLogic4(64U, Logic4::zero);
        for (std::size_t bit = 0U; bit < 64U; ++bit) {
            result.bits.set(bit, ((primary.magnitude >> bit) & 1U) != 0U
                    ? Logic4::one
                    : Logic4::zero);
        }
        return result;
    }
    if (entry.kind == Kind::format) {
        const auto* const record = statement(entry.id);
        auto text = record ? format_text(*record) : std::nullopt;
        if (!text) {
            return std::nullopt;
        }
        VpiStatementValue result;
        result.kind = VpiStatementValue::Kind::string;
        result.text = std::move(*text);
        return result;
    }
    if (entry.kind != Kind::expression) {
        return std::nullopt;
    }
    const auto* const record = expression(entry.id);
    if (record == nullptr) {
        return std::nullopt;
    }
    using enum sv::ExpressionKind;
    switch (record->kind) {
    case string_literal: {
        VpiStatementValue result;
        result.kind = VpiStatementValue::Kind::string;
        result.text = record->decoded_string.value_or(record->text);
        return result;
    }
    case integer_literal:
    case boolean_literal:
    case logic_literal: {
        if (record->decimal_literal) {
            const auto& decimal = *record->decimal_literal;
            VpiStatementValue result;
            result.kind = VpiStatementValue::Kind::real;
            double digits { };
            const auto parsed = std::from_chars(decimal.digits.data(),
                decimal.digits.data() + decimal.digits.size(), digits);
            if (parsed.ec != std::errc { }) {
                return std::nullopt;
            }
            result.real = digits
                * std::pow(10.0, static_cast<double>(decimal.decimal_exponent));
            return result;
        }
        if (record->text == "true" || record->text == "false") {
            VpiStatementValue result;
            result.bits = PackedLogic4(1U,
                record->text == "true" ? Logic4::one : Logic4::zero);
            return result;
        }
        return integer_value(record->text);
    }
    default:
        return std::nullopt;
    }
}

} // namespace fsim::app::application_detail
