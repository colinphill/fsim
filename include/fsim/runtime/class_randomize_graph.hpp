// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_randomize.hpp"

#include <map>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime {

/// One object reached from a randomized object through class-handle
/// properties (IEEE 1800-2017 18.5.9). Its variables join the root's solve
/// under `path` + canonical property identity; `path` is the dot-joined
/// canonical identities of the handle properties followed, each with a
/// trailing '.', so a constraint naming `h.m` binds to that variable.
struct SystemVerilogClassRandomizeMember {
  SystemVerilogClassHandle handle{};
  std::string path;
  /// Reached only through enabled rand handles: its own enabled random
  /// properties are randomized. Otherwise its properties are state.
  bool randomized{};
  /// The member's own class constraints; the variable view it receives is
  /// keyed relative to `path`.
  SystemVerilogClassConstraintConfigurator class_constraints;
  std::map<std::string, std::vector<PackedLogic4>, std::less<>>
      property_domains;
};

/// A value-container property (fixed-size unpacked array, dynamic array or
/// queue of packed values) of the root or of a member. Its elements join the
/// solve as `name[i]`; a random dynamic array or queue is resized first when
/// its `name.size()` is constrained, and keeps its size otherwise (IEEE
/// 1800-2017 18.4, 18.5.8.1).
struct SystemVerilogClassRandomizeContainer {
  /// 0 is the root; n is members[n - 1].
  std::size_t node{};
  /// The property's canonical identity in its object.
  std::string name;
  bool resizable{};
  /// The element's kind, packed width and random profile.
  SystemVerilogClassPropertyValue element;
  /// The current elements; the committed elements after a successful call.
  std::vector<PackedLogic4> values;
  /// Set when the call committed new elements.
  bool committed{};
  /// One container of the property: the property itself (empty `path`) or
  /// a nested one (`[1]`, `[1][0]`), with its element count and shape.
  struct Shape {
    std::string path;
    std::size_t size{};
    SystemVerilogConstraintContainerShape shape;
  };
  /// The property's containers in pre-order. A property whose elements are
  /// themselves containers (nested or multi-dimensional arrays) or which is
  /// associative lists each of them and names its element values by
  /// `element_paths` (`[1][0]`, by position); such a property keeps its
  /// sizes. Otherwise at most the property itself is listed and its
  /// elements are `[0]`, `[1]`, ...
  std::vector<Shape> shapes;
  std::vector<std::string> element_paths;
  /// A structure property (packed, or held as its packed image) joins the
  /// solve member by member: `element_paths` are its members (`.a`,
  /// `.b.c`), each with its own profile here; a member that is not random
  /// keeps its value (IEEE 1800-2017 7.2, 18.4).
  std::vector<SystemVerilogClassPropertyValue> element_profiles;
  /// A resizable array of structures: the member paths of one element
  /// (`.a`, `.b.c`) with their profiles for a new element. Its size is
  /// solved first when constrained; its elements are then `[i]` + path.
  std::vector<std::string> member_paths;
  std::vector<SystemVerilogClassPropertyValue> member_profiles;
};

/// The non-null objects reachable from `root` through class-handle
/// properties, in pre-order (parent before children), each once. The root
/// itself is not listed. Configurators and domains are left empty.
[[nodiscard]] std::vector<SystemVerilogClassRandomizeMember>
systemverilog_class_randomize_members(
    const SystemVerilogClassHeap& heap,
    SystemVerilogClassHandle root);

/// Randomize `root` together with `members` as one transaction: one solve
/// over every variable, all-or-nothing commit.
[[nodiscard]] SystemVerilogClassRandomizeResult
randomize_systemverilog_class_graph(
    SystemVerilogClassHeap& heap,
    SystemVerilogClassHandle root,
    const SystemVerilogClassRandomizeRequest& request,
    std::span<const SystemVerilogClassRandomizeMember> members,
    std::span<SystemVerilogClassRandomizeContainer> containers = {});

}  // namespace fsim::runtime
