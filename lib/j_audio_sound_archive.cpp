#include <aurora/j_audio_sound_archive.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace aurora::audio {
namespace {
constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U |
         static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U |
         static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U |
         static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

[[noreturn]] void malformed(std::string_view detail) {
  throw std::runtime_error("Malformed JAudio sound-archive resource: " + std::string(detail));
}

class Reader final {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : Reader(bytes, 0U, bytes.size()) {}

  Reader(std::span<const std::uint8_t> bytes, std::size_t begin, std::size_t length)
  : _bytes(bytes), _begin(begin), _end(begin) {
    if (begin > bytes.size() || length > bytes.size() - begin) {
      malformed("resource segment extends outside its containing archive");
    }
    _end = begin + length;
  }

  [[nodiscard]] std::size_t size() const { return _end - _begin; }

  void require(std::size_t offset, std::size_t length, std::string_view detail) const {
    if (offset < _begin || offset > _end || length > _end - offset) {
      malformed(detail);
    }
  }

  [[nodiscard]] std::uint8_t u8(std::size_t offset, std::string_view detail) const {
    require(offset, 1U, detail);
    return _bytes[offset];
  }

  [[nodiscard]] std::uint16_t u16(std::size_t offset, std::string_view detail) const {
    require(offset, 2U, detail);
    return static_cast<std::uint16_t>(_bytes[offset]) << 8U | static_cast<std::uint16_t>(_bytes[offset + 1U]);
  }

  [[nodiscard]] std::int16_t s16(std::size_t offset, std::string_view detail) const {
    return std::bit_cast<std::int16_t>(u16(offset, detail));
  }

  [[nodiscard]] std::uint32_t u24(std::size_t offset, std::string_view detail) const {
    require(offset, 3U, detail);
    return static_cast<std::uint32_t>(_bytes[offset]) << 16U | static_cast<std::uint32_t>(_bytes[offset + 1U]) << 8U |
           static_cast<std::uint32_t>(_bytes[offset + 2U]);
  }

  [[nodiscard]] std::uint32_t u32(std::size_t offset, std::string_view detail) const {
    require(offset, 4U, detail);
    return static_cast<std::uint32_t>(_bytes[offset]) << 24U | static_cast<std::uint32_t>(_bytes[offset + 1U]) << 16U |
           static_cast<std::uint32_t>(_bytes[offset + 2U]) << 8U | static_cast<std::uint32_t>(_bytes[offset + 3U]);
  }

  [[nodiscard]] float f32(std::size_t offset, std::string_view detail) const {
    return std::bit_cast<float>(u32(offset, detail));
  }

  [[nodiscard]] std::span<const std::uint8_t> slice(std::size_t offset, std::size_t length,
                                                    std::string_view detail) const {
    require(offset, length, detail);
    return _bytes.subspan(offset, length);
  }

  [[nodiscard]] std::string string(std::size_t offset, std::size_t maximum_length, std::string_view detail) const {
    require(offset, maximum_length, detail);
    const auto region = _bytes.subspan(offset, maximum_length);
    const auto terminator = std::ranges::find(region, std::uint8_t{0});
    if (terminator == region.end()) {
      malformed(detail);
    }
    return std::string(reinterpret_cast<const char*>(region.data()),
                       static_cast<std::size_t>(terminator - region.begin()));
  }

private:
  std::span<const std::uint8_t> _bytes;
  std::size_t _begin = 0;
  std::size_t _end = 0;
};

struct Segment {
  std::size_t offset = 0;
  std::size_t size = 0;
};

struct BankRecord {
  std::uint32_t wave_bank_index = 0;
  Segment data;
};

struct TrackRecipe {
  std::uint8_t bank = 0;
  std::uint8_t program = 0;
  std::uint8_t note = 0;
  std::uint8_t velocity = 0;
  std::uint16_t direct_release = 0;
  double start_delay_seconds = 0.0;
  double gate_seconds = 0.0;
  float track_gain = 1.0F;
  float track_pitch = 1.0F;
  float track_pan = 0.5F;
  std::optional<std::uint8_t> sweep_start_note;
  bool waits_for_sample_completion = false;
  std::vector<std::size_t> completion_dependencies;
};

struct InstrumentRecipe {
  std::uint16_t wave_id = 0;
  float volume = 1.0F;
  float pitch = 1.0F;
  float pan = 0.5F;
  double attack_seconds = 0.0;
  EnvelopeCurve attack_curve = EnvelopeCurve::Linear;
  float attack_peak = 1.0F;
  double release_seconds = 0.0;
  EnvelopeCurve release_curve = EnvelopeCurve::Linear;
  std::uint16_t direct_release = 0U;
};

struct WaveRecipe {
  std::string archive_name;
  std::uint8_t format = 0;
  std::uint8_t base_key = 60;
  float sample_rate = 0.0F;
  std::uint32_t archive_offset = 0;
  std::uint32_t archive_length = 0;
  std::uint32_t loop_start = 0;
  std::uint32_t loop_end = 0;
  std::uint32_t sample_count = 0;
  std::int16_t loop_yn1 = 0;
  std::int16_t loop_yn2 = 0;
  bool looping = false;
};

[[nodiscard]] std::size_t checked_add(std::size_t a, std::size_t b, std::string_view detail) {
  if (b > std::numeric_limits<std::size_t>::max() - a) {
    malformed(detail);
  }
  return a + b;
}

[[nodiscard]] std::size_t checked_multiply(std::size_t a, std::size_t b, std::string_view detail) {
  if (a != 0U && b > std::numeric_limits<std::size_t>::max() / a) {
    malformed(detail);
  }
  return a * b;
}

void require_table(const Reader& reader, std::size_t offset, std::size_t count, std::size_t stride,
                   std::string_view detail) {
  reader.require(offset, checked_multiply(count, stride, detail), detail);
}

[[nodiscard]] std::size_t relative(std::size_t base, std::uint32_t offset, const Reader& reader,
                                   std::string_view detail) {
  const auto result = checked_add(base, static_cast<std::size_t>(offset), detail);
  reader.require(result, 1U, detail);
  return result;
}

[[nodiscard]] std::pair<std::uint32_t, std::size_t> read_midi_value(const Reader& reader, std::size_t cursor) {
  auto value = std::uint32_t{0};
  for (auto index = 0U; index != 4U; ++index) {
    const auto byte = reader.u8(cursor++, "truncated BSC MIDI value");
    value = value << 7U | static_cast<std::uint32_t>(byte & 0x7fU);
    if ((byte & 0x80U) == 0U) {
      return {value, cursor};
    }
  }
  malformed("BSC MIDI value exceeds JASSeqReader's four-byte limit");
}

[[nodiscard]] EnvelopeCurve curve_from_jaudio(std::int16_t curve) {
  switch (curve) {
  case 0:
    return EnvelopeCurve::Linear;
  case 3:
    return EnvelopeCurve::JAudioSampleCell;
  default:
    malformed("unsupported JAudio oscillator curve");
  }
}

} // namespace

struct JAudioSoundArchive::Impl {
  explicit Impl(std::span<const std::uint8_t> data, WaveArchiveLoader loader)
  : baa(data.begin(), data.end()), wave_loader(std::move(loader)) {
    if (!wave_loader) {
      throw std::invalid_argument("JAudio wave archive loader is required");
    }
    parse_baa();
  }

  void parse_baa() {
    const auto reader = Reader{baa};
    if (reader.u32(0U, "missing BAA header") != fourcc('A', 'A', '_', '<')) {
      malformed("invalid BAA header");
    }

    auto cursor = std::size_t{4};
    while (true) {
      const auto command = reader.u32(cursor, "truncated BAA command");
      cursor += 4U;
      if (command == fourcc('>', '_', 'A', 'A')) {
        break;
      }
      switch (command) {
      case fourcc('w', 's', ' ', ' '): {
        const auto index = reader.u32(cursor, "truncated BAA WS command");
        const auto offset = reader.u32(cursor + 4U, "truncated BAA WS command");
        (void)reader.u32(cursor + 8U, "truncated BAA WS command");
        const auto absolute = relative(0U, offset, reader, "invalid WSYS offset");
        const auto size = reader.u32(absolute + 4U, "truncated WSYS header");
        reader.require(absolute, size, "WSYS extends outside BAA");
        wave_banks[index] = Segment{absolute, size};
        cursor += 12U;
        break;
      }
      case fourcc('b', 'n', 'k', ' '): {
        const auto wave_bank = reader.u32(cursor, "truncated BAA BNK command");
        const auto offset = reader.u32(cursor + 4U, "truncated BAA BNK command");
        const auto absolute = relative(0U, offset, reader, "invalid IBNK offset");
        const auto size = reader.u32(absolute + 4U, "truncated IBNK header");
        reader.require(absolute, size, "IBNK extends outside BAA");
        const auto internal_number = reader.u32(absolute + 8U, "truncated IBNK bank number");
        banks[internal_number] = BankRecord{wave_bank, {absolute, size}};
        cursor += 8U;
        break;
      }
      case fourcc('b', 's', 'c', ' '):
      case fourcc('b', 's', 't', ' '):
      case fourcc('b', 's', 't', 'n'): {
        const auto begin = reader.u32(cursor, "truncated BAA table command");
        const auto end = reader.u32(cursor + 4U, "truncated BAA table command");
        if (end < begin) {
          malformed("backwards BAA table range");
        }
        reader.require(begin, end - begin, "BAA table extends outside archive");
        const auto segment = Segment{begin, end - begin};
        if (command == fourcc('b', 's', 'c', ' ')) {
          bsc = segment;
        } else if (command == fourcc('b', 's', 't', ' ')) {
          bst = segment;
        } else {
          bstn = segment;
        }
        cursor += 8U;
        break;
      }
      case fourcc('b', 'l', '_', '<'):
        reader.require(cursor, 8U, "truncated BAA bank-list command");
        cursor += 8U;
        break;
      case fourcc('>', '_', 'b', 'l'):
        break;
      case fourcc('b', 'm', 's', ' '):
        reader.require(cursor, 12U, "truncated BAA BMS command");
        cursor += 12U;
        break;
      case fourcc('b', 'm', 's', 'a'):
      case fourcc('d', 's', 'q', 'b'):
      case fourcc('b', 's', 'f', 't'):
      case fourcc('s', 'e', 'c', 't'):
        reader.require(cursor, 4U, "truncated BAA command argument");
        cursor += 4U;
        break;
      case fourcc('v', 'b', 'n', 'k'):
        reader.require(cursor, 8U, "truncated BAA voice-bank command");
        cursor += 8U;
        break;
      default:
        malformed("unsupported BAA command");
      }
    }

    if (bst.size == 0U || bstn.size == 0U || bsc.size == 0U) {
      malformed("BAA is missing BST, BSTN, or BSC data");
    }
  }

  [[nodiscard]] std::optional<std::uint32_t> find_sound_id(std::string_view wanted) const {
    const auto reader = Reader{baa, bstn.offset, bstn.size};
    const auto base = bstn.offset;
    if (reader.u32(base, "truncated BSTN") != fourcc('B', 'S', 'T', 'N')) {
      malformed("invalid BSTN header");
    }
    const auto root = relative(base, reader.u32(base + 12U, "truncated BSTN root"), reader, "invalid BSTN root");
    const auto section_count = reader.u32(root, "truncated BSTN section count");
    require_table(reader, root + 4U, section_count, 4U, "truncated BSTN section table");
    for (auto section = std::uint32_t{0}; section < section_count; ++section) {
      const auto section_offset =
          reader.u32(root + 4U + static_cast<std::size_t>(section) * 4U, "truncated BSTN section table");
      if (section_offset == 0U) {
        continue;
      }
      const auto section_data = relative(base, section_offset, reader, "invalid BSTN section offset");
      const auto group_count = reader.u32(section_data, "truncated BSTN group count");
      require_table(reader, section_data + 8U, group_count, 4U, "truncated BSTN group table");
      for (auto group = std::uint32_t{0}; group < group_count; ++group) {
        const auto group_offset =
            reader.u32(section_data + 8U + static_cast<std::size_t>(group) * 4U, "truncated BSTN group table");
        if (group_offset == 0U) {
          continue;
        }
        const auto group_data = relative(base, group_offset, reader, "invalid BSTN group offset");
        const auto item_count = reader.u32(group_data, "truncated BSTN item count");
        require_table(reader, group_data + 8U, item_count, 4U, "truncated BSTN item table");
        for (auto item = std::uint32_t{0}; item < item_count; ++item) {
          const auto name_offset =
              reader.u32(group_data + 8U + static_cast<std::size_t>(item) * 4U, "truncated BSTN item table");
          if (name_offset == 0U) {
            continue;
          }
          const auto name_data = relative(base, name_offset, reader, "invalid BSTN name offset");
          const auto name =
              reader.string(name_data, bstn.offset + bstn.size - name_data, "unterminated BSTN sound name");
          if (name == wanted) {
            if (section > 0xffU || group > 0xffU || item > 0xffffU) {
              malformed("BSTN identity exceeds JAISoundID fields");
            }
            return section << 24U | group << 16U | item;
          }
        }
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] JAudioSoundMetadata sound_metadata(std::uint32_t sound_id) const {
    const auto reader = Reader{baa, bst.offset, bst.size};
    const auto base = bst.offset;
    if (reader.u32(base, "truncated BST") != fourcc('B', 'S', 'T', ' ')) {
      malformed("invalid BST header");
    }
    const auto root = relative(base, reader.u32(base + 12U, "truncated BST root"), reader, "invalid BST root");
    const auto section_count = reader.u32(root, "truncated BST section count");
    require_table(reader, root + 4U, section_count, 4U, "truncated BST section table");
    const auto section = sound_id >> 24U;
    const auto group = sound_id >> 16U & 0xffU;
    const auto item = sound_id & 0xffffU;
    if (section >= section_count) {
      malformed("sound ID section is outside BST");
    }
    const auto section_data =
        relative(base, reader.u32(root + 4U + static_cast<std::size_t>(section) * 4U, "truncated BST section table"),
                 reader, "invalid BST section offset");
    const auto group_count = reader.u32(section_data, "truncated BST group count");
    require_table(reader, section_data + 4U, group_count, 4U, "truncated BST group table");
    if (group >= group_count) {
      malformed("sound ID group is outside BST");
    }
    const auto group_data = relative(
        base, reader.u32(section_data + 4U + static_cast<std::size_t>(group) * 4U, "truncated BST group table"), reader,
        "invalid BST group offset");
    const auto item_count = reader.u32(group_data, "truncated BST item count");
    require_table(reader, group_data + 8U, item_count, 4U, "truncated BST item table");
    if (item >= item_count) {
      malformed("sound ID item is outside BST");
    }
    const auto item_entry =
        reader.u32(group_data + 8U + static_cast<std::size_t>(item) * 4U, "truncated BST item table");
    const auto item_data = relative(base, item_entry & 0x00ffffffU, reader, "invalid BST item offset");
    auto result = JAudioSoundMetadata{
        .sound_id = sound_id,
        .priority = reader.u8(item_data, "truncated BST sound item"),
        .volume = reader.u8(item_data + 1U, "truncated BST sound item"),
    };
    switch (item_entry >> 24U & 0xf0U) {
    case 0x50U:
      result.kind = JAudioSoundKind::SoundEffect;
      if (section != 0U) {
        malformed("BST SE item is outside the SE sound-ID section");
      }
      reader.require(item_data, 6U, "truncated BST SE item");
      break;
    case 0x60U:
      result.kind = JAudioSoundKind::Sequence;
      if (section != 1U) {
        malformed("BST sequence item is outside the sequence sound-ID section");
      }
      reader.require(item_data, 8U, "truncated BST sequence item");
      result.resource_id = reader.u16(item_data + 2U, "truncated BST sequence resource ID");
      result.chord_resource_id = reader.u16(item_data + 4U, "truncated BST chord resource ID");
      break;
    case 0x70U: {
      result.kind = JAudioSoundKind::Stream;
      if (section != 2U) {
        malformed("BST stream item is outside the stream sound-ID section");
      }
      reader.require(item_data, 8U, "truncated BST stream item");
      result.channel_control = reader.u16(item_data + 2U, "truncated BST stream channel control");
      const auto path_offset = reader.u32(item_data + 4U, "truncated BST stream path offset");
      const auto path_data = relative(base, path_offset, reader, "invalid BST stream path offset");
      result.stream_path = reader.string(path_data, bst.offset + bst.size - path_data, "unterminated BST stream path");
      if (result.stream_path.empty()) {
        malformed("BST stream path is empty");
      }
      break;
    }
    default:
      malformed("unsupported BST sound-item type");
    }
    return result;
  }

  [[nodiscard]] std::pair<std::uint8_t, std::uint8_t> sound_table_properties(std::uint32_t sound_id) const {
    const auto metadata = sound_metadata(sound_id);
    if (metadata.kind != JAudioSoundKind::SoundEffect) {
      malformed("requested level sound is not a JAudio SE sequence");
    }
    return {metadata.priority, metadata.volume};
  }

  [[nodiscard]] Reader bsc_reader() const {
    const auto segment_reader = Reader{baa, bsc.offset, bsc.size};
    const auto file_size = segment_reader.u32(bsc.offset + 4U, "truncated BSC size");
    if (file_size < 8U || file_size > bsc.size) {
      malformed("BSC declared size exceeds its BAA segment");
    }
    return Reader{baa, bsc.offset, file_size};
  }

  [[nodiscard]] std::size_t sequence_offset(std::uint32_t sound_id) const {
    const auto reader = bsc_reader();
    const auto base = bsc.offset;
    if (reader.u16(base, "truncated BSC") != 0x5343U) {
      malformed("invalid BSC header");
    }
    const auto group = sound_id >> 16U & 0xffU;
    const auto item = sound_id & 0xffffU;
    const auto group_count = reader.u16(base + 2U, "truncated BSC group count");
    const auto file_size = reader.u32(base + 4U, "truncated BSC size");
    if (file_size > bsc.size) {
      malformed("BSC declared size exceeds its BAA segment");
    }
    if (group >= group_count) {
      malformed("sound ID group is outside BSC");
    }
    require_table(reader, base + 8U, group_count, 4U, "truncated BSC group table");
    const auto group_data =
        relative(base, reader.u32(base + 8U + static_cast<std::size_t>(group) * 4U, "truncated BSC group table"),
                 reader, "invalid BSC group offset");
    const auto item_count = reader.u32(group_data, "truncated BSC item count");
    require_table(reader, group_data + 4U, item_count, 4U, "truncated BSC sequence table");
    if (item >= item_count) {
      malformed("sound ID item is outside BSC");
    }
    return relative(base,
                    reader.u32(group_data + 4U + static_cast<std::size_t>(item) * 4U, "truncated BSC sequence table"),
                    reader, "invalid BSC sequence offset");
  }

  void execute_track_setup(std::size_t& cursor, std::vector<std::size_t>& calls, TrackRecipe& track, double& wait_ticks,
                           bool stop_at_note) const {
    const auto reader = bsc_reader();
    for (auto instructions = std::size_t{0}; instructions != 512U; ++instructions) {
      const auto command_offset = cursor;
      const auto command = reader.u8(cursor++, "truncated BSC track");
      if (command < 0x80U) {
        const auto control = reader.u8(cursor++, "truncated BSC note control");
        const auto velocity = reader.u8(cursor++, "truncated BSC note velocity");
        if ((control & 0x07U) != 0U || (control & 0x40U) == 0U) {
          malformed("level-sound track does not use JAudio's persistent gate channel");
        }
        const auto [duration, after_duration] = read_midi_value(reader, cursor);
        (void)duration;
        cursor = after_duration;
        track.note = command;
        track.velocity = velocity;
        if (!stop_at_note) {
          malformed("a BSC setup subroutine unexpectedly starts a note");
        }
        if (reader.u8(cursor++, "truncated BSC level-sound loop") != 0xc7U ||
            reader.u24(cursor, "truncated BSC level-sound jump") != command_offset - bsc.offset) {
          malformed("level-sound note is not a persistent self-loop");
        }
        cursor += 3U;
        return;
      }

      switch (command) {
      case 0xc3U:
        if (calls.size() == 8U) {
          malformed("BSC call stack exceeds JASSeqReader limit");
        }
        calls.push_back(cursor + 3U);
        cursor = relative(bsc.offset, reader.u24(cursor, "truncated BSC call"), reader, "invalid BSC call target");
        break;
      case 0xc5U:
        if (calls.empty()) {
          if (stop_at_note) {
            malformed("level-sound track returns before starting a note");
          }
          return;
        }
        cursor = calls.back();
        calls.pop_back();
        break;
      case 0xd8U: {
        const auto reg = reader.u8(cursor++, "truncated BSC register load");
        const auto value = reader.u16(cursor, "truncated BSC register load");
        cursor += 2U;
        if (reg == 0x67U) {
          track.bank = static_cast<std::uint8_t>(value);
        } else if (reg == 0x68U) {
          track.program = static_cast<std::uint8_t>(value);
        } else if (reg == 0x6dU) {
          track.direct_release = value;
        }
        break;
      }
      case 0xe1U: {
        const auto value = reader.u16(cursor, "truncated BSC bank/program command");
        cursor += 2U;
        track.bank = static_cast<std::uint8_t>(value >> 8U);
        track.program = static_cast<std::uint8_t>(value);
        break;
      }
      case 0xeaU:
        reader.require(cursor, 3U, "truncated BSC bus-connect command");
        cursor += 3U;
        break;
      case 0xf0U:
        wait_ticks += reader.u8(cursor++, "truncated BSC byte wait");
        break;
      default:
        malformed("unsupported command in JAudio level-sound track");
      }
    }
    malformed("BSC level-sound track exceeds instruction limit");
  }

  [[nodiscard]] std::vector<TrackRecipe> sequence_tracks(std::uint32_t sound_id) const {
    const auto reader = bsc_reader();
    auto cursor = sequence_offset(sound_id);
    auto children = std::vector<std::size_t>{};
    auto terminated = false;
    for (auto instructions = std::size_t{0}; instructions != 32U; ++instructions) {
      const auto command = reader.u8(cursor++, "truncated BSC root track");
      if (command == 0xc1U) {
        (void)reader.u8(cursor++, "truncated BSC open-track index");
        children.push_back(relative(bsc.offset, reader.u24(cursor, "truncated BSC open-track target"), reader,
                                    "invalid BSC child-track target"));
        cursor += 3U;
      } else if (command == 0xc3U) {
        // The retail root enters its keep-alive helper only after
        // opening every child. It cannot change inherited child state.
        if (children.empty()) {
          malformed("BSC root calls a helper before opening level tracks");
        }
        (void)relative(bsc.offset, reader.u24(cursor, "truncated BSC root call"), reader,
                       "invalid BSC root call target");
        cursor += 3U;
      } else if (command == 0xffU) {
        terminated = true;
        break;
      } else {
        malformed("unsupported JAudio level-sound root sequence");
      }
    }
    if (!terminated) {
      malformed("BSC level-sound root lacks an end command");
    }
    if (children.empty()) {
      malformed("JAudio level sound has no child tracks");
    }

    auto result = std::vector<TrackRecipe>{};
    result.reserve(children.size());
    for (auto child : children) {
      auto track = TrackRecipe{};
      auto calls = std::vector<std::size_t>{};
      auto wait_ticks = 0.0;
      execute_track_setup(child, calls, track, wait_ticks, true);
      track.start_delay_seconds = wait_ticks * 60.0 / (120.0 * 48.0);
      if (track.direct_release == 0U) {
        malformed("level-sound track lacks a concrete release duration");
      }
      result.push_back(track);
    }
    return result;
  }

  struct FiniteSequenceState {
    std::uint16_t bank = 0U;
    std::uint16_t program = 0xf0U;
    std::uint16_t direct_release = 0U;
    std::uint16_t tempo = 120U;
    std::uint16_t timebase = 48U;
    std::uint16_t gate_rate = 100U;
    std::uint16_t bend_sense = 12U;
    std::int8_t transpose = 0;
    std::uint8_t latest_note = 60U;
    float volume = 1.0F;
    float pitch_bend = 0.0F;
    float pan = 0.5F;
    double elapsed_seconds = 0.0;
    std::array<std::uint16_t, 0x40U> registers{};
    std::uint16_t condition_value = 0U;
    std::uint16_t open_child_mask = 0U;
    std::vector<std::size_t> completion_dependencies;
  };

  [[nodiscard]] static double ticks_to_seconds(const FiniteSequenceState& state, std::uint32_t ticks) {
    if (state.tempo == 0U || state.timebase == 0U) {
      malformed("BSC track has a zero tempo or timebase");
    }
    return static_cast<double>(ticks) * 60.0 / (static_cast<double>(state.tempo) * static_cast<double>(state.timebase));
  }

  [[nodiscard]] static std::uint16_t sequence_register(const FiniteSequenceState& state, std::uint8_t reg) {
    if (reg < state.registers.size()) {
      return state.registers[reg];
    }
    switch (reg) {
    case 0x60U:
      return state.open_child_mask;
    case 0x62U:
      return state.timebase;
    case 0x63U:
      return static_cast<std::uint8_t>(state.transpose);
    case 0x64U:
      return state.bend_sense;
    case 0x65U:
      return state.gate_rate;
    case 0x67U:
      return state.bank;
    case 0x68U:
      return state.program;
    case 0x6dU:
      return state.direct_release;
    default:
      return 0U;
    }
  }

  static void write_sequence_register(FiniteSequenceState& state, std::uint8_t reg, std::uint16_t value) {
    state.condition_value = value;
    if (reg < state.registers.size()) {
      state.registers[reg] = value;
      return;
    }
    switch (reg) {
    case 0x62U:
      state.timebase = value;
      break;
    case 0x63U:
      state.transpose = static_cast<std::int8_t>(value);
      break;
    case 0x64U:
      state.bend_sense = value;
      break;
    case 0x65U:
      state.gate_rate = value;
      break;
    case 0x67U:
      state.bank = value;
      break;
    case 0x68U:
      state.program = value;
      break;
    case 0x6dU:
      state.direct_release = value;
      break;
    default:
      break;
    }
  }

  static void apply_register_operation(FiniteSequenceState& state, std::uint8_t operation, std::uint8_t reg,
                                       std::uint16_t operand) {
    const auto current = sequence_register(state, reg);
    auto destination = reg;
    auto value = operand;
    switch (operation) {
    case 0U:
      break;
    case 1U:
      value = static_cast<std::uint16_t>(current + operand);
      break;
    case 2U:
      value = static_cast<std::uint16_t>(current - operand);
      break;
    case 3U:
      value = static_cast<std::uint16_t>(current - operand);
      destination = 3U;
      break;
    case 5U:
      value = static_cast<std::uint16_t>(current & operand);
      break;
    case 6U:
      value = static_cast<std::uint16_t>(current | operand);
      break;
    case 7U:
      value = static_cast<std::uint16_t>(current ^ operand);
      break;
    case 9U:
      if (operand >= std::numeric_limits<std::uint16_t>::digits) {
        malformed("BSC left-shift operand exceeds the sequence-register width");
      }
      value = static_cast<std::uint16_t>(static_cast<std::uint32_t>(current) << operand);
      break;
    case 10U:
      if (operand >= std::numeric_limits<std::uint16_t>::digits) {
        malformed("BSC right-shift operand exceeds the sequence-register width");
      }
      value = static_cast<std::uint16_t>(static_cast<std::uint32_t>(current) >> operand);
      break;
    default:
      malformed("unsupported stateful BSC register operation");
    }
    write_sequence_register(state, destination, value);
  }

  [[nodiscard]] static bool sequence_condition(std::uint8_t condition, std::uint16_t value) {
    switch (condition) {
    case 0U:
      return true;
    case 1U:
      return value == 0U;
    case 2U:
      return value != 0U;
    case 3U:
      return value == 1U;
    case 4U:
      return value >= 0x8000U;
    case 5U:
      return value < 0x8000U;
    default:
      malformed("invalid BSC branch condition");
    }
  }

  static void set_sequence_parameter(FiniteSequenceState& state, std::uint8_t parameter, float value,
                                     std::uint16_t transition_ticks) {
    if (transition_ticks != 0U) {
      malformed("timed BSC track parameters are not representable in a static PCM recipe");
    }
    switch (parameter) {
    case 0U:
      state.volume = value;
      break;
    case 1U:
      state.pitch_bend = value;
      break;
    case 2U: // FX mix; the PCM recipe preserves the dry retail voice.
      break;
    case 3U:
      state.pan = value;
      break;
    case 4U: // Dolby; stereo output preserves the dry retail voice.
    case 5U: // Distance filter; handled by the original DSP, not the wave recipe.
      break;
    default:
      malformed("invalid BSC track parameter index");
    }
  }

  void execute_finite_sequence_track(std::size_t entry, FiniteSequenceState state, std::vector<TrackRecipe>& notes,
                                     std::size_t recursion_depth) const {
    if (recursion_depth > 16U) {
      malformed("BSC child-track nesting exceeds JASTrack limits");
    }
    const auto reader = bsc_reader();
    auto cursor = entry;
    auto calls = std::vector<std::size_t>{};
    struct LoopState {
      std::size_t cursor = 0U;
      std::uint16_t remaining = 0U;
    };
    auto loops = std::vector<LoopState>{};
    auto visited = std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::size_t>{};

    for (auto instructions = std::size_t{0}; instructions != 16384U; ++instructions) {
      const auto return_cursor = calls.empty() ? 0U : calls.back();
      const auto key = std::tuple{cursor, calls.size(), return_cursor};
      const auto [visit, inserted] = visited.emplace(key, notes.size());
      if (!inserted) {
        if (visit->second == notes.size()) {
          // Retail roots use a register-controlled, note-free
          // keep-alive loop while their already-open children run.
          return;
        }
        malformed("finite BSC sequence repeats control flow after emitting notes");
      }

      const auto command_offset = cursor;
      const auto command = reader.u8(cursor++, "truncated BSC track");
      if (command < 0x80U) {
        const auto control = reader.u8(cursor++, "truncated BSC note control");
        const auto velocity = reader.u8(cursor++, "truncated BSC note velocity");
        if ((control & 0x07U) != 0U) {
          malformed("finite BSC MIDI-channel note requires an explicit note-off scheduler (note=" +
                    std::to_string(command) + ", control=" + std::to_string(control) + ")");
        }
        const auto [duration, after_duration] = read_midi_value(reader, cursor);
        cursor = after_duration;
        if (state.bank > 0xffU || state.program > 0xffU) {
          malformed("BSC bank or program exceeds the retail eight-bit range");
        }
        const auto transposed = static_cast<int>(command) + state.transpose;
        if (transposed < 0 || transposed > 127) {
          malformed("BSC note transpose leaves the MIDI key range");
        }
        if (state.gate_rate != 0U &&
            duration > std::numeric_limits<std::uint32_t>::max() / static_cast<std::uint32_t>(state.gate_rate)) {
          malformed("BSC note gate duration overflows the JAS tick range");
        }
        const auto gate_ticks = duration * static_cast<std::uint32_t>(state.gate_rate);
        notes.push_back(TrackRecipe{
            .bank = static_cast<std::uint8_t>(state.bank),
            .program = static_cast<std::uint8_t>(state.program),
            .note = static_cast<std::uint8_t>(transposed),
            .velocity = velocity,
            .direct_release = state.direct_release,
            .start_delay_seconds = state.elapsed_seconds,
            .gate_seconds = ticks_to_seconds(state, gate_ticks) / 100.0,
            .track_gain = state.volume,
            .track_pitch = std::exp2(state.pitch_bend * static_cast<float>(state.bend_sense) / 36.0F),
            .track_pan = std::clamp(state.pan, 0.0F, 1.0F),
            .sweep_start_note = (control & 0x80U) != 0U ? std::optional<std::uint8_t>{state.latest_note} : std::nullopt,
            .waits_for_sample_completion = duration == 0U,
            .completion_dependencies = state.completion_dependencies,
        });
        state.latest_note = static_cast<std::uint8_t>(transposed);
        if (duration == 0U) {
          // JASSeqReader::waitNoteFinish resumes when this wave's
          // concrete backend channel ends. Resolution happens in
          // note order, so a later layer can add the exact natural
          // lifetime of each earlier dependency to its start time.
          state.completion_dependencies.push_back(notes.size() - 1U);
        } else {
          state.elapsed_seconds += ticks_to_seconds(state, duration);
        }
        continue;
      }
      if (command >= 0x80U && command <= 0x87U) {
        if ((command & 0x07U) != 0U) {
          malformed("finite BSC note-off references an unscheduled MIDI channel");
        }
        continue;
      }
      if ((command & 0xf0U) == 0x90U) {
        const auto argument_count = (command & 0x07U) + 1U;
        const auto register_mask = reader.u8(cursor++, "truncated BSC register-prefix mask");
        const auto nested = reader.u8(cursor++, "truncated BSC register-prefix command");
        if (argument_count != 1U || nested != 0xebU || (register_mask & 0x80U) == 0U) {
          malformed("unsupported register-prefixed BSC command");
        }
        const auto cutoff_register = reader.u8(cursor++, "truncated BSC cutoff register");
        (void)sequence_register(state, cutoff_register);
        continue;
      }

      switch (command) {
      case 0xb4U:
        state.latest_note = reader.u8(cursor++, "truncated BSC last-note command");
        break;
      case 0xb8U: {
        const auto parameter = reader.u8(cursor++, "truncated BSC parameter command");
        const auto compact = reader.u8(cursor++, "truncated BSC parameter command");
        auto raw = static_cast<std::uint16_t>(compact) << 8U;
        if ((compact & 0x80U) == 0U) {
          raw |= static_cast<std::uint16_t>(compact) << 1U;
        }
        set_sequence_parameter(state, parameter, static_cast<float>(static_cast<std::int16_t>(raw)) / 32767.0F, 0U);
        break;
      }
      case 0xb9U: {
        const auto parameter = reader.u8(cursor++, "truncated BSC parameter command");
        const auto raw = static_cast<std::int16_t>(reader.u16(cursor, "truncated BSC parameter command"));
        cursor += 2U;
        set_sequence_parameter(state, parameter, static_cast<float>(raw) / 32767.0F, 0U);
        break;
      }
      case 0xbaU:
      case 0xbbU: {
        const auto parameter = reader.u8(cursor++, "truncated BSC timed parameter command");
        float value = 0.0F;
        if (command == 0xbaU) {
          const auto compact = reader.u8(cursor++, "truncated BSC timed parameter command");
          auto raw = static_cast<std::uint16_t>(compact) << 8U;
          if ((compact & 0x80U) == 0U) {
            raw |= static_cast<std::uint16_t>(compact) << 1U;
          }
          value = static_cast<float>(static_cast<std::int16_t>(raw)) / 32767.0F;
        } else {
          value = static_cast<float>(
                      static_cast<std::int16_t>(reader.u16(cursor, "truncated BSC timed parameter command"))) /
                  32767.0F;
          cursor += 2U;
        }
        const auto transition = reader.u16(cursor, "truncated BSC timed parameter duration");
        cursor += 2U;
        set_sequence_parameter(state, parameter, value, transition);
        break;
      }
      case 0xc1U: {
        const auto child_index = reader.u8(cursor++, "truncated BSC open-track index");
        const auto child = relative(bsc.offset, reader.u24(cursor, "truncated BSC open-track target"), reader,
                                    "invalid BSC child-track target");
        cursor += 3U;
        if (child_index >= 16U) {
          malformed("BSC child-track index exceeds JASTrack capacity");
        }
        state.open_child_mask |= static_cast<std::uint16_t>(1U << child_index);
        auto child_state = FiniteSequenceState{};
        child_state.bank = state.bank;
        child_state.program = state.program;
        child_state.elapsed_seconds = state.elapsed_seconds;
        child_state.completion_dependencies = state.completion_dependencies;
        execute_finite_sequence_track(child, child_state, notes, recursion_depth + 1U);
        break;
      }
      case 0xc2U:
        (void)reader.u8(cursor++, "truncated BSC close-track index");
        break;
      case 0xc3U:
        if (calls.size() == 8U) {
          malformed("BSC call stack exceeds JASSeqReader limit");
        }
        calls.push_back(cursor + 3U);
        cursor = relative(bsc.offset, reader.u24(cursor, "truncated BSC call"), reader, "invalid BSC call target");
        break;
      case 0xc4U: {
        const auto condition = reader.u8(cursor++, "truncated BSC conditional call");
        const auto target = relative(bsc.offset, reader.u24(cursor, "truncated BSC conditional call"), reader,
                                     "invalid BSC conditional call target");
        cursor += 3U;
        if (sequence_condition(condition, state.condition_value)) {
          if (calls.size() == 8U) {
            malformed("BSC call stack exceeds JASSeqReader limit");
          }
          calls.push_back(cursor);
          cursor = target;
        }
        break;
      }
      case 0xc5U:
        if (calls.empty()) {
          return;
        }
        cursor = calls.back();
        calls.pop_back();
        break;
      case 0xc6U: {
        const auto condition = reader.u8(cursor++, "truncated BSC conditional return");
        if (sequence_condition(condition, state.condition_value)) {
          if (calls.empty()) {
            return;
          }
          cursor = calls.back();
          calls.pop_back();
        }
        break;
      }
      case 0xc7U:
        cursor = relative(bsc.offset, reader.u24(cursor, "truncated BSC jump"), reader, "invalid BSC jump target");
        break;
      case 0xc8U: {
        const auto condition = reader.u8(cursor++, "truncated BSC conditional jump");
        const auto target = relative(bsc.offset, reader.u24(cursor, "truncated BSC conditional jump"), reader,
                                     "invalid BSC conditional jump target");
        cursor += 3U;
        if (sequence_condition(condition, state.condition_value)) {
          cursor = target;
        }
        break;
      }
      case 0xcbU:
        loops.push_back(LoopState{
            .cursor = cursor + 2U,
            .remaining = reader.u16(cursor, "truncated BSC loop count"),
        });
        cursor += 2U;
        break;
      case 0xccU:
        if (loops.empty()) {
          malformed("BSC loop end has no matching start");
        }
        if (loops.back().remaining == 0U) {
          malformed("finite BSC recipe contains an infinite note loop");
        }
        if (--loops.back().remaining != 0U) {
          cursor = loops.back().cursor;
        } else {
          loops.pop_back();
        }
        break;
      case 0xd0U: {
        (void)reader.u8(cursor++, "truncated BSC read-port index");
        const auto destination = reader.u8(cursor++, "truncated BSC read-port register");
        write_sequence_register(state, destination, 0U);
        break;
      }
      case 0xd8U: {
        const auto reg = reader.u8(cursor++, "truncated BSC register load");
        const auto value = reader.u16(cursor, "truncated BSC register load");
        cursor += 2U;
        write_sequence_register(state, reg, value);
        break;
      }
      case 0xd9U: {
        const auto operation = reader.u8(cursor++, "truncated BSC register operation");
        const auto reg = reader.u8(cursor++, "truncated BSC register operation");
        const auto operand_reg = reader.u8(cursor++, "truncated BSC register operand");
        apply_register_operation(state, operation, reg, sequence_register(state, operand_reg));
        break;
      }
      case 0xdaU: {
        const auto operation = reader.u8(cursor++, "truncated BSC register operation");
        const auto reg = reader.u8(cursor++, "truncated BSC register operation");
        const auto operand = reader.u16(cursor, "truncated BSC register operand");
        cursor += 2U;
        apply_register_operation(state, operation, reg, operand);
        break;
      }
      case 0xe0U:
        state.tempo = reader.u16(cursor, "truncated BSC tempo");
        cursor += 2U;
        break;
      case 0xe1U: {
        const auto value = reader.u16(cursor, "truncated BSC bank/program command");
        cursor += 2U;
        state.bank = value >> 8U;
        state.program = value & 0xffU;
        break;
      }
      case 0xe2U:
        state.bank = reader.u8(cursor++, "truncated BSC bank command");
        break;
      case 0xe3U:
        state.program = reader.u8(cursor++, "truncated BSC program command");
        break;
      case 0xeaU:
        reader.require(cursor, 3U, "truncated BSC bus-connect command");
        cursor += 3U;
        break;
      case 0xebU:
        (void)reader.u8(cursor++, "truncated BSC cutoff command");
        break;
      case 0xf0U: {
        const auto [ticks, after_wait] = read_midi_value(reader, cursor);
        cursor = after_wait;
        state.elapsed_seconds += ticks_to_seconds(state, ticks);
        break;
      }
      case 0xf1U:
        state.elapsed_seconds += ticks_to_seconds(state, reader.u8(cursor++, "truncated BSC byte wait"));
        break;
      case 0xfeU:
        break;
      case 0xffU:
        return;
      default:
        malformed("unsupported command in finite BSC sound effect");
      }
      (void)command_offset;
    }
    malformed("finite BSC sound effect exceeds its instruction limit");
  }

  [[nodiscard]] std::vector<TrackRecipe> finite_sequence_tracks(std::uint32_t sound_id) const {
    auto notes = std::vector<TrackRecipe>{};
    execute_finite_sequence_track(sequence_offset(sound_id), FiniteSequenceState{}, notes, 0U);
    if (notes.empty()) {
      malformed("finite BSC sound effect contains no playable notes");
    }
    return notes;
  }

  static void apply_instrument_effect(const Reader& reader, std::size_t bank_base, std::uint32_t effect_offset,
                                      const TrackRecipe& track, InstrumentRecipe& recipe) {
    const auto effect = relative(bank_base, effect_offset, reader, "invalid IBNK instrument-effect offset");
    const auto magic = reader.u32(effect, "truncated IBNK instrument effect");
    if (magic == fourcc('R', 'a', 'n', 'd')) {
      malformed("IBNK random instrument effects require a live JAudio random scheduler");
    }
    if (magic != fourcc('S', 'e', 'n', 's')) {
      malformed("unsupported IBNK instrument effect");
    }
    reader.require(effect, 16U, "truncated IBNK sense effect");
    const auto target = reader.u8(effect + 4U, "truncated IBNK sense target");
    const auto sense_type = reader.u8(effect + 5U, "truncated IBNK sense source");
    const auto maximum = reader.u8(effect + 6U, "truncated IBNK sense maximum");
    const auto start = reader.f32(effect + 8U, "truncated IBNK sense start");
    const auto end = reader.f32(effect + 12U, "truncated IBNK sense end");
    if (!std::isfinite(start) || !std::isfinite(end)) {
      malformed("IBNK sense effect contains a non-finite value");
    }
    auto input = 0U;
    if (sense_type == 1U) {
      input = track.velocity;
    } else if (sense_type == 2U) {
      input = track.note;
    } else if (sense_type != 0U) {
      malformed("IBNK sense effect has an invalid source");
    }
    float value = 0.0F;
    if (maximum == 0U || maximum == 127U) {
      value = start + static_cast<float>(input) * (end - start) / 127.0F;
    } else if (input < maximum) {
      value = start + (1.0F - start) * static_cast<float>(input) / static_cast<float>(maximum);
    } else {
      value = 1.0F + (end - 1.0F) * static_cast<float>(input - maximum) / static_cast<float>(127U - maximum);
    }
    switch (target) {
    case 0U:
      recipe.volume *= value;
      break;
    case 1U:
      recipe.pitch *= value;
      break;
    case 2U:
      recipe.pan += value - 0.5F;
      break;
    default:
      malformed("IBNK sense effect targets an unavailable host DSP parameter");
    }
  }

  static void parse_velocity_regions(const Reader& reader, std::size_t& cursor, std::uint32_t velocity_count,
                                     const TrackRecipe& track, InstrumentRecipe& recipe, bool key_matches,
                                     bool& selected) {
    require_table(reader, cursor, velocity_count, 16U, "truncated IBNK velocity map");
    for (auto velocity = std::uint32_t{0}; velocity < velocity_count; ++velocity) {
      const auto high_velocity = reader.u8(cursor, "truncated IBNK velocity map");
      const auto wave_id = static_cast<std::uint16_t>(reader.u32(cursor + 4U, "truncated IBNK wave ID"));
      const auto volume = reader.f32(cursor + 8U, "truncated IBNK volume");
      const auto pitch = reader.f32(cursor + 12U, "truncated IBNK pitch");
      if (!std::isfinite(volume) || !std::isfinite(pitch)) {
        malformed("IBNK velocity map contains a non-finite value");
      }
      if (key_matches && !selected && track.velocity <= high_velocity) {
        recipe.wave_id = wave_id;
        recipe.volume *= volume;
        recipe.pitch *= pitch;
        selected = true;
      }
      cursor += 16U;
    }
  }

  [[nodiscard]] InstrumentRecipe instrument(const BankRecord& bank, const TrackRecipe& track) const {
    const auto reader = Reader{baa, bank.data.offset, bank.data.size};
    const auto base = bank.data.offset;
    if (reader.u32(base, "truncated IBNK") != fourcc('I', 'B', 'N', 'K') ||
        reader.u32(base + 12U, "truncated IBNK version") != 1U) {
      malformed("level sound requires an IBNK version-1 bank");
    }

    auto envt = Segment{};
    auto osct = Segment{};
    auto list = Segment{};
    auto chunk = base + 0x20U;
    const auto end = base + bank.data.size;
    while (chunk + 8U <= end) {
      const auto id = reader.u32(chunk, "truncated IBNK chunk");
      const auto payload_size = reader.u32(chunk + 4U, "truncated IBNK chunk");
      reader.require(chunk + 8U, payload_size, "IBNK chunk extends outside bank");
      const auto segment = Segment{chunk + 8U, payload_size};
      if (id == fourcc('E', 'N', 'V', 'T')) {
        envt = segment;
      } else if (id == fourcc('O', 'S', 'C', 'T')) {
        osct = segment;
      } else if (id == fourcc('L', 'I', 'S', 'T')) {
        list = segment;
      }
      chunk = (chunk + 11U + payload_size) & ~std::size_t{3};
    }
    if (envt.size == 0U || osct.size == 0U || list.size == 0U) {
      malformed("IBNK is missing ENVT, OSCT, or LIST");
    }

    const auto list_reader = Reader{baa, list.offset, list.size};
    const auto program_count = list_reader.u32(list.offset, "truncated IBNK LIST");
    require_table(list_reader, list.offset + 4U, program_count, 4U, "truncated IBNK program table");
    if (track.program >= program_count) {
      malformed("BSC program is outside IBNK LIST");
    }
    const auto instrument_offset =
        list_reader.u32(list.offset + 4U + track.program * 4U, "truncated IBNK program table");
    if (instrument_offset == 0U) {
      malformed("BSC program has no IBNK instrument");
    }
    auto cursor = relative(base, instrument_offset, reader, "invalid IBNK instrument offset");
    const auto instrument_magic = reader.u32(cursor, "truncated IBNK instrument");
    cursor += 4U;
    if (instrument_magic == fourcc('P', 'e', 'r', 'c')) {
      const auto percussion_count = reader.u32(cursor, "truncated IBNK percussion count");
      cursor += 4U;
      require_table(reader, cursor, percussion_count, 4U, "truncated IBNK percussion table");
      if (track.note >= percussion_count) {
        malformed("BSC note is outside its IBNK percussion map");
      }
      const auto percussion_offset =
          reader.u32(cursor + static_cast<std::size_t>(track.note) * 4U, "truncated IBNK percussion table");
      if (percussion_offset == 0U) {
        malformed("BSC note has no IBNK percussion entry");
      }
      auto percussion = relative(base, percussion_offset, reader, "invalid IBNK percussion offset");
      reader.require(percussion, 20U, "truncated IBNK percussion");
      auto recipe = InstrumentRecipe{};
      recipe.volume = reader.f32(percussion + 4U, "truncated IBNK percussion volume");
      recipe.pitch = reader.f32(percussion + 8U, "truncated IBNK percussion pitch");
      recipe.pan = static_cast<float>(reader.u8(percussion + 12U, "truncated IBNK percussion pan")) / 127.0F;
      recipe.direct_release = reader.u16(percussion + 14U, "truncated IBNK percussion release");
      if (!std::isfinite(recipe.volume) || !std::isfinite(recipe.pitch)) {
        malformed("IBNK percussion has a non-finite parameter");
      }
      percussion += 16U;
      const auto effect_count = reader.u32(percussion, "truncated IBNK percussion effects");
      percussion += 4U;
      require_table(reader, percussion, effect_count, 4U, "truncated IBNK percussion effect table");
      for (auto effect = std::uint32_t{0}; effect < effect_count; ++effect) {
        apply_instrument_effect(reader, base, reader.u32(percussion, "truncated IBNK percussion effect table"), track,
                                recipe);
        percussion += 4U;
      }
      const auto velocity_count = reader.u32(percussion, "truncated IBNK percussion velocity count");
      percussion += 4U;
      auto selected = false;
      parse_velocity_regions(reader, percussion, velocity_count, track, recipe, true, selected);
      if (!selected) {
        malformed("no IBNK percussion velocity region matches the note");
      }
      return recipe;
    }
    if (instrument_magic != fourcc('I', 'n', 's', 't')) {
      malformed("BSC program selects an unsupported IBNK instrument type");
    }
    const auto oscillator_count = reader.u32(cursor, "truncated IBNK oscillator count");
    cursor += 4U;
    if (oscillator_count > 2U) {
      malformed("IBNK melodic instrument exceeds JASChannel oscillator capacity");
    }
    require_table(reader, cursor, oscillator_count, 4U, "truncated IBNK oscillator table");
    auto oscillator_indices = std::vector<std::uint32_t>{};
    oscillator_indices.reserve(oscillator_count);
    for (auto index = std::uint32_t{0}; index < oscillator_count; ++index) {
      oscillator_indices.push_back(reader.u32(cursor, "truncated IBNK oscillator table"));
      cursor += 4U;
    }
    const auto effect_count = reader.u32(cursor, "truncated IBNK effect count");
    cursor += 4U;
    require_table(reader, cursor, effect_count, 4U, "truncated IBNK instrument-effect table");
    auto effect_offsets = std::vector<std::uint32_t>{};
    effect_offsets.reserve(effect_count);
    for (auto effect = std::uint32_t{0}; effect < effect_count; ++effect) {
      effect_offsets.push_back(reader.u32(cursor, "truncated IBNK instrument-effect table"));
      cursor += 4U;
    }
    const auto key_count = reader.u32(cursor, "truncated IBNK key count");
    cursor += 4U;
    require_table(reader, cursor, key_count, 8U, "truncated IBNK key map");

    auto recipe = InstrumentRecipe{};
    auto selected = false;
    for (auto key = std::uint32_t{0}; key < key_count; ++key) {
      const auto high_key = reader.u8(cursor, "truncated IBNK key map");
      const auto velocity_count = reader.u32(cursor + 4U, "truncated IBNK velocity count");
      cursor += 8U;
      parse_velocity_regions(reader, cursor, velocity_count, track, recipe, track.note <= high_key, selected);
    }
    const auto instrument_volume = reader.f32(cursor, "truncated IBNK volume");
    const auto instrument_pitch = reader.f32(cursor + 4U, "truncated IBNK pitch");
    if (!selected) {
      malformed("no IBNK key/velocity region matches the level-sound note");
    }
    recipe.volume *= instrument_volume;
    recipe.pitch *= instrument_pitch;
    for (const auto effect_offset : effect_offsets) {
      apply_instrument_effect(reader, base, effect_offset, track, recipe);
    }

    const auto osct_reader = Reader{baa, osct.offset, osct.size};
    const auto envt_reader = Reader{baa, envt.offset, envt.size};
    const auto osct_count = osct_reader.u32(osct.offset, "truncated IBNK OSCT");
    require_table(osct_reader, osct.offset + 4U, osct_count, 28U, "truncated IBNK oscillator table");
    for (const auto oscillator_index : oscillator_indices) {
      if (oscillator_index >= osct_count) {
        malformed("IBNK oscillator index is outside OSCT");
      }
      const auto oscillator = osct.offset + 4U + static_cast<std::size_t>(oscillator_index) * 28U;
      osct_reader.require(oscillator, 28U, "truncated IBNK oscillator");
      const auto counter_scale = osct_reader.f32(oscillator + 8U, "truncated IBNK oscillator scale");
      const auto table_offset = osct_reader.u32(oscillator + 12U, "truncated IBNK envelope offset");
      const auto release_table_offset = osct_reader.u32(oscillator + 16U, "truncated IBNK release-envelope offset");
      const auto value_scale = osct_reader.f32(oscillator + 20U, "truncated IBNK envelope scale");
      const auto value_offset = osct_reader.f32(oscillator + 24U, "truncated IBNK envelope offset");
      if (!std::isfinite(counter_scale) || counter_scale <= 0.0F || !std::isfinite(value_scale) ||
          !std::isfinite(value_offset)) {
        malformed("IBNK oscillator contains a non-finite or nonpositive scale");
      }
      const auto target = osct_reader.u8(oscillator + 4U, "truncated IBNK oscillator target");
      auto initial_value = value_scale + value_offset;
      if (table_offset != 0U) {
        const auto point = relative(envt.offset, table_offset, envt_reader, "invalid IBNK envelope offset");
        envt_reader.require(point, 6U, "truncated IBNK attack point");
        const auto curve = envt_reader.s16(point, "truncated IBNK attack curve");
        const auto duration = envt_reader.s16(point + 2U, "truncated IBNK attack duration");
        const auto point_target = envt_reader.s16(point + 4U, "truncated IBNK attack target");
        if (duration < 0) {
          malformed("IBNK oscillator has a negative attack duration");
        }
        initial_value = static_cast<float>(point_target) / 32768.0F * value_scale + value_offset;
        if (duration != 0) {
          if (target == 0U && recipe.attack_seconds != 0.0) {
            malformed("multiple timed IBNK volume attacks are not representable");
          }
          if (target == 0U) {
            recipe.attack_seconds = static_cast<double>(duration) / (600.0 * static_cast<double>(counter_scale));
            recipe.attack_curve = curve_from_jaudio(curve);
          } else {
            malformed("time-varying non-volume IBNK oscillator is unavailable");
          }
        }
      }
      switch (target) {
      case 0U:
        recipe.attack_peak *= initial_value;
        break;
      case 1U:
        recipe.pitch *= initial_value;
        break;
      case 2U:
        recipe.pan += initial_value - 0.5F;
        break;
      default:
        malformed("IBNK oscillator targets an unavailable host DSP parameter");
      }
      if (target == 0U && release_table_offset != 0U) {
        const auto point =
            relative(envt.offset, release_table_offset, envt_reader, "invalid IBNK release-envelope offset");
        envt_reader.require(point, 6U, "truncated IBNK release point");
        const auto curve = envt_reader.s16(point, "truncated IBNK release curve");
        const auto duration = envt_reader.s16(point + 2U, "truncated IBNK release duration");
        const auto release_target = envt_reader.s16(point + 4U, "truncated IBNK release target");
        if (duration < 0 || release_target != 0) {
          malformed("IBNK volume release is not a finite fade to silence");
        }
        recipe.release_seconds = static_cast<double>(duration) / (600.0 * static_cast<double>(counter_scale));
        if (duration != 0) {
          recipe.release_curve = curve_from_jaudio(curve);
        }
      }
    }
    recipe.pan = std::clamp(recipe.pan, 0.0F, 1.0F);
    return recipe;
  }

  [[nodiscard]] WaveRecipe wave(const BankRecord& bank, std::uint16_t wanted_wave) const {
    const auto bank_iter = wave_banks.find(bank.wave_bank_index);
    if (bank_iter == wave_banks.end()) {
      malformed("IBNK references a missing WSYS wave bank");
    }
    const auto reader = Reader{baa, bank_iter->second.offset, bank_iter->second.size};
    const auto base = bank_iter->second.offset;
    if (reader.u32(base, "truncated WSYS") != fourcc('W', 'S', 'Y', 'S')) {
      malformed("invalid WSYS header");
    }
    const auto archive_bank = relative(base, reader.u32(base + 16U, "truncated WSYS archive bank"), reader,
                                       "invalid WSYS archive-bank offset");
    const auto control_group = relative(base, reader.u32(base + 20U, "truncated WSYS control group"), reader,
                                        "invalid WSYS control-group offset");
    const auto group_count = reader.u32(control_group + 8U, "truncated WSYS group count");
    require_table(reader, control_group + 12U, group_count, 4U, "truncated WSYS scene table");
    require_table(reader, archive_bank + 8U, group_count, 4U, "truncated WSYS archive table");
    for (auto group = std::uint32_t{0}; group < group_count; ++group) {
      const auto scene = relative(
          base, reader.u32(control_group + 12U + static_cast<std::size_t>(group) * 4U, "truncated WSYS scene table"),
          reader, "invalid WSYS scene offset");
      const auto control = relative(base, reader.u32(scene + 12U, "truncated WSYS control offset"), reader,
                                    "invalid WSYS control offset");
      const auto wave_count = reader.u32(control + 4U, "truncated WSYS wave count");
      const auto archive = relative(
          base, reader.u32(archive_bank + 8U + static_cast<std::size_t>(group) * 4U, "truncated WSYS archive table"),
          reader, "invalid WSYS archive offset");
      require_table(reader, control + 8U, wave_count, 4U, "truncated WSYS wave mapping table");
      reader.require(archive, 0x74U, "truncated WSYS archive header");
      require_table(reader, archive + 0x74U, wave_count, 4U, "truncated WSYS wave table");
      if (reader.u32(archive + 0x70U, "truncated WSYS archive wave count") < wave_count) {
        malformed("WSYS archive/control wave counts disagree");
      }
      for (auto index = std::uint32_t{0}; index < wave_count; ++index) {
        const auto mapping = relative(
            base, reader.u32(control + 8U + static_cast<std::size_t>(index) * 4U, "truncated WSYS wave mapping"),
            reader, "invalid WSYS wave mapping");
        if (reader.u16(mapping + 2U, "truncated WSYS wave ID") != wanted_wave) {
          continue;
        }
        const auto wave_data = relative(
            base, reader.u32(archive + 0x74U + static_cast<std::size_t>(index) * 4U, "truncated WSYS wave table"),
            reader, "invalid WSYS wave offset");
        auto result = WaveRecipe{};
        result.archive_name = reader.string(archive, 0x70U, "unterminated WSYS AW filename");
        result.format = reader.u8(wave_data + 1U, "truncated WSYS wave");
        result.base_key = reader.u8(wave_data + 2U, "truncated WSYS wave");
        result.sample_rate = reader.f32(wave_data + 4U, "truncated WSYS wave");
        result.archive_offset = reader.u32(wave_data + 8U, "truncated WSYS wave");
        result.archive_length = reader.u32(wave_data + 12U, "truncated WSYS wave");
        const auto loop_flags = reader.u32(wave_data + 16U, "truncated WSYS wave");
        result.loop_start = reader.u32(wave_data + 20U, "truncated WSYS wave");
        result.loop_end = reader.u32(wave_data + 24U, "truncated WSYS wave");
        result.sample_count = reader.u32(wave_data + 28U, "truncated WSYS wave");
        result.loop_yn1 = reader.s16(wave_data + 32U, "truncated WSYS wave");
        result.loop_yn2 = reader.s16(wave_data + 34U, "truncated WSYS wave");
        result.looping = loop_flags != 0U;
        if (result.format != 0U || result.sample_count == 0U || !std::isfinite(result.sample_rate) ||
            result.sample_rate <= 0.0F) {
          malformed("JAudio sound requires a nonempty AFC-HQ wave");
        }
        if (result.looping && (result.loop_start >= result.loop_end || result.loop_end > result.sample_count)) {
          malformed("WSYS AFC-HQ loop range is outside its decoded wave");
        }
        const auto decoded_sample_count = result.looping ? result.loop_end : result.sample_count;
        const auto encoded_for_samples = (static_cast<std::size_t>(decoded_sample_count) + 15U) / 16U * 9U;
        if (encoded_for_samples != result.archive_length) {
          malformed("WSYS AFC-HQ length does not cover exactly its sample count (samples=" +
                    std::to_string(decoded_sample_count) + ", encoded=" + std::to_string(result.archive_length) + ")");
        }
        return result;
      }
    }
    malformed("IBNK wave ID is absent from its WSYS bank");
  }

  template <typename Recipe>
  [[nodiscard]] Recipe resolve_tracks(std::uint32_t sound_id, const std::vector<TrackRecipe>& tracks) const {
    const auto [priority, table_volume] = sound_table_properties(sound_id);
    auto result = Recipe{
        .sound_id = sound_id,
        .priority = priority,
        .table_volume = table_volume,
        .voice = {},
        .layers = {},
    };
    result.voice.layers.reserve(tracks.size());
    result.layers.reserve(tracks.size());

    auto wave_cache = std::map<std::string, std::vector<std::uint8_t>>{};
    auto natural_lifetimes = std::vector<double>{};
    natural_lifetimes.reserve(tracks.size());
    for (const auto& track : tracks) {
      const auto bank_iter = banks.find(track.bank);
      if (bank_iter == banks.end()) {
        malformed("BSC selects an unavailable IBNK bank");
      }
      const auto inst = instrument(bank_iter->second, track);
      const auto wave_info = wave(bank_iter->second, inst.wave_id);
      auto archive_iter = wave_cache.find(wave_info.archive_name);
      if (archive_iter == wave_cache.end()) {
        archive_iter = wave_cache.emplace(wave_info.archive_name, wave_loader(wave_info.archive_name)).first;
      }
      const auto& archive = archive_iter->second;
      if (wave_info.archive_offset > archive.size() ||
          wave_info.archive_length > archive.size() - wave_info.archive_offset) {
        malformed("WSYS wave range extends outside its AW file");
      }
      const auto encoded =
          std::span<const std::uint8_t>{archive}.subspan(wave_info.archive_offset, wave_info.archive_length);
      const auto decoded_sample_count = wave_info.looping ? wave_info.loop_end : wave_info.sample_count;
      auto decoded = decode_jaudio_afc_hq(encoded, decoded_sample_count);
      if (wave_info.looping) {
        const auto block_start = static_cast<std::size_t>(wave_info.loop_start / 16U) * 16U;
        if (block_start < 2U || decoded.samples[block_start - 1U] != wave_info.loop_yn1 ||
            decoded.samples[block_start - 2U] != wave_info.loop_yn2) {
          malformed("decoded AFC loop history disagrees with WSYS metadata");
        }
      }

      auto pcm = std::make_shared<std::vector<float>>();
      pcm->reserve(decoded.samples.size());
      for (const auto sample : decoded.samples) {
        pcm->push_back(static_cast<float>(sample) / 32768.0F);
      }
      const auto velocity = static_cast<float>(track.velocity) / 127.0F;
      const auto gain = static_cast<float>(table_volume) / 255.0F * velocity * velocity * inst.volume *
                        inst.attack_peak * track.track_gain;
      const auto base_note = track.sweep_start_note.value_or(track.note);
      const auto pitch = inst.pitch * track.track_pitch *
                         std::exp2((static_cast<float>(base_note) - static_cast<float>(wave_info.base_key)) / 12.0F);
      auto start_delay = track.start_delay_seconds;
      for (const auto dependency : track.completion_dependencies) {
        if (dependency >= natural_lifetimes.size()) {
          malformed("BSC sample-completion dependency is not an earlier note");
        }
        start_delay += natural_lifetimes[dependency];
      }
      const auto release = track.direct_release != 0U ? track.direct_release : inst.direct_release;
      const auto release_seconds =
          release != 0U ? static_cast<double>(release & 0x3fffU) / 600.0 : inst.release_seconds;
      const auto release_curve =
          release != 0U ? curve_from_jaudio(static_cast<std::int16_t>(release >> 14U & 3U)) : inst.release_curve;
      result.voice.layers.push_back(PcmLayer{
          .samples = std::move(pcm),
          .sample_rate = static_cast<std::uint32_t>(std::lround(wave_info.sample_rate)),
          .loop_start = wave_info.looping ? wave_info.loop_start : 0U,
          .loop_end = wave_info.looping ? wave_info.loop_end : 0U,
          .gain = gain,
          .pitch_ratio = pitch,
          .pitch_sweep_semitones = track.sweep_start_note.has_value()
                                       ? static_cast<float>(track.note) - static_cast<float>(*track.sweep_start_note)
                                       : 0.0F,
          .pitch_sweep_seconds = track.sweep_start_note.has_value() ? track.gate_seconds : 0.0,
          .pan = std::clamp(track.track_pan + inst.pan - 0.5F, 0.0F, 1.0F),
          .start_delay_seconds = start_delay,
          .gate_seconds = track.gate_seconds,
          .attack_seconds = inst.attack_seconds,
          .release_seconds = release_seconds,
          .attack_curve = inst.attack_curve,
          .release_curve = release_curve,
      });
      if (wave_info.looping) {
        natural_lifetimes.push_back(std::numeric_limits<double>::infinity());
      } else {
        natural_lifetimes.push_back(static_cast<double>(decoded_sample_count) /
                                    static_cast<double>(wave_info.sample_rate) / static_cast<double>(pitch));
      }
      if (!std::isfinite(start_delay)) {
        malformed("BSC waits for natural completion of a looping wave");
      }
      result.layers.push_back(JAudioSoundLayerAudit{
          .bank = track.bank,
          .program = track.program,
          .note = track.note,
          .velocity = track.velocity,
          .wave_id = inst.wave_id,
          .wave_archive_name = wave_info.archive_name,
          .wave_archive_offset = wave_info.archive_offset,
          .encoded_length = wave_info.archive_length,
          .decoded_loop_start = wave_info.loop_start,
          .decoded_loop_end = wave_info.loop_end,
          .source_sample_rate = static_cast<std::uint32_t>(std::lround(wave_info.sample_rate)),
          .decoded_sample_count = wave_info.sample_count,
          .direct_release_ticks = release,
          .loop_history_yn1 = wave_info.loop_yn1,
          .loop_history_yn2 = wave_info.loop_yn2,
          .start_delay_seconds = start_delay,
          .gate_seconds = track.gate_seconds,
          .waits_for_sample_completion = track.waits_for_sample_completion,
          .looping = wave_info.looping,
      });
    }
    return result;
  }

  [[nodiscard]] JAudioPersistentSoundRecipe resolve_persistent(std::uint32_t sound_id) const {
    return resolve_tracks<JAudioPersistentSoundRecipe>(sound_id, sequence_tracks(sound_id));
  }

  [[nodiscard]] JAudioSoundEffectRecipe resolve_finite(std::uint32_t sound_id) const {
    return resolve_tracks<JAudioSoundEffectRecipe>(sound_id, finite_sequence_tracks(sound_id));
  }

  std::vector<std::uint8_t> baa;
  WaveArchiveLoader wave_loader;
  Segment bst;
  Segment bstn;
  Segment bsc;
  std::map<std::uint32_t, Segment> wave_banks;
  std::map<std::uint32_t, BankRecord> banks;
};

JAudioSoundArchive::JAudioSoundArchive(std::span<const std::uint8_t> decompressed_baa,
                                       WaveArchiveLoader wave_archive_loader)
: _impl(std::make_unique<Impl>(decompressed_baa, std::move(wave_archive_loader))) {}

JAudioSoundArchive::~JAudioSoundArchive() = default;
JAudioSoundArchive::JAudioSoundArchive(JAudioSoundArchive&&) noexcept = default;
JAudioSoundArchive& JAudioSoundArchive::operator=(JAudioSoundArchive&&) noexcept = default;

std::optional<std::uint32_t> JAudioSoundArchive::find_sound_id(std::string_view name) const {
  return _impl->find_sound_id(name);
}

std::optional<JAudioSoundMetadata> JAudioSoundArchive::resolve_sound(std::string_view name) const {
  const auto sound_id = find_sound_id(name);
  if (!sound_id.has_value()) {
    return std::nullopt;
  }
  return _impl->sound_metadata(*sound_id);
}

JAudioSoundMetadata JAudioSoundArchive::resolve_sound(std::uint32_t sound_id) const {
  return _impl->sound_metadata(sound_id);
}

std::optional<JAudioPersistentSoundRecipe> JAudioSoundArchive::resolve_persistent_sound(std::string_view name) const {
  const auto sound_id = find_sound_id(name);
  if (!sound_id.has_value()) {
    return std::nullopt;
  }
  return _impl->resolve_persistent(*sound_id);
}

std::optional<JAudioSoundEffectRecipe> JAudioSoundArchive::resolve_sound_effect(std::string_view name) const {
  const auto sound_id = find_sound_id(name);
  if (!sound_id.has_value()) {
    return std::nullopt;
  }
  return _impl->resolve_finite(*sound_id);
}

JAudioSoundEffectRecipe JAudioSoundArchive::resolve_sound_effect(std::uint32_t sound_id) const {
  return _impl->resolve_finite(sound_id);
}

} // namespace aurora::audio
