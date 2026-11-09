// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/run_state_sections.hpp"
#include "run_state_archive.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {

void RunStateSectionRegistry::add(RunStateSectionOwner owner)
{
    if (find(owner.name) != nullptr) {
        throw std::logic_error { "run-state section registered twice: "
            + owner.name };
    }
    owners_.push_back(std::move(owner));
}

const RunStateSectionOwner* RunStateSectionRegistry::find(
    const std::string_view name) const noexcept
{
    const auto place = std::ranges::find(owners_, name, &RunStateSectionOwner::name);
    return place == owners_.end() ? nullptr : &*place;
}

bool save_section(RunStateImage& image, std::string name,
    const std::uint32_t version,
    const std::function<void(RunStateArchive&)>& visit,
    std::vector<std::string>& errors)
{
    std::string bytes;
    auto archive = RunStateArchive::writer(bytes);
    visit(archive);
    if (!archive.ok()) {
        errors.push_back("FSIM-SAVE-0005: section '" + name
            + "' cannot be written: " + archive.error());
        return false;
    }
    image.put(std::move(name), version, std::move(bytes));
    return true;
}

bool load_section(const RunStateImage& image, const std::string_view name,
    const std::uint32_t version,
    const std::function<void(RunStateArchive&)>& visit,
    std::vector<std::string>& errors)
{
    const auto* const section = image.find(name);
    if (section == nullptr) {
        errors.push_back("FSIM-SAVE-0008: the save file has no section '"
            + std::string { name } + "'");
        return false;
    }
    if (section->version != version) {
        errors.push_back("FSIM-SAVE-0009: section '" + std::string { name }
            + "' has version " + std::to_string(section->version)
            + " but this build reads version " + std::to_string(version));
        return false;
    }
    auto archive = RunStateArchive::reader(section->bytes);
    visit(archive);
    if (!archive.ok() || !archive.finished()) {
        errors.push_back("FSIM-SAVE-0010: section '" + std::string { name }
            + "' does not fit this run: "
            + (archive.ok() ? std::string { "trailing bytes" } : archive.error()));
        return false;
    }
    return true;
}

bool RunStateSectionRegistry::audit(std::vector<std::string>& errors) const {
  const auto before = errors.size();
  for (const auto& owner : owners_) {
    if (owner.audit) {
      owner.audit(errors);
    }
  }
  return errors.size() == before;
}

void RunStateSectionRegistry::clear_all() const {
  for (const auto& owner : owners_) {
    if (owner.clear) {
      owner.clear();
    }
  }
}

bool RunStateSectionRegistry::save(
    RunStateImage& image, std::vector<std::string>& errors) const
{
    bool all = true;
    for (const auto& owner : owners_) {
        if (owner.present && !owner.present()) {
            continue;
        }
        if (owner.refuse) {
            if (const auto reason = owner.refuse(); !reason.empty()) {
                errors.push_back("FSIM-SAVE-0021: " + owner.name
                    + " cannot be saved: " + reason);
                all = false;
                continue;
            }
        }
        if (!owner.save) {
            continue;
        }
        all = save_section(image, owner.name, owner.version, owner.save, errors)
            && all;
    }
    return all;
}

bool RunStateSectionRegistry::restore(
    const RunStateImage& image, std::vector<std::string>& errors) const
{
    for (const auto& owner : owners_) {
        if (!owner.restore && !owner.clear) {
            continue;
        }
        if ((!owner.required || owner.present)
            && image.find(owner.name) == nullptr) {
            // The saved run did not have this subsystem's state.
            if (owner.clear) {
                owner.clear();
            }
            continue;
        }
        if (!owner.restore) {
            continue;
        }
        if (!load_section(image, owner.name, owner.version, owner.restore, errors)) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> RunStateSectionRegistry::unclaimed(
    const RunStateImage& image) const
{
    std::vector<std::string> names;
    for (const auto& section : image.sections()) {
        if (find(section.name) == nullptr) {
            names.push_back(section.name);
        }
    }
    return names;
}

} // namespace fsim::runtime
