// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_sequence.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmRegisterModelService;

template <typename Tag> class SystemVerilogUvmRegisterModelHandle final {
public:
  SystemVerilogUvmRegisterModelHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmRegisterModelHandle &,
                         const SystemVerilogUvmRegisterModelHandle &) = default;

private:
  friend class SystemVerilogUvmRegisterModelService;
  SystemVerilogUvmRegisterModelHandle(std::shared_ptr<const void> owner,
                                      std::uint64_t slot,
                                      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

struct SystemVerilogUvmRegisterBlockTag final {};
struct SystemVerilogUvmRegisterMapTag final {};
struct SystemVerilogUvmRegisterTag final {};
struct SystemVerilogUvmRegisterFieldTag final {};
struct SystemVerilogUvmRegisterMemoryTag final {};
struct SystemVerilogUvmRegisterAdapterTag final {};
struct SystemVerilogUvmRegisterPredictorTag final {};
struct SystemVerilogUvmRegisterFrontdoorTag final {};
struct SystemVerilogUvmRegisterUserFrontdoorTag final {};
struct SystemVerilogUvmRegisterHdlPathTag final {};
struct SystemVerilogUvmRegisterCallbackTag final {};
struct SystemVerilogUvmRegisterCoverageTag final {};

using SystemVerilogUvmRegisterBlockHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterBlockTag>;
using SystemVerilogUvmRegisterMapHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterMapTag>;
using SystemVerilogUvmRegisterHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterTag>;
using SystemVerilogUvmRegisterFieldHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterFieldTag>;
using SystemVerilogUvmRegisterMemoryHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterMemoryTag>;
using SystemVerilogUvmRegisterAdapterHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterAdapterTag>;
using SystemVerilogUvmRegisterPredictorHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterPredictorTag>;
using SystemVerilogUvmRegisterFrontdoorHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterFrontdoorTag>;
using SystemVerilogUvmRegisterUserFrontdoorHandle =
    SystemVerilogUvmRegisterModelHandle<
        SystemVerilogUvmRegisterUserFrontdoorTag>;
using SystemVerilogUvmRegisterHdlPathHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterHdlPathTag>;
using SystemVerilogUvmRegisterCallbackHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterCallbackTag>;
using SystemVerilogUvmRegisterCoverageHandle =
    SystemVerilogUvmRegisterModelHandle<SystemVerilogUvmRegisterCoverageTag>;

enum class SystemVerilogUvmRegisterModelState : std::uint8_t {
  Building,
  Locked,
};

enum class SystemVerilogUvmRegisterDeclarationKind : std::uint8_t {
  Block,
  Map,
  Register,
  Field,
  Memory,
};

enum class SystemVerilogUvmRegisterAccessPolicy : std::uint8_t {
  ReadWrite,
  ReadOnly,
  WriteOnly,
  ReadClear,
  ReadSet,
  WriteReadClear,
  WriteReadSet,
  WriteClear,
  WriteSet,
  WriteSetReadClear,
  WriteClearReadSet,
  WriteOneClear,
  WriteOneSet,
  WriteOneToggle,
  WriteZeroClear,
  WriteZeroSet,
  WriteZeroToggle,
  WriteOneSetReadClear,
  WriteOneClearReadSet,
  WriteZeroSetReadClear,
  WriteZeroClearReadSet,
  WriteOnce,
  WriteOnlyClear,
  WriteOnlySet,
  WriteOnceReadWrite,
  NoAccess,
};

enum class SystemVerilogUvmRegisterComparePolicy : std::uint8_t {
  Check,
  NoCheck,
};

enum class SystemVerilogUvmRegisterPredictKind : std::uint8_t {
  Direct,
  Read,
  Write,
};

enum class SystemVerilogUvmRegisterOperationStatus : std::uint8_t {
  IsOk,
  NotOk,
  HasUnknown,
};

enum class SystemVerilogUvmRegisterMapEndianness : std::uint8_t {
  Little,
  Big,
  LittleFifo,
  BigFifo,
};

enum class SystemVerilogUvmRegisterMapRights : std::uint8_t {
  ReadWrite,
  ReadOnly,
  WriteOnly,
};

enum class SystemVerilogUvmRegisterBusOperationKind : std::uint8_t {
  Read,
  Write,
};

enum class SystemVerilogUvmRegisterFrontdoorKind : std::uint8_t {
  Read,
  Write,
  Update,
  Mirror,
};

enum class SystemVerilogUvmRegisterFrontdoorState : std::uint8_t {
  Pending,
  Completed,
  Cancelled,
  Failed,
};

enum class SystemVerilogUvmRegisterHdlKind : std::uint8_t {
  Vpi,
  Vhpi,
};

enum class SystemVerilogUvmRegisterBackdoorKind : std::uint8_t {
  Read,
  Deposit,
  Force,
  Release,
};

enum class SystemVerilogUvmRegisterStandardSequenceKind : std::uint8_t {
  Reset,
  HardwareReset,
  BitBash,
  Access,
  SharedAccess,
  MemoryAccess,
  MemoryWalk,
  Traverse,
};

enum class SystemVerilogUvmRegisterCallbackScope : std::uint8_t {
  Block,
  Map,
  Register,
  Memory,
  Field,
};

enum class SystemVerilogUvmRegisterCallbackPhase : std::uint8_t {
  PreRead,
  PostRead,
  PreWrite,
  PostWrite,
};

struct SystemVerilogUvmRegisterBlockDescriptor {
  SystemVerilogUvmRootHandle root{};
  std::optional<SystemVerilogUvmRegisterBlockHandle> parent;
  std::string name;
};

struct SystemVerilogUvmRegisterMapDescriptor {
  SystemVerilogUvmRegisterBlockHandle block;
  std::string name;
  std::uint64_t base_offset{};
  std::uint32_t bus_width_bytes{4};
  SystemVerilogUvmRegisterMapEndianness endianness{
      SystemVerilogUvmRegisterMapEndianness::Little};
  bool byte_addressing{true};
};

struct SystemVerilogUvmRegisterDescriptor {
  SystemVerilogUvmRegisterBlockHandle block;
  std::string name;
  std::uint32_t width_bits{};
  std::uint64_t offset{};
};

struct SystemVerilogUvmRegisterFieldDescriptor {
  SystemVerilogUvmRegisterHandle register_handle;
  std::string name;
  std::uint32_t width_bits{};
  std::uint32_t least_significant_bit{};
  SystemVerilogUvmRegisterAccessPolicy access{
      SystemVerilogUvmRegisterAccessPolicy::ReadWrite};
  bool is_volatile{};
  SystemVerilogUvmRegisterComparePolicy compare{
      SystemVerilogUvmRegisterComparePolicy::Check};
};

struct SystemVerilogUvmRegisterMemoryDescriptor {
  SystemVerilogUvmRegisterBlockHandle block;
  std::string name;
  std::uint32_t word_width_bits{};
  std::uint64_t offset{};
  std::vector<std::size_t> dimensions;
  SystemVerilogUvmRegisterAccessPolicy access{
      SystemVerilogUvmRegisterAccessPolicy::ReadWrite};
};

struct SystemVerilogUvmRegisterBlockSnapshot {
  SystemVerilogUvmRegisterBlockHandle handle;
  std::uint64_t identity{};
  SystemVerilogUvmRootHandle root{};
  std::optional<SystemVerilogUvmRegisterBlockHandle> parent;
  std::string name;
  std::string full_name;
  std::size_t depth{};
  SystemVerilogUvmRegisterModelState state{
      SystemVerilogUvmRegisterModelState::Building};
  std::uint64_t declaration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterBlockSnapshot &,
                         const SystemVerilogUvmRegisterBlockSnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterMapSnapshot {
  SystemVerilogUvmRegisterMapHandle handle;
  std::uint64_t identity{};
  SystemVerilogUvmRegisterBlockHandle block;
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::uint64_t base_offset{};
  std::uint32_t bus_width_bytes{4};
  SystemVerilogUvmRegisterMapEndianness endianness{
      SystemVerilogUvmRegisterMapEndianness::Little};
  bool byte_addressing{true};
  std::uint64_t declaration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterMapSnapshot &,
                         const SystemVerilogUvmRegisterMapSnapshot &) = default;
};

struct SystemVerilogUvmRegisterSnapshot {
  SystemVerilogUvmRegisterHandle handle;
  std::uint64_t identity{};
  SystemVerilogUvmRegisterBlockHandle block;
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::uint32_t width_bits{};
  std::uint64_t offset{};
  std::uint64_t declaration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterSnapshot &,
                         const SystemVerilogUvmRegisterSnapshot &) = default;
};

struct SystemVerilogUvmRegisterFieldSnapshot {
  SystemVerilogUvmRegisterFieldHandle handle;
  std::uint64_t identity{};
  SystemVerilogUvmRegisterHandle register_handle;
  SystemVerilogUvmRegisterBlockHandle block;
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::uint32_t width_bits{};
  std::uint32_t least_significant_bit{};
  std::uint64_t declaration_order{};
  SystemVerilogUvmRegisterAccessPolicy access{
      SystemVerilogUvmRegisterAccessPolicy::ReadWrite};
  bool is_volatile{};
  SystemVerilogUvmRegisterComparePolicy compare{
      SystemVerilogUvmRegisterComparePolicy::Check};

  friend bool operator==(const SystemVerilogUvmRegisterFieldSnapshot &,
                         const SystemVerilogUvmRegisterFieldSnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterMemorySnapshot {
  SystemVerilogUvmRegisterMemoryHandle handle;
  std::uint64_t identity{};
  SystemVerilogUvmRegisterBlockHandle block;
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::uint32_t word_width_bits{};
  std::uint64_t offset{};
  std::vector<std::size_t> dimensions;
  std::size_t word_count{};
  std::uint64_t declaration_order{};
  SystemVerilogUvmRegisterAccessPolicy access{
      SystemVerilogUvmRegisterAccessPolicy::ReadWrite};

  friend bool operator==(const SystemVerilogUvmRegisterMemorySnapshot &,
                         const SystemVerilogUvmRegisterMemorySnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterDeclarationSnapshot {
  SystemVerilogUvmRegisterDeclarationKind kind{
      SystemVerilogUvmRegisterDeclarationKind::Block};
  std::uint64_t identity{};
  std::uint64_t owner_identity{};
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::uint64_t declaration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterDeclarationSnapshot &,
                         const SystemVerilogUvmRegisterDeclarationSnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterValueSnapshot {
  SystemVerilogUvmRegisterHandle handle;
  PackedLogic4 desired;
  PackedLogic4 mirrored;
  bool needs_update{};

  friend bool operator==(const SystemVerilogUvmRegisterValueSnapshot &,
                         const SystemVerilogUvmRegisterValueSnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterFieldValueSnapshot {
  SystemVerilogUvmRegisterFieldHandle handle;
  PackedLogic4 desired;
  PackedLogic4 mirrored;
  bool needs_update{};
  std::map<std::string, PackedLogic4, std::less<>> reset_values;

  friend bool operator==(const SystemVerilogUvmRegisterFieldValueSnapshot &,
                         const SystemVerilogUvmRegisterFieldValueSnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterOperationResult {
  SystemVerilogUvmRegisterOperationStatus status{
      SystemVerilogUvmRegisterOperationStatus::IsOk};
  PackedLogic4 value;
  bool changed{};
  std::string message;

  [[nodiscard]] bool success() const noexcept {
    return status == SystemVerilogUvmRegisterOperationStatus::IsOk;
  }
};

struct SystemVerilogUvmRegisterCompareResult {
  SystemVerilogUvmRegisterOperationStatus status{
      SystemVerilogUvmRegisterOperationStatus::IsOk};
  bool matches{true};
  std::vector<SystemVerilogUvmRegisterFieldHandle> mismatches;
};

struct SystemVerilogUvmRegisterMapRegisterSnapshot {
  SystemVerilogUvmRegisterMapHandle map;
  SystemVerilogUvmRegisterHandle register_handle;
  std::uint64_t offset{};
  SystemVerilogUvmRegisterMapRights rights{
      SystemVerilogUvmRegisterMapRights::ReadWrite};
  bool unmapped{};
  std::uint64_t registration_order{};

  friend bool operator==(
      const SystemVerilogUvmRegisterMapRegisterSnapshot &,
      const SystemVerilogUvmRegisterMapRegisterSnapshot &) = default;
};

struct SystemVerilogUvmRegisterMapMemorySnapshot {
  SystemVerilogUvmRegisterMapHandle map;
  SystemVerilogUvmRegisterMemoryHandle memory;
  std::uint64_t offset{};
  SystemVerilogUvmRegisterMapRights rights{
      SystemVerilogUvmRegisterMapRights::ReadWrite};
  bool unmapped{};
  std::uint64_t registration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterMapMemorySnapshot &,
                         const SystemVerilogUvmRegisterMapMemorySnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterSubmapSnapshot {
  SystemVerilogUvmRegisterMapHandle parent;
  SystemVerilogUvmRegisterMapHandle child;
  std::uint64_t offset{};
  std::uint64_t registration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterSubmapSnapshot &,
                         const SystemVerilogUvmRegisterSubmapSnapshot &) =
      default;
};

struct SystemVerilogUvmRegisterBusBeat {
  std::uint64_t address{};
  std::size_t data_byte_offset{};
  std::vector<std::uint8_t> byte_enables;

  friend bool operator==(const SystemVerilogUvmRegisterBusBeat &,
                         const SystemVerilogUvmRegisterBusBeat &) = default;
};

struct SystemVerilogUvmRegisterBusItem {
  SystemVerilogUvmRegisterBusOperationKind kind{
      SystemVerilogUvmRegisterBusOperationKind::Read};
  SystemVerilogUvmRegisterMapHandle map;
  std::optional<SystemVerilogUvmRegisterHandle> register_handle;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  std::size_t memory_word_index{};
  std::uint64_t address{};
  PackedLogic4 data;
  std::vector<std::uint8_t> byte_enables;
  std::size_t data_byte_offset{};
  std::size_t beat_index{};
  std::size_t beat_count{};
  std::uint64_t operation_order{};

  friend bool operator==(const SystemVerilogUvmRegisterBusItem &,
                         const SystemVerilogUvmRegisterBusItem &) = default;
};

struct SystemVerilogUvmRegisterBusResponse {
  SystemVerilogUvmRegisterOperationStatus status{
      SystemVerilogUvmRegisterOperationStatus::IsOk};
  PackedLogic4 data;
  std::string message;

  [[nodiscard]] bool success() const noexcept {
    return status == SystemVerilogUvmRegisterOperationStatus::IsOk;
  }
};

struct SystemVerilogUvmRegisterBusObservation {
  SystemVerilogUvmRegisterBusOperationKind kind{
      SystemVerilogUvmRegisterBusOperationKind::Read};
  std::uint64_t address{};
  PackedLogic4 data;
  std::vector<std::uint8_t> byte_enables;
  std::size_t data_byte_offset{};
  SystemVerilogUvmRegisterOperationStatus status{
      SystemVerilogUvmRegisterOperationStatus::IsOk};
};

struct SystemVerilogUvmRegisterAdapterDescriptor {
  using RegToBus = std::function<SystemVerilogUvmSequenceItemHandle(
      const SystemVerilogUvmRegisterBusItem &)>;
  using Drive = std::function<std::optional<SystemVerilogUvmSequenceItemHandle>(
      const SystemVerilogUvmRegisterBusItem &,
      const SystemVerilogUvmSequenceTransactionSnapshot &)>;
  using BusToReg = std::function<SystemVerilogUvmRegisterBusResponse(
      SystemVerilogUvmSequenceItemHandle)>;

  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceHandle sequence;
  std::string name;
  std::uint32_t priority{100};
  bool auto_predict{true};
  RegToBus reg_to_bus;
  Drive drive;
  BusToReg bus_to_reg;
};

struct SystemVerilogUvmRegisterAdapterSnapshot {
  SystemVerilogUvmRegisterAdapterHandle handle;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceHandle sequence;
  std::string name;
  std::uint32_t priority{100};
  bool auto_predict{true};
  std::uint64_t registration_order{};
};

struct SystemVerilogUvmRegisterPredictorDescriptor {
  using Convert = std::function<std::optional<
      SystemVerilogUvmRegisterBusObservation>(
      const SystemVerilogUvmTlm1Payload &)>;

  SystemVerilogUvmRegisterMapHandle map;
  SystemVerilogUvmTlm1EndpointHandle analysis_implementation;
  std::string name;
  Convert convert;
};

struct SystemVerilogUvmRegisterPredictorSnapshot {
  SystemVerilogUvmRegisterPredictorHandle handle;
  SystemVerilogUvmRegisterMapHandle map;
  SystemVerilogUvmTlm1EndpointHandle analysis_implementation;
  std::string name;
  std::uint64_t registration_order{};
  std::uint64_t observations{};
  std::uint64_t predictions{};
  std::uint64_t failures{};
};

struct SystemVerilogUvmRegisterFrontdoorOptions {
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle process;
  SimulationTick timeout_ticks{};
  bool check{true};
};

struct SystemVerilogUvmRegisterFrontdoorSnapshot {
  SystemVerilogUvmRegisterFrontdoorHandle handle;
  SystemVerilogUvmRegisterFrontdoorKind kind{
      SystemVerilogUvmRegisterFrontdoorKind::Read};
  SystemVerilogUvmRegisterFrontdoorState state{
      SystemVerilogUvmRegisterFrontdoorState::Pending};
  SystemVerilogUvmRegisterAdapterHandle adapter;
  SystemVerilogUvmRegisterMapHandle map;
  std::optional<SystemVerilogUvmRegisterHandle> register_handle;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  std::size_t memory_word_index{};
  SystemVerilogUvmRegisterOperationResult result;
  std::vector<SystemVerilogUvmRegisterBusItem> bus_items;
  std::vector<SystemVerilogUvmSequenceTransactionHandle> transactions;
  std::size_t completed_beats{};
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle process;
  std::optional<SimulationTick> deadline;
  SystemVerilogUvmSequenceCancellationReason cancellation{
      SystemVerilogUvmSequenceCancellationReason::None};
  bool check{};
  bool mismatch{};
  std::uint64_t operation_order{};
};

struct SystemVerilogUvmRegisterTarget {
  std::optional<SystemVerilogUvmRegisterHandle> register_handle;
  std::optional<SystemVerilogUvmRegisterFieldHandle> field;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  std::size_t memory_word_index{};

  friend bool operator==(const SystemVerilogUvmRegisterTarget &,
                         const SystemVerilogUvmRegisterTarget &) = default;
};

struct SystemVerilogUvmRegisterUserFrontdoorDescriptor {
  using Read = std::function<SystemVerilogUvmRegisterOperationResult(
      const SystemVerilogUvmRegisterFrontdoorOptions &)>;
  using Write = std::function<SystemVerilogUvmRegisterOperationResult(
      const PackedLogic4 &, const SystemVerilogUvmRegisterFrontdoorOptions &)>;

  SystemVerilogUvmRootHandle root{};
  std::string name;
  SystemVerilogUvmRegisterTarget target;
  Read read;
  Write write;
};

struct SystemVerilogUvmRegisterUserFrontdoorSnapshot {
  SystemVerilogUvmRegisterUserFrontdoorHandle handle;
  SystemVerilogUvmRootHandle root{};
  std::string name;
  SystemVerilogUvmRegisterTarget target;
  std::uint64_t registration_order{};
  std::uint64_t reads{};
  std::uint64_t writes{};
  std::uint64_t failures{};
};

struct SystemVerilogUvmRegisterHdlSlice {
  SystemVerilogUvmRegisterHdlKind kind{
      SystemVerilogUvmRegisterHdlKind::Vpi};
  std::string path;
  std::size_t hdl_lsb{};
  std::size_t value_lsb{};
  std::size_t width{};

  friend bool operator==(const SystemVerilogUvmRegisterHdlSlice &,
                         const SystemVerilogUvmRegisterHdlSlice &) = default;
};

struct SystemVerilogUvmRegisterHdlPathDescriptor {
  SystemVerilogUvmRootHandle root{};
  std::string abstraction;
  SystemVerilogUvmRegisterTarget target;
  std::vector<SystemVerilogUvmRegisterHdlSlice> slices;
};

struct SystemVerilogUvmRegisterHdlPathSnapshot {
  SystemVerilogUvmRegisterHdlPathHandle handle;
  SystemVerilogUvmRootHandle root{};
  std::string abstraction;
  SystemVerilogUvmRegisterTarget target;
  std::vector<SystemVerilogUvmRegisterHdlSlice> slices;
  std::uint64_t registration_order{};
  std::uint64_t accesses{};
};

struct SystemVerilogUvmRegisterBackdoorTransport {
  using Resolve = std::function<std::optional<std::size_t>(
      SystemVerilogUvmRegisterHdlKind, std::string_view)>;
  using Read = std::function<PackedLogic4(
      SystemVerilogUvmRegisterHdlKind, std::string_view)>;
  using Write = std::function<void(SystemVerilogUvmRegisterHdlKind,
                                   std::string_view,
                                   SystemVerilogUvmRegisterBackdoorKind,
                                   const PackedLogic4 &)>;

  Resolve resolve;
  Read read;
  Write write;
};

struct SystemVerilogUvmRegisterAddressLookup {
  std::optional<SystemVerilogUvmRegisterHandle> register_handle;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  std::size_t memory_word_index{};
  SystemVerilogUvmRegisterMapRights rights{
      SystemVerilogUvmRegisterMapRights::ReadWrite};
  std::uint64_t registration_order{};

  friend bool operator==(const SystemVerilogUvmRegisterAddressLookup &,
                         const SystemVerilogUvmRegisterAddressLookup &) =
      default;
};

struct SystemVerilogUvmRegisterMemoryBurstResult {
  SystemVerilogUvmRegisterOperationStatus status{
      SystemVerilogUvmRegisterOperationStatus::IsOk};
  std::vector<PackedLogic4> values;
  std::size_t completed_words{};
  bool changed{};
  std::string message;

  [[nodiscard]] bool success() const noexcept {
    return status == SystemVerilogUvmRegisterOperationStatus::IsOk;
  }
};

struct SystemVerilogUvmRegisterStandardSequenceExclusion {
  std::string full_name;
  std::uint32_t kinds{UINT32_MAX};
};

struct SystemVerilogUvmRegisterStandardSequenceOptions {
  using Read = std::function<SystemVerilogUvmRegisterOperationResult(
      const SystemVerilogUvmRegisterTarget &)>;
  using Write = std::function<SystemVerilogUvmRegisterOperationResult(
      const SystemVerilogUvmRegisterTarget &, const PackedLogic4 &)>;
  using HardwareReset = std::function<void()>;

  SystemVerilogUvmRegisterStandardSequenceKind kind{
      SystemVerilogUvmRegisterStandardSequenceKind::Traverse};
  SystemVerilogUvmRegisterBlockHandle top;
  std::optional<SystemVerilogUvmRegisterMapHandle> map;
  std::string reset_kind{"HARD"};
  std::uint64_t seed{1};
  std::size_t maximum_failures{64};
  std::vector<SystemVerilogUvmRegisterStandardSequenceExclusion> exclusions;
  Read read;
  Write write;
  HardwareReset hardware_reset;
};

struct SystemVerilogUvmRegisterStandardSequenceFailure {
  std::string target;
  std::string message;

  friend bool operator==(
      const SystemVerilogUvmRegisterStandardSequenceFailure &,
      const SystemVerilogUvmRegisterStandardSequenceFailure &) = default;
};

struct SystemVerilogUvmRegisterStandardSequenceSnapshot {
  SystemVerilogUvmRegisterStandardSequenceKind kind{
      SystemVerilogUvmRegisterStandardSequenceKind::Traverse};
  SystemVerilogUvmRegisterBlockHandle top;
  std::optional<SystemVerilogUvmRegisterMapHandle> map;
  std::string reset_kind;
  std::uint64_t seed{};
  std::uint64_t final_random_state{};
  std::uint64_t execution_order{};
  std::size_t excluded{};
  std::size_t visited_blocks{};
  std::size_t visited_maps{};
  std::size_t visited_registers{};
  std::size_t visited_fields{};
  std::size_t visited_memories{};
  std::size_t operations{};
  std::size_t reads{};
  std::size_t writes{};
  std::size_t comparisons{};
  std::vector<SystemVerilogUvmRegisterStandardSequenceFailure> failures;

  [[nodiscard]] bool success() const noexcept { return failures.empty(); }
};

struct SystemVerilogUvmRegisterCallbackContext {
  SystemVerilogUvmRegisterCallbackPhase phase{
      SystemVerilogUvmRegisterCallbackPhase::PreRead};
  SystemVerilogUvmRegisterTarget target;
  std::optional<SystemVerilogUvmRegisterMapHandle> map;
  SystemVerilogUvmRegisterCallbackScope scope{
      SystemVerilogUvmRegisterCallbackScope::Block};
  std::optional<SystemVerilogUvmRegisterBlockHandle> block;
  std::optional<SystemVerilogUvmRegisterHandle> reg;
  std::optional<SystemVerilogUvmRegisterFieldHandle> field;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  PackedLogic4 value;
  SystemVerilogUvmRegisterOperationStatus status{
      SystemVerilogUvmRegisterOperationStatus::IsOk};
  std::string message;
  bool proceed{true};
};

struct SystemVerilogUvmRegisterCallbackDescriptor {
  using Callback = std::function<void(SystemVerilogUvmRegisterCallbackContext &)>;

  SystemVerilogUvmRegisterCallbackScope scope{
      SystemVerilogUvmRegisterCallbackScope::Block};
  std::optional<SystemVerilogUvmRegisterBlockHandle> block;
  std::optional<SystemVerilogUvmRegisterMapHandle> map;
  std::optional<SystemVerilogUvmRegisterHandle> reg;
  std::optional<SystemVerilogUvmRegisterFieldHandle> field;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  std::string name;
  std::uint32_t phases{UINT32_C(0x0f)};
  std::int32_t priority{};
  Callback callback;
};

struct SystemVerilogUvmRegisterCallbackSnapshot {
  SystemVerilogUvmRegisterCallbackHandle handle;
  SystemVerilogUvmRegisterCallbackScope scope{
      SystemVerilogUvmRegisterCallbackScope::Block};
  std::optional<SystemVerilogUvmRegisterBlockHandle> block;
  std::optional<SystemVerilogUvmRegisterMapHandle> map;
  std::optional<SystemVerilogUvmRegisterHandle> reg;
  std::optional<SystemVerilogUvmRegisterFieldHandle> field;
  std::optional<SystemVerilogUvmRegisterMemoryHandle> memory;
  std::string name;
  std::uint32_t phases{};
  std::int32_t priority{};
  std::uint64_t registration_order{};
  std::uint64_t invocations{};
  std::uint64_t failures{};
};

struct SystemVerilogUvmRegisterCoverageDescriptor {
  SystemVerilogUvmRegisterBlockHandle block;
  std::string name;
  bool per_map{true};
  bool per_field{true};
  bool reset_desired_mirror_cross{true};
};

struct SystemVerilogUvmRegisterCoverageSnapshot {
  SystemVerilogUvmRegisterCoverageHandle handle;
  SystemVerilogUvmRegisterBlockHandle block;
  std::string name;
  bool per_map{};
  bool per_field{};
  bool reset_desired_mirror_cross{};
  std::uint64_t registration_order{};
  std::uint64_t samples{};
  std::map<std::string, std::uint64_t, std::less<>> bins;
};

struct SystemVerilogUvmRegisterModelLimits {
  std::size_t maximum_blocks{4'096};
  std::size_t maximum_maps{4'096};
  std::size_t maximum_registers{65'536};
  std::size_t maximum_fields{262'144};
  std::size_t maximum_memories{16'384};
  std::size_t maximum_declarations{524'288};
  std::size_t maximum_declarations_per_block{65'536};
  std::size_t maximum_block_depth{256};
  std::size_t maximum_name_bytes{4'096};
  std::size_t maximum_full_name_bytes{1U << 20U};
  std::uint32_t maximum_width_bits{1U << 20U};
  std::size_t maximum_register_value_bits{1U << 28U};
  std::size_t maximum_dimensions{32};
  std::size_t maximum_dimension_extent{1U << 30U};
  std::size_t maximum_memory_words{
      static_cast<std::size_t>(UINT64_C(1) << 32U)};
  std::size_t maximum_reset_kinds_per_field{64};
  std::size_t maximum_reset_name_bytes{4'096};
  std::size_t maximum_reset_value_bits{1U << 28U};
  std::size_t maximum_materialized_memory_words{1U << 20U};
  std::size_t maximum_materialized_memory_bits{1U << 28U};
  std::size_t maximum_map_registrations{1U << 20U};
  std::size_t maximum_map_registrations_per_map{65'536};
  std::size_t maximum_submap_depth{256};
  std::uint32_t maximum_bus_width_bytes{4'096};
  std::size_t maximum_physical_beats{1U << 20U};
  std::size_t maximum_physical_byte_enables{1U << 26U};
  std::size_t maximum_burst_words{1U << 20U};
  std::size_t maximum_burst_value_bits{1U << 28U};
  std::size_t maximum_address_lookup_work{1U << 20U};
  std::size_t maximum_adapters{4'096};
  std::size_t maximum_predictors{4'096};
  std::size_t maximum_frontdoor_operations{1U << 20U};
  std::size_t maximum_pending_frontdoor_operations{65'536};
  std::size_t maximum_frontdoor_bus_items{1U << 20U};
  std::size_t maximum_frontdoor_value_bits{1U << 28U};
  std::size_t maximum_frontdoor_byte_enables{1U << 26U};
  std::size_t maximum_predictor_observations{1U << 24U};
  std::size_t maximum_user_frontdoors{4'096};
  std::size_t maximum_hdl_paths{1U << 20U};
  std::size_t maximum_hdl_slices{1U << 20U};
  std::size_t maximum_hdl_path_bytes{1U << 24U};
  std::size_t maximum_backdoor_operations{1U << 24U};
  std::size_t maximum_backdoor_value_bits{1U << 28U};
  std::size_t maximum_standard_sequences{1U << 20U};
  std::size_t maximum_standard_sequence_operations{1U << 24U};
  std::size_t maximum_standard_sequence_failures{1U << 20U};
  std::size_t maximum_standard_sequence_failure_bytes{1U << 24U};
  std::size_t maximum_standard_sequence_exclusions{65'536};
  std::size_t maximum_standard_sequence_exclusion_bytes{1U << 24U};
  std::size_t maximum_register_callbacks{65'536};
  std::size_t maximum_register_callbacks_per_access{65'536};
  std::size_t maximum_register_callback_invocations{1U << 24U};
  std::size_t maximum_register_callback_failures{1U << 20U};
  std::size_t maximum_register_coverage_models{4'096};
  std::size_t maximum_register_coverage_samples{1U << 24U};
  std::size_t maximum_register_coverage_bins{1U << 20U};
  std::size_t maximum_register_coverage_bin_bytes{1U << 24U};
  std::size_t maximum_operations{1U << 24U};
  std::size_t maximum_mutations{1U << 24U};
};

class SystemVerilogUvmRegisterModelError final : public std::runtime_error {
public:
  SystemVerilogUvmRegisterModelError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

private:
  std::string code_;
};

class SystemVerilogUvmRegisterModelService final {
public:
  SystemVerilogUvmRegisterModelService(
      SystemVerilogUvmComponentService &components,
      SystemVerilogUvmRegisterModelLimits limits = {});

  void set_activity_service(SystemVerilogUvmActivityService &activity) noexcept {
    activity_ = &activity;
  }

  [[nodiscard]] SystemVerilogUvmRegisterBlockHandle
  create_block(SystemVerilogUvmRegisterBlockDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterMapHandle
  create_map(SystemVerilogUvmRegisterMapDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterHandle
  create_register(SystemVerilogUvmRegisterDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterFieldHandle
  create_field(SystemVerilogUvmRegisterFieldDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterMemoryHandle
  create_memory(SystemVerilogUvmRegisterMemoryDescriptor descriptor);
  void configure_field(SystemVerilogUvmRegisterFieldHandle field,
                       SystemVerilogUvmRegisterAccessPolicy access,
                       bool is_volatile,
                       SystemVerilogUvmRegisterComparePolicy compare);
  void set_reset(SystemVerilogUvmRegisterFieldHandle field,
                 std::string kind, PackedLogic4 value);
  void set_memory_access(SystemVerilogUvmRegisterMemoryHandle memory,
                         SystemVerilogUvmRegisterAccessPolicy access);
  void add_register(SystemVerilogUvmRegisterMapHandle map,
                    SystemVerilogUvmRegisterHandle reg, std::uint64_t offset,
                    SystemVerilogUvmRegisterMapRights rights =
                        SystemVerilogUvmRegisterMapRights::ReadWrite,
                    bool unmapped = false);
  void add_memory(SystemVerilogUvmRegisterMapHandle map,
                  SystemVerilogUvmRegisterMemoryHandle memory,
                  std::uint64_t offset,
                  SystemVerilogUvmRegisterMapRights rights =
                      SystemVerilogUvmRegisterMapRights::ReadWrite,
                  bool unmapped = false);
  void add_submap(SystemVerilogUvmRegisterMapHandle parent,
                  SystemVerilogUvmRegisterMapHandle child,
                  std::uint64_t offset);
  void lock_model(SystemVerilogUvmRegisterBlockHandle top);

  void set(SystemVerilogUvmRegisterFieldHandle field, PackedLogic4 value);
  void set(SystemVerilogUvmRegisterHandle reg, PackedLogic4 value);
  [[nodiscard]] PackedLogic4
  get(SystemVerilogUvmRegisterFieldHandle field) const;
  [[nodiscard]] PackedLogic4 get(SystemVerilogUvmRegisterHandle reg) const;
  [[nodiscard]] PackedLogic4
  get_mirrored(SystemVerilogUvmRegisterFieldHandle field) const;
  [[nodiscard]] PackedLogic4
  get_mirrored(SystemVerilogUvmRegisterHandle reg) const;
  [[nodiscard]] SystemVerilogUvmRegisterFieldValueSnapshot
  value_snapshot(SystemVerilogUvmRegisterFieldHandle field) const;
  [[nodiscard]] SystemVerilogUvmRegisterValueSnapshot
  value_snapshot(SystemVerilogUvmRegisterHandle reg) const;
  [[nodiscard]] bool needs_update(SystemVerilogUvmRegisterHandle reg) const;
  [[nodiscard]] SystemVerilogUvmRegisterCompareResult
  compare(SystemVerilogUvmRegisterHandle reg, const PackedLogic4 &expected) const;

  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  predict(SystemVerilogUvmRegisterFieldHandle field, PackedLogic4 value,
          SystemVerilogUvmRegisterPredictKind kind);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  predict(SystemVerilogUvmRegisterHandle reg, PackedLogic4 value,
          SystemVerilogUvmRegisterPredictKind kind);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  read(SystemVerilogUvmRegisterFieldHandle field);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  read(SystemVerilogUvmRegisterHandle reg);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  write(SystemVerilogUvmRegisterFieldHandle field, PackedLogic4 value);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  write(SystemVerilogUvmRegisterHandle reg, PackedLogic4 value);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  read(SystemVerilogUvmRegisterMemoryHandle memory,
       std::span<const std::size_t> indices);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  write(SystemVerilogUvmRegisterMemoryHandle memory,
        std::span<const std::size_t> indices, PackedLogic4 value);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  write(SystemVerilogUvmRegisterMemoryHandle memory,
        std::span<const std::size_t> indices, PackedLogic4 value,
        std::span<const std::uint8_t> byte_enables);
  void reset(SystemVerilogUvmRegisterBlockHandle top, std::string_view kind);
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterBusBeat>
  physical_accesses(SystemVerilogUvmRegisterMapHandle map,
                    std::uint64_t offset, std::size_t byte_count,
                    std::size_t memory_word_offset = 0,
                    std::size_t memory_word_bytes = 0) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterBusBeat>
  register_accesses(SystemVerilogUvmRegisterMapHandle map,
                    SystemVerilogUvmRegisterHandle reg) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterBusBeat>
  memory_accesses(SystemVerilogUvmRegisterMapHandle map,
                  SystemVerilogUvmRegisterMemoryHandle memory,
                  std::size_t word_index) const;
  [[nodiscard]] std::optional<SystemVerilogUvmRegisterAddressLookup>
  lookup(SystemVerilogUvmRegisterMapHandle map, std::uint64_t address,
         bool read) const;
  [[nodiscard]] SystemVerilogUvmRegisterMemoryBurstResult
  read_burst(SystemVerilogUvmRegisterMapHandle map,
             SystemVerilogUvmRegisterMemoryHandle memory,
             std::size_t first_word, std::size_t word_count);
  [[nodiscard]] SystemVerilogUvmRegisterMemoryBurstResult
  write_burst(SystemVerilogUvmRegisterMapHandle map,
              SystemVerilogUvmRegisterMemoryHandle memory,
              std::size_t first_word, std::span<const PackedLogic4> values,
              std::span<const std::vector<std::uint8_t>> byte_enables = {});

  void set_frontdoor_services(SystemVerilogUvmSequenceService &sequences,
                              SystemVerilogUvmTlm1Service &tlm1,
                              SystemVerilogUvmPhaseService &phases) noexcept;
  [[nodiscard]] SystemVerilogUvmRegisterAdapterHandle
  register_adapter(SystemVerilogUvmRegisterAdapterDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterPredictorHandle
  register_predictor(SystemVerilogUvmRegisterPredictorDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_read(SystemVerilogUvmRegisterAdapterHandle adapter,
                 SystemVerilogUvmRegisterMapHandle map,
                 SystemVerilogUvmRegisterHandle reg,
                 SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_write(SystemVerilogUvmRegisterAdapterHandle adapter,
                  SystemVerilogUvmRegisterMapHandle map,
                  SystemVerilogUvmRegisterHandle reg, PackedLogic4 value,
                  SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_update(SystemVerilogUvmRegisterAdapterHandle adapter,
                   SystemVerilogUvmRegisterMapHandle map,
                   SystemVerilogUvmRegisterHandle reg,
                   SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_mirror(SystemVerilogUvmRegisterAdapterHandle adapter,
                   SystemVerilogUvmRegisterMapHandle map,
                   SystemVerilogUvmRegisterHandle reg,
                   SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_read(SystemVerilogUvmRegisterAdapterHandle adapter,
                 SystemVerilogUvmRegisterMapHandle map,
                 SystemVerilogUvmRegisterMemoryHandle memory,
                 std::size_t word_index,
                 SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_write(SystemVerilogUvmRegisterAdapterHandle adapter,
                  SystemVerilogUvmRegisterMapHandle map,
                  SystemVerilogUvmRegisterMemoryHandle memory,
                  std::size_t word_index, PackedLogic4 value,
                  SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  complete_frontdoor(SystemVerilogUvmRegisterFrontdoorHandle operation,
                     SystemVerilogUvmSequenceItemHandle response);
  void cancel_frontdoor(SystemVerilogUvmRegisterFrontdoorHandle operation,
                        SystemVerilogUvmSequenceCancellationReason reason =
                            SystemVerilogUvmSequenceCancellationReason::Explicit);
  void cancel_phase_frontdoors(SystemVerilogUvmPhaseHandle phase,
                               SystemVerilogUvmSequenceCancellationReason reason =
                                   SystemVerilogUvmSequenceCancellationReason::PhaseJumped);
  void advance_frontdoor_time(SimulationTick ticks);
  void synchronize_frontdoors();
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  predict_bus(SystemVerilogUvmRegisterPredictorHandle predictor,
              const SystemVerilogUvmRegisterBusObservation &observation);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  predict(SystemVerilogUvmRegisterMemoryHandle memory,
          std::span<const std::size_t> indices, PackedLogic4 value,
          SystemVerilogUvmRegisterPredictKind kind);

  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterAdapterHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterPredictorHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterFrontdoorHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogUvmRegisterAdapterSnapshot
  adapter_snapshot(SystemVerilogUvmRegisterAdapterHandle adapter) const;
  [[nodiscard]] SystemVerilogUvmRegisterPredictorSnapshot
  predictor_snapshot(SystemVerilogUvmRegisterPredictorHandle predictor) const;
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot
  frontdoor_snapshot(SystemVerilogUvmRegisterFrontdoorHandle operation) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterAdapterSnapshot>
  adapters() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterPredictorSnapshot>
  predictors() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterFrontdoorSnapshot>
  frontdoor_operations() const;

  void set_backdoor_transport(
      SystemVerilogUvmRegisterBackdoorTransport transport) noexcept;
  [[nodiscard]] SystemVerilogUvmRegisterUserFrontdoorHandle
  register_user_frontdoor(
      SystemVerilogUvmRegisterUserFrontdoorDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult user_frontdoor_read(
      SystemVerilogUvmRegisterUserFrontdoorHandle frontdoor,
      SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult user_frontdoor_write(
      SystemVerilogUvmRegisterUserFrontdoorHandle frontdoor, PackedLogic4 value,
      SystemVerilogUvmRegisterFrontdoorOptions options = {});
  [[nodiscard]] SystemVerilogUvmRegisterHdlPathHandle
  register_hdl_path(SystemVerilogUvmRegisterHdlPathDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult backdoor_read(
      SystemVerilogUvmRegisterHdlPathHandle path);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult backdoor_write(
      SystemVerilogUvmRegisterHdlPathHandle path, PackedLogic4 value,
      SystemVerilogUvmRegisterBackdoorKind kind =
          SystemVerilogUvmRegisterBackdoorKind::Deposit);
  [[nodiscard]] bool contains(
      SystemVerilogUvmRegisterUserFrontdoorHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterHdlPathHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogUvmRegisterUserFrontdoorSnapshot
  user_frontdoor_snapshot(
      SystemVerilogUvmRegisterUserFrontdoorHandle frontdoor) const;
  [[nodiscard]] SystemVerilogUvmRegisterHdlPathSnapshot
  hdl_path_snapshot(SystemVerilogUvmRegisterHdlPathHandle path) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterUserFrontdoorSnapshot>
  user_frontdoors() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterHdlPathSnapshot>
  hdl_paths() const;

  [[nodiscard]] SystemVerilogUvmRegisterStandardSequenceSnapshot
  run_standard_sequence(
      const SystemVerilogUvmRegisterStandardSequenceOptions &options);
  [[nodiscard]] const std::vector<
      SystemVerilogUvmRegisterStandardSequenceSnapshot> &
  standard_sequences() const noexcept {
    return standard_sequences_;
  }

  [[nodiscard]] SystemVerilogUvmRegisterCallbackHandle
  register_callback(SystemVerilogUvmRegisterCallbackDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterCoverageHandle register_coverage_model(
      SystemVerilogUvmRegisterCoverageDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult callback_read(
      SystemVerilogUvmRegisterTarget target,
      std::optional<SystemVerilogUvmRegisterMapHandle> map = std::nullopt);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult callback_write(
      SystemVerilogUvmRegisterTarget target, PackedLogic4 value,
      std::optional<SystemVerilogUvmRegisterMapHandle> map = std::nullopt);
  [[nodiscard]] SystemVerilogUvmRegisterCallbackSnapshot callback_snapshot(
      SystemVerilogUvmRegisterCallbackHandle callback) const;
  [[nodiscard]] SystemVerilogUvmRegisterCoverageSnapshot coverage_snapshot(
      SystemVerilogUvmRegisterCoverageHandle coverage) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterCallbackSnapshot>
  register_callbacks() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterCoverageSnapshot>
  coverage_models() const;

  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterBlockHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterMapHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterFieldHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmRegisterMemoryHandle handle) const noexcept;

  [[nodiscard]] SystemVerilogUvmRegisterBlockSnapshot
  snapshot(SystemVerilogUvmRegisterBlockHandle handle) const;
  [[nodiscard]] SystemVerilogUvmRegisterMapSnapshot
  snapshot(SystemVerilogUvmRegisterMapHandle handle) const;
  [[nodiscard]] SystemVerilogUvmRegisterSnapshot
  snapshot(SystemVerilogUvmRegisterHandle handle) const;
  [[nodiscard]] SystemVerilogUvmRegisterFieldSnapshot
  snapshot(SystemVerilogUvmRegisterFieldHandle handle) const;
  [[nodiscard]] SystemVerilogUvmRegisterMemorySnapshot
  snapshot(SystemVerilogUvmRegisterMemoryHandle handle) const;

  [[nodiscard]] std::vector<SystemVerilogUvmRegisterBlockSnapshot>
  blocks() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterMapSnapshot> maps() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterSnapshot> registers() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterFieldSnapshot>
  fields() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterMemorySnapshot>
  memories() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterDeclarationSnapshot>
  declarations() const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterMapRegisterSnapshot>
  mapped_registers(SystemVerilogUvmRegisterMapHandle map,
                   bool hierarchical = false) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterMapMemorySnapshot>
  mapped_memories(SystemVerilogUvmRegisterMapHandle map,
                  bool hierarchical = false) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterSubmapSnapshot>
  submaps(SystemVerilogUvmRegisterMapHandle map,
          bool hierarchical = false) const;

  [[nodiscard]] std::uint64_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmRegisterModelLimits &limits() const
      noexcept {
    return limits_;
  }

private:
  template <typename Handle, typename Snapshot>
  struct Entry {
    Snapshot snapshot;
    std::uint64_t generation{1};
  };

  using BlockEntry = Entry<SystemVerilogUvmRegisterBlockHandle,
                           SystemVerilogUvmRegisterBlockSnapshot>;
  using MapEntry = Entry<SystemVerilogUvmRegisterMapHandle,
                         SystemVerilogUvmRegisterMapSnapshot>;
  using RegisterEntry = Entry<SystemVerilogUvmRegisterHandle,
                              SystemVerilogUvmRegisterSnapshot>;
  using FieldEntry = Entry<SystemVerilogUvmRegisterFieldHandle,
                           SystemVerilogUvmRegisterFieldSnapshot>;
  using MemoryEntry = Entry<SystemVerilogUvmRegisterMemoryHandle,
                            SystemVerilogUvmRegisterMemorySnapshot>;

  struct AdapterEntry {
    SystemVerilogUvmRegisterAdapterSnapshot snapshot;
    SystemVerilogUvmRegisterAdapterDescriptor::RegToBus reg_to_bus;
    SystemVerilogUvmRegisterAdapterDescriptor::Drive drive;
    SystemVerilogUvmRegisterAdapterDescriptor::BusToReg bus_to_reg;
    std::uint64_t generation{1};
  };

  struct PredictorEntry {
    SystemVerilogUvmRegisterPredictorSnapshot snapshot;
    SystemVerilogUvmRegisterPredictorDescriptor::Convert convert;
    std::uint64_t generation{1};
  };

  struct FrontdoorEntry {
    SystemVerilogUvmRegisterFrontdoorSnapshot snapshot;
    PackedLogic4 requested_value;
    PackedLogic4 original_mirror;
    std::optional<SystemVerilogUvmSequenceTransactionHandle>
        pending_transaction;
    std::uint64_t generation{1};
  };

  struct UserFrontdoorEntry {
    SystemVerilogUvmRegisterUserFrontdoorSnapshot snapshot;
    SystemVerilogUvmRegisterUserFrontdoorDescriptor::Read read;
    SystemVerilogUvmRegisterUserFrontdoorDescriptor::Write write;
    std::uint64_t generation{1};
  };

  struct HdlPathEntry {
    SystemVerilogUvmRegisterHdlPathSnapshot snapshot;
    std::uint64_t generation{1};
  };

  struct RegisterCallbackEntry {
    SystemVerilogUvmRegisterCallbackSnapshot snapshot;
    SystemVerilogUvmRegisterCallbackDescriptor::Callback callback;
    std::uint64_t generation{1};
  };

  struct RegisterCoverageEntry {
    SystemVerilogUvmRegisterCoverageSnapshot snapshot;
    std::uint64_t generation{1};
  };

  struct ResolvedHdlSlice {
    const SystemVerilogUvmRegisterHdlSlice *slice{};
    PackedLogic4 original;
    PackedLogic4 updated;
  };

  [[nodiscard]] BlockEntry &block(SystemVerilogUvmRegisterBlockHandle handle);
  [[nodiscard]] const BlockEntry &
  block(SystemVerilogUvmRegisterBlockHandle handle) const;
  [[nodiscard]] const MapEntry &
  map(SystemVerilogUvmRegisterMapHandle handle) const;
  [[nodiscard]] MapEntry &map(SystemVerilogUvmRegisterMapHandle handle);
  [[nodiscard]] const RegisterEntry &
  reg(SystemVerilogUvmRegisterHandle handle) const;
  [[nodiscard]] RegisterEntry &reg(SystemVerilogUvmRegisterHandle handle);
  [[nodiscard]] const FieldEntry &
  field(SystemVerilogUvmRegisterFieldHandle handle) const;
  [[nodiscard]] FieldEntry &field(SystemVerilogUvmRegisterFieldHandle handle);
  [[nodiscard]] const MemoryEntry &
  memory(SystemVerilogUvmRegisterMemoryHandle handle) const;
  [[nodiscard]] MemoryEntry &memory(SystemVerilogUvmRegisterMemoryHandle handle);
  [[nodiscard]] AdapterEntry &
  adapter(SystemVerilogUvmRegisterAdapterHandle handle);
  [[nodiscard]] const AdapterEntry &
  adapter(SystemVerilogUvmRegisterAdapterHandle handle) const;
  [[nodiscard]] PredictorEntry &
  predictor(SystemVerilogUvmRegisterPredictorHandle handle);
  [[nodiscard]] const PredictorEntry &
  predictor(SystemVerilogUvmRegisterPredictorHandle handle) const;
  [[nodiscard]] FrontdoorEntry &
  frontdoor(SystemVerilogUvmRegisterFrontdoorHandle handle);
  [[nodiscard]] const FrontdoorEntry &
  frontdoor(SystemVerilogUvmRegisterFrontdoorHandle handle) const;
  [[nodiscard]] UserFrontdoorEntry &user_frontdoor(
      SystemVerilogUvmRegisterUserFrontdoorHandle handle);
  [[nodiscard]] const UserFrontdoorEntry &user_frontdoor(
      SystemVerilogUvmRegisterUserFrontdoorHandle handle) const;
  [[nodiscard]] HdlPathEntry &
  hdl_path(SystemVerilogUvmRegisterHdlPathHandle handle);
  [[nodiscard]] const HdlPathEntry &
  hdl_path(SystemVerilogUvmRegisterHdlPathHandle handle) const;
  [[nodiscard]] RegisterCallbackEntry &
  register_callback_entry(SystemVerilogUvmRegisterCallbackHandle handle);
  [[nodiscard]] const RegisterCallbackEntry &register_callback_entry(
      SystemVerilogUvmRegisterCallbackHandle handle) const;
  [[nodiscard]] RegisterCoverageEntry &
  coverage_entry(SystemVerilogUvmRegisterCoverageHandle handle);
  [[nodiscard]] const RegisterCoverageEntry &
  coverage_entry(SystemVerilogUvmRegisterCoverageHandle handle) const;
  [[nodiscard]] std::pair<SystemVerilogUvmRootHandle, std::size_t>
  target_shape(const SystemVerilogUvmRegisterTarget &target) const;
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult predict_target(
      const SystemVerilogUvmRegisterTarget &target, PackedLogic4 value,
      SystemVerilogUvmRegisterPredictKind kind);
  [[nodiscard]] PackedLogic4
  read_hdl_value(const HdlPathEntry &entry, std::size_t width) const;
  [[nodiscard]] std::size_t validate_hdl_slices(
      const SystemVerilogUvmRegisterHdlPathDescriptor &descriptor,
      std::size_t width) const;
  [[nodiscard]] std::vector<ResolvedHdlSlice> resolve_hdl_write(
      const HdlPathEntry &entry, const PackedLogic4 &value,
      SystemVerilogUvmRegisterBackdoorKind kind) const;
  void apply_hdl_write(std::span<const ResolvedHdlSlice> slices,
                       SystemVerilogUvmRegisterBackdoorKind kind);
  void rollback_hdl_write(std::span<const ResolvedHdlSlice> slices,
                          std::size_t applied,
                          SystemVerilogUvmRegisterBackdoorKind kind) noexcept;
  [[nodiscard]] SystemVerilogUvmRegisterBlockHandle
  target_block(const SystemVerilogUvmRegisterTarget &target) const;
  [[nodiscard]] bool callback_matches(
      const SystemVerilogUvmRegisterCallbackSnapshot &callback,
      const SystemVerilogUvmRegisterCallbackContext &context) const;
  [[nodiscard]] bool dispatch_register_callbacks(
      SystemVerilogUvmRegisterCallbackContext &context, bool reverse);
  [[nodiscard]] std::vector<RegisterCallbackEntry *>
  matching_register_callbacks(
      const SystemVerilogUvmRegisterCallbackContext &context);
  [[nodiscard]] bool invoke_register_callback(
      RegisterCallbackEntry &entry,
      SystemVerilogUvmRegisterCallbackContext &context);
  void record_register_callback_failure(
      RegisterCallbackEntry &entry,
      SystemVerilogUvmRegisterCallbackContext &context, std::string message);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult
  direct_target_read(const SystemVerilogUvmRegisterTarget &target);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult direct_target_write(
      const SystemVerilogUvmRegisterTarget &target, PackedLogic4 value);
  void validate_callback_map(const SystemVerilogUvmRegisterTarget &target,
                             SystemVerilogUvmRegisterMapHandle map,
                             bool read) const;
  void sample_register_coverage(
      const SystemVerilogUvmRegisterTarget &target,
      std::optional<SystemVerilogUvmRegisterMapHandle> map, bool read);
  void sample_coverage_model(
      RegisterCoverageEntry &coverage,
      const SystemVerilogUvmRegisterTarget &target,
      std::optional<SystemVerilogUvmRegisterMapHandle> map, bool read);
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterFieldSnapshot>
  coverage_fields(const SystemVerilogUvmRegisterTarget &target) const;
  void sample_field_coverage(
      RegisterCoverageEntry &coverage,
      const SystemVerilogUvmRegisterFieldSnapshot &field);
  void sample_coverage_bin(RegisterCoverageEntry &coverage, std::string key);
  void validate_name(std::string_view name) const;
  void validate_declaration_capacity(std::size_t current,
                                     std::size_t maximum) const;
  void require_building(SystemVerilogUvmRegisterBlockHandle handle) const;
  void reserve_child_name(SystemVerilogUvmRegisterBlockHandle owner,
                          std::string_view name);
  void freeze_subtree(SystemVerilogUvmRegisterBlockHandle handle);
  [[nodiscard]] std::string child_full_name(
      const SystemVerilogUvmRegisterBlockSnapshot &owner,
      std::string_view name) const;
  [[nodiscard]] std::uint64_t next_identity();
  [[nodiscard]] std::uint64_t next_order();
  void record_declaration(SystemVerilogUvmRegisterDeclarationKind kind,
                          std::uint64_t identity,
                          std::uint64_t owner_identity,
                          SystemVerilogUvmRootHandle root,
                          std::string name, std::string full_name,
                          std::uint64_t order);
  void record_mutation();
  void require_frontdoor_services() const;
  [[nodiscard]] SystemVerilogUvmRegisterFrontdoorSnapshot begin_frontdoor(
      SystemVerilogUvmRegisterAdapterHandle adapter,
      SystemVerilogUvmRegisterMapHandle map,
      std::optional<SystemVerilogUvmRegisterHandle> reg,
      std::optional<SystemVerilogUvmRegisterMemoryHandle> memory,
      std::size_t memory_word_index, SystemVerilogUvmRegisterFrontdoorKind kind,
      PackedLogic4 value, SystemVerilogUvmRegisterFrontdoorOptions options);
  void continue_frontdoor(FrontdoorEntry &operation,
                          std::optional<SystemVerilogUvmSequenceItemHandle>
                              supplied_response = std::nullopt);
  [[nodiscard]] bool drive_frontdoor_beat(
      FrontdoorEntry &operation,
      SystemVerilogUvmSequenceRequestHandle &queued_request);
  void cancel_frontdoor_transport(
      FrontdoorEntry &operation,
      SystemVerilogUvmSequenceRequestHandle queued_request) noexcept;
  void consume_frontdoor_response(
      FrontdoorEntry &operation,
      SystemVerilogUvmSequenceItemHandle response);
  void finish_frontdoor(FrontdoorEntry &operation);
  void fail_frontdoor(FrontdoorEntry &operation,
                      SystemVerilogUvmRegisterOperationStatus status,
                      std::string message);
  [[nodiscard]] SystemVerilogUvmRegisterOperationResult predict_observation(
      SystemVerilogUvmRegisterMapHandle map,
      const SystemVerilogUvmRegisterBusObservation &observation);

  struct MapState {
    std::optional<SystemVerilogUvmRegisterMapHandle> parent;
    std::vector<SystemVerilogUvmRegisterSubmapSnapshot> children;
    std::vector<SystemVerilogUvmRegisterMapRegisterSnapshot> registers;
    std::vector<SystemVerilogUvmRegisterMapMemorySnapshot> memories;
  };

  struct PhysicalAccessPlan {
    std::uint64_t address{};
    std::size_t byte_count{};
    std::uint32_t bus_width_bytes{};
    std::uint32_t address_unit_bytes{};
    std::size_t beat_count{};
    SystemVerilogUvmRegisterMapEndianness endianness{
        SystemVerilogUvmRegisterMapEndianness::Little};
  };

  void validate_map_layouts(SystemVerilogUvmRegisterBlockHandle top) const;
  [[nodiscard]] std::size_t
  map_depth(SystemVerilogUvmRegisterMapHandle map) const;
  [[nodiscard]] SystemVerilogUvmRegisterMapHandle
  root_map(SystemVerilogUvmRegisterMapHandle map) const;
  [[nodiscard]] std::uint32_t
  address_unit_bytes(SystemVerilogUvmRegisterMapHandle map) const;
  [[nodiscard]] std::uint64_t
  hierarchical_base(SystemVerilogUvmRegisterMapHandle map) const;
  [[nodiscard]] std::vector<std::size_t>
  unflatten_memory_index(const SystemVerilogUvmRegisterMemorySnapshot &memory,
                         std::size_t index) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterBusBeat>
  physical_accesses_impl(SystemVerilogUvmRegisterMapHandle map,
                         std::uint64_t offset, std::size_t byte_count,
                         std::size_t memory_word_offset,
                         std::size_t memory_word_bytes) const;
  [[nodiscard]] PhysicalAccessPlan
  make_physical_access_plan(SystemVerilogUvmRegisterMapHandle map,
                            std::uint64_t offset, std::size_t byte_count,
                            std::size_t memory_word_offset,
                            std::size_t memory_word_bytes) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRegisterBusBeat>
  materialize_physical_accesses(const PhysicalAccessPlan &plan) const;
  [[nodiscard]] std::optional<SystemVerilogUvmRegisterAddressLookup>
  lookup_register_address(SystemVerilogUvmRegisterMapHandle root,
                          std::uint64_t address, bool read,
                          std::size_t &work) const;
  [[nodiscard]] std::optional<SystemVerilogUvmRegisterAddressLookup>
  lookup_memory_address(SystemVerilogUvmRegisterMapHandle root,
                        std::uint64_t address, bool read,
                        std::size_t &work) const;

  struct RegisterValueState {
    PackedLogic4 desired;
    PackedLogic4 mirrored;
    std::map<std::uint64_t, bool> written_fields;

    friend bool operator==(const RegisterValueState &,
                           const RegisterValueState &) = default;
  };

  struct FieldResetState {
    std::map<std::string, PackedLogic4, std::less<>> values;
  };

  struct MemoryValueState {
    std::map<std::size_t, PackedLogic4> desired;
    std::map<std::size_t, PackedLogic4> mirrored;
    std::set<std::size_t> written;
  };

  struct MemoryWordBackup {
    std::size_t index{};
    std::optional<PackedLogic4> desired;
    std::optional<PackedLogic4> mirrored;
    bool written{};
  };

  void restore_memory_prediction(
      MemoryValueState &state, std::size_t index,
      const std::optional<PackedLogic4> &desired,
      const std::optional<PackedLogic4> &mirrored,
      std::size_t materialized_bits);

  [[nodiscard]] std::vector<MemoryWordBackup>
  backup_memory_words(const SystemVerilogUvmRegisterMemorySnapshot &memory,
                      std::size_t first_word, std::size_t word_count) const;
  void restore_memory_words(
      const SystemVerilogUvmRegisterMemorySnapshot &memory,
      std::span<const MemoryWordBackup> backup, std::uint64_t operations,
      std::uint64_t mutations, std::size_t materialized_bits);

  void require_locked(SystemVerilogUvmRegisterBlockHandle block) const;
  void record_operation(bool mutation);
  void publish_activity(SystemVerilogUvmActivityAction action,
                        std::string identity,
                        SystemVerilogUvmRootHandle root,
                        std::uint64_t value,
                        std::string detail = {}) noexcept;
  [[nodiscard]] std::size_t flatten_memory_index(
      const SystemVerilogUvmRegisterMemorySnapshot &memory,
      std::span<const std::size_t> indices) const;

  SystemVerilogUvmComponentService *components_{};
  SystemVerilogUvmSequenceService *sequences_{};
  SystemVerilogUvmTlm1Service *tlm1_{};
  SystemVerilogUvmPhaseService *phases_{};
  SystemVerilogUvmActivityService *activity_{};
  SystemVerilogUvmRegisterModelLimits limits_;
  std::shared_ptr<const void> owner_{std::make_shared<std::uint8_t>(0)};
  std::map<std::uint64_t, BlockEntry> blocks_;
  std::map<std::uint64_t, MapEntry> maps_;
  std::map<std::uint64_t, RegisterEntry> registers_;
  std::map<std::uint64_t, FieldEntry> fields_;
  std::map<std::uint64_t, MemoryEntry> memories_;
  std::map<std::uint64_t, AdapterEntry> adapters_;
  std::map<std::uint64_t, PredictorEntry> predictors_;
  std::map<std::uint64_t, FrontdoorEntry> frontdoors_;
  std::map<std::uint64_t, UserFrontdoorEntry> user_frontdoors_;
  std::map<std::uint64_t, HdlPathEntry> hdl_paths_;
  std::map<std::uint64_t, RegisterCallbackEntry> register_callbacks_;
  std::map<std::uint64_t, RegisterCoverageEntry> coverage_models_;
  std::vector<SystemVerilogUvmRegisterStandardSequenceSnapshot>
      standard_sequences_;
  std::map<std::uint64_t, std::set<std::string, std::less<>>> child_names_;
  std::map<SystemVerilogUvmRootHandle,
           std::set<std::string, std::less<>>>
      top_names_;
  std::map<std::uint64_t, std::vector<SystemVerilogUvmRegisterBlockHandle>>
      child_blocks_;
  std::map<std::uint64_t, std::vector<SystemVerilogUvmRegisterFieldHandle>>
      register_fields_;
  std::map<std::uint64_t, std::set<std::string, std::less<>>> field_names_;
  std::map<std::uint64_t, RegisterValueState> register_values_;
  std::map<std::uint64_t, FieldResetState> field_resets_;
  std::map<std::uint64_t, MemoryValueState> memory_values_;
  std::map<std::uint64_t, MapState> map_states_;
  std::vector<SystemVerilogUvmRegisterDeclarationSnapshot> declarations_;
  std::uint64_t next_identity_{1};
  std::uint64_t next_order_{1};
  std::uint64_t mutations_{};
  std::uint64_t operations_{};
  std::uint64_t next_mapping_order_{1};
  std::uint64_t next_adapter_slot_{1};
  std::uint64_t next_predictor_slot_{1};
  std::uint64_t next_frontdoor_slot_{1};
  std::uint64_t next_user_frontdoor_slot_{1};
  std::uint64_t next_hdl_path_slot_{1};
  std::uint64_t next_frontdoor_order_{1};
  std::uint64_t next_standard_sequence_order_{1};
  std::uint64_t next_register_callback_slot_{1};
  std::uint64_t next_register_coverage_slot_{1};
  std::uint64_t next_register_callback_order_{1};
  SystemVerilogUvmRegisterBackdoorTransport backdoor_transport_;
  std::size_t map_registrations_{};
  std::size_t pending_frontdoors_{};
  std::size_t frontdoor_bus_items_{};
  std::size_t frontdoor_byte_enables_{};
  std::size_t hdl_slices_{};
  std::size_t hdl_path_bytes_{};
  std::size_t backdoor_operations_{};
  std::size_t standard_sequence_operations_{};
  std::size_t standard_sequence_failures_{};
  std::size_t standard_sequence_failure_bytes_{};
  std::size_t register_callback_invocations_{};
  std::size_t register_callback_failures_{};
  std::size_t register_coverage_samples_{};
  std::size_t register_coverage_bins_{};
  std::size_t register_coverage_bin_bytes_{};
  std::size_t register_value_bits_{};
  std::size_t reset_value_bits_{};
  std::size_t materialized_memory_bits_{};
};

} // namespace fsim::runtime
