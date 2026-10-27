"""Keep diagnostics at the actual Home and reader BLE input consumers."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
INPUT_HEADER = (ROOT / "src/MappedInputManager.h").read_text(encoding="utf-8")
INPUT_SOURCE = (ROOT / "src/MappedInputManager.cpp").read_text(encoding="utf-8")
HOME = (ROOT / "src/activities/home/HomeActivity.cpp").read_text(encoding="utf-8")
READER_UTILS = (ROOT / "src/activities/reader/ReaderUtils.h").read_text(encoding="utf-8")
EPUB = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
XTC = (ROOT / "src/activities/reader/XtcReaderActivity.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")


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


class BleConsumerTraceContract(unittest.TestCase):
    def test_ble_origin_queries_do_not_mix_gpio_edges(self):
        for declaration in (
            "bool bleWasPressed(Button button) const;",
            "bool bleWasReleased(Button button) const;",
            "bool bleIsPressed(Button button) const;",
            "bool bleWasNavigationPressed(Button button) const;",
        ):
            self.assertIn(declaration, INPUT_HEADER)
        self.assertIn("return bleEdge(blePressEdge, button);", INPUT_SOURCE)
        self.assertIn("return bleEdge(bleReleaseEdge, button);", INPUT_SOURCE)
        self.assertIn("return bleWasPressed(button) || bleNavigationAlias(blePressEdge, button);", INPUT_SOURCE)
        semantic_api = INPUT_HEADER.split("// ButtonNavigator-only semantic queries", 1)[1].split("bool hasTouch", 1)[0]
        normal_api = semantic_api.split("#if NOOIR_BLE_DIAGNOSTICS", 1)[0]
        self.assertIn("bool wasNavigationPressed(Button button) const;", normal_api)
        self.assertIn("bool wasNavigationReleased(Button button) const;", normal_api)
        self.assertIn("bool isNavigationPressed(Button button) const;", normal_api)

    def test_home_logs_only_after_a_ble_mapped_action_reaches_consumer(self):
        self.assertIn('consumer=Home action=confirm selection=%d', HOME)
        self.assertIn('consumer=Home action=next selection=%d', HOME)
        self.assertIn('consumer=Home action=previous selection=%d', HOME)
        self.assertIn("mappedInput.bleWasPressed(MappedInputManager::Button::Confirm)", HOME)
        self.assertIn("mappedInput.bleIsNavigationPressed(MappedInputManager::Button::NavNext)", HOME)
        self.assertIn("mappedInput.bleIsNavigationPressed(MappedInputManager::Button::NavPrevious)", HOME)
        self.assertIn('consumer=Home menu=main action=next', HOME)
        self.assertIn('consumer=Home menu=book_actions action=confirm', HOME)

    def test_reader_page_diagnostic_tracks_ble_contribution_through_turn_policy(self):
        result = READER_UTILS.split("struct PageTurnResult", 1)[1].split("inline PageTurnResult detectPageTurn", 1)[0]
        self.assertNotIn("fromBle", result)
        detect = function_body(READER_UTILS, "inline PageTurnResult detectPageTurn(")
        self.assertIn("return {prev, next, tiltPrev || tiltNext, sideEdge};", detect)
        ble_source = function_body(READER_UTILS, "inline bool blePageTurnInput(")
        self.assertIn("#if NOOIR_BLE_DIAGNOSTICS", READER_UTILS)
        self.assertIn("inline bool blePageTurnInput", READER_UTILS)
        self.assertIn("input.bleWasPressed(MappedInputManager::Button::PageBack)", ble_source)
        self.assertIn("input.bleWasReleased(MappedInputManager::Button::PageForward)", ble_source)
        self.assertIn("input.bleWasReleased(MappedInputManager::Button::Power)", ble_source)

        self.assertIn('consumer=EPUB action=%s', EPUB)
        self.assertIn("ReaderUtils::blePageTurnInput(mappedInput, false)", EPUB)
        self.assertIn("(prevTriggered && fromBlePrev) || (nextTriggered && fromBleNext)", EPUB)
        self.assertIn('consumer=XTC action=%s page=%lu', XTC)
        self.assertIn("std::swap(fromBlePrev, fromBleNext);", XTC)
        self.assertIn("auto [prevTriggered, nextTriggered, fromTilt, fromSide] = ReaderUtils::detectPageTurn(mappedInput);", EPUB)
        self.assertIn("auto [prevTriggered, nextTriggered, fromTilt, fromSide] = ReaderUtils::detectPageTurn(mappedInput);", XTC)

    def test_main_drains_and_maps_remote_input_before_activity_consumption(self):
        loop = function_body(MAIN, "void loop()")
        self.assertLess(loop.index("mappedInputManager.pollBle();"), loop.index("activityManager.loop();"))
        self.assertIn("bleinput::recordDecodedKey(event", INPUT_SOURCE)


if __name__ == "__main__":
    unittest.main()
