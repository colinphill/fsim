// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <charconv>

namespace fsim::runtime::simir {
namespace {

std::optional<std::size_t> direction(
    const Logic9 previous,
    const Logic9 current) noexcept {
  const auto before = vital_x01_ordinal(previous);
  const auto after = vital_x01_ordinal(current);
  if (before == 1U && after == 2U) return 0U;
  if (before == 2U && after == 1U) return 1U;
  return std::nullopt;
}

bool too_soon(
    const std::optional<SimulationTick> previous,
    const SimulationTick now,
    const SimulationTick limit) noexcept {
  return limit != 0 && previous && now >= *previous
      && now - *previous < limit;
}

SimulationTick level_limit(
    const Logic9 value,
    const SimulationTick high,
    const SimulationTick low) noexcept {
  const auto level = vital_x01_ordinal(value);
  if (level == 2U) return high;
  if (level == 1U) return low;
  return std::max(high, low);
}

SimulationTick vital_transition_delay(
    const Logic9 previous,
    const Logic9 current,
    const VitalDelayShape shape,
    const std::array<SimulationTick, 6>& delays) noexcept {
  if (shape == VitalDelayShape::single) return delays[0];
  const auto before = vital_x01_ordinal(previous);
  const auto after = vital_x01_ordinal(current);
  const bool before_z = previous == Logic9::z;
  const bool after_z = current == Logic9::z;
  const auto rise = delays[0];
  const auto fall = delays[1];
  if (shape == VitalDelayShape::delay01) {
    if (after == 1U) return fall;
    if (after == 2U) return rise;
    if (before == 1U) return rise;
    if (before == 2U) return fall;
    if (before_z) return std::min(rise, fall);
    return std::max(rise, fall);
  }
  if (before == 1U && after == 2U) return rise;
  if (before == 2U && after == 1U) return fall;
  if (before == 1U && after_z) return delays[2];
  if (before_z && after == 2U) return delays[3];
  if (before == 2U && after_z) return delays[4];
  if (before_z && after == 1U) return delays[5];
  if (before == 1U) return after == 1U ? fall : std::min(rise, delays[2]);
  if (before == 2U) return after == 2U ? rise : std::min(fall, delays[4]);
  if (before_z) {
    if (after == 1U) return delays[5];
    if (after == 2U) return delays[3];
    return std::min(delays[3], delays[5]);
  }
  if (after == 1U) return std::max(fall, delays[5]);
  if (after == 2U) return std::max(rise, delays[3]);
  if (after_z) return std::max(delays[2], delays[4]);
  return std::max(rise, fall);
}

PackedLogic4 vital_scalar(const Logic9 value) {
  PackedLogic4 result(1U);
  result.fill(value);
  return result;
}

bool same_logic_value(
    const PackedLogic4& left,
    const PackedLogic4& right) {
  if (left.width() != right.width()) return false;
  for (std::size_t bit = 0; bit < left.width(); ++bit) {
    if (left.get_logic9(bit) != right.get_logic9(bit)) return false;
  }
  return true;
}

std::optional<std::uint16_t> memory_edge_symbol(const char symbol) {
  constexpr std::string_view symbols{"/\\PNrfpnRF^vEAD*"};
  const auto index = symbols.find(symbol);
  return index == std::string_view::npos
      ? std::nullopt
      : std::optional<std::uint16_t>{
            static_cast<std::uint16_t>(std::uint16_t{1U} << index)};
}

std::optional<bool> memory_control_matches(
    const char symbol,
    const Logic9 previous,
    const Logic9 current) {
  if (const auto selected = memory_edge_symbol(symbol)) {
    return vital_edge_symbol_matches(previous, current, *selected);
  }
  const auto before = vital_x01_ordinal(previous);
  const auto after = vital_x01_ordinal(current);
  switch (symbol) {
    case 'X': return after == 0U;
    case '0': return after == 1U;
    case '1': return after == 2U;
    case '-': return true;
    case 'B': return after != 0U;
    case 'S': return before == after && after != 0U;
    case 'Z': return std::nullopt;
    default: return std::nullopt;
  }
}

char memory_state_symbol(const VitalMemoryAddressState state) {
  switch (state) {
    case VitalMemoryAddressState::good: return 'g';
    case VitalMemoryAddressState::unknown: return 'u';
    case VitalMemoryAddressState::invalid: return 'i';
    case VitalMemoryAddressState::good_transition: return 'G';
    case VitalMemoryAddressState::unknown_transition: return 'U';
    case VitalMemoryAddressState::invalid_transition: return 'I';
  }
  return 'u';
}

std::optional<bool> memory_flag_matches(
    const char symbol,
    const VitalMemoryAddressState state) {
  const auto flag = memory_state_symbol(state);
  switch (symbol) {
    case 'g': case 'u': case 'i':
    case 'G': case 'U': case 'I': return symbol == flag;
    case '-': return true;
    case '*': return flag == 'G' || flag == 'U' || flag == 'I';
    case 'S': return flag == 'g' || flag == 'u' || flag == 'i';
    default: return std::nullopt;
  }
}

PackedLogic4 word_corruption_mask(
    const char action,
    const std::size_t word_width) {
  PackedLogic4 result{word_width, Logic4::zero};
  constexpr std::string_view corrupting{"cldeCLDE"};
  if (corrupting.find(action) != std::string_view::npos) {
    result.fill(Logic4::x);
  }
  return result;
}

PackedLogic4 subword_corruption_mask(
    const char action,
    const std::size_t word_width,
    const std::size_t subword_width,
    const std::size_t subword) {
  constexpr std::string_view whole_word{"clde"};
  if (whole_word.find(action) != std::string_view::npos) {
    return word_corruption_mask(action, word_width);
  }
  PackedLogic4 result{word_width, Logic4::zero};
  constexpr std::string_view partial{"CLDE"};
  if (partial.find(action) == std::string_view::npos) return result;
  const auto low = subword * subword_width;
  const auto high = std::min(word_width, low + subword_width);
  for (auto bit = low; bit < high; ++bit) result.set(bit, Logic4::x);
  return result;
}

VitalMemoryAddressState decode_vital_memory_data_slice(
    const PackedLogic4& previous,
    const PackedLogic4& current,
    const std::size_t low,
    const std::size_t high) {
  bool unknown{};
  bool transitioned{};
  for (auto bit = low; bit < high; ++bit) {
    const auto value = current.get_logic9(bit);
    unknown = unknown || (value != Logic9::zero && value != Logic9::one);
    transitioned = transitioned
        || previous.get_logic9(bit) != value;
  }
  if (unknown) {
    return transitioned ? VitalMemoryAddressState::unknown_transition
                        : VitalMemoryAddressState::unknown;
  }
  return transitioned ? VitalMemoryAddressState::good_transition
                      : VitalMemoryAddressState::good;
}

void fill_logic_range(
    PackedLogic4& value,
    const std::size_t low,
    const std::size_t high,
    const Logic9 fill) {
  for (auto bit = low; bit < high; ++bit) value.set_logic9(bit, fill);
}

void copy_logic_range(
    PackedLogic4& destination,
    const PackedLogic4& source,
    const std::size_t low,
    const std::size_t high) {
  for (auto bit = low; bit < high; ++bit) {
    destination.set_logic9(bit, source.get_logic9(bit));
  }
}

void apply_corruption_mask(
    PackedLogic4& value,
    const PackedLogic4& mask,
    const std::size_t low,
    const std::size_t high) {
  for (auto bit = low; bit < high; ++bit) {
    if (mask.get(bit) != Logic4::zero) value.set_logic9(bit, Logic9::x);
  }
}

bool all_exact_z(const PackedLogic4& value) {
  for (std::size_t bit = 0; bit < value.width(); ++bit) {
    if (value.get_logic9(bit) != Logic9::z) return false;
  }
  return true;
}

bool memory_writing(const VitalMemoryPortFlag& flag) {
  return flag.memory_current == VitalMemoryPortState::write
      || flag.memory_current == VitalMemoryPortState::corrupt;
}

bool memory_reading(const VitalMemoryPortFlag& flag) {
  return flag.memory_current == VitalMemoryPortState::read
      || flag.output_disable;
}

}  // namespace

VitalMemoryState make_vital_memory(
    const std::uint64_t word_count,
    const std::uint64_t word_width,
    const std::uint64_t subword_width) {
  if (word_count == 0U || word_width == 0U || subword_width == 0U) {
    throw std::invalid_argument{
        "VITAL memory geometry requires positive words and widths"};
  }
  if (subword_width > word_width) {
    throw std::invalid_argument{
        "VITAL memory subword width exceeds its word width"};
  }
  if (word_width > std::numeric_limits<std::uint32_t>::max()
      || word_count > std::numeric_limits<std::size_t>::max()) {
    throw std::length_error{
        "VITAL memory geometry exceeds the host representation"};
  }
  const auto chunks = (word_width + 63U) / 64U;
  constexpr auto overhead = sizeof(PackedLogic4);
  if (chunks > (std::numeric_limits<std::size_t>::max() - overhead)
          / sizeof(Logic9Word)) {
    throw std::length_error{"VITAL memory storage size overflows"};
  }
  const auto bytes_per_word = overhead
      + static_cast<std::size_t>(chunks) * sizeof(Logic9Word);
  if (bytes_per_word > maximum_container_storage_bytes) {
    throw std::length_error{
        "one VITAL memory word exceeds the 256 MiB host-storage budget"};
  }
  VitalMemoryState result;
  result.word_count = static_cast<std::size_t>(word_count);
  result.word_width = static_cast<std::uint32_t>(word_width);
  result.subword_width = static_cast<std::uint32_t>(subword_width);
  result.bits_per_enable = static_cast<std::uint32_t>(
      (word_width - 1U) / subword_width + 1U);
  PackedLogic4 initial{static_cast<std::size_t>(word_width)};
  initial.fill(Logic9::u);
  result.default_word = initial;
  constexpr std::size_t eager_storage_target = 1024U * 1024U;
  if (word_count <= eager_storage_target / bytes_per_word) {
    result.words.assign(static_cast<std::size_t>(word_count), initial);
  }
  return result;
}

const PackedLogic4& vital_memory_word(
    const VitalMemoryState& memory,
    const std::size_t address) {
  if (address >= memory.word_count) {
    throw std::out_of_range{"VITAL memory address is outside its depth"};
  }
  if (!memory.words.empty()) return memory.words[address];
  const auto found = memory.sparse_words.find(address);
  return found == memory.sparse_words.end()
      ? memory.default_word : found->second;
}

PackedLogic4& vital_memory_word(
    VitalMemoryState& memory,
    const std::size_t address) {
  if (address >= memory.word_count) {
    throw std::out_of_range{"VITAL memory address is outside its depth"};
  }
  if (!memory.words.empty()) return memory.words[address];
  const auto chunks = (static_cast<std::size_t>(memory.word_width) + 63U) / 64U;
  const auto bytes_per_word = sizeof(PackedLogic4) + chunks * sizeof(Logic9Word);
  const auto maximum_sparse_words =
      maximum_container_storage_bytes / bytes_per_word;
  auto found = memory.sparse_words.find(address);
  if (found != memory.sparse_words.end()) return found->second;
  if (memory.sparse_words.size() >= maximum_sparse_words) {
    throw std::length_error{
        "VITAL memory sparse materialization exceeds the host-storage budget"};
  }
  return memory.sparse_words.emplace(address, memory.default_word)
      .first->second;
}

void load_vital_memory_text(
    VitalMemoryState& memory,
    const std::string_view text,
    const bool binary) {
  if (text.size() > maximum_memory_file_bytes) {
    throw std::length_error{
        "VITAL memory load file exceeds the 1 MiB input budget"};
  }
  std::vector<std::string> tokens;
  std::string token;
  bool comment{};
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto character = text[index];
    const auto next = index + 1U < text.size() ? text[index + 1U] : '\0';
    if (comment) {
      if (character == '\n') comment = false;
      continue;
    }
    if ((character == '-' && next == '-') || character == '#') {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      comment = true;
      if (character == '-') ++index;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
      }
      continue;
    }
    token.push_back(character);
  }
  if (!token.empty()) tokens.push_back(std::move(token));

  std::size_t address{};
  const auto logic = [](const char character) -> char {
    switch (character) {
    case '0': case 'l': case 'L': return '0';
    case '1': case 'h': case 'H': return '1';
    case 'u': case 'U': return 'U';
    case 'x': case 'X': case 'z': case 'Z': case 'w': case 'W': case '-':
      return 'X';
    default: return '\0';
    }
  };
  std::vector<std::pair<std::size_t, PackedLogic4>> updates;
  for (auto value : tokens) {
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    if (value.empty()) continue;
    if (value.front() == '@') {
      if (value.size() == 1U) {
        throw std::invalid_argument{
            "VITAL memory load address is empty"};
      }
      std::uint64_t parsed{};
      const auto* begin = value.data() + 1;
      const auto* end = value.data() + value.size();
      const auto conversion = std::from_chars(begin, end, parsed, 16);
      if (conversion.ec != std::errc{} || conversion.ptr != end
          || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument{
            "VITAL memory load address is not valid hexadecimal"};
      }
      address = static_cast<std::size_t>(parsed);
      continue;
    }
    std::string bits;
    if (binary) {
      bits.reserve(value.size());
      for (const auto character : value) {
        const auto converted = logic(character);
        if (converted == '\0') {
          throw std::invalid_argument{
              "VITAL binary memory data contains an invalid digit"};
        }
        bits.push_back(converted);
      }
    } else {
      bits.reserve(value.size() * 4U);
      for (const auto character : value) {
        const auto unknown = logic(character);
        if (unknown == 'U' || unknown == 'X') {
          bits.append(4U, unknown);
          continue;
        }
        unsigned nibble{};
        if (character >= '0' && character <= '9') {
          nibble = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
          nibble = static_cast<unsigned>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
          nibble = static_cast<unsigned>(character - 'A' + 10);
        } else {
          throw std::invalid_argument{
              "VITAL hexadecimal memory data contains an invalid digit"};
        }
        for (unsigned shift = 4U; shift-- > 0U;) {
          bits.push_back((nibble & (1U << shift)) != 0U ? '1' : '0');
        }
      }
    }
    if (address >= memory.word_count) {
      throw std::out_of_range{
          "VITAL memory load address is outside the declared memory"};
    }
    if (bits.size() > memory.word_width) {
      const auto excess = bits.size() - memory.word_width;
      if (std::ranges::any_of(
              bits.begin(), bits.begin() + static_cast<std::ptrdiff_t>(excess),
              [](const char bit) { return bit != '0'; })) {
        throw std::invalid_argument{
            "VITAL memory load word does not fit the declared width"};
      }
      bits.erase(0, excess);
    }
    bits.insert(bits.begin(), memory.word_width - bits.size(), '0');
    updates.emplace_back(
        address++, PackedLogic4::from_logic9_msb_string(bits));
  }

  if (memory.words.empty() && !updates.empty()) {
    std::vector<std::size_t> new_addresses;
    new_addresses.reserve(updates.size());
    for (const auto& [update_address, ignored] : updates) {
      (void)ignored;
      if (!memory.sparse_words.contains(update_address)) {
        new_addresses.push_back(update_address);
      }
    }
    std::ranges::sort(new_addresses);
    const auto unique_end = std::ranges::unique(new_addresses).begin();
    new_addresses.erase(unique_end, new_addresses.end());
    const auto chunks =
        (static_cast<std::size_t>(memory.word_width) + 63U) / 64U;
    const auto bytes_per_word =
        sizeof(PackedLogic4) + chunks * sizeof(Logic9Word);
    const auto maximum_sparse_words =
        maximum_container_storage_bytes / bytes_per_word;
    if (new_addresses.size() > maximum_sparse_words
            - std::min(memory.sparse_words.size(), maximum_sparse_words)) {
      throw std::length_error{
          "VITAL memory load exceeds the sparse host-storage budget"};
    }
    memory.sparse_words.reserve(
        memory.sparse_words.size() + new_addresses.size());
  }
  for (auto& [update_address, value] : updates) {
    vital_memory_word(memory, update_address) = std::move(value);
  }
}

VitalMemoryAddress decode_vital_memory_address(
    const PackedLogic4& previous,
    const PackedLogic4& current,
    const std::size_t word_count) {
  if (previous.width() != current.width()) {
    throw std::invalid_argument{
        "VITAL previous/current address widths differ"};
  }
  if (word_count == 0U) {
    throw std::invalid_argument{
        "VITAL address decoding requires a nonempty memory"};
  }
  const bool transitioned = !same_logic_value(previous, current);
  std::size_t address{};
  bool unknown{};
  bool invalid{};
  for (std::size_t position = current.width(); position-- > 0U;) {
    const auto bit = current.get_logic9(position);
    if (bit != Logic9::zero && bit != Logic9::one) {
      unknown = true;
      continue;
    }
    if (invalid || unknown) continue;
    const auto digit = bit == Logic9::one ? std::size_t{1U} : 0U;
    const auto maximum = word_count - 1U;
    if (address > maximum / 2U
        || (address == maximum / 2U && digit > maximum % 2U)) {
      invalid = true;
    } else {
      address = address * 2U + digit;
    }
  }
  if (unknown) {
    return {
        transitioned ? VitalMemoryAddressState::unknown_transition
                     : VitalMemoryAddressState::unknown,
        std::nullopt};
  }
  if (invalid) {
    return {
        transitioned ? VitalMemoryAddressState::invalid_transition
                     : VitalMemoryAddressState::invalid,
        std::nullopt};
  }
  return {
      transitioned ? VitalMemoryAddressState::good_transition
                   : VitalMemoryAddressState::good,
      address};
}

VitalMemoryTableResult lookup_vital_memory_table(
    const std::vector<VitalMemoryTableRow>& rows,
    const PackedLogic4& previous_controls,
    const PackedLogic4& controls,
    const VitalMemoryAddressState address_state,
    const VitalMemoryAddressState data_state,
    const std::size_t word_width) {
  if (previous_controls.width() != controls.width()) {
    throw std::invalid_argument{
        "VITAL memory table control widths differ"};
  }
  if (word_width == 0U) {
    throw std::invalid_argument{
        "VITAL memory table word width must be positive"};
  }
  VitalMemoryTableResult result{
      's', 'S', PackedLogic4{word_width, Logic4::zero},
      PackedLogic4{word_width, Logic4::zero}, std::nullopt, false};
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    if (!row.enables.empty()) {
      throw std::invalid_argument{
          "word-wide VITAL memory table rows cannot contain enable symbols"};
    }
    if (row.controls.size() != controls.width()) {
      throw std::invalid_argument{
          "VITAL memory table row width does not match its controls"};
    }
    bool match = true;
    for (std::size_t position = 0; position < controls.width(); ++position) {
      const auto offset = controls.width() - position - 1U;
      const auto control_match = memory_control_matches(
          row.controls[position],
          previous_controls.get_logic9(offset),
          controls.get_logic9(offset));
      if (!control_match) {
        result.invalid_input_symbol = true;
        return result;
      }
      if (!*control_match) {
        match = false;
        break;
      }
    }
    if (!match) continue;
    const auto address_match = memory_flag_matches(
        row.address, address_state);
    if (!address_match) {
      result.invalid_input_symbol = true;
      return result;
    }
    if (!*address_match) continue;
    const auto data_match = memory_flag_matches(row.data, data_state);
    if (!data_match) {
      result.invalid_input_symbol = true;
      return result;
    }
    if (!*data_match) continue;
    result.memory_action = row.memory_action;
    result.data_action = row.data_action;
    result.memory_corrupt_mask = word_corruption_mask(
        row.memory_action, word_width);
    result.data_corrupt_mask = word_corruption_mask(
        row.data_action, word_width);
    result.matched_row = row_index;
    return result;
  }
  return result;
}

VitalMemorySubwordTableResult lookup_vital_memory_subword_table(
    const std::vector<VitalMemoryTableRow>& rows,
    const PackedLogic4& previous_controls,
    const PackedLogic4& controls,
    const std::vector<PackedLogic4>& previous_enables,
    const std::vector<PackedLogic4>& enables,
    const PackedLogic4& previous_data,
    const PackedLogic4& data,
    const VitalMemoryAddressState address_state,
    const std::size_t word_width,
    const std::size_t subword_width) {
  if (word_width == 0U || subword_width == 0U
      || subword_width > word_width) {
    throw std::invalid_argument{
        "VITAL subword table geometry is invalid"};
  }
  if (previous_controls.width() != controls.width()
      || previous_data.width() != word_width || data.width() != word_width
      || previous_enables.size() != enables.size()) {
    throw std::invalid_argument{
        "VITAL subword table bus dimensions differ"};
  }
  const auto subwords = (word_width - 1U) / subword_width + 1U;
  for (std::size_t index = 0; index < enables.size(); ++index) {
    if (previous_enables[index].width() != subwords
        || enables[index].width() != subwords) {
      throw std::invalid_argument{
          "VITAL enable width does not match the subword count"};
    }
  }
  VitalMemorySubwordTableResult result;
  result.subwords.reserve(subwords);
  for (std::size_t subword = 0; subword < subwords; ++subword) {
    VitalMemoryTableResult selected{
        's', 'S', PackedLogic4{word_width, Logic4::zero},
        PackedLogic4{word_width, Logic4::zero}, std::nullopt, false};
    const auto low = subword * subword_width;
    const auto high = std::min(word_width, low + subword_width);
    const auto data_state = decode_vital_memory_data_slice(
        previous_data, data, low, high);
    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
      const auto& row = rows[row_index];
      if (row.controls.size() != controls.width()
          || row.enables.size() != enables.size()) {
        throw std::invalid_argument{
            "VITAL subword table row width does not match its inputs"};
      }
      bool match = true;
      for (std::size_t position = 0;
           position < controls.width(); ++position) {
        const auto offset = controls.width() - position - 1U;
        const auto control_match = memory_control_matches(
            row.controls[position],
            previous_controls.get_logic9(offset),
            controls.get_logic9(offset));
        if (!control_match) {
          selected.invalid_input_symbol = true;
          break;
        }
        if (!*control_match) {
          match = false;
          break;
        }
      }
      if (selected.invalid_input_symbol) break;
      if (!match) continue;
      for (std::size_t enable = 0; enable < enables.size(); ++enable) {
        const auto enable_match = memory_control_matches(
            row.enables[enable],
            previous_enables[enable].get_logic9(subword),
            enables[enable].get_logic9(subword));
        if (!enable_match) {
          selected.invalid_input_symbol = true;
          break;
        }
        if (!*enable_match) {
          match = false;
          break;
        }
      }
      if (selected.invalid_input_symbol) break;
      if (!match) continue;
      const auto address_match = memory_flag_matches(
          row.address, address_state);
      const auto data_match = memory_flag_matches(row.data, data_state);
      if (!address_match || !data_match) {
        selected.invalid_input_symbol = true;
        break;
      }
      if (!*address_match || !*data_match) continue;
      selected.memory_action = row.memory_action;
      selected.data_action = row.data_action;
      selected.memory_corrupt_mask = subword_corruption_mask(
          row.memory_action, word_width, subword_width, subword);
      selected.data_corrupt_mask = subword_corruption_mask(
          row.data_action, word_width, subword_width, subword);
      selected.matched_row = row_index;
      break;
    }
    result.subwords.push_back(std::move(selected));
  }
  return result;
}

void apply_vital_memory_table_actions(
    VitalMemoryState& memory,
    PackedLogic4& data_output,
    const PackedLogic4& data_input,
    const std::optional<std::size_t> address,
    const std::size_t low_bit,
    const std::size_t high_bit,
    const VitalMemoryTableResult& actions,
    VitalMemoryPortFlag& port_flag) {
  if (data_output.width() != memory.word_width
      || data_input.width() != memory.word_width
      || actions.memory_corrupt_mask.width() != memory.word_width
      || actions.data_corrupt_mask.width() != memory.word_width
      || low_bit >= high_bit || high_bit > memory.word_width) {
    throw std::invalid_argument{
        "VITAL memory action dimensions are invalid"};
  }
  const auto valid_address = address && *address < memory.word_count;
  const auto read_word = [&]() -> const PackedLogic4* {
    return valid_address
        ? &vital_memory_word(
              static_cast<const VitalMemoryState&>(memory), *address)
        : nullptr;
  };

  switch (actions.data_action) {
    case 'l':
      data_output.fill(Logic9::x);
      port_flag.data_current = VitalMemoryPortState::corrupt;
      break;
    case 'd':
      apply_corruption_mask(
          data_output, actions.data_corrupt_mask, 0U, memory.word_width);
      port_flag.data_current = VitalMemoryPortState::corrupt;
      break;
    case 'e':
      if (const auto* word = read_word(); word != nullptr) {
        data_output = *word;
        if (!same_logic_value(*word, data_input)) {
          data_output.fill(Logic9::x);
        }
      }
      port_flag.data_current = VitalMemoryPortState::corrupt;
      break;
    case 'L':
      if (const auto* word = read_word(); word != nullptr) data_output = *word;
      apply_corruption_mask(
          data_output, actions.data_corrupt_mask, low_bit, high_bit);
      port_flag.data_current = VitalMemoryPortState::corrupt;
      break;
    case 'D':
      apply_corruption_mask(
          data_output, actions.data_corrupt_mask, low_bit, high_bit);
      port_flag.data_current = VitalMemoryPortState::corrupt;
      break;
    case 'E':
      if (const auto* word = read_word(); word != nullptr) {
        data_output = *word;
        bool differs{};
        for (auto bit = low_bit; bit < high_bit; ++bit) {
          differs = differs
              || word->get_logic9(bit) != data_input.get_logic9(bit);
        }
        if (differs) {
          fill_logic_range(data_output, low_bit, high_bit, Logic9::x);
        }
      }
      port_flag.data_current = VitalMemoryPortState::corrupt;
      break;
    case 'M':
      port_flag.data_current = VitalMemoryPortState::read;
      break;
    case 'm':
      if (const auto* word = read_word(); word != nullptr) data_output = *word;
      port_flag.data_current = VitalMemoryPortState::read;
      break;
    case 't':
      data_output = data_input;
      port_flag.data_current = VitalMemoryPortState::read;
      break;
    case '0': case '1': case 'Z':
      fill_logic_range(
          data_output, low_bit, high_bit,
          actions.data_action == '0' ? Logic9::zero
              : actions.data_action == '1' ? Logic9::one : Logic9::z);
      port_flag.data_current = actions.data_action == 'Z'
          ? VitalMemoryPortState::high_z
          : VitalMemoryPortState::read;
      break;
    case 'S':
      port_flag.output_disable = true;
      break;
    default:
      port_flag.data_current = VitalMemoryPortState::undefined;
      break;
  }

  const auto corrupt_word = [&](PackedLogic4& word, const bool partial) {
    apply_corruption_mask(
        word, actions.memory_corrupt_mask,
        partial ? low_bit : 0U,
        partial ? high_bit : memory.word_width);
  };
  switch (actions.memory_action) {
    case 'w':
      if (valid_address) {
        copy_logic_range(
            vital_memory_word(memory, *address),
            data_input, low_bit, high_bit);
      }
      port_flag.memory_current = VitalMemoryPortState::write;
      break;
    case 's':
      port_flag.memory_current = VitalMemoryPortState::read;
      break;
    case 'c':
      if (!memory.words.empty()) {
        for (auto& word : memory.words) word.fill(Logic9::x);
      } else {
        memory.default_word.fill(Logic9::x);
        memory.sparse_words.clear();
      }
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case 'l':
      if (valid_address) vital_memory_word(memory, *address).fill(Logic9::x);
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case 'd':
      if (valid_address) corrupt_word(vital_memory_word(memory, *address), false);
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case 'e':
      if (valid_address
          && !same_logic_value(
              vital_memory_word(
                  static_cast<const VitalMemoryState&>(memory), *address),
              data_input)) {
        vital_memory_word(memory, *address).fill(Logic9::x);
      }
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case 'C':
      if (!memory.words.empty()) {
        for (auto& word : memory.words) corrupt_word(word, true);
      } else {
        corrupt_word(memory.default_word, true);
        for (auto& [ignored, word] : memory.sparse_words) {
          (void)ignored;
          corrupt_word(word, true);
        }
      }
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case 'L': case 'D':
      if (valid_address) corrupt_word(vital_memory_word(memory, *address), true);
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case 'E':
      if (valid_address) {
        bool differs{};
        for (auto bit = low_bit; bit < high_bit; ++bit) {
          differs = differs
              || vital_memory_word(
                     static_cast<const VitalMemoryState&>(memory), *address)
                     .get_logic9(bit)
                  != data_input.get_logic9(bit);
        }
        if (differs) corrupt_word(vital_memory_word(memory, *address), true);
      }
      port_flag.memory_current = VitalMemoryPortState::corrupt;
      break;
    case '0': case '1': case 'Z':
      if (valid_address) {
        fill_logic_range(
            vital_memory_word(memory, *address), low_bit, high_bit,
            actions.memory_action == '0' ? Logic9::zero
                : actions.memory_action == '1' ? Logic9::one : Logic9::z);
      }
      port_flag.memory_current = VitalMemoryPortState::write;
      break;
    default:
      port_flag.memory_current = VitalMemoryPortState::undefined;
      break;
  }
}

VitalMemoryAddress execute_vital_memory_word_table(
    VitalMemoryState& memory,
    VitalMemoryTableState& state,
    PackedLogic4& data_output,
    const PackedLogic4& controls,
    const PackedLogic4& data_input,
    const PackedLogic4& address_bus,
    const std::vector<VitalMemoryTableRow>& rows) {
  if (data_input.width() != memory.word_width
      || data_output.width() != memory.word_width) {
    throw std::invalid_argument{
        "VITAL word-table data width does not match memory"};
  }
  const bool first_call = !state.initialized;
  if (first_call) {
    state.initialized = true;
    state.previous_controls = controls;
    state.previous_data = data_input;
    state.previous_address = address_bus;
    state.port_flags.assign(1U, VitalMemoryPortFlag{});
  } else if (state.port_flags.size() != 1U
      || state.previous_controls.width() != controls.width()
      || state.previous_data.width() != data_input.width()
      || state.previous_address.width() != address_bus.width()) {
    throw std::invalid_argument{
        "VITAL word-table call-site dimensions changed"};
  }
  auto& flag = state.port_flags.front();
  const bool unchanged = !first_call
      && same_logic_value(state.previous_controls, controls)
      && same_logic_value(state.previous_data, data_input)
      && same_logic_value(state.previous_address, address_bus)
      && flag.memory_current == flag.memory_previous
      && flag.data_current == flag.data_previous;
  const auto decoded = decode_vital_memory_address(
      state.previous_address, address_bus, memory.word_count);
  if (unchanged) {
    flag.output_disable = true;
    return decoded;
  }
  flag.data_previous = flag.data_current;
  flag.memory_previous = flag.memory_current;
  flag.output_disable = false;
  const auto data_state = decode_vital_memory_data_slice(
      state.previous_data, data_input, 0U, memory.word_width);
  const auto actions = lookup_vital_memory_table(
      rows, state.previous_controls, controls, decoded.state,
      data_state, memory.word_width);
  apply_vital_memory_table_actions(
      memory, data_output, data_input, decoded.value,
      0U, memory.word_width, actions, flag);
  if (actions.data_action == 'S'
      || (flag.data_current == VitalMemoryPortState::high_z
          && flag.data_current == flag.data_previous)) {
    flag.output_disable = true;
  }
  state.previous_controls = controls;
  state.previous_data = data_input;
  state.previous_address = address_bus;
  return decoded;
}

VitalMemoryAddress execute_vital_memory_subword_table(
    VitalMemoryState& memory,
    VitalMemoryTableState& state,
    PackedLogic4& data_output,
    const PackedLogic4& controls,
    const std::vector<PackedLogic4>& enables,
    const PackedLogic4& data_input,
    const PackedLogic4& address_bus,
    const std::vector<VitalMemoryTableRow>& rows) {
  if (data_input.width() != memory.word_width
      || data_output.width() != memory.word_width
      || enables.empty()) {
    throw std::invalid_argument{
        "VITAL subword-table data or enable dimensions are invalid"};
  }
  const auto subwords = static_cast<std::size_t>(memory.bits_per_enable);
  const bool first_call = !state.initialized;
  if (first_call) {
    state.initialized = true;
    state.previous_controls = controls;
    state.previous_enables = enables;
    state.previous_data = data_input;
    state.previous_address = address_bus;
    state.port_flags.assign(subwords, VitalMemoryPortFlag{});
  } else if (state.port_flags.size() != subwords
      || state.previous_controls.width() != controls.width()
      || state.previous_data.width() != data_input.width()
      || state.previous_address.width() != address_bus.width()
      || state.previous_enables.size() != enables.size()) {
    throw std::invalid_argument{
        "VITAL subword-table call-site dimensions changed"};
  }
  bool unchanged = !first_call
      && same_logic_value(state.previous_controls, controls)
      && same_logic_value(state.previous_data, data_input)
      && same_logic_value(state.previous_address, address_bus);
  for (std::size_t index = 0; index < enables.size(); ++index) {
    unchanged = unchanged
        && same_logic_value(state.previous_enables[index], enables[index]);
  }
  unchanged = unchanged && std::ranges::all_of(
      state.port_flags, [](const auto& flag) {
        return flag.memory_current == flag.memory_previous
            && flag.data_current == flag.data_previous;
      });
  const auto decoded = decode_vital_memory_address(
      state.previous_address, address_bus, memory.word_count);
  if (unchanged) {
    for (auto& flag : state.port_flags) flag.output_disable = true;
    return decoded;
  }
  auto actions = lookup_vital_memory_subword_table(
      rows, state.previous_controls, controls,
      state.previous_enables, enables,
      state.previous_data, data_input, decoded.state,
      memory.word_width, memory.subword_width);
  for (std::size_t subword = 0; subword < subwords; ++subword) {
    auto& flag = state.port_flags[subword];
    flag.data_previous = flag.data_current;
    flag.memory_previous = flag.memory_current;
    flag.output_disable = false;
    const auto low = subword * memory.subword_width;
    const auto high = std::min<std::size_t>(
        memory.word_width, low + memory.subword_width);
    apply_vital_memory_table_actions(
        memory, data_output, data_input, decoded.value,
        low, high, actions.subwords[subword], flag);
    if (actions.subwords[subword].data_action == 'S'
        || (flag.data_current == VitalMemoryPortState::high_z
            && flag.data_current == flag.data_previous)) {
      flag.output_disable = true;
    }
  }
  state.previous_controls = controls;
  state.previous_enables = enables;
  state.previous_data = data_input;
  state.previous_address = address_bus;
  return decoded;
}

void apply_vital_memory_cross_ports(
    VitalMemoryState& memory,
    PackedLogic4& data_output,
    std::vector<VitalMemoryPortFlag>& same_port_flags,
    const std::optional<std::size_t> same_port_address,
    const std::vector<VitalMemoryCrossPort>& cross_ports,
    const VitalMemoryCrossPortMode mode) {
  const auto subwords = static_cast<std::size_t>(memory.bits_per_enable);
  if (data_output.width() != memory.word_width
      || same_port_flags.size() != subwords) {
    throw std::invalid_argument{
        "VITAL cross-port same-port dimensions are invalid"};
  }
  for (const auto& port : cross_ports) {
    if (port.flags.size() != subwords) {
      throw std::invalid_argument{
          "VITAL cross-port flag dimensions are inconsistent"};
    }
  }
  if (all_exact_z(data_output) || !same_port_address
      || *same_port_address >= memory.word_count) {
    return;
  }
  auto memory_value = vital_memory_word(
      static_cast<const VitalMemoryState&>(memory), *same_port_address);
  bool memory_changed{};
  for (std::size_t subword = 0; subword < subwords; ++subword) {
    const auto low = subword * memory.subword_width;
    const auto high = std::min<std::size_t>(
        memory.word_width, low + memory.subword_width);
    auto& same = same_port_flags[subword];
    for (const auto& cross : cross_ports) {
      if (!cross.address || *cross.address >= memory.word_count
          || *cross.address != *same_port_address) {
        continue;
      }
      const auto& other = cross.flags[subword];
      const bool write_contention =
          (mode == VitalMemoryCrossPortMode::write_contention
           || mode
               == VitalMemoryCrossPortMode::cross_read_and_write_contention)
          && memory_writing(same) && memory_writing(other);
      const bool cross_read =
          (mode == VitalMemoryCrossPortMode::cross_read
           || mode
               == VitalMemoryCrossPortMode::cross_read_and_write_contention)
          && memory_reading(same) && memory_writing(other);
      const bool corrupt_contention =
          mode == VitalMemoryCrossPortMode::read_write_contention
          && memory_reading(same) && memory_writing(other);
      const bool read_write_contention =
          mode == VitalMemoryCrossPortMode::cross_read_and_read_contention
          && memory_reading(same) && memory_writing(other);
      if (write_contention || corrupt_contention) {
        fill_logic_range(memory_value, low, high, Logic9::x);
        fill_logic_range(data_output, low, high, Logic9::x);
        same.memory_current = VitalMemoryPortState::corrupt;
        same.data_current = VitalMemoryPortState::corrupt;
        same.output_disable = false;
        memory_changed = true;
        break;
      }
      if (cross_read) {
        copy_logic_range(data_output, memory_value, low, high);
        same.memory_current = VitalMemoryPortState::read;
        same.data_current = VitalMemoryPortState::read;
        same.output_disable = false;
        break;
      }
      if (read_write_contention) {
        fill_logic_range(data_output, low, high, Logic9::x);
        same.data_current = VitalMemoryPortState::corrupt;
        same.output_disable = false;
        break;
      }
    }
  }
  if (memory_changed) {
    vital_memory_word(memory, *same_port_address) = std::move(memory_value);
  }
}

void apply_vital_memory_write_contention(
    VitalMemoryState& memory,
    const std::vector<VitalMemoryCrossPort>& cross_ports) {
  const auto subwords = static_cast<std::size_t>(memory.bits_per_enable);
  for (const auto& port : cross_ports) {
    if (port.flags.size() != subwords) {
      throw std::invalid_argument{
          "VITAL write-contention flag dimensions are inconsistent"};
    }
  }
  for (std::size_t subword = 0; subword < subwords; ++subword) {
    const auto low = subword * memory.subword_width;
    const auto high = std::min<std::size_t>(
        memory.word_width, low + memory.subword_width);
    for (std::size_t first = 0; first < cross_ports.size(); ++first) {
      const auto& left = cross_ports[first];
      if (!left.address || *left.address >= memory.word_count) continue;
      bool contention{};
      for (std::size_t second = first + 1U;
           second < cross_ports.size(); ++second) {
        const auto& right = cross_ports[second];
        if (!right.address || *right.address >= memory.word_count) continue;
        const auto& left_flag = left.flags[subword];
        const auto& right_flag = right.flags[subword];
        const bool both_write =
            left_flag.memory_current == VitalMemoryPortState::write
            && right_flag.memory_current == VitalMemoryPortState::write
            && *left.address == *right.address;
        const bool involves_corruption =
            memory_writing(left_flag) && memory_writing(right_flag)
            && (left_flag.memory_current == VitalMemoryPortState::corrupt
                || right_flag.memory_current
                    == VitalMemoryPortState::corrupt);
        if (both_write || involves_corruption) {
          contention = true;
          break;
        }
      }
      if (contention) {
        fill_logic_range(
            vital_memory_word(memory, *left.address), low, high, Logic9::x);
      }
    }
  }
}

VitalMemoryViolationResult lookup_vital_memory_violation(
    const std::vector<VitalMemoryTableRow>& rows,
    const PackedLogic4& scalar_flags,
    const PackedLogic4& vector_flags,
    const std::vector<std::size_t>& vector_flag_sizes,
    const std::size_t word_width,
    const std::size_t subword_width,
    const bool message_on) {
  if (word_width == 0U || subword_width == 0U
      || subword_width > word_width) {
    throw std::invalid_argument{
        "VITAL violation-table geometry is invalid"};
  }
  std::size_t vector_size{};
  for (const auto size : vector_flag_sizes) {
    if (size > vector_flags.width() - vector_size) {
      throw std::invalid_argument{
          "VITAL violation vector sizes exceed their input"};
    }
    vector_size += size;
  }
  if (vector_size != vector_flags.width()) {
    throw std::invalid_argument{
        "VITAL violation vector sizes do not cover their input"};
  }
  const auto zero_mask = [&] {
    return PackedLogic4{word_width, Logic4::zero};
  };
  VitalMemoryViolationResult result{
      {'s', 'S', zero_mask(), zero_mask(), std::nullopt, false},
      false, false};
  const auto flag_is_x = [](const Logic9 value) {
    return vital_x01_ordinal(value) == 0U;
  };
  const auto violation_match = [](
      const char symbol, const bool is_x, const bool is_zero)
      -> std::optional<bool> {
    if (symbol == '-') return true;
    if (symbol == 'X') return is_x;
    if (symbol == '0') return is_zero;
    return std::nullopt;
  };
  for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
    const auto& row = rows[row_index];
    if (row.controls.size() != scalar_flags.width()
        || row.enables.size() != vector_flag_sizes.size()) {
      throw std::invalid_argument{
          "VITAL violation table row width does not match its flags"};
    }
    bool match = true;
    for (std::size_t position = 0;
         position < scalar_flags.width(); ++position) {
      const auto flag = scalar_flags.get_logic9(
          scalar_flags.width() - position - 1U);
      const auto matched = violation_match(
          row.controls[position], flag_is_x(flag),
          vital_x01_ordinal(flag) == 1U);
      if (!matched) {
        result.actions.invalid_input_symbol = true;
        return result;
      }
      if (!*matched) {
        match = false;
        break;
      }
    }
    if (!match) continue;
    std::size_t vector_position{};
    for (std::size_t group = 0;
         group < vector_flag_sizes.size(); ++group) {
      bool group_x{};
      for (std::size_t bit = 0; bit < vector_flag_sizes[group]; ++bit) {
        group_x = group_x || flag_is_x(
            vector_flags.get_logic9(vector_position + bit));
      }
      vector_position += vector_flag_sizes[group];
      const auto matched = violation_match(
          row.enables[group], group_x, !group_x);
      if (!matched) {
        result.actions.invalid_input_symbol = true;
        return result;
      }
      if (!*matched) {
        match = false;
        break;
      }
    }
    if (!match) continue;

    PackedLogic4 raw_mask{word_width, Logic4::zero};
    for (std::size_t position = 0;
         position < scalar_flags.width(); ++position) {
      if (row.controls[position] == 'X'
          && flag_is_x(scalar_flags.get_logic9(
              scalar_flags.width() - position - 1U))) {
        raw_mask.set(0U, Logic4::x);
      }
    }
    vector_position = 0U;
    for (std::size_t group = 0;
         group < vector_flag_sizes.size(); ++group) {
      if (row.enables[group] == 'X') {
        for (std::size_t bit = 0;
             bit < vector_flag_sizes[group] && bit < word_width; ++bit) {
          if (flag_is_x(vector_flags.get_logic9(vector_position + bit))) {
            raw_mask.set(bit, Logic4::x);
          }
        }
      }
      vector_position += vector_flag_sizes[group];
    }
    const auto action_mask = [&](const char action) {
      constexpr std::string_view whole{"cle"};
      if (whole.find(action) != std::string_view::npos) {
        return PackedLogic4{word_width, Logic4::x};
      }
      if (action == 'd') return raw_mask;
      PackedLogic4 expanded{word_width, Logic4::zero};
      if (std::string_view{"CLDE"}.find(action)
          == std::string_view::npos) {
        return expanded;
      }
      const auto subwords = (word_width - 1U) / subword_width + 1U;
      for (std::size_t subword = 0; subword < subwords; ++subword) {
        if (subword < raw_mask.width()
            && raw_mask.get(subword) != Logic4::zero) {
          fill_logic_range(
              expanded, subword * subword_width,
              std::min(word_width, (subword + 1U) * subword_width),
              Logic9::x);
        }
      }
      return expanded;
    };
    result.actions.memory_action = row.memory_action;
    result.actions.data_action = row.data_action;
    result.actions.memory_corrupt_mask = action_mask(row.memory_action);
    result.actions.data_corrupt_mask = action_mask(row.data_action);
    result.actions.matched_row = row_index;
    result.violation = true;
    result.message_requested = message_on;
    return result;
  }
  return result;
}

VitalMemoryViolationResult apply_vital_memory_violation(
    VitalMemoryState& memory,
    PackedLogic4& data_output,
    std::vector<VitalMemoryPortFlag>& port_flags,
    const PackedLogic4& data_input,
    const std::optional<std::size_t> address,
    const PackedLogic4& scalar_flags,
    const PackedLogic4& vector_flags,
    const std::vector<std::size_t>& vector_flag_sizes,
    const std::vector<VitalMemoryTableRow>& rows,
    const VitalMemoryPortType port_type,
    const bool message_on) {
  if (!address || *address >= memory.word_count) {
    return {
        {'s', 'S', PackedLogic4{memory.word_width, Logic4::zero},
         PackedLogic4{memory.word_width, Logic4::zero}, std::nullopt, false},
        false, false};
  }
  if (port_flags.size() != memory.bits_per_enable) {
    throw std::invalid_argument{
        "VITAL violation port-flag width is inconsistent"};
  }
  auto result = lookup_vital_memory_violation(
      rows, scalar_flags, vector_flags, vector_flag_sizes,
      memory.word_width, memory.subword_width, message_on);
  auto actions = result.actions;
  const bool reads = port_type == VitalMemoryPortType::read
      || port_type == VitalMemoryPortType::read_write;
  const bool writes = port_type == VitalMemoryPortType::write
      || port_type == VitalMemoryPortType::read_write;
  if (!reads) actions.data_action = 'S';
  if (!writes) actions.memory_action = 's';
  auto flag = port_flags.front();
  apply_vital_memory_table_actions(
      memory, data_output, data_input, address,
      0U, memory.word_width, actions, flag);
  if (reads && result.actions.data_action != 'S') {
    flag.output_disable = false;
    for (auto& destination : port_flags) destination = flag;
  }
  return result;
}

VitalMemoryVectorTimingResult evaluate_vital_memory_setup_hold(
    VitalMemoryVectorTimingState& state,
    const SimulationTick now,
    const PackedLogic4& test,
    const PackedLogic4& reference,
    const std::vector<VitalMemorySetupHoldEntry>& entries,
    const VitalMemoryTimingArc arc,
    const std::size_t bits_per_subword,
    const std::uint16_t reference_edges,
    const std::array<bool, 4> direction_enables,
    const bool x_on,
    const bool message_on,
    const VitalMemoryMessageFormat message_format) {
  if (test.empty() || reference.empty() || bits_per_subword == 0U) {
    throw std::invalid_argument{
        "VITAL vector setup/hold dimensions are invalid"};
  }
  std::vector<std::pair<std::size_t, std::size_t>> checks;
  if (arc == VitalMemoryTimingArc::cross) {
    if (test.width() > maximum_container_storage_bytes / reference.width()) {
      throw std::length_error{
          "VITAL cross-arc timing matrix exceeds the resource budget"};
    }
    checks.reserve(test.width() * reference.width());
    for (std::size_t test_bit = 0; test_bit < test.width(); ++test_bit) {
      for (std::size_t reference_bit = 0;
           reference_bit < reference.width(); ++reference_bit) {
        checks.emplace_back(test_bit, reference_bit);
      }
    }
  } else {
    if (arc == VitalMemoryTimingArc::parallel
        && test.width() != reference.width()) {
      throw std::invalid_argument{
          "VITAL parallel timing arcs require equal vector widths"};
    }
    const auto required_references = arc == VitalMemoryTimingArc::subword
        ? (test.width() - 1U) / bits_per_subword + 1U
        : test.width();
    if (reference.width() < required_references) {
      throw std::invalid_argument{
          "VITAL subword timing reference vector is too short"};
    }
    checks.reserve(test.width());
    for (std::size_t test_bit = 0; test_bit < test.width(); ++test_bit) {
      checks.emplace_back(
          test_bit,
          arc == VitalMemoryTimingArc::subword
              ? test_bit / bits_per_subword : test_bit);
    }
  }
  if (entries.empty()
      || (entries.size() != 1U && entries.size() != checks.size())) {
    throw std::invalid_argument{
        "VITAL setup/hold profile count does not match its arcs"};
  }
  if (state.elements.empty()) {
    state.elements.resize(checks.size());
  } else if (state.elements.size() != checks.size()) {
    throw std::invalid_argument{
        "VITAL setup/hold call-site dimensions changed"};
  }

  PackedLogic4 test_violations{test.width()};
  test_violations.fill(Logic9::zero);
  PackedLogic4 reference_violations{reference.width()};
  reference_violations.fill(Logic9::zero);
  VitalMemoryVectorTimingResult result{
      std::move(test_violations), std::move(reference_violations),
      false, false, {}};
  for (std::size_t index = 0; index < checks.size(); ++index) {
    const auto [test_bit, reference_bit] = checks[index];
    const auto test_value = test.get_logic9(test_bit);
    const auto reference_value = reference.get_logic9(reference_bit);
    auto& element_state = state.elements[index];
    const bool test_event = element_state.initialized
        && element_state.test != test_value;
    const bool reference_event = element_state.initialized
        && element_state.reference != reference_value;
    const auto& entry = entries.size() == 1U ? entries.front()
                                             : entries[index];
    VitalTimingCheck operation;
    operation.kind = VitalTimingCheckKind::setup_hold;
    operation.limits = entry.limits;
    operation.reference_edges = reference_edges;
    operation.check_enabled = entry.check_enabled;
    operation.enables = direction_enables;
    operation.x_on = x_on;
    operation.message_on = message_on;
    // The standard uses element delays to normalize diagnostic observation
    // time; detection itself operates on the recorded source event times.
    static_cast<void>(entry.test_delay);
    static_cast<void>(entry.reference_delay);
    static_cast<void>(evaluate_vital_timing_check(
        operation, element_state, now, test_value, test_event,
        reference_value, reference_event, false));
    if (!element_state.last_violation) continue;
    result.violation = true;
    result.message_requested = result.message_requested || message_on;
    result.violated_checks.push_back(index);
    if (x_on) {
      result.test_violations.set_logic9(test_bit, Logic9::x);
      result.reference_violations.set_logic9(reference_bit, Logic9::x);
    }
  }
  static_cast<void>(message_format);
  return result;
}

PackedLogic4 aggregate_vital_memory_violations(
    const PackedLogic4& violations,
    const std::size_t group_size) {
  if (group_size == 0U) {
    throw std::invalid_argument{
        "VITAL violation aggregation group must be positive"};
  }
  const auto groups = violations.empty()
      ? 0U : (violations.width() - 1U) / group_size + 1U;
  PackedLogic4 result{groups};
  result.fill(Logic9::zero);
  for (std::size_t bit = 0; bit < violations.width(); ++bit) {
    if (vital_x01_ordinal(violations.get_logic9(bit)) == 0U) {
      result.set_logic9(bit / group_size, Logic9::x);
    }
  }
  return result;
}

VitalMemoryVectorTimingResult evaluate_vital_memory_period_pulse(
    VitalMemoryVectorTimingState& state,
    const SimulationTick now,
    const PackedLogic4& test,
    const std::vector<VitalMemoryPeriodPulseEntry>& entries,
    const bool x_on,
    const bool message_on,
    const VitalMemoryMessageFormat message_format) {
  if (test.empty() || entries.empty()
      || (entries.size() != 1U && entries.size() != test.width())) {
    throw std::invalid_argument{
        "VITAL vector period/pulse profile count is invalid"};
  }
  if (state.elements.empty()) {
    state.elements.resize(test.width());
  } else if (state.elements.size() != test.width()) {
    throw std::invalid_argument{
        "VITAL period/pulse call-site dimensions changed"};
  }
  PackedLogic4 violations{test.width()};
  violations.fill(Logic9::zero);
  VitalMemoryVectorTimingResult result{
      std::move(violations), PackedLogic4{}, false, false, {}};
  for (std::size_t bit = 0; bit < test.width(); ++bit) {
    auto& element_state = state.elements[bit];
    const auto value = test.get_logic9(bit);
    const bool event = element_state.initialized
        && element_state.test != value;
    const auto& entry = entries.size() == 1U ? entries.front()
                                             : entries[bit];
    VitalTimingCheck operation;
    operation.kind = VitalTimingCheckKind::period_pulse;
    operation.limits = {
        entry.period, entry.pulse_width_high,
        entry.pulse_width_low, 0U};
    operation.check_enabled = entry.check_enabled;
    operation.x_on = x_on;
    operation.message_on = message_on;
    static_cast<void>(entry.test_delay);
    static_cast<void>(evaluate_vital_timing_check(
        operation, element_state, now, value, event,
        Logic9::u, false, false));
    if (!element_state.last_violation) continue;
    result.violation = true;
    result.message_requested = result.message_requested || message_on;
    result.violated_checks.push_back(bit);
    if (x_on) result.test_violations.set_logic9(bit, Logic9::x);
  }
  static_cast<void>(message_format);
  return result;
}

Logic9 evaluate_vital_timing_check(
    const VitalTimingCheck& operation,
    VitalTimingState& state,
    const SimulationTick now,
    const Logic9 test,
    const bool test_event,
    const Logic9 reference,
    const bool reference_event,
    const bool trigger_event) {
  state.trigger_request.reset();
  if (!operation.check_enabled) {
    state.last_violation = false;
    state.test = test;
    state.reference = reference;
    return Logic9::zero;
  }
  if (!state.initialized) {
    state.initialized = true;
    state.last_violation = false;
    state.test = test;
    state.reference = reference;
    if (test_event) state.test_event = now;
    if (reference_event) state.reference_event = now;
    return Logic9::zero;
  }

  bool violation{};
  const auto test_direction = direction(state.test, test);
  const auto reference_direction = direction(state.reference, reference);
  const bool selected_reference_event = reference_event
      && vital_edge_symbol_matches(
          state.reference, reference, operation.reference_edges);

  switch (operation.kind) {
    case VitalTimingCheckKind::setup_hold: {
      if (selected_reference_event) {
        state.setup_enabled =
            state.setup_enabled && operation.enables[1];
        state.hold_enabled = operation.enables[2];
      }
      if (test_event) {
        state.setup_enabled = operation.enables[0];
        state.hold_enabled =
            state.hold_enabled && operation.enables[3];
      }
      if (selected_reference_event) {
        if (test_event) state.test_event = now;
        if (state.setup_enabled) {
          violation = violation || too_soon(
              state.test_event, now,
              level_limit(test, operation.limits[0], operation.limits[1]));
        }
        state.setup_enabled = false;
        state.reference_event = now;
      } else if (test_event) {
        if (state.hold_enabled) {
          const auto hold_limit = vital_x01_ordinal(test) == 1U
              ? operation.limits[2]
              : vital_x01_ordinal(test) == 2U
                  ? operation.limits[3]
                  : std::max(operation.limits[2], operation.limits[3]);
          violation = violation || too_soon(
              state.reference_event, now, hold_limit);
        }
        state.hold_enabled = !violation;
        state.test_event = now;
      }
      break;
    }
    case VitalTimingCheckKind::recovery_removal: {
      if (selected_reference_event) {
        state.setup_enabled =
            state.setup_enabled && operation.enables[1];
        state.hold_enabled = operation.enables[2];
      }
      if (test_event) {
        state.setup_enabled = operation.enables[0];
        state.hold_enabled =
            state.hold_enabled && operation.enables[3];
      }
      const bool inactive = operation.active_low
          ? vital_x01_ordinal(test) == 2U
          : vital_x01_ordinal(test) == 1U;
      if (selected_reference_event) {
        if (test_event) state.test_event = now;
        if (state.setup_enabled && inactive) {
          violation = violation || too_soon(
              state.test_event, now, operation.limits[0]);
        }
        state.setup_enabled = false;
        state.reference_event = now;
      } else if (test_event) {
        if (state.hold_enabled && inactive) {
          violation = violation ||
              too_soon(state.reference_event, now, operation.limits[1]);
        }
        state.hold_enabled = !violation;
        state.test_event = now;
      }
      break;
    }
    case VitalTimingCheckKind::period_pulse: {
      if (test_event) {
        const auto edges = vital_edge_symbol_mask(state.test, test);
        const bool starts_high = (edges & (std::uint16_t{1U} << 2U)) != 0U;
        const bool starts_low = (edges & (std::uint16_t{1U} << 3U)) != 0U;
        const bool ends_low = (edges & (std::uint16_t{1U} << 6U)) != 0U;
        const bool ends_high = (edges & (std::uint16_t{1U} << 7U)) != 0U;
        if (starts_high) {
          violation = violation || too_soon(
              state.test_direction[0], now, operation.limits[0]);
          state.test_direction[0] = now;
        } else if (starts_low) {
          violation = violation || too_soon(
              state.test_direction[1], now, operation.limits[0]);
          state.test_direction[1] = now;
        }
        if (ends_low) {
          violation = violation || too_soon(
              state.test_direction[1], now, operation.limits[2]);
        } else if (ends_high) {
          violation = violation || too_soon(
              state.test_direction[0], now, operation.limits[1]);
        }
        if (edges != 0U) state.test_event = now;
      }
      break;
    }
    case VitalTimingCheckKind::in_phase_skew:
    case VitalTimingCheckKind::out_phase_skew: {
      const auto test_target = test_direction
          ? std::optional<std::size_t>{*test_direction}
          : std::nullopt;
      const auto reference_target = reference_direction
          ? std::optional<std::size_t>{2U + *reference_direction}
          : std::nullopt;
      if (test_target) state.skew_deadlines[*test_target].reset();
      if (reference_target) {
        state.skew_deadlines[*reference_target].reset();
      }

      if (trigger_event && state.scheduled_trigger
          && now >= *state.scheduled_trigger) {
        state.scheduled_trigger.reset();
      }
      for (auto& deadline : state.skew_deadlines) {
        if (deadline && now >= *deadline) {
          violation = true;
          deadline.reset();
        }
      }

      const bool opposite =
          operation.kind == VitalTimingCheckKind::out_phase_skew;
      const auto schedule = [&](const std::size_t target,
                                const SimulationTick limit) {
        if (limit == std::numeric_limits<SimulationTick>::max()) return;
        const auto deadline = limit <=
                std::numeric_limits<SimulationTick>::max() - now
            ? now + limit
            : std::numeric_limits<SimulationTick>::max();
        state.skew_deadlines[target] = deadline;
      };
      if (test_direction) {
        const auto selected = *test_direction;
        const auto target_direction = opposite ? 1U - selected : selected;
        const auto target = 2U + target_direction;
        if (!reference_direction
            || *reference_direction != target_direction) {
          schedule(target, operation.limits[selected == 0U ? 0U : 2U]);
        }
        state.test_direction[selected] = now;
        state.test_event = now;
      }
      if (reference_direction) {
        const auto selected = *reference_direction;
        const auto target_direction = opposite ? 1U - selected : selected;
        const auto target = target_direction;
        if (!test_direction || *test_direction != target_direction) {
          schedule(target, operation.limits[selected == 0U ? 1U : 3U]);
        }
        state.reference_direction[selected] = now;
        state.reference_event = now;
      }

      std::optional<SimulationTick> earliest;
      for (const auto deadline : state.skew_deadlines) {
        if (deadline && (!earliest || *deadline < *earliest)) {
          earliest = deadline;
        }
      }
      if (earliest
          && (!state.scheduled_trigger
              || *earliest < *state.scheduled_trigger)) {
        state.scheduled_trigger = earliest;
        state.trigger_request = earliest;
      }
      break;
    }
  }

  state.test = test;
  state.reference = reference;
  state.last_violation = violation;
  return violation && operation.x_on ? Logic9::x : Logic9::zero;
}

Logic9 Interpreter::Impl::execute_vital_timing_check(
    const ProcessId process,
    const InstructionIndex instruction,
    const VitalTimingCheck& operation) {
  auto& state = get_process(process).vital_timing_states[instruction];
  const auto event = [&](const SignalId signal) {
    (void)get_signal(signal);
    const auto& stamp = signal_events[signal];
    return stamp && stamp->first == scheduler.now()
        && stamp->second == scheduler.delta();
  };
  const auto& test_value = get_signal(operation.test_signal).initial_value;
  const auto reference_signal =
      operation.reference_signal.value_or(operation.test_signal);
  const auto& reference_value = get_signal(reference_signal).initial_value;
  const auto result = simir::evaluate_vital_timing_check(
      operation,
      state,
      scheduler.now(),
      test_value.get_logic9(operation.test_offset),
      event(operation.test_signal),
      reference_value.get_logic9(operation.reference_offset),
      operation.reference_signal && event(reference_signal),
      operation.trigger_signal && event(*operation.trigger_signal));
  if (operation.trigger_signal && state.trigger_request) {
    PackedLogic4 trigger(1U);
    state.trigger_level = !state.trigger_level;
    trigger.fill(state.trigger_level ? Logic9::one : Logic9::zero);
    scheduler.schedule_after(
        *state.trigger_request - scheduler.now(),
        SchedulerPhase::update,
        process,
        [this, process, signal = *operation.trigger_signal,
         value = std::move(trigger)](Scheduler&) mutable {
          stage_update(process, signal, std::move(value));
        });
  }
  if (state.last_violation && operation.message_on && report_hook) {
    report_hook(
        process,
        operation.message,
        operation.severity,
        operation.source,
        scheduler.now(),
        scheduler.delta());
  }
  return result;
}

void Interpreter::Impl::execute_vital_delay(
    const ProcessId process,
    const InstructionIndex instruction,
    const VitalDelay& operation,
    const VitalDelayRuntimeValues& values) {
  auto& state = get_process(process).vital_delay_states[instruction];
  auto delay = vital_transition_delay(
      state.last_value, values.source,
      operation.shape, values.default_delays);
  bool selected{};
  for (const auto& path : values.paths) {
    if (!path.condition
        || path.input_change_time
            == std::numeric_limits<SimulationTick>::max()) {
      continue;
    }
    const auto path_delay = vital_transition_delay(
        state.last_value, values.source, operation.shape, path.delays);
    const auto remaining = path_delay > path.input_change_time
        ? path_delay - path.input_change_time : SimulationTick{};
    if (!selected || remaining < delay) delay = remaining;
    selected = true;
  }
  if (operation.kind == VitalDelayKind::path && !selected
      && operation.ignore_default_delay) {
    state.last_value = values.source;
    return;
  }
  if (delay > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
    throw std::overflow_error{
        "simulation time overflow while scheduling a VITAL delay"};
  }
  auto output = values.source;
  if (operation.shape == VitalDelayShape::delay01z) {
    const auto ordinal = static_cast<std::size_t>(values.source);
    if (ordinal >= values.output_map.size()) {
      throw std::invalid_argument{"VITAL output map index is outside its range"};
    }
    output = values.output_map[ordinal];
  }
  const auto now = scheduler.now();
  const auto target = now + delay;
  const bool glitch = operation.kind == VitalDelayKind::path
      && state.initialized && output != state.scheduled_value
      && now < state.scheduled_time;
  state.last_glitch = glitch;
  if (glitch) state.glitch_time = now;
  if (glitch && operation.message_on && report_hook) {
    report_hook(
        process, operation.message, operation.severity,
        operation.source_location, now, scheduler.delta());
  }
  if (glitch && operation.reject_fast_path && target < state.scheduled_time) {
    state.last_value = values.source;
    return;
  }
  if (glitch && !operation.negative_preemption
      && target < state.scheduled_time) {
    delay = state.scheduled_time - now;
  }
  if (glitch && operation.x_on
      && operation.mode == VitalGlitchMode::on_detect) {
    std::vector<ProjectedWaveformValue> waveform;
    waveform.push_back({vital_scalar(Logic9::x), 0U});
    if (delay != 0U) waveform.push_back({vital_scalar(output), delay});
    schedule_projected_waveform(
        process, operation.output, waveform, std::nullopt, 0U,
        ProjectedDelayMode::transport);
    state.scheduled_value = delay == 0U ? Logic9::x : output;
    state.scheduled_time = now + delay;
  } else if (glitch && operation.x_on
             && operation.mode == VitalGlitchMode::on_event) {
    const auto event_delay = state.scheduled_time - now;
    std::vector<ProjectedWaveformValue> waveform;
    waveform.push_back({vital_scalar(Logic9::x), std::min(event_delay, delay)});
    if (delay > event_delay) waveform.push_back({vital_scalar(output), delay});
    schedule_projected_waveform(
        process, operation.output, waveform, std::nullopt, 0U,
        ProjectedDelayMode::transport);
    state.scheduled_value = delay > event_delay ? output : Logic9::x;
    state.scheduled_time = now + delay;
  } else {
    const auto mode = operation.mode == VitalGlitchMode::inertial
        ? ProjectedDelayMode::inertial : ProjectedDelayMode::transport;
    schedule_projected(
        process, operation.output, vital_scalar(output), std::nullopt,
        delay, mode == ProjectedDelayMode::inertial ? delay : 0U, mode);
    state.scheduled_value = output;
    state.scheduled_time = now + delay;
  }
  state.initialized = true;
  state.last_value = values.source;
}

void Interpreter::Impl::execute_vital_delay_operation(
    const ProcessId process_id,
    ProcessState& process,
    const InstructionIndex instruction,
    const VitalDelay& operation) {
  const auto tick = [&](const RegisterId id) {
    const auto word = get_register(process, id).low_word();
    if (word.bval != 0U) {
      throw std::invalid_argument{"VITAL delay contains an unknown time value"};
    }
    return static_cast<SimulationTick>(word.aval);
  };
  VitalDelayRuntimeValues values;
  values.source = get_register(process, operation.source).get_logic9(0U);
  for (std::size_t index = 0; index < 6U; ++index) {
    values.default_delays[index] = tick(operation.default_delays[index]);
  }
  if (operation.shape == VitalDelayShape::delay01z) {
    const auto& map = get_register(process, operation.output_map);
    for (std::size_t index = 0; index < 9U; ++index) {
      values.output_map[index] = map.get_logic9(8U - index);
    }
  }
  values.paths.reserve(operation.paths.size());
  for (const auto& path : operation.paths) {
    VitalPathRuntimeValue value;
    value.input_change_time = tick(path.input_change_time);
    const auto condition = get_register(process, path.condition).low_word();
    if (condition.bval != 0U) {
      throw std::invalid_argument{
          "VITAL path condition contains an unknown value"};
    }
    value.condition = (condition.aval & 1U) != 0U;
    for (std::size_t index = 0; index < 6U; ++index) {
      value.delays[index] = tick(path.delays[index]);
    }
    values.paths.push_back(value);
  }
  execute_vital_delay(process_id, instruction, operation, values);
}

}  // namespace fsim::runtime::simir
