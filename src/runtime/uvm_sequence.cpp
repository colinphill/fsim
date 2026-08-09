// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-SEQ-001"};
constexpr std::string_view kForeignHandle{"FSIM-UVM-SEQ-002"};
constexpr std::string_view kInvalidType{"FSIM-UVM-SEQ-003"};
constexpr std::string_view kInvalidHierarchy{"FSIM-UVM-SEQ-004"};
constexpr std::string_view kInvalidLifecycle{"FSIM-UVM-SEQ-005"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-SEQ-006"};

[[noreturn]] void fail(const std::string_view code,
                       const std::string_view message) {
  throw SystemVerilogUvmSequenceError{std::string{code}, std::string{message}};
}

[[nodiscard]] bool valid_name(const std::string_view name) {
  if (name.empty())
    return false;
  return std::ranges::none_of(name, [](const unsigned char character) {
    return character == '.' || character == '/' || character <= ' ';
  });
}

[[nodiscard]] bool class_type_matches(const SystemVerilogClassObject &object,
                                      const std::string_view nominal_type) {
  return object.dynamic_type == nominal_type ||
         object.declared_type == nominal_type ||
         object.specialization_identity == nominal_type ||
         std::ranges::find(object.assignable_declared_types, nominal_type) !=
             object.assignable_declared_types.end();
}

} // namespace

SystemVerilogUvmSequenceError::SystemVerilogUvmSequenceError(
    std::string code, std::string message)
    : std::runtime_error{std::move(message)}, code_(std::move(code)) {}

SystemVerilogUvmSequenceService::SystemVerilogUvmSequenceService(
    SystemVerilogClassHeap &heap, SystemVerilogUvmObjectService &objects,
    SystemVerilogUvmComponentService &components,
    SystemVerilogUvmSequenceLimits limits)
    : heap_(&heap), objects_(&objects), components_(&components),
      limits_(limits), owner_(std::make_shared<unsigned char>()) {
  if (limits_.maximum_items == 0 || limits_.maximum_sequences == 0 ||
      limits_.maximum_sequencers == 0 || limits_.maximum_registrations == 0 ||
      limits_.maximum_mutations == 0 || limits_.maximum_name_bytes == 0 ||
      limits_.maximum_full_name_bytes == 0 ||
      limits_.maximum_nominal_type_bytes == 0 ||
      limits_.maximum_profile_bytes == 0 || limits_.maximum_depth == 0 ||
      limits_.maximum_children_per_sequence == 0 ||
      limits_.maximum_items_per_sequence == 0 ||
      limits_.maximum_active_executions == 0 ||
      limits_.maximum_execution_events == 0 ||
      limits_.maximum_execution_failures == 0 ||
      limits_.maximum_responses_per_sequence == 0 ||
      limits_.maximum_pending_requests == 0 ||
      limits_.maximum_requests_per_sequencer == 0 ||
      limits_.maximum_arbitration_work == 0 ||
      limits_.maximum_relevance_waits == 0 || limits_.maximum_priority == 0 ||
      limits_.maximum_access_requests == 0 ||
      limits_.maximum_access_depth == 0 ||
      limits_.maximum_consecutive_grabs == 0 ||
      limits_.maximum_transactions == 0 ||
      limits_.maximum_transactions_per_sequencer == 0 ||
      limits_.maximum_in_flight_per_sequencer == 0 ||
      limits_.maximum_push_capacity == 0 ||
      limits_.maximum_transaction_timeout == 0 || limits_.maximum_roles == 0 ||
      limits_.maximum_roles_per_root == 0 ||
      limits_.maximum_role_dispatches == 0 ||
      limits_.maximum_role_failures == 0 || limits_.maximum_role_events == 0 ||
      limits_.maximum_analysis_connections_per_monitor == 0 ||
      limits_.maximum_role_publications == 0 ||
      limits_.maximum_virtual_domains_per_sequencer == 0 ||
      limits_.maximum_virtual_steps == 0 ||
      limits_.maximum_virtual_restarts == 0 ||
      limits_.maximum_virtual_events == 0) {
    fail(kResourceLimit, "UVM sequence limits must all be nonzero");
  }
}

SystemVerilogUvmSequenceService::SystemVerilogUvmSequenceService(
    SystemVerilogClassHeap &heap, SystemVerilogUvmObjectService &objects,
    SystemVerilogUvmComponentService &components,
    SystemVerilogUvmPhaseService &phases,
    SystemVerilogUvmObjectionService &objections,
    SystemVerilogUvmSequenceLimits limits)
    : SystemVerilogUvmSequenceService(heap, objects, components,
                                      std::move(limits)) {
  phases_ = &phases;
  objections_ = &objections;
}

void SystemVerilogUvmSequenceService::publish_activity(
    const SystemVerilogUvmActivityAction action, std::string identity,
    const SystemVerilogUvmRootHandle root, const std::uint64_t value,
    std::string detail) noexcept {
  if (!activity_)
    return;
  try {
    activity_->publish({SystemVerilogUvmActivityKind::Sequence, action,
                        std::move(identity), std::move(detail), root, value});
  } catch (...) {
    // An exhausted optional observation surface cannot invalidate sequence
    // lifecycle state that has already committed.
  }
}

void SystemVerilogUvmSequenceService::validate_registration_capacity(
    const std::size_t live, const std::size_t maximum) {
  if (live >= maximum || registrations_ >= limits_.maximum_registrations ||
      mutations_ >= limits_.maximum_mutations ||
      next_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_declaration_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence registration ceiling exceeded");
  }
}

void SystemVerilogUvmSequenceService::validate_name(
    const std::string_view name) const {
  if (!valid_name(name)) {
    fail(kInvalidHierarchy, "UVM sequence identity name is malformed");
  }
  if (name.size() > limits_.maximum_name_bytes) {
    fail(kResourceLimit, "UVM sequence identity name exceeds its byte ceiling");
  }
}

void SystemVerilogUvmSequenceService::validate_nominal_type(
    const std::string_view type) const {
  if (type.empty()) {
    fail(kInvalidType, "UVM sequence nominal type is empty");
  }
  if (type.size() > limits_.maximum_nominal_type_bytes) {
    fail(kResourceLimit, "UVM sequence nominal type exceeds its byte ceiling");
  }
}

void SystemVerilogUvmSequenceService::validate_profile(
    const SystemVerilogUvmSequenceProfile &profile) const {
  if (profile.request_type.empty()) {
    fail(kInvalidType, "UVM sequence request nominal type must not be empty");
  }
  if (profile.request_type.size() > limits_.maximum_profile_bytes ||
      profile.response_type.size() >
          limits_.maximum_profile_bytes - profile.request_type.size()) {
    fail(kResourceLimit, "UVM sequence profile exceeds its byte ceiling");
  }
}

void SystemVerilogUvmSequenceService::require_object_type(
    const SystemVerilogClassHandle object,
    const std::string_view nominal_type) const {
  if (object == 0 || !objects_->contains(object)) {
    fail(kInvalidHandle, "UVM sequence object is empty or stale");
  }
  if (!class_type_matches(heap_->object(object), nominal_type)) {
    fail(kInvalidType, "UVM sequence object does not match its nominal type");
  }
}

SystemVerilogUvmSequencerHandle
SystemVerilogUvmSequenceService::register_sequencer(
    SystemVerilogUvmSequencerDescriptor descriptor) {
  validate_registration_capacity(live_sequencers_, limits_.maximum_sequencers);
  validate_nominal_type(descriptor.nominal_type);
  validate_profile(descriptor.profile);
  if (descriptor.component == 0 ||
      !components_->contains(descriptor.component)) {
    fail(kInvalidHandle, "UVM sequencer component is empty or stale");
  }
  require_object_type(descriptor.component, descriptor.nominal_type);
  for (const auto &[slot, existing] : sequencers_) {
    (void)slot;
    if (existing.live && existing.value.component == descriptor.component) {
      fail(kInvalidLifecycle, "UVM sequencer component is already registered");
    }
  }

  const auto component = components_->snapshot(descriptor.component);
  const auto slot = next_slot_;
  const auto handle = SystemVerilogUvmSequencerHandle{owner_, slot, 1};
  SystemVerilogUvmSequencerSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.component = descriptor.component;
  snapshot.root = component.root;
  snapshot.nominal_type = std::move(descriptor.nominal_type);
  snapshot.profile = std::move(descriptor.profile);
  snapshot.full_name = component.full_name;
  snapshot.debug_name =
      std::string{components_->root_identity(component.root)} + ":" +
      component.full_name;
  snapshot.declaration_order = next_declaration_order_;
  sequencers_.emplace(slot, Sequencer{std::move(snapshot), 1, true, 1, {}});
  ++next_slot_;
  ++next_declaration_order_;
  ++registrations_;
  ++mutations_;
  ++live_sequencers_;
  const auto &created = sequencers_.at(slot).value;
  publish_activity(SystemVerilogUvmActivityAction::Created,
                   created.debug_name, created.root,
                   created.declaration_order, "sequencer");
  return handle;
}

SystemVerilogUvmSequenceHandle
SystemVerilogUvmSequenceService::register_sequence(
    SystemVerilogUvmSequenceDescriptor descriptor) {
  validate_registration_capacity(live_sequences_, limits_.maximum_sequences);
  validate_name(descriptor.name);
  validate_nominal_type(descriptor.nominal_type);
  validate_profile(descriptor.profile);
  require_object_type(descriptor.object, descriptor.nominal_type);
  for (const auto &[slot, existing] : sequences_) {
    (void)slot;
    if (existing.live && existing.value.object == descriptor.object) {
      fail(kInvalidLifecycle, "UVM sequence object is already registered");
    }
  }
  for (const auto &[slot, existing] : items_) {
    (void)slot;
    if (existing.live && existing.value.object == descriptor.object) {
      fail(kInvalidLifecycle, "UVM object is already registered as an item");
    }
  }

  Sequence *parent{};
  if (descriptor.parent)
    parent = &sequence(descriptor.parent);
  if (parent && !descriptor.sequencer && parent->value.sequencer) {
    descriptor.sequencer = parent->value.sequencer;
  }
  Sequencer *selected_sequencer{};
  if (descriptor.sequencer) {
    selected_sequencer = &sequencer(descriptor.sequencer);
    if (selected_sequencer->value.profile != descriptor.profile) {
      fail(kInvalidType, "UVM sequence and sequencer profiles do not match");
    }
  }
  if (parent && parent->value.sequencer && descriptor.sequencer &&
      parent->value.sequencer != descriptor.sequencer) {
    fail(kInvalidHierarchy, "UVM child sequence changes its parent sequencer");
  }

  const auto root =
      selected_sequencer
          ? selected_sequencer->value.root
          : (parent ? parent->value.root : SystemVerilogUvmRootHandle{});
  if (parent && parent->value.root != 0 && root != 0 &&
      parent->value.root != root) {
    fail(kInvalidHierarchy, "UVM child sequence crosses simulation roots");
  }
  const auto depth = parent ? parent->value.depth + 1U : 0U;
  if (depth > limits_.maximum_depth) {
    fail(kResourceLimit, "UVM sequence hierarchy depth ceiling exceeded");
  }

  std::vector<SystemVerilogUvmSequenceHandle> updated_children;
  if (parent) {
    if (parent->value.children.size() >=
        limits_.maximum_children_per_sequence) {
      fail(kResourceLimit, "UVM child sequence ceiling exceeded");
    }
    for (const auto &child : parent->value.children) {
      if (sequence(child).value.name == descriptor.name) {
        fail(kInvalidHierarchy, "duplicate UVM child sequence name");
      }
    }
    updated_children = parent->value.children;
  } else {
    for (const auto &[slot, existing] : sequences_) {
      (void)slot;
      if (existing.live && !existing.value.parent &&
          existing.value.root == root &&
          existing.value.name == descriptor.name) {
        fail(kInvalidHierarchy, "duplicate top-level UVM sequence name");
      }
    }
  }

  const auto slot = next_slot_;
  const auto handle = SystemVerilogUvmSequenceHandle{owner_, slot, 1};
  if (parent)
    updated_children.push_back(handle);
  SystemVerilogUvmSequenceSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.object = descriptor.object;
  snapshot.root = root;
  snapshot.name = std::move(descriptor.name);
  snapshot.full_name =
      parent ? parent->value.full_name + "." + snapshot.name : snapshot.name;
  if (snapshot.full_name.size() > limits_.maximum_full_name_bytes) {
    fail(kResourceLimit, "UVM sequence full name exceeds its byte ceiling");
  }
  snapshot.debug_name = root == 0
                            ? "unbound:" + snapshot.full_name
                            : std::string{components_->root_identity(root)} +
                                  ":" + snapshot.full_name;
  snapshot.nominal_type = std::move(descriptor.nominal_type);
  snapshot.profile = std::move(descriptor.profile);
  snapshot.parent = descriptor.parent;
  snapshot.sequencer = descriptor.sequencer;
  snapshot.depth = depth;
  snapshot.declaration_order = next_declaration_order_;
  snapshot.response_queue_depth = limits_.maximum_responses_per_sequence;
  sequences_.emplace(slot, Sequence{std::move(snapshot),
                                    std::move(descriptor.hooks),
                                    1,
                                    true,
                                    false,
                                    false,
                                    false,
                                    0,
                                    {},
                                    false,
                                    false,
                                    0});
  if (parent)
    parent->value.children.swap(updated_children);
  ++next_slot_;
  ++next_declaration_order_;
  ++registrations_;
  ++mutations_;
  ++live_sequences_;
  const auto &created = sequences_.at(slot).value;
  publish_activity(SystemVerilogUvmActivityAction::Created,
                   created.debug_name, created.root,
                   created.declaration_order, "sequence");
  return handle;
}

SystemVerilogUvmSequenceItemHandle
SystemVerilogUvmSequenceService::register_item(
    SystemVerilogUvmSequenceItemDescriptor descriptor) {
  validate_registration_capacity(live_items_, limits_.maximum_items);
  validate_name(descriptor.name);
  validate_nominal_type(descriptor.nominal_type);
  require_object_type(descriptor.object, descriptor.nominal_type);
  for (const auto &[slot, existing] : items_) {
    (void)slot;
    if (existing.live && existing.value.object == descriptor.object) {
      fail(kInvalidLifecycle, "UVM sequence item object is already registered");
    }
  }
  for (const auto &[slot, existing] : sequences_) {
    (void)slot;
    if (existing.live && existing.value.object == descriptor.object) {
      fail(kInvalidLifecycle, "UVM object is already registered as a sequence");
    }
  }

  Sequence *owner_sequence{};
  if (descriptor.owner_sequence) {
    owner_sequence = &sequence(descriptor.owner_sequence);
    if (!descriptor.sequencer && owner_sequence->value.sequencer) {
      descriptor.sequencer = owner_sequence->value.sequencer;
    }
  }
  Sequencer *selected_sequencer{};
  if (descriptor.sequencer) {
    selected_sequencer = &sequencer(descriptor.sequencer);
  }
  if (owner_sequence && owner_sequence->value.sequencer &&
      descriptor.sequencer &&
      owner_sequence->value.sequencer != descriptor.sequencer) {
    fail(kInvalidHierarchy, "UVM sequence item changes its owner sequencer");
  }
  if (owner_sequence && owner_sequence->value.root != 0 && selected_sequencer &&
      owner_sequence->value.root != selected_sequencer->value.root) {
    fail(kInvalidHierarchy, "UVM sequence item crosses simulation roots");
  }

  const SystemVerilogUvmSequenceProfile *profile{};
  if (owner_sequence)
    profile = &owner_sequence->value.profile;
  else if (selected_sequencer)
    profile = &selected_sequencer->value.profile;
  if (profile) {
    const auto &expected =
        descriptor.role == SystemVerilogUvmSequenceItemRole::Request
            ? profile->request_type
            : profile->response_type;
    if (expected.empty() || expected != descriptor.nominal_type) {
      fail(kInvalidType, "UVM sequence item does not match its owner profile");
    }
  }

  const auto root = selected_sequencer
                        ? selected_sequencer->value.root
                        : (owner_sequence ? owner_sequence->value.root
                                          : SystemVerilogUvmRootHandle{});

  std::vector<SystemVerilogUvmSequenceItemHandle> updated_items;
  if (owner_sequence) {
    if (owner_sequence->value.items.size() >=
        limits_.maximum_items_per_sequence) {
      fail(kResourceLimit, "UVM owned sequence item ceiling exceeded");
    }
    for (const auto &owned_item : owner_sequence->value.items) {
      if (item(owned_item).value.name == descriptor.name) {
        fail(kInvalidHierarchy, "duplicate UVM owned sequence item name");
      }
    }
    updated_items = owner_sequence->value.items;
  } else {
    for (const auto &[slot, existing] : items_) {
      (void)slot;
      if (existing.live && !existing.value.owner_sequence &&
          existing.value.root == root &&
          existing.value.name == descriptor.name) {
        fail(kInvalidHierarchy, "duplicate unowned UVM sequence item name");
      }
    }
  }

  const auto slot = next_slot_;
  const auto handle = SystemVerilogUvmSequenceItemHandle{owner_, slot, 1};
  if (owner_sequence)
    updated_items.push_back(handle);
  SystemVerilogUvmSequenceItemSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.object = descriptor.object;
  snapshot.root = root;
  snapshot.name = std::move(descriptor.name);
  snapshot.full_name =
      owner_sequence ? owner_sequence->value.full_name + "." + snapshot.name
                     : snapshot.name;
  if (snapshot.full_name.size() > limits_.maximum_full_name_bytes) {
    fail(kResourceLimit,
         "UVM sequence item full name exceeds its byte ceiling");
  }
  snapshot.debug_name = root == 0
                            ? "unbound:" + snapshot.full_name
                            : std::string{components_->root_identity(root)} +
                                  ":" + snapshot.full_name;
  snapshot.nominal_type = std::move(descriptor.nominal_type);
  snapshot.role = descriptor.role;
  snapshot.owner_sequence = descriptor.owner_sequence;
  snapshot.sequencer = descriptor.sequencer;
  snapshot.declaration_order = next_declaration_order_;
  snapshot.state = owner_sequence ? SystemVerilogUvmSequenceItemState::Owned
                                  : SystemVerilogUvmSequenceItemState::Created;
  items_.emplace(slot, Item{std::move(snapshot), 1, true});
  if (owner_sequence)
    owner_sequence->value.items.swap(updated_items);
  ++next_slot_;
  ++next_declaration_order_;
  ++registrations_;
  ++mutations_;
  ++live_items_;
  const auto &created = items_.at(slot).value;
  publish_activity(SystemVerilogUvmActivityAction::Created,
                   created.debug_name, created.root,
                   created.declaration_order, "item");
  return handle;
}

bool SystemVerilogUvmSequenceService::contains(
    const SystemVerilogUvmSequencerHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_)
    return false;
  const auto found = sequencers_.find(handle.slot_);
  return found != sequencers_.end() && found->second.live &&
         found->second.generation == handle.generation_ &&
         components_->contains(found->second.value.component) &&
         components_->contains_root(found->second.value.root);
}

bool SystemVerilogUvmSequenceService::contains(
    const SystemVerilogUvmSequenceHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_)
    return false;
  const auto found = sequences_.find(handle.slot_);
  return found != sequences_.end() && found->second.live &&
         found->second.generation == handle.generation_ &&
         objects_->contains(found->second.value.object);
}

bool SystemVerilogUvmSequenceService::contains(
    const SystemVerilogUvmSequenceItemHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_)
    return false;
  const auto found = items_.find(handle.slot_);
  return found != items_.end() && found->second.live &&
         found->second.generation == handle.generation_ &&
         objects_->contains(found->second.value.object);
}

SystemVerilogUvmSequencerSnapshot SystemVerilogUvmSequenceService::snapshot(
    const SystemVerilogUvmSequencerHandle handle) const {
  return sequencer(handle).value;
}

SystemVerilogUvmSequenceSnapshot SystemVerilogUvmSequenceService::snapshot(
    const SystemVerilogUvmSequenceHandle handle) const {
  return sequence(handle).value;
}

SystemVerilogUvmSequenceItemSnapshot SystemVerilogUvmSequenceService::snapshot(
    const SystemVerilogUvmSequenceItemHandle handle) const {
  return item(handle).value;
}

std::vector<SystemVerilogUvmSequencerHandle>
SystemVerilogUvmSequenceService::sequencers() const {
  std::vector<SystemVerilogUvmSequencerHandle> result;
  result.reserve(live_sequencers_);
  for (const auto &[slot, value] : sequencers_) {
    if (value.live && contains(sequencer_handle(slot))) {
      result.push_back(sequencer_handle(slot));
    }
  }
  return result;
}

std::vector<SystemVerilogUvmSequenceHandle>
SystemVerilogUvmSequenceService::sequences() const {
  std::vector<SystemVerilogUvmSequenceHandle> result;
  result.reserve(live_sequences_);
  for (const auto &[slot, value] : sequences_) {
    if (value.live && contains(sequence_handle(slot))) {
      result.push_back(sequence_handle(slot));
    }
  }
  return result;
}

std::vector<SystemVerilogUvmSequenceItemHandle>
SystemVerilogUvmSequenceService::items() const {
  std::vector<SystemVerilogUvmSequenceItemHandle> result;
  result.reserve(live_items_);
  for (const auto &[slot, value] : items_) {
    if (value.live && contains(item_handle(slot))) {
      result.push_back(item_handle(slot));
    }
  }
  return result;
}

void SystemVerilogUvmSequenceService::release(
    const SystemVerilogUvmSequenceItemHandle handle) {
  auto &value = item(handle);
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (existing.live && (existing.value.request_item == handle ||
                          existing.value.response_item == handle)) {
      fail(kInvalidLifecycle, "UVM sequence item has a retained transaction");
    }
  }
  if (mutations_ >= limits_.maximum_mutations ||
      value.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence item release ceiling exceeded");
  }
  if (value.value.owner_sequence) {
    const auto found = sequences_.find(value.value.owner_sequence.slot_);
    if (found != sequences_.end() && found->second.live &&
        found->second.generation == value.value.owner_sequence.generation_) {
      std::erase(found->second.value.items, handle);
      std::erase(found->second.value.responses, handle);
    }
  }
  value.live = false;
  ++value.generation;
  ++mutations_;
  --live_items_;
}

void SystemVerilogUvmSequenceService::release(
    const SystemVerilogUvmSequenceHandle handle) {
  auto &value = sequence(handle);
  for (const auto &[slot, pending] : requests_) {
    (void)slot;
    if (pending.live && pending.value.sequence == handle) {
      fail(kInvalidLifecycle, "UVM sequence still owns a pending request");
    }
  }
  for (const auto &[slot, pending] : accesses_) {
    (void)slot;
    if (pending.live && pending.value.sequence == handle) {
      fail(kInvalidLifecycle, "UVM sequence still owns sequencer access");
    }
  }
  for (const auto &[slot, existing] : transactions_) {
    (void)slot;
    if (existing.live && existing.value.sequence == handle) {
      fail(kInvalidLifecycle, "UVM sequence has a retained transaction");
    }
  }
  if (value.active || !value.value.children.empty() ||
      !value.value.items.empty()) {
    fail(kInvalidLifecycle, "owned UVM sequence state must be released first");
  }
  if (mutations_ >= limits_.maximum_mutations ||
      value.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence release ceiling exceeded");
  }
  if (value.value.parent) {
    const auto found = sequences_.find(value.value.parent.slot_);
    if (found != sequences_.end() && found->second.live &&
        found->second.generation == value.value.parent.generation_) {
      std::erase(found->second.value.children, handle);
    }
  }
  value.live = false;
  ++value.generation;
  ++mutations_;
  --live_sequences_;
}

void SystemVerilogUvmSequenceService::release(
    const SystemVerilogUvmSequencerHandle handle) {
  auto &value = sequencer(handle);
  if (value.value.parent_virtual && contains(value.value.parent_virtual)) {
    fail(kInvalidLifecycle,
         "UVM sequencer is still owned by a virtual sequencer");
  }
  if (!value.value.requests.empty()) {
    fail(kInvalidLifecycle, "UVM sequencer still owns pending requests");
  }
  if (!value.value.access_queue.empty() || !value.value.access_stack.empty()) {
    fail(kInvalidLifecycle, "UVM sequencer still owns access requests");
  }
  if (!value.value.transactions.empty()) {
    fail(kInvalidLifecycle, "UVM sequencer still owns retained transactions");
  }
  for (const auto &[slot, existing] : sequences_) {
    (void)slot;
    if (existing.live && existing.value.sequencer == handle) {
      fail(kInvalidLifecycle, "UVM sequencer still owns a live sequence");
    }
  }
  for (const auto &[slot, existing] : items_) {
    (void)slot;
    if (existing.live && existing.value.sequencer == handle) {
      fail(kInvalidLifecycle, "UVM sequencer still owns a live item");
    }
  }
  if (mutations_ >= limits_.maximum_mutations ||
      value.generation == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequencer release ceiling exceeded");
  }
  if (value.value.is_virtual) {
    for (const auto &domain : value.value.virtual_domains) {
      auto &child = sequencer(domain.sequencer);
      child.value.parent_virtual = {};
    }
  }
  value.live = false;
  ++value.generation;
  ++mutations_;
  --live_sequencers_;
}

SystemVerilogUvmSequenceService::Sequencer &
SystemVerilogUvmSequenceService::sequencer(
    const SystemVerilogUvmSequencerHandle handle) {
  return const_cast<Sequencer &>(std::as_const(*this).sequencer(handle));
}

const SystemVerilogUvmSequenceService::Sequencer &
SystemVerilogUvmSequenceService::sequencer(
    const SystemVerilogUvmSequencerHandle handle) const {
  if (!handle.valid())
    fail(kInvalidHandle, "UVM sequencer handle is empty");
  if (handle.owner_ != owner_) {
    fail(kForeignHandle, "UVM sequencer handle belongs to another simulation");
  }
  const auto found = sequencers_.find(handle.slot_);
  if (found == sequencers_.end() || !found->second.live ||
      found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM sequencer handle is stale");
  }
  require_live(found->second);
  return found->second;
}

SystemVerilogUvmSequenceService::Sequence &
SystemVerilogUvmSequenceService::sequence(
    const SystemVerilogUvmSequenceHandle handle) {
  return const_cast<Sequence &>(std::as_const(*this).sequence(handle));
}

const SystemVerilogUvmSequenceService::Sequence &
SystemVerilogUvmSequenceService::sequence(
    const SystemVerilogUvmSequenceHandle handle) const {
  if (!handle.valid())
    fail(kInvalidHandle, "UVM sequence handle is empty");
  if (handle.owner_ != owner_) {
    fail(kForeignHandle, "UVM sequence handle belongs to another simulation");
  }
  const auto found = sequences_.find(handle.slot_);
  if (found == sequences_.end() || !found->second.live ||
      found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM sequence handle is stale");
  }
  require_live(found->second);
  return found->second;
}

SystemVerilogUvmSequenceService::Item &SystemVerilogUvmSequenceService::item(
    const SystemVerilogUvmSequenceItemHandle handle) {
  return const_cast<Item &>(std::as_const(*this).item(handle));
}

const SystemVerilogUvmSequenceService::Item &
SystemVerilogUvmSequenceService::item(
    const SystemVerilogUvmSequenceItemHandle handle) const {
  if (!handle.valid())
    fail(kInvalidHandle, "UVM sequence item handle is empty");
  if (handle.owner_ != owner_) {
    fail(kForeignHandle, "UVM sequence item belongs to another simulation");
  }
  const auto found = items_.find(handle.slot_);
  if (found == items_.end() || !found->second.live ||
      found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM sequence item handle is stale");
  }
  require_live(found->second);
  return found->second;
}

SystemVerilogUvmSequencerHandle
SystemVerilogUvmSequenceService::sequencer_handle(
    const std::uint64_t slot) const {
  return SystemVerilogUvmSequencerHandle{owner_, slot,
                                         sequencers_.at(slot).generation};
}

SystemVerilogUvmSequenceHandle SystemVerilogUvmSequenceService::sequence_handle(
    const std::uint64_t slot) const {
  return SystemVerilogUvmSequenceHandle{owner_, slot,
                                        sequences_.at(slot).generation};
}

SystemVerilogUvmSequenceItemHandle
SystemVerilogUvmSequenceService::item_handle(const std::uint64_t slot) const {
  return SystemVerilogUvmSequenceItemHandle{owner_, slot,
                                            items_.at(slot).generation};
}

void SystemVerilogUvmSequenceService::require_live(
    const Sequencer &value) const {
  if (!components_->contains(value.value.component) ||
      !components_->contains_root(value.value.root) ||
      components_->root_of(value.value.component) != value.value.root) {
    fail(kInvalidHandle, "UVM sequencer component or root is stale");
  }
}

void SystemVerilogUvmSequenceService::require_live(
    const Sequence &value) const {
  if (!objects_->contains(value.value.object)) {
    fail(kInvalidHandle, "UVM sequence backing object is stale");
  }
  if (value.value.parent && !contains(value.value.parent)) {
    fail(kInvalidHandle, "UVM sequence parent is stale");
  }
  if (value.value.sequencer && !contains(value.value.sequencer)) {
    fail(kInvalidHandle, "UVM sequence sequencer is stale");
  }
}

void SystemVerilogUvmSequenceService::require_live(const Item &value) const {
  if (!objects_->contains(value.value.object)) {
    fail(kInvalidHandle, "UVM sequence item backing object is stale");
  }
  if (value.value.owner_sequence && !contains(value.value.owner_sequence)) {
    fail(kInvalidHandle, "UVM sequence item owner is stale");
  }
  if (value.value.sequencer && !contains(value.value.sequencer)) {
    fail(kInvalidHandle, "UVM sequence item sequencer is stale");
  }
}

} // namespace fsim::runtime
