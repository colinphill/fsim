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
  using fsim::runtime::VhdlVhpiPackageProvenance;
  using fsim::runtime::VhdlVhpiObjectRegistry;
  using fsim::runtime::VhdlVhpiPropertyKind;
  using fsim::runtime::VhdlVhpiPropertyValueKind;
  using fsim::runtime::VhdlVhpiRelationshipKind;
  using fsim::runtime::VhdlVhpiSourceLocation;

  VhdlVhpiObjectRegistry registry{401};
  VhdlVhpiObjectRegistry other_registry{402};
  VhdlVhpiSourceLocation root_source{"design/top.vhd", 2, 1};
  const std::vector<VhdlVhpiPackageProvenance> root_packages {
      { "1993", "ieee-1076-standard:1993:fsim-v3",
          "ieee.std_logic_unsigned",
          "synopsys-legacy-ieee:1990-1992:"
          "fsim-synopsys-ieee-compat-v2:std_logic_unsigned:vhdl-1993",
          std::string(64U, 'a') }
  };
  VhdlVhpiObjectDescriptor root_descriptor {
      VhdlVhpiObjectKind::Root, 0, "WORK", {}, root_source
  };
  root_descriptor.language_standard = "1993";
  root_descriptor.predefined_environment
      = "ieee-1076-standard:1993:fsim-v3";
  root_descriptor.compatibility_profile
      = "fsim-synopsys-ieee-compat-v2";
  root_descriptor.package_dependencies = root_packages;
  const auto root = registry.create_object(root_descriptor);
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
          && root_metadata.value.language_standard == "1993"
          && root_metadata.value.predefined_environment
              == "ieee-1076-standard:1993:fsim-v3"
          && root_metadata.value.compatibility_profile
              == "fsim-synopsys-ieee-compat-v2"
          && root_metadata.value.package_dependencies.size() == 1U
          && root_metadata.value.package_dependencies.front().package
              == "ieee.std_logic_unsigned"
          && root_metadata.value.package_dependencies.front().revision
              == root_packages.front().revision
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

  const auto signal_kind = registry.property(
      signal.value, VhdlVhpiPropertyKind::ObjectKind);
  const auto signal_parent = registry.property(
      signal.value, VhdlVhpiPropertyKind::Parent);
  const auto signal_name = registry.property(
      signal.value, VhdlVhpiPropertyKind::Name);
  const auto signal_full_name = registry.property(
      signal.value, VhdlVhpiPropertyKind::FullName);
  const auto signal_index_count = registry.property(
      signal.value, VhdlVhpiPropertyKind::IndexCount);
  const auto signal_source = registry.property(
      signal.value, VhdlVhpiPropertyKind::SourceFile);
  const auto signal_line = registry.property(
      signal.value, VhdlVhpiPropertyKind::SourceLine);
  const auto root_standard = registry.property(
      root.value, VhdlVhpiPropertyKind::LanguageStandard);
  const auto root_environment = registry.property(
      root.value, VhdlVhpiPropertyKind::PredefinedEnvironment);
  const auto root_compatibility = registry.property(
      root.value, VhdlVhpiPropertyKind::CompatibilityProfile);
  const auto root_package_count = registry.property(
      root.value, VhdlVhpiPropertyKind::PackageDependencyCount);
  require_vhpi_hierarchy(
      signal_kind
          && signal_kind.kind == VhdlVhpiPropertyValueKind::ObjectKind
          && signal_kind.object_kind == VhdlVhpiObjectKind::Signal
          && signal_parent
          && signal_parent.kind == VhdlVhpiPropertyValueKind::Handle
          && signal_parent.handle == entity.value && signal_name
          && signal_name.kind == VhdlVhpiPropertyValueKind::String
          && signal_name.string == "\\Data.Bus\\" && signal_full_name
          && signal_full_name.string == "work.top.\\Data.Bus\\(7)"
          && signal_index_count && signal_index_count.unsigned_integer == 1U
          && signal_source && signal_source.string == "design/top.vhd"
          && signal_line && signal_line.unsigned_integer == 18U
          && root_standard && root_standard.string == "1993"
          && root_environment
          && root_environment.string == "ieee-1076-standard:1993:fsim-v3"
          && root_compatibility
          && root_compatibility.string == "fsim-synopsys-ieee-compat-v2"
          && root_package_count && root_package_count.unsigned_integer == 1U,
      "VHPI checked properties preserve kind, identity, source, and provenance");
  require_vhpi_hierarchy(
      registry.property(entity.value, VhdlVhpiPropertyKind::LanguageStandard)
              .error
              == VhdlVhpiObjectError::NotFound
          && registry.property(
                 entity.value, static_cast<VhdlVhpiPropertyKind>(999U))
                 .error
              == VhdlVhpiObjectError::InvalidProperty
          && registry.property(other_root.value, VhdlVhpiPropertyKind::Name)
                 .error
              == VhdlVhpiObjectError::CrossSimulation,
      "VHPI property access diagnoses absent, unknown, and foreign properties");

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
  VhdlVhpiObjectDescriptor invalid_provenance {
      VhdlVhpiObjectKind::Root, 0, "bad_provenance"
  };
  invalid_provenance.language_standard = "1993";
  require_vhpi_hierarchy(
      registry.create_object(invalid_provenance).error
          == VhdlVhpiObjectError::InvalidProvenance,
      "VHPI partial provenance metadata is rejected");
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

  const auto interface_view = registry.create_object(
      VhdlVhpiObjectDescriptor{VhdlVhpiObjectKind::InterfaceView,
          entity.value, "bus_view", {},
          VhdlVhpiSourceLocation{"design/top.vhd", 30, 3}});
  const auto view_element = registry.create_object(
      VhdlVhpiObjectDescriptor{VhdlVhpiObjectKind::ViewElement,
          interface_view.value, "ready", {},
          VhdlVhpiSourceLocation{"design/top.vhd", 31, 5}});
  require_vhpi_hierarchy(
      interface_view && view_element
          && registry
                 .property(interface_view.value, VhdlVhpiPropertyKind::ObjectKind)
                 .object_kind
              == VhdlVhpiObjectKind::InterfaceView,
      "VHPI 2019 interface views and their elements have distinct object kinds");

  const auto children = registry.iterate_relationship(
      entity.value, VhdlVhpiRelationshipKind::Children);
  const auto regions = registry.iterate_relationship(
      entity.value, VhdlVhpiRelationshipKind::Regions);
  const auto declarations = registry.iterate_relationship(
      entity.value, VhdlVhpiRelationshipKind::Declarations);
  const auto view_parent = registry.iterate_relationship(
      view_element.value, VhdlVhpiRelationshipKind::Parent);
  const auto root_parent = registry.iterate_relationship(
      root.value, VhdlVhpiRelationshipKind::Parent);
  require_vhpi_hierarchy(
      children && regions && declarations && view_parent && root_parent,
      "VHPI checked relationship iterators are created");
  require_vhpi_hierarchy(
      scan_all(registry, children.value)
              == std::vector<fsim_vhpi_handle_v1>{
                  generate.value, signal.value, block.value, variable.value,
                  interface_view.value}
          && scan_all(registry, regions.value)
              == std::vector<fsim_vhpi_handle_v1>{
                  generate.value, block.value}
          && scan_all(registry, declarations.value)
              == std::vector<fsim_vhpi_handle_v1>{
                  signal.value, variable.value, interface_view.value}
          && scan_all(registry, view_parent.value)
              == std::vector<fsim_vhpi_handle_v1>{interface_view.value}
          && scan_all(registry, root_parent.value).empty(),
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
          && registry.release_object(view_element.value)
              == VhdlVhpiObjectError::None
          && registry.release_object(interface_view.value)
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
