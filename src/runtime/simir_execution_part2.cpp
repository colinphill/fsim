// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_execution_context.hpp"
#include "simir_execution_shared.hpp"
#include "simir_internal.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/path.hpp"

#include "simir_signal_attributes.hpp"
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>

#include <boost/multiprecision/cpp_int.hpp>

namespace fsim::runtime::simir {

namespace {

    struct VhdlAssertFormatField {
        char variable { };
        char alignment { };
        char fill { ' ' };
        std::size_t width { };
        std::string_view precision;
    };

    [[nodiscard]] bool parse_vhdl_assert_format_field(
        const std::string_view text, VhdlAssertFormatField& field)
    {
        if (text.empty()
            || (text.front() != 's' && text.front() != 'S'
                && text.front() != 'r' && text.front() != 't'
                && text.front() != 'i')) {
            return false;
        }
        field.variable = text.front();
        auto rest = text.substr(1U);
        if (!rest.empty() && rest.front() == '.') {
            if (field.variable != 't') {
                return false;
            }
            field.precision = rest.substr(1U);
            rest = { };
        } else if (!rest.empty()) {
            if (rest.front() != ':') {
                return false;
            }
            rest.remove_prefix(1U);
            if (rest.size() >= 2U
                && (rest[1] == '<' || rest[1] == '>' || rest[1] == '^')) {
                field.fill = rest.front();
                field.alignment = rest[1];
                rest.remove_prefix(2U);
            } else if (!rest.empty()
                && (rest.front() == '<' || rest.front() == '>'
                    || rest.front() == '^')) {
                field.alignment = rest.front();
                rest.remove_prefix(1U);
            }
            const auto dot = rest.find('.');
            const auto width = rest.substr(0U, dot);
            if (!width.empty()) {
                std::size_t parsed { };
                const auto converted = std::from_chars(
                    width.data(), width.data() + width.size(), parsed);
                if (converted.ec != std::errc { }
                    || converted.ptr != width.data() + width.size()
                    || parsed > maximum_string_bytes) {
                    return false;
                }
                field.width = parsed;
            }
            if (dot != std::string_view::npos) {
                if (field.variable != 't') {
                    return false;
                }
                field.precision = rest.substr(dot + 1U);
            }
            rest = { };
        }
        if (!rest.empty()) {
            return false;
        }
        if (!field.precision.empty()
            && field.precision != "fs" && field.precision != "ps"
            && field.precision != "ns" && field.precision != "us"
            && field.precision != "ms" && field.precision != "sec"
            && field.precision != "min" && field.precision != "hr") {
            return false;
        }
        if (field.alignment == 0) {
            field.alignment = field.variable == 't' ? '>' : '<';
        }
        return true;
    }

    [[nodiscard]] bool valid_vhdl_assert_format(
        const std::string_view format)
    {
        if (format.size() > maximum_string_bytes) {
            return false;
        }
        for (std::size_t index = 0U; index < format.size();) {
            if (format[index] == '}') {
                if (index + 1U < format.size()
                    && format[index + 1U] == '}') {
                    index += 2U;
                    continue;
                }
                return false;
            }
            if (format[index] != '{') {
                ++index;
                continue;
            }
            if (index + 1U < format.size() && format[index + 1U] == '{') {
                index += 2U;
                continue;
            }
            const auto end = format.find('}', index + 1U);
            if (end == std::string_view::npos) {
                return false;
            }
            VhdlAssertFormatField field;
            if (!parse_vhdl_assert_format_field(
                    format.substr(index + 1U, end - index - 1U), field)) {
                return false;
            }
            index = end + 1U;
        }
        return true;
    }

    [[nodiscard]] std::string vhdl_assert_time(
        const SimulationTick tick,
        const std::uint64_t resolution_femtoseconds,
        const std::string_view precision)
    {
        using boost::multiprecision::cpp_int;
        std::uint64_t unit = 1U;
        std::string_view suffix = precision;
        if (precision == "ps") unit = 1'000U;
        else if (precision == "ns") unit = 1'000'000U;
        else if (precision == "us") unit = 1'000'000'000U;
        else if (precision == "ms") unit = 1'000'000'000'000U;
        else if (precision == "sec") unit = 1'000'000'000'000'000U;
        else if (precision == "min") unit = 60'000'000'000'000'000U;
        else if (precision == "hr") unit = 3'600'000'000'000'000'000U;
        else if (precision.empty()) suffix = "fs";
        const cpp_int femtoseconds = cpp_int { tick }
            * resolution_femtoseconds;
        cpp_int rounded = femtoseconds / unit;
        if ((femtoseconds % unit) * 2 >= unit) {
            ++rounded;
        }
        return rounded.convert_to<std::string>() + " "
            + std::string { suffix };
    }

    [[nodiscard]] std::string aligned_vhdl_assert_field(
        std::string value, const VhdlAssertFormatField& field)
    {
        if (value.size() >= field.width) {
            return value;
        }
        const auto padding = field.width - value.size();
        if (field.alignment == '<') {
            value.append(padding, field.fill);
        } else if (field.alignment == '>') {
            value.insert(0U, padding, field.fill);
        } else {
            const auto left = padding / 2U;
            value.insert(0U, left, field.fill);
            value.append(padding - left, field.fill);
        }
        return value;
    }

    [[nodiscard]] std::string format_vhdl_assert_message(
        const std::string_view format,
        const std::string_view message,
        const AssertionSeverity severity,
        const std::string_view instance,
        const SimulationTick tick,
        const std::uint64_t resolution_femtoseconds)
    {
        std::string result;
        result.reserve(std::min<std::size_t>(
            maximum_string_bytes, format.size() + message.size()));
        for (std::size_t index = 0U; index < format.size();) {
            if (format[index] == '}' && index + 1U < format.size()
                && format[index + 1U] == '}') {
                result.push_back('}');
                index += 2U;
                continue;
            }
            if (format[index] != '{') {
                result.push_back(format[index++]);
                continue;
            }
            if (index + 1U < format.size() && format[index + 1U] == '{') {
                result.push_back('{');
                index += 2U;
                continue;
            }
            const auto end = format.find('}', index + 1U);
            VhdlAssertFormatField field;
            if (end == std::string_view::npos
                || !parse_vhdl_assert_format_field(
                    format.substr(index + 1U, end - index - 1U), field)) {
                throw std::logic_error { "invalid retained VHDL assert format" };
            }
            std::string value;
            if (field.variable == 'r') {
                value = message;
            } else if (field.variable == 'i') {
                value = instance;
            } else if (field.variable == 't') {
                value = vhdl_assert_time(
                    tick, resolution_femtoseconds, field.precision);
            } else {
                static constexpr std::array names {
                    std::string_view { "note" },
                    std::string_view { "warning" },
                    std::string_view { "error" },
                    std::string_view { "failure" } };
                value = names[static_cast<std::size_t>(severity)];
                if (field.variable == 'S') {
                    std::ranges::transform(value, value.begin(), [](char ch) {
                        return static_cast<char>(
                            ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
                    });
                }
            }
            value = aligned_vhdl_assert_field(std::move(value), field);
            if (value.size() > maximum_string_bytes
                || result.size() > maximum_string_bytes - value.size()) {
                throw std::length_error {
                    "formatted VHDL report exceeds the bounded string limit" };
            }
            result += value;
            index = end + 1U;
        }
        return result;
    }



} // namespace

void Interpreter::Impl::execute_vhdl_report(
    ProcessState& process,
    const InstructionIndex instruction,
    const std::string_view message,
    const AssertionSeverity severity,
    const SourceLocation& source,
    const bool standalone)
{
    const auto level = static_cast<std::size_t>(severity);
    if (level >= vhdl_assert_enabled.size()) {
        process.pc = instruction;
        fail(process, "VHDL report severity is invalid");
    }
    if (!vhdl_assert_enabled[level]) {
        return;
    }
    if (vhdl_assert_counts[level] != std::numeric_limits<std::uint64_t>::max()) {
        ++vhdl_assert_counts[level];
    }
    const auto formatted = format_vhdl_assert_message(
        vhdl_assert_formats[level], message, severity,
        process.program().name(), scheduler.now(),
        time_format.resolution_femtoseconds);
    if (report_hook) {
        report_hook(
            process.id, formatted, severity, source,
            scheduler.now(), scheduler.delta());
    }
    if (severity == AssertionSeverity::failure) {
        throw AssertionError(
            process.id, instruction,
            formatted.empty()
                ? (standalone ? "report failure" : "assertion failed")
                : formatted,
            severity, source, true);
    }
}

void Interpreter::Impl::execute_vhdl_assert_api(
    ProcessState& process, const VhdlAssertApi& operation)
{
    using Kind = VhdlAssertApiKind;
    const auto packed = [&](const RegisterId id, const std::size_t width)
        -> PackedLogic4 {
        return process.executor
            ? process.executor->read_register(id, width)
            : get_register(process, id);
    };
    const auto write_packed = [&](const RegisterId id, PackedLogic4 value) {
        write_process_register(process, id, value);
    };
    const auto string_value = [&](const StringRegisterId id) {
        return process.executor
            ? process.executor->read_string_register(id)
            : get_string_register(process, id);
    };
    const auto write_string = [&](const StringRegisterId id,
                                  const std::string_view value) {
        if (process.executor) {
            process.executor->write_string_register(id, value);
        } else {
            get_string_register(process, id) = value;
        }
    };
    const auto decode_level = [&](const RegisterId id) {
        const auto value = packed(id, 2U);
        if (value.width() != 2U || value.is_logic9()
            || value.low_word().bval != 0U
            || value.low_word().aval >= 4U) {
            fail(process, "VHDL assert API severity is not SEVERITY_LEVEL");
        }
        return static_cast<std::size_t>(value.low_word().aval);
    };
    const auto decode_boolean = [&](const RegisterId id) {
        const auto value = packed(id, 1U);
        if (value.width() != 1U || value.is_logic9()
            || value.low_word().bval != 0U) {
            fail(process, "VHDL assert API enable is not BOOLEAN");
        }
        return (value.low_word().aval & 1U) != 0U;
    };
    const auto no_other_operands = [&] {
        return !operation.string_destination && !operation.enable
            && !operation.format && !operation.valid;
    };

    if ((operation.kind == Kind::is_failed
            || operation.kind == Kind::get_count)
        && operation.destination && no_other_operands()) {
        std::uint64_t result { };
        if (operation.level) {
            result = vhdl_assert_counts[decode_level(*operation.level)];
        } else {
            for (std::size_t level = 1U;
                level < vhdl_assert_counts.size(); ++level) {
                const auto maximum = operation.kind == Kind::get_count
                    ? static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())
                    : std::uint64_t { 1U };
                result = vhdl_assert_counts[level] > maximum - result
                    ? maximum : result + vhdl_assert_counts[level];
            }
        }
        if (operation.kind == Kind::is_failed) {
            result = result != 0U ? 1U : 0U;
        } else {
            result = std::min<std::uint64_t>(result,
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()));
        }
        write_packed(*operation.destination,
            PackedLogic4::from_aval_bval(
                operation.kind == Kind::is_failed ? 1U : 64U,
                result, 0U));
        return;
    }
    if (operation.kind == Kind::clear && !operation.destination
        && !operation.string_destination && !operation.level
        && !operation.enable && !operation.format && !operation.valid) {
        vhdl_assert_counts.fill(0U);
        return;
    }
    if (operation.kind == Kind::set_enable && !operation.destination
        && !operation.string_destination && operation.enable
        && !operation.format && !operation.valid) {
        const auto enabled = decode_boolean(*operation.enable);
        if (operation.level) {
            vhdl_assert_enabled[decode_level(*operation.level)] = enabled;
        } else {
            vhdl_assert_enabled.fill(enabled);
        }
        return;
    }
    if (operation.kind == Kind::get_enable && operation.destination
        && operation.level && no_other_operands()) {
        const auto enabled = vhdl_assert_enabled[decode_level(*operation.level)];
        write_packed(*operation.destination,
            PackedLogic4::from_aval_bval(1U, enabled ? 1U : 0U, 0U));
        return;
    }
    if (operation.kind == Kind::set_format && !operation.destination
        && !operation.string_destination && operation.level
        && !operation.enable && operation.format) {
        const auto level = decode_level(*operation.level);
        const auto format = string_value(*operation.format);
        const auto valid = valid_vhdl_assert_format(format);
        if (operation.valid) {
            write_packed(*operation.valid,
                PackedLogic4::from_aval_bval(1U, valid ? 1U : 0U, 0U));
        }
        if (valid) {
            vhdl_assert_formats[level] = format;
        } else if (!operation.valid) {
            execute_vhdl_report(
                process, process.pc,
                "invalid VHDL assert format", AssertionSeverity::failure,
                operation.source, true);
        }
        return;
    }
    if (operation.kind == Kind::get_format && !operation.destination
        && operation.string_destination && operation.level
        && !operation.enable && !operation.format && !operation.valid) {
        write_string(*operation.string_destination,
            vhdl_assert_formats[decode_level(*operation.level)]);
        return;
    }
    if (operation.kind == Kind::set_read_severity && !operation.destination
        && !operation.string_destination && operation.level
        && !operation.enable && !operation.format && !operation.valid) {
        vhdl_read_severity = static_cast<AssertionSeverity>(
            decode_level(*operation.level));
        return;
    }
    if (operation.kind == Kind::get_read_severity && operation.destination
        && !operation.string_destination && !operation.level
        && no_other_operands()) {
        write_packed(*operation.destination,
            PackedLogic4::from_aval_bval(2U,
                static_cast<std::uint64_t>(vhdl_read_severity), 0U));
        return;
    }
    if (operation.kind == Kind::record_read_failure
        && !operation.destination && !operation.string_destination
        && !operation.level && !operation.enable && !operation.format
        && !operation.valid) {
        execute_vhdl_report(
            process, process.pc,
            "VHDL TextIO read did not convert the requested element",
            vhdl_read_severity, operation.source, false);
        return;
    }
    fail(process, "VHDL assert API operation is invalid");
}

void Interpreter::Impl::execute_vhdl_reflection_api(
    ProcessState& process, const VhdlReflectionApi& operation)
{
    using Kind = VhdlReflectionApiKind;
    constexpr std::size_t maximum_mirrors = 65'536U;
    const auto packed = [&](const RegisterId id, const std::size_t width) {
        return process.executor
            ? process.executor->read_register(id, width)
            : get_register(process, id);
    };
    const auto write_packed = [&](PackedLogic4 value) {
        if (!operation.destination || value.width() != operation.result_width) {
            fail(process, "VHDL reflection packed result is invalid");
        }
        value = coerce_value_kind(
            std::move(value),
            register_value_kind(process, *operation.destination));
        if (process.executor) {
            process.executor->write_register(*operation.destination, value);
        } else {
            get_register(process, *operation.destination) = std::move(value);
        }
    };
    const auto write_unsigned = [&](const std::uint64_t value) {
        write_packed(PackedLogic4::from_aval_bval(
            operation.result_width, value, 0U));
    };
    const auto write_string = [&](const std::string_view value) {
        if (!operation.string_destination
            || value.size() > maximum_string_bytes) {
            fail(process, "VHDL reflection string result is invalid");
        }
        if (process.executor) {
            process.executor->write_string_register(
                *operation.string_destination, value);
        } else {
            get_string_register(process, *operation.string_destination) = value;
        }
    };
    const auto string_argument = [&]() -> std::string {
        if (!operation.string_argument) {
            fail(process, "VHDL reflection string argument is missing");
        }
        return process.executor
            ? process.executor->read_string_register(*operation.string_argument)
            : get_string_register(process, *operation.string_argument);
    };
    const auto integer_argument = [&](const std::size_t index) -> std::int64_t {
        if (index >= operation.arguments.size()) {
            fail(process, "VHDL reflection integer argument is missing");
        }
        const auto value = packed(operation.arguments[index], 64U);
        const auto decoded = value.known_signed_value();
        if (!decoded) {
            fail(process, "VHDL reflection integer argument is not known");
        }
        return *decoded;
    };
    const auto create = [&](VhdlReflectionType type,
                            PackedLogic4 value,
                            const bool has_value,
                            std::optional<PackedLogic4> designated_value = { })
        -> std::uint32_t {
        if (vhdl_reflection_mirrors.size() >= maximum_mirrors) {
            fail(process, "VHDL reflection mirror limit is exhausted");
        }
        vhdl_reflection_mirrors.push_back(
            VhdlReflectionMirror { std::move(type), std::move(value), has_value,
                std::move(designated_value) });
        return static_cast<std::uint32_t>(vhdl_reflection_mirrors.size() - 1U);
    };
    const auto mirror = [&]() -> const VhdlReflectionMirror& {
        if (!operation.receiver) {
            fail(process, "VHDL reflection receiver is missing");
        }
        const auto value = packed(*operation.receiver, 32U);
        const auto handle = value.known_unsigned_value();
        if (!handle || *handle == 0U
            || *handle >= vhdl_reflection_mirrors.size()) {
            fail(process, "VHDL reflection receiver is null or stale");
        }
        return vhdl_reflection_mirrors[static_cast<std::size_t>(*handle)];
    };
    const auto range = [&](const VhdlReflectionMirror& value,
                           const std::size_t dimension = 0U)
        -> const VhdlReflectionRange& {
        if (dimension >= value.type.ranges.size()) {
            fail(process, "VHDL reflection dimension is outside the subtype");
        }
        return value.type.ranges[dimension];
    };
    const auto range_length = [](const VhdlReflectionRange& value) {
        const auto distance = value.left >= value.right
            ? static_cast<std::uint64_t>(value.left - value.right)
            : static_cast<std::uint64_t>(value.right - value.left);
        return distance + 1U;
    };
    const auto bound = [&](const VhdlReflectionRange& value) {
        switch (operation.kind) {
        case Kind::left: return value.left;
        case Kind::right: return value.right;
        case Kind::low: return std::min(value.left, value.right);
        case Kind::high: return std::max(value.left, value.right);
        default: fail(process, "VHDL reflection bound query is invalid");
        }
    };
    const auto require_value = [&](const VhdlReflectionMirror& value) {
        if (!value.has_value) {
            fail(process, "VHDL reflection value method used a subtype mirror");
        }
    };
    const auto child_handle = [&](const VhdlReflectionMirror& owner,
                                  const std::size_t index,
                                  const bool with_value) {
        if (index >= owner.type.children.size()) {
            fail(process, "VHDL reflection element is outside the subtype");
        }
        auto child = owner.type.children[index];
        auto value = PackedLogic4(child.packed_width, Logic4::zero);
        if (with_value) {
            require_value(owner);
            if (child.packed_width == 0U
                || child.lsb_offset > owner.value.width()
                || child.packed_width > owner.value.width() - child.lsb_offset) {
                fail(process, "VHDL reflection element has no executable snapshot");
            }
            value = owner.value.extract_bits(
                static_cast<std::size_t>(child.lsb_offset), child.packed_width);
        }
        return create(std::move(child), std::move(value), with_value);
    };

    if (operation.kind == Kind::create_subtype
        || operation.kind == Kind::create_value) {
        if (operation.receiver || operation.string_destination
            || !operation.destination || operation.result_width != 32U
            || (operation.kind == Kind::create_subtype
                && (operation.source || operation.access_heap))
            || (operation.kind == Kind::create_value
                && operation.type.packed_width != 0U && !operation.source)
            || (operation.access_heap
                && operation.type.type_class != VhdlReflectionClass::access)) {
            fail(process, "VHDL reflection mirror creation is invalid");
        }
        auto value = operation.source
            ? packed(*operation.source, operation.type.packed_width)
            : PackedLogic4(operation.type.packed_width, Logic4::zero);
        std::optional<PackedLogic4> designated_value;
        if (operation.access_heap) {
            const auto handle = value.known_unsigned_value();
            if (!handle) {
                fail(process, "reflected access value is not known");
            }
            if (*handle != 0U) {
                const auto& heap = process.executor
                    ? process.executor->read_container_register(
                        *operation.access_heap)
                    : read_container_register(process, *operation.access_heap);
                if (heap.keys.size() != heap.elements.size()) {
                    fail(process, "VHDL access heap snapshot is inconsistent");
                }
                const auto key = PackedLogic4::from_aval_bval(
                    value.width(), *handle, 0U);
                const auto found = std::ranges::find(heap.keys, key);
                if (found == heap.keys.end()) {
                    fail(process, "reflected access value is stale");
                }
                designated_value = heap.elements[static_cast<std::size_t>(
                    found - heap.keys.begin())];
            }
        }
        write_unsigned(create(operation.type, std::move(value),
            operation.kind == Kind::create_value,
            std::move(designated_value)));
        return;
    }

    const auto& owner = mirror();
    if (operation.kind == Kind::get_type_class) {
        write_unsigned(static_cast<std::uint64_t>(owner.type.type_class));
        return;
    }
    if (operation.kind == Kind::get_subtype_mirror) {
        write_unsigned(create(owner.type,
            PackedLogic4(owner.type.packed_width, Logic4::zero), false));
        return;
    }
    if (operation.kind == Kind::convert) {
        if (owner.type.type_class != operation.type.type_class) {
            fail(process, "VHDL reflection class conversion does not match the mirror");
        }
        const auto receiver_value = packed(*operation.receiver, 32U);
        write_packed(receiver_value);
        return;
    }
    if (operation.kind == Kind::convert_generic) {
        const auto receiver_value = packed(*operation.receiver, 32U);
        write_packed(receiver_value);
        return;
    }
    if (operation.kind == Kind::simple_name) {
        write_string(owner.type.simple_name);
        return;
    }
    if (operation.kind == Kind::enumeration_literal) {
        if (owner.type.type_class != VhdlReflectionClass::enumeration) {
            fail(process, "enumeration_literal requires an enumeration subtype mirror");
        }
        std::size_t index { };
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) {
                fail(process, "enumeration literal name is outside the subtype");
            }
            index = static_cast<std::size_t>(found - owner.type.names.begin());
        } else {
            const auto requested = integer_argument(0U);
            if (requested < 0
                || static_cast<std::uint64_t>(requested) >= owner.type.names.size()) {
                fail(process, "enumeration literal index is outside the subtype");
            }
            index = static_cast<std::size_t>(requested);
        }
        auto type = owner.type;
        write_unsigned(create(std::move(type),
            PackedLogic4::from_aval_bval(owner.type.packed_width, index, 0U), true));
        return;
    }
    if (operation.kind == Kind::pos) {
        require_value(owner);
        const auto value = owner.value.known_unsigned_value();
        if (!value) fail(process, "reflected enumeration value is not known");
        write_unsigned(*value);
        return;
    }
    if (operation.kind == Kind::image) {
        require_value(owner);
        if (owner.type.type_class == VhdlReflectionClass::enumeration) {
            const auto ordinal = owner.value.known_unsigned_value();
            if (!ordinal || *ordinal >= owner.type.names.size()) {
                fail(process, "reflected enumeration value is outside the subtype");
            }
            write_string(owner.type.names[static_cast<std::size_t>(*ordinal)]);
        } else if (owner.type.type_class == VhdlReflectionClass::floating) {
            const auto bits = owner.value.known_unsigned_value();
            if (!bits || owner.value.width() != 64U) {
                fail(process, "reflected floating value is invalid");
            }
            write_string(std::to_string(std::bit_cast<double>(*bits)));
        } else {
            std::optional<std::int64_t> value;
            if (owner.type.signed_value) {
                value = owner.value.known_signed_value();
            } else if (const auto unsigned_value
                    = owner.value.known_unsigned_value();
                unsigned_value
                && *unsigned_value
                    <= static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                value = static_cast<std::int64_t>(*unsigned_value);
            }
            if (!value) fail(process, "reflected scalar value is not known");
            auto text = std::to_string(*value);
            if (owner.type.type_class == VhdlReflectionClass::physical
                && !owner.type.names.empty()) {
                text += " " + owner.type.names.front();
            }
            write_string(text);
        }
        return;
    }
    if (operation.kind == Kind::left || operation.kind == Kind::right
        || operation.kind == Kind::low || operation.kind == Kind::high) {
        const auto dimension = owner.type.type_class == VhdlReflectionClass::array
            && !operation.arguments.empty()
            ? static_cast<std::size_t>(integer_argument(0U) - 1)
            : 0U;
        const auto result = bound(range(owner, dimension));
        if (owner.type.type_class == VhdlReflectionClass::array) {
            write_unsigned(static_cast<std::uint64_t>(result));
        } else {
            auto type = owner.type;
            write_unsigned(create(std::move(type),
                PackedLogic4::from_aval_bval(owner.type.packed_width,
                    static_cast<std::uint64_t>(result), 0U), true));
        }
        return;
    }
    if (operation.kind == Kind::length) {
        if (owner.type.type_class == VhdlReflectionClass::record) {
            if (owner.type.names.size() != owner.type.children.size()) {
                fail(process, "record reflection descriptor is inconsistent");
            }
            write_unsigned(owner.type.children.size());
            return;
        }
        const auto dimension = owner.type.type_class == VhdlReflectionClass::array
            && !operation.arguments.empty()
            ? static_cast<std::size_t>(integer_argument(0U) - 1)
            : 0U;
        write_unsigned(range_length(range(owner, dimension)));
        return;
    }
    if (operation.kind == Kind::ascending) {
        const auto dimension = owner.type.type_class == VhdlReflectionClass::array
            && !operation.arguments.empty()
            ? static_cast<std::size_t>(integer_argument(0U) - 1)
            : 0U;
        write_unsigned(range(owner, dimension).ascending ? 1U : 0U);
        return;
    }
    if (operation.kind == Kind::value) {
        require_value(owner);
        auto value = resize_class_value(owner.value, operation.result_width);
        if (owner.type.signed_value && operation.result_width > owner.value.width()
            && owner.value.width() != 0U) {
            const auto extension = owner.value.get(owner.value.width() - 1U);
            for (std::size_t bit = owner.value.width();
                 bit < operation.result_width; ++bit) {
                value.set(bit, extension);
            }
        }
        write_packed(std::move(value));
        return;
    }
    if (operation.kind == Kind::units_length) {
        write_unsigned(owner.type.names.size());
        return;
    }
    if (operation.kind == Kind::unit_name) {
        const auto index = integer_argument(0U);
        if (index < 0 || static_cast<std::uint64_t>(index) >= owner.type.names.size())
            fail(process, "physical unit index is outside the subtype");
        write_string(owner.type.names[static_cast<std::size_t>(index)]);
        return;
    }
    if (operation.kind == Kind::unit_index) {
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) fail(process, "physical unit name is unknown");
            write_unsigned(static_cast<std::uint64_t>(found - owner.type.names.begin()));
        } else {
            require_value(owner);
            const auto value = owner.value.known_signed_value();
            if (!value || owner.type.scales.empty()) {
                fail(process, "reflected physical value has no unit");
            }
            std::size_t selected = 0U;
            for (std::size_t index = 0U; index < owner.type.scales.size(); ++index) {
                if (owner.type.scales[index] != 0U
                    && *value % static_cast<std::int64_t>(
                        owner.type.scales[index]) == 0
                    && owner.type.scales[index]
                        >= owner.type.scales[selected]) {
                    selected = index;
                }
            }
            write_unsigned(selected);
        }
        return;
    }
    if (operation.kind == Kind::scale) {
        std::size_t index { };
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) fail(process, "physical unit name is unknown");
            index = static_cast<std::size_t>(found - owner.type.names.begin());
        } else {
            const auto requested = integer_argument(0U);
            if (requested < 0) fail(process, "physical unit index is negative");
            index = static_cast<std::size_t>(requested);
        }
        if (index >= owner.type.scales.size()) fail(process, "physical unit index is outside the subtype");
        write_unsigned(owner.type.scales[index]);
        return;
    }
    if (operation.kind == Kind::record_element_name) {
        const auto index = integer_argument(0U);
        if (index < 0 || static_cast<std::uint64_t>(index) >= owner.type.names.size())
            fail(process, "record element index is outside the subtype");
        write_string(owner.type.names[static_cast<std::size_t>(index)]);
        return;
    }
    if (operation.kind == Kind::record_element_index) {
        const auto name = string_argument();
        const auto found = std::ranges::find(owner.type.names, name);
        if (found == owner.type.names.end()) fail(process, "record element name is unknown");
        write_unsigned(static_cast<std::uint64_t>(found - owner.type.names.begin()));
        return;
    }
    if (operation.kind == Kind::record_element_subtype
        || (operation.kind == Kind::aggregate_get
            && owner.type.type_class == VhdlReflectionClass::record)) {
        std::size_t index { };
        if (operation.string_argument) {
            const auto name = string_argument();
            const auto found = std::ranges::find(owner.type.names, name);
            if (found == owner.type.names.end()) fail(process, "record element name is unknown");
            index = static_cast<std::size_t>(found - owner.type.names.begin());
        } else {
            const auto requested = integer_argument(0U);
            if (requested < 0) fail(process, "aggregate element index is negative");
            index = static_cast<std::size_t>(requested);
        }
        write_unsigned(child_handle(owner, index,
            operation.kind == Kind::aggregate_get));
        return;
    }
    if (operation.kind == Kind::aggregate_get
        && owner.type.type_class == VhdlReflectionClass::array) {
        require_value(owner);
        if (owner.type.children.size() != 1U
            || operation.arguments.size() != owner.type.ranges.size()) {
            fail(process, "array reflection index arity does not match its dimensions");
        }
        const auto& element = owner.type.children.front();
        std::uint64_t bit_offset = 0U;
        std::uint64_t stride = element.packed_width;
        for (std::size_t dimension = owner.type.ranges.size();
             dimension-- > 0U;) {
            const auto& bounds = owner.type.ranges[dimension];
            const auto index = integer_argument(dimension);
            const auto low = std::min(bounds.left, bounds.right);
            const auto high = std::max(bounds.left, bounds.right);
            if (index < low || index > high) {
                fail(process, "array reflection index is outside the subtype");
            }
            const auto ordinal = index >= bounds.right
                ? static_cast<std::uint64_t>(index - bounds.right)
                : static_cast<std::uint64_t>(bounds.right - index);
            if (ordinal != 0U
                && stride > std::numeric_limits<std::uint64_t>::max()
                    / ordinal) {
                fail(process, "array reflection offset overflows");
            }
            const auto contribution = ordinal * stride;
            if (contribution
                > std::numeric_limits<std::uint64_t>::max() - bit_offset) {
                fail(process, "array reflection offset overflows");
            }
            bit_offset += contribution;
            const auto count = range_length(bounds);
            if (count != 0U && dimension != 0U
                && stride > std::numeric_limits<std::uint64_t>::max() / count) {
                fail(process, "array reflection stride overflows");
            }
            stride *= count;
        }
        if (element.packed_width == 0U
            || bit_offset > owner.value.width()
            || element.packed_width > owner.value.width() - bit_offset) {
            fail(process, "array reflection element has no executable snapshot");
        }
        auto element_type = element;
        element_type.lsb_offset = 0U;
        write_unsigned(create(std::move(element_type),
            owner.value.extract_bits(static_cast<std::size_t>(bit_offset),
                element.packed_width), true));
        return;
    }
    if (operation.kind == Kind::dimensions) {
        write_unsigned(owner.type.ranges.size());
        return;
    }
    if (operation.kind == Kind::array_index_subtype) {
        const auto dimension = operation.arguments.empty()
            ? 0U : static_cast<std::size_t>(integer_argument(0U) - 1);
        if (dimension >= owner.type.ranges.size()) fail(process, "array dimension is outside the subtype");
        VhdlReflectionType index_type;
        index_type.type_class = VhdlReflectionClass::integer;
        index_type.simple_name = "index";
        index_type.packed_width = 64U;
        index_type.signed_value = true;
        index_type.ranges.push_back(owner.type.ranges[dimension]);
        write_unsigned(create(std::move(index_type), PackedLogic4(64U, Logic4::zero), false));
        return;
    }
    if (operation.kind == Kind::array_element_subtype) {
        write_unsigned(child_handle(owner, 0U, false));
        return;
    }
    if (operation.kind == Kind::is_null) {
        require_value(owner);
        const auto value = owner.value.known_unsigned_value();
        if (!value) fail(process, "reflected access value is not known");
        write_unsigned(*value == 0U ? 1U : 0U);
        return;
    }
    if (operation.kind == Kind::designated_subtype) {
        write_unsigned(child_handle(owner, 0U, false));
        return;
    }
    if (operation.kind == Kind::file_logical_name
        || operation.kind == Kind::file_open_kind) {
        require_value(owner);
        const auto handle = owner.value.known_unsigned_value();
        if (!handle) fail(process, "reflected file handle is not known");
        const auto found = files.find(static_cast<FileHandle>(*handle));
        if (found == files.end() || found->second.closed) {
            fail(process, "reflected file is not open");
        }
        if (operation.kind == Kind::file_logical_name) {
            write_string(found->second.logical_name);
        } else {
            write_unsigned(found->second.mode.starts_with("r") ? 0U
                : found->second.mode.starts_with("w") ? 1U : 2U);
        }
        return;
    }
    if (operation.kind == Kind::access_get) {
        require_value(owner);
        if (owner.type.type_class != VhdlReflectionClass::access
            || owner.type.children.size() != 1U) {
            fail(process, "access reflection requires one designated subtype");
        }
        const auto handle = owner.value.known_unsigned_value();
        if (!handle || *handle == 0U || !owner.designated_value) {
            fail(process, "cannot reflect the designated value of null access");
        }
        auto designated_type = owner.type.children.front();
        write_unsigned(create(std::move(designated_type),
            *owner.designated_value, true));
        return;
    }
    fail(process, "VHDL reflection API operation is invalid");
}

SchedulerBatchResult Interpreter::Impl::execute(
    Scheduler& runtime,
    const std::span<const std::uint64_t> cohort_ids)
{
    if (!cohort_ids.empty()
        && (cohort_ids.front() & systemverilog_wave_payload) != 0U) {
        return execute_systemverilog_wave(cohort_ids);
    }
    SchedulerBatchResult result;
    const auto phase_revision = runtime.current_phase_revision();
    std::size_t batch_index { };
    while (batch_index < cohort_ids.size()) {
        if ((cohort_ids[batch_index]
                & generic_projected_region_payload) != 0U) {
            auto region = execute_generic_projected_region(
                cohort_ids.subspan(batch_index), batch_index);
            if (region.failure) {
                // Preserve a prefix whose deferred Update rows were secured.
                // The scheduler must not replay those publications.
                result.executed += region.executed;
                result.failure = region.failure;
                break;
            }
            if (region.executed == 0U) {
                break;
            }
            result.executed += region.executed;
            batch_index += region.executed;
            if (runtime.stop_requested()
                || runtime.current_phase_revision() != phase_revision) {
                break;
            }
            continue;
        }
        std::size_t offered_tasks { };
        try {
            if (const auto completed = try_execute_fused_static_cohort(
                    cohort_ids.subspan(batch_index), offered_tasks)) {
                result.executed += *completed;
                batch_index += *completed;
                if (runtime.stop_requested()
                    || runtime.current_phase_revision()
                        != phase_revision) {
                    break;
                }
                continue;
            }
        } catch (...) {
            result.failure = std::current_exception();
            // A prepared executor sets offered_tasks only after accepting a
            // task prefix. Zero means admission/staging failed before any
            // task was accepted, so leave the scheduler's whole suffix
            // available for retry.
            result.executed += offered_tasks;
            break;
        }
        const auto raw_cohort = cohort_ids[batch_index];
        if (raw_cohort >= static_sensitivity_cohorts.size()) {
            result.failure = std::make_exception_ptr(
                std::logic_error(
                    "scheduler batch references an invalid static cohort"));
            ++result.executed;
            break;
        }
        try {
            execute_queued_static_cohort(
                static_cast<std::size_t>(raw_cohort));
        } catch (...) {
            result.failure = std::current_exception();
        }
        ++result.executed;
        ++batch_index;
        if (result.failure || runtime.stop_requested()
            || runtime.current_phase_revision() != phase_revision) {
            break;
        }
    }
    return result;
}
} // namespace fsim::runtime::simir
