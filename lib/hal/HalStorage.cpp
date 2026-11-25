#include "HalStorage.h"

#include <Arduino.h>
#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SDCardManager.h>

#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
#include <esp32-hal-spi.h>
#include "NooirSdSpiDiagnostics.h"
#endif

#include <cassert>
#include <cstdint>

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

namespace {
// Emit only a bounded set of slow SD lock records per boot. This separates
// time waiting behind another SD user from time held inside the filesystem or
// card operation without changing storage ordering or retry behavior.
uint16_t slowStorageDiagnosticRecords = 0;
constexpr uint16_t kMaxSlowStorageDiagnosticRecords = 96;
constexpr uint32_t kStorageLockWaitLogThresholdUs = 5000;
constexpr uint32_t kStorageLockHoldLogThresholdUs = 100000;
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
constexpr uint16_t kMaxSdSpiDiagnosticRecords = 96;
uint16_t sdSpiDiagnosticRecords = 0;
TaskHandle_t activeStorageTraceTask = nullptr;
uint32_t activeStorageTraceDepth = 0;
uint32_t activeStorageSpiCalls = 0;
uint32_t activeStorageSpiReceiveCalls = 0;
uint64_t activeStorageSpiReceiveBytes = 0;
uint64_t activeStorageSpiReceiveUs = 0;
uint32_t activeStorageSpiSendCalls = 0;
uint64_t activeStorageSpiSendBytes = 0;
uint64_t activeStorageSpiSendUs = 0;
uint32_t activeStorageCardBusyWaitCalls = 0;
uint64_t activeStorageCardBusyWaitUs = 0;
uint32_t activeStorageReadSectorsCalls = 0;
uint64_t activeStorageReadSectors = 0;
uint64_t activeStorageReadSectorsUs = 0;
uint32_t activeStorageWriteSectorsCalls = 0;
uint64_t activeStorageWriteSectors = 0;
uint64_t activeStorageWriteSectorsUs = 0;
uint32_t activeStorageFatAllocationSearchCalls = 0;
uint64_t activeStorageFatClustersScanned = 0;
uint64_t activeStorageFatAllocationSearchUs = 0;

uint32_t diagnosticPathFingerprint(const char* path) {
  if (!path) return 0;
  uint32_t hash = 2166136261u;
  while (*path) {
    hash ^= static_cast<uint8_t>(*path++);
    hash *= 16777619u;
  }
  return hash == 0 ? 1u : hash;
}
#endif
}  // namespace

#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
extern "C" void nooirSdSpiTimingRecord(const uint8_t kind, const uint64_t elapsedUs, const uint32_t units) {
  if (activeStorageTraceDepth == 0 || activeStorageTraceTask != xTaskGetCurrentTaskHandle()) return;
  switch (kind) {
    case kNooirSdCardBusyWait:
      ++activeStorageCardBusyWaitCalls;
      activeStorageCardBusyWaitUs += elapsedUs;
      break;
    case kNooirSdReadSectors:
      ++activeStorageReadSectorsCalls;
      activeStorageReadSectors += units;
      activeStorageReadSectorsUs += elapsedUs;
      break;
    case kNooirSdWriteSectors:
      ++activeStorageWriteSectorsCalls;
      activeStorageWriteSectors += units;
      activeStorageWriteSectorsUs += elapsedUs;
      break;
    case kNooirSdSpiReceiveBuffer:
      ++activeStorageSpiCalls;
      ++activeStorageSpiReceiveCalls;
      activeStorageSpiReceiveBytes += units;
      activeStorageSpiReceiveUs += elapsedUs;
      break;
    case kNooirSdSpiSendBuffer:
      ++activeStorageSpiCalls;
      ++activeStorageSpiSendCalls;
      activeStorageSpiSendBytes += units;
      activeStorageSpiSendUs += elapsedUs;
      break;
    case kNooirSdFatAllocationSearch:
      ++activeStorageFatAllocationSearchCalls;
      activeStorageFatClustersScanned += units;
      activeStorageFatAllocationSearchUs += elapsedUs;
      break;
  }
}
#endif

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

uint64_t HalStorage::cardCapacityBytes() const { return SDCard.ready() ? SDCard.sdTotalBytes() : 0; }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  explicit StorageLock(const char* operation, const size_t requestedBytes = 0, const uint32_t pathHash = 0)
      : operation_(operation ? operation : "unknown"), requestedBytes_(requestedBytes)
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
        , pathHash_(pathHash)
#endif
  {
#if !defined(NOOIR_SD_SPI_DIAGNOSTICS)
    (void)pathHash;
#endif
    const uint32_t waitStartedUs = micros();
    xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY);
    acquiredAtUs_ = micros();
    waitUs_ = acquiredAtUs_ - waitStartedUs;
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    if (activeStorageTraceDepth == 0) {
      outermost_ = true;
      activeStorageTraceTask = xTaskGetCurrentTaskHandle();
      activeStorageSpiCalls = 0;
      activeStorageSpiReceiveCalls = 0;
      activeStorageSpiReceiveBytes = 0;
      activeStorageSpiReceiveUs = 0;
      activeStorageSpiSendCalls = 0;
      activeStorageSpiSendBytes = 0;
      activeStorageSpiSendUs = 0;
      activeStorageCardBusyWaitCalls = 0;
      activeStorageCardBusyWaitUs = 0;
      activeStorageReadSectorsCalls = 0;
      activeStorageReadSectors = 0;
      activeStorageReadSectorsUs = 0;
      activeStorageWriteSectorsCalls = 0;
      activeStorageWriteSectors = 0;
      activeStorageWriteSectorsUs = 0;
      activeStorageFatAllocationSearchCalls = 0;
      activeStorageFatClustersScanned = 0;
      activeStorageFatAllocationSearchUs = 0;
    }
    ++activeStorageTraceDepth;
#endif
  }

  ~StorageLock() {
    const uint32_t heldUs = micros() - acquiredAtUs_;
    const bool slow = waitUs_ >= kStorageLockWaitLogThresholdUs || heldUs >= kStorageLockHoldLogThresholdUs;
    const bool emit = slow && slowStorageDiagnosticRecords < kMaxSlowStorageDiagnosticRecords;
    if (emit) ++slowStorageDiagnosticRecords;
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    uint32_t spiCalls = 0;
    uint32_t spiReceiveCalls = 0;
    uint64_t spiReceiveBytes = 0;
    uint64_t spiReceiveUs = 0;
    uint32_t spiSendCalls = 0;
    uint64_t spiSendBytes = 0;
    uint64_t spiSendUs = 0;
    uint32_t cardBusyWaitCalls = 0;
    uint64_t cardBusyWaitUs = 0;
    uint32_t readSectorsCalls = 0;
    uint64_t readSectors = 0;
    uint64_t readSectorsUs = 0;
    uint32_t writeSectorsCalls = 0;
    uint64_t writeSectors = 0;
    uint64_t writeSectorsUs = 0;
    uint32_t fatAllocationSearchCalls = 0;
    uint64_t fatClustersScanned = 0;
    uint64_t fatAllocationSearchUs = 0;
    bool traceComplete = false;
    if (activeStorageTraceDepth != 0) --activeStorageTraceDepth;
    if (outermost_) {
      traceComplete = true;
      spiCalls = activeStorageSpiCalls;
      spiReceiveCalls = activeStorageSpiReceiveCalls;
      spiReceiveBytes = activeStorageSpiReceiveBytes;
      spiReceiveUs = activeStorageSpiReceiveUs;
      spiSendCalls = activeStorageSpiSendCalls;
      spiSendBytes = activeStorageSpiSendBytes;
      spiSendUs = activeStorageSpiSendUs;
      cardBusyWaitCalls = activeStorageCardBusyWaitCalls;
      cardBusyWaitUs = activeStorageCardBusyWaitUs;
      readSectorsCalls = activeStorageReadSectorsCalls;
      readSectors = activeStorageReadSectors;
      readSectorsUs = activeStorageReadSectorsUs;
      writeSectorsCalls = activeStorageWriteSectorsCalls;
      writeSectors = activeStorageWriteSectors;
      writeSectorsUs = activeStorageWriteSectorsUs;
      fatAllocationSearchCalls = activeStorageFatAllocationSearchCalls;
      fatClustersScanned = activeStorageFatClustersScanned;
      fatAllocationSearchUs = activeStorageFatAllocationSearchUs;
      activeStorageTraceTask = nullptr;
      activeStorageTraceDepth = 0;
    }
#endif
    xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex);
    if (emit) {
      LOG_INF("SDLOCK", "operation=%s requested_bytes=%u mutex_wait_us=%u lock_hold_us=%u", operation_,
              static_cast<unsigned>(requestedBytes_), static_cast<unsigned>(waitUs_), static_cast<unsigned>(heldUs));
    }
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    if (traceComplete && slow && sdSpiDiagnosticRecords < kMaxSdSpiDiagnosticRecords) {
      ++sdSpiDiagnosticRecords;
      const TaskHandle_t traceTask = xTaskGetCurrentTaskHandle();
      const char* const traceTaskName = pcTaskGetName(traceTask);
      // FreeRTOS reports the task's minimum-ever remaining stack in bytes on
      // ESP-IDF. Sampling after the card/filesystem call captures any deeper
      // transient frame even after that call has returned.
      const UBaseType_t traceTaskStackHighWaterBytes = uxTaskGetStackHighWaterMark(traceTask);
      const uint64_t spiBufferUs = spiReceiveUs + spiSendUs;
      const uint32_t outsideSpiBufferUs = heldUs > spiBufferUs ? heldUs - static_cast<uint32_t>(spiBufferUs) : 0;
      // Keep each diagnostic record below Logging.cpp's 256-byte formatted
      // line limit. A single longer record was truncated before its newline,
      // merging the next serial record and hiding the card/sector timings.
      // The compact read/write tuples are calls/sectors/microseconds.
      LOG_INF("SDTRACE", "op=%s task=%s task_stack_hwm_bytes=%u held_us=%u timer_probe_calls=%u timer_probe_overhead_included=1 outside_spi_buffer_us=%u",
              operation_, traceTaskName ? traceTaskName : "unknown",
              static_cast<unsigned>(traceTaskStackHighWaterBytes), static_cast<unsigned>(heldUs),
              static_cast<unsigned>(spiCalls * 2), static_cast<unsigned>(outsideSpiBufferUs));
      LOG_INF("SDTRACE", "op=%s rx_calls=%u rx_bytes=%llu rx_us=%llu tx_calls=%u tx_bytes=%llu tx_us=%llu",
              operation_, static_cast<unsigned>(spiReceiveCalls), static_cast<unsigned long long>(spiReceiveBytes),
              static_cast<unsigned long long>(spiReceiveUs), static_cast<unsigned>(spiSendCalls),
              static_cast<unsigned long long>(spiSendBytes), static_cast<unsigned long long>(spiSendUs));
      LOG_INF("SDTRACE", "op=%s busy_wait_calls=%u busy_wait_us=%llu read=%u/%llu/%llu write=%u/%llu/%llu",
              operation_, static_cast<unsigned>(cardBusyWaitCalls),
              static_cast<unsigned long long>(cardBusyWaitUs), static_cast<unsigned>(readSectorsCalls),
              static_cast<unsigned long long>(readSectors), static_cast<unsigned long long>(readSectorsUs),
              static_cast<unsigned>(writeSectorsCalls), static_cast<unsigned long long>(writeSectors),
              static_cast<unsigned long long>(writeSectorsUs));
      if (pathHash_ != 0 || requestedBytes_ != 0 || hasOutcome_ || hasLimit_) {
        LOG_INF("SDTRACE", "op=%s path_hash=%08x requested_bytes=%u limit_bytes=%u result_bytes=%u result=%u",
                operation_, static_cast<unsigned>(pathHash_), static_cast<unsigned>(requestedBytes_),
                static_cast<unsigned>(limitBytes_), static_cast<unsigned>(outcomeBytes_),
                static_cast<unsigned>(outcomeCode_));
      }
      if (fatAllocationSearchCalls != 0) {
        LOG_INF("SDTRACE", "op=%s fat_alloc_calls=%u clusters_scanned=%llu fat_scan_us=%llu",
                operation_, static_cast<unsigned>(fatAllocationSearchCalls),
                static_cast<unsigned long long>(fatClustersScanned),
                static_cast<unsigned long long>(fatAllocationSearchUs));
      }
    }
#endif
  }

  void setPathHash(const uint32_t pathHash) {
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    pathHash_ = pathHash;
#else
    (void)pathHash;
#endif
  }

  void setLimitBytes(const size_t limitBytes) {
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    limitBytes_ = static_cast<uint32_t>(limitBytes);
    hasLimit_ = true;
#else
    (void)limitBytes;
#endif
  }

  void setOutcome(const size_t resultBytes, const uint8_t resultCode) {
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    outcomeBytes_ = static_cast<uint32_t>(resultBytes);
    outcomeCode_ = resultCode;
    hasOutcome_ = true;
#else
    (void)resultBytes;
    (void)resultCode;
#endif
  }

 private:
  const char* operation_;
  size_t requestedBytes_ = 0;
  uint32_t acquiredAtUs_ = 0;
  uint32_t waitUs_ = 0;
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
  uint32_t pathHash_ = 0;
  uint32_t limitBytes_ = 0;
  uint32_t outcomeBytes_ = 0;
  uint8_t outcomeCode_ = 0;
  bool hasLimit_ = false;
  bool hasOutcome_ = false;
  bool outermost_ = false;
#endif
};

// These methods instantiate StorageLock, so keep them after its complete definition.
bool HalStorage::beginUsbDrive() {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
  StorageLock lock("usb_begin");
  if (!SDCard.ready()) {
    LOG_ERR("USB", "USB Drive requires a mounted SD filesystem");
    return false;
  }
  if (!SDCard.syncFilesystem()) {
    LOG_ERR("USB", "Unable to flush SD filesystem before USB Drive ownership transfer");
    return false;
  }

  auto* const blockDevice = SDCard.detachFilesystemForRawAccess();
  if (!blockDevice) {
    LOG_ERR("USB", "Unable to detach SD filesystem for raw USB Drive access");
    return false;
  }

  if (!usbMassStorage.begin(blockDevice)) {
    LOG_ERR("USB", "USB Drive MSC initialization failed; restoring SD filesystem");
    if (!SDCard.begin()) {
      LOG_ERR("USB", "Unable to remount SD card after USB Drive startup failure");
    }
    return false;
  }
  LOG_INF("USB", "SD filesystem flushed and detached; USB MSC owns the card");
  return true;
#else
  return false;
#endif
}

bool HalStorage::disconnectUsbDriveHost() {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
  StorageLock lock("usb_disconnect");
  return usbMassStorage.disconnectHost();
#else
  return false;
#endif
}

void HalStorage::endUsbDrive() {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
  StorageLock lock("usb_end");
  // The host may have changed FAT metadata. Never remount here; the USB activity
  // restarts the device so all app indexes and fonts are rebuilt from disk.
  usbMassStorage.end();
#endif
}

UsbDriveState HalStorage::usbDriveState() const {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
  StorageLock lock("usb_state");
  switch (usbMassStorage.state()) {
    case freeink::UsbMassStorageState::WaitingForHost:
      return UsbDriveState::WaitingForHost;
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      return UsbDriveState::Connected;
    case freeink::UsbMassStorageState::Ejected:
      return UsbDriveState::Ejected;
    case freeink::UsbMassStorageState::Disconnected:
      return UsbDriveState::Disconnected;
    case freeink::UsbMassStorageState::IoError:
      return UsbDriveState::IoError;
    case freeink::UsbMassStorageState::Idle:
      break;
  }
#endif
  return UsbDriveState::Unsupported;
}

bool HalStorage::usbDriveHostSuspended() const {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
  StorageLock lock("usb_suspend_state");
  return usbMassStorage.hostSuspended();
#else
  return false;
#endif
}

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock("HalStorage::" #method); \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

HalStorageReadStatus HalStorage::readFileChecked(const char* path, String& content, const size_t maxBytes) {
  StorageLock lock("HalStorage::readFileChecked", 0,
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                   diagnosticPathFingerprint(path)
#else
                   0
#endif
  );
  lock.setLimitBytes(maxBytes);
#if defined(NOOIR_S3_SDK_COMPAT) && NOOIR_S3_SDK_COMPAT
  // The pinned S3 FreeInk SDK predates SDCardManager::readFileChecked(). Keep
  // the v1.6.6 checked-read contract here while retaining v1.6.7 lock telemetry.
  const auto finish = [&](const HalStorageReadStatus status) {
    lock.setOutcome(content.length(), static_cast<uint8_t>(status));
    return status;
  };
  content = "";
  if (!SDCard.ready()) return finish(HalStorageReadStatus::Unavailable);
  if (!path || !path[0]) return finish(HalStorageReadStatus::IoError);

  FsFile file = SDCard.open(path, O_RDONLY);
  if (file) {
    const uint64_t expected = file.fileSize();
    if (expected > maxBytes) {
      file.close();
      return finish(HalStorageReadStatus::TooLarge);
    }
    if (!content.reserve(static_cast<unsigned int>(expected))) {
      file.close();
      return finish(HalStorageReadStatus::IoError);
    }

    uint8_t buffer[256];
    uint64_t total = 0;
    while (total < expected) {
      const size_t wanted = static_cast<size_t>(std::min<uint64_t>(sizeof(buffer), expected - total));
      const int count = file.read(buffer, wanted);
      if (count <= 0 || static_cast<size_t>(count) > wanted ||
          !content.concat(reinterpret_cast<const char*>(buffer), static_cast<unsigned int>(count))) {
        file.close();
        content = "";
        return finish(HalStorageReadStatus::IoError);
      }
      total += static_cast<size_t>(count);
    }
    const bool readError = file.getError() != 0 || total != expected || file.fileSize() != expected;
    file.close();
    if (readError) {
      content = "";
      return finish(HalStorageReadStatus::IoError);
    }
    return finish(HalStorageReadStatus::Success);
  }

  // Confirm a clean miss by scanning the parent. A failed directory scan is
  // conservatively an I/O error so persistence recovery never treats a card
  // fault as an absent settings file.
  const char* finalSlash = strrchr(path, '/');
  const char* baseName = finalSlash ? finalSlash + 1 : path;
  if (!baseName[0]) return finish(HalStorageReadStatus::IoError);
  String parentPath = finalSlash ? String(path).substring(0, finalSlash - path) : String(".");
  if (parentPath.isEmpty()) parentPath = "/";

  auto scanDirectory = [](const char* directoryPath, const char* wantedName, bool& complete) {
    complete = false;
    FsFile directory = SDCard.open(directoryPath, O_RDONLY);
    if (!directory || !directory.isDirectory()) return false;
    bool found = false;
    char name[256];
    while (true) {
      FsFile entry = directory.openNextFile();
      if (!entry) break;
      name[0] = '\0';
      entry.getName(name, sizeof(name));
      entry.close();
      if (strcmp(name, wantedName) == 0) {
        found = true;
        break;
      }
    }
    complete = found || directory.getError() == 0;
    directory.close();
    return found;
  };

  bool parentScanComplete = false;
  if (scanDirectory(parentPath.c_str(), baseName, parentScanComplete))
    return finish(HalStorageReadStatus::IoError);
  if (parentScanComplete) return finish(HalStorageReadStatus::NotFound);

  if (parentPath == "/.crosspoint") {
    bool rootScanComplete = false;
    if (scanDirectory("/", ".crosspoint", rootScanComplete)) return finish(HalStorageReadStatus::IoError);
    if (rootScanComplete) return finish(HalStorageReadStatus::ParentMissing);
  }
  return finish(HalStorageReadStatus::IoError);
#else
  const auto status = SDCard.readFileChecked(path, content, maxBytes);
  lock.setOutcome(content.length(), static_cast<uint8_t>(status));
  switch (status) {
    case SDCardManager::ReadStatus::Success:
      return HalStorageReadStatus::Success;
    case SDCardManager::ReadStatus::NotFound:
      return HalStorageReadStatus::NotFound;
    case SDCardManager::ReadStatus::ParentMissing:
      return HalStorageReadStatus::ParentMissing;
    case SDCardManager::ReadStatus::Unavailable:
      return HalStorageReadStatus::Unavailable;
    case SDCardManager::ReadStatus::TooLarge:
      return HalStorageReadStatus::TooLarge;
    case SDCardManager::ReadStatus::IoError:
    default:
      return HalStorageReadStatus::IoError;
  }
#endif
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  StorageLock lock("HalStorage::writeFile", content.length(),
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                   diagnosticPathFingerprint(path)
#else
                   0
#endif
  );
  const bool written = SDCard.writeFile(path, content);
  lock.setOutcome(written ? content.length() : 0, written ? 1 : 0);
  return written;
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile, const uint32_t pathHash = 0) : file(std::move(fsFile)) {
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    pathHash_ = pathHash;
#else
    (void)pathHash;
#endif
  }
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518 and the HAL note in CLAUDE.md.
  ~Impl() {
    HalStorage::StorageLock lock("HalFile::Impl::~Impl");
    lock.setPathHash(pathFingerprint());
    file.close();
  }

  uint32_t pathFingerprint() const {
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    return pathHash_;
#else
    return 0;
#endif
  }

  void setPathFingerprint(const uint32_t pathHash) {
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
    pathHash_ = pathHash;
#else
    (void)pathHash;
#endif
  }

  FsFile file;
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
  uint32_t pathHash_ = 0;
#endif
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock("HalStorage::open", 0,
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                   diagnosticPathFingerprint(path)
#else
                   0
#endif
  );  // ensure thread safety for the duration of this function
  return HalFile(std::make_unique<HalFile::Impl>(SDCard.open(path, oflag),
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                                                 diagnosticPathFingerprint(path)
#else
                                                 0
#endif
                                                 ));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock("HalStorage::openFileForRead", 0,
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                   diagnosticPathFingerprint(path)
#else
                   0
#endif
  );  // ensure thread safety for the duration of this function
  FsFile fsFile;
  const bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile),
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                                                  diagnosticPathFingerprint(path)
#else
                                                  0
#endif
                                                  ));
  lock.setOutcome(0, ok ? 1 : 0);
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock("HalStorage::openFileForWrite", 0,
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                   diagnosticPathFingerprint(path)
#else
                   0
#endif
  );  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile),
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
                                                  diagnosticPathFingerprint(path)
#else
                                                  0
#endif
                                                  ));
  lock.setOutcome(0, ok ? 1 : 0);
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock("HalFile::" #method); \
  assert(impl != nullptr);                 \
  lock.setPathHash(impl->pathFingerprint()); \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
bool HalFile::getModifyDateTime(uint16_t* date, uint16_t* time) {
  HAL_FILE_WRAPPED_CALL(getModifyDateTime, date, time);
}
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, const size_t count) {
  HalStorage::StorageLock lock("HalFile::read", count);
  assert(impl != nullptr);
  lock.setPathHash(impl->pathFingerprint());
  const int result = impl->file.read(buf, count);
  lock.setOutcome(result > 0 ? static_cast<size_t>(result) : 0, result > 0 ? 1 : 0);
  return result;
}
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, const size_t count) {
  HalStorage::StorageLock lock("HalFile::write", count);
  assert(impl != nullptr);
  lock.setPathHash(impl->pathFingerprint());
  const size_t written = impl->file.write(buf, count);
  lock.setOutcome(written, written == count ? 1 : 0);
  return written;
}
size_t HalFile::write(const uint8_t b) {
  HalStorage::StorageLock lock("HalFile::write_byte", 1);
  assert(impl != nullptr);
  lock.setPathHash(impl->pathFingerprint());
  const size_t written = impl->file.write(b);
  lock.setOutcome(written, written == 1 ? 1 : 0);
  return written;
}
bool HalFile::rename(const char* newPath) {
  HalStorage::StorageLock lock("HalFile::rename");
  assert(impl != nullptr);
  lock.setPathHash(impl->pathFingerprint());
  const bool renamed = impl->file.rename(newPath);
  if (renamed) {
    impl->setPathFingerprint(
#if defined(NOOIR_SD_SPI_DIAGNOSTICS)
        diagnosticPathFingerprint(newPath)
#else
        0
#endif
    );
  }
  lock.setOutcome(0, renamed ? 1 : 0);
  return renamed;
}
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock("HalFile::openNextFile");
  assert(impl != nullptr);
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
