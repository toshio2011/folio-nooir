import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src" / "SettingsGroups.h"
BEFORE = ROOT / "docs" / "settings-organization" / "BEFORE_inventory.json"


def read_group_arrays():
    text = HEADER.read_text(encoding="utf-8")
    arrays = {}
    for match in re.finditer(r"static constexpr const char\* (\w+)\[\] = \{(.*?)\};", text, re.S):
        arrays[match.group(1)] = re.findall(r'"([^"\\]+)"', match.group(2))
    return arrays


class SettingsGroupParityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inventory = json.loads(BEFORE.read_text(encoding="utf-8"))
        cls.arrays = read_group_arrays()
        cls.source_group_ids = {
            "homeKeys": "home_appearance",
            "sleepKeys": "sleep",
            "readingKeys": "reading",
            "controlsKeys": "controls",
            "connectivityKeys": "connectivity",
            "libraryKeys": "library",
            "systemKeys": "system",
        }
        cls.group_for_key = {
            key: cls.source_group_ids[group_name]
            for group_name, keys in cls.arrays.items()
            for key in keys
        }

    def test_all_pre_deferral_web_keys_have_one_group(self):
        expected = set(self.inventory["summary"]["oldWebStableKeysList"])
        self.assertEqual(len(expected), 84)
        self.assertTrue(expected.issubset(self.group_for_key))
        for key in expected:
            matches = [self.source_group_ids[name] for name, keys in self.arrays.items() if key in keys]
            self.assertEqual(len(matches), 1, f"{key} must belong to exactly one group")
            record = next(row for row in self.inventory["records"] if row.get("key") == key)
            self.assertEqual(matches[0], record["proposedGroupId"])

    def test_every_keyed_device_descriptor_is_classified(self):
        descriptors = [
            row for row in self.inventory["records"]
            if row.get("key") and row.get("sourceScope") in {"base_list", "text_settings"}
        ]
        self.assertTrue(descriptors)
        for row in descriptors:
            key = row["key"]
            self.assertIn(key, self.group_for_key, f"unclassified descriptor key {key}")
            matches = [self.source_group_ids[name] for name, keys in self.arrays.items() if key in keys]
            self.assertEqual(len(matches), 1, f"{key} must belong to exactly one group")
            self.assertEqual(matches[0], row["proposedGroupId"], key)

    def test_manual_device_keys_and_hidden_composer_keys_are_grouped(self):
        for row in self.inventory["records"]:
            if row.get("sourceScope") == "manual_device_setting" and row.get("key"):
                self.assertEqual(self.group_for_key.get(row["key"]), row["proposedGroupId"], row["key"])

        for key in ("sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement"):
            self.assertEqual(self.group_for_key.get(key), "sleep")
            self.assertNotIn(key, self.inventory["summary"]["oldWebStableKeysList"])

        flexible = next(row for row in self.inventory["records"]
                        if row["stableId"] == "sleepScreen.option.flexible_composite")
        self.assertTrue(flexible["intentionallyHidden"])
        self.assertEqual(flexible["enumValue"], 17)

    def test_quick_actions_are_regrouped_without_changing_the_old_location(self):
        quick_actions = next(row for row in self.inventory["records"]
                             if row["stableId"] == "action:QuickActions")
        self.assertEqual(quick_actions["deviceGroupBefore"], "reading")
        self.assertEqual(quick_actions["proposedGroupId"], "controls")

    def test_group_id_list_stays_at_one_level(self):
        text = HEADER.read_text(encoding="utf-8")
        ids = re.findall(r'\{SettingsGroupId::\w+, "([a-z_]+)",', text)
        self.assertEqual(ids, [
            "home_appearance", "sleep", "reading", "controls",
            "connectivity", "library", "system",
        ])


if __name__ == "__main__":
    unittest.main()
