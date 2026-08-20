// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

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
        "string container copies own strict UTF-8 values independently");

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
    auto invalid_utf8_key = default_container_value(string_associative_type);
    invalid_utf8_key.string_keys = { std::string { "\xc0\x80", 2 } };
    invalid_utf8_key.elements = { value(8, 1) };
    require_invalid_string_keys(std::move(invalid_utf8_key));
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
            bridged_object, 0, 1, true, false, std::nullopt },
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

} // namespace fsim::tests::runtime
