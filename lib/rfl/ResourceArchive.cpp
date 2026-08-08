#include <aurora/rfl/ResourceArchive.hpp>

#include <algorithm>
#include <utility>

namespace aurora::rfl {
namespace {

constexpr auto header_size = std::size_t{4} + ResourceArchive::archive_count * sizeof(std::uint32_t);

[[nodiscard]] bool contains_range(std::size_t size, std::size_t offset, std::size_t length) {
  return offset <= size && length <= size - offset;
}

[[nodiscard]] std::uint16_t read_be_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
}

[[nodiscard]] std::uint32_t read_be_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::size_t archive_index(ResourceArchiveId archive) { return static_cast<std::size_t>(archive); }

} // namespace

ResourceArchive ResourceArchive::copy_from(std::span<const std::uint8_t> bytes) {
  ResourceArchive archive;
  archive.m_bytes.assign(bytes.begin(), bytes.end());
  archive.parse();
  return archive;
}

bool ResourceArchive::valid() const { return m_error == ResourceArchiveError::None; }

ResourceArchiveError ResourceArchive::error() const { return m_error; }

std::uint16_t ResourceArchive::version() const { return m_version; }

std::span<const std::uint8_t> ResourceArchive::bytes() const { return m_bytes; }

const ResourceArchiveSection& ResourceArchive::section(ResourceArchiveId archive) const {
  return m_sections.at(archive_index(archive));
}

std::optional<std::span<const std::uint8_t>> ResourceArchive::file(ResourceArchiveId archive,
                                                                   std::uint16_t file_index) const {
  if (!valid()) {
    return std::nullopt;
  }

  const auto& archive_section = section(archive);
  if (file_index >= archive_section.file_count) {
    return std::nullopt;
  }

  const auto table = std::span<const std::uint8_t>{m_bytes};
  const auto start = static_cast<std::size_t>(read_be_u32(
      table, archive_section.offset_table_offset + static_cast<std::size_t>(file_index) * sizeof(std::uint32_t)));
  const auto end = static_cast<std::size_t>(
      read_be_u32(table, archive_section.offset_table_offset +
                             (static_cast<std::size_t>(file_index) + 1U) * sizeof(std::uint32_t)));
  return table.subspan(archive_section.file_data_offset + start, end - start);
}

void ResourceArchive::parse() {
  m_sections = {};
  m_version = 0;
  const auto data = std::span<const std::uint8_t>{m_bytes};
  if (data.size() < header_size) {
    m_error = ResourceArchiveError::HeaderTooSmall;
    return;
  }
  if (read_be_u16(data, 0) != archive_count) {
    m_error = ResourceArchiveError::WrongArchiveCount;
    return;
  }

  m_version = read_be_u16(data, 2);
  if (m_version == 0) {
    m_error = ResourceArchiveError::MissingVersion;
    return;
  }

  auto previous_end = header_size;
  for (auto index = std::size_t{}; index < archive_count; ++index) {
    const auto section_offset = static_cast<std::size_t>(read_be_u32(data, 4U + index * sizeof(std::uint32_t)));
    if (section_offset < previous_end || !contains_range(data.size(), section_offset, 4U)) {
      m_error = ResourceArchiveError::SectionOffsetOutOfRange;
      return;
    }

    auto& archive_section = m_sections[index];
    archive_section.file_count = read_be_u16(data, section_offset);
    archive_section.largest_file_size = read_be_u16(data, section_offset + 2U);
    if (archive_section.file_count == 0U) {
      m_error = ResourceArchiveError::EmptySection;
      return;
    }

    archive_section.offset_table_offset = section_offset + 4U;
    const auto table_size = (static_cast<std::size_t>(archive_section.file_count) + 1U) * sizeof(std::uint32_t);
    if (!contains_range(data.size(), archive_section.offset_table_offset, table_size)) {
      m_error = ResourceArchiveError::OffsetTableOutOfRange;
      return;
    }
    if (read_be_u32(data, archive_section.offset_table_offset) != 0U) {
      m_error = ResourceArchiveError::NonZeroFirstFileOffset;
      return;
    }

    auto previous_file_end = std::size_t{};
    auto largest_file_size = std::size_t{};
    for (auto file_index = std::size_t{}; file_index < archive_section.file_count; ++file_index) {
      const auto next_file_end = static_cast<std::size_t>(
          read_be_u32(data, archive_section.offset_table_offset + (file_index + 1U) * sizeof(std::uint32_t)));
      if (next_file_end < previous_file_end) {
        m_error = ResourceArchiveError::NonMonotonicFileOffsets;
        return;
      }
      largest_file_size = std::max(largest_file_size, next_file_end - previous_file_end);
      previous_file_end = next_file_end;
    }
    if (largest_file_size != archive_section.largest_file_size) {
      m_error = ResourceArchiveError::IncorrectLargestFileSize;
      return;
    }

    archive_section.file_data_offset = archive_section.offset_table_offset + table_size;
    archive_section.file_data_size = previous_file_end;
    if (!contains_range(data.size(), archive_section.file_data_offset, archive_section.file_data_size)) {
      m_error = ResourceArchiveError::FileDataOutOfRange;
      return;
    }
    previous_end = archive_section.file_data_offset + archive_section.file_data_size;
  }

  m_error = ResourceArchiveError::None;
}

} // namespace aurora::rfl
