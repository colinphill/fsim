// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/design_ir.hpp"

namespace fsim::semantic::design {
namespace {

template <typename IdType, typename Range>
[[nodiscard]] bool contains(const IdType id, const Range& range) noexcept {
    return id.valid() && id.value() < range.size();
}

} // namespace

const std::string& DesignIr::top() const noexcept { return top_; }
const std::vector<std::string>& DesignIr::roots() const noexcept {
    return roots_;
}
const std::vector<Specialization>&
DesignIr::specializations() const noexcept { return specializations_; }
const std::vector<InstanceOccurrence>&
DesignIr::instances() const noexcept { return instances_; }
const std::vector<Object>& DesignIr::objects() const noexcept {
    return objects_;
}
const std::vector<Port>& DesignIr::ports() const noexcept { return ports_; }
const std::vector<ProcessOccurrence>& DesignIr::processes() const noexcept {
    return processes_;
}
const std::vector<Sensitivity>& DesignIr::sensitivities() const noexcept {
    return sensitivities_;
}
const std::vector<Driver>& DesignIr::drivers() const noexcept {
    return drivers_;
}
const std::vector<Transaction>& DesignIr::transactions() const noexcept {
    return transactions_;
}
const std::vector<Conversion>& DesignIr::conversions() const noexcept {
    return conversions_;
}
const std::vector<Boundary>& DesignIr::boundaries() const noexcept {
    return boundaries_;
}

std::string& DesignIr::mutable_top() noexcept { return top_; }
std::vector<std::string>& DesignIr::mutable_roots() noexcept {
    return roots_;
}
std::vector<Specialization>& DesignIr::mutable_specializations() noexcept {
    return specializations_;
}
std::vector<InstanceOccurrence>& DesignIr::mutable_instances() noexcept {
    return instances_;
}
std::vector<Object>& DesignIr::mutable_objects() noexcept { return objects_; }
std::vector<Port>& DesignIr::mutable_ports() noexcept { return ports_; }
std::vector<ProcessOccurrence>& DesignIr::mutable_processes() noexcept {
    return processes_;
}
std::vector<Sensitivity>& DesignIr::mutable_sensitivities() noexcept {
    return sensitivities_;
}
std::vector<Driver>& DesignIr::mutable_drivers() noexcept { return drivers_; }
std::vector<Transaction>& DesignIr::mutable_transactions() noexcept {
    return transactions_;
}
std::vector<Conversion>& DesignIr::mutable_conversions() noexcept {
    return conversions_;
}
std::vector<Boundary>& DesignIr::mutable_boundaries() noexcept {
    return boundaries_;
}

bool DesignIr::valid() const noexcept {
    if (!roots_.empty() && top_ != roots_.front()) {
        return false;
    }
    for (std::size_t index = 0; index < specializations_.size(); ++index) {
        const auto& item = specializations_[index];
        if (item.id.value() != index || !contains(item.instance, instances_)) {
            return false;
        }
        for (const auto id : item.processes) {
            if (!contains(id, processes_)) {
                return false;
            }
        }
        for (const auto id : item.objects) {
            if (!contains(id, objects_)) {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < instances_.size(); ++index) {
        const auto& item = instances_[index];
        if (item.id.value() != index
            || !contains(item.specialization, specializations_)
            || (item.parent && !contains(*item.parent, instances_))) {
            return false;
        }
    }
    for (std::size_t index = 0; index < objects_.size(); ++index) {
        const auto& item = objects_[index];
        if (item.id.value() != index
            || !contains(item.specialization, specializations_)
            || (item.parent_object
                && !contains(*item.parent_object, objects_))) {
            return false;
        }
    }
    for (std::size_t index = 0; index < ports_.size(); ++index) {
        const auto& item = ports_[index];
        if (item.id.value() != index || !contains(item.instance, instances_)
            || !contains(item.object, objects_)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < processes_.size(); ++index) {
        const auto& item = processes_[index];
        if (item.id.value() != index
            || !contains(item.specialization, specializations_)) {
            return false;
        }
        for (const auto id : item.sensitivities) {
            if (!contains(id, sensitivities_)) {
                return false;
            }
        }
        for (const auto id : item.drivers) {
            if (!contains(id, drivers_)) {
                return false;
            }
        }
        for (const auto id : item.transactions) {
            if (!contains(id, transactions_)) {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < sensitivities_.size(); ++index) {
        const auto& item = sensitivities_[index];
        if (item.id.value() != index || !contains(item.process, processes_)
            || !contains(item.object, objects_)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < drivers_.size(); ++index) {
        const auto& item = drivers_[index];
        if (item.id.value() != index || !contains(item.process, processes_)
            || !contains(item.object, objects_)) {
            return false;
        }
        for (const auto id : item.transactions) {
            if (!contains(id, transactions_)) {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < transactions_.size(); ++index) {
        const auto& item = transactions_[index];
        if (item.id.value() != index || !contains(item.process, processes_)
            || !contains(item.driver, drivers_)
            || !contains(item.object, objects_)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < conversions_.size(); ++index) {
        const auto& item = conversions_[index];
        if (item.id.value() != index || !contains(item.formal, objects_)
            || !contains(item.actual, objects_)
            || (item.process && !contains(*item.process, processes_))) {
            return false;
        }
    }
    for (std::size_t index = 0; index < boundaries_.size(); ++index) {
        const auto& item = boundaries_[index];
        if (item.id.value() != index
            || (item.instance && !contains(*item.instance, instances_))
            || (item.object && !contains(*item.object, objects_))
            || (item.port && !contains(*item.port, ports_))
            || (item.process && !contains(*item.process, processes_))) {
            return false;
        }
        if (item.conversion && !contains(*item.conversion, conversions_)) {
            return false;
        }
    }
    return true;
}

bool DesignIr::valid(const semantic::Model& model) const noexcept {
    if (!valid()) {
        return false;
    }
    const auto valid_source = [&](const std::optional<SourceSpanId> source) {
        return !source || contains(*source, model.source_spans());
    };
    for (const auto& specialization : specializations_) {
        if (specialization.language == Language::systemc) {
            if (specialization.unit.valid() || specialization.scope.valid()) {
                return false;
            }
        } else if (!contains(specialization.unit, model.units())
                   || !contains(specialization.scope, model.scopes())) {
            return false;
        }
        if (!valid_source(specialization.source)) {
            return false;
        }
        for (const auto& parameter : specialization.parameters) {
            if (parameter.declaration
                && !contains(*parameter.declaration, model.declarations())) {
                return false;
            }
        }
        for (const auto callable : specialization.callables) {
            if (!contains(callable, model.declarations())) {
                return false;
            }
        }
    }
    for (const auto& instance : instances_) {
        if ((instance.source_instance
             && !contains(*instance.source_instance, model.instances()))
            || !valid_source(instance.source)
            || (instance.origin
                && !contains(*instance.origin, model.origins()))) {
            return false;
        }
    }
    for (const auto& object : objects_) {
        if ((object.declaration_value
             && !contains(*object.declaration_value, model.values()))
            || !valid_source(object.source)
            || (object.type.target.valid()
                && !contains(object.type.target, model.types()))
            || (object.type.source.valid()
                && !contains(object.type.source, model.source_spans()))) {
            return false;
        }
    }
    for (const auto& port : ports_) {
        if ((port.declaration
             && !contains(*port.declaration, model.declarations()))
            || !valid_source(port.source)) {
            return false;
        }
    }
    for (const auto& process : processes_) {
        if ((process.source_process
             && !contains(*process.source_process,
                          model.process_identities()))
            || !valid_source(process.source)) {
            return false;
        }
    }
    for (const auto& conversion : conversions_) {
        if (!valid_source(conversion.source)) {
            return false;
        }
    }
    for (const auto& boundary : boundaries_) {
        if (!valid_source(boundary.source)) {
            return false;
        }
    }
    return true;
}

} // namespace fsim::semantic::design
