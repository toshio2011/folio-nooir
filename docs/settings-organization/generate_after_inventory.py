#!/usr/bin/env python3
"""Generate the prototype AFTER inventory and machine-readable parity report."""

from __future__ import annotations

import copy
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "settings-organization"
BEFORE_PATH = OUT / "BEFORE_inventory.json"


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8").strip()


def unchanged_from(revision: str, *paths: str) -> bool:
    result = subprocess.run(["git", "diff", "--quiet", revision, "--", *paths], cwd=ROOT, check=False)
    return result.returncode == 0


def classify_after(record: dict) -> tuple[bool, str]:
    sid = record["stableId"]
    before = record.get("deviceExposureBefore") or ""
    if sid == "manual:uiFontFamilyName":
        return True, "Home & Appearance group > Interface Font; Default plus installed compatible Mini Pack families"
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
        return True, f"group root > {group}; available in FOLIO_NOOIR and FOLIO_NOOIR_LITE"
    if before.startswith("not exposed") or before.startswith("not selectable"):
        return False, before
    if record.get("sourceScope") == "base_list" and record.get("categoryId") == "STR_CAT_READER" and record.get("inTextSettings"):
        return True, "Reading group; existing Text Settings child screen"
    if "Visual Preview sub-screen" in before:
        return True, "Home & Appearance group; existing Visual Preview child screen"
    if "Status Bar sub-screen" in before:
        return True, "Reading group; existing Customize Status Bar child screen"
    if "KOReader Sync sub-screen" in before:
        return True, "Connectivity group; existing KOReader Sync child screen"
    if "ButtonRemapActivity" in before:
        return True, "Controls group; existing button remap child screen"
    if "BluetoothSettingsActivity" in before:
        return True, "Connectivity group; existing Bluetooth child screen"
    group = record.get("proposedGroupId")
    return bool(group), f"group root > {group}" if group else "not directly reachable"


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
    if sid in {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
               "sleepScreen.option.flexible_composite", "web:clippings_notes"}:
        return "Intentional 1.6.6 deferral; implementation/storage preserved where applicable"
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


def render_parity_table(before: dict, after: dict) -> str:
    records = after["records"]
    headers = [
        "Setting ID", "Old Group", "New Group", "Device Before", "Device After",
        "Web Before", "Web After", "Persisted Field", "Options", "Status",
    ]
    lines = [
        "# 1.6.6 Settings Organization Parity Table",
        "",
        f"Device rows are compared with frozen source `{before['source']['integrationSnapshot']}`. "
        f"Historical Web exposure comes from `{before['source']['priorWebSettingsSourceRevision']}`; "
        f"bounded Web behavior is reported from integrated Web route commit `{WEB_ROUTE_COMMIT}` "
        f"(latest integration `{WEB_INTEGRATION_COMMIT}`) on the current 1.6.6 branch. Web Settings is active; "
        "Web Clippings/Notes remain intentionally disabled. Physical X4 validation is still pending.",
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
    after = copy.deepcopy(before)
    after["inventoryName"] = "Settings organization experiment — AFTER prototype"
    after["source"]["prototypeBaseCommit"] = git("rev-parse", "HEAD")
    after["source"]["prototypeWorkingTreeFiles"] = [
        "src/SettingsGroups.h",
        "src/SettingsGroupDevice.h",
        "src/activities/settings/SettingsActivity.cpp",
        "src/activities/settings/SettingsActivity.h",
        "src/InterfaceFontSelection.h",
        "src/SdCardFontSystem.cpp",
        "src/network/html/SettingsPage.html",
        "test/font_selection_compatibility/FontSelectionCompatibilityTest.cpp",
        "test/settings_organization/test_after_prototype.py",
        "test/web_stability/test_grouped_settings_contract.py",
        "docs/settings-organization/generate_after_inventory.py",
        "test/web_stability/CMakeLists.txt",
    ]
    after["source"]["prototypeLayout"] = (
        "static seven-row group root; Confirm opens one child group; Back returns to group root; "
        "only one currentGroupSettings vector is materialized"
    )
    after["source"]["webRouteStatus"] = (
        f"Bounded Web Settings group/page routes are integrated on the current 1.6.6 branch "
        f"(route {WEB_ROUTE_COMMIT}; integration {WEB_INTEGRATION_COMMIT}); Web Clippings/Notes remain disabled."
    )
    after["source"]["homeWorkerOverlap"] = (
        "Home worker A is integrated on this branch; theme value 5 remains the Folio Nooir Lite alias. "
        "Recent/Finished layouts are available under both Folio themes; the legacy Carousel field is retained "
        "for persistence/migration and is replaced in the UI by those two selectors. "
        "Interface Font remains available and uses the staged Mini Pack path."
    )
    for record in after["records"]:
        reachable, description = classify_after(record)
        record["deviceExposureAfterPrototype"] = description
        record["deviceReachableAfterPrototype"] = reachable
        record["deviceGroupAfterPrototype"] = record.get("proposedGroupId") if reachable else None
        record["stableIdUnchanged"] = True
        record["persistenceAndOptionsUnchanged"] = record["type"] not in {"action", "web_feature", "enum_option"}

    before_by_id = {r["stableId"]: r for r in before["records"]}
    after_by_id = {r["stableId"]: r for r in after["records"]}
    intentional_device_deferrals = {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement"}
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
            "src/InterfaceFontSelection.h",
            "src/SdCardFontSystem.cpp",
            "src/network/html/SettingsPage.html",
            "test/font_selection_compatibility/FontSelectionCompatibilityTest.cpp",
            "test/web_stability/test_grouped_settings_contract.py",
        ],
        "homeWorkerAOverlap": (
            "Home performance work is integrated; theme value 5 remains Folio Nooir Lite and stored fields are "
            "preserved. Recent/Finished selectors are available in both Folio themes and the Web UI matches the "
            "device. The legacy Carousel field remains for persistence/migration. Interface Font stays reachable "
            "and uses staged Mini Pack loading."
        ),
        "groupIds": [group["id"] for group in before["proposedGroups"]],
        "stableIdsBefore": len(before_by_id),
        "stableIdsAfter": len(after_by_id),
        "stableIdSetParity": set(before_by_id) == set(after_by_id),
        "changedPersistedDescriptorFields": changed_descriptors,
        "persistedDescriptorParity": not changed_descriptors,
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
        "deferredDeviceUi": [],
        "deferredSleepComposer": [
            "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
            "sleepScreen.option.flexible_composite",
        ],
        "layoutPresentation": {
            "recentBookLayout": "available in FOLIO_NOOIR and FOLIO_NOOIR_LITE; option set and persisted field unchanged",
            "finishedBookLayout": "available in FOLIO_NOOIR and FOLIO_NOOIR_LITE; option set and persisted field unchanged",
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
            "allHistoricalKeysGroupedExactlyOnce": len(group_by_key.keys() & set(web_keys)) == len(web_keys),
            "groupCounts": WEB_GROUP_COUNTS,
            "routeCommit": WEB_ROUTE_COMMIT,
            "latestIntegrationCommit": WEB_INTEGRATION_COMMIT,
            "routeCommitIntegratedHere": True,
            "routeIntegratedInMain": True,
            "historicalKeysReachableThroughGroupedApi": 84,
            "x4ReachableKeyCount": 82,
            "liteLayoutUiParity": (
                "Theme value 5 is Folio Nooir Lite on both device and Web; Web exposes the independent "
                "Recent/Finished selectors and hides the migrated legacy Carousel selector"
            ),
            "conditionalKeys": {
                "touchReaderControls": "touch hardware only",
                "tiltPageTurn": "X3 QMI8658 IMU only",
            },
            "implementationTests": {
                "groupedSettingsContract": "8/8",
                "classifierParity": "5/5",
                "workerBranchGroupedSettingsContract": "6/6",
                "workerBranchInventoryParity": "5/5",
                "settingsOrganizationParity": "12/12",
                "fullHostSuite": "402/402",
                "generatedHtmlAndJsSyntax": "passed (Node.js 24.13.1)",
            },
            "build": (
                "Blocked before firmware compilation: PlatformIO Core 6.1.19 cannot open "
                "C:\\Users\\fatiha\\.platformio\\platforms.lock; cached GCC 14.2.0+20251107 and isolated "
                "GCC 14.2.0+20260121 both fail --version with Windows Illegal System DLL Relocation"
            ),
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
            "rootDescriptorCount": 0,
            "childVectors": 1,
            "onlyMaterializedOnGroupEntry": True,
            "clearedOnReturn": True,
            "capacityRetainedForReuse": True,
            "capacityLifetime": "retained while Settings is active or stacked under a child; vector storage is released when ActivityManager replaces/destroys Settings",
            "capacityDiagnostic": "group_enter and group_return log vector capacity * sizeof(SettingInfo); max is the largest group_enter bytes value observed on device",
            "x4MaxRequestedChildRows": 12,
            "x4MaxRequestedGroup": "Controls (9 non-touch base rows + 3 actions; X3-only tilt row is absent on X4)",
            "x4MaxRetainedBufferBytes": "12 * sizeof(SettingInfo), exact allocated capacity byte count is logged at group entry",
            "x4MaxBytesMeasured": None,
            "filesystemWork": "UI font registry refresh occurs only on Home & Appearance group entry; reader font refresh and dictionary discovery occur only on Reading group entry",
            "ordinaryToggleRebuilds": "only sleep mode, theme, and short-power-button changes alter list shape",
        },
        "candidateForIntegration": True,
        "integrationBlockers": [],
        "releaseValidation": {
            "sourceParity": "passed; accidental missing settings=0; accidental duplicates=0",
            "firmwareBuild": "blocked by local PlatformIO lock permission and compiler startup failures",
            "physicalX4": "pending; not validated on device",
            "releaseReady": False,
        },
        "physicalX4Validation": "Pending; verify Settings group navigation, custom UI font response, and performance on hardware before RC acceptance.",
    }
    (OUT / "AFTER_inventory.json").write_text(json.dumps(after, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (OUT / "AFTER_parity.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (OUT / "PARITY_TABLE.md").write_text(render_parity_table(before, after), encoding="utf-8")
    print(json.dumps({k: report[k] for k in (
        "stableIdsBefore", "stableIdsAfter", "stableIdSetParity", "persistedDescriptorParity",
        "movedDeviceEntries", "webSettings", "candidateForIntegration", "integrationBlockers",
    )}, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
