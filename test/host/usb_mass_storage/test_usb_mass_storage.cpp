#include <UsbMassStorage.h>
#include <USB.h>
#include <USBMSC.h>
#include <test_state.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

USBClass USB;

extern "C" bool tud_mounted(void) { return usb_test::mounted; }
extern "C" bool tud_suspended(void) { return usb_test::suspended; }
extern "C" bool tud_disconnect(void) {
  ++usb_test::disconnectCalls;
  if (usb_test::disconnectResult) usb_test::mounted = false;
  return usb_test::disconnectResult;
}

class FakeBlockDevice final : public FsBlockDeviceInterface {
 public:
  explicit FakeBlockDevice(const uint32_t sectors) : sectors_(sectors), bytes_(sectors * 512u) {}
  Sector_t sectorCount() const override { return sectors_; }

  bool readSectors(const Sector_t sector, uint8_t* dst, const size_t count) override {
    maxReadSectors = std::max(maxReadSectors, count);
    if (failRead || !dst || sector + count > sectors_) return false;
    std::copy_n(bytes_.data() + static_cast<size_t>(sector) * 512u, count * 512u, dst);
    return true;
  }
  bool writeSectors(const Sector_t sector, const uint8_t* src, const size_t count) override {
    maxWriteSectors = std::max(maxWriteSectors, count);
    if (failWrite || !src || sector + count > sectors_) return false;
    std::copy_n(src, count * 512u, bytes_.data() + static_cast<size_t>(sector) * 512u);
    return true;
  }
  bool readSector(const Sector_t sector, uint8_t* dst) override {
    ++singleReads;
    if (failRead || !dst || sector >= sectors_) return false;
    std::copy_n(bytes_.data() + static_cast<size_t>(sector) * 512u, 512u, dst);
    return true;
  }
  bool writeSector(const Sector_t sector, const uint8_t* src) override {
    ++singleWrites;
    if (failWrite || !src || sector >= sectors_) return false;
    std::copy_n(src, 512u, bytes_.data() + static_cast<size_t>(sector) * 512u);
    return true;
  }

  uint8_t* data() { return bytes_.data(); }
  bool failRead = false;
  bool failWrite = false;
  size_t maxReadSectors = 0;
  size_t maxWriteSectors = 0;
  unsigned singleReads = 0;
  unsigned singleWrites = 0;

 private:
  uint32_t sectors_;
  std::vector<uint8_t> bytes_;
};

static void resetUsbTestState() {
  usb_test::mounted = false;
  usb_test::suspended = false;
  usb_test::disconnectResult = true;
  usb_test::mscBeginResult = true;
  usb_test::usbBeginResult = true;
  usb_test::watchdogSubscribed = false;
  usb_test::watchdogResets = 0;
  usb_test::yields = 0;
  usb_test::delays = 0;
  usb_test::disconnectCalls = 0;
  usb_test::usbBeginCalls = 0;
  usb_test::mscBeginCalls = 0;
  usb_test::mscEndCalls = 0;
  usb_test::lastBlockCount = 0;
  usb_test::lastBlockSize = 0;
}

int main() {
  resetUsbTestState();

  FakeBlockDevice invalid(0);
  freeink::UsbMassStorage unused;
  assert(!unused.begin(nullptr));
  assert(!unused.begin(&invalid));
  assert(unused.state() == freeink::UsbMassStorageState::Idle);

  FakeBlockDevice card(128);
  for (size_t i = 0; i < 128u * 512u; ++i) card.data()[i] = static_cast<uint8_t>(i * 17u);
  freeink::UsbMassStorage msc;
  assert(msc.begin(&card));
  assert(usb_test::lastBlockCount == 128 && usb_test::lastBlockSize == 512);
  assert(msc.state() == freeink::UsbMassStorageState::WaitingForHost);

  usb_test::mounted = true;
  assert(msc.state() == freeink::UsbMassStorageState::Connected);
  assert(msc.hostConnected());

  usb_test::watchdogSubscribed = true;
  std::vector<uint8_t> readBuffer(50u * 512u);
  auto* driver = USBMSC::current;
  assert(driver && driver->readCallback);
  assert(driver->readCallback(3, 0, readBuffer.data(), static_cast<uint32_t>(readBuffer.size())) ==
         static_cast<int32_t>(readBuffer.size()));
  assert(std::equal(readBuffer.begin(), readBuffer.end(), card.data() + 3u * 512u));
  assert(card.maxReadSectors <= 16 && card.maxReadSectors > 0);
  assert(usb_test::watchdogResets >= 4 && usb_test::yields >= 4);
  assert(msc.state() == freeink::UsbMassStorageState::Accessed);

  std::fill(readBuffer.begin(), readBuffer.end(), 0xA5);
  assert(driver->writeCallback(20, 0, readBuffer.data(), static_cast<uint32_t>(readBuffer.size())) ==
         static_cast<int32_t>(readBuffer.size()));
  assert(card.maxWriteSectors <= 16 && card.maxWriteSectors > 0);
  assert(std::equal(readBuffer.begin(), readBuffer.end(), card.data() + 20u * 512u));

  uint8_t partial[31]{};
  const size_t partialStart = 5u * 512u + 7u;
  assert(driver->readCallback(5, 7, partial, sizeof(partial)) == static_cast<int32_t>(sizeof(partial)));
  assert(std::equal(std::begin(partial), std::end(partial), card.data() + partialStart));

  uint8_t partialWrite[31];
  std::fill(std::begin(partialWrite), std::end(partialWrite), 0x3C);
  assert(driver->writeCallback(5, 7, partialWrite, sizeof(partialWrite)) == static_cast<int32_t>(sizeof(partialWrite)));
  assert(std::equal(std::begin(partialWrite), std::end(partialWrite), card.data() + partialStart));

  // A non-subscribed transfer yields without altering watchdog configuration.
  usb_test::watchdogSubscribed = false;
  const unsigned resetsBefore = usb_test::watchdogResets;
  assert(driver->readCallback(0, 0, readBuffer.data(), 512) == 512);
  assert(usb_test::watchdogResets == resetsBefore);

  assert(driver->startStopCallback && driver->startStopCallback(0, false, true));
  assert(msc.state() == freeink::UsbMassStorageState::Ejected);
  msc.end();
  assert(msc.state() == freeink::UsbMassStorageState::Idle);
  assert(usb_test::disconnectCalls == 1 && usb_test::mscEndCalls == 1);

  resetUsbTestState();
  FakeBlockDevice failedCard(16);
  failedCard.failRead = true;
  freeink::UsbMassStorage failedMsc;
  assert(failedMsc.begin(&failedCard));
  usb_test::mounted = true;
  driver = USBMSC::current;
  uint8_t sector[512]{};
  assert(driver->readCallback(0, 0, sector, sizeof(sector)) == -1);
  assert(failedMsc.state() == freeink::UsbMassStorageState::IoError);
  assert(failedMsc.disconnectHost());
  assert(usb_test::disconnectCalls == 1);
  assert(failedMsc.state() == freeink::UsbMassStorageState::Disconnected);
  failedMsc.end();

  resetUsbTestState();
  usb_test::mscBeginResult = false;
  freeink::UsbMassStorage mscBeginFailure;
  assert(!mscBeginFailure.begin(&card));
  assert(mscBeginFailure.state() == freeink::UsbMassStorageState::Idle);
  assert(usb_test::mscEndCalls == 1);

  resetUsbTestState();
  usb_test::usbBeginResult = false;
  freeink::UsbMassStorage usbBeginFailure;
  assert(!usbBeginFailure.begin(&card));
  assert(usb_test::usbBeginCalls == 1 && usb_test::mscEndCalls == 1);

  std::puts("USB MSC callback, sector bounds, eject/disconnect, startup-failure, and watchdog-yield tests passed");
  return 0;
}