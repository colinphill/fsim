// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal implementation fragment included by simir.hpp after all concrete
// operation alternatives have been defined.

template <typename... Alternatives> struct OperationGroup {
  using Storage = std::variant<Alternatives...>;

  Storage storage;

  OperationGroup() = default;

  template <typename Alternative>
    requires(
        std::is_same_v<std::remove_cvref_t<Alternative>, Alternatives>
        || ...)
  OperationGroup(Alternative&& operation)
      : storage(std::forward<Alternative>(operation)) {}
};

using ValueOperationGroup =
    OperationGroup<LoadConstant, CopyRegister, UnaryNot, LogicalNot,
                   LogicalBinary, Reduction, CountOnes, CountBits, Shift,
                   Extract, DynamicExtract, DynamicPartSelect, Concatenate,
                   Binary, Insert, DynamicInsert, DynamicPartInsert,
                   IntegerUnary, IntegerBinary, IntegerCheck,
                   ConditionalSelect, RandomValue>;

using SignalOperationGroup =
    OperationGroup<ReadSignal, SignalEvent, SignalLastValue, SignalLastEvent,
                   SignalActive, WriteBlocking, WriteUpdate, WriteAfter,
                   WriteInertial, WriteProjected, WriteProjectedWaveform,
                   WriteBlockingSlice, WriteUpdateSlice, WriteAfterSlice,
                   WriteInertialSlice, WriteProjectedSlice,
                   WriteProjectedWaveformSlice, WriteBlockingDynamicSlice,
                   WriteUpdateDynamicSlice, WriteAfterDynamicSlice,
                   WriteBlockingDynamicPartSlice,
                   WriteUpdateDynamicPartSlice,
                   WriteAfterDynamicPartSlice, ForceSignalSlice,
                   ReleaseSignalSlice, WriteInertialDynamicSlice,
                   WriteProjectedDynamicSlice,
                   WriteProjectedWaveformDynamicSlice, SignalLastActive,
                   SignalDriving, SignalDrivingValue>;

using StringOperationGroup =
    OperationGroup<LoadStringConstant, CopyStringRegister, ReadStringObject,
                   WriteStringObject, ConcatenateStrings, CompareStrings,
                   StringLength, StringIndex, StringReplaceByte, StringMethod>;

using ContainerOperationGroup =
    OperationGroup<ResizeContainer, CopyContainerRegister,
                   ConditionalContainerSelect, CompareContainers,
                   ReadContainerObject, WriteContainerObject, ContainerSize,
                   ContainerReduction, OrderContainer, LocateContainer,
                   ContainerRead, ContainerWrite, DeleteContainer,
                   ContainerExists, TraverseContainer, LoadMemory,
                   PushContainer, PopContainer>;

using FileOperationGroup =
    OperationGroup<FileOpen, FileClose, FileWriteLiteral, FileWriteFormatted,
                   FileWriteString, FileReadLine, FileEndOfFile,
                   FileErrorStatus, FileScan, FileBinaryRead, FilePosition,
                   FileFlush>;

using SchedulingOperationGroup =
    OperationGroup<WaitFor, WaitOn, WaitSensitivity, WaitForever, Yield, Fork,
                   ForkEnd, WaitFork, DisableFork>;

using ControlOperationGroup =
    OperationGroup<Jump, Call, Return, Branch, DebugPoint, Assert, Report,
                   Pause, Stop, Halt>;

using OutputOperationGroup =
    OperationGroup<Display, FormatDisplay, StringDisplay, StringReport,
                   TimeDisplay, MonitorInstall, MonitorControl>;

template <typename Alternative, typename Variant>
struct VariantContains;

template <typename Alternative, typename... Alternatives>
struct VariantContains<Alternative, std::variant<Alternatives...>>
    : std::bool_constant<
          (std::is_same_v<Alternative, Alternatives> || ...)> {};

template <typename Alternative, typename Group>
inline constexpr bool operation_group_contains_v =
    VariantContains<Alternative, typename Group::Storage>::value;

template <typename Alternative>
inline constexpr bool is_operation_alternative_v =
    operation_group_contains_v<Alternative, ValueOperationGroup>
    || operation_group_contains_v<Alternative, SignalOperationGroup>
    || operation_group_contains_v<Alternative, StringOperationGroup>
    || operation_group_contains_v<Alternative, ContainerOperationGroup>
    || operation_group_contains_v<Alternative, FileOperationGroup>
    || operation_group_contains_v<Alternative, SchedulingOperationGroup>
    || operation_group_contains_v<Alternative, ControlOperationGroup>
    || operation_group_contains_v<Alternative, OutputOperationGroup>;

template <typename Alternative>
struct OperationGroupSelector {
  using Type = std::remove_cvref_t<Alternative>;
  static_assert(
      is_operation_alternative_v<Type>,
      "unsupported SimIR operation alternative");
  using Group = std::conditional_t<
      operation_group_contains_v<Type, ValueOperationGroup>,
      ValueOperationGroup,
      std::conditional_t<
          operation_group_contains_v<Type, SignalOperationGroup>,
          SignalOperationGroup,
          std::conditional_t<
              operation_group_contains_v<Type, StringOperationGroup>,
              StringOperationGroup,
              std::conditional_t<
                  operation_group_contains_v<Type, ContainerOperationGroup>,
                  ContainerOperationGroup,
                  std::conditional_t<
                      operation_group_contains_v<Type, FileOperationGroup>,
                      FileOperationGroup,
                      std::conditional_t<
                          operation_group_contains_v<
                              Type, SchedulingOperationGroup>,
                          SchedulingOperationGroup,
                          std::conditional_t<
                              operation_group_contains_v<
                                  Type, ControlOperationGroup>,
                              ControlOperationGroup,
                              OutputOperationGroup>>>>>>>;
};

template <typename Alternative>
using OperationGroupFor =
    typename OperationGroupSelector<Alternative>::Group;

struct Operation {
  using Storage =
      std::variant<ValueOperationGroup, SignalOperationGroup,
                   StringOperationGroup, ContainerOperationGroup,
                   FileOperationGroup, SchedulingOperationGroup,
                   ControlOperationGroup, OutputOperationGroup>;

  Storage storage;

  Operation() = default;
  Operation(const Operation&) = default;
  Operation(Operation&&) noexcept = default;
  Operation& operator=(const Operation&) = default;
  Operation& operator=(Operation&&) noexcept = default;

  template <typename Alternative>
    requires is_operation_alternative_v<
        std::remove_cvref_t<Alternative>>
  Operation(Alternative&& operation)
      : storage(
            std::in_place_type<OperationGroupFor<Alternative>>,
            std::forward<Alternative>(operation)) {}

  template <typename Alternative>
    requires is_operation_alternative_v<
        std::remove_cvref_t<Alternative>>
  Operation& operator=(Alternative&& operation) {
    using Group = OperationGroupFor<Alternative>;
    if (auto* group = std::get_if<Group>(&storage)) {
      group->storage = std::forward<Alternative>(operation);
    } else {
      storage.emplace<Group>(
          std::forward<Alternative>(operation));
    }
    return *this;
  }
};

static_assert(
    std::variant_size_v<ValueOperationGroup::Storage>
        + std::variant_size_v<SignalOperationGroup::Storage>
        + std::variant_size_v<StringOperationGroup::Storage>
        + std::variant_size_v<ContainerOperationGroup::Storage>
        + std::variant_size_v<FileOperationGroup::Storage>
        + std::variant_size_v<SchedulingOperationGroup::Storage>
        + std::variant_size_v<ControlOperationGroup::Storage>
        + std::variant_size_v<OutputOperationGroup::Storage>
    == 119);

template <typename Alternative>
[[nodiscard]] Alternative* operation_get_if(Operation* operation) noexcept {
  if (operation == nullptr) {
    return nullptr;
  }
  using Group = OperationGroupFor<Alternative>;
  auto* group = std::get_if<Group>(&operation->storage);
  return group == nullptr
             ? nullptr
             : std::get_if<Alternative>(&group->storage);
}

template <typename Alternative>
[[nodiscard]] const Alternative* operation_get_if(
    const Operation* operation) noexcept {
  if (operation == nullptr) {
    return nullptr;
  }
  using Group = OperationGroupFor<Alternative>;
  const auto* group = std::get_if<Group>(&operation->storage);
  return group == nullptr
             ? nullptr
             : std::get_if<Alternative>(&group->storage);
}

template <typename Alternative>
[[nodiscard]] Alternative& operation_get(Operation& operation) {
  using Group = OperationGroupFor<Alternative>;
  return std::get<Alternative>(
      std::get<Group>(operation.storage).storage);
}

template <typename Alternative>
[[nodiscard]] const Alternative& operation_get(
    const Operation& operation) {
  using Group = OperationGroupFor<Alternative>;
  return std::get<Alternative>(
      std::get<Group>(operation.storage).storage);
}

template <typename Alternative>
[[nodiscard]] bool operation_holds(
    const Operation& operation) noexcept {
  return operation_get_if<Alternative>(&operation) != nullptr;
}

template <typename Visitor, typename OperationType>
decltype(auto) visit_operation(
    Visitor&& visitor, OperationType&& operation) {
  return std::visit(
      [&visitor](auto&& group) -> decltype(auto) {
        return std::visit(
            visitor,
            std::forward<decltype(group)>(group).storage);
      },
      std::forward<OperationType>(operation).storage);
}
