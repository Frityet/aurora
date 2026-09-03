#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

// Owned representation of the Wii SYSCONF wire format. Scalar payloads stay
// big-endian; no console pointers or host object layouts enter the file.
class SysConf final {
public:
  enum class Type : std::uint8_t {
    BigArray = 1,
    SmallArray = 2,
    Byte = 3,
    Short = 4,
    Long = 5,
    LongLong = 6,
    Bool = 7,
  };

  struct Entry {
    Type type;
    std::string name;
    std::vector<std::uint8_t> data;
    bool operator==(const Entry&) const = default;
  };

  static constexpr std::size_t FileSize = 0x4000;

  [[nodiscard]] static SysConf decode(std::span<const std::uint8_t> bytes);
  [[nodiscard]] std::vector<std::uint8_t> encode() const;
  [[nodiscard]] std::span<const Entry> entries() const noexcept;
  [[nodiscard]] const Entry* find(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> integer(std::string_view name, Type type) const noexcept;

  // Replace the first matching item, as the SDK's name-to-ID index does.
  // A failed size/type/capacity check leaves the previous document intact.
  void replace(Entry entry);
  void replace_integer(std::string_view name, Type type, std::uint64_t value);
  bool erase(std::string_view name);

private:
  std::vector<Entry> m_entries;
};

} // namespace aurora
