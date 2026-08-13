// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec.tpp"

namespace fsim::app::codec_detail {

using SelectedOperationGroup = FSIM_DESIGN_ARTIFACT_OPERATION_GROUP;

void write_operation_group(
    Writer& writer,
    const SelectedOperationGroup& group)
{
    writer.write(group.storage);
}

bool read_operation_group(
    Reader& reader,
    SelectedOperationGroup& group)
{
    return reader.read(group.storage);
}

} // namespace fsim::app::codec_detail
