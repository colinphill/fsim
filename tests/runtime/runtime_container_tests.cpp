// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error { std::string { message } };
        }
    }

    [[nodiscard]] fsim::runtime::PackedLogic4 value(
        const std::uint32_t width,
        const std::uint64_t bits)
    {
        return fsim::runtime::PackedLogic4::from_aval_bval(
            width, bits, 0);
    }

} // namespace

void test_simir_containers()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    ContainerType array_type;
    array_type.element_width = 32;
    array_type.two_state = true;
    array_type.signed_elements = true;
    ContainerType queue_type;
    queue_type.element_width = 8;
    queue_type.queue = true;
    queue_type.maximum_elements = 3;
    const ContainerValue reduction_values {
        queue_type,
        { value(8, 3), value(8, 5), value(8, 7) },
        { }
    };
    const auto unknown_element = PackedLogic4::from_aval_bval(8, 5, 1);
    const ContainerValue equal_unknown {
        queue_type,
        { value(8, 3), unknown_element }, { }
    };
    const ContainerValue unequal_known {
        queue_type,
        { value(8, 3), value(8, 6), value(8, 7) }, { }
    };
    const ContainerValue unequal_size {
        queue_type,
        { value(8, 3) }, { }
    };
    require(
        compare_container_values(
            reduction_values, reduction_values, false)
                == value(1, 1)
            && compare_container_values(
                   reduction_values, unequal_known, false)
                == value(1, 0)
            && compare_container_values(
                   equal_unknown, equal_unknown, false)
                    .get(0)
                == Logic4::x
            && compare_container_values(
                   equal_unknown, equal_unknown, true)
                == value(1, 1)
            && compare_container_values(
                   reduction_values, unequal_size, true)
                == value(1, 0),
        "container logical and case equality preserve shape and X policy");

    const auto fixed_type = [](ContainerType type,
                                const std::int32_t left,
                                const std::int32_t right) {
        type.fixed = true;
        type.index_left = left;
        type.index_right = right;
        type.dimensions = { { left, right } };
        return type;
    };
    ContainerType real_element_type;
    real_element_type.element_kind = ContainerElementKind::Scalar;
    real_element_type.scalar_kind = SystemVerilogScalarKind::Real;
    real_element_type.element_width = 64;
    auto real_array_type = fixed_type(real_element_type, 1, 0);
    auto positive_zero_reals = default_container_value(real_array_type);
    auto negative_zero_reals = default_container_value(real_array_type);
    const auto positive_zero = encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::real(0.0));
    const auto negative_zero = encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::real(-0.0));
    const auto not_a_number = encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::real(
            std::numeric_limits<double>::quiet_NaN()));
    require(positive_zero && negative_zero && not_a_number,
        "real container fixtures encode exactly");
    positive_zero_reals.elements[0] = positive_zero.value;
    negative_zero_reals.elements[0] = negative_zero.value;
    require(
        compare_container_values(
            positive_zero_reals, negative_zero_reals, false)
            == value(1, 1),
        "real container equality is numeric rather than payload-bit equality");
    negative_zero_reals.elements[0] = not_a_number.value;
    require(
        compare_container_values(
            positive_zero_reals, negative_zero_reals, false)
                == value(1, 0)
            && compare_container_values(
                   positive_zero_reals, negative_zero_reals, true)
                == value(1, 0),
        "real container logical and case equality retain IEEE NaN behavior");

    ContainerType time_element_type;
    time_element_type.element_kind = ContainerElementKind::Scalar;
    time_element_type.scalar_kind = SystemVerilogScalarKind::Time;
    time_element_type.element_width = 64;
    auto times = default_container_value(
        fixed_type(time_element_type, 0, 0));
    times.elements[0] = encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::time(42))
                            .value;
    auto time_copy = times;
    require(
        compare_container_values(times, time_copy, true) == value(1, 1),
        "time container copy and equality retain exact ticks");

    ContainerType chandle_element_type;
    chandle_element_type.element_kind = ContainerElementKind::Scalar;
    chandle_element_type.scalar_kind = SystemVerilogScalarKind::Chandle;
    chandle_element_type.element_width = 64;
    auto chandles = default_container_value(
        fixed_type(chandle_element_type, 0, 0));
    chandles.elements[0] = encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::chandle(UINT64_C(0x100000001)))
                               .value;
    auto distinct_chandles = chandles;
    distinct_chandles.elements[0] = encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::chandle(UINT64_C(0x200000001)))
                                        .value;
    require(
        compare_container_values(chandles, distinct_chandles, true)
            == value(1, 0),
        "chandle container equality uses the complete opaque identity");

    ContainerType string_element_type;
    string_element_type.element_kind = ContainerElementKind::String;
    string_element_type.element_width = 0;
    auto strings = default_container_value(
        fixed_type(string_element_type, 1, 0));
    strings.string_elements = { "alpha", "\xcf\x80" };
    auto string_copy = strings;
    strings.string_elements[0] = "changed";
    require(
        string_copy.string_elements[0] == "alpha"
            && compare_container_values(string_copy, string_copy, false)
                == value(1, 1)
            && compare_container_values(strings, string_copy, false)
                == value(1, 0),
        "string container copies own bounded byte values independently");

    ContainerType nested_string_type;
    nested_string_type.element_kind = ContainerElementKind::Container;
    nested_string_type.element_width = 0;
    nested_string_type.element_types = {
        fixed_type(string_element_type, 0, 0)
    };
    auto nested_strings = default_container_value(
        fixed_type(nested_string_type, 1, 0));
    nested_strings.nested_elements[0].string_elements[0] = "inner";
    auto nested_copy = nested_strings;
    nested_strings.nested_elements[0].string_elements[0] = "mutated";
    require(
        nested_copy.nested_elements[0].string_elements[0] == "inner"
            && container_value_size(nested_copy) == 2,
        "nested unpacked container copies recursively own their elements");

    ContainerType aggregate_type;
    aggregate_type.element_kind = ContainerElementKind::Aggregate;
    aggregate_type.aggregate_value = true;
    aggregate_type.element_width = 0;
    aggregate_type.element_nominal_type = "record_t";
    aggregate_type.element_types = {
        fixed_type(real_element_type, 0, 0),
        fixed_type(string_element_type, 0, 0),
        fixed_type(chandle_element_type, 0, 0)
    };
    aggregate_type.member_names = { "weight", "label", "cookie" };
    auto aggregate_array_type = aggregate_type;
    aggregate_array_type.aggregate_value = false;
    auto records = default_container_value(
        fixed_type(aggregate_array_type, 1, 0));
    require(
        records.nested_elements.size() == 2
            && records.nested_elements[0].nested_elements.size() == 3
            && records.nested_elements[0].nested_elements[1].string_elements.size() == 1
            && compare_container_values(records, records, true)
                == value(1, 1),
        "unpacked aggregate container defaults retain ordered heterogeneous "
        "member profiles");
    auto dynamic_records = default_container_value(aggregate_array_type);
    resize_container_value(dynamic_records, 2);
    require(
        dynamic_records.nested_elements.size() == 2
            && dynamic_records.nested_elements.front()
                .type.aggregate_value
            && dynamic_records.nested_elements.front()
                    .nested_elements.size()
                == 3,
        "dynamic unpacked aggregate arrays resize into recursive value boxes");

    auto dynamic_strings = default_container_value(string_element_type);
    resize_container_value(dynamic_strings, 3);
    dynamic_strings.string_elements[1] = "kept";
    const auto dynamic_initializer = dynamic_strings;
    resize_container_value(dynamic_strings, 4, &dynamic_initializer);
    require(
        dynamic_strings.string_elements.size() == 4
            && dynamic_strings.string_elements[1] == "kept"
            && dynamic_strings.string_elements[3].empty(),
        "dynamic string arrays preserve initializer prefixes and default tails");
    try {
        auto distinct_type = queue_type;
        distinct_type.element_nominal_type = "other_packet_t";
        (void)compare_container_values(
            reduction_values,
            ContainerValue { distinct_type, reduction_values.elements, { } },
            true);
        require(false, "distinct aggregate element identities must not compare");
    } catch (const std::invalid_argument&) {
    }
    require(
        reduce_container_value(
            reduction_values,
            ContainerReductionOperator::sum)
                == value(8, 15)
            && reduce_container_value(
                   reduction_values,
                   ContainerReductionOperator::product)
                == value(8, 105)
            && reduce_container_value(
                   reduction_values,
                   ContainerReductionOperator::bit_and)
                == value(8, 1)
            && reduce_container_value(
                   reduction_values,
                   ContainerReductionOperator::bit_or)
                == value(8, 7)
            && reduce_container_value(
                   reduction_values,
                   ContainerReductionOperator::bit_xor)
                == value(8, 1),
        "container reductions retain exact element width and values");
    const std::vector<ContainerPredicateNode> positive_index_mask {
        { ContainerPredicateOperator::item, 0, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::index, 0, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::constant, 0, 0,
            value(32, 0), ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::greater, 1, 2,
            PackedLogic4 { }, ContainerPredicateValueKind::logical },
        { ContainerPredicateOperator::constant, 0, 0,
            value(8, 0), ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::conditional, 3, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::element, 4 }
    };
    require(
        reduce_container_value(
            reduction_values,
            ContainerReductionOperator::sum,
            positive_index_mask)
            == value(8, 12),
        "reduction transformations use current queue indices once per "
        "element");
    ContainerType fixed_reduction_type = queue_type;
    fixed_reduction_type.queue = false;
    fixed_reduction_type.fixed = true;
    fixed_reduction_type.maximum_elements.reset();
    fixed_reduction_type.index_left = -1;
    fixed_reduction_type.index_right = 1;
    const ContainerValue fixed_reduction_values {
        fixed_reduction_type,
        { value(8, 3), value(8, 5), value(8, 7) },
        { }
    };
    auto negative_index_mask = positive_index_mask;
    negative_index_mask[3].operation = ContainerPredicateOperator::less;
    require(
        reduce_container_value(
            fixed_reduction_values,
            ContainerReductionOperator::sum,
            negative_index_mask)
            == value(8, 3),
        "reduction transformations use signed declared static indices");
    const ContainerValue empty_values { queue_type, { }, { } };
    require(
        reduce_container_value(
            empty_values, ContainerReductionOperator::sum)
                == value(8, 0)
            && reduce_container_value(
                   empty_values,
                   ContainerReductionOperator::product)
                == value(8, 1)
            && reduce_container_value(
                   empty_values,
                   ContainerReductionOperator::bit_and)
                == value(8, 0xff)
            && reduce_container_value(
                   empty_values,
                   ContainerReductionOperator::bit_or)
                == value(8, 0)
            && reduce_container_value(
                   empty_values,
                   ContainerReductionOperator::bit_xor)
                == value(8, 0),
        "empty container reductions use SystemVerilog identities");
    require(
        reduce_container_value(
            empty_values,
            ContainerReductionOperator::product,
            positive_index_mask)
            == value(8, 1),
        "empty transformed reductions preserve their operation identity");
    const ContainerValue unknown_values {
        queue_type,
        { value(8, 3),
            PackedLogic4::from_aval_bval(8, 3, 2) },
        { }
    };
    require(
        reduce_container_value(
            unknown_values,
            ContainerReductionOperator::sum)
                    .to_msb_string()
                == "XXXXXXXX"
            && reduce_container_value(
                   unknown_values,
                   ContainerReductionOperator::bit_and)
                    .to_msb_string()
                == "000000X1",
        "arithmetic and bitwise reductions propagate four-state values");
    const std::vector<ContainerPredicateNode> unknown_mask {
        { ContainerPredicateOperator::item, 0, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::constant, 0, 0,
            value(8, 3), ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::equal, 0, 1,
            PackedLogic4 { }, ContainerPredicateValueKind::logical },
        { ContainerPredicateOperator::constant, 0, 0,
            value(8, 0), ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::conditional, 2, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::element, 3 }
    };
    const ContainerValue one_unknown {
        queue_type,
        { PackedLogic4::from_aval_bval(8, 3, 2) },
        { }
    };
    require(
        reduce_container_value(
            one_unknown,
            ContainerReductionOperator::bit_or,
            unknown_mask)
                .to_msb_string()
            == "000000XX",
        "unknown transformation conditions merge element alternatives "
        "with four-state conditional semantics");
    ContainerType signed_order_type = queue_type;
    signed_order_type.signed_elements = true;
    signed_order_type.maximum_elements.reset();
    ContainerValue signed_order {
        signed_order_type,
        { value(8, 0x7f), value(8, 0xff), value(8, 0x80),
            value(8, 0), value(8, 0xff) },
        { }
    };
    order_container_value(
        signed_order, ContainerOrderingOperator::ascending);
    require(
        signed_order.elements
            == std::vector<PackedLogic4> {
                value(8, 0x80), value(8, 0xff), value(8, 0xff),
                value(8, 0), value(8, 0x7f) },
        "ascending ordering uses exact signed element semantics");
    order_container_value(
        signed_order, ContainerOrderingOperator::descending);
    require(
        signed_order.elements
            == std::vector<PackedLogic4> {
                value(8, 0x7f), value(8, 0), value(8, 0xff),
                value(8, 0xff), value(8, 0x80) },
        "descending ordering is stable and reverses the comparison");
    order_container_value(
        signed_order, ContainerOrderingOperator::reverse);
    require(
        signed_order.elements.front() == value(8, 0x80)
            && signed_order.elements.back() == value(8, 0x7f),
        "reverse permutes storage without changing container metadata");
    ContainerValue shuffled_a {
        signed_order_type,
        { value(8, 0), value(8, 1), value(8, 2), value(8, 3), value(8, 4) },
        { }
    };
    auto shuffled_b = shuffled_a;
    const std::vector<std::uint32_t> shuffle_draws { 1, 2, 0, 1 };
    std::size_t shuffle_a_cursor = 0;
    std::size_t shuffle_b_cursor = 0;
    order_container_value(
        shuffled_a, ContainerOrderingOperator::shuffle, { },
        [&]() { return shuffle_draws.at(shuffle_a_cursor++); });
    order_container_value(
        shuffled_b, ContainerOrderingOperator::shuffle, { },
        [&]() { return shuffle_draws.at(shuffle_b_cursor++); });
    require(
        shuffle_a_cursor == shuffle_draws.size()
            && shuffle_b_cursor == shuffle_draws.size()
            && shuffled_a.elements == shuffled_b.elements
            && shuffled_a.elements
                == std::vector<PackedLogic4> {
                    value(8, 3), value(8, 4), value(8, 0),
                    value(8, 2), value(8, 1) },
        "shuffle consumes one deterministic stream and preserves an exact "
        "permutation");
    try {
        auto invalid = shuffled_a;
        order_container_value(
            invalid, ContainerOrderingOperator::shuffle);
        require(false, "shuffle without a random source must reject");
    } catch (const std::invalid_argument&) {
    }
    try {
        auto invalid = shuffled_a;
        const std::vector<ContainerPredicateNode> invalid_key {
            { ContainerPredicateOperator::item, 0, 0,
                PackedLogic4 { }, ContainerPredicateValueKind::element }
        };
        order_container_value(
            invalid, ContainerOrderingOperator::shuffle, invalid_key,
            []() { return std::uint32_t { }; });
        require(false, "shuffle with an ordering key must reject");
    } catch (const std::invalid_argument&) {
    }
    auto four_state_order_type = queue_type;
    four_state_order_type.maximum_elements.reset();
    ContainerValue four_state_order {
        four_state_order_type,
        {
            PackedLogic4::from_msb_string("0000000Z"),
            value(8, 1),
            PackedLogic4::from_msb_string("0000000X"),
            value(8, 0),
        },
        { }
    };
    order_container_value(
        four_state_order, ContainerOrderingOperator::ascending);
    require(
        four_state_order.elements[0] == value(8, 0)
            && four_state_order.elements[1] == value(8, 1)
            && four_state_order.elements[2].to_msb_string()
                == "0000000X"
            && four_state_order.elements[3].to_msb_string()
                == "0000000Z",
        "four-state ordering uses the documented 0/1/X/Z total order");
    const std::vector<ContainerPredicateNode> ordering_index_key {
        { ContainerPredicateOperator::item, 0, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::index, 0, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::constant, 0, 0,
            value(32, 2), ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::less, 1, 2,
            PackedLogic4 { }, ContainerPredicateValueKind::logical },
        { ContainerPredicateOperator::constant, 0, 0,
            value(8, 0), ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::conditional, 3, 4,
            PackedLogic4 { }, ContainerPredicateValueKind::element, 0 }
    };
    ContainerValue ascending_keyed {
        four_state_order_type,
        { value(8, 30), value(8, 10),
            value(8, 20), value(8, 11) },
        { }
    };
    order_container_value(
        ascending_keyed, ContainerOrderingOperator::ascending,
        ordering_index_key);
    require(
        ascending_keyed.elements
            == std::vector<PackedLogic4> {
                value(8, 30), value(8, 10),
                value(8, 11), value(8, 20) },
        "ordering keys use original current indices and preserve original "
        "order among equal precomputed keys");
    ContainerValue descending_keyed {
        four_state_order_type,
        { value(8, 30), value(8, 10),
            value(8, 20), value(8, 11) },
        { }
    };
    order_container_value(
        descending_keyed, ContainerOrderingOperator::descending,
        ordering_index_key);
    require(
        descending_keyed.elements
            == std::vector<PackedLogic4> {
                value(8, 20), value(8, 11),
                value(8, 30), value(8, 10) },
        "descending transformed ordering remains stable for equal keys");
    auto fixed_key_type = four_state_order_type;
    fixed_key_type.queue = false;
    fixed_key_type.fixed = true;
    fixed_key_type.index_left = -1;
    fixed_key_type.index_right = 1;
    fixed_key_type.maximum_elements.reset();
    ContainerValue fixed_keyed {
        fixed_key_type,
        { value(8, 12), value(8, 11), value(8, 10) },
        { }
    };
    auto static_ordering_key = ordering_index_key;
    static_ordering_key[2].constant = value(32, 0);
    order_container_value(
        fixed_keyed, ContainerOrderingOperator::ascending,
        static_ordering_key);
    require(
        fixed_keyed.elements
            == std::vector<PackedLogic4> {
                value(8, 12), value(8, 10), value(8, 11) },
        "ordering keys use signed declared static-array indices");
    ContainerValue four_state_keyed {
        four_state_order_type,
        {
            PackedLogic4::from_msb_string("0000000Z"),
            value(8, 1),
            PackedLogic4::from_msb_string("0000000X"),
            value(8, 0),
        },
        { }
    };
    const std::vector<ContainerPredicateNode> item_key {
        { ContainerPredicateOperator::item, 0, 0,
            PackedLogic4 { }, ContainerPredicateValueKind::element }
    };
    order_container_value(
        four_state_keyed, ContainerOrderingOperator::ascending,
        item_key);
    require(
        four_state_keyed.elements[0] == value(8, 0)
            && four_state_keyed.elements[1] == value(8, 1)
            && four_state_keyed.elements[2].to_msb_string()
                == "0000000X"
            && four_state_keyed.elements[3].to_msb_string()
                == "0000000Z",
        "transformed keys preserve exact four-state total ordering");
    ContainerValue four_state_locator {
        four_state_order_type, { }, { }
    };
    locate_container_values(
        four_state_locator, four_state_order,
        ContainerLocatorOperator::maximum, { }, item_key);
    require(
        four_state_locator.elements.size() == 1
            && four_state_locator.elements[0].to_msb_string()
                == "0000000Z",
        "transformed extrema use the exact four-state total order");
    try {
        order_container_value(
            four_state_keyed, ContainerOrderingOperator::reverse,
            item_key);
        require(false, "reverse must reject ordering-key metadata");
    } catch (const std::invalid_argument&) {
    }
    ContainerValue locator_result {
        signed_order_type, { }, { }
    };
    ContainerType index_result_type;
    index_result_type.element_width = 32;
    index_result_type.two_state = true;
    index_result_type.signed_elements = true;
    index_result_type.queue = true;
    const ContainerValue empty_locator_source {
        signed_order_type, { }, { }
    };
    for (const auto operation :
        { ContainerLocatorOperator::minimum,
            ContainerLocatorOperator::maximum,
            ContainerLocatorOperator::unique,
            ContainerLocatorOperator::unique_index }) {
        ContainerValue empty_result {
            operation == ContainerLocatorOperator::unique_index
                ? index_result_type
                : signed_order_type,
            { }, { }
        };
        locate_container_values(
            empty_result, empty_locator_source, operation);
        require(
            empty_result.elements.empty(),
            "empty container locators return an empty queue");
        locate_container_values(
            empty_result, empty_locator_source, operation, { }, item_key);
        require(
            empty_result.elements.empty(),
            "empty transformed container locators return an empty queue");
    }
    locate_container_values(
        locator_result, signed_order,
        ContainerLocatorOperator::minimum);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 0x80) },
        "minimum locator returns the signed extremum");
    locate_container_values(
        locator_result, signed_order,
        ContainerLocatorOperator::maximum);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 0x7f) },
        "maximum locator returns the signed extremum");
    locate_container_values(
        locator_result, signed_order,
        ContainerLocatorOperator::unique);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> {
                value(8, 0x80), value(8, 0xff),
                value(8, 0), value(8, 0x7f) },
        "unique locator preserves first-occurrence order");
    ContainerValue index_result { index_result_type, { }, { } };
    locate_container_values(
        index_result, signed_order,
        ContainerLocatorOperator::unique_index);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, 0), value(32, 1),
                value(32, 3), value(32, 4) },
        "unique_index returns first current indices");
    locate_container_values(
        signed_order, signed_order,
        ContainerLocatorOperator::unique);
    require(
        signed_order.elements == locator_result.elements,
        "locator assignment safely supports an aliased queue receiver");
    ContainerType fixed_locator_type = signed_order_type;
    fixed_locator_type.queue = false;
    fixed_locator_type.fixed = true;
    fixed_locator_type.index_left = -2;
    fixed_locator_type.index_right = 1;
    ContainerValue fixed_locator {
        fixed_locator_type,
        { value(8, 5), value(8, 7), value(8, 5), value(8, 9) },
        { }
    };
    locate_container_values(
        index_result, fixed_locator,
        ContainerLocatorOperator::unique_index);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, UINT32_C(0xfffffffe)),
                value(32, UINT32_C(0xffffffff)),
                value(32, 1) },
        "static unique_index preserves signed declared indices");
    const std::vector<ContainerPredicateNode> map_five_to_ten {
        { ContainerPredicateOperator::item, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::constant, 0, 0, value(8, 5),
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::equal, 0, 1, PackedLogic4 { },
            ContainerPredicateValueKind::logical },
        { ContainerPredicateOperator::constant, 0, 0, value(8, 10),
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::conditional, 2, 3, PackedLogic4 { },
            ContainerPredicateValueKind::element, 0 }
    };
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::minimum, { }, map_five_to_ten);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 7) },
        "transformed minimum compares keys and returns the original "
        "first extremum");
    auto map_nine_to_zero = map_five_to_ten;
    map_nine_to_zero[1].constant = value(8, 9);
    map_nine_to_zero[3].constant = value(8, 0);
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::maximum, { }, map_nine_to_zero);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 7) },
        "transformed maximum compares keys and returns the original "
        "first extremum");
    auto map_seven_to_five = map_five_to_ten;
    map_seven_to_five[1].constant = value(8, 7);
    map_seven_to_five[3].constant = value(8, 5);
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::unique, { }, map_seven_to_five);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> {
                value(8, 5), value(8, 9) },
        "transformed unique preserves the first original element for "
        "each exact four-state key");
    locate_container_values(
        index_result, fixed_locator,
        ContainerLocatorOperator::unique_index, { },
        map_seven_to_five);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, UINT32_C(0xfffffffe)),
                value(32, 1) },
        "transformed unique_index returns original signed declared "
        "indices");
    const std::vector<ContainerPredicateNode> static_index_key {
        { ContainerPredicateOperator::item, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::index, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::constant, 0, 0, value(32, 0),
            ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::less, 1, 2, PackedLogic4 { },
            ContainerPredicateValueKind::logical },
        { ContainerPredicateOperator::constant, 0, 0, value(8, 0),
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::conditional, 3, 4, PackedLogic4 { },
            ContainerPredicateValueKind::element, 0 }
    };
    locate_container_values(
        index_result, fixed_locator,
        ContainerLocatorOperator::unique_index, { },
        static_index_key);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, UINT32_C(0xfffffffe)),
                value(32, 0),
                value(32, 1) },
        "locator transformations use original signed declared indices "
        "and preserve the first equal key");
    auto dynamic_index_key = static_index_key;
    dynamic_index_key[2].constant = value(32, 2);
    const ContainerValue dynamic_locator_source {
        signed_order_type,
        { value(8, 5), value(8, 7),
            value(8, 5), value(8, 9) },
        { }
    };
    locate_container_values(
        index_result, dynamic_locator_source,
        ContainerLocatorOperator::unique_index, { },
        dynamic_index_key);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, 0), value(32, 2), value(32, 3) },
        "locator transformations use original current zero-based indices");
    auto aliased_transformed = dynamic_locator_source;
    locate_container_values(
        aliased_transformed, aliased_transformed,
        ContainerLocatorOperator::unique, { },
        map_seven_to_five);
    require(
        aliased_transformed.elements
            == std::vector<PackedLogic4> {
                value(8, 5), value(8, 9) },
        "transformed locators safely support an aliased queue receiver");
    const std::vector<ContainerPredicateNode> greater_than_five {
        { ContainerPredicateOperator::item, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::constant, 0, 0, value(8, 5),
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::greater, 0, 1, PackedLogic4 { },
            ContainerPredicateValueKind::logical }
    };
    const std::vector<ContainerPredicateNode> equal_five {
        { ContainerPredicateOperator::item, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::constant, 0, 0, value(8, 5),
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::equal, 0, 1, PackedLogic4 { },
            ContainerPredicateValueKind::logical }
    };
    const std::vector<ContainerPredicateNode> negative_index {
        { ContainerPredicateOperator::index, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::constant, 0, 0, value(32, 0),
            ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::less, 0, 1, PackedLogic4 { },
            ContainerPredicateValueKind::logical }
    };
    const std::vector<ContainerPredicateNode> current_index_after_zero {
        { ContainerPredicateOperator::index, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::constant, 0, 0, value(32, 0),
            ContainerPredicateValueKind::index },
        { ContainerPredicateOperator::greater, 0, 1, PackedLogic4 { },
            ContainerPredicateValueKind::logical }
    };
    for (const auto operation :
        { ContainerLocatorOperator::find,
            ContainerLocatorOperator::find_index,
            ContainerLocatorOperator::find_first,
            ContainerLocatorOperator::find_first_index,
            ContainerLocatorOperator::find_last,
            ContainerLocatorOperator::find_last_index }) {
        const bool indices = operation == ContainerLocatorOperator::find_index
            || operation
                == ContainerLocatorOperator::find_first_index
            || operation
                == ContainerLocatorOperator::find_last_index;
        ContainerValue empty_result {
            indices ? index_result_type : signed_order_type,
            { }, { }
        };
        locate_container_values(
            empty_result, empty_locator_source, operation,
            greater_than_five);
        require(
            empty_result.elements.empty(),
            "empty predicate locators return an empty queue");
    }
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::find,
        greater_than_five);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> {
                value(8, 7), value(8, 9) },
        "find preserves declared element order");
    locate_container_values(
        index_result, fixed_locator,
        ContainerLocatorOperator::find_index,
        equal_five);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, UINT32_C(0xfffffffe)),
                value(32, 0) },
        "find_index returns signed declared static indices");
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::find,
        negative_index);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> {
                value(8, 5), value(8, 7) },
        "predicate index comparisons use signed declared static indices");
    locate_container_values(
        locator_result, signed_order,
        ContainerLocatorOperator::find,
        current_index_after_zero);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> {
                signed_order.elements[1],
                signed_order.elements[2],
                signed_order.elements[3] },
        "dynamic and queue predicate indices use current zero-based positions");
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::find_first,
        greater_than_five);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 7) },
        "find_first returns the first matching value");
    locate_container_values(
        index_result, fixed_locator,
        ContainerLocatorOperator::find_first_index,
        greater_than_five);
    require(
        index_result.elements
            == std::vector<PackedLogic4> {
                value(32, UINT32_C(0xffffffff)) },
        "find_first_index returns the first matching declared index");
    locate_container_values(
        locator_result, fixed_locator,
        ContainerLocatorOperator::find_last,
        greater_than_five);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 9) },
        "find_last returns the last matching value");
    locate_container_values(
        index_result, fixed_locator,
        ContainerLocatorOperator::find_last_index,
        greater_than_five);
    require(
        index_result.elements
            == std::vector<PackedLogic4> { value(32, 1) },
        "find_last_index returns the last matching declared index");
    ContainerValue unknown_find_result {
        four_state_order_type, { }, { }
    };
    const std::vector<ContainerPredicateNode> equal_one {
        { ContainerPredicateOperator::item, 0, 0, PackedLogic4 { },
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::constant, 0, 0, value(8, 1),
            ContainerPredicateValueKind::element },
        { ContainerPredicateOperator::equal, 0, 1, PackedLogic4 { },
            ContainerPredicateValueKind::logical }
    };
    locate_container_values(
        unknown_find_result, four_state_order,
        ContainerLocatorOperator::find,
        equal_one);
    require(
        unknown_find_result.elements
            == std::vector<PackedLogic4> { value(8, 1) },
        "unknown predicate results do not select an element");
    ContainerType bounded_find_type = signed_order_type;
    bounded_find_type.maximum_elements = 1;
    ContainerValue bounded_find { bounded_find_type, { }, { } };
    locate_container_values(
        bounded_find, fixed_locator,
        ContainerLocatorOperator::find,
        greater_than_five);
    require(
        bounded_find.elements
            == std::vector<PackedLogic4> { value(8, 7) },
        "find respects a bounded result queue");
    bounded_find.elements.clear();
    locate_container_values(
        bounded_find, fixed_locator,
        ContainerLocatorOperator::unique, { },
        map_seven_to_five);
    require(
        bounded_find.elements
            == std::vector<PackedLogic4> { value(8, 5) },
        "transformed uniqueness respects destination queue capacity");
    locate_container_values(
        locator_result, locator_result,
        ContainerLocatorOperator::find,
        greater_than_five);
    require(
        locator_result.elements
            == std::vector<PackedLogic4> { value(8, 9) },
        "predicate locator assignment safely supports an aliased queue");
    Interpreter interpreter;
    const auto array_object = interpreter.add_container_object(
        { "array", ContainerValue { array_type, { }, { } }, std::nullopt });
    const auto queue_object = interpreter.add_container_object(
        { "queue", ContainerValue { queue_type, { }, { } }, std::nullopt });
    Process process;
    process.id = 0;
    process.name = "containers";
    process.register_count = 8;
    process.container_register_count = 3;
    process.container_register_types = {
        array_type, queue_type, array_type
    };
    process.debug_locals = {
        { "size", "integer", 5, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "popped", "byte", 6, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    process.debug_container_locals = {
        { "values", 0, array_type, { } },
        { "pending", 1, queue_type, { } },
        { "copied", 2, array_type, { } }
    };
    process.operations = {
        LoadConstant { 0, value(32, 3) },
        ResizeContainer { 0, 0 },
        LoadConstant { 1, value(32, 0) },
        LoadConstant { 2, value(32, 11) },
        ContainerWrite { 0, 1, 2, true },
        LoadConstant { 1, value(32, 2) },
        LoadConstant { 2, value(32, 33) },
        ContainerWrite { 0, 1, 2, true },
        CopyContainerRegister { 2, 0 },
        LoadConstant { 0, value(32, 5) },
        ResizeContainer { 0, 0, 2 },
        WriteContainerObject { array_object, 0, std::nullopt },
        LoadConstant { 3, value(8, 1) },
        PushContainer { 1, 3, false },
        LoadConstant { 3, value(8, 2) },
        PushContainer { 1, 3, false },
        LoadConstant { 3, value(8, 3) },
        PushContainer { 1, 3, false },
        LoadConstant { 3, value(8, 4) },
        PushContainer { 1, 3, false },
        LoadConstant { 3, value(8, 9) },
        PushContainer { 1, 3, true },
        LoadConstant { 4, value(32, 1) },
        LoadConstant { 3, value(8, 7) },
        PushContainer { 1, 3, false, 4 },
        LoadConstant { 4, value(32, 2) },
        DeleteContainer { 1, 4 },
        ContainerSize { 5, 1 },
        PopContainer { 6, 1, false },
        WriteContainerObject { queue_object, 1, std::nullopt },
        Halt { }
    };
    (void)interpreter.add_process(std::move(process));
    require(
        interpreter.run().status == RunStatus::completed,
        "container process completes");
    const auto& array = interpreter.container_object_value(array_object);
    require(
        array.elements
            == std::vector<PackedLogic4> {
                value(32, 11), value(32, 0), value(32, 33),
                value(32, 0), value(32, 0) },
        "dynamic-array initialization preserves source elements and defaults "
        "the expanded tail");
    const auto& queue = interpreter.container_object_value(queue_object);
    require(
        queue.elements
            == std::vector<PackedLogic4> { value(8, 9) },
        "bounded queue insert, indexed delete, overflow, and pop ordering are "
        "deterministic");
    require(
        interpreter.read_debug_local(0, 0) == value(32, 2)
            && interpreter.read_debug_local(0, 1)
                == value(8, 7),
        "size and pop results retain exact scalar types");
    require(
        interpreter.read_debug_container_local(0, 0).elements
            == array.elements,
        "container locals remain debugger-visible");

    ContainerType logic_array_type;
    logic_array_type.element_width = 8;
    Interpreter logic_defaults;
    const auto logic_object = logic_defaults.add_container_object(
        { "logic_values", ContainerValue { logic_array_type, { }, { } },
            std::nullopt });
    Process logic_process;
    logic_process.id = 0;
    logic_process.name = "logic-dynamic-default";
    logic_process.register_count = 1;
    logic_process.container_register_count = 1;
    logic_process.container_register_types = { logic_array_type };
    logic_process.operations = {
        LoadConstant { 0, value(32, 2) }, ResizeContainer { 0, 0 },
        WriteContainerObject { logic_object, 0, std::nullopt }, Halt { }
    };
    (void)logic_defaults.add_process(std::move(logic_process));
    require(
        logic_defaults.run().status == RunStatus::completed
            && logic_defaults.container_object_value(logic_object)
                    .elements[1]
                    .to_msb_string()
                == "XXXXXXXX",
        "new[size] defaults four-state dynamic-array elements to X");

    ContainerType conditional_type;
    conditional_type.element_width = 8;
    Interpreter conditional_interpreter;
    const auto when_true_object = conditional_interpreter.add_container_object({ "when_true",
        ContainerValue {
            conditional_type,
            { value(8, 0x11), value(8, 0x22) }, { } },
        std::nullopt });
    const auto when_false_object = conditional_interpreter.add_container_object({ "when_false",
        ContainerValue {
            conditional_type,
            { value(8, 0x11), value(8, 0x2a) }, { } },
        std::nullopt });
    const auto short_object = conditional_interpreter.add_container_object({ "short",
        ContainerValue {
            conditional_type, { value(8, 0x11) }, { } },
        std::nullopt });
    const auto selected_object = conditional_interpreter.add_container_object({ "selected", default_container_value(conditional_type),
        std::nullopt });
    const auto merged_object = conditional_interpreter.add_container_object({ "merged", default_container_value(conditional_type),
        std::nullopt });
    const auto shape_object = conditional_interpreter.add_container_object({ "shape", default_container_value(conditional_type),
        std::nullopt });
    Process conditional_process;
    conditional_process.id = 0;
    conditional_process.name = "container-conditional";
    conditional_process.register_count = 1;
    conditional_process.container_register_count = 6;
    conditional_process.container_register_types.assign(
        6, conditional_type);
    conditional_process.operations = {
        ReadContainerObject { 0, when_true_object },
        ReadContainerObject { 1, when_false_object },
        ReadContainerObject { 2, short_object },
        LoadConstant { 0, value(1, 1) },
        ConditionalContainerSelect { 3, 0, 0, 1 },
        WriteContainerObject { selected_object, 3, std::nullopt },
        LoadConstant {
            0, PackedLogic4::from_aval_bval(1, 1, 1) },
        ConditionalContainerSelect { 4, 0, 0, 1 },
        WriteContainerObject { merged_object, 4, std::nullopt },
        ConditionalContainerSelect { 5, 0, 0, 2 },
        WriteContainerObject { shape_object, 5, std::nullopt },
        Halt { }
    };
    (void)conditional_interpreter.add_process(
        std::move(conditional_process));
    require(
        conditional_interpreter.run().status
            == RunStatus::completed,
        "container conditional process completes");
    require(
        conditional_interpreter
                .container_object_value(selected_object)
                .elements
            == std::vector<PackedLogic4> {
                value(8, 0x11), value(8, 0x22) },
        "known container conditional selects one isolated snapshot");
    const auto& merged = conditional_interpreter.container_object_value(merged_object);
    require(
        merged.elements.size() == 2
            && merged.elements[0] == value(8, 0x11)
            && merged.elements[1].low_word().aval == 0x2a
            && merged.elements[1].low_word().bval == 0x08,
        "unknown container conditional merges equal-shape elements");
    require(
        conditional_interpreter
            .container_object_value(shape_object)
            .elements.empty(),
        "unknown nonstatic container conditional resets unequal shapes");

    ContainerType associative_type;
    associative_type.element_width = 8;
    associative_type.associative = true;
    associative_type.index_width = 8;
    associative_type.signed_indices = true;
    auto wide_associative_type = associative_type;
    wide_associative_type.index_width = 137;
    wide_associative_type.signed_indices = false;
    auto wide_low_key = PackedLogic4(137, Logic4::zero);
    auto wide_high_key = PackedLogic4(137, Logic4::zero);
    wide_low_key.set(96, Logic4::one);
    wide_high_key.set(136, Logic4::one);
    const ContainerValue wide_keys {
        wide_associative_type,
        { value(8, 0x11), value(8, 0x22) },
        { wide_low_key, wide_high_key }
    };
    Interpreter wide_key_validator;
    (void)wide_key_validator.add_container_object(
        { "wide_keys", wide_keys, std::nullopt });
    require(
        wide_keys.keys[0].width() == 137
            && wide_keys.keys[0].get(96) == Logic4::one
            && wide_keys.keys[1].get(136) == Logic4::one,
        "associative keys preserve and order bits above the host word");
    auto unknown_wide_keys = wide_keys;
    unknown_wide_keys.keys[0].set(96, Logic4::x);
    try {
        Interpreter invalid_wide_key_validator;
        (void)invalid_wide_key_validator.add_container_object(
            { "unknown_wide_keys", unknown_wide_keys, std::nullopt });
        require(false,
            "unknown associative key bits above the host word must reject");
    } catch (const std::invalid_argument&) {
    }
    Interpreter associative;
    const auto associative_object = associative.add_container_object(
        { "lookup",
            ContainerValue { associative_type, { }, { } },
            std::nullopt });
    Process associative_process;
    associative_process.id = 0;
    associative_process.name = "associative";
    associative_process.register_count = 14;
    associative_process.container_register_count = 1;
    associative_process.container_register_types = {
        associative_type
    };
    associative_process.debug_locals = {
        { "size", "int", 2, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "exists", "int", 3, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "missing", "byte", 4, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "first_key", "byte", 7, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "next_key", "byte", 9, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "last_key", "byte", 11, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "previous_key", "byte", 13, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    associative_process.operations = {
        LoadConstant { 0, value(8, 2) },
        LoadConstant { 1, value(8, 22) },
        ContainerWrite { 0, 0, 1, true },
        LoadConstant { 0, value(8, 0xff) },
        LoadConstant { 1, value(8, 11) },
        ContainerWrite { 0, 0, 1, true },
        LoadConstant { 0, value(8, 7) },
        LoadConstant { 1, value(8, 77) },
        ContainerWrite { 0, 0, 1, true },
        ContainerSize { 2, 0 },
        LoadConstant { 0, value(8, 2) },
        ContainerExists { 3, 0, 0 },
        LoadConstant { 0, value(8, 3) },
        ContainerRead { 4, 0, 0, true },
        LoadConstant { 5, value(8, 0) },
        TraverseContainer {
            6, 0, 5, ContainerTraversal::first },
        CopyRegister { 7, 5 },
        TraverseContainer {
            8, 0, 5, ContainerTraversal::next },
        CopyRegister { 9, 5 },
        TraverseContainer {
            10, 0, 5, ContainerTraversal::last },
        CopyRegister { 11, 5 },
        TraverseContainer {
            12, 0, 5, ContainerTraversal::previous },
        CopyRegister { 13, 5 },
        LoadConstant { 0, value(8, 2) },
        DeleteContainer { 0, 0 },
        WriteContainerObject { associative_object, 0, std::nullopt },
        Halt { }
    };
    (void)associative.add_process(
        std::move(associative_process));
    require(
        associative.run().status == RunStatus::completed,
        "associative-array process completes");
    const auto& lookup = associative.container_object_value(associative_object);
    require(
        lookup.keys
                == std::vector<PackedLogic4> {
                    value(8, 0xff), value(8, 7) }
            && lookup.elements == std::vector<PackedLogic4> { value(8, 11), value(8, 77) },
        "signed keys are canonically ordered and delete removes one pair");
    require(
        associative.read_debug_local(0, 0) == value(32, 3)
            && associative.read_debug_local(0, 1)
                == value(32, 1)
            && associative.read_debug_local(0, 2)
                == value(8, 0),
        "size, exists, and missing reads have deterministic results");
    require(
        associative.read_debug_local(0, 3)
                == value(8, 0xff)
            && associative.read_debug_local(0, 4)
                == value(8, 2)
            && associative.read_debug_local(0, 5)
                == value(8, 7)
            && associative.read_debug_local(0, 6)
                == value(8, 2),
        "first/next/last/previous traverse canonical key order");

    auto string_associative_type = associative_type;
    string_associative_type.index_width = 0;
    string_associative_type.signed_indices = false;
    string_associative_type.two_state_indices = true;
    string_associative_type.string_indices = true;
    Interpreter string_associative;
    const auto string_associative_object = string_associative.add_container_object(
        { "string_lookup",
            default_container_value(string_associative_type),
            std::nullopt });
    Process string_associative_process;
    string_associative_process.id = 0;
    string_associative_process.name = "string_associative";
    string_associative_process.register_count = 4;
    string_associative_process.string_register_count = 2;
    string_associative_process.container_register_count = 1;
    string_associative_process.container_register_types = {
        string_associative_type
    };
    string_associative_process.debug_locals = {
        { "exists", "int", 1, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "traversed", "int", 2, 32, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } },
        { "selected", "byte", 3, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    string_associative_process.debug_string_locals = {
        { "first_key", 1, { } }
    };
    string_associative_process.operations = {
        LoadStringConstant { 0, "beta" },
        LoadConstant { 0, value(8, 22) },
        ContainerWrite { 0, 0, 0, true, false, true },
        LoadStringConstant { 0, "alpha" },
        LoadConstant { 0, value(8, 11) },
        ContainerWrite { 0, 0, 0, true, false, true },
        LoadStringConstant { 0, "beta" },
        ContainerExists { 1, 0, 0, true },
        LoadStringConstant { 1, "" },
        TraverseContainer {
            2, 0, 1, ContainerTraversal::first, true },
        ContainerRead { 3, 0, 1, true, false, true },
        DeleteContainer { 0, 0, true },
        WriteContainerObject {
            string_associative_object, 0, std::nullopt },
        Halt { }
    };
    (void)string_associative.add_process(
        std::move(string_associative_process));
    require(
        string_associative.run().status == RunStatus::completed,
        "string-indexed associative-array process completes");
    const auto& string_lookup = string_associative.container_object_value(
        string_associative_object);
    require(
        string_lookup.string_keys == std::vector<std::string> { "alpha" }
            && string_lookup.keys.empty()
            && string_lookup.elements
                == std::vector<PackedLogic4> { value(8, 11) },
        "string keys use isolated lexicographic identity and paired deletion");
    require(
        string_associative.read_debug_local(0, 0) == value(32, 1)
            && string_associative.read_debug_local(0, 1)
                == value(32, 1)
            && string_associative.read_debug_local(0, 2)
                == value(8, 11)
            && string_associative.read_debug_string_local(0, 0)
                == "alpha",
        "string-key exists, traversal, and selection are deterministic");

    const auto require_invalid_string_keys =
        [&](ContainerValue invalid) {
            try {
                Interpreter rejected;
                (void)rejected.add_container_object(
                    { "invalid_string_keys", std::move(invalid), std::nullopt });
                require(false, "invalid associative string keys must reject");
            } catch (const std::invalid_argument&) {
            }
        };
    auto unordered_string_keys = default_container_value(string_associative_type);
    unordered_string_keys.string_keys = { "beta", "alpha" };
    unordered_string_keys.elements = { value(8, 2), value(8, 1) };
    require_invalid_string_keys(std::move(unordered_string_keys));
    auto arbitrary_byte_key = default_container_value(string_associative_type);
    arbitrary_byte_key.string_keys = { std::string { "\xc0\x80", 2 } };
    arbitrary_byte_key.elements = { value(8, 1) };
    Interpreter byte_key_interpreter;
    const auto byte_key_object = byte_key_interpreter.add_container_object(
        { "arbitrary_byte_key", std::move(arbitrary_byte_key), std::nullopt });
    require(
        byte_key_interpreter.container_object_value(byte_key_object).string_keys
            == std::vector<std::string> { std::string { "\xc0\x80", 2 } },
        "associative string keys preserve arbitrary bytes");
    auto oversized_string_key = default_container_value(string_associative_type);
    oversized_string_key.string_keys = {
        std::string(maximum_string_bytes + 1U, 'x')
    };
    oversized_string_key.elements = { value(8, 1) };
    require_invalid_string_keys(std::move(oversized_string_key));

    ContainerType static_type;
    static_type.element_width = 8;
    static_type.fixed = true;
    static_type.index_left = 2;
    static_type.index_right = -1;
    auto static_initial = default_container_value(static_type);
    require(
        static_initial.elements.size() == 4
            && static_initial.elements.front().to_msb_string()
                == "XXXXXXXX",
        "four-state static arrays materialize their full declared range "
        "with X defaults");
    auto oversized_static_type = static_type;
    oversized_static_type.index_left = std::numeric_limits<std::int32_t>::min();
    oversized_static_type.index_right = std::numeric_limits<std::int32_t>::max();
    try {
        (void)default_container_value(oversized_static_type);
        require(false, "oversized static type must fail before allocation");
    } catch (const std::length_error&) {
    }
    Interpreter fixed;
    const auto fixed_object = fixed.add_container_object(
        { "fixed", static_initial, std::nullopt });
    std::vector<std::pair<ContainerObjectId, SimulationTick>> fixed_changes;
    fixed.set_container_object_change_hook(
        [&](const auto object, const auto time) {
            fixed_changes.emplace_back(object, time);
        });
    Process fixed_process;
    fixed_process.id = 0;
    fixed_process.name = "fixed";
    fixed_process.register_count = 3;
    fixed_process.container_register_count = 2;
    fixed_process.container_register_types = {
        static_type, static_type
    };
    fixed_process.debug_locals = {
        { "selected", "byte", 2, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    fixed_process.operations = {
        LoadConstant {
            0, value(32, static_cast<std::uint32_t>(-1)) },
        LoadConstant { 1, value(8, 0x5a) },
        ContainerWrite { 0, 0, 1, true },
        ContainerRead { 2, 0, 0, true },
        CopyContainerRegister { 1, 0 },
        WriteContainerObject { fixed_object, 1, std::nullopt },
        Halt { }
    };
    (void)fixed.add_process(std::move(fixed_process));
    require(
        fixed.run().status == RunStatus::completed
            && fixed.read_debug_local(0, 0) == value(8, 0x5a)
            && fixed.container_object_value(fixed_object)
                    .elements[3]
                == value(8, 0x5a)
            && fixed_changes
                == std::vector<std::pair<ContainerObjectId, SimulationTick>> {
                    { fixed_object, 0 } },
        "descending signed static indices map to dense declared-order "
        "storage and whole copies publish one base-container change");
    auto fixed_replacement = fixed.container_object_value(fixed_object);
    fixed.deposit_container_object(fixed_object, fixed_replacement);
    fixed_replacement.elements.front() = value(8, 0xa5);
    fixed.deposit_container_object(fixed_object, fixed_replacement);
    require(
        fixed_changes.size() == 2 && fixed_changes.back().first == fixed_object
            && fixed_changes.back().second == 0,
        "container change hooks suppress unchanged deposits and publish changed deposits");

    Interpreter bridged;
    auto bridged_type = static_type;
    bridged_type.dimensions = { { 2, -1 } };
    const auto bridged_signal = bridged.add_signal(
        { "bridged.storage", value(32, 0x11223344U) });
    const auto bridged_object = bridged.add_container_object(
        { "bridged", default_container_value(bridged_type), std::nullopt });
    bridged.add_container_signal_alias(
        { bridged_object, bridged_signal, true, true });
    Process bridged_process;
    bridged_process.id = 0;
    bridged_process.name = "bridged_element_write";
    bridged_process.register_count = 2;
    bridged_process.operations = {
        LoadConstant { 0, value(32, 1) },
        LoadConstant { 1, value(8, 0xaa) },
        WriteContainerObjectElement {
            bridged_object, 0, 1, true, false, false, std::nullopt,
            std::nullopt },
        Halt { }
    };
    (void)bridged.add_process(std::move(bridged_process));
    require(
        bridged.run().status == RunStatus::completed
            && bridged.signal_value(bridged_signal)
                == value(32, 0x11aa3344U)
            && bridged.container_object_value(bridged_object).elements[1]
                == value(8, 0xaa),
        "signal-backed element writes update one packed slice without "
        "requiring a whole-container snapshot");
    bridged.deposit_signal(
        bridged_signal, value(32, 0x55667788U));
    require(
        bridged.container_object_value(bridged_object).elements
            == std::vector<PackedLogic4> {
                value(8, 0x55), value(8, 0x66),
                value(8, 0x77), value(8, 0x88) }
            && bridged.container_object_value(bridged_object).elements[2]
                == value(8, 0x77),
        "signal-backed container materialization refreshes after a backing "
        "signal revision and remains coherent on repeated reads");

    {
        Interpreter cross_domain;
        auto origin_type = static_type;
        origin_type.index_left = 0;
        origin_type.index_right = 0;
        origin_type.dimensions = { { 0, 0 } };
        const auto backing = cross_domain.add_signal(
            { "container_origin.backing", value(8, 0) });
        const auto transaction = cross_domain.add_signal(
            { "container_origin.transaction", value(1, 1) });
        const auto object = cross_domain.add_container_object({
            "container_origin.array",
            default_container_value(origin_type), std::nullopt
        });
        cross_domain.add_container_signal_alias(
            { object, backing, true, true });

        struct Dispatch {
            ProcessId process { };
            SchedulerPhase phase { SchedulerPhase::active };
            SimulationTick time { };
            std::uint64_t delta { };
            std::uint64_t systemverilog_round { };
            PackedLogic4 container_element;
        };
        std::vector<Dispatch> dispatches;
        cross_domain.set_output_hook(
            [&](const ProcessId process,
                const std::string_view,
                const bool,
                const SimulationTick time,
                const std::uint64_t) {
                const auto phase = cross_domain.scheduler().current_phase();
                require(phase.has_value(),
                    "container waiter must run in a scheduler phase");
                dispatches.push_back({
                    process, *phase, time,
                    cross_domain.scheduler().delta(),
                    cross_domain.scheduler().systemverilog_round(),
                    cross_domain.container_object_value(object)
                        .elements.front(),
                });
            });
        const auto add_waiter = [&](const ProcessId id,
                                    const std::string_view name,
                                    const SignalId signal,
                                    const ProcessSchedulingDomain domain) {
            Process waiter;
            waiter.id = id;
            waiter.name = name;
            waiter.scheduling_domain = domain;
            waiter.operations = {
                WaitOn { { signal } },
                Display { std::string { name }, true },
                Halt { },
            };
            return cross_domain.add_process(std::move(waiter));
        };
        const auto generic_backing = add_waiter(
            0U, "generic backing waiter", backing,
            ProcessSchedulingDomain::generic);
        const auto sv_backing = add_waiter(
            1U, "SV backing waiter", backing,
            ProcessSchedulingDomain::systemverilog);
        const auto generic_transaction = add_waiter(
            2U, "generic transaction waiter", transaction,
            ProcessSchedulingDomain::generic);
        const auto sv_transaction = add_waiter(
            3U, "SV transaction waiter", transaction,
            ProcessSchedulingDomain::systemverilog);

        Process writer;
        writer.id = 4U;
        writer.name = "container_origin_systemverilog_writer";
        writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        writer.register_count = 2U;
        writer.operations = {
            WaitFor { 1U },
            LoadConstant { 0U, value(32U, 0U) },
            LoadConstant { 1U, value(8U, 0xaaU) },
            WriteContainerObjectElement {
                object, 0U, 1U, true, false, true,
                transaction, std::nullopt,
            },
            Halt { },
        };
        (void)cross_domain.add_process(std::move(writer));

        const auto result = cross_domain.run();
        const auto dispatch_for = [&](const ProcessId process) {
            return std::ranges::find(
                dispatches, process, &Dispatch::process);
        };
        const auto generic_backing_dispatch
            = dispatch_for(generic_backing);
        const auto sv_backing_dispatch = dispatch_for(sv_backing);
        const auto generic_transaction_dispatch
            = dispatch_for(generic_transaction);
        const auto sv_transaction_dispatch = dispatch_for(sv_transaction);
        require(
            result.status == RunStatus::completed
                && dispatches.size() == 4U
                && generic_backing_dispatch != dispatches.end()
                && sv_backing_dispatch != dispatches.end()
                && generic_transaction_dispatch != dispatches.end()
                && sv_transaction_dispatch != dispatches.end()
                && cross_domain.signal_value(backing) == value(8U, 0xaaU)
                && cross_domain.signal_value(transaction) == value(1U, 0U)
                && cross_domain.container_object_value(object)
                    .elements.front() == value(8U, 0xaaU),
            "SV container NBA publishes its aliased value and transaction");
        require(
            generic_backing_dispatch->phase == SchedulerPhase::active
                && generic_backing_dispatch->time == 1U
                && generic_backing_dispatch->delta > 0U
                && generic_transaction_dispatch->phase
                    == SchedulerPhase::active
                && generic_transaction_dispatch->time == 1U
                && generic_transaction_dispatch->delta > 0U,
            "container NBA wakes generic observers across a generic cycle");
        require(
            sv_backing_dispatch->phase == SchedulerPhase::active
                && sv_backing_dispatch->time == 1U
                && sv_backing_dispatch->systemverilog_round > 0U
                && sv_transaction_dispatch->phase == SchedulerPhase::active
                && sv_transaction_dispatch->time == 1U
                && sv_transaction_dispatch->systemverilog_round > 0U,
            "container NBA wakes SV observers in Active rounds");
        require(
            generic_transaction_dispatch->container_element == value(8U, 0xaaU)
                && sv_transaction_dispatch->container_element == value(8U, 0xaaU),
            "transaction observers see the aliased container publication when they wake");
    }

    Interpreter element_nets;
    auto element_net_type = fixed_type(static_type, 4, 3);
    element_net_type.element_width = 8U;
    const auto element_net_zero = element_nets.add_signal(
        { "element_net[4]", value(8, 0x12U) });
    const auto element_net_one = element_nets.add_signal(
        { "element_net[3]", value(8, 0x34U) });
    const auto element_net_object = element_nets.add_container_object(
        { "element_net", default_container_value(element_net_type),
            std::nullopt });
    element_nets.add_container_element_signal_alias(
        { element_net_object, 0U, element_net_zero, true, true });
    element_nets.add_container_element_signal_alias(
        { element_net_object, 1U, element_net_one, true, true });
    std::vector<std::pair<std::size_t, PackedLogic4>> element_changes;
    element_nets.set_container_element_change_hook(
        [&](const auto object, const auto ordinal, const auto& changed,
            const auto) {
            require(object == element_net_object,
                "element-net publication retains the logical array handle");
            element_changes.emplace_back(ordinal, changed);
        });
    require(
        element_nets.container_object_value(element_net_object).elements
            == std::vector<PackedLogic4> { value(8, 0x12U),
                value(8, 0x34U) },
        "logical fixed-array reads compose independently stored elements");
    element_nets.deposit_signal(element_net_one, value(8, 0x35U));
    require(
        element_nets.container_object_value(element_net_object).elements[1]
            == value(8, 0x35U),
        "direct leaf-signal deposits update logical array observations");
    std::size_t force_stored_notifications { };
    element_nets.set_stored_signal_change_hook(
        [&](const SignalId, const SimulationTick) {
            ++force_stored_notifications;
        });
    element_nets.force_signal(element_net_zero, value(8, 0x56U));
    require(
        element_nets.container_object_value(element_net_object).elements[0]
            == value(8, 0x56U),
        "logical array reads expose a forced element's effective value");
    element_nets.release_signal(element_net_zero);
    require(
        element_nets.container_object_value(element_net_object).elements[0]
            == value(8, 0x12U),
        "releasing one element force restores its stored value");
    require(force_stored_notifications == 0U,
        "element force and release do not publish stored-value changes");
    element_nets.set_stored_signal_change_hook({ });
    element_nets.deposit_container_object_element(
        element_net_object, 1U, value(8, 0x78U));
    auto element_net_replacement
        = element_nets.container_object_value(element_net_object);
    element_net_replacement.elements = { value(8, 0x9aU),
        value(8, 0xbcU) };
    element_nets.deposit_container_object(
        element_net_object, element_net_replacement);
    const auto dynamic_element_observation = element_nets.add_signal(
        { "element_net.dynamic_observation", value(8, 0U) });
    Process dynamic_element_reader;
    dynamic_element_reader.id = 0U;
    dynamic_element_reader.name = "dynamic_element_reader";
    dynamic_element_reader.register_count = 2U;
    dynamic_element_reader.container_register_count = 1U;
    dynamic_element_reader.container_register_types = { element_net_type };
    dynamic_element_reader.operations = {
        ReadContainerObject { 0U, element_net_object },
        LoadConstant { 0U, value(32U, 3U) },
        ContainerRead { 1U, 0U, 0U, true },
        WriteBlocking { dynamic_element_observation, 1U },
        Halt { }
    };
    (void)element_nets.add_process(std::move(dynamic_element_reader));
    require(element_nets.run().status == RunStatus::completed,
        "element alias dynamic reader completes");
    require(element_nets.signal_value(dynamic_element_observation)
            == value(8, 0xbcU),
        "element alias dynamic index reads the selected leaf");
    require(element_nets.signal_value(element_net_zero) == value(8, 0x9aU)
            && element_nets.signal_value(element_net_one) == value(8, 0xbcU),
        "whole-array deposit installs both element values");
    require(element_nets.container_object_value(element_net_object).elements
            == element_net_replacement.elements,
        "whole-array reads agree with the deposited element values");
    const auto expected_element_changes
        = std::vector<std::pair<std::size_t, PackedLogic4>> {
            { 1U, value(8, 0x35U) },
            { 0U, value(8, 0x56U) },
            { 0U, value(8, 0x12U) },
            { 1U, value(8, 0x78U) },
            { 0U, value(8, 0x9aU) },
            { 1U, value(8, 0xbcU) } };
    require(element_changes.size() == expected_element_changes.size(),
        "element alias callback count: expected "
            + std::to_string(expected_element_changes.size()) + ", got "
            + std::to_string(element_changes.size()));
    for (std::size_t index = 0U; index < element_changes.size(); ++index) {
        require(element_changes[index] == expected_element_changes[index],
            "element alias callback " + std::to_string(index)
                + " observed ordinal "
                + std::to_string(element_changes[index].first)
                + " value " + element_changes[index].second.to_msb_string());
    }

    Interpreter driven_element_nets;
    auto driven_element_type = fixed_type(static_type, 1, 0);
    driven_element_type.element_width = 4U;
    const auto driven_element_first = driven_element_nets.add_signal(
        { "driven_element_net[1]",
            PackedLogic4::from_msb_string("ZZZZ"),
            ResolutionKind::sv_wire });
    const auto driven_element_second = driven_element_nets.add_signal(
        { "driven_element_net[0]",
            PackedLogic4::from_msb_string("ZZZZ"),
            ResolutionKind::sv_wire });
    const auto driven_element_aggregate
        = driven_element_nets.add_signal(
            { "driven_element_net",
                PackedLogic4::from_msb_string("ZZZZZZZZ"),
                ResolutionKind::sv_wire });
    const auto driven_element_object
        = driven_element_nets.add_container_object(
            { "driven_element_net",
                default_container_value(driven_element_type),
                std::nullopt });
    driven_element_nets.add_container_element_signal_alias(
        { driven_element_object, 0U, driven_element_first, true, true });
    driven_element_nets.add_container_element_signal_alias(
        { driven_element_object, 1U, driven_element_second, true, true });
    driven_element_nets.add_container_aggregate_signal_alias(
        { driven_element_object, driven_element_aggregate, true, false });
    require(
        driven_element_nets.signal_value(driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZZZZZZZ")
            && driven_element_nets.stored_signal_value(
                   driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZZZZZZZ"),
        "aggregate signal projections begin from authoritative leaf values");
    std::vector<std::pair<std::size_t, std::string>> driven_element_changes;
    std::vector<ContainerObjectId> driven_object_changes;
    driven_element_nets.set_container_object_change_hook(
        [&](const auto object, const auto) {
            driven_object_changes.push_back(object);
        });
    driven_element_nets.set_container_element_change_hook(
        [&](const auto object, const auto ordinal, const auto& changed,
            const auto) {
            require(object == driven_element_object,
                "driver publication retains the logical array object");
            driven_element_changes.emplace_back(
                ordinal, changed.to_msb_string());
        });
    ProcessId next_element_writer { };
    const auto make_element_writer = [&](const std::string_view name,
                                         const SignalId signal,
                                         const std::uint32_t index,
                                         const PackedLogic4& data,
                                         const bool nonblocking,
                                         const std::optional<DynamicPartIndex>& part) {
        Process writer;
        writer.id = next_element_writer++;
        writer.name = std::string { name };
        writer.register_count = 3U;
        writer.driver_regions.push_back({ signal, 0U, 4U, false });
        writer.operations = {
            LoadConstant { 0U, value(32U, index) },
            LoadConstant { 1U, data },
            LoadConstant { 2U, value(32U, 0U) },
            WriteContainerObjectElement {
                driven_element_object, 0U, 1U, true, false,
                nonblocking, std::nullopt, part },
            Halt { },
        };
        return driven_element_nets.add_process(std::move(writer));
    };
    const DynamicPartIndex low_pair {
        2U, 3, 0, 0U, 2U, true, true };
    const auto first_element_driver = make_element_writer(
        "element_net_first_driver", driven_element_first, 1U,
        PackedLogic4::from_msb_string("10"), false, low_pair);
    const auto second_element_driver = make_element_writer(
        "element_net_second_driver", driven_element_first, 1U,
        PackedLogic4::from_msb_string("01"), true, low_pair);
    const auto independent_element_driver = make_element_writer(
        "element_net_independent_driver", driven_element_second, 0U,
        PackedLogic4::from_msb_string("1100"), false, std::nullopt);
    require(
        driven_element_nets.run().status == RunStatus::completed
            && driven_element_nets.driver_value(
                   first_element_driver, driven_element_first)
                == PackedLogic4::from_msb_string("ZZ10")
            && driven_element_nets.driver_value(
                   second_element_driver, driven_element_first)
                == PackedLogic4::from_msb_string("ZZ01")
            && driven_element_nets.signal_value(driven_element_first)
                == PackedLogic4::from_msb_string("ZZXX")
            && driven_element_nets.driver_value(
                   independent_element_driver, driven_element_second)
                == PackedLogic4::from_msb_string("1100")
            && driven_element_nets.signal_value(driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZXX1100")
            && driven_element_nets.stored_signal_value(
                   driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZXX1100")
            && driven_element_nets.driver_value(
                   first_element_driver, driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZ10ZZZZ")
            && driven_element_nets.driver_value(
                   independent_element_driver, driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZZZ1100")
            && driven_element_nets.container_object_value(
                   driven_element_object).elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("ZZXX"),
                    PackedLogic4::from_msb_string("1100") }
            && driven_element_changes
                == std::vector<std::pair<std::size_t, std::string>> {
                    { 0U, "ZZ10" }, { 1U, "1100" }, { 0U, "ZZXX" } }
            && driven_object_changes
                == std::vector<ContainerObjectId> {
                    driven_element_object, driven_element_object,
                    driven_element_object },
        "element-backed dynamic and nonblocking writes preserve each leaf's "
        "original driver and publish each observable element change");

    driven_element_nets.force_signal(
        driven_element_first, PackedLogic4::from_msb_string("0000"));
    require(
        driven_element_nets.signal_is_forced(driven_element_aggregate)
            && driven_element_nets.signal_value(driven_element_aggregate)
                == PackedLogic4::from_msb_string("00001100")
            && driven_element_nets.stored_signal_value(
                   driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZXX1100"),
        "aggregate handles project forced current values separately from "
        "their stored raw-driver resolution");
    auto aggregate_deposit_rejected = false;
    try {
        driven_element_nets.deposit_signal(
            driven_element_aggregate,
            PackedLogic4::from_msb_string("00000000"));
    } catch (const std::logic_error&) {
        aggregate_deposit_rejected = true;
    }
    require(
        aggregate_deposit_rejected
            && driven_element_nets.signal_value(driven_element_aggregate)
                == PackedLogic4::from_msb_string("00001100"),
        "the incomplete aggregate mutation bridge rejects a write without "
        "altering authoritative element storage");
    driven_element_nets.release_signal(driven_element_first);
    require(
        !driven_element_nets.signal_is_forced(driven_element_aggregate)
            && driven_element_nets.signal_value(driven_element_aggregate)
                == PackedLogic4::from_msb_string("ZZXX1100"),
        "aggregate forced status clears after the final element release");

    Interpreter writable_aggregate;
    auto writable_aggregate_type = fixed_type(static_type, 1, 0);
    writable_aggregate_type.element_width = 4U;
    const auto writable_aggregate_first = writable_aggregate.add_signal(
        { "writable_aggregate[1]", PackedLogic4(4U, Logic4::z),
            ResolutionKind::sv_wire });
    const auto writable_aggregate_second = writable_aggregate.add_signal(
        { "writable_aggregate[0]", PackedLogic4(4U, Logic4::z),
            ResolutionKind::sv_wire });
    const auto writable_aggregate_proxy = writable_aggregate.add_signal(
        { "writable_aggregate", PackedLogic4(8U, Logic4::z),
            ResolutionKind::sv_wire });
    const auto writable_aggregate_object
        = writable_aggregate.add_container_object({
            "writable_aggregate",
            ContainerValue {
                writable_aggregate_type,
                { PackedLogic4(4U, Logic4::z),
                    PackedLogic4(4U, Logic4::z) },
                { } },
            std::nullopt });
    writable_aggregate.add_container_element_signal_alias({
        writable_aggregate_object, 0U, writable_aggregate_first,
        true, true });
    writable_aggregate.add_container_element_signal_alias({
        writable_aggregate_object, 1U, writable_aggregate_second,
        true, true });
    writable_aggregate.add_container_aggregate_signal_alias({
        writable_aggregate_object, writable_aggregate_proxy, true, true });
    auto writable_aggregate_slice_type = writable_aggregate_type;
    writable_aggregate_slice_type.index_right = 1;
    writable_aggregate_slice_type.dimensions = { { 1, 1 } };
    const auto writable_aggregate_slice
        = writable_aggregate.add_container_object({
            "writable_aggregate.slice",
            ContainerValue {
                writable_aggregate_slice_type,
                { PackedLogic4(4U, Logic4::z) },
                { } },
            ContainerSliceAlias { writable_aggregate_object, 1, 1 } });
    const auto writable_proxy_view
        = writable_aggregate.add_container_object({
            "writable_aggregate.proxy_view",
            ContainerValue {
                writable_aggregate_type,
                { PackedLogic4(4U, Logic4::z),
                    PackedLogic4(4U, Logic4::z) },
                { } },
            std::nullopt });
    writable_aggregate.add_container_signal_alias({
        writable_proxy_view, writable_aggregate_proxy, true, false });
    auto writable_proxy_slice_type = writable_aggregate_type;
    writable_proxy_slice_type.index_right = 1;
    writable_proxy_slice_type.dimensions = { { 1, 1 } };
    const auto writable_proxy_slice
        = writable_aggregate.add_container_object({
            "writable_aggregate.proxy_slice",
            ContainerValue {
                writable_proxy_slice_type,
                { PackedLogic4(4U, Logic4::z) },
                { } },
            ContainerSliceAlias { writable_proxy_view, 1, 1 } });
    const auto* const retained_writable_aggregate
        = &writable_aggregate.container_object_value(
            writable_aggregate_object);
    const auto* const retained_writable_aggregate_slice
        = &writable_aggregate.container_object_value(
            writable_aggregate_slice);
    const auto* const retained_writable_proxy_view
        = &writable_aggregate.container_object_value(writable_proxy_view);
    const auto* const retained_writable_proxy_slice
        = &writable_aggregate.container_object_value(writable_proxy_slice);
    const auto* const retained_writable_elements
        = retained_writable_aggregate->elements.data();
    const auto* const retained_writable_slice_elements
        = retained_writable_aggregate_slice->elements.data();
    const auto* const retained_writable_proxy_elements
        = retained_writable_proxy_view->elements.data();
    const auto* const retained_writable_proxy_slice_elements
        = retained_writable_proxy_slice->elements.data();
    std::vector<std::string> aggregate_proxy_publications;
    std::vector<std::string> proxy_view_callback_values;
    std::vector<std::size_t> writable_element_publications;
    std::vector<ContainerObjectId> writable_object_publications;
    std::vector<std::string> writable_object_values;
    writable_aggregate.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4& changed,
            const SimulationTick) {
            if (signal == writable_aggregate_proxy) {
                aggregate_proxy_publications.push_back(
                    changed.to_msb_string());
                proxy_view_callback_values.push_back(
                    retained_writable_proxy_view->elements[0U]
                        .to_msb_string()
                    + retained_writable_proxy_view->elements[1U]
                        .to_msb_string()
                    + ":"
                    + retained_writable_proxy_slice->elements[0U]
                        .to_msb_string());
            }
        });
    writable_aggregate.set_container_element_change_hook(
        [&](const ContainerObjectId object, const std::size_t ordinal,
            const PackedLogic4&, const SimulationTick) {
            if (object == writable_aggregate_object) {
                writable_element_publications.push_back(ordinal);
            }
        });
    writable_aggregate.set_container_object_change_hook(
        [&](const ContainerObjectId object, const SimulationTick) {
            writable_object_publications.push_back(object);
            const auto& elements
                = writable_aggregate.container_object_value(object).elements;
            writable_object_values.push_back(
                elements[0].to_msb_string()
                + elements[1].to_msb_string());
        });
    const auto aggregate_deposit
        = PackedLogic4::from_msb_string("10101011");
    writable_aggregate.deposit_signal(
        writable_aggregate_proxy, aggregate_deposit);
    require(
        writable_aggregate.signal_value(writable_aggregate_proxy)
                == aggregate_deposit
            && writable_aggregate.stored_signal_value(
                   writable_aggregate_proxy)
                == aggregate_deposit
            && writable_aggregate.signal_value(writable_aggregate_first)
                == PackedLogic4::from_msb_string("1010")
            && writable_aggregate.signal_value(writable_aggregate_second)
                == PackedLogic4::from_msb_string("1011")
            && retained_writable_aggregate->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("1011") }
            && retained_writable_aggregate_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010") }
            && retained_writable_aggregate->elements.data()
                == retained_writable_elements
            && retained_writable_aggregate_slice->elements.data()
                == retained_writable_slice_elements
            && retained_writable_proxy_view->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("1011") }
            && retained_writable_proxy_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010") }
            && retained_writable_proxy_view->elements.data()
                == retained_writable_proxy_elements
            && retained_writable_proxy_slice->elements.data()
                == retained_writable_proxy_slice_elements
            && proxy_view_callback_values
                == std::vector<std::string> { "10101011:1010" }
            && aggregate_proxy_publications
                == std::vector<std::string> { "10101011" }
            && writable_element_publications
                == std::vector<std::size_t> { 0U, 1U }
            && writable_object_publications
                == std::vector<ContainerObjectId> {
                    writable_aggregate_object },
        "a whole aggregate deposit translates to leaf storage while "
        "publishing each leaf and one atomic aggregate change");
    writable_aggregate.force_signal(
        writable_aggregate_proxy,
        PackedLogic4::from_msb_string("01010100"));
    writable_aggregate.force_signal_slice(
        writable_aggregate_proxy,
        PackedLogic4::from_msb_string("01"), 3U);
    require(
        writable_aggregate.signal_is_forced(writable_aggregate_proxy)
            && writable_aggregate.signal_value(writable_aggregate_proxy)
                == PackedLogic4::from_msb_string("01001100")
            && writable_aggregate.stored_signal_value(
                   writable_aggregate_proxy)
                == aggregate_deposit
            && retained_writable_aggregate->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0100"),
                    PackedLogic4::from_msb_string("1100") }
            && retained_writable_aggregate_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0100") }
            && retained_writable_proxy_view->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0100"),
                    PackedLogic4::from_msb_string("1100") }
            && retained_writable_proxy_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0100") }
            && retained_writable_aggregate->elements.data()
                == retained_writable_elements
            && retained_writable_aggregate_slice->elements.data()
                == retained_writable_slice_elements
            && retained_writable_proxy_view->elements.data()
                == retained_writable_proxy_elements
            && retained_writable_proxy_slice->elements.data()
                == retained_writable_proxy_slice_elements
            && aggregate_proxy_publications
                == std::vector<std::string> {
                    "10101011", "01010100", "01001100" },
        "aggregate full and cross-element partial forces compose leaf masks "
        "without changing stored values");
    writable_aggregate.release_signal_slice(
        writable_aggregate_proxy, 3U, 2U);
    writable_aggregate.release_signal(writable_aggregate_proxy);
    // Current-only force changes also publish the logical container object
    // so bound VPI memory words observe the same complete value.
    require(
        !writable_aggregate.signal_is_forced(writable_aggregate_proxy)
            && writable_aggregate.signal_value(writable_aggregate_proxy)
                == aggregate_deposit
            && retained_writable_aggregate->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("1011") }
            && retained_writable_aggregate_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010") }
            && retained_writable_aggregate->elements.data()
                == retained_writable_elements
            && retained_writable_aggregate_slice->elements.data()
                == retained_writable_slice_elements
            && retained_writable_proxy_view->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010"),
                    PackedLogic4::from_msb_string("1011") }
            && retained_writable_proxy_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("1010") }
            && retained_writable_proxy_view->elements.data()
                == retained_writable_proxy_elements
            && retained_writable_proxy_slice->elements.data()
                == retained_writable_proxy_slice_elements
            && aggregate_proxy_publications
                == std::vector<std::string> {
                    "10101011", "01010100", "01001100",
                    "10101011" }
            && proxy_view_callback_values
                == std::vector<std::string> {
                    "10101011:1010", "01010100:0101",
                    "01001100:0100", "10101011:1010" }
            && writable_object_publications.size() == 4U
            && writable_object_values
                == std::vector<std::string> {
                    "10101011", "01010100", "01001100", "10101011" },
        "aggregate force release splits partial masks and republishes only "
        "the final visible value");
    const auto aggregate_scheduled
        = PackedLogic4::from_msb_string("01010101");
    writable_aggregate.schedule_signal_at(
        writable_aggregate_proxy, aggregate_scheduled, 1U);
    require(
        writable_aggregate.run().status == RunStatus::completed
            && writable_aggregate.signal_value(writable_aggregate_proxy)
                == aggregate_scheduled
            && writable_aggregate.stored_signal_value(
                   writable_aggregate_proxy)
                == aggregate_scheduled
            && aggregate_proxy_publications.back() == "01010101"
            && aggregate_proxy_publications.size() == 5U
            && retained_writable_proxy_view->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0101"),
                    PackedLogic4::from_msb_string("0101") }
            && retained_writable_proxy_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0101") }
            && retained_writable_proxy_view->elements.data()
                == retained_writable_proxy_elements
            && retained_writable_proxy_slice->elements.data()
                == retained_writable_proxy_slice_elements
            && writable_object_publications.size() == 5U
            && writable_object_values
                == std::vector<std::string> {
                    "10101011", "01010100", "01001100", "10101011",
                    "01010101" },
        "scheduled aggregate updates distribute persistent external drives "
        "across the leaves and publish once");
    writable_aggregate.deposit_signal(
        writable_aggregate_second,
        PackedLogic4::from_msb_string("0010"));
    require(
        writable_aggregate.signal_value(writable_aggregate_proxy)
                == PackedLogic4::from_msb_string("01010010")
            && retained_writable_aggregate->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0101"),
                    PackedLogic4::from_msb_string("0010") }
            && retained_writable_aggregate_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0101") }
            && retained_writable_aggregate->elements.data()
                == retained_writable_elements
            && retained_writable_aggregate_slice->elements.data()
                == retained_writable_slice_elements
            && retained_writable_proxy_view->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0101"),
                    PackedLogic4::from_msb_string("0010") }
            && retained_writable_proxy_slice->elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("0101") }
            && retained_writable_proxy_view->elements.data()
                == retained_writable_proxy_elements
            && retained_writable_proxy_slice->elements.data()
                == retained_writable_proxy_slice_elements
            && proxy_view_callback_values.back() == "01010010:0101"
            && proxy_view_callback_values.size() == 6U,
        "an individual leaf publication refreshes retained aggregate and "
        "slice references without changing their backing addresses");

    {
        Interpreter nested_publication;
        auto family_type = writable_aggregate_type;
        const auto first = nested_publication.add_signal({
            "nested_reference[1]", PackedLogic4(4U, Logic4::z),
            ResolutionKind::sv_wire });
        const auto second = nested_publication.add_signal({
            "nested_reference[0]", PackedLogic4(4U, Logic4::z),
            ResolutionKind::sv_wire });
        const auto proxy = nested_publication.add_signal({
            "nested_reference", PackedLogic4(8U, Logic4::z),
            ResolutionKind::sv_wire });
        const auto family = nested_publication.add_container_object({
            "nested_reference",
            ContainerValue {
                family_type,
                { PackedLogic4(4U, Logic4::z),
                    PackedLogic4(4U, Logic4::z) },
                { } },
            std::nullopt });
        nested_publication.add_container_element_signal_alias(
            { family, 0U, first, true, true });
        nested_publication.add_container_element_signal_alias(
            { family, 1U, second, true, true });
        nested_publication.add_container_aggregate_signal_alias(
            { family, proxy, true, true });
        const auto view = nested_publication.add_container_object({
            "nested_reference.view",
            ContainerValue {
                family_type,
                { PackedLogic4(4U, Logic4::z),
                    PackedLogic4(4U, Logic4::z) },
                { } },
            std::nullopt });
        nested_publication.add_container_signal_alias(
            { view, proxy, true, false });
        auto view_slice_type = family_type;
        view_slice_type.index_right = 1;
        view_slice_type.dimensions = { { 1, 1 } };
        const auto view_slice = nested_publication.add_container_object({
            "nested_reference.view_slice",
            ContainerValue {
                view_slice_type, { PackedLogic4(4U, Logic4::z) }, { } },
            ContainerSliceAlias { view, 1, 1 } });

        const auto inner_value = PackedLogic4::from_msb_string("10100101");
        const auto outer_value = PackedLogic4::from_msb_string("01011010");
        const ContainerValue* retained_view { };
        const ContainerValue* retained_slice { };
        const auto* retained_view_elements
            = static_cast<const PackedLogic4*>(nullptr);
        const auto* retained_slice_elements
            = static_cast<const PackedLogic4*>(nullptr);
        bool outer_stored_hook { };
        bool late_exposure { };
        std::string first_exposed_value;
        std::string after_inner_publication;
        std::vector<std::string> proxy_callbacks;
        nested_publication.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                if (changed != first) {
                    return;
                }
                if (!outer_stored_hook) {
                    outer_stored_hook = true;
                    nested_publication.deposit_signal(proxy, outer_value);
                    after_inner_publication
                        = retained_view->elements[0U].to_msb_string()
                        + retained_view->elements[1U].to_msb_string()
                        + ":"
                        + retained_slice->elements[0U].to_msb_string();
                    return;
                }
                if (late_exposure) {
                    return;
                }
                retained_slice
                    = &nested_publication.container_object_value(view_slice);
                retained_view
                    = &nested_publication.container_object_value(view);
                retained_view_elements = retained_view->elements.data();
                retained_slice_elements = retained_slice->elements.data();
                first_exposed_value
                    = retained_view->elements[0U].to_msb_string()
                    + retained_view->elements[1U].to_msb_string()
                    + ":"
                    + retained_slice->elements[0U].to_msb_string();
                late_exposure = true;
            });
        nested_publication.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4& current,
                const SimulationTick) {
                if (changed == proxy && retained_view != nullptr) {
                    proxy_callbacks.push_back(
                        current.to_msb_string() + ":"
                        + retained_view->elements[0U].to_msb_string()
                        + retained_view->elements[1U].to_msb_string()
                        + ":"
                        + retained_slice->elements[0U].to_msb_string());
                }
            });

        nested_publication.deposit_signal(proxy, inner_value);
        // The nested deposit publishes before the original captured family
        // resumes, so retained references see both complete values in order.
        require(
            outer_stored_hook && late_exposure
                && first_exposed_value == "ZZZZZZZZ:ZZZZ"
                && after_inner_publication == "01011010:0101"
                && proxy_callbacks
                    == std::vector<std::string> {
                        "01011010:01011010:0101",
                        "10100101:10100101:1010" }
                && retained_view != nullptr && retained_slice != nullptr
                && retained_view->elements.data() == retained_view_elements
                && retained_slice->elements.data()
                    == retained_slice_elements
                && retained_view->elements
                    == std::vector<PackedLogic4> {
                        PackedLogic4::from_msb_string("1010"),
                        PackedLogic4::from_msb_string("0101") }
                && retained_slice->elements
                    == std::vector<PackedLogic4> {
                        PackedLogic4::from_msb_string("1010") },
            "nested preflighted family publications each retain independent "
            "scratch when a proxy-backed container reference is first exposed");
    }

    Interpreter nonblocking;
    ContainerType dynamic_type = static_type;
    dynamic_type.fixed = false;
    dynamic_type.index_left = 0;
    dynamic_type.index_right = 0;
    dynamic_type.dimensions.clear();
    const auto dynamic_object = nonblocking.add_container_object(
        { "dynamic", ContainerValue {
              dynamic_type, { value(8, 0x11) }, { } }, std::nullopt });
    Process nonblocking_process;
    nonblocking_process.id = 0;
    nonblocking_process.name = "nonblocking_element_write";
    nonblocking_process.register_count = 3;
    nonblocking_process.container_register_count = 1;
    nonblocking_process.container_register_types = { dynamic_type };
    nonblocking_process.debug_locals = {
        { "active_value", "byte", 2, 8, { }, std::nullopt,
            std::nullopt, ValueKind::logic4, { } }
    };
    nonblocking_process.operations = {
        LoadConstant { 0, value(32, 0) },
        LoadConstant { 1, value(8, 0xaa) },
        WriteContainerObjectElement {
            dynamic_object, 0, 1, true, false, true, std::nullopt,
            std::nullopt },
        ReadContainerObject { 0, dynamic_object },
        ContainerRead { 2, 0, 0, true, false, false },
        Halt { }
    };
    (void)nonblocking.add_process(std::move(nonblocking_process));
    require(
        nonblocking.run().status == RunStatus::completed
            && nonblocking.read_debug_local(0, 0) == value(8, 0x11)
            && nonblocking.container_object_value(dynamic_object).elements[0]
                == value(8, 0xaa),
        "nonblocking container-element writes capture their operands and "
        "publish only after active execution completes");

    Interpreter dynamic_part;
    ContainerType word_array_type;
    word_array_type.element_width = 16;
    word_array_type.fixed = true;
    word_array_type.index_left = 0;
    word_array_type.index_right = 1;
    const auto word_array_object = dynamic_part.add_container_object(
        { "word_array",
            ContainerValue {
                word_array_type,
                { value(16, 0), value(16, 0) },
                { } },
            std::nullopt });
    const DynamicPartIndex byte_lane {
        1, 15, 0, 0, 8, true, true };
    Process dynamic_part_process;
    dynamic_part_process.id = 0;
    dynamic_part_process.name = "nonblocking_dynamic_part_element_write";
    dynamic_part_process.register_count = 3;
    dynamic_part_process.operations = {
        LoadConstant { 0, value(32, 0) },
        LoadConstant { 1, value(32, 0) },
        LoadConstant { 2, value(8, 0x12) },
        WriteContainerObjectElement {
            word_array_object, 0, 2, false, false, true,
            std::nullopt, byte_lane },
        LoadConstant { 0, value(32, 0) },
        LoadConstant { 1, value(32, 8) },
        LoadConstant { 2, value(8, 0x34) },
        WriteContainerObjectElement {
            word_array_object, 0, 2, false, false, true,
            std::nullopt, byte_lane },
        LoadConstant { 0, value(32, 0) },
        LoadConstant { 1, value(32, 0) },
        LoadConstant { 2, value(8, 0x56) },
        WriteContainerObjectElement {
            word_array_object, 0, 2, false, false, true,
            std::nullopt, byte_lane },
        LoadConstant { 0, value(32, 1) },
        LoadConstant { 1, value(32, 0) },
        LoadConstant {
            2, PackedLogic4::from_msb_string("X1010101") },
        WriteContainerObjectElement {
            word_array_object, 0, 2, false, false, true,
            std::nullopt, byte_lane },
        // Change every captured operand before the update phase begins.
        LoadConstant { 0, value(32, 0) },
        LoadConstant { 1, value(32, 8) },
        LoadConstant { 2, value(8, 0xee) },
        Halt { }
    };
    (void)dynamic_part.add_process(std::move(dynamic_part_process));
    const auto dynamic_part_result = dynamic_part.run();
    const auto& dynamic_part_words
        = dynamic_part.container_object_value(word_array_object).elements;
    require(
        dynamic_part_result.status == RunStatus::completed
            && dynamic_part_words.size() == 2U
            && dynamic_part_words[0] == value(16, 0x3456)
            && dynamic_part_words[1]
                == PackedLogic4::from_msb_string("00000000X1010101"),
        "nonblocking packed-slice writes capture address, base, and data; "
        "disjoint lanes compose, later overlapping writes win, and four-state "
        "bits are preserved");

    const auto expect_dynamic_part_failure =
        [&](const DynamicPartIndex selection,
            const std::string_view expected_message) {
            Interpreter failing;
            const auto object = failing.add_container_object(
                { "word_array",
                    ContainerValue {
                        word_array_type,
                        { value(16, 0x1234), value(16, 0x5678) },
                        { } },
                    std::nullopt });
            Process candidate;
            candidate.id = 0;
            candidate.name = "malformed_dynamic_part_element_write";
            candidate.register_count = 3;
            candidate.operations = {
                LoadConstant { 0, value(32, 0) },
                LoadConstant { 1, value(32, 0) },
                LoadConstant { 2, value(8, 0xaa) },
                WriteContainerObjectElement {
                    object, 0, 2, false, false, true,
                    std::nullopt, selection },
                Halt { }
            };
            (void)failing.add_process(std::move(candidate));
            try {
                (void)failing.run();
                require(false, "malformed dynamic part write must fail");
            } catch (const InterpreterError& error) {
                require(
                    std::string_view { error.what() }.find(expected_message)
                        != std::string_view::npos,
                    "malformed dynamic part write retains its diagnostic");
            }
            require(
                failing.container_object_value(object).elements
                    == std::vector<PackedLogic4> {
                        value(16, 0x1234), value(16, 0x5678) },
                "malformed dynamic part writes never fall back to replacing "
                "the whole element");
        };
    expect_dynamic_part_failure(
        DynamicPartIndex {
            1,
            static_cast<std::int64_t>(
                std::numeric_limits<std::int32_t>::max()) + 1,
            0,
            0,
            8,
            true,
            true },
        "bounds must fit signed 32-bit integers");
    expect_dynamic_part_failure(
        DynamicPartIndex { 1, 15, 0, 1, 8, true, true },
        "range is outside its packed target");
    expect_dynamic_part_failure(
        DynamicPartIndex { 3, 15, 0, 0, 8, true, true },
        "invalid register ID");

    ContainerType slice_parent_type = static_type;
    slice_parent_type.index_left = 5;
    slice_parent_type.index_right = 0;
    ContainerValue slice_parent {
        slice_parent_type,
        { value(8, 0x50),
            value(8, 0x40),
            PackedLogic4::from_msb_string("0000000X"),
            PackedLogic4::from_msb_string("0000000Z"),
            value(8, 0x10),
            value(8, 0x00) },
        { }
    };
    ContainerType slice_formal_type = static_type;
    slice_formal_type.index_left = -2;
    slice_formal_type.index_right = 0;
    ContainerType nested_formal_type = static_type;
    nested_formal_type.index_left = 9;
    nested_formal_type.index_right = 8;
    Interpreter slice_aliases;
    const auto slice_parent_object = slice_aliases.add_container_object(
        { "slice_parent", slice_parent, std::nullopt });
    const auto slice_formal_object = slice_aliases.add_container_object(
        { "slice_formal",
            default_container_value(slice_formal_type),
            ContainerSliceAlias {
                slice_parent_object, 4, 2 } });
    const auto nested_formal_object = slice_aliases.add_container_object(
        { "nested_formal",
            default_container_value(nested_formal_type),
            ContainerSliceAlias {
                slice_formal_object, -1, 0 } });
    require(
        slice_aliases.container_object_value(
                         slice_formal_object)
                    .elements
                == std::vector<PackedLogic4> {
                    value(8, 0x40),
                    PackedLogic4::from_msb_string("0000000X"),
                    PackedLogic4::from_msb_string("0000000Z") }
            && slice_aliases.container_object_value(nested_formal_object).elements == std::vector<PackedLogic4> { PackedLogic4::from_msb_string("0000000X"), PackedLogic4::from_msb_string("0000000Z") },
        "slice aliases materialize exact X/Z elements in formal ordinal "
        "order across differing declared indices and directions");
    slice_aliases.deposit_container_object(
        nested_formal_object,
        ContainerValue {
            nested_formal_type,
            { value(8, 0xa3), value(8, 0xa2) },
            { } });
    require(
        slice_aliases.container_object_value(
                         slice_parent_object)
                    .elements
                == std::vector<PackedLogic4> {
                    value(8, 0x50),
                    value(8, 0x40),
                    value(8, 0xa3),
                    value(8, 0xa2),
                    value(8, 0x10),
                    value(8, 0x00) }
            && slice_aliases.container_object_value(slice_formal_object).elements == std::vector<PackedLogic4> { value(8, 0x40), value(8, 0xa3), value(8, 0xa2) },
        "nested slice-alias deposits atomically replace only the selected "
        "root range and refresh every ordinal view");

    auto element_slice_parent_type = fixed_type(static_type, 1, 0);
    const PackedLogic4 high_impedance_word
        = PackedLogic4::from_msb_string("ZZZZZZZZ");
    Interpreter element_slice_aliases;
    const auto element_slice_first = element_slice_aliases.add_signal(
        { "element_slice_parent[1]", high_impedance_word,
            ResolutionKind::sv_wire });
    const auto element_slice_second = element_slice_aliases.add_signal(
        { "element_slice_parent[0]", high_impedance_word,
            ResolutionKind::sv_wire });
    const auto element_slice_parent =
        element_slice_aliases.add_container_object(
            { "element_slice_parent",
                default_container_value(element_slice_parent_type),
                std::nullopt });
    element_slice_aliases.add_container_element_signal_alias(
        { element_slice_parent, 0U, element_slice_first, true, true });
    element_slice_aliases.add_container_element_signal_alias(
        { element_slice_parent, 1U, element_slice_second, true, true });
    const auto element_slice_formal_type = fixed_type(static_type, 9, 8);
    const auto element_slice_formal =
        element_slice_aliases.add_container_object(
            { "element_slice_formal",
                default_container_value(element_slice_formal_type),
                ContainerSliceAlias { element_slice_parent, 1, 0 } });
    Process element_slice_writer;
    element_slice_writer.id = 0U;
    element_slice_writer.name = "element_slice_writer";
    element_slice_writer.register_count = 2U;
    element_slice_writer.driver_regions.push_back(
        { element_slice_first, 0U, 8U, false });
    element_slice_writer.operations = {
        LoadConstant { 0U, value(32U, 9U) },
        LoadConstant { 1U, value(8U, 0xa5U) },
        WriteContainerObjectElement {
            element_slice_formal, 0U, 1U, true, false, false,
            std::nullopt, std::nullopt },
        Halt { },
    };
    const auto element_slice_process = element_slice_aliases.add_process(
        std::move(element_slice_writer));
    require(
        element_slice_aliases.run().status == RunStatus::completed
            && element_slice_aliases.driver_value(
                   element_slice_process, element_slice_first)
                == value(8U, 0xa5U)
            && element_slice_aliases.signal_value(element_slice_first)
                == value(8U, 0xa5U)
            && element_slice_aliases.signal_value(element_slice_second)
                == high_impedance_word,
        "element-backed slice writes retain their originating process on "
        "the selected leaf signal");

    const auto expect_invalid_slice_alias =
        [&](const ContainerType& formal,
            const ContainerSliceAlias alias) {
            Interpreter invalid;
            (void)invalid.add_container_object(
                { "parent", slice_parent, std::nullopt });
            try {
                (void)invalid.add_container_object(
                    { "invalid",
                        default_container_value(formal),
                        alias });
                require(false, "invalid slice alias must be rejected");
            } catch (const std::invalid_argument&) {
            }
        };
    expect_invalid_slice_alias(
        slice_formal_type,
        ContainerSliceAlias { 1, 4, 2 });
    expect_invalid_slice_alias(
        slice_formal_type,
        ContainerSliceAlias { 0, 6, 4 });
    expect_invalid_slice_alias(
        nested_formal_type,
        ContainerSliceAlias { 0, 4, 2 });

    ContainerType memory_type;
    memory_type.element_width = 8;
    memory_type.element_nominal_type = "packet_t";
    memory_type.fixed = true;
    memory_type.index_left = 3;
    memory_type.index_right = 0;
    auto memory = default_container_value(memory_type);
    load_memory_text(
        memory,
        "a5 /* inline */ xz\n@3 0f // tail\n",
        true);
    require(
        memory.elements[0].to_msb_string() == "00001111"
            && memory.elements[1].to_msb_string()
                == "XXXXXXXX"
            && memory.elements[2].to_msb_string()
                == "XXXXZZZZ"
            && memory.elements[3].to_msb_string()
                == "10100101",
        "$readmemh semantics retain comments, @addresses, and exact X/Z "
        "nibbles while default addresses increase numerically");
    load_memory_text(
        memory, "10z1 0011", false, 1, 0);
    require(
        memory.elements[2].to_msb_string()
                == "000010Z1"
            && memory.elements[3].to_msb_string()
                == "00000011",
        "$readmemb optional bounds use declared indices and retain Z bits");
    require(
        write_memory_text(memory, true)
            == "03\n0x\nxx\n0f\n",
        "$writememh emits numeric address order and deterministic unknown "
        "nibbles");
    const auto binary_dump = write_memory_text(memory, false, 1, 0);
    require(
        binary_dump == "000010Z1\n00000011\n",
        "$writememb preserves exact four-state bits and descending bounds");
    auto binary_round_trip = default_container_value(memory_type);
    load_memory_text(binary_round_trip, binary_dump, false, 1, 0);
    require(
        binary_round_trip.elements[2] == memory.elements[2]
            && binary_round_trip.elements[3] == memory.elements[3],
        "bounded binary memory text round trips selected elements");
    try {
        load_memory_text(memory, "@7 00", true);
        require(false, "out-of-range read-memory address must fail");
    } catch (const std::out_of_range&) {
    }
    try {
        load_memory_text(memory, "0 2", false);
        require(false, "partly invalid read-memory data must fail");
    } catch (const std::invalid_argument&) {
        require(
            memory.elements
                == std::vector<PackedLogic4> {
                    value(8, 0x0f),
                    PackedLogic4::from_msb_string("XXXXXXXX"),
                    PackedLogic4::from_msb_string("000010Z1"),
                    value(8, 0x03) },
            "failed read-memory parsing leaves the complete target unchanged");
    }
    try {
        load_memory_text(memory, "2", false);
        require(false, "invalid binary read-memory digit must fail");
    } catch (const std::invalid_argument&) {
    }
    try {
        load_memory_text(
            memory,
            std::string(maximum_memory_file_bytes + 1U, '0'),
            false);
        require(false, "oversized read-memory input must fail");
    } catch (const std::length_error&) {
    }
    auto matrix_type = memory_type;
    matrix_type.dimensions = { { 1, 0 }, { 0, 1 } };
    auto matrix = default_container_value(matrix_type);
    load_memory_text(matrix, "11 22 33 44", true);
    require(
        matrix.elements
                == std::vector<PackedLogic4> {
                    value(8, 0x11), value(8, 0x22),
                    value(8, 0x33), value(8, 0x44) }
            && write_memory_text(matrix, true) == "11\n22\n33\n44\n",
        "multidimensional memory files use deterministic row-major linear "
        "addresses");

    ContainerType string_memory_type;
    string_memory_type.element_kind = ContainerElementKind::String;
    string_memory_type.element_width = 0;
    string_memory_type.fixed = true;
    string_memory_type.index_left = 0;
    string_memory_type.index_right = 1;
    string_memory_type.dimensions = { { 0, 1 } };
    auto string_memory = default_container_value(string_memory_type);
    load_memory_text(
        string_memory,
        "\"alpha beta\" \"line\\n//literal\"",
        true);
    require(
        string_memory.string_elements
                == std::vector<std::string> {
                    "alpha beta", "line\n//literal" }
            && write_memory_text(string_memory, false) == "\"alpha beta\"\n\"line\\n//literal\"\n",
        "string memory files retain quoted whitespace, escapes, and comment "
        "markers");

    ContainerType aggregate_leaf;
    aggregate_leaf.element_width = 4;
    aggregate_leaf.fixed = true;
    aggregate_leaf.index_left = 0;
    aggregate_leaf.index_right = 0;
    aggregate_leaf.dimensions = { { 0, 0 } };
    ContainerType aggregate_memory_type;
    aggregate_memory_type.element_kind = ContainerElementKind::Aggregate;
    aggregate_memory_type.element_width = 0;
    aggregate_memory_type.fixed = true;
    aggregate_memory_type.index_left = 0;
    aggregate_memory_type.index_right = 1;
    aggregate_memory_type.dimensions = { { 0, 1 } };
    aggregate_memory_type.element_types = {
        aggregate_leaf, aggregate_leaf
    };
    aggregate_memory_type.member_names = { "tag", "data" };
    auto aggregate_memory = default_container_value(
        aggregate_memory_type);
    load_memory_text(aggregate_memory, "a5 3c", true);
    require(
        aggregate_memory.nested_elements[0]
                    .nested_elements[0]
                    .elements[0]
                == value(4, 0xa)
            && aggregate_memory.nested_elements[0]
                    .nested_elements[1]
                    .elements[0]
                == value(4, 0x5)
            && aggregate_memory.nested_elements[1]
                    .nested_elements[0]
                    .elements[0]
                == value(4, 0x3)
            && aggregate_memory.nested_elements[1]
                    .nested_elements[1]
                    .elements[0]
                == value(4, 0xc)
            && write_memory_text(aggregate_memory, true)
                == "a5\n3c\n",
        "aggregate memory files recursively pack and unpack declaration-order "
        "members");
    try {
        (void)write_memory_text(memory, true, 7, 0);
        require(false, "out-of-range write-memory bounds must fail");
    } catch (const std::out_of_range&) {
    }

    const auto expect_failure =
        [&](const ContainerType& type,
            std::vector<Operation> operations,
            const std::string_view expected) {
            Interpreter failing;
            Process candidate;
            candidate.id = 0;
            candidate.name = "container_failure";
            candidate.register_count = 3;
            candidate.container_register_count = 1;
            candidate.container_register_types = { type };
            candidate.operations = std::move(operations);
            (void)failing.add_process(std::move(candidate));
            try {
                (void)failing.run();
                require(false, "invalid container process must fail");
            } catch (const InterpreterError& error) {
                require(
                    std::string_view { error.what() }.find(expected)
                        != std::string_view::npos,
                    "container failure retains its diagnostic");
            }
        };
    require(
        maximum_container_elements(array_type) > 4096
            && maximum_container_elements(associative_type) > 4096
            && maximum_container_elements(associative_type)
                < maximum_container_elements(array_type),
        "container capacities derive from owning representation rather than "
        "the former 4096-element cap");
    expect_failure(
        array_type,
        { LoadConstant { 0, value(32, 1) },
            ResizeContainer { 0, 0 },
            LoadConstant { 1, value(32, 2) },
            ContainerRead { 2, 0, 1, true },
            Halt { } },
        "out of range");
    expect_failure(
        array_type,
        { LoadConstant { 0, value(8, 1) },
            PushContainer { 0, 0, false },
            Halt { } },
        "dynamic array");
    expect_failure(
        queue_type,
        { PopContainer { 0, 0, true }, Halt { } },
        "empty queue");
    expect_failure(
        queue_type,
        { LoadConstant { 0, value(8, 1) },
            LoadConstant { 1, value(32, 1) },
            PushContainer { 0, 0, false, 1 }, Halt { } },
        "insert index is out of range");
    expect_failure(
        associative_type,
        { LoadConstant {
              0,
              PackedLogic4::from_aval_bval(8, 1, 1) },
            ContainerExists { 1, 0, 0 },
            Halt { } },
        "known integral value");
    expect_failure(
        array_type,
        { LoadConstant { 0, value(32, 1) },
            DeleteContainer { 0, 0 },
            Halt { } },
        "requires an associative array");
    const auto expect_invalid_static_read = [&](PackedLogic4 index) {
        Interpreter candidate_interpreter;
        const auto observed = candidate_interpreter.add_signal(
            { "static.invalid_read", PackedLogic4(8, Logic4::zero) });
        Process candidate;
        candidate.id = 0;
        candidate.name = "static_invalid_read";
        candidate.register_count = 2;
        candidate.container_register_count = 1;
        candidate.container_register_types = { static_type };
        candidate.operations = {
            LoadConstant { 0, std::move(index) },
            ContainerRead { 1, 0, 0, true },
            WriteBlocking { observed, 1 },
            Halt { }
        };
        (void)candidate_interpreter.add_process(std::move(candidate));
        require(
            candidate_interpreter.run().status == RunStatus::completed
                && candidate_interpreter.signal_value(observed).to_msb_string()
                    == "XXXXXXXX",
            "invalid four-state static-array reads complete with X");
    };
    expect_invalid_static_read(
        PackedLogic4::from_aval_bval(32, 1, 1));
    expect_invalid_static_read(value(32, 3));
    expect_failure(
        static_type,
        { DeleteContainer { 0, std::nullopt }, Halt { } },
        "cannot clear a static array");

    ContainerType expanded_type = associative_type;
    expanded_type.index_width = 13;
    expanded_type.signed_indices = false;
    Interpreter expanded;
    const auto expanded_object = expanded.add_container_object(
        { "expanded", default_container_value(expanded_type), std::nullopt });
    Process expanded_process;
    expanded_process.id = 0;
    expanded_process.name = "expanded_container";
    expanded_process.register_count = 2;
    expanded_process.container_register_count = 1;
    expanded_process.container_register_types = { expanded_type };
    expanded_process.operations.reserve(4097U * 3U + 2U);
    for (std::size_t entry = 0; entry < 4097U; ++entry) {
        expanded_process.operations.emplace_back(
            LoadConstant { 0, value(13, entry) });
        expanded_process.operations.emplace_back(
            LoadConstant { 1, value(8, entry) });
        expanded_process.operations.emplace_back(
            ContainerWrite { 0, 0, 1, false });
    }
    expanded_process.operations.emplace_back(
        WriteContainerObject { expanded_object, 0, std::nullopt });
    expanded_process.operations.emplace_back(Halt { });
    (void)expanded.add_process(std::move(expanded_process));
    require(
        expanded.run().status == RunStatus::completed
            && expanded.container_object_value(expanded_object)
                    .elements.size()
                == 4097U,
        "associative arrays retain entries beyond the former hard cap");
}

void test_simir_retained_commit_hook_phase_and_alias_materialization()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    {
        Interpreter interpreter;
        const auto signal = interpreter.add_signal({
            "stored_hook.signal", PackedLogic4::from_msb_string("00")
        });
        std::vector<std::pair<std::string, std::string>> stored_snapshots;
        std::vector<std::pair<std::string, std::string>> visible_snapshots;
        std::size_t stored_calls { };
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                if (changed != signal) {
                    return;
                }
                stored_snapshots.emplace_back(
                    interpreter.stored_signal_value(signal).to_msb_string(),
                    interpreter.signal_value(signal).to_msb_string());
                if (++stored_calls == 1U) {
                    interpreter.deposit_signal(
                        signal, PackedLogic4::from_msb_string("00"));
                    interpreter.force_signal_slice(
                        signal, PackedLogic4::from_msb_string("1"), 0U);
                }
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4&,
                const SimulationTick) {
                if (changed == signal) {
                    visible_snapshots.emplace_back(
                        interpreter.signal_value(signal).to_msb_string(),
                        interpreter.stored_signal_value(signal).to_msb_string());
                }
            });

        interpreter.deposit_signal(
            signal, PackedLogic4::from_msb_string("10"));
        require(
            stored_snapshots
                    == std::vector<std::pair<std::string, std::string>> {
                        { "10", "00" }, { "00", "00" }
                    },
            "stored hook observes new aggregate storage before current publication");
        require(
            visible_snapshots
                    == std::vector<std::pair<std::string, std::string>> {
                        { "01", "00" }, { "11", "00" }
                    }
                && interpreter.signal_is_forced(signal)
                && interpreter.stored_signal_value(signal)
                    == PackedLogic4::from_msb_string("00")
                && interpreter.signal_value(signal)
                    == PackedLogic4::from_msb_string("11"),
            "outer captured aggregate is published with the live force overlay");
    }

    {
        Interpreter interpreter;
        const auto signal = interpreter.add_signal({
            "current_hook.signal", PackedLogic4::from_msb_string("00")
        });
        std::vector<std::pair<std::string, std::string>> stored_snapshots;
        std::vector<std::pair<std::string, std::string>> visible_snapshots;
        std::size_t visible_calls { };
        std::string outer_argument_before_nested_write;
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                if (changed != signal) {
                    return;
                }
                stored_snapshots.emplace_back(
                    interpreter.stored_signal_value(signal).to_msb_string(),
                    interpreter.signal_value(signal).to_msb_string());
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4& callback_value,
                const SimulationTick) {
                if (changed != signal) {
                    return;
                }
                visible_snapshots.emplace_back(
                    interpreter.signal_value(signal).to_msb_string(),
                    interpreter.stored_signal_value(signal).to_msb_string());
                if (++visible_calls == 1U) {
                    outer_argument_before_nested_write
                        = callback_value.to_msb_string();
                    interpreter.deposit_signal(
                        signal, PackedLogic4::from_msb_string("01"));
                }
            });

        interpreter.deposit_signal(
            signal, PackedLogic4::from_msb_string("10"));
        require(
            stored_snapshots
                    == std::vector<std::pair<std::string, std::string>> {
                        { "10", "00" }, { "01", "10" }
                    }
                && visible_snapshots
                    == std::vector<std::pair<std::string, std::string>> {
                        { "10", "10" }, { "01", "01" }
                    }
                && outer_argument_before_nested_write == "10"
                && interpreter.stored_signal_value(signal)
                    == PackedLogic4::from_msb_string("01")
                && interpreter.signal_value(signal)
                    == PackedLogic4::from_msb_string("01"),
            "nested current-hook deposit remains current after the outer hook returns");
    }

    {
        Interpreter interpreter;
        const auto backing = interpreter.add_signal({
            "alias_hook.backing", PackedLogic4::from_msb_string("00000000")
        });
        ContainerType alias_type;
        alias_type.fixed = true;
        alias_type.index_left = 0;
        alias_type.index_right = 0;
        alias_type.dimensions = { { 0, 0 } };
        alias_type.element_width = 8;
        alias_type.two_state = true;
        const auto object = interpreter.add_container_object({
            "alias_hook.object",
            ContainerValue {
                alias_type,
                { PackedLogic4::from_msb_string("00000000") },
                { }
            },
            std::nullopt
        });
        interpreter.add_container_signal_alias({ object, backing, true, true });
        const auto* const retained
            = &interpreter.container_object_value(object);
        const auto* const retained_elements = retained->elements.data();
        require(
            retained->elements.front()
                == PackedLogic4::from_msb_string("00000000"),
            "warm the retained whole-object alias before the outer write");

        std::string alias_at_outer_stored_hook;
        std::string alias_after_nested_deposit;
        std::size_t stored_calls { };
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                if (changed != backing) {
                    return;
                }
                if (++stored_calls == 1U) {
                    alias_at_outer_stored_hook
                        = retained->elements.front().to_msb_string();
                    interpreter.deposit_signal(
                        backing, PackedLogic4::from_msb_string("10111011"));
                    alias_after_nested_deposit
                        = retained->elements.front().to_msb_string();
                }
            });

        auto outer = interpreter.container_object_value(object);
        outer.elements.front()
            = PackedLogic4::from_msb_string("10101010");
        interpreter.deposit_container_object(object, std::move(outer));
        const auto current = interpreter.signal_value(backing);
        const auto stored = interpreter.stored_signal_value(backing);
        const auto final_alias = retained->elements.front();
        const bool alias_matches_current = final_alias == current;
        const auto diagnostic = std::string {
            "whole-object alias rematerializes the final current aggregate "
            "after reentrant stored-hook writes; current="
        } + current.to_msb_string() + " stored=" + stored.to_msb_string()
            + " alias=" + final_alias.to_msb_string();
        require(
            alias_at_outer_stored_hook == "10101010"
                && alias_after_nested_deposit == "10111011"
                && current == PackedLogic4::from_msb_string("10101010")
                && stored == PackedLogic4::from_msb_string("10111011")
                && retained->elements.data() == retained_elements
                && alias_matches_current,
            diagnostic);
    }

    for (const auto element_width :
        std::array<std::size_t, 2U> { 65U, 129U }) {
        Interpreter interpreter;
        const auto full_width = element_width * 2U;
        const auto backing = interpreter.add_signal({
            "retained_wide_alias.backing",
            PackedLogic4(full_width, Logic4::zero) });
        ContainerType array_type;
        array_type.fixed = true;
        array_type.index_left = 1;
        array_type.index_right = 0;
        array_type.dimensions = { { 1, 0 } };
        array_type.element_width = static_cast<std::uint32_t>(element_width);
        const PackedLogic4 zeros(element_width, Logic4::zero);
        const auto array = interpreter.add_container_object({
            "retained_wide_alias.array",
            ContainerValue { array_type, { zeros, zeros }, { } },
            std::nullopt });
        interpreter.add_container_signal_alias(
            { array, backing, true, true });
        ContainerType slice_type = array_type;
        slice_type.index_left = 1;
        slice_type.index_right = 1;
        slice_type.dimensions = { { 1, 1 } };
        const auto slice = interpreter.add_container_object({
            "retained_wide_alias.slice",
            ContainerValue { slice_type, { zeros }, { } },
            ContainerSliceAlias { array, 1, 1 } });

        const auto* const retained_array
            = &interpreter.container_object_value(array);
        const auto* const retained_slice
            = &interpreter.container_object_value(slice);
        const auto* const retained_array_elements
            = retained_array->elements.data();
        const auto* const retained_slice_elements
            = retained_slice->elements.data();
        std::vector<std::string> callback_values;
        interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4& callback_value,
                const SimulationTick) {
                if (changed != backing) {
                    return;
                }
                callback_values.push_back(
                    std::to_string(callback_value.width()) + ":"
                    + retained_array->elements[0].to_msb_string() + ":"
                    + retained_array->elements[1].to_msb_string() + ":"
                    + retained_slice->elements[0].to_msb_string());
            });

        PackedLogic4 replacement(full_width, Logic4::zero);
        replacement.set(full_width - 1U, Logic4::one);
        replacement.set(element_width, Logic4::one);
        replacement.set(0U, Logic4::one);
        interpreter.deposit_signal(backing, replacement);
        require(
            retained_array->elements[0]
                    == replacement.extract_bits(element_width, element_width)
                && retained_array->elements[1]
                    == replacement.extract_bits(0U, element_width)
                && retained_slice->elements[0]
                    == retained_array->elements[0]
                && retained_array->elements.data()
                    == retained_array_elements
                && retained_slice->elements.data()
                    == retained_slice_elements
                && callback_values.size() == 1U
                && callback_values[0U].starts_with(
                    std::to_string(full_width) + ":"),
            "retained wide array and slice references update in place before "
            "the current callback at 65/129-bit element widths");

        interpreter.force_signal_slice(
            backing, PackedLogic4(1U, Logic4::zero), full_width - 1U);
        require(
            retained_array->elements[0].get(element_width - 1U)
                == Logic4::zero
                && retained_slice->elements[0]
                    == retained_array->elements[0],
            "a retained wide slice follows a forced current leaf change");
        interpreter.release_signal_slice(backing, full_width - 1U, 1U);
        require(
            retained_array->elements[0].get(element_width - 1U)
                == Logic4::one
                && retained_slice->elements[0]
                    == retained_array->elements[0],
            "release restores the retained wide slice from stored signal data");
    }

    {
        Interpreter interpreter;
        const auto backing = interpreter.add_signal({
            "retained_reentrant_alias.backing",
            PackedLogic4::from_msb_string("0000000000000000") });
        ContainerType array_type;
        array_type.fixed = true;
        array_type.index_left = 1;
        array_type.index_right = 0;
        array_type.dimensions = { { 1, 0 } };
        array_type.element_width = 8U;
        array_type.two_state = true;
        const auto array = interpreter.add_container_object({
            "retained_reentrant_alias.array",
            ContainerValue {
                array_type,
                { value(8U, 0U), value(8U, 0U) },
                { } },
            std::nullopt });
        interpreter.add_container_signal_alias(
            { array, backing, true, true });
        ContainerType slice_type = array_type;
        slice_type.index_left = 1;
        slice_type.index_right = 1;
        slice_type.dimensions = { { 1, 1 } };
        const auto slice = interpreter.add_container_object({
            "retained_reentrant_alias.slice",
            ContainerValue { slice_type, { value(8U, 0U) }, { } },
            ContainerSliceAlias { array, 1, 1 } });
        Process terminator;
        terminator.id = 0U;
        terminator.name = "retained_reentrant_alias.terminator";
        terminator.operations = { Halt { } };
        (void)interpreter.add_process(std::move(terminator));
        interpreter.start();

        struct TraceCapture {
            Interpreter* interpreter { };
            SignalId signal { };
            ContainerObjectId array { };
            ContainerObjectId slice { };
            const ContainerValue* array_value { };
            const ContainerValue* slice_value { };
            const PackedLogic4* array_elements { };
            const PackedLogic4* slice_elements { };
            std::optional<ContainerValue> before_publication_snapshot;
            bool saw_old_current { };
            bool failed { };
        } capture {
            &interpreter, backing, array, slice,
            nullptr, nullptr, nullptr, nullptr,
            std::nullopt, false, false
        };
        interpreter.scheduler().set_trace_hook(
            &capture,
            [](void* context,
                const SchedulerTraceRecord& record) noexcept {
                auto& selected = *static_cast<TraceCapture*>(context);
                if (record.kind != SchedulerTraceKind::signal_transaction
                    || record.signal != selected.signal
                    || selected.slice_value != nullptr) {
                    return;
                }
                try {
                    const auto& slice_value
                        = selected.interpreter->container_object_value(
                            selected.slice);
                    selected.slice_value = &slice_value;
                    selected.slice_elements = slice_value.elements.data();
                    const auto& array_value
                        = selected.interpreter->container_object_value(
                            selected.array);
                    selected.array_value = &array_value;
                    selected.array_elements = array_value.elements.data();
                    selected.before_publication_snapshot
                        = selected.interpreter
                              ->container_object_value_snapshot(
                                  selected.array);
                    selected.saw_old_current
                        = slice_value.elements.size() == 1U
                        && slice_value.elements[0U] == value(8U, 0U);
                } catch (...) {
                    selected.failed = true;
                }
            });

        interpreter.deposit_signal(
            backing, PackedLogic4::from_msb_string("1010101010111100"));
        interpreter.scheduler().set_trace_hook(nullptr, nullptr);
        require(
            interpreter.run().status == RunStatus::completed
                && !capture.failed && capture.saw_old_current
                && capture.array_value
                && capture.slice_value
                && capture.array_value
                    == &interpreter.container_object_value(array)
                && capture.slice_value
                    == &interpreter.container_object_value(slice)
                && capture.before_publication_snapshot
                && capture.before_publication_snapshot->elements
                    == std::vector<PackedLogic4> {
                        value(8U, 0U), value(8U, 0U) }
                && capture.array_value->elements.data()
                    == capture.array_elements
                && capture.slice_value->elements.data()
                    == capture.slice_elements
                && capture.array_value->elements
                    == std::vector<PackedLogic4> {
                        value(8U, 0xaaU), value(8U, 0xbcU) }
                && capture.slice_value->elements
                    == std::vector<PackedLogic4> { value(8U, 0xaaU) },
            "a transaction trace may first retain an aliased slice before "
            "the outer current publication, which then refreshes that same "
            "slice and source backing in place");
    }
}

void test_simir_resolved_driver_hook_maturity_and_reentrancy()
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;

    {
        Interpreter interpreter;
        const auto signal = interpreter.add_signal({
            "driver_hook.matured_wire",
            PackedLogic4::from_msb_string("Z"),
            ResolutionKind::sv_wire
        });

        Process first;
        first.id = 0U;
        first.name = "driver_hook.first";
        first.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        first.driver_regions.push_back({ signal, 0U, 0U, true });
        first.register_count = 1U;
        first.operations = {
            LoadConstant { 0U, PackedLogic4::from_msb_string("0") },
            WriteBlocking { signal, 0U },
            Halt { }
        };
        const auto first_id = interpreter.add_process(std::move(first));

        Process second;
        second.id = 1U;
        second.name = "driver_hook.delayed_second";
        second.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        second.driver_regions.push_back({ signal, 0U, 0U, true });
        second.register_count = 1U;
        second.operations = {
            LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
            WriteAfter {
                signal, 0U, 2U, SignalUpdateDomain::systemverilog_nba
            },
            Halt { }
        };
        const auto second_id = interpreter.add_process(std::move(second));

        interpreter.force_signal(
            signal, PackedLogic4::from_msb_string("1"));

        std::vector<std::string> snapshots;
        const auto record = [&](const std::string_view phase,
                                const SimulationTick time) {
            snapshots.push_back(
                std::string { phase } + "@" + std::to_string(time)
                + " d0=" + interpreter.driver_value(first_id, signal)
                      .to_msb_string()
                + " d1=" + interpreter.driver_value(second_id, signal)
                      .to_msb_string()
                + " stored=" + interpreter.stored_signal_value(signal)
                      .to_msb_string()
                + " current=" + interpreter.signal_value(signal)
                      .to_msb_string());
        };
        interpreter.set_driver_change_hook(
            [&](const ProcessId process,
                const SignalId changed,
                const SimulationTick time) {
                if (changed == signal) {
                    record(
                        process == first_id ? "driver0" : "driver1", time);
                }
            });
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick time) {
                if (changed == signal) {
                    record("stored", time);
                }
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4&,
                const SimulationTick time) {
                if (changed == signal) {
                    record("current", time);
                }
            });

        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed && result.time == 2U,
            "the delayed resolved driver update matures at its requested tick");
        require(
            snapshots
                == std::vector<std::string> {
                    "driver0@0 d0=0 d1=Z stored=Z current=1",
                    "stored@0 d0=0 d1=Z stored=0 current=1",
                    "driver1@2 d0=0 d1=1 stored=0 current=1",
                    "stored@2 d0=0 d1=1 stored=X current=1"
                }
                && interpreter.driver_value(first_id, signal)
                    == PackedLogic4::from_msb_string("0")
                && interpreter.driver_value(second_id, signal)
                    == PackedLogic4::from_msb_string("1")
                && interpreter.stored_signal_value(signal)
                    == PackedLogic4::from_msb_string("X")
                && interpreter.signal_value(signal)
                    == PackedLogic4::from_msb_string("1"),
            "driver hooks see the changed raw slot before resolved storage, "
            "stored hooks precede current publication, and force masks both "
            "resolved changes");

        interpreter.release_signal(signal);
        require(
            snapshots
                    == std::vector<std::string> {
                        "driver0@0 d0=0 d1=Z stored=Z current=1",
                        "stored@0 d0=0 d1=Z stored=0 current=1",
                        "driver1@2 d0=0 d1=1 stored=0 current=1",
                        "stored@2 d0=0 d1=1 stored=X current=1",
                        "current@2 d0=0 d1=1 stored=X current=X"
                    }
                && !interpreter.signal_is_forced(signal)
                && interpreter.signal_value(signal)
                    == PackedLogic4::from_msb_string("X"),
            "force release reveals the final resolved driver aggregate");
    }

    {
        Interpreter interpreter;
        const auto signal = interpreter.add_signal({
            "driver_hook.reentrant_wire",
            PackedLogic4::from_msb_string("Z"),
            ResolutionKind::sv_wire
        });
        Process writer;
        writer.id = 0U;
        writer.name = "driver_hook.reentrant_writer";
        writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        writer.driver_regions.push_back({ signal, 0U, 0U, true });
        writer.operations = { Halt { } };
        const auto writer_id = interpreter.add_process(std::move(writer));

        std::vector<std::string> snapshots;
        class ReentrantWriter final : public ProcessExecutor {
        public:
            ReentrantWriter(
                Interpreter& interpreter,
                const ProcessId process,
                const SignalId signal,
                std::vector<std::string>& snapshots)
                : interpreter_ { interpreter }
                , process_ { process }
                , signal_ { signal }
                , snapshots_ { snapshots }
            {
            }

            [[nodiscard]] ProcessResumeResult resume(
                ProcessExecutionContext& context,
                const InstructionIndex start) override
            {
                require(start == 0U, "the reentrant writer starts at entry");
                const auto record = [this](
                    const std::string_view phase,
                    const SimulationTick time) {
                    snapshots_.push_back(
                        std::string { phase } + "@" + std::to_string(time)
                        + " driver=" + interpreter_.driver_value(
                            process_, signal_).to_msb_string()
                        + " stored=" + interpreter_.stored_signal_value(
                            signal_).to_msb_string()
                        + " current=" + interpreter_.signal_value(
                            signal_).to_msb_string());
                };
                interpreter_.set_driver_change_hook(
                    [this, &context, record](
                        const ProcessId changed_process,
                        const SignalId changed_signal,
                        const SimulationTick time) {
                        if (changed_process != process_
                            || changed_signal != signal_) {
                            return;
                        }
                        record("driver", time);
                        if (!nested_write_) {
                            nested_write_ = true;
                            context.write_blocking(
                                signal_, PackedLogic4::from_msb_string("1"));
                        }
                    });
                interpreter_.set_stored_signal_change_hook(
                    [this, record](
                        const SignalId changed,
                        const SimulationTick time) {
                        if (changed == signal_) {
                            record("stored", time);
                        }
                    });
                interpreter_.set_signal_change_hook(
                    [this, record](
                        const SignalId changed,
                        const PackedLogic4&,
                        const SimulationTick time) {
                        if (changed == signal_) {
                            record("current", time);
                        }
                    });

                context.write_blocking(
                    signal_, PackedLogic4::from_msb_string("0"));
                interpreter_.set_driver_change_hook({ });
                interpreter_.set_stored_signal_change_hook({ });
                interpreter_.set_signal_change_hook({ });

                ProcessResumeResult result { 0U, 1U };
                result.external.kind = ExternalSuspendKind::halt;
                return result;
            }

        private:
            Interpreter& interpreter_;
            ProcessId process_ { };
            SignalId signal_ { };
            std::vector<std::string>& snapshots_;
            bool nested_write_ { };
        };

        interpreter.set_process_executor(
            writer_id,
            std::make_unique<ReentrantWriter>(
                interpreter, writer_id, signal, snapshots));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed && result.time == 0U,
            "the synchronous reentrant driver write completes at its current tick");
        require(
            snapshots
                == std::vector<std::string> {
                    "driver@0 driver=0 stored=Z current=Z",
                    "driver@0 driver=1 stored=Z current=Z",
                    "stored@0 driver=1 stored=1 current=Z",
                    "current@0 driver=1 stored=1 current=1"
                }
                && interpreter.driver_value(writer_id, signal)
                    == PackedLogic4::from_msb_string("1")
                && interpreter.stored_signal_value(signal)
                    == PackedLogic4::from_msb_string("1")
                && interpreter.signal_value(signal)
                    == PackedLogic4::from_msb_string("1"),
            "a reentrant writer callback changes the live raw slot before "
            "the nested aggregate is stored and published; the outer resolve "
            "then observes that same current driver slot");
    }

    {
        // A retained packed signal is the reference route for one whole
        // array-driver assignment: set_driver replaces the complete raw
        // vector before the driver callback, and the container alias projects
        // that vector into its two logical elements.
        Interpreter interpreter;
        ContainerType type;
        type.fixed = true;
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        type.element_width = 8U;
        const auto object = interpreter.add_container_object({
            "driver_hook.retained_array",
            default_container_value(type),
            std::nullopt
        });
        const auto signal = interpreter.add_signal({
            "driver_hook.retained_array_backing",
            PackedLogic4::from_msb_string("ZZZZZZZZZZZZZZZZ"),
            ResolutionKind::sv_wire
        });
        interpreter.add_container_signal_alias(
            { object, signal, true, true });

        Process writer;
        writer.id = 0U;
        writer.name = "driver_hook.retained_array_writer";
        writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        writer.driver_regions.push_back({ signal, 0U, 0U, true });
        writer.operations = { Halt { } };
        const auto writer_id = interpreter.add_process(std::move(writer));
        interpreter.force_signal_slice(
            signal, PackedLogic4::from_msb_string("10101010"), 8U);

        std::vector<std::string> snapshots;
        class WholeArrayDriverWriter final : public ProcessExecutor {
        public:
            WholeArrayDriverWriter(
                Interpreter& interpreter,
                const ProcessId process,
                const SignalId signal,
                const ContainerObjectId object,
                std::vector<std::string>& snapshots)
                : interpreter_ { interpreter }
                , process_ { process }
                , signal_ { signal }
                , object_ { object }
                , snapshots_ { snapshots }
            {
            }

            [[nodiscard]] ProcessResumeResult resume(
                ProcessExecutionContext& context,
                const InstructionIndex start) override
            {
                require(start == 0U,
                    "the retained whole-array writer starts at entry");
                interpreter_.set_driver_change_hook(
                    [this, &context](
                        const ProcessId changed_process,
                        const SignalId changed_signal,
                        const SimulationTick time) {
                        if (changed_process != process_
                            || changed_signal != signal_) {
                            return;
                        }
                        record("driver", time);
                        if (!nested_write_) {
                            nested_write_ = true;
                            context.write_blocking(
                                signal_, PackedLogic4::from_msb_string(
                                    "1111000010100101"));
                        }
                    });
                interpreter_.set_stored_signal_change_hook(
                    [this](
                        const SignalId changed,
                        const SimulationTick time) {
                        if (changed == signal_) {
                            record("stored", time);
                        }
                    });
                interpreter_.set_signal_change_hook(
                    [this](
                        const SignalId changed,
                        const PackedLogic4&,
                        const SimulationTick time) {
                        if (changed == signal_) {
                            record("current", time);
                        }
                    });

                context.write_blocking(
                    signal_, PackedLogic4::from_msb_string(
                        "0001001000110100"));
                interpreter_.set_driver_change_hook({ });
                interpreter_.set_stored_signal_change_hook({ });
                interpreter_.set_signal_change_hook({ });

                ProcessResumeResult result { 0U, 1U };
                result.external.kind = ExternalSuspendKind::halt;
                return result;
            }

        private:
            void record(
                const std::string_view phase,
                const SimulationTick time)
            {
                const auto& logical
                    = interpreter_.container_object_value(object_);
                const auto current = project_packed(
                    interpreter_.signal_value(signal_));
                require(project_container(logical) == current,
                    "the logical array follows the retained current vector");
                snapshots_.push_back(
                    std::string { phase } + "@" + std::to_string(time)
                    + " raw=" + project_packed(
                        interpreter_.driver_value(process_, signal_))
                    + " stored=" + project_packed(
                        interpreter_.stored_signal_value(signal_))
                    + " current=" + current
                    + " forced="
                    + (interpreter_.signal_is_forced(signal_)
                            ? "1" : "0"));
            }

            [[nodiscard]] std::string project_packed(
                const PackedLogic4& packed) const
            {
                const auto bits = packed.to_msb_string();
                require(bits.size() == 16U,
                    "the retained aggregate projection has two bytes");
                return bits.substr(0U, 8U) + "|"
                    + bits.substr(8U, 8U);
            }

            [[nodiscard]] std::string project_container(
                const ContainerValue& value) const
            {
                require(value.elements.size() == 2U,
                    "the retained logical array has two elements");
                return value.elements[0].to_msb_string() + "|"
                    + value.elements[1].to_msb_string();
            }

            Interpreter& interpreter_;
            ProcessId process_ { };
            SignalId signal_ { };
            ContainerObjectId object_ { };
            std::vector<std::string>& snapshots_;
            bool nested_write_ { };
        };

        interpreter.set_process_executor(
            writer_id,
            std::make_unique<WholeArrayDriverWriter>(
                interpreter, writer_id, signal, object, snapshots));
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed && result.time == 0U,
            "the retained whole-array driver reference completes");
        require(
            snapshots == std::vector<std::string> {
                "driver@0 raw=00010010|00110100 "
                "stored=ZZZZZZZZ|ZZZZZZZZ current=10101010|ZZZZZZZZ "
                "forced=1",
                "driver@0 raw=11110000|10100101 "
                "stored=ZZZZZZZZ|ZZZZZZZZ current=10101010|ZZZZZZZZ "
                "forced=1",
                "stored@0 raw=11110000|10100101 "
                "stored=11110000|10100101 current=10101010|ZZZZZZZZ "
                "forced=1",
                "current@0 raw=11110000|10100101 "
                "stored=11110000|10100101 current=10101010|10100101 "
                "forced=1"
            }
            && interpreter.driver_value(writer_id, signal)
                == PackedLogic4::from_msb_string("1111000010100101")
            && interpreter.stored_signal_value(signal)
                == PackedLogic4::from_msb_string("1111000010100101")
            && interpreter.signal_value(signal)
                == PackedLogic4::from_msb_string("1010101010100101")
            && interpreter.container_object_value(object).elements
                == std::vector<PackedLogic4> {
                    PackedLogic4::from_msb_string("10101010"),
                    PackedLogic4::from_msb_string("10100101")
                }
            && interpreter.signal_is_forced(signal),
            "whole driver writes expose complete raw-driver, stored, current, "
            "force, and logical-array projections at each callback");
        interpreter.release_signal_slice(signal, 8U, 8U);
        require(
            !interpreter.signal_is_forced(signal)
                && interpreter.signal_value(signal)
                    == PackedLogic4::from_msb_string("1111000010100101")
                && interpreter.container_object_value(object).elements
                    == std::vector<PackedLogic4> {
                        PackedLogic4::from_msb_string("11110000"),
                        PackedLogic4::from_msb_string("10100101")
                    },
            "releasing the reference force reveals the final full driver vector");
    }
}

} // namespace fsim::tests::runtime
