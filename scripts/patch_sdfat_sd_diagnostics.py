"""Add equivalent source-local timing scopes to SdFat for both SPI profiles.

The diagnostic profile patches only its local PlatformIO dependency cache.
Ordinary gh_release builds do not run this script or define the diagnostic
macro, so runtime behavior and release binaries remain unchanged.
"""

from pathlib import Path

Import("env")  # noqa: F821 (SCons-injected global)


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
PIO_ENV = env.subst("$PIOENV")  # noqa: F821
SOURCE = PROJECT_DIR / ".pio" / "libdeps" / PIO_ENV / "SdFat" / "src" / "SdCard" / "SdSpiCard" / "SdSpiCard.cpp"
DRIVER_SOURCE = (
    PROJECT_DIR
    / ".pio"
    / "libdeps"
    / PIO_ENV
    / "SdFat"
    / "src"
    / "SdCard"
    / "SdSpiCard"
    / "SpiDriver"
    / "SdSpiLibDriver.h"
)
FAT_PARTITION_SOURCE = (
    PROJECT_DIR
    / ".pio"
    / "libdeps"
    / PIO_ENV
    / "SdFat"
    / "src"
    / "FatLib"
    / "FatPartition.cpp"
)
INCLUDE_MARKER = "#include <NooirSdSpiDiagnostics.h>  // NOOIR_SD_SPI_DIAGNOSTICS"


def add_scope(source: str, signature: str, scope: str) -> str:
    if scope in source:
        return source
    if source.count(signature) != 1:
        raise RuntimeError("Expected one SdFat timing anchor: %s" % signature.strip())
    return source.replace(signature, signature + "\n#if defined(NOOIR_SD_SPI_DIAGNOSTICS)\n  " + scope + "\n#endif", 1)


if not SOURCE.is_file():
    raise RuntimeError("SdFat source not found for diagnostic environment: %s" % SOURCE)
if not DRIVER_SOURCE.is_file():
    raise RuntimeError("SdFat Arduino SPI driver not found for diagnostic environment: %s" % DRIVER_SOURCE)
if not FAT_PARTITION_SOURCE.is_file():
    raise RuntimeError("SdFat FAT allocation source not found for diagnostic environment: %s" % FAT_PARTITION_SOURCE)

source_text = SOURCE.read_text(encoding="utf-8")
if INCLUDE_MARKER not in source_text:
    include_anchor = '#include "SdSpiCard.h"'
    if source_text.count(include_anchor) != 1:
        raise RuntimeError("Expected one SdSpiCard.h include in %s" % SOURCE)
    source_text = source_text.replace(
        include_anchor,
        include_anchor + "\n#if defined(NOOIR_SD_SPI_DIAGNOSTICS)\n" + INCLUDE_MARKER + "\n#endif",
        1,
    )

source_text = add_scope(
    source_text,
    "bool SdSpiCard::waitReady(uint16_t ms) {",
    "NooirSdTimingScope nooirCardBusyWait(kNooirSdCardBusyWait);",
)
source_text = add_scope(
    source_text,
    "bool SdSpiCard::readSectors(Sector_t sector, uint8_t* dst, size_t ns) {",
    "NooirSdTimingScope nooirReadSectors(kNooirSdReadSectors, static_cast<uint32_t>(ns));",
)
source_text = add_scope(
    source_text,
    "bool SdSpiCard::writeSectors(Sector_t sector, const uint8_t* src, size_t ns) {",
    "NooirSdTimingScope nooirWriteSectors(kNooirSdWriteSectors, static_cast<uint32_t>(ns));",
)

SOURCE.write_text(source_text, encoding="utf-8", newline="\n")

# Measure buffer-level SPI transfers from the SdFat driver. This is the same
# boundary in byte and array mode, so one timer pair covers a complete transfer
# call rather than timing every byte (which would heavily bias byte mode).
driver_text = DRIVER_SOURCE.read_text(encoding="utf-8")
if "NOOIR_SD_SPI_DIAGNOSTICS" not in driver_text:
    include_anchor = "#pragma once"
    if driver_text.count(include_anchor) != 1:
        raise RuntimeError("Expected one driver header pragma anchor in %s" % DRIVER_SOURCE)
    driver_text = driver_text.replace(
        include_anchor,
        include_anchor
        + "\n#if defined(NOOIR_SD_SPI_DIAGNOSTICS)\n"
        + INCLUDE_MARKER
        + "\n#endif",
        1,
    )
driver_text = add_scope(
    driver_text,
    "inline uint8_t SdSpiArduinoDriver::receive(uint8_t* buf, size_t count) {",
    "NooirSdTimingScope nooirSpiReceiveBuffer(kNooirSdSpiReceiveBuffer, static_cast<uint32_t>(count));",
)
driver_text = add_scope(
    driver_text,
    "inline void SdSpiArduinoDriver::send(const uint8_t* buf, size_t count) {",
    "NooirSdTimingScope nooirSpiSendBuffer(kNooirSdSpiSendBuffer, static_cast<uint32_t>(count));",
)
DRIVER_SOURCE.write_text(driver_text, encoding="utf-8", newline="\n")

# Count and time FAT free-cluster search separately. A tiny file write can
# trigger a long metadata scan when the allocator's next-free hint is far from
# an available cluster; card/SPI counters alone cannot identify that work.
fat_text = FAT_PARTITION_SOURCE.read_text(encoding="utf-8")
if "NOOIR_SD_SPI_DIAGNOSTICS" not in fat_text:
    include_anchor = '#include "FatLib.h"'
    if fat_text.count(include_anchor) != 1:
        raise RuntimeError("Expected one FatLib.h include anchor in %s" % FAT_PARTITION_SOURCE)
    fat_text = fat_text.replace(
        include_anchor,
        include_anchor + "\n#if defined(NOOIR_SD_SPI_DIAGNOSTICS)\n" + INCLUDE_MARKER + "\n#endif",
        1,
    )
fat_text = add_scope(
    fat_text,
    "bool FatPartition::allocateCluster(Cluster_t current, Cluster_t* next) {",
    "NooirSdTimingScope nooirFatAllocationSearch(kNooirSdFatAllocationSearch);",
)
scan_anchor = "    int8_t fg = fatGet(find, &f);"
scan_count = "#if defined(NOOIR_SD_SPI_DIAGNOSTICS)\n    nooirFatAllocationSearch.addUnits();\n#endif\n    int8_t fg = fatGet(find, &f);"
if scan_count not in fat_text:
    if fat_text.count(scan_anchor) != 1:
        raise RuntimeError("Expected one FAT allocation-search anchor in %s" % FAT_PARTITION_SOURCE)
    fat_text = fat_text.replace(scan_anchor, scan_count, 1)
FAT_PARTITION_SOURCE.write_text(fat_text, encoding="utf-8", newline="\n")
print("Instrumented SdFat card and buffer-level SPI timing for %s" % PIO_ENV)
