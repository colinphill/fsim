// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_tlm1.hpp"

#include <limits>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-TLM1-001"};
constexpr std::string_view kInvalidAnalysis{"FSIM-UVM-TLM1-007"};
constexpr std::string_view kAnalysisLimit{"FSIM-UVM-TLM1-008"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmTlm1Error{std::string{code}, std::string{message}};
}

}  // namespace

void SystemVerilogUvmTlm1Service::set_analysis_subscriber(
    const SystemVerilogUvmTlm1EndpointHandle implementation,
    AnalysisSubscriber subscriber) {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
          != SystemVerilogUvmTlm1EndpointKind::Implementation
      || selected.descriptor.profile.interface_kind
          != SystemVerilogUvmTlm1Interface::Analysis
      || !subscriber) {
    fail(kInvalidAnalysis, "invalid UVM TLM1 analysis subscriber");
  }
  analysis_subscribers_.insert_or_assign(
      implementation.slot_, std::move(subscriber));
}

void SystemVerilogUvmTlm1Service::configure_analysis_fifo(
    const SystemVerilogUvmTlm1EndpointHandle implementation,
    const std::size_t capacity) {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
          != SystemVerilogUvmTlm1EndpointKind::Implementation
      || selected.descriptor.profile.interface_kind
          != SystemVerilogUvmTlm1Interface::Analysis) {
    fail(kInvalidAnalysis, "analysis FIFO requires an analysis implementation");
  }
  configure_fifo(implementation, capacity);
}

SystemVerilogUvmTlm1EndpointHandle
SystemVerilogUvmTlm1Service::register_analysis_implementation(
    const SystemVerilogClassHandle component,
    std::string name,
    std::string nominal_type,
    AnalysisSubscriber subscriber) {
  if (!subscriber) {
    fail(kInvalidAnalysis, "macro-generated analysis implementation needs a subscriber");
  }
  SystemVerilogUvmTlm1EndpointDescriptor descriptor;
  descriptor.kind = SystemVerilogUvmTlm1EndpointKind::Implementation;
  descriptor.profile.interface_kind = SystemVerilogUvmTlm1Interface::Analysis;
  descriptor.profile.direction = SystemVerilogUvmTlm1Direction::Forward;
  descriptor.profile.request_type = std::move(nominal_type);
  descriptor.component = component;
  descriptor.name = std::move(name);
  descriptor.minimum_connections = 0;
  descriptor.maximum_connections = 0;
  const auto result = register_endpoint(std::move(descriptor));
  set_analysis_subscriber(result, std::move(subscriber));
  return result;
}

SystemVerilogUvmTlm1AnalysisResult
SystemVerilogUvmTlm1Service::write_analysis(
    const SystemVerilogUvmTlm1EndpointHandle source,
    SystemVerilogUvmTlm1Payload payload) {
  const auto& selected = endpoint(source);
  require_live(selected);
  if (!binding_valid()) {
    fail(kInvalidAnalysis, "analysis publication requires a stable binding");
  }
  if (selected.descriptor.kind
          == SystemVerilogUvmTlm1EndpointKind::Implementation
      || selected.descriptor.profile.interface_kind
          != SystemVerilogUvmTlm1Interface::Analysis) {
    fail(kInvalidAnalysis, "analysis publication requires a port or export");
  }
  if (analysis_depth_ >= limits_.maximum_analysis_recursion_depth
      || analysis_publications_ >= limits_.maximum_analysis_publications
      || next_analysis_publication_
          == std::numeric_limits<std::uint64_t>::max()) {
    fail(kAnalysisLimit, "UVM TLM1 analysis publication ceiling exceeded");
  }

  const auto targets = selected.resolved;
  if (targets.size() > limits_.maximum_analysis_callbacks_per_publication
          - std::min(
              analysis_callbacks_reserved_,
              limits_.maximum_analysis_callbacks_per_publication)
      || targets.size() > limits_.maximum_analysis_failures
      || targets.size()
          > std::numeric_limits<std::uint64_t>::max()
              - next_analysis_delivery_) {
    fail(kAnalysisLimit, "UVM TLM1 analysis callback or failure ceiling exceeded");
  }
  validate_payload(payload, selected, false);

  struct Target {
    SystemVerilogUvmTlm1EndpointHandle handle;
    AnalysisSubscriber subscriber;
    bool fifo{};
  };
  std::vector<Target> snapshot;
  snapshot.reserve(targets.size());
  for (const auto& target : targets) {
    const auto& implementation = endpoint(target);
    require_live(implementation);
    if (implementation.descriptor.kind
            != SystemVerilogUvmTlm1EndpointKind::Implementation
        || implementation.descriptor.profile.interface_kind
            != SystemVerilogUvmTlm1Interface::Analysis) {
      fail(kInvalidAnalysis, "analysis binding reaches a non-analysis endpoint");
    }
    const auto subscriber = analysis_subscribers_.find(target.slot_);
    snapshot.push_back({
        target,
        subscriber == analysis_subscribers_.end()
            ? AnalysisSubscriber{}
            : subscriber->second,
        fifos_.contains(target.slot_)});
  }

  SystemVerilogUvmTlm1AnalysisResult result;
  result.publication_sequence = next_analysis_publication_++;
  result.recursion_depth = ++analysis_depth_;
  ++analysis_publications_;
  analysis_callbacks_reserved_ += snapshot.size();
  try {
    for (const auto& target : snapshot) {
      const auto delivery_order = next_analysis_delivery_++;
      auto delivered = false;
      std::string failure;
      auto delivered_payload = payload;
      delivered_payload.owner_endpoint = target.handle;
      try {
        const auto& implementation = endpoint(target.handle);
        require_live(implementation);
        require_payload_type(delivered_payload, implementation, false);
        if (target.fifo) {
          auto& selected_fifo = fifo(target.handle.slot_);
          if (selected_fifo.values.size() >= selected_fifo.capacity) {
            failure = "analysis FIFO is full";
          } else if (queued_payload_count()
                     >= limits_.maximum_queued_payloads) {
            failure = "analysis queued-payload ceiling exceeded";
          } else {
            selected_fifo.values.push_back(delivered_payload);
            publish_fifo_activity(target.handle.slot_, "analysis-enqueue");
            delivered = true;
          }
        }
        if (target.subscriber) {
          target.subscriber(delivered_payload);
          delivered = true;
        } else if (!target.fifo) {
          failure = "analysis implementation has no subscriber or FIFO";
        }
      } catch (const SystemVerilogUvmTlm1Error& error) {
        failure = error.what();
      } catch (const std::exception& error) {
        failure = error.what();
      } catch (...) {
        failure = "analysis subscriber threw a non-standard exception";
      }
      if (delivered) {
        result.deliveries.push_back({
            target.handle, delivered_payload, delivery_order});
      }
      if (!failure.empty()) {
        result.failures.push_back({
            std::string{kInvalidAnalysis}, target.handle,
            std::move(failure), delivery_order});
      }
    }
  } catch (...) {
    analysis_callbacks_reserved_ -= snapshot.size();
    --analysis_depth_;
    throw;
  }
  analysis_callbacks_reserved_ -= snapshot.size();
  --analysis_depth_;
  return result;
}

std::optional<SystemVerilogUvmTlm1Payload>
SystemVerilogUvmTlm1Service::analysis_fifo_try_get(
    const SystemVerilogUvmTlm1EndpointHandle implementation) {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
          != SystemVerilogUvmTlm1EndpointKind::Implementation
      || selected.descriptor.profile.interface_kind
          != SystemVerilogUvmTlm1Interface::Analysis) {
    fail(kInvalidAnalysis, "analysis FIFO read requires an analysis implementation");
  }
  return implementation_try_read(implementation);
}

}  // namespace fsim::runtime
