// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_object.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {

namespace {

void require_vhpi_hierarchy(
    const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<fsim_vhpi_handle_v1> scan_all(
    fsim::runtime::VhdlVhpiObjectRegistry& registry,
    const fsim_vhpi_handle_v1 iterator) {
  using fsim::runtime::VhdlVhpiIteratorError;
  std::vector<fsim_vhpi_handle_v1> result;
  for (;;) {
    const auto scan = registry.scan(iterator);
    if (scan.error == VhdlVhpiIteratorError::Exhausted) {
      break;
    }
    require_vhpi_hierarchy(
        static_cast<bool>(scan), "VHPI relationship scan failed");
    result.push_back(scan.value);
  }
  require_vhpi_hierarchy(
      registry.release_iterator(iterator) == VhdlVhpiIteratorError::None,
      "VHPI relationship iterator release failed");
  return result;
}

}  // namespace

void test_vhdl_vhpi_hierarchy_and_names() {
  using fsim::runtime::VhdlVhpiIteratorError;
  using fsim::runtime::VhdlVhpiObjectDescriptor;
  using fsim::runtime::VhdlVhpiObjectError;
  using fsim::runtime::VhdlVhpiObjectKind;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiRelationshipKind;
  using fsim::runtime::VhdlVhpiSourceLocation;

  VhdlVhpiObjectRegistry registry{401};
  VhdlVhpiObjectRegistry other_registry{402};
  VhdlVhpiSourceLocation root_source{"design/top.vhd", 2, 1};
  const auto root = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Root, 0, "WORK", {}, root_source});
  const auto other_root =
      other_registry.create_object(VhdlVhpiObjectDescriptor{
          VhdlVhpiObjectKind::Root, 0, "other", {}, std::nullopt});
  require_vhpi_hierarchy(
      root && other_root, "VHPI named roots are created independently");

  const auto entity = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Entity,
      root.value,
      "Top",
      {},
      VhdlVhpiSourceLocation{"design/top.vhd", 5, 3}});
  const std::array<std::int64_t, 2> generate_indices{3, -1};
  const auto generate = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Generate,
      entity.value,
      "Gen",
      generate_indices,
      VhdlVhpiSourceLocation{"design/top.vhd", 12, 7}});
  const std::array<std::int64_t, 1> signal_indices{7};
  const auto signal = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Signal,
      entity.value,
      "\\Data.Bus\\",
      signal_indices,
      VhdlVhpiSourceLocation{"design/top.vhd", 18, 9}});
  const auto block = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Block,
      entity.value,
      "Body",
      {},
      VhdlVhpiSourceLocation{"design/top.vhd", 22, 5}});
  const auto variable = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Variable,
      entity.value,
      "Value",
      {},
      VhdlVhpiSourceLocation{"design/top.vhd", 24, 11}});
  require_vhpi_hierarchy(
      entity && generate && signal && block && variable,
      "VHPI hierarchy accepts regions, declarations, and indexed names");

  root_source.file.assign("changed");
  const auto root_metadata = registry.lookup_object(root.value);
  const auto generate_metadata = registry.lookup_object(generate.value);
  const auto signal_metadata = registry.lookup_object(signal.value);
  require_vhpi_hierarchy(
      root_metadata && root_metadata.value.name == "work"
          && root_metadata.value.selected_name == "work"
          && root_metadata.value.full_name == "work"
          && root_metadata.value.source
          && root_metadata.value.source->file == "design/top.vhd"
          && generate_metadata
          && generate_metadata.value.name == "gen"
          && generate_metadata.value.selected_name == "gen(3,-1)"
          && generate_metadata.value.full_name == "work.top.gen(3,-1)"
          && generate_metadata.value.indices
              == std::vector<std::int64_t>{3, -1}
          && signal_metadata
          && signal_metadata.value.name == "\\Data.Bus\\"
          && signal_metadata.value.selected_name == "\\Data.Bus\\(7)"
          && signal_metadata.value.full_name
              == "work.top.\\Data.Bus\\(7)"
          && signal_metadata.value.source
          && signal_metadata.value.source->line == 18
          && signal_metadata.value.source->column == 9,
      "VHPI metadata owns canonical selected/full names and source records");

  require_vhpi_hierarchy(
      registry.find("WoRk.ToP.Gen(3, -1)").value.handle == generate.value
          && registry.find("WORK.TOP.\\Data.Bus\\(7)").value.handle
              == signal.value
          && registry.find_child(
                 entity.value, "gEn", generate_indices)
                 .value.handle
              == generate.value
          && registry.find_child(
                 entity.value, "\\Data.Bus\\", signal_indices)
                 .value.handle
              == signal.value,
      "VHPI lookup canonicalizes basic selected names and exact indices");
  require_vhpi_hierarchy(
      registry.find("work.top.\\data.Bus\\(7)").error
              == VhdlVhpiObjectError::NotFound
          && registry.find("work..top").error
              == VhdlVhpiObjectError::InvalidName
          && registry.find("work.top.missing").error
              == VhdlVhpiObjectError::NotFound,
      "VHPI lookup preserves extended-name case and diagnoses malformed names");

  require_vhpi_hierarchy(
      registry.create_object(VhdlVhpiObjectDescriptor{
                   VhdlVhpiObjectKind::Root,
                   0,
                   "work",
                   {},
                   std::nullopt})
              .error
          == VhdlVhpiObjectError::DuplicateName,
      "VHPI root identity is case-insensitively unique");
  require_vhpi_hierarchy(
      registry.create_object(VhdlVhpiObjectDescriptor{
                   VhdlVhpiObjectKind::Signal,
                   entity.value,
                   "\\Data.Bus\\",
                   signal_indices,
                   std::nullopt})
              .error
          == VhdlVhpiObjectError::DuplicateName,
      "VHPI indexed sibling identity rejects exact duplicates");
  const std::array<std::int64_t, 1> other_index{8};
  const auto distinct_index = registry.create_object(VhdlVhpiObjectDescriptor{
      VhdlVhpiObjectKind::Signal,
      entity.value,
      "\\Data.Bus\\",
      other_index,
      std::nullopt});
  require_vhpi_hierarchy(
      distinct_index
          && registry.release_object(distinct_index.value)
              == VhdlVhpiObjectError::None,
      "VHPI indexed sibling identity distinguishes declared indices");

  for (const std::string invalid_name :
      {"3bad", "bad__name", "bad_", "\\unterminated"}) {
    require_vhpi_hierarchy(
        registry.create_object(VhdlVhpiObjectDescriptor{
                     VhdlVhpiObjectKind::Signal,
                     entity.value,
                     invalid_name,
                     {},
                     std::nullopt})
                .error
            == VhdlVhpiObjectError::InvalidName,
        "VHPI malformed identifier was accepted");
  }
  require_vhpi_hierarchy(
      registry.create_object(VhdlVhpiObjectDescriptor{
                   VhdlVhpiObjectKind::Signal,
                   entity.value,
                   "bad_source",
                   {},
                   VhdlVhpiSourceLocation{"", 0, 0}})
              .error
          == VhdlVhpiObjectError::InvalidSource,
      "VHPI malformed source metadata is rejected");
  const auto anonymous_root =
      registry.create_object(VhdlVhpiObjectKind::Root);
  require_vhpi_hierarchy(
      anonymous_root
          && registry.create_object(VhdlVhpiObjectDescriptor{
                         VhdlVhpiObjectKind::Entity,
                         anonymous_root.value,
                         "nested",
                         {},
                         std::nullopt})
                     .error
              == VhdlVhpiObjectError::InvalidParent,
      "VHPI named hierarchy rejects an anonymous parent");

  const auto children = registry.iterate_relationship(
      entity.value, VhdlVhpiRelationshipKind::Children);
  const auto regions = registry.iterate_relationship(
      entity.value, VhdlVhpiRelationshipKind::Regions);
  const auto declarations = registry.iterate_relationship(
      entity.value, VhdlVhpiRelationshipKind::Declarations);
  require_vhpi_hierarchy(
      children && regions && declarations,
      "VHPI checked relationship iterators are created");
  require_vhpi_hierarchy(
      scan_all(registry, children.value)
              == std::vector<fsim_vhpi_handle_v1>{
                  generate.value, signal.value, block.value, variable.value}
          && scan_all(registry, regions.value)
              == std::vector<fsim_vhpi_handle_v1>{
                  generate.value, block.value}
          && scan_all(registry, declarations.value)
              == std::vector<fsim_vhpi_handle_v1>{
                  signal.value, variable.value},
      "VHPI relationships preserve creation order and region classification");
  require_vhpi_hierarchy(
      registry.iterate_relationship(
                   entity.value,
                   static_cast<VhdlVhpiRelationshipKind>(99))
              .error
              == VhdlVhpiIteratorError::InvalidRelationship
          && registry.iterate_relationship(
                 other_root.value, VhdlVhpiRelationshipKind::Children)
                 .error
              == VhdlVhpiIteratorError::CrossSimulation,
      "VHPI relationship iteration rejects unknown and cross-root requests");

  require_vhpi_hierarchy(
      registry.release_object(generate.value) == VhdlVhpiObjectError::None
          && registry.release_object(signal.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(block.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(variable.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(entity.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(root.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(anonymous_root.value)
              == VhdlVhpiObjectError::None,
      "VHPI named hierarchy releases leaf-first and removes lookup identity");
}

}  // namespace fsim::tests::runtime
