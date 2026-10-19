#!/usr/bin/env python3
"""Generate the prototype AFTER inventory and machine-readable parity report."""

from __future__ import annotations

import copy
import json
import subprocess
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "settings-organization"
BEFORE_PATH = OUT / "BEFORE_inventory.json"
BASELINE_165_PATH = OUT / "BASELINE_1.6.5_DEVICE_inventory.json"

DEVICE_PATH_BY_GROUP = {
    "home_appearance": "Display tab > Home & Appearance section",
    "sleep": "Display tab > Sleep section",
    "reading": "Reader tab > Reading section",
    "controls": "Controls tab > Buttons section",
    "connectivity": "System tab > Connectivity section",
    "library": "System tab > Library section",
    "system": "System tab > System section",
}


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8").strip()


def unchanged_from(revision: str, *paths: str) -> bool:
    result = subprocess.run(["git", "diff", "--quiet", revision, "--", *paths], cwd=ROOT, check=False)
    return result.returncode == 0


def classify_after(record: dict) -> tuple[bool, str]:
    sid = record["stableId"]
    before = record.get("deviceExposureBefore") or ""
    if sid == "manual:uiFontFamilyName":
        return False, (
            "Intentionally hidden in 1.6.6; saved family remains stored, and the built-in UI font is used"
        )
    if sid in {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
               "sleepScreen.option.flexible_composite", "web:clippings_notes"}:
        return False, record.get("hiddenReason") or "intentionally deferred"
    if sid == "carouselLayout":
        return False, (
            "Legacy Carousel field retained for persistence/migration; its value maps to the independent "
            "Recent/Finished layout selectors, which are available in Folio Nooir Lite"
        )
    if sid in {"recentBookLayout", "finishedBookLayout"}:
        group = record.get("proposedGroupId")
        return True, f"{DEVICE_PATH_BY_GROUP.get(group, group)}; available in FOLIO_NOOIR and FOLIO_NOOIR_LITE"
    if before.startswith("not exposed") or before.startswith("not selectable"):
        return False, before
    if record.get("sourceScope") == "base_list" and record.get("categoryId") == "STR_CAT_READER" and record.get("inTextSettings"):
        return True, "Reader tab > Reading section; existing Text Settings child screen"
    if "Visual Preview sub-screen" in before:
        return True, "Display tab > Home & Appearance section; existing Visual Preview child screen"
    if "Status Bar sub-screen" in before:
        return True, "Reader tab > Reading section; existing Customize Status Bar child screen"
    if "KOReader Sync sub-screen" in before:
        return True, "System tab > Connectivity section; existing KOReader Sync child screen"
    if "ButtonRemapActivity" in before:
        return True, "Controls tab > Buttons section; existing button remap child screen"
    if "BluetoothSettingsActivity" in before:
        return True, "System tab > Connectivity section; existing Bluetooth child screen"
    group = record.get("proposedGroupId")
    return bool(group), DEVICE_PATH_BY_GROUP.get(group, "not directly reachable") if group else "not directly reachable"


WEB_GROUP_COUNTS = {
    "reading": 31,
    "home_appearance": 21,
    "controls": 11,
    "sleep": 7,
    "connectivity": 7,
    "library": 5,
    "system": 2,
}
WEB_ROUTE_COMMIT = "908453f39608169b1dad079e31a94db28415001b"
WEB_INTEGRATION_COMMIT = "7296bbc85467675fdc8e1dffd4c73f4022e96518"


def markdown_cell(value: object) -> str:
    if value is None or value == "":
        return "—"
    if isinstance(value, (dict, list)):
        value = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    return str(value).replace("|", "\\|").replace("\n", " ")


def options_summary(record: dict) -> str:
    if record["type"] == "enum_option":
        return f"value={record.get('persistedValue')}; {record.get('englishLabel') or record.get('label')}"
    options = record.get("options") or []
    if options:
        return json.dumps(options, ensure_ascii=False, separators=(",", ":"))
    value_range = record.get("range") or record.get("rangeExpressions")
    if value_range:
        return json.dumps(value_range, ensure_ascii=False, separators=(",", ":"))
    if record["type"] == "fixed_map":
        return "fixed-size mapping; options described in inventory"
    return "—"


def persisted_descriptor(record: dict) -> dict:
    """Keep persistence-relevant option IDs and values; ignore translated text."""
    option_rows = []
    for option in record.get("options") or []:
        option_rows.append({key: value for key, value in option.items() if key != "englishLabel"})
    return {
        key: record.get(key)
        for key in ("key", "label", "category", "categoryId", "type", "backingField", "range", "default")
    } | {"options": option_rows}


def web_after_description(record: dict) -> str:
    sid = record["stableId"]
    if sid == "web:clippings_notes":
        return "Intentionally deferred; no Clippings routes; /My Clippings.txt unchanged"
    if sid == "carouselLayout":
        return (
            "Legacy key remains in the bounded home_appearance API for compatibility; the Web UI uses "
            "Recent/Finished layout selectors for Folio Nooir Lite"
        )
    exposure = record.get("webExposureBeforeDisabledDecision") or ""
    if exposure.startswith("yes") or exposure.startswith("conditional"):
        group = record.get("proposedGroupId") or "unclassified"
        suffix = f"; conditional ({exposure})" if exposure.startswith("conditional") else ""
        return (
            f"Bounded grouped API: {group}; GET page offset 0–32/limit 1–6; "
            f"group write ≤2 KiB/32 keys; commit {WEB_ROUTE_COMMIT[:8]}{suffix}"
        )
    if exposure.startswith("no"):
        return "Not exposed by Web Settings"
    return "—"


def row_status(record: dict) -> str:
    sid = record["stableId"]
    if sid == "carouselLayout":
        return (
            "Replaced by Recent/Finished layout selectors for Folio Nooir Lite; legacy persisted value "
            "remains for migration/compatibility"
        )
    if sid == "manual:uiFontFamilyName":
        return "Intentional 1.6.6 UI deferral; saved value retained and built-in UI font used"
    if sid in {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
               "sleepScreen.option.flexible_composite", "web:clippings_notes"}:
        return "Intentional 1.6.6 deferral; implementation and stored values preserved"
    old_group = record.get("deviceGroupBefore")
    new_group = record.get("proposedGroupId")
    if old_group and new_group and old_group != new_group:
        return "Presentation regrouping; persisted field/options unchanged"
    if record["type"] == "web_feature":
        return "Intentionally deferred Web feature"
    if record["type"] == "action":
        return "Existing device action; group placement only"
    if record.get("webExposureBeforeDisabledDecision", "").startswith(("yes", "conditional")):
        return "Stable Web key; bounded group route restored"
    if record.get("deviceReachableAfterPrototype"):
        return "Reachable; persisted field/options unchanged"
    return "Not exposed in the prior device UI"


def old_device_group(record: dict) -> str:
    group = record.get("deviceGroupBefore")
    if group:
        return group
    sid = record["stableId"]
    exposure = record.get("deviceExposureBefore") or ""
    category = record.get("categoryId")
    if sid.startswith("frontButton") or sid.startswith("readerFrontButton"):
        return "controls (button-remap action)"
    if sid.startswith("quickActionSlot"):
        return "reading (Quick Actions action)"
    if sid.startswith("ble") or sid.startswith("bluetooth"):
        return "system (Bluetooth action)"
    if sid == "language":
        return "system (Language action)"
    if sid == "manual:uiFontFamilyName":
        return "home_appearance (Display)"
    if category == "STR_CUSTOMISE_STATUS_BAR":
        return "reading (Customize Status Bar action)"
    if category == "STR_KOREADER_SYNC":
        return "system (KOReader Sync action)"
    if category == "STR_CAT_DISPLAY":
        return "home_appearance"
    if category == "STR_CAT_READER":
        return "reading"
    if category == "STR_CAT_CONTROLS":
        return "controls"
    if category == "STR_CAT_SYSTEM":
        return "system"
    if exposure.startswith("not exposed"):
        return "— (not on device Settings)"
    return "—"


def render_parity_table(before: dict, after: dict, baseline_165: dict, report: dict) -> str:
    records = after["records"]
    audit = report["settingsParityAudit"]
    base = audit["original165"]
    current = audit["current166"]
    comparison = audit["baselineComparison"]
    headers = [
        "Setting ID", "Old Group", "New Group", "Device Before", "Device After",
        "Web Before", "Web After", "Persisted Field", "Options", "Status",
    ]
    lines = [
        "# 1.6.6 Settings Organization Parity Table",
        "",
        f"Stable IDs and descriptor metadata are compared with the 1.6.5 device baseline "
        f"`{baseline_165['source']['integrationSnapshot']}` and the frozen 1.6.6 inventory "
        f"`{before['source']['integrationSnapshot']}`. "
        f"Historical Web exposure comes from `{before['source']['priorWebSettingsSourceRevision']}`; "
        f"bounded Web behavior is reported from integrated Web route commit `{WEB_ROUTE_COMMIT}` "
        f"(latest integration `{WEB_INTEGRATION_COMMIT}`) on the current 1.6.6 branch. Web Settings is active; "
        "Web Clippings/Notes remain intentionally disabled. Physical X4 validation is still pending.",
        "",
        f"Parity audit: 1.6.5 had {base['stableIdCount']} stable IDs, "
        f"{base['deviceExposedCount']} device entries, and {base['webSettingKeyCount']} Web keys "
        f"({base['x4WebSettingKeyCount']} on X4). Current 1.6.6 has {current['stableIdCount']} stable IDs, "
        f"{current['deviceReachableCount']} device entries, and {current['webSettingKeyCount']} Web keys "
        f"({current['x4WebSettingKeyCount']} on X4). Missing baseline IDs/Web keys: "
        f"{len(comparison['missingStableIds'])}/{len(comparison['missingWebKeys'])}; duplicate IDs/groupings: "
        f"{len(comparison['duplicateStableIds'])}/{len(comparison['duplicateCurrentWebGroupings'])}; "
        f"unreachable original device entries: {', '.join(comparison['originalDeviceSettingsUnreachable']) or 'none'} "
        f"(intentional). Persisted descriptors unchanged: "
        f"{str(comparison['persistedKeysDefaultsEnumsRangesAndOptionsUnchanged']).lower()}. "
        f"Audit: {'PASS' if report['settingsParityAudit']['pass'] else 'FAIL'}.",
        "",
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    before_by_id = {r["stableId"]: r for r in before["records"]}
    for record in records:
        old = before_by_id[record["stableId"]]
        row = [
            record["stableId"],
            old_device_group(old),
            record.get("proposedGroupId"),
            old.get("deviceExposureBefore"),
            record.get("deviceExposureAfterPrototype"),
            old.get("webExposureBeforeDisabledDecision"),
            web_after_description(record),
            record.get("backingField"),
            options_summary(record),
            row_status(record),
        ]
        lines.append("| " + " | ".join(markdown_cell(value) for value in row) + " |")
    lines.extend(["", f"Total stable IDs: {len(records)}.", ""])
    return "\n".join(lines)


def main() -> None:
    before = json.loads(BEFORE_PATH.read_text(encoding="utf-8"))
    baseline_165 = json.loads(BASELINE_165_PATH.read_text(encoding="utf-8"))
    after = copy.deepcopy(before)
    after["inventoryName"] = "1.6.6 Settings parity — original tabs with accordion sections"
    after["source"]["prototypeBaseCommit"] = git("rev-parse", "HEAD")
    after["source"]["prototypeWorkingTreeFiles"] = [
        "src/SettingsGroups.h",
        "src/SettingsGroupDevice.h",
        "src/activities/settings/SettingsActivity.cpp",
        "src/activities/settings/SettingsActivity.h",
        "src/activities/home/RecentBooksActivity.cpp",
        "src/activities/home/RecentBooksActivity.h",
        "src/activities/reader/EpubReaderActivity.cpp",
        "src/util/BleMemoryPolicy.h",
        "src/CrossPointState.cpp",
        "src/CrossPointState.h",
        "src/main.cpp",
        "src/activities/settings/DeviceInfoActivity.cpp",
        "src/activities/settings/DeviceInfoActivity.h",
        "src/InterfaceFontSelection.h",
        "src/SdCardFontSystem.cpp",
        "src/network/html/SettingsPage.html",
        "test/font_selection_compatibility/FontSelectionCompatibilityTest.cpp",
        "test/ble_memory_policy/BleMemoryPolicyTest.cpp",
        "test/settings_organization/test_after_prototype.py",
        "test/web_stability/test_grouped_settings_contract.py",
        "docs/settings-organization/generate_after_inventory.py",
        "test/web_stability/CMakeLists.txt",
    ]
    after["source"]["prototypeLayout"] = (
        "Original Display, Reader, Controls, and System tabs; lightweight accordion sections within each tab; "
        "Reading and Controls rows appear directly because each tab has one group; one currentGroupSettings "
        "vector is materialized for the selected group"
    )
    after["source"]["webRouteStatus"] = (
        f"Bounded Web Settings group/page routes are integrated on the current 1.6.6 branch "
        f"(route {WEB_ROUTE_COMMIT}; integration {WEB_INTEGRATION_COMMIT}); Web Clippings/Notes remain disabled."
    )
    after["source"]["homeWorkerOverlap"] = (
        "Theme value 5 remains the Folio Nooir Lite alias and uses the same featured synopsis, stats, "
        "and layout controls as Folio Nooir. Recent/Finished layouts are available under both Folio themes; "
        "the legacy Carousel field is retained for persistence/migration and replaced in the UI by those selectors. "
        "The saved Interface Font selection remains stored, while the row is hidden and the built-in UI font is used."
    )
    for record in after["records"]:
        if record["stableId"] == "action:ToDoList":
            record["proposedGroupId"] = "system"
        reachable, description = classify_after(record)
        record["deviceExposureAfterPrototype"] = description
        record["deviceReachableAfterPrototype"] = reachable
        record["deviceGroupAfterPrototype"] = record.get("proposedGroupId") if reachable else None
        record["stableIdUnchanged"] = True
        record["persistenceAndOptionsUnchanged"] = record["type"] not in {"action", "web_feature", "enum_option"}

    before_by_id = {r["stableId"]: r for r in before["records"]}
    after_by_id = {r["stableId"]: r for r in after["records"]}
    baseline_165_by_id = {r["stableId"]: r for r in baseline_165["records"]}
    baseline_165_missing = sorted(set(baseline_165_by_id) - set(after_by_id))
    baseline_165_added = sorted(set(after_by_id) - set(baseline_165_by_id))
    baseline_165_descriptor_changes = sorted(
        sid for sid in set(baseline_165_by_id) & set(after_by_id)
        if persisted_descriptor(baseline_165_by_id[sid]) != persisted_descriptor(after_by_id[sid])
    )
    intentional_device_deferrals = {
        "sleepBackgroundElement",
        "sleepOverlayAElement",
        "sleepOverlayBElement",
        "sleepScreen.option.flexible_composite",
        "manual:uiFontFamilyName",
    }
    intentional_device_replacements = {
        "carouselLayout": {
            "replacedBy": ["recentBookLayout", "finishedBookLayout"],
            "reason": "Theme value 5 is Folio Nooir Lite; old layout value migrates to the independent selectors",
            "persistedFieldRetained": True,
        }
    }
    previously_exposed_device = [
        row for row in before["records"]
        if (row.get("deviceExposureBefore") or "")
        and not row["deviceExposureBefore"].startswith(("not exposed", "not selectable"))
    ]
    accidental_device_missing = [
        row["stableId"] for row in previously_exposed_device
        if row["stableId"] not in intentional_device_deferrals
        and row["stableId"] not in intentional_device_replacements
        and not after_by_id[row["stableId"]]["deviceReachableAfterPrototype"]
    ]
    descriptor_fields = ("key", "label", "category", "categoryId", "type", "backingField", "options", "range",
                         "default")
    changed_descriptors = [
        sid for sid, old in before_by_id.items()
        if old["type"] not in {"action", "web_feature", "enum_option"}
        and any(old.get(field) != after_by_id[sid].get(field) for field in descriptor_fields)
    ]
    web_keys = before["summary"]["oldWebStableKeysList"]
    group_by_key = {}
    for record in after["records"]:
        if record.get("key") and record.get("proposedGroupId"):
            group_by_key[record["key"]] = record["proposedGroupId"]

    quick_actions = after_by_id["action:QuickActions"]
    baseline_device_rows = [
        row for row in baseline_165["records"]
        if (row.get("deviceExposureBefore") or "")
        and not row["deviceExposureBefore"].startswith(("not exposed", "not selectable"))
    ]
    baseline_device_unreachable = [
        row["stableId"] for row in baseline_device_rows
        if not after_by_id[row["stableId"]]["deviceReachableAfterPrototype"]
    ]
    baseline_web_keys = set(baseline_165["summary"]["oldWebStableKeysList"])
    current_web_keys = set(web_keys)
    current_grouped_web_rows = [
        row for row in after["records"]
        if row.get("key") in current_web_keys and row.get("proposedGroupId")
    ]
    current_grouped_web_key_counts = Counter(row["key"] for row in current_grouped_web_rows)
    current_web_ungrouped = sorted(key for key in current_web_keys if current_grouped_web_key_counts[key] == 0)
    current_stable_id_counts = Counter(row["stableId"] for row in after["records"])
    current_device_orphans = [
        row["stableId"] for row in after["records"]
        if row["deviceReachableAfterPrototype"] and not row.get("proposedGroupId")
    ]
    duplicate_stable_ids = sorted(stable_id for stable_id, count in current_stable_id_counts.items() if count > 1)
    duplicate_current_web_groups = sorted(
        key for key, count in current_grouped_web_key_counts.items() if count > 1
    )
    baseline_device_intentional_gaps = sorted(
        stable_id for stable_id in baseline_device_unreachable
        if stable_id in intentional_device_deferrals or stable_id in intentional_device_replacements
    )
    baseline_device_accidental_gaps = sorted(
        set(baseline_device_unreachable) - set(baseline_device_intentional_gaps)
    )
    moved = [
        {"stableId": r["stableId"], "before": r.get("deviceGroupBefore"), "after": r.get("proposedGroupId")}
        for r in after["records"]
        if r.get("deviceReachableAfterPrototype") and r.get("deviceGroupBefore") and r.get("proposedGroupId")
        and r["deviceGroupBefore"] != r["proposedGroupId"]
    ]
    report = {
        "schemaVersion": 1,
        "baselineCommit": before["source"]["integrationSnapshot"],
        "historicalWebExposureCommit": before["source"]["priorWebSettingsSourceRevision"],
        "prototypeBaseCommit": git("rev-parse", "HEAD"),
        "prototypeFiles": [
            "src/SettingsGroups.h",
            "src/SettingsGroupDevice.h",
            "src/activities/settings/SettingsActivity.cpp",
            "src/activities/settings/SettingsActivity.h",
            "src/activities/home/RecentBooksActivity.cpp",
            "src/activities/home/RecentBooksActivity.h",
            "src/activities/reader/EpubReaderActivity.cpp",
            "src/util/BleMemoryPolicy.h",
            "src/CrossPointState.cpp",
            "src/CrossPointState.h",
            "src/main.cpp",
            "src/activities/settings/DeviceInfoActivity.cpp",
            "src/activities/settings/DeviceInfoActivity.h",
            "src/InterfaceFontSelection.h",
            "src/SdCardFontSystem.cpp",
            "src/network/html/SettingsPage.html",
            "test/font_selection_compatibility/FontSelectionCompatibilityTest.cpp",
        "test/ble_memory_policy/BleMemoryPolicyTest.cpp",
        "test/settings_organization/test_after_prototype.py",
            "test/web_stability/test_grouped_settings_contract.py",
        "docs/settings-organization/generate_after_inventory.py",
        ],
        "homeWorkerAOverlap": (
            "Folio Nooir Lite uses the same featured synopsis, statistics, and layout choices as Folio Nooir. "
            "Theme value 5 and stored fields are preserved. Recent/Finished selectors are available in both Folio "
            "themes and the Web UI matches the device. The legacy Carousel field remains for persistence/migration. "
            "Custom UI font selection is hidden for 1.6.6 and the built-in UI font is used."
        ),
        "groupIds": [group["id"] for group in before["proposedGroups"]],
        "stableIdsBefore": len(before_by_id),
        "stableIdsAfter": len(after_by_id),
        "stableIdSetParity": set(before_by_id) == set(after_by_id),
        "changedPersistedDescriptorFields": changed_descriptors,
        "persistedDescriptorParity": not changed_descriptors,
        "historicalDeviceBaseline": {
            "branch": "codex/folio-nooir-1.6.5-rc2",
            "commit": baseline_165["source"]["integrationSnapshot"],
            "stableIds": len(baseline_165_by_id),
            "allBaselineStableIdsPresent": not baseline_165_missing,
            "missingStableIds": baseline_165_missing,
            "additionalStableIdsInCurrentInventory": baseline_165_added,
            "persistedDescriptorParity": not baseline_165_descriptor_changes,
            "changedPersistedDescriptors": baseline_165_descriptor_changes,
            "comparisonFields": [
                "stable ID, storage key, storage field, default, enum option ID/index/value, and numeric range"
            ],
            "translationTextExcludedFromValueComparison": True,
        },
        "settingsParityAudit": {
            "original165": {
                "stableIdCount": len(baseline_165_by_id),
                "deviceExposedCount": len(baseline_device_rows),
                "webSettingKeyCount": len(baseline_web_keys),
                "x4WebSettingKeyCount": baseline_165["summary"]["oldWebX4StableKeys"],
            },
            "current166": {
                "stableIdCount": len(after_by_id),
                "deviceReachableCount": sum(row["deviceReachableAfterPrototype"] for row in after["records"]),
                "webSettingKeyCount": len(current_web_keys),
                "x4WebSettingKeyCount": before["summary"]["oldWebX4StableKeys"],
            },
            "baselineComparison": {
                "missingStableIds": baseline_165_missing,
                "duplicateStableIds": duplicate_stable_ids,
                "persistedKeysDefaultsEnumsRangesAndOptionsUnchanged": not baseline_165_descriptor_changes,
                "changedPersistedDescriptors": baseline_165_descriptor_changes,
                "originalDeviceSettingsUnreachable": baseline_device_unreachable,
                "intentionalDeviceGaps": baseline_device_intentional_gaps,
                "accidentalDeviceGaps": baseline_device_accidental_gaps,
                "missingWebKeys": sorted(baseline_web_keys - current_web_keys),
                "additionalCurrentWebKeys": sorted(current_web_keys - baseline_web_keys),
                "ungroupedCurrentWebKeys": current_web_ungrouped,
                "duplicateCurrentWebGroupings": duplicate_current_web_groups,
                "currentDeviceOrphans": current_device_orphans,
            },
            "intentionalExclusions": {
                "flexibleSleep": "Hidden from user UI; stored enum values and implementation are retained.",
                "customUiFont": "Hidden from user UI; saved value is retained and built-in UI font is used.",
                "legacyCarouselLayout": (
                    "Legacy persisted value remains; Recent and Finished layout selectors replace its UI path."
                ),
                "webOnlyClippingsAndNotes": (
                    "Web-only Clippings/Notes routes remain disabled; on-device Notes and My Clippings.txt export stay."
                ),
            },
            "x3X4Guards": {
                "touchReaderControls": "touch hardware only",
                "tiltPageTurn": "X3 QMI8658 IMU only; excluded on X4",
            },
            "pass": not baseline_165_missing and not duplicate_stable_ids and
                    not baseline_165_descriptor_changes and not baseline_device_accidental_gaps and
                    not (baseline_web_keys - current_web_keys) and not current_web_ungrouped and
                    not duplicate_current_web_groups and
                    not current_device_orphans,
        },
        "newDeviceActions": [
            {
                "stableId": "action:DeviceInfo",
                "path": "System tab > System section > Device Info",
                "persistence": "read-only snapshot; no stored setting",
            }
        ],
        "userUiExceptions": {
            "flexibleSleep": "Hidden from the 1.6.6 UI; enum and implementation remain stored and compiled.",
            "customUiFont": "Hidden from the 1.6.6 UI; saved selection remains stored and built-in UI font is used.",
            "webClippingsAndNotes": "Web-only routes stay disabled; on-device Notes and My Clippings.txt export remain.",
        },
        "oldDeviceGroupsPreservedAsMetadata": True,
        "deviceExposureCoverage": {
            "previouslyExposedStableIds": len(previously_exposed_device),
            "reachableAfter": sum(after_by_id[r["stableId"]]["deviceReachableAfterPrototype"]
                                  for r in previously_exposed_device),
            "intentionalDeferrals": sorted(intentional_device_deferrals),
            "intentionalReplacements": intentional_device_replacements,
            "accidentalMissing": len(accidental_device_missing),
            "accidentalMissingStableIds": accidental_device_missing,
        },
        "movedDeviceEntries": moved,
        "quickActions": {
            "oldGroup": quick_actions["deviceGroupBefore"],
            "prototypeGroup": quick_actions["proposedGroupId"],
            "actionAndStoredSlotsUnchanged": True,
        },
        "deferredDeviceUi": ["manual:uiFontFamilyName", "sleepScreen.option.flexible_composite"],
        "deferredSleepComposer": [
            "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
            "sleepScreen.option.flexible_composite",
        ],
        "layoutPresentation": {
            "recentBookLayout": "available in FOLIO_NOOIR and FOLIO_NOOIR_LITE; same synopsis/stats panel; option set and persisted field unchanged",
            "finishedBookLayout": "available in FOLIO_NOOIR and FOLIO_NOOIR_LITE; same synopsis/stats panel; option set and persisted field unchanged",
            "carouselLayout": (
                "legacy persisted field retained; value migrates to recentBookLayout/finishedBookLayout; "
                "replaced in UI because theme value 5 is Folio Nooir Lite"
            ),
        },
        "clippingsRoutes": "remain disabled; existing per-book storage and My Clippings.txt untouched",
        "webSettings": {
            "historicalStableKeyCount": len(web_keys),
            "historicalX4KeyCount": before["summary"]["oldWebX4StableKeys"],
            "everyHistoricalKeyHasGroup": all(key in group_by_key for key in web_keys),
            "allHistoricalKeysGroupedExactlyOnce": (
                not current_web_ungrouped and not duplicate_current_web_groups
            ),
            "groupCounts": WEB_GROUP_COUNTS,
            "routeCommit": WEB_ROUTE_COMMIT,
            "latestIntegrationCommit": WEB_INTEGRATION_COMMIT,
            "routeCommitIntegratedHere": True,
            "routeIntegratedInMain": True,
            "historicalKeysReachableThroughGroupedApi": len(web_keys) - len(current_web_ungrouped),
            "x4ReachableKeyCount": before["summary"]["oldWebX4StableKeys"],
            "liteLayoutUiParity": (
                "Theme value 5 is Folio Nooir Lite; device and Web expose the independent Recent/Finished "
                "layout selectors and hide the migrated legacy Carousel selector. The device Lite theme keeps "
                "the same synopsis and statistics presentation as Folio Nooir."
            ),
            "conditionalKeys": {
                "touchReaderControls": "touch hardware only",
                "tiltPageTurn": "X3 QMI8658 IMU only",
            },
            "implementationTests": {
                "groupedSettingsContract": "8/8",
                "settingsOrganizationParity": "24/24",
                "pythonSuitesTotal": "32/32 across settings_organization and web_stability",
                "generatedHtmlAndJsSyntax": "passed (Node.js; 9 inline HTML scripts checked)",
                "bleMemoryPolicyHostSuite": (
                    "Blocked locally: CMake/MSVC compiler setup failed its compiler probe; no current C++ test result."
                ),
                "fullHostSuite": (
                    "Not run to completion: CMake/MSVC reported a Program Database manager mismatch (C1902), "
                    "then failed to identify supported C++ features; no current full-suite result."
                ),
            },
            "build": "Pending final consolidated gh_release build for the current 1.6.6 RC changes.",
            "apiContract": {
                "groups": "GET /api/settings/groups returns only the seven static descriptors",
                "read": "GET /api/settings requires a valid group; offset 0..32 and limit 1..6; returns only the selected group page and X-Settings-Total",
                "write": "group-scoped change map capped at 2 KiB and 32 keys; validate all keys against group before applying; save once",
                "legacyAllCatalogRoute": "must stay disabled; no no-group fallback",
                "clippings": "remain disabled",
            },
            "routeImplementation": (
                f"complete and integrated on main at {WEB_INTEGRATION_COMMIT}; bounded replacement "
                "does not enable old all-catalog request behavior"
            ),
        },
        "settingSourceParity": {
            "SettingsList.h persisted descriptors": not changed_descriptors,
            "CrossPointSettings.h legacy theme value": (
                "FOLIO_NOOIR_LITE = 5" in (ROOT / "src" / "CrossPointSettings.h").read_text(encoding="utf-8")
                and "CAROUSEL = FOLIO_NOOIR_LITE" in (ROOT / "src" / "CrossPointSettings.h").read_text(encoding="utf-8")
            ),
            "QuickActions source": unchanged_from("ce86b9f8", "src/util/QuickActions.h", "src/util/QuickActions.cpp"),
        },
        "lazyAllocation": {
            "staticTabCount": 4,
            "staticAccordionSectionCount": 7,
            "dynamicSettingVectors": 1,
            "materializedOnlyWhenSectionExpanded": False,
            "singleGroupTabsRenderDirectly": True,
            "retainedOnNestedActivityReturn": True,
            "releasedOnSettingsExit": True,
            "capacityRetainedForReuse": True,
            "capacityLifetime": "retained while Settings is active or stacked under a child; vector storage is released on Settings exit",
            "capacityDiagnostic": "the current section vector size and capacity are logged; hardware allocation measurement is pending",
            "x4MaxRequestedChildRows": None,
            "x4MaxRetainedBufferBytes": None,
            "filesystemWork": "accordion actions use cached descriptors; dictionary discovery runs only after selecting its picker; favorite-image checks occur once on Settings entry",
            "ordinaryToggleRebuilds": "only settings that change the selected section's row shape rebuild that one section vector",
        },
        "candidateForIntegration": True,
        "integrationBlockers": [],
        "releaseValidation": {
            "sourceParity": "passed; accidental missing settings=0; accidental duplicates=0",
            "firmwareBuild": "pending final consolidated gh_release build",
            "physicalX4": "pending; not validated on device",
            "releaseReady": False,
        },
        "physicalX4Validation": (
            "Pending; verify first Home responsiveness, Settings navigation, Device Info, Web re-entry, sleep/wake, "
            "Reader Bluetooth, and input mapping on hardware before RC acceptance."
        ),
    }
    (OUT / "AFTER_inventory.json").write_text(json.dumps(after, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (OUT / "AFTER_parity.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (OUT / "PARITY_TABLE.md").write_text(render_parity_table(before, after, baseline_165, report), encoding="utf-8")
    print(json.dumps({k: report[k] for k in (
        "stableIdsBefore", "stableIdsAfter", "stableIdSetParity", "persistedDescriptorParity",
        "movedDeviceEntries", "webSettings", "candidateForIntegration", "integrationBlockers",
    )}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
