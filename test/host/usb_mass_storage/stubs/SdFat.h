#pragma once
#include <cstddef>
#include <cstdint>
using Sector_t = uint32_t;
class FsBlockDeviceInterface {
 public:
  virtual ~FsBlockDeviceInterface() = default;
  virtual Sector_t sectorCount() const = 0;
  virtual bool readSectors(Sector_t sector, uint8_t* dst, size_t count) = 0;
  virtual bool writeSectors(Sector_t sector, const uint8_t* src, size_t count) = 0;
  virtual bool readSector(Sector_t sector, uint8_t* dst) = 0;
  virtual bool writeSector(Sector_t sector, const uint8_t* src) = 0;
};