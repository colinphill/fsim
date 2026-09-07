// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>

namespace {

struct VclLink {
  fsim_acc_handle_context_v3* context{};
  std::uint64_t simulation_identity{};
  std::uint64_t hierarchy_generation{};
  std::uint64_t object{};
  handle acc_object{};
  PLI_INT32 type{};
  PLI_INT32 full_type{};
  PLI_INT32 width{};
  PLI_INT32 (*consumer)(p_vc_record){};
  PLI_BYTE8* user_data{};
  PLI_INT32 flags{};
  bool canceling{};
  std::uint32_t in_flight{};
  std::uint64_t last_sequence{};
};

std::mutex link_mutex;
std::condition_variable link_condition;
std::map<std::uint64_t, VclLink> links;
std::uint64_t next_link_identity{1};
thread_local std::uint64_t dispatching_link{};

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] bool checked_bytes(
    const void* const pointer, const std::size_t size,
    const fsim::runtime::TfNativePointerAccess access) noexcept {
  return pointer != nullptr && size != 0 &&
         fsim::runtime::validate_tf_native_pointer(pointer, size, access) ==
             fsim::runtime::TfContainmentError::None;
}

[[nodiscard]] bool resolve_metadata(
    const handle object, std::uint64_t& vpi,
    fsim_acc_read_result_v3& metadata) noexcept {
  vpi = fsim_acc_handle_to_vpi_v3(object);
  const auto* const active = fsim::runtime::acc_detail::current_context();
  if (vpi == 0 || active == nullptr) return false;
  fsim_acc_read_query_v3 query{};
  query.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = FSIM_ACC_READ_OBJECT;
  query.object = vpi;
  metadata = {};
  metadata.abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  metadata.struct_size = sizeof(metadata);
  try {
    PLI_INT32 current_type{};
    return active->read(active->user_data, &query, &metadata) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           metadata.abi_version == FSIM_ACC_READ_QUERY_ABI_VERSION &&
           metadata.struct_size >= sizeof(metadata) && metadata.reserved == 0 &&
           metadata.type > 0 && metadata.full_type > 0 && metadata.width > 0 &&
           metadata.width <=
               static_cast<PLI_INT32>(FSIM_ACC_READ_MAXIMUM_BITS) &&
           active->resolve(active->user_data, vpi, &current_type) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           current_type == metadata.full_type;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool is_net(const PLI_INT32 type) noexcept {
  return type == accNet || type == accNetBit || type == accWire ||
         (type >= accWand && type <= accSupply1);
}

[[nodiscard]] bool is_register(const PLI_INT32 type) noexcept {
  return type == accReg || type == accRegBit || type == accIntegerVar ||
         type == accRealVar || type == accTimeVar;
}

[[nodiscard]] bool is_port(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accPort);
}

[[nodiscard]] bool valid_target(const fsim_acc_read_result_v3& metadata,
                                const PLI_INT32 flags) noexcept {
  const bool observable = is_net(metadata.type) || is_net(metadata.full_type) ||
                          is_register(metadata.type) ||
                          is_register(metadata.full_type) ||
                          is_port(metadata.type) || is_port(metadata.full_type) ||
                          metadata.type == accNamedEvent ||
                          metadata.full_type == accNamedEvent;
  if (!observable) return false;
  if (flags == vcl_verilog_logic) return true;
  if (flags != vcl_verilog_strength) return false;
  return is_net(metadata.type) || is_net(metadata.full_type) ||
         is_port(metadata.type) || is_port(metadata.full_type);
}

[[nodiscard]] bool valid_event_shape(const fsim_acc_vcl_event_v3& event,
                                     const VclLink& link) noexcept {
  if (event.reason < logic_value_change ||
      event.reason > realtime_value_change || event.logic_value > accZ ||
      event.strength1 > vclSupply || event.strength2 > vclSupply) {
    return false;
  }
  switch (event.reason) {
    case logic_value_change:
    case event_value_change:
      return link.flags == vcl_verilog_logic && event.vector_width == 0 &&
             event.logic_word_count == 0 && event.logic_words == nullptr;
    case strength_value_change:
      return link.flags == vcl_verilog_strength && event.vector_width == 0 &&
             event.logic_word_count == 0 && event.logic_words == nullptr;
    case real_value_change:
    case realtime_value_change:
      return link.flags == vcl_verilog_logic && std::isfinite(event.real_value) &&
             event.vector_width == 0 && event.logic_word_count == 0 &&
             event.logic_words == nullptr;
    case vector_value_change:
    case integer_value_change:
    case time_value_change:
    case sregister_value_change:
    case vregister_value_change: {
      const auto words = static_cast<std::uint32_t>((link.width + 31) / 32);
      return link.flags == vcl_verilog_logic &&
             event.vector_width == static_cast<std::uint32_t>(link.width) &&
             event.logic_word_count == words &&
             checked_bytes(event.logic_words,
                           static_cast<std::size_t>(words) *
                               sizeof(*event.logic_words),
                           fsim::runtime::TfNativePointerAccess::Read);
    }
    default: return false;
  }
}

[[nodiscard]] s_vc_record make_record(const fsim_acc_vcl_event_v3& event,
                                      const VclLink& link) noexcept {
  s_vc_record record{};
  record.vc_reason = event.reason;
  record.vc_hightime = event.time_high;
  record.vc_lowtime = event.time_low;
  record.user_data = link.user_data;
  switch (event.reason) {
    case logic_value_change:
    case event_value_change:
      record.out_value.logic_value =
          static_cast<PLI_UBYTE8>(event.logic_value);
      break;
    case strength_value_change:
      record.out_value.strengths_s.logic_value =
          static_cast<PLI_UBYTE8>(event.logic_value);
      record.out_value.strengths_s.strength1 =
          static_cast<PLI_UBYTE8>(event.strength1);
      record.out_value.strengths_s.strength2 =
          static_cast<PLI_UBYTE8>(event.strength2);
      break;
    case real_value_change:
    case realtime_value_change:
      record.out_value.real_value = event.real_value;
      break;
    default: record.out_value.vector_handle = link.acc_object; break;
  }
  return record;
}

}  // namespace

extern "C" {

void acc_vcl_add(const handle object,
                 PLI_INT32 (*const consumer)(p_vc_record),
                 PLI_BYTE8* const user_data, const PLI_INT32 flags) {
  std::uint64_t vpi{};
  fsim_acc_read_result_v3 metadata{};
  const auto* const active_const =
      fsim::runtime::acc_detail::current_context();
  if (active_const == nullptr ||
      fsim::runtime::validate_tf_callback_pointer(consumer) !=
          fsim::runtime::TfContainmentError::None ||
      !resolve_metadata(object, vpi, metadata) ||
      !valid_target(metadata, flags)) {
    publish_error(true);
    return;
  }
  auto* const active = const_cast<fsim_acc_handle_context_v3*>(active_const);
  std::uint64_t identity{};
  {
    std::scoped_lock lock{link_mutex};
    if (links.size() >= FSIM_ACC_VCL_MAXIMUM_LINKS ||
        next_link_identity == 0 ||
        next_link_identity == std::numeric_limits<std::uint64_t>::max()) {
      publish_error(true);
      return;
    }
    for (const auto& [unused, link] : links) {
      static_cast<void>(unused);
      if (!link.canceling && link.context == active &&
          link.acc_object == object && link.consumer == consumer &&
          link.user_data == user_data && link.flags == flags) {
        publish_error(true);
        return;
      }
    }
    identity = next_link_identity++;
    try {
      links.emplace(identity,
                    VclLink{active,
                            active->simulation_identity,
                            active->hierarchy_generation,
                            vpi,
                            object,
                            metadata.type,
                            metadata.full_type,
                            metadata.width,
                            consumer,
                            user_data,
                            flags,
                            false,
                            0,
                            0});
    } catch (...) {
      publish_error(true);
      return;
    }
  }

  fsim_acc_vcl_query_v3 query{};
  query.abi_version = FSIM_ACC_VCL_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = FSIM_ACC_VCL_REGISTER;
  query.link_identity = identity;
  query.object = vpi;
  query.object_type = metadata.type;
  query.object_full_type = metadata.full_type;
  query.flags = flags;
  bool registered{};
  try {
    registered = active->vcl(active->user_data, &query) ==
                 FSIM_ACC_VPI_OBJECT_VALID;
  } catch (...) {
  }
  if (!registered) {
    std::scoped_lock lock{link_mutex};
    links.erase(identity);
    publish_error(true);
    return;
  }
  publish_error(false);
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fsim_acc_vcl_dispatch_v3(
    fsim_acc_handle_context_v3* const context,
    const fsim_acc_vcl_event_v3* const event) {
  if (context == nullptr ||
      context != fsim::runtime::acc_detail::current_context() ||
      !checked_bytes(event, sizeof(*event),
                     fsim::runtime::TfNativePointerAccess::Read) ||
      event->abi_version != FSIM_ACC_VCL_QUERY_ABI_VERSION ||
      event->struct_size < sizeof(*event) || event->reserved != 0 ||
      event->reserved2 != 0 || event->link_identity == 0) {
    publish_error(true);
    return 0;
  }
  VclLink link;
  {
    std::scoped_lock lock{link_mutex};
    const auto found = links.find(event->link_identity);
    if (found == links.end() || found->second.context != context ||
        found->second.simulation_identity != context->simulation_identity ||
        found->second.hierarchy_generation != context->hierarchy_generation ||
        found->second.canceling || found->second.in_flight != 0 ||
        event->sequence_identity == 0 ||
        event->sequence_identity <= found->second.last_sequence) {
      publish_error(true);
      return 0;
    }
    found->second.in_flight = 1;
    found->second.last_sequence = event->sequence_identity;
    link = found->second;
  }
  PLI_INT32 current_type{};
  try {
    if (context->resolve(context->user_data, link.object, &current_type) !=
            FSIM_ACC_VPI_OBJECT_VALID ||
        current_type != link.full_type || !valid_event_shape(*event, link)) {
      std::scoped_lock lock{link_mutex};
      const auto found = links.find(event->link_identity);
      if (found != links.end()) {
        found->second.in_flight = 0;
        if (found->second.canceling) links.erase(found);
      }
      link_condition.notify_all();
      publish_error(true);
      return 0;
    }
  } catch (...) {
    publish_error(true);
    return 0;
  }
  auto record = make_record(*event, link);
  bool callback_ok{true};
  const auto previous_dispatch = dispatching_link;
  dispatching_link = event->link_identity;
  try {
    static_cast<void>(link.consumer(&record));
  } catch (...) {
    callback_ok = false;
  }
  dispatching_link = previous_dispatch;
  {
    std::scoped_lock lock{link_mutex};
    const auto found = links.find(event->link_identity);
    if (found != links.end()) {
      found->second.in_flight = 0;
      if (found->second.canceling) links.erase(found);
    }
  }
  link_condition.notify_all();
  publish_error(!callback_ok);
  return callback_ok ? 1 : 0;
}

}  // extern "C"

namespace fsim::runtime::acc_detail {

bool cancel_vcl_link(const handle object,
                     PLI_INT32 (*const consumer)(p_vc_record),
                     PLI_BYTE8* const user_data,
                     const PLI_INT32 flags) noexcept {
  auto* const active = const_cast<fsim_acc_handle_context_v3*>(current_context());
  if (active == nullptr || object == nullptr ||
      validate_tf_callback_pointer(consumer) != TfContainmentError::None ||
      (flags != vcl_verilog_logic && flags != vcl_verilog_strength)) {
    return false;
  }
  std::uint64_t identity{};
  fsim_acc_vcl_query_v3 query{};
  {
    std::scoped_lock lock{link_mutex};
    for (auto& [candidate_identity, link] : links) {
      if (!link.canceling && link.context == active &&
          link.acc_object == object && link.consumer == consumer &&
          link.user_data == user_data && link.flags == flags) {
        identity = candidate_identity;
        link.canceling = true;
        query.abi_version = FSIM_ACC_VCL_QUERY_ABI_VERSION;
        query.struct_size = sizeof(query);
        query.operation = FSIM_ACC_VCL_UNREGISTER;
        query.link_identity = identity;
        query.object = link.object;
        query.object_type = link.type;
        query.object_full_type = link.full_type;
        query.flags = link.flags;
        break;
      }
    }
  }
  if (identity == 0) return false;
  bool unregistered{};
  try {
    unregistered = active->vcl(active->user_data, &query) ==
                   FSIM_ACC_VPI_OBJECT_VALID;
  } catch (...) {
  }
  if (!unregistered) {
    std::scoped_lock lock{link_mutex};
    const auto found = links.find(identity);
    if (found != links.end()) found->second.canceling = false;
    link_condition.notify_all();
    return false;
  }
  std::unique_lock lock{link_mutex};
  if (dispatching_link == identity) return true;
  link_condition.wait(lock, [identity] {
    const auto found = links.find(identity);
    return found == links.end() || found->second.in_flight == 0;
  });
  links.erase(identity);
  return true;
}

}  // namespace fsim::runtime::acc_detail
