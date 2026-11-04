// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::project {

enum class Language : std::uint8_t {
    vhdl,
    verilog,
    system_verilog,
    systemc,
};

enum class VhdlStandard : std::uint8_t {
    vhdl_1987,
    vhdl_1993,
    vhdl_2000,
    vhdl_2002,
    vhdl_2008,
    vhdl_2019,
};

enum class VerilogStandard : std::uint8_t {
    verilog_1995,
    verilog_2001,
    verilog_2001_noconfig,
    verilog_2005,
};

enum class SystemVerilogStandard : std::uint8_t {
    systemverilog_2005,
    systemverilog_2009,
    systemverilog_2012,
    systemverilog_2017,
    systemverilog_2023,
};

enum class Optimization : std::uint8_t {
    o0,
    o1,
    o2,
    o3,
};

enum class DelayMode : std::uint8_t {
  minimum,
  typical,
  maximum,
};

enum class TraceFormat : std::uint8_t {
  automatic,
  vcd,
  fst,
};

enum class TraceCompression : std::uint8_t {
  automatic,
  none,
  deterministic,
};

// Canonical governed UVM source release. `none` preserves ordinary
// SystemVerilog projects which do not opt into a governed UVM package.
enum class SystemVerilogUvmRelease : std::uint8_t {
  none,
  uvm_1_2,
  ieee_1800_2_2020_3_1,
};

// Normalized compatibility dispatch for the complete governed difference
// boundary. Common runtime behavior is deliberately absent from this record.
struct SystemVerilogUvmCompatibility {
  SystemVerilogUvmRelease release{SystemVerilogUvmRelease::none};
  std::uint32_t version_major{};
  std::uint32_t version_minor{};
  bool legacy_global_controls{};
  bool legacy_registration_macros{};
  bool legacy_component_stop_methods{};
  bool legacy_sequence_library_methods{};
  bool component_config_compatibility{};
  bool test_done_objection_compatibility{};
  bool ieee_policy_classes{};
  bool ieee_object_policy_dispatch{};
  bool ieee_report_summary_methods{};

  friend bool operator==(
      const SystemVerilogUvmCompatibility&,
      const SystemVerilogUvmCompatibility&) = default;
};

struct ProjectSection {
  struct TopLevel {
    std::string target;
    std::string alias;

    friend bool operator==(const TopLevel&, const TopLevel&) = default;
  };

  std::string name;
  // Legacy source-compatible spelling for one root. Parsed configurations
  // always expose the normalized ordered selection through tops.
  std::string top;
  std::vector<TopLevel> tops;
  // Top-level generic and parameter overrides (`--generic NAME=VALUE`),
  // applied to every root that declares a generic of that name.
  std::vector<std::pair<std::string, std::string>> generics;
  std::string time_resolution{"auto"};
  std::uint64_t seed{1};
  bool random_seed{false};
};

struct SourceSet {
  Language language{Language::system_verilog};
  std::string standard;
  std::vector<std::string> compatibility_switches;
  std::string vhdl_compatibility;
  std::string library{"work"};
  std::vector<std::filesystem::path> file_patterns;
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  // `timescale for Verilog and SystemVerilog design elements that none
  // applies to ("1ns/1ps"); empty keeps the tool default.
  std::string default_timescale;
  std::string compilation_unit{"file"};
  SystemVerilogUvmRelease uvm_release{SystemVerilogUvmRelease::none};
};

struct Binding {
  std::string instance;
  std::optional<std::string> target;
  std::optional<std::string> resolver;
};

// Maps one logical library to a relocatable, read-only precompiled library
// directory. Paths parsed from a manifest are absolute and normalized against
// the manifest directory; programmatic callers may provide absolute paths.
struct LibraryMapping {
  std::string library;
  std::filesystem::path path;

  friend bool operator==(const LibraryMapping&, const LibraryMapping&) = default;
};

struct ElaborationSection {
  // Unqualified lookup uses the parent logical library followed by first
  // occurrences from this ordered list as one complete ambiguity scope.
  std::vector<std::string> search_libraries;
  // VPI libraries (`lib[:bootstrap]`) whose startup routines register the
  // user system tasks and functions that calls in the design name.
  std::vector<std::string> vpi_modules;
};

struct BuildSection {
  Optimization optimization{Optimization::o2};
  std::uint32_t jobs{0};
  std::filesystem::path cache_path{".fsim-cache"};
};

// One exact, language-neutral FSM description selected by stable elaborated
// instance and retained object names. The elaboration coverage layer validates
// and combines these entries with language-source hints transactionally.
struct CoverageFsmHintEntry {
  std::string instance;
  std::string current_state;
  std::optional<std::string> next_state;
  std::optional<std::vector<std::string>> legal_states;

  friend bool operator==(const CoverageFsmHintEntry&,
      const CoverageFsmHintEntry&) = default;
};

// One language-neutral external coverage exclusion. Optional selectors are
// conjunctive; an omitted selector matches every value in that dimension.
struct CoverageExclusionEntry {
  std::optional<std::string> source;
  std::optional<std::string> hierarchy;
  std::optional<std::string> object;
  std::string metric;
  std::string reason;

  friend bool operator==(const CoverageExclusionEntry&,
      const CoverageExclusionEntry&) = default;
};

struct CoverageSection {
  bool enabled{false};
  std::vector<CoverageFsmHintEntry> fsm_hints;
  std::vector<CoverageExclusionEntry> exclusions;
};

struct RunSection {
  std::optional<std::string> duration;
  std::uint64_t max_deltas{100'000};
  DelayMode delay_mode{DelayMode::typical};
  std::optional<std::filesystem::path> trace_file;
  TraceFormat trace_format{TraceFormat::automatic};
  TraceCompression trace_compression{TraceCompression::automatic};
  std::vector<std::string> trace_filters;
  std::size_t trace_report_limit{4'096U};
  bool trace_enabled{true};
  // Code-coverage database written when the simulation ends.
  std::optional<std::filesystem::path> coverage_output;
  // Foreign libraries loaded into the simulation: VPI modules
  // (`lib[:bootstrap]`), DPI-C libraries and VHPI modules.
  std::vector<std::string> vpi_modules;
  std::vector<std::string> sv_libs;
  std::vector<std::string> vhpi_modules;
  // Statement, call and suspend probes for the foreign libraries
  // (cbStmt, vhpiCbStmt and the like); the kernel keeps its debug points.
  bool foreign_probes{false};
  // VHPI after-delay and timeout callbacks registered or enabled by an
  // end-of-time-step callback are accepted and run from the next time step,
  // an extension of IEEE 1076-2019 21.3.6.8 that cocotb relies on.
  bool vhpi_end_of_step_timers{false};
};

struct SystemCSection {
  std::string compiler;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  std::vector<std::string> compile_options;
  std::vector<std::string> link_options;
  std::vector<std::string> libraries;
};

// A configuration-script setting in force at elaboration, as a snapshot
// records it: `key` names one `elaborate.*` key or one keyed entry
// (`coverage.fsm[...]`, `coverage.exclude[...]`, `bind[...]`).
struct RecordedSetting {
    std::string key;
    std::string value;
    std::string origin;

    friend bool operator==(const RecordedSetting&, const RecordedSetting&) = default;
};

struct Config {
    std::filesystem::path base_directory;
    ProjectSection project;
    std::vector<SourceSet> source_sets;
    std::vector<Binding> bindings;
    std::vector<LibraryMapping> library_mappings;
    ElaborationSection elaboration;
    BuildSection build;
    CoverageSection coverage;
    RunSection run;
    SystemCSection systemc;
    // The configuration-script settings an elaboration records in its
    // snapshot.
    std::vector<RecordedSetting> recorded_settings;
};

[[nodiscard]] std::string_view to_string(Language language) noexcept;
[[nodiscard]] std::string_view to_string(VhdlStandard standard) noexcept;
[[nodiscard]] std::string_view to_string(VerilogStandard standard) noexcept;
[[nodiscard]] std::string_view to_string(
    SystemVerilogStandard standard) noexcept;
[[nodiscard]] std::string_view to_string(Optimization optimization) noexcept;
[[nodiscard]] std::string_view to_string(DelayMode mode) noexcept;
[[nodiscard]] std::string_view to_string(TraceFormat format) noexcept;
[[nodiscard]] std::string_view to_string(TraceCompression compression) noexcept;
[[nodiscard]] std::string_view to_string(SystemVerilogUvmRelease release) noexcept;
[[nodiscard]] std::optional<Language> parse_language(std::string_view spelling) noexcept;
[[nodiscard]] std::optional<VhdlStandard> parse_vhdl_standard(
    std::string_view spelling) noexcept;
[[nodiscard]] std::optional<VerilogStandard> parse_verilog_standard(
    std::string_view spelling) noexcept;
[[nodiscard]] std::optional<SystemVerilogStandard>
parse_systemverilog_standard(std::string_view spelling) noexcept;
[[nodiscard]] std::string_view default_standard(Language language) noexcept;
[[nodiscard]] std::optional<std::string_view> canonical_standard(
    Language language,
    std::string_view spelling) noexcept;
[[nodiscard]] std::optional<std::string_view> parse_compatibility_switch(
    std::string_view spelling) noexcept;
[[nodiscard]] std::optional<std::string_view> parse_vhdl_compatibility(
    std::string_view spelling) noexcept;
[[nodiscard]] std::string compatibility_profile(
    const std::vector<std::string>& switches);
[[nodiscard]] std::optional<SystemVerilogUvmRelease>
parse_systemverilog_uvm_release(std::string_view spelling) noexcept;
[[nodiscard]] SystemVerilogUvmCompatibility
systemverilog_uvm_compatibility(SystemVerilogUvmRelease release) noexcept;
[[nodiscard]] std::optional<DelayMode> parse_delay_mode(
    std::string_view spelling) noexcept;
// `0`..`3` or `O0`..`O3`, either case.
[[nodiscard]] std::optional<Optimization> parse_optimization(
    std::string_view spelling) noexcept;
// A library name: a letter or underscore, then letters, digits, underscores.
[[nodiscard]] bool valid_library_name(std::string_view value) noexcept;
[[nodiscard]] std::optional<TraceFormat> parse_trace_format(
    std::string_view spelling) noexcept;
[[nodiscard]] std::optional<TraceCompression> parse_trace_compression(
    std::string_view spelling) noexcept;

}  // namespace fsim::project
