import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOGGING_CPP = (ROOT / "lib/Logging/Logging.cpp").read_text(encoding="utf-8")
LOGGING_H = (ROOT / "lib/Logging/Logging.h").read_text(encoding="utf-8")


class LoggingSerializationContract(unittest.TestCase):
    def test_serial_proxy_has_a_linkable_singleton_and_forwarders(self):
        self.assertIn("MySerialImpl MySerialImpl::instance;", LOGGING_CPP)
        for method in ("printf", "write(const uint8_t byte)", "write(const uint8_t* buffer", "flush()"):
            self.assertIn("MySerialImpl::" + method, LOGGING_CPP)
        self.assertIn("logSerial.vprintf(format, args)", LOGGING_CPP)

    def test_complete_log_line_and_ring_update_share_a_mutex(self):
        body = LOGGING_CPP.split("void logPrintf(", 1)[1].split("std::string getLastLogs()", 1)[0]
        acquire = body.index("xSemaphoreTake(mutex, portMAX_DELAY)")
        release = body.index("xSemaphoreGive(mutex)")
        self.assertLess(acquire, body.index("logSerial.print(buf)"))
        self.assertLess(acquire, body.index("esp_rom_printf(\"%s\", buf)"))
        self.assertLess(body.index("addToLogRingBuffer(buf)"), release)

    def test_logger_mutex_is_created_in_serial_begin_before_output(self):
        begin = LOGGING_CPP.split("void MySerialImpl::begin(", 1)[1].split("void logPrintf(", 1)[0]
        self.assertLess(begin.index("xSemaphoreCreateMutex()"), begin.index("logSerial.begin(baud)"))
        self.assertIn("void begin(unsigned long baud);", LOGGING_H)

    def test_truncated_records_still_end_their_serial_line(self):
        body = LOGGING_CPP.split("void logPrintf(", 1)[1].split("std::string getLastLogs()", 1)[0]
        self.assertIn("const size_t remaining = sizeof(buf) - static_cast<size_t>(c - buf)", body)
        self.assertIn("static_cast<size_t>(len) >= remaining", body)
        self.assertIn("buf[sizeof(buf) - 2] = '\\n'", body)
        self.assertIn("buf[sizeof(buf) - 1] = '\\0'", body)


if __name__ == "__main__":
    unittest.main()
