#include "util/BleMemoryPolicy.h"

using namespace bleinput;

static_assert(kReaderBleEpubIdleOnlyRequiredFree == 102 * 1024);
static_assert(kReaderBleEpubIdleOnlyRequiredLargest == kReaderBleStartMinLargestBlock);
static_assert(kReaderBleEpubIdleOnlyRequiredLargest == 36 * 1024 - kHeapPoisonLargestBlockOverhead);
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(110 * 1024, 60 * 1024, false, true));
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(110 * 1024, 60 * 1024, true, false));
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(102 * 1024 - 1, 36 * 1024, true, true));
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(102 * 1024, kReaderBleStartMinLargestBlock - 1, true, true));
// Latest automatic sequential-mode COM3: generation 4 reached 106,332/53,236 B.
// The pre-start block floor is the existing general cold-start floor; a separate
// post-start check protects the 32 KiB / 16 KiB recovery reserve.
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(106332, 53236, false, true));
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(106332, 53236, true, false));
static_assert(readerBleEpubIdleOnlyPostStartSafe(32 * 1024, 16 * 1024));
static_assert(!readerBleEpubIdleOnlyPostStartSafe(32 * 1024 - 1, 16 * 1024));
static_assert(!readerBleEpubIdleOnlyPostStartSafe(32 * 1024, 16 * 1024 - 1));
// Supplied 20261119.1 COM3 evidence: XTC had 98,160 B free and 65,524 B
// contiguous, but the universal 98,304 B floor rejected it despite the large
// block. The format-scoped experiment accepts only this measured case while
// retaining the shared contiguous-block requirement.
static_assert(!readerBleStartMemoryAdmitted(98160, 65524));
static_assert(readerBleXtcStartMemoryAdmitted(98160, 65524));
static_assert(!readerBleXtcStartMemoryAdmitted(98160 - 1, 65524));
static_assert(!readerBleXtcStartMemoryAdmitted(95 * 1024, kReaderBleStartMinLargestBlock - 1));

#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
static_assert(readerBleEpubIdleOnlyAdmissionAdmitted(102 * 1024, 36 * 1024, true, true));
static_assert(readerBleEpubIdleOnlyAdmissionAdmitted(106332, 53236, true, true));
#else
static_assert(!readerBleEpubIdleOnlyAdmissionAdmitted(102 * 1024, 36 * 1024, true, true));
#endif

int idleEpubBleBudgetCompileFixture() { return 0; }
