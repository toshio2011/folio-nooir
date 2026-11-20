import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")
HAL_STORAGE = (ROOT / "lib/hal/HalStorage.cpp").read_text(encoding="utf-8")
SD_MANAGER = (ROOT / "freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp").read_text(encoding="utf-8")
PROBE_HEADER = (ROOT / "lib/hal/NooirSdSpiDiagnostics.h").read_text(encoding="utf-8")
PATCH_SCRIPT = (ROOT / "scripts/patch_sdfat_sd_diagnostics.py").read_text(encoding="utf-8")


def environment_block(name):
    match = re.search(r"(?ms)^\[env:%s\]\n(.*?)(?=^\[env:|\Z)" % re.escape(name), PLATFORMIO)
    if not match:
        raise AssertionError("missing PlatformIO environment: " + name)
    return match.group(1)


class SdSpiDiagnosticsContract(unittest.TestCase):
    def test_sd_trace_is_a_separate_ble_release_derived_profile(self):
        profile = environment_block("gh_release_sdtrace")
        release = environment_block("gh_release")
        self.assertIn("extends = env:gh_release", profile)
        self.assertIn("h2zero/NimBLE-Arduino", release)
        self.assertIn("-DFREEINK_CAP_BLE_HID_HOST=1", release)
        self.assertIn("-DNOOIR_BLE_READER_COEXISTENCE=1", release)
        self.assertIn("-DFREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS=1", release)
        self.assertIn("-DNOOIR_SD_SPI_DIAGNOSTICS=1", profile)
        self.assertIn("patch_sdfat_sd_diagnostics.py", profile)
        self.assertNotIn("NOOIR_SD_SPI_DIAGNOSTICS", release)

    def test_hal_separates_lock_wait_and_instrumented_storage_layers(self):
        self.assertIn("mutex_wait_us=%u lock_hold_us=%u", HAL_STORAGE)
        self.assertIn("rx_us=%llu", HAL_STORAGE)
        self.assertIn("tx_us=%llu", HAL_STORAGE)
        self.assertIn("timer_probe_calls=%u", HAL_STORAGE)
        self.assertIn("timer_probe_overhead_included=1", HAL_STORAGE)
        self.assertIn("busy_wait_us=%llu", HAL_STORAGE)
        self.assertIn("read=%u/%llu/%llu", HAL_STORAGE)
        self.assertIn("write=%u/%llu/%llu", HAL_STORAGE)
        self.assertIn("outside_spi_buffer_us=%u", HAL_STORAGE)
        self.assertIn("task_stack_hwm_bytes=%u", HAL_STORAGE)
        self.assertIn("pcTaskGetName(traceTask)", HAL_STORAGE)
        self.assertIn("uxTaskGetStackHighWaterMark(traceTask)", HAL_STORAGE)

    def test_sd_trace_is_split_into_complete_bounded_records(self):
        formats = re.findall(r'LOG_INF\("SDTRACE", "([^"]+)"', HAL_STORAGE)
        self.assertEqual(len(formats), 5)

        # ESP32 millis() is a 32-bit unsigned long. Include its ten-digit
        # maximum, the log prefix, the macro-added newline, and the NUL byte.
        prefix_bytes = len("[4294967295] [INF] [SDTRACE] ")
        worst_values = {"%s": "x" * 32, "%08x": "F" * 8, "%u": "9" * 10, "%llu": "9" * 20}
        for fmt in formats:
            worst = re.sub(r"%08x|%llu|%u|%s", lambda m: worst_values[m.group(0)], fmt)
            formatted_bytes = prefix_bytes + len(worst) + 1 + 1
            self.assertLessEqual(formatted_bytes, 256)

        self.assertIn("timer_probe_overhead_included=1", formats[0])
        self.assertIn("rx_bytes=%llu", formats[1])
        self.assertIn("tx_bytes=%llu", formats[1])
        # Third-line read and write tuples are calls / sectors / microseconds.
        self.assertIn("read=%u/%llu/%llu", formats[2])
        self.assertIn("write=%u/%llu/%llu", formats[2])
        self.assertIn("path_hash=%08x", formats[3])
        self.assertIn("limit_bytes=%u", formats[3])
        self.assertIn("result_bytes=%u", formats[3])
        self.assertIn("clusters_scanned=%llu", formats[4])

    def test_sdfat_probes_cover_card_ready_poll_and_sector_operations(self):
        self.assertIn('"bool SdSpiCard::waitReady(uint16_t ms) {"', PATCH_SCRIPT)
        self.assertIn('"bool SdSpiCard::readSectors(Sector_t sector, uint8_t* dst, size_t ns) {"', PATCH_SCRIPT)
        self.assertIn('"bool SdSpiCard::writeSectors(Sector_t sector, const uint8_t* src, size_t ns) {"', PATCH_SCRIPT)
        self.assertIn("kNooirSdCardBusyWait", PROBE_HEADER)
        self.assertIn("kNooirSdReadSectors", PROBE_HEADER)
        self.assertIn("kNooirSdWriteSectors", PROBE_HEADER)
        self.assertIn("kNooirSdSpiReceiveBuffer", PROBE_HEADER)
        self.assertIn("kNooirSdSpiSendBuffer", PROBE_HEADER)
        self.assertIn("kNooirSdFatAllocationSearch", PROBE_HEADER)
        self.assertIn("nooirFatAllocationSearch.addUnits()", PATCH_SCRIPT)
        self.assertIn("must not be summed together", PROBE_HEADER)
        self.assertIn("esp_timer_get_time()", PROBE_HEADER)

    def test_successful_file_open_uses_one_directory_lookup(self):
        open_reader = SD_MANAGER.split("bool SDCardManager::openFileForRead(const char* moduleName, const char* path, FsFile& file)", 1)[1]
        open_reader = open_reader.split("bool SDCardManager::openFileForRead(const char* moduleName, const std::string& path, FsFile& file)", 1)[0]
        self.assertIn("file = vol().open(path, O_RDONLY)", open_reader)
        self.assertNotIn("vol().exists(path)", open_reader)
        self.assertIn("readFileChecked performs its own", open_reader)
        self.assertIn("SDCard.openFileForRead(moduleName, path, fsFile)", HAL_STORAGE)

    def test_byte_and_array_trace_profiles_share_stack_usage_instrumentation(self):
        byte = environment_block("gh_release_sdtrace")
        array = environment_block("gh_release_spiarray_sdtrace")
        self.assertIn("-fstack-usage", byte)
        self.assertIn("extends = env:gh_release_sdtrace", array)
        self.assertIn("-DUSE_SPI_ARRAY_TRANSFER=1", array)
        self.assertNotIn("-fstack-usage", array)

    def test_spi_array_option_is_confined_to_ble_release_comparison_profile(self):
        candidate = environment_block("gh_release_spiarray")
        instrumented = environment_block("gh_release_spiarray_sdtrace")
        release = environment_block("gh_release")
        self.assertIn("extends = env:gh_release", candidate)
        self.assertIn("-DUSE_SPI_ARRAY_TRANSFER=1", candidate)
        self.assertIn("-fstack-usage", candidate)
        self.assertIn("extends = env:gh_release_sdtrace", instrumented)
        self.assertIn("-DUSE_SPI_ARRAY_TRANSFER=1", instrumented)
        self.assertIn("-fstack-usage", environment_block("gh_release_sdtrace"))
        self.assertNotIn("spiTransferByteNL", instrumented)
        self.assertNotIn("spiTransferBytesNL", instrumented)
        self.assertNotIn("USE_SPI_ARRAY_TRANSFER", release)
        self.assertIn("h2zero/NimBLE-Arduino", release)
        self.assertIn("-DFREEINK_CAP_BLE_HID_HOST=1", release)


if __name__ == "__main__":
    unittest.main()
