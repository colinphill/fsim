// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_randomize.hpp"

#include "constraint_bounds_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <optional>
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
[[nodiscard]] bool has_exact_domain(
    const SystemVerilogClassRandomizeRequest& request,
    const std::string_view canonical_identity) {
  if (request.property_domains.contains(canonical_identity)) return true;
  const auto separator = canonical_identity.rfind("::");
  return separator != std::string_view::npos
      && request.property_domains.contains(
          canonical_identity.substr(separator + 2U));
}

[[nodiscard]] std::vector<PackedLogic4> exact_domain(
    const SystemVerilogClassRandomizeRequest& request,
    const std::string_view canonical_identity,
    const SystemVerilogClassPropertyValue& property,
    const std::size_t available_values) {
  auto found = request.property_domains.find(canonical_identity);
  if (found == request.property_domains.end()) {
    const auto separator = canonical_identity.rfind("::");
    if (separator != std::string_view::npos) {
      found = request.property_domains.find(canonical_identity.substr(
          separator + 2U));
    }
  }
  if (found == request.property_domains.end()) {
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

[[nodiscard]] SystemVerilogClassRandomizeResult exhausted(
    const SystemVerilogConstraintResource resource) {
  return {
      SystemVerilogConstraintSolveStatus::ResourceExhausted,
      resource, 0, 0};
}

}  // namespace

SystemVerilogClassRandomizeResult randomize_systemverilog_class_object(
    SystemVerilogClassHeap& heap,
    const SystemVerilogClassHandle handle,
    const SystemVerilogClassRandomizeRequest& request) {
  if (request.call_identity.empty()) {
    throw std::invalid_argument{
        "class randomize call identity must not be empty"};
  }
  auto& object = heap.object(handle);
  std::set<std::size_t> selected_indices;
  if (request.selection
          == SystemVerilogClassRandomizeSelection::NoProperties) {
    if (!request.variable_list.empty()) {
      throw std::invalid_argument{
          "class randomize checker mode cannot select properties"};
    }
  } else if (request.variable_list.empty()) {
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
      const auto& random = object.properties[index].random_state;
      if (random && random->enabled
          && random->kind != SystemVerilogClassRandomKind::None) {
        selected_indices.insert(index);
      }
    }
  } else {
    for (const auto& name : request.variable_list) {
      const auto index = property_index(object, name);
      if (!index) {
        throw std::out_of_range{
            "SystemVerilog class randomize property '" + name
            + "' does not exist"};
      }
      auto& property = object.properties[*index];
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
      if (!random) {
        throw std::invalid_argument{
            "SystemVerilog class randomize property '" + name
            + "' is not randomizable"};
      }
      if (!random->enabled) {
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

  // A random property that no constraint references takes a uniformly
  // chosen value (IEEE 1800-2017 18.5); a wide one is drawn directly
  // instead of enumerating its complete domain.
  std::set<std::size_t> direct_indices;
  std::map<std::size_t, std::vector<PackedLogic4>> bounded;
  {
    std::vector<std::size_t> wide;
    for (const auto index : selected_indices) {
      const auto& property = object.properties[index];
      const auto width = property.packed.width();
      if (property.packed.empty() || property.packed.is_logic9()
          || width > 64U || !property.random_state
          || property.random_state->kind == SystemVerilogClassRandomKind::Randc
          || width <= 16U
          || request.property_domains.contains(object.property_names[index])) {
        continue;
      }
      wide.push_back(index);
    }
    if (!wide.empty()) {
      try {
        SystemVerilogConstraintSolver probe;
        SystemVerilogClassRandomizeVariables probe_variables;
        std::map<SystemVerilogConstraintVariableId, std::size_t> owners;
        for (std::size_t index = 0; index < object.properties.size();
             ++index) {
          const auto& property = object.properties[index];
          if (property.packed.empty() || property.packed.is_logic9()) {
            continue;
          }
          SystemVerilogConstraintVariable variable;
          variable.canonical_identity = object.property_names[index];
          variable.profile = profile(property);
          if (has_exact_domain(request, object.property_names[index])) {
            // An enumerated property's constraints use the enumeration
            // profile (IEEE 1800-2017 18.4).
            variable.profile.kind = SystemVerilogConstraintDomainKind::Enumeration;
          }
          variable.domain.push_back(property.packed);
          const auto id = probe.add_variable(std::move(variable));
          probe_variables.emplace(object.property_names[index], id);
          owners.emplace(id, index);
        }
        if (request.class_constraints) {
          request.class_constraints(probe, probe_variables);
        }
        if (request.inline_constraints) {
          request.inline_constraints(probe, probe_variables);
        }
        std::set<std::size_t> referenced;
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
        for (const auto index : wide) {
          if (!referenced.contains(index)) {
            direct_indices.insert(index);
            continue;
          }
          // A constrained wide property draws from the values its hard
          // clauses' comparisons with constants allow, and from the ranges
          // of its distributions (IEEE 1800-2017 18.5.4).
          const auto id = probe_variables.at(object.property_names[index]);
          auto stream = heap.random_stream(handle,
              request.call_identity + "::" + object.property_names[index]
                  + "::$bounded");
          const auto& profile = probe.variables()[id].profile;
          // State variables hold their current values during the call.
          std::map<SystemVerilogConstraintVariableId, PackedLogic4> fixed;
          for (const auto& [fixed_id, owner] : owners) {
            if (!selected_indices.contains(owner)) {
              fixed.emplace(fixed_id, object.properties[owner].packed);
            }
          }
          auto domain = systemverilog_constraint_variable_domain_with_fixed(
              probe.clauses(), id, profile,
              [&stream] { return stream.next_u64(); }, fixed);
          for (const auto& entry : probe.distribution_entries(id)) {
            const auto low = entry.low.low_word().aval;
            const auto high = entry.high.low_word().aval;
            const auto span = high - low;
            if (span < 64U) {
              for (std::uint64_t value = 0U; value <= span; ++value) {
                domain.push_back(PackedLogic4::from_aval_bval(
                    profile.width, low + value, 0U));
              }
            } else {
              domain.push_back(entry.low);
              domain.push_back(entry.high);
              for (std::size_t sample = 0U; sample < 32U; ++sample) {
                domain.push_back(PackedLogic4::from_aval_bval(profile.width,
                    low + stream.next_u64() % (span + 1U), 0U));
              }
            }
          }
          // The domain lists each value once.
          std::vector<PackedLogic4> unique;
          unique.reserve(domain.size());
          for (auto& value : domain) {
            if (std::ranges::find(unique, value) == unique.end()) {
              unique.push_back(std::move(value));
            }
          }
          bounded.emplace(index, std::move(unique));
        }
      } catch (const std::exception&) {
        direct_indices.clear();
        bounded.clear();
      }
    }
  }

  try {
    SystemVerilogConstraintSolver solver{request.limits};
    SystemVerilogClassRandomizeVariables variables;
    std::vector<SelectedProperty> selected;
    std::size_t domain_values{};
    for (std::size_t index = 0; index < object.properties.size(); ++index) {
      const auto& property = object.properties[index];
      if (property.packed.empty() || property.packed.is_logic9()) continue;
      SystemVerilogConstraintVariable variable;
      variable.canonical_identity = object.property_names[index];
      variable.profile = profile(property);
      if (has_exact_domain(request, object.property_names[index])) {
        variable.profile.kind = SystemVerilogConstraintDomainKind::Enumeration;
      }
      std::vector<PackedLogic4> base_domain;
      std::optional<SystemVerilogClassRandomState> staged_randc;
      if (selected_indices.contains(index)) {
        const auto available = request.limits.maximum_domain_values
                >= domain_values
            ? request.limits.maximum_domain_values - domain_values
            : 0;
        if (direct_indices.contains(index)) {
          const auto width = property.packed.width();
          auto value = heap.random_stream(
                  handle,
                  request.call_identity + "::"
                      + object.property_names[index])
                           .next_u64();
          if (width < 64U) value &= (std::uint64_t{1} << width) - 1U;
          base_domain.push_back(
              PackedLogic4::from_aval_bval(width, value, 0));
        } else if (const auto found = bounded.find(index);
                   found != bounded.end()) {
          if (found->second.empty()) {
            return {SystemVerilogConstraintSolveStatus::Unsatisfiable,
                SystemVerilogConstraintResource::None, 0, 0};
          }
          base_domain = found->second;
        } else {
          base_domain = exact_domain(
              request, object.property_names[index], property, available);
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
          if (staged_randc->randc_used_values.size() == base_domain.size()) {
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
      variables.emplace(object.property_names[index], id);
      if (selected_indices.contains(index)) {
        const auto& retained = solver.variables()[id];
        if (!staged_randc) {
          solver.add_distribution({
              retained.canonical_identity + "::$randomize",
              id,
              {{retained.domain.front(), retained.domain.back(), 1,
                SystemVerilogConstraintDistributionWeight::PerValue}}});
        }
        selected.push_back({
            index, id, std::move(base_domain), std::move(staged_randc)});
      }
    }
    if (request.class_constraints) {
      request.class_constraints(solver, variables);
    }
    if (request.inline_constraints) {
      request.inline_constraints(solver, variables);
    }
    const auto selection = request.selection
            == SystemVerilogClassRandomizeSelection::NoProperties
        ? std::uint64_t{}
        : heap.random_stream(handle, request.call_identity).next_u64();
    auto solved = solver.solve(selection);
    if (solved.status == SystemVerilogConstraintSolveStatus::Unsatisfiable
        && std::ranges::any_of(selected, [](const auto& item) {
          return item.randc_state
              && !item.randc_state->randc_used_values.empty();
        })) {
      for (auto& item : selected) {
        if (!item.randc_state) continue;
        if (item.randc_state->randc_cycle
            == std::numeric_limits<std::uint64_t>::max()) {
          throw std::overflow_error{"randc cycle ordinal exhausted"};
        }
        ++item.randc_state->randc_cycle;
        item.randc_state->randc_used_values.clear();
        solver.replace_domain(
            item.variable,
            randc_domain(item.base_domain, *item.randc_state));
      }
      solved = solver.solve(selection);
    }
    if (solved.status != SystemVerilogConstraintSolveStatus::Satisfied) {
      return {
          solved.status,
          solved.exhausted_resource,
          solved.search_steps,
          solved.clause_evaluations};
    }
    for (auto& item : selected) {
      const auto& property = object.properties[item.property_index];
      if (item.variable >= solved.values.size()
          || solved.values[item.variable].width() != property.packed.width()
          || solved.values[item.variable].is_logic9()) {
        throw std::logic_error{
            "class randomize solver returned an invalid assignment"};
      }
      if (property.kind == SystemVerilogClassPropertyKind::Bit2
          && std::ranges::any_of(
              solved.values[item.variable].bval_words(),
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
      if (item.randc_state) {
        const auto selected_value = std::ranges::find(
            item.base_domain, solved.values[item.variable]);
        if (selected_value == item.base_domain.end()) {
          throw std::logic_error{
              "randc solver assignment is outside the exact domain"};
        }
        item.randc_state->randc_used_values.push_back(
            static_cast<std::uint64_t>(std::distance(
                item.base_domain.begin(), selected_value)));
        item.randc_state->revision = property.random_state->revision + 1U;
      }
    }
    std::vector<SystemVerilogClassRandcStateUpdate> randc_updates;
    randc_updates.reserve(selected.size());
    for (auto& item : selected) {
      if (item.randc_state) {
        randc_updates.push_back({
            item.property_index, std::move(*item.randc_state)});
      }
    }
    heap.commit_randc_states(handle, randc_updates);
    for (const auto& item : selected) {
      auto& property = object.properties[item.property_index];
      std::swap(property.packed, solved.values[item.variable]);
      if (!item.randc_state) {
        ++property.random_state->revision;
      }
    }
    return {
        solved.status,
        solved.exhausted_resource,
        solved.search_steps,
        solved.clause_evaluations};
  } catch (const SystemVerilogConstraintResourceError& error) {
    return exhausted(error.resource());
  } catch (const std::bad_alloc&) {
    return exhausted(SystemVerilogConstraintResource::DomainValues);
  }
}

}  // namespace fsim::runtime
