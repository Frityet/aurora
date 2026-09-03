#include <aurora/sysconf.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aurora {
namespace {

std::size_t scalar_size(SysConf::Type type) {
  switch (type) {
  case SysConf::Type::Byte:
  case SysConf::Type::Bool:
    return 1;
  case SysConf::Type::Short:
    return 2;
  case SysConf::Type::Long:
    return 4;
  case SysConf::Type::LongLong:
    return 8;
  case SysConf::Type::BigArray:
  case SysConf::Type::SmallArray:
    return 0;
  }
  throw std::invalid_argument("SYSCONF has an invalid entry type");
}

std::size_t packed_size(const SysConf::Entry& entry) {
  if (entry.name.empty() || entry.name.size() > 32 || entry.data.empty()) {
    throw std::invalid_argument("SYSCONF entry name or payload has an invalid length");
  }
  const auto size = scalar_size(entry.type);
  if (size != 0 && entry.data.size() != size) {
    throw std::invalid_argument("SYSCONF integer payload does not match its declared type");
  }
  if ((entry.type == SysConf::Type::SmallArray && entry.data.size() > 256) ||
      (entry.type == SysConf::Type::BigArray && entry.data.size() > 65536)) {
    throw std::invalid_argument("SYSCONF array exceeds its wire length field");
  }
  const auto length_bytes = entry.type == SysConf::Type::BigArray ? 2U :
                            entry.type == SysConf::Type::SmallArray ? 1U : 0U;
  return 1 + entry.name.size() + length_bytes + entry.data.size();
}

std::size_t document_size(std::span<const SysConf::Entry> entries) {
  std::size_t size = 6 + 2 * (entries.size() + 1);
  for (const auto& entry : entries) {
    size += packed_size(entry);
    if (size > SysConf::FileSize - 4) {
      throw std::length_error("SYSCONF entries exceed the 16 KiB document capacity");
    }
  }
  return size;
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset > bytes.size() || bytes.size() - offset < 2) {
    throw std::invalid_argument("SYSCONF truncated halfword");
  }
  return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

void write_u16(std::span<std::uint8_t> bytes, std::size_t offset, std::size_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 8);
  bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

} // namespace

SysConf SysConf::decode(std::span<const std::uint8_t> bytes) {
  constexpr auto header = std::array<std::uint8_t, 4>{'S', 'C', 'v', '0'};
  constexpr auto footer = std::array<std::uint8_t, 4>{'S', 'C', 'e', 'd'};
  if (bytes.size() < 12 || bytes.size() > FileSize ||
      !std::ranges::equal(bytes.first(4), header) || !std::ranges::equal(bytes.last(4), footer)) {
    throw std::invalid_argument("SYSCONF signature or document size is invalid");
  }
  const auto end = bytes.size() - 4;
  const auto count = read_u16(bytes, 4);
  std::size_t position = 6 + 2 * (static_cast<std::size_t>(count) + 1);
  if (position > end) {
    throw std::invalid_argument("SYSCONF offset table exceeds the document");
  }
  SysConf result;
  result.m_entries.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    if (read_u16(bytes, 6 + 2 * index) != position || position >= end) {
      throw std::invalid_argument("SYSCONF entries are not contiguous with their offset table");
    }
    const auto description = bytes[position++];
    Entry entry{static_cast<Type>(description >> 5), {}, {}};
    const std::size_t name_length = (description & 31) + 1;
    if (name_length > end - position) {
      throw std::invalid_argument("SYSCONF entry name exceeds the document");
    }
    entry.name.assign(reinterpret_cast<const char*>(bytes.data() + position), name_length);
    position += name_length;
    std::size_t data_length = scalar_size(entry.type);
    if (entry.type == Type::BigArray) {
      if (end - position < 2) {
        throw std::invalid_argument("SYSCONF missing array length");
      }
      data_length = static_cast<std::size_t>(read_u16(bytes, position)) + 1;
      position += 2;
    } else if (entry.type == Type::SmallArray) {
      if (position == end) {
        throw std::invalid_argument("SYSCONF missing array length");
      }
      data_length = static_cast<std::size_t>(bytes[position++]) + 1;
    }
    if (data_length > end - position) {
      throw std::invalid_argument("SYSCONF entry payload exceeds the document");
    }
    entry.data.assign(bytes.begin() + position, bytes.begin() + position + data_length);
    position += data_length;
    result.m_entries.push_back(std::move(entry));
  }
  if (read_u16(bytes, 6 + 2 * count) != position) {
    throw std::invalid_argument("SYSCONF terminal offset does not match its entries");
  }
  return result;
}

std::vector<std::uint8_t> SysConf::encode() const {
  (void)document_size(m_entries);
  std::vector<std::uint8_t> result(FileSize);
  std::copy_n("SCv0", 4, result.begin());
  std::copy_n("SCed", 4, result.end() - 4);
  write_u16(result, 4, m_entries.size());
  std::size_t position = 6 + 2 * (m_entries.size() + 1);
  for (std::size_t index = 0; index < m_entries.size(); ++index) {
    const auto& entry = m_entries[index];
    write_u16(result, 6 + 2 * index, position);
    result[position++] = (static_cast<std::uint8_t>(entry.type) << 5) |
                         static_cast<std::uint8_t>(entry.name.size() - 1);
    std::ranges::copy(entry.name, result.begin() + position);
    position += entry.name.size();
    if (entry.type == Type::BigArray) {
      write_u16(result, position, entry.data.size() - 1);
      position += 2;
    } else if (entry.type == Type::SmallArray) {
      result[position++] = static_cast<std::uint8_t>(entry.data.size() - 1);
    }
    std::ranges::copy(entry.data, result.begin() + position);
    position += entry.data.size();
  }
  write_u16(result, 6 + 2 * m_entries.size(), position);
  return result;
}

std::span<const SysConf::Entry> SysConf::entries() const noexcept { return m_entries; }

const SysConf::Entry* SysConf::find(std::string_view name) const noexcept {
  const auto found = std::ranges::find(m_entries, name, &Entry::name);
  return found == m_entries.end() ? nullptr : &*found;
}

std::optional<std::uint64_t> SysConf::integer(std::string_view name, Type type) const noexcept {
  const auto* entry = find(name);
  if (entry == nullptr || entry->type != type || type == Type::BigArray || type == Type::SmallArray) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const auto byte : entry->data) {
    value = (value << 8) | byte;
  }
  return value;
}

void SysConf::replace(Entry entry) {
  (void)packed_size(entry);
  auto candidate = m_entries;
  const auto found = std::ranges::find(candidate, entry.name, &Entry::name);
  if (found == candidate.end()) {
    candidate.push_back(std::move(entry));
  } else {
    *found = std::move(entry);
  }
  (void)document_size(candidate);
  m_entries.swap(candidate);
}

void SysConf::replace_integer(std::string_view name, Type type, std::uint64_t value) {
  const auto size = scalar_size(type);
  if (size == 0 || (size < sizeof(value) && value >= (std::uint64_t{1} << (8 * size)))) {
    throw std::invalid_argument("SYSCONF value does not fit the requested integer type");
  }
  Entry entry{type, std::string(name), std::vector<std::uint8_t>(size)};
  for (std::size_t i = size; i != 0; --i) {
    entry.data[i - 1] = static_cast<std::uint8_t>(value);
    value >>= 8;
  }
  replace(std::move(entry));
}

bool SysConf::erase(std::string_view name) {
  const auto found = std::ranges::find(m_entries, name, &Entry::name);
  if (found == m_entries.end()) {
    return false;
  }
  m_entries.erase(found);
  return true;
}

void SysConf::replace_at(std::size_t index, Entry entry) {
  (void)packed_size(entry);
  auto candidate = m_entries;
  candidate.at(index) = std::move(entry);
  (void)document_size(candidate);
  m_entries.swap(candidate);
}

void SysConf::append(Entry entry) {
  (void)packed_size(entry);
  auto candidate = m_entries;
  candidate.push_back(std::move(entry));
  (void)document_size(candidate);
  m_entries.swap(candidate);
}

void SysConf::erase_at(std::size_t index) {
  if (index >= m_entries.size()) {
    throw std::out_of_range("SYSCONF entry index is outside the document");
  }
  m_entries.erase(m_entries.begin() + index);
}

} // namespace aurora
