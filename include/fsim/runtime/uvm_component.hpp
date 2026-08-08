// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {

using SystemVerilogUvmRootHandle = std::uint64_t;

enum class SystemVerilogUvmComponentState : std::uint8_t {
  Active,
  Destroying,
};

struct SystemVerilogUvmComponentLimits {
  std::size_t maximum_roots{64};
  std::size_t maximum_components{65'536};
  std::size_t maximum_depth{256};
  std::size_t maximum_children{65'536};
  std::size_t maximum_name_bytes{1'024};
  std::size_t maximum_full_name_bytes{16'384};
};

struct SystemVerilogUvmComponentSnapshot {
  SystemVerilogClassHandle object{};
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle parent{};
  std::string name;
  std::string full_name;
  std::size_t depth{};
  std::uint64_t creation_order{};
  SystemVerilogUvmComponentState state{
      SystemVerilogUvmComponentState::Active};
};

/// Simulation-owned UVM component hierarchy. Root contexts isolate equal paths,
/// sibling and top-level names are unique, traversal is creation ordered, and
/// recursive teardown is leaf first and generation safe.
class SystemVerilogUvmComponentService final {
 public:
  using LifecycleHook =
      std::function<void(const SystemVerilogUvmComponentSnapshot&)>;

  SystemVerilogUvmComponentService(
      SystemVerilogClassHeap& heap,
      SystemVerilogUvmObjectService& objects,
      SystemVerilogUvmComponentLimits limits = {});
  ~SystemVerilogUvmComponentService() noexcept;

  [[nodiscard]] SystemVerilogUvmRootHandle create_root(
      std::string identity);
  [[nodiscard]] bool contains_root(
      SystemVerilogUvmRootHandle root) const noexcept;
  [[nodiscard]] std::string_view root_identity(
      SystemVerilogUvmRootHandle root) const;
  [[nodiscard]] std::vector<SystemVerilogUvmRootHandle> roots() const;

  void initialize(
      SystemVerilogClassHandle object,
      std::string name,
      SystemVerilogClassHandle parent = 0,
      SystemVerilogUvmRootHandle root = 0);
  [[nodiscard]] bool contains(
      SystemVerilogClassHandle object) const noexcept;
  [[nodiscard]] SystemVerilogUvmComponentSnapshot snapshot(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] SystemVerilogClassHandle parent(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] SystemVerilogUvmRootHandle root_of(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] std::string full_name(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] std::vector<SystemVerilogClassHandle> children(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] std::vector<SystemVerilogClassHandle> top_components(
      SystemVerilogUvmRootHandle root) const;
  [[nodiscard]] SystemVerilogClassHandle find_child(
      SystemVerilogClassHandle object,
      std::string_view name) const;
  [[nodiscard]] SystemVerilogClassHandle lookup_root(
      SystemVerilogUvmRootHandle root,
      std::string_view full_name) const;
  [[nodiscard]] SystemVerilogClassHandle lookup(
      SystemVerilogClassHandle start,
      std::string_view path) const;

  void release(SystemVerilogClassHandle object);
  void destroy_root(SystemVerilogUvmRootHandle root);
  [[nodiscard]] std::size_t component_count() const noexcept {
    return components_.size();
  }
  void set_pre_destroy_hook(LifecycleHook hook) {
    pre_destroy_hook_ = std::move(hook);
  }
  void set_post_destroy_hook(LifecycleHook hook) {
    post_destroy_hook_ = std::move(hook);
  }
  [[nodiscard]] const SystemVerilogUvmComponentLimits& limits() const
      noexcept {
    return limits_;
  }

 private:
  struct Component {
    SystemVerilogUvmComponentSnapshot value;
    std::vector<SystemVerilogClassHandle> children;
    std::map<std::string, SystemVerilogClassHandle, std::less<>>
        children_by_name;
  };
  struct Root {
    std::string identity;
    std::vector<SystemVerilogClassHandle> tops;
    std::map<std::string, SystemVerilogClassHandle, std::less<>>
        tops_by_name;
  };

  [[nodiscard]] Component& component(SystemVerilogClassHandle object);
  [[nodiscard]] const Component& component(
      SystemVerilogClassHandle object) const;
  [[nodiscard]] Root& root(SystemVerilogUvmRootHandle handle);
  [[nodiscard]] const Root& root(SystemVerilogUvmRootHandle handle) const;
  [[nodiscard]] std::vector<SystemVerilogClassHandle> postorder(
      SystemVerilogClassHandle object) const;
  void validate_name(std::string_view name) const;

  SystemVerilogClassHeap* heap_{};
  SystemVerilogUvmObjectService* objects_{};
  SystemVerilogUvmComponentLimits limits_;
  std::map<SystemVerilogUvmRootHandle, Root> roots_;
  std::map<std::string, SystemVerilogUvmRootHandle, std::less<>>
      roots_by_identity_;
  std::map<SystemVerilogClassHandle, Component> components_;
  SystemVerilogUvmRootHandle next_root_{1};
  std::uint64_t next_creation_order_{};
  LifecycleHook pre_destroy_hook_;
  LifecycleHook post_destroy_hook_;
};

}  // namespace fsim::runtime
