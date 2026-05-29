#pragma once

#include <revolution.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aurora {

enum class NandOperationKind {
  Write,
  Read,
  Delete,
  Rename,
  Check,
};

struct NandFileMetadata {
  std::string path;
  u8 permission = 0x3CU;
  u8 attribute = 0U;
  std::size_t size = 0U;
};

struct NandCheckResult {
  s32 result = NAND_RESULT_OK;
  u32 free_blocks = 0U;
  u32 free_inodes = 0U;
};

struct NandOperationTrace {
  NandOperationKind kind = NandOperationKind::Read;
  std::string path;
  std::string destination_path;
  s32 result = NAND_RESULT_OK;
  std::size_t byte_count = 0U;
  u8 permission = 0U;
  u8 attribute = 0U;
  u32 requested_blocks = 0U;
  u32 requested_inodes = 0U;
  u32 free_blocks = 0U;
  u32 free_inodes = 0U;
};

class NandFileSystem final {
public:
  [[nodiscard]] static std::string title_data_root();
  [[nodiscard]] static std::string rfl_db_path();
  [[nodiscard]] static std::string file_name(std::string_view path);

  [[nodiscard]] std::string normalize_path(std::string_view path) const;
  void write_file(std::string_view path, std::span<const std::uint8_t> bytes, u8 permission = 0x3CU, u8 attribute = 0U);
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(std::string_view path) const;
  [[nodiscard]] bool exists(std::string_view path) const;
  bool erase(std::string_view path);
  [[nodiscard]] s32 rename(std::string_view source_path, std::string_view destination_path);
  [[nodiscard]] NandCheckResult check(u32 requested_blocks, u32 requested_inodes);
  [[nodiscard]] std::optional<NandFileMetadata> metadata(std::string_view path) const;
  [[nodiscard]] std::span<const NandOperationTrace> trace() const;
  void clear();
  void clear_trace();

private:
  struct StoredFile {
    std::vector<std::uint8_t> bytes;
    u8 permission = 0x3CU;
    u8 attribute = 0U;
  };

  void push_trace(NandOperationTrace trace) const;
  [[nodiscard]] u32 used_blocks() const;
  [[nodiscard]] u32 used_inodes() const;

  std::map<std::string, StoredFile, std::less<>> m_files;
  u32 m_quotaBlocks = 4096U;
  u32 m_quotaInodes = 256U;
  mutable std::vector<NandOperationTrace> m_trace;
};

} // namespace aurora
