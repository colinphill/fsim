// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_object.hpp"
#include "fsim/runtime/vpi_type_descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

namespace {

    using fsim::runtime::SystemVerilogVpiDescriptorError;
    using fsim::runtime::SystemVerilogVpiDescriptorKind;
    using fsim::runtime::SystemVerilogVpiEnumLiteral;
    using fsim::runtime::SystemVerilogVpiLanguage;
    using fsim::runtime::SystemVerilogVpiObjectDescriptor;
    using fsim::runtime::SystemVerilogVpiObjectError;
    using fsim::runtime::SystemVerilogVpiObjectKind;
    using fsim::runtime::SystemVerilogVpiObjectRegistry;
    using fsim::runtime::SystemVerilogVpiTypeDescriptor;
    using fsim::runtime::SystemVerilogVpiTypeInfo;
    using fsim::runtime::SystemVerilogVpiValueCategory;
    using fsim::runtime::validate_systemverilog_vpi_descriptor;

    void require_vpi_descriptor(const bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    SystemVerilogVpiTypeDescriptor scalar(
        const std::uint32_t width = 8,
        const SystemVerilogVpiValueCategory category = SystemVerilogVpiValueCategory::Logic4,
        const bool is_signed = false)
    {
        SystemVerilogVpiTypeDescriptor descriptor;
        descriptor.category = category;
        descriptor.width = width;
        descriptor.is_signed = is_signed;
        return descriptor;
    }

    SystemVerilogVpiTypeDescriptor string_descriptor()
    {
        SystemVerilogVpiTypeDescriptor descriptor;
        descriptor.kind = SystemVerilogVpiDescriptorKind::String;
        descriptor.category = SystemVerilogVpiValueCategory::String;
        descriptor.width = 0;
        return descriptor;
    }

    SystemVerilogVpiTypeDescriptor enum_descriptor()
    {
        auto descriptor = scalar(
            2, SystemVerilogVpiValueCategory::Integer2, false);
        descriptor.kind = SystemVerilogVpiDescriptorKind::Enum;
        descriptor.enum_literals = {
            SystemVerilogVpiEnumLiteral {
                "Idle", fsim::runtime::PackedLogic4::from_msb_string("00") },
            SystemVerilogVpiEnumLiteral {
                "Busy", fsim::runtime::PackedLogic4::from_msb_string("01") },
            SystemVerilogVpiEnumLiteral {
                "Done", fsim::runtime::PackedLogic4::from_msb_string("10") },
        };
        return descriptor;
    }

    SystemVerilogVpiTypeDescriptor aggregate(
        const SystemVerilogVpiDescriptorKind kind,
        std::vector<SystemVerilogVpiTypeDescriptor> children,
        std::vector<std::string> names = { })
    {
        SystemVerilogVpiTypeDescriptor descriptor;
        descriptor.kind = kind;
        descriptor.category = SystemVerilogVpiValueCategory::None;
        descriptor.width = 0;
        descriptor.children = std::move(children);
        descriptor.member_names = std::move(names);
        return descriptor;
    }

    SystemVerilogVpiTypeInfo descriptor_type(
        const SystemVerilogVpiTypeDescriptor& descriptor)
    {
        SystemVerilogVpiTypeInfo type;
        type.descriptor = std::make_shared<const SystemVerilogVpiTypeDescriptor>(descriptor);
        if (descriptor.kind == SystemVerilogVpiDescriptorKind::Scalar
            || descriptor.kind == SystemVerilogVpiDescriptorKind::Enum
            || descriptor.kind == SystemVerilogVpiDescriptorKind::String) {
            type.category = descriptor.category;
            type.width = descriptor.width;
            type.is_signed = descriptor.is_signed;
        }
        return type;
    }

} // namespace

void test_systemverilog_vpi_recursive_type_descriptors()
{
    auto packed = aggregate(
        SystemVerilogVpiDescriptorKind::PackedArray, { scalar(4) });
    packed.ranges = { { 7, 0 }, { 0, 1 } };
    const auto packed_layout = validate_systemverilog_vpi_descriptor(packed);
    require_vpi_descriptor(
        packed_layout && packed_layout.value.node_count == 2
            && packed_layout.value.fixed_element_count == 16
            && packed_layout.value.fixed_bits == 64
            && packed_layout.value.maximum_depth == 2
            && !packed_layout.value.dynamic,
        "VPI descriptors measure ascending and descending packed ranges");

    const auto maximum_scalar = validate_systemverilog_vpi_descriptor(
        scalar(std::numeric_limits<std::uint32_t>::max()));
    auto wide_packed = aggregate(
        SystemVerilogVpiDescriptorKind::PackedArray, { scalar() });
    wide_packed.ranges = { { 1'200'000, 0 } };
    const auto wide_packed_layout
        = validate_systemverilog_vpi_descriptor(wide_packed);
    require_vpi_descriptor(
        maximum_scalar
            && maximum_scalar.value.fixed_bits
                == std::numeric_limits<std::uint32_t>::max()
            && wide_packed_layout
            && wide_packed_layout.value.fixed_bits == 9'600'008,
        "VPI packed descriptors preserve the host width domain without arbitrary bit limits");

    auto wide_enum = scalar(
        129, SystemVerilogVpiValueCategory::Logic4, true);
    wide_enum.kind = SystemVerilogVpiDescriptorKind::Enum;
    wide_enum.enum_literals = {
        { "Minimum",
            fsim::runtime::PackedLogic4::from_msb_string(
                "1" + std::string(128, '0')) },
        { "Unknown",
            fsim::runtime::PackedLogic4::from_msb_string(
                "X" + std::string(127, '0') + "Z") },
        { "Maximum",
            fsim::runtime::PackedLogic4::from_msb_string(
                "0" + std::string(128, '1')) },
    };
    const auto wide_enum_layout
        = validate_systemverilog_vpi_descriptor(wide_enum);
    require_vpi_descriptor(
        wide_enum_layout && wide_enum_layout.value.fixed_bits == 129
            && wide_enum.enum_literals[0].value.to_msb_string()
                == "1" + std::string(128, '0')
            && wide_enum.enum_literals[1].value.to_msb_string()
                == "X" + std::string(127, '0') + "Z",
        "VPI enum descriptors preserve arbitrary-width value and unknown-plane identity");

    auto record = aggregate(
        SystemVerilogVpiDescriptorKind::Struct,
        { packed, enum_descriptor(), string_descriptor() },
        { "payload", "state", "label" });
    const auto record_layout = validate_systemverilog_vpi_descriptor(record);
    require_vpi_descriptor(
        record_layout && record_layout.value.node_count == 5
            && record_layout.value.maximum_depth == 3
            && record_layout.value.dynamic
            && record_layout.value.fixed_element_count == 0
            && record_layout.value.fixed_bits == 0,
        "VPI descriptors recursively retain structs, enums, strings, and packed arrays");

    auto memory = aggregate(
        SystemVerilogVpiDescriptorKind::UnpackedArray, { record });
    memory.ranges = { { 3, 0 } };
    const auto dynamic_memory_layout = validate_systemverilog_vpi_descriptor(memory);
    require_vpi_descriptor(
        dynamic_memory_layout && dynamic_memory_layout.value.node_count == 6
            && dynamic_memory_layout.value.maximum_depth == 4
            && dynamic_memory_layout.value.dynamic
            && dynamic_memory_layout.value.fixed_element_count == 0
            && dynamic_memory_layout.value.fixed_bits == 0,
        "VPI unpacked arrays retain fixed extents around dynamic records");
    memory.children = { scalar(16) };
    const auto memory_layout = validate_systemverilog_vpi_descriptor(memory);
    require_vpi_descriptor(
        memory_layout && memory_layout.value.fixed_element_count == 4
            && memory_layout.value.fixed_bits == 64,
        "VPI unpacked-memory descriptors preserve fixed extents");

    auto queue = aggregate(
        SystemVerilogVpiDescriptorKind::Queue, { record });
    queue.maximum_size = 32;
    auto dynamic_array = aggregate(
        SystemVerilogVpiDescriptorKind::DynamicArray, { enum_descriptor() });
    auto associative = aggregate(
        SystemVerilogVpiDescriptorKind::AssociativeArray,
        { string_descriptor(), queue });
    auto variant = aggregate(
        SystemVerilogVpiDescriptorKind::Union,
        { scalar(64), dynamic_array },
        { "word", "states" });
    auto class_handle = aggregate(
        SystemVerilogVpiDescriptorKind::ClassHandle, { });
    class_handle.nominal_name = "packet";
    class_handle.width = 64;
    auto class_descriptor = aggregate(
        SystemVerilogVpiDescriptorKind::Class,
        { record, associative, variant, class_handle },
        { "header", "by_name", "body", "next" });
    class_descriptor.nominal_name = "packet";
    const auto class_layout = validate_systemverilog_vpi_descriptor(class_descriptor);
    require_vpi_descriptor(
        class_layout && class_layout.value.node_count == 19
            && class_layout.value.maximum_depth == 6
            && class_layout.value.dynamic,
        "VPI descriptors recursively retain queues, associative arrays, unions, class properties, and class handles");

    SystemVerilogVpiObjectRegistry registry { 601 };
    const auto root = registry.create(SystemVerilogVpiObjectKind::Root, 0, "top");
    const auto memory_object = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Memory,
        root.value,
        "words",
        std::nullopt,
        descriptor_type(memory),
    });
    const auto memory_word = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Variable,
        memory_object.value,
        "[3]",
        std::nullopt,
        descriptor_type(scalar(16)),
    });
    const auto queue_object = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Array,
        root.value,
        "pending",
        std::nullopt,
        descriptor_type(queue),
    });
    const auto string_object = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Variable,
        root.value,
        "message",
        std::nullopt,
        descriptor_type(string_descriptor()),
    });
    const auto class_object = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Class,
        root.value,
        "packet_type",
        std::nullopt,
        descriptor_type(class_descriptor),
    });
    const auto property_object = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::ClassProperty,
        class_object.value,
        "next_packet",
        std::nullopt,
        descriptor_type(class_handle),
    });
    require_vpi_descriptor(
        root && memory_object && memory_word && queue_object && string_object
            && class_object && property_object,
        "VPI registry accepts memory, dynamic-array, string, class, and class-property descriptors");
    require_vpi_descriptor(
        registry.find("top.words[3]")
            && registry.lookup(memory_word.value).value->full_name
                == "top.words[3]",
        "VPI memory words use canonical bracket selectors without a hierarchy separator");
    const auto class_type = registry.type_info(class_object.value);
    const auto property_type = registry.type_info(property_object.value);
    require_vpi_descriptor(
        class_type && class_type.value->descriptor
            && class_type.value->descriptor->nominal_name == "packet"
            && class_type.value->descriptor->member_names.at(1) == "by_name"
            && property_type && property_type.value->descriptor
            && property_type.value->descriptor->kind
                == SystemVerilogVpiDescriptorKind::ClassHandle,
        "VPI queries expose semantic recursive descriptors without host layout");

    auto mutable_descriptor = std::make_shared<SystemVerilogVpiTypeDescriptor>(enum_descriptor());
    auto mutable_type = descriptor_type(*mutable_descriptor);
    mutable_type.descriptor = mutable_descriptor;
    const auto snapshot_object = registry.create(SystemVerilogVpiObjectDescriptor {
        SystemVerilogVpiObjectKind::Variable,
        root.value,
        "snapshot",
        std::nullopt,
        mutable_type,
    });
    mutable_descriptor->enum_literals.front().name = "Corrupted";
    mutable_descriptor->width = 1;
    const auto snapshot = registry.type_info(snapshot_object.value);
    require_vpi_descriptor(
        snapshot && snapshot.value->descriptor
            && snapshot.value->descriptor->width == 2
            && snapshot.value->descriptor->enum_literals.front().name
                == "Idle",
        "VPI registry owns an immutable descriptor snapshot");

    auto duplicate_member = record;
    duplicate_member.member_names = { "field", "field", "other" };
    auto duplicate_enum = enum_descriptor();
    duplicate_enum.enum_literals.back().value
        = fsim::runtime::PackedLogic4::from_msb_string("01");
    auto wrong_width_enum = wide_enum;
    wrong_width_enum.enum_literals.back().value
        = fsim::runtime::PackedLogic4::from_msb_string("1");
    auto unknown_two_state_enum = wide_enum;
    unknown_two_state_enum.category = SystemVerilogVpiValueCategory::Bit2;
    auto real_enum = wide_enum;
    real_enum.category = SystemVerilogVpiValueCategory::Real;
    auto invalid_associative = associative;
    invalid_associative.children.front() = record;
    auto invalid_packed = packed;
    invalid_packed.children = { string_descriptor() };
    auto invalid_range = aggregate(
        SystemVerilogVpiDescriptorKind::UnpackedArray, { scalar() });
    invalid_range.ranges = {
        { std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max() }
    };
    require_vpi_descriptor(
        validate_systemverilog_vpi_descriptor(duplicate_member).error
                == SystemVerilogVpiDescriptorError::InvalidName
            && validate_systemverilog_vpi_descriptor(duplicate_enum).error
                == SystemVerilogVpiDescriptorError::InvalidEnum
            && validate_systemverilog_vpi_descriptor(wrong_width_enum).error
                == SystemVerilogVpiDescriptorError::InvalidEnum
            && validate_systemverilog_vpi_descriptor(unknown_two_state_enum).error
                == SystemVerilogVpiDescriptorError::InvalidEnum
            && validate_systemverilog_vpi_descriptor(real_enum).error
                == SystemVerilogVpiDescriptorError::InvalidEnum
            && validate_systemverilog_vpi_descriptor(invalid_associative).error
                == SystemVerilogVpiDescriptorError::InvalidShape
            && validate_systemverilog_vpi_descriptor(invalid_packed).error
                == SystemVerilogVpiDescriptorError::InvalidShape
            && validate_systemverilog_vpi_descriptor(invalid_range).error
                == SystemVerilogVpiDescriptorError::ArithmeticOverflow,
        "VPI descriptors reject duplicate or ill-typed enum identity, composite keys, dynamic packed elements, and overflowing ranges");

    auto too_deep = scalar();
    for (std::size_t depth = 0; depth < 65; ++depth) {
        too_deep = aggregate(
            SystemVerilogVpiDescriptorKind::DynamicArray,
            { std::move(too_deep) });
    }
    require_vpi_descriptor(
        validate_systemverilog_vpi_descriptor(too_deep).error
                == SystemVerilogVpiDescriptorError::DepthLimit
            && registry.create(SystemVerilogVpiObjectDescriptor {
                                   SystemVerilogVpiObjectKind::Memory,
                                   root.value,
                                   "bad_memory",
                                   std::nullopt,
                                   descriptor_type(queue) })
                    .error
                == SystemVerilogVpiObjectError::InvalidType,
        "VPI descriptors reject excessive recursion and object-kind mismatches");

    auto legacy_queue_type = descriptor_type(queue);
    legacy_queue_type.language = SystemVerilogVpiLanguage::Verilog2005;
    require_vpi_descriptor(
        registry.create(SystemVerilogVpiObjectDescriptor {
                            SystemVerilogVpiObjectKind::Array,
                            root.value,
                            "legacy_queue",
                            std::nullopt,
                            legacy_queue_type })
                .error
            == SystemVerilogVpiObjectError::InvalidType,
        "VPI descriptors reject SystemVerilog-only dynamic shapes in Verilog ownership");
}

} // namespace fsim::tests::runtime
