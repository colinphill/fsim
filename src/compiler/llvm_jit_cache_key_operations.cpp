// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_cache_key_internal.hpp"

#include <boost/pfr/core.hpp>

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace fsim::compiler::llvm_detail {
namespace {

template <typename Type>
struct OperationCacheTraits;

// Explicit wire tags remain stable when operation groups or variant positions
// change. A tag change requires a native cache schema change.
#define FSIM_CACHE_OPERATIONS(X) \
    X(LoadConstant, "LoadConstant") \
    X(CopyRegister, "CopyRegister") \
    X(ConvertToTwoState, "ConvertToTwoState") \
    X(UnaryNot, "UnaryNot") \
    X(LogicalNot, "LogicalNot") \
    X(LogicalBinary, "LogicalBinary") \
    X(Reduction, "Reduction") \
    X(CountOnes, "CountOnes") \
    X(CountBits, "CountBits") \
    X(Shift, "Shift") \
    X(Extract, "Extract") \
    X(DynamicExtract, "DynamicExtract") \
    X(DynamicPartSelect, "DynamicPartSelect") \
    X(Concatenate, "Concatenate") \
    X(Binary, "Binary") \
    X(Insert, "Insert") \
    X(DynamicInsert, "DynamicInsert") \
    X(DynamicPartInsert, "DynamicPartInsert") \
    X(IntegerUnary, "IntegerUnary") \
    X(IntegerBinary, "IntegerBinary") \
    X(IntegerCheck, "IntegerCheck") \
    X(ConditionalSelect, "ConditionalSelect") \
    X(RandomValue, "RandomValue") \
    X(RandomDistribution, "RandomDistribution") \
    X(ScopeRandomize, "ScopeRandomize") \
    X(StochasticQueueOperation, "StochasticQueueOperation") \
    X(VhdlEnvironmentTime, "VhdlEnvironmentTime") \
    X(ReadSignal, "ReadSignal") \
    X(SignalEvent, "SignalEvent") \
    X(SignalLastValue, "SignalLastValue") \
    X(SignalLastEvent, "SignalLastEvent") \
    X(SignalActive, "SignalActive") \
    X(WriteBlocking, "WriteBlocking") \
    X(WriteUpdate, "WriteUpdate") \
    X(WriteAfter, "WriteAfter") \
    X(WriteInertial, "WriteInertial") \
    X(WriteProjected, "WriteProjected") \
    X(WriteProjectedWaveform, "WriteProjectedWaveform") \
    X(WriteBlockingSlice, "WriteBlockingSlice") \
    X(WriteUpdateSlice, "WriteUpdateSlice") \
    X(WriteAfterSlice, "WriteAfterSlice") \
    X(WriteInertialSlice, "WriteInertialSlice") \
    X(WriteProjectedSlice, "WriteProjectedSlice") \
    X(WriteProjectedWaveformSlice, "WriteProjectedWaveformSlice") \
    X(WriteBlockingDynamicSlice, "WriteBlockingDynamicSlice") \
    X(WriteUpdateDynamicSlice, "WriteUpdateDynamicSlice") \
    X(WriteAfterDynamicSlice, "WriteAfterDynamicSlice") \
    X(WriteBlockingDynamicPartSlice, "WriteBlockingDynamicPartSlice") \
    X(WriteUpdateDynamicPartSlice, "WriteUpdateDynamicPartSlice") \
    X(WriteAfterDynamicPartSlice, "WriteAfterDynamicPartSlice") \
    X(ForceSignalSlice, "ForceSignalSlice") \
    X(ReleaseSignalSlice, "ReleaseSignalSlice") \
    X(WriteInertialDynamicSlice, "WriteInertialDynamicSlice") \
    X(WriteProjectedDynamicSlice, "WriteProjectedDynamicSlice") \
    X(WriteProjectedWaveformDynamicSlice, "WriteProjectedWaveformDynamicSlice") \
    X(SignalLastActive, "SignalLastActive") \
    X(SignalDriving, "SignalDriving") \
    X(SignalDrivingValue, "SignalDrivingValue") \
    X(ReadSimulationTime, "ReadSimulationTime") \
    X(VitalTimingCheck, "VitalTimingCheck") \
    X(VitalDelay, "VitalDelay") \
    X(WriteInertialDynamicPartSlice, "WriteInertialDynamicPartSlice") \
    X(LoadStringConstant, "LoadStringConstant") \
    X(CopyStringRegister, "CopyStringRegister") \
    X(ReadStringObject, "ReadStringObject") \
    X(WriteStringObject, "WriteStringObject") \
    X(ConcatenateStrings, "ConcatenateStrings") \
    X(CompareStrings, "CompareStrings") \
    X(StringLength, "StringLength") \
    X(StringIndex, "StringIndex") \
    X(StringReplaceByte, "StringReplaceByte") \
    X(StringMethod, "StringMethod") \
    X(PlusArgSelect, "PlusArgSelect") \
    X(SystemCommand, "SystemCommand") \
    X(VcdControl, "VcdControl") \
    X(CoverageDatabaseControl, "CoverageDatabaseControl") \
    X(CoverageAccess, "CoverageAccess") \
    X(VhdlEnvironmentTimeToString, "VhdlEnvironmentTimeToString") \
    X(VhdlEnvironmentGetenv, "VhdlEnvironmentGetenv") \
    X(VhdlEnvironmentCallPath, "VhdlEnvironmentCallPath") \
    X(SystemVerilogScalarBinary, "SystemVerilogScalarBinary") \
    X(SystemVerilogMath, "SystemVerilogMath") \
    X(ResizeContainer, "ResizeContainer") \
    X(CopyContainerRegister, "CopyContainerRegister") \
    X(ConditionalContainerSelect, "ConditionalContainerSelect") \
    X(CompareContainers, "CompareContainers") \
    X(ReadContainerObject, "ReadContainerObject") \
    X(WriteContainerObject, "WriteContainerObject") \
    X(ContainerSize, "ContainerSize") \
    X(ContainerReduction, "ContainerReduction") \
    X(OrderContainer, "OrderContainer") \
    X(LocateContainer, "LocateContainer") \
    X(ContainerRead, "ContainerRead") \
    X(ContainerWrite, "ContainerWrite") \
    X(WriteContainerObjectElement, "WriteContainerObjectElement") \
    X(ContainerAggregateRead, "ContainerAggregateRead") \
    X(ContainerAggregateWrite, "ContainerAggregateWrite") \
    X(CopyContainerAggregateElement, "CopyContainerAggregateElement") \
    X(DeleteContainer, "DeleteContainer") \
    X(ContainerExists, "ContainerExists") \
    X(TraverseContainer, "TraverseContainer") \
    X(LoadMemory, "LoadMemory") \
    X(VitalMemoryDeclare, "VitalMemoryDeclare") \
    X(PushContainer, "PushContainer") \
    X(PopContainer, "PopContainer") \
    X(ContainerStringRead, "ContainerStringRead") \
    X(ContainerStringWrite, "ContainerStringWrite") \
    X(ContainerElementRead, "ContainerElementRead") \
    X(ContainerElementWrite, "ContainerElementWrite") \
    X(VhdlEnvironmentDirectory, "VhdlEnvironmentDirectory") \
    X(VhdlEnvironmentGetCallPath, "VhdlEnvironmentGetCallPath") \
    X(PlaEvaluate, "PlaEvaluate") \
    X(FileOpen, "FileOpen") \
    X(FileClose, "FileClose") \
    X(FileWriteLiteral, "FileWriteLiteral") \
    X(FileWriteFormatted, "FileWriteFormatted") \
    X(FileWriteString, "FileWriteString") \
    X(FileReadLine, "FileReadLine") \
    X(FileEndOfFile, "FileEndOfFile") \
    X(FileErrorStatus, "FileErrorStatus") \
    X(FileScan, "FileScan") \
    X(FileBinaryRead, "FileBinaryRead") \
    X(FilePosition, "FilePosition") \
    X(FileFlush, "FileFlush") \
    X(WaitFor, "WaitFor") \
    X(WaitRegion, "WaitRegion") \
    X(WaitOn, "WaitOn") \
    X(WaitPla, "WaitPla") \
    X(WaitOrder, "WaitOrder") \
    X(EventTriggered, "EventTriggered") \
    X(EventAlias, "EventAlias") \
    X(WaitSensitivity, "WaitSensitivity") \
    X(WaitForever, "WaitForever") \
    X(Yield, "Yield") \
    X(Fork, "Fork") \
    X(ForkEnd, "ForkEnd") \
    X(WaitFork, "WaitFork") \
    X(DisableFork, "DisableFork") \
    X(DisableBlock, "DisableBlock") \
    X(ProcessSelf, "ProcessSelf") \
    X(ProcessStatusQuery, "ProcessStatusQuery") \
    X(ProcessCompleted, "ProcessCompleted") \
    X(ProcessAwait, "ProcessAwait") \
    X(ProcessKill, "ProcessKill") \
    X(ProcessSuspend, "ProcessSuspend") \
    X(ProcessResume, "ProcessResume") \
    X(ProcessGetRandState, "ProcessGetRandState") \
    X(ProcessSetRandState, "ProcessSetRandState") \
    X(ProcessSrandom, "ProcessSrandom") \
    X(MailboxCreate, "MailboxCreate") \
    X(MailboxPut, "MailboxPut") \
    X(MailboxGet, "MailboxGet") \
    X(MailboxNum, "MailboxNum") \
    X(SemaphoreCreate, "SemaphoreCreate") \
    X(SemaphoreGet, "SemaphoreGet") \
    X(SemaphorePut, "SemaphorePut") \
    X(Jump, "Jump") \
    X(Call, "Call") \
    X(Return, "Return") \
    X(CallableFramePush, "CallableFramePush") \
    X(CallableFramePop, "CallableFramePop") \
    X(Branch, "Branch") \
    X(DebugPoint, "DebugPoint") \
    X(Assert, "Assert") \
    X(Report, "Report") \
    X(Pause, "Pause") \
    X(Stop, "Stop") \
    X(Halt, "Halt") \
    X(Display, "Display") \
    X(FormatDisplay, "FormatDisplay") \
    X(StringDisplay, "StringDisplay") \
    X(StringReport, "StringReport") \
    X(TimeDisplay, "TimeDisplay") \
    X(MonitorInstall, "MonitorInstall") \
    X(MonitorControl, "MonitorControl") \
    X(TimeFormatControl, "TimeFormatControl") \
    X(CoverageSample, "CoverageSample") \
    X(CoverageQuery, "CoverageQuery") \
    X(CodeCoverageHit, "CodeCoverageHit") \
    X(CoverageControl, "CoverageControl") \
    X(VhdlPslApi, "VhdlPslApi") \
    X(VhdlAssertApi, "VhdlAssertApi") \
    X(VhdlReflectionApi, "VhdlReflectionApi") \
    X(ClassAllocate, "ClassAllocate") \
    X(ClassPropertyRead, "ClassPropertyRead") \
    X(ClassPropertyWrite, "ClassPropertyWrite") \
    X(ClassMethodCall, "ClassMethodCall") \
    X(ClassStaticPropertyRead, "ClassStaticPropertyRead") \
    X(ClassStaticPropertyWrite, "ClassStaticPropertyWrite") \
    X(ClassStaticMethodCall, "ClassStaticMethodCall")

#define FSIM_DECLARE_CACHE_TRAIT(Type, Tag) \
    template <> \
    struct OperationCacheTraits<runtime::simir::Type> { \
        static constexpr std::string_view tag = Tag; \
    };
FSIM_CACHE_OPERATIONS(FSIM_DECLARE_CACHE_TRAIT)
#undef FSIM_DECLARE_CACHE_TRAIT

template <typename Group, std::size_t... Indices>
consteval bool group_has_cache_traits(std::index_sequence<Indices...>)
{
    return (requires {
        OperationCacheTraits<std::variant_alternative_t<
            Indices, typename Group::Storage>>::tag;
    } && ...);
}

template <typename Group>
consteval bool group_has_cache_traits()
{
    return group_has_cache_traits<Group>(std::make_index_sequence<
        std::variant_size_v<typename Group::Storage>> { });
}

static_assert(group_has_cache_traits<runtime::simir::ValueOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::SignalOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::StringOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::ContainerOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::FileOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::SchedulingOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::ControlOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::OutputOperationGroup>());
static_assert(group_has_cache_traits<runtime::simir::ClassOperationGroup>());

#define FSIM_CACHE_TAG_VALUE(Type, Tag) std::string_view { Tag },
constexpr auto operation_cache_tags = std::to_array<std::string_view>({
    FSIM_CACHE_OPERATIONS(FSIM_CACHE_TAG_VALUE)
});
#undef FSIM_CACHE_TAG_VALUE

consteval bool unique_operation_cache_tags()
{
    for (std::size_t left = 0; left < operation_cache_tags.size(); ++left) {
        for (std::size_t right = left + 1;
            right < operation_cache_tags.size(); ++right) {
            if (operation_cache_tags[left] == operation_cache_tags[right]) {
                return false;
            }
        }
    }
    return true;
}

static_assert(operation_cache_tags.size() == 190U);
static_assert(unique_operation_cache_tags());
#undef FSIM_CACHE_OPERATIONS

template <typename Type>
struct IsOptional : std::false_type { };

template <typename Value>
struct IsOptional<std::optional<Value>> : std::true_type { };

template <std::unsigned_integral Value>
struct IsOptional<runtime::simir::RareOptionalId<Value>> : std::true_type { };

template <typename Value>
struct IsOptional<support::RareOptional<Value>> : std::true_type { };

template <typename Type>
struct IsVariant : std::false_type { };

template <typename... Values>
struct IsVariant<std::variant<Values...>> : std::true_type { };

template <typename Type>
inline constexpr bool unsupported_semantic_value = false;

template <typename Value>
void add_semantic_value(CacheKeyBuilder& builder, const Value& value)
{
    using Type = std::remove_cvref_t<Value>;
    if constexpr (std::is_same_v<Type, runtime::PackedLogic4>) {
        add_packed_value_key(builder, value);
    } else if constexpr (std::is_same_v<Type,
                             runtime::simir::InternedString>) {
        builder.add("string", value.str());
    } else if constexpr (std::is_same_v<Type, std::string>
        || std::is_same_v<Type, std::string_view>) {
        builder.add("string", value);
    } else if constexpr (std::is_same_v<Type, bool>) {
        add_key_u64(builder, "bool", value ? 1U : 0U);
    } else if constexpr (std::is_enum_v<Type>) {
        add_key_u64(builder, "enum",
            static_cast<std::uint64_t>(
                static_cast<std::underlying_type_t<Type>>(value)));
    } else if constexpr (std::is_integral_v<Type>) {
        add_key_u64(builder, "integer", static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_floating_point_v<Type>
        && sizeof(Type) == sizeof(std::uint32_t)) {
        add_key_u64(builder, "float32", std::bit_cast<std::uint32_t>(value));
    } else if constexpr (std::is_floating_point_v<Type>
        && sizeof(Type) == sizeof(std::uint64_t)) {
        add_key_u64(builder, "float64", std::bit_cast<std::uint64_t>(value));
    } else if constexpr (IsOptional<Type>::value) {
        add_key_u64(builder, "optional", value.has_value() ? 1U : 0U);
        if (value) {
            add_semantic_value(builder, *value);
        }
    } else if constexpr (IsVariant<Type>::value) {
        add_key_u64(builder, "variant", value.index());
        std::visit([&](const auto& item) {
            add_semantic_value(builder, item);
        }, value);
    } else if constexpr (requires { archive_fields(value); }) {
        const auto fields = archive_fields(value);
        add_key_u64(builder, "field-count",
            std::tuple_size_v<decltype(fields)>);
        std::apply([&](const auto&... field) {
            (add_semantic_value(builder, field), ...);
        }, fields);
    } else if constexpr (std::is_same_v<Type, runtime::simir::WaitFor>) {
        add_semantic_value(builder, std::tie(value.delay, value.source,
            value.source_width, value.source_kind, value.source_signed,
            value.rounding_quantum));
    } else if constexpr (std::is_same_v<Type, runtime::simir::WaitOn>) {
        add_semantic_value(builder, std::tie(value.signals, value.edges,
            value.timeout, value.timeout_result, value.timeout_origin));
    } else if constexpr (std::is_same_v<Type, runtime::simir::DebugPoint>) {
        add_semantic_value(builder,
            std::tie(value.kind, value.source, value.scope));
    } else if constexpr (std::is_same_v<Type, runtime::simir::FileOpen>) {
        add_semantic_value(builder, std::tie(value.destination, value.path,
            value.mode, value.status, value.vhdl));
    } else if constexpr (std::is_same_v<Type, runtime::simir::FileClose>) {
        add_semantic_value(builder,
            std::tie(value.handle, value.clear_handle, value.ignore_zero));
    } else if constexpr (std::is_same_v<Type, runtime::simir::FileEndOfFile>) {
        add_semantic_value(builder,
            std::tie(value.destination, value.handle, value.lookahead));
    } else if constexpr (std::is_same_v<Type, runtime::simir::FileScan>) {
        add_semantic_value(builder, std::tie(value.destination, value.handle,
            value.source, value.string_source, value.conversions,
            value.trailing_text, value.require_assignments, value.success,
            value.consume_string_source));
    } else if constexpr (requires { typename std::tuple_size<Type>::type; }) {
        add_key_u64(builder, "field-count", std::tuple_size_v<Type>);
        std::apply([&](const auto&... field) {
            (add_semantic_value(builder, field), ...);
        }, value);
    } else if constexpr (std::ranges::sized_range<const Type>) {
        static_assert(!requires { typename Type::hasher; },
            "unordered semantic fields need an explicit stable ordering");
        add_key_u64(builder, "element-count", std::ranges::size(value));
        for (const auto& element : value) {
            add_semantic_value(builder, element);
        }
    } else if constexpr (std::is_aggregate_v<Type>) {
        add_key_u64(builder, "field-count", boost::pfr::tuple_size_v<Type>);
        boost::pfr::for_each_field(value, [&](const auto& field) {
            add_semantic_value(builder, field);
        });
    } else {
        static_assert(unsupported_semantic_value<Type>,
            "unsupported SimIR semantic cache field");
    }
}

void add_contextual_signal_widths(CacheKeyBuilder& builder,
    const runtime::simir::Process& process,
    const runtime::simir::Operation& operation,
    const std::span<const std::uint32_t> signal_widths)
{
    runtime::simir::visit_operation([&](const auto& value) {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (requires { value.signal; }) {
            add_key_u64(builder, "signal-width", signal_widths[value.signal]);
        } else if constexpr (std::is_same_v<Type,
                                 runtime::simir::WaitOn>
            || std::is_same_v<Type, runtime::simir::WaitPla>) {
            for (const auto signal : value.signals) {
                add_key_u64(builder, "signal-width", signal_widths[signal]);
            }
        } else if constexpr (std::is_same_v<Type,
                                 runtime::simir::WaitOrder>) {
            for (const auto signal : value.events) {
                add_key_u64(builder, "signal-width", signal_widths[signal]);
            }
        } else if constexpr (std::is_same_v<Type,
                                 runtime::simir::EventTriggered>) {
            add_key_u64(builder, "signal-width", signal_widths[value.event]);
        } else if constexpr (std::is_same_v<Type,
                                 runtime::simir::WaitSensitivity>) {
            for (const auto& sensitivity : process.static_sensitivity) {
                add_key_u64(builder, "signal-width",
                    signal_widths[sensitivity.signal]);
            }
        }
    }, operation);
}

} // namespace

void add_operation_cache_keys(CacheKeyBuilder& builder,
    const runtime::simir::Process& process,
    const std::span<const std::uint32_t> signal_widths)
{
    add_key_u64(builder, "operation-count", process.operations.size());
    for (const auto& operation : process.operations) {
        runtime::simir::visit_operation([&](const auto& value) {
            using Type = std::remove_cvref_t<decltype(value)>;
            builder.add("operation", OperationCacheTraits<Type>::tag);
            add_semantic_value(builder, value);
        }, operation);
        add_contextual_signal_widths(builder, process, operation,
            signal_widths);
    }
}

} // namespace fsim::compiler::llvm_detail
