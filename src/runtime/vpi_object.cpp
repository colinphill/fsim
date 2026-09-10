// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_object.hpp"

#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {

namespace {

    constexpr std::uint64_t slot_mask = (1ULL << 24U) - 1U;
    constexpr std::uint64_t epoch_mask = (1ULL << 16U) - 1U;
    constexpr std::uint64_t iterator_bit = 1ULL << 63U;
    constexpr std::uint64_t coverage_bit = 1ULL << 62U;
    // Bit 62 is reserved for v3 coverage objects. Ordinary VPI object handles
    // therefore use only bits 40..61 for their registry identity.
    constexpr std::uint64_t registry_mask = (1ULL << 22U) - 1U;
    constexpr std::uint32_t maximum_registry_identity = (1U << 22U) - 1U;
    constexpr std::size_t maximum_name_size = 4096;
    constexpr std::size_t maximum_source_size = 1U << 20U;
    constexpr std::size_t maximum_iterator_objects = 1U << 20U;
    std::atomic<std::uint32_t> next_registry_identity { 1 };

    SystemVerilogVpiTypeInfo default_type(
        const SystemVerilogVpiObjectKind kind)
    {
        SystemVerilogVpiTypeInfo type;
        switch (kind) {
        case SystemVerilogVpiObjectKind::Port:
            type.category = SystemVerilogVpiValueCategory::Logic4;
            type.direction = SystemVerilogVpiDirection::Inout;
            type.width = 1;
            break;
        case SystemVerilogVpiObjectKind::Net:
            type.category = SystemVerilogVpiValueCategory::Logic4;
            type.net_kind = SystemVerilogVpiNetKind::Wire;
            type.width = 1;
            break;
        case SystemVerilogVpiObjectKind::Variable:
        case SystemVerilogVpiObjectKind::Memory:
        case SystemVerilogVpiObjectKind::Array:
        case SystemVerilogVpiObjectKind::ClassProperty:
            type.category = SystemVerilogVpiValueCategory::Logic4;
            type.width = 1;
            break;
        case SystemVerilogVpiObjectKind::Parameter:
        case SystemVerilogVpiObjectKind::Constant:
            type.category = SystemVerilogVpiValueCategory::Integer4;
            type.width = 32;
            type.is_signed = true;
            type.is_constant = true;
            break;
        case SystemVerilogVpiObjectKind::Concatenation:
        case SystemVerilogVpiObjectKind::Operation:
        case SystemVerilogVpiObjectKind::MinTypMax:
            type.category = SystemVerilogVpiValueCategory::Logic4;
            type.width = 1;
            break;
        case SystemVerilogVpiObjectKind::NamedEvent:
            type.category = SystemVerilogVpiValueCategory::Event;
            break;
        default:
            break;
        }
        return type;
    }
    constexpr bool structural_object(
        const SystemVerilogVpiObjectKind kind) noexcept
    {
        return kind >= SystemVerilogVpiObjectKind::ModuleArray;
    }
    constexpr bool scope_object(
        const SystemVerilogVpiObjectKind kind) noexcept
    {
        switch (kind) {
        case SystemVerilogVpiObjectKind::Root:
        case SystemVerilogVpiObjectKind::Module:
        case SystemVerilogVpiObjectKind::Interface:
        case SystemVerilogVpiObjectKind::Program:
        case SystemVerilogVpiObjectKind::Package:
        case SystemVerilogVpiObjectKind::GenerateScope:
        case SystemVerilogVpiObjectKind::Class:
        case SystemVerilogVpiObjectKind::Process:
        case SystemVerilogVpiObjectKind::Assertion:
        case SystemVerilogVpiObjectKind::ModuleArray:
        case SystemVerilogVpiObjectKind::InterfaceArray:
        case SystemVerilogVpiObjectKind::ProgramArray:
        case SystemVerilogVpiObjectKind::GenerateScopeArray:
        case SystemVerilogVpiObjectKind::Modport:
        case SystemVerilogVpiObjectKind::ClockingBlock:
        case SystemVerilogVpiObjectKind::Task:
        case SystemVerilogVpiObjectKind::Function:
        case SystemVerilogVpiObjectKind::Method:
        case SystemVerilogVpiObjectKind::Covergroup:
            return true;
        default:
            return false;
        }
    }
    constexpr bool relationship_matches(
        const SystemVerilogVpiRelationshipKind relationship,
        const SystemVerilogVpiObjectKind kind) noexcept
    {
        using Kind = SystemVerilogVpiObjectKind;
        switch (relationship) {
        case SystemVerilogVpiRelationshipKind::Children:
            return true;
        case SystemVerilogVpiRelationshipKind::Parent:
            return false;
        case SystemVerilogVpiRelationshipKind::InternalScopes:
            return scope_object(kind);
        case SystemVerilogVpiRelationshipKind::Declarations:
            return !scope_object(kind);
        case SystemVerilogVpiRelationshipKind::Ports:
            return kind == Kind::Port || kind == Kind::PortBit
                || kind == Kind::ModportPort || kind == Kind::ClockingIo;
        case SystemVerilogVpiRelationshipKind::Nets:
            return kind == Kind::Net || kind == Kind::NetBit;
        case SystemVerilogVpiRelationshipKind::Variables:
            return kind == Kind::Variable || kind == Kind::VariableBit
                || kind == Kind::Memory || kind == Kind::MemoryWord
                || kind == Kind::Array || kind == Kind::ArrayWord
                || kind == Kind::ClassProperty || kind == Kind::NamedEvent;
        case SystemVerilogVpiRelationshipKind::Parameters:
            return kind == Kind::Parameter || kind == Kind::ParameterAssignment
                || kind == Kind::GenVar;
        case SystemVerilogVpiRelationshipKind::Processes:
            return kind == Kind::Process || kind == Kind::Initial
                || kind == Kind::Always;
        case SystemVerilogVpiRelationshipKind::Assertions:
            return kind == Kind::Assertion || kind == Kind::PropertyDeclaration
                || kind == Kind::SequenceDeclaration;
        case SystemVerilogVpiRelationshipKind::Drivers:
            return kind == Kind::Driver || kind == Kind::ContinuousAssignment
                || kind == Kind::Assignment || kind == Kind::Force
                || kind == Kind::Release;
        case SystemVerilogVpiRelationshipKind::Expressions:
            return kind == Kind::Constant || kind == Kind::Concatenation
                || kind == Kind::Operation || kind == Kind::MinTypMax
                || kind == Kind::Expression || kind == Kind::Range
                || kind == Kind::PartSelect || kind == Kind::IndexedPartSelect
                || kind == Kind::BitSelect || kind == Kind::Call;
        case SystemVerilogVpiRelationshipKind::Arguments:
            return kind == Kind::Argument;
        case SystemVerilogVpiRelationshipKind::Types:
            return kind == Kind::TypeSpecification
                || kind == Kind::EnumerationConstant
                || kind == Kind::StructureMember || kind == Kind::PackedArrayType
                || kind == Kind::UnpackedArrayType || kind == Kind::QueueType
                || kind == Kind::AssociativeArrayType
                || kind == Kind::DynamicArrayType || kind == Kind::StringType
                || kind == Kind::ClassType || kind == Kind::InterfaceType;
        case SystemVerilogVpiRelationshipKind::Coverage:
            return kind == Kind::Covergroup || kind == Kind::CoverPoint
                || kind == Kind::CoverageCross || kind == Kind::CoverageBin;
        }
        return false;
    }
    SystemVerilogVpiValueError value_error(
        const SystemVerilogVpiObjectError error)
    {
        switch (error) {
        case SystemVerilogVpiObjectError::InvalidSimulation:
            return SystemVerilogVpiValueError::InvalidSimulation;
        case SystemVerilogVpiObjectError::InvalidHandle:
            return SystemVerilogVpiValueError::InvalidHandle;
        case SystemVerilogVpiObjectError::CrossSimulation:
            return SystemVerilogVpiValueError::CrossSimulation;
        case SystemVerilogVpiObjectError::StaleHandle:
            return SystemVerilogVpiValueError::StaleHandle;
        case SystemVerilogVpiObjectError::ReleasedHandle:
            return SystemVerilogVpiValueError::ReleasedHandle;
        default:
            return SystemVerilogVpiValueError::InvalidHandle;
        }
    }

    SystemVerilogVpiValueReadResult value_failure(
        const SystemVerilogVpiValueError error)
    {
        SystemVerilogVpiValueReadResult result;
        result.error = error;
        return result;
    }

    bool writable_value(
        const SystemVerilogVpiObjectKind kind,
        const SystemVerilogVpiTypeInfo& type)
    {
        if (type.is_constant) {
            return false;
        }
        switch (kind) {
        case SystemVerilogVpiObjectKind::Port:
            return type.direction == SystemVerilogVpiDirection::Output
                || type.direction == SystemVerilogVpiDirection::Inout;
        case SystemVerilogVpiObjectKind::Net:
        case SystemVerilogVpiObjectKind::Variable:
        case SystemVerilogVpiObjectKind::Memory:
        case SystemVerilogVpiObjectKind::Array:
        case SystemVerilogVpiObjectKind::ClassProperty:
            return true;
        default:
            return false;
        }
    }

    SystemVerilogVpiValueError write_access_error(
        const SystemVerilogVpiObjectKind kind,
        const SystemVerilogVpiTypeInfo& type)
    {
        if (kind == SystemVerilogVpiObjectKind::Port
            && type.direction == SystemVerilogVpiDirection::Input) {
            return SystemVerilogVpiValueError::InputOnly;
        }
        return SystemVerilogVpiValueError::ReadOnly;
    }

    void notify_value_observers(
        const std::vector<SystemVerilogVpiValueObserver>& observers,
        const fsim_vpi_handle_v1 handle,
        const SystemVerilogVpiStoredValue& value) noexcept
    {
        for (const auto& observer : observers) {
            try {
                observer(handle, value);
            } catch (...) {
            }
        }
    }

    SystemVerilogVpiValueError notify_value_state_observers(
        const std::vector<SystemVerilogVpiValueStateObserver>& observers,
        const SystemVerilogVpiValueStateUpdate& update) noexcept
    {
        for (const auto& observer : observers) {
            try {
                const auto error = observer(update);
                if (error != SystemVerilogVpiValueError::None) {
                    return error;
                }
            } catch (...) {
                return SystemVerilogVpiValueError::ResourceLimit;
            }
        }
        return SystemVerilogVpiValueError::None;
    }

    bool equivalent_type(
        const SystemVerilogVpiTypeInfo& lhs,
        const SystemVerilogVpiTypeInfo& rhs)
    {
        const bool descriptors_equal = (!lhs.descriptor && !rhs.descriptor)
            || (lhs.descriptor && rhs.descriptor
                && *lhs.descriptor == *rhs.descriptor);
        return lhs.language == rhs.language
            && lhs.category == rhs.category
            && lhs.net_kind == rhs.net_kind
            && lhs.direction == rhs.direction
            && lhs.lifetime == rhs.lifetime
            && lhs.width == rhs.width
            && lhs.is_signed == rhs.is_signed
            && lhs.is_constant == rhs.is_constant
            && lhs.driver_range == rhs.driver_range
            && descriptors_equal;
    }

    constexpr bool valid_language(
        const SystemVerilogVpiLanguage language) noexcept
    {
        switch (language) {
        case SystemVerilogVpiLanguage::Verilog1995:
        case SystemVerilogVpiLanguage::Verilog2001:
        case SystemVerilogVpiLanguage::Verilog2001NoConfig:
        case SystemVerilogVpiLanguage::Verilog2005:
        case SystemVerilogVpiLanguage::SystemVerilog2005:
        case SystemVerilogVpiLanguage::SystemVerilog2009:
        case SystemVerilogVpiLanguage::SystemVerilog2012:
        case SystemVerilogVpiLanguage::SystemVerilog2017:
            return true;
        }
        return false;
    }

    constexpr bool systemverilog_language(
        const SystemVerilogVpiLanguage language) noexcept
    {
        switch (language) {
        case SystemVerilogVpiLanguage::SystemVerilog2005:
        case SystemVerilogVpiLanguage::SystemVerilog2009:
        case SystemVerilogVpiLanguage::SystemVerilog2012:
        case SystemVerilogVpiLanguage::SystemVerilog2017:
            return true;
        default:
            return false;
        }
    }

    bool valid_type(
        const SystemVerilogVpiObjectKind kind,
        const std::optional<SystemVerilogVpiTypeInfo>& candidate)
    {
        if (!candidate) {
            return false;
        }
        const auto& type = *candidate;
        if (!valid_language(type.language)
            || static_cast<unsigned>(type.category)
                > static_cast<unsigned>(SystemVerilogVpiValueCategory::Event)
            || static_cast<unsigned>(type.net_kind)
                > static_cast<unsigned>(SystemVerilogVpiNetKind::TriOr)
            || static_cast<unsigned>(type.direction)
                > static_cast<unsigned>(SystemVerilogVpiDirection::Inout)
            || static_cast<unsigned>(type.lifetime)
                > static_cast<unsigned>(SystemVerilogVpiLifetime::Automatic)) {
            return false;
        }
        const bool has_provenance = type.semantic_unit_id.has_value()
            || type.source_id.has_value() || !type.semantic_unit.empty()
            || !type.source_path.empty() || !type.standard.empty()
            || !type.compatibility_profile.empty();
        if (has_provenance
            && (!type.semantic_unit_id || !type.source_id
                || type.semantic_unit.empty() || type.source_path.empty()
                || type.standard.empty()
                || type.compatibility_profile.empty()
                || type.semantic_unit.size() > maximum_name_size
                || type.source_path.size() > maximum_source_size
                || type.standard.size() > maximum_name_size
                || type.compatibility_profile.size() > maximum_name_size)) {
            return false;
        }
        const bool systemverilog_only = kind == SystemVerilogVpiObjectKind::Interface
            || kind == SystemVerilogVpiObjectKind::Program
            || kind == SystemVerilogVpiObjectKind::Package
            || kind == SystemVerilogVpiObjectKind::Class
            || kind == SystemVerilogVpiObjectKind::ClassProperty
            || kind == SystemVerilogVpiObjectKind::Assertion;
        if (systemverilog_only && !systemverilog_language(type.language)) {
            return false;
        }

        switch (type.category) {
        case SystemVerilogVpiValueCategory::None:
        case SystemVerilogVpiValueCategory::String:
        case SystemVerilogVpiValueCategory::Event:
            if (type.width != 0U) {
                return false;
            }
            break;
        case SystemVerilogVpiValueCategory::Real:
            if (type.width != 64U) {
                return false;
            }
            break;
        case SystemVerilogVpiValueCategory::ShortReal:
            if (type.width != 32U) {
                return false;
            }
            break;
        case SystemVerilogVpiValueCategory::Time:
            if (type.width != 64U) {
                return false;
            }
            break;
        default:
            if (type.width == 0U) {
                return false;
            }
            break;
        }
        if ((type.category == SystemVerilogVpiValueCategory::None
                || type.category == SystemVerilogVpiValueCategory::String
                || type.category == SystemVerilogVpiValueCategory::Event
                || type.category == SystemVerilogVpiValueCategory::Real
                || type.category == SystemVerilogVpiValueCategory::ShortReal)
            && type.is_signed) {
            return false;
        }

        const bool scope = scope_object(kind);
        if (scope && type.category != SystemVerilogVpiValueCategory::None) {
            return false;
        }
        if (kind == SystemVerilogVpiObjectKind::NamedEvent
            && (type.category != SystemVerilogVpiValueCategory::Event
                || type.descriptor)) {
            return false;
        }
        if (!scope && !structural_object(kind)
            && kind != SystemVerilogVpiObjectKind::NamedEvent
            && type.category == SystemVerilogVpiValueCategory::None
            && !type.descriptor) {
            return false;
        }

        if (kind == SystemVerilogVpiObjectKind::Net) {
            if (type.net_kind == SystemVerilogVpiNetKind::None
                || type.lifetime != SystemVerilogVpiLifetime::Static) {
                return false;
            }
        } else if (kind != SystemVerilogVpiObjectKind::Port
            && type.net_kind != SystemVerilogVpiNetKind::None) {
            return false;
        }
        if (kind == SystemVerilogVpiObjectKind::Port) {
            if (type.direction == SystemVerilogVpiDirection::None
                || type.lifetime != SystemVerilogVpiLifetime::Static) {
                return false;
            }
        } else if (type.direction != SystemVerilogVpiDirection::None) {
            return false;
        }
        if (type.lifetime == SystemVerilogVpiLifetime::Automatic
            && kind != SystemVerilogVpiObjectKind::Variable
            && kind != SystemVerilogVpiObjectKind::ClassProperty) {
            return false;
        }
        if (type.is_constant
            != (kind == SystemVerilogVpiObjectKind::Parameter
                || kind == SystemVerilogVpiObjectKind::Driver
                || kind == SystemVerilogVpiObjectKind::Constant)) {
            return false;
        }
        if (type.driver_range) {
            const auto range_end
                = static_cast<std::uint64_t>(type.driver_range->offset)
                + type.driver_range->width;
            if (kind != SystemVerilogVpiObjectKind::Driver
                || type.driver_range->width == 0U
                || range_end > type.width
                || (type.driver_range->whole
                    && (type.driver_range->offset != 0U
                        || type.driver_range->width != type.width))) {
                return false;
            }
        }
        if (!type.descriptor) {
            return true;
        }

        const auto descriptor_result = validate_systemverilog_vpi_descriptor(*type.descriptor);
        if (!descriptor_result) {
            return false;
        }
        const auto descriptor_kind = type.descriptor->kind;
        const bool descriptor_requires_systemverilog = descriptor_kind == SystemVerilogVpiDescriptorKind::DynamicArray
            || descriptor_kind == SystemVerilogVpiDescriptorKind::Queue
            || descriptor_kind == SystemVerilogVpiDescriptorKind::AssociativeArray
            || descriptor_kind == SystemVerilogVpiDescriptorKind::Struct
            || descriptor_kind == SystemVerilogVpiDescriptorKind::Union
            || descriptor_kind == SystemVerilogVpiDescriptorKind::Enum
            || descriptor_kind == SystemVerilogVpiDescriptorKind::String
            || descriptor_kind == SystemVerilogVpiDescriptorKind::Class
            || descriptor_kind == SystemVerilogVpiDescriptorKind::ClassHandle;
        if (descriptor_requires_systemverilog
            && !systemverilog_language(type.language)) {
            return false;
        }

        if (descriptor_kind == SystemVerilogVpiDescriptorKind::String) {
            if (type.category != SystemVerilogVpiValueCategory::String
                || type.width != 0U || type.is_signed) {
                return false;
            }
        } else if (descriptor_kind
            == SystemVerilogVpiDescriptorKind::PackedArray) {
            const auto* element = &type.descriptor->children.front();
            while (element->kind
                == SystemVerilogVpiDescriptorKind::PackedArray) {
                element = &element->children.front();
            }
            const auto element_category = element->category;
            const bool category_matches
                = type.category == element_category
                || (type.category
                        == SystemVerilogVpiValueCategory::Integer2
                    && element_category
                        == SystemVerilogVpiValueCategory::Bit2)
                || (type.category
                        == SystemVerilogVpiValueCategory::Integer4
                    && element_category
                        == SystemVerilogVpiValueCategory::Logic4);
            if (descriptor_result.value.dynamic
                || descriptor_result.value.fixed_bits != type.width
                || !category_matches) {
                return false;
            }
        } else if (descriptor_kind == SystemVerilogVpiDescriptorKind::Enum
            || descriptor_kind == SystemVerilogVpiDescriptorKind::Scalar) {
            if (type.category != type.descriptor->category
                || type.width != type.descriptor->width
                || type.is_signed != type.descriptor->is_signed) {
                return false;
            }
        } else if (type.category != SystemVerilogVpiValueCategory::None
            || type.width != 0U || type.is_signed) {
            return false;
        }

        if (scope) {
            return kind == SystemVerilogVpiObjectKind::Class
                && descriptor_kind == SystemVerilogVpiDescriptorKind::Class;
        }
        if (kind == SystemVerilogVpiObjectKind::Memory) {
            return descriptor_kind
                == SystemVerilogVpiDescriptorKind::UnpackedArray;
        }
        if (kind == SystemVerilogVpiObjectKind::Array) {
            return descriptor_kind == SystemVerilogVpiDescriptorKind::PackedArray
                || descriptor_kind
                == SystemVerilogVpiDescriptorKind::UnpackedArray
                || descriptor_kind
                == SystemVerilogVpiDescriptorKind::DynamicArray
                || descriptor_kind == SystemVerilogVpiDescriptorKind::Queue
                || descriptor_kind
                == SystemVerilogVpiDescriptorKind::AssociativeArray;
        }
        if (kind == SystemVerilogVpiObjectKind::Net
            || kind == SystemVerilogVpiObjectKind::Port) {
            return descriptor_kind == SystemVerilogVpiDescriptorKind::Scalar
                || descriptor_kind == SystemVerilogVpiDescriptorKind::PackedArray
                || descriptor_kind == SystemVerilogVpiDescriptorKind::Enum;
        }
        return descriptor_kind != SystemVerilogVpiDescriptorKind::Class;
    }

} // namespace

SystemVerilogVpiObjectRegistry::SystemVerilogVpiObjectRegistry(
    const std::uint64_t simulation_identity) noexcept
    : simulation_identity_(simulation_identity)
{
    const auto identity = next_registry_identity.fetch_add(1, std::memory_order_relaxed);
    if (simulation_identity != 0U && identity <= maximum_registry_identity) {
        registry_identity_ = identity;
    }
}

std::uint64_t SystemVerilogVpiObjectRegistry::simulation_identity()
    const noexcept
{
    return simulation_identity_;
}

bool SystemVerilogVpiObjectRegistry::valid() const noexcept
{
    return simulation_identity_ != 0U && registry_identity_ != 0U;
}
SystemVerilogVpiObjectCapabilities
SystemVerilogVpiObjectRegistry::capabilities() noexcept
{
    return {
        2023U,
        static_cast<std::uint32_t>(
            SystemVerilogVpiObjectKind::AttributeSpecification) + 1U,
        static_cast<std::uint32_t>(
            SystemVerilogVpiRelationshipKind::Coverage) + 1U,
        static_cast<std::uint32_t>(
            SystemVerilogVpiPropertyKind::HasTypeDescriptor) + 1U,
        static_cast<std::uint32_t>(maximum_iterator_objects),
        true,
        true,
        true,
        true,
    };
}
bool SystemVerilogVpiObjectRegistry::supports(
    const SystemVerilogVpiObjectKind kind) noexcept
{
    return static_cast<std::uint32_t>(kind)
        <= static_cast<std::uint32_t>(
            SystemVerilogVpiObjectKind::AttributeSpecification);
}
bool SystemVerilogVpiObjectRegistry::supports(
    const SystemVerilogVpiRelationshipKind relationship) noexcept
{
    return static_cast<std::uint32_t>(relationship)
        <= static_cast<std::uint32_t>(
            SystemVerilogVpiRelationshipKind::Coverage);
}
bool SystemVerilogVpiObjectRegistry::supports(
    const SystemVerilogVpiPropertyKind property) noexcept
{
    return static_cast<std::uint32_t>(property)
        <= static_cast<std::uint32_t>(
            SystemVerilogVpiPropertyKind::HasTypeDescriptor);
}
fsim_vpi_handle_v1 SystemVerilogVpiObjectRegistry::encode_object(
    const std::uint32_t slot, const std::uint16_t epoch) const noexcept
{
    return (static_cast<std::uint64_t>(registry_identity_) << 40U)
        | (static_cast<std::uint64_t>(epoch) << 24U)
        | (static_cast<std::uint64_t>(slot) + 1U);
}

fsim_vpi_handle_v1 SystemVerilogVpiObjectRegistry::encode_iterator(
    const std::uint32_t slot, const std::uint16_t epoch) const noexcept
{
    return iterator_bit | encode_object(slot, epoch);
}

SystemVerilogVpiObjectError SystemVerilogVpiObjectRegistry::resolve_object(
    const fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept
{
    if (handle == 0U || (handle & iterator_bit) != 0U
        || (handle & coverage_bit) != 0U
        || (handle & slot_mask) == 0U) {
        return SystemVerilogVpiObjectError::InvalidHandle;
    }
    if (static_cast<std::uint32_t>((handle >> 40U) & registry_mask)
        != registry_identity_) {
        return SystemVerilogVpiObjectError::CrossSimulation;
    }
    const auto decoded_slot = static_cast<std::uint32_t>((handle & slot_mask) - 1U);
    if (decoded_slot >= records_.size()) {
        return SystemVerilogVpiObjectError::InvalidHandle;
    }
    const auto& record = records_[decoded_slot];
    const auto epoch = static_cast<std::uint16_t>((handle >> 24U) & epoch_mask);
    if (epoch != record.epoch) {
        return SystemVerilogVpiObjectError::StaleHandle;
    }
    if (!record.live) {
        return SystemVerilogVpiObjectError::ReleasedHandle;
    }
    slot = decoded_slot;
    return SystemVerilogVpiObjectError::None;
}

SystemVerilogVpiIteratorError SystemVerilogVpiObjectRegistry::resolve_iterator(
    const fsim_vpi_handle_v1 handle, std::uint32_t& slot) const noexcept
{
    if (handle == 0U || (handle & iterator_bit) == 0U
        || (handle & coverage_bit) != 0U
        || (handle & slot_mask) == 0U) {
        return SystemVerilogVpiIteratorError::InvalidHandle;
    }
    if (static_cast<std::uint32_t>((handle >> 40U) & registry_mask)
        != registry_identity_) {
        return SystemVerilogVpiIteratorError::CrossSimulation;
    }
    const auto decoded_slot = static_cast<std::uint32_t>((handle & slot_mask) - 1U);
    if (decoded_slot >= iterators_.size()) {
        return SystemVerilogVpiIteratorError::InvalidHandle;
    }
    const auto& record = iterators_[decoded_slot];
    const auto epoch = static_cast<std::uint16_t>((handle >> 24U) & epoch_mask);
    if (epoch != record.epoch) {
        return SystemVerilogVpiIteratorError::StaleHandle;
    }
    if (!record.live) {
        return SystemVerilogVpiIteratorError::ReleasedHandle;
    }
    slot = decoded_slot;
    return SystemVerilogVpiIteratorError::None;
}

bool SystemVerilogVpiObjectRegistry::normalize_name(
    const std::string_view input,
    std::string& normalized,
    std::string& hierarchy_segment)
{
    if (input.empty() || input.size() > maximum_name_size
        || input.find('\0') != std::string_view::npos) {
        return false;
    }
    if (input.front() != '\\') {
        if (input.find('.') != std::string_view::npos
            || std::ranges::any_of(input, [](const char character) {
                   return std::isspace(
                              static_cast<unsigned char>(character))
                       != 0;
               })) {
            return false;
        }
        normalized.assign(input);
        hierarchy_segment = normalized;
        return true;
    }

    auto end = input.size();
    while (end > 1U
        && std::isspace(static_cast<unsigned char>(input[end - 1U])) != 0) {
        --end;
    }
    if (end == 1U
        || std::ranges::any_of(input.substr(1U, end - 1U),
            [](const char character) {
                return std::isspace(
                           static_cast<unsigned char>(character))
                    != 0;
            })) {
        return false;
    }
    normalized.assign(input.substr(0U, end));
    hierarchy_segment = normalized;
    hierarchy_segment.push_back(' ');
    return true;
}

std::string SystemVerilogVpiObjectRegistry::sibling_key(
    const fsim_vpi_handle_v1 parent, const std::string_view name)
{
    auto key = std::to_string(parent);
    key.push_back('\0');
    key.append(name);
    return key;
}

SystemVerilogVpiObjectResult SystemVerilogVpiObjectRegistry::create(
    const SystemVerilogVpiObjectKind kind,
    const fsim_vpi_handle_v1 parent,
    const std::string_view name)
{
    return create(SystemVerilogVpiObjectDescriptor {
        kind,
        parent,
        std::string { name },
        std::nullopt,
        default_type(kind),
    });
}

SystemVerilogVpiObjectResult SystemVerilogVpiObjectRegistry::create(
    const SystemVerilogVpiObjectDescriptor& descriptor)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { { }, SystemVerilogVpiObjectError::InvalidSimulation };
    }
    if (!supports(descriptor.kind)) {
        return { { }, SystemVerilogVpiObjectError::InvalidKind };
    }

    std::string normalized_name;
    std::string hierarchy_segment;
    if (!normalize_name(
            descriptor.name, normalized_name, hierarchy_segment)) {
        return { { }, SystemVerilogVpiObjectError::InvalidName };
    }
    if (descriptor.source
        && (descriptor.source->file.empty()
            || descriptor.source->file.size() > maximum_source_size
            || descriptor.source->file.find('\0') != std::string::npos
            || descriptor.source->line == 0U
            || descriptor.source->column == 0U)) {
        return { { }, SystemVerilogVpiObjectError::InvalidSource };
    }
    if (!valid_type(descriptor.kind, descriptor.type)) {
        return { { }, SystemVerilogVpiObjectError::InvalidType };
    }
    auto owned_type = descriptor.type;
    if (owned_type && owned_type->descriptor) {
        owned_type->descriptor = std::make_shared<const SystemVerilogVpiTypeDescriptor>(
            *owned_type->descriptor);
    }

    std::uint32_t parent_slot { };
    const bool top_level
        = descriptor.kind == SystemVerilogVpiObjectKind::Root
        || ((descriptor.kind == SystemVerilogVpiObjectKind::Package
                || descriptor.kind == SystemVerilogVpiObjectKind::Class)
            && descriptor.parent == 0U);
    if (top_level) {
        if (descriptor.kind == SystemVerilogVpiObjectKind::Root
            && descriptor.parent != 0U) {
            return { { }, SystemVerilogVpiObjectError::InvalidParent };
        }
    } else {
        const auto parent_error = resolve_object(descriptor.parent, parent_slot);
        if (parent_error != SystemVerilogVpiObjectError::None) {
            return { { }, SystemVerilogVpiObjectError::InvalidParent };
        }
    }

    auto key = sibling_key(descriptor.parent, normalized_name);
    if (siblings_.contains(key)) {
        return { { }, SystemVerilogVpiObjectError::DuplicateName };
    }
    std::string full_name;
    if (top_level) {
        full_name = hierarchy_segment;
    } else {
        full_name = records_[parent_slot].full_name;
        const bool indexed_child
            = (records_[parent_slot].kind
                      == SystemVerilogVpiObjectKind::Memory
                  || records_[parent_slot].kind
                      == SystemVerilogVpiObjectKind::Array)
            && normalized_name.starts_with('[')
            && normalized_name.ends_with(']');
        if (!indexed_child) {
            full_name.push_back('.');
        }
        full_name.append(hierarchy_segment);
    }
    if (full_names_.contains(full_name)) {
        return { { }, SystemVerilogVpiObjectError::DuplicateName };
    }

    std::uint32_t slot { };
    bool reused { };
    while (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
        if (records_[slot].epoch != std::numeric_limits<std::uint16_t>::max()) {
            reused = true;
            break;
        }
    }
    const auto ordinal = next_ordinal_++;
    if (reused) {
        auto& record = records_[slot];
        ++record.epoch;
        record.live = true;
        record.live_children = 0;
        record.ordinal = ordinal;
        record.parent = descriptor.parent;
        record.kind = descriptor.kind;
        record.name = std::move(normalized_name);
        record.full_name = std::move(full_name);
        record.source = descriptor.source;
        record.type = owned_type;
        record.forced_value.reset();
        record.value.reset();
        record.initial_value.reset();
    } else {
        if (records_.size() >= slot_mask) {
            return { { }, SystemVerilogVpiObjectError::ResourceLimit };
        }
        slot = static_cast<std::uint32_t>(records_.size());
        records_.push_back(Record {
            0,
            true,
            0,
            ordinal,
            descriptor.parent,
            descriptor.kind,
            std::move(normalized_name),
            std::move(full_name),
            descriptor.source,
            owned_type,
            std::nullopt,
            std::nullopt,
            std::nullopt,
        });
    }

    const auto handle = encode_object(slot, records_[slot].epoch);
    siblings_.emplace(std::move(key), handle);
    full_names_.emplace(records_[slot].full_name, handle);
    if (!top_level) {
        ++records_[parent_slot].live_children;
    }
    return { handle, { } };
}

SystemVerilogVpiObjectLookupResult
SystemVerilogVpiObjectRegistry::lookup_locked(
    const fsim_vpi_handle_v1 handle) const
{
    if (!valid()) {
        return { { }, SystemVerilogVpiObjectError::InvalidSimulation };
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return { { }, error };
    }
    const auto& record = records_[slot];
    auto type = record.type;
    if (type && type->semantic_unit.empty()) {
        auto parent = record.parent;
        while (parent != 0U) {
            std::uint32_t parent_slot { };
            if (resolve_object(parent, parent_slot)
                != SystemVerilogVpiObjectError::None) {
                break;
            }
            const auto& ancestor = records_[parent_slot];
            if (ancestor.type && !ancestor.type->semantic_unit.empty()) {
                type->language = ancestor.type->language;
                type->semantic_unit_id = ancestor.type->semantic_unit_id;
                type->source_id = ancestor.type->source_id;
                type->semantic_unit = ancestor.type->semantic_unit;
                type->source_path = ancestor.type->source_path;
                type->source_line = ancestor.type->source_line;
                type->source_column = ancestor.type->source_column;
                type->standard = ancestor.type->standard;
                type->compatibility_profile
                    = ancestor.type->compatibility_profile;
                break;
            }
            parent = ancestor.parent;
        }
    }
    return { SystemVerilogVpiObjectInfo {
                 handle,
                 record.parent,
                 record.kind,
                 record.live_children,
                 record.ordinal,
                 record.name,
                 record.full_name,
                 record.source,
                 std::move(type),
             },
        { } };
}

SystemVerilogVpiObjectLookupResult SystemVerilogVpiObjectRegistry::lookup(
    const fsim_vpi_handle_v1 handle) const
{
    std::scoped_lock lock { mutex_ };
    return lookup_locked(handle);
}

SystemVerilogVpiObjectLookupResult SystemVerilogVpiObjectRegistry::find(
    const std::string_view full_name) const
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { { }, SystemVerilogVpiObjectError::InvalidSimulation };
    }
    const auto found = full_names_.find(std::string { full_name });
    if (found == full_names_.end()) {
        return { { }, SystemVerilogVpiObjectError::NotFound };
    }
    return lookup_locked(found->second);
}

SystemVerilogVpiObjectLookupResult
SystemVerilogVpiObjectRegistry::find_child(
    const fsim_vpi_handle_v1 parent, const std::string_view name) const
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { { }, SystemVerilogVpiObjectError::InvalidSimulation };
    }
    std::uint32_t parent_slot { };
    if (resolve_object(parent, parent_slot)
        != SystemVerilogVpiObjectError::None) {
        return { { }, SystemVerilogVpiObjectError::InvalidParent };
    }
    std::string normalized;
    std::string segment;
    if (!normalize_name(name, normalized, segment)) {
        return { { }, SystemVerilogVpiObjectError::InvalidName };
    }
    const auto found = siblings_.find(sibling_key(parent, normalized));
    if (found == siblings_.end()) {
        return { { }, SystemVerilogVpiObjectError::NotFound };
    }
    return lookup_locked(found->second);
}

SystemVerilogVpiTypeLookupResult
SystemVerilogVpiObjectRegistry::type_info(
    const fsim_vpi_handle_v1 handle) const
{
    std::scoped_lock lock { mutex_ };
    const auto object = lookup_locked(handle);
    if (!object) {
        return { { }, object.error };
    }
    if (!object.value->type) {
        return { { }, SystemVerilogVpiObjectError::InvalidType };
    }
    return { object.value->type, { } };
}

SystemVerilogVpiPropertyResult SystemVerilogVpiObjectRegistry::property(
    const fsim_vpi_handle_v1 handle,
    const SystemVerilogVpiPropertyKind property_kind) const
{
    std::scoped_lock lock { mutex_ };
    const auto object = lookup_locked(handle);
    if (!object) {
        SystemVerilogVpiPropertyResult result;
        result.error = object.error;
        return result;
    }
    if (!supports(property_kind)) {
        SystemVerilogVpiPropertyResult result;
        result.error = SystemVerilogVpiObjectError::InvalidProperty;
        return result;
    }
    const auto unsigned_result = [](const std::uint64_t value) {
        SystemVerilogVpiPropertyResult result;
        result.kind = SystemVerilogVpiPropertyValueKind::UnsignedInteger;
        result.unsigned_integer = value;
        return result;
    };
    const auto string_result = [](const std::string& value) {
        SystemVerilogVpiPropertyResult result;
        result.kind = SystemVerilogVpiPropertyValueKind::String;
        result.string = value;
        return result;
    };
    const auto boolean_result = [](const bool value) {
        SystemVerilogVpiPropertyResult result;
        result.kind = SystemVerilogVpiPropertyValueKind::Boolean;
        result.boolean = value;
        return result;
    };
    const auto missing = [] {
        SystemVerilogVpiPropertyResult result;
        result.error = SystemVerilogVpiObjectError::NotFound;
        return result;
    };
    const auto& value = *object.value;
    switch (property_kind) {
    case SystemVerilogVpiPropertyKind::ObjectKind: {
        SystemVerilogVpiPropertyResult result;
        result.kind = SystemVerilogVpiPropertyValueKind::ObjectKind;
        result.object_kind = value.kind;
        return result;
    }
    case SystemVerilogVpiPropertyKind::Parent: {
        SystemVerilogVpiPropertyResult result;
        result.kind = SystemVerilogVpiPropertyValueKind::Handle;
        result.handle = value.parent;
        return result;
    }
    case SystemVerilogVpiPropertyKind::LiveChildren:
        return unsigned_result(value.live_children);
    case SystemVerilogVpiPropertyKind::Ordinal:
        return unsigned_result(value.ordinal);
    case SystemVerilogVpiPropertyKind::Name:
        return string_result(value.name);
    case SystemVerilogVpiPropertyKind::FullName:
        return string_result(value.full_name);
    case SystemVerilogVpiPropertyKind::SourceFile:
        return value.source ? string_result(value.source->file) : missing();
    case SystemVerilogVpiPropertyKind::SourceLine:
        return value.source ? unsigned_result(value.source->line) : missing();
    case SystemVerilogVpiPropertyKind::SourceColumn:
        return value.source ? unsigned_result(value.source->column) : missing();
    default:
        break;
    }
    if (!value.type) {
        return missing();
    }
    const auto& type = *value.type;
    switch (property_kind) {
    case SystemVerilogVpiPropertyKind::Language:
        return unsigned_result(static_cast<std::uint32_t>(type.language));
    case SystemVerilogVpiPropertyKind::SemanticUnitId:
        return type.semantic_unit_id
            ? unsigned_result(*type.semantic_unit_id) : missing();
    case SystemVerilogVpiPropertyKind::SourceId:
        return type.source_id ? unsigned_result(*type.source_id) : missing();
    case SystemVerilogVpiPropertyKind::SemanticUnit:
        return type.semantic_unit.empty()
            ? missing() : string_result(type.semantic_unit);
    case SystemVerilogVpiPropertyKind::SourcePath:
        return type.source_path.empty()
            ? missing() : string_result(type.source_path);
    case SystemVerilogVpiPropertyKind::Standard:
        return type.standard.empty() ? missing() : string_result(type.standard);
    case SystemVerilogVpiPropertyKind::CompatibilityProfile:
        return type.compatibility_profile.empty()
            ? missing() : string_result(type.compatibility_profile);
    case SystemVerilogVpiPropertyKind::ValueCategory:
        return unsigned_result(static_cast<std::uint32_t>(type.category));
    case SystemVerilogVpiPropertyKind::NetKind:
        return unsigned_result(static_cast<std::uint32_t>(type.net_kind));
    case SystemVerilogVpiPropertyKind::Direction:
        return unsigned_result(static_cast<std::uint32_t>(type.direction));
    case SystemVerilogVpiPropertyKind::Lifetime:
        return unsigned_result(static_cast<std::uint32_t>(type.lifetime));
    case SystemVerilogVpiPropertyKind::Width:
        return unsigned_result(type.width);
    case SystemVerilogVpiPropertyKind::IsSigned:
        return boolean_result(type.is_signed);
    case SystemVerilogVpiPropertyKind::IsConstant:
        return boolean_result(type.is_constant);
    case SystemVerilogVpiPropertyKind::HasTypeDescriptor:
        return boolean_result(static_cast<bool>(type.descriptor));
    default:
        return missing();
    }
}
std::optional<std::uint64_t>
SystemVerilogVpiObjectRegistry::add_value_observer(
    SystemVerilogVpiValueObserver observer)
{
    std::scoped_lock lock { mutex_ };
    constexpr std::size_t maximum_observers = 65'536;
    if (!valid() || !observer
        || value_observers_.size() >= maximum_observers
        || next_value_observer_ == 0U) {
        return std::nullopt;
    }
    const auto id = next_value_observer_++;
    value_observers_.emplace(id, std::move(observer));
    return id;
}

bool SystemVerilogVpiObjectRegistry::remove_value_observer(
    const std::uint64_t observer)
{
    std::scoped_lock lock { mutex_ };
    return observer != 0U && value_observers_.erase(observer) == 1U;
}

std::optional<std::uint64_t>
SystemVerilogVpiObjectRegistry::add_value_state_observer(
    SystemVerilogVpiValueStateObserver observer)
{
    std::scoped_lock lock { mutex_ };
    constexpr std::size_t maximum_observers = 65'536;
    if (!valid() || !observer
        || value_state_observers_.size() >= maximum_observers
        || next_value_state_observer_ == 0U) {
        return std::nullopt;
    }
    const auto id = next_value_state_observer_++;
    value_state_observers_.emplace(id, std::move(observer));
    return id;
}

bool SystemVerilogVpiObjectRegistry::remove_value_state_observer(
    const std::uint64_t observer)
{
    std::scoped_lock lock { mutex_ };
    return observer != 0U
        && value_state_observers_.erase(observer) == 1U;
}

SystemVerilogVpiValueError SystemVerilogVpiObjectRegistry::bind_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type) {
        return SystemVerilogVpiValueError::NotReadable;
    }
    if (record.value) {
        return SystemVerilogVpiValueError::AlreadyBound;
    }
    if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
        return SystemVerilogVpiValueError::TypeMismatch;
    }
    record.initial_value = value;
    record.value = std::move(value);
    return SystemVerilogVpiValueError::None;
}
SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::validate_value_write(
    const fsim_vpi_handle_v1 handle,
    const SystemVerilogVpiStoredValue* const value) const
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    const auto& record = records_[slot];
    if (!record.type) {
        return SystemVerilogVpiValueError::NotReadable;
    }
    if (!writable_value(record.kind, *record.type)) {
        return write_access_error(record.kind, *record.type);
    }
    if (value
        && !validate_systemverilog_vpi_stored_value(*record.type, *value)) {
        return SystemVerilogVpiValueError::TypeMismatch;
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::update_bound_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value)
{
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::optional<SystemVerilogVpiStoredValue> published;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type || !record.value) {
        return record.type ? SystemVerilogVpiValueError::NotBound
                           : SystemVerilogVpiValueError::NotReadable;
    }
    if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
        return SystemVerilogVpiValueError::TypeMismatch;
    }
    const bool changed = !record.forced_value && *record.value != value;
    if (changed && !value_observers_.empty()) {
        published = value;
        observers.reserve(value_observers_.size());
        for (const auto& [id, observer] : value_observers_) {
            (void)id;
            observers.push_back(observer);
        }
    }
    record.value = std::move(value);
    lock.unlock();
    if (published) {
        notify_value_observers(observers, handle, *published);
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::update_forced_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value)
{
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::optional<SystemVerilogVpiStoredValue> published;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type || !record.value) {
        return record.type ? SystemVerilogVpiValueError::NotBound
                           : SystemVerilogVpiValueError::NotReadable;
    }
    if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
        return SystemVerilogVpiValueError::TypeMismatch;
    }
    const bool changed = !record.forced_value || *record.forced_value != value;
    if (changed && !value_observers_.empty()) {
        published = value;
        observers.reserve(value_observers_.size());
        for (const auto& [id, observer] : value_observers_) {
            (void)id;
            observers.push_back(observer);
        }
    }
    record.forced_value = std::move(value);
    lock.unlock();
    if (published) {
        notify_value_observers(observers, handle, *published);
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::release_bound_force(
    const fsim_vpi_handle_v1 handle)
{
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::optional<SystemVerilogVpiStoredValue> published;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type || !record.value) {
        return record.type ? SystemVerilogVpiValueError::NotBound
                           : SystemVerilogVpiValueError::NotReadable;
    }
    if (!record.forced_value) {
        return SystemVerilogVpiValueError::NotForced;
    }
    if (*record.forced_value != *record.value
        && !value_observers_.empty()) {
        published = record.value;
        observers.reserve(value_observers_.size());
        for (const auto& [id, observer] : value_observers_) {
            (void)id;
            observers.push_back(observer);
        }
    }
    record.forced_value.reset();
    lock.unlock();
    if (published) {
        notify_value_observers(observers, handle, *published);
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError SystemVerilogVpiObjectRegistry::deposit_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value)
{
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::vector<SystemVerilogVpiValueStateObserver> state_observers;
    std::optional<SystemVerilogVpiStoredValue> published;
    std::optional<SystemVerilogVpiValueStateUpdate> state_update;
    std::optional<SystemVerilogVpiStoredValue> previous_value;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type) {
        return SystemVerilogVpiValueError::NotReadable;
    }
    if (!writable_value(record.kind, *record.type)) {
        return write_access_error(record.kind, *record.type);
    }
    if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
        return SystemVerilogVpiValueError::TypeMismatch;
    }
    const bool changed = !record.forced_value
        && (!record.value || *record.value != value);
    if (changed && !value_observers_.empty()) {
        published = value;
        observers.reserve(value_observers_.size());
        for (const auto& [id, observer] : value_observers_) {
            (void)id;
            observers.push_back(observer);
        }
    }
    if (!value_state_observers_.empty()) {
        state_update = SystemVerilogVpiValueStateUpdate {
            handle, value, record.forced_value
        };
        state_observers.reserve(value_state_observers_.size());
        for (const auto& [id, observer] : value_state_observers_) {
            (void)id;
            state_observers.push_back(observer);
        }
    }
    previous_value = record.value;
    record.value = std::move(value);
    lock.unlock();
    if (state_update) {
        const auto state_error
            = notify_value_state_observers(state_observers, *state_update);
        if (state_error != SystemVerilogVpiValueError::None) {
            std::scoped_lock rollback_lock { mutex_ };
            records_[slot].value = std::move(previous_value);
            return state_error;
        }
    }
    if (published) {
        notify_value_observers(observers, handle, *published);
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError SystemVerilogVpiObjectRegistry::force_value(
    const fsim_vpi_handle_v1 handle,
    SystemVerilogVpiStoredValue value)
{
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::vector<SystemVerilogVpiValueStateObserver> state_observers;
    std::optional<SystemVerilogVpiStoredValue> published;
    std::optional<SystemVerilogVpiValueStateUpdate> state_update;
    std::optional<SystemVerilogVpiStoredValue> previous_forced_value;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type) {
        return SystemVerilogVpiValueError::NotReadable;
    }
    if (!writable_value(record.kind, *record.type)) {
        return write_access_error(record.kind, *record.type);
    }
    if (!validate_systemverilog_vpi_stored_value(*record.type, value)) {
        return SystemVerilogVpiValueError::TypeMismatch;
    }
    const bool changed = !record.forced_value || *record.forced_value != value;
    if (changed && !value_observers_.empty()) {
        published = value;
        observers.reserve(value_observers_.size());
        for (const auto& [id, observer] : value_observers_) {
            (void)id;
            observers.push_back(observer);
        }
    }
    if (!value_state_observers_.empty() && record.value) {
        state_update = SystemVerilogVpiValueStateUpdate {
            handle, *record.value, value
        };
        state_observers.reserve(value_state_observers_.size());
        for (const auto& [id, observer] : value_state_observers_) {
            (void)id;
            state_observers.push_back(observer);
        }
    }
    previous_forced_value = record.forced_value;
    record.forced_value = std::move(value);
    lock.unlock();
    if (state_update) {
        const auto state_error
            = notify_value_state_observers(state_observers, *state_update);
        if (state_error != SystemVerilogVpiValueError::None) {
            std::scoped_lock rollback_lock { mutex_ };
            records_[slot].forced_value = std::move(previous_forced_value);
            return state_error;
        }
    }
    if (published) {
        notify_value_observers(observers, handle, *published);
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::release_forced_value(
    const fsim_vpi_handle_v1 handle)
{
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::vector<SystemVerilogVpiValueStateObserver> state_observers;
    std::optional<SystemVerilogVpiStoredValue> published;
    std::optional<SystemVerilogVpiValueStateUpdate> state_update;
    std::optional<SystemVerilogVpiStoredValue> previous_forced_value;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_error(error);
    }
    auto& record = records_[slot];
    if (!record.type) {
        return SystemVerilogVpiValueError::NotReadable;
    }
    if (!writable_value(record.kind, *record.type)) {
        return write_access_error(record.kind, *record.type);
    }
    if (!record.forced_value) {
        return SystemVerilogVpiValueError::NotForced;
    }
    const bool changed = record.value && *record.forced_value != *record.value;
    if (changed && !value_observers_.empty()) {
        published = record.value;
        observers.reserve(value_observers_.size());
        for (const auto& [id, observer] : value_observers_) {
            (void)id;
            observers.push_back(observer);
        }
    }
    if (!value_state_observers_.empty() && record.value) {
        state_update = SystemVerilogVpiValueStateUpdate {
            handle, *record.value, std::nullopt
        };
        state_observers.reserve(value_state_observers_.size());
        for (const auto& [id, observer] : value_state_observers_) {
            (void)id;
            state_observers.push_back(observer);
        }
    }
    previous_forced_value = record.forced_value;
    record.forced_value.reset();
    lock.unlock();
    if (state_update) {
        const auto state_error
            = notify_value_state_observers(state_observers, *state_update);
        if (state_error != SystemVerilogVpiValueError::None) {
            std::scoped_lock rollback_lock { mutex_ };
            records_[slot].forced_value = std::move(previous_forced_value);
            return state_error;
        }
    }
    if (published) {
        notify_value_observers(observers, handle, *published);
    }
    return SystemVerilogVpiValueError::None;
}
SystemVerilogVpiValueError
SystemVerilogVpiObjectRegistry::reset_values()
{
    struct ResetChange {
        std::size_t slot { };
        fsim_vpi_handle_v1 handle { };
        SystemVerilogVpiStoredValue reset_value;
        std::optional<SystemVerilogVpiStoredValue> published;
        SystemVerilogVpiValueStateUpdate state_update;
    };

    std::vector<ResetChange> changes;
    std::vector<SystemVerilogVpiValueObserver> observers;
    std::vector<SystemVerilogVpiValueStateObserver> state_observers;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiValueError::InvalidSimulation;
    }
    try {
        changes.reserve(records_.size());
        for (std::size_t slot = 0; slot < records_.size(); ++slot) {
            const auto& record = records_[slot];
            if (!record.live || !record.value || !record.initial_value) {
                continue;
            }
            const auto& visible = record.forced_value ? *record.forced_value : *record.value;
            ResetChange change;
            change.slot = slot;
            change.handle = encode_object(
                static_cast<std::uint32_t>(slot), record.epoch);
            change.reset_value = *record.initial_value;
            change.state_update = SystemVerilogVpiValueStateUpdate {
                change.handle, *record.initial_value, std::nullopt
            };
            if (visible != *record.initial_value
                && !value_observers_.empty()) {
                change.published = *record.initial_value;
            }
            changes.push_back(std::move(change));
        }
        if (std::ranges::any_of(
                changes,
                [](const ResetChange& change) {
                    return change.published.has_value();
                })) {
            observers.reserve(value_observers_.size());
            for (const auto& [id, observer] : value_observers_) {
                (void)id;
                observers.push_back(observer);
            }
        }
        if (!changes.empty() && !value_state_observers_.empty()) {
            state_observers.reserve(value_state_observers_.size());
            for (const auto& [id, observer] : value_state_observers_) {
                (void)id;
                state_observers.push_back(observer);
            }
        }
    } catch (...) {
        return SystemVerilogVpiValueError::ResourceLimit;
    }

    for (auto& change : changes) {
        auto& record = records_[change.slot];
        record.value = std::move(change.reset_value);
        record.forced_value.reset();
    }
    lock.unlock();

    for (const auto& change : changes) {
        if (change.published) {
            notify_value_observers(
                observers, change.handle, *change.published);
        }
        if (!state_observers.empty()) {
            const auto state_error = notify_value_state_observers(
                state_observers, change.state_update);
            if (state_error != SystemVerilogVpiValueError::None) {
                return state_error;
            }
        }
    }
    return SystemVerilogVpiValueError::None;
}

SystemVerilogVpiObjectStateSnapshot
SystemVerilogVpiObjectRegistry::snapshot_values() const
{
    SystemVerilogVpiObjectStateSnapshot snapshot;
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        snapshot.error = SystemVerilogVpiObjectStateError::InvalidSimulation;
        return snapshot;
    }
    snapshot.simulation_identity = simulation_identity_;
    try {
        snapshot.objects.reserve(records_.size());
        for (std::size_t slot = 0; slot < records_.size(); ++slot) {
            const auto& record = records_[slot];
            if (!record.live || !record.type) {
                continue;
            }
            snapshot.objects.push_back(SystemVerilogVpiObjectState {
                encode_object(
                    static_cast<std::uint32_t>(slot), record.epoch),
                record.full_name,
                *record.type,
                record.value,
                record.forced_value,
            });
        }
    } catch (...) {
        snapshot.objects.clear();
        snapshot.error = SystemVerilogVpiObjectStateError::ResourceLimit;
    }
    return snapshot;
}

SystemVerilogVpiObjectStateRestoreResult
SystemVerilogVpiObjectRegistry::restore_values(
    const SystemVerilogVpiObjectStateSnapshot& snapshot)
{
    struct PendingRestore {
        std::uint32_t slot { };
        fsim_vpi_handle_v1 handle { };
        std::optional<SystemVerilogVpiStoredValue> value;
        std::optional<SystemVerilogVpiStoredValue> forced_value;
    };

    SystemVerilogVpiObjectStateRestoreResult result;
    std::unique_lock lock { mutex_ };
    if (!valid()) {
        result.error = SystemVerilogVpiObjectStateError::InvalidSimulation;
        return result;
    }
    if (snapshot.error != SystemVerilogVpiObjectStateError::None
        || snapshot.simulation_identity == 0) {
        result.error = SystemVerilogVpiObjectStateError::InvalidState;
        return result;
    }

    std::vector<PendingRestore> pending;
    std::unordered_set<std::string> names;
    std::unordered_set<fsim_vpi_handle_v1> handles;
    try {
        const auto restorable_objects = std::ranges::count_if(
            records_, [](const Record& record) {
                return record.live && record.type.has_value();
            });
        if (snapshot.objects.size()
            != static_cast<std::size_t>(restorable_objects)) {
            result.error = SystemVerilogVpiObjectStateError::InvalidState;
            return result;
        }
        pending.reserve(snapshot.objects.size());
        result.handles.reserve(snapshot.objects.size());
        names.reserve(snapshot.objects.size());
        handles.reserve(snapshot.objects.size());
        for (const auto& state : snapshot.objects) {
            if (state.source_handle == 0U || state.full_name.empty()
                || !names.insert(state.full_name).second
                || !handles.insert(state.source_handle).second) {
                result.error = SystemVerilogVpiObjectStateError::InvalidState;
                return result;
            }
            const auto found = full_names_.find(state.full_name);
            if (found == full_names_.end()) {
                result.error = SystemVerilogVpiObjectStateError::MissingObject;
                return result;
            }
            std::uint32_t slot { };
            if (resolve_object(found->second, slot)
                != SystemVerilogVpiObjectError::None) {
                result.error = SystemVerilogVpiObjectStateError::MissingObject;
                return result;
            }
            const auto& record = records_[slot];
            if (!record.type || !equivalent_type(*record.type, state.type)
                || (state.value
                    && !validate_systemverilog_vpi_stored_value(
                        *record.type, *state.value))
                || (state.forced_value
                    && !validate_systemverilog_vpi_stored_value(
                        *record.type, *state.forced_value))) {
                result.error = SystemVerilogVpiObjectStateError::TypeMismatch;
                return result;
            }
            pending.push_back(
                { slot, found->second, state.value, state.forced_value });
            result.handles.push_back(
                { state.source_handle, found->second });
        }
    } catch (...) {
        result.handles.clear();
        result.error = SystemVerilogVpiObjectStateError::ResourceLimit;
        return result;
    }

    std::vector<SystemVerilogVpiValueStateObserver> state_observers;
    std::vector<SystemVerilogVpiValueStateUpdate> state_updates;
    try {
        if (!value_state_observers_.empty()) {
            state_observers.reserve(value_state_observers_.size());
            for (const auto& [id, observer] : value_state_observers_) {
                (void)id;
                state_observers.push_back(observer);
            }
            state_updates.reserve(pending.size());
            for (const auto& restore : pending) {
                if (restore.value) {
                    state_updates.push_back(SystemVerilogVpiValueStateUpdate {
                        restore.handle, *restore.value, restore.forced_value });
                }
            }
        }
    } catch (...) {
        result.handles.clear();
        result.error = SystemVerilogVpiObjectStateError::ResourceLimit;
        return result;
    }

    for (auto& restore : pending) {
        auto& record = records_[restore.slot];
        record.value = std::move(restore.value);
        record.forced_value = std::move(restore.forced_value);
    }
    lock.unlock();
    for (const auto& update : state_updates) {
        if (notify_value_state_observers(state_observers, update)
            != SystemVerilogVpiValueError::None) {
            result.handles.clear();
            result.error = SystemVerilogVpiObjectStateError::ResourceLimit;
            return result;
        }
    }
    return result;
}

SystemVerilogVpiValueReadResult SystemVerilogVpiObjectRegistry::read_value(
    const fsim_vpi_handle_v1 handle,
    const SystemVerilogVpiValueFormat format,
    const SystemVerilogVpiValueReadBuffers buffers) const
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return value_failure(SystemVerilogVpiValueError::InvalidSimulation);
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return value_failure(value_error(error));
    }
    const auto& record = records_[slot];
    if (!record.type) {
        return value_failure(SystemVerilogVpiValueError::NotReadable);
    }
    const auto& visible_value = record.forced_value ? record.forced_value : record.value;
    if (!visible_value) {
        return value_failure(SystemVerilogVpiValueError::NotBound);
    }
    return read_systemverilog_vpi_value(
        *record.type, *visible_value, format, buffers);
}

SystemVerilogVpiObjectError SystemVerilogVpiObjectRegistry::release(
    const fsim_vpi_handle_v1 handle)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiObjectError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_object(handle, slot);
    if (error != SystemVerilogVpiObjectError::None) {
        return error;
    }
    auto& record = records_[slot];
    if (record.live_children != 0U) {
        return SystemVerilogVpiObjectError::HasChildren;
    }

    siblings_.erase(sibling_key(record.parent, record.name));
    record.value.reset();
    record.forced_value.reset();
    record.initial_value.reset();
    full_names_.erase(record.full_name);
    record.live = false;
    if (record.kind != SystemVerilogVpiObjectKind::Root) {
        std::uint32_t parent_slot { };
        if (resolve_object(record.parent, parent_slot)
            == SystemVerilogVpiObjectError::None) {
            --records_[parent_slot].live_children;
        }
    }
    free_slots_.push_back(slot);
    return SystemVerilogVpiObjectError::None;
}
SystemVerilogVpiIteratorResult
SystemVerilogVpiObjectRegistry::create_iterator_locked(
    std::vector<fsim_vpi_handle_v1> objects)
{
    if (!valid()) {
        return { { }, SystemVerilogVpiIteratorError::InvalidSimulation };
    }
    if (objects.size() > maximum_iterator_objects) {
        return { { }, SystemVerilogVpiIteratorError::ResourceLimit };
    }

    std::uint32_t slot { };
    bool reused { };
    while (!free_iterators_.empty()) {
        slot = free_iterators_.back();
        free_iterators_.pop_back();
        if (iterators_[slot].epoch
            != std::numeric_limits<std::uint16_t>::max()) {
            reused = true;
            break;
        }
    }
    if (reused) {
        auto& iterator = iterators_[slot];
        ++iterator.epoch;
        iterator.live = true;
        iterator.cursor = 0;
        iterator.objects = std::move(objects);
    } else {
        if (iterators_.size() >= slot_mask) {
            return { { }, SystemVerilogVpiIteratorError::ResourceLimit };
        }
        slot = static_cast<std::uint32_t>(iterators_.size());
        try {
            iterators_.push_back(
                IteratorRecord { 0, true, 0, std::move(objects) });
        } catch (...) {
            return { { }, SystemVerilogVpiIteratorError::ResourceLimit };
        }
    }
    return { encode_iterator(slot, iterators_[slot].epoch), { } };
}
SystemVerilogVpiIteratorResult
SystemVerilogVpiObjectRegistry::iterate_children(
    const fsim_vpi_handle_v1 parent)
{
    return iterate_relationship(
        parent, SystemVerilogVpiRelationshipKind::Children);
}
SystemVerilogVpiIteratorResult
SystemVerilogVpiObjectRegistry::iterate_objects(
    const SystemVerilogVpiObjectKind kind,
    const fsim_vpi_handle_v1 parent)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { { }, SystemVerilogVpiIteratorError::InvalidSimulation };
    }
    if (!supports(kind)) {
        return { { }, SystemVerilogVpiIteratorError::InvalidKind };
    }
    if (parent != 0U) {
        std::uint32_t parent_slot { };
        if (resolve_object(parent, parent_slot)
            != SystemVerilogVpiObjectError::None) {
            return { { }, SystemVerilogVpiIteratorError::InvalidObject };
        }
    }
    std::vector<std::pair<std::uint64_t, fsim_vpi_handle_v1>> ordered;
    try {
        for (std::uint32_t slot = 0; slot < records_.size(); ++slot) {
            const auto& record = records_[slot];
            if (record.live && record.kind == kind
                && (parent == 0U || record.parent == parent)) {
                ordered.emplace_back(
                    record.ordinal, encode_object(slot, record.epoch));
            }
        }
        std::ranges::sort(ordered);
        std::vector<fsim_vpi_handle_v1> objects;
        objects.reserve(ordered.size());
        for (const auto& [ordinal, handle] : ordered) {
            (void)ordinal;
            objects.push_back(handle);
        }
        return create_iterator_locked(std::move(objects));
    } catch (...) {
        return { { }, SystemVerilogVpiIteratorError::ResourceLimit };
    }
}

SystemVerilogVpiIteratorResult
SystemVerilogVpiObjectRegistry::iterate_relationship(
    const fsim_vpi_handle_v1 object,
    const SystemVerilogVpiRelationshipKind relationship)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { { }, SystemVerilogVpiIteratorError::InvalidSimulation };
    }
    if (!supports(relationship)) {
        return { { }, SystemVerilogVpiIteratorError::InvalidRelationship };
    }
    std::uint32_t object_slot { };
    if (resolve_object(object, object_slot)
        != SystemVerilogVpiObjectError::None) {
        return { { }, SystemVerilogVpiIteratorError::InvalidObject };
    }
    if (relationship == SystemVerilogVpiRelationshipKind::Parent) {
        const auto parent = records_[object_slot].parent;
        if (parent == 0U) {
            return create_iterator_locked({ });
        }
        return create_iterator_locked({ parent });
    }
    std::vector<std::pair<std::uint64_t, fsim_vpi_handle_v1>> ordered;
    try {
        for (std::uint32_t slot = 0; slot < records_.size(); ++slot) {
            const auto& record = records_[slot];
            if (record.live && record.parent == object
                && relationship_matches(relationship, record.kind)) {
                ordered.emplace_back(
                    record.ordinal, encode_object(slot, record.epoch));
            }
        }
        std::ranges::sort(ordered);
        std::vector<fsim_vpi_handle_v1> objects;
        objects.reserve(ordered.size());
        for (const auto& [ordinal, handle] : ordered) {
            (void)ordinal;
            objects.push_back(handle);
        }
        return create_iterator_locked(std::move(objects));
    } catch (...) {
        return { { }, SystemVerilogVpiIteratorError::ResourceLimit };
    }
}

SystemVerilogVpiIteratorScanResult SystemVerilogVpiObjectRegistry::scan(
    const fsim_vpi_handle_v1 iterator)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return { { }, SystemVerilogVpiIteratorError::InvalidSimulation };
    }
    std::uint32_t slot { };
    const auto error = resolve_iterator(iterator, slot);
    if (error != SystemVerilogVpiIteratorError::None) {
        return { { }, error };
    }
    auto& record = iterators_[slot];
    if (record.cursor == record.objects.size()) {
        return { { }, SystemVerilogVpiIteratorError::End };
    }
    const auto object = record.objects[record.cursor];
    std::uint32_t object_slot { };
    const auto object_error = resolve_object(object, object_slot);
    if (object_error != SystemVerilogVpiObjectError::None) {
        return { { }, object_error == SystemVerilogVpiObjectError::CrossSimulation
                ? SystemVerilogVpiIteratorError::CrossSimulation
                : object_error == SystemVerilogVpiObjectError::StaleHandle
                    ? SystemVerilogVpiIteratorError::StaleHandle
                    : object_error == SystemVerilogVpiObjectError::ReleasedHandle
                        ? SystemVerilogVpiIteratorError::ReleasedHandle
                        : SystemVerilogVpiIteratorError::InvalidObject };
    }
    ++record.cursor;
    return { object, { } };
}

SystemVerilogVpiIteratorError
SystemVerilogVpiObjectRegistry::release_iterator(
    const fsim_vpi_handle_v1 iterator)
{
    std::scoped_lock lock { mutex_ };
    if (!valid()) {
        return SystemVerilogVpiIteratorError::InvalidSimulation;
    }
    std::uint32_t slot { };
    const auto error = resolve_iterator(iterator, slot);
    if (error != SystemVerilogVpiIteratorError::None) {
        return error;
    }
    iterators_[slot].live = false;
    iterators_[slot].objects.clear();
    free_iterators_.push_back(slot);
    return SystemVerilogVpiIteratorError::None;
}

} // namespace fsim::runtime
