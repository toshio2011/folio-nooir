import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETTINGS_CPP = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(encoding="utf-8")
SETTINGS_H = (ROOT / "src/activities/settings/SettingsActivity.h").read_text(encoding="utf-8")
DEVICE_CPP = (ROOT / "src/activities/settings/DeviceInfoActivity.cpp").read_text(encoding="utf-8")
DEVICE_H = (ROOT / "src/activities/settings/DeviceInfoActivity.h").read_text(encoding="utf-8")
STORAGE_H = (ROOT / "lib/hal/HalStorage.h").read_text(encoding="utf-8")
FONT_GATE = (ROOT / "src/InterfaceFontSelection.h").read_text(encoding="utf-8")


def function_body(source, signature, next_signature):
    return source.split(signature, 1)[1].split(next_signature, 1)[0]


class SettingsUiContractTest(unittest.TestCase):
    def test_original_tabs_and_lazy_accordion_sections_are_retained(self):
        self.assertIn("static constexpr int categoryCount = 4;", SETTINGS_H)
        self.assertIn("GUI.drawTabBar", SETTINGS_CPP)
        self.assertNotIn("groupListActive", SETTINGS_CPP)
        self.assertIn("HomeAppearance, StrId::STR_HOME_APPEARANCE", SETTINGS_CPP)
        self.assertIn("SettingsGroupId::Sleep, StrId::STR_SLEEP_SCREEN", SETTINGS_CPP)
        self.assertIn("SettingsGroupId::Connectivity, StrId::STR_CONNECTIVITY", SETTINGS_CPP)
        self.assertIn("SettingAction::DeviceInfo", SETTINGS_CPP)

    def test_accordion_transitions_do_not_scan_or_persist(self):
        toggle = function_body(SETTINGS_CPP, "void SettingsActivity::toggleAccordion()", "SettingsActivity::SettingsActivity")
        rebuild = function_body(SETTINGS_CPP, "void SettingsActivity::rebuildSettingsLists()", "void SettingsActivity::onEnter()")
        self.assertNotIn("saveToFile", toggle)
        self.assertNotIn("Storage.", toggle)
        self.assertNotIn("DictionaryRegistry::discover", rebuild)
        self.assertNotIn("refreshIfDirty", rebuild)
        self.assertNotIn("Storage.", rebuild)
        self.assertIn("DictionaryRegistry::discover(dictionaries)", SETTINGS_CPP)

    def test_visit_state_resets_only_on_settings_exit_and_custom_font_uses_builtin(self):
        enter = function_body(SETTINGS_CPP, "void SettingsActivity::onEnter()", "void SettingsActivity::onExit()")
        leave = function_body(SETTINGS_CPP, "void SettingsActivity::onExit()", "void SettingsActivity::loop()")
        self.assertIn("selectedCategoryIndex = 0", enter)
        self.assertIn("sectionExpanded = false", enter)
        self.assertIn("selectedCategoryIndex = selectedSectionIndex = selectedSettingIndex = 0", leave)
        self.assertIn("kCustomUiFontsEnabledForCurrentRelease = false", FONT_GATE)
        self.assertIn("if (setting.nameId == StrId::STR_INTERFACE_FONT) return false;", SETTINGS_CPP)

    def test_device_info_uses_one_fixed_snapshot_and_cached_capacity(self):
        enter = function_body(DEVICE_CPP, "void DeviceInfoActivity::onEnter()", "const char* DeviceInfoActivity::rowTitle")
        self.assertIn("powerManager.getBatteryPercentage()", enter)
        self.assertIn("display.getDisplayWidth()", enter)
        self.assertIn("gpio.deviceIsX3()", enter)
        self.assertIn("Storage.ready()", enter)
        self.assertIn("Storage.cardCapacityBytes()", enter)
        self.assertIn("WiFi.getMode()", enter)
        self.assertIn("ESP.getFreeHeap()", enter)
        self.assertIn("ESP.getMaxAllocHeap()", enter)
        self.assertIn("NOOIR_BUILD_SHA", DEVICE_CPP)
        self.assertIn("std::array<Row, 13>", DEVICE_H)
        self.assertIn("uint64_t cardCapacityBytes() const", STORAGE_H)
        self.assertNotIn("freeBytes", DEVICE_CPP)
        self.assertNotIn("totalBytes()", DEVICE_CPP)
        self.assertIn("buildShaLineCount", DEVICE_CPP)
        self.assertIn("const size_t split = shaLength / 2", DEVICE_CPP)
        loop = DEVICE_CPP[DEVICE_CPP.index("void DeviceInfoActivity::loop()"):
                          DEVICE_CPP.index("void DeviceInfoActivity::render")]
        for getter in ("powerManager.getBatteryPercentage", "Storage.ready", "cardCapacityBytes", "WiFi.getMode",
                       "BleHid.isConnected", "ESP.getFreeHeap", "millis()"):
            self.assertNotIn(getter, loop)


if __name__ == "__main__":
    unittest.main()
