// SPDX-License-Identifier: Apache-2.0
//
// SDF backannotation of VITAL timing generics (IEEE 1076.4-2000 5.1.1, 5.2).
//
// This file is the VITAL SDF map: it turns the DELAY and TIMINGCHECK entries
// of the SDF cells that name VHDL instances into annotations of timing
// generics (clause 5.2.7), with the delay lists of 5.2.6. The elaborator
// applies the annotations to the generics of the instances it lowers.

#include "application_sdf_vital_generics.hpp"

#include "application_internal.hpp"
#include "application_sdf_session.hpp"

#include "fsim/app/sdf_annotation_scope.hpp"
#include "fsim/app/sdf_control.hpp"
#include "fsim/app/sdf_value_policy.hpp"
#include "fsim/frontend/sdf.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <system_error>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {

    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfConstructKind;
    using frontend::SdfFile;
    using frontend::SdfSyntaxAtom;
    using frontend::SdfSyntaxNode;
    using frontend::SdfTokenKind;
    using frontend::SourceSpan;

    [[nodiscard]] unsigned char byte(const char character) noexcept
    {
        return static_cast<unsigned char>(character);
    }

    [[nodiscard]] std::string ascii_lowercase(std::string_view text)
    {
        std::string result { text };
        for (auto& character : result)
            character = static_cast<char>(std::tolower(byte(character)));
        return result;
    }

    [[nodiscard]] std::string join_nonempty(
        const std::initializer_list<std::string_view> parts)
    {
        std::string result;
        for (const auto part : parts) {
            if (part.empty())
                continue;
            if (!result.empty())
                result.push_back('_');
            result.append(part);
        }
        return result;
    }

    // A basic identifier (IEEE 1076-2019 15.4.2): a letter, then letters,
    // digits and single underscores, not ending in an underscore.
    [[nodiscard]] bool legal_vhdl_identifier(const std::string_view name)
    {
        if (name.empty() || !std::isalpha(byte(name.front())))
            return false;
        bool previous_underscore = false;
        for (const auto character : name) {
            if (std::isalnum(byte(character))) {
                previous_underscore = false;
            } else if (character == '_') {
                if (previous_underscore)
                    return false;
                previous_underscore = true;
            } else {
                return false;
            }
        }
        return !previous_underscore;
    }

    [[nodiscard]] std::string location(const SourceSpan& span)
    {
        return span.source_name.str() + ':' + std::to_string(span.begin.line)
            + ':' + std::to_string(span.begin.column);
    }

    // The port of an SDF port_instance or port_spec (IEEE 1076.4-2000
    // 5.2.7.3.3): its instance path, simple name and whether it selects a
    // bit or a range of a bus.
    struct PortName {
        std::string instance;
        std::string name;
        bool indexed { };
        bool escaped { };
        std::int64_t left { };
        std::int64_t right { };
    };

    [[nodiscard]] PortName parse_port(const std::string_view spelling)
    {
        PortName result;
        result.escaped = spelling.find('\\') != std::string_view::npos;
        int depth { };
        std::size_t cut = std::string_view::npos;
        for (std::size_t index { }; index < spelling.size(); ++index) {
            const auto character = spelling[index];
            if (character == '[')
                ++depth;
            else if (character == ']')
                --depth;
            else if (depth == 0 && (character == '.' || character == '/'))
                cut = index;
        }
        auto base = spelling;
        if (cut != std::string_view::npos) {
            result.instance = std::string { spelling.substr(0U, cut) };
            base = spelling.substr(cut + 1U);
        }
        const auto bracket = base.find('[');
        result.indexed = bracket != std::string_view::npos;
        result.name = std::string { base.substr(0U, bracket) };
        if (result.indexed) {
            // [index] or [left:right] (IEEE 1076.4-2000 5.2.7.3.3).
            auto inside = base.substr(bracket + 1U);
            if (!inside.empty() && inside.back() == ']')
                inside.remove_suffix(1U);
            const auto colon = inside.find(':');
            const auto number = [](const std::string_view text) {
                std::int64_t value { };
                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), value);
                return error == std::errc { }
                        && end == text.data() + text.size()
                    ? std::optional<std::int64_t> { value }
                    : std::nullopt;
            };
            const auto left = number(inside.substr(0U, colon));
            const auto right = colon == std::string_view::npos
                ? left
                : number(inside.substr(colon + 1U));
            if (left && right) {
                result.left = *left;
                result.right = *right;
            } else {
                result.escaped = true; // not a translatable index
            }
        }
        return result;
    }

    // A port of a timing check or path: the port, its edge and its condition
    // suffix, and where it stands in the SDF text.
    struct SpecPort {
        PortName port;
        std::string edge;
        std::string condition;
        std::size_t offset { };
    };

    // IEEE 1076.4-2000 5.2.7.3.2: the lexical suffix of a condition.
    struct ConditionOperator {
        std::string_view spelling;
        std::string_view name;
    };
    constexpr ConditionOperator condition_operators[] = {
        { "===", "EQ3" }, { "!==", "NE3" }, { "<<", "LS" }, { ">>", "RS" },
        { "==", "EQ" }, { "!=", "NE" }, { "&&", "AN" }, { "||", "OR" },
        { "<=", "LE" }, { ">=", "GE" }, { "~&", "NA" }, { "~|", "NO" },
        { "^~", "XNB" }, { "~^", "XNB" }, { "(", "OP" }, { ")", "CP" },
        { "{", "OB" }, { "}", "CB" }, { "[", "OSB" }, { "]", "CSB" },
        { ",", "CM" }, { "?", "QM" }, { ":", "CLN" }, { "+", "PL" },
        { "-", "MI" }, { "*", "MU" }, { "/", "DI" }, { "%", "MOD" },
        { "<", "LT" }, { ">", "GT" }, { "&", "ANB" }, { "|", "ORB" },
        { "^", "XOB" }, { "!", "NT" }, { "~", "NTB" } };

} // namespace

std::string sdf_vital_condition_suffix(const std::string_view text)
{
    std::vector<std::string> parts;
    std::size_t index { };
    while (index < text.size()) {
        const auto character = text[index];
        if (std::isspace(byte(character))) {
            ++index;
            continue;
        }
        if (character == '\\')
            return { };
        if (std::isalpha(byte(character)) || character == '_') {
            auto end = index;
            while (end < text.size()
                && (std::isalnum(byte(text[end])) || text[end] == '_'
                    || text[end] == '$')) {
                ++end;
            }
            parts.emplace_back(text.substr(index, end - index));
            index = end;
            // A range [x:y] becomes xTOy and a bit [x] becomes x.
            if (index < text.size() && text[index] == '[') {
                const auto close = text.find(']', index);
                if (close == std::string_view::npos)
                    return { };
                std::string range { text.substr(index + 1U, close - index - 1U) };
                range.erase(std::remove_if(range.begin(), range.end(),
                                [](const char each) {
                                    return std::isspace(byte(each)) != 0;
                                }),
                    range.end());
                const auto colon = range.find(':');
                std::string mapped = range;
                if (colon != std::string::npos)
                    mapped = range.substr(0U, colon) + "TO" + range.substr(colon + 1U);
                if (mapped.empty()
                    || !std::ranges::all_of(mapped, [](const char each) {
                           return std::isalnum(byte(each)) != 0;
                       })) {
                    return { };
                }
                parts.push_back(std::move(mapped));
                index = close + 1U;
            }
            continue;
        }
        if (std::isdigit(byte(character)) || character == '\'') {
            auto end = index;
            while (end < text.size()
                && (std::isalnum(byte(text[end])) || text[end] == '\''
                    || text[end] == '_')) {
                ++end;
            }
            const auto token = text.substr(index, end - index);
            index = end;
            const auto tick = token.find('\'');
            if (tick != std::string_view::npos) {
                // 1'b0, 1'B1, 'b0 and 'B1 are the scalar constants 0 and 1.
                const auto width = token.substr(0U, tick);
                const auto rest = token.substr(tick + 1U);
                if (std::ranges::all_of(width,
                        [](const char each) { return std::isdigit(byte(each)) != 0; })
                    && rest.size() == 2U && (rest[0] == 'b' || rest[0] == 'B')
                    && (rest[1] == '0' || rest[1] == '1')) {
                    parts.emplace_back(1U, rest[1]);
                    continue;
                }
                std::string cleaned;
                for (const auto each : token) {
                    if (each != '\'')
                        cleaned.push_back(each);
                }
                if (cleaned.empty())
                    return { };
                parts.push_back(std::move(cleaned));
                continue;
            }
            parts.emplace_back(token);
            continue;
        }
        bool matched { };
        for (const auto& candidate : condition_operators) {
            if (text.substr(index).starts_with(candidate.spelling)) {
                parts.emplace_back(candidate.name);
                index += candidate.spelling.size();
                matched = true;
                break;
            }
        }
        if (!matched)
            return { };
    }
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty())
            result.push_back('_');
        result += part;
    }
    return result;
}

namespace {

    // The VHDL instances of the design by lower-case path, and the paths of
    // the other instances.
    struct InstanceIndex {
        std::map<std::string, std::string> vhdl;
        std::set<std::string> other;
    };

    class Translator {
    public:
        Translator(const SdfFile& file, const InstanceIndex& instances,
            SdfDelaySelection selection, std::vector<Diagnostic>& diagnostics,
            std::vector<project::TimingGenericAnnotation>& annotations)
            : file_(file)
            , instances_(instances)
            , diagnostics_(diagnostics)
            , annotations_(annotations)
        {
            policy_.selection = selection;
            policy_.design_time_unit_femtoseconds = 1U;
            policy_.simulation_precision_femtoseconds = 1U;
            selection_index_ = selection == SdfDelaySelection::Minimum ? 0U
                : selection == SdfDelaySelection::Maximum ? 2U
                                                         : 1U;
        }

        // The cell of a Verilog or SystemVerilog instance: only the
        // INTERCONNECT and PORT entries that end at an input port of a VHDL
        // instance below it belong to the VITAL map (5.2.7.1.3, 5.2.7.1.4);
        // the others are annotated by the SDF session of that language.
        void annotate_boundary(const frontend::SdfCell& cell,
            const SdfResolvedInstance& target)
        {
            boundary_ = true;
            annotate(cell, target);
            boundary_ = false;
        }

        void annotate(const frontend::SdfCell& cell,
            const SdfResolvedInstance& target)
        {
            target_path_ = target.instance_path;
            instance_name_.clear();
            if (cell.normalized_instance
                && !cell.normalized_instance->segments.empty()) {
                instance_name_ = cell.normalized_instance->segments.back();
            } else {
                const auto dot = target.instance_path.rfind('.');
                instance_name_ = dot == std::string::npos
                    ? target.instance_path
                    : target.instance_path.substr(dot + 1U);
            }
            for (const auto& declaration : cell.declarations) {
                if (declaration.kind == SdfConstructKind::Delay)
                    delay(declaration);
                else if (declaration.kind == SdfConstructKind::TimingCheck
                    && !boundary_)
                    timing_check(declaration);
                // CORRELATION, TIMINGENV and LABEL entries are not supported
                // by the VITAL SDF map and have no effect (5.2.3, 5.2).
            }
        }

    private:
        void report(const DiagnosticSeverity severity, std::string code,
            std::string message, const SourceSpan& span)
        {
            diagnostics_.push_back(Diagnostic { severity, std::move(code),
                std::move(message), span, { } });
        }

        void error(std::string code, std::string message, const SourceSpan& span)
        {
            report(DiagnosticSeverity::Error, std::move(code),
                std::move(message), span);
        }

        // 5.2.6: one rvalue in femtoseconds after the min:typ:max selection;
        // an empty value and a missing component are SDF null values.
        [[nodiscard]] bool convert(const SdfSyntaxNode& node,
            std::optional<std::int64_t>& result)
        {
            result.reset();
            const SdfSyntaxNode* leaf = &node;
            // A delval of several values: the delay is the first.
            if (!node.exact_value && !node.children.empty()
                && node.children.front().kind == SdfConstructKind::Value) {
                leaf = &node.children.front();
            }
            if (!leaf->exact_value
                || leaf->exact_value->kind == frontend::SdfExactValueKind::Empty) {
                return true;
            }
            const auto& value = *leaf->exact_value;
            if (value.kind == frontend::SdfExactValueKind::Triple
                && !value.components[selection_index_]) {
                return true;
            }
            frontend::SdfNormalizedTimescale timescale;
            if (file_.normalized_timescale) {
                timescale = *file_.normalized_timescale;
            } else {
                // The SDF default time scale is 1 ns.
                timescale.femtoseconds.coefficient = "1";
                timescale.femtoseconds.exponent10 = 6;
                timescale.femtoseconds.canonical = "1e6";
                timescale.canonical = "1ns";
            }
            const auto selected = select_sdf_timing_check_value(
                value, timescale, policy_, leaf->span);
            if (!selected.ok()) {
                diagnostics_.insert(diagnostics_.end(),
                    selected.diagnostics.begin(), selected.diagnostics.end());
                return false;
            }
            result = selected.selected->ticks;
            return true;
        }

        [[nodiscard]] bool values_of(const SdfSyntaxNode& node,
            std::vector<std::optional<std::int64_t>>& values)
        {
            values.clear();
            bool valid = true;
            for (const auto& child : node.children) {
                if (child.kind != SdfConstructKind::Value)
                    continue;
                std::optional<std::int64_t> value;
                valid = convert(child, value) && valid;
                values.push_back(value);
            }
            return valid;
        }

        static project::TimingGenericPort generic_port(const PortName& port)
        {
            return { port.name, port.indexed, port.left, port.right };
        }

        void emit(const std::string& instance, std::string generic,
            std::vector<std::optional<std::int64_t>> values,
            const bool increment, const SourceSpan& span,
            std::vector<project::TimingGenericPort> ports = { },
            std::vector<std::optional<std::int64_t>> retain = { },
            std::string source_instance = { }, std::string source_port = { })
        {
            if (!legal_vhdl_identifier(generic)) {
                error("FSIM-SDF-VITAL-001",
                    "SDF construct has no translation to a legal VHDL "
                    "identifier for the timing generic '" + generic
                        + "' (IEEE 1076.4-2000 5.2)",
                    span);
                return;
            }
            if (values.empty())
                return;
            if (values.size() > 12U) {
                error("FSIM-SDF-VITAL-010",
                    "SDF delay list for '" + generic
                        + "' has more than twelve values (IEEE 1076.4-2000 5.2.6)",
                    span);
                return;
            }
            // A list of an intermediate length is read as if it had trailing
            // empty values (5.2.6).
            if (values.size() == 4U || values.size() == 5U)
                values.resize(6U);
            else if (values.size() > 6U && values.size() < 12U)
                values.resize(12U);
            project::TimingGenericAnnotation annotation;
            annotation.instance = instance;
            annotation.generic = std::move(generic);
            annotation.increment = increment;
            annotation.values = std::move(values);
            annotation.source = location(span);
            annotation.ports = std::move(ports);
            annotation.retain = std::move(retain);
            annotation.interconnect = !source_port.empty();
            annotation.source_instance = std::move(source_instance);
            annotation.source_port = std::move(source_port);
            annotations_.push_back(std::move(annotation));
        }

        // The VHDL instance that a port_instance's instance path names below
        // the cell's instance, or none when the path is not an elaborated VHDL
        // instance; `reported` says an error was reported.
        [[nodiscard]] std::optional<std::string> instance_of(
            const std::string& relative, const SourceSpan& span, bool& reported)
        {
            reported = false;
            auto path = target_path_;
            if (!relative.empty()) {
                std::string tail = relative;
                std::ranges::replace(tail, '/', '.');
                path += '.' + tail;
            }
            const auto key = ascii_lowercase(path);
            if (const auto found = instances_.vhdl.find(key);
                found != instances_.vhdl.end()) {
                return found->second;
            }
            if (!boundary_ && !instances_.other.contains(key)) {
                error("FSIM-SDF-VITAL-006",
                    "SDF port instance '" + path
                        + "' is not an elaborated instance of the design "
                          "(IEEE 1076.4-2000 5.2.4)",
                    span);
                reported = true;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool usable(const PortName& port, const SourceSpan& span)
        {
            if (port.escaped || port.name.empty()) {
                error("FSIM-SDF-VITAL-001",
                    "SDF port '" + port.name
                        + "' has no translation to a legal VHDL identifier "
                          "(IEEE 1076.4-2000 5.2.7.3.3)",
                    span);
                return false;
            }
            return true;
        }

        // The tokens of the node between its head keyword and `stop` (a token
        // index), as the text the condition suffix is made from.
        [[nodiscard]] std::string condition_text(
            const SdfSyntaxNode& node, const std::size_t stop) const
        {
            const auto& tokens = file_.tokens;
            auto index = node.first_token + 1U;
            while (index < tokens.size() && tokens[index].kind == SdfTokenKind::Comment)
                ++index;
            ++index; // COND
            std::string text;
            std::size_t previous_end = std::string::npos;
            bool label = true;
            for (; index < stop && index < tokens.size(); ++index) {
                const auto& token = tokens[index];
                if (token.kind == SdfTokenKind::Comment)
                    continue;
                if (label && token.kind == SdfTokenKind::String) {
                    label = false;
                    continue;
                }
                label = false;
                if (previous_end != std::string::npos
                    && token.span.begin.offset != previous_end) {
                    text.push_back(' ');
                }
                text += token.spelling;
                previous_end = token.span.end.offset;
            }
            return text;
        }

        // 5.2.7.1.2: ( COND expr ( IOPATH ... ) ).
        void conditional_path(const SdfSyntaxNode& node)
        {
            const SdfSyntaxNode* path = nullptr;
            for (const auto& child : node.children) {
                if (child.kind == SdfConstructKind::Iopath)
                    path = &child;
            }
            if (path == nullptr)
                return;
            const auto text = condition_text(node, path->first_token);
            const auto suffix = sdf_vital_condition_suffix(text);
            if (suffix.empty()) {
                error("FSIM-SDF-VITAL-001",
                    "SDF condition '" + text
                        + "' has no translation to a legal VHDL identifier "
                          "(IEEE 1076.4-2000 5.2.7.3.2)",
                    node.span);
                return;
            }
            iopath(*path, suffix);
        }

        void iopath(const SdfSyntaxNode& node, const std::string& condition)
        {
            std::vector<SpecPort> ports;
            for (const auto& atom : node.atoms) {
                if (atom.kind != SdfTokenKind::Identifier
                    && atom.kind != SdfTokenKind::EscapedIdentifier) {
                    continue;
                }
                SpecPort port;
                port.port = parse_port(atom.spelling);
                port.offset = atom.span.begin.offset;
                ports.push_back(std::move(port));
            }
            for (const auto& child : node.children) {
                if (child.kind == SdfConstructKind::Edge && !child.atoms.empty()) {
                    SpecPort port;
                    port.port = parse_port(child.atoms.front().spelling);
                    port.edge = child.keyword_spelling;
                    port.offset = child.span.begin.offset;
                    ports.push_back(std::move(port));
                }
            }
            std::ranges::sort(ports, {}, &SpecPort::offset);
            if (ports.size() != 2U)
                return;
            if (!usable(ports[0].port, node.span)
                || !usable(ports[1].port, node.span)) {
                return;
            }
            // RETAIN (r1 [r2 [r3]]) maps by Table 2 (IEEE 1076.4-2000 5.2.6).
            std::vector<std::optional<std::int64_t>> retain;
            for (const auto& child : node.children) {
                if (child.kind == SdfConstructKind::Retain && retain.empty()
                    && !values_of(child, retain)) {
                    return;
                }
            }
            std::vector<std::optional<std::int64_t>> values;
            if (!values_of(node, values))
                return;
            // tpd_<InputPort>_<OutputPort>[_<condition>][_<edge>]
            const auto generic = "tpd_" + ports[0].port.name + '_'
                + ports[1].port.name
                + (condition.empty() ? std::string { } : '_' + condition)
                + (ports[0].edge.empty() ? std::string { }
                                         : '_' + ports[0].edge);
            emit(target_path_, generic, std::move(values), increment_,
                node.span, { generic_port(ports[0].port),
                                generic_port(ports[1].port) },
                std::move(retain));
        }

        // 5.2.7.1.3: ( PORT port_instance delval_list ) -> tipd_<port>.
        void port_delay(const SdfSyntaxNode& node)
        {
            const SdfSyntaxAtom* atom = first_port_atom(node);
            if (atom == nullptr)
                return;
            const auto port = parse_port(atom->spelling);
            bool reported { };
            if (boundary_
                && !instance_of(port.instance, node.span, reported)) {
                return;
            }
            if (!usable(port, node.span))
                return;
            const auto instance = instance_of(port.instance, node.span, reported);
            if (!instance)
                return;
            std::vector<std::optional<std::int64_t>> values;
            if (!values_of(node, values))
                return;
            emit(*instance, "tipd_" + port.name, std::move(values), increment_,
                node.span, { generic_port(port) });
        }

        // 5.2.7.1.4: ( INTERCONNECT source destination delval_list ) ->
        // tipd_<destination port>. The annotation names its source, so that
        // a second source of the same generic makes the multiple source mode
        // of 5.2.7.1.5, which the elaborator applies.
        void interconnect(const SdfSyntaxNode& node)
        {
            std::vector<const SdfSyntaxAtom*> atoms;
            for (const auto& atom : node.atoms) {
                if (atom.kind == SdfTokenKind::Identifier
                    || atom.kind == SdfTokenKind::EscapedIdentifier) {
                    atoms.push_back(&atom);
                }
            }
            if (atoms.size() != 2U)
                return;
            const auto source = parse_port(atoms[0]->spelling);
            const auto destination = parse_port(atoms[1]->spelling);
            bool reported { };
            // In the cell of another language, only a destination that is a
            // VHDL instance's port is the VITAL map's; nothing else is
            // reported here.
            if (boundary_
                && !instance_of(destination.instance, node.span, reported)) {
                return;
            }
            if (!usable(destination, node.span) || source.escaped)
                return;
            const auto instance
                = instance_of(destination.instance, node.span, reported);
            if (!instance)
                return;
            std::vector<std::optional<std::int64_t>> values;
            if (!values_of(node, values))
                return;
            auto source_path = target_path_;
            if (!source.instance.empty()) {
                auto tail = source.instance;
                std::ranges::replace(tail, '/', '.');
                source_path += '.' + tail;
            }
            std::string source_port = source.name;
            if (source.indexed) {
                source_port += '[' + std::to_string(source.left);
                if (source.right != source.left)
                    source_port += ':' + std::to_string(source.right);
                source_port += ']';
            }
            emit(*instance, "tipd_" + destination.name, std::move(values),
                increment_, node.span, { generic_port(destination) }, { },
                std::move(source_path), std::move(source_port));
        }

        // 5.2.7.1.6: ( DEVICE [port_instance] delval_list ) ->
        // tdevice_<InstanceName>[_<OutputPort>].
        void device(const SdfSyntaxNode& node)
        {
            std::string generic = "tdevice_" + instance_name_;
            if (const auto* atom = first_port_atom(node)) {
                const auto port = parse_port(atom->spelling);
                if (!usable(port, node.span))
                    return;
                generic += '_' + port.name;
            }
            std::vector<std::optional<std::int64_t>> values;
            if (!values_of(node, values))
                return;
            emit(target_path_, generic, std::move(values), increment_, node.span);
        }

        [[nodiscard]] static const SdfSyntaxAtom* first_port_atom(
            const SdfSyntaxNode& node)
        {
            for (const auto& atom : node.atoms) {
                if (atom.kind == SdfTokenKind::Identifier
                    || atom.kind == SdfTokenKind::EscapedIdentifier) {
                    return &atom;
                }
            }
            return nullptr;
        }

        void delay(const SdfSyntaxNode& node)
        {
            for (const auto& child : node.children) {
                if (child.kind != SdfConstructKind::Absolute
                    && child.kind != SdfConstructKind::Increment) {
                    // PATHPULSE and GLOBALPATHPULSE are not supported by the
                    // VITAL SDF map and have no effect (5.2.7.1).
                    continue;
                }
                increment_ = child.kind == SdfConstructKind::Increment;
                for (const auto& definition : child.children) {
                    if (boundary_
                        && definition.kind != SdfConstructKind::Interconnect
                        && definition.kind != SdfConstructKind::Port) {
                        continue;
                    }
                    switch (definition.kind) {
                    case SdfConstructKind::Iopath:
                        iopath(definition, { });
                        break;
                    case SdfConstructKind::Conditional:
                        conditional_path(definition);
                        break;
                    case SdfConstructKind::Interconnect:
                        interconnect(definition);
                        break;
                    case SdfConstructKind::Port:
                        port_delay(definition);
                        break;
                    case SdfConstructKind::Device:
                        device(definition);
                        break;
                    default:
                        // NETDELAY, CONDELSE and the other definitions are
                        // not supported by the VITAL SDF map (5.2.7.1).
                        break;
                    }
                }
            }
            increment_ = false;
        }

        // A port_tchk: a port, a port with an edge, or a COND with either
        // (5.2.7.2).
        [[nodiscard]] std::optional<SpecPort> conditional_port(
            const SdfSyntaxNode& node)
        {
            const SdfSyntaxNode* edge = nullptr;
            for (const auto& child : node.children) {
                if (child.kind == SdfConstructKind::Edge)
                    edge = &child;
            }
            SpecPort result;
            result.offset = node.span.begin.offset;
            std::size_t stop { };
            if (edge != nullptr) {
                if (edge->atoms.empty())
                    return std::nullopt;
                result.port = parse_port(edge->atoms.front().spelling);
                result.edge = edge->keyword_spelling;
                stop = edge->first_token;
            } else {
                const SdfSyntaxAtom* port_atom = nullptr;
                for (const auto& atom : node.atoms) {
                    if (atom.kind == SdfTokenKind::Identifier
                        || atom.kind == SdfTokenKind::EscapedIdentifier) {
                        port_atom = &atom;
                    }
                }
                if (port_atom == nullptr)
                    return std::nullopt;
                result.port = parse_port(port_atom->spelling);
                // The port is the last token before the closing parenthesis.
                const auto& tokens = file_.tokens;
                auto last = node.first_token + node.token_count;
                if (last == 0U || last > tokens.size())
                    return std::nullopt;
                --last; // the closing parenthesis
                while (last > node.first_token
                    && tokens[last - 1U].kind == SdfTokenKind::Comment) {
                    --last;
                }
                stop = last - 1U;
            }
            const auto text = condition_text(node, stop);
            result.condition = sdf_vital_condition_suffix(text);
            if (result.condition.empty()) {
                error("FSIM-SDF-VITAL-001",
                    "SDF timing-check condition '" + text
                        + "' has no translation to a legal VHDL identifier "
                          "(IEEE 1076.4-2000 5.2.7.3.2)",
                    node.span);
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] bool check_ports(
            const SdfSyntaxNode& node, std::vector<SpecPort>& ports)
        {
            ports.clear();
            for (const auto& atom : node.atoms) {
                if (atom.kind != SdfTokenKind::Identifier
                    && atom.kind != SdfTokenKind::EscapedIdentifier) {
                    continue;
                }
                SpecPort port;
                port.port = parse_port(atom.spelling);
                port.offset = atom.span.begin.offset;
                ports.push_back(std::move(port));
            }
            for (const auto& child : node.children) {
                if (child.kind == SdfConstructKind::Edge && !child.atoms.empty()) {
                    SpecPort port;
                    port.port = parse_port(child.atoms.front().spelling);
                    port.edge = child.keyword_spelling;
                    port.offset = child.span.begin.offset;
                    ports.push_back(std::move(port));
                } else if (child.kind == SdfConstructKind::Conditional) {
                    auto port = conditional_port(child);
                    if (!port)
                        return false;
                    ports.push_back(std::move(*port));
                }
            }
            std::ranges::sort(ports, {}, &SpecPort::offset);
            return std::ranges::all_of(ports, [&](const SpecPort& port) {
                return usable(port.port, node.span);
            });
        }

        void timing_check(const SdfSyntaxNode& node)
        {
            for (const auto& check : node.children) {
                // The forward-annotation constraints and the SDF 3.0 timing
                // check forms outside the VITAL SDF map have no effect.
                const char* first = nullptr;
                const char* second = nullptr;
                switch (check.kind) {
                case SdfConstructKind::Setup:
                    first = "tsetup";
                    break;
                case SdfConstructKind::Hold:
                    first = "thold";
                    break;
                case SdfConstructKind::SetupHold:
                    first = "tsetup";
                    second = "thold";
                    break;
                case SdfConstructKind::Recovery:
                    first = "trecovery";
                    break;
                case SdfConstructKind::Removal:
                    first = "tremoval";
                    break;
                case SdfConstructKind::RecRem:
                    first = "trecovery";
                    second = "tremoval";
                    break;
                case SdfConstructKind::Skew:
                    first = "tskew";
                    break;
                case SdfConstructKind::BidirectSkew:
                    first = "tskew";
                    second = "tskew";
                    break;
                case SdfConstructKind::NoChange:
                    first = "tncsetup";
                    second = "tnchold";
                    break;
                case SdfConstructKind::Width:
                    first = "tpw";
                    break;
                case SdfConstructKind::Period:
                    first = "tperiod";
                    break;
                default:
                    continue;
                }
                const bool conditional_forms = std::ranges::any_of(check.children,
                    [](const SdfSyntaxNode& child) {
                        return child.kind == SdfConstructKind::StampCondition
                            || child.kind == SdfConstructKind::CheckCondition;
                    });
                if (conditional_forms)
                    continue;
                std::vector<SpecPort> ports;
                if (!check_ports(check, ports))
                    continue;
                std::vector<std::optional<std::int64_t>> values;
                if (!values_of(check, values) || values.empty())
                    continue;
                const bool single = check.kind == SdfConstructKind::Width
                    || check.kind == SdfConstructKind::Period;
                if (single) {
                    if (ports.size() != 1U)
                        continue;
                    // A single-port check: <SDFSimpleConditionAndOrEdge>.
                    const auto suffix = simple_suffix(ports[0]);
                    emit(target_path_,
                        std::string { first } + '_' + ports[0].port.name
                            + (suffix.empty() ? std::string { } : '_' + suffix),
                        { values.front() }, false, check.span,
                        { generic_port(ports[0].port) });
                    continue;
                }
                if (ports.size() != 2U || values.size() < (second != nullptr ? 2U : 1U))
                    continue;
                const auto names = [&](const char* prefix, const SpecPort& left,
                                       const SpecPort& right) {
                    return std::string { prefix } + '_' + left.port.name + '_'
                        + right.port.name + '_' + full_suffix(left, right);
                };
                emit(target_path_, names(first, ports[0], ports[1]),
                    { values[0] }, false, check.span,
                    { generic_port(ports[0].port), generic_port(ports[1].port) });
                if (second != nullptr) {
                    // BIDIRECTSKEW updates both skew generics between the
                    // ports; the second takes the ports in the other order.
                    const bool swap = check.kind == SdfConstructKind::BidirectSkew;
                    emit(target_path_,
                        swap ? names(second, ports[1], ports[0])
                             : names(second, ports[0], ports[1]),
                        { values[1] }, false, check.span,
                        swap ? std::vector<project::TimingGenericPort> {
                                   generic_port(ports[1].port),
                                   generic_port(ports[0].port) }
                             : std::vector<project::TimingGenericPort> {
                                   generic_port(ports[0].port),
                                   generic_port(ports[1].port) });
                }
            }
        }

        [[nodiscard]] static std::string simple_suffix(const SpecPort& port)
        {
            return join_nonempty({ port.condition, port.edge });
        }

        // <ConditionNameEdge>[_<SDFSimpleConditionAndOrEdge>]; the first port
        // without an edge contributes `noedge`.
        [[nodiscard]] static std::string full_suffix(
            const SpecPort& first, const SpecPort& second)
        {
            const auto head = join_nonempty({ first.condition,
                first.edge.empty() ? std::string_view { "noedge" }
                                   : std::string_view { first.edge } });
            return join_nonempty({ head, simple_suffix(second) });
        }

        const SdfFile& file_;
        const InstanceIndex& instances_;
        std::vector<Diagnostic>& diagnostics_;
        std::vector<project::TimingGenericAnnotation>& annotations_;
        SdfValuePolicy policy_;
        std::size_t selection_index_ { 1U };
        std::string target_path_;
        std::string instance_name_;
        bool increment_ { };
        bool boundary_ { };
    };

    // The cell selection of the SDF session (--sdf-cell): a pattern with `*`
    // and `?` against the instance path or the path below the scope root.
    [[nodiscard]] bool pattern_matches(
        const std::string_view pattern, const std::string_view name)
    {
        std::size_t pattern_index { };
        std::size_t name_index { };
        std::size_t star = std::string_view::npos;
        std::size_t retry { };
        while (name_index < name.size()) {
            if (pattern_index < pattern.size()
                && (pattern[pattern_index] == '?'
                    || pattern[pattern_index] == name[name_index])) {
                ++pattern_index;
                ++name_index;
            } else if (pattern_index < pattern.size()
                && pattern[pattern_index] == '*') {
                star = pattern_index++;
                retry = name_index;
            } else if (star != std::string_view::npos) {
                pattern_index = star + 1U;
                name_index = ++retry;
            } else {
                return false;
            }
        }
        while (pattern_index < pattern.size() && pattern[pattern_index] == '*')
            ++pattern_index;
        return pattern_index == pattern.size();
    }

    [[nodiscard]] bool cell_selected(const project::SdfInput& input,
        const std::string_view instance)
    {
        if (input.root != "*" && instance != input.root
            && !(instance.size() > input.root.size()
                && instance.starts_with(input.root)
                && instance[input.root.size()] == '.')) {
            return false;
        }
        const auto relative = input.root != "*"
                && instance.size() > input.root.size()
            ? instance.substr(input.root.size() + 1U)
            : instance;
        return pattern_matches(input.cell, instance)
            || pattern_matches(input.cell, relative);
    }

} // namespace

void add_sdf_vital_inputs(
    project::Config& config, const cli::Invocation& invocation)
{
    const auto request = make_cli_sdf_request(
        invocation, SdfControlPhase::Elaborate, config.run.delay_mode);
    config.project.sdf_inputs.clear();
    for (const auto& input : request.inputs) {
        config.project.sdf_inputs.push_back({ input.source_identity,
            input.root, input.cell_pattern, input.file_precedence,
            input.cell_precedence });
    }
    config.project.sdf_delay_mode
        = request.selection == SdfDelaySelection::Minimum
        ? project::DelayMode::minimum
        : request.selection == SdfDelaySelection::Maximum
            ? project::DelayMode::maximum
            : project::DelayMode::typical;
}

std::size_t remove_sdf_vital_targets(std::vector<SdfResolvedCell>& cells)
{
    std::size_t removed { };
    for (auto& cell : cells) {
        removed += static_cast<std::size_t>(std::erase_if(cell.targets,
            [](const SdfResolvedInstance& target) {
                return target.language == SdfScopeRootLanguage::Vhdl;
            }));
    }
    return removed;
}

bool plan_sdf_vital_timing_generics(
    const elaboration::ElaboratedDesign& design,
    const project::Config& config, diagnostic::Engine& diagnostics,
    std::vector<project::TimingGenericAnnotation>& annotations)
{
    annotations.clear();
    auto inputs = config.project.sdf_inputs;
    for (const auto& request : design.sdf_annotation_requests()) {
        project::SdfInput input;
        input.file = request.file;
        input.root = request.scope.empty() ? std::string { "*" } : request.scope;
        input.file_precedence = inputs.size();
        inputs.push_back(std::move(input));
    }
    if (inputs.empty())
        return true;
    std::ranges::stable_sort(inputs,
        [](const project::SdfInput& left, const project::SdfInput& right) {
            return std::tie(left.file_precedence, left.cell_precedence)
                < std::tie(right.file_precedence, right.cell_precedence);
        });
    InstanceIndex instances;
    for (const auto& specialization : design.specializations()) {
        auto key = ascii_lowercase(specialization.instance);
        if (specialization.language == frontend::Language::Vhdl2008)
            instances.vhdl.emplace(std::move(key), specialization.instance);
        else
            instances.other.insert(std::move(key));
    }
    const auto selection = config.project.sdf_delay_mode
            == project::DelayMode::minimum
        ? SdfDelaySelection::Minimum
        : config.project.sdf_delay_mode == project::DelayMode::maximum
            ? SdfDelaySelection::Maximum
            : SdfDelaySelection::Typical;
    std::vector<Diagnostic> reported;
    constexpr std::uintmax_t maximum_body_bytes = 64U << 20U;
    for (const auto& input : inputs) {
        // Files the session cannot read or parse, and scopes it cannot
        // resolve, are the session's errors.
        const auto body = application_detail::read_binary_payload(
            support::path_from_utf8(input.file), maximum_body_bytes);
        if (!body.bytes)
            continue;
        auto parsed = frontend::parse_sdf({ input.file, *body.bytes });
        if (!parsed.ok())
            continue;
        SdfAnnotationScopeRequest scope_request;
        scope_request.selection = input.root == "*"
            ? SdfScopeSelection::All
            : SdfScopeSelection::Single;
        std::vector<std::string> scope_path;
        auto scope_root = input.root;
        if (scope_request.selection == SdfScopeSelection::Single
            && std::ranges::find(design.roots(), scope_root)
                == design.roots().end()) {
            std::size_t start { };
            std::vector<std::string> pieces;
            for (auto dot = scope_root.find('.'); dot != std::string::npos;
                dot = scope_root.find('.', start)) {
                pieces.push_back(scope_root.substr(start, dot - start));
                start = dot + 1U;
            }
            if (!pieces.empty()) {
                pieces.push_back(scope_root.substr(start));
                scope_root = pieces.front();
                scope_path.assign(pieces.begin() + 1, pieces.end());
            }
        }
        if (scope_request.selection == SdfScopeSelection::Single)
            scope_request.root_aliases.push_back(scope_root);
        scope_request.expected_project_identity = "sdf-vital";
        scope_request.expected_design_identity = "sdf-vital";
        const auto scope = bind_sdf_annotation_scope(
            parsed.file, design, "sdf-vital", "sdf-vital", scope_request);
        if (!scope.ok())
            continue;
        const auto cells = resolve_sdf_cells(scope.scope, design, scope_path);
        if (!cells.ok())
            continue;
        Translator translator { parsed.file, instances, selection, reported,
            annotations };
        for (const auto& resolved : cells.resolution->cells()) {
            if (resolved.cell_id == 0U
                || resolved.cell_id > parsed.file.cells.size()) {
                continue;
            }
            const auto& cell = parsed.file.cells[resolved.cell_id - 1U];
            for (const auto& target : resolved.targets) {
                if (!cell_selected(input, target.instance_path))
                    continue;
                if (target.language == SdfScopeRootLanguage::Vhdl)
                    translator.annotate(cell, target);
                else
                    translator.annotate_boundary(cell, target);
            }
        }
    }
    for (const auto& item : reported)
        application_detail::import_diagnostic(diagnostics, item);
    return !std::ranges::any_of(reported, [](const Diagnostic& item) {
        return item.severity == DiagnosticSeverity::Error;
    });
}

} // namespace fsim::app
