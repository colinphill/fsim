// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/project/project.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::project {

// The configuration a session builds from its Tcl configuration scripts:
// defaults for command options (`fsim::config set KEY VALUE`) and the
// settings only a script can give (coverage FSM hints and exclusions,
// instance bindings). The per-user script runs first, then the workspace
// script; a Tcl session may change both afterwards. An explicit option of
// a command always wins over a default.

enum class ConfigLayer : std::uint8_t {
    user,
    workspace,
    session,
};

[[nodiscard]] std::string_view to_string(ConfigLayer layer) noexcept;

// Where a setting was made: a script file and line, or an interactive
// command (empty file).
struct ConfigOrigin {
    ConfigLayer layer { ConfigLayer::session };
    std::filesystem::path file;
    std::uint32_t line {};

    friend bool operator==(const ConfigOrigin&, const ConfigOrigin&) = default;
};

[[nodiscard]] std::string describe(const ConfigOrigin& origin);

enum class ConfigValueKind : std::uint8_t {
    text,
    integer,
    boolean,
    list,
};

struct ConfigKey {
    std::string_view name;
    ConfigValueKind kind;
    std::string_view default_value;
    std::string_view summary;
};

// Every key `fsim::config` accepts, sorted by name.
[[nodiscard]] std::span<const ConfigKey> config_keys() noexcept;
[[nodiscard]] const ConfigKey* find_config_key(std::string_view name) noexcept;

// The closest documented key, for an unknown key's diagnostic.
[[nodiscard]] std::optional<std::string_view> closest_config_key(
    std::string_view name) noexcept;

// The key spelling of a language in `command.<language>.option` keys.
[[nodiscard]] std::string_view config_language(Language language) noexcept;

struct ConfigSetting {
    // A scalar holds one element.
    std::vector<std::string> values;
    ConfigOrigin origin;
};

struct ConfigFsmHint {
    CoverageFsmHintEntry entry;
    ConfigOrigin origin;
};

struct ConfigExclusion {
    std::string id;
    CoverageExclusionEntry entry;
    ConfigOrigin origin;
};

struct ConfigBinding {
    Binding entry;
    ConfigOrigin origin;
};

enum class ConfigChange : std::uint8_t {
    added,
    replaced,
};

[[nodiscard]] std::string_view to_string(ConfigChange change) noexcept;

// Validation shared by the configuration commands. Each returns the
// diagnostic text of the first problem, or nothing.
[[nodiscard]] std::optional<std::string> validate_time_resolution(
    std::string_view value);
// UNIT/PRECISION of a `timescale (IEEE 1800-2023 22.7): each 1, 10 or 100
// followed by s, ms, us, ns, ps or fs, the precision no coarser than the unit.
[[nodiscard]] std::optional<std::string> validate_default_timescale(
    std::string_view value);
[[nodiscard]] std::optional<std::string> validate_fsm_hint(
    const CoverageFsmHintEntry& hint);
[[nodiscard]] std::optional<std::string> validate_exclusion(
    const CoverageExclusionEntry& exclusion);
[[nodiscard]] std::optional<std::string> validate_binding(
    const Binding& binding);

class Configuration {
public:
    // Validates the value against the key and records it in the origin's
    // layer. An error leaves the configuration unchanged.
    [[nodiscard]] std::optional<std::string> set(std::string_view key,
        std::vector<std::string> values, ConfigOrigin origin);
    // Removes the key from one layer; the value of a lower layer, or the
    // built-in default, applies again. False when that layer has no value.
    [[nodiscard]] bool unset(std::string_view key, ConfigLayer layer);
    // The value in force: the highest layer that sets the key.
    [[nodiscard]] const ConfigSetting* find(std::string_view key) const;
    // `command.<language>.option`, else `command.option`.
    [[nodiscard]] const ConfigSetting* find(std::string_view command,
        Language language, std::string_view option) const;
    // Every key in force, sorted by key.
    [[nodiscard]] std::vector<std::pair<std::string, ConfigSetting>>
    effective() const;

    // Keyed by instance and current-state object.
    [[nodiscard]] std::optional<std::string> put_fsm_hint(
        CoverageFsmHintEntry hint, ConfigOrigin origin, ConfigChange& change);
    // Every hint of the instance, or only the one of `current_state`.
    [[nodiscard]] std::size_t remove_fsm_hints(std::string_view instance,
        std::optional<std::string_view> current_state);
    [[nodiscard]] const std::vector<ConfigFsmHint>& fsm_hints() const noexcept
    {
        return fsm_hints_;
    }
    void clear_fsm_hints() noexcept { fsm_hints_.clear(); }

    // Keyed by the given id, or by a generated `xN` when none is given.
    [[nodiscard]] std::optional<std::string> put_exclusion(
        std::optional<std::string> id, CoverageExclusionEntry exclusion,
        ConfigOrigin origin, ConfigChange& change, std::string& assigned_id);
    [[nodiscard]] bool remove_exclusion(std::string_view id);
    [[nodiscard]] const std::vector<ConfigExclusion>& exclusions()
        const noexcept
    {
        return exclusions_;
    }
    void clear_exclusions() noexcept { exclusions_.clear(); }

    // Keyed by instance path.
    [[nodiscard]] std::optional<std::string> put_binding(Binding binding,
        ConfigOrigin origin, ConfigChange& change);
    [[nodiscard]] bool remove_binding(std::string_view instance);
    [[nodiscard]] const std::vector<ConfigBinding>& bindings() const noexcept
    {
        return bindings_;
    }
    void clear_bindings() noexcept { bindings_.clear(); }

    // The scripts that were run, in order.
    [[nodiscard]] const std::vector<std::filesystem::path>& scripts()
        const noexcept
    {
        return scripts_;
    }
    void add_script(std::filesystem::path script)
    {
        scripts_.push_back(std::move(script));
    }

    // Puts the script-only settings into an elaboration configuration:
    // the FSM hints, exclusions and bindings in key order, and
    // `elaborate.time_resolution`; records elaboration_record() in it.
    void apply_elaboration_settings(Config& config) const;

    // Every `elaborate.*` key in force and every keyed entry, sorted by
    // key, as a snapshot records them.
    [[nodiscard]] std::vector<RecordedSetting> elaboration_record() const;

private:
    std::map<std::string, ConfigSetting, std::less<>> layers_[3];
    std::vector<ConfigFsmHint> fsm_hints_;
    std::vector<ConfigExclusion> exclusions_;
    std::uint64_t next_exclusion_ { 1 };
    std::vector<ConfigBinding> bindings_;
    std::vector<std::filesystem::path> scripts_;
};

// The keys whose values differ between two records, or that only one has,
// sorted.
[[nodiscard]] std::vector<std::string> differing_settings(
    std::span<const RecordedSetting> recorded,
    std::span<const RecordedSetting> current);

}  // namespace fsim::project
