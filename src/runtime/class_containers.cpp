// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_heap.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

SystemVerilogClassHandleContainer::SystemVerilogClassHandleContainer(
    SystemVerilogClassHandleContainerDescriptor descriptor)
    : descriptor_(std::move(descriptor)) {
  if (descriptor_.declared_element_type.empty()) {
    throw std::invalid_argument{
        "class handle container requires a declared element type"};
  }
  if (descriptor_.packed) {
    throw std::invalid_argument{
        "class handles are not legal members of packed aggregates"};
  }
  if (descriptor_.kind
      == SystemVerilogClassContainerKind::UnpackedAggregate) {
    if (descriptor_.initial_elements != 0) {
      throw std::invalid_argument{
          "aggregate class handle storage uses named members"};
    }
    if (descriptor_.aggregate_members.size()
        > descriptor_.maximum_elements) {
      throw std::length_error{
          "class handle aggregate exceeds its element budget"};
    }
    for (const auto& member : descriptor_.aggregate_members) {
      if (member.empty() || !keyed_.emplace(member, 0).second) {
        throw std::invalid_argument{
            "aggregate class handle members must be nonempty and unique"};
      }
    }
    return;
  }
  if (!descriptor_.aggregate_members.empty()) {
    throw std::invalid_argument{
        "named members require an unpacked aggregate container"};
  }
  if (descriptor_.initial_elements > descriptor_.maximum_elements) {
    throw std::length_error{
        "class handle container exceeds its element budget"};
  }
  if (descriptor_.kind == SystemVerilogClassContainerKind::AssociativeArray
      && descriptor_.initial_elements != 0) {
    throw std::invalid_argument{
        "associative class handle arrays do not have an initial size"};
  }
  sequential_.resize(descriptor_.initial_elements);
}

std::size_t SystemVerilogClassHandleContainer::size() const noexcept {
  if (descriptor_.kind
          == SystemVerilogClassContainerKind::AssociativeArray
      || descriptor_.kind
          == SystemVerilogClassContainerKind::UnpackedAggregate) {
    return keyed_.size();
  }
  return sequential_.size();
}

SystemVerilogClassHandle SystemVerilogClassHandleContainer::at(
    const std::size_t index) const {
  if (descriptor_.kind == SystemVerilogClassContainerKind::AssociativeArray
      || descriptor_.kind
          == SystemVerilogClassContainerKind::UnpackedAggregate) {
    throw std::logic_error{"indexed access requires a sequential container"};
  }
  return sequential_.at(index);
}

void SystemVerilogClassHandleContainer::set(
    SystemVerilogClassHeap& heap,
    const std::size_t index,
    const SystemVerilogClassHandle handle) {
  if (descriptor_.kind == SystemVerilogClassContainerKind::AssociativeArray
      || descriptor_.kind
          == SystemVerilogClassContainerKind::UnpackedAggregate) {
    throw std::logic_error{"indexed access requires a sequential container"};
  }
  validate(heap, handle);
  sequential_.at(index) = handle;
}

void SystemVerilogClassHandleContainer::resize(const std::size_t size) {
  if (descriptor_.kind != SystemVerilogClassContainerKind::DynamicArray) {
    throw std::logic_error{"only dynamic class handle arrays may be resized"};
  }
  if (size > descriptor_.maximum_elements) {
    throw std::length_error{
        "dynamic class handle array exceeds its element budget"};
  }
  sequential_.resize(size);
}

void SystemVerilogClassHandleContainer::push_back(
    SystemVerilogClassHeap& heap,
    const SystemVerilogClassHandle handle) {
  if (descriptor_.kind != SystemVerilogClassContainerKind::Queue) {
    throw std::logic_error{"push_back requires a class handle queue"};
  }
  if (sequential_.size() >= descriptor_.maximum_elements) {
    throw std::length_error{"class handle queue exceeds its element budget"};
  }
  validate(heap, handle);
  sequential_.push_back(handle);
}

SystemVerilogClassHandle SystemVerilogClassHandleContainer::pop_front() {
  if (descriptor_.kind != SystemVerilogClassContainerKind::Queue) {
    throw std::logic_error{"pop_front requires a class handle queue"};
  }
  if (sequential_.empty()) return 0;
  const auto result = sequential_.front();
  sequential_.erase(sequential_.begin());
  return result;
}

SystemVerilogClassHandle SystemVerilogClassHandleContainer::at(
    const std::string_view key) const {
  if (descriptor_.kind != SystemVerilogClassContainerKind::AssociativeArray
      && descriptor_.kind
          != SystemVerilogClassContainerKind::UnpackedAggregate) {
    throw std::logic_error{"keyed access requires a keyed container"};
  }
  const auto found = keyed_.find(std::string{key});
  if (found == keyed_.end()) {
    if (descriptor_.kind
        == SystemVerilogClassContainerKind::AssociativeArray) return 0;
    throw std::out_of_range{"class handle aggregate member does not exist"};
  }
  return found->second;
}

void SystemVerilogClassHandleContainer::set(
    SystemVerilogClassHeap& heap,
    std::string key,
    const SystemVerilogClassHandle handle) {
  if (descriptor_.kind != SystemVerilogClassContainerKind::AssociativeArray
      && descriptor_.kind
          != SystemVerilogClassContainerKind::UnpackedAggregate) {
    throw std::logic_error{"keyed access requires a keyed container"};
  }
  validate(heap, handle);
  const auto found = keyed_.find(key);
  if (descriptor_.kind
          == SystemVerilogClassContainerKind::UnpackedAggregate
      && found == keyed_.end()) {
    throw std::out_of_range{"class handle aggregate member does not exist"};
  }
  if (found != keyed_.end()) {
    found->second = handle;
    return;
  }
  if (keyed_.size() >= descriptor_.maximum_elements) {
    throw std::length_error{
        "associative class handle array exceeds its element budget"};
  }
  keyed_.emplace(std::move(key), handle);
}

bool SystemVerilogClassHandleContainer::erase(const std::string_view key) {
  if (descriptor_.kind != SystemVerilogClassContainerKind::AssociativeArray) {
    throw std::logic_error{"erase requires an associative class handle array"};
  }
  return keyed_.erase(std::string{key}) != 0;
}

void SystemVerilogClassHandleContainer::validate(
    SystemVerilogClassHeap& heap,
    const SystemVerilogClassHandle handle) const {
  (void)heap.checked_cast(handle, descriptor_.declared_element_type);
}

}  // namespace fsim::runtime
