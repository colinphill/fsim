// SPDX-License-Identifier: Apache-2.0
#pragma once

// The foreign interfaces' share of a run-state file (#128; IEEE 1800-2023
// 38.9 vpi_get_data, 38.31 vpi_put_data, 38.36.3; IEEE 1076-2019 20.7, 20.8,
// 23.12, 23.27; IEEE 1364-2001 25.41 tf_read_restart, 25.65 tf_write_save).
//
// Two optional sections, "foreign.vpi" and "foreign.vhpi" (one per interface), hold what the libraries put during the save
// (the data sets by save/restart id), the highest id issued, and the
// registrations of the restart callbacks: a library registers its
// cbStartOfRestart / vhpiCbStartOfRestart callback while saving (IEEE
// 1076-2019 21.3.8.3), so a fresh process that loads the libraries again has
// no such callback until the file restores it (20.7: "the tool shall save
// registration of such callbacks and restore the registration"). A routine is
// saved as the module that holds it and its offset in the module, and a
// user_data value is saved as the bytes of the pointer (IEEE 1800-2023 38.36.3
// NOTE: it should be an identifier, not memory).
#include "fsim/runtime/run_state_sections.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {

class VpiForeignService;
class VhpiForeignService;

/// One restart-callback registration.
struct ForeignCallbackRecord {
    std::int32_t reason { };
    /// The module holding the routine and the routine's offset in it.
    std::string module;
    std::uint64_t offset { };
    std::uint64_t user_data { };
    /// VHPI: vhpiEnable or vhpiDisable; unused by VPI.
    std::int32_t state { };
};

/// The value a user function call returned at its last evaluation, by the
/// call's position (VPI vpi_get_value on a vpiFuncCall, IEEE 1800-2023 38.15,
/// 37.42): a restart keeps it until the call runs again.
struct ForeignCallResult {
    std::uint32_t line { };
    std::uint32_t column { };
    std::string file;
    /// True for a real result; `real_bits` then holds its IEEE 754 bits.
    bool real_value { };
    std::uint64_t real_bits { };
    runtime::PackedLogic4 bits;
    bool is_signed { };
};

/// What one interface contributes to the file.
struct ForeignRunState {
    std::map<std::int32_t, std::string> data;
    std::int32_t last_id { };
    std::vector<ForeignCallbackRecord> callbacks;
    /// VPI only: the results of user function calls (--foreign-probes).
    std::vector<ForeignCallResult> call_results;
};

/// The module and offset of a loaded routine; nullopt when it is not in a
/// shared module (or the platform cannot say).
[[nodiscard]] std::optional<std::pair<std::string, std::uint64_t>>
encode_callback_routine(const void* routine);
/// The routine at `offset` of the already loaded `module`; null when the
/// module is not loaded.
[[nodiscard]] void* decode_callback_routine(
    const std::string& module, std::uint64_t offset);

/// Registers the sections "foreign.vpi", "foreign.vhpi" for the services that
/// exist (a null pointer is skipped). The sections are optional: a run without
/// that interface neither writes nor needs one. The driver saves these after
/// the end-of-save callbacks ran (so that vpi_put_data / vhpi_put_data calls
/// made from them are in the file) and restores them before the
/// start-of-restart callbacks fire (so that vpi_get_data / vhpi_get_data see
/// the data and the restart callbacks exist).
void register_foreign_run_state_sections(
    runtime::RunStateSectionRegistry& registry, VpiForeignService* vpi,
    VhpiForeignService* vhpi);

}  // namespace fsim::app::application_detail
