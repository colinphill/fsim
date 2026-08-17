// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc.hpp"
#include "fsim/systemc/kernel_backend_protocol.hpp"
#include "fsim/systemc/scv.hpp"
#include "fsim/systemc/scv_backend_protocol.hpp"

#include "fsim/runtime/transaction_record.hpp"

#include <tlm>

#include <cassert>
#include <string>
#include <string_view>
#include <type_traits>

// FSIM-CONFORMANCE CF-SC-ACCELLERA-001 source=SRC-SYSTEMC expectation=accept
#if !defined(SC_VERSION_MAJOR) || !defined(SC_VERSION_MINOR) \
    || !defined(SC_VERSION_PATCH)
#error "official SystemC version macros are unavailable"
#endif

static_assert(SC_VERSION_MAJOR == 3);
static_assert(SC_VERSION_MINOR == 0);
static_assert(SC_VERSION_PATCH == 2);
static_assert(FSIM_SYSTEMC_ABI_VERSION == 4u);
static_assert(TLM_VERSION_MAJOR == 2);
static_assert(TLM_VERSION_MINOR == 0);
static_assert(TLM_VERSION_PATCH == 6);
static_assert(fsim::systemc::accellera_version == "3.0.2");
static_assert(fsim::systemc::accellera_bridge_revision == 2u);
static_assert(
    fsim::systemc::accellera_source_sha256
    == "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4");
static_assert(fsim::systemc::scv_version == "2.0.1");
static_assert(fsim::systemc::scv_header_version == "2.0.0-20140417");
static_assert(
    fsim::systemc::scv_source_sha256
    == "7bd1c4037f3c108d02f45cae003d112efdb788d469cb029fada247d330ca4881");
static_assert(
    fsim::systemc::scv_patch_sha256
    == "bda0f09d9071884b00423c8e1e7c9f43746ab350b138ae7b01f84766d03941e3");
static_assert(
    fsim::systemc::scv_patched_tree_sha256
    == "760660f1beb27fc7166784f57239bbccbb319b884822dc8623e166ddad2a9a8c");

static_assert(sizeof(fsim_sc_value_view_v1) == 32U);
static_assert(alignof(fsim_sc_value_view_v1) == 8U);
static_assert(sizeof(fsim_sc_host_v1) == 152U);
static_assert(alignof(fsim_sc_host_v1) == 8U);
static_assert(sizeof(fsim_sc_registrar_v1) == 32U);
static_assert(alignof(fsim_sc_registrar_v1) == 8U);
static_assert(std::is_standard_layout_v<fsim_sc_value_view_v1>);
static_assert(std::is_trivially_copyable_v<fsim_sc_value_view_v1>);
static_assert(std::is_standard_layout_v<fsim_sc_host_v1>);
static_assert(std::is_trivially_copyable_v<fsim_sc_host_v1>);
static_assert(std::is_standard_layout_v<fsim_sc_registrar_v1>);
static_assert(std::is_trivially_copyable_v<fsim_sc_registrar_v1>);
static_assert(std::is_same_v<fsim_plugin_init_v1_fn,
    fsim_sc_status_v1 (*)(const fsim_sc_host_v1*, fsim_sc_registrar_v1*)>);
static_assert(std::is_same_v<decltype(&fsim_systemc_accellera_version),
    const char* (*)() noexcept>);
static_assert(
    std::is_same_v<decltype(&fsim_systemc_accellera_runtime_identity),
        const char* (*)() noexcept>);
static_assert(std::is_same_v<decltype(&fsim_systemc_accellera_context),
    const void* (*)() noexcept>);
static_assert(
    std::is_same_v<decltype(&fsim_systemc_accellera_accepts_identity),
        bool (*)(const char*) noexcept>);
static_assert(
    std::is_same_v<decltype(&fsim_systemc_accellera_compatibility_identity),
        const char* (*)() noexcept>);
static_assert(std::is_same_v<
    decltype(&fsim_systemc_accellera_accepts_compatibility_identity),
    bool (*)(const char*) noexcept>);

static_assert(fsim::systemc::kSystemCKernelProtocolVersion == 1U);
static_assert(fsim::systemc::kSystemCKernelMessageHeaderBytes == 128U);
static_assert(fsim::systemc::scv_backend_protocol_version == 1U);
static_assert(fsim::systemc::scv_backend_message_header_bytes == 160U);
static_assert(fsim::runtime::transaction_record_schema_version == 1U);
static_assert(sizeof(fsim::systemc::SystemCIslandId) == 16U);
static_assert(sizeof(fsim::systemc::ScvIslandId) == 16U);
static_assert(sizeof(fsim::runtime::TransactionStableId) == 16U);
static_assert(!std::is_pointer_v<fsim::systemc::SystemCIslandId>);
static_assert(!std::is_pointer_v<fsim::systemc::ScvIslandId>);
static_assert(!std::is_pointer_v<fsim::runtime::TransactionStableId>);
static_assert(std::is_trivially_copyable_v<fsim::systemc::SystemCIslandId>);
static_assert(std::is_trivially_copyable_v<fsim::systemc::ScvIslandId>);
static_assert(
    std::is_trivially_copyable_v<fsim::runtime::TransactionStableId>);

SC_MODULE(CompatibilityProbe)
{
    sc_core::sc_signal<bool> input { "input" };
    bool observed { };

    SC_CTOR(CompatibilityProbe)
    {
        SC_METHOD(observe);
        sensitive << input;
    }

    void observe() { observed = input.read(); }
};

SC_FSIM_EXPORT_AS(CompatibilityProbe, "compatibility_probe");

int main()
{
    static_assert(
        std::is_base_of_v<sc_core::sc_module, CompatibilityProbe>);
    const auto parameters = fsim::systemc::make_factory_parameters(
        fsim::systemc::factory_parameter {
            "WIDTH", FSIM_SC_CONSTRUCTION_POSITIVE, true, 8 });
    assert(parameters.size() == 1);

    CompatibilityProbe probe { "probe" };
    const auto* const context = sc_core::sc_get_curr_simcontext();
    assert(context != nullptr);
    assert(fsim_systemc_accellera_context() == context);
    probe.input.write(true);
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    assert(probe.observed);

    assert(std::string_view { fsim_systemc_accellera_version() }.starts_with(
        "SystemC 3.0.2"));
    const auto runtime_identity = std::string { fsim_systemc_accellera_runtime_identity() };
    const auto compatibility_identity = std::string { fsim_systemc_accellera_compatibility_identity() };
    assert(compatibility_identity.starts_with(runtime_identity));
    assert(compatibility_identity.find(FSIM_SYSTEMC_ACCELERA_SOURCE_SHA256)
        != std::string::npos);
    assert(compatibility_identity.find("|bridge=2|stdlib=")
        != std::string::npos);
    assert(!compatibility_identity.ends_with("unknown-stdlib"));
    assert(fsim_systemc_accellera_accepts_compatibility_identity(
        compatibility_identity.c_str()));
    assert(!fsim_systemc_accellera_accepts_compatibility_identity(nullptr));

    auto wrong_bridge = compatibility_identity;
    const auto bridge = wrong_bridge.find("|bridge=2|");
    assert(bridge != std::string::npos);
    wrong_bridge.replace(bridge, std::string_view { "|bridge=2|" }.size(),
        "|bridge=3|");
    assert(!fsim_systemc_accellera_accepts_compatibility_identity(
        wrong_bridge.c_str()));

    auto wrong_stdlib = compatibility_identity;
    const auto stdlib = wrong_stdlib.find("|stdlib=");
    assert(stdlib != std::string::npos);
    wrong_stdlib.replace(stdlib, std::string::npos, "|stdlib=other");
    assert(!fsim_systemc_accellera_accepts_compatibility_identity(
        wrong_stdlib.c_str()));
}
