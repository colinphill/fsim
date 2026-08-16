// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv.hpp"

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

extern "C" int sc_main(int, char*[])
{
    static_assert(fsim::systemc::scv_version == "2.0.1");
    static_assert(fsim::systemc::scv_header_version == "2.0.0-20140417");
    static_assert(fsim::systemc::scv_adapter_abi_version == 1);
    static_assert(fsim::systemc::scv_plugin_abi_version == 1);
    static_assert(fsim::systemc::scv_artifact_schema_version == 1);
    static_assert(fsim::systemc::scv_cache_schema_version == 1);

    const auto identity = std::string { fsim_scv_compatibility_identity() };
    assert(!identity.empty());
    assert(identity.find("unknown-compiler") == std::string::npos);
    assert(identity.find("unknown-stdlib") == std::string::npos);
    assert(fsim_scv_accepts_compatibility_identity(identity.c_str()));
    assert(std::string_view { fsim_scv_compatibility_diagnostic(
               identity.c_str()) }.empty());
    assert(!fsim_scv_accepts_compatibility_identity(nullptr));
    assert(std::string_view { fsim_scv_compatibility_diagnostic(nullptr) }
               .find("missing") != std::string_view::npos);

    std::vector<std::string> fields;
    std::string_view remaining { identity };
    while (!remaining.empty()) {
        const auto delimiter = remaining.find('|');
        fields.emplace_back(remaining.substr(0, delimiter));
        remaining = delimiter == std::string_view::npos
            ? std::string_view { }
            : remaining.substr(delimiter + 1);
    }
    assert(fields.size() == 16);
    for (std::size_t index = 0; index < fields.size(); ++index) {
        auto changed_fields = fields;
        changed_fields[index].back() = changed_fields[index].back() == 'x'
            ? 'y'
            : 'x';
        std::string changed;
        for (const auto& field : changed_fields) {
            if (!changed.empty()) {
                changed += '|';
            }
            changed += field;
        }
        assert(!fsim_scv_accepts_compatibility_identity(changed.c_str()));
        assert(!std::string_view { fsim_scv_compatibility_diagnostic(
                    changed.c_str()) }.empty());
    }
    auto trailing = identity + "|unexpected=1";
    assert(!fsim_scv_accepts_compatibility_identity(trailing.c_str()));
    assert(std::string_view { fsim_scv_compatibility_diagnostic(
               trailing.c_str()) }.find("trailing") != std::string_view::npos);
    return 0;
}
