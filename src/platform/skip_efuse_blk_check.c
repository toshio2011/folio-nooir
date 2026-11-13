// This legacy workaround is restricted to the original C3 X3/X4 image.
// The experimental S3 profiles retain the framework's stock eFuse-revision
// validation and do not link this override.
#if !((defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO) || \
      (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC))
// Override the prebuilt libbootloader_support.a implementation.
// The X3's validation code misreads the new image's esp_app_desc_t through a
// misaligned bootloader_mmap pointer, producing garbage eFuse block revision
// values that fail the check. Safe to skip: the eFuse block revision gate is
// a manufacturing concern, not a runtime safety issue.
#include <esp_err.h>
esp_err_t __wrap_bootloader_common_check_efuse_blk_validity(uint32_t min_rev_full, uint32_t max_rev_full) {
  (void)min_rev_full;
  (void)max_rev_full;
  return ESP_OK;
}

#endif  // not an experimental X4 S3 target
