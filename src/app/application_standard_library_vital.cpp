// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <limits>

namespace fsim::app::application_detail {
namespace {

    frontend::Type vital_scalar_type(
        const std::string_view spelling,
        const frontend::ValueDomain domain,
        const frontend::SourceSpan& span)
    {
        frontend::Type type;
        type.spelling = std::string { spelling };
        type.domain = domain;
        type.named_type_span = span;
        if (domain == frontend::ValueDomain::Integer) {
            type.is_signed = true;
            type.packed_range = frontend::PackedRange { 63, 0, true };
            type.integer_range = frontend::IntegerRange {
                0, std::numeric_limits<std::int64_t>::max(), false
            };
            type.nominal_type = "@builtin:time";
            type.vhdl_type_declaration = type.nominal_type;
        }
        return type;
    }

    frontend::Type vital_enumeration_type(
        const std::string_view name,
        std::vector<std::string> literals,
        const frontend::SourceSpan& span)
    {
        frontend::Type type;
        type.spelling = std::string { name };
        type.domain = frontend::ValueDomain::Bit2;
        type.nominal_type = "@fsim-vital:" + std::string { name };
        type.vhdl_type_declaration = type.nominal_type;
        type.named_type_span = span;
        type.enumeration_literals = std::move(literals);
        std::uint64_t width = 1;
        auto maximum = type.enumeration_literals.empty()
            ? std::uint64_t { 0 }
            : static_cast<std::uint64_t>(type.enumeration_literals.size() - 1U);
        while (maximum > 1U) {
            ++width;
            maximum >>= 1U;
        }
        type.packed_range = frontend::PackedRange {
            static_cast<std::int64_t>(width - 1U), 0, true
        };
        type.enumeration_range = frontend::EnumerationRange {
            0,
            static_cast<std::int64_t>(type.enumeration_literals.size() - 1U),
            false
        };
        return type;
    }

    frontend::Type vital_integer_type(
        const std::string_view spelling,
        const frontend::SourceSpan& span,
        const frontend::VhdlStandard standard)
    {
        auto type = frontend::vhdl_predefined_integer_type(
            standard, spelling);
        type.nominal_type = "@builtin:integer";
        type.vhdl_type_declaration = type.nominal_type;
        type.named_type_span = span;
        return type;
    }

    frontend::Type vital_array_type(
        const std::string_view name,
        const std::string_view index_subtype,
        const std::optional<std::size_t> element_count,
        frontend::Type element,
        const frontend::SourceSpan& span,
        const std::optional<frontend::IntegerRange> index_base = std::nullopt)
    {
        frontend::Type type;
        type.spelling = std::string { name };
        type.domain = element.domain;
        type.nominal_type = "@fsim-vital:" + std::string { name };
        type.vhdl_type_declaration = type.nominal_type;
        type.named_type_span = span;
        frontend::VhdlArrayDimension dimension;
        dimension.index_subtype = std::string { index_subtype };
        dimension.index_span = span;
        dimension.index_base_range = index_base;
        dimension.unconstrained = !element_count.has_value();
        frontend::VhdlArrayInfo array;
        array.index_subtype = dimension.index_subtype;
        array.index_span = span;
        array.index_base_range = index_base;
        array.element_spelling = element.spelling;
        array.element_span = span;
        array.element_domain = element.domain;
        array.unconstrained = dimension.unconstrained;
        const auto element_width = element.width();
        if (element_count) {
            const auto last = static_cast<std::int64_t>(*element_count - 1U);
            dimension.range = frontend::IntegerRange { 0, last, false };
            dimension.stride = element_width.value_or(0);
            array.flat_width = element_width
                ? std::optional<std::uint64_t> {
                      *element_width * static_cast<std::uint64_t>(*element_count)
                  }
                : std::nullopt;
            type.packed_range = frontend::PackedRange { 0, last, false };
        }
        array.dimensions.push_back(std::move(dimension));
        array.element_types.push_back(std::move(element));
        type.vhdl_array = std::move(array);
        return type;
    }

    frontend::Type vital_two_dimensional_table_type(
        const std::string_view name,
        frontend::Type element,
        const frontend::SourceSpan& span)
    {
        auto type = vital_array_type(
            name, "natural", std::nullopt, std::move(element), span,
            frontend::IntegerRange {
                0, std::numeric_limits<std::int32_t>::max(), false });
        auto& array = *type.vhdl_array;
        array.dimensions.push_back(array.dimensions.front());
        return type;
    }

    frontend::Type vital_access_type(
        const std::string_view name,
        frontend::Type designated,
        const frontend::SourceSpan& span)
    {
        frontend::Type type;
        type.spelling = std::string { name };
        type.domain = frontend::ValueDomain::Bit2;
        type.nominal_type = "@fsim-vital:" + std::string { name };
        type.vhdl_type_declaration = type.nominal_type;
        type.named_type_span = span;
        frontend::VhdlAccessInfo access;
        access.designated_span = span;
        access.designated_types.push_back(std::move(designated));
        type.packed_range = frontend::PackedRange {
            static_cast<std::int64_t>(access.handle_width - 1U), 0, true
        };
        type.vhdl_access = std::move(access);
        return type;
    }

    frontend::Type vital_record_type(
        const std::string_view name,
        std::vector<std::pair<std::string, frontend::Type>> fields,
        const frontend::SourceSpan& span)
    {
        frontend::Type type;
        type.spelling = std::string { name };
        type.domain = frontend::ValueDomain::Bit2;
        type.nominal_type = "@fsim-vital:" + std::string { name };
        type.vhdl_type_declaration = type.nominal_type;
        type.named_type_span = span;
        type.packed_aggregate = frontend::PackedAggregateKind::Struct;
        std::uint64_t width = 0;
        for (auto& [field_name, field_type] : fields) {
            const auto field_width = field_type.width();
            if (!field_width || *field_width == 0
                || *field_width > std::numeric_limits<std::uint64_t>::max() - width) {
                type.packed_range.reset();
                width = 0;
                break;
            }
            if (field_type.domain == frontend::ValueDomain::Logic9) {
                type.domain = frontend::ValueDomain::Logic9;
            }
            type.packed_members.push_back(frontend::PackedMember {
                std::move(field_name), field_type.domain, field_type.spelling,
                field_type.packed_range, field_type.is_signed,
                field_type.packed_range_expression, 0, span,
                std::vector<frontend::Type> { std::move(field_type) },
                std::nullopt });
            width += *field_width;
        }
        if (width != 0) {
            auto offset = width;
            for (auto& member : type.packed_members) {
                offset -= *member.width();
                member.lsb_offset = offset;
            }
            type.packed_range = frontend::PackedRange {
                static_cast<std::int64_t>(width - 1U), 0, true
            };
        }
        return type;
    }

    void add_vital_alias(
        frontend::DesignUnit& unit,
        const std::string_view name,
        frontend::Type type,
        const frontend::TypeDeclarationKind kind)
    {
        std::vector<frontend::EnumLiteralDeclaration> literals;
        if (!type.enumeration_literals.empty()) {
            literals.reserve(type.enumeration_literals.size());
            for (std::size_t index = 0;
                index < type.enumeration_literals.size(); ++index) {
                literals.push_back(frontend::EnumLiteralDeclaration {
                    type.enumeration_literals[index],
                    frontend::Expression {
                        frontend::ExpressionKind::IntegerLiteral,
                        std::to_string(index), { }, unit.span },
                    unit.span });
            }
        }
        unit.type_aliases.push_back(frontend::TypeAliasDeclaration {
            std::string { name }, std::move(type), unit.span, std::move(literals), kind,
            { }, { }, { }, false });
    }

} // namespace


    void materialize_vital_types(frontend::DesignUnit& unit)
    {
        if (!unit.primary_name.empty()) {
            return;
        }
        const auto logic = vital_scalar_type(
            "std_ulogic", frontend::ValueDomain::Logic9, unit.span);
        const auto time = vital_scalar_type(
            "vitaldelaytype", frontend::ValueDomain::Integer, unit.span);
        const auto natural_base = frontend::IntegerRange {
            0, std::numeric_limits<std::int32_t>::max(), false
        };
        if (unit.name == "vital_timing") {
            auto transition = vital_enumeration_type(
                "vitaltransitiontype",
                { "tr01", "tr10", "tr0z", "trz1", "tr1z", "trz0",
                    "tr0x", "trx1", "tr1x", "trx0", "trxz", "trzx" },
                unit.span);
            add_vital_alias(
                unit, "vitaltransitiontype", transition,
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "vitaldelaytype", time,
                frontend::TypeDeclarationKind::VhdlSubtype);
            const auto delay01 = vital_array_type(
                "vitaldelaytype01", "vitaltransitiontype", 2, time, unit.span,
                frontend::IntegerRange { 0, 11, false });
            const auto delay01z = vital_array_type(
                "vitaldelaytype01z", "vitaltransitiontype", 6, time, unit.span,
                frontend::IntegerRange { 0, 11, false });
            const auto delay01zx = vital_array_type(
                "vitaldelaytype01zx", "vitaltransitiontype", 12, time, unit.span,
                frontend::IntegerRange { 0, 11, false });
            add_vital_alias(
                unit, "vitaldelaytype01", delay01,
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitaldelaytype01z", delay01z,
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitaldelaytype01zx", delay01zx,
                frontend::TypeDeclarationKind::VhdlArray);
            for (auto [name, element] : {
                     std::pair { "vitaldelayarraytype", time },
                     std::pair { "vitaldelayarraytype01", delay01 },
                     std::pair { "vitaldelayarraytype01z", delay01z },
                     std::pair { "vitaldelayarraytype01zx", delay01zx } }) {
                add_vital_alias(
                    unit, name,
                    vital_array_type(
                        name, "natural", std::nullopt, std::move(element),
                        unit.span, natural_base),
                    frontend::TypeDeclarationKind::VhdlArray);
            }
            for (const auto& [name, count] : {
                     std::pair { "std_logic_vector2", std::size_t { 2 } },
                     std::pair { "std_logic_vector3", std::size_t { 3 } },
                     std::pair { "std_logic_vector4", std::size_t { 4 } },
                     std::pair { "std_logic_vector8", std::size_t { 8 } } }) {
                auto vector = vital_array_type(
                    name, "natural", count, logic, unit.span, natural_base);
                vector.nominal_type.clear();
                vector.vhdl_type_declaration = "@builtin:std_logic_vector";
                vector.packed_range = frontend::PackedRange {
                    static_cast<std::int64_t>(count - 1U), 0, true
                };
                vector.vhdl_array->dimensions.front().range = frontend::IntegerRange {
                    static_cast<std::int64_t>(count - 1U), 0, true
                };
                add_vital_alias(
                    unit, name, std::move(vector),
                    frontend::TypeDeclarationKind::VhdlSubtype);
            }
            for (const auto& [name, count] : {
                     std::pair { "vitaloutputmaptype", std::size_t { 9 } },
                     std::pair { "vitalresultmaptype", std::size_t { 4 } },
                     std::pair { "vitalresultzmaptype", std::size_t { 5 } } }) {
                add_vital_alias(
                    unit, name,
                    vital_array_type(
                        name, "std_ulogic", count, logic, unit.span,
                        frontend::IntegerRange { 0, 8, false }),
                    frontend::TypeDeclarationKind::VhdlArray);
            }
            add_vital_alias(
                unit, "vitaltablesymboltype",
                vital_enumeration_type(
                    "vitaltablesymboltype",
                    { "'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'",
                        "'n'", "'R'", "'F'", "'^'", "'v'", "'E'", "'A'",
                        "'D'", "'*'", "'X'", "'0'", "'1'", "'-'", "'B'",
                        "'Z'", "'S'" },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlEnumeration);
            auto edge = unit.type_aliases.back().type;
            edge.spelling = "vitaledgesymboltype";
            edge.enumeration_range = frontend::EnumerationRange { 0, 15, false };
            add_vital_alias(
                unit, "vitaledgesymboltype", std::move(edge),
                frontend::TypeDeclarationKind::VhdlSubtype);
            const auto boolean = vital_scalar_type(
                "boolean", frontend::ValueDomain::Boolean, unit.span);
            const auto time_array = vital_array_type(
                "vitaltimearrayt", "integer", std::nullopt, time, unit.span);
            const auto bool_array = vital_array_type(
                "vitalboolarrayt", "integer", std::nullopt, boolean, unit.span);
            add_vital_alias(
                unit, "vitaltimearrayt", time_array,
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitalboolarrayt", bool_array,
                frontend::TypeDeclarationKind::VhdlArray);
            const auto time_access = vital_access_type(
                "vitaltimearraypt", time_array, unit.span);
            const auto bool_access = vital_access_type(
                "vitalboolarraypt", bool_array, unit.span);
            const auto logic_vector = vital_array_type(
                "std_logic_vector", "natural", std::nullopt, logic, unit.span,
                natural_base);
            const auto logic_access = vital_access_type(
                "vitallogicarraypt", logic_vector, unit.span);
            add_vital_alias(
                unit, "vitaltimearraypt", time_access,
                frontend::TypeDeclarationKind::VhdlAccess);
            add_vital_alias(
                unit, "vitalboolarraypt", bool_access,
                frontend::TypeDeclarationKind::VhdlAccess);
            add_vital_alias(
                unit, "vitallogicarraypt", logic_access,
                frontend::TypeDeclarationKind::VhdlAccess);
            add_vital_alias(
                unit, "vitaltimingdatatype",
                vital_record_type(
                    "vitaltimingdatatype",
                    { { "notfirstflag", boolean }, { "reflast", logic },
                        { "reftime", time }, { "holden", boolean },
                        { "testlast", logic }, { "testtime", time },
                        { "setupen", boolean }, { "testlasta", logic_access },
                        { "testtimea", time_access }, { "holdena", bool_access },
                        { "setupena", bool_access } },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlRecord);
            add_vital_alias(
                unit, "vitalperioddatatype",
                vital_record_type(
                    "vitalperioddatatype",
                    { { "last", logic }, { "rise", time }, { "fall", time },
                        { "notfirstflag", boolean } },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlRecord);
            auto glitch_kind = vital_enumeration_type(
                "vitalglitchkindtype",
                { "onevent", "ondetect", "vitalinertial", "vitaltransport" },
                unit.span);
            add_vital_alias(
                unit, "vitalglitchkindtype", glitch_kind,
                frontend::TypeDeclarationKind::VhdlEnumeration);
            const auto glitch_data = vital_record_type(
                "vitalglitchdatatype",
                { { "schedtime", time }, { "glitchtime", time },
                    { "schedvalue", logic }, { "lastvalue", logic } },
                unit.span);
            add_vital_alias(
                unit, "vitalglitchdatatype", glitch_data,
                frontend::TypeDeclarationKind::VhdlRecord);
            add_vital_alias(
                unit, "vitalglitchdataarraytype",
                vital_array_type(
                    "vitalglitchdataarraytype", "natural", std::nullopt,
                    glitch_data, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            for (const auto& [record_name, array_name, delay_type] : {
                     std::tuple {
                         "vitalpathtype", "vitalpatharraytype", time },
                     std::tuple {
                         "vitalpath01type", "vitalpatharray01type", delay01 },
                     std::tuple {
                         "vitalpath01ztype", "vitalpatharray01ztype", delay01z } }) {
                auto record = vital_record_type(
                    record_name,
                    { { "inputchangetime", time }, { "pathdelay", delay_type },
                        { "pathcondition", boolean } },
                    unit.span);
                add_vital_alias(
                    unit, record_name, record,
                    frontend::TypeDeclarationKind::VhdlRecord);
                add_vital_alias(
                    unit, array_name,
                    vital_array_type(
                        array_name, "natural", std::nullopt, std::move(record),
                        unit.span, natural_base),
                    frontend::TypeDeclarationKind::VhdlArray);
            }
            auto skew_expected = vital_enumeration_type(
                "vitalskewexpectedtype", { "none", "s1r", "s1f", "s2r", "s2f" },
                unit.span);
            add_vital_alias(
                unit, "vitalskewexpectedtype", skew_expected,
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "vitalskewdatatype",
                vital_record_type(
                    "vitalskewdatatype",
                    { { "expectedtype", skew_expected }, { "signal1old1", time },
                        { "signal2old1", time }, { "signal1old2", time },
                        { "signal2old2", time } },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlRecord);
            return;
        }
        if (unit.name == "vital_memory") {
            const auto boolean = vital_scalar_type(
                "boolean", frontend::ValueDomain::Boolean, unit.span);
            const auto integer = vital_integer_type(
                "integer", unit.span, unit.vhdl_standard);
            const auto positive = vital_integer_type(
                "positive", unit.span, unit.vhdl_standard);
            const auto logic_vector = vital_array_type(
                "std_logic_vector", "natural", std::nullopt, logic, unit.span,
                natural_base);
            const auto x01_array = vital_array_type(
                "x01arrayt", "natural", std::nullopt, logic, unit.span,
                natural_base);
            const auto x01_access = vital_access_type(
                "x01arraypt", x01_array, unit.span);
            add_vital_alias(
                unit, "vitalmemoryarctype",
                vital_enumeration_type(
                    "vitalmemoryarctype",
                    { "parallelarc", "crossarc", "subwordarc" }, unit.span),
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "outputretainbehaviortype",
                vital_enumeration_type(
                    "outputretainbehaviortype", { "bitcorrupt", "wordcorrupt" },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "vitalmemorymsgformattype",
                vital_enumeration_type(
                    "vitalmemorymsgformattype", { "vector", "scalar", "vectorenum" },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "x01arrayt", x01_array,
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "x01arraypt", x01_access,
                frontend::TypeDeclarationKind::VhdlAccess);
            auto violation_access = x01_access;
            violation_access.spelling = "vitalmemoryviolationtype";
            violation_access.nominal_type = "@fsim-vital:vitalmemoryviolationtype";
            violation_access.vhdl_type_declaration = violation_access.nominal_type;
            add_vital_alias(
                unit, "vitalmemoryviolationtype", std::move(violation_access),
                frontend::TypeDeclarationKind::VhdlAccess);
            const auto schedule_data = vital_record_type(
                "vitalmemoryscheduledatatype",
                { { "outputdata", logic }, { "numbitspersubword", integer },
                    { "scheduletime", time }, { "schedulevalue", logic },
                    { "lastoutputvalue", logic }, { "propdelay", time },
                    { "outputretaindelay", time }, { "inputage", time } },
                unit.span);
            add_vital_alias(
                unit, "vitalmemoryscheduledatatype", schedule_data,
                frontend::TypeDeclarationKind::VhdlRecord);
            const auto time_array = vital_array_type(
                "vitaltimearrayt", "integer", std::nullopt, time, unit.span);
            const auto bool_array = vital_array_type(
                "vitalboolarrayt", "integer", std::nullopt, boolean, unit.span);
            const auto time_access = vital_access_type(
                "vitaltimearraypt", time_array, unit.span);
            const auto bool_access = vital_access_type(
                "vitalboolarraypt", bool_array, unit.span);
            const auto logic_access = vital_access_type(
                "vitallogicarraypt", logic_vector, unit.span);
            const auto memory_timing_data = vital_record_type(
                "vitalmemorytimingdatatype",
                { { "notfirstflag", boolean }, { "reflast", logic },
                    { "reftime", time }, { "holden", boolean },
                    { "testlast", logic }, { "testtime", time },
                    { "setupen", boolean }, { "testlasta", logic_access },
                    { "testtimea", time_access }, { "reflasta", x01_access },
                    { "reftimea", time_access }, { "holdena", bool_access },
                    { "setupena", bool_access } },
                unit.span);
            add_vital_alias(
                unit, "vitalmemorytimingdatatype", memory_timing_data,
                frontend::TypeDeclarationKind::VhdlRecord);
            const auto period_data = vital_record_type(
                "vitalperioddatatype",
                { { "last", logic }, { "rise", time }, { "fall", time },
                    { "notfirstflag", boolean } },
                unit.span);
            add_vital_alias(
                unit, "vitalperioddataarraytype",
                vital_array_type(
                    "vitalperioddataarraytype", "natural", std::nullopt,
                    period_data, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitalmemoryscheduledatavectortype",
                vital_array_type(
                    "vitalmemoryscheduledatavectortype", "natural", std::nullopt,
                    schedule_data, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            auto port_state = vital_enumeration_type(
                "vitalportstatetype",
                { "undef", "read", "write", "corrupt", "highz" }, unit.span);
            add_vital_alias(
                unit, "vitalportstatetype", port_state,
                frontend::TypeDeclarationKind::VhdlEnumeration);
            const auto port_flag = vital_record_type(
                "vitalportflagtype",
                { { "memorycurrent", port_state }, { "memoryprevious", port_state },
                    { "datacurrent", port_state }, { "dataprevious", port_state },
                    { "outputdisable", boolean } },
                unit.span);
            add_vital_alias(
                unit, "vitalportflagtype", port_flag,
                frontend::TypeDeclarationKind::VhdlRecord);
            add_vital_alias(
                unit, "vitalportflagvectortype",
                vital_array_type(
                    "vitalportflagvectortype", "natural", std::nullopt,
                    port_flag, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            const auto memory_word = vital_array_type(
                "memorywordtype", "natural", std::nullopt, logic, unit.span,
                natural_base);
            const auto memory_word_ptr = vital_access_type(
                "memorywordptr", memory_word, unit.span);
            add_vital_alias(
                unit, "memorywordtype", memory_word,
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "memorywordptr", memory_word_ptr,
                frontend::TypeDeclarationKind::VhdlAccess);
            const auto memory_array = vital_array_type(
                "memoryarraytype", "natural", std::nullopt, memory_word_ptr,
                unit.span, natural_base);
            const auto memory_array_ptr = vital_access_type(
                "memoryarrayptrtype", memory_array, unit.span);
            add_vital_alias(
                unit, "memoryarraytype", memory_array,
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "memoryarrayptrtype", memory_array_ptr,
                frontend::TypeDeclarationKind::VhdlAccess);
            const auto memory_record = vital_record_type(
                "vitalmemoryarrayrectype",
                { { "noofwords", positive }, { "noofbitsperword", positive },
                    { "noofbitspersubword", positive }, { "noofbitsperenable", positive },
                    { "memoryarrayptr", memory_array_ptr } },
                unit.span);
            add_vital_alias(
                unit, "vitalmemoryarrayrectype", memory_record,
                frontend::TypeDeclarationKind::VhdlRecord);
            add_vital_alias(
                unit, "vitalmemorydatatype",
                vital_access_type("vitalmemorydatatype", memory_record, unit.span),
                frontend::TypeDeclarationKind::VhdlAccess);
            const auto timing_data = vital_record_type(
                "vitaltimingdatatype",
                { { "notfirstflag", boolean }, { "reflast", logic },
                    { "reftime", time }, { "holden", boolean },
                    { "testlast", logic }, { "testtime", time },
                    { "setupen", boolean }, { "testlasta", logic_access },
                    { "testtimea", time_access }, { "holdena", bool_access },
                    { "setupena", bool_access } },
                unit.span);
            add_vital_alias(
                unit, "vitaltimingdatavectortype",
                vital_array_type(
                    "vitaltimingdatavectortype", "natural", std::nullopt,
                    timing_data, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitalmemoryviolflagsizetype",
                vital_array_type(
                    "vitalmemoryviolflagsizetype", "natural", std::nullopt,
                    integer, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            auto memory_symbol = vital_enumeration_type(
                "vitalmemorysymboltype",
                { "'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'",
                    "'n'", "'R'", "'F'", "'^'", "'v'", "'E'", "'A'", "'D'",
                    "'*'", "'X'", "'0'", "'1'", "'-'", "'B'", "'Z'", "'S'",
                    "'g'", "'u'", "'i'", "'G'", "'U'", "'I'", "'w'", "'s'",
                    "'c'", "'l'", "'d'", "'e'", "'C'", "'L'", "'M'", "'m'",
                    "'t'" },
                unit.span);
            add_vital_alias(
                unit, "vitalmemorysymboltype", memory_symbol,
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "vitalmemorytabletype",
                vital_two_dimensional_table_type(
                    "vitalmemorytabletype", memory_symbol, unit.span),
                frontend::TypeDeclarationKind::VhdlArray);
            auto violation_symbol = vital_enumeration_type(
                "vitalmemoryviolationsymboltype", { "'X'", "'0'", "'-'" },
                unit.span);
            add_vital_alias(
                unit, "vitalmemoryviolationsymboltype", violation_symbol,
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "vitalmemoryviolationtabletype",
                vital_two_dimensional_table_type(
                    "vitalmemoryviolationtabletype", violation_symbol, unit.span),
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitalporttype",
                vital_enumeration_type(
                    "vitalporttype", { "undef", "read", "write", "rdnwr" },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlEnumeration);
            add_vital_alias(
                unit, "vitalcrossportmodetype",
                vital_enumeration_type(
                    "vitalcrossportmodetype",
                    { "cpread", "writecontention", "readwritecontention",
                        "cpreadandwritecontention", "cpreadandreadcontention" },
                    unit.span),
                frontend::TypeDeclarationKind::VhdlEnumeration);
            auto address = integer;
            address.spelling = "vitaladdressvaluetype";
            add_vital_alias(
                unit, "vitaladdressvaluetype", address,
                frontend::TypeDeclarationKind::VhdlSubtype);
            add_vital_alias(
                unit, "vitaladdressvaluevectortype",
                vital_array_type(
                    "vitaladdressvaluevectortype", "natural", std::nullopt,
                    address, unit.span, natural_base),
                frontend::TypeDeclarationKind::VhdlArray);
            return;
        }
        if (unit.name == "vital_primitives") {
            auto table = vital_enumeration_type(
                "vitaltablesymboltype",
                { "'/'", "'\\'", "'P'", "'N'", "'r'", "'f'", "'p'",
                    "'n'", "'R'", "'F'", "'^'", "'v'", "'E'", "'A'", "'D'",
                    "'*'", "'X'", "'0'", "'1'", "'-'", "'B'", "'Z'", "'S'" },
                unit.span);
            auto truth = table;
            truth.spelling = "vitaltruthsymboltype";
            truth.enumeration_range = frontend::EnumerationRange { 16, 21, false };
            add_vital_alias(
                unit, "vitaltruthsymboltype", truth,
                frontend::TypeDeclarationKind::VhdlSubtype);
            auto state = table;
            state.spelling = "vitalstatesymboltype";
            state.enumeration_range = frontend::EnumerationRange { 0, 22, false };
            add_vital_alias(
                unit, "vitalstatesymboltype", state,
                frontend::TypeDeclarationKind::VhdlSubtype);
            add_vital_alias(
                unit, "vitaltruthtabletype",
                vital_two_dimensional_table_type(
                    "vitaltruthtabletype", truth, unit.span),
                frontend::TypeDeclarationKind::VhdlArray);
            add_vital_alias(
                unit, "vitalstatetabletype",
                vital_two_dimensional_table_type(
                    "vitalstatetabletype", state, unit.span),
                frontend::TypeDeclarationKind::VhdlArray);
        }
    }

} // namespace fsim::app::application_detail
