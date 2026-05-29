#include <aurora/nand.hpp>

#include <algorithm>
#include <utility>

namespace aurora {

std::string NandFileSystem::title_data_root() { return "/title/00010000/524d474b/data"; }

std::string NandFileSystem::rfl_db_path() { return "/shared2/menu/FaceLib/RFL_DB.dat"; }

std::string NandFileSystem::file_name(std::string_view path) {
  auto text = std::string(path);
  while (!text.empty() && text.back() == '/') {
    text.pop_back();
  }
  const auto slash = text.find_last_of('/');
  return slash == std::string::npos ? text : text.substr(slash + 1U);
}

std::string NandFileSystem::normalize_path(std::string_view path) const {
  auto text = std::string(path);
  std::replace(text.begin(), text.end(), '\\', '/');
  if (text.empty()) {
    text = title_data_root();
  } else if (text.front() != '/') {
    text = title_data_root() + "/" + text;
  }

  auto parts = std::vector<std::string>{};
  auto offset = std::size_t{};
  while (offset <= text.size()) {
    const auto slash = text.find('/', offset);
    const auto token = text.substr(offset, slash == std::string::npos ? std::string::npos : slash - offset);
    if (!token.empty() && token != ".") {
      if (token == "..") {
        if (!parts.empty()) {
          parts.pop_back();
        }
      } else {
        parts.push_back(token);
      }
    }
    if (slash == std::string::npos) {
      break;
    }
    offset = slash + 1U;
  }

  auto normalized = std::string{"/"};
  for (auto index = std::size_t{}; index < parts.size(); ++index) {
    if (index != 0U) {
      normalized += '/';
    }
    normalized += parts[index];
  }
  return normalized;
}

void NandFileSystem::write_file(std::string_view path, std::span<const std::uint8_t> bytes, u8 permission,
                                u8 attribute) {
  const auto normalized = normalize_path(path);
  m_files[normalized] = StoredFile{
      .bytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
      .permission = permission,
      .attribute = attribute,
  };
  auto trace = NandOperationTrace{};
  trace.kind = NandOperationKind::Write;
  trace.path = normalized;
  trace.result = NAND_RESULT_OK;
  trace.byte_count = bytes.size();
  trace.permission = permission;
  trace.attribute = attribute;
  push_trace(std::move(trace));
}

std::optional<std::vector<std::uint8_t>> NandFileSystem::read_file(std::string_view path) const {
  const auto normalized = normalize_path(path);
  const auto it = m_files.find(normalized);
  if (it == m_files.end()) {
    auto trace = NandOperationTrace{};
    trace.kind = NandOperationKind::Read;
    trace.path = normalized;
    trace.result = NAND_RESULT_NOEXISTS;
    push_trace(std::move(trace));
    return std::nullopt;
  }

  auto trace = NandOperationTrace{};
  trace.kind = NandOperationKind::Read;
  trace.path = normalized;
  trace.result = NAND_RESULT_OK;
  trace.byte_count = it->second.bytes.size();
  trace.permission = it->second.permission;
  trace.attribute = it->second.attribute;
  push_trace(std::move(trace));
  return it->second.bytes;
}

bool NandFileSystem::exists(std::string_view path) const { return m_files.contains(normalize_path(path)); }

bool NandFileSystem::erase(std::string_view path) {
  const auto normalized = normalize_path(path);
  const auto erased = m_files.erase(normalized) != 0U;
  auto trace = NandOperationTrace{};
  trace.kind = NandOperationKind::Delete;
  trace.path = normalized;
  trace.result = erased ? NAND_RESULT_OK : NAND_RESULT_NOEXISTS;
  push_trace(std::move(trace));
  return erased;
}

s32 NandFileSystem::rename(std::string_view source_path, std::string_view destination_path) {
  const auto source = normalize_path(source_path);
  const auto destination = normalize_path(destination_path);
  auto node = m_files.extract(source);
  if (node.empty()) {
    auto trace = NandOperationTrace{};
    trace.kind = NandOperationKind::Rename;
    trace.path = source;
    trace.destination_path = destination;
    trace.result = NAND_RESULT_NOEXISTS;
    push_trace(std::move(trace));
    return NAND_RESULT_NOEXISTS;
  }

  node.key() = destination;
  const auto byte_count = node.mapped().bytes.size();
  m_files.insert(std::move(node));
  auto trace = NandOperationTrace{};
  trace.kind = NandOperationKind::Rename;
  trace.path = source;
  trace.destination_path = destination;
  trace.result = NAND_RESULT_OK;
  trace.byte_count = byte_count;
  push_trace(std::move(trace));
  return NAND_RESULT_OK;
}

NandCheckResult NandFileSystem::check(u32 requested_blocks, u32 requested_inodes) {
  const auto blocks = used_blocks();
  const auto inodes = used_inodes();
  const auto free_blocks = blocks >= m_quotaBlocks ? 0U : m_quotaBlocks - blocks;
  const auto free_inodes = inodes >= m_quotaInodes ? 0U : m_quotaInodes - inodes;
  const auto result = requested_blocks > free_blocks   ? NAND_RESULT_MAXBLOCKS
                      : requested_inodes > free_inodes ? NAND_RESULT_MAXFILES
                                                       : NAND_RESULT_OK;
  auto trace = NandOperationTrace{};
  trace.kind = NandOperationKind::Check;
  trace.result = result;
  trace.requested_blocks = requested_blocks;
  trace.requested_inodes = requested_inodes;
  trace.free_blocks = free_blocks;
  trace.free_inodes = free_inodes;
  push_trace(std::move(trace));
  return NandCheckResult{
      .result = result,
      .free_blocks = free_blocks,
      .free_inodes = free_inodes,
  };
}

std::optional<NandFileMetadata> NandFileSystem::metadata(std::string_view path) const {
  const auto normalized = normalize_path(path);
  const auto it = m_files.find(normalized);
  if (it == m_files.end()) {
    return std::nullopt;
  }

  return NandFileMetadata{
      .path = normalized,
      .permission = it->second.permission,
      .attribute = it->second.attribute,
      .size = it->second.bytes.size(),
  };
}

std::span<const NandOperationTrace> NandFileSystem::trace() const { return m_trace; }

void NandFileSystem::clear() {
  m_files.clear();
  clear_trace();
}

void NandFileSystem::clear_trace() { m_trace.clear(); }

void NandFileSystem::push_trace(NandOperationTrace trace) const { m_trace.push_back(std::move(trace)); }

u32 NandFileSystem::used_blocks() const {
  auto blocks = u32{};
  for (const auto& [_, file] : m_files) {
    blocks += static_cast<u32>((file.bytes.size() + 0x3FFFU) / 0x4000U);
  }
  return blocks;
}

u32 NandFileSystem::used_inodes() const { return static_cast<u32>(m_files.size()); }

} // namespace aurora
