// Replace the generic descriptor in the shared precompiled Arduino/ESP-IDF
// archive with this application's version and build identity. The framework
// definition is weak; this strong definition makes esptool, OTA metadata, and
// on-device ESP-IDF diagnostics report the actual Folio Nooir image.
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"

#ifndef NOOIR_APP_VERSION
#ifdef CROSSPOINT_VERSION
#define NOOIR_APP_VERSION CROSSPOINT_VERSION
#else
#define NOOIR_APP_VERSION "unknown"
#endif
#endif

extern "C" {
__attribute__((used, section(".rodata_desc"))) const esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = NOOIR_APP_VERSION,
    .project_name = "Folio Nooir",
    .time = __TIME__,
    .date = __DATE__,
    .idf_ver = IDF_VER,
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};
}
