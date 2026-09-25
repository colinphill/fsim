// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app::tcl_completion {

enum class RequestContext : std::uint8_t {
    interactive_console,
    script,
    callback,
};

enum class Capability : std::uint8_t {
    none = 0,
    workspace = 1U << 0U,
    loaded_design = 1U << 1U,
    simulation = 1U << 2U,
    debugger = 1U << 3U,
};

using CapabilityMask = std::uint8_t;

constexpr CapabilityMask capability_mask(const Capability capability)
{
    return static_cast<CapabilityMask>(capability);
}

constexpr CapabilityMask operator|(const Capability left, const Capability right)
{
    return static_cast<CapabilityMask>(capability_mask(left) | capability_mask(right));
}

constexpr CapabilityMask operator|(const CapabilityMask left, const Capability right)
{
    return static_cast<CapabilityMask>(left | capability_mask(right));
}

struct Generations {
    std::uint64_t catalog { };
    std::uint64_t workspace { };
    std::uint64_t session { };
    std::uint64_t debugger { };

    friend bool operator==(const Generations&, const Generations&) = default;
};

enum class CandidateKind : std::uint8_t {
    builtin_command,
    namespace_name,
    procedure,
    variable,
    fsim_command,
    option,
    path,
    directory,
    library,
    snapshot,
    compiled_definition,
    design_object,
    package,
    type,
    debugger_entity,
    literal,
};

enum class ArgumentDomain : std::uint8_t {
    literal,
    source_path,
    directory_path,
    library,
    snapshot,
    compiled_definition,
    design_object,
    package,
    type,
    debugger_entity,
    tcl_namespace,
    tcl_procedure,
    tcl_variable,
};

struct CompletionItem {
    std::string_view text;
    std::string_view help;
    CandidateKind kind { CandidateKind::design_object };
    CapabilityMask required_capabilities { };
};

struct CompletionChoice {
    std::string_view text;
    std::string_view help;
};

struct CompletionArgument {
    ArgumentDomain domain { ArgumentDomain::literal };
    bool optional { false };
    bool repeatable { false };
    std::string_view name { };
    std::span<const CompletionChoice> choices { };
};

struct CompletionOption {
    std::string_view text;
    std::string_view help;
    std::optional<ArgumentDomain> value_domain { };
    std::span<const CompletionChoice> value_choices { };
};

struct CompletionSubcommand {
    std::string_view name;
    std::string_view usage;
    std::string_view help;
    std::span<const CompletionArgument> arguments;
    std::span<const CompletionOption> options;
    CapabilityMask required_capabilities { };
};

struct CompletionCommand {
    std::string_view name;
    std::string_view usage;
    std::string_view help;
    CapabilityMask required_capabilities { };
    bool callback_safe { false };
    std::span<const CompletionArgument> arguments;
    std::span<const CompletionOption> options;
    std::span<const CompletionSubcommand> subcommands { };
};

// All views in a snapshot need to remain valid only for the synchronous
// complete() call. Snapshot collections should have stable contents and
// enumeration order for the supplied generations. The engine does not retain
// these views.
struct CompletionSnapshot {
    Generations generations;
    std::span<const CompletionCommand> commands;
    std::span<const CompletionItem> namespaces;
    std::span<const CompletionItem> procedures;
    std::span<const CompletionItem> variables;
    std::span<const CompletionItem> paths;
    std::span<const CompletionItem> libraries;
    std::span<const CompletionItem> snapshots;
    std::span<const CompletionItem> compiled_definitions;
    std::span<const CompletionItem> design_objects;
    std::span<const CompletionItem> packages;
    std::span<const CompletionItem> types;
    std::span<const CompletionItem> debugger_entities;
};

struct CompletionRequest {
    std::string_view buffer;
    std::size_t cursor_byte { };
    RequestContext context { RequestContext::interactive_console };
    CapabilityMask capabilities { };
    Generations generations;
    std::size_t max_candidates { 64 };
};

enum class CompletionStatus : std::uint8_t {
    ok,
    invalid_utf8,
    invalid_cursor,
    input_too_large,
    complexity_limit,
    stale_snapshot,
};

struct Candidate {
    // Replaces CompletionResult's half-open byte range. For ordinary words,
    // text is a complete Tcl word with separators escaped. Variable completion
    // replaces only the variable name, so text is the raw variable name.
    std::string text;
    std::string display;
    std::string help;
    CandidateKind kind { CandidateKind::design_object };
};

struct Hint {
    std::string text;
};

enum class SpanKind : std::uint8_t {
    command,
    variable,
    option,
    quoted_string,
    braced_word,
    command_substitution,
    comment,
    diagnostic,
};

enum class Severity : std::uint8_t {
    none,
    informational,
    warning,
    error,
};

struct Span {
    std::size_t begin_byte { };
    std::size_t end_byte { };
    SpanKind kind { SpanKind::command };
    Severity severity { Severity::none };
    std::string message;
};

struct CompletionResult {
    CompletionStatus status { CompletionStatus::ok };
    std::size_t replace_begin_byte { };
    std::size_t replace_end_byte { };
    Generations generations;
    std::vector<Candidate> candidates;
    std::vector<Hint> hints;
    std::vector<Span> spans;
    bool candidates_truncated { false };
    bool spans_truncated { false };
};

// The service is pure with respect to the Tcl interpreter, simulator, and
// workspace. It parses but never evaluates the supplied Tcl text.
CompletionResult complete(
    const CompletionRequest&, const CompletionSnapshot&);

// Consumers should check this immediately before applying asynchronous or
// cached results to an input buffer.
bool is_current(const CompletionResult&, const Generations&) noexcept;

} // namespace fsim::app::tcl_completion
