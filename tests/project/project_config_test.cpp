// SPDX-License-Identifier: Apache-2.0
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/configuration.hpp"
#include "fsim/project/project.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

const fsim::project::ConfigOrigin kScript {
    fsim::project::ConfigLayer::workspace, "fsim.tcl", 1U };

// The error of setting one value, or nothing when it is accepted.
std::optional<std::string> set_value(fsim::project::Configuration& config,
    const std::string_view key, const std::string& value)
{
    return config.set(key, { value }, kScript);
}

bool rejected_with(fsim::project::Configuration& config,
    const std::string_view key, const std::string& value,
    const std::string_view text)
{
    // A rejected value leaves the configuration unchanged.
    const auto* before = config.find(key);
    const auto previous = before == nullptr
        ? std::optional<std::vector<std::string>> {}
        : std::optional { before->values };
    const auto error = set_value(config, key, value);
    const auto* after = config.find(key);
    const auto current = after == nullptr
        ? std::optional<std::vector<std::string>> {}
        : std::optional { after->values };
    return error && error->find(key) != std::string::npos
        && error->find(text) != std::string::npos && current == previous;
}

void test_json_diagnostics_are_escaped()
{
    fsim::diagnostic::Engine diagnostics;
    diagnostics.error("FSIM-TEST", "quoted \"message\"\nnext",
        { "a\\b.sv", { 2, 3, 4 }, { 2, 4, 5 } });
    std::ostringstream output;
    fsim::diagnostic::print_json(output, diagnostics);
    check(output.str().find("quoted \\\"message\\\"\\nnext")
            != std::string::npos,
        "JSON diagnostic message is escaped");
    check(output.str().find("a\\\\b.sv") != std::string::npos,
        "JSON diagnostic path is escaped");
}

void test_key_table()
{
    const auto keys = fsim::project::config_keys();
    check(!keys.empty()
            && std::ranges::adjacent_find(keys,
                   [](const auto& left, const auto& right) {
                       return left.name >= right.name;
                   })
                == keys.end(),
        "configuration keys are sorted and unique");
    const auto* standard = fsim::project::find_config_key("compile.vhdl.standard");
    check(standard != nullptr && standard->default_value == "2008"
            && fsim::project::find_config_key("compile.vhdl") == nullptr,
        "keys are found by exact name");
    check(fsim::project::closest_config_key("simulate.max_delta")
                == std::optional<std::string_view> { "simulate.max_deltas" }
            && !fsim::project::closest_config_key("zzzz"),
        "an unknown key suggests only a close spelling");

    fsim::project::Configuration config;
    const auto unknown = set_value(config, "simulate.max_delta", "10");
    check(unknown
            && unknown->find("unknown configuration key 'simulate.max_delta'")
                != std::string::npos
            && unknown->find("did you mean 'simulate.max_deltas'?")
                != std::string::npos,
        "an unknown key is rejected with the closest key");
    check(config.set("simulate.duration", { "1ns", "2ns" }, kScript)
                .value_or("")
                .find("takes one value")
            != std::string::npos,
        "a scalar key takes one value");
    check(rejected_with(config, "elaborate.generics", "=8", "NAME=VALUE"),
        "a generic needs a name");
    check(!config.set("elaborate.generics", { "W=8", "D=2" }, kScript)
            && config.find("elaborate.generics")->values.size() == 2U,
        "a list key keeps every value");
}

std::string_view kind_name(const fsim::project::ConfigValueKind kind)
{
    switch (kind) {
    case fsim::project::ConfigValueKind::text:
        return "text";
    case fsim::project::ConfigValueKind::integer:
        return "integer";
    case fsim::project::ConfigValueKind::boolean:
        return "boolean";
    case fsim::project::ConfigValueKind::list:
        return "list";
    }
    return "text";
}

// The key table of docs/tcl.md is the registry, row for row.
void test_documented_key_table()
{
    std::ifstream document { std::string { FSIM_TEST_SOURCE_DIR } + "/docs/tcl.md" };
    std::vector<std::string> documented;
    bool inside = false;
    for (std::string line; std::getline(document, line);) {
        if (line == "<!-- fsim-config-keys:end -->") {
            inside = false;
        } else if (inside && line.starts_with("| `")) {
            documented.push_back(line);
        } else if (line == "<!-- fsim-config-keys:begin -->") {
            inside = true;
        }
    }
    std::vector<std::string> expected;
    for (const auto& key : fsim::project::config_keys()) {
        expected.push_back("| `" + std::string { key.name } + "` | "
            + std::string { kind_name(key.kind) } + " | "
            + (key.default_value.empty()
                    ? std::string {}
                    : "`" + std::string { key.default_value } + "`")
            + " | " + std::string { key.summary } + " |");
    }
    check(documented == expected,
        "docs/tcl.md lists every configuration key with its kind, default and summary");
    for (std::size_t index = 0;
         index < std::max(documented.size(), expected.size()); ++index) {
        const auto* left = index < documented.size() ? &documented[index] : nullptr;
        const auto* right = index < expected.size() ? &expected[index] : nullptr;
        if (left == nullptr || right == nullptr || *left != *right) {
            std::cerr << "  documented: " << (left ? *left : "(none)")
                      << "\n  registry:   " << (right ? *right : "(none)")
                      << '\n';
            break;
        }
    }
}

void test_boolean_and_integer_values()
{
    fsim::project::Configuration config;
    for (const auto& [spelling, canonical] : std::array {
             std::pair { "1", "true" }, std::pair { "Yes", "true" },
             std::pair { "on", "true" }, std::pair { "0", "false" },
             std::pair { "NO", "false" }, std::pair { "off", "false" } }) {
        check(!set_value(config, "elaborate.code_coverage", spelling)
                && config.find("elaborate.code_coverage")->values.front()
                    == canonical,
            std::string { "boolean spelling is canonical: " } + spelling);
    }
    check(rejected_with(config, "simulate.trace", "maybe", "not a boolean"),
        "an unknown boolean spelling is rejected");
    check(rejected_with(config, "compile.jobs", "-1", "unsigned integer")
            && !set_value(config, "compile.jobs", "0"),
        "jobs is an unsigned integer where 0 selects the default");
    check(rejected_with(config, "simulate.max_deltas", "0", "positive integer")
            && rejected_with(
                config, "simulate.trace_report_limit", "00", "positive integer")
            && !set_value(config, "simulate.max_deltas", "1")
            && !set_value(config, "simulate.trace_report_limit", "1"),
        "delta and trace report limits must be positive, as on the command line");
    check(!set_value(config, "simulate.seed", "random")
            && !set_value(config, "elaborate.seed", "42")
            && rejected_with(config, "simulate.seed", "-3", "'random'"),
        "a seed is an unsigned integer or random");
}

void test_time_values()
{
    check(!fsim::project::validate_time_resolution("auto")
            && !fsim::project::validate_time_resolution("1ns")
            && !fsim::project::validate_time_resolution("10ps")
            && !fsim::project::validate_time_resolution("1fs"),
        "auto and nonzero resolutions are accepted");
    for (const auto* value : { "0ns", "000ps", "ns", "1", "1 ns", "1min" }) {
        check(fsim::project::validate_time_resolution(value).has_value(),
            std::string { "an invalid time resolution is rejected: " } + value);
    }
    fsim::project::Configuration config;
    check(rejected_with(
              config, "elaborate.time_resolution", "0ns", "nonzero integer"),
        "zero time resolution is rejected");
    check(!set_value(config, "simulate.duration", "10us")
            && rejected_with(config, "simulate.duration", "10", "fs, ps"),
        "a duration needs a unit");
}

void test_delay_mode_values()
{
    check(fsim::project::RunSection {}.delay_mode
            == fsim::project::DelayMode::typical,
        "typical is the deterministic default delay mode");
    check(fsim::project::parse_delay_mode("minimum")
                == fsim::project::DelayMode::minimum
            && fsim::project::parse_delay_mode("TYP")
                == fsim::project::DelayMode::typical
            && fsim::project::parse_delay_mode("maximum")
                == fsim::project::DelayMode::maximum
            && fsim::project::to_string(fsim::project::DelayMode::minimum)
                == "min"
            && fsim::project::to_string(fsim::project::DelayMode::typical)
                == "typ"
            && fsim::project::to_string(fsim::project::DelayMode::maximum)
                == "max",
        "delay-mode aliases parse and serialize canonically");

    fsim::project::Configuration config;
    check(rejected_with(config, "elaborate.delay_mode", "slow", "delay mode")
            && rejected_with(config, "simulate.delay_mode", "slow", "delay mode")
            && !set_value(config, "simulate.delay_mode", "max"),
        "an invalid delay mode has a targeted diagnostic");
}

void test_trace_format_values()
{
    using Format = fsim::project::TraceFormat;
    check(fsim::project::RunSection {}.trace_format == Format::automatic,
        "automatic is the deterministic default trace format");
    check(fsim::project::parse_trace_format("automatic") == Format::automatic
            && fsim::project::parse_trace_format("VCD") == Format::vcd
            && fsim::project::parse_trace_format("fst") == Format::fst
            && fsim::project::to_string(Format::automatic) == "auto"
            && fsim::project::to_string(Format::vcd) == "vcd"
            && fsim::project::to_string(Format::fst) == "fst",
        "trace-format aliases parse and serialize canonically");

    using Compression = fsim::project::TraceCompression;
    check(fsim::project::RunSection {}.trace_compression
                == Compression::automatic
            && fsim::project::parse_trace_compression("automatic")
                == Compression::automatic
            && fsim::project::parse_trace_compression("NONE")
                == Compression::none
            && fsim::project::parse_trace_compression("fixed")
                == Compression::deterministic
            && fsim::project::parse_trace_compression("fst-v1")
                == Compression::deterministic
            && fsim::project::to_string(Compression::automatic) == "auto"
            && fsim::project::to_string(Compression::none) == "none"
            && fsim::project::to_string(Compression::deterministic)
                == "deterministic",
        "trace-compression aliases parse and serialize canonically");

    fsim::project::Configuration config;
    check(rejected_with(config, "simulate.trace_format", "wave", "trace format"),
        "an invalid trace format has a targeted diagnostic");
    check(rejected_with(
              config, "simulate.trace_compression", "host", "trace compression"),
        "an invalid trace compression has a targeted diagnostic");
    check(rejected_with(config, "simulate.trace_filters", "", "must not be empty"),
        "an empty trace selection is rejected");
}

void test_uvm_release_values()
{
    using Release = fsim::project::SystemVerilogUvmRelease;
    check(fsim::project::parse_systemverilog_uvm_release("uvm-1.2")
                == Release::uvm_1_2
            && fsim::project::parse_systemverilog_uvm_release("2020.3.1")
                == Release::ieee_1800_2_2020_3_1
            && fsim::project::to_string(Release::uvm_1_2) == "1.2"
            && fsim::project::to_string(Release::ieee_1800_2_2020_3_1)
                == "2020.3.1",
        "UVM release aliases normalize to two canonical governed values");

    fsim::project::Configuration config;
    check(!set_value(config, "compile.sv.uvm_release", "1.2")
            && rejected_with(
                config, "compile.sv.uvm_release", "1.1d", "unknown UVM release"),
        "only governed UVM releases are accepted");
    check(set_value(config, "compile.vhdl.uvm_release", "1.2").has_value(),
        "a UVM release is a SystemVerilog setting only");
}

void test_vhdl_standard_values()
{
    using fsim::project::VhdlStandard;
    const std::array aliases {
        std::tuple { "87", "1987", VhdlStandard::vhdl_1987 },
        std::tuple { "1987", "1987", VhdlStandard::vhdl_1987 },
        std::tuple { "vhdl-87", "1987", VhdlStandard::vhdl_1987 },
        std::tuple { "93", "1993", VhdlStandard::vhdl_1993 },
        std::tuple { "1993", "1993", VhdlStandard::vhdl_1993 },
        std::tuple { "vhdl-1993", "1993", VhdlStandard::vhdl_1993 },
        std::tuple { "00", "2000", VhdlStandard::vhdl_2000 },
        std::tuple { "2000", "2000", VhdlStandard::vhdl_2000 },
        std::tuple { "02", "2002", VhdlStandard::vhdl_2002 },
        std::tuple { "2002", "2002", VhdlStandard::vhdl_2002 },
        std::tuple { "08", "2008", VhdlStandard::vhdl_2008 },
        std::tuple { "2008", "2008", VhdlStandard::vhdl_2008 },
        std::tuple { "19", "2019", VhdlStandard::vhdl_2019 },
        std::tuple { "2019", "2019", VhdlStandard::vhdl_2019 },
        std::tuple { "vhdl-19", "2019", VhdlStandard::vhdl_2019 },
        std::tuple { "vhdl-2019", "2019", VhdlStandard::vhdl_2019 },
    };
    for (const auto& [spelling, canonical, identity] : aliases) {
        fsim::project::Configuration config;
        check(!set_value(config, "compile.vhdl.standard", spelling)
                && fsim::project::canonical_standard(
                       fsim::project::Language::vhdl, spelling)
                    == std::optional<std::string_view> { canonical },
            std::string { "VHDL standard alias is canonicalized: " } + spelling);
        check(fsim::project::parse_vhdl_standard(spelling) == identity
                && fsim::project::to_string(identity) == canonical,
            std::string { "VHDL standard identity round-trips: " } + spelling);
    }
    check(fsim::project::parse_language("VHDL-93")
                == fsim::project::Language::vhdl
            && fsim::project::parse_language("VHDL-2019")
                == fsim::project::Language::vhdl
            && fsim::project::default_standard(fsim::project::Language::vhdl)
                == "2008",
        "explicit VHDL language profiles retain the VHDL-2008 default");

    fsim::project::Configuration config;
    check(rejected_with(config, "compile.vhdl.standard", "1988",
              "unsupported standard '1988' for vhdl"),
        "VHDL standard-mode rejection is targeted and deterministic");
}

void test_verilog_systemverilog_standard_values()
{
    using fsim::project::SystemVerilogStandard;
    using fsim::project::VerilogStandard;
    const std::array verilog_aliases {
        std::tuple { "95", "1995", VerilogStandard::verilog_1995 },
        std::tuple { "verilog-1995", "1995", VerilogStandard::verilog_1995 },
        std::tuple { "01", "2001", VerilogStandard::verilog_2001 },
        std::tuple { "v2001", "2001", VerilogStandard::verilog_2001 },
        std::tuple { "2001-noconfig", "2001-noconfig",
            VerilogStandard::verilog_2001_noconfig },
        std::tuple { "verilog-2001-noconfig", "2001-noconfig",
            VerilogStandard::verilog_2001_noconfig },
        std::tuple { "05", "2005", VerilogStandard::verilog_2005 },
        std::tuple { "verilog-2005", "2005", VerilogStandard::verilog_2005 },
    };
    for (const auto& [spelling, canonical, identity] : verilog_aliases) {
        fsim::project::Configuration config;
        check(!set_value(config, "compile.verilog.standard", spelling)
                && fsim::project::canonical_standard(
                       fsim::project::Language::verilog, spelling)
                    == std::optional<std::string_view> { canonical },
            std::string { "Verilog standard alias is canonicalized: " }
                + spelling);
        check(fsim::project::parse_verilog_standard(spelling) == identity
                && fsim::project::to_string(identity) == canonical,
            std::string { "Verilog standard identity round-trips: " }
                + spelling);
    }

    const std::array systemverilog_aliases {
        std::tuple { "05", "2005",
            SystemVerilogStandard::systemverilog_2005 },
        std::tuple { "systemverilog-2005", "2005",
            SystemVerilogStandard::systemverilog_2005 },
        std::tuple { "09", "2009",
            SystemVerilogStandard::systemverilog_2009 },
        std::tuple { "sv-2009", "2009",
            SystemVerilogStandard::systemverilog_2009 },
        std::tuple { "12", "2012",
            SystemVerilogStandard::systemverilog_2012 },
        std::tuple { "systemverilog-2012", "2012",
            SystemVerilogStandard::systemverilog_2012 },
        std::tuple { "17", "2017",
            SystemVerilogStandard::systemverilog_2017 },
        std::tuple { "sv-2017", "2017",
            SystemVerilogStandard::systemverilog_2017 },
        std::tuple { "23", "2023",
            SystemVerilogStandard::systemverilog_2023 },
        std::tuple { "systemverilog-2023", "2023",
            SystemVerilogStandard::systemverilog_2023 },
    };
    for (const auto& [spelling, canonical, identity] : systemverilog_aliases) {
        fsim::project::Configuration config;
        check(!set_value(config, "compile.sv.standard", spelling)
                && fsim::project::canonical_standard(
                       fsim::project::Language::system_verilog, spelling)
                    == std::optional<std::string_view> { canonical },
            std::string { "SystemVerilog standard alias is canonicalized: " }
                + spelling);
        check(fsim::project::parse_systemverilog_standard(spelling) == identity
                && fsim::project::to_string(identity) == canonical,
            std::string { "SystemVerilog standard identity round-trips: " }
                + spelling);
    }

    check(fsim::project::parse_language("Verilog-2001-noconfig")
                == fsim::project::Language::verilog
            && fsim::project::parse_language("SV-2009")
                == fsim::project::Language::system_verilog
            && fsim::project::default_standard(
                   fsim::project::Language::verilog)
                == "2005"
            && fsim::project::default_standard(
                   fsim::project::Language::system_verilog)
                == "2017",
        "explicit language profiles retain the Verilog-2005 and SystemVerilog-2017 defaults");
    check(!fsim::project::canonical_standard(
              fsim::project::Language::verilog, "sv-2009")
            && !fsim::project::canonical_standard(
                fsim::project::Language::system_verilog, "verilog-1995")
            && !fsim::project::parse_verilog_standard("1996")
            && !fsim::project::parse_systemverilog_standard("2018"),
        "cross-family and unknown Verilog/SystemVerilog standards are rejected");

    const std::array rejected {
        std::tuple { "compile.verilog.standard", "sv-2009",
            "unsupported standard 'sv-2009' for verilog" },
        std::tuple { "compile.sv.standard", "2018",
            "unsupported standard '2018' for systemverilog" },
    };
    for (const auto& [key, standard, message] : rejected) {
        fsim::project::Configuration config;
        check(rejected_with(config, key, standard, message),
            std::string { "standard rejection is targeted: " } + key + '/'
                + standard);
    }
}

void test_verilog_systemverilog_compatibility_selection()
{
    check(fsim::project::compatibility_profile(
              { "sizing", "implicit-net", "keyword-profile" })
            == "keyword-profile,implicit-net,sizing",
        "compatibility switches canonicalize independently of input order");
    check(fsim::project::parse_compatibility_switch("scheduler_assertion")
                == std::optional<std::string_view> { "scheduler-assertion" }
            && !fsim::project::parse_compatibility_switch("future-switch"),
        "compatibility switch aliases round-trip and unknown switches are rejected");

    fsim::project::Configuration config;
    check(!config.set("compile.sv.compatibility",
              { "sizing", "implicit_net", "keyword-profile" }, kScript)
            && rejected_with(config, "compile.verilog.compatibility",
                "future-switch", "unknown compatibility switch"),
        "Verilog and SystemVerilog compatibility switches are validated");
    check(rejected_with(config, "compile.vhdl.compatibility", "sizing",
              "unknown VHDL compatibility profile"),
        "Verilog compatibility switches are not VHDL compatibility profiles");
}

void test_vhdl_compatibility_selection()
{
    const auto* key = fsim::project::find_config_key("compile.vhdl.compatibility");
    check(key != nullptr && key->default_value.empty(),
        "VHDL compatibility defaults to strict mode");
    check(fsim::project::parse_vhdl_compatibility(
              "legacy-unprotected-shared-variable")
            == std::optional<std::string_view> {
                "legacy-unprotected-shared-variable" },
        "the named VHDL compatibility mode is recognized");

    fsim::project::Configuration config;
    check(rejected_with(config, "compile.vhdl.compatibility", "future-mode",
              "unknown VHDL compatibility profile 'future-mode'"),
        "unknown VHDL compatibility modes are rejected");
    check(!set_value(config, "compile.vhdl.compatibility",
              "legacy-unprotected-shared-variable")
            && config.find("compile", fsim::project::Language::vhdl,
                   "compatibility")
                != nullptr,
        "the named VHDL compatibility mode is configured for VHDL");
    check(config.find("compile", fsim::project::Language::system_verilog,
              "compatibility")
            == nullptr,
        "the VHDL compatibility mode does not apply to SystemVerilog");
}

void test_layers_and_language_scope()
{
    fsim::project::Configuration config;
    check(!config.set("compile.defines", { "COMMON" },
              { fsim::project::ConfigLayer::user, "user.tcl", 3U })
            && !config.set("compile.sv.defines", { "SV_ONLY" }, kScript),
        "defines are accepted at every scope");
    const auto* vhdl = config.find(
        "compile", fsim::project::Language::vhdl, "defines");
    const auto* sv = config.find(
        "compile", fsim::project::Language::system_verilog, "defines");
    check(vhdl != nullptr && vhdl->values.front() == "COMMON"
            && fsim::project::describe(vhdl->origin) == "user.tcl:3"
            && sv != nullptr && sv->values.front() == "SV_ONLY",
        "a language-scoped key wins over the command key");

    check(!config.set("simulate.seed", { "2" },
              { fsim::project::ConfigLayer::session, {}, 0U })
            && !set_value(config, "simulate.seed", "3")
            && config.find("simulate.seed")->values.front() == "2"
            && fsim::project::describe(config.find("simulate.seed")->origin)
                == "interactive",
        "the session layer wins over the workspace script");
    check(config.unset("simulate.seed", fsim::project::ConfigLayer::session)
            && config.find("simulate.seed")->values.front() == "3"
            && !config.unset("simulate.seed", fsim::project::ConfigLayer::user),
        "unsetting a layer exposes the layer below");
}

void test_keyed_entries_and_record()
{
    fsim::project::Configuration config;
    fsim::project::ConfigChange change {};
    check(!config.put_fsm_hint({ "root", "state", "next_state",
                                   std::vector<std::string> { "idle", "run" } },
              kScript, change)
            && change == fsim::project::ConfigChange::added
            && !config.put_fsm_hint({ "other", "state", {}, {} }, kScript, change),
        "FSM hints are accepted with and without optional fields");
    check(config.put_fsm_hint({ "root", "", {}, {} }, kScript, change)
              .value_or("")
              .find("current state")
            != std::string::npos,
        "an FSM hint must name its current state");
    check(!config.put_fsm_hint({ "root", "state", {}, {} }, kScript, change)
            && change == fsim::project::ConfigChange::replaced
            && config.fsm_hints().size() == 2U,
        "an FSM hint is keyed by instance and current state");
    check(fsim::project::validate_binding({ "top.u0", "rtl:work.cell", {} })
                .has_value()
            && fsim::project::validate_binding({ "top.u0", {}, "wired" })
                .has_value()
            && !fsim::project::validate_binding(
                { "top.u0", "sv:work.cell", "sv_wire" }),
        "binding targets and resolvers are validated");
    check(!config.put_binding({ "top.u0", "sv:work.cell", {} }, kScript, change),
        "a valid binding is accepted");
    check(!set_value(config, "elaborate.time_resolution", "1ps")
            && !set_value(config, "simulate.seed", "5"),
        "elaboration and simulation settings are accepted");

    fsim::project::Config elaboration;
    config.apply_elaboration_settings(elaboration);
    check(elaboration.project.time_resolution == "1ps"
            && elaboration.coverage.fsm_hints.size() == 2U
            && elaboration.bindings.size() == 1U,
        "script-only settings reach the elaboration configuration");
    const auto& record = elaboration.recorded_settings;
    check(std::ranges::is_sorted(record, {},
              &fsim::project::RecordedSetting::key)
            && std::ranges::none_of(record,
                [](const auto& setting) {
                    return setting.key.starts_with("simulate.");
                })
            && std::ranges::any_of(record,
                [](const auto& setting) {
                    return setting.key == "elaborate.time_resolution"
                        && setting.value == "1ps"
                        && setting.origin == "fsim.tcl:1";
                })
            && std::ranges::any_of(record,
                [](const auto& setting) {
                    return setting.key == "bind[top.u0]"
                        && setting.value == "target=sv:work.cell";
                }),
        "a snapshot records elaboration settings and keyed entries with origins");

    auto changed = record;
    std::ranges::find(changed, "elaborate.time_resolution",
        &fsim::project::RecordedSetting::key)
        ->value
        = "1ns";
    changed.push_back({ "elaborate.seed", "9", "fsim.tcl:2" });
    check(fsim::project::differing_settings(record, changed)
            == std::vector<std::string> { "elaborate.seed",
                "elaborate.time_resolution" },
        "changed and added settings are reported in key order");
}

} // namespace

int main()
{
    // FSIM-CONFORMANCE CF-COMMON-PROJECT-001 source=SRC-FSIM expectation=accept
    // FSIM-CONFORMANCE CF-COMMON-DIAGNOSTIC-N01 source=SRC-FSIM expectation=reject
    test_json_diagnostics_are_escaped();
    test_key_table();
    test_documented_key_table();
    test_boolean_and_integer_values();
    test_time_values();
    test_delay_mode_values();
    test_trace_format_values();
    test_uvm_release_values();
    test_vhdl_standard_values();
    test_verilog_systemverilog_standard_values();
    test_verilog_systemverilog_compatibility_selection();
    test_vhdl_compatibility_selection();
    test_layers_and_language_scope();
    test_keyed_entries_and_record();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "project_config_test: all tests passed\n";
    return 0;
}
