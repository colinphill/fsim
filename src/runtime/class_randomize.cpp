// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_randomize.hpp"
#include "fsim/runtime/class_randomize_graph.hpp"

#include "constraint_bounds_internal.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fsim::runtime {
namespace {

struct SelectedProperty {
  std::size_t property_index{};
  SystemVerilogConstraintVariableId variable{};
  std::vector<PackedLogic4> base_domain;
  std::optional<SystemVerilogClassRandomState> randc_state;
  bool randc_committed{};
  // Drawn from the keyed permutation of a wide unconstrained randc.
  bool wide_randc{};
};

[[nodiscard]] std::uint64_t mixed(const std::uint64_t input) noexcept {
  auto value = input;
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t domain_signature(
    const std::span<const PackedLogic4> domain) noexcept {
  auto signature = UINT64_C(14695981039346656037);
  const auto retain = [&](const std::uint64_t word) {
    signature ^= word;
    signature *= UINT64_C(1099511628211);
  };
  retain(domain.size());
  for (const auto& value : domain) {
    retain(value.width());
    for (const auto word : value.aval_words()) retain(word);
    for (const auto word : value.bval_words()) retain(word);
  }
  return signature == 0 ? UINT64_C(1) : signature;
}

// The domain signature of a wide randc property's permutation walk.
[[nodiscard]] std::uint64_t wide_randc_signature(
    const std::size_t width) noexcept {
  return mixed(UINT64_C(0x7769646572616e64) ^ width);
}

// Position `position` of a keyed bijection of the `width`-bit values: each
// round adds, multiplies by an odd constant and xor-shifts within the width,
// all invertible modulo 2^width, so one cycle draws every value once
// (IEEE 1800-2017 18.4.2).
[[nodiscard]] std::uint64_t wide_randc_value(const std::uint64_t position,
    const std::size_t width, const std::uint64_t key) noexcept {
  const auto mask = width >= 64U
      ? ~std::uint64_t{}
      : (std::uint64_t{1} << width) - 1U;
  const auto shift = static_cast<unsigned>(std::max<std::size_t>(width / 2U, 1U));
  auto value = position & mask;
  auto round_key = key;
  for (int round = 0; round < 4; ++round) {
    round_key = mixed(round_key + UINT64_C(0x9e3779b97f4a7c15));
    value = (value + round_key) & mask;
    value = (value * (mixed(round_key) | 1U)) & mask;
    value ^= value >> shift;
  }
  return value;
}

[[nodiscard]] std::vector<PackedLogic4> randc_domain(
    const std::span<const PackedLogic4> base,
    const SystemVerilogClassRandomState& state) {
  std::set<std::uint64_t> used;
  for (const auto index : state.randc_used_values) {
    if (index >= base.size() || !used.insert(index).second) {
      throw std::logic_error{"randc cycle state is structurally invalid"};
    }
  }
  std::vector<std::size_t> order;
  order.reserve(base.size() - used.size());
  for (std::size_t index = 0; index < base.size(); ++index) {
    if (!used.contains(index)) order.push_back(index);
  }
  auto selection = mixed(
      state.stream_seed ^ state.randc_domain_signature
      ^ mixed(state.randc_cycle));
  for (std::size_t remaining = order.size(); remaining > 1U; --remaining) {
    const auto selected = static_cast<std::size_t>(selection % remaining);
    std::swap(order[remaining - 1U], order[selected]);
    selection = mixed(selection + UINT64_C(0x9e3779b97f4a7c15));
  }
  std::vector<PackedLogic4> result;
  result.reserve(order.size());
  for (const auto index : order) result.push_back(base[index]);
  return result;
}

[[nodiscard]] std::optional<std::size_t> property_index(
    const SystemVerilogClassObject& object,
    const std::string_view name) {
  const auto exact = std::ranges::find(object.property_names, name);
  if (exact != object.property_names.end()) {
    return static_cast<std::size_t>(
        std::distance(object.property_names.begin(), exact));
  }
  const auto suffix = "::" + std::string{name};
  const auto found = std::find_if(
      object.property_names.rbegin(), object.property_names.rend(),
      [&](const auto& candidate) { return candidate.ends_with(suffix); });
  if (found == object.property_names.rend()) return std::nullopt;
  return static_cast<std::size_t>(
      std::distance(object.property_names.begin(), std::prev(found.base())));
}

[[nodiscard]] SystemVerilogConstraintVariableProfile profile(
    const SystemVerilogClassPropertyValue& property) {
  const auto* random = property.random_state
      ? &*property.random_state : nullptr;
  // Integer types (`int`, `integer`, `shortint`, ...) have the integer
  // domain kind, as the constraint lowering gives them.
  const bool integer_type = property.kind == SystemVerilogClassPropertyKind::Integer
      || (random != nullptr
          && random->nominal_type.find("int") != std::string::npos);
  return {
      integer_type
          ? SystemVerilogConstraintDomainKind::Integer
          : SystemVerilogConstraintDomainKind::BitVector,
      property.packed.width(),
      random != nullptr && random->signed_value,
      random != nullptr
          ? random->nominal_type
          : property.kind == SystemVerilogClassPropertyKind::Integer
              ? std::string{"integer"}
              : std::string{"packed property"},
      property.kind != SystemVerilogClassPropertyKind::Bit2};
}

[[nodiscard]] std::vector<PackedLogic4> complete_domain(
    const std::size_t width,
    const std::size_t available_values) {
  constexpr auto size_bits = std::numeric_limits<std::size_t>::digits;
  if (width >= size_bits) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "class random property domain exceeds the solver domain-value budget"};
  }
  const auto count = std::size_t{1} << width;
  if (count > available_values) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "class random property domain exceeds the solver domain-value budget"};
  }
  std::vector<PackedLogic4> result;
  result.reserve(count);
  for (std::size_t value = 0; value < count; ++value) {
    result.push_back(PackedLogic4::from_aval_bval(width, value, 0));
  }
  return result;
}

// Whether the request gives a property an exact (enumerated) domain.
using PropertyDomains
    = std::map<std::string, std::vector<PackedLogic4>, std::less<>>;

// An element `a[i]` or `a[i][j]` of a container takes the container's
// domain.
[[nodiscard]] std::string_view element_container(
    std::string_view canonical_identity) {
  while (canonical_identity.ends_with(']')) {
    const auto open = canonical_identity.rfind('[');
    if (open == std::string_view::npos) break;
    const auto index = canonical_identity.substr(
        open + 1U, canonical_identity.size() - open - 2U);
    if (index.empty() || !std::ranges::all_of(index, [](const char digit) {
          return digit >= '0' && digit <= '9';
        })) {
      break;
    }
    canonical_identity = canonical_identity.substr(0, open);
  }
  return canonical_identity;
}

[[nodiscard]] bool has_exact_domain(
    const PropertyDomains& domains,
    std::string_view canonical_identity) {
  canonical_identity = element_container(canonical_identity);
  if (domains.contains(canonical_identity)) return true;
  const auto separator = canonical_identity.rfind("::");
  return separator != std::string_view::npos
      && domains.contains(canonical_identity.substr(separator + 2U));
}

[[nodiscard]] std::vector<PackedLogic4> exact_domain(
    const PropertyDomains& domains,
    std::string_view canonical_identity,
    const SystemVerilogClassPropertyValue& property,
    const std::size_t available_values) {
  canonical_identity = element_container(canonical_identity);
  auto found = domains.find(canonical_identity);
  if (found == domains.end()) {
    const auto separator = canonical_identity.rfind("::");
    if (separator != std::string_view::npos) {
      found = domains.find(canonical_identity.substr(separator + 2U));
    }
  }
  if (found == domains.end()) {
    return complete_domain(property.packed.width(), available_values);
  }
  if (found->second.empty() || found->second.size() > available_values) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "class random property exact domain exceeds the solver domain-value budget"};
  }
  for (std::size_t index = 0; index < found->second.size(); ++index) {
    if (found->second[index].width() != property.packed.width()
        || found->second[index].is_logic9()
        || std::ranges::find(
               std::span{found->second}.first(index), found->second[index])
            != std::span{found->second}.first(index).end()) {
      throw std::invalid_argument{
          "class random property exact domain does not match its profile"};
    }
  }
  return found->second;
}

// Whether `left` orders before `right` as known values of one width.
[[nodiscard]] bool value_less(const PackedLogic4& left,
                              const PackedLogic4& right,
                              const bool signed_value) {
  // Known single-word values compare as integers.
  if (left.width() > 0U && left.width() <= 64U
      && left.width() == right.width() && !left.is_logic9()
      && !right.is_logic9()) {
    const auto l = left.low_word();
    const auto r = right.low_word();
    if ((l.bval | r.bval) == 0U) {
      const auto flip = signed_value
          ? std::uint64_t{1} << (left.width() - 1U) : std::uint64_t{};
      return (l.aval ^ flip) < (r.aval ^ flip);
    }
  }
  for (std::size_t bit = left.width(); bit-- > 0U;) {
    auto a = left.get(bit) == Logic4::one;
    auto b = right.get(bit) == Logic4::one;
    if (signed_value && bit + 1U == left.width()) std::swap(a, b);
    if (a != b) return b;
  }
  return false;
}

// The least and greatest values of a domain in the variable's order.
[[nodiscard]] std::pair<PackedLogic4, PackedLogic4> domain_bounds(
    const std::span<const PackedLogic4> domain, const bool signed_value) {
  auto lowest = domain.front();
  auto highest = domain.front();
  for (const auto& value : domain) {
    if (value.width() != lowest.width()) continue;
    if (value_less(value, lowest, signed_value)) lowest = value;
    if (value_less(highest, value, signed_value)) highest = value;
  }
  return {std::move(lowest), std::move(highest)};
}

// A uniformly chosen two-state value of any width.
[[nodiscard]] PackedLogic4 random_packed(
    SystemVerilogClassRandomStream& stream, const std::size_t width) {
  if (width <= 64U) {
    auto value = stream.next_u64();
    if (width < 64U) value &= (std::uint64_t{1} << width) - 1U;
    return PackedLogic4::from_aval_bval(width, value, 0);
  }
  PackedLogic4 result(width, Logic4::zero);
  for (std::size_t base = 0; base < width; base += 64U) {
    const auto word = stream.next_u64();
    for (std::size_t bit = 0; bit < 64U && base + bit < width; ++bit) {
      if (((word >> bit) & 1U) != 0U) result.set(base + bit, Logic4::one);
    }
  }
  return result;
}

[[nodiscard]] SystemVerilogClassRandomizeResult exhausted(
    const SystemVerilogConstraintResource resource) {
  return {
      SystemVerilogConstraintSolveStatus::ResourceExhausted,
      resource, 0, 0};
}

/// The objects an array, queue or associative array of class handles holds,
/// by position: sequential elements in order, associative elements in key
/// order, nested containers level by level (IEEE 1800-2017 7.4.5, 7.8,
/// 18.5.9). A container is a `level` (its element count and shape, named
/// by `path` from the property) and a handle is a `leaf` at `path`
/// (`[1]`, `[1][0]`).
struct HandleLevel {
  std::string path;
  std::size_t size{};
  SystemVerilogConstraintContainerShape shape;
};

struct HandleLeaf {
  std::string path;
  SystemVerilogClassHandle handle{};
};

struct HandleLayout {
  std::vector<HandleLevel> levels;
  std::vector<HandleLeaf> leaves;
};

[[nodiscard]] std::size_t extent(
    const std::int64_t left, const std::int64_t right) {
  return static_cast<std::size_t>(left >= right ? left - right : right - left)
      + 1U;
}

SystemVerilogConstraintContainerShape fixed_handle_shape(
    const std::int64_t left, const std::int64_t right) {
  SystemVerilogConstraintContainerShape shape;
  shape.fixed = true;
  shape.left = left;
  shape.descending = left > right;
  return shape;
}

/// The levels and leaves of a multidimensional fixed-size array stored
/// flat, the rightmost dimension varying fastest (IEEE 1800-2017 7.4.5);
/// `handle_at` gives the handle at a flat position.
template <typename HandleAt>
void fixed_handle_layout(
    const std::span<const std::pair<std::int64_t, std::int64_t>> dimensions,
    const std::size_t stored, const std::string& path, HandleAt&& handle_at,
    HandleLayout& layout) {
  const auto rank = dimensions.size();
  std::vector<std::size_t> strides(rank, 1U);
  for (std::size_t k = rank - 1U; k-- > 0U;) {
    strides[k] = strides[k + 1U]
        * extent(dimensions[k + 1U].first, dimensions[k + 1U].second);
  }
  const auto dimension = [&](auto&& next, const std::size_t k,
                             const std::size_t offset,
                             const std::string& prefix) -> void {
    const auto [left, right] = dimensions[k];
    const auto count = extent(left, right);
    layout.levels.push_back({prefix, count, fixed_handle_shape(left, right)});
    for (std::size_t position = 0; position < count; ++position) {
      const auto child = prefix + "[" + std::to_string(position) + "]";
      const auto at = offset + position * strides[k];
      if (k + 1U != rank) {
        next(next, k + 1U, at, child);
      } else if (at < stored) {
        layout.leaves.push_back({child, handle_at(at)});
      }
    }
  };
  dimension(dimension, 0U, 0U, path);
}

void handle_value_layout(const simir::ContainerValue& value,
                         const std::string& path, HandleLayout& layout) {
  const auto& type = value.type;
  if (type.fixed && type.dimensions.size() > 1U) {
    std::vector<std::pair<std::int64_t, std::int64_t>> dimensions;
    for (const auto& [left, right] : type.dimensions) {
      dimensions.emplace_back(left, right);
    }
    fixed_handle_layout(dimensions, value.elements.size(), path,
        [&](const std::size_t at) {
          return static_cast<SystemVerilogClassHandle>(
              value.elements[at].low_word().aval);
        },
        layout);
    return;
  }
  SystemVerilogConstraintContainerShape shape;
  if (type.fixed) {
    shape = fixed_handle_shape(type.index_left, type.index_right);
  }
  if (type.associative) {
    shape.associative = true;
    shape.string_keys = type.string_indices;
    shape.keys = value.keys;
    shape.string_key_values = value.string_keys;
  }
  if (type.element_kind == simir::ContainerElementKind::Container) {
    layout.levels.push_back({path, value.nested_elements.size(), shape});
    for (std::size_t index = 0; index < value.nested_elements.size();
         ++index) {
      handle_value_layout(value.nested_elements[index],
          path + "[" + std::to_string(index) + "]", layout);
    }
    return;
  }
  layout.levels.push_back({path, value.elements.size(), shape});
  for (std::size_t index = 0; index < value.elements.size(); ++index) {
    layout.leaves.push_back({path + "[" + std::to_string(index) + "]",
        static_cast<SystemVerilogClassHandle>(
            value.elements[index].low_word().aval)});
  }
}

/// The layout of a property holding class handles; nothing for another
/// property.
[[nodiscard]] std::optional<HandleLayout> handle_layout(
    const SystemVerilogClassPropertyValue& property) {
  HandleLayout layout;
  if (property.nested_handles && property.container) {
    handle_value_layout(*property.container, std::string{}, layout);
    return layout;
  }
  if (!property.handle_container) return std::nullopt;
  const auto& held = *property.handle_container;
  if (held.descriptor().dimensions.size() > 1U) {
    const auto elements = held.sequential_values();
    fixed_handle_layout(held.descriptor().dimensions, elements.size(),
        std::string{}, [&](const std::size_t at) { return elements[at]; },
        layout);
    return layout;
  }
  if (held.kind() != SystemVerilogClassContainerKind::AssociativeArray) {
    layout.levels.push_back({std::string{}, held.size(), {}});
    const auto elements = held.sequential_values();
    for (std::size_t index = 0; index < elements.size(); ++index) {
      layout.leaves.push_back({"[" + std::to_string(index) + "]",
          elements[index]});
    }
    return layout;
  }
  const auto& description = held.descriptor();
  SystemVerilogConstraintContainerShape shape;
  shape.associative = true;
  shape.string_keys = description.string_keys;
  if (description.string_keys) {
    // std::map keeps string keys in the order of their bytes (7.8.2).
    for (const auto& [key, handle] : held.keyed_values()) {
      shape.string_key_values.push_back(key);
      layout.leaves.push_back({
          "[" + std::to_string(layout.leaves.size()) + "]", handle});
    }
  } else {
    // Integral keys are named by their decimal text; they order as
    // numbers of the key type (7.8.3).
    const auto width = std::max<std::size_t>(description.key_width, 1U);
    std::vector<std::pair<PackedLogic4, SystemVerilogClassHandle>> keyed;
    for (const auto& [key, handle] : held.keyed_values()) {
      std::int64_t number{};
      const auto parsed = std::from_chars(
          key.data(), key.data() + key.size(), number);
      if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size()) {
        std::uint64_t unsigned_number{};
        const auto retried = std::from_chars(
            key.data(), key.data() + key.size(), unsigned_number);
        if (retried.ec != std::errc{}
            || retried.ptr != key.data() + key.size()) {
          return std::nullopt;
        }
        number = static_cast<std::int64_t>(unsigned_number);
      }
      keyed.emplace_back(PackedLogic4::from_aval_bval(
                             width, static_cast<std::uint64_t>(number), 0),
          handle);
    }
    const auto signed_value = [&](const PackedLogic4& key) {
      auto word = key.low_word().aval;
      if (width < 64U && (word & (UINT64_C(1) << (width - 1U))) != 0U) {
        word |= ~UINT64_C(0) << width;
      }
      return static_cast<std::int64_t>(word);
    };
    std::ranges::sort(keyed, [&](const auto& left, const auto& right) {
      return description.signed_keys
          ? signed_value(left.first) < signed_value(right.first)
          : left.first.low_word().aval < right.first.low_word().aval;
    });
    for (auto& [key, handle] : keyed) {
      shape.keys.push_back(std::move(key));
      layout.leaves.push_back({
          "[" + std::to_string(layout.leaves.size()) + "]", handle});
    }
  }
  layout.levels.push_back({std::string{}, layout.leaves.size(), shape});
  return layout;
}

}  // namespace

SystemVerilogClassRandomizeResult randomize_systemverilog_class_object(
    SystemVerilogClassHeap& heap,
    const SystemVerilogClassHandle handle,
    const SystemVerilogClassRandomizeRequest& request) {
  return randomize_systemverilog_class_graph(heap, handle, request, {});
}

std::vector<SystemVerilogClassRandomizeMember>
systemverilog_class_randomize_members(
    const SystemVerilogClassHeap& heap,
    const SystemVerilogClassHandle root) {
  std::vector<SystemVerilogClassRandomizeMember> members;
  std::set<SystemVerilogClassHandle> visited{root};
  struct Pending {
    SystemVerilogClassHandle handle{};
    std::string path;
    bool randomized{};
  };
  // Pre-order: each object before the objects it refers to.
  std::vector<Pending> stack{{root, std::string{}, true}};
  while (!stack.empty()) {
    auto current = std::move(stack.back());
    stack.pop_back();
    if (current.handle != root) {
      members.push_back({current.handle, current.path, current.randomized,
          {}, {}});
    }
    const auto& object = heap.object(current.handle);
    std::vector<Pending> children;
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
      const auto& property = object.properties[index];
      if (property.kind != SystemVerilogClassPropertyKind::ClassHandle
          && !property.handle_container && !property.nested_handles) {
        continue;
      }
      // Only rand handles extend randomization to their objects
      // (IEEE 1800-2017 18.5.9); other referenced objects are state.
      const bool rand_handle = property.random_state
          && property.random_state->enabled
          && property.random_state->kind == SystemVerilogClassRandomKind::Rand;
      if (property.handle_container || property.nested_handles) {
        // The objects of an array, queue or associative array of handles
        // join position by position as `name[i].` (IEEE 1800-2017 7.8,
        // 18.4, 18.5.9).
        if (const auto layout = handle_layout(property)) {
          for (const auto& leaf : layout->leaves) {
            if (leaf.handle == 0 || !visited.insert(leaf.handle).second) {
              continue;
            }
            children.push_back({leaf.handle,
                current.path + object.property_names[index] + leaf.path + ".",
                current.randomized && rand_handle});
          }
        }
        continue;
      }
      if (property.handle == 0 || !visited.insert(property.handle).second) {
        continue;
      }
      children.push_back({property.handle,
          current.path + object.property_names[index] + ".",
          current.randomized && rand_handle});
    }
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
      stack.push_back(std::move(*child));
    }
  }
  return members;
}

SystemVerilogClassRandomizeResult randomize_systemverilog_class_graph(
    SystemVerilogClassHeap& heap,
    const SystemVerilogClassHandle handle,
    const SystemVerilogClassRandomizeRequest& request,
    const std::span<const SystemVerilogClassRandomizeMember> members,
    const std::span<SystemVerilogClassRandomizeContainer> containers) {
  if (request.call_identity.empty()) {
    throw std::invalid_argument{
        "class randomize call identity must not be empty"};
  }
  // The root and each member object take part in one solve.
  struct Node {
    SystemVerilogClassHandle handle{};
    SystemVerilogClassObject* object{};
    std::string path;
    std::set<std::size_t> selected;
    const PropertyDomains* domains{};
    const SystemVerilogClassConstraintConfigurator* constraints{};
    /// Cleared for an element past the solved size of its container.
    bool active{true};
  };
  std::vector<Node> nodes;
  nodes.reserve(members.size() + 1U);
  nodes.push_back({handle, &heap.object(handle), std::string{}, {},
      &request.property_domains, &request.class_constraints});
  for (const auto& member : members) {
    nodes.push_back({member.handle, &heap.object(member.handle), member.path,
        {}, &member.property_domains, &member.class_constraints});
    if (!member.randomized) continue;
    auto& node = nodes.back();
    for (std::size_t index = 0; index < node.object->properties.size();
         ++index) {
      const auto& property = node.object->properties[index];
      const auto& random = property.random_state;
      if (random && random->enabled
          && random->kind != SystemVerilogClassRandomKind::None
          && (!property.packed.empty()
              || property.kind == SystemVerilogClassPropertyKind::Container)) {
        node.selected.insert(index);
      }
    }
  }
  auto& object = *nodes.front().object;
  auto& selected_indices = nodes.front().selected;
  if (request.selection
          == SystemVerilogClassRandomizeSelection::NoProperties) {
    if (!request.variable_list.empty()) {
      throw std::invalid_argument{
          "class randomize checker mode cannot select properties"};
    }
    for (auto& node : nodes) node.selected.clear();
  } else if (request.variable_list.empty()) {
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
      const auto& random = object.properties[index].random_state;
      if (random && random->enabled
          && random->kind != SystemVerilogClassRandomKind::None) {
        selected_indices.insert(index);
      }
    }
  } else {
    // randomize(a, b) selects the root's named properties only (18.11).
    for (std::size_t node = 1; node < nodes.size(); ++node) {
      nodes[node].selected.clear();
    }
    for (const auto& name : request.variable_list) {
      const auto index = property_index(object, name);
      if (!index) {
        throw std::out_of_range{
            "SystemVerilog class randomize property '" + name
            + "' does not exist"};
      }
      auto& property = object.properties[*index];
      // A listed handle is random for this call, so its object's random
      // properties are randomized (IEEE 1800-2017 18.11, 18.5.9).
      if (property.kind == SystemVerilogClassPropertyKind::ClassHandle
          && !property.handle_container) {
        const auto path = object.property_names[*index] + ".";
        for (std::size_t node = 1; node < nodes.size(); ++node) {
          if (nodes[node].path != path) continue;
          const auto& properties = nodes[node].object->properties;
          for (std::size_t member = 0; member < properties.size(); ++member) {
            const auto& random = properties[member].random_state;
            if (random && random->enabled
                && random->kind != SystemVerilogClassRandomKind::None
                && (!properties[member].packed.empty()
                    || properties[member].kind
                        == SystemVerilogClassPropertyKind::Container)) {
              nodes[node].selected.insert(member);
            }
          }
        }
        continue;
      }
      // A listed property is random for this call even when it is not
      // declared rand (IEEE 1800-2017 18.11).
      if (!property.random_state && !property.packed.empty()
          && (property.kind == SystemVerilogClassPropertyKind::Bit2
              || property.kind == SystemVerilogClassPropertyKind::Logic4
              || property.kind == SystemVerilogClassPropertyKind::Integer)) {
        property.random_state = SystemVerilogClassRandomState{
            SystemVerilogClassRandomKind::None,
            property.packed.width(),
            property.kind == SystemVerilogClassPropertyKind::Integer,
            property.kind == SystemVerilogClassPropertyKind::Integer
                ? std::string{"int"}
                : property.kind == SystemVerilogClassPropertyKind::Bit2
                    ? std::string{"bit"}
                    : std::string{"logic"},
            true,
            0,
            0,
            0,
            0,
            {}};
      }
      const auto& random = property.random_state;
      const bool listed_container
          = property.kind == SystemVerilogClassPropertyKind::Container
          && std::ranges::any_of(containers, [&](const auto& container) {
               return container.node == 0U
                   && container.name == object.property_names[*index];
             });
      if (!random && !listed_container) {
        throw std::invalid_argument{
            "SystemVerilog class randomize property '" + name
            + "' is not randomizable"};
      }
      if (random && !random->enabled) {
        throw std::invalid_argument{
            "SystemVerilog class randomize property '" + name
            + "' is disabled"};
      }
      if (!selected_indices.insert(*index).second) {
        throw std::invalid_argument{
            "duplicate SystemVerilog class randomize property '" + name
            + "'"};
      }
    }
  }
  const auto identity_of = [&](const Node& node, const std::size_t index) {
    return node.path + node.object->property_names[index];
  };
  // Each object's constraints see its own names and, through `h.m`
  // paths, those of the objects it refers to.
  const auto view_of = [](const Node& node,
                          const SystemVerilogClassRandomizeVariables& all) {
    if (node.path.empty()) return all;
    SystemVerilogClassRandomizeVariables view;
    for (const auto& [identity, id] : all) {
      if (identity.starts_with(node.path)) {
        view.emplace(identity.substr(node.path.size()), id);
      }
    }
    return view;
  };
  const auto configure = [&](SystemVerilogConstraintSolver& solver,
                             const SystemVerilogClassRandomizeVariables& all) {
    for (const auto& container : containers) {
      for (const auto& shape : container.shapes) {
        solver.set_container_shape(
            nodes[container.node].path + container.name + shape.path,
            shape.shape);
      }
    }
    // The shapes of the arrays of handles: the keys of an associative array
    // and the bounds of a fixed-size one (IEEE 1800-2017 7.4, 7.8).
    for (const auto& node : nodes) {
      for (std::size_t index = 0; index < node.object->properties.size();
           ++index) {
        const auto layout = handle_layout(node.object->properties[index]);
        if (!layout) continue;
        for (const auto& level : layout->levels) {
          if (!level.shape.fixed && !level.shape.associative) continue;
          solver.set_container_shape(node.path
                  + node.object->property_names[index] + level.path,
              level.shape);
        }
      }
    }
    // Soft constraints added later take priority; a containing object's
    // soft constraints outrank those of its rand member objects (IEEE
    // 1800-2017 18.5.14.1), so the members (in reverse preorder) come
    // first and the root last.
    for (auto node = nodes.rbegin(); node != nodes.rend(); ++node) {
      if (node->active && node->constraints != nullptr
          && *node->constraints) {
        (*node->constraints)(solver, view_of(*node, all));
      }
    }
    if (request.inline_constraints) {
      request.inline_constraints(solver, all);
    }
  };
  using Key = std::pair<std::size_t, std::size_t>;

  // Container elements and sizes take part as properties of their objects
  // for the duration of the call: element `name[i]`, the materialized
  // element count `name[]`, and, while sizes are solved, `name.size()`.
  std::vector<std::size_t> original_counts;
  original_counts.reserve(nodes.size());
  for (const auto& node : nodes) {
    original_counts.push_back(node.object->properties.size());
  }
  // Random size properties that no constraint references keep their size.
  std::set<Key> preserved;
  const auto strip = [&] {
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      auto& node = nodes[n];
      node.object->properties.resize(original_counts[n]);
      node.object->property_names.resize(original_counts[n]);
      std::erase_if(node.selected, [&](const std::size_t index) {
        return index >= original_counts[n];
      });
    }
    preserved.clear();
  };
  struct StripOnExit {
    const decltype(strip)& run;
    explicit StripOnExit(const decltype(strip)& action) : run(action) {}
    StripOnExit(const StripOnExit&) = delete;
    StripOnExit& operator=(const StripOnExit&) = delete;
    ~StripOnExit() { run(); }
  } strip_on_exit{strip};
  std::vector<bool> randomized_container(containers.size());
  for (std::size_t c = 0; c < containers.size(); ++c) {
    const auto& container = containers[c];
    if (container.node >= nodes.size()) {
      throw std::invalid_argument{
          "class randomize container belongs to no object of the call"};
    }
    const auto& node = nodes[container.node];
    const auto found = std::ranges::find(
        node.object->property_names, container.name);
    if (found == node.object->property_names.end()) {
      throw std::invalid_argument{
          "class randomize container '" + container.name
          + "' is not a property of its object"};
    }
    randomized_container[c] = node.selected.contains(static_cast<std::size_t>(
        std::distance(node.object->property_names.begin(), found)));
  }
  // Arrays and queues of class handles take part through their element
  // count `name[]` and, when random and resizable, a solved `name.size()`;
  // an element past the solved size leaves the call together with the
  // objects reached from it (IEEE 1800-2017 18.4, 18.5.8.1, 18.5.9).
  struct HandleContainer {
    std::size_t node{};
    std::size_t index{};
    std::string name;
    std::size_t count{};
    bool random{};
    bool resizable{};
    /// The containers of the property, outermost first.
    std::vector<HandleLevel> levels;
  };
  std::vector<HandleContainer> handle_containers;
  for (std::size_t n = 0; n < nodes.size(); ++n) {
    const auto& node = nodes[n];
    for (std::size_t index = 0; index < node.object->properties.size();
         ++index) {
      const auto& property = node.object->properties[index];
      auto layout = handle_layout(property);
      if (!layout) continue;
      const auto kind = property.handle_container
          ? property.handle_container->kind()
          : SystemVerilogClassContainerKind::UnpackedAggregate;
      const bool rand = property.random_state
          && property.random_state->enabled
          && property.random_state->kind == SystemVerilogClassRandomKind::Rand;
      handle_containers.push_back({n, index, node.object->property_names[index],
          layout->levels.front().size,
          rand && node.selected.contains(index),
          kind == SystemVerilogClassContainerKind::DynamicArray
              || kind == SystemVerilogClassContainerKind::Queue,
          std::move(layout->levels)});
    }
  }
  std::vector<std::set<std::size_t>> base_selected;
  base_selected.reserve(nodes.size());
  for (const auto& node : nodes) base_selected.push_back(node.selected);
  const auto sized_handles = [](const HandleContainer& held) {
    return held.random && held.resizable;
  };
  // The objects of elements at or past `sizes[h]` leave the solve.
  const auto activate = [&](const std::vector<std::size_t>& sizes) {
    for (std::size_t n = 1; n < nodes.size(); ++n) {
      bool active = true;
      for (std::size_t h = 0; active && h < handle_containers.size(); ++h) {
        const auto& held = handle_containers[h];
        const auto prefix = nodes[held.node].path + held.name + "[";
        if (!nodes[n].path.starts_with(prefix)) continue;
        const auto close = nodes[n].path.find(']', prefix.size());
        std::size_t element{};
        const auto parsed = std::from_chars(
            nodes[n].path.data() + prefix.size(),
            nodes[n].path.data() + close, element);
        if (parsed.ec == std::errc{} && element >= sizes[h]) active = false;
      }
      nodes[n].active = active;
      nodes[n].selected = active ? base_selected[n] : std::set<std::size_t>{};
    }
  };
  const auto synthetic = [&](const std::size_t n, std::string name,
                             SystemVerilogClassPropertyValue value,
                             const bool chosen) {
    auto& node = nodes[n];
    const auto index = node.object->properties.size();
    node.object->property_names.push_back(std::move(name));
    node.object->properties.push_back(std::move(value));
    if (chosen) node.selected.insert(index);
    return index;
  };
  const auto count_property = [](const std::size_t count, const bool random) {
    SystemVerilogClassPropertyValue value;
    value.kind = SystemVerilogClassPropertyKind::Bit2;
    value.packed = PackedLogic4::from_aval_bval(32, count, 0);
    value.random_state = SystemVerilogClassRandomState{
        random ? SystemVerilogClassRandomKind::Rand
               : SystemVerilogClassRandomKind::None,
        32, true, "int", true, 0, 0, 0, 0, {}};
    return value;
  };
  // The name of element `index` of container `c` in the solve.
  const auto element_name = [&](const std::size_t c, const std::size_t index) {
    const auto& container = containers[c];
    return container.element_paths.empty()
        ? container.name + "[" + std::to_string(index) + "]"
        : container.name + container.element_paths.at(index);
  };
  const auto materialize = [&](const std::size_t c, std::size_t count) {
    const auto& container = containers[c];
    const bool chosen = randomized_container[c];
    if (!container.element_paths.empty()) count = container.values.size();
    for (std::size_t index = 0; index < count; ++index) {
      const bool profiled = index < container.element_profiles.size();
      auto element = profiled
          ? container.element_profiles[index] : container.element;
      if (index < container.values.size()) {
        element.packed = container.values[index];
      } else {
        element.packed = PackedLogic4(
            element.packed.width(), Logic4::zero);
      }
      const bool random = chosen
          && (!profiled
              || (element.random_state
                  && element.random_state->kind
                      != SystemVerilogClassRandomKind::None));
      if (element.random_state) {
        if (!profiled || !random) {
          element.random_state->kind = random
              ? SystemVerilogClassRandomKind::Rand
              : SystemVerilogClassRandomKind::None;
        }
        element.random_state->enabled = true;
      }
      (void)synthetic(container.node, element_name(c, index),
          std::move(element), random);
    }
    if (container.element_paths.empty()) {
      (void)synthetic(container.node, container.name + "[]",
          count_property(count, false), false);
      return;
    }
    // Each container of a nested property has its element count.
    for (const auto& shape : container.shapes) {
      (void)synthetic(container.node, container.name + shape.path + "[]",
          count_property(shape.size, false), false);
    }
  };
  // Phase one gives each random resizable container a size; phase two
  // materializes every container's elements.
  // Each container of a property of handles has its element count `[]`.
  const auto add_levels = [&](const HandleContainer& held,
                              const std::size_t outer) {
    for (std::size_t level = 0; level < held.levels.size(); ++level) {
      (void)synthetic(held.node, held.name + held.levels[level].path + "[]",
          count_property(level == 0U ? outer : held.levels[level].size, false),
          false);
    }
  };
  const auto add_handle_counts = [&](const std::vector<std::size_t>& sizes) {
    for (std::size_t h = 0; h < handle_containers.size(); ++h) {
      add_levels(handle_containers[h], sizes[h]);
    }
  };
  const auto add_sizes = [&] {
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      nodes[n].active = true;
      nodes[n].selected = base_selected[n];
    }
    for (const auto& held : handle_containers) {
      if (sized_handles(held)) {
        const auto index = synthetic(held.node, held.name + ".size()",
            count_property(held.count, true), true);
        preserved.emplace(held.node, index);
      } else {
        add_levels(held, held.count);
      }
    }
    for (std::size_t c = 0; c < containers.size(); ++c) {
      const auto& container = containers[c];
      if (container.resizable && randomized_container[c]
          && (container.element_paths.empty()
              || !container.member_paths.empty())) {
        const auto index = synthetic(container.node,
            container.name + ".size()",
            count_property(container.values.size(), true), true);
        preserved.emplace(container.node, index);
      } else {
        materialize(c, container.values.size());
      }
    }
  };

  struct SelectedNodeProperty {
    std::size_t node{};
    SelectedProperty property;
  };
  struct Outcome {
    SystemVerilogClassRandomizeResult result;
    std::vector<SelectedNodeProperty> selected;
    std::vector<PackedLogic4> values;
  };
  const auto solve = [&]() -> Outcome {
    // A random property that no constraint references takes a uniformly
    // chosen value (IEEE 1800-2017 18.5); a wide one is drawn directly
    // instead of enumerating its complete domain.
    std::set<Key> direct_indices;
    std::set<Key> direct_randc;
    std::map<Key, std::vector<PackedLogic4>> bounded;
    std::vector<Key> wide;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      const auto& node = nodes[n];
      for (const auto index : node.selected) {
        const auto& property = node.object->properties[index];
        const auto width = property.packed.width();
        if (property.packed.empty() || property.packed.is_logic9()
            || !property.random_state
            || (property.random_state->kind
                    == SystemVerilogClassRandomKind::Randc
                && width > 64U)
            || width <= 16U
            || has_exact_domain(
                *node.domains, node.object->property_names[index])) {
          continue;
        }
        wide.emplace_back(n, index);
      }
    }
    if (!wide.empty()) {
      try {
        SystemVerilogConstraintSolver probe;
        SystemVerilogClassRandomizeVariables probe_variables;
        std::map<SystemVerilogConstraintVariableId, Key> owners;
        for (std::size_t n = 0; n < nodes.size(); ++n) {
          const auto& node = nodes[n];
          for (std::size_t index = 0;
               index < node.object->properties.size(); ++index) {
            const auto& property = node.object->properties[index];
            if (property.packed.empty() || property.packed.is_logic9()) {
              continue;
            }
            SystemVerilogConstraintVariable variable;
            variable.canonical_identity = identity_of(node, index);
            variable.profile = profile(property);
            if (has_exact_domain(
                    *node.domains, node.object->property_names[index])) {
              // An enumerated property's constraints use the enumeration
              // profile (IEEE 1800-2017 18.4).
              variable.profile.kind
                  = SystemVerilogConstraintDomainKind::Enumeration;
            }
            variable.domain.push_back(property.packed);
            const auto id = probe.add_variable(std::move(variable));
            probe_variables.emplace(identity_of(node, index), id);
            owners.emplace(id, Key{n, index});
          }
        }
        configure(probe, probe_variables);
        std::set<Key> referenced;
        for (const auto& clause : probe.clauses()) {
          for (const auto id : clause.variables) {
            if (const auto owner = owners.find(id); owner != owners.end()) {
              referenced.insert(owner->second);
            }
          }
        }
        for (const auto& [id, owner] : owners) {
          if (!probe.distribution_entries(id).empty()) {
            referenced.insert(owner);
          }
        }
        // A wide variable whose bounded domain is a single value fixes the
        // variables compared with it in later passes.
        std::map<SystemVerilogConstraintVariableId, PackedLogic4> derived;
        // A bounded wide variable's interval bounds the variables compared
        // with it in later passes (`x < a + 5`, IEEE 1800-2017 18.5).
        SystemVerilogConstraintValueIntervals intervals;
        std::set<SystemVerilogConstraintVariableId> wide_ids;
        for (const auto& key : wide) {
          wide_ids.insert(probe_variables.at(
              identity_of(nodes[key.first], key.second)));
        }
        const bool coupled = std::ranges::any_of(
            probe.clauses(), [&](const auto& clause) {
              return std::ranges::count_if(clause.variables,
                         [&](const auto id) { return wide_ids.contains(id); })
                  > 1;
            });
        const auto passes = std::min<std::size_t>(64U, wide.size() + 1U);
        for (std::size_t pass = 0U; pass < passes; ++pass) {
        const auto derived_before = derived.size();
        bool narrowed{};
        for (const auto& key : wide) {
          const bool randc = nodes[key.first].object->properties[key.second]
                                 .random_state->kind
              == SystemVerilogClassRandomKind::Randc;
          if (!referenced.contains(key)) {
            if (preserved.contains(key)) {
              nodes[key.first].selected.erase(key.second);
            } else if (randc) {
              direct_randc.insert(key);
            } else {
              direct_indices.insert(key);
            }
            continue;
          }
          // A constrained wide randc keeps the exact-domain cycle.
          if (randc) continue;
          const auto& node = nodes[key.first];
          // A constrained wide property draws from the values its hard
          // clauses' comparisons with constants allow, and from the ranges
          // of its distributions (IEEE 1800-2017 18.5.4).
          const auto id = probe_variables.at(identity_of(node, key.second));
          auto stream = heap.random_stream(node.handle,
              request.call_identity + "::"
                  + node.object->property_names[key.second] + "::$bounded");
          const auto& profile = probe.variables()[id].profile;
          // State variables hold their current values during the call.
          std::map<SystemVerilogConstraintVariableId, PackedLogic4> fixed;
          for (const auto& [fixed_id, owner] : owners) {
            if (!nodes[owner.first].selected.contains(owner.second)) {
              fixed.emplace(fixed_id,
                  nodes[owner.first].object->properties[owner.second].packed);
            }
          }
          for (const auto& [derived_id, value] : derived) {
            if (derived_id != id) fixed.emplace(derived_id, value);
          }
          if (profile.width > 64U) {
            bounded.insert_or_assign(key,
                systemverilog_constraint_wide_variable_domain(
                    probe.clauses(), id, profile,
                    [&stream] { return stream.next_u64(); }, fixed));
            continue;
          }
          auto domain = systemverilog_constraint_variable_domain_with_fixed(
              probe.clauses(), id, profile,
              [&stream] { return stream.next_u64(); }, fixed, &intervals);
          if (const auto interval
              = systemverilog_constraint_value_interval(domain, profile)) {
            const auto known = intervals.find(id);
            if (known == intervals.end() || !(known->second == *interval)) {
              intervals.insert_or_assign(id, *interval);
              narrowed = true;
            }
          }
          // Ranges are walked in the variable's order: a signed value's
          // key inverts its sign bit.
          const auto mask = profile.width >= 64U
              ? ~std::uint64_t{} : (std::uint64_t{1} << profile.width) - 1U;
          const auto sign = profile.signed_value
              ? std::uint64_t{1} << (profile.width - 1U) : std::uint64_t{};
          for (const auto& entry : probe.distribution_entries(id)) {
            const auto low = (entry.low.low_word().aval & mask) ^ sign;
            const auto high = (entry.high.low_word().aval & mask) ^ sign;
            if (high < low) continue;
            const auto span = high - low;
            const auto value_of = [&](const std::uint64_t key) {
              return PackedLogic4::from_aval_bval(
                  profile.width, (key ^ sign) & mask, 0U);
            };
            if (span < 64U) {
              for (std::uint64_t value = 0U; value <= span; ++value) {
                domain.push_back(value_of(low + value));
              }
            } else {
              domain.push_back(value_of(low));
              domain.push_back(value_of(high));
              for (std::size_t sample = 0U; sample < 32U; ++sample) {
                domain.push_back(value_of(
                    low + stream.next_u64() % (span + 1U)));
              }
            }
          }
          // A sampled domain also offers the values of a simple bit
          // structure, which counting and masking constraints select.
          if (domain.size() > 64U && !preserved.contains(key)) {
            domain.push_back(PackedLogic4::from_aval_bval(profile.width, 0U, 0U));
            domain.push_back(
                PackedLogic4::from_aval_bval(profile.width, mask, 0U));
            for (std::size_t bit = 0; bit < profile.width; ++bit) {
              const auto single = std::uint64_t{1} << bit;
              domain.push_back(
                  PackedLogic4::from_aval_bval(profile.width, single, 0U));
              domain.push_back(PackedLogic4::from_aval_bval(
                  profile.width, (single - 1U) & mask, 0U));
            }
          }
          // The domain is ordered, so its first and last values bound the
          // implicit uniform distribution, and lists each value once.
          std::ranges::sort(domain, [&](const auto& left, const auto& right) {
            const auto a = left.low_word();
            const auto b = right.low_word();
            const auto left_key = a.aval ^ sign;
            const auto right_key = b.aval ^ sign;
            return left_key != right_key ? left_key < right_key
                                         : a.bval < b.bval;
          });
          domain.erase(std::ranges::unique(domain).begin(), domain.end());
          std::vector<PackedLogic4> unique = std::move(domain);
          if (preserved.contains(key)) {
            // A size is not negative; a size bounded only from below takes
            // one of the smallest allowed sizes.
            const bool sampled = unique.size() > 64U;
            std::vector<std::uint64_t> sizes;
            bool below_zero{};
            for (const auto& value : unique) {
              const auto word = value.low_word().aval;
              if (value.low_word().bval != 0U) continue;
              if (word < (UINT64_C(1) << 31U)) {
                sizes.push_back(word);
              } else {
                below_zero = true;
              }
            }
            std::ranges::sort(sizes);
            unique.clear();
            if (!sizes.empty()) {
              // A range reaching below zero starts the sizes at zero.
              const auto lowest = sampled && below_zero
                  ? std::uint64_t{} : sizes.front();
              if (sampled) {
                for (std::uint64_t size = lowest; size < lowest + 32U; ++size) {
                  sizes.push_back(size);
                }
                std::ranges::sort(sizes);
              }
              for (const auto size : sizes) {
                if (sampled && size > lowest + 256U) break;
                const auto value = PackedLogic4::from_aval_bval(32, size, 0U);
                if (std::ranges::find(unique, value) == unique.end()) {
                  unique.push_back(value);
                }
              }
            }
          }
          if (unique.size() == 1U) derived.insert_or_assign(id, unique.front());
          bounded.insert_or_assign(key, std::move(unique));
        }
        if (derived.size() == derived_before && !(coupled && narrowed)) break;
        }
        // Wide variables a hard equality joins (`x == y`) share their
        // sampled values, so the search can meet on any sampled value
        // rather than only on the values both samples happen to hold
        // (IEEE 1800-2017 18.5, 18.5.12).
        using Operator = SystemVerilogConstraintExpressionOperator;
        std::map<SystemVerilogConstraintVariableId, Key> sampled_keys;
        for (const auto& key : wide) {
          if (bounded.contains(key)) {
            sampled_keys.emplace(probe_variables.at(
                identity_of(nodes[key.first], key.second)), key);
          }
        }
        for (const auto& clause : probe.clauses()) {
          if (clause.soft || !clause.expression) continue;
          const auto& graph = *clause.expression;
          if (graph.root >= graph.nodes.size()) continue;
          const auto& root = graph.nodes[graph.root];
          if ((root.operation != Operator::Equal
                  && root.operation != Operator::CaseEqual)
              || root.operands.size() != 2U) {
            continue;
          }
          const auto& left = graph.nodes[root.operands[0]];
          const auto& right = graph.nodes[root.operands[1]];
          if (left.operation != Operator::Variable
              || right.operation != Operator::Variable) {
            continue;
          }
          const auto first = sampled_keys.find(left.variable);
          const auto second = sampled_keys.find(right.variable);
          // A size's domain and a single derived value stay as narrowed.
          if (first == sampled_keys.end() || second == sampled_keys.end()
              || first->second == second->second
              || preserved.contains(first->second)
              || preserved.contains(second->second)) {
            continue;
          }
          auto& one = bounded.at(first->second);
          auto& other = bounded.at(second->second);
          const auto& profile = probe.variables()[left.variable].profile;
          if (one.size() <= 1U || other.size() <= 1U || profile.width > 64U
              || one.front().width() != other.front().width()
              || probe.variables()[right.variable].profile.signed_value
                  != profile.signed_value) {
            continue;
          }
          const auto sign = profile.signed_value
              ? std::uint64_t{1} << (profile.width - 1U) : std::uint64_t{};
          const auto order = [&](const PackedLogic4& value) {
            return value.low_word().aval ^ sign;
          };
          std::vector<PackedLogic4> merged = one;
          merged.insert(merged.end(), other.begin(), other.end());
          std::ranges::sort(merged, {}, order);
          merged.erase(std::ranges::unique(merged).begin(), merged.end());
          one = merged;
          other = std::move(merged);
        }
      } catch (const std::exception&) {
        direct_indices.clear();
        direct_randc.clear();
        bounded.clear();
        for (const auto& key : preserved) {
          nodes[key.first].selected.erase(key.second);
        }
      }
    }

    SystemVerilogConstraintSolver solver{request.limits};
    SystemVerilogClassRandomizeVariables variables;
    std::vector<SelectedNodeProperty> selected;
    std::size_t domain_values{};
    const auto finished = [&](const SystemVerilogConstraintSolveResult& solved) {
      return SystemVerilogClassRandomizeResult{
          solved.status,
          solved.exhausted_resource,
          solved.search_steps,
          solved.clause_evaluations};
    };
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      const auto& node = nodes[n];
      for (std::size_t index = 0; index < node.object->properties.size();
           ++index) {
        const auto& property = node.object->properties[index];
        if (property.packed.empty() || property.packed.is_logic9()) continue;
        const auto& name = node.object->property_names[index];
        const auto identity = identity_of(node, index);
        const bool chosen = node.selected.contains(index);
        SystemVerilogConstraintVariable variable;
        variable.canonical_identity = identity;
        variable.profile = profile(property);
        if (has_exact_domain(*node.domains, name)) {
          variable.profile.kind = SystemVerilogConstraintDomainKind::Enumeration;
        }
        std::vector<PackedLogic4> base_domain;
        std::optional<SystemVerilogClassRandomState> staged_randc;
        const bool wide_randc = chosen && direct_randc.contains(Key{n, index});
        if (wide_randc) {
          staged_randc = *property.random_state;
          const auto width = property.packed.width();
          const auto signature = wide_randc_signature(width);
          if (staged_randc->randc_domain_signature != signature) {
            staged_randc->randc_domain_signature = signature;
            staged_randc->randc_cycle = 0;
            staged_randc->randc_used_values.clear();
            staged_randc->randc_position = 0;
          }
          if (width < 64U
              && staged_randc->randc_position
                  >= (std::uint64_t{1} << width)) {
            if (staged_randc->randc_cycle
                == std::numeric_limits<std::uint64_t>::max()) {
              throw std::overflow_error{"randc cycle ordinal exhausted"};
            }
            ++staged_randc->randc_cycle;
            staged_randc->randc_position = 0;
          }
          const auto drawn = wide_randc_value(staged_randc->randc_position,
              width,
              mixed(staged_randc->stream_seed ^ signature
                  ^ mixed(staged_randc->randc_cycle)));
          base_domain.push_back(
              PackedLogic4::from_aval_bval(width, drawn, 0U));
          variable.domain = base_domain;
        } else if (chosen) {
          const auto available = request.limits.maximum_domain_values
                  >= domain_values
              ? request.limits.maximum_domain_values - domain_values
              : 0;
          if (direct_indices.contains(Key{n, index})) {
            auto stream = heap.random_stream(
                node.handle, request.call_identity + "::" + name);
            base_domain.push_back(
                random_packed(stream, property.packed.width()));
          } else if (const auto found = bounded.find(Key{n, index});
                     found != bounded.end()) {
            if (found->second.empty()) {
              return {{SystemVerilogConstraintSolveStatus::Unsatisfiable,
                  SystemVerilogConstraintResource::None, 0, 0}, {}, {}};
            }
            base_domain = found->second;
          } else {
            base_domain = exact_domain(
                *node.domains, name, property, available);
          }
          if (property.random_state->kind
              == SystemVerilogClassRandomKind::Randc) {
            staged_randc = *property.random_state;
            const auto signature = domain_signature(base_domain);
            if (staged_randc->randc_domain_signature != signature) {
              staged_randc->randc_domain_signature = signature;
              staged_randc->randc_cycle = 0;
              staged_randc->randc_used_values.clear();
            }
            if (staged_randc->randc_used_values.size()
                == base_domain.size()) {
              if (staged_randc->randc_cycle
                  == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error{"randc cycle ordinal exhausted"};
              }
              ++staged_randc->randc_cycle;
              staged_randc->randc_used_values.clear();
            }
            variable.domain = randc_domain(base_domain, *staged_randc);
          } else {
            variable.domain = base_domain;
          }
        } else {
          variable.domain.push_back(property.packed);
        }
        domain_values += variable.domain.size();
        const auto id = solver.add_variable(std::move(variable));
        variables.emplace(identity, id);
        if (chosen) {
          const auto& retained = solver.variables()[id];
          if (!staged_randc) {
            const auto [lowest, highest] = domain_bounds(
                retained.domain, retained.profile.signed_value);
            solver.add_distribution({
                retained.canonical_identity + "::$randomize",
                id,
                {{lowest, highest, 1,
                  SystemVerilogConstraintDistributionWeight::PerValue}}});
          }
          selected.push_back({n,
              {index, id, std::move(base_domain), std::move(staged_randc),
                  false, wide_randc}});
        }
      }
    }
    configure(solver, variables);
    const auto selection = request.selection
            == SystemVerilogClassRandomizeSelection::NoProperties
        ? std::uint64_t{}
        : heap.random_stream(handle, request.call_identity).next_u64();
    auto solved = solver.solve(selection);
    if (solved.status == SystemVerilogConstraintSolveStatus::Unsatisfiable
        && std::ranges::any_of(selected, [](const auto& item) {
          return item.property.randc_state
              && !item.property.randc_state->randc_used_values.empty();
        })) {
      for (auto& item : selected) {
        auto& entry = item.property;
        if (!entry.randc_state || entry.wide_randc) continue;
        if (entry.randc_state->randc_cycle
            == std::numeric_limits<std::uint64_t>::max()) {
          throw std::overflow_error{"randc cycle ordinal exhausted"};
        }
        ++entry.randc_state->randc_cycle;
        entry.randc_state->randc_used_values.clear();
        solver.replace_domain(
            entry.variable,
            randc_domain(entry.base_domain, *entry.randc_state));
      }
      solved = solver.solve(selection);
    }
    auto result = finished(solved);
    return {result, std::move(selected), std::move(solved.values)};
  };

  // A resizable array of structures takes its solved size: its elements
  // are rebuilt from the call's starting elements, a new element taking
  // its members' initial values (IEEE 1800-2017 7.5.1, 18.4).
  struct StructureElements {
    std::vector<std::string> paths;
    std::vector<PackedLogic4> values;
    std::vector<SystemVerilogClassPropertyValue> profiles;
  };
  std::vector<StructureElements> structure_elements(containers.size());
  for (std::size_t c = 0; c < containers.size(); ++c) {
    if (!containers[c].member_paths.empty()) {
      structure_elements[c] = {containers[c].element_paths,
          containers[c].values, containers[c].element_profiles};
    }
  }
  const auto resize_structures = [&](const std::size_t c,
                                     const std::size_t count) {
    auto& container = containers[c];
    const auto& start = structure_elements[c];
    const auto members = container.member_paths.size();
    container.element_paths.clear();
    container.values.clear();
    container.element_profiles.clear();
    for (std::size_t index = 0; index < count; ++index) {
      for (std::size_t member = 0; member < members; ++member) {
        const auto at = index * members + member;
        container.element_paths.push_back("[" + std::to_string(index) + "]"
            + container.member_paths[member]);
        if (at < start.values.size()) {
          container.values.push_back(start.values[at]);
          container.element_profiles.push_back(start.profiles[at]);
        } else {
          container.values.push_back(
              container.member_profiles[member].packed);
          container.element_profiles.push_back(
              container.member_profiles[member]);
        }
      }
    }
    if (!container.shapes.empty()) container.shapes.front().size = count;
  };

  try {
    const bool sized = std::ranges::any_of(
        std::views::iota(std::size_t{}, containers.size()),
        [&](const std::size_t c) {
          return containers[c].resizable && randomized_container[c]
              && (containers[c].element_paths.empty()
                  || !containers[c].member_paths.empty());
        }) || std::ranges::any_of(handle_containers, sized_handles);
    std::vector<std::size_t> sizes(containers.size());
    for (std::size_t c = 0; c < containers.size(); ++c) {
      sizes[c] = containers[c].values.size();
    }
    std::vector<std::size_t> handle_sizes(handle_containers.size());
    for (std::size_t h = 0; h < handle_containers.size(); ++h) {
      handle_sizes[h] = handle_containers[h].count;
    }
    Outcome outcome;
    // A size that leaves the elements' constraints unsatisfiable is
    // replaced by another (IEEE 1800-2017 18.5.8.1).
    constexpr std::size_t size_attempts = 8U;
    for (std::size_t attempt = 0; attempt < size_attempts; ++attempt) {
      if (sized) {
        add_sizes();
        auto sizing = solve();
        if (sizing.result.status
            != SystemVerilogConstraintSolveStatus::Satisfied) {
          return sizing.result;
        }
        for (std::size_t c = 0; c < containers.size(); ++c) {
          if (!containers[c].resizable || !randomized_container[c]
              || (!containers[c].element_paths.empty()
                  && containers[c].member_paths.empty())) {
            continue;
          }
          auto& node = nodes[containers[c].node];
          const auto name = containers[c].name + ".size()";
          const auto found = std::ranges::find(
              node.object->property_names, name);
          const auto index = static_cast<std::size_t>(
              std::distance(node.object->property_names.begin(), found));
          const auto chosen = std::ranges::find_if(
              sizing.selected, [&](const auto& item) {
                return item.node == containers[c].node
                    && item.property.property_index == index;
              });
          if (chosen == sizing.selected.end()) continue;
          const auto& value = sizing.values.at(chosen->property.variable);
          sizes[c] = static_cast<std::size_t>(value.low_word().aval);
          if (!containers[c].member_paths.empty()) {
            resize_structures(c, sizes[c]);
            sizes[c] = containers[c].values.size();
          }
        }
        for (std::size_t h = 0; h < handle_containers.size(); ++h) {
          if (!sized_handles(handle_containers[h])) continue;
          const auto& node = nodes[handle_containers[h].node];
          const auto found = std::ranges::find(node.object->property_names,
              handle_containers[h].name + ".size()");
          const auto index = static_cast<std::size_t>(
              std::distance(node.object->property_names.begin(), found));
          const auto chosen = std::ranges::find_if(
              sizing.selected, [&](const auto& item) {
                return item.node == handle_containers[h].node
                    && item.property.property_index == index;
              });
          if (chosen == sizing.selected.end()) continue;
          handle_sizes[h] = static_cast<std::size_t>(
              sizing.values.at(chosen->property.variable).low_word().aval);
        }
        strip();
      }
      activate(handle_sizes);
      add_handle_counts(handle_sizes);
      for (std::size_t c = 0; c < containers.size(); ++c) {
        materialize(c, sizes[c]);
      }
      outcome = solve();
      if (!sized || outcome.result.status
              != SystemVerilogConstraintSolveStatus::Unsatisfiable) {
        break;
      }
      strip();
    }
    if (outcome.result.status
        != SystemVerilogConstraintSolveStatus::Satisfied) {
      return outcome.result;
    }
    auto& solved_values = outcome.values;
    for (auto& item : outcome.selected) {
      auto& entry = item.property;
      const auto& property
          = nodes[item.node].object->properties[entry.property_index];
      if (entry.variable >= solved_values.size()
          || solved_values[entry.variable].width() != property.packed.width()
          || solved_values[entry.variable].is_logic9()) {
        throw std::logic_error{
            "class randomize solver returned an invalid assignment"};
      }
      if (property.kind == SystemVerilogClassPropertyKind::Bit2
          && std::ranges::any_of(
              solved_values[entry.variable].bval_words(),
              [](const auto word) { return word != 0; })) {
        throw std::logic_error{
            "class randomize solver returned an unknown two-state assignment"};
      }
      if (!property.random_state) {
        throw std::logic_error{
            "class randomize selected property lost its random state"};
      }
      if (property.random_state->revision
          == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{
            "class random property revision exhausted"};
      }
      if (entry.randc_state && entry.wide_randc) {
        ++entry.randc_state->randc_position;
        entry.randc_state->revision = property.random_state->revision + 1U;
      } else if (entry.randc_state) {
        const auto selected_value = std::ranges::find(
            entry.base_domain, solved_values[entry.variable]);
        if (selected_value == entry.base_domain.end()) {
          throw std::logic_error{
              "randc solver assignment is outside the exact domain"};
        }
        entry.randc_state->randc_used_values.push_back(
            static_cast<std::uint64_t>(std::distance(
                entry.base_domain.begin(), selected_value)));
        entry.randc_state->revision = property.random_state->revision + 1U;
      }
    }
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      std::vector<SystemVerilogClassRandcStateUpdate> randc_updates;
      for (auto& item : outcome.selected) {
        if (item.node == n && item.property.randc_state) {
          randc_updates.push_back({
              item.property.property_index,
              std::move(*item.property.randc_state)});
          item.property.randc_state.reset();
          item.property.randc_committed = true;
        }
      }
      if (!randc_updates.empty() || n == 0U) {
        heap.commit_randc_states(nodes[n].handle, randc_updates);
      }
    }
    for (const auto& item : outcome.selected) {
      auto& property
          = nodes[item.node].object->properties[item.property.property_index];
      std::swap(property.packed, solved_values[item.property.variable]);
      if (!item.property.randc_committed) {
        ++property.random_state->revision;
      }
    }
    // The containers take their elements from the committed properties.
    for (std::size_t c = 0; c < containers.size(); ++c) {
      auto& container = containers[c];
      if (!randomized_container[c]) continue;
      const auto& node = nodes[container.node];
      std::vector<PackedLogic4> values;
      values.reserve(sizes[c]);
      for (std::size_t index = 0; index < sizes[c]; ++index) {
        const auto name = element_name(c, index);
        const auto found = std::ranges::find(
            node.object->property_names, name);
        if (found == node.object->property_names.end()) {
          throw std::logic_error{
              "class randomize container element was not materialized"};
        }
        values.push_back(node.object->properties[static_cast<std::size_t>(
            std::distance(node.object->property_names.begin(), found))]
            .packed);
      }
      container.values = std::move(values);
      container.committed = true;
    }
    // A resized array or queue of handles keeps its leading elements; new
    // elements are null (IEEE 1800-2017 7.5.1, 18.4).
    for (std::size_t h = 0; h < handle_containers.size(); ++h) {
      const auto& held = handle_containers[h];
      if (!sized_handles(held) || handle_sizes[h] == held.count) continue;
      auto& stored = *nodes[held.node].object->properties[held.index]
          .handle_container;
      if (stored.kind() == SystemVerilogClassContainerKind::DynamicArray) {
        stored.resize(handle_sizes[h]);
        continue;
      }
      while (stored.size() > handle_sizes[h]) (void)stored.pop_back();
      while (stored.size() < handle_sizes[h]) stored.push_back(heap, 0);
    }
    return outcome.result;
  } catch (const SystemVerilogConstraintResourceError& error) {
    return exhausted(error.resource());
  } catch (const std::bad_alloc&) {
    return exhausted(SystemVerilogConstraintResource::DomainValues);
  }
}

}  // namespace fsim::runtime
