// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <cctype>
#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view vital_simple_name(const std::string_view name)
{
    const auto separator = name.find_last_of('.');
    return name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
}

std::string_view vital_name(const semantic::vhdl::Name& name)
{
    return name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
}

bool same_vital_identifier(
    const std::string_view left, const std::string_view right)
{
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char lhs, const char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs))
                   == std::tolower(static_cast<unsigned char>(rhs));
           });
}

std::string canonical_vital_identifier(const std::string_view name)
{
    auto result = std::string { name };
    std::ranges::transform(result, result.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
    return result;
}

bool is_vital_delay_name(const std::string_view name)
{
    return same_vital_identifier(name, "vitalsignaldelay")
        || same_vital_identifier(name, "vitalwiredelay")
        || same_vital_identifier(name, "vitalpathdelay")
        || same_vital_identifier(name, "vitalpathdelay01")
        || same_vital_identifier(name, "vitalpathdelay01z");
}

bool has_vital_timing_qualification(const std::string_view name)
{
    const auto member_separator = name.find_last_of('.');
    if (member_separator == std::string_view::npos) {
        return true;
    }
    const auto owner = name.substr(0U, member_separator);
    const auto package_separator = owner.find_last_of('.');
    if (package_separator == std::string_view::npos) {
        return same_vital_identifier(owner, "vital_timing");
    }
    return same_vital_identifier(
               owner.substr(package_separator + 1U), "vital_timing")
        && same_vital_identifier(
            owner.substr(0U, package_separator), "ieee");
}

std::optional<AssertionSeverity> vital_delay_severity(
    const semantic::vhdl::Expression& expression)
{
    if (expression.kind != semantic::vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    auto name = std::string_view { expression.text };
    constexpr std::string_view prefix { "@fsim-enum:" };
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }
    name = vital_simple_name(name);
    if (same_vital_identifier(name, "note")) {
        return AssertionSeverity::note;
    }
    if (same_vital_identifier(name, "warning")) {
        return AssertionSeverity::warning;
    }
    if (same_vital_identifier(name, "error")) {
        return AssertionSeverity::error;
    }
    if (same_vital_identifier(name, "failure")) {
        return AssertionSeverity::failure;
    }
    return std::nullopt;
}

std::optional<VitalGlitchMode> vital_glitch_mode(
    const semantic::vhdl::Expression& expression)
{
    if (expression.kind != semantic::vhdl::ExpressionKind::name) {
        return std::nullopt;
    }
    auto name = std::string_view { expression.text };
    constexpr std::string_view prefix { "@fsim-enum:" };
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }
    name = vital_simple_name(name);
    if (same_vital_identifier(name, "onevent")) {
        return VitalGlitchMode::on_event;
    }
    if (same_vital_identifier(name, "ondetect")) {
        return VitalGlitchMode::on_detect;
    }
    if (same_vital_identifier(name, "vitalinertial")) {
        return VitalGlitchMode::inertial;
    }
    if (same_vital_identifier(name, "vitaltransport")) {
        return VitalGlitchMode::transport;
    }
    return std::nullopt;
}

} // namespace

bool Lowerer::is_hir_vhdl_vital_delay_call(
    const semantic::StatementId statement_id) const
{
    if (specialized_hir_unit_ == nullptr) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr
        || statement->vhdl->kind
            != semantic::vhdl::StatementKind::procedure_call) {
        return false;
    }
    const auto& procedure = statement->vhdl->procedure;
    const auto qualified_name = vital_name(procedure);
    const auto simple_name = vital_simple_name(qualified_name);
    if (!is_vital_delay_name(simple_name)
        || !has_vital_timing_qualification(qualified_name)) {
        return false;
    }

    // Compiler-owned IEEE VITAL packages deliberately retain only their
    // exported declaration surface. A linked user declaration therefore
    // takes precedence over the intrinsic even when vital_timing is visible.
    if (procedure.selected || !procedure.overloads.empty()) {
        return false;
    }
    semantic::CompiledDesignResolver resolver { *specialized_hir_unit_ };
    return resolver.resolve_vhdl_callable_candidates(
                       procedure, statement->vhdl->scope)
               .candidates.empty()
        && resolver.vhdl_standard_package_member_visible(
            procedure, statement->vhdl->scope);
}

bool Lowerer::lower_hir_vhdl_vital_delay_call(
    const semantic::StatementId statement_id)
{
    if (!is_hir_vhdl_vital_delay_call(statement_id)) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto statement_span = hir_source_span(source.source);
    const auto name = vital_simple_name(vital_name(source.procedure));
    const bool signal_delay = same_vital_identifier(
        name, "vitalsignaldelay");
    const bool wire_delay = same_vital_identifier(name, "vitalwiredelay");
    const bool path_single = same_vital_identifier(name, "vitalpathdelay");
    const bool path_01 = same_vital_identifier(name, "vitalpathdelay01");
    const bool path_01z = same_vital_identifier(name, "vitalpathdelay01z");
    const bool path_delay = path_single || path_01 || path_01z;

    std::vector<semantic::ExpressionId> positional;
    std::map<std::string, semantic::ExpressionId, std::less<>> named;
    bool saw_named { };
    bool valid = true;
    for (const auto& association : source.procedure_arguments) {
        if (association.formal) {
            saw_named = true;
            const auto formal = canonical_vital_identifier(
                vital_simple_name(vital_name(*association.formal)));
            if (!named.emplace(formal, association.actual).second) {
                report(
                    "FSIM-ELAB-VITAL-017",
                    "duplicate VITAL delay actual for formal '"
                        + formal + "'",
                    hir_source_span(association.source));
                valid = false;
            }
        } else {
            if (saw_named) {
                report(
                    "FSIM-ELAB-VITAL-017",
                    "a positional VITAL delay actual cannot follow a named "
                    "actual",
                    hir_source_span(association.source));
                valid = false;
            }
            positional.push_back(association.actual);
        }
    }
    const auto formals = path_delay
        ? std::vector<std::string_view> {
              "outsignal", "glitchdata", "outsignalname", "outtemp",
              "paths", "defaultdelay", "mode", "xon", "msgon",
              "msgseverity", "outputmap", "negpreempton",
              "ignoredefaultdelay", "rejectfastpath" }
        : signal_delay
        ? std::vector<std::string_view> { "outsig", "insig", "dly" }
        : std::vector<std::string_view> { "outsig", "insig", "twire" };
    const auto maximum_position = path_single ? 12U : path_01 ? 13U : 14U;
    if (positional.size() > (path_delay ? maximum_position : 3U)) {
        report(
            "FSIM-ELAB-VITAL-017",
            std::string { name } + " has too many positional actuals",
            statement_span);
        valid = false;
    }
    for (const auto& [formal, expression_id] : named) {
        if (std::ranges::find(formals, std::string_view { formal })
                == formals.end()
            || (!path_01z && formal == "outputmap")
            || (path_single && formal == "rejectfastpath")) {
            const auto expression
                = specialized_hir_unit_->find_expression(expression_id);
            report(
                "FSIM-ELAB-VITAL-017",
                std::string { name } + " has no formal parameter '"
                    + formal + "'",
                expression && expression->vhdl != nullptr
                    ? hir_source_span(expression->vhdl->source)
                    : statement_span);
            valid = false;
        }
    }
    const auto actual = [&](const std::string_view formal,
                            const std::size_t position)
        -> std::optional<semantic::ExpressionId> {
        const auto selected = named.find(formal);
        if (selected != named.end()) {
            return selected->second;
        }
        return position < positional.size()
            ? std::optional { positional[position] }
            : std::nullopt;
    };
    const auto expression_span = [&](const semantic::ExpressionId id) {
        const auto expression = specialized_hir_unit_->find_expression(id);
        return expression && expression->vhdl != nullptr
            ? hir_source_span(expression->vhdl->source)
            : statement_span;
    };
    const auto static_bool = [&](const std::string_view formal,
                                 const std::size_t position,
                                 const bool fallback) {
        const auto expression = actual(formal, position);
        if (!expression) {
            return fallback;
        }
        const auto value = hir_constant_integer(*expression);
        if (!value || (*value != 0 && *value != 1)) {
            report(
                "FSIM-ELAB-VITAL-018",
                "VITAL delay formal '" + std::string { formal }
                    + "' requires a static Boolean value",
                expression_span(*expression));
            valid = false;
            return fallback;
        }
        return *value != 0;
    };
    const auto static_string = [&](const std::string_view formal,
                                   const std::size_t position,
                                   const std::string_view fallback) {
        const auto expression_id = actual(formal, position);
        if (!expression_id) {
            return std::string { fallback };
        }
        const auto expression = specialized_hir_unit_->find_expression(
            *expression_id);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::string_literal
            || !expression->vhdl->decoded_string) {
            report(
                "FSIM-ELAB-VITAL-018",
                "VITAL delay formal '" + std::string { formal }
                    + "' requires a static string literal",
                expression_span(*expression_id));
            valid = false;
            return std::string { fallback };
        }
        return *expression->vhdl->decoded_string;
    };
    const auto signal = [&](const std::string_view formal,
                            const std::size_t position)
        -> std::optional<SignalId> {
        const auto expression = actual(formal, position);
        auto binding = expression
            ? hir_direct_signal_binding(*expression)
            : std::nullopt;
        if (!binding && expression) {
            const auto declaration = hir_target_declaration(*expression);
            binding = declaration
                ? hir_runtime_binding(
                      *declaration, hir_process_scope_, false)
                : std::nullopt;
        }
        if (!binding && expression) {
            const auto source_expression
                = specialized_hir_unit_->find_expression(*expression);
            if (source_expression && source_expression->vhdl != nullptr
                && source_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name) {
                // VITAL procedures are compiler intrinsics rather than HIR
                // callables, so their formals do not create a callable
                // binding frame. Retain the lexical signal-name fallback
                // used by precompiled VITAL models when the reference ID is
                // absent from an intrinsic actual.
                const auto found = signals_.find(
                    source_expression->vhdl->text);
                if (found != signals_.end()
                    && found->second < design_.signal_info_.size()) {
                    const auto& info = design_.signal_info_[found->second];
                    HirRuntimeBinding direct;
                    direct.kind = HirRuntimeBindingKind::signal;
                    direct.name = info.name;
                    direct.width = info.width;
                    direct.domain = info.source_domain;
                    direct.signed_value = info.is_signed;
                    direct.integer_range = info.integer_range;
                    direct.signal = found->second;
                    binding = std::move(direct);
                }
            }
        }
        if (!binding || !binding->signal) {
            report(
                "FSIM-ELAB-VITAL-019",
                "VITAL delay formal '" + std::string { formal }
                    + "' is not a visible signal",
                expression ? expression_span(*expression) : statement_span);
            valid = false;
            return std::nullopt;
        }
        if (binding->width != 1U
            || binding->domain != frontend::ValueDomain::Logic9) {
            report(
                "FSIM-ELAB-VITAL-019",
                "VITAL delay formal '" + std::string { formal }
                    + "' requires a scalar standard-logic signal",
                expression_span(*expression));
            valid = false;
            return std::nullopt;
        }
        return binding->signal;
    };
    const auto zero_register = [&] {
        const auto result = allocate_register(
            64U, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(
            LoadConstant { result, unsigned_value(0U, 64U) });
        return result;
    };
    const auto contains_negative = [&](const auto& self,
                                       const semantic::ExpressionId id)
        -> bool {
        const auto expression = specialized_hir_unit_->find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return false;
        }
        const auto& value = *expression->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::unary
            && value.text == "-") {
            return true;
        }
        if (value.kind == semantic::vhdl::ExpressionKind::aggregate
            && std::ranges::any_of(value.operands,
                [&](const semantic::ExpressionId element) {
                    return self(self, element);
                })) {
            return true;
        }
        const auto evaluated = hir_constant_integer(id);
        return evaluated && *evaluated < 0;
    };
    const auto delay_set = [&](const std::optional<semantic::ExpressionId>
                                   expression,
                               const std::size_t count,
                               const std::string_view formal)
        -> std::optional<std::array<RegisterId, 6>> {
        std::array<RegisterId, 6> result { };
        const auto zero = zero_register();
        result.fill(zero);
        if (!expression) {
            return result;
        }
        if (contains_negative(contains_negative, *expression)) {
            report(
                "FSIM-ELAB-VITAL-020",
                "VITAL delay formal '" + std::string { formal }
                    + "' requires nonnegative delay values",
                expression_span(*expression));
            valid = false;
            return std::nullopt;
        }
        const auto delay_expression = specialized_hir_unit_->find_expression(
            *expression);
        if (delay_expression && delay_expression->vhdl != nullptr
            && delay_expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate) {
            if (delay_expression->vhdl->operands.size() != count
                || delay_expression->vhdl->associations.size() != count
                || std::ranges::any_of(
                    delay_expression->vhdl->associations,
                    [](const semantic::vhdl::AggregateAssociation& item) {
                        return !item.choices.empty();
                    })) {
                report(
                    "FSIM-ELAB-VITAL-020",
                    "VITAL delay formal '" + std::string { formal }
                        + "' requires exactly " + std::to_string(count)
                        + " positional delay values",
                    expression_span(*expression));
                valid = false;
                return std::nullopt;
            }
            for (std::size_t index { }; index < count; ++index) {
                const auto element = lower_hir_expression(
                    delay_expression->vhdl->operands[index], 64U);
                if (!element) {
                    report(
                        "FSIM-ELAB-VITAL-020",
                        "VITAL delay formal '" + std::string { formal }
                            + "' contains an incompatible delay value",
                        expression_span(
                            delay_expression->vhdl->operands[index]));
                    valid = false;
                    return std::nullopt;
                }
                result[index] = register_width(*element) == 64U
                    ? *element
                    : resize_register(*element, 64U, false);
            }
            return result;
        }
        std::optional<RegisterId> value;
        value = lower_hir_expression(*expression, count * 64U);
        if (!value) {
            report(
                "FSIM-ELAB-VITAL-020",
                "VITAL delay formal '" + std::string { formal }
                    + "' requires a compatible nonnegative delay value",
                expression_span(*expression));
            valid = false;
            return std::nullopt;
        }
        for (std::size_t index { }; index < count; ++index) {
            result[index] = allocate_register(
                64U, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(Extract {
                result[index], *value,
                static_cast<std::uint32_t>(
                    (count - 1U - index) * 64U),
                64U });
        }
        return result;
    };

    VitalDelay operation;
    operation.source_location = SourceLocation {
        statement_span.source_name.str(),
        static_cast<std::uint32_t>(statement_span.begin.line),
        static_cast<std::uint32_t>(statement_span.begin.column),
    };
    operation.severity = AssertionSeverity::warning;
    if (signal_delay || wire_delay) {
        operation.kind = signal_delay
            ? VitalDelayKind::signal
            : VitalDelayKind::wire;
        const auto output = signal("outsig", 0U);
        const auto input = signal("insig", 1U);
        const auto delay_expression = actual(
            signal_delay ? "dly" : "twire", 2U);
        if (!delay_expression) {
            report(
                "FSIM-ELAB-VITAL-017",
                std::string { name } + " requires its delay actual",
                statement_span);
            return true;
        }
        std::size_t count = 1U;
        if (wire_delay) {
            const auto expression = specialized_hir_unit_->find_expression(
                *delay_expression);
            const auto width = hir_expression_width(
                *delay_expression, hir_process_scope_);
            if (width && (*width == 128U || *width == 384U)) {
                count = *width / 64U;
            } else if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                count = expression->vhdl->operands.size();
            }
            if (count != 1U && count != 2U && count != 6U) {
                report(
                    "FSIM-ELAB-VITAL-020",
                    "VitalWireDelay requires scalar, 01, or 01Z delay type",
                    expression_span(*delay_expression));
                return true;
            }
        }
        operation.shape = count == 1U
            ? VitalDelayShape::single
            : count == 2U ? VitalDelayShape::delay01
                          : VitalDelayShape::delay01z;
        if (operation.shape == VitalDelayShape::delay01z) {
            operation.output_map = allocate_register(
                9U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(LoadConstant {
                operation.output_map,
                runtime::PackedLogic4::from_logic9_msb_string(
                    "UX01ZWLH-") });
        }
        const auto delays = delay_set(
            delay_expression, count, signal_delay ? "dly" : "twire");
        if (output) {
            operation.output = *output;
        }
        if (input) {
            operation.source = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(
                ReadSignal { operation.source, *input });
        }
        if (delays) {
            operation.default_delays = *delays;
        }
        operation.mode = VitalGlitchMode::transport;
        if (valid && output && input && delays) {
            process_.operations.emplace_back(std::move(operation));
        }
        return true;
    }

    operation.kind = VitalDelayKind::path;
    const auto delay_count = path_single ? 1U : path_01 ? 2U : 6U;
    operation.shape = path_single
        ? VitalDelayShape::single
        : path_01 ? VitalDelayShape::delay01
                  : VitalDelayShape::delay01z;
    const auto output = signal("outsignal", 0U);
    const auto out_temp = actual("outtemp", 3U);
    if (!out_temp) {
        report(
            "FSIM-ELAB-VITAL-017",
            std::string { name } + " requires OutTemp",
            statement_span);
        valid = false;
    } else {
        const auto lowered = lower_hir_expression(*out_temp, 1U);
        if (!lowered
            || register_domain(*lowered)
                != frontend::ValueDomain::Logic9) {
            report(
                "FSIM-ELAB-VITAL-019",
                "VITAL path OutTemp requires a scalar standard-logic value",
                expression_span(*out_temp));
            valid = false;
        } else {
            operation.source = *lowered;
        }
    }
    const auto glitch = actual("glitchdata", 1U);
    const auto glitch_declaration = glitch
        ? hir_target_declaration(*glitch)
        : std::nullopt;
    const auto glitch_binding = glitch_declaration
        ? hir_runtime_binding(
              *glitch_declaration, hir_process_scope_, true)
        : std::nullopt;
    const auto glitch_subtype = glitch
        ? hir_vhdl_expression_subtype(*glitch)
        : std::nullopt;
    if (!glitch || !glitch_binding || !glitch_binding->local
        || !glitch_subtype
        || !same_vital_identifier(
            vital_simple_name(glitch_subtype->type_mark.spelling),
            "vitalglitchdatatype")) {
        report(
            "FSIM-ELAB-VITAL-019",
            "VITAL path GlitchData requires a writable "
            "VitalGlitchDataType variable",
            glitch ? expression_span(*glitch) : statement_span);
        valid = false;
    } else {
        operation.glitch_data = *glitch_binding->local;
    }
    const auto paths = actual("paths", 4U);
    const auto paths_expression = paths
        ? specialized_hir_unit_->find_expression(*paths)
        : std::nullopt;
    if (!paths_expression || paths_expression->vhdl == nullptr
        || paths_expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::aggregate) {
        report(
            "FSIM-ELAB-VITAL-021",
            "VITAL path Paths requires a static array aggregate",
            paths ? expression_span(*paths) : statement_span);
        valid = false;
    } else {
        const auto& path_array = *paths_expression->vhdl;
        if (path_array.associations.size()
            != path_array.operands.size()) {
            report(
                "FSIM-ELAB-VITAL-021",
                "VITAL path array aggregate metadata is inconsistent",
                expression_span(*paths));
            valid = false;
        }
        for (std::size_t row_index { };
            row_index < path_array.operands.size(); ++row_index) {
            const auto row_id = path_array.operands[row_index];
            std::size_t copies = 1U;
            if (row_index < path_array.associations.size()) {
                const auto& association
                    = path_array.associations[row_index];
                if (!association.choices.empty()) {
                    copies = 0U;
                }
                for (const auto choice_id : association.choices) {
                    const auto choice
                        = specialized_hir_unit_->find_expression(choice_id);
                    if (!choice || choice->vhdl == nullptr) {
                        valid = false;
                        continue;
                    }
                    const auto& choice_source = *choice->vhdl;
                    if (choice_source.kind
                            == semantic::vhdl::ExpressionKind::default_choice
                        || (choice_source.kind
                                == semantic::vhdl::ExpressionKind::name
                            && choice_source.text == "others")) {
                        report(
                            "FSIM-ELAB-VITAL-021",
                            "an unconstrained VITAL path aggregate cannot "
                            "use others",
                            expression_span(choice_id));
                        valid = false;
                        continue;
                    }
                    if (choice_source.kind
                            == semantic::vhdl::ExpressionKind::binary
                        && (choice_source.text == "to"
                            || choice_source.text == "downto")
                        && choice_source.operands.size() == 2U) {
                        const auto left = hir_constant_integer(
                            choice_source.operands.front());
                        const auto right = hir_constant_integer(
                            choice_source.operands.back());
                        if (!left || !right) {
                            report(
                                "FSIM-ELAB-VITAL-021",
                                "VITAL path range choices require static "
                                "integer bounds",
                                expression_span(choice_id));
                            valid = false;
                            continue;
                        }
                        const bool descending
                            = choice_source.text == "downto";
                        if ((descending && *left < *right)
                            || (!descending && *left > *right)) {
                            continue;
                        }
                        const auto distance = *left >= *right
                            ? static_cast<std::uint64_t>(*left)
                                - static_cast<std::uint64_t>(*right)
                            : static_cast<std::uint64_t>(*right)
                                - static_cast<std::uint64_t>(*left);
                        if (distance
                                >= std::numeric_limits<std::size_t>::max()
                            || copies
                                > std::numeric_limits<std::size_t>::max()
                                    - static_cast<std::size_t>(distance)
                                    - 1U) {
                            report(
                                "FSIM-ELAB-VITAL-021",
                                "VITAL path range is not representable by "
                                "the host",
                                expression_span(choice_id));
                            valid = false;
                            continue;
                        }
                        copies += static_cast<std::size_t>(distance) + 1U;
                        continue;
                    }
                    if (!hir_constant_integer(choice_id)) {
                        report(
                            "FSIM-ELAB-VITAL-021",
                            "VITAL path choices require static integer "
                            "indices",
                            expression_span(choice_id));
                        valid = false;
                        continue;
                    }
                    ++copies;
                }
            }
            if (copies == 0U) {
                continue;
            }
            const auto row = specialized_hir_unit_->find_expression(row_id);
            if (!row || row->vhdl == nullptr
                || row->vhdl->kind
                    != semantic::vhdl::ExpressionKind::aggregate
                || row->vhdl->operands.size() != 3U) {
                report(
                    "FSIM-ELAB-VITAL-021",
                    "each VITAL path record requires InputChangeTime, "
                    "PathDelay, and PathCondition",
                    expression_span(row_id));
                valid = false;
                continue;
            }
            VitalPathCandidate candidate;
            const auto input_change = lower_hir_expression(
                row->vhdl->operands[0], 64U);
            const auto delays = delay_set(
                row->vhdl->operands[1], delay_count, "pathdelay");
            const auto condition = lower_hir_expression(
                row->vhdl->operands[2], 1U);
            if (!input_change || !delays || !condition
                || register_domain(*condition)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-VITAL-021",
                    "a VITAL path record has an incompatible time, delay, "
                    "or Boolean field",
                    expression_span(row_id));
                valid = false;
                continue;
            }
            candidate.input_change_time = *input_change;
            candidate.delays = *delays;
            candidate.condition = *condition;
            for (std::size_t copy { }; copy < copies; ++copy) {
                operation.paths.push_back(candidate);
            }
        }
    }
    const auto defaults = delay_set(
        actual("defaultdelay", 5U), delay_count, "defaultdelay");
    if (defaults) {
        operation.default_delays = *defaults;
    }
    if (const auto mode = actual("mode", 6U)) {
        const auto expression = specialized_hir_unit_->find_expression(
            *mode);
        const auto decoded = expression && expression->vhdl != nullptr
            ? vital_glitch_mode(*expression->vhdl)
            : std::nullopt;
        if (!decoded) {
            report(
                "FSIM-ELAB-VITAL-018",
                "VITAL path Mode requires a static VitalGlitchKindType "
                "literal",
                expression_span(*mode));
            valid = false;
        } else {
            operation.mode = *decoded;
        }
    }
    operation.x_on = static_bool("xon", 7U, true);
    operation.message_on = static_bool("msgon", 8U, true);
    if (const auto severity = actual("msgseverity", 9U)) {
        const auto expression = specialized_hir_unit_->find_expression(
            *severity);
        const auto decoded = expression && expression->vhdl != nullptr
            ? vital_delay_severity(*expression->vhdl)
            : std::nullopt;
        if (!decoded) {
            report(
                "FSIM-ELAB-VITAL-018",
                "VITAL path MsgSeverity requires a static severity_level "
                "literal",
                expression_span(*severity));
            valid = false;
        } else {
            operation.severity = *decoded;
        }
    }
    const auto output_name = static_string("outsignalname", 2U, "");
    operation.message = "VitalPathDelay(" + output_name + ")";
    operation.negative_preemption = static_bool(
        "negpreempton", path_01z ? 11U : 10U, false);
    operation.ignore_default_delay = static_bool(
        "ignoredefaultdelay", path_01z ? 12U : 11U, false);
    operation.reject_fast_path = !path_single && static_bool(
        "rejectfastpath", path_01z ? 13U : 12U, false);
    const auto default_map = [&] {
        const auto result = allocate_register(
            9U, frontend::ValueDomain::Logic9);
        process_.operations.emplace_back(LoadConstant {
            result,
            runtime::PackedLogic4::from_logic9_msb_string(
                "UX01ZWLH-") });
        return result;
    }();
    operation.output_map = default_map;
    if (path_01z) {
        if (const auto map = actual("outputmap", 10U)) {
            const auto lowered = lower_hir_expression(*map, 9U);
            if (!lowered
                || register_domain(*lowered)
                    != frontend::ValueDomain::Logic9) {
                report(
                    "FSIM-ELAB-VITAL-020",
                    "VitalPathDelay01Z OutputMap requires "
                    "VitalOutputMapType",
                    expression_span(*map));
                valid = false;
            } else {
                operation.output_map = *lowered;
            }
        }
    }
    if (output) {
        operation.output = *output;
    }
    if (valid && output && defaults) {
        process_.operations.emplace_back(std::move(operation));
    }
    return true;
}

} // namespace fsim::elaboration
