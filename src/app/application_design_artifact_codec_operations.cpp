// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec.tpp"

namespace fsim::app::codec_detail {
namespace {

    template <typename Group>
    bool select_operation_group(
        Reader& reader,
        runtime::simir::Operation& operation)
    {
        return read_operation_group(
            reader,
            runtime::simir::operation_emplace_group<Group>(operation));
    }

} // namespace

void write_operation(
    Writer& writer,
    const runtime::simir::Operation& operation)
{
    writer.u64(runtime::simir::operation_group_index(operation));
    runtime::simir::visit_operation(
        [&](const auto& value) {
            using Group = runtime::simir::OperationGroupFor<decltype(value)>;
            const auto* group
                = runtime::simir::operation_group_if<Group>(&operation);
            write_operation_group(writer, *group);
        },
        operation);
}

bool read_operation(
    Reader& reader,
    runtime::simir::Operation& operation)
{
    std::uint64_t selected { };
    if (!reader.u64(selected)) {
        return false;
    }
    switch (selected) {
    case 0:
        return select_operation_group<runtime::simir::ValueOperationGroup>(reader, operation);
    case 1:
        return select_operation_group<runtime::simir::SignalOperationGroup>(reader, operation);
    case 2:
        return select_operation_group<runtime::simir::StringOperationGroup>(reader, operation);
    case 3:
        return select_operation_group<runtime::simir::ContainerOperationGroup>(reader, operation);
    case 4:
        return select_operation_group<runtime::simir::FileOperationGroup>(reader, operation);
    case 5:
        return select_operation_group<runtime::simir::SchedulingOperationGroup>(reader, operation);
    case 6:
        return select_operation_group<runtime::simir::ControlOperationGroup>(reader, operation);
    case 7:
        return select_operation_group<runtime::simir::OutputOperationGroup>(reader, operation);
    case 8:
        return select_operation_group<runtime::simir::ClassOperationGroup>(reader, operation);
    default:
        return reader.invalid_variant();
    }
}

} // namespace fsim::app::codec_detail
