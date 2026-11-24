"""Apply Nooir-local bounded MSC I/O slices so long TinyUSB requests yield/feed WDT."""

from __future__ import annotations

from pathlib import Path

MARKER = "// NOOIR_USB_MSC_WDT_CHUNKED_IO"


def patch_source(source: str) -> str:
    if MARKER in source:
        return source

    source = source.replace(
        "#include <cstring>",
        "#include <cstring>\n#include <esp_task_wdt.h>\n#include <freertos/FreeRTOS.h>",
        1,
    )
    source = source.replace(
        "constexpr uint16_t kBlockSize = 512;",
        "constexpr uint16_t kBlockSize = 512;\n"
        "constexpr size_t kSectorsPerWatchdogYield = 16;\n"
        + MARKER,
        1,
    )

    helper_anchor = "bool isRangeValid("
    helper = (
        "void serviceTransferWatchdog() {\n"
        "  if (esp_task_wdt_status(nullptr) == ESP_OK) (void)esp_task_wdt_reset();\n"
        "  taskYIELD();\n"
        "}\n\n"
    )
    if source.count(helper_anchor) != 1:
        raise RuntimeError("USB MSC patch anchor is missing: range validator")
    source = source.replace(helper_anchor, helper + helper_anchor, 1)

    read_start = source.index("int32_t mscRead(")
    read_end = source.index("\nint32_t mscWrite(", read_start)
    read = source[read_start:read_end]
    block_start = read.index("  if ((offset % kBlockSize) == 0 && (bufsize % kBlockSize) == 0) {")
    block_end = read.index("  if (owner) owner->markAccessed();", block_start)
    read_replacement = """  if ((offset % kBlockSize) == 0 && (bufsize % kBlockSize) == 0) {
    size_t sectors = bufsize / kBlockSize;
    Sector_t sector = static_cast<Sector_t>(start / kBlockSize);
    while (sectors > 0) {
      const size_t count = std::min(sectors, kSectorsPerWatchdogYield);
      if (!dev->readSectors(sector, output, count)) {
        if (owner) owner->markIoError();
        return -1;
      }
      output += count * kBlockSize;
      sector += count;
      sectors -= count;
      serviceTransferWatchdog();
    }
  } else {
    uint32_t sectorsSinceYield = 0;
    while (remaining > 0) {
      const auto sector = static_cast<Sector_t>(cursor / kBlockSize);
      const size_t within = static_cast<size_t>(cursor % kBlockSize);
      const size_t count = std::min<size_t>(kBlockSize - within, remaining);
      if (!dev->readSector(sector, gSectorScratch)) {
        if (owner) owner->markIoError();
        return -1;
      }
      memcpy(output, gSectorScratch + within, count);
      output += count;
      cursor += count;
      remaining -= count;
      if (++sectorsSinceYield >= kSectorsPerWatchdogYield && remaining > 0) {
        serviceTransferWatchdog();
        sectorsSinceYield = 0;
      }
    }
  }

"""
    read = read[:block_start] + read_replacement + read[block_end:]
    source = source[:read_start] + read + source[read_end:]

    write_start = source.index("int32_t mscWrite(")
    write_end = source.index("\nbool mscStartStop(", write_start)
    write = source[write_start:write_end]
    block_start = write.index("  if ((offset % kBlockSize) == 0 && (bufsize % kBlockSize) == 0) {")
    block_end = write.index("  if (owner) owner->markAccessed();", block_start)
    write_replacement = """  if ((offset % kBlockSize) == 0 && (bufsize % kBlockSize) == 0) {
    size_t sectors = bufsize / kBlockSize;
    Sector_t sector = static_cast<Sector_t>(start / kBlockSize);
    while (sectors > 0) {
      const size_t count = std::min(sectors, kSectorsPerWatchdogYield);
      if (!dev->writeSectors(sector, input, count)) {
        if (owner) owner->markIoError();
        return -1;
      }
      input += count * kBlockSize;
      sector += count;
      sectors -= count;
      serviceTransferWatchdog();
    }
  } else {
    uint32_t sectorsSinceYield = 0;
    while (remaining > 0) {
      const auto sector = static_cast<Sector_t>(cursor / kBlockSize);
      const size_t within = static_cast<size_t>(cursor % kBlockSize);
      const size_t count = std::min<size_t>(kBlockSize - within, remaining);
      if (!dev->readSector(sector, gSectorScratch)) {
        if (owner) owner->markIoError();
        return -1;
      }
      memcpy(gSectorScratch + within, input, count);
      if (!dev->writeSector(sector, gSectorScratch)) {
        if (owner) owner->markIoError();
        return -1;
      }
      input += count;
      cursor += count;
      remaining -= count;
      if (++sectorsSinceYield >= kSectorsPerWatchdogYield && remaining > 0) {
        serviceTransferWatchdog();
        sectorsSinceYield = 0;
      }
    }
  }

"""
    write = write[:block_start] + write_replacement + write[block_end:]
    return source[:write_start] + write + source[write_end:]


def patch_file(path: Path, output: Path | None = None) -> None:
    original = path.read_text(encoding="utf-8")
    patched = patch_source(original)
    destination = output or path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(patched, encoding="utf-8", newline="\n")


try:
    Import("env")  # noqa: F821 -- PlatformIO/SCons builtin
    _device = str(env.GetProjectOption("custom_nooir_device_id", "")).upper()  # noqa: F821
    if _device in {"X4PRO", "X4CLASSIC"}:
        _project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
        _sdk = _project / "freeink-sdk-s3" / "libs" / "hardware" / "UsbMassStorage" / "src" / "UsbMassStorage.cpp"
        patch_file(_sdk)
except NameError:
    # Host tests import the pure patch_source function without SCons.
    pass


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    patch_file(args.source, args.output)