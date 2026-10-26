"""Contracts for the shared 1.6.5 Bluetooth preference and mapped input path."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
BLE_SOURCE = (REPO_ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
BLE_SETTINGS = (REPO_ROOT / "src/activities/settings/BluetoothSettingsActivity.cpp").read_text(
    encoding="utf-8"
)
ACTIVITY_MANAGER = (REPO_ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
MAIN = (REPO_ROOT / "src/main.cpp").read_text(encoding="utf-8")
XTC_READER = (REPO_ROOT / "src/activities/reader/XtcReaderActivity.cpp").read_text(encoding="utf-8")
READER_SHELL = (REPO_ROOT / "src/activities/reader/ReaderActivity.cpp").read_text(encoding="utf-8")
ACTIVITY = (REPO_ROOT / "src/activities/Activity.h").read_text(encoding="utf-8")
EPUB_READER = (REPO_ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
EPUB_MENU = (REPO_ROOT / "src/activities/reader/EpubReaderMenuActivity.cpp").read_text(encoding="utf-8")
MAPPED_INPUT = (REPO_ROOT / "src/MappedInputManager.cpp").read_text(encoding="utf-8")
BUTTON_NAVIGATOR = (REPO_ROOT / "src/util/ButtonNavigator.cpp").read_text(encoding="utf-8")
HOME_ACTIVITY = (REPO_ROOT / "src/activities/home/HomeActivity.cpp").read_text(encoding="utf-8")
BLE_MAP_ACTIVITY = (REPO_ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
READER_UTILS = (REPO_ROOT / "src/activities/reader/ReaderUtils.h").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unclosed function body: {signature}")


class ReaderBluetoothToggleContract(unittest.TestCase):
    def test_lifecycle_settings_accessor_is_outside_interrupt_critical_section(self):
        reevaluate = function_body(BLE_SOURCE, "void requestLifecycleReevaluation()")
        settings_read = reevaluate.index("SETTINGS.bluetoothEnabled")
        critical_enter = reevaluate.index("portENTER_CRITICAL(&g_noDeviceTimeoutMux)")
        self.assertLess(settings_read, critical_enter)

        critical_body = reevaluate[critical_enter:]
        first_exit = critical_body.index("portEXIT_CRITICAL(&g_noDeviceTimeoutMux)")
        self.assertNotIn("SETTINGS.", critical_body[:first_exit])

    def test_epub_and_xtc_use_one_saved_preference_and_common_toggle(self):
        self.assertNotIn("ReaderBluetoothSession", BLE_SOURCE)
        self.assertNotIn("ReaderSessionBluetooth", BLE_SOURCE)
        self.assertNotIn("beginReaderBluetoothSession", EPUB_READER + XTC_READER)
        self.assertNotIn("endReaderBluetoothSession", EPUB_READER + XTC_READER)
        self.assertIn("bleinput::toggleBluetooth()", EPUB_READER)
        self.assertIn("bleinput::toggleBluetooth()", XTC_READER)
        self.assertIn("bleinput::bluetoothEnabled()", EPUB_MENU)

    def test_home_and_both_reader_formats_share_mapped_hid_input(self):
        self.assertIn("SETTINGS.bleKeyMap", MAPPED_INPUT)
        poll = function_body(MAPPED_INPUT, "void MappedInputManager::pollBle()")
        self.assertIn("!bleinput::bluetoothEnabled()", poll)
        self.assertIn("if (BleHid.isRunning()) BleHid.poll();", MAIN)
        self.assertIn("mappedInputManager.pollBle();", MAIN)
        self.assertIn("currentActivity->acceptsBluetoothInput()", ACTIVITY_MANAGER)
        self.assertIn("virtual bool acceptsBluetoothInput() const { return true; }", ACTIVITY)

    def test_active_key_release_uses_legacy_event_identity_after_typed_learn_match(self):
        poll = function_body(MAPPED_INPUT, "void MappedInputManager::pollBle()")
        self.assertIn("matchesAt(entry, learnedKind, learnedValue, index)", poll)
        self.assertIn("bleActiveKinds[button] = legacyKind;", poll)
        self.assertIn("bleActiveValues[button] = legacyValue;", poll)
        release = poll[poll.index("const bool activeIdentityMatches") :]
        release = release[: release.index("if (!bleActiveBinding[button]")]
        self.assertIn("bleActiveKinds[button] == legacyKind", release)
        self.assertIn("bleActiveValues[button] == legacyValue", release)
        self.assertNotIn("learnedKind", release)

    def test_failed_bluetooth_activation_waits_for_explicit_retry(self):
        render = function_body(ACTIVITY_MANAGER, "void ActivityManager::renderTaskLoop()")
        failure_gate = render.index("if (!bluetoothSettingsActivationFailed_.load")
        recheck = render.index("if (bluetoothSettingsActivationFailed_.load", failure_gate)
        self.assertLess(failure_gate, render.index("bleinput::requestLifecycleReevaluation();", failure_gate))
        self.assertIn("bleinput::clearReAdmissionPending();", render[recheck:])
        self.assertIn("bleinput::takeLifecycleReevaluation();", render[recheck:])

        lifecycle = function_body(MAIN, "void updateBluetoothLifecycle()")
        failure_gate = lifecycle.index("activityManager.bluetoothSettingsActivationFailed()")
        self.assertLess(failure_gate, lifecycle.index("bleinput::takeLifecycleReevaluation();", failure_gate))
        self.assertLess(failure_gate, lifecycle.index("bleinput::connectionAdmissionAllowed();", failure_gate))
        self.assertIn("bleinput::clearReAdmissionPending();", lifecycle[failure_gate:])

        preparation = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::beginPreparation(")
        self.assertIn("activityManager.setBluetoothSettingsActivationFailed(false);", preparation)
        result = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::onBluetoothActivationResult(")
        self.assertIn("activityManager.setBluetoothSettingsActivationFailed(!started);", result)

    def test_bluetooth_off_remains_available_after_activation_failure(self):
        loop = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::loop()")
        failed = loop[loop.index("if (activationState.load(std::memory_order_acquire) == ActivationState::Failed)") :]
        self.assertIn("const int optionCount = retryAvailable ? 3 : 2;", failed)
        self.assertIn("bleinput::setBluetoothEnabled(false);", failed)
        self.assertIn("activationState.store(ActivationState::Ready", failed)

        render = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::render(")
        failed_render = render[render.index("if (state == ActivationState::Failed)") :]
        self.assertIn("tr(STR_STATE_OFF)", failed_render)

    def test_global_bluetooth_off_stops_and_persists_shared_preference(self):
        body = function_body(BLE_SOURCE, "void setBluetoothEnabled(")
        self.assertIn("SETTINGS.bluetoothEnabled = enabled ? 1 : 0;", body)
        reevaluate = body.index("requestLifecycleReevaluation();")
        stop = body.index("stop();")
        self.assertLess(body.index("SETTINGS.bluetoothEnabled = enabled"), reevaluate)
        self.assertLess(reevaluate, stop)
        self.assertIn("SETTINGS.saveToFile();", body[stop:])

    def test_reader_input_uses_saved_preference_and_off_skips_host_poll(self):
        poll = function_body(MAPPED_INPUT, "void MappedInputManager::pollBle()")
        self.assertIn("!bleinput::bluetoothEnabled()", poll)
        loop = function_body(MAIN, "void loop()")
        self.assertIn("if (BleHid.isRunning()) BleHid.poll();", loop)

    def test_directional_ble_mappings_reach_shared_reader_menus(self):
        alias = function_body(MAPPED_INPUT, "bool MappedInputManager::bleNavigationAlias(")
        self.assertIn("readerMappingMode", alias)
        self.assertIn("navigationAliasForMenu(", alias)
        self.assertIn("input->wasNavigationPressed(button)", BUTTON_NAVIGATOR)
        self.assertIn("input->wasNavigationReleased(button)", BUTTON_NAVIGATOR)
        self.assertIn("input->isNavigationPressed(button)", BUTTON_NAVIGATOR)

        # Page turns continue to consume the same physical-style BLE edges;
        # only ButtonNavigator's logical menu queries add the aliases.
        page_turn = function_body(READER_UTILS, "inline PageTurnResult detectPageTurn(")
        self.assertIn("input.wasPressed(nextButton)", page_turn)
        self.assertIn("input.wasPressed(prevButton)", page_turn)
        self.assertNotIn("wasNavigationPressed", page_turn)

    def test_legacy_page_back_and_forward_actions_remain_assignable(self):
        mapping_functions = BLE_MAP_ACTIVITY.split("const BleButtonMapActivity::Fn BleButtonMapActivity::kFunctions[] =", 1)[1].split(
            "};", 1
        )[0]
        self.assertIn(
            "MappedInputManager::Button::PageBack", mapping_functions
        )
        self.assertIn("StrId::STR_BT_PAGE_BACK", mapping_functions)
        self.assertIn("MappedInputManager::Button::PageForward", mapping_functions)
        self.assertIn("StrId::STR_BT_PAGE_FORWARD", mapping_functions)

        # One poll drains the key map before either Home or a reader consumes
        # the frame. Page actions use the shared physical-style edge queried by
        # ReaderUtils; Home uses ButtonNavigator's shared mapped-input pointer.
        main_loop = function_body(MAIN, "void loop()")
        self.assertLess(
            main_loop.index("mappedInputManager.pollBle();"),
            main_loop.index("activityManager.loop();"),
        )
        self.assertIn("buttonNavigator.onNext(", HOME_ACTIVITY)
        self.assertIn("buttonNavigator.onPrevious(", HOME_ACTIVITY)
        self.assertIn("input->wasNavigationPressed(button)", BUTTON_NAVIGATOR)
        page_turn = function_body(READER_UTILS, "inline PageTurnResult detectPageTurn(")
        self.assertIn("input.wasPressed(MappedInputManager::Button::PageBack)", page_turn)
        self.assertIn("input.wasPressed(MappedInputManager::Button::PageForward)", page_turn)
        self.assertIn("input.wasReleased(MappedInputManager::Button::PageBack)", page_turn)
        self.assertIn("input.wasReleased(MappedInputManager::Button::PageForward)", page_turn)

    def test_home_keeps_host_and_reader_resource_stop_remains(self):
        transition = function_body(ACTIVITY_MANAGER, "void ActivityManager::loop()")
        self.assertNotIn("homeTransitionNeedsBleSuspension", transition)
        self.assertIn(
            "activityTransitionNeedsBleSuspension(bleRunning, currentActivity->bluetoothResourceSensitive())",
            transition,
        )
        reader_enter = function_body(READER_SHELL, "void ReaderActivity::onEnter()")
        self.assertIn("bleinput::stopForReaderResourcePressure();", reader_enter)
        self.assertIn("if (BleHid.isRunning()) BleHid.poll();", MAIN)

    def test_bluetooth_off_remains_available_while_activation_is_pending(self):
        loop = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::loop()")
        preparing = loop[
            loop.index("if (activationState.load(std::memory_order_acquire) == ActivationState::Preparing)") :
            loop.index("if (activationState.load(std::memory_order_acquire) == ActivationState::Failed)")
        ]
        self.assertIn("handleListTouch(selectedOption, 2", preparing)
        self.assertIn("bleinput::setBluetoothEnabled(false);", preparing)

        render = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::render(")
        preparing_render = render[
            render.index("if (state == ActivationState::Preparing)") :
            render.index("if (state == ActivationState::Failed)")
        ]
        self.assertIn("GUI.drawList(renderer, listRect, 2", preparing_render)
        self.assertIn("tr(STR_STATE_OFF)", preparing_render)

    def test_failed_manual_scan_start_preserves_saved_bluetooth_preference(self):
        scan = function_body(BLE_SETTINGS, "void BluetoothSettingsActivity::startScanView()")
        failure = scan[scan.index("if (!BleHid.isRunning() && !bleinput::ensureStarted())") :]
        self.assertNotIn("SETTINGS.bluetoothEnabled = 0", failure)
        self.assertNotIn("SETTINGS.saveToFile()", failure)
        self.assertIn("setBanner(tr(STR_BT_START_FAILED));", failure)

    def test_no_device_timeout_stops_runtime_without_erasing_saved_preference(self):
        timeout = function_body(BLE_SOURCE, "void updateNoDeviceWindow(")
        self.assertIn("suppressNoDeviceRetryUntilLifecycleBoundary();", timeout)
        self.assertIn("requestLifecycleReevaluation();", timeout)
        self.assertNotIn("SETTINGS.bluetoothEnabled = 0", timeout)
        self.assertNotIn("SETTINGS.saveToFile()", timeout)
        self.assertIn("g_noDeviceTimeoutStoppedHost.store(true", BLE_SOURCE)
        self.assertIn("lifecycleReevaluationAllowed(", BLE_SOURCE)
        reevaluate = function_body(BLE_SOURCE, "void requestLifecycleReevaluation()")
        self.assertIn("portENTER_CRITICAL(&g_noDeviceTimeoutMux);", reevaluate)
        self.assertIn("portEXIT_CRITICAL(&g_noDeviceTimeoutMux);", reevaluate)
        self.assertNotIn("currentActivityName()", BLE_SOURCE[: BLE_SOURCE.index("void updateNoDeviceWindow(")])
        self.assertIn("bleinput::clearNoDeviceTimeoutSuppression();", BLE_SETTINGS)

    def test_committed_activity_changes_clear_timeout_pause_before_reentry(self):
        starts = []
        offset = 0
        while True:
            offset = ACTIVITY_MANAGER.find("currentActivity = std::move(", offset)
            if offset < 0:
                break
            starts.append(offset)
            offset += 1
        self.assertEqual(len(starts), 3)
        for start in starts:
            self.assertIn("bleinput::clearNoDeviceTimeoutSuppression();",
                          ACTIVITY_MANAGER[start : start + 220])
        self.assertIn("bleinput::requestLifecycleReevaluation();", ACTIVITY_MANAGER)


if __name__ == "__main__":
    unittest.main()
