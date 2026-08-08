#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aurora::rfl {

enum class ResourceArchiveId : std::uint8_t {
  Beard,
  Eye,
  Eyebrow,
  Faceline,
  FaceTexture,
  Forehead,
  Glass,
  GlassTexture,
  Hair,
  Mask,
  Mole,
  Mouth,
  Mustache,
  Nose,
  NoseLine,
  NoseLineTexture,
  Cap,
  CapTexture,
  Count,
};

enum class ResourceArchiveError {
  None,
  HeaderTooSmall,
  WrongArchiveCount,
  MissingVersion,
  SectionOffsetOutOfRange,
  EmptySection,
  OffsetTableOutOfRange,
  NonZeroFirstFileOffset,
  NonMonotonicFileOffsets,
  IncorrectLargestFileSize,
  FileDataOutOfRange,
};

struct ResourceArchiveSection {
  std::uint16_t file_count = 0;
  std::uint16_t largest_file_size = 0;
  std::size_t offset_table_offset = 0;
  std::size_t file_data_offset = 0;
  std::size_t file_data_size = 0;
};

class ResourceArchive final {
public:
  static constexpr auto archive_count = static_cast<std::size_t>(ResourceArchiveId::Count);

  ResourceArchive() = default;

  [[nodiscard]] static ResourceArchive copy_from(std::span<const std::uint8_t> bytes);

  [[nodiscard]] bool valid() const;
  [[nodiscard]] ResourceArchiveError error() const;
  [[nodiscard]] std::uint16_t version() const;
  [[nodiscard]] std::span<const std::uint8_t> bytes() const;
  [[nodiscard]] const ResourceArchiveSection& section(ResourceArchiveId archive) const;
  [[nodiscard]] std::optional<std::span<const std::uint8_t>> file(ResourceArchiveId archive,
                                                                  std::uint16_t file_index) const;

private:
  void parse();

  std::vector<std::uint8_t> m_bytes;
  std::array<ResourceArchiveSection, archive_count> m_sections{};
  ResourceArchiveError m_error = ResourceArchiveError::HeaderTooSmall;
  std::uint16_t m_version = 0;
};

} // namespace aurora::rfl
