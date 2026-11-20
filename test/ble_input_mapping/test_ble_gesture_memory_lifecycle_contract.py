from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOST = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp").read_text(encoding="utf-8")
HOST_HEADER = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/include/BleKeyboardHost.h").read_text(encoding="utf-8")
BLE_INPUT = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
PROFILE_STORE = (ROOT / "src/BleMappingProfileStore.cpp").read_text(encoding="utf-8")
PROFILE_ACTIVITY = (ROOT / "src/activities/settings/BleMappingProfilesActivity.cpp").read_text(encoding="utf-8")
PROFILE_ACTIVITY_HEADER = (ROOT / "src/activities/settings/BleMappingProfilesActivity.h").read_text(encoding="utf-8")
POLICY = (ROOT / "src/util/BleMemoryPolicy.h").read_text(encoding="utf-8")
READER_BUDGET = (ROOT / "src/util/ReaderRenderBudget.h").read_text(encoding="utf-8")
EPUB = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


class IineGestureMemoryLifecycleContract(unittest.TestCase):
    def test_gestures_are_disabled_by_default_and_require_scoped_descriptor_route(self):
        self.assertIn("#ifndef NOOIR_BLE_IINE_TOUCH_GESTURES\n#define NOOIR_BLE_IINE_TOUCH_GESTURES 0", HOST)
        body = function_body(HOST, "void BleKeyboardHost::onReportIngest(")
        route_start = body.index("const bool iineDeviceName")
        touch_route_start = body.index("const bool iineTouchReport", route_start)
        route_end = body.index("if (iineTouchReport && !genericLearningCapture_)", touch_route_start)
        route = body[route_start:route_end]
        for condition in ("characteristicUuid ==", "reportId ==", "reportType ==", "hasReportReference",
                          "isSupportedDeviceName(connName_)", "reportMapAllowsDigitizerCoordinates"):
            self.assertIn(condition, route)
        generic_start = body.index("// Generic controller learning")
        fallback_start = body.index("if (genericLearningCapture_ && iineTouchReport)")
        self.assertLess(generic_start, fallback_start)
        self.assertIn("payloadForDeclaredReport", body)
        self.assertIn("if (iineTouchReport && !genericLearningCapture_)", body)
        self.assertIn("enqueueGesturePair", body)
        self.assertNotIn("Serial.", body[touch_route_start:body.index("// Any other report interrupts", touch_route_start)])

    def test_partial_and_queued_gestures_are_cleared_without_dropping_ordinary_inputs(self):
        clear = function_body(HOST, "void BleKeyboardHost::clearPendingGestureInput()")
        self.assertIn("g_iineTouchGesture.reset()", clear)
        self.assertIn("g_genericInputDecoder.resetTouch()", clear)
        self.assertIn("g_rawBitFallback.reset()", clear)
        self.assertIn("event.lifecycleScopedGesture", clear)
        self.assertIn("event.usagePage == 0x0D", clear)
        self.assertIn("ringHead_ = write", clear)
        reset = function_body(HOST, "void BleKeyboardHost::resetInputState()")
        self.assertIn("ringHead_ = 0", reset)
        self.assertIn("g_genericInputDecoder.reset()", reset)
        self.assertIn("g_rawBitFallback.reset()", reset)
        self.assertIn("g_iineTouchGesture.reset()", reset)
        link_down = function_body(HOST, "void BleKeyboardHost::onLinkDown()")
        self.assertIn("clearHidReportBindings()", link_down)
        self.assertIn("resetInputState()", link_down)
        self.assertIn("void clearPendingGestureInput();", HOST_HEADER)
        capture_mode = function_body(HOST, "void BleKeyboardHost::setGenericLearningCapture(")
        self.assertIn("genericLearningCapture_ = enabled", capture_mode)
        self.assertIn("g_genericInputDecoder.reset()", capture_mode)
        self.assertIn("g_rawBitFallback.reset()", capture_mode)
        helper = function_body(BLE_INPUT, "void clearPendingIineGesture()")
        self.assertIn("if (SETTINGS.bluetoothEnabled)", helper)

    def test_reader_boundary_cleanup_is_bluetooth_enabled_only_and_does_not_change_memory_stages(self):
        self.assertGreaterEqual(MANAGER.count("#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_IINE_TOUCH_GESTURES"), 3)
        self.assertGreaterEqual(MANAGER.count("bleinput::bluetoothEnabled() && currentActivity"), 2)
        self.assertIn("bleinput::bluetoothEnabled() && readerActivity", MANAGER)
        self.assertIn("if (!bleinput::bluetoothEnabled() || WiFi.getMode() != WIFI_MODE_NULL) return;", EPUB)
        self.assertIn("section->isBuilding()", EPUB)
        self.assertIn("pendingPageTurns.hasPending()", EPUB)
        self.assertIn("activityManager.hasPendingRenderWork()", EPUB)
        self.assertIn("readerBleReturnCleanupAllowed(", EPUB)
        self.assertIn("readerBleDeepCacheReclaimSafe(", EPUB)
        self.assertIn("releaseSdFontCaches()", EPUB)
        for unchanged in ("kReaderBleStartMinFreeHeap = 96 * 1024",
                          "kReaderBleLogicalMinLargestBlock = 36 * 1024",
                          "kReaderBleRenderMinFreeHeap = 36 * 1024",
                          "kReaderBleRenderMinLargestBlock = 20 * 1024",
                          "kReaderBleEpubPageAwareRecoveryFreeFloor = 32 * 1024",
                          "kReaderBleEpubPageAwareRecoveryLargestFloor = 16 * 1024",
                          "kReaderBleEpubPageAwareFreeMargin = 4 * 1024",
                          "kReaderBleEpubPageAwareLargestMargin = 4 * 1024"):
            self.assertIn(unchanged, POLICY)
        self.assertIn("PressureTier::Heavy", READER_BUDGET)
        self.assertIn("kPageLoadReserve = 8 * 1024", READER_BUDGET)
        self.assertIn("survivalStopRequired", READER_BUDGET)

    def test_profile_switch_only_swaps_active_map_and_never_controls_ble_or_reader_budgets(self):
        body = function_body(PROFILE_STORE, "bool BleMappingProfileStore::switchTo(")
        self.assertIn("BleKeyMapEntry candidate[", body)
        self.assertNotIn("previous[", body)
        self.assertIn("readMap(profileId, candidate, &candidatePreset)", body)
        self.assertIn("sameMap(candidate, SETTINGS.bleKeyMap)", body)
        self.assertNotIn("writeMap(oldId, SETTINGS.bleKeyMap, SETTINGS.bleControllerPreset)", body)
        self.assertIn("BleMappingProfileStore::loadActive()", (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8"))
        self.assertIn("SETTINGS.saveToFile()", body)
        self.assertIn("BleHid.setInputPreset", body)
        for forbidden in ("bleinput::start", "bleinput::stop", "connectionAdmissionAllowed",
                          "kReaderBle", "freeHeap", "largestBlock", "requestLifecycleReevaluation"):
            self.assertNotIn(forbidden, body)
        catalog = function_body(PROFILE_STORE, "bool BleMappingProfileStore::readCatalog(")
        self.assertIn('doc["profiles"].is<JsonArrayConst>()', catalog)
        self.assertIn("array.size() > kProfileCapacity", catalog)
        self.assertIn("bleinput::profiles::ProfileRecord profiles[bleinput::profiles::kMaxProfiles]", PROFILE_ACTIVITY_HEADER)
        self.assertIn("if (action == 6)", PROFILE_ACTIVITY)
        self.assertIn("Yiser (built-in)", PROFILE_STORE)
        self.assertIn("Free2 (built-in)", PROFILE_STORE)
        self.assertIn("Free3 (built-in)", PROFILE_STORE)
        self.assertIn("set(0, freeink::SpecialKey::Up, Btn::Up)", PROFILE_STORE)
        self.assertIn("set(0, freeink::SpecialKey::PageDown, Btn::PageForward)", PROFILE_STORE)
        self.assertIn("set(2, freeink::SpecialKey::Enter, Btn::Confirm)", PROFILE_STORE)
        self.assertIn("editMappings(true, selected.name)", PROFILE_ACTIVITY)
        button_map = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        self.assertIn("viewOnly ? Step::ViewOnly", button_map)
        self.assertIn("setBleCaptureMode(!viewOnly)", button_map)
        self.assertIn("BleMappingProfileStore::createEmpty", PROFILE_ACTIVITY)
        self.assertNotIn("BleKeyMapEntry", PROFILE_ACTIVITY)
        self.assertNotIn("bleinput::start", PROFILE_ACTIVITY)
        self.assertNotIn("bleinput::stop", PROFILE_ACTIVITY)


if __name__ == "__main__":
    unittest.main()
