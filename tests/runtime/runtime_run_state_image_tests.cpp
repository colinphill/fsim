// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/run_state_image.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require_image(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

void test_run_state_image() {
  using fsim::runtime::RunStateImage;
  using fsim::runtime::RunStateImageError;
  using fsim::runtime::RunStateImageKind;
  using fsim::runtime::RunStateReader;
  using fsim::runtime::RunStateWriter;

  RunStateImage base{"design-a"};
  std::string values;
  RunStateWriter{values}.words({1U, 2U, 3U});
  base.put("kernel.values", 1U, values);
  base.put("host.files", 1U, "files");
  base.put("app.heap", 2U, "heap");
  require_image(base.sections().size() == 3U &&
                    base.sections().front().name == "app.heap",
                "run-state sections were not kept in name order");

  // Round trip, including the hashes.
  RunStateImageError error{};
  const auto bytes = base.serialize();
  const auto parsed = RunStateImage::parse(bytes, error);
  require_image(parsed && error == RunStateImageError::none &&
                    parsed->state_hash() == base.state_hash() &&
                    parsed->identity() == "design-a" &&
                    parsed->find("host.files") != nullptr &&
                    parsed->find("host.files")->bytes == "files",
                "run-state image did not round-trip");
  std::vector<std::uint64_t> words;
  require_image(RunStateReader{parsed->find("kernel.values")->bytes}.words(
                    words) &&
                    words == std::vector<std::uint64_t>({1U, 2U, 3U}),
                "run-state words did not round-trip");

  // A flipped byte, a truncation and a foreign file are refused.
  auto damaged = bytes;
  damaged[damaged.size() / 2U] =
      static_cast<char>(damaged[damaged.size() / 2U] ^ 0x40);
  require_image(!RunStateImage::parse(damaged, error) &&
                    error == RunStateImageError::corrupt,
                "a damaged run-state image was accepted");
  require_image(!RunStateImage::parse(bytes.substr(0U, bytes.size() - 3U),
                                      error),
                "a truncated run-state image was accepted");
  require_image(!RunStateImage::parse("not an image at all", error) &&
                    error == RunStateImageError::malformed,
                "a foreign file was accepted as a run-state image");

  // $incsave: only the sections whose hash changed since the last full
  // save.
  RunStateImage later{"design-a"};
  later.put("kernel.values", 1U, "changed");
  later.put("host.files", 1U, "files");
  later.put("host.mailboxes", 1U, "new");
  const auto increment = later.incremental_since(base);
  require_image(increment.kind() == RunStateImageKind::incremental &&
                    increment.sections().size() == 2U &&
                    increment.find("host.files") == nullptr &&
                    increment.find("kernel.values") != nullptr &&
                    increment.find("host.mailboxes") != nullptr &&
                    increment.removed() ==
                        std::vector<std::string>{"app.heap"} &&
                    increment.base_hash() == base.state_hash(),
                "incremental run-state image holds the wrong sections");
  const auto reparsed = RunStateImage::parse(increment.serialize(), error);
  require_image(reparsed && reparsed->kind() == RunStateImageKind::incremental &&
                    reparsed->base_hash() == base.state_hash(),
                "incremental run-state image did not round-trip");

  auto restored = base;
  require_image(restored.apply(*reparsed) == RunStateImageError::none &&
                    restored.state_hash() == later.state_hash(),
                "applying an increment did not give the later state");
  require_image(restored.apply(*reparsed) == RunStateImageError::base_mismatch,
                "an increment applied to a state that is not its base");
  require_image(restored.apply(base) == RunStateImageError::wrong_kind,
                "a full image was applied as an increment");
  RunStateImage other{"design-b"};
  require_image(other.apply(increment) == RunStateImageError::identity_mismatch,
                "an increment crossed designs");
}

} // namespace fsim::tests::runtime
