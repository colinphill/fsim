// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/path.hpp"
#include "simir_execution_context.hpp"
#include "simir_execution_shared.hpp"
#include "simir_internal.hpp"
#include "simir_signal_attributes.hpp"

#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>

#include <boost/multiprecision/cpp_int.hpp>

namespace fsim::runtime::simir {
namespace {

constexpr std::size_t vhdl_time_record_width = 515U;

struct VhdlCalendarFields {
    std::int64_t microsecond { };
    std::int64_t second { };
    std::int64_t minute { };
    std::int64_t hour { };
    std::int64_t day { };
    std::int64_t month { };
    std::int64_t year { };
    std::int64_t weekday { };
    std::int64_t dayofyear { };
};

[[nodiscard]] std::int64_t known_calendar_member(
    const PackedLogic4& value,
    const std::size_t offset,
    const std::size_t width,
    const char* name)
{
    const auto selected = value.extract_bits(offset, width);
    auto known = selected.known_signed_value();
    if (width != 64U) {
        const auto unsigned_value = selected.known_unsigned_value();
        known = unsigned_value
            ? std::optional<std::int64_t> {
                  static_cast<std::int64_t>(*unsigned_value) }
            : std::nullopt;
    }
    if (!known) {
        throw std::invalid_argument {
            std::string { "STD.ENV TIME_RECORD " } + name
                + " is not a known value"
        };
    }
    return *known;
}

[[nodiscard]] VhdlCalendarFields unpack_calendar(
    const PackedLogic4& value)
{
    if (value.width() != vhdl_time_record_width || value.is_logic9()) {
        throw std::invalid_argument {
            "STD.ENV TIME_RECORD has an invalid packed representation"
        };
    }
    VhdlCalendarFields result {
        known_calendar_member(value, 451U, 64U, "microsecond"),
        known_calendar_member(value, 387U, 64U, "second"),
        known_calendar_member(value, 323U, 64U, "minute"),
        known_calendar_member(value, 259U, 64U, "hour"),
        known_calendar_member(value, 195U, 64U, "day"),
        known_calendar_member(value, 131U, 64U, "month"),
        known_calendar_member(value, 67U, 64U, "year"),
        known_calendar_member(value, 64U, 3U, "weekday"),
        known_calendar_member(value, 0U, 64U, "dayofyear")
    };
    const auto in_range = [](const std::int64_t member,
                              const std::int64_t lower,
                              const std::int64_t upper) {
        return member >= lower && member <= upper;
    };
    if (!in_range(result.microsecond, 0, 999'999)
        || !in_range(result.second, 0, 61)
        || !in_range(result.minute, 0, 59)
        || !in_range(result.hour, 0, 23)
        || !in_range(result.day, 1, 31)
        || !in_range(result.month, 0, 11)
        || !in_range(result.year, 1, 4095)
        || !in_range(result.weekday, 0, 6)
        || !in_range(result.dayofyear, 0, 365)) {
        throw std::invalid_argument {
            "STD.ENV TIME_RECORD contains a field outside its subtype"
        };
    }
    return result;
}

[[nodiscard]] PackedLogic4 pack_calendar(
    const VhdlCalendarFields& value)
{
    PackedLogic4 result(vhdl_time_record_width, Logic4::zero);
    const auto insert = [&](const std::int64_t field,
                            const std::size_t offset,
                            const std::size_t width) {
        result.insert_bits(
            PackedLogic4::from_aval_bval(
                width, static_cast<std::uint64_t>(field), 0U),
            offset);
    };
    insert(value.microsecond, 451U, 64U);
    insert(value.second, 387U, 64U);
    insert(value.minute, 323U, 64U);
    insert(value.hour, 259U, 64U);
    insert(value.day, 195U, 64U);
    insert(value.month, 131U, 64U);
    insert(value.year, 67U, 64U);
    insert(value.weekday, 64U, 3U);
    insert(value.dayofyear, 0U, 64U);
    return result;
}



[[nodiscard]] std::tm local_calendar(const std::time_t value)
{
    std::tm result { };
#if defined(_WIN32)
    if (::localtime_s(&result, &value) != 0) {
#else
    if (::localtime_r(&value, &result) == nullptr) {
#endif
        throw std::invalid_argument {
            "STD.ENV local-time conversion is outside the host calendar range"
        };
    }
    return result;
}

[[nodiscard]] std::pair<std::int64_t, std::int64_t> split_epoch(
    const double epoch)
{
    if (!std::isfinite(epoch)) {
        throw std::invalid_argument {
            "STD.ENV epoch value must be finite"
        };
    }
    auto integral = std::floor(epoch);
    if (integral < static_cast<double>(
            std::numeric_limits<std::int64_t>::min())
        || integral > static_cast<double>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument {
            "STD.ENV epoch value is outside the supported calendar range"
        };
    }
    auto whole = static_cast<std::int64_t>(integral);
    auto microseconds = static_cast<std::int64_t>(
        std::llround((epoch - integral) * 1'000'000.0));
    if (microseconds == 1'000'000) {
        if (whole == std::numeric_limits<std::int64_t>::max()) {
            throw std::invalid_argument {
                "STD.ENV epoch value is outside the supported calendar range"
            };
        }
        ++whole;
        microseconds = 0;
    }
    return {whole, microseconds};
}

[[nodiscard]] VhdlCalendarFields utc_calendar_from_epoch(
    const double epoch)
{
    using namespace std::chrono;
    const auto [whole, microseconds] = split_epoch(epoch);
    const auto instant = sys_seconds { seconds { whole } };
    const auto date = floor<days>(instant);
    const auto ymd = year_month_day { date };
    if (!ymd.ok()) {
        throw std::invalid_argument {
            "STD.ENV UTC conversion produced an invalid calendar date"
        };
    }
    const auto clock = hh_mm_ss { instant - date };
    const auto first_day = sys_days {
        ymd.year() / January / day { 1U }
    };
    return VhdlCalendarFields {
        microseconds,
        clock.seconds().count(),
        clock.minutes().count(),
        clock.hours().count(),
        static_cast<std::int64_t>(static_cast<unsigned>(ymd.day())),
        static_cast<std::int64_t>(static_cast<unsigned>(ymd.month())) - 1,
        static_cast<std::int64_t>(static_cast<int>(ymd.year())),
        static_cast<std::int64_t>(weekday { date }.c_encoding()),
        (date - first_day).count()
    };
}

[[nodiscard]] std::time_t checked_time_t(const std::int64_t seconds)
{
    const auto value = static_cast<long double>(seconds);
    if (value < static_cast<long double>(
            std::numeric_limits<std::time_t>::lowest())
        || value > static_cast<long double>(
            std::numeric_limits<std::time_t>::max())) {
        throw std::invalid_argument {
            "STD.ENV local-time conversion is outside the host calendar range"
        };
    }
    return static_cast<std::time_t>(seconds);
}

[[nodiscard]] VhdlCalendarFields local_calendar_from_epoch(
    const double epoch)
{
    const auto [whole, microseconds] = split_epoch(epoch);
    const auto calendar = local_calendar(checked_time_t(whole));
    return VhdlCalendarFields {
        microseconds,
        calendar.tm_sec,
        calendar.tm_min,
        calendar.tm_hour,
        calendar.tm_mday,
        calendar.tm_mon,
        calendar.tm_year + 1900,
        calendar.tm_wday,
        calendar.tm_yday
    };
}

[[nodiscard]] double utc_epoch_from_calendar(
    const VhdlCalendarFields& value)
{
    using namespace std::chrono;
    const auto date = year { static_cast<int>(value.year) }
        / month { static_cast<unsigned>(value.month + 1) }
        / day { static_cast<unsigned>(value.day) };
    if (!date.ok()) {
        throw std::invalid_argument {
            "STD.ENV TIME_RECORD contains an invalid calendar date"
        };
    }
    const auto leap = std::max<std::int64_t>(value.second - 59, 0);
    const auto second = std::min<std::int64_t>(value.second, 59);
    const auto instant = sys_days { date }
        + hours { value.hour } + minutes { value.minute }
        + seconds { second + leap };
    return duration<double>(instant.time_since_epoch()).count()
        + static_cast<double>(value.microsecond) / 1'000'000.0;
}

[[nodiscard]] double local_epoch_from_calendar(
    const VhdlCalendarFields& value)
{
    std::tm calendar { };
    calendar.tm_year = static_cast<int>(value.year - 1900);
    calendar.tm_mon = static_cast<int>(value.month);
    calendar.tm_mday = static_cast<int>(value.day);
    calendar.tm_hour = static_cast<int>(value.hour);
    calendar.tm_min = static_cast<int>(value.minute);
    calendar.tm_sec = static_cast<int>(std::min<std::int64_t>(value.second, 59));
    calendar.tm_isdst = -1;
    const auto requested = calendar;
    const auto seconds = std::mktime(&calendar);
    const auto round_trip = local_calendar(seconds);
    if (round_trip.tm_year != requested.tm_year
        || round_trip.tm_mon != requested.tm_mon
        || round_trip.tm_mday != requested.tm_mday
        || round_trip.tm_hour != requested.tm_hour
        || round_trip.tm_min != requested.tm_min
        || round_trip.tm_sec != requested.tm_sec) {
        throw std::invalid_argument {
            "STD.ENV TIME_RECORD is not a representable local calendar time"
        };
    }
    const auto leap = std::max<std::int64_t>(value.second - 59, 0);
    return static_cast<double>(seconds)
        + static_cast<double>(leap)
        + static_cast<double>(value.microsecond) / 1'000'000.0;
}

[[nodiscard]] std::string calendar_string(
    const VhdlCalendarFields& value,
    const std::uint32_t fractional_digits)
{
    if (fractional_digits > 6U) {
        throw std::invalid_argument {
            "STD.ENV TO_STRING FRAC_DIGITS must be in 0 through 6"
        };
    }
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << value.year
           << '-' << std::setw(2) << value.month + 1
           << '-' << std::setw(2) << value.day
           << 'T' << std::setw(2) << value.hour
           << ':' << std::setw(2) << value.minute
           << ':' << std::setw(2) << value.second;
    if (fractional_digits != 0U) {
        auto fractional = value.microsecond;
        for (auto omitted = 6U; omitted > fractional_digits; --omitted) {
            fractional /= 10;
        }
        output << '.' << std::setw(static_cast<int>(fractional_digits))
               << fractional;
    }
    return output.str();
}

constexpr std::size_t vhdl_directory_item_limit = 4096U;
constexpr std::size_t vhdl_directory_text_limit = 1024U * 1024U;








} // namespace

void Interpreter::Impl::handle_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex next_instruction)
{
    if (instruction >= process.program.operations.size()) {
        process.pc = instruction;
        fail(process, "executor returned an invalid boundary instruction");
    }

    const auto& operation
        = std::as_const(process.program.operations)[instruction];
    const auto* dynamic_call = fsim::runtime::simir::operation_get_if<Call>(&operation);
    const auto* dynamic_return = fsim::runtime::simir::operation_get_if<Return>(&operation);
    const auto* frame_push = fsim::runtime::simir::operation_get_if<CallableFramePush>(&operation);
    const auto* frame_pop = fsim::runtime::simir::operation_get_if<CallableFramePop>(&operation);
    const bool callable_boundary = (dynamic_call && dynamic_call->stack.capacity == 0)
        || (dynamic_return && dynamic_return->stack.capacity == 0)
        || frame_push || frame_pop;
    const auto expected_next = callable_boundary
        ? instruction
        : instruction + 1;
    if (instruction == std::numeric_limits<InstructionIndex>::max()
        || next_instruction != expected_next) {
        process.pc = instruction;
        fail(
            process,
            "executor returned a non-sequential boundary resume instruction");
    }

    process.pc = callable_boundary ? instruction : next_instruction;
    if (dynamic_call && dynamic_call->stack.capacity == 0) {
        execute_dynamic_call(process, *dynamic_call);
        return;
    }
    if (dynamic_return && dynamic_return->stack.capacity == 0) {
        execute_dynamic_return(process, *dynamic_return);
        return;
    }
    if (frame_push) {
        if (jit_skip_callable_frames) {
            ++process.pc;
            return;
        }
        push_callable_frame(process, *frame_push);
        return;
    }
    if (frame_pop) {
        if (jit_skip_callable_frames) {
            ++process.pc;
            return;
        }
        pop_callable_frame(process, *frame_pop);
        return;
    }
    if (const auto* read = operation_get_if<ReadSignal>(&operation);
        read && read->kind != SignalReadKind::current) {
        execute_sampled_read(process, *read);
        return;
    }
    if (const auto* sample
        = fsim::runtime::simir::operation_get_if<CoverageSample>(&operation)) {
        if (!coverage_sample_hook) {
            process.pc = instruction;
            fail(process, "coverage sampling service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "coverage sampling boundary requires an executor");
        }
        std::vector<PackedLogic4> actuals;
        actuals.reserve(sample->actuals.size());
        for (std::size_t index = 0; index < sample->actuals.size(); ++index) {
            actuals.push_back(process.executor->read_register(
                sample->actuals[index], sample->actual_widths[index]));
        }
        coverage_sample_hook(
            sample->instance_identity, actuals, sample->signed_actuals,
            sample->scalar_kinds,
            sample->trigger);
        return;
    }
    if (fsim::runtime::simir::operation_holds<CodeCoverageHit>(operation)) {
        process.pc = instruction;
        fail(process, "code coverage counter service is unavailable");
    }
    if (const auto* query
        = fsim::runtime::simir::operation_get_if<CoverageQuery>(&operation)) {
        if (query->kind != CoverageQueryKind::overall_type
            && query->kind != CoverageQueryKind::overall_instance) {
            process.pc = instruction;
            fail(process, "coverage query kind is invalid");
        }
        if (!coverage_query_hook) {
            process.pc = instruction;
            fail(process, "coverage query service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "coverage query boundary requires an executor");
        }
        auto value = coverage_query_hook(query->kind);
        if (value.width() != 64U || value.is_logic9()) {
            process.pc = instruction;
            fail(process, "coverage query service returned an invalid real payload");
        }
        process.executor->write_register(query->destination, value);
        return;
    }
    if (const auto* api
        = fsim::runtime::simir::operation_get_if<VhdlPslApi>(&operation)) {
        const bool query = api->kind == VhdlPslApiKind::assert_failed
            || api->kind == VhdlPslApiKind::is_covered
            || api->kind == VhdlPslApiKind::get_cover_assert
            || api->kind == VhdlPslApiKind::is_assert_covered;
        const bool set = api->kind == VhdlPslApiKind::set_cover_assert;
        const bool clear = api->kind == VhdlPslApiKind::clear_state;
        if ((!query && !set && !clear)
            || (query && (!api->destination || api->enable))
            || (set && (api->destination || !api->enable))
            || (clear && (api->destination || api->enable))) {
            process.pc = instruction;
            fail(process, "VHDL PSL API operation is invalid");
        }
        if (!vhdl_psl_api_hook) {
            process.pc = instruction;
            fail(process, "VHDL PSL API service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "VHDL PSL API boundary requires an executor");
        }
        std::optional<bool> enable;
        if (api->enable) {
            const auto value = process.executor->read_register(*api->enable, 1U);
            if (value.width() != 1U || value.is_logic9()
                || value.low_word().bval != 0U) {
                process.pc = instruction;
                fail(process, "VHDL PSL API enable value is not BOOLEAN");
            }
            enable = (value.low_word().aval & 1U) != 0U;
        }
        const auto result = vhdl_psl_api_hook(api->kind, enable);
        if (api->destination) {
            process.executor->write_register(
                *api->destination,
                PackedLogic4::from_aval_bval(
                    1U, result ? 1U : 0U, 0U));
        }
        return;
    }
    if (const auto* api
        = fsim::runtime::simir::operation_get_if<VhdlAssertApi>(&operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "VHDL assert API boundary requires an executor");
        }
        execute_vhdl_assert_api(process, *api);
        return;
    }
    if (const auto* api
        = fsim::runtime::simir::operation_get_if<VhdlReflectionApi>(&operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "VHDL reflection boundary requires an executor");
        }
        execute_vhdl_reflection_api(process, *api);
        return;
    }
    if (const auto* control
        = fsim::runtime::simir::operation_get_if<CoverageControl>(
            &operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "coverage control boundary requires an executor");
        }
        const auto decode = [&](const RegisterId id)
            -> std::optional<std::int32_t> {
            const auto value = process.executor->read_register(id, 32U);
            if (value.width() != 32U || value.is_logic9()) {
                return std::nullopt;
            }
            const auto word = value.low_word();
            if (word.bval != 0U) {
                return std::nullopt;
            }
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(word.aval));
        };
        const auto command = decode(control->command);
        const auto coverage_type = decode(control->coverage_type);
        const auto scope = decode(control->scope);
        auto status = static_cast<std::int32_t>(
            SystemVerilogCoverageStatus::error);
        if (command && coverage_type && scope) {
            CoverageControlEvent event;
            event.command = *command;
            event.coverage_type = *coverage_type;
            event.scope = *scope;
            event.selector = process.executor->read_string_register(
                control->selector);
            event.instance_context = control->instance_context;
            event.selector_is_instance = control->selector_is_instance;
            status = control_coverage(event);
        }
        process.executor->write_register(
            control->destination,
            PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(status), 0U));
        return;
    }
    if (const auto* access
        = fsim::runtime::simir::operation_get_if<CoverageAccess>(
            &operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "coverage access boundary requires an executor");
        }
        const auto value = process.executor->read_register(
            access->coverage_type, 32U);
        auto status = static_cast<std::int32_t>(
            SystemVerilogCoverageStatus::error);
        if (value.width() == 32U && !value.is_logic9()
            && value.low_word().bval == 0U) {
            CoverageAccessEvent event;
            event.kind = access->kind;
            event.coverage_type = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(value.low_word().aval));
            if (access->scope && access->selector) {
                const auto scope_value = process.executor->read_register(
                    *access->scope, 32U);
                if (scope_value.width() == 32U
                    && !scope_value.is_logic9()
                    && scope_value.low_word().bval == 0U) {
                    event.scope = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(
                            scope_value.low_word().aval));
                }
                event.selector = process.executor->read_string_register(
                    *access->selector);
                event.instance_context = access->instance_context;
                event.selector_is_instance = access->selector_is_instance;
            }
            if (access->filename) {
                event.filename = process.executor->read_string_register(
                    *access->filename);
            }
            if ((!access->scope && !access->selector)
                || (event.scope && !event.selector.empty())) {
                status = access_coverage(event);
            }
        }
        process.executor->write_register(
            access->destination,
            PackedLogic4::from_aval_bval(
                32U, static_cast<std::uint32_t>(status), 0U));
        return;
    }
    if (const auto* query
        = fsim::runtime::simir::operation_get_if<PlusArgSelect>(&operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "plusarg boundary requires an executor");
        }
        const auto prefix = process.executor->read_string_register(
            query->query);
        const std::string* selected = nullptr;
        for (const auto& argument : plusargs) {
            auto candidate = std::string_view { argument };
            if (candidate.starts_with('+')) {
                candidate.remove_prefix(1);
            }
            if (candidate.starts_with(prefix)) {
                selected = &argument;
                break;
            }
        }
        if (query->selected) {
            auto selected_text = std::string_view { };
            if (selected != nullptr) {
                selected_text = *selected;
                if (selected_text.starts_with('+')) {
                    selected_text.remove_prefix(1);
                }
            }
            process.executor->write_string_register(
                *query->selected, selected_text);
        }
        write_process_register(
            process,
            query->destination,
            PackedLogic4::from_aval_bval(
                32, selected != nullptr ? 1U : 0U, 0));
        return;
    }
    if (const auto* command
        = fsim::runtime::simir::operation_get_if<SystemCommand>(&operation)) {
        if (!system_command_hook) {
            process.pc = instruction;
            fail(process, "$system service is unavailable");
        }
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "$system boundary requires an executor");
        }
        std::optional<std::string> owned_text;
        if (command->command) {
            owned_text = process.executor->read_string_register(
                *command->command);
        }
        const auto status = system_command_hook(
            owned_text
                ? std::optional<std::string_view> { *owned_text }
                : std::nullopt);
        if (command->destination) {
            process.executor->write_register(
                *command->destination,
                PackedLogic4::from_aval_bval(
                    32, static_cast<std::uint32_t>(status), 0));
        }
        return;
    }
    if (const auto* control
        = fsim::runtime::simir::operation_get_if<VcdControl>(&operation)) {
        if (!vcd_control_hook || !process.executor) {
            process.pc = instruction;
            fail(process, "VCD control service is unavailable");
        }
        VcdControlEvent event;
        event.kind = control->kind;
        if (control->filename) {
            event.filename = process.executor->read_string_register(
                *control->filename);
        }
        if (control->value) {
            const auto converted = process.executor
                                       ->read_register(*control->value, 64U)
                                       .known_unsigned_value();
            if (!converted) {
                process.pc = instruction;
                fail(process,
                    "VCD control value must be a known unsigned 64-bit integer");
            }
            event.value = *converted;
        }
        event.selections = control->selections;
        event.scope = control->scope;
        event.time = scheduler.now();
        event.delta = scheduler.delta();
        vcd_control_hook(event);
        if (control->kind == VcdControlKind::variables
            || control->kind == VcdControlKind::ports) {
            const auto begin_kind
                = control->kind == VcdControlKind::variables
                ? VcdControlKind::begin_variables
                : VcdControlKind::begin_ports;
            scheduler.schedule(
                SchedulerPhase::postponed,
                process.program.id,
                [this, begin_kind](Scheduler& runtime) {
                    if (!vcd_control_hook) {
                        return;
                    }
                    VcdControlEvent begin;
                    begin.kind = begin_kind;
                    begin.time = runtime.now();
                    begin.delta = runtime.delta();
                    vcd_control_hook(begin);
                });
        }
        return;
    }
    if (const auto* control
        = fsim::runtime::simir::operation_get_if<CoverageDatabaseControl>(
            &operation)) {
        if (!coverage_database_control_hook || !process.executor) {
            process.pc = instruction;
            fail(process, "coverage database service is unavailable");
        }
        coverage_database_control_hook({ control->kind,
            process.executor->read_string_register(control->filename) });
        return;
    }
    if (const auto* queue
        = fsim::runtime::simir::operation_get_if<StochasticQueueOperation>(
            &operation)) {
        execute_stochastic_queue(process, *queue);
        return;
    }
    if (const auto* pla
        = fsim::runtime::simir::operation_get_if<PlaEvaluate>(&operation)) {
        execute_pla(process, *pla);
        return;
    }
    if (const auto* distribution = operation_get_if<RandomDistribution>(&operation)) {
        execute_random_distribution(process, *distribution); return;
    }
    if (const auto* format
        = fsim::runtime::simir::operation_get_if<TimeFormatControl>(
            &operation)) {
        if (!process.executor) {
            process.pc = instruction;
            fail(process, "$timeformat boundary requires an executor");
        }
        const auto decode = [&](const RegisterId id, const char* name) {
            const auto value = process.executor->read_register(id, 32);
            const auto word = value.low_word();
            if (word.bval != 0) {
                process.pc = instruction;
                fail(
                    process,
                    std::string { "$timeformat " } + name
                        + " must be a known 32-bit value");
            }
            return static_cast<std::uint32_t>(word.aval);
        };
        const auto units = static_cast<std::int32_t>(
            decode(format->units, "units"));
        const auto precision = decode(format->precision, "precision");
        const auto minimum_width = decode(
            format->minimum_width, "minimum width");
        const auto suffix = process.executor->read_string_register(
            format->suffix);
        if (units < -15 || units > 0
            || precision > maximum_string_bytes
            || minimum_width > maximum_string_bytes
            || suffix.size() > maximum_string_bytes) {
            process.pc = instruction;
            fail(
                process,
                "$timeformat arguments exceed their supported IEEE profile");
        }
        time_format.units = units;
        time_format.precision = precision;
        time_format.suffix = suffix;
        time_format.minimum_width = minimum_width;
        return;
    }
    if (const auto* point = fsim::runtime::simir::operation_get_if<DebugPoint>(&operation)) {
        const auto& actual_point = process.program.operations.debug_point(
            instruction, *point);
        clear_wait_timeout(process);
        process.current_source = actual_point.source;
        process.current_scope = process.program.operations.debug_scope(
            actual_point.scope);
        auto kind = ExecutionPointKind::statement;
        switch (actual_point.kind) {
        case DebugPointKind::statement:
            kind = ExecutionPointKind::statement;
            break;
        case DebugPointKind::call:
            kind = ExecutionPointKind::call;
            break;
        case DebugPointKind::wait:
            kind = ExecutionPointKind::wait;
            break;
        case DebugPointKind::assertion:
            kind = ExecutionPointKind::assertion;
            break;
        case DebugPointKind::process_entry:
            kind = ExecutionPointKind::process_entry;
            break;
        }
        notify_execution_point(
            process, instruction, kind, process.current_source,
            process.current_scope);
        if (scheduler.stop_requested()) {
            queue_current(process.program.id);
        }
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitRegion>(&operation)) {
        clear_wait_timeout(process);
        const auto current = scheduler.current_phase();
        if (!current || wait->phase <= *current) {
            process.pc = instruction;
            fail(process, "WaitRegion must target a later scheduler region");
        }
        process.status = ProcessStatus::waiting;
        process.queued = true;
        scheduler.schedule(
            wait->phase,
            process.program.id,
            [this, id = process.program.id](Scheduler&) {
                auto& state = get_process(id);
                state.queued = false;
                execute(id);
            });
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitFor>(&operation)) {
        clear_wait_timeout(process);
        auto delay = wait->delay;
        if (wait->source) {
            try {
                const auto payload = process.executor
                    ? process.executor->read_register(
                          *wait->source, wait->source_width)
                    : get_register(process, *wait->source);
                delay = normalized_dynamic_wait_delay(*wait, payload);
            } catch (const std::exception& error) {
                process.pc = instruction;
                fail(process, error.what());
            }
        }
        if (delay == 0) {
            process.status = ProcessStatus::waiting;
            if (process.program.postponed) {
                queue_next_delta(process.program.id);
            } else {
                process.queued = true;
                scheduler.schedule(
                    SchedulerPhase::inactive,
                    process.program.id,
                    [this, id = process.program.id](Scheduler&) {
                        auto& state = get_process(id);
                        state.queued = false;
                        execute(id);
                    });
            }
            notify_execution_point(
                process, instruction, ExecutionPointKind::process_suspend,
                process.current_source);
            return;
        }
        if (delay
            > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
            process.pc = instruction;
            fail(process, "simulation time overflow in WaitFor");
        }
        queue_at(process.program.id, scheduler.now() + delay);
        process.status = ProcessStatus::waiting;
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitOn>(&operation)) {
        if (wait->signals.empty() && !wait->timeout) {
            process.pc = instruction;
            fail(
                process,
                "WaitOn requires at least one signal or a timeout");
        }
        if (!wait->edges.empty()
            && wait->edges.size() != wait->signals.size()) {
            process.pc = instruction;
            fail(process, "WaitOn edge count must match its signal count");
        }
        if (!wait->timeout
            && (wait->timeout_result
                || wait->timeout_origin)) {
            process.pc = instruction;
            fail(
                process,
                "WaitOn timeout metadata requires a timeout");
        }
        if (wait->timeout_origin
            && !wait->timeout_result) {
            process.pc = instruction;
            fail(
                process,
                "WaitOn timeout rearm requires a result register");
        }
        if (wait->timeout_origin) {
            if (*wait->timeout_origin >= instruction) {
                process.pc = instruction;
                fail(
                    process,
                    "WaitOn timeout origin must precede its rearm");
            }
            const auto* origin = fsim::runtime::simir::operation_get_if<WaitOn>(
                &std::as_const(process.program.operations)[
                    *wait->timeout_origin]);
            if (origin == nullptr
                || !origin->timeout
                || origin->timeout_origin
                || origin->timeout != wait->timeout
                || origin->timeout_result
                    != wait->timeout_result
                || origin->signals != wait->signals
                || origin->edges != wait->edges) {
                process.pc = instruction;
                fail(
                    process,
                    "WaitOn timeout rearm does not match its origin");
            }
        }
        process.waiting_on_signal = true;
        process.status = ProcessStatus::waiting;
        process.dynamic_sensitivity.clear();
        process.dynamic_sensitivity.reserve(wait->signals.size());
        for (std::size_t index = 0; index < wait->signals.size(); ++index) {
            const auto source_signal = wait->signals[index];
            const auto& source = get_signal(source_signal);
            const auto identity = source.event_variable
                ? event_identities.at(source_signal)
                : std::optional<SignalId> { source_signal };
            const auto edge = wait->edges.empty() ? EdgeKind::any : wait->edges[index];
            switch (edge) {
            case EdgeKind::any:
                break;
            case EdgeKind::posedge:
            case EdgeKind::negedge:
                if (source.initial_value.width() != 1) {
                    process.pc = instruction;
                    fail(process, "WaitOn edge requires a scalar signal");
                }
                break;
            default:
                process.pc = instruction;
                fail(process, "WaitOn has an invalid edge kind");
            }
            if (identity) {
                process.dynamic_sensitivity.push_back({ *identity, edge });
            }
        }
        std::sort(
            process.dynamic_sensitivity.begin(),
            process.dynamic_sensitivity.end(),
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal
                    || (lhs.signal == rhs.signal
                        && lhs.edge < rhs.edge);
            });
        process.dynamic_sensitivity.erase(
            std::unique(
                process.dynamic_sensitivity.begin(),
                process.dynamic_sensitivity.end(),
                [](const Sensitivity& lhs, const Sensitivity& rhs) {
                    return lhs.signal == rhs.signal
                        && lhs.edge == rhs.edge;
                }),
            process.dynamic_sensitivity.end());
        for (const auto sensitivity : process.dynamic_sensitivity) {
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        if (wait->timeout) {
            if (wait->timeout_origin) {
                rearm_wait_timeout(
                    process,
                    instruction,
                    *wait->timeout_origin,
                    wait->timeout_result);
            } else {
                begin_wait_timeout(
                    process,
                    instruction,
                    *wait->timeout,
                    wait->timeout_result);
            }
        } else {
            clear_wait_timeout(process);
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitPla>(
            &operation)) {
        clear_wait_timeout(process);
        (void)get_container_object(wait->memory);
        process.waiting_on_signal = true;
        process.waiting_on_container = wait->memory;
        process.status = ProcessStatus::waiting;
        process.dynamic_sensitivity.clear();
        process.dynamic_sensitivity.reserve(wait->signals.size());
        for (const auto signal : wait->signals) {
            (void)get_signal(signal);
            process.dynamic_sensitivity.push_back(
                { signal, EdgeKind::any });
        }
        std::ranges::sort(
            process.dynamic_sensitivity,
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal;
            });
        process.dynamic_sensitivity.erase(
            std::ranges::unique(process.dynamic_sensitivity).begin(),
            process.dynamic_sensitivity.end());
        for (const auto sensitivity : process.dynamic_sensitivity) {
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        auto& memory_waiters = container_dynamic_fanout.at(wait->memory);
        if (std::ranges::find(memory_waiters, process.program.id)
            == memory_waiters.end()) {
            memory_waiters.push_back(process.program.id);
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* wait = fsim::runtime::simir::operation_get_if<WaitOrder>(
            &operation)) {
        if (wait->events.empty()) {
            process.pc = instruction;
            fail(process, "WaitOrder requires at least one event");
        }
        clear_wait_timeout(process);
        process.waiting_on_signal = true;
        process.status = ProcessStatus::waiting;
        process.wait_order_events.clear();
        process.wait_order_events.reserve(wait->events.size());
        process.wait_order_index = 0;
        process.wait_order_result = wait->result;
        process.dynamic_sensitivity.clear();
        process.dynamic_sensitivity.reserve(wait->events.size());
        for (const auto event : wait->events) {
            const auto& signal = get_signal(event);
            if (!signal.event_variable) {
                process.pc = instruction;
                fail(process, "WaitOrder requires named-event variables");
            }
            const auto identity = event_identities.at(event);
            process.wait_order_events.push_back(identity);
            if (identity) {
                process.dynamic_sensitivity.push_back(
                    { *identity, EdgeKind::any });
            }
        }
        std::ranges::sort(
            process.dynamic_sensitivity,
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
                return lhs.signal < rhs.signal;
            });
        process.dynamic_sensitivity.erase(
            std::ranges::unique(process.dynamic_sensitivity).begin(),
            process.dynamic_sensitivity.end());
        for (const auto sensitivity : process.dynamic_sensitivity) {
            dynamic_fanout[sensitivity.signal].push_back(
                { process.program.id, sensitivity.edge });
        }
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<WaitSensitivity>(operation)) {
        clear_wait_timeout(process);
        if (process.program.static_sensitivity.empty()) {
            process.pc = instruction;
            fail(process, "WaitSensitivity requires a static sensitivity list");
        }
        process.waiting_on_static = true;
        process.static_trigger_mask = 0U;
        process.status = ProcessStatus::waiting;
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<WaitForever>(operation)) {
        clear_wait_timeout(process);
        process.status = ProcessStatus::waiting;
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<Yield>(operation)) {
        clear_wait_timeout(process);
        process.status = ProcessStatus::waiting;
        queue_next_delta(process.program.id);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    const auto read_boundary_value = [&](const RegisterId id) {
        return process.executor
            ? process.executor->read_register(
                  id, ProcessExecutor::native_register_width)
            : get_register(process, id);
    };
    const auto read_boundary_handle = [&](const RegisterId id) {
        return process.executor->read_register(id, 64U);
    };
    if (const auto* time
        = fsim::runtime::simir::operation_get_if<VhdlEnvironmentTime>(
            &operation)) {
        const auto require = [&](const std::optional<RegisterId> value,
                                 const char* name) -> PackedLogic4 {
            if (!value) {
                throw std::invalid_argument {
                    std::string { "STD.ENV date/time operation is missing " }
                        + name
                };
            }
            return read_boundary_value(*value);
        };
        try {
            PackedLogic4 result;
            const auto now = [] {
                return std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            };
            switch (time->kind) {
            case VhdlEnvironmentTimeKind::current_local:
                result = pack_calendar(local_calendar_from_epoch(now()));
                break;
            case VhdlEnvironmentTimeKind::current_utc:
                result = pack_calendar(utc_calendar_from_epoch(now()));
                break;
            case VhdlEnvironmentTimeKind::current_epoch:
                result = real_value(now());
                break;
            case VhdlEnvironmentTimeKind::local_from_epoch:
                result = pack_calendar(local_calendar_from_epoch(
                    known_real(require(time->first, "TIMER"), "TIMER")));
                break;
            case VhdlEnvironmentTimeKind::utc_from_epoch:
                result = pack_calendar(utc_calendar_from_epoch(
                    known_real(require(time->first, "TIMER"), "TIMER")));
                break;
            case VhdlEnvironmentTimeKind::epoch_from_local:
                result = real_value(local_epoch_from_calendar(
                    unpack_calendar(require(time->first, "TREC"))));
                break;
            case VhdlEnvironmentTimeKind::local_from_utc_record:
                result = pack_calendar(local_calendar_from_epoch(
                    utc_epoch_from_calendar(
                        unpack_calendar(require(time->first, "TREC")))));
                break;
            case VhdlEnvironmentTimeKind::utc_from_local_record:
                result = pack_calendar(utc_calendar_from_epoch(
                    local_epoch_from_calendar(
                        unpack_calendar(require(time->first, "TREC")))));
                break;
            case VhdlEnvironmentTimeKind::add_seconds:
            case VhdlEnvironmentTimeKind::subtract_seconds:
            case VhdlEnvironmentTimeKind::reverse_subtract_seconds: {
                const auto record = unpack_calendar(
                    require(time->first, "TREC"));
                const auto delta = known_real(
                    require(time->second, "DELTA"), "DELTA");
                const auto subtract = time->kind
                    != VhdlEnvironmentTimeKind::add_seconds;
                result = pack_calendar(utc_calendar_from_epoch(
                    utc_epoch_from_calendar(record)
                        + (subtract ? -delta : delta)));
                break;
            }
            case VhdlEnvironmentTimeKind::difference_seconds: {
                const auto first = unpack_calendar(
                    require(time->first, "TR1"));
                const auto second = unpack_calendar(
                    require(time->second, "TR2"));
                result = real_value(
                    utc_epoch_from_calendar(first)
                    - utc_epoch_from_calendar(second));
                break;
            }
            case VhdlEnvironmentTimeKind::time_to_seconds: {
                const auto ticks = require(time->first, "TIME_VAL")
                    .known_unsigned_value();
                if (!ticks) {
                    throw std::invalid_argument {
                        "STD.ENV TIME_TO_SECONDS requires a known nonnegative TIME"
                    };
                }
                const auto seconds = static_cast<long double>(*ticks)
                    * static_cast<long double>(
                        time_format.resolution_femtoseconds)
                    / 1'000'000'000'000'000.0L;
                result = real_value(static_cast<double>(seconds));
                break;
            }
            case VhdlEnvironmentTimeKind::seconds_to_time: {
                const auto seconds = known_real(
                    require(time->first, "REAL_VAL"), "REAL_VAL");
                const auto ticks = static_cast<long double>(seconds)
                    * 1'000'000'000'000'000.0L
                    / static_cast<long double>(
                        time_format.resolution_femtoseconds);
                if (ticks < 0.0L
                    || ticks > static_cast<long double>(
                        std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument {
                        "STD.ENV SECONDS_TO_TIME result is outside TIME'LOW through TIME'HIGH"
                    };
                }
                const auto rounded = std::round(ticks);
                if (rounded > static_cast<long double>(
                        std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument {
                        "STD.ENV SECONDS_TO_TIME result is outside TIME'LOW through TIME'HIGH"
                    };
                }
                result = PackedLogic4::from_aval_bval(
                    64U, static_cast<std::uint64_t>(rounded), 0U);
                break;
            }
            }
            write_process_register(process, time->destination, result);
        } catch (const std::invalid_argument& error) {
            process.pc = instruction;
            fail(process, error.what());
        }
        return;
    }
    if (const auto* format
        = fsim::runtime::simir::operation_get_if<
            VhdlEnvironmentTimeToString>(&operation)) {
        try {
            const auto digits = read_boundary_value(
                format->fractional_digits).known_signed_value();
            if (!digits || *digits < 0 || *digits > 6) {
                throw std::invalid_argument {
                    "STD.ENV TO_STRING FRAC_DIGITS must be in 0 through 6"
                };
            }
            auto text = calendar_string(
                unpack_calendar(read_boundary_value(format->record)),
                static_cast<std::uint32_t>(*digits));
            if (process.executor) {
                process.executor->write_string_register(
                    format->destination, text);
            } else {
                get_string_register(process, format->destination)
                    = std::move(text);
            }
        } catch (const std::invalid_argument& error) {
            process.pc = instruction;
            fail(process, error.what());
        }
        return;
    }
    if (const auto* directory
        = fsim::runtime::simir::operation_get_if<
            VhdlEnvironmentDirectory>(&operation)) {
        const auto read_string = [&](const StringRegisterId id) {
            return process.executor
                ? process.executor->read_string_register(id)
                : get_string_register(process, id);
        };
        const auto write_string = [&](const StringRegisterId id,
                                      std::string value) {
            if (process.executor) {
                process.executor->write_string_register(id, value);
            } else {
                get_string_register(process, id) = std::move(value);
            }
        };
        const auto read_container = [&](const ContainerRegisterId id) {
            return process.executor
                ? process.executor->read_container_register(id)
                : read_container_register(process, id);
        };
        const auto write_container = [&](const ContainerRegisterId id,
                                         const ContainerValue& value) {
            if (process.executor) {
                process.executor->write_container_register(id, value);
            } else {
                get_container_register(process, id) = value;
            }
        };
        const auto write_result = [&](const std::uint64_t value) {
            if (!directory->result) {
                throw std::invalid_argument {
                    "STD.ENV directory operation is missing its result"
                };
            }
            const auto width = directory->kind
                    == VhdlEnvironmentDirectoryKind::item_exists
                    || directory->kind
                        == VhdlEnvironmentDirectoryKind::item_is_directory
                    || directory->kind
                        == VhdlEnvironmentDirectoryKind::item_is_file
                ? 1U : 3U;
            write_process_register(process, *directory->result,
                PackedLogic4::from_aval_bval(width, value, 0U));
        };
        const auto path_text = [&]() -> std::string {
            if (!directory->path) {
                throw std::invalid_argument {
                    "STD.ENV directory operation is missing PATH"
                };
            }
            return read_string(*directory->path);
        };
        const auto option = [&]() {
            if (!directory->option) {
                return false;
            }
            const auto known = read_boundary_value(*directory->option)
                .known_unsigned_value();
            if (!known || *known > 1U) {
                throw std::invalid_argument {
                    "STD.ENV directory option must be a known BOOLEAN"
                };
            }
            return *known != 0U;
        };
        try {
            using Kind = VhdlEnvironmentDirectoryKind;
            if (directory->kind == Kind::separator) {
                if (!directory->string_result) {
                    throw std::invalid_argument {
                        "STD.ENV DIR_SEPARATOR is missing its result"
                    };
                }
                write_string(*directory->string_result,
                    std::string(1U,
                        std::filesystem::path::preferred_separator));
                return;
            }
            if (directory->kind == Kind::get_working_directory) {
                if (!directory->string_result) {
                    throw std::invalid_argument {
                        "STD.ENV DIR_WORKINGDIR is missing its result"
                    };
                }
                write_string(*directory->string_result,
                    fsim::support::path_to_utf8(vhdl_working_directory));
                return;
            }
            if (directory->kind == Kind::close) {
                if (!directory->directory) {
                    throw std::invalid_argument {
                        "STD.ENV DIR_CLOSE is missing DIR"
                    };
                }
                auto value = read_container(*directory->directory);
                value.string_elements.clear();
                write_container(*directory->directory, value);
                return;
            }

            std::error_code error;
            const auto resolved = vhdl_directory_path(
                file_root, vhdl_working_directory, path_text(), error);
            if (directory->kind == Kind::item_exists
                || directory->kind == Kind::item_is_directory
                || directory->kind == Kind::item_is_file) {
                bool result = false;
                if (resolved) {
                    if (directory->kind == Kind::item_exists) {
                        result = std::filesystem::exists(*resolved, error);
                    } else if (directory->kind == Kind::item_is_directory) {
                        result = std::filesystem::is_directory(*resolved, error);
                    } else {
                        result = std::filesystem::is_regular_file(*resolved, error);
                    }
                }
                write_result(!error && result ? 1U : 0U);
                return;
            }
            if (directory->kind == Kind::open) {
                std::uint64_t status = 0U;
                if (!resolved) {
                    status = vhdl_access_denied(error) ? 3U : 4U;
                } else if (!std::filesystem::exists(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 3U : 1U;
                } else if (!std::filesystem::is_directory(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 3U : 2U;
                } else {
                    if (!directory->directory) {
                        throw std::invalid_argument {
                            "STD.ENV DIR_OPEN is missing DIR"
                        };
                    }
                    auto value = read_container(*directory->directory);
                    std::vector<std::string> items;
                    std::size_t text_bytes = 0U;
                    for (std::filesystem::directory_iterator iterator(
                             *resolved, error), end;
                         !error && iterator != end; iterator.increment(error)) {
                        auto item = fsim::support::path_to_utf8(
                            iterator->path().filename());
                        if (items.size() >= vhdl_directory_item_limit
                            || item.size() > vhdl_directory_text_limit
                            || text_bytes > vhdl_directory_text_limit
                                - item.size()) {
                            error = std::make_error_code(
                                std::errc::value_too_large);
                            break;
                        }
                        text_bytes += item.size();
                        items.push_back(std::move(item));
                    }
                    if (error) {
                        status = vhdl_access_denied(error) ? 3U : 4U;
                    } else {
                        std::sort(items.begin(), items.end());
                        value.elements.clear();
                        value.nested_elements.clear();
                        value.keys.clear();
                        value.string_keys.clear();
                        value.string_elements.clear();
                        value.string_elements.reserve(items.size() + 1U);
                        value.string_elements.push_back(
                            fsim::support::path_to_utf8(*resolved));
                        value.string_elements.insert(
                            value.string_elements.end(),
                            std::make_move_iterator(items.begin()),
                            std::make_move_iterator(items.end()));
                        validate_container_value(value);
                        write_container(*directory->directory, value);
                    }
                }
                write_result(status);
                return;
            }
            if (directory->kind == Kind::set_working_directory) {
                std::uint64_t status = 0U;
                if (!resolved) {
                    status = vhdl_access_denied(error) ? 3U : 4U;
                } else if (!std::filesystem::exists(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 3U : 1U;
                } else if (!std::filesystem::is_directory(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 3U : 2U;
                } else {
                    vhdl_working_directory = *resolved;
                }
                write_result(status);
                return;
            }
            if (directory->kind == Kind::create_directory) {
                std::uint64_t status = 0U;
                if (!resolved) {
                    status = vhdl_access_denied(error) ? 2U : 3U;
                } else if (std::filesystem::exists(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 2U : 1U;
                } else {
                    const bool created = option()
                        ? std::filesystem::create_directories(*resolved, error)
                        : std::filesystem::create_directory(*resolved, error);
                    if (error || !created) {
                        status = vhdl_access_denied(error) ? 2U : 3U;
                    }
                }
                write_result(status);
                return;
            }
            if (directory->kind == Kind::delete_directory) {
                std::uint64_t status = 0U;
                if (!resolved) {
                    status = vhdl_access_denied(error) ? 3U : 4U;
                } else if (*resolved == file_root
                    || vhdl_path_below_root(
                        *resolved, vhdl_working_directory)) {
                    // Preserve the configured sandbox root and the logical
                    // working-directory chain for subsequent operations.
                    status = 3U;
                } else if (!std::filesystem::is_directory(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 3U : 1U;
                } else if (option()) {
                    static_cast<void>(
                        std::filesystem::remove_all(*resolved, error));
                    if (error) {
                        status = vhdl_access_denied(error) ? 3U : 4U;
                    }
                } else if (!std::filesystem::remove(*resolved, error)) {
                    status = error == std::errc::directory_not_empty
                        ? 2U : vhdl_access_denied(error) ? 3U : 4U;
                }
                write_result(status);
                return;
            }
            if (directory->kind == Kind::delete_file) {
                std::uint64_t status = 0U;
                if (!resolved) {
                    status = vhdl_access_denied(error) ? 2U : 3U;
                } else if (!std::filesystem::is_regular_file(*resolved, error)) {
                    status = error && vhdl_access_denied(error) ? 2U : 1U;
                } else if (!std::filesystem::remove(*resolved, error)) {
                    status = vhdl_access_denied(error) ? 2U : 3U;
                }
                write_result(status);
                return;
            }
            throw std::invalid_argument {
                "STD.ENV directory operation kind is invalid"
            };
        } catch (const std::exception& error) {
            process.pc = instruction;
            fail(process, error.what());
        }
        return;
    }
    if (const auto* environment
        = fsim::runtime::simir::operation_get_if<
            VhdlEnvironmentGetenv>(&operation)) {
        const auto name = process.executor
            ? process.executor->read_string_register(environment->name)
            : get_string_register(process, environment->name);
        if (name.find('\0') != std::string::npos) {
            process.pc = instruction;
            fail(process,
                "STD.ENV GETENV name contains an embedded null character");
        }
        auto value = fsim::support::environment_variable(name)
            .value_or(std::string { });
        if (value.size() > maximum_string_bytes) {
            process.pc = instruction;
            fail(process,
                "STD.ENV GETENV result exceeds the bounded string limit");
        }
        if (process.executor) {
            process.executor->write_string_register(
                environment->destination, value);
        } else {
            get_string_register(process, environment->destination)
                = std::move(value);
        }
        return;
    }
    struct VhdlCallPathFrame {
        SourceLocation source;
        std::string scope;
    };
    const auto collect_vhdl_call_path = [&](const SourceLocation& source,
                                             const InternedString& scope) {
        std::vector<VhdlCallPathFrame> frames;
        frames.push_back(VhdlCallPathFrame { source, scope.str() });
        constexpr std::size_t maximum_call_path_depth = 256U;
        if (process.dynamic_call_stack.size() > maximum_call_path_depth - 1U) {
            process.pc = instruction;
            fail(process, "STD.ENV call path exceeds 256 frames");
        }
        for (auto returns = process.dynamic_call_stack.rbegin();
            returns != process.dynamic_call_stack.rend(); ++returns) {
            const DebugPoint* caller = nullptr;
            for (std::size_t cursor = *returns; cursor != 0U; --cursor) {
                const auto candidate = cursor - 1U;
                const auto* call = operation_get_if<Call>(
                    &process.program.operations[candidate]);
                if (call == nullptr || call->return_target != *returns) {
                    continue;
                }
                for (std::size_t debug_cursor = candidate;
                    debug_cursor != 0U; --debug_cursor) {
                    const auto debug_candidate = debug_cursor - 1U;
                    const auto* point = operation_get_if<DebugPoint>(
                        &process.program.operations[debug_candidate]);
                    if (point != nullptr) {
                        caller = &process.program.operations.debug_point(
                            debug_candidate, *point);
                        break;
                    }
                }
                break;
            }
            if (caller != nullptr) {
                frames.push_back(VhdlCallPathFrame {
                    caller->source,
                    process.program.operations.debug_scope(
                        caller->scope).str() });
            }
        }
        return frames;
    };
    if (const auto* get_call_path
        = fsim::runtime::simir::operation_get_if<
            VhdlEnvironmentGetCallPath>(&operation)) {
        try {
            const auto& type = process.program.container_register_types.at(
                get_call_path->destination);
            if (type.element_kind != ContainerElementKind::Aggregate
                || type.element_types.size() != 4U
                || type.member_names != std::vector<std::string> {
                    "name", "file_name", "file_path", "file_line" }) {
                throw std::invalid_argument {
                    "STD.ENV GET_CALL_PATH destination has an invalid profile" };
            }
            const auto frames = collect_vhdl_call_path(
                get_call_path->source, get_call_path->scope);
            auto value = default_container_value(type);
            resize_container_value(value, frames.size());
            for (std::size_t index = 0; index < frames.size(); ++index) {
                auto& element = value.nested_elements.at(index);
                if (element.nested_elements.size() != 4U
                    || element.nested_elements[0].string_elements.size() != 1U
                    || element.nested_elements[1].string_elements.size() != 1U
                    || element.nested_elements[2].string_elements.size() != 1U
                    || element.nested_elements[3].elements.size() != 1U) {
                    throw std::invalid_argument {
                        "STD.ENV GET_CALL_PATH storage profile is invalid" };
                }
                const auto path = std::filesystem::path {
                    frames[index].source.path.str() }
                    .lexically_normal();
                element.nested_elements[0].string_elements[0]
                    = frames[index].scope.empty()
                    ? process.program.name : frames[index].scope;
                element.nested_elements[1].string_elements[0]
                    = path.filename().generic_string();
                element.nested_elements[2].string_elements[0]
                    = path.generic_string();
                element.nested_elements[3].elements[0]
                    = PackedLogic4::from_aval_bval(
                        64U, frames[index].source.line, 0U);
            }
            validate_container_value(value);
            if (process.executor) {
                process.executor->write_container_register(
                    get_call_path->destination, value);
            } else {
                set_container_register_storage(
                    process, get_call_path->destination,
                    std::make_shared<ContainerValue>(std::move(value)));
            }
        } catch (const std::exception& error) {
            process.pc = instruction;
            fail(process, error.what());
        }
        return;
    }
    if (const auto* call_path
        = fsim::runtime::simir::operation_get_if<
            VhdlEnvironmentCallPath>(&operation)) {
        struct Frame {
            SourceLocation source;
            std::string scope;
        };
        std::vector<Frame> frames;
        if (call_path->source_value) {
            const auto value = process.executor
                ? process.executor->read_container_register(
                    *call_path->source_value)
                : read_container_register(process, *call_path->source_value);
            if (value.type.element_kind != ContainerElementKind::Aggregate) {
                process.pc = instruction;
                fail(process,
                    "STD.ENV TO_STRING call path has an invalid profile");
            }
            std::size_t first = 0U;
            std::size_t count = value.nested_elements.size();
            if (call_path->source_index) {
                const auto selected = read_boundary_value(
                    *call_path->source_index).known_signed_value();
                if (!selected || *selected < 0
                    || static_cast<std::uint64_t>(*selected)
                        >= value.nested_elements.size()) {
                    process.pc = instruction;
                    fail(process,
                        "STD.ENV TO_STRING call-path index is out of range");
                }
                first = static_cast<std::size_t>(*selected);
                count = 1U;
            }
            frames.reserve(count);
            for (std::size_t index = first; index < first + count; ++index) {
                const auto& element = value.nested_elements[index];
                if (element.nested_elements.size() != 4U
                    || element.nested_elements[0].string_elements.size() != 1U
                    || element.nested_elements[2].string_elements.size() != 1U
                    || element.nested_elements[3].elements.size() != 1U) {
                    process.pc = instruction;
                    fail(process,
                        "STD.ENV TO_STRING call path storage is invalid");
                }
                const auto line = element.nested_elements[3]
                    .elements[0].known_unsigned_value();
                if (!line || *line == 0U
                    || *line > std::numeric_limits<std::uint32_t>::max()) {
                    process.pc = instruction;
                    fail(process,
                        "STD.ENV TO_STRING call path line is invalid");
                }
                frames.push_back(Frame {
                    SourceLocation {
                        element.nested_elements[2].string_elements[0],
                        static_cast<std::uint32_t>(*line), 1U },
                    element.nested_elements[0].string_elements[0] });
            }
        } else {
            const auto current = collect_vhdl_call_path(
                call_path->source, call_path->scope);
            frames.reserve(current.size());
            for (const auto& frame : current) {
                frames.push_back(Frame { frame.source, frame.scope });
            }
        }
        const auto separator = process.executor
            ? process.executor->read_string_register(call_path->separator)
            : get_string_register(process, call_path->separator);
        std::string text;
        for (std::size_t index = 0; index < frames.size(); ++index) {
            const auto& frame = frames[index];
            const auto path = std::filesystem::path { frame.source.path.str() }
                .lexically_normal().generic_string();
            const auto name = frame.scope.empty()
                ? process.program.name : frame.scope;
            const auto line = std::to_string(frame.source.line);
            const auto required = name.size() + path.size() + line.size()
                + 4U + (index == 0U ? 0U : separator.size());
            if (required > maximum_string_bytes
                || text.size() > maximum_string_bytes - required) {
                process.pc = instruction;
                fail(process,
                    "STD.ENV call-path text exceeds the bounded string limit");
            }
            if (index != 0U) {
                text += separator;
            }
            text += name;
            text += " [";
            text += path;
            text += ':';
            text += line;
            text += ']';
        }
        if (process.executor) {
            process.executor->write_string_register(
                call_path->destination, text);
        } else {
            get_string_register(process, call_path->destination)
                = std::move(text);
        }
        return;
    }
    if (const auto* class_allocate = fsim::runtime::simir::operation_get_if<ClassAllocate>(&operation)) {
        if (!class_allocate_hook) {
            fail(process, "class allocation service is unavailable");
        }
        std::vector<PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        actuals.reserve(class_allocate->constructor_actuals.size());
        string_actuals.reserve(class_allocate->constructor_actuals.size());
        for (std::size_t index = 0;
            index < class_allocate->constructor_actuals.size(); ++index) {
            const auto actual = class_allocate->constructor_actuals[index];
            const auto string_actual = !class_allocate->constructor_actual_kinds.empty()
                && class_allocate->constructor_actual_kinds[index] == 1U;
            actuals.push_back(
                string_actual ? PackedLogic4(64) : read_boundary_value(actual));
            string_actuals.push_back(
                string_actual
                    ? process.executor->read_string_register(actual)
                    : std::string { });
        }
        const auto handle = class_allocate_hook(
            process.program.name,
            class_allocate->specialization_identity,
            class_allocate->declared_type,
            actuals,
            string_actuals,
            class_allocate->constructor_actual_names);
        write_process_register(
            process,
            class_allocate->destination,
            PackedLogic4::from_aval_bval(64, handle, 0));
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassPropertyRead>(
            &operation)) {
        if (!class_property_read_hook) {
            fail(process, "class property service is unavailable");
        }
        const auto handle = read_boundary_handle(
            property->receiver)
                                .low_word()
                                .aval;
        write_process_register(
            process,
            property->destination,
            resize_class_value(
                class_property_read_hook(handle, property->property_identity),
                property->width));
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassPropertyWrite>(
            &operation)) {
        if (!class_property_write_hook) {
            fail(process, "class property service is unavailable");
        }
        class_property_write_hook(
            read_boundary_handle(property->receiver).low_word().aval,
            property->property_identity,
            read_boundary_value(property->source));
        return;
    }
    if (const auto* method = fsim::runtime::simir::operation_get_if<ClassMethodCall>(
            &operation)) {
        if (!class_method_call_hook) {
            fail(process, "class method service is unavailable");
        }
        std::vector<PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        actuals.reserve(method->actuals.size());
        string_actuals.reserve(method->actuals.size());
        for (std::size_t index = 0; index < method->actuals.size(); ++index) {
            const auto actual = method->actuals[index];
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            actuals.push_back(
                string_actual ? PackedLogic4(64) : read_boundary_value(actual));
            string_actuals.push_back(
                string_actual
                    ? process.executor->read_string_register(actual)
                    : std::string { });
        }
        const auto handle = read_boundary_handle(
            method->receiver)
                                .low_word()
                                .aval;
        write_process_register(
            process,
            method->destination,
            resize_class_value(
                class_method_call_hook(
                    handle,
                    method->method_identity,
                    actuals,
                    string_actuals,
                    method->actual_names,
                    method->actual_directions,
                    method->inline_constraints,
                    method->virtual_dispatch),
                method->result_width));
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            if (string_actual) {
                process.executor->write_string_register(
                    method->actuals[index], string_actuals[index]);
            } else {
                write_process_register(process, method->actuals[index], actuals[index]);
            }
        }
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassStaticPropertyRead>(
            &operation)) {
        if (!class_static_property_read_hook) {
            fail(process, "class static property service is unavailable");
        }
        write_process_register(
            process,
            property->destination,
            resize_class_value(
                class_static_property_read_hook(property->property_identity),
                property->width));
        return;
    }
    if (const auto* property = fsim::runtime::simir::operation_get_if<ClassStaticPropertyWrite>(
            &operation)) {
        if (!class_static_property_write_hook) {
            fail(process, "class static property service is unavailable");
        }
        class_static_property_write_hook(
            property->property_identity,
            read_boundary_value(property->source));
        return;
    }
    if (const auto* method = fsim::runtime::simir::operation_get_if<ClassStaticMethodCall>(
            &operation)) {
        if (!class_static_method_call_hook) {
            fail(process, "class static method service is unavailable");
        }
        std::vector<PackedLogic4> actuals;
        std::vector<std::string> string_actuals;
        actuals.reserve(method->actuals.size());
        string_actuals.reserve(method->actuals.size());
        for (std::size_t index = 0; index < method->actuals.size(); ++index) {
            const auto actual = method->actuals[index];
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            actuals.push_back(
                string_actual ? PackedLogic4(64) : read_boundary_value(actual));
            string_actuals.push_back(
                string_actual
                    ? process.executor->read_string_register(actual)
                    : std::string { });
        }
        write_process_register(
            process,
            method->destination,
            resize_class_value(
                class_static_method_call_hook(
                    method->method_identity,
                    actuals,
                    string_actuals,
                    method->actual_names,
                    method->actual_directions),
                method->result_width));
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto string_actual = !method->actual_kinds.empty()
                && method->actual_kinds[index] == 1U;
            if (string_actual) {
                process.executor->write_string_register(
                    method->actuals[index], string_actuals[index]);
            } else {
                write_process_register(process, method->actuals[index], actuals[index]);
            }
        }
        return;
    }
    if (handle_synchronization_boundary(process, instruction, operation)) {
        return;
    }
    if (handle_process_boundary(process, instruction, operation)) {
        return;
    }
    if (handle_fork_boundary(process, instruction, operation)) {
        return;
    }
    const auto capture_simulator_status = [&](const auto& control) {
        if (!control.status) {
            simulator_status.reset();
            return;
        }
        const auto value = read_boundary_value(
            *control.status).known_signed_value();
        if (!value) {
            throw InterpreterError {
                process.program.id,
                instruction,
                "STD.ENV simulator status is not a known signed INTEGER"
            };
        }
        simulator_status = *value;
    };
    if (const auto* pause
        = fsim::runtime::simir::operation_get_if<Pause>(&operation)) {
        capture_simulator_status(*pause);
        clear_wait_timeout(process);
        scheduler.request_stop();
        queue_current(process.program.id);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (const auto* stop
        = fsim::runtime::simir::operation_get_if<Stop>(&operation)) {
        capture_simulator_status(*stop);
        clear_wait_timeout(process);
        process.halted = true;
        stopped_by_design = true;
        scheduler.request_stop();
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        return;
    }
    if (fsim::runtime::simir::operation_holds<Halt>(operation)) {
        const auto halt = *fsim::runtime::simir::operation_get_if<Halt>(
            &operation);
        clear_wait_timeout(process);
        notify_execution_point(
            process, instruction, ExecutionPointKind::process_suspend,
            process.current_source);
        if (halt.program_exit) {
            exit_program(process);
        } else if (process.fork_parent) {
            complete_fork_child(process);
        } else {
            complete_process(process, ProcessStatus::finished);
            complete_program_process(process);
        }
        return;
    }

    process.pc = instruction;
    fail(
        process,
        "executor returned at an operation that is not a kernel boundary");
}

} // namespace fsim::runtime::simir
