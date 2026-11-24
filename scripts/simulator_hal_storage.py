"""Add the bounded Nooir checked-read API to the host simulator HAL.

The simulator's host-backed card remains the source of file data. This patch
only mirrors the device HAL status API so existing application storage code
can run unchanged against the simulated filesystem.
"""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


SIMULATOR_ENVS = {
    "simulator_x4",
    "simulator_x3",
    "simulator_x4pro",
    "simulator_x4classic",
    "simulator_nooir167_x4pro",
    "simulator_nooir167_x4classic",
}

if str(env.get("PIOENV") or "") in SIMULATOR_ENVS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = (
        project_dir / ".pio" / "libdeps" / str(env.get("PIOENV")) / "simulator"
    )
    header_path = simulator_dir / "src" / "HalStorage.h"
    source_path = simulator_dir / "src" / "HalStorage.cpp"

    if not header_path.is_file() or not source_path.is_file():
        print("Simulator HalStorage compatibility: simulator dependency not installed yet")
    else:
        header = header_path.read_text(encoding="utf-8")
        source = source_path.read_text(encoding="utf-8")

        if "#include <ctime>" not in source:
            anchor = "#include <cstring>\n"
            if anchor not in source:
                raise RuntimeError("Simulator HalStorage.cpp changed: cstring include missing")
            source = source.replace(anchor, anchor + "#include <ctime>\n", 1)

        enum_decl = """enum class HalStorageReadStatus : uint8_t {
  Success,
  NotFound,
  ParentMissing,
  Unavailable,
  IoError,
  TooLarge,
};

"""
        if "enum class HalStorageReadStatus" not in header:
            anchor = "class HalFile;\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalStorage.h changed: HalFile forward declaration missing")
            header = header.replace(anchor, enum_decl + anchor, 1)

        if "cardCapacityBytes()" not in header:
            anchor = "  bool ready() const;\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalStorage.h changed: ready() declaration missing")
            header = header.replace(
                anchor,
                anchor + "  uint64_t cardCapacityBytes() const;\n",
                1,
            )

        method_decl = (
            "  // Bounded read with explicit missing/I/O status for recovery-sensitive files.\n"
            "  HalStorageReadStatus readFileChecked(const char *path, String &content, "
            "size_t maxBytes = 65536);\n"
        )
        if "readFileChecked(" not in header:
            anchor = "  bool ready() const;\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalStorage.h changed: ready() declaration missing")
            header = header.replace(anchor, anchor + method_decl, 1)

        if "getModifyDateTime(" not in header:
            anchor = "  uint32_t modificationTime();\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalStorage.h changed: modificationTime() declaration missing")
            header = header.replace(
                anchor,
                anchor + "  bool getModifyDateTime(uint16_t *date, uint16_t *time);\n",
                1,
            )

        marker = "HalStorageReadStatus HalStorage::readFileChecked("
        if "HalStorage::cardCapacityBytes()" not in source:
            anchor = "bool HalStorage::ready() const { return true; }\n"
            if anchor not in source:
                raise RuntimeError("Simulator HalStorage.cpp changed: ready() implementation missing")
            source = source.replace(
                anchor,
                anchor
                + "// Host-backed storage has no modeled card capacity.\n"
                + "uint64_t HalStorage::cardCapacityBytes() const { return 0; }\n",
                1,
            )

        if marker not in source:
            anchor = "bool HalStorage::ready() const { return true; }\n"
            if anchor not in source:
                raise RuntimeError("Simulator HalStorage.cpp changed: ready() implementation missing")
            implementation = r'''HalStorageReadStatus HalStorage::readFileChecked(
    const char *path, String &content, size_t maxBytes) {
  content = String("");
  if (!ready()) {
    return HalStorageReadStatus::Unavailable;
  }

  const std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return HalStorageReadStatus::IoError;
  }

  struct stat info {};
  if (::stat(full.c_str(), &info) != 0) {
    if (errno != ENOENT) {
      return HalStorageReadStatus::IoError;
    }
    const size_t slash = full.find_last_of('/');
    if (slash == std::string::npos) {
      return HalStorageReadStatus::ParentMissing;
    }
    const std::string parent = slash == 0 ? std::string("/")
                                          : full.substr(0, slash);
    struct stat parentInfo {};
    if (::stat(parent.c_str(), &parentInfo) != 0) {
      return errno == ENOENT ? HalStorageReadStatus::ParentMissing
                             : HalStorageReadStatus::IoError;
    }
    return S_ISDIR(parentInfo.st_mode) ? HalStorageReadStatus::NotFound
                                       : HalStorageReadStatus::IoError;
  }
  if (!S_ISREG(info.st_mode) || info.st_size < 0) {
    return HalStorageReadStatus::IoError;
  }
  if (static_cast<uint64_t>(info.st_size) > maxBytes) {
    return HalStorageReadStatus::TooLarge;
  }

  const int fd = ::open(full.c_str(), O_RDONLY);
  if (fd < 0) {
    return HalStorageReadStatus::IoError;
  }

  std::string data;
  data.reserve(static_cast<size_t>(info.st_size));
  std::vector<char> buffer(4096);
  for (;;) {
    const size_t remaining =
        maxBytes - data.size() < buffer.size()
            ? maxBytes - data.size() + 1
            : buffer.size();
    const ssize_t count = ::read(fd, buffer.data(), remaining);
    if (count < 0) {
      ::close(fd);
      return HalStorageReadStatus::IoError;
    }
    if (count == 0) {
      break;
    }
    if (static_cast<size_t>(count) > maxBytes - data.size()) {
      ::close(fd);
      return HalStorageReadStatus::TooLarge;
    }
    data.append(buffer.data(), static_cast<size_t>(count));
  }
  ::close(fd);

  content = String(data);
  return HalStorageReadStatus::Success;
}

'''
            source = source.replace(anchor, anchor + implementation, 1)

        if "HalFile::getModifyDateTime(" not in source:
            anchor = "uint32_t HalFile::modificationTime() {"
            start = source.find(anchor)
            if start < 0:
                raise RuntimeError("Simulator HalStorage.cpp changed: modificationTime() implementation missing")
            end = source.find("\n}\n", start)
            if end < 0:
                raise RuntimeError("Simulator HalStorage.cpp changed: modificationTime() body shape changed")
            end += len("\n}\n")
            implementation = r'''bool HalFile::getModifyDateTime(uint16_t *date, uint16_t *time) {
  if (!date || !time || !impl || impl->path.empty())
    return false;
  struct stat info{};
  const int result = (impl->fd >= 0) ? fstat(impl->fd, &info)
                                     : stat(impl->path.c_str(), &info);
  if (result != 0)
    return false;
  std::tm utc{};
  if (!gmtime_r(&info.st_mtime, &utc))
    return false;
  const int year = utc.tm_year + 1900;
  if (year < 1980 || year > 2107)
    return false;
  *date = static_cast<uint16_t>(((year - 1980) << 9) |
                                ((utc.tm_mon + 1) << 5) | utc.tm_mday);
  *time = static_cast<uint16_t>((utc.tm_hour << 11) | (utc.tm_min << 5) |
                                (utc.tm_sec / 2));
  return true;
}
'''
            source = source[:end] + implementation + source[end:]

        header_path.write_text(header, encoding="utf-8", newline="")
        source_path.write_text(source, encoding="utf-8", newline="")
        print("Patched simulator HalStorage with bounded Nooir checked reads")
