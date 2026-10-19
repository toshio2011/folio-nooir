import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs" / "settings-organization"
ACTIVITY_CPP = ROOT / "src" / "activities" / "settings" / "SettingsActivity.cpp"
ACTIVITY_H = ROOT / "src" / "activities" / "settings" / "SettingsActivity.h"
GROUPS_H = ROOT / "src" / "SettingsGroups.h"
FONT_SELECTION_H = ROOT / "src" / "InterfaceFontSelection.h"
SETTINGS_LIST_H = ROOT / "src" / "SettingsList.h"
HOME_CPP = ROOT / "src" / "activities" / "home" / "RecentBooksActivity.cpp"
BASELINE_165 = DOCS / "BASELINE_1.6.5_DEVICE_inventory.json"


class SettingsGroupPrototypeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = json.loads((DOCS / "BEFORE_inventory.json").read_text(encoding="utf-8"))
        cls.after = json.loads((DOCS / "AFTER_inventory.json").read_text(encoding="utf-8"))
        cls.report = json.loads((DOCS / "AFTER_parity.json").read_text(encoding="utf-8"))
        cls.cpp = ACTIVITY_CPP.read_text(encoding="utf-8")
        cls.header = ACTIVITY_H.read_text(encoding="utf-8")
        cls.group_header = GROUPS_H.read_text(encoding="utf-8")
        cls.font_selection = FONT_SELECTION_H.read_text(encoding="utf-8")
        cls.settings_list = SETTINGS_LIST_H.read_text(encoding="utf-8")
        cls.home_cpp = HOME_CPP.read_text(encoding="utf-8")
        cls.baseline_165 = json.loads(BASELINE_165.read_text(encoding="utf-8"))

    def test_all_setting_ids_and_persistence_descriptors_are_unchanged(self):
        before = {row["stableId"]: row for row in self.before["records"]}
        after = {row["stableId"]: row for row in self.after["records"]}
        self.assertEqual(set(before), set(after))
        fields = ("key", "label", "category", "categoryId", "type", "backingField", "options", "range", "default")
        for stable_id, old in before.items():
            if old["type"] in {"action", "web_feature", "enum_option"}:
                continue
            self.assertEqual({key: old.get(key) for key in fields},
                             {key: after[stable_id].get(key) for key in fields}, stable_id)
        self.assertTrue(self.report["persistedDescriptorParity"])

    def test_only_one_lazy_child_vector_is_kept(self):
        self.assertIn("std::vector<SettingInfo> currentGroupSettings;", self.header)
        for old_name in ("displaySettings", "readerSettings", "controlsSettings", "systemSettings"):
            self.assertNotIn(old_name, self.header)
        self.assertIn("static constexpr int categoryCount = 4;", self.header)
        self.assertIn("GUI.drawTabBar", self.cpp)
        self.assertNotIn("groupListActive", self.cpp)
        self.assertIn("currentGroupSettings.clear();", self.cpp)
        self.assertIn("const auto& baseSettings = getBaseSettingsList();", self.cpp)
        self.assertNotIn("getSettingsList(", self.cpp)

    def test_child_ordering_uses_no_temporary_heap_container(self):
        self.assertIn("std::initializer_list<StrId> preferred", self.cpp)
        self.assertIn("std::rotate(next, found, found + 1)", self.cpp)
        self.assertNotIn("std::stable_sort(settings.begin()", self.cpp)

    def test_folio_lite_layout_visibility_and_carousel_filter_match_main(self):
        self.assertIn("const bool folioLayoutTheme", self.cpp)
        self.assertIn("SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR_LITE", self.cpp)
        self.assertIn("isFolioLayoutSetting(setting.nameId) && !folioLayoutTheme", self.cpp)
        self.assertIn("if (isCarouselLayoutSetting(setting.nameId)) return false;", self.cpp)
        rows = {row["stableId"]: row for row in self.after["records"]}
        for stable_id in ("recentBookLayout", "finishedBookLayout"):
            self.assertTrue(rows[stable_id]["deviceReachableAfterPrototype"])
            self.assertIn("FOLIO_NOOIR and FOLIO_NOOIR_LITE",
                          rows[stable_id]["deviceExposureAfterPrototype"])
        self.assertFalse(rows["carouselLayout"]["deviceReachableAfterPrototype"])
        self.assertIn("Legacy Carousel field retained", rows["carouselLayout"]["deviceExposureAfterPrototype"])
        self.assertIn("legacy persisted field retained", self.report["layoutPresentation"]["carouselLayout"])
        self.assertEqual(self.report["deviceExposureCoverage"]["intentionalReplacements"]["carouselLayout"]["replacedBy"],
                         ["recentBookLayout", "finishedBookLayout"])
        self.assertIn("featuredShowSynopsis", self.home_cpp)
        self.assertIn("drawStats();", self.home_cpp)
        self.assertNotIn("if (!liteTheme)", self.home_cpp)
        self.assertNotIn("layout.detailHeight = 0", self.home_cpp)

    def test_single_section_reader_and_controls_tabs_render_settings_directly(self):
        self.assertIn("bool tabUsesAccordion(const int tab)", self.cpp)
        self.assertIn("if (!tabUsesAccordion(selectedCategoryIndex)) return settingsCount;", self.cpp)
        self.assertIn("if (!accordion)", self.cpp)
        self.assertIn("tabUsesAccordion(selectedCategoryIndex) && selectedSettingIndex == 0", self.cpp)

    def test_home_clears_stale_theme_switch_input_and_resets_wake_selection(self):
        self.assertIn("ignoreEntryInputUntilNeutral = hasActiveHomeInput(mappedInput);", self.home_cpp)
        self.assertIn("if (ignoreEntryInputUntilNeutral)", self.home_cpp)
        self.assertIn("HOME_ENTRY_INPUT_GATE_MAX_MS", self.home_cpp)
        self.assertIn("isFolioShelfTheme(SETTINGS.uiTheme)", self.home_cpp)
        self.assertIn("version == FOLIO_HOME_PREVIOUS_VERSION", self.home_cpp)
        self.assertIn("APP_STATE.selectFirstHomeBookAfterWake", self.home_cpp)
        self.assertIn("FOLIO_HOME_VERSION = 9", self.home_cpp)
        self.assertIn("selectFirstHomeBookAfterWake", (ROOT / "src" / "CrossPointState.cpp").read_text(encoding="utf-8"))

    def test_accordion_transitions_and_quick_actions_move_are_explicit(self):
        self.assertIn("void SettingsActivity::toggleAccordion()", self.cpp)
        self.assertIn("loadedTabIndex != selectedCategoryIndex || loadedSectionIndex != selectedSectionIndex", self.cpp)
        self.assertIn("sectionExpanded = false;", self.cpp)
        action_section = self.cpp.split("// Device-only actions are added only", 1)[1]
        controls = action_section.split("case SettingsGroupId::Controls:", 1)[1].split(
            "case SettingsGroupId::Connectivity:", 1)[0]
        self.assertIn("SettingAction::QuickActions", controls)
        quick = next(row for row in self.after["records"] if row["stableId"] == "action:QuickActions")
        self.assertEqual(quick["deviceGroupBefore"], "reading")
        self.assertEqual(quick["deviceGroupAfterPrototype"], "controls")

    def test_interface_font_selection_is_preserved_but_hidden_for_this_release(self):
        row = next(item for item in self.after["records"] if item["stableId"] == "manual:uiFontFamilyName")
        self.assertEqual(row["backingField"], "uiFontFamilyName")
        self.assertFalse(row["deviceReachableAfterPrototype"])
        self.assertIn("buildInterfaceFontSetting", self.settings_list)
        self.assertIn('if (setting.nameId == StrId::STR_INTERFACE_FONT) return false;', self.cpp)
        self.assertIn("kCustomUiFontsEnabledForCurrentRelease = false", self.font_selection)

    def test_165_device_baseline_ids_values_and_defaults_are_preserved(self):
        baseline = {row["stableId"]: row for row in self.baseline_165["records"]}
        current = {row["stableId"]: row for row in self.after["records"]}
        self.assertEqual(self.baseline_165["source"]["integrationSnapshot"],
                         "3a66ef9b8b88c538e96ac763878fa899e72a53b1")
        self.assertEqual(len(baseline), 124)
        self.assertFalse(set(baseline) - set(current))
        self.assertTrue(self.report["historicalDeviceBaseline"]["persistedDescriptorParity"])
        self.assertEqual(self.report["historicalDeviceBaseline"]["changedPersistedDescriptors"], [])
        self.assertEqual(
            set(self.report["historicalDeviceBaseline"]["additionalStableIdsInCurrentInventory"]),
            {
                "action:RandomSleepPool", "action:SleepScreenPreview", "randomSleepOrder",
                "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
                "sleepScreen.option.flexible_composite",
            },
        )

    def test_all_historical_web_keys_have_one_unchanged_group_contract(self):
        arrays = {}
        for match in re.finditer(r"static constexpr const char\* (\w+)\[\] = \{(.*?)\};", self.group_header, re.S):
            arrays[match.group(1)] = re.findall(r'"([^"\\]+)"', match.group(2))
        expected = set(self.before["summary"]["oldWebStableKeysList"])
        for key in expected:
            self.assertEqual(sum(key in keys for keys in arrays.values()), 1, key)
        self.assertEqual(len(expected), 84)
        self.assertEqual(self.before["summary"]["oldWebX4StableKeys"], 82)
        self.assertEqual(self.report["webSettings"]["everyHistoricalKeyHasGroup"], True)
        self.assertTrue(self.report["webSettings"]["routeCommitIntegratedHere"])
        self.assertEqual(self.report["integrationBlockers"], [])

    def test_bounded_web_contract_never_restores_all_catalog_access(self):
        contract = self.report["webSettings"]["apiContract"]
        self.assertIn("only the seven static descriptors", contract["groups"])
        self.assertIn("requires a valid group", contract["read"])
        self.assertIn("2 KiB", contract["write"])
        self.assertIn("must stay disabled", contract["legacyAllCatalogRoute"])
        self.assertTrue(all(self.report["settingSourceParity"].values()))

    def test_device_group_actions_cover_existing_child_routes(self):
        expected = {
            "HomeAppearance": {"VisualPreview"},
            "Sleep": {"ClearFavoriteSleepImage", "SleepScreenPreview"},
            "Reading": {"TextSettings", "DictionarySettings", "DictionaryIndexes", "DownloadFonts",
                        "CustomiseStatusBar"},
            "Controls": {"RemapFrontButtons", "RemapReaderFrontButtons", "QuickActions"},
            "Connectivity": {"Network", "Bluetooth", "KOReaderSync", "OPDSBrowser"},
            "Library": set(),
            "System": {"ClockWeatherSync", "SettingsProfiles", "ClearCache", "CheckForUpdates",
                       "SdFirmwareUpdate", "Language", "ToDoList"},
        }
        action_code = self.cpp.split("// Device-only actions are added only", 1)[1]
        for group, actions in expected.items():
            block = action_code.split(f"case SettingsGroupId::{group}:", 1)[1].split(
                "case SettingsGroupId::", 1)[0]
            for action in actions:
                self.assertIn(f"SettingAction::{action}", block, f"{group}: {action}")
        todo = next(row for row in self.after["records"] if row["stableId"] == "action:ToDoList")
        self.assertEqual(todo["deviceGroupAfterPrototype"], "system")
        self.assertIn("System tab > System section", todo["deviceExposureAfterPrototype"])

    def test_parity_report_and_markdown_table_have_requested_full_schema(self):
        coverage = self.report["deviceExposureCoverage"]
        self.assertEqual(coverage["previouslyExposedStableIds"], 127)
        self.assertEqual(coverage["reachableAfter"], 122)
        self.assertEqual(coverage["accidentalMissing"], 0)
        self.assertEqual(sum(self.report["webSettings"]["groupCounts"].values()), 84)
        self.assertEqual(self.report["webSettings"]["x4ReachableKeyCount"], 82)
        table = (DOCS / "PARITY_TABLE.md").read_text(encoding="utf-8")
        headers = [
            "Setting ID", "Old Group", "New Group", "Device Before", "Device After", "Web Before",
            "Web After", "Persisted Field", "Options", "Status",
        ]
        header_line = "| " + " | ".join(headers) + " |"
        self.assertIn(header_line, table)
        self.assertEqual(sum(line.startswith("| ") for line in table.splitlines()) - 2,
                         len(self.after["records"]))

    def test_intended_deferrals_are_not_counted_as_accidental_losses(self):
        self.assertEqual(self.report["deferredDeviceUi"], ["manual:uiFontFamilyName", "sleepScreen.option.flexible_composite"])
        self.assertEqual(len(self.report["deferredSleepComposer"]), 4)
        for stable_id in ("sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
                          "sleepScreen.option.flexible_composite", "web:clippings_notes"):
            row = next(item for item in self.after["records"] if item["stableId"] == stable_id)
            self.assertFalse(row["deviceReachableAfterPrototype"])

    def test_every_other_previously_reachable_device_setting_stays_reachable(self):
        deferred = {
            "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
            "sleepScreen.option.flexible_composite", "manual:uiFontFamilyName", "web:clippings_notes",
        }
        after = {row["stableId"]: row for row in self.after["records"]}
        for row in self.before["records"]:
            if row["stableId"] in deferred:
                continue
            if row["stableId"] == "carouselLayout":
                self.assertFalse(after[row["stableId"]]["deviceReachableAfterPrototype"])
                self.assertIn("maps", after[row["stableId"]]["deviceExposureAfterPrototype"])
                continue
            old_exposure = row.get("deviceExposureBefore") or ""
            if not old_exposure or old_exposure.startswith(("not exposed", "not selectable")):
                continue
            self.assertTrue(after[row["stableId"]]["deviceReachableAfterPrototype"], row["stableId"])


if __name__ == "__main__":
    unittest.main()
