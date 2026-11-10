// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <tuple>

namespace fsim::test {

// IEEE 1800-2023 4.7: the events of one region execute in an arbitrary
// order, so updates of different objects in one delta cycle have no
// LRM-defined relative order (a run that owns its signals in the compiled
// kernel may update them in another order than the interpreter). `key`
// returns (time, delta, object) for a change; the log is ordered by that key
// and each object keeps its own changes in recorded order, so a comparison
// still checks every object's full history, every time step and every delta
// cycle, only not the 4.7 order between objects inside one delta cycle.
template <typename Changes, typename Key>
void order_changes_by_delta_and_object(Changes& changes, Key key)
{
  std::ranges::stable_sort(changes, std::less<>{}, key);
}

// One change as a run recorded it, in the order the hook heard it, with the
// process whose write made it (Simulation::signal_change_writer; empty when
// no process wrote it).
struct WrittenChange {
  std::string signal;
  std::string value;
  std::uint64_t time{};
  std::uint64_t delta{};
  std::optional<std::uint32_t> writer;
};

// IEEE 1800-2023 4.6 a) and b): the statements of one begin-end block execute
// in order, and the nonblocking assignments one process executes update in
// that order, so two changes one process makes in one delta cycle come in the
// same order in every engine; changes of different processes in one region do
// not (4.7), and order_changes_by_delta_and_object leaves those out of the
// comparison. This checks the order the LRM fixes: for each pair of changes of
// `reference` in one (time, delta) with the same writer, the same two changes
// of `actual` (the same time, delta, object and value, matched by occurrence)
// come in the same order when `actual` names that writer for both. `compared`
// counts the pairs checked. On a difference it prints the pair and returns
// false.
inline bool same_writer_order_matches(const char* label,
    const std::vector<WrittenChange>& reference,
    const std::vector<WrittenChange>& actual, std::size_t& compared)
{
  using Identity = std::tuple<std::uint64_t, std::uint64_t, std::string,
      std::string, std::size_t>;
  const auto identities = [](const std::vector<WrittenChange>& changes) {
    std::map<std::tuple<std::uint64_t, std::uint64_t, std::string,
                 std::string>,
        std::size_t>
        seen;
    std::vector<Identity> result;
    result.reserve(changes.size());
    for (const auto& change : changes) {
      const auto occurrence
          = seen[{change.time, change.delta, change.signal, change.value}]++;
      result.emplace_back(change.time, change.delta, change.signal,
          change.value, occurrence);
    }
    return result;
  };
  const auto reference_identities = identities(reference);
  const auto actual_identities = identities(actual);
  std::map<Identity, std::size_t> actual_position;
  for (std::size_t index = 0U; index < actual_identities.size(); ++index) {
    actual_position.emplace(actual_identities[index], index);
  }
  std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint32_t>,
      std::vector<std::size_t>>
      groups;
  for (std::size_t index = 0U; index < reference.size(); ++index) {
    const auto& change = reference[index];
    if (change.writer) {
      groups[{change.time, change.delta, *change.writer}].push_back(index);
    }
  }
  const auto describe = [](const WrittenChange& change) {
    std::ostringstream out;
    out << change.signal << '=' << change.value;
    return out.str();
  };
  compared = 0U;
  for (const auto& [key, members] : groups) {
    const auto writer = std::get<2>(key);
    for (std::size_t first = 0U; first < members.size(); ++first) {
      const auto earlier = actual_position.find(
          reference_identities[members[first]]);
      if (earlier == actual_position.end()
          || actual[earlier->second].writer != writer) {
        continue;
      }
      for (auto second = first + 1U; second < members.size(); ++second) {
        const auto later = actual_position.find(
            reference_identities[members[second]]);
        if (later == actual_position.end()
            || actual[later->second].writer != writer) {
          continue;
        }
        ++compared;
        if (earlier->second > later->second) {
          std::cerr << label << ": process " << writer << " changed "
                    << describe(reference[members[first]]) << " before "
                    << describe(reference[members[second]]) << " @"
                    << std::get<0>(key) << '+' << std::get<1>(key)
                    << " in the reference, after it in the run\n";
          return false;
        }
      }
    }
  }
  return true;
}

// Prints the first difference between two change lists, so a failing
// equality assert says where the engines diverged.
template <typename Changes>
void report_change_mismatch(
    const char* label, const Changes& reference, const Changes& actual)
{
  if (reference == actual) {
    return;
  }
  const auto describe = [](const auto& change) {
    std::ostringstream out;
    if constexpr (requires { change.value; }) {
      if constexpr (requires { change.signal; }) {
        out << change.signal << '=';
      }
      out << change.value << " @" << change.time << '+' << change.delta;
    } else {
      out << std::get<0>(change) << '=' << std::get<1>(change) << " @"
          << std::get<2>(change) << '+' << std::get<3>(change);
    }
    return out.str();
  };
  std::size_t first = 0U;
  while (first < reference.size() && first < actual.size()
         && reference[first] == actual[first]) {
    ++first;
  }
  std::cerr << label << ": changes differ at " << first << " (reference "
            << reference.size() << ", actual " << actual.size() << ")\n";
  const auto from = first < 3U ? 0U : first - 3U;
  for (auto index = from; index < first + 6U; ++index) {
    std::cerr << "  [" << index << "] "
              << (index < reference.size() ? describe(reference[index])
                                           : std::string{"-"})
              << " | "
              << (index < actual.size() ? describe(actual[index])
                                        : std::string{"-"})
              << '\n';
  }
}

} // namespace fsim::test
