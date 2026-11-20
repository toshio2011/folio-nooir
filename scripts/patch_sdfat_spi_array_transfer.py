"""Keep SdFat's 512-byte SPI-array transmit scratch in one bounded helper.

The helper is compiled only for the experimental array-transfer profiles. It
keeps the existing copy-before-transfer behavior while making the stack frame
explicit and easy to inspect with the compiler's stack-usage report.
"""

from pathlib import Path

Import("env")  # noqa: F821 (SCons-injected global)


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
PIO_ENV = env.subst("$PIOENV")  # noqa: F821
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
DRIVER_HEADER = DRIVER_SOURCE.with_name("SdSpiArduinoDriver.h")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError("Expected one SdFat patch anchor for " + label)
    return text.replace(old, new, 1)


def remove_once(text: str, old: str, label: str) -> str:
    if old not in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError("Expected one legacy SdFat block for " + label)
    return text.replace(old, "", 1)


if not DRIVER_SOURCE.is_file():
    raise RuntimeError("SdFat Arduino SPI driver not found for " + PIO_ENV)
if not DRIVER_HEADER.is_file():
    raise RuntimeError("SdFat Arduino SPI driver declaration not found for " + PIO_ENV)

source = DRIVER_SOURCE.read_text(encoding="utf-8")
header = DRIVER_HEADER.read_text(encoding="utf-8")

# PlatformIO may reuse a dependency directory patched by the earlier
# X3-conservative experiment. Normalize those generated files before applying
# the shared implementation so a stale cache cannot silently retain a runtime
# X3 transfer gate.
header = remove_once(
    header,
    "#if defined(NOOIR_SPI_ARRAY_X3_FALLBACK)\n"
    "  bool m_nooirArrayTransfersAllowed = false;\n"
    "#endif\n",
    "X3-only driver state",
)
source = remove_once(
    source,
    "#if defined(NOOIR_SPI_ARRAY_X3_FALLBACK)\n"
    "extern \"C\" bool nooirSdArrayTransfersAllowedForDetectedDevice();\n"
    "#if USE_SPI_ARRAY_TRANSFER == 1\n"
    "static __attribute__((noinline)) void nooirSendSdSpiArray(SPIClass* spi, const uint8_t* buf, size_t count) {\n"
    "  uint8_t tmp[512];\n"
    "  while (count > 0) {\n"
    "    size_t n = count <= sizeof(tmp) ? count : sizeof(tmp);\n"
    "    memcpy(tmp, buf, n);\n"
    "    spi->transfer(tmp, n);\n"
    "    count -= n;\n"
    "    buf += n;\n"
    "  }\n"
    "}\n"
    "#endif\n"
    "#endif\n",
    "legacy X3-only array helper and callback",
)
source = remove_once(
    source,
    "#if defined(NOOIR_SPI_ARRAY_X3_FALLBACK)\n"
    "  m_nooirArrayTransfersAllowed = nooirSdArrayTransfersAllowedForDetectedDevice();\n"
    "#endif\n",
    "legacy X3 runtime detection gate",
)
receive_guarded = (
    "#elif USE_SPI_ARRAY_TRANSFER == 1\n"
    "#if defined(NOOIR_SPI_ARRAY_X3_FALLBACK)\n"
    "  if (m_nooirArrayTransfersAllowed) {\n"
    "    memset(buf, 0XFF, count);\n"
    "    m_spi->transfer(buf, count);\n"
    "  } else {\n"
    "    for (size_t i = 0; i < count; i++) buf[i] = m_spi->transfer(0XFF);\n"
    "  }\n"
    "#else\n"
    "  memset(buf, 0XFF, count);\n"
    "  m_spi->transfer(buf, count);\n"
    "#endif"
)
receive_shared = (
    "#elif USE_SPI_ARRAY_TRANSFER == 1\n"
    "  memset(buf, 0XFF, count);\n"
    "  m_spi->transfer(buf, count);"
)
if receive_guarded in source:
    source = replace_once(source, receive_guarded, receive_shared, "legacy X3 guarded array receive")
elif receive_shared not in source:
    raise RuntimeError("Expected SdFat array receive implementation")

send_guarded = (
    "#elif USE_SPI_ARRAY_TRANSFER == 1\n"
    "#if defined(NOOIR_SPI_ARRAY_X3_FALLBACK)\n"
    "  if (m_nooirArrayTransfersAllowed) {\n"
    "    nooirSendSdSpiArray(m_spi, buf, count);\n"
    "  } else {\n"
    "    for (size_t i = 0; i < count; i++) m_spi->transfer(buf[i]);\n"
    "  }\n"
    "#else\n"
    "  uint8_t tmp[512];\n"
    "  while (count > 0) {\n"
    "    size_t n = count <= sizeof(tmp) ? count : sizeof(tmp);\n"
    "    memcpy(tmp, buf, n);\n"
    "    m_spi->transfer(tmp, n);\n"
    "    count -= n;\n"
    "    buf += n;\n"
    "  }\n"
    "#endif"
)
send_stock = (
    "#elif USE_SPI_ARRAY_TRANSFER == 1\n"
    "  uint8_t tmp[512];\n"
    "  while (count > 0) {\n"
    "    size_t n = count <= sizeof(tmp) ? count : sizeof(tmp);\n"
    "    memcpy(tmp, buf, n);\n"
    "    m_spi->transfer(tmp, n);\n"
    "    count -= n;\n"
    "    buf += n;\n"
    "  }"
)
send_shared = "#elif USE_SPI_ARRAY_TRANSFER == 1\n  nooirSendSdSpiArray(m_spi, buf, count);"
if send_guarded in source:
    source = replace_once(source, send_guarded, send_shared, "legacy X3 guarded array send")
elif send_stock in source:
    source = replace_once(source, send_stock, send_shared, "SdFat array send")
elif send_shared not in source:
    raise RuntimeError("Expected SdFat array send implementation")
source = source.replace("\r\n", "\n")
header = header.replace("\r\n", "\n")
legacy_symbols = (
    "NOOIR_SPI_ARRAY_X3_FALLBACK",
    "nooirSdArrayTransfersAllowedForDetectedDevice",
    "m_nooirArrayTransfersAllowed",
)
if any(symbol in source or symbol in header for symbol in legacy_symbols):
    raise RuntimeError("Stale X3-only SPI-array guard remains in cached SdFat")
helper = (
    "#pragma once\n"
    "#if USE_SPI_ARRAY_TRANSFER == 1\n"
    "static __attribute__((noinline)) void nooirSendSdSpiArray(SPIClass* spi, "
    "const uint8_t* buf, size_t count) {\n"
    "  uint8_t tmp[512];\n"
    "  while (count > 0) {\n"
    "    size_t n = count <= sizeof(tmp) ? count : sizeof(tmp);\n"
    "    memcpy(tmp, buf, n);\n"
    "    spi->transfer(tmp, n);\n"
    "    count -= n;\n"
    "    buf += n;\n"
    "  }\n"
    "}\n"
    "#endif"
)
if "static __attribute__((noinline)) void nooirSendSdSpiArray(" not in source:
    source = replace_once(source, "#pragma once", helper, "bounded array-send helper")

if send_shared not in source:
    raise RuntimeError("Shared SdFat array-send implementation was not installed")

DRIVER_SOURCE.write_text(source, encoding="utf-8", newline="\n")
DRIVER_HEADER.write_text(header, encoding="utf-8", newline="\n")
print("Applied shared SdFat SPI-array transfer patch for " + PIO_ENV)
