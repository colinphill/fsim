// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <cctype>
#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;
using runtime::SimulationTick;

namespace {

std::string_view vital_timing_simple_name(const std::string_view name)
{
    const auto separator = name.find_last_of('.');
    return name.substr(
        separator == std::string_view::npos ? 0U : separator + 1U);
}

std::string_view vital_timing_name(const semantic::vhdl::Name& name)
{
    return name.canonical.empty()
        ? std::string_view { name.spelling }
        : std::string_view { name.canonical };
}

bool same_vital_timing_identifier(
    const std::string_view left, const std::string_view right)
{
    return left.size() == right.size()
        && std::ranges::equal(left, right, [](const char lhs, const char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs))
                   == std::tolower(static_cast<unsigned char>(rhs));
           });
}

std::string canonical_vital_timing_identifier(const std::string_view name)
{
    auto result = std::string { name };
    std::ranges::transform(result, result.begin(), [](const char value) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    });
    return result;
}

bool vital_timing_procedure_name(const std::string_view name)
{
    return same_vital_timing_identifier(name, "vitalsetupholdcheck")
        || same_vital_timing_identifier(
            name, "vitalrecoveryremovalcheck")
        || same_vital_timing_identifier(name, "vitalperiodpulsecheck")
        || same_vital_timing_identifier(name, "vitalinphaseskewcheck")
        || same_vital_timing_identifier(name, "vitaloutphaseskewcheck");
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
        return same_vital_timing_identifier(owner, "vital_timing");
    }
    return same_vital_timing_identifier(
               owner.substr(package_separator + 1U), "vital_timing")
        && same_vital_timing_identifier(
            owner.substr(0U, package_separator), "ieee");
}

std::optional<AssertionSeverity> vital_timing_severity(
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
    name = vital_timing_simple_name(name);
    if (same_vital_timing_identifier(name, "note")) {
        return AssertionSeverity::note;
    }
    if (same_vital_timing_identifier(name, "warning")) {
        return AssertionSeverity::warning;
    }
    if (same_vital_timing_identifier(name, "error")) {
        return AssertionSeverity::error;
    }
    if (same_vital_timing_identifier(name, "failure")) {
        return AssertionSeverity::failure;
    }
    return std::nullopt;
}

std::optional<std::uint16_t> vital_timing_edge(
    const semantic::vhdl::Expression& expression)
{
    auto text = expression.decoded_string
        ? std::string_view { *expression.decoded_string }
        : std::string_view { expression.text };
    constexpr std::string_view prefix { "@fsim-enum:" };
    if (text.starts_with(prefix)) {
        text.remove_prefix(prefix.size());
    }
    if (text.size() >= 3U && text.front() == '\'' && text.back() == '\'') {
        text.remove_prefix(1U);
        text.remove_suffix(1U);
    }
    static constexpr std::array<std::string_view, 16U> literals {
        "/", "\\", "P", "N", "r", "f", "p", "n",
        "R", "F", "^", "v", "E", "A", "D", "*"
    };
    const auto found = std::ranges::find(literals, text);
    if (found == literals.end()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(std::uint16_t { 1U }
        << static_cast<unsigned>(std::distance(literals.begin(), found)));
}

} // namespace

bool Lowerer::is_hir_vhdl_vital_timing_call(
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
    const auto qualified_name = vital_timing_name(procedure);
    if (!vital_timing_procedure_name(
            vital_timing_simple_name(qualified_name))
        || !has_vital_timing_qualification(qualified_name)) {
        return false;
    }
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

bool Lowerer::lower_hir_vhdl_vital_timing_call(
    const semantic::StatementId statement_id)
{
    if (!is_hir_vhdl_vital_timing_call(statement_id)) {
        return false;
    }
    const auto statement = specialized_hir_unit_->find_statement(
        statement_id);
    if (!statement || statement->vhdl == nullptr) {
        return false;
    }
    const auto& source = *statement->vhdl;
    const auto statement_span = hir_source_span(source.source);
    const auto name = vital_timing_simple_name(
        vital_timing_name(source.procedure));
    const bool setup_hold = same_vital_timing_identifier(
        name, "vitalsetupholdcheck");
    const bool recovery_removal = same_vital_timing_identifier(
        name, "vitalrecoveryremovalcheck");
    const bool period_pulse = same_vital_timing_identifier(
        name, "vitalperiodpulsecheck");
    const bool in_phase = same_vital_timing_identifier(
        name, "vitalinphaseskewcheck");
    const bool out_phase = same_vital_timing_identifier(
        name, "vitaloutphaseskewcheck");
    const bool skew = in_phase || out_phase;

    std::vector<semantic::ExpressionId> positional;
    std::map<std::string, semantic::ExpressionId, std::less<>> named;
    bool saw_named { };
    bool valid = true;
    for (const auto& association : source.procedure_arguments) {
        if (association.formal) {
            saw_named = true;
            const auto formal = canonical_vital_timing_identifier(
                vital_timing_simple_name(
                    vital_timing_name(*association.formal)));
            if (!named.emplace(formal, association.actual).second) {
                report("FSIM-ELAB-VITAL-010",
                    "duplicate VITAL timing actual for formal '"
                        + formal + "'",
                    hir_source_span(association.source));
                valid = false;
            }
        } else {
            if (saw_named) {
                report("FSIM-ELAB-VITAL-010",
                    "a positional VITAL timing actual cannot follow a named actual",
                    hir_source_span(association.source));
                valid = false;
            }
            positional.push_back(association.actual);
        }
    }
    const auto expression_span = [&](const semantic::ExpressionId id) {
        const auto expression = specialized_hir_unit_->find_expression(id);
        return expression && expression->vhdl != nullptr
            ? hir_source_span(expression->vhdl->source)
            : statement_span;
    };
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
    const auto formals = [&] {
        if (period_pulse) {
            return std::vector<std::string_view> {
                "violation", "perioddata", "testsignal",
                "testsignalname", "testdelay", "period",
                "pulsewidthhigh", "pulsewidthlow", "checkenabled",
                "headermsg", "xon", "msgon", "msgseverity"
            };
        }
        if (setup_hold) {
            return std::vector<std::string_view> {
                "violation", "timingdata", "testsignal",
                "testsignalname", "testdelay", "refsignal",
                "refsignalname", "refdelay", "setuphigh", "setuplow",
                "holdhigh", "holdlow", "checkenabled", "reftransition",
                "headermsg", "xon", "msgon", "msgseverity",
                "enablesetupontest", "enablesetuponref",
                "enableholdonref", "enableholdontest"
            };
        }
        if (recovery_removal) {
            return std::vector<std::string_view> {
                "violation", "timingdata", "testsignal",
                "testsignalname", "testdelay", "refsignal",
                "refsignalname", "refdelay", "recovery", "removal",
                "activelow", "checkenabled", "reftransition",
                "headermsg", "xon", "msgon", "msgseverity",
                "enablerecontest", "enablereconref", "enableremonref",
                "enableremontest"
            };
        }
        return std::vector<std::string_view> {
            "violation", "skewdata", "signal1", "signal1name",
            "signal1delay", "signal2", "signal2name", "signal2delay",
            in_phase ? "skews1s2riserise" : "skews1s2risefall",
            in_phase ? "skews2s1riserise" : "skews2s1risefall",
            in_phase ? "skews1s2fallfall" : "skews1s2fallrise",
            in_phase ? "skews2s1fallfall" : "skews2s1fallrise",
            "checkenabled", "xon", "msgon", "msgseverity", "headermsg",
            "trigger"
        };
    }();
    if (positional.size() > formals.size()) {
        report("FSIM-ELAB-VITAL-010",
            std::string { name } + " has too many positional actuals",
            statement_span);
        valid = false;
    }
    for (const auto& [formal, expression_id] : named) {
        if (std::ranges::find(formals, std::string_view { formal })
            == formals.end()) {
            report("FSIM-ELAB-VITAL-010",
                std::string { name } + " has no formal parameter '"
                    + formal + "'",
                expression_span(expression_id));
            valid = false;
        }
    }

    const auto static_time = [&](const std::string_view formal,
                                 const std::size_t position,
                                 const SimulationTick fallback) {
        const auto expression = actual(formal, position);
        if (!expression) {
            return fallback;
        }
        const auto value = hir_constant_integer(*expression);
        if (!value || *value < 0) {
            report("FSIM-ELAB-VITAL-011",
                "VITAL timing formal '" + std::string { formal }
                    + "' requires a static nonnegative time in project ticks",
                expression_span(*expression));
            valid = false;
            return SimulationTick { };
        }
        return static_cast<SimulationTick>(*value);
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
            report("FSIM-ELAB-VITAL-011",
                "VITAL timing formal '" + std::string { formal }
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
            report("FSIM-ELAB-VITAL-011",
                "VITAL timing formal '" + std::string { formal }
                    + "' requires a static string literal",
                expression_span(*expression_id));
            valid = false;
            return std::string { fallback };
        }
        return *expression->vhdl->decoded_string;
    };
    const auto runtime_binding = [&](const semantic::ExpressionId expression,
                                     const bool require_storage)
        -> std::optional<HirRuntimeBinding> {
        if (const auto direct = hir_direct_signal_binding(expression)) {
            return direct;
        }
        const auto declaration = hir_target_declaration(expression);
        return declaration
            ? hir_runtime_binding(
                  *declaration, hir_process_scope_, require_storage)
            : std::nullopt;
    };
    const auto required_local = [&](const std::string_view formal,
                                    const std::size_t position,
                                    const std::string_view type_name)
        -> std::optional<RegisterId> {
        const auto expression = actual(formal, position);
        const auto binding = expression
            ? runtime_binding(*expression, true)
            : std::nullopt;
        bool compatible = binding && binding->local;
        if (compatible && type_name.empty()) {
            compatible = binding->domain == frontend::ValueDomain::Logic9
                && binding->width == 1U;
        } else if (compatible) {
            const auto subtype = hir_vhdl_expression_subtype(*expression);
            compatible = subtype
                && same_vital_timing_identifier(
                    vital_timing_simple_name(subtype->type_mark.spelling),
                    type_name);
        }
        if (!compatible) {
            report("FSIM-ELAB-VITAL-012",
                "VITAL timing formal '" + std::string { formal }
                    + "' requires a writable variable of the expected type",
                expression ? expression_span(*expression) : statement_span);
            valid = false;
            return std::nullopt;
        }
        return binding->local;
    };
    const auto signal_actual = [&](const std::string_view formal,
                                   const std::size_t position,
                                   const SimulationTick delay)
        -> std::optional<SignalId> {
        const auto expression = actual(formal, position);
        const auto binding = expression
            ? runtime_binding(*expression, false)
            : std::nullopt;
        if (!binding || !binding->signal) {
            report("FSIM-ELAB-VITAL-012",
                "VITAL timing formal '" + std::string { formal }
                    + "' requires a visible signal",
                expression ? expression_span(*expression) : statement_span);
            valid = false;
            return std::nullopt;
        }
        if (binding->domain != frontend::ValueDomain::Logic9) {
            report("FSIM-ELAB-VITAL-012",
                "VITAL timing formal '" + std::string { formal }
                    + "' requires a std_ulogic signal or vector",
                expression_span(*expression));
            valid = false;
            return std::nullopt;
        }
        if (delay == 0U) {
            implicit_signal_dependencies_.push_back(*binding->signal);
            return binding->signal;
        }
        const auto delayed = vhdl_implicit_signal_attribute(
            *binding->signal, "delayed", delay,
            expression_span(*expression));
        if (!delayed) {
            valid = false;
        }
        return delayed;
    };
    const auto severity = [&](const std::size_t position) {
        const auto expression_id = actual("msgseverity", position);
        if (!expression_id) {
            return AssertionSeverity::warning;
        }
        const auto expression = specialized_hir_unit_->find_expression(
            *expression_id);
        const auto decoded = expression && expression->vhdl != nullptr
            ? vital_timing_severity(*expression->vhdl)
            : std::nullopt;
        if (!decoded) {
            report("FSIM-ELAB-VITAL-011",
                "VITAL MsgSeverity requires a static severity_level literal",
                expression_span(*expression_id));
            valid = false;
            return AssertionSeverity::warning;
        }
        return *decoded;
    };
    const auto reference_edges = [&](const std::size_t position,
                                     const std::string_view procedure)
        -> std::optional<std::uint16_t> {
        const auto expression_id = actual("reftransition", position);
        if (!expression_id) {
            report("FSIM-ELAB-VITAL-010",
                std::string { procedure } + " requires RefTransition",
                statement_span);
            valid = false;
            return std::nullopt;
        }
        const auto expression = specialized_hir_unit_->find_expression(
            *expression_id);
        const auto decoded = expression && expression->vhdl != nullptr
            ? vital_timing_edge(*expression->vhdl)
            : std::nullopt;
        if (!decoded) {
            report("FSIM-ELAB-VITAL-011",
                "VITAL RefTransition requires a static edge symbol",
                expression_span(*expression_id));
            valid = false;
        }
        return decoded;
    };

    const auto violation = required_local("violation", 0U, "");
    const auto state_type = period_pulse
        ? std::string_view { "vitalperioddatatype" }
        : skew
        ? std::string_view { "vitalskewdatatype" }
        : std::string_view { "vitaltimingdatatype" };
    (void)required_local(period_pulse
            ? "perioddata"
            : skew
            ? "skewdata"
            : "timingdata",
        1U, state_type);

    VitalTimingCheck operation;
    operation.source = SourceLocation {
        statement_span.source_name.str(),
        static_cast<std::uint32_t>(statement_span.begin.line),
        static_cast<std::uint32_t>(statement_span.begin.column),
    };
    std::size_t test_width { 1U };
    if (period_pulse) {
        const auto test_delay = static_time("testdelay", 4U, 0U);
        const auto test = signal_actual("testsignal", 2U, test_delay);
        if (test) {
            operation.test_signal = *test;
            test_width = design_.signal_info_[*test].width;
            if (test_width != 1U) {
                report("FSIM-ELAB-VITAL-012",
                    "VitalPeriodPulseCheck test signal must be scalar",
                    statement_span);
                valid = false;
            }
        }
        operation.kind = VitalTimingCheckKind::period_pulse;
        operation.limits = { static_time("period", 5U, 0U),
            static_time("pulsewidthhigh", 6U, 0U),
            static_time("pulsewidthlow", 7U, 0U), 0U };
        operation.check_enabled = static_bool("checkenabled", 8U, true);
        operation.message = static_string("headermsg", 9U, " ")
            + "VitalPeriodPulseCheck("
            + static_string("testsignalname", 3U, "") + ")";
        operation.x_on = static_bool("xon", 10U, true);
        operation.message_on = static_bool("msgon", 11U, true);
        operation.severity = severity(12U);
    } else if (setup_hold || recovery_removal) {
        const auto test_delay = static_time("testdelay", 4U, 0U);
        const auto reference_delay = static_time("refdelay", 7U, 0U);
        const auto test = signal_actual("testsignal", 2U, test_delay);
        const auto reference = signal_actual(
            "refsignal", 5U, reference_delay);
        if (test) {
            operation.test_signal = *test;
            test_width = design_.signal_info_[*test].width;
        }
        if (reference) {
            operation.reference_signal = *reference;
            if (design_.signal_info_[*reference].width != 1U) {
                report("FSIM-ELAB-VITAL-012",
                    "VITAL timing reference signal must be scalar",
                    statement_span);
                valid = false;
            }
        }
        if (recovery_removal && test && test_width != 1U) {
            report("FSIM-ELAB-VITAL-012",
                "VitalRecoveryRemovalCheck test signal must be scalar",
                statement_span);
            valid = false;
        }
        operation.kind = setup_hold
            ? VitalTimingCheckKind::setup_hold
            : VitalTimingCheckKind::recovery_removal;
        if (setup_hold) {
            operation.limits = { static_time("setuphigh", 8U, 0U),
                static_time("setuplow", 9U, 0U),
                static_time("holdhigh", 10U, 0U),
                static_time("holdlow", 11U, 0U) };
            operation.check_enabled = static_bool(
                "checkenabled", 12U, true);
            if (const auto edges = reference_edges(
                    13U, "VitalSetupHoldCheck")) {
                operation.reference_edges = *edges;
            }
            operation.message = static_string("headermsg", 14U, " ")
                + "VitalSetupHoldCheck("
                + static_string("testsignalname", 3U, "") + ","
                + static_string("refsignalname", 6U, "") + ")";
            operation.x_on = static_bool("xon", 15U, true);
            operation.message_on = static_bool("msgon", 16U, true);
            operation.severity = severity(17U);
            operation.enables = {
                static_bool("enablesetupontest", 18U, true),
                static_bool("enablesetuponref", 19U, true),
                static_bool("enableholdonref", 20U, true),
                static_bool("enableholdontest", 21U, true),
            };
        } else {
            operation.limits = { static_time("recovery", 8U, 0U),
                static_time("removal", 9U, 0U), 0U, 0U };
            operation.active_low = static_bool("activelow", 10U, true);
            operation.check_enabled = static_bool(
                "checkenabled", 11U, true);
            if (const auto edges = reference_edges(
                    12U, "VitalRecoveryRemovalCheck")) {
                operation.reference_edges = *edges;
            }
            operation.message = static_string("headermsg", 13U, " ")
                + "VitalRecoveryRemovalCheck("
                + static_string("testsignalname", 3U, "") + ","
                + static_string("refsignalname", 6U, "") + ")";
            operation.x_on = static_bool("xon", 14U, true);
            operation.message_on = static_bool("msgon", 15U, true);
            operation.severity = severity(16U);
            operation.enables = {
                static_bool("enablerecontest", 17U, true),
                static_bool("enablereconref", 18U, true),
                static_bool("enableremonref", 19U, true),
                static_bool("enableremontest", 20U, true),
            };
        }
    } else {
        const auto signal1_delay = static_time("signal1delay", 4U, 0U);
        const auto signal2_delay = static_time("signal2delay", 7U, 0U);
        const auto signal1 = signal_actual("signal1", 2U, signal1_delay);
        const auto signal2 = signal_actual("signal2", 5U, signal2_delay);
        if (signal1) {
            operation.test_signal = *signal1;
            test_width = design_.signal_info_[*signal1].width;
        }
        if (signal2) {
            operation.reference_signal = *signal2;
        }
        if ((signal1 && test_width != 1U)
            || (signal2 && design_.signal_info_[*signal2].width != 1U)) {
            report("FSIM-ELAB-VITAL-012",
                "VITAL skew check signals must be scalar", statement_span);
            valid = false;
        }
        operation.kind = in_phase
            ? VitalTimingCheckKind::in_phase_skew
            : VitalTimingCheckKind::out_phase_skew;
        operation.limits = {
            static_time(in_phase ? "skews1s2riserise"
                                 : "skews1s2risefall",
                8U, std::numeric_limits<SimulationTick>::max()),
            static_time(in_phase ? "skews2s1riserise"
                                 : "skews2s1risefall",
                9U, std::numeric_limits<SimulationTick>::max()),
            static_time(in_phase ? "skews1s2fallfall"
                                 : "skews1s2fallrise",
                10U, std::numeric_limits<SimulationTick>::max()),
            static_time(in_phase ? "skews2s1fallfall"
                                 : "skews2s1fallrise",
                11U, std::numeric_limits<SimulationTick>::max()),
        };
        operation.check_enabled = static_bool("checkenabled", 12U, true);
        operation.x_on = static_bool("xon", 13U, true);
        operation.message_on = static_bool("msgon", 14U, true);
        operation.severity = severity(15U);
        operation.message = static_string("headermsg", 16U, "")
            + (in_phase ? "VitalInPhaseSkewCheck("
                        : "VitalOutPhaseSkewCheck(")
            + static_string("signal1name", 3U, "") + ","
            + static_string("signal2name", 6U, "") + ")";
        const auto trigger = signal_actual("trigger", 17U, 0U);
        if (trigger) {
            if (design_.signal_info_[*trigger].width != 1U) {
                report("FSIM-ELAB-VITAL-012",
                    "VITAL skew Trigger must be a scalar standard-logic signal",
                    statement_span);
                valid = false;
            } else {
                operation.trigger_signal = *trigger;
            }
        }
    }

    if (!valid || !violation) {
        return true;
    }
    if (test_width == 0U
        || test_width > std::numeric_limits<std::uint32_t>::max()) {
        report("FSIM-ELAB-VITAL-012",
            "VITAL timing test signal has no bounded executable width",
            statement_span);
        return true;
    }
    std::vector<RegisterId> results;
    results.reserve(test_width);
    for (std::size_t bit { }; bit < test_width; ++bit) {
        auto instance = operation;
        instance.test_offset = static_cast<std::uint32_t>(bit);
        instance.destination = test_width == 1U
            ? *violation
            : allocate_register(1U, frontend::ValueDomain::Logic9);
        results.push_back(instance.destination);
        process_.operations.emplace_back(std::move(instance));
    }
    if (results.size() > 1U) {
        auto combined = results.front();
        for (std::size_t index { 1U }; index < results.size(); ++index) {
            const auto next = allocate_register(
                1U, frontend::ValueDomain::Logic9);
            process_.operations.emplace_back(Binary {
                BinaryOperator::bit_or, next, combined, results[index] });
            combined = next;
        }
        process_.operations.emplace_back(
            CopyRegister { *violation, combined });
    }
    return true;
}

} // namespace fsim::elaboration
