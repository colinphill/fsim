// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <sstream>

namespace fsim::compiler::llvm_detail {

namespace {

    [[nodiscard]] std::string instruction_error(
        const runtime::simir::Process& process,
        const std::size_t instruction,
        const std::string_view message)
    {
        std::ostringstream result;
        result << "cannot JIT SimIR process " << process.id << " ('" << process.name
               << "'), instruction " << instruction << ": " << message;
        return result.str();
    }

} // namespace

void validate_process_shape(
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds)
{
    using namespace runtime::simir;
    if (process.operations.empty())
        throw LlvmJitError { "cannot JIT an empty SimIR process" };
    if (process.operations.size()
        > std::numeric_limits<InstructionIndex>::max())
        throw LlvmJitUnsupportedError {
            "SimIR process has too many instructions for the resumable JIT ABI"
        };
    if (process.register_count > std::numeric_limits<RegisterId>::max())
        throw LlvmJitUnsupportedError {
            "SimIR process has too many registers for the JIT ABI"
        };
    if (!process.register_value_kinds.empty()
        && process.register_value_kinds.size() != process.register_count)
        throw LlvmJitError {
            "SimIR register value-domain metadata count does not match "
            "register_count"
        };
    if (!signal_value_kinds.empty()
        && signal_value_kinds.size() != signal_widths.size())
        throw LlvmJitError {
            "SimIR signal value-domain metadata count does not match "
            "signal_widths"
        };
    if (const auto error = validate_expression_profile_metadata(
            process.expression_profiles))
        throw LlvmJitError { *error };
}

bool supports_wide_register_operation(
    const runtime::simir::Operation& stored,
    const std::span<const std::uint32_t> register_widths)
{
    using namespace runtime::simir;
    return visit_operation(
        [&](const auto& operation) {
            using OperationType = std::decay_t<decltype(operation)>;
            if constexpr (
                std::is_same_v<OperationType, LoadConstant>
                || std::is_same_v<OperationType, CopyRegister>
                || std::is_same_v<OperationType, ConvertToTwoState>
                || std::is_same_v<OperationType, UnaryNot>
                || std::is_same_v<OperationType, LogicalNot>
                || std::is_same_v<OperationType, LogicalBinary>
                || std::is_same_v<OperationType, Reduction>
                || std::is_same_v<OperationType, CountOnes>
                || std::is_same_v<OperationType, CountBits>
                || std::is_same_v<OperationType, Binary>
                || std::is_same_v<OperationType, Extract>
                || std::is_same_v<OperationType, Insert>
                || std::is_same_v<OperationType, Concatenate>
                || std::is_same_v<OperationType, ConditionalSelect>
                || std::is_same_v<OperationType, Shift>
                || std::is_same_v<OperationType, DynamicExtract>
                || std::is_same_v<OperationType, DynamicPartSelect>
                || std::is_same_v<OperationType, DynamicInsert>
                || std::is_same_v<OperationType, DynamicPartInsert>
                || std::is_same_v<OperationType, StringMethod>
                || std::is_same_v<OperationType, PlusArgSelect>
                || std::is_same_v<OperationType, SystemCommand>
                || std::is_same_v<OperationType, VcdControl>
                || std::is_same_v<OperationType, SystemVerilogMath>
                || std::is_same_v<OperationType, FileScan>
                || std::is_same_v<OperationType, FileWriteFormatted>
                || std::is_same_v<OperationType, CallableFramePush>
                || std::is_same_v<OperationType, CallableFramePop>
                || std::is_same_v<OperationType, CoverageSample>
                || std::is_same_v<OperationType, CoverageQuery>
                || std::is_same_v<OperationType, ReadSignal>
                || std::is_same_v<OperationType, WriteBlocking>
                || std::is_same_v<OperationType, WriteBlockingSlice>
                || std::is_same_v<OperationType, WriteBlockingDynamicSlice>
                || std::is_same_v<
                    OperationType,
                    WriteBlockingDynamicPartSlice>
                || std::is_same_v<OperationType, WriteUpdate>
                || std::is_same_v<OperationType, WriteUpdateSlice>
                || std::is_same_v<OperationType, WriteUpdateDynamicSlice>
                || std::is_same_v<
                    OperationType,
                    WriteUpdateDynamicPartSlice>
                || std::is_same_v<OperationType, WriteAfter>
                || std::is_same_v<OperationType, WriteAfterSlice>
                || std::is_same_v<OperationType, WriteAfterDynamicSlice>
                || std::is_same_v<
                    OperationType,
                    WriteAfterDynamicPartSlice>
                || std::is_same_v<OperationType, WriteInertial>
                || std::is_same_v<OperationType, WriteInertialSlice>
                || std::is_same_v<OperationType, WriteInertialDynamicSlice>
                || std::is_same_v<
                    OperationType,
                    WriteInertialDynamicPartSlice>
                || std::is_same_v<OperationType, ForceSignalSlice>
                || std::is_same_v<OperationType, ReleaseSignalSlice>) {
                return true;
            } else if constexpr (std::is_same_v<OperationType, ContainerRead>) {
                return operation.string_index
                    || register_widths[operation.destination] <= 64;
            } else if constexpr (std::is_same_v<OperationType, ContainerWrite>) {
                return operation.string_index
                    || register_widths[operation.source] <= 64;
            }
            return false;
        },
        stored);
}

[[noreturn]] void reject(
    const runtime::simir::Process& process,
    const std::size_t instruction,
    const std::string_view message)
{
    throw LlvmJitError(instruction_error(process, instruction, message));
}

[[noreturn]] void reject_unsupported(
    const runtime::simir::Process& process,
    const std::size_t instruction,
    const std::string_view message)
{
    throw LlvmJitUnsupportedError(
        instruction_error(process, instruction, message));
}

void validate_fork_operation(
    const runtime::simir::Process& process,
    const std::size_t instruction,
    const runtime::simir::Fork& operation)
{
    if (instruction + 1 >= process.operations.size()) {
        reject(
            process, instruction,
            "fork parent continuation is outside the operation stream");
    }
    std::set<runtime::simir::InstructionIndex> branches;
    for (const auto branch : operation.branches) {
        if (branch >= process.operations.size()) {
            reject(process, instruction, "fork branch target is out of range");
        }
        if (branch <= instruction + 1) {
            reject(
                process, instruction,
                "fork branch must follow its parent continuation");
        }
        if (!branches.insert(branch).second) {
            reject(process, instruction, "fork branch target is duplicated");
        }
    }
    if (operation.join != runtime::simir::ForkJoinKind::all
        && operation.join != runtime::simir::ForkJoinKind::any
        && operation.join != runtime::simir::ForkJoinKind::none) {
        reject(process, instruction, "fork has an invalid join kind");
    }
}

[[nodiscard]] bool is_resume_boundary(
    const runtime::simir::Operation& operation) noexcept
{
    using namespace runtime::simir;
    return fsim::runtime::simir::operation_holds<WaitFor>(operation)
        || fsim::runtime::simir::operation_holds<CoverageSample>(operation)
        || fsim::runtime::simir::operation_holds<CoverageQuery>(operation)
        || fsim::runtime::simir::operation_holds<SystemCommand>(operation)
        || fsim::runtime::simir::operation_holds<VcdControl>(operation)
        || fsim::runtime::simir::operation_holds<StochasticQueueOperation>(
            operation)
        || fsim::runtime::simir::operation_holds<PlaEvaluate>(operation)
        || fsim::runtime::simir::operation_holds<TimeFormatControl>(operation)
        || fsim::runtime::simir::operation_holds<WaitRegion>(operation)
        || fsim::runtime::simir::operation_holds<WaitOn>(operation)
        || fsim::runtime::simir::operation_holds<WaitPla>(operation)
        || fsim::runtime::simir::operation_holds<WaitOrder>(operation)
        || fsim::runtime::simir::operation_holds<EventTriggered>(operation)
        || fsim::runtime::simir::operation_holds<EventAlias>(operation)
        || fsim::runtime::simir::operation_holds<WaitSensitivity>(operation)
        || fsim::runtime::simir::operation_holds<WaitForever>(operation)
        || fsim::runtime::simir::operation_holds<Yield>(operation)
        || fsim::runtime::simir::operation_holds<Fork>(operation)
        || fsim::runtime::simir::operation_holds<ForkEnd>(operation)
        || fsim::runtime::simir::operation_holds<WaitFork>(operation)
        || fsim::runtime::simir::operation_holds<DisableFork>(operation)
        || fsim::runtime::simir::operation_holds<DisableBlock>(operation)
        || fsim::runtime::simir::operation_holds<ProcessSelf>(operation)
        || fsim::runtime::simir::operation_holds<ProcessStatusQuery>(
            operation)
        || fsim::runtime::simir::operation_holds<ProcessCompleted>(
            operation)
        || fsim::runtime::simir::operation_holds<ProcessAwait>(operation)
        || fsim::runtime::simir::operation_holds<ProcessKill>(operation)
        || fsim::runtime::simir::operation_holds<ProcessSuspend>(operation)
        || fsim::runtime::simir::operation_holds<ProcessResume>(operation)
        || fsim::runtime::simir::operation_holds<ProcessGetRandState>(operation)
        || fsim::runtime::simir::operation_holds<ProcessSetRandState>(operation)
        || fsim::runtime::simir::operation_holds<ProcessSrandom>(operation)
        || fsim::runtime::simir::operation_holds<MailboxCreate>(operation)
        || fsim::runtime::simir::operation_holds<MailboxPut>(operation)
        || fsim::runtime::simir::operation_holds<MailboxGet>(operation)
        || fsim::runtime::simir::operation_holds<MailboxNum>(operation)
        || fsim::runtime::simir::operation_holds<SemaphoreCreate>(operation)
        || fsim::runtime::simir::operation_holds<SemaphoreGet>(operation)
        || fsim::runtime::simir::operation_holds<SemaphorePut>(operation)
        || fsim::runtime::simir::operation_holds<Pause>(operation)
        || fsim::runtime::simir::operation_holds<ClassAllocate>(operation)
        || fsim::runtime::simir::operation_holds<ClassPropertyRead>(operation)
        || fsim::runtime::simir::operation_holds<ClassPropertyWrite>(operation)
        || fsim::runtime::simir::operation_holds<ClassMethodCall>(operation)
        || fsim::runtime::simir::operation_holds<ClassStaticPropertyRead>(
            operation)
        || fsim::runtime::simir::operation_holds<ClassStaticPropertyWrite>(
            operation)
        || fsim::runtime::simir::operation_holds<ClassStaticMethodCall>(
            operation);
}

[[nodiscard]] bool valid_symbol(const std::string_view symbol) noexcept
{
    if (symbol.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(symbol.front());
    if (std::isalpha(first) == 0 && symbol.front() != '_') {
        return false;
    }
    return std::all_of(
        symbol.begin() + 1, symbol.end(), [](const char value) {
            const auto character = static_cast<unsigned char>(value);
            return std::isalnum(character) != 0 || value == '_';
        });
}

std::optional<std::string> validate_string_method_metadata(
    const runtime::simir::StringMethod& operation,
    const runtime::simir::Process& process,
    std::vector<PackedRegisterValidation>& registers)
{
    using namespace runtime::simir;
    const auto string_register = [&](const StringRegisterId id) {
        return id < process.string_register_count;
    };
    const auto kind = operation.operation;
    if (!string_register(operation.source))
        return "StringMethod source string register is out of range";
    if (kind > StringMethodOperator::format_time)
        return "StringMethod kind is invalid";
    const auto formatted_width = formatted_value_width(
        operation.format, operation.scalar_kind);
    if (kind == StringMethodOperator::format_packed && !formatted_width)
        return "StringMethod format metadata is inconsistent";
    if (kind != StringMethodOperator::format_packed
        && operation.scalar_kind != runtime::SystemVerilogScalarKind::None)
        return "StringMethod scalar metadata is unexpected";
    const bool string_result = kind == StringMethodOperator::toupper
        || kind == StringMethodOperator::tolower
        || kind == StringMethodOperator::substr;
    const bool integer_result = kind == StringMethodOperator::getc
        || kind == StringMethodOperator::compare
        || kind == StringMethodOperator::icompare
        || (kind >= StringMethodOperator::atoi
            && kind <= StringMethodOperator::atobin);
    if (string_result && !string_register(operation.string_destination))
        return "StringMethod destination string register is out of range";
    if (integer_result)
        registers.push_back({ operation.destination, 32U, true });
    if (kind == StringMethodOperator::atoreal)
        registers.push_back({ operation.destination, 64U, true });
    if (kind == StringMethodOperator::getc || kind == StringMethodOperator::putc
        || kind == StringMethodOperator::substr
        || kind == StringMethodOperator::format_packed
        || (kind >= StringMethodOperator::itoa
            && kind <= StringMethodOperator::realtoa))
        registers.push_back({ operation.first,
            kind == StringMethodOperator::format_packed
                ? *formatted_width
                : kind == StringMethodOperator::realtoa ? 64U
                                                        : 32U,
            false });
    if (kind == StringMethodOperator::compare
        || kind == StringMethodOperator::icompare
        || kind == StringMethodOperator::format_string) {
        if (!string_register(operation.argument))
            return "StringMethod argument string register is out of range";
    }
    if (kind == StringMethodOperator::putc || kind == StringMethodOperator::substr
        || kind == StringMethodOperator::format_packed)
        registers.push_back({ operation.second, 32U, false });
    return std::nullopt;
}

std::optional<std::string> validate_scalar_binary_metadata(
    const runtime::simir::SystemVerilogScalarBinary& operation,
    std::vector<PackedRegisterValidation>& registers)
{
    const auto valid_kind = [](const auto kind) {
        return kind >= runtime::SystemVerilogScalarKind::ShortReal
            && kind <= runtime::SystemVerilogScalarKind::Chandle;
    };
    if (operation.operation
            > runtime::SystemVerilogScalarBinaryOperator::GreaterEqual
        || !valid_kind(operation.lhs_kind)
        || !valid_kind(operation.rhs_kind))
        return "SystemVerilogScalarBinary metadata is invalid";
    const bool comparison = operation.operation
        >= runtime::SystemVerilogScalarBinaryOperator::Equal;
    if ((!comparison && !valid_kind(operation.result_kind))
        || (comparison
            && operation.result_kind
                != runtime::SystemVerilogScalarKind::None))
        return "SystemVerilogScalarBinary result kind is invalid";
    const auto width = [](const auto kind) {
        return kind == runtime::SystemVerilogScalarKind::ShortReal ? 32U : 64U;
    };
    registers.push_back({ operation.lhs, width(operation.lhs_kind), false });
    registers.push_back({ operation.rhs, width(operation.rhs_kind), false });
    registers.push_back({ operation.destination,
        comparison ? 1U : width(operation.result_kind),
        true });
    return std::nullopt;
}

std::optional<std::string> validate_scalar_math_metadata(
    const runtime::simir::SystemVerilogMath& operation,
    std::vector<PackedRegisterValidation>& registers)
{
    using runtime::SystemVerilogMathFunction;
    using runtime::SystemVerilogScalarKind;
    if (operation.function > SystemVerilogMathFunction::Realtime) {
        return "SystemVerilogMath metadata is invalid";
    }
    const bool time_query
        = operation.function >= SystemVerilogMathFunction::Time;
    if (time_query) {
        if (operation.first_width != 0 || operation.second_width != 0
            || operation.first_kind != SystemVerilogScalarKind::None
            || operation.second_kind != SystemVerilogScalarKind::None
            || operation.time_unit_femtoseconds == 0
            || operation.time_precision_femtoseconds == 0
            || operation.time_unit_femtoseconds
                    % operation.time_precision_femtoseconds
                != 0) {
            return "SystemVerilog time-query metadata is invalid";
        }
        const auto result_width
            = operation.function == SystemVerilogMathFunction::Stime
            ? 32U
            : 64U;
        registers.push_back(
            { operation.destination, result_width, true });
        return std::nullopt;
    }
    if (operation.first_width == 0) {
        return "SystemVerilogMath metadata is invalid";
    }
    const auto valid_operand = [](const auto kind, const auto width) {
        if (kind == SystemVerilogScalarKind::None)
            return width != 0;
        if (kind == SystemVerilogScalarKind::ShortReal)
            return width == 32U;
        return (kind == SystemVerilogScalarKind::Real
                   || kind == SystemVerilogScalarKind::Realtime
                   || kind == SystemVerilogScalarKind::Time)
            && width == 64U;
    };
    const bool binary = operation.function == SystemVerilogMathFunction::Pow
        || operation.function == SystemVerilogMathFunction::Atan2
        || operation.function == SystemVerilogMathFunction::Hypot;
    if (!valid_operand(operation.first_kind, operation.first_width)
        || binary != (operation.second_width != 0)
        || (binary
            && !valid_operand(operation.second_kind, operation.second_width))) {
        return "SystemVerilogMath operand metadata is invalid";
    }
    const bool real_first = operation.first_kind == SystemVerilogScalarKind::Real
        || operation.first_kind == SystemVerilogScalarKind::Realtime;
    switch (operation.function) {
    case SystemVerilogMathFunction::Rtoi:
        if (!real_first
            && operation.first_kind != SystemVerilogScalarKind::ShortReal)
            return "$rtoi requires a real operand";
        break;
    case SystemVerilogMathFunction::Itor:
        if (operation.first_kind != SystemVerilogScalarKind::None
            || operation.first_width != 32U)
            return "$itor requires a 32-bit integral operand";
        break;
    case SystemVerilogMathFunction::BitsToReal:
        if (operation.first_kind != SystemVerilogScalarKind::None
            || operation.first_width != 64U)
            return "$bitstoreal requires a 64-bit packed operand";
        break;
    case SystemVerilogMathFunction::RealToBits:
        if (!real_first)
            return "$realtobits requires a real operand";
        break;
    case SystemVerilogMathFunction::BitsToShortReal:
        if (operation.first_kind != SystemVerilogScalarKind::None
            || operation.first_width != 32U)
            return "$bitstoshortreal requires a 32-bit packed operand";
        break;
    case SystemVerilogMathFunction::ShortRealToBits:
        if (operation.first_kind != SystemVerilogScalarKind::ShortReal)
            return "$shortrealtobits requires a shortreal operand";
        break;
    default:
        break;
    }
    const auto result_width = operation.function == SystemVerilogMathFunction::Rtoi
            || operation.function == SystemVerilogMathFunction::BitsToShortReal
            || operation.function == SystemVerilogMathFunction::ShortRealToBits
        ? 32U
        : 64U;
    registers.push_back(
        { operation.first, operation.first_width, false });
    if (binary) {
        registers.push_back(
            { operation.second, operation.second_width, false });
    }
    registers.push_back(
        { operation.destination, result_width, true });
    return std::nullopt;
}

std::optional<std::string> validate_file_scan_metadata(
    const runtime::simir::FileScan& operation,
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths,
    const std::span<const runtime::simir::ValueKind> signal_value_kinds,
    std::vector<PackedRegisterValidation>& registers)
{
    using namespace runtime::simir;
    if (operation.string_source && operation.source >= process.string_register_count)
        return "FileScan source string register is out of range";
    if (operation.conversions.empty() || operation.conversions.size() > 64U
        || operation.trailing_text.size() > maximum_string_bytes)
        return "FileScan format metadata is empty or oversized";
    for (const auto& conversion : operation.conversions) {
        if (conversion.format > InputScanFormat::real
            || conversion.prefix.size() > maximum_string_bytes
            || conversion.maximum_characters > maximum_string_bytes)
            return "FileScan conversion metadata is invalid or oversized";
        if (conversion.suppress)
            continue;
        const bool string_target = conversion.target.kind
                == InputScanTargetKind::string_register
            || conversion.target.kind == InputScanTargetKind::string_object;
        const bool text_format = conversion.format == InputScanFormat::character
            || conversion.format == InputScanFormat::string;
        const auto scalar = conversion.target.scalar_kind;
        const bool real_scalar = scalar == runtime::SystemVerilogScalarKind::ShortReal
            || scalar == runtime::SystemVerilogScalarKind::Real
            || scalar == runtime::SystemVerilogScalarKind::Realtime;
        const auto scalar_width = scalar == runtime::SystemVerilogScalarKind::ShortReal
            ? 32U
            : 64U;
        const bool scalar_format = scalar == runtime::SystemVerilogScalarKind::None
            ? conversion.format != InputScanFormat::real
            : real_scalar ? conversion.format == InputScanFormat::real
            : scalar == runtime::SystemVerilogScalarKind::Time
            ? conversion.format == InputScanFormat::decimal
                || conversion.format == InputScanFormat::unsigned_decimal
                || conversion.format == InputScanFormat::real
            : scalar == runtime::SystemVerilogScalarKind::Chandle
                && conversion.format == InputScanFormat::hexadecimal;
        if (scalar > runtime::SystemVerilogScalarKind::Chandle || !scalar_format
            || (scalar != runtime::SystemVerilogScalarKind::None
                && (!conversion.target.two_state
                    || conversion.target.width != scalar_width)))
            return "FileScan scalar target metadata is inconsistent";
        if (string_target && !text_format)
            return "FileScan numeric conversion has a string target";
        if (string_target && scalar != runtime::SystemVerilogScalarKind::None)
            return "FileScan string target has scalar metadata";
        if (conversion.target.kind > InputScanTargetKind::string_object
            || conversion.target.width == 0)
            return "FileScan target metadata is invalid";
        if (conversion.target.kind == InputScanTargetKind::packed_register) {
            if (conversion.target.id >= process.register_count)
                return "FileScan packed target register is out of range";
            if (!process.register_value_kinds.empty()) {
                const auto kind = process.register_value_kinds[conversion.target.id];
                if (kind == ValueKind::logic9)
                    return "FileScan packed target value domain is inconsistent";
            }
            registers.push_back(
                { conversion.target.id, conversion.target.width, true });
        } else if (conversion.target.kind == InputScanTargetKind::packed_signal) {
            if (conversion.target.id >= signal_widths.size()
                || signal_widths[conversion.target.id] != conversion.target.width)
                return "FileScan packed signal target is out of range or mismatched";
            if (!signal_value_kinds.empty()) {
                const auto kind = signal_value_kinds[conversion.target.id];
                if (kind == ValueKind::logic9)
                    return "FileScan packed signal value domain is inconsistent";
            }
        } else if (conversion.target.kind == InputScanTargetKind::string_register
            && conversion.target.id >= process.string_register_count) {
            return "FileScan target string register is out of range";
        }
    }
    return std::nullopt;
}

std::optional<std::string> validate_file_binary_metadata(
    const runtime::simir::FileBinaryRead& operation,
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths,
    std::vector<PackedRegisterValidation>& registers)
{
    using namespace runtime::simir;
    if (operation.target_kind > FileBinaryTargetKind::container_object
        || operation.width == 0
        || (operation.has_count && !operation.has_start))
        return "FileBinaryRead metadata is invalid";
    const auto scalar = operation.scalar_kind;
    const auto scalar_width = scalar == runtime::SystemVerilogScalarKind::ShortReal
        ? 32U
        : 64U;
    if (scalar > runtime::SystemVerilogScalarKind::Chandle
        || (scalar != runtime::SystemVerilogScalarKind::None
            && (!operation.two_state || operation.width != scalar_width)))
        return "FileBinaryRead scalar target metadata is inconsistent";
    registers.push_back({ operation.handle, 32U, false });
    registers.push_back({ operation.destination, 32U, true });
    if (operation.has_start)
        registers.push_back({ operation.start, 32U, false });
    if (operation.has_count)
        registers.push_back({ operation.count, 32U, false });
    if (operation.target_kind == FileBinaryTargetKind::packed_register) {
        if (operation.target >= process.register_count
            || operation.has_start || operation.has_count)
            return "FileBinaryRead packed target metadata is invalid";
        registers.push_back({ operation.target, operation.width, true });
    } else if (operation.target_kind == FileBinaryTargetKind::packed_signal) {
        if (operation.target >= signal_widths.size()
            || signal_widths[operation.target] != operation.width
            || operation.has_start || operation.has_count)
            return "FileBinaryRead packed signal metadata is invalid";
    } else if (operation.target_kind
        == FileBinaryTargetKind::container_register) {
        if (operation.target >= process.container_register_types.size())
            return "FileBinaryRead container register is out of range";
        const auto& type = process.container_register_types[operation.target];
        if (!type.fixed || type.dimensions.size() != 1U
            || type.element_width != operation.width
            || type.two_state != operation.two_state
            || type.scalar_kind != operation.scalar_kind)
            return "FileBinaryRead container target profile is invalid";
    }
    return std::nullopt;
}

std::optional<std::uint32_t> formatted_value_width(
    const runtime::simir::OutputFormat format,
    const runtime::SystemVerilogScalarKind scalar_kind) noexcept
{
    using runtime::SystemVerilogScalarKind;
    using runtime::simir::OutputFormat;
    if (scalar_kind == SystemVerilogScalarKind::None)
        return format <= OutputFormat::string
            ? std::optional<std::uint32_t> { 0U }
            : std::nullopt;
    const bool real_format = format >= OutputFormat::real_scientific
        && format <= OutputFormat::real_general;
    if (scalar_kind == SystemVerilogScalarKind::ShortReal)
        return real_format ? std::optional<std::uint32_t> { 32U } : std::nullopt;
    if (scalar_kind == SystemVerilogScalarKind::Real
        || scalar_kind == SystemVerilogScalarKind::Realtime)
        return real_format ? std::optional<std::uint32_t> { 64U } : std::nullopt;
    if (scalar_kind == SystemVerilogScalarKind::Time)
        return format == OutputFormat::decimal || format == OutputFormat::time
            ? std::optional<std::uint32_t> { 64U }
            : std::nullopt;
    if (scalar_kind == SystemVerilogScalarKind::Chandle)
        return format == OutputFormat::hexadecimal
            ? std::optional<std::uint32_t> { 64U }
            : std::nullopt;
    return std::nullopt;
}

std::optional<std::string> validate_file_write_metadata(
    const runtime::simir::FileWriteFormatted& operation,
    std::vector<PackedRegisterValidation>& registers)
{
    const auto scalar_width = formatted_value_width(
        operation.format, operation.scalar_kind);
    if (operation.width == 0)
        return "FileWriteFormatted width must be nonzero";
    if (!scalar_width
        || (*scalar_width != 0 && *scalar_width != operation.width))
        return "FileWriteFormatted scalar metadata is inconsistent";
    registers.push_back({ operation.handle, 32U, false });
    registers.push_back({ operation.source, operation.width, false });
    return std::nullopt;
}

std::optional<std::string> validate_file_position_metadata(
    const runtime::simir::FilePosition& operation,
    std::vector<PackedRegisterValidation>& registers)
{
    using namespace runtime::simir;
    if (operation.kind > FilePositionKind::rewind)
        return "FilePosition kind is invalid";
    registers.push_back({ operation.handle, 32U, false });
    registers.push_back({ operation.destination, 32U, true });
    if (operation.kind == FilePositionKind::seek) {
        registers.push_back({ operation.offset, 32U, false });
        registers.push_back({ operation.origin, 32U, false });
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view generated_runtime_error_reason(
    const JitGeneratedRuntimeErrorReason reason) noexcept
{
    switch (reason) {
    case JitGeneratedRuntimeErrorReason::unknown_branch_condition:
        return "branch condition is unknown or high impedance";
    case JitGeneratedRuntimeErrorReason::integer_operand_unknown:
        return "VHDL integer operand contains an unknown or high-impedance value";
    case JitGeneratedRuntimeErrorReason::integer_overflow:
        return "VHDL integer arithmetic overflow";
    case JitGeneratedRuntimeErrorReason::integer_division_by_zero:
        return "VHDL integer division by zero";
    case JitGeneratedRuntimeErrorReason::integer_negative_exponent:
        return "VHDL integer exponent must be nonnegative";
    case JitGeneratedRuntimeErrorReason::integer_subtype_range:
        return "VHDL integer subtype range check failed";
    case JitGeneratedRuntimeErrorReason::dynamic_index_unknown:
        return "dynamic packed index contains an unknown or high-impedance value";
    case JitGeneratedRuntimeErrorReason::dynamic_index_range:
        return "dynamic packed index is outside the declared range";
    case JitGeneratedRuntimeErrorReason::call_stack_unknown:
        return "call-stack state contains an unknown or high-impedance value";
    case JitGeneratedRuntimeErrorReason::call_stack_overflow:
        return "call-stack capacity is exhausted";
    case JitGeneratedRuntimeErrorReason::call_stack_underflow:
        return "call-stack underflow";
    case JitGeneratedRuntimeErrorReason::call_stack_target:
        return "call-stack return target is invalid";
    case JitGeneratedRuntimeErrorReason::string_callback_failure:
        return "mutable string runtime callback failed";
    case JitGeneratedRuntimeErrorReason::file_callback_failure:
        return "text file runtime callback failed";
    case JitGeneratedRuntimeErrorReason::container_callback_failure:
        return "bounded container runtime callback failed";
    case JitGeneratedRuntimeErrorReason::signal_callback_failure:
        return "exact-width signal runtime callback failed";
    }
    return "unknown generated runtime error";
}

[[nodiscard]] std::string generated_runtime_error_message(
    const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason)
{
    return "generated SimIR process, instruction "
        + std::to_string(instruction) + ": "
        + std::string { generated_runtime_error_reason(reason) };
}

[[nodiscard]] std::optional<JitGeneratedRuntimeErrorReason>
decode_generated_runtime_error(const std::uint64_t value) noexcept
{
    const auto reason = static_cast<JitGeneratedRuntimeErrorReason>(value);
    switch (reason) {
    case JitGeneratedRuntimeErrorReason::unknown_branch_condition:
    case JitGeneratedRuntimeErrorReason::integer_operand_unknown:
    case JitGeneratedRuntimeErrorReason::integer_overflow:
    case JitGeneratedRuntimeErrorReason::integer_division_by_zero:
    case JitGeneratedRuntimeErrorReason::integer_negative_exponent:
    case JitGeneratedRuntimeErrorReason::integer_subtype_range:
    case JitGeneratedRuntimeErrorReason::dynamic_index_unknown:
    case JitGeneratedRuntimeErrorReason::dynamic_index_range:
    case JitGeneratedRuntimeErrorReason::call_stack_unknown:
    case JitGeneratedRuntimeErrorReason::call_stack_overflow:
    case JitGeneratedRuntimeErrorReason::call_stack_underflow:
    case JitGeneratedRuntimeErrorReason::call_stack_target:
    case JitGeneratedRuntimeErrorReason::string_callback_failure:
    case JitGeneratedRuntimeErrorReason::file_callback_failure:
    case JitGeneratedRuntimeErrorReason::container_callback_failure:
    case JitGeneratedRuntimeErrorReason::signal_callback_failure:
        return reason;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
validate_container_locator_metadata(
    const runtime::simir::LocateContainer& operation,
    const runtime::simir::ContainerType& destination,
    const runtime::simir::ContainerType& source)
{
    using namespace runtime::simir;
    if (operation.operation > ContainerLocatorOperator::find_last_index) {
        return "LocateContainer has an invalid operator";
    }
    const bool predicate_locator = operation.operation >= ContainerLocatorOperator::find;
    const bool index_result = operation.operation == ContainerLocatorOperator::unique_index
        || operation.operation == ContainerLocatorOperator::find_index
        || operation.operation
            == ContainerLocatorOperator::find_first_index
        || operation.operation
            == ContainerLocatorOperator::find_last_index;
    if (!destination.queue || destination.associative
        || destination.fixed || source.associative
        || (index_result
                ? destination.element_width != 32
                    || !destination.two_state
                    || !destination.signed_elements
                : destination.element_width != source.element_width
                    || destination.two_state != source.two_state
                    || destination.signed_elements
                        != source.signed_elements)) {
        return "LocateContainer has incompatible container types";
    }
    if (predicate_locator != !operation.predicate.empty()
        || operation.predicate.size()
            > maximum_container_predicate_nodes) {
        return "LocateContainer has invalid predicate metadata";
    }
    std::vector<ContainerPredicateValueKind> value_kinds;
    value_kinds.reserve(operation.predicate.size());
    for (std::size_t index = 0;
        index < operation.predicate.size(); ++index) {
        const auto& node = operation.predicate[index];
        const auto earlier =
            [index](const std::uint32_t operand) {
                return operand < index;
            };
        const bool comparison = node.operation >= ContainerPredicateOperator::equal
            && node.operation
                <= ContainerPredicateOperator::greater_equal;
        if (node.operation
            > ContainerPredicateOperator::logical_not) {
            return "LocateContainer predicate has an invalid operator";
        }
        if (node.operation == ContainerPredicateOperator::item) {
            if (node.value_kind
                != ContainerPredicateValueKind::element) {
                return "LocateContainer predicate item has the wrong type";
            }
            value_kinds.push_back(node.value_kind);
        } else if (
            node.operation == ContainerPredicateOperator::index) {
            if (node.value_kind
                != ContainerPredicateValueKind::index) {
                return "LocateContainer predicate index has the wrong type";
            }
            value_kinds.push_back(node.value_kind);
        } else if (
            node.operation == ContainerPredicateOperator::constant) {
            if (node.value_kind == ContainerPredicateValueKind::logical
                || node.constant.width()
                    != (node.value_kind
                                == ContainerPredicateValueKind::index
                            ? 32U
                            : source.element_width)
                || node.constant.is_logic9()
                || ((node.value_kind
                            == ContainerPredicateValueKind::index
                        || source.two_state)
                    && node.constant.low_word().bval != 0)) {
                return "LocateContainer predicate constant has the wrong type";
            }
            value_kinds.push_back(node.value_kind);
        } else if (comparison) {
            if (!earlier(node.left) || !earlier(node.right)
                || node.value_kind
                    != ContainerPredicateValueKind::logical
                || value_kinds[node.left]
                    != value_kinds[node.right]
                || value_kinds[node.left]
                    == ContainerPredicateValueKind::logical) {
                return "LocateContainer comparison operands are invalid";
            }
            value_kinds.push_back(node.value_kind);
        } else if (
            node.operation
            == ContainerPredicateOperator::logical_not) {
            if (!earlier(node.left)
                || node.value_kind
                    != ContainerPredicateValueKind::logical) {
                return "LocateContainer logical operand is invalid";
            }
            value_kinds.push_back(node.value_kind);
        } else {
            if (!earlier(node.left) || !earlier(node.right)
                || node.value_kind
                    != ContainerPredicateValueKind::logical) {
                return "LocateContainer logical operands are invalid";
            }
            value_kinds.push_back(node.value_kind);
        }
    }
    return std::nullopt;
}

namespace {

    [[nodiscard]] std::optional<std::string>
    validate_container_element_graph(
        const std::span<const runtime::simir::ContainerPredicateNode> nodes,
        const runtime::simir::ContainerType& source,
        const std::string_view owner,
        const std::string_view graph_name)
    {
        using namespace runtime::simir;
        if (source.associative
            || nodes.empty()
            || nodes.size() > maximum_container_predicate_nodes) {
            return std::string { owner } + " has invalid "
                + std::string { graph_name } + " metadata";
        }
        std::vector<ContainerPredicateValueKind> value_kinds;
        value_kinds.reserve(nodes.size());
        std::size_t conditional_count { };
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const auto& node = nodes[index];
            const auto earlier =
                [index](const std::uint32_t operand) {
                    return operand < index;
                };
            const bool comparison = node.operation >= ContainerPredicateOperator::equal
                && node.operation
                    <= ContainerPredicateOperator::greater_equal;
            if (node.operation > ContainerPredicateOperator::conditional) {
                return std::string { owner } + " " + std::string { graph_name }
                + " has an invalid operator";
            }
            if (node.operation != ContainerPredicateOperator::conditional
                && node.third != 0) {
                return std::string { owner } + " " + std::string { graph_name }
                + " has an unused third edge";
            }
            if (node.operation == ContainerPredicateOperator::item) {
                if (node.value_kind
                    != ContainerPredicateValueKind::element) {
                    return std::string { owner } + " " + std::string { graph_name }
                    + " item has the wrong type";
                }
            } else if (
                node.operation == ContainerPredicateOperator::index) {
                if (node.value_kind
                    != ContainerPredicateValueKind::index) {
                    return std::string { owner } + " " + std::string { graph_name }
                    + " index has the wrong type";
                }
            } else if (
                node.operation == ContainerPredicateOperator::constant) {
                if (node.value_kind > ContainerPredicateValueKind::logical
                    || node.value_kind
                        == ContainerPredicateValueKind::logical
                    || node.constant.width()
                        != (node.value_kind
                                    == ContainerPredicateValueKind::index
                                ? 32U
                                : source.element_width)
                    || node.constant.is_logic9()
                    || ((node.value_kind
                                == ContainerPredicateValueKind::index
                            || source.two_state)
                        && node.constant.low_word().bval != 0)) {
                    return std::string { owner } + " " + std::string { graph_name }
                    + " constant has the wrong type";
                }
            } else if (comparison) {
                if (!earlier(node.left) || !earlier(node.right)
                    || node.value_kind
                        != ContainerPredicateValueKind::logical
                    || value_kinds[node.left]
                        != value_kinds[node.right]
                    || value_kinds[node.left]
                        == ContainerPredicateValueKind::logical) {
                    return std::string { owner } + " " + std::string { graph_name }
                    + " comparison operands are invalid";
                }
            } else if (
                node.operation
                == ContainerPredicateOperator::logical_not) {
                if (!earlier(node.left)
                    || node.value_kind
                        != ContainerPredicateValueKind::logical) {
                    return std::string { owner } + " " + std::string { graph_name }
                    + " logical operand is invalid";
                }
            } else if (
                node.operation
                == ContainerPredicateOperator::conditional) {
                ++conditional_count;
                if (conditional_count > 1U
                    || !earlier(node.left)
                    || !earlier(node.right)
                    || !earlier(node.third)
                    || node.value_kind
                        != ContainerPredicateValueKind::element
                    || value_kinds[node.right]
                        != ContainerPredicateValueKind::element
                    || value_kinds[node.third]
                        != ContainerPredicateValueKind::element) {
                    return std::string { owner }
                    + " conditional operands are invalid";
                }
            } else {
                if (!earlier(node.left) || !earlier(node.right)
                    || node.value_kind
                        != ContainerPredicateValueKind::logical) {
                    return std::string { owner } + " " + std::string { graph_name }
                    + " logical operands are invalid";
                }
            }
            value_kinds.push_back(node.value_kind);
        }
        if (value_kinds.back()
            != ContainerPredicateValueKind::element) {
            return std::string { owner } + " " + std::string { graph_name }
            + " root has the wrong type";
        }
        return std::nullopt;
    }

} // namespace

[[nodiscard]] std::optional<std::string>
validate_container_reduction_metadata(
    const runtime::simir::ContainerReduction& operation,
    const runtime::simir::ContainerType& source)
{
    if (operation.transformation.empty()) {
        return std::nullopt;
    }
    return validate_container_element_graph(
        operation.transformation, source,
        "ContainerReduction", "transformation");
}

[[nodiscard]] std::optional<std::string>
validate_container_ordering_metadata(
    const runtime::simir::OrderContainer& operation,
    const runtime::simir::ContainerType& target)
{
    using runtime::simir::ContainerOrderingOperator;
    if (operation.key.empty()) {
        return std::nullopt;
    }
    if (operation.operation != ContainerOrderingOperator::ascending
        && operation.operation
            != ContainerOrderingOperator::descending) {
        return "OrderContainer key metadata requires sort or rsort";
    }
    return validate_container_element_graph(
        operation.key, target, "OrderContainer", "key");
}

[[nodiscard]] std::optional<std::string>
validate_container_locator_transformation_metadata(
    const runtime::simir::LocateContainer& operation,
    const runtime::simir::ContainerType& source)
{
    using runtime::simir::ContainerLocatorOperator;
    if (operation.transformation.empty()) {
        return std::nullopt;
    }
    if (operation.operation >= ContainerLocatorOperator::find
        || !operation.predicate.empty()) {
        return "LocateContainer transformation metadata requires "
               "min, max, unique, or unique_index";
    }
    return validate_container_element_graph(
        operation.transformation, source,
        "LocateContainer", "transformation");
}

} // namespace fsim::compiler::llvm_detail
