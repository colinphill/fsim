// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_component.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

std::vector<std::string_view> path_parts(const std::string_view path) {
  std::vector<std::string_view> result;
  std::size_t begin{};
  while (begin < path.size()) {
    const auto end = path.find('.', begin);
    const auto part = path.substr(
        begin, end == std::string_view::npos ? path.size() - begin
                                             : end - begin);
    if (part.empty()) {
      throw std::invalid_argument{"UVM component path contains an empty name"};
    }
    result.push_back(part);
    if (end == std::string_view::npos) break;
    begin = end + 1U;
  }
  return result;
}

}  // namespace

SystemVerilogUvmComponentService::SystemVerilogUvmComponentService(
    SystemVerilogClassHeap& heap,
    SystemVerilogUvmObjectService& objects,
    SystemVerilogUvmComponentLimits limits)
    : heap_(&heap), objects_(&objects), limits_(limits) {
  if (limits_.maximum_roots == 0
      || limits_.maximum_components == 0
      || limits_.maximum_depth == 0
      || limits_.maximum_children == 0
      || limits_.maximum_name_bytes == 0
      || limits_.maximum_full_name_bytes == 0) {
    throw std::invalid_argument{
        "UVM component resource limits must all be positive"};
  }
}

SystemVerilogUvmComponentService::~SystemVerilogUvmComponentService()
    noexcept {
  while (!roots_.empty()) {
    const auto root_handle = roots_.begin()->first;
    try {
      destroy_root(root_handle);
    } catch (...) {
      // Destruction hooks cannot prevent the remaining roots from being
      // reclaimed during simulation teardown.
      if (const auto found = roots_.find(root_handle);
          found != roots_.end()) {
        roots_by_identity_.erase(found->second.identity);
        roots_.erase(found);
      }
    }
  }
}

SystemVerilogUvmRootHandle
SystemVerilogUvmComponentService::create_root(std::string identity) {
  if (identity.empty()) {
    throw std::invalid_argument{"UVM root identity must not be empty"};
  }
  if (identity.size() > limits_.maximum_full_name_bytes) {
    throw std::length_error{"UVM root identity exceeds the path limit"};
  }
  if (roots_.size() >= limits_.maximum_roots) {
    throw std::length_error{"UVM root budget exceeded"};
  }
  if (roots_by_identity_.contains(identity)) {
    throw std::invalid_argument{"duplicate UVM root identity"};
  }
  if (next_root_ == 0) {
    throw std::overflow_error{"UVM root identity overflow"};
  }
  const auto handle = next_root_++;
  roots_.emplace(handle, Root{identity, {}, {}});
  try {
    roots_by_identity_.emplace(std::move(identity), handle);
  } catch (...) {
    roots_.erase(handle);
    throw;
  }
  return handle;
}

bool SystemVerilogUvmComponentService::contains_root(
    const SystemVerilogUvmRootHandle handle) const noexcept {
  return handle != 0 && roots_.contains(handle);
}

std::string_view SystemVerilogUvmComponentService::root_identity(
    const SystemVerilogUvmRootHandle handle) const {
  return root(handle).identity;
}

std::vector<SystemVerilogUvmRootHandle>
SystemVerilogUvmComponentService::roots() const {
  std::vector<SystemVerilogUvmRootHandle> result;
  result.reserve(roots_.size());
  for (const auto& [handle, value] : roots_) {
    (void)value;
    result.push_back(handle);
  }
  return result;
}

void SystemVerilogUvmComponentService::validate_name(
    const std::string_view name) const {
  if (name.empty()) {
    throw std::invalid_argument{"UVM component name must not be empty"};
  }
  if (name.size() > limits_.maximum_name_bytes) {
    throw std::length_error{"UVM component name exceeds the byte limit"};
  }
  if (name.find('.') != std::string_view::npos) {
    throw std::invalid_argument{"UVM component name must not contain '.'"};
  }
}

void SystemVerilogUvmComponentService::initialize(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogClassHandle parent_handle,
    SystemVerilogUvmRootHandle root_handle) {
  validate_name(name);
  if (object == 0 || !heap_->contains(object)
      || !objects_->contains(object)) {
    throw std::out_of_range{"UVM component object is null or stale"};
  }
  if (components_.contains(object)) {
    throw std::invalid_argument{"UVM component object is already registered"};
  }
  if (components_.size() >= limits_.maximum_components) {
    throw std::length_error{"UVM component budget exceeded"};
  }

  Component* parent_component{};
  std::size_t depth{};
  std::string full_name_value;
  if (parent_handle != 0) {
    if (parent_handle == object) {
      throw std::invalid_argument{"UVM component cannot parent itself"};
    }
    parent_component = &component(parent_handle);
    if (parent_component->value.state
        != SystemVerilogUvmComponentState::Active) {
      throw std::logic_error{"UVM component parent is being destroyed"};
    }
    if (root_handle != 0 && root_handle != parent_component->value.root) {
      throw std::invalid_argument{"UVM component parent crosses roots"};
    }
    root_handle = parent_component->value.root;
    depth = parent_component->value.depth + 1U;
    if (parent_component->children.size()
        >= limits_.maximum_children) {
      throw std::length_error{"UVM component child budget exceeded"};
    }
    if (parent_component->children_by_name.contains(name)) {
      throw std::invalid_argument{"duplicate UVM component sibling name"};
    }
    full_name_value = parent_component->value.full_name + "." + name;
  } else {
    auto& owner = root(root_handle);
    if (owner.tops.size() >= limits_.maximum_children) {
      throw std::length_error{"UVM top-level component budget exceeded"};
    }
    if (owner.tops_by_name.contains(name)) {
      throw std::invalid_argument{"duplicate top-level UVM component name"};
    }
    full_name_value = name;
  }
  if (depth >= limits_.maximum_depth) {
    throw std::length_error{"UVM component hierarchy depth exceeded"};
  }
  if (full_name_value.size() > limits_.maximum_full_name_bytes) {
    throw std::length_error{"UVM component full name exceeds the path limit"};
  }
  if (next_creation_order_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error{"UVM component creation order overflow"};
  }

  SystemVerilogUvmComponentSnapshot value{
      object, root_handle, parent_handle, name, full_name_value, depth,
      next_creation_order_++, SystemVerilogUvmComponentState::Active};
  components_.emplace(object, Component{value, {}, {}});
  try {
    if (parent_component != nullptr) {
      parent_component->children_by_name.emplace(name, object);
      try {
        parent_component->children.push_back(object);
      } catch (...) {
        parent_component->children_by_name.erase(name);
        throw;
      }
    } else {
      auto& owner = root(root_handle);
      owner.tops_by_name.emplace(name, object);
      try {
        owner.tops.push_back(object);
      } catch (...) {
        owner.tops_by_name.erase(name);
        throw;
      }
    }
    objects_->set_name(object, name);
    objects_->set_full_name(object, full_name_value);
  } catch (...) {
    if (parent_component != nullptr) {
      parent_component->children_by_name.erase(name);
      std::erase(parent_component->children, object);
    } else if (contains_root(root_handle)) {
      auto& owner = root(root_handle);
      owner.tops_by_name.erase(name);
      std::erase(owner.tops, object);
    }
    components_.erase(object);
    throw;
  }
}

bool SystemVerilogUvmComponentService::contains(
    const SystemVerilogClassHandle object) const noexcept {
  return object != 0 && heap_->contains(object)
      && objects_->contains(object) && components_.contains(object);
}

SystemVerilogUvmComponentSnapshot
SystemVerilogUvmComponentService::snapshot(
    const SystemVerilogClassHandle object) const {
  return component(object).value;
}

SystemVerilogClassHandle SystemVerilogUvmComponentService::parent(
    const SystemVerilogClassHandle object) const {
  return component(object).value.parent;
}

SystemVerilogUvmRootHandle SystemVerilogUvmComponentService::root_of(
    const SystemVerilogClassHandle object) const {
  return component(object).value.root;
}

std::string SystemVerilogUvmComponentService::full_name(
    const SystemVerilogClassHandle object) const {
  return component(object).value.full_name;
}

std::vector<SystemVerilogClassHandle>
SystemVerilogUvmComponentService::children(
    const SystemVerilogClassHandle object) const {
  return component(object).children;
}

std::vector<SystemVerilogClassHandle>
SystemVerilogUvmComponentService::top_components(
    const SystemVerilogUvmRootHandle handle) const {
  return root(handle).tops;
}

SystemVerilogClassHandle SystemVerilogUvmComponentService::find_child(
    const SystemVerilogClassHandle object,
    const std::string_view name) const {
  const auto& owner = component(object);
  const auto found = owner.children_by_name.find(name);
  return found == owner.children_by_name.end() ? 0 : found->second;
}

SystemVerilogClassHandle SystemVerilogUvmComponentService::lookup_root(
    const SystemVerilogUvmRootHandle handle,
    const std::string_view full_name_value) const {
  if (full_name_value.empty()) return 0;
  const auto parts = path_parts(full_name_value);
  const auto& owner = root(handle);
  auto found = owner.tops_by_name.find(parts.front());
  if (found == owner.tops_by_name.end()) return 0;
  auto current = found->second;
  for (const auto part : std::span{parts}.subspan(1)) {
    current = find_child(current, part);
    if (current == 0) return 0;
  }
  return current;
}

SystemVerilogClassHandle SystemVerilogUvmComponentService::lookup(
    const SystemVerilogClassHandle start,
    std::string_view path) const {
  const auto& start_component = component(start);
  if (path.empty()) return start;
  if (path.front() == '.') {
    path.remove_prefix(1);
    return lookup_root(start_component.value.root, path);
  }
  auto current = start;
  for (const auto part : path_parts(path)) {
    current = find_child(current, part);
    if (current == 0) return 0;
  }
  return current;
}

std::vector<SystemVerilogClassHandle>
SystemVerilogUvmComponentService::postorder(
    const SystemVerilogClassHandle object) const {
  (void)component(object);
  std::vector<SystemVerilogClassHandle> result;
  std::vector<std::pair<SystemVerilogClassHandle, bool>> work{
      {object, false}};
  while (!work.empty()) {
    const auto [current, visited] = work.back();
    work.pop_back();
    if (visited) {
      result.push_back(current);
      continue;
    }
    work.emplace_back(current, true);
    const auto& current_children = component(current).children;
    for (auto child = current_children.rbegin();
         child != current_children.rend(); ++child) {
      work.emplace_back(*child, false);
    }
    if (work.size() > limits_.maximum_components + 1U) {
      throw std::length_error{"UVM component teardown work exceeded"};
    }
  }
  return result;
}

void SystemVerilogUvmComponentService::release(
    const SystemVerilogClassHandle object) {
  const auto order = postorder(object);
  std::exception_ptr first_error;
  for (const auto current : order) {
    auto& owned = component(current);
    owned.value.state = SystemVerilogUvmComponentState::Destroying;
    const auto value = owned.value;
    if (pre_destroy_hook_) {
      try {
        pre_destroy_hook_(value);
      } catch (...) {
        if (!first_error) first_error = std::current_exception();
      }
    }
    if (value.parent != 0) {
      auto& parent_value = component(value.parent);
      parent_value.children_by_name.erase(value.name);
      std::erase(parent_value.children, current);
    } else {
      auto& root_value = root(value.root);
      root_value.tops_by_name.erase(value.name);
      std::erase(root_value.tops, current);
    }
    components_.erase(current);
    objects_->erase(current);
    if (!heap_->release(current) && !first_error) {
      first_error = std::make_exception_ptr(
          std::logic_error{"UVM component heap release failed"});
    }
    if (post_destroy_hook_) {
      try {
        post_destroy_hook_(value);
      } catch (...) {
        if (!first_error) first_error = std::current_exception();
      }
    }
  }
  if (first_error) std::rethrow_exception(first_error);
}

void SystemVerilogUvmComponentService::destroy_root(
    const SystemVerilogUvmRootHandle handle) {
  auto tops = root(handle).tops;
  std::exception_ptr first_error;
  for (const auto top : tops) {
    try {
      release(top);
    } catch (...) {
      if (!first_error) first_error = std::current_exception();
    }
  }
  auto found = roots_.find(handle);
  if (found != roots_.end()) {
    roots_by_identity_.erase(found->second.identity);
    roots_.erase(found);
  }
  if (first_error) std::rethrow_exception(first_error);
}

SystemVerilogUvmComponentService::Component&
SystemVerilogUvmComponentService::component(
    const SystemVerilogClassHandle object) {
  auto found = components_.find(object);
  if (found == components_.end() || !heap_->contains(object)
      || !objects_->contains(object)) {
    throw std::out_of_range{"null or stale UVM component handle"};
  }
  return found->second;
}

const SystemVerilogUvmComponentService::Component&
SystemVerilogUvmComponentService::component(
    const SystemVerilogClassHandle object) const {
  auto found = components_.find(object);
  if (found == components_.end() || !heap_->contains(object)
      || !objects_->contains(object)) {
    throw std::out_of_range{"null or stale UVM component handle"};
  }
  return found->second;
}

SystemVerilogUvmComponentService::Root&
SystemVerilogUvmComponentService::root(
    const SystemVerilogUvmRootHandle handle) {
  auto found = roots_.find(handle);
  if (handle == 0 || found == roots_.end()) {
    throw std::out_of_range{"null or stale UVM root handle"};
  }
  return found->second;
}

const SystemVerilogUvmComponentService::Root&
SystemVerilogUvmComponentService::root(
    const SystemVerilogUvmRootHandle handle) const {
  auto found = roots_.find(handle);
  if (handle == 0 || found == roots_.end()) {
    throw std::out_of_range{"null or stale UVM root handle"};
  }
  return found->second;
}

}  // namespace fsim::runtime
