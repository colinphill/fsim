// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_smart_ptr.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    using namespace fsim::systemc;

    const auto baseline = ScvNativeSmartPtrRegistry::live_native_payloads();
    {
        ScvNativeSmartPtrRegistry registry;
        fsim::diagnostic::Engine diagnostics;
        const ScvObjectId scalar_id { 1U, 11U };
        const ScvObjectId aggregate_id { 2U, 22U };
        const auto scalar = registry.create(ScvNativeValueKind::scalar,
            scalar_id, "top.scalar", 101U, diagnostics);
        const auto aggregate = registry.create(ScvNativeValueKind::aggregate,
            aggregate_id, "top.aggregate", 202U, diagnostics);
        assert(scalar && aggregate && !diagnostics.has_error());
        assert(registry.live_handles() == 2U);
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline + 2U);

        const auto scalar_info = registry.info(*scalar, diagnostics);
        const auto aggregate_info = registry.info(*aggregate, diagnostics);
        assert(scalar_info && scalar_info->object == scalar_id);
        assert(scalar_info->kind == ScvNativeValueKind::scalar);
        assert(scalar_info->name == "top.scalar");
        assert(aggregate_info && aggregate_info->object == aggregate_id);

        const auto root = registry.extension(*aggregate, { }, diagnostics);
        assert(root && root->kind == ScvNativeExtensionKind::record);
        assert(root->children == 3U && root->randomization_enabled);

        const std::array scalar_path {
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::field, 0U }
        };
        const std::array nested_path {
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::field, 1U }
        };
        const std::array nested_flag_path {
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::field, 1U },
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::field, 0U }
        };
        const std::array array_path {
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::field, 2U }
        };
        const std::array array_element_path {
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::field, 2U },
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::element, 2U }
        };
        const auto nested = registry.extension(
            *aggregate, nested_path, diagnostics);
        const auto array = registry.extension(*aggregate, array_path, diagnostics);
        assert(nested && nested->kind == ScvNativeExtensionKind::record
            && nested->children == 2U);
        assert(array && array->kind == ScvNativeExtensionKind::array
            && array->children == 4U);

        assert(registry.assign_signed(
            *aggregate, scalar_path, -91, diagnostics));
        assert(registry.assign_signed(
            *aggregate, nested_flag_path, 17, diagnostics));
        assert(registry.assign_signed(
            *aggregate, array_element_path, 44, diagnostics));
        assert(registry.extension(*aggregate, scalar_path, diagnostics)
                   ->signed_value
            == -91);
        assert(registry.extension(*aggregate, nested_flag_path, diagnostics)
                   ->signed_value
            == 17);
        assert(registry.extension(*aggregate, array_element_path, diagnostics)
                   ->signed_value
            == 44);

        const auto alias = registry.copy(*aggregate, diagnostics);
        assert(alias && registry.live_handles() == 3U);
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline + 2U);
        assert(registry.assign_signed(*alias, scalar_path, 733, diagnostics));
        assert(registry.extension(*aggregate, scalar_path, diagnostics)
                   ->signed_value
            == 733);

        const auto destination = registry.create(ScvNativeValueKind::aggregate,
            { 3U, 33U }, "top.destination", 303U, diagnostics);
        assert(destination);
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline + 3U);
        assert(registry.assign(*destination, *aggregate, diagnostics));
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline + 2U);
        assert(registry.info(*destination, diagnostics)->object == aggregate_id);
        assert(registry.extension(*destination, scalar_path, diagnostics)
                   ->signed_value
            == 733);

        fsim::diagnostic::Engine mismatch_diagnostics;
        assert(!registry.assign(*scalar, *aggregate, mismatch_diagnostics));
        assert(has_code(mismatch_diagnostics, "FSIM-SCV-P002"));
        assert(registry.info(*scalar, diagnostics)->object == scalar_id);

        assert(registry.set_randomization(
            *aggregate, array_element_path, false, diagnostics));
        const auto disabled = registry.extension(
            *aggregate, array_element_path, diagnostics);
        assert(disabled && !disabled->randomization_enabled);
        const auto retained = disabled->signed_value;
        assert(registry.randomize(*aggregate, { }, diagnostics));
        assert(registry.extension(*aggregate, array_element_path, diagnostics)
                   ->signed_value
            == retained);
        assert(registry.set_randomization(
            *aggregate, array_element_path, true, diagnostics));
        assert(registry.extension(*aggregate, array_element_path, diagnostics)
                ->randomization_enabled);

        const std::array invalid_path {
            ScvNativeExtensionStep { ScvNativeExtensionStepKind::element, 0U }
        };
        fsim::diagnostic::Engine path_diagnostics;
        assert(!registry.extension(*aggregate, invalid_path, path_diagnostics));
        assert(has_code(path_diagnostics, "FSIM-SCV-P002"));

        assert(registry.release(*aggregate, diagnostics));
        assert(registry.live_handles() == 3U);
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline + 2U);
        fsim::diagnostic::Engine stale_diagnostics;
        assert(!registry.info(*aggregate, stale_diagnostics));
        assert(has_code(stale_diagnostics, "FSIM-SCV-P001"));
        assert(registry.release(*alias, diagnostics));
        assert(registry.release(*destination, diagnostics));
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline + 1U);
        assert(registry.release(*scalar, diagnostics));
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline);
    }
    assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline);

    {
        ScvNativeSmartPtrRegistry invalid_limits { { 0U, 64U, 4U } };
        fsim::diagnostic::Engine diagnostics;
        assert(!invalid_limits.create(ScvNativeValueKind::scalar, { 6U, 66U },
            "invalid.limits", 1U, diagnostics));
        assert(has_code(diagnostics, "FSIM-SCV-P003"));
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline);
    }

    {
        ScvNativeSmartPtrRegistry limited { { 1U, 64U, 4U } };
        fsim::diagnostic::Engine diagnostics;
        assert(limited.create(ScvNativeValueKind::aggregate, { 4U, 44U },
            "limited.first", 1U, diagnostics));
        const auto before_failure = ScvNativeSmartPtrRegistry::live_native_payloads();
        fsim::diagnostic::Engine resource_diagnostics;
        assert(!limited.create(ScvNativeValueKind::aggregate, { 5U, 55U },
            "limited.second", 2U, resource_diagnostics));
        assert(has_code(resource_diagnostics, "FSIM-SCV-P003"));
        assert(ScvNativeSmartPtrRegistry::live_native_payloads() == before_failure);
    }
    assert(ScvNativeSmartPtrRegistry::live_native_payloads() == baseline);
}
