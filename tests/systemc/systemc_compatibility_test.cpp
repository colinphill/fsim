// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc.hpp"

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
static_assert(FSIM_SYSTEMC_ABI_VERSION == 3u);
static_assert(fsim::systemc::accellera_version == "3.0.2");
static_assert(fsim::systemc::accellera_bridge_revision == 2u);
static_assert(
    fsim::systemc::accellera_source_sha256
    == "9b3693ed286aab958b9e5d79bb0ad3bc523bbc46931100553275352038f4a0c4");

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
