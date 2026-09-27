#include <aurora/nand.hpp>
#include <aurora/allocation.hpp>
#include <aurora/exception.hpp>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <utility>

namespace {
std::mutex s_sdkMutex;
aurora::NandFileSystem* s_activeNand = nullptr;
// Descriptor identities never repeat across filesystem activations.
s32 s_nextDescriptor = 1;
std::atomic<u32> s_nextSafeOpenId{0};
}

namespace aurora {

NandFileSystem::NandFileSystem(std::string_view root) {
  const allocation::HostAllocationScope host;
  if (root.empty() || root.front() != '/' || root.size() >= NAND_MAX_PATH)
    throw_host_exception<std::invalid_argument>("NAND requires a bounded absolute title directory");
  m_titleDataRoot = normalize_path(root);
}

NandFileSystem::~NandFileSystem() { deactivate_sdk(); }

NandFileSystem::NandFileSystem(const NandFileSystem& source, StorageCopyTag)
: m_titleDataRoot(source.m_titleDataRoot), m_files(source.m_files),
  m_quotaBlocks(source.m_quotaBlocks), m_quotaInodes(source.m_quotaInodes), m_trace(source.m_trace) {}

NandFileSystem NandFileSystem::clone_storage() const {
  const allocation::HostAllocationScope host;
  return NandFileSystem(*this, StorageCopyTag{});
}

void NandFileSystem::swap_storage(NandFileSystem& other) noexcept {
  m_files.swap(other.m_files);
  std::swap(m_quotaBlocks, other.m_quotaBlocks);
  std::swap(m_quotaInodes, other.m_quotaInodes);
  m_trace.swap(other.m_trace);
}

const std::string& NandFileSystem::title_data_root() const noexcept { return m_titleDataRoot; }

void NandFileSystem::activate_sdk(NandIoCallbacks callbacks) {
  const allocation::HostAllocationScope host;
  const std::lock_guard lock(s_sdkMutex);
  if (s_activeNand)
    throw_host_exception<std::logic_error>("NAND already has an active filesystem");
  const bool any = callbacks.read || callbacks.metadata || callbacks.commit || callbacks.create || callbacks.move || callbacks.erase;
  if (any && !(callbacks.read && callbacks.commit && callbacks.create && callbacks.move && callbacks.erase))
    throw_host_exception<std::invalid_argument>("NAND storage callbacks must supply every operation");
  m_io = callbacks;
  s_activeNand = this;
}

void NandFileSystem::deactivate_sdk() noexcept {
  const allocation::HostAllocationScope host;
  const std::lock_guard lock(s_sdkMutex);
  m_openFiles.clear();
  m_io = {};
  if (s_activeNand == this)
    s_activeNand = nullptr;
}

void NandFileSystem::retire_thread_files(const OSThread* thread) noexcept {
  const allocation::HostAllocationScope host;
  const std::lock_guard lock(s_sdkMutex);
  if (s_activeNand && thread)
    std::erase_if(s_activeNand->m_openFiles, [thread](const auto& entry) { return entry.second.thread == thread; });
}

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

std::size_t NandFileSystem::erase_subtree(std::string_view path) {
  const auto normalized = normalize_path(path);
  const auto prefix = normalized == "/" ? normalized : normalized + "/";
  auto removed = std::size_t{};
  for (auto it = m_files.begin(); it != m_files.end();) {
    if (it->first != normalized && !it->first.starts_with(prefix)) {
      ++it;
      continue;
    }
    auto trace = NandOperationTrace{};
    trace.kind = NandOperationKind::Delete;
    trace.path = it->first;
    trace.result = NAND_RESULT_OK;
    push_trace(std::move(trace));
    it = m_files.erase(it);
    ++removed;
  }
  return removed;
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
  m_files.erase(destination);
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

NandUsage NandFileSystem::usage(std::string_view root) const {
  const auto normalized = normalize_path(root);
  const auto prefix = normalized == "/" ? normalized : normalized + "/";
  NandUsage result;
  for (const auto& [path, file] : m_files) {
    if (path == normalized || path.starts_with(prefix)) {
      result.blocks += static_cast<u32>((file.bytes.size() + 0x3fffU) / 0x4000U);
      ++result.inodes;
    }
  }
  return result;
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

namespace aurora {
// Only the C SDK facade uses these internals. Descriptor storage belongs to
// the actual filesystem, rather than an application publication side table.
struct NandSdkAccess {
  using OpenFile = NandFileSystem::OpenFile;
  static auto& files(NandFileSystem& fs) { return fs.m_openFiles; }
  static std::optional<std::vector<u8>> read(NandFileSystem& fs, std::string_view path) {
    return fs.m_io.read ? fs.m_io.read(fs.m_io.context, path) : fs.read_file(path);
  }
  static s32 create(NandFileSystem& fs, std::string_view path, u8 permission, u8 attribute) {
    if (fs.m_io.create) return fs.m_io.create(fs.m_io.context, path, permission, attribute);
    if (fs.exists(path)) return NAND_RESULT_EXISTS;
    const auto capacity = fs.check(0, 1);
    if (capacity.result != NAND_RESULT_OK) return capacity.result;
    fs.write_file(path, {}, permission, attribute);
    return NAND_RESULT_OK;
  }
  static void commit(NandFileSystem& fs, const OpenFile& file) {
    const auto metadata = NandSdkAccess::metadata(fs, file.path);
    const auto permission = metadata ? metadata->permission : u8{0x3c};
    const auto attribute = metadata ? metadata->attribute : u8{0};
    commit(fs, file, permission, attribute);
  }
  static void commit(NandFileSystem& fs, const OpenFile& file, u8 permission, u8 attribute) {
    if (fs.m_io.commit)
      fs.m_io.commit(fs.m_io.context, file.path, file.bytes, permission, attribute);
    else
      fs.write_file(file.path, file.bytes, permission, attribute);
  }
  static s32 move(NandFileSystem& fs, std::string_view from, std::string_view to) {
    return fs.m_io.move ? fs.m_io.move(fs.m_io.context, from, to) : fs.rename(from, to);
  }
  static bool erase(NandFileSystem& fs, std::string_view path) {
    return fs.m_io.erase ? fs.m_io.erase(fs.m_io.context, path) : fs.erase(path);
  }
  static std::optional<NandFileMetadata> metadata(NandFileSystem& fs, std::string_view path) {
    if (fs.m_io.metadata) {
      if (auto metadata = fs.m_io.metadata(fs.m_io.context, path)) return metadata;
    }
    return fs.metadata(path);
  }
};
} // namespace aurora

namespace {
using Access = aurora::NandSdkAccess;

aurora::NandFileSystem& active_nand() {
  if (!s_activeNand)
    aurora::throw_host_exception<std::logic_error>("NAND requires an active filesystem");
  return *s_activeNand;
}

bool valid_path(const char* path) {
  return path && path[0] != '\0' && strnlen(path, NAND_MAX_PATH) < NAND_MAX_PATH;
}

template<class F> s32 invoke(F&& function) {
  const aurora::allocation::HostAllocationScope host;
  const std::lock_guard lock(s_sdkMutex);
  try {
    return function();
  } catch (const std::bad_alloc&) {
    return NAND_RESULT_ALLOC_FAILED;
  } catch (const std::invalid_argument&) {
    return NAND_RESULT_INVALID;
  } catch (const std::exception&) {
    return NAND_RESULT_UNKNOWN;
  }
}

Access::OpenFile* find_file(const NANDFileInfo* info) {
  if (!info || !s_activeNand) return nullptr;
  auto& files = Access::files(*s_activeNand);
  const auto it = files.find(info->fileDescriptor);
  return it == files.end() ? nullptr : &it->second;
}

bool is_open(std::string_view path) {
  for (const auto& [descriptor, file] : Access::files(active_nand()))
    if (file.path == path) return true;
  return false;
}

s32 seek_file(NANDFileInfo* info, s32 offset, s32 whence) {
  auto* file = find_file(info);
  if (!file) return NAND_RESULT_INVALID;

  std::int64_t base = 0;
  switch (whence) {
  case 0:
    break;
  case 1:
    base = static_cast<std::int64_t>(file->position);
    break;
  case 2:
    base = static_cast<std::int64_t>(file->bytes.size());
    break;
  default:
    return NAND_RESULT_INVALID;
  }

  const std::int64_t target = base + static_cast<std::int64_t>(offset);
  if (target < 0 || target > std::numeric_limits<s32>::max() ||
      static_cast<u64>(target) > std::numeric_limits<std::size_t>::max()) {
    return NAND_RESULT_INVALID;
  }
  file->position = static_cast<std::size_t>(target);
  return static_cast<s32>(target);
}

void complete_async(NANDCallback callback, NANDCommandBlock* block, s32 result) {
  if (callback) callback(result, block);
}

s32 open_descriptor(aurora::NandFileSystem& nand, std::string path, std::vector<u8> bytes, u8 access) {
  if (s_nextDescriptor == std::numeric_limits<s32>::max()) return NAND_RESULT_MAXFD;
  const s32 descriptor = s_nextDescriptor++;
  Access::files(nand).emplace(descriptor,
                              Access::OpenFile{std::move(path), std::move(bytes), 0, access, false, OSGetCurrentThread()});
  return descriptor;
}

void close_descriptor(aurora::NandFileSystem& nand, s32 descriptor, bool commit) {
  auto& files = Access::files(nand);
  const auto it = files.find(descriptor);
  if (it == files.end()) return;
  if (commit && it->second.dirty) Access::commit(nand, it->second);
  files.erase(it);
}

bool has_access(const std::optional<aurora::NandFileMetadata>& metadata, u8 access) {
  return !metadata ||
         (!((access & NAND_ACCESS_READ) && !(metadata->permission & NAND_PERM_RUSR)) &&
          !((access & NAND_ACCESS_WRITE) && !(metadata->permission & NAND_PERM_WUSR)));
}

struct TemporaryFileCleanup {
  aurora::NandFileSystem& nand;
  std::string& path;
  bool active = false;

  ~TemporaryFileCleanup() {
    if (!active) return;
    try {
      Access::erase(nand, path);
    } catch (...) {
    }
  }
};

struct OpenDescriptorCleanup {
  aurora::NandFileSystem& nand;
  s32 descriptor = -1;

  ~OpenDescriptorCleanup() {
    close();
  }

  void close() {
    if (descriptor < 0) return;
    close_descriptor(nand, descriptor, false);
    descriptor = -1;
  }

  void release() noexcept {
    descriptor = -1;
  }
};
} // namespace

extern "C" {
    void NANDInitBanner(NANDBanner* banner, u32 flags, const u16* title, const u16* comment) {
        static_assert(sizeof(NANDBanner) == 0xf0a0);
        std::memset(banner, 0, sizeof(*banner));
        banner->signature = NAND_BANNER_SIGNATURE;
        banner->flag = flags;
        const u16* lines[] = {title, comment};
        for (std::size_t line = 0; line < 2; ++line) {
            const auto* source = lines[line];
            if (*source == 0) {
                banner->comment[line][0] = u16{' '};
            } else {
                for (std::size_t i = 0; i < NAND_BANNER_COMMENT_SIZE && source[i] != 0; ++i) {
                    banner->comment[line][i] = source[i];
                }
            }
        }
    }

    s32 NANDInit() {
        return invoke([] { (void)active_nand(); return s32{NAND_RESULT_OK}; });
    }

    s32 NANDCreate(const char* path, u8 permission, u8 attribute) {
        return invoke([&] {
            if (!valid_path(path) || (permission & ~0x3fU)) return s32{NAND_RESULT_INVALID};
            const auto normalized = active_nand().normalize_path(path);
            if (normalized.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
            return Access::create(active_nand(), normalized, permission, attribute);
        });
    }

    s32 NANDPrivateCreateAsync(const char* path, u8 permission, u8 attribute, NANDCallback callback,
                               NANDCommandBlock* block) {
      bool accepted = false;
      const auto result = invoke([&] {
        if (!valid_path(path) || !block || !callback || (permission & ~0x3fU) || !(permission & NAND_PERM_RUSR) ||
            !s_activeNand) {
          return s32{!s_activeNand ? NAND_RESULT_FATAL_ERROR : NAND_RESULT_INVALID};
        }
        const auto normalized = active_nand().normalize_path(path);
        if (normalized.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
        accepted = true;
        block->callback = reinterpret_cast<void*>(callback);
        std::memset(block->absPath, 0, sizeof(block->absPath));
        std::memcpy(block->absPath, normalized.c_str(), normalized.size() + 1);
        return Access::create(active_nand(), normalized, permission, attribute);
      });
      if (!accepted) return result;
      complete_async(callback, block, result);
      return NAND_RESULT_OK;
    }

    s32 NANDOpen(const char* path, NANDFileInfo* info, u8 access) {
        return invoke([&] {
            if (!valid_path(path) || !info || access < NAND_ACCESS_READ || access > NAND_ACCESS_RW) return s32{NAND_RESULT_INVALID};
            auto& nand = active_nand();
            const auto normalized = nand.normalize_path(path);
            if (normalized.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
            if (is_open(normalized)) return s32{NAND_RESULT_OPENFD};
            const auto metadata = Access::metadata(nand, normalized);
            if (metadata && (((access & NAND_ACCESS_READ) && !(metadata->permission & NAND_PERM_RUSR)) ||
                             ((access & NAND_ACCESS_WRITE) && !(metadata->permission & NAND_PERM_WUSR)))) return s32{NAND_RESULT_ACCESS};
            // A newly created empty file has no encoded container to translate yet.
            auto bytes = metadata && metadata->size == 0 ? std::optional<std::vector<u8>>(std::vector<u8>{}) : Access::read(nand, path);
            if (!bytes) return s32{NAND_RESULT_NOEXISTS};
            if (s_nextDescriptor == std::numeric_limits<s32>::max()) return s32{NAND_RESULT_MAXFD};
            const s32 descriptor = s_nextDescriptor++;
            Access::files(nand).emplace(descriptor, Access::OpenFile{normalized, std::move(*bytes), 0, access, false, OSGetCurrentThread()});
            std::memset(info, 0, sizeof(*info));
            info->fileDescriptor = descriptor;
            info->origFd = descriptor;
            info->accType = access;
            std::memcpy(info->origPath, normalized.c_str(), normalized.size() + 1);
            return s32{NAND_RESULT_OK};
        });
    }

    s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 access) {
      // The private entry point uses the same descriptor semantics as the
      // normal host open; path privacy is a Wii privilege boundary, not a
      // second host storage implementation.
      const auto result = NANDOpen(path, info, access);
      if (result == NAND_RESULT_OK) info->mark = 1;
      return result;
    }

    s32 NANDRead(NANDFileInfo* info, void* destination, u32 size) {
        return invoke([&] {
            auto* file = find_file(info);
            if (!file || (!destination && size)) return s32{NAND_RESULT_INVALID};
            if (!(file->access & NAND_ACCESS_READ)) return s32{NAND_RESULT_ACCESS};
            const auto available = file->position >= file->bytes.size() ? std::size_t{0}
                                                                        : file->bytes.size() - file->position;
            const auto count = std::min<std::size_t>(size, available);
            if (count > static_cast<std::size_t>(std::numeric_limits<s32>::max())) return s32{NAND_RESULT_INVALID};
            if (count) std::memcpy(destination, file->bytes.data() + file->position, count);
            file->position += count;
            return static_cast<s32>(count);
        });
    }

    s32 NANDReadAsync(NANDFileInfo* info, void* destination, u32 size, NANDCallback callback,
                      NANDCommandBlock* block) {
      bool accepted = false;
      const auto result = invoke([&] {
        if (!info || !block || !callback || (!destination && size) || !s_activeNand) {
          return s32{!s_activeNand ? NAND_RESULT_FATAL_ERROR : NAND_RESULT_INVALID};
        }
        accepted = true;
        block->callback = reinterpret_cast<void*>(callback);
        block->fileInfo = info;
        auto* file = find_file(info);
        if (!file) return s32{NAND_RESULT_INVALID};
        if (!(file->access & NAND_ACCESS_READ)) return s32{NAND_RESULT_ACCESS};
        const auto available = file->position >= file->bytes.size() ? std::size_t{0}
                                                                    : file->bytes.size() - file->position;
        const auto count = std::min<std::size_t>(size, available);
        if (count > static_cast<std::size_t>(std::numeric_limits<s32>::max())) return s32{NAND_RESULT_INVALID};
        if (count) std::memcpy(destination, file->bytes.data() + file->position, count);
        file->position += count;
        return static_cast<s32>(count);
      });
      if (!accepted) return result;
      complete_async(callback, block, result);
      return NAND_RESULT_OK;
    }

    s32 NANDWrite(NANDFileInfo* info, const void* source, u32 size) {
        return invoke([&] {
            auto* file = find_file(info);
            if (!file || (!source && size) || size > static_cast<u32>(std::numeric_limits<s32>::max())) return s32{NAND_RESULT_INVALID};
            if (!(file->access & NAND_ACCESS_WRITE)) return s32{NAND_RESULT_ACCESS};
            if (size > std::numeric_limits<std::size_t>::max() - file->position) return s32{NAND_RESULT_INVALID};
            const auto end = file->position + size;
            const auto added_blocks = (end + 0x3fffU) / 0x4000U - (file->bytes.size() + 0x3fffU) / 0x4000U;
            if (end > file->bytes.size()) {
                const auto capacity = active_nand().check(static_cast<u32>(added_blocks), 0);
                if (capacity.result != NAND_RESULT_OK) return capacity.result;
                file->bytes.resize(end);
            }
            if (size) std::memcpy(file->bytes.data() + file->position, source, size);
            file->position = end;
            file->dirty = file->dirty || size != 0;
            return static_cast<s32>(size);
        });
    }

    s32 NANDGetLength(NANDFileInfo* info, u32* length) {
      return invoke([&] {
        const auto* file = find_file(info);
        if (!file || !length || file->bytes.size() > std::numeric_limits<u32>::max()) return s32{NAND_RESULT_INVALID};
        *length = static_cast<u32>(file->bytes.size());
        return s32{NAND_RESULT_OK};
      });
    }

    s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) {
      return invoke([&] {
        if (!s_activeNand) return s32{NAND_RESULT_FATAL_ERROR};
        return seek_file(info, offset, whence);
      });
    }

    s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, s32 whence, NANDCallback callback,
                      NANDCommandBlock* block) {
      bool accepted = false;
      const auto result = invoke([&] {
        if (!info || !block || !callback || !s_activeNand) {
          return s32{!s_activeNand ? NAND_RESULT_FATAL_ERROR : NAND_RESULT_INVALID};
        }
        if (whence < 0 || whence > 2 || !find_file(info)) return s32{NAND_RESULT_INVALID};
        accepted = true;
        block->callback = reinterpret_cast<void*>(callback);
        block->fileInfo = info;
        return seek_file(info, offset, whence);
      });
      if (!accepted) return result;
      complete_async(callback, block, result);
      return NAND_RESULT_OK;
    }

    s32 NANDGetLengthAsync(NANDFileInfo* info, u32* length, NANDCallback callback,
                           NANDCommandBlock* block) {
      bool accepted = false;
      const auto result = invoke([&] {
        if (!info || !length || !block || !callback || !s_activeNand) {
          return s32{!s_activeNand ? NAND_RESULT_FATAL_ERROR : NAND_RESULT_INVALID};
        }
        const auto* file = find_file(info);
        if (!file || file->bytes.size() > std::numeric_limits<u32>::max()) return s32{NAND_RESULT_INVALID};
        accepted = true;
        block->callback = reinterpret_cast<void*>(callback);
        block->fileInfo = info;
        block->length = length;
        block->pos = nullptr;
        *length = static_cast<u32>(file->bytes.size());
        return s32{NAND_RESULT_OK};
      });
      if (!accepted) return result;
      complete_async(callback, block, result);
      return NAND_RESULT_OK;
    }

    void NANDSetUserData(NANDCommandBlock* block, void* data) {
      block->userData = data;
    }

    void* NANDGetUserData(const NANDCommandBlock* block) {
      return block->userData;
    }

    s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, const u8 access, void* buffer,
                                 const u32 bufferLength, NANDCallback callback, NANDCommandBlock* block) {
      bool accepted = false;
      const auto result = invoke([&] {
        if (!valid_path(path) || !info || !block || !callback || access < NAND_ACCESS_READ || access > NAND_ACCESS_RW ||
            !s_activeNand || (access != NAND_ACCESS_READ && (!buffer || bufferLength == 0))) {
          if (!s_activeNand) return s32{NAND_RESULT_FATAL_ERROR};
          return s32{NAND_RESULT_INVALID};
        }

        auto& nand = active_nand();
        const auto originalPath = nand.normalize_path(path);
        if (originalPath.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
        if (is_open(originalPath)) return s32{NAND_RESULT_OPENFD};
        accepted = true;
        block->callback = reinterpret_cast<void*>(callback);
        block->fileInfo = info;
        block->simpleFlag = FALSE;
        block->state = 0;
        block->copyBuf = buffer;
        block->bufLength = bufferLength;
        std::memset(block->absPath, 0, sizeof(block->absPath));
        std::memcpy(block->absPath, originalPath.c_str(), originalPath.size() + 1);

        const auto metadata = Access::metadata(nand, originalPath);
        if (!has_access(metadata, access)) {
          return s32{NAND_RESULT_ACCESS};
        }
        const auto originalBytes = Access::read(nand, originalPath);
        if (!originalBytes) return s32{NAND_RESULT_NOEXISTS};

        const auto userPermission = metadata ? metadata->permission : u8{0x3C};
        const auto userAttribute = metadata ? metadata->attribute : u8{0};
        std::string temporaryPath;
        TemporaryFileCleanup temporaryCleanup{nand, temporaryPath};
        if (access != NAND_ACCESS_READ) {
          const auto id = s_nextSafeOpenId.fetch_add(1, std::memory_order_relaxed);
          block->uniqNo = id;
          const auto name = aurora::NandFileSystem::file_name(originalPath);
          char temporary[NAND_MAX_PATH]{};
          const int written = std::snprintf(temporary, sizeof(temporary), "/tmp/sys/%08x/%s", id, name.c_str());
          if (written < 0 || static_cast<std::size_t>(written) >= sizeof(temporary)) {
            return s32{NAND_RESULT_INVALID};
          }
          temporaryPath = nand.normalize_path(temporary);
          if (is_open(temporaryPath)) return s32{NAND_RESULT_OPENFD};

          const auto neededBlocks = (originalBytes->size() + 0x3FFFU) / 0x4000U;
          if (neededBlocks > std::numeric_limits<u32>::max()) return s32{NAND_RESULT_MAXBLOCKS};
          const auto capacity = nand.check(static_cast<u32>(neededBlocks), 1);
          if (capacity.result != NAND_RESULT_OK) return capacity.result;
          const auto created = Access::create(nand, temporaryPath, userPermission, userAttribute);
          if (created != NAND_RESULT_OK) return created;
          temporaryCleanup.active = true;

          std::vector<u8> copied;
          copied.reserve(originalBytes->size());
          for (std::size_t offset = 0; offset < originalBytes->size();) {
            const auto count = std::min<std::size_t>(bufferLength, originalBytes->size() - offset);
            std::memcpy(buffer, originalBytes->data() + offset, count);
            const auto* bytes = static_cast<const u8*>(buffer);
            copied.insert(copied.end(), bytes, bytes + count);
            offset += count;
          }
          Access::commit(nand, Access::OpenFile{temporaryPath, std::move(copied), 0, NAND_ACCESS_WRITE, true,
                                                OSGetCurrentThread()},
                         userPermission, userAttribute);
        }

        auto cleanInfo = NANDFileInfo{};
        cleanInfo.fileDescriptor = -1;
        cleanInfo.origFd = -1;
        cleanInfo.accType = access;
        cleanInfo.stage = access == NAND_ACCESS_READ ? 2 : 5;
        cleanInfo.mark = 3;
        std::memcpy(cleanInfo.origPath, originalPath.c_str(), originalPath.size() + 1);
        s32 originalDescriptor = -1;
        s32 activeDescriptor = -1;
        OpenDescriptorCleanup originalDescriptorCleanup{nand};
        if (access == NAND_ACCESS_READ) {
          activeDescriptor = open_descriptor(nand, originalPath, *originalBytes, access);
          if (activeDescriptor < 0) return activeDescriptor;
          cleanInfo.fileDescriptor = activeDescriptor;
        } else {
          originalDescriptor = open_descriptor(nand, originalPath, *originalBytes, NAND_ACCESS_READ);
          originalDescriptorCleanup.descriptor = originalDescriptor;
          if (originalDescriptor < 0) return originalDescriptor;
          const auto temporaryBytes = Access::read(nand, temporaryPath);
          if (!temporaryBytes) {
            originalDescriptorCleanup.close();
            return s32{NAND_RESULT_NOEXISTS};
          }
          activeDescriptor = open_descriptor(nand, temporaryPath, *temporaryBytes, access);
          if (activeDescriptor < 0) {
            originalDescriptorCleanup.close();
            return activeDescriptor;
          }
          cleanInfo.fileDescriptor = activeDescriptor;
          cleanInfo.origFd = originalDescriptor;
          std::memcpy(cleanInfo.tmpPath, temporaryPath.c_str(), temporaryPath.size() + 1);
        }
        *info = cleanInfo;
        originalDescriptorCleanup.release();
        temporaryCleanup.active = false;
        block->state = 9;
        return s32{NAND_RESULT_OK};
      });
      if (!accepted) return result;
      complete_async(callback, block, result);
      return NAND_RESULT_OK;
    }

    s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback callback, NANDCommandBlock* block) {
      bool accepted = false;
      const auto result = invoke([&] {
        if (!info || !block || !callback || !s_activeNand) {
          return s32{!s_activeNand ? NAND_RESULT_FATAL_ERROR : NAND_RESULT_INVALID};
        }
        if (info->mark != 3 ||
            (info->accType != NAND_ACCESS_READ && info->accType != NAND_ACCESS_WRITE && info->accType != NAND_ACCESS_RW)) {
          return s32{NAND_RESULT_INVALID};
        }
        accepted = true;
        block->callback = reinterpret_cast<void*>(callback);
        block->fileInfo = info;
        block->simpleFlag = FALSE;
        block->state = 10;
        auto& nand = active_nand();

        if (info->accType == NAND_ACCESS_READ) {
          if (!find_file(info)) return s32{NAND_RESULT_INVALID};
          close_descriptor(nand, info->fileDescriptor, false);
          info->fileDescriptor = -1;
          info->stage = 7;
          info->mark = 4;
          return s32{NAND_RESULT_OK};
        }

        auto& files = Access::files(nand);
        const auto temp = files.find(info->fileDescriptor);
        if (temp == files.end() || temp->second.path != info->tmpPath) return s32{NAND_RESULT_INVALID};
        close_descriptor(nand, info->fileDescriptor, true);
        info->fileDescriptor = -1;
        info->stage = 6;

        const auto original = files.find(info->origFd);
        if (original == files.end() || original->second.path != info->origPath) return s32{NAND_RESULT_INVALID};
        close_descriptor(nand, info->origFd, false);
        info->origFd = -1;
        info->stage = 7;

        const auto moved = Access::move(nand, info->tmpPath, info->origPath);
        if (moved != NAND_RESULT_OK) return moved;
        info->stage = 8;
        info->stage = 9;
        info->mark = 4;
        return s32{NAND_RESULT_OK};
      });
      if (!accepted) return result;
      complete_async(callback, block, result);
      return NAND_RESULT_OK;
    }

    s32 NANDClose(NANDFileInfo* info) {
        return invoke([&] {
            if (!find_file(info)) return s32{NAND_RESULT_INVALID};
            auto node = Access::files(active_nand()).extract(info->fileDescriptor);
            info->fileDescriptor = -1;
            info->origFd = -1;
            auto& file = node.mapped();
            if (file.dirty)
                Access::commit(active_nand(), file);
            return s32{NAND_RESULT_OK};
        });
    }

    s32 NANDDelete(const char* path) {
        return invoke([&] {
            if (!valid_path(path)) return s32{NAND_RESULT_INVALID};
            auto& nand = active_nand();
            const auto normalized = nand.normalize_path(path);
            if (normalized.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
            if (is_open(normalized)) return s32{NAND_RESULT_OPENFD};
            return s32{Access::erase(nand, path) ? NAND_RESULT_OK : NAND_RESULT_NOEXISTS};
        });
    }

    s32 NANDMove(const char* source, const char* destination_directory) {
        return invoke([&] {
            if (!valid_path(source) || !valid_path(destination_directory)) return s32{NAND_RESULT_INVALID};
            auto& nand = active_nand();
            const auto normalized_source = nand.normalize_path(source);
            if (normalized_source.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
            auto destination = nand.normalize_path(destination_directory);
            if (destination != "/") destination += '/';
            destination += aurora::NandFileSystem::file_name(normalized_source);
            if (destination.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
            if (is_open(normalized_source) || is_open(destination)) return s32{NAND_RESULT_OPENFD};
            return Access::move(nand, normalized_source, destination);
        });
    }

    s32 NANDCheck(u32 blocks, u32 inodes, u32* answer) {
        return invoke([&] {
            if (!answer) return s32{NAND_RESULT_INVALID};
            const auto& nand = active_nand();
            const auto home = nand.usage(nand.title_data_root());
            aurora::NandUsage user;
            for (const auto* root : {"/meta", "/ticket", "/title/00010000", "/title/00010001", "/title/00010003",
                                     "/title/00010004", "/title/00010005", "/title/00010006", "/title/00010007", "/shared2/title"}) {
                const auto usage = nand.usage(root);
                user.blocks += usage.blocks;
                user.inodes += usage.inodes;
            }
            *answer = 0;
            if (static_cast<u64>(home.blocks) + blocks > 0x400U) *answer |= NAND_CHECK_HOME_INSSPACE;
            if (static_cast<u64>(home.inodes) + inodes > 0x21U) *answer |= NAND_CHECK_HOME_INSINODE;
            if (static_cast<u64>(user.blocks) + blocks > 0x4400U) *answer |= NAND_CHECK_SYS_INSSPACE;
            if (static_cast<u64>(user.inodes) + inodes > 0xfa0U) *answer |= NAND_CHECK_SYS_INSINODE;
            // Capacity exhaustion is reported in the answer bits, not as an I/O error.
            return s32{NAND_RESULT_OK};
        });
    }

    s32 NANDGetHomeDir(char* path) {
        return invoke([&] {
            if (!path) return s32{NAND_RESULT_INVALID};
            (void)active_nand();
            const auto& directory = active_nand().title_data_root();
            if (directory.size() >= NAND_MAX_PATH) return s32{NAND_RESULT_INVALID};
            std::memcpy(path, directory.c_str(), directory.size() + 1);
            return s32{NAND_RESULT_OK};
        });
    }
}
