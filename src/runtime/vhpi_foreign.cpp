// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_foreign.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ranges>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_registration = 0xc3U;
constexpr std::uint8_t k_call = 0xc4U;
constexpr std::uint8_t k_argument = 0xc5U;
constexpr std::uint8_t k_result = 0xc6U;
constexpr std::size_t maximum_parameters = 64;
std::atomic<std::uint32_t> next_foreign_system{1U};

[[nodiscard]] bool valid_scope_kind(
    const VhdlVhpiObjectKind kind) noexcept {
  return kind == VhdlVhpiObjectKind::Root
      || kind == VhdlVhpiObjectKind::Region
      || kind == VhdlVhpiObjectKind::Entity
      || kind == VhdlVhpiObjectKind::Architecture
      || kind == VhdlVhpiObjectKind::Process
      || kind == VhdlVhpiObjectKind::Subprogram;
}

}  // namespace

VhdlVhpiForeignSystem::VhdlVhpiForeignSystem(
    VhdlVhpiObjectRegistry& objects,
    VhdlVhpiTypeSystem& types) noexcept
    : objects_(&objects),
      types_(&types),
      system_identity_(
          next_foreign_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_foreign_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
}

VhdlVhpiForeignSystem::~VhdlVhpiForeignSystem() {
  teardown();
}

bool VhdlVhpiForeignSystem::valid() const noexcept {
  return objects_ != nullptr && types_ != nullptr && objects_->valid();
}

bool VhdlVhpiForeignSystem::valid_name(
    const std::string_view name) noexcept {
  if (name.empty() || name.size() > 256
      || !(std::isalpha(static_cast<unsigned char>(name.front()))
          || name.front() == '_')) {
    return false;
  }
  return std::ranges::all_of(name, [](const char character) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_';
  });
}

std::string VhdlVhpiForeignSystem::canonical_name(
    const std::string_view name) {
  std::string result{name};
  std::ranges::transform(result, result.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return result;
}

std::uint64_t VhdlVhpiForeignSystem::make_identity(
    const std::uint8_t kind, const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(kind) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::validate_identity(
    const std::uint64_t identity,
    const std::uint8_t kind) const noexcept {
  if (identity == 0U
      || static_cast<std::uint8_t>(identity >> 56U) != kind
      || static_cast<std::uint32_t>(identity) == 0U) {
    return VhdlVhpiForeignError::InvalidHandle;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiForeignError::None
                                   : VhdlVhpiForeignError::CrossSimulation;
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::validate_profile(
    const VhdlVhpiForeignRegistration& registration) const {
  if (static_cast<std::uint32_t>(registration.kind)
          > static_cast<std::uint32_t>(VhdlVhpiForeignKind::Model)
      || !valid_name(registration.name) || !registration.invoke
      || registration.parameters.size() > maximum_parameters) {
    return VhdlVhpiForeignError::InvalidProfile;
  }
  if ((registration.kind == VhdlVhpiForeignKind::Model)
          != static_cast<bool>(registration.start)
      || (registration.kind == VhdlVhpiForeignKind::Model)
          != static_cast<bool>(registration.stop)) {
    return VhdlVhpiForeignError::InvalidProfile;
  }
  std::unordered_set<std::string> names;
  try {
    for (const auto& parameter : registration.parameters) {
      if (!valid_name(parameter.name)
          || static_cast<std::uint32_t>(parameter.mode)
              > static_cast<std::uint32_t>(
                  VhdlVhpiParameterMode::InOut)
          || !types_->query(parameter.type)
          || !names.emplace(canonical_name(parameter.name)).second) {
        return VhdlVhpiForeignError::InvalidProfile;
      }
    }
  } catch (...) {
    return VhdlVhpiForeignError::ResourceLimit;
  }
  if (registration.result_type != 0U
      && !types_->query(registration.result_type)) {
    return VhdlVhpiForeignError::InvalidType;
  }
  return VhdlVhpiForeignError::None;
}

VhdlVhpiForeignRegistrationResult
VhdlVhpiForeignSystem::register_foreign(
    VhdlVhpiForeignRegistration registration) {
  if (!valid()) {
    return {{}, VhdlVhpiForeignError::InvalidSimulation};
  }
  const auto profile_error = validate_profile(registration);
  if (profile_error != VhdlVhpiForeignError::None) {
    return {{}, profile_error};
  }
  std::string name;
  try {
    name = canonical_name(registration.name);
  } catch (...) {
    return {{}, VhdlVhpiForeignError::ResourceLimit};
  }
  std::scoped_lock lock{mutex_};
  if (closed_) {
    return {{}, VhdlVhpiForeignError::Closed};
  }
  if (names_.contains(name)) {
    return {{}, VhdlVhpiForeignError::Duplicate};
  }
  if (next_registration_ == 0U) {
    return {{}, VhdlVhpiForeignError::ResourceLimit};
  }
  const auto identity = make_identity(k_registration, next_registration_++);
  VhdlVhpiForeignDescriptor descriptor{
      identity,
      registration.kind,
      std::move(registration.name),
      std::move(registration.parameters),
      registration.result_type,
      next_ordinal_++,
      registration.user_data};
  try {
    registrations_.emplace(
        identity,
        RegistrationEntry{
            descriptor,
            std::move(registration.start),
            std::move(registration.invoke),
            std::move(registration.stop)});
    names_.emplace(std::move(name), identity);
  } catch (...) {
    registrations_.erase(identity);
    return {{}, VhdlVhpiForeignError::ResourceLimit};
  }
  return {descriptor, VhdlVhpiForeignError::None};
}

VhdlVhpiForeignDescriptorResult VhdlVhpiForeignSystem::registration(
    const std::uint64_t identity) const {
  const auto error = validate_identity(identity, k_registration);
  if (error != VhdlVhpiForeignError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = registrations_.find(identity);
  return found == registrations_.end()
      ? VhdlVhpiForeignDescriptorResult{
            {}, VhdlVhpiForeignError::InvalidHandle}
      : VhdlVhpiForeignDescriptorResult{
            found->second.descriptor, VhdlVhpiForeignError::None};
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::start(
    const std::uint64_t identity) {
  const auto error = validate_identity(identity, k_registration);
  if (error != VhdlVhpiForeignError::None) {
    return error;
  }
  VhdlVhpiForeignLifecycle callback;
  VhdlVhpiForeignDescriptor descriptor;
  VhdlVhpiForeignState previous;
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return VhdlVhpiForeignError::Closed;
    }
    const auto found = registrations_.find(identity);
    if (found == registrations_.end()
        || found->second.descriptor.state
            == VhdlVhpiForeignState::Unregistered) {
      return VhdlVhpiForeignError::Unregistered;
    }
    if (found->second.descriptor.kind != VhdlVhpiForeignKind::Model
        || (found->second.descriptor.state
                != VhdlVhpiForeignState::Registered
            && found->second.descriptor.state
                != VhdlVhpiForeignState::Stopped)) {
      return VhdlVhpiForeignError::InvalidState;
    }
    previous = found->second.descriptor.state;
    found->second.descriptor.state = VhdlVhpiForeignState::Started;
    callback = found->second.start;
    descriptor = found->second.descriptor;
  }
  try {
    callback(descriptor);
  } catch (...) {
    std::scoped_lock lock{mutex_};
    registrations_.at(identity).descriptor.state = previous;
    return VhdlVhpiForeignError::CallbackFailed;
  }
  return VhdlVhpiForeignError::None;
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::stop(
    const std::uint64_t identity) {
  const auto error = validate_identity(identity, k_registration);
  if (error != VhdlVhpiForeignError::None) {
    return error;
  }
  VhdlVhpiForeignLifecycle callback;
  VhdlVhpiForeignDescriptor descriptor;
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return VhdlVhpiForeignError::Closed;
    }
    const auto found = registrations_.find(identity);
    if (found == registrations_.end()
        || found->second.descriptor.state
            == VhdlVhpiForeignState::Unregistered) {
      return VhdlVhpiForeignError::Unregistered;
    }
    if (found->second.descriptor.kind != VhdlVhpiForeignKind::Model
        || found->second.descriptor.state
            != VhdlVhpiForeignState::Started) {
      return VhdlVhpiForeignError::InvalidState;
    }
    if (found->second.descriptor.active_calls != 0U) {
      return VhdlVhpiForeignError::ActiveCall;
    }
    found->second.descriptor.state = VhdlVhpiForeignState::Stopped;
    callback = found->second.stop;
    descriptor = found->second.descriptor;
  }
  try {
    callback(descriptor);
  } catch (...) {
    return VhdlVhpiForeignError::CallbackFailed;
  }
  return VhdlVhpiForeignError::None;
}

VhdlVhpiForeignCallResult VhdlVhpiForeignSystem::invoke(
    const std::uint64_t registration_identity,
    const fsim_vhpi_handle_v1 scope,
    std::vector<VhdlVhpiForeignValue> values,
    const std::uint64_t user_data) {
  const auto identity_error =
      validate_identity(registration_identity, k_registration);
  if (identity_error != VhdlVhpiForeignError::None) {
    return {{}, identity_error};
  }
  const auto scope_metadata = objects_->lookup_object(scope);
  if (!scope_metadata) {
    return {{}, scope_metadata.error == VhdlVhpiObjectError::CrossSimulation
        ? VhdlVhpiForeignError::CrossSimulation
        : VhdlVhpiForeignError::InvalidScope};
  }
  if (!valid_scope_kind(scope_metadata.value.kind)) {
    return {{}, VhdlVhpiForeignError::InvalidScope};
  }

  VhdlVhpiForeignCallback callback;
  VhdlVhpiForeignInvocation invocation;
  std::uint64_t call_identity{};
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return {{}, VhdlVhpiForeignError::Closed};
    }
    const auto registered = registrations_.find(registration_identity);
    if (registered == registrations_.end()
        || registered->second.descriptor.state
            == VhdlVhpiForeignState::Unregistered) {
      return {{}, VhdlVhpiForeignError::Unregistered};
    }
    if (registered->second.descriptor.kind == VhdlVhpiForeignKind::Model
        && registered->second.descriptor.state
            != VhdlVhpiForeignState::Started) {
      return {{}, VhdlVhpiForeignError::InvalidState};
    }
    if (values.size()
        != registered->second.descriptor.parameters.size()) {
      return {{}, VhdlVhpiForeignError::InvalidArguments};
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (values[index].type
          != registered->second.descriptor.parameters[index].type) {
        return {{}, VhdlVhpiForeignError::InvalidArguments};
      }
    }
    if (next_call_ == 0U || next_argument_ == 0U) {
      return {{}, VhdlVhpiForeignError::ResourceLimit};
    }
    call_identity = make_identity(k_call, next_call_++);
    CallEntry call_entry;
    call_entry.descriptor.identity = call_identity;
    call_entry.descriptor.registration = registration_identity;
    call_entry.descriptor.scope = scope;
    call_entry.descriptor.user_data = user_data;
    call_entry.values = std::move(values);
    try {
      call_entry.descriptor.arguments.reserve(call_entry.values.size());
      for (const auto& value : call_entry.values) {
        const auto argument_identity =
            make_identity(k_argument, next_argument_++);
        call_entry.descriptor.arguments.push_back(argument_identity);
        arguments_.emplace(
            argument_identity, ValueEntry{call_identity, value});
      }
      invocation = VhdlVhpiForeignInvocation{
          registration_identity,
          call_identity,
          scope,
          call_entry.descriptor.arguments,
          registered->second.descriptor.user_data,
          user_data};
      calls_.emplace(call_identity, std::move(call_entry));
      ++registered->second.descriptor.active_calls;
      ++registered->second.descriptor.invocations;
      callback = registered->second.invoke;
    } catch (...) {
      for (auto iterator = arguments_.begin();
           iterator != arguments_.end();) {
        iterator = iterator->second.call == call_identity
            ? arguments_.erase(iterator) : std::next(iterator);
      }
      return {{}, VhdlVhpiForeignError::ResourceLimit};
    }
  }

  VhdlVhpiForeignError execution_error{VhdlVhpiForeignError::None};
  try {
    callback(invocation);
  } catch (...) {
    execution_error = VhdlVhpiForeignError::CallbackFailed;
  }
  std::scoped_lock lock{mutex_};
  auto& call_entry = calls_.at(call_identity);
  auto& registered = registrations_.at(registration_identity);
  if (execution_error == VhdlVhpiForeignError::None
      && registered.descriptor.result_type != 0U
      && !call_entry.result) {
    execution_error = VhdlVhpiForeignError::ResultMissing;
  }
  call_entry.descriptor.execution_error = execution_error;
  call_entry.descriptor.state = execution_error == VhdlVhpiForeignError::None
      ? VhdlVhpiCallState::Completed : VhdlVhpiCallState::Failed;
  --registered.descriptor.active_calls;
  return {call_entry.descriptor, execution_error};
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::publish_result(
    const std::uint64_t call_identity,
    VhdlVhpiForeignValue value) {
  const auto error = validate_identity(call_identity, k_call);
  if (error != VhdlVhpiForeignError::None) {
    return error;
  }
  std::scoped_lock lock{mutex_};
  const auto found = calls_.find(call_identity);
  if (found == calls_.end()) {
    return VhdlVhpiForeignError::InvalidHandle;
  }
  if (found->second.descriptor.state != VhdlVhpiCallState::Active) {
    return VhdlVhpiForeignError::InvalidState;
  }
  const auto& registered =
      registrations_.at(found->second.descriptor.registration).descriptor;
  if (registered.result_type == 0U) {
    return VhdlVhpiForeignError::ResultNotAllowed;
  }
  if (value.type != registered.result_type) {
    return VhdlVhpiForeignError::ResultTypeMismatch;
  }
  if (found->second.result) {
    return VhdlVhpiForeignError::ResultAlreadyPublished;
  }
  if (next_result_ == 0U) {
    return VhdlVhpiForeignError::ResourceLimit;
  }
  const auto result_identity = make_identity(k_result, next_result_++);
  try {
    results_.emplace(
        result_identity, ValueEntry{call_identity, value});
    found->second.result = std::move(value);
  } catch (...) {
    results_.erase(result_identity);
    return VhdlVhpiForeignError::ResourceLimit;
  }
  found->second.descriptor.result = result_identity;
  return VhdlVhpiForeignError::None;
}

VhdlVhpiForeignCallResult VhdlVhpiForeignSystem::call(
    const std::uint64_t identity) const {
  const auto error = validate_identity(identity, k_call);
  if (error != VhdlVhpiForeignError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = calls_.find(identity);
  return found == calls_.end()
      ? VhdlVhpiForeignCallResult{{}, VhdlVhpiForeignError::InvalidHandle}
      : VhdlVhpiForeignCallResult{
            found->second.descriptor, VhdlVhpiForeignError::None};
}

VhdlVhpiForeignValueResult VhdlVhpiForeignSystem::argument(
    const std::uint64_t identity) const {
  const auto error = validate_identity(identity, k_argument);
  if (error != VhdlVhpiForeignError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = arguments_.find(identity);
  return found == arguments_.end()
      ? VhdlVhpiForeignValueResult{{}, VhdlVhpiForeignError::InvalidHandle}
      : VhdlVhpiForeignValueResult{
            found->second.value, VhdlVhpiForeignError::None};
}

VhdlVhpiForeignValueResult VhdlVhpiForeignSystem::result(
    const std::uint64_t identity) const {
  const auto error = validate_identity(identity, k_result);
  if (error != VhdlVhpiForeignError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = results_.find(identity);
  return found == results_.end()
      ? VhdlVhpiForeignValueResult{{}, VhdlVhpiForeignError::InvalidHandle}
      : VhdlVhpiForeignValueResult{
            found->second.value, VhdlVhpiForeignError::None};
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::release_call(
    const std::uint64_t identity) {
  const auto error = validate_identity(identity, k_call);
  if (error != VhdlVhpiForeignError::None) {
    return error;
  }
  std::scoped_lock lock{mutex_};
  const auto found = calls_.find(identity);
  if (found == calls_.end()) {
    return VhdlVhpiForeignError::InvalidHandle;
  }
  if (found->second.descriptor.state == VhdlVhpiCallState::Active) {
    return VhdlVhpiForeignError::ActiveCall;
  }
  for (const auto argument : found->second.descriptor.arguments) {
    arguments_.erase(argument);
  }
  if (found->second.descriptor.result != 0U) {
    results_.erase(found->second.descriptor.result);
  }
  calls_.erase(found);
  return VhdlVhpiForeignError::None;
}

VhdlVhpiForeignError VhdlVhpiForeignSystem::unregister_foreign(
    const std::uint64_t identity) {
  const auto error = validate_identity(identity, k_registration);
  if (error != VhdlVhpiForeignError::None) {
    return error;
  }
  VhdlVhpiForeignLifecycle start_callback;
  VhdlVhpiForeignCallback invoke_callback;
  VhdlVhpiForeignLifecycle stop_callback;
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return VhdlVhpiForeignError::Closed;
    }
    const auto found = registrations_.find(identity);
    if (found == registrations_.end()
        || found->second.descriptor.state
            == VhdlVhpiForeignState::Unregistered) {
      return VhdlVhpiForeignError::Unregistered;
    }
    if (found->second.descriptor.active_calls != 0U) {
      return VhdlVhpiForeignError::ActiveCall;
    }
    if (found->second.descriptor.state == VhdlVhpiForeignState::Started) {
      return VhdlVhpiForeignError::InvalidState;
    }
    for (auto position = names_.begin(); position != names_.end();
         ++position) {
      if (position->second == identity) {
        names_.erase(position);
        break;
      }
    }
    found->second.descriptor.state = VhdlVhpiForeignState::Unregistered;
    start_callback = std::move(found->second.start);
    invoke_callback = std::move(found->second.invoke);
    stop_callback = std::move(found->second.stop);
  }
  start_callback = {};
  invoke_callback = {};
  stop_callback = {};
  return VhdlVhpiForeignError::None;
}

void VhdlVhpiForeignSystem::teardown() noexcept {
  {
    std::scoped_lock lock{mutex_};
    if (closed_) {
      return;
    }
    closed_ = true;
    names_.clear();
    calls_.clear();
    arguments_.clear();
    results_.clear();
  }
  while (true) {
    VhdlVhpiForeignLifecycle start_callback;
    VhdlVhpiForeignCallback invoke_callback;
    VhdlVhpiForeignLifecycle stop_callback;
    {
      std::scoped_lock lock{mutex_};
      auto selected = registrations_.end();
      for (auto position = registrations_.begin();
           position != registrations_.end(); ++position) {
        const auto& entry = position->second;
        if (!entry.start && !entry.invoke && !entry.stop) {
          continue;
        }
        if (selected == registrations_.end()
            || entry.descriptor.ordinal
                < selected->second.descriptor.ordinal) {
          selected = position;
        }
      }
      if (selected == registrations_.end()) {
        return;
      }
      selected->second.descriptor.state =
          VhdlVhpiForeignState::Unregistered;
      start_callback = std::move(selected->second.start);
      invoke_callback = std::move(selected->second.invoke);
      stop_callback = std::move(selected->second.stop);
    }
    start_callback = {};
    invoke_callback = {};
    stop_callback = {};
  }
}

}  // namespace fsim::runtime
