// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal definition fragment included by simir.hpp inside
// fsim::runtime::simir after RegisterId and PackedLogic4 are available.

enum class ScopeRandomizeDomainKind : std::uint8_t {
  bit_vector,
  integer,
  enumeration,
};

struct ScopeRandomizeTarget {
  RegisterId target{};
  std::string canonical_identity;
  std::uint32_t width{};
  bool signed_value{};
  ScopeRandomizeDomainKind domain_kind{
      ScopeRandomizeDomainKind::bit_vector};
  std::string nominal_type;
  std::vector<PackedLogic4> domain;
};

/// An unpacked array, queue or associative array of packed elements in a
/// std::randomize call: element `i` is the solver variable
/// `<canonical_identity>[i]` and `<canonical_identity>[]` holds the count
/// (IEEE 1800-2017 18.12, 18.5.8.1). An argument's elements are randomized;
/// a calling-scope array the with-clause reads keeps its values.
struct ScopeRandomizeContainer {
  ContainerRegisterId container{};
  std::string canonical_identity;
  std::uint32_t element_width{};
  bool signed_value{};
  ScopeRandomizeDomainKind domain_kind{
      ScopeRandomizeDomainKind::bit_vector};
  std::string nominal_type;
  std::vector<PackedLogic4> domain;
  bool randomized{};
};

/// Jointly solve and transactionally publish one source std::randomize call.
/// Target registers hold copy-in values and are copied back to their source
/// lvalues by the ordinary callable-association path after this operation.
struct ScopeRandomize {
  RegisterId destination{};
  RareVector<ScopeRandomizeTarget> targets;
  RareVector<SystemVerilogConstraintTemplate> inline_constraints { };
  RareVector<ScopeRandomizeContainer> containers { };
  std::size_t maximum_domain_values{
      std::numeric_limits<std::size_t>::max()};
};
