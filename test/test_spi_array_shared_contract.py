from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_experimental_spi_modes_are_shared_by_original_x3_and_x4():
    config = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    release = config.split("[env:gh_release]", 1)[1].split("[env:gh_release_sdtrace]", 1)[0]
    array = config.split("[env:gh_release_spiarray]", 1)[1].split("[env:gh_release_spiarray_sdtrace]", 1)[0]
    array_trace = config.split("[env:gh_release_spiarray_sdtrace]", 1)[1].split("[env:gh_release_epubdiag]", 1)[0]

    assert "FREEINK_DEVICE_X4=1" in release
    assert "FREEINK_DEVICE_X3=1" in release
    assert "USE_SPI_ARRAY_TRANSFER=1" not in release
    assert "USE_SPI_ARRAY_TRANSFER=1" in array
    assert "USE_SPI_ARRAY_TRANSFER=1" in array_trace
    assert "NOOIR_SPI_ARRAY_X3_FALLBACK" not in config
    assert "NOOIR_SPI_ARRAY_ENABLE_X3_EXPERIMENTAL" not in config
    assert "gh_release_spiarray_x3exp" not in config
    assert "patch_sdfat_spi_array_transfer.py" in array
    assert "patch_sdfat_spi_array_transfer.py" in array_trace


def test_array_startup_identity_and_sd_profile_remain_runtime_selected():
    main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    gpio = (ROOT / "lib/hal/HalGPIO.cpp").read_text(encoding="utf-8")
    board = (ROOT / "freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h").read_text(encoding="utf-8")
    storage = (ROOT / "freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp").read_text(encoding="utf-8")

    assert 'const char* sdTransferMode = "array";' in main
    assert 'const char* sdTransferMode = "byte";' in main
    assert "nooirSdArrayTransfersAllowedForDetectedDevice" not in gpio
    assert "BoardConfig::ACTIVE.sd.spiHz" in storage
    assert "constexpr BoardProfile XTEINK_X3" in board
    assert "constexpr BoardProfile XTEINK_X4" in board
    assert "{PIN_UNASSIGNED, 7, PIN_UNASSIGNED, 12, PIN_UNASSIGNED, false, 0}" in board
    assert "{PIN_UNASSIGNED, 7, PIN_UNASSIGNED, 12, 13, false, 0}" in board


def test_array_send_keeps_bounded_copy_in_noninline_stack_frame():
    patch = (ROOT / "scripts/patch_sdfat_spi_array_transfer.py").read_text(encoding="utf-8")

    assert "static __attribute__((noinline)) void nooirSendSdSpiArray" in patch
    assert "uint8_t tmp[512];" in patch
    assert "memcpy(tmp, buf, n);" in patch
    assert "spi->transfer(tmp, n);" in patch
    assert "nooirSendSdSpiArray(m_spi, buf, count);" in patch
    assert "deviceIsX3" not in patch
    assert "Stale X3-only SPI-array guard remains in cached SdFat" in patch
    assert '"legacy X3-only array helper and callback"' in patch
    assert '"legacy X3 guarded array receive"' in patch
    assert '"legacy X3 guarded array send"' in patch
    assert "elif receive_shared not in source" in patch
    assert "elif send_stock in source" in patch
    assert '"Expected SdFat array receive implementation"' in patch
    assert '"Shared SdFat array-send implementation was not installed"' in patch


if __name__ == "__main__":
    test_experimental_spi_modes_are_shared_by_original_x3_and_x4()
    test_array_startup_identity_and_sd_profile_remain_runtime_selected()
    test_array_send_keeps_bounded_copy_in_noninline_stack_frame()
    print("3 shared X3/X4 SPI-array contract checks passed")
