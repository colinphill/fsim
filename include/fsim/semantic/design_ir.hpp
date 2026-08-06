// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsim::semantic::design {

struct SpecializationTag;
struct InstanceOccurrenceTag;
struct ObjectTag;
struct ProcessOccurrenceTag;
struct SensitivityTag;
struct TransactionTag;
struct ConversionTag;
struct BoundaryTag;

using SpecializationId = semantic::Id<SpecializationTag>;
using InstanceOccurrenceId = semantic::Id<InstanceOccurrenceTag>;
using ObjectId = semantic::Id<ObjectTag>;
using ProcessOccurrenceId = semantic::Id<ProcessOccurrenceTag>;
using SensitivityId = semantic::Id<SensitivityTag>;
using TransactionId = semantic::Id<TransactionTag>;
using ConversionId = semantic::Id<ConversionTag>;
using BoundaryId = semantic::Id<BoundaryTag>;

enum class ObjectKind : std::uint8_t {
    signal,
    string,
    container,
    protected_object,
    protected_member,
    systemc_module,
    systemc_port,
    systemc_event,
    systemc_channel,
    systemc_signal,
    systemc_export,
};

enum class Direction : std::uint8_t {
    unknown,
    input,
    output,
    inout,
    ref,
    buffer,
};

enum class EdgeKind : std::uint8_t { any, positive, negative, transaction };

enum class TransactionKind : std::uint8_t {
    procedural,
    continuous,
    boundary_adapter,
    systemc_update,
};

enum class ConversionKind : std::uint8_t {
    ordinal_alias,
    width_adapter,
    signedness_adapter,
    width_signedness_adapter,
    boolean_adapter,
    integer_adapter,
    state_domain_alias,
};

enum class BoundaryKind : std::uint8_t {
    language_conversion,
    systemc_instance,
    systemc_port,
    systemc_event,
    systemc_channel,
    systemc_signal,
    systemc_export,
    systemc_process,
};

struct ParameterValue {
    std::string name;
    std::string value;
    std::string identity;
    std::optional<DeclarationId> declaration;
};

struct InstanceOccurrence {
    InstanceOccurrenceId id;
    std::optional<InstanceOccurrenceId> parent;
    std::optional<InstanceId> source_instance;
    SpecializationId specialization;
    std::string name;
    std::string path;
    std::string target;
    std::optional<SourceSpanId> source;
    std::optional<OriginId> origin;
};

struct Specialization {
    SpecializationId id;
    UnitId unit;
    ScopeId scope;
    InstanceOccurrenceId instance;
    Language language{Language::system_verilog};
    std::string library;
    std::string name;
    std::optional<SourceSpanId> source;
    std::vector<std::string> source_dependencies;
    std::vector<ParameterValue> parameters;
    std::vector<DeclarationId> callables;
    std::vector<ProcessOccurrenceId> processes;
    std::vector<ObjectId> objects;
};

struct Object {
    ObjectId id;
    SpecializationId specialization;
    ObjectKind kind{ObjectKind::signal};
    std::string name;
    std::string path;
    std::optional<ValueId> declaration_value;
    TypeReference type;
    std::optional<SourceSpanId> source;
    std::optional<ObjectId> parent_object;
    std::uint64_t runtime_index{};
    std::uint64_t native_handle{};
    std::uint64_t width{};
    bool signed_value{};
    std::string external_type;
};

struct Port {
    PortId id;
    InstanceOccurrenceId instance;
    ObjectId object;
    std::optional<DeclarationId> declaration;
    Direction direction{Direction::unknown};
    bool export_object{};
    bool writable{};
    std::optional<SourceSpanId> source;
};

struct Sensitivity {
    SensitivityId id;
    ProcessOccurrenceId process;
    ObjectId object;
    EdgeKind edge{EdgeKind::any};
};

struct Transaction {
    TransactionId id;
    ProcessOccurrenceId process;
    DriverId driver;
    ObjectId object;
    TransactionKind kind{TransactionKind::procedural};
    std::uint32_t offset{};
    std::uint32_t width{};
    bool whole{};
};

struct Driver {
    DriverId id;
    ProcessOccurrenceId process;
    ObjectId object;
    std::uint32_t offset{};
    std::uint32_t width{};
    bool whole{};
    std::vector<TransactionId> transactions;
};

struct ProcessOccurrence {
    ProcessOccurrenceId id;
    SpecializationId specialization;
    std::optional<ProcessId> source_process;
    std::string name;
    std::optional<SourceSpanId> source;
    std::uint32_t runtime_index{};
    bool initialize{};
    bool reactive{};
    bool final{};
    std::vector<SensitivityId> sensitivities;
    std::vector<DriverId> drivers;
    std::vector<TransactionId> transactions;
};

struct Conversion {
    ConversionId id;
    ConversionKind kind{ConversionKind::ordinal_alias};
    std::string path;
    ObjectId formal;
    ObjectId actual;
    std::optional<ProcessOccurrenceId> process;
    Direction direction{Direction::unknown};
    std::uint64_t formal_width{};
    std::uint64_t actual_width{};
    bool formal_signed{};
    bool actual_signed{};
    bool state_domain_changed{};
    std::optional<SourceSpanId> source;
};

struct Boundary {
    BoundaryId id;
    BoundaryKind kind{BoundaryKind::language_conversion};
    std::string name;
    std::string path;
    std::optional<InstanceOccurrenceId> instance;
    std::optional<ObjectId> object;
    std::optional<PortId> port;
    std::optional<ProcessOccurrenceId> process;
    std::optional<ConversionId> conversion;
    std::uint64_t native_handle{};
    std::optional<SourceSpanId> source;
};

/// Parser-independent elaborated representation. Cross-record relationships
/// use stable semantic or DesignIR IDs; legacy runtime indices are retained
/// only as explicit adapter locators for the Task 8 consumer migration.
class DesignIr final {
public:
    [[nodiscard]] const std::string& top() const noexcept;
    [[nodiscard]] const std::vector<std::string>& roots() const noexcept;
    [[nodiscard]] const std::vector<Specialization>&
    specializations() const noexcept;
    [[nodiscard]] const std::vector<InstanceOccurrence>&
    instances() const noexcept;
    [[nodiscard]] const std::vector<Object>& objects() const noexcept;
    [[nodiscard]] const std::vector<Port>& ports() const noexcept;
    [[nodiscard]] const std::vector<ProcessOccurrence>&
    processes() const noexcept;
    [[nodiscard]] const std::vector<Sensitivity>&
    sensitivities() const noexcept;
    [[nodiscard]] const std::vector<Driver>& drivers() const noexcept;
    [[nodiscard]] const std::vector<Transaction>&
    transactions() const noexcept;
    [[nodiscard]] const std::vector<Conversion>&
    conversions() const noexcept;
    [[nodiscard]] const std::vector<Boundary>& boundaries() const noexcept;

    std::string& mutable_top() noexcept;
    std::vector<std::string>& mutable_roots() noexcept;
    std::vector<Specialization>& mutable_specializations() noexcept;
    std::vector<InstanceOccurrence>& mutable_instances() noexcept;
    std::vector<Object>& mutable_objects() noexcept;
    std::vector<Port>& mutable_ports() noexcept;
    std::vector<ProcessOccurrence>& mutable_processes() noexcept;
    std::vector<Sensitivity>& mutable_sensitivities() noexcept;
    std::vector<Driver>& mutable_drivers() noexcept;
    std::vector<Transaction>& mutable_transactions() noexcept;
    std::vector<Conversion>& mutable_conversions() noexcept;
    std::vector<Boundary>& mutable_boundaries() noexcept;

    /// Validate dense IDs and every internal DesignIR relationship.
    [[nodiscard]] bool valid() const noexcept;
    /// Also validate every relationship into the owning semantic model.
    [[nodiscard]] bool valid(const semantic::Model& model) const noexcept;

private:
    std::string top_;
    std::vector<std::string> roots_;
    std::vector<Specialization> specializations_;
    std::vector<InstanceOccurrence> instances_;
    std::vector<Object> objects_;
    std::vector<Port> ports_;
    std::vector<ProcessOccurrence> processes_;
    std::vector<Sensitivity> sensitivities_;
    std::vector<Driver> drivers_;
    std::vector<Transaction> transactions_;
    std::vector<Conversion> conversions_;
    std::vector<Boundary> boundaries_;
};

} // namespace fsim::semantic::design
