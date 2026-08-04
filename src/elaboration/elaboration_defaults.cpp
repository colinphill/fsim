// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
namespace {

void insert_default(
    PackedLogic4& destination,
    const PackedLogic4& source,
    const std::size_t offset) {
  for (std::size_t bit = 0; bit < source.width(); ++bit) {
    if (destination.is_logic9()) {
      destination.set_logic9(
          offset + bit, source.get_logic9(bit));
    } else {
      destination.set(
          offset + bit,
          runtime::to_logic4(source.get_logic9(bit)));
    }
  }
}

}  // namespace

PackedLogic4 default_packed_value(
    const frontend::Type& type,
    const std::size_t width) {
  if (!type.enumeration_literals.empty()
      && type.enumeration_range) {
    return unsigned_value(
        static_cast<std::uint64_t>(type.enumeration_range->left),
        width);
  }
  auto result = PackedLogic4(
      width,
      is_two_state_domain(type.domain)
          ? Logic4::zero
          : Logic4::x);
  if (type.domain == frontend::ValueDomain::Logic9) {
    result.fill(runtime::Logic9::u);
  }
  if (type.vhdl_array
      && !type.vhdl_array->element_types.empty()) {
    const auto& element = type.vhdl_array->element_types.front();
    const auto element_width = element.width();
    if (element_width && *element_width != 0) {
      const auto element_default = default_packed_value(
          element, static_cast<std::size_t>(*element_width));
      for (std::size_t offset = 0;
           offset + *element_width <= width;
           offset += static_cast<std::size_t>(*element_width)) {
        insert_default(result, element_default, offset);
      }
    }
    return result;
  }
  if (type.domain == frontend::ValueDomain::Integer && width != 0) {
    return unsigned_value(
        static_cast<std::uint64_t>(
            type.integer_range
                ? type.integer_range->left
                : std::numeric_limits<std::int32_t>::min()),
        width);
  }
  for (const auto& member : type.packed_members) {
    const auto member_width = member.width();
    if (!member_width
        || member.lsb_offset > width
        || *member_width > width - member.lsb_offset) {
      continue;
    }
    if (!member.nested_types.empty()) {
      insert_default(
          result,
          default_packed_value(
              member.nested_types.front(),
              static_cast<std::size_t>(*member_width)),
          static_cast<std::size_t>(member.lsb_offset));
      continue;
    }
    for (std::uint64_t bit = 0; bit < *member_width; ++bit) {
      const auto index =
          static_cast<std::size_t>(member.lsb_offset + bit);
      if (result.is_logic9()) {
        result.set_logic9(
            index,
            member.domain == frontend::ValueDomain::Logic9
                ? runtime::Logic9::u
                : member.domain == frontend::ValueDomain::Logic4
                    ? runtime::Logic9::x
                    : runtime::Logic9::zero);
      } else {
        result.set(
            index,
            is_two_state_domain(member.domain)
                ? Logic4::zero
                : Logic4::x);
      }
    }
  }
  return result;
}

}  // namespace fsim::elaboration::elaboration_detail
