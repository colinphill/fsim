// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/svdpi_bridge.h"
#include "fsim/runtime/tf_plugin_abi.h"
#include "native_cache_schema.hpp"

#include <string_view>

static_assert(fsim::project::kSchemaVersion == 3);
static_assert(FSIM_NATIVE_PLUGIN_ABI_VERSION == 3U);
static_assert(FSIM_TF_INTERFACE_ABI_VERSION == 3U);
static_assert(FSIM_TF_REGISTRATION_TABLE_ABI_VERSION == 3U);
static_assert(FSIM_SVDPI_CONTEXT_ABI_VERSION == 3U);
static_assert(FSIM_ACC_STANDARD_QUERY_ABI_VERSION == 3U);
static_assert(fsim::artifact::kObjectFormatVersion == 8);
static_assert(fsim::library::kFormatVersion == 6);
static_assert(fsim::library::kPortableSchemaVersion == 15);
static_assert(fsim::library::kCompiledHirSchemaVersion == 1);
static_assert(fsim::artifact::kDesignFormatVersion == 14);
static_assert(fsim::app::kRuntimeStateSchema == 64);
static_assert(fsim::app::kSemanticStateSchema == 4);
static_assert(fsim::app::kDesignIrStateSchema == 5);
static_assert(fsim::app::kCompiledHirBundleSchema == 1);
static_assert(fsim::app::kSystemVerilogConstraintHirStateSchema == 8);
static_assert(fsim::app::kSystemVerilogCoverageStateSchema == 7);
static_assert(fsim::app::kSystemVerilogUvmStateSchema == 3);
static_assert(fsim::app::kVhdlHirStateSchema == 6);
static_assert(fsim::compiler::llvm_detail::kNativeObjectCacheSchema ==
    std::string_view { "fsim-llvm-native-object-v169" });

int main()
{
    return 0;
}
