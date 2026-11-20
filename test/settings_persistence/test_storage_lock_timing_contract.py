import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HAL_STORAGE = (ROOT / "lib/hal/HalStorage.cpp").read_text(encoding="utf-8")


class StorageLockTimingContract(unittest.TestCase):
    def test_diagnostics_separate_wait_from_locked_operation_time(self):
        self.assertIn("waitUs_ = acquiredAtUs_ - waitStartedUs", HAL_STORAGE)
        self.assertIn("heldUs = micros() - acquiredAtUs_", HAL_STORAGE)
        self.assertIn("mutex_wait_us=%u lock_hold_us=%u", HAL_STORAGE)
        self.assertIn("operation=", HAL_STORAGE)

    def test_diagnostics_are_rate_limited_and_do_not_change_lock_order(self):
        self.assertIn("kMaxSlowStorageDiagnosticRecords = 96", HAL_STORAGE)
        self.assertLess(HAL_STORAGE.index("xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex)"),
                        HAL_STORAGE.index('LOG_INF("SDLOCK"'))
        self.assertIn("portMAX_DELAY", HAL_STORAGE)


if __name__ == "__main__":
    unittest.main()
