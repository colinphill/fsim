// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/dpi_c_type.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

namespace {

std::optional<std::uint64_t> dpi_decimal_token(
    const std::string_view text)
{
    std::uint64_t value { };
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    return result.ec == std::errc { }
            && result.ptr == text.data() + text.size()
        ? std::optional<std::uint64_t> { value }
        : std::nullopt;
}

}  // namespace

std::optional<DpiCType> dpi_c_type(
    const std::span<const semantic::sv::SourceToken> tokens)
{
    DpiCType result;
    std::string_view base;
    bool explicit_signed { };
    bool explicit_unsigned { };
    std::size_t packed_width = 1U;
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
        const auto text = std::string_view { tokens[index].text };
        if (text == "signed") {
            explicit_signed = true;
            continue;
        }
        if (text == "unsigned") {
            explicit_unsigned = true;
            continue;
        }
        if (text == "[") {
            if (index + 4U >= tokens.size()
                || tokens[index + 2U].text != ":"
                || tokens[index + 4U].text != "]") {
                return std::nullopt;
            }
            const auto left = dpi_decimal_token(tokens[index + 1U].text);
            const auto right = dpi_decimal_token(tokens[index + 3U].text);
            if (!left || !right) {
                return std::nullopt;
            }
            const auto extent = *left >= *right
                ? *left - *right + 1U
                : *right - *left + 1U;
            if (extent == 0U
                || extent > std::numeric_limits<std::size_t>::max()
                || packed_width
                    > std::numeric_limits<std::size_t>::max()
                        / static_cast<std::size_t>(extent)) {
                return std::nullopt;
            }
            packed_width *= static_cast<std::size_t>(extent);
            index += 4U;
            continue;
        }
        if (text == "bit" || text == "logic" || text == "reg"
            || text == "byte" || text == "shortint" || text == "int"
            || text == "integer" || text == "longint" || text == "time"
            || text == "chandle" || text == "shortreal"
            || text == "real" || text == "realtime" || text == "string") {
            if (!base.empty()) {
                return std::nullopt;
            }
            base = text;
            continue;
        }
        return std::nullopt;
    }
    if (base.empty() || (explicit_signed && explicit_unsigned)) {
        return std::nullopt;
    }
    result.string = base == "string";
    if (result.string) {
        result.code = 'z';
        return packed_width == 1U && !explicit_signed
                && !explicit_unsigned
            ? std::optional<DpiCType> { result }
            : std::nullopt;
    }
    const bool ranged = packed_width != 1U
        || std::ranges::any_of(tokens, [](const auto& token) {
               return token.text == "[";
           });
    if (ranged && base != "bit" && base != "logic" && base != "reg") {
        return std::nullopt;
    }
    result.code = base == "byte" ? 'c'
        : base == "shortint"     ? 's'
        : base == "int"          ? 'i'
        : base == "longint"      ? 'l'
        : base == "real" || base == "realtime" ? 'd'
        : base == "shortreal"    ? 'f'
        : base == "chandle"      ? 'p'
        : base == "integer" || base == "time" ? 'V'
        : base == "bit"          ? (ranged ? 'B' : 'b')
                                 : (ranged ? 'V' : 'g');
    if (explicit_unsigned
        && (result.code == 'c' || result.code == 's' || result.code == 'i'
            || result.code == 'l')) {
        result.code = static_cast<char>(result.code - 'a' + 'A');
    }
    result.scalar = result.code == 'd' ? frontend::SystemVerilogScalarKind::Real
        : result.code == 'f' ? frontend::SystemVerilogScalarKind::ShortReal
        : result.code == 'p' ? frontend::SystemVerilogScalarKind::Chandle
                             : frontend::SystemVerilogScalarKind::None;
    const auto base_width = base == "byte" ? 8U
        : base == "shortint" ? 16U
        : base == "int" || base == "integer" || base == "shortreal"
            ? 32U
        : base == "longint" || base == "time" || base == "chandle"
                || base == "real" || base == "realtime"
            ? 64U
            : 1U;
    if (packed_width > std::numeric_limits<std::size_t>::max()
            / base_width) {
        return std::nullopt;
    }
    result.width = packed_width * base_width;
    result.domain = base == "logic" || base == "reg"
            || base == "integer" || base == "time"
        ? frontend::ValueDomain::Logic4
        : base == "byte" || base == "shortint" || base == "int"
                || base == "longint"
            ? frontend::ValueDomain::Integer
            : frontend::ValueDomain::Bit2;
    const auto implicitly_signed = base == "byte" || base == "shortint"
        || base == "int" || base == "integer" || base == "longint";
    result.signed_value = explicit_signed
        || (!explicit_unsigned && implicitly_signed);
    return result;
}

namespace {

constexpr std::size_t dpi_type_depth_limit = 64U;

bool dpi_builtin_keyword(const std::string_view text)
{
    return text == "bit" || text == "logic" || text == "reg"
        || text == "byte" || text == "shortint" || text == "int"
        || text == "integer" || text == "longint" || text == "time"
        || text == "chandle" || text == "shortreal" || text == "real"
        || text == "realtime" || text == "string";
}

semantic::sv::SourceToken dpi_token(std::string text)
{
    semantic::sv::SourceToken token;
    token.text = std::move(text);
    return token;
}

std::optional<std::uint64_t> dpi_range_extent(
    const semantic::sv::PackedRange& range, const DpiTypeContext& context)
{
    const auto bound = [&](const std::optional<std::int64_t> value,
                           const std::optional<semantic::ExpressionId>
                               expression) -> std::optional<std::int64_t> {
        if (value) {
            return value;
        }
        return expression && context.evaluate
            ? context.evaluate(*expression)
            : std::nullopt;
    };
    const auto left = bound(range.left, range.left_expression);
    const auto right = bound(range.right, range.right_expression);
    if (!left || !right) {
        return std::nullopt;
    }
    return *left >= *right
        ? static_cast<std::uint64_t>(*left - *right) + 1U
        : static_cast<std::uint64_t>(*right - *left) + 1U;
}

// The extent of a reference's packed dimensions: 1 when it has none.
std::optional<std::uint64_t> dpi_packed_extent(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context)
{
    std::uint64_t extent = 1U;
    if (type.packed_dimensions.size() > 1U) {
        for (const auto& dimension : type.packed_dimensions) {
            const auto count = dpi_range_extent(dimension, context);
            if (!count || *count == 0U
                || extent > (std::uint64_t { 1 } << 32U) / *count) {
                return std::nullopt;
            }
            extent *= *count;
        }
        return extent;
    }
    if (type.packed_range) {
        return dpi_range_extent(*type.packed_range, context);
    }
    return extent;
}

std::optional<DpiCType> dpi_reference_c_type_impl(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    std::size_t depth);

// FSIM_DPI_TYPE_TRACE=1 names each reference the mapping rejects.
std::optional<DpiCType> dpi_reference_c_type(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    const std::size_t depth)
{
    auto result = dpi_reference_c_type_impl(type, context, depth);
    static const bool trace = std::getenv("FSIM_DPI_TYPE_TRACE") != nullptr;
    if (trace) {
        std::cerr << "[fsim-dpi-type] depth=" << depth << " spelling='"
                  << type.target.spelling << "' target="
                  << (type.target.target.valid()
                             ? std::to_string(type.target.target.value())
                             : std::string { "-" })
                  << " packed_range="
                  << (type.packed_range
                             ? "[" + (type.packed_range->left
                                          ? std::to_string(*type.packed_range->left)
                                          : std::string { "e" })
                                 + ":"
                                 + (type.packed_range->right
                                         ? std::to_string(*type.packed_range->right)
                                         : std::string { "e" })
                                 + "]"
                             : std::string { "-" })
                  << " dims=" << type.packed_dimensions.size()
                  << " -> " << (result ? std::string(1, result->code) + ":"
                                       + std::to_string(result->width)
                                       : std::string { "none" })
                  << '\n';
    }
    return result;
}

std::optional<DpiCType> dpi_reference_c_type_impl(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context,
    const std::size_t depth)
{
    if (depth > dpi_type_depth_limit || type.container_form
        || !type.unpacked_dimensions.empty() || type.virtual_interface
        || !type.class_identity.empty()) {
        return std::nullopt;
    }
    std::vector<semantic::sv::SourceToken> tokens;
    for (std::size_t start = 0U; start < type.target.spelling.size();) {
        const auto end = type.target.spelling.find(' ', start);
        const auto word = type.target.spelling.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (!word.empty()) {
            tokens.push_back(dpi_token(word));
        }
        start = end == std::string::npos ? type.target.spelling.size()
                                         : end + 1U;
    }
    if (!tokens.empty() && dpi_builtin_keyword(tokens.front().text)) {
        // A built-in type keeps its keyword, its signedness and its
        // packed dimensions, flattened to one range; the dimensions come
        // from the reference, not from the spelling.
        std::erase_if(tokens, [](const auto& token) {
            return !dpi_builtin_keyword(token.text) && token.text != "signed"
                && token.text != "unsigned";
        });
        const auto base = tokens.front().text;
        const bool integral = base != "chandle" && base != "shortreal"
            && base != "real" && base != "realtime" && base != "string";
        const bool explicit_sign = std::ranges::any_of(tokens,
            [](const auto& token) {
                return token.text == "signed" || token.text == "unsigned";
            });
        if (integral && !explicit_sign) {
            tokens.push_back(dpi_token(
                type.signed_value ? "signed" : "unsigned"));
        }
        // An integer atom's own width is no packed dimension.
        const bool vector_base
            = base == "bit" || base == "logic" || base == "reg";
        const auto extent = vector_base ? dpi_packed_extent(type, context)
                                        : std::optional<std::uint64_t> { 1U };
        if (!extent || *extent == 0U) {
            return std::nullopt;
        }
        if (vector_base
            && (type.packed_range || type.packed_dimensions.size() > 1U)) {
            tokens.push_back(dpi_token("["));
            tokens.push_back(dpi_token(std::to_string(*extent - 1U)));
            tokens.push_back(dpi_token(":"));
            tokens.push_back(dpi_token("0"));
            tokens.push_back(dpi_token("]"));
        }
        return dpi_c_type(tokens);
    }
    if (!type.target.target.valid() || !context.definition) {
        return std::nullopt;
    }
    const auto* const definition = context.definition(type.target.target);
    if (definition == nullptr) {
        return std::nullopt;
    }
    using Form = semantic::sv::TypeForm;
    std::optional<DpiCType> element;
    if (definition->form == Form::packed_structure
        || definition->form == Form::packed_union) {
        // A packed structure or union is a vector of its members' bits;
        // any 4-state member makes it a logic vector (H.7.6).
        if (definition->members.empty()) {
            return std::nullopt;
        }
        DpiCType aggregate;
        bool four_state { };
        for (const auto& member : definition->members) {
            const auto member_type
                = dpi_reference_c_type(member.type, context, depth + 1U);
            if (!member_type || member_type->string
                || member_type->scalar
                    != frontend::SystemVerilogScalarKind::None) {
                return std::nullopt;
            }
            four_state = four_state
                || member_type->domain == frontend::ValueDomain::Logic4;
            aggregate.width = definition->form == Form::packed_structure
                ? aggregate.width + member_type->width
                : std::max(aggregate.width, member_type->width);
        }
        aggregate.code = four_state ? 'V' : 'B';
        aggregate.domain = four_state ? frontend::ValueDomain::Logic4
                                      : frontend::ValueDomain::Bit2;
        element = aggregate;
    } else if (definition->form == Form::string) {
        DpiCType text;
        text.code = 'z';
        text.string = true;
        element = text;
    } else if (definition->form != Form::unpacked_structure
        && definition->form != Form::unpacked_union
        && definition->form != Form::tagged_union
        && definition->form != Form::class_handle
        && definition->form != Form::dynamic_array
        && definition->form != Form::queue
        && definition->form != Form::associative_array
        && definition->form != Form::static_array
        && definition->base != type) {
        // A typedef, enumeration or type parameter maps like its base.
        element = dpi_reference_c_type(definition->base, context, depth + 1U);
    }
    if (!element || (!type.packed_range && type.packed_dimensions.size() < 2U)) {
        return element;
    }
    // A named packed type's reference spells its flattened width: its own
    // `[W-1:0]`, or for a packed array of it the outer dimensions followed
    // by that range, so the extent is the whole vector's width.
    const auto extent = dpi_packed_extent(type, context);
    if (!extent || *extent == 0U || element->string
        || element->scalar != frontend::SystemVerilogScalarKind::None
        || element->width == 0U || *extent % element->width != 0U) {
        return std::nullopt;
    }
    if (*extent == element->width) {
        return element;
    }
    element->width = static_cast<std::size_t>(*extent);
    const bool four_state = element->domain == frontend::ValueDomain::Logic4;
    element->code = four_state ? 'V' : 'B';
    element->domain = four_state ? frontend::ValueDomain::Logic4
                                 : frontend::ValueDomain::Bit2;
    element->signed_value = false;
    return element;
}

}  // namespace

std::optional<DpiCType> dpi_c_type(
    const std::span<const semantic::sv::SourceToken> tokens,
    const DpiTypeContext& context)
{
    if (auto result = dpi_c_type(tokens)) {
        return result;
    }
    // A typedef name, possibly package-qualified (`pkg::name_t`).
    if (tokens.empty() || !context.named_type) {
        return std::nullopt;
    }
    for (std::size_t index = 1U; index < tokens.size(); index += 2U) {
        if (tokens[index].text != "::" || index + 1U >= tokens.size()) {
            return std::nullopt;
        }
    }
    const auto type = context.named_type(tokens.back().text);
    return type ? dpi_reference_c_type(*type, context, 0U) : std::nullopt;
}

std::optional<DpiCType> dpi_c_type(
    const semantic::sv::TypeReference& type, const DpiTypeContext& context)
{
    return dpi_reference_c_type(type, context, 0U);
}

DpiTypeContext dpi_hir_type_context(
    const semantic::sv::Hir& hir, const semantic::ScopeId scope)
{
    DpiTypeContext context;
    context.named_type = [&hir, scope](const std::string_view name)
        -> std::optional<semantic::sv::TypeReference> {
        const semantic::sv::Declaration* found { };
        std::size_t matches { };
        for (const auto& declaration : hir.declarations()) {
            if (declaration.form
                    != semantic::sv::DeclarationForm::typedef_declaration
                || declaration.name != name || !declaration.type) {
                continue;
            }
            if (declaration.scope == scope) {
                return declaration.type;
            }
            found = &declaration;
            ++matches;
        }
        return matches == 1U ? found->type : std::nullopt;
    };
    context.definition = [&hir](const semantic::TypeId type) {
        return hir.find_type(type);
    };
    return context;
}

namespace {

std::string dpi_c_scalar(const char code)
{
    switch (code) {
    case 'c':
        return "char";
    case 'C':
        return "unsigned char";
    case 's':
        return "short";
    case 'S':
        return "unsigned short";
    case 'i':
        return "int";
    case 'I':
        return "unsigned int";
    case 'l':
        return "long long";
    case 'L':
        return "unsigned long long";
    case 'd':
        return "double";
    case 'f':
        return "float";
    case 'p':
        return "void*";
    case 'z':
        return "const char*";
    case 'b':
        return "svBit";
    case 'g':
        return "svLogic";
    default:
        return { };
    }
}

// A formal's C parameter type (IEEE 1800-2023 H.7.4, H.8.7 to H.8.10,
// H.12): inputs pass small values by value and vectors by const pointer;
// outputs and inouts pass by pointer; open arrays pass a handle.
std::optional<std::string> dpi_c_parameter(
    const std::optional<DpiCType>& type,
    const semantic::sv::Direction direction, const bool open_array,
    const std::string& name)
{
    if (!type) {
        return std::nullopt;
    }
    const bool input = direction == semantic::sv::Direction::input;
    if (!input && direction != semantic::sv::Direction::output
        && direction != semantic::sv::Direction::inout) {
        return std::nullopt;
    }
    if (open_array) {
        return std::string { input ? "const svOpenArrayHandle "
                                   : "svOpenArrayHandle " }
            + name;
    }
    if (type->code == 'B' || type->code == 'V') {
        const std::string vector
            = type->code == 'B' ? "svBitVecVal* " : "svLogicVecVal* ";
        return (input ? "const " : "") + vector + name;
    }
    auto scalar = dpi_c_scalar(type->code);
    if (scalar.empty()) {
        return std::nullopt;
    }
    return scalar + (input ? " " : "* ") + name;
}

// A function result is a small value: a packed bit array of at most 32
// bits returns as an svBitVecVal (IEEE 1800-2023 H.7.4).
std::optional<std::string> dpi_c_result(const std::optional<DpiCType>& type)
{
    if (type && type->code == 'B' && type->width <= 32U) {
        return "svBitVecVal";
    }
    if (!type || type->code == 'B' || type->code == 'V') {
        return std::nullopt;
    }
    auto result = dpi_c_scalar(type->code);
    return result.empty() ? std::nullopt : std::optional { result };
}

// The C result type and parameter declarations of an import or export.
struct DpiCSignature {
    std::string result;
    std::vector<std::string> parameters;
    std::vector<std::string> names;
};

std::optional<DpiCSignature> dpi_c_signature(
    const semantic::sv::DpiDeclaration& declaration,
    const DpiTypeContext& context)
{
    std::vector<std::string> names;
    std::vector<std::string> parameters;
    std::string result;
    const bool task
        = declaration.callable_kind == semantic::sv::DpiCallableKind::task;
    if (declaration.direction
        == semantic::sv::DpiDirection::export_declaration) {
        // An export's types are those of the callable it names.
        if (!declaration.resolved_profile) {
            return std::nullopt;
        }
        const auto& profile = *declaration.resolved_profile;
        if (task) {
            result = "int";
        } else if (!profile.return_type
            || profile.return_type->target.spelling == "void") {
            result = "void";
        } else {
            const auto type = dpi_c_result(
                dpi_c_type(*profile.return_type, context));
            if (!type) {
                return std::nullopt;
            }
            result = *type;
        }
        for (const auto& formal : profile.formals) {
            auto element = formal.type;
            const bool open_array = !element.unpacked_dimensions.empty();
            element.unpacked_dimensions.clear();
            element.container_form.reset();
            const auto parameter = dpi_c_parameter(
                dpi_c_type(element, context), formal.direction, open_array,
                formal.name);
            if (!parameter) {
                return std::nullopt;
            }
            parameters.push_back(*parameter);
            names.push_back(formal.name);
        }
    } else {
        if (task) {
            // A task returns its disable status (IEEE 1800-2023 35.9).
            result = "int";
        } else if (declaration.return_type_tokens.size() == 1U
            && declaration.return_type_tokens.front().text == "void") {
            result = "void";
        } else {
            const auto type = dpi_c_result(
                dpi_c_type(declaration.return_type_tokens, context));
            if (!type) {
                return std::nullopt;
            }
            result = *type;
        }
        for (const auto& formal : declaration.formals) {
            // Unpacked dimensions of a formal are open (H.12).
            for (std::size_t token = 0U;
                 token < formal.dimension_tokens.size(); token += 2U) {
                if (token + 1U >= formal.dimension_tokens.size()
                    || formal.dimension_tokens[token].text != "["
                    || formal.dimension_tokens[token + 1U].text != "]") {
                    return std::nullopt;
                }
            }
            const auto parameter = dpi_c_parameter(
                dpi_c_type(formal.type_tokens, context), formal.direction,
                !formal.dimension_tokens.empty(), formal.name);
            if (!parameter) {
                return std::nullopt;
            }
            parameters.push_back(*parameter);
            names.push_back(formal.name);
        }
    }
    return DpiCSignature { result, parameters, names };
}

std::string dpi_c_declarator(
    const DpiCSignature& signature, const std::string& linkage)
{
    auto result = signature.result + ' ' + linkage + '(';
    for (std::size_t index = 0U; index < signature.parameters.size();
         ++index) {
        result += (index == 0U ? "" : ", ") + signature.parameters[index];
    }
    if (signature.parameters.empty()) {
        result += "void";
    }
    return result + ')';
}

// A weak definition of an export that the C code links against: it hands
// the address of each argument's C value (an argument passed by pointer
// is that address) to the simulator, which runs the export.
std::string dpi_export_stub(
    const DpiCSignature& signature, const std::string& linkage)
{
    std::string arguments;
    for (std::size_t index = 0U; index < signature.parameters.size();
         ++index) {
        const auto& parameter = signature.parameters[index];
        const auto& name = signature.names[index];
        const auto type = std::string_view { parameter }.substr(
            0U, parameter.size() - name.size());
        const bool by_value = type.find('*') == std::string_view::npos
            || type.starts_with("const char* ")
            || type.starts_with("void* ") || type.starts_with("const void* ");
        arguments += std::string { index == 0U ? "" : ", " }
            + "(void*)" + (by_value ? "&" : "") + name;
    }
    const bool value = signature.result != "void";
    return "__attribute__((weak)) " + dpi_c_declarator(signature, linkage)
        + "\n{\n    void* fsim_arguments[] = { "
        + (arguments.empty() ? std::string { "0" } : arguments) + " };\n"
        + (value ? "    " + signature.result + " fsim_result = 0;\n" : "")
        + "    (void)fsim_svdpi_export_call_v3(\"" + linkage
        + "\", fsim_arguments, " + (value ? "&fsim_result" : "0") + ");\n"
        + (value ? "    return fsim_result;\n" : "") + "}\n";
}

}  // namespace

std::string systemverilog_dpi_header(const semantic::sv::Hir& hir)
{
    // Declarations in a design unit belong to it; those in a compilation
    // unit's scope to the design (IEEE 1800-2023 35.5.4).
    std::vector<const semantic::sv::DpiDeclaration*> declarations;
    for (const auto& declaration : hir.dpi_declarations()) {
        declarations.push_back(&declaration);
    }
    for (const auto& unit : hir.units()) {
        for (const auto& declaration : unit.dpi_declarations) {
            declarations.push_back(&declaration);
        }
    }
    std::string result
        = "/* DPI-C prototypes of the design's imports and exports (IEEE "
          "1800-2023 35, Annex H). */\n"
          "#ifndef FSIM_DPI_HEADER_H\n#define FSIM_DPI_HEADER_H\n"
          "#include \"svdpi.h\"\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
          "extern int fsim_svdpi_export_call_v3(const char* name, "
          "void** arguments, void* result);\n";
    std::string stubs;
    std::set<std::string, std::less<>> declared;
    for (const auto* const entry : declarations) {
        const auto& declaration = *entry;
        // The hidden imports of a unit's export service are not C code.
        if (!declaration.validated || declaration.linkage_name.empty()
            || declaration.linkage_name.starts_with("__fsim_dpi_export_")
            || !declared.insert(declaration.linkage_name).second) {
            continue;
        }
        const bool import
            = declaration.direction == semantic::sv::DpiDirection::import;
        const auto signature = dpi_c_signature(declaration,
            dpi_hir_type_context(hir, declaration.owner_scope));
        const auto prototype = signature
            ? std::optional { dpi_c_declarator(
                  *signature, declaration.linkage_name) }
            : std::nullopt;
        if (!import && signature) {
            stubs += dpi_export_stub(*signature, declaration.linkage_name);
        }
        if (!prototype) {
            result += "/* " + std::string { import ? "import" : "export" }
                + ' ' + declaration.linkage_name
                + ": no C prototype for its types */\n";
            continue;
        }
        result += "/* " + std::string { import ? "Imported" : "Exported" }
            + " " + (declaration.callable_kind
                    == semantic::sv::DpiCallableKind::task
                ? "task" : "function")
            + " " + declaration.systemverilog_name + " */\nextern "
            + *prototype + ";\n";
    }
    if (!stubs.empty()) {
        result += "#ifndef FSIM_DPI_NO_EXPORT_STUBS\n" + stubs + "#endif\n";
    }
    return result + "#ifdef __cplusplus\n}\n#endif\n#endif\n";
}

}  // namespace fsim::elaboration
