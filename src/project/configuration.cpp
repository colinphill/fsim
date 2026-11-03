// SPDX-License-Identifier: Apache-2.0
#include "fsim/project/configuration.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>

namespace fsim::project {
namespace {

using Validator = std::optional<std::string> (*)(std::string_view);

std::string lowercase(const std::string_view value)
{
    std::string result { value };
    std::ranges::transform(result, result.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::optional<std::string> valid_integer(const std::string_view value)
{
    std::uint64_t number {};
    const auto [end, error]
        = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || error != std::errc { }
        || end != value.data() + value.size()) {
        return "'" + std::string { value }
            + "' is not an unsigned integer";
    }
    return std::nullopt;
}

// Runs after the integer check, so only zero remains to reject.
std::optional<std::string> valid_positive(const std::string_view value)
{
    if (value.find_first_not_of('0') != std::string_view::npos) {
        return std::nullopt;
    }
    return "the value must be a positive integer";
}

template <Language language>
std::optional<std::string> valid_standard(const std::string_view value)
{
    if (canonical_standard(language, value)) {
        return std::nullopt;
    }
    return "unsupported standard '" + std::string { value } + "' for "
        + std::string { to_string(language) };
}

std::optional<std::string> valid_compatibility(const std::string_view value)
{
    if (parse_compatibility_switch(value)) {
        return std::nullopt;
    }
    return "unknown compatibility switch '" + std::string { value } + "'";
}

std::optional<std::string> valid_vhdl_compatibility(
    const std::string_view value)
{
    if (parse_vhdl_compatibility(value)) {
        return std::nullopt;
    }
    return "unknown VHDL compatibility profile '" + std::string { value }
        + "'";
}

std::optional<std::string> valid_compilation_unit(
    const std::string_view value)
{
    if (value == "file" || value == "source-set" || value == "combined") {
        return std::nullopt;
    }
    return "compilation unit must be 'file', 'source-set', or 'combined'";
}

std::optional<std::string> valid_uvm_release(const std::string_view value)
{
    if (parse_systemverilog_uvm_release(value)) {
        return std::nullopt;
    }
    return "unknown UVM release '" + std::string { value } + "'";
}

std::optional<std::string> valid_library(const std::string_view value)
{
    if (valid_library_name(value)) {
        return std::nullopt;
    }
    return "'" + std::string { value } + "' is not a library name";
}

std::optional<std::string> valid_optimization(const std::string_view value)
{
    if (parse_optimization(value)) {
        return std::nullopt;
    }
    return "optimization must be 0, 1, 2 or 3";
}

std::optional<std::string> valid_delay_mode(const std::string_view value)
{
    if (parse_delay_mode(value)) {
        return std::nullopt;
    }
    return "delay mode must be 'minimum', 'typical', or 'maximum'";
}

std::optional<std::string> valid_seed(const std::string_view value)
{
    if (lowercase(value) == "random" || !valid_integer(value)) {
        return std::nullopt;
    }
    return "seed must be an unsigned integer or 'random'";
}

std::optional<std::string> valid_generic(const std::string_view value)
{
    const auto separator = value.find('=');
    if (separator != std::string_view::npos && separator != 0U) {
        return std::nullopt;
    }
    return "a generic is NAME=VALUE, not '" + std::string { value } + "'";
}

bool valid_time(const std::string_view value)
{
    std::size_t index {};
    while (index < value.size()
        && std::isdigit(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    if (index == 0U || index == value.size()) {
        return false;
    }
    const auto unit = value.substr(index);
    return unit == "fs" || unit == "ps" || unit == "ns" || unit == "us"
        || unit == "ms" || unit == "s";
}

std::optional<std::string> valid_duration(const std::string_view value)
{
    if (valid_time(value)) {
        return std::nullopt;
    }
    return "a duration is an integer followed by fs, ps, ns, us, ms, or s";
}

std::optional<std::string> valid_trace_format(const std::string_view value)
{
    if (parse_trace_format(value)) {
        return std::nullopt;
    }
    return "trace format must be 'auto', 'vcd', or 'fst'";
}

std::optional<std::string> valid_trace_compression(
    const std::string_view value)
{
    if (parse_trace_compression(value)) {
        return std::nullopt;
    }
    return "trace compression must be 'auto', 'none', or 'deterministic'";
}

std::optional<std::string> valid_timescale(const std::string_view value)
{
    return validate_default_timescale(value);
}

std::optional<std::string> valid_nonempty(const std::string_view value)
{
    if (!value.empty()) {
        return std::nullopt;
    }
    return "the value must not be empty";
}

struct KeyRule {
    ConfigKey key;
    Validator validate;
};

using enum ConfigValueKind;

// Sorted by name.
constexpr std::array kKeys {
    KeyRule { { "compile.code_coverage", boolean, "false",
        "compile with code coverage; elaborate.code_coverage must match" },
        nullptr },
    KeyRule { { "compile.compilation_unit", text, "",
        "compilation unit of every language: file, source-set, combined" },
        valid_compilation_unit },
    KeyRule { { "compile.defines", list, "",
        "preprocessor defines (NAME or NAME=VALUE) of Verilog and SystemVerilog" },
        valid_nonempty },
    KeyRule { { "compile.include_directories", list, "",
        "include directories of Verilog and SystemVerilog, relative to the workspace" },
        valid_nonempty },
    KeyRule { { "compile.jobs", integer, "0",
        "parallel compilation jobs; 0 picks the machine's count" },
        valid_integer },
    KeyRule { { "compile.library", text, "work",
        "library compiled sources go into" },
        valid_library },
    KeyRule { { "compile.sv.compatibility", list, "",
        "SystemVerilog compatibility switches" },
        valid_compatibility },
    KeyRule { { "compile.sv.compilation_unit", text, "source-set",
        "SystemVerilog compilation unit" },
        valid_compilation_unit },
    KeyRule { { "compile.sv.defines", list, "",
        "SystemVerilog preprocessor defines" },
        valid_nonempty },
    KeyRule { { "compile.sv.include_directories", list, "",
        "SystemVerilog include directories" },
        valid_nonempty },
    KeyRule { { "compile.sv.standard", text, "2017",
        "SystemVerilog standard" },
        valid_standard<Language::system_verilog> },
    KeyRule { { "compile.sv.timescale", text, "",
        "SystemVerilog default timescale (UNIT/PRECISION)" },
        valid_timescale },
    KeyRule { { "compile.sv.uvm_release", text, "none",
        "governed UVM release SystemVerilog sources compile against" },
        valid_uvm_release },
    KeyRule { { "compile.timescale", text, "",
        "default timescale (UNIT/PRECISION, 1ns/1ps) of Verilog and SystemVerilog "
        "design elements no `timescale applies to" },
        valid_timescale },
    KeyRule { { "compile.verilog.compatibility", list, "",
        "Verilog compatibility switches" },
        valid_compatibility },
    KeyRule { { "compile.verilog.compilation_unit", text, "source-set",
        "Verilog compilation unit" },
        valid_compilation_unit },
    KeyRule { { "compile.verilog.defines", list, "",
        "Verilog preprocessor defines" },
        valid_nonempty },
    KeyRule { { "compile.verilog.include_directories", list, "",
        "Verilog include directories" },
        valid_nonempty },
    KeyRule { { "compile.verilog.standard", text, "2005",
        "Verilog standard" },
        valid_standard<Language::verilog> },
    KeyRule { { "compile.verilog.timescale", text, "",
        "Verilog default timescale (UNIT/PRECISION)" },
        valid_timescale },
    KeyRule { { "compile.vhdl.compatibility", text, "",
        "VHDL compatibility profile" },
        valid_vhdl_compatibility },
    KeyRule { { "compile.vhdl.compilation_unit", text, "file",
        "VHDL compilation unit" },
        valid_compilation_unit },
    KeyRule { { "compile.vhdl.standard", text, "2008",
        "VHDL standard" },
        valid_standard<Language::vhdl> },
    KeyRule { { "elaborate.aot", boolean, "false",
        "build the static kernel's native code at elaboration" },
        nullptr },
    KeyRule { { "elaborate.code_coverage", boolean, "false",
        "instrument the design for code coverage; compile.code_coverage "
        "must match" },
        nullptr },
    KeyRule { { "elaborate.delay_mode", text, "typical",
        "delay mode: minimum, typical, maximum" },
        valid_delay_mode },
    KeyRule { { "elaborate.generics", list, "",
        "top-level generic and parameter overrides, NAME=VALUE" },
        valid_generic },
    KeyRule { { "elaborate.jobs", integer, "0",
        "parallel elaboration jobs; 0 picks the machine's count" },
        valid_integer },
    KeyRule { { "elaborate.optimization", text, "2",
        "optimization level 0 to 3" },
        valid_optimization },
    KeyRule { { "elaborate.search_libraries", list, "",
        "libraries searched for unqualified design units, in order" },
        valid_library },
    KeyRule { { "elaborate.seed", text, "1",
        "random seed, or 'random'" },
        valid_seed },
    KeyRule { { "elaborate.time_resolution", text, "auto",
        "simulation time resolution, or 'auto'" },
        validate_time_resolution },
    KeyRule { { "elaborate.vpi", list, "",
        "VPI modules LIB[:BOOTSTRAP] loaded for elaboration" },
        valid_nonempty },
    KeyRule { { "simulate.coverage_output", text, "",
        "code-coverage database written at the end of simulation" },
        valid_nonempty },
    KeyRule { { "simulate.delay_mode", text, "typical",
        "delay mode: minimum, typical, maximum" },
        valid_delay_mode },
    KeyRule { { "simulate.duration", text, "",
        "time to simulate, e.g. 10us; empty runs to the end" },
        valid_duration },
    KeyRule { { "simulate.foreign_probes", boolean, "false",
        "statement, call and suspend callbacks for VPI/VHPI libraries" },
        nullptr },
    KeyRule { { "simulate.max_deltas", integer, "100000",
        "delta cycles allowed in one time step" },
        valid_positive },
    KeyRule { { "simulate.seed", text, "1",
        "random seed, or 'random'" },
        valid_seed },
    KeyRule { { "simulate.sv_libs", list, "",
        "DPI-C libraries loaded for simulation" },
        valid_nonempty },
    KeyRule { { "simulate.trace", boolean, "true",
        "write the waveform trace" },
        nullptr },
    KeyRule { { "simulate.trace_compression", text, "auto",
        "trace compression: auto, none, deterministic" },
        valid_trace_compression },
    KeyRule { { "simulate.trace_filters", list, "",
        "trace selections" },
        valid_nonempty },
    KeyRule { { "simulate.trace_format", text, "auto",
        "trace format: auto, vcd, fst" },
        valid_trace_format },
    KeyRule { { "simulate.trace_output", text, "",
        "waveform trace file" },
        valid_nonempty },
    KeyRule { { "simulate.trace_report_limit", integer, "4096",
        "trace diagnostics reported before the rest are counted" },
        valid_positive },
    KeyRule { { "simulate.vhpi", list, "",
        "VHPI modules LIB[:BOOTSTRAP] loaded for simulation" },
        valid_nonempty },
    KeyRule { { "simulate.vpi", list, "",
        "VPI modules LIB[:BOOTSTRAP] loaded for simulation" },
        valid_nonempty },
    KeyRule { { "systemc.compile_options", list, "",
        "options passed to the SystemC compiler" },
        valid_nonempty },
    KeyRule { { "systemc.compiler", text, "",
        "SystemC C++ compiler" },
        valid_nonempty },
    KeyRule { { "systemc.defines", list, "",
        "SystemC preprocessor defines" },
        valid_nonempty },
    KeyRule { { "systemc.include_directories", list, "",
        "SystemC include directories" },
        valid_nonempty },
    KeyRule { { "systemc.link_libraries", list, "",
        "libraries linked into SystemC plug-ins" },
        valid_nonempty },
    KeyRule { { "systemc.link_options", list, "",
        "options passed to the SystemC linker" },
        valid_nonempty },
};

constexpr auto kKeyList = [] {
    std::array<ConfigKey, kKeys.size()> result {};
    for (std::size_t index = 0; index < kKeys.size(); ++index) {
        result[index] = kKeys[index].key;
    }
    return result;
}();

static_assert(std::ranges::is_sorted(kKeyList, {}, &ConfigKey::name));

const KeyRule* find_rule(const std::string_view name) noexcept
{
    const auto found
        = std::ranges::lower_bound(kKeys, name, {}, [](const KeyRule& rule) {
              return rule.key.name;
          });
    return found != kKeys.end() && found->key.name == name ? &*found
                                                           : nullptr;
}

std::size_t edit_distance(const std::string_view left,
    const std::string_view right)
{
    std::vector<std::size_t> row(right.size() + 1U);
    for (std::size_t column = 0; column < row.size(); ++column) {
        row[column] = column;
    }
    for (std::size_t index = 1; index <= left.size(); ++index) {
        auto diagonal = row[0];
        row[0] = index;
        for (std::size_t column = 1; column <= right.size(); ++column) {
            const auto above = row[column];
            row[column] = std::min({ row[column] + 1U, row[column - 1U] + 1U,
                diagonal + (left[index - 1U] == right[column - 1U] ? 0U : 1U) });
            diagonal = above;
        }
    }
    return row.back();
}

std::optional<std::string> normalize_boolean(std::string& value)
{
    const auto spelling = lowercase(value);
    if (spelling == "1" || spelling == "true" || spelling == "yes"
        || spelling == "on") {
        value = "true";
        return std::nullopt;
    }
    if (spelling == "0" || spelling == "false" || spelling == "no"
        || spelling == "off") {
        value = "false";
        return std::nullopt;
    }
    return "'" + value + "' is not a boolean";
}

} // namespace

std::string_view to_string(const ConfigLayer layer) noexcept
{
    switch (layer) {
    case ConfigLayer::user:
        return "user";
    case ConfigLayer::workspace:
        return "workspace";
    case ConfigLayer::session:
        return "session";
    }
    return "session";
}

std::string_view to_string(const ConfigChange change) noexcept
{
    return change == ConfigChange::added ? "added" : "replaced";
}

std::string describe(const ConfigOrigin& origin)
{
    if (origin.file.empty()) {
        return "interactive";
    }
    auto result = origin.file.generic_string();
    if (origin.line != 0U) {
        result += ":" + std::to_string(origin.line);
    }
    return result;
}

std::span<const ConfigKey> config_keys() noexcept
{
    return kKeyList;
}

const ConfigKey* find_config_key(const std::string_view name) noexcept
{
    const auto* rule = find_rule(name);
    return rule == nullptr ? nullptr : &rule->key;
}

std::optional<std::string_view> closest_config_key(
    const std::string_view name) noexcept
{
    std::optional<std::string_view> best;
    std::size_t best_distance = std::numeric_limits<std::size_t>::max();
    for (const auto& key : kKeyList) {
        const auto distance = edit_distance(name, key.name);
        if (distance < best_distance) {
            best_distance = distance;
            best = key.name;
        }
    }
    // A suggestion more different than similar does not help.
    if (!best || best_distance * 2U > name.size()) {
        return std::nullopt;
    }
    return best;
}

std::string_view config_language(const Language language) noexcept
{
    switch (language) {
    case Language::vhdl:
        return "vhdl";
    case Language::verilog:
        return "verilog";
    case Language::system_verilog:
        return "sv";
    case Language::systemc:
        return "systemc";
    }
    return "sv";
}

std::optional<std::string> validate_time_resolution(
    const std::string_view value)
{
    if (lowercase(value) == "auto") {
        return std::nullopt;
    }
    const auto digits = value.find_first_not_of("0123456789");
    if (valid_time(value) && digits != std::string_view::npos
        && value.substr(0U, digits).find_first_not_of('0')
            != std::string_view::npos) {
        return std::nullopt;
    }
    return "time resolution must be 'auto' or a nonzero integer followed by "
           "fs, ps, ns, us, ms, or s";
}

std::optional<std::string> validate_default_timescale(
    const std::string_view value)
{
    // The power of ten of one time literal, or nothing.
    const auto exponent = [](const std::string_view text) -> std::optional<int> {
        constexpr std::array<std::pair<std::string_view, int>, 6> units { {
            { "s", 0 }, { "ms", -3 }, { "us", -6 },
            { "ns", -9 }, { "ps", -12 }, { "fs", -15 } } };
        constexpr std::array<std::pair<std::string_view, int>, 3> magnitudes { {
            { "100", 2 }, { "10", 1 }, { "1", 0 } } };
        for (const auto& [magnitude, magnitude_exponent] : magnitudes) {
            if (!text.starts_with(magnitude)) {
                continue;
            }
            for (const auto& [unit, unit_exponent] : units) {
                if (text.substr(magnitude.size()) == unit) {
                    return magnitude_exponent + unit_exponent;
                }
            }
            return std::nullopt;
        }
        return std::nullopt;
    };
    const auto separator = value.find('/');
    if (separator != std::string_view::npos) {
        const auto unit = exponent(value.substr(0U, separator));
        const auto precision = exponent(value.substr(separator + 1U));
        if (unit && precision) {
            if (*precision > *unit) {
                return "the timescale precision '"
                    + std::string { value.substr(separator + 1U) }
                    + "' is coarser than its unit";
            }
            return std::nullopt;
        }
    }
    return "a timescale is UNIT/PRECISION, each 1, 10, or 100 followed by "
           "s, ms, us, ns, ps, or fs (1ns/1ps), not '"
        + std::string { value } + "'";
}

std::optional<std::string> validate_fsm_hint(const CoverageFsmHintEntry& hint)
{
    if (hint.instance.empty()) {
        return "an FSM hint needs a non-empty instance";
    }
    if (hint.current_state.empty()) {
        return "an FSM hint needs a non-empty current state";
    }
    if (hint.next_state && hint.next_state->empty()) {
        return "an FSM hint's next state must not be empty";
    }
    if (hint.legal_states
        && (hint.legal_states->empty()
            || std::ranges::any_of(*hint.legal_states,
                [](const std::string& state) { return state.empty(); }))) {
        return "an FSM hint's legal states must be non-empty names";
    }
    return std::nullopt;
}

std::optional<std::string> validate_exclusion(
    const CoverageExclusionEntry& exclusion)
{
    if (exclusion.metric.empty()) {
        return "a coverage exclusion needs a non-empty metric";
    }
    if (exclusion.reason.empty()) {
        return "a coverage exclusion needs a non-empty reason";
    }
    for (const auto& [name, selector] : {
             std::pair { "source", &exclusion.source },
             std::pair { "hierarchy", &exclusion.hierarchy },
             std::pair { "object", &exclusion.object } }) {
        if (*selector && (*selector)->empty()) {
            return "a coverage exclusion's " + std::string { name }
                + " selector must not be empty";
        }
    }
    return std::nullopt;
}

std::optional<std::string> validate_binding(const Binding& binding)
{
    if (binding.instance.empty()) {
        return "a binding needs a non-empty instance";
    }
    if (binding.target) {
        if (binding.target->empty()) {
            return "a binding target must not be empty";
        }
        if (!binding.target->starts_with("vhdl:")
            && !binding.target->starts_with("sv:")
            && !binding.target->starts_with("systemc:")) {
            return "binding target '" + *binding.target
                + "' must start with vhdl:, sv:, or systemc:";
        }
    }
    if (binding.resolver && *binding.resolver != "std_logic"
        && *binding.resolver != "sv_wire") {
        return "binding resolver must be 'std_logic' or 'sv_wire'";
    }
    if (!binding.target && !binding.resolver) {
        return "a binding needs a target, a resolver, or both";
    }
    return std::nullopt;
}

std::optional<std::string> Configuration::set(const std::string_view key,
    std::vector<std::string> values, ConfigOrigin origin)
{
    const auto* rule = find_rule(key);
    if (rule == nullptr) {
        auto message = "unknown configuration key '" + std::string { key }
            + "'";
        if (const auto closest = closest_config_key(key)) {
            message += "; did you mean '" + std::string { *closest } + "'?";
        }
        return message;
    }
    if (rule->key.kind != ConfigValueKind::list && values.size() != 1U) {
        return "configuration key '" + std::string { key }
            + "' takes one value";
    }
    for (auto& value : values) {
        if (rule->key.kind == ConfigValueKind::boolean) {
            if (auto error = normalize_boolean(value)) {
                return "configuration key '" + std::string { key } + "': "
                    + *error;
            }
            continue;
        }
        if (rule->key.kind == ConfigValueKind::integer) {
            if (auto error = valid_integer(value)) {
                return "configuration key '" + std::string { key } + "': "
                    + *error;
            }
        }
        if (rule->validate != nullptr) {
            if (auto error = rule->validate(value)) {
                return "configuration key '" + std::string { key } + "': "
                    + *error;
            }
        }
    }
    auto& layer = layers_[static_cast<std::size_t>(origin.layer)];
    layer.insert_or_assign(std::string { key },
        ConfigSetting { std::move(values), std::move(origin) });
    return std::nullopt;
}

bool Configuration::unset(const std::string_view key, const ConfigLayer layer)
{
    auto& values = layers_[static_cast<std::size_t>(layer)];
    const auto found = values.find(key);
    if (found == values.end()) {
        return false;
    }
    values.erase(found);
    return true;
}

const ConfigSetting* Configuration::find(const std::string_view key) const
{
    for (auto layer = std::size(layers_); layer-- > 0U;) {
        if (const auto found = layers_[layer].find(key);
            found != layers_[layer].end()) {
            return &found->second;
        }
    }
    return nullptr;
}

const ConfigSetting* Configuration::find(const std::string_view command,
    const Language language, const std::string_view option) const
{
    const auto scoped = std::string { command } + "."
        + std::string { config_language(language) } + "."
        + std::string { option };
    if (const auto* setting = find(scoped)) {
        return setting;
    }
    return find(std::string { command } + "." + std::string { option });
}

std::vector<std::pair<std::string, ConfigSetting>>
Configuration::effective() const
{
    std::map<std::string, ConfigSetting> merged;
    for (const auto& layer : layers_) {
        for (const auto& [key, setting] : layer) {
            merged.insert_or_assign(key, setting);
        }
    }
    return { merged.begin(), merged.end() };
}

std::optional<std::string> Configuration::put_fsm_hint(
    CoverageFsmHintEntry hint, ConfigOrigin origin, ConfigChange& change)
{
    if (auto error = validate_fsm_hint(hint)) {
        return error;
    }
    const auto existing = std::ranges::find_if(fsm_hints_,
        [&](const ConfigFsmHint& item) {
            return item.entry.instance == hint.instance
                && item.entry.current_state == hint.current_state;
        });
    if (existing != fsm_hints_.end()) {
        *existing = { std::move(hint), std::move(origin) };
        change = ConfigChange::replaced;
        return std::nullopt;
    }
    fsm_hints_.push_back({ std::move(hint), std::move(origin) });
    change = ConfigChange::added;
    return std::nullopt;
}

std::size_t Configuration::remove_fsm_hints(const std::string_view instance,
    const std::optional<std::string_view> current_state)
{
    return std::erase_if(fsm_hints_, [&](const ConfigFsmHint& item) {
        return item.entry.instance == instance
            && (!current_state || item.entry.current_state == *current_state);
    });
}

std::optional<std::string> Configuration::put_exclusion(
    std::optional<std::string> id, CoverageExclusionEntry exclusion,
    ConfigOrigin origin, ConfigChange& change, std::string& assigned_id)
{
    if (auto error = validate_exclusion(exclusion)) {
        return error;
    }
    if (id && id->empty()) {
        return "a coverage exclusion name must not be empty";
    }
    if (!id) {
        // Generated names never collide with a given one.
        do {
            id = "x" + std::to_string(next_exclusion_++);
        } while (std::ranges::find(exclusions_, *id, &ConfigExclusion::id)
            != exclusions_.end());
    }
    assigned_id = *id;
    const auto existing
        = std::ranges::find(exclusions_, *id, &ConfigExclusion::id);
    if (existing != exclusions_.end()) {
        existing->entry = std::move(exclusion);
        existing->origin = std::move(origin);
        change = ConfigChange::replaced;
        return std::nullopt;
    }
    exclusions_.push_back(
        { std::move(*id), std::move(exclusion), std::move(origin) });
    change = ConfigChange::added;
    return std::nullopt;
}

bool Configuration::remove_exclusion(const std::string_view id)
{
    return std::erase_if(exclusions_, [&](const ConfigExclusion& item) {
        return item.id == id;
    }) != 0U;
}

std::optional<std::string> Configuration::put_binding(Binding binding,
    ConfigOrigin origin, ConfigChange& change)
{
    if (auto error = validate_binding(binding)) {
        return error;
    }
    const auto existing = std::ranges::find_if(bindings_,
        [&](const ConfigBinding& item) {
            return item.entry.instance == binding.instance;
        });
    if (existing != bindings_.end()) {
        *existing = { std::move(binding), std::move(origin) };
        change = ConfigChange::replaced;
        return std::nullopt;
    }
    bindings_.push_back({ std::move(binding), std::move(origin) });
    change = ConfigChange::added;
    return std::nullopt;
}

bool Configuration::remove_binding(const std::string_view instance)
{
    return std::erase_if(bindings_, [&](const ConfigBinding& item) {
        return item.entry.instance == instance;
    }) != 0U;
}

void Configuration::apply_elaboration_settings(Config& config) const
{
    config.coverage.fsm_hints.clear();
    for (const auto& hint : fsm_hints_) {
        config.coverage.fsm_hints.push_back(hint.entry);
    }
    config.coverage.exclusions.clear();
    for (const auto& exclusion : exclusions_) {
        config.coverage.exclusions.push_back(exclusion.entry);
    }
    config.bindings.clear();
    for (const auto& binding : bindings_) {
        config.bindings.push_back(binding.entry);
    }
    if (const auto* resolution = find("elaborate.time_resolution")) {
        config.project.time_resolution = resolution->values.front();
    }
    config.recorded_settings = elaboration_record();
}

namespace {

std::string join(const std::vector<std::string>& values)
{
    std::string result;
    for (const auto& value : values) {
        result += (result.empty() ? "" : " ") + value;
    }
    return result;
}

void field(std::string& text, const std::string_view name,
    const std::optional<std::string>& value)
{
    if (value) {
        text += (text.empty() ? "" : " ") + std::string { name } + "=" + *value;
    }
}

} // namespace

std::vector<RecordedSetting> Configuration::elaboration_record() const
{
    std::vector<RecordedSetting> result;
    for (const auto& [key, setting] : effective()) {
        if (key.starts_with("elaborate.")) {
            result.push_back({ key, join(setting.values), describe(setting.origin) });
        }
    }
    for (const auto& hint : fsm_hints_) {
        std::string value;
        field(value, "next", hint.entry.next_state);
        if (hint.entry.legal_states) {
            field(value, "legal", join(*hint.entry.legal_states));
        }
        result.push_back({ "coverage.fsm[" + hint.entry.instance + ","
                + hint.entry.current_state + "]",
            std::move(value), describe(hint.origin) });
    }
    for (const auto& exclusion : exclusions_) {
        std::string value;
        field(value, "source", exclusion.entry.source);
        field(value, "hierarchy", exclusion.entry.hierarchy);
        field(value, "object", exclusion.entry.object);
        field(value, "metric", exclusion.entry.metric);
        field(value, "reason", exclusion.entry.reason);
        result.push_back({ "coverage.exclude[" + exclusion.id + "]",
            std::move(value), describe(exclusion.origin) });
    }
    for (const auto& binding : bindings_) {
        std::string value;
        field(value, "target", binding.entry.target);
        field(value, "resolver", binding.entry.resolver);
        result.push_back({ "bind[" + binding.entry.instance + "]",
            std::move(value), describe(binding.origin) });
    }
    std::ranges::sort(result, {}, &RecordedSetting::key);
    return result;
}

std::vector<std::string> differing_settings(
    const std::span<const RecordedSetting> recorded,
    const std::span<const RecordedSetting> current)
{
    std::map<std::string, std::pair<const std::string*, const std::string*>,
        std::less<>>
        values;
    for (const auto& setting : recorded) {
        values[setting.key].first = &setting.value;
    }
    for (const auto& setting : current) {
        values[setting.key].second = &setting.value;
    }
    std::vector<std::string> result;
    for (const auto& [key, pair] : values) {
        if (pair.first == nullptr || pair.second == nullptr
            || *pair.first != *pair.second) {
            result.push_back(key);
        }
    }
    return result;
}

}  // namespace fsim::project
