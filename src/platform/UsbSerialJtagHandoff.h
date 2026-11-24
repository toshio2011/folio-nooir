#pragma once

// Release the ESP32-S3 USB-OTG PHY and restore hardware USB Serial/JTAG
// before a normal application restart after USB Drive mode.
void handoffUsbOtgToSerialJtag();