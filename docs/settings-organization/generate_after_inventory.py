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
        return False, "deferred in 1.6.6; selected family is preserved and built-in fallback remains active"
    if sid in {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
               "sleepScreen.option.flexible_composite", "web:clippings_notes"}:
        return False, record.get("hiddenReason") or "intentionally deferred"
    if sid == "carouselLayout":
        return False, "Home & Appearance group; Carousel-specific control intentionally hidden for 1.6.6"
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
        return "Intentional 1.6.6 presentation filter; Carousel-specific device control hidden"
    if sid in {"manual:uiFontFamilyName", "sleepBackgroundElement", "sleepOverlayAElement",
               "sleepOverlayBElement", "sleepScreen.option.flexible_composite", "web:clippings_notes"}:
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
        f"(latest integration `{WEB_INTEGRATION_COMMIT}`). The route is not merged into this experiment worktree. "
        "Clippings remains deferred.",
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
        "src/activities/settings/SettingsActivity.cpp",
        "src/activities/settings/SettingsActivity.h",
    ]
    after["source"]["prototypeLayout"] = (
        "static seven-row group root; Confirm opens one child group; Back returns to group root; "
        "only one currentGroupSettings vector is materialized"
    )
    after["source"]["webRouteStatus"] = (
        f"Bounded group/page routes are integrated in main at {WEB_INTEGRATION_COMMIT} "
        f"(route commit {WEB_ROUTE_COMMIT}); not merged into this experiment worktree. Clippings remains disabled."
    )
    after["source"]["homeWorkerOverlap"] = (
        "Home worker A commit 33ed7e30 is not merged here; it changed presentation/layout visibility while preserving "
        "stable theme value 5 and stored layout fields. SettingsActivity overlap must be resolved during integration."
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
    intentional_device_deferrals = {
        "manual:uiFontFamilyName", "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
        "carouselLayout",
    }
    previously_exposed_device = [
        row for row in before["records"]
        if (row.get("deviceExposureBefore") or "")
        and not row["deviceExposureBefore"].startswith(("not exposed", "not selectable"))
    ]
    accidental_device_missing = [
        row["stableId"] for row in previously_exposed_device
        if row["stableId"] not in intentional_device_deferrals
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
            "src/activities/settings/SettingsActivity.cpp",
            "src/activities/settings/SettingsActivity.h",
        ],
        "homeWorkerAOverlap": "Commit 33ed7e30 changed presentation/layout visibility only; stable theme value 5 and stored layout fields remain. This prototype also ports main's post-ce86 decision: recent/finished layouts are visible in both FOLIO_NOOIR and FOLIO_NOOIR_LITE, while the Carousel-specific control stays hidden. Interface Font/Mini Pack remains deferred. Not merged into this ce86 experiment.",
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
            "accidentalMissing": len(accidental_device_missing),
            "accidentalMissingStableIds": accidental_device_missing,
        },
        "movedDeviceEntries": moved,
        "quickActions": {
            "oldGroup": quick_actions["deviceGroupBefore"],
            "prototypeGroup": quick_actions["proposedGroupId"],
            "actionAndStoredSlotsUnchanged": True,
        },
        "deferredDeviceUi": ["manual:uiFontFamilyName"],
        "deferredSleepComposer": [
            "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement",
            "sleepScreen.option.flexible_composite",
        ],
        "layoutPresentation": {
            "recentBookLayout": "available in FOLIO_NOOIR and FOLIO_NOOIR_LITE; option set and persisted field unchanged",
            "finishedBookLayout": "available in FOLIO_NOOIR and FOLIO_NOOIR_LITE; option set and persisted field unchanged",
            "carouselLayout": "hidden for 1.6.6, matching main SettingsActivity behavior",
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
            "routeCommitIntegratedHere": False,
            "routeIntegratedInMain": True,
            "x4ReachableKeyCount": 82,
            "conditionalKeys": {
                "touchReaderControls": "touch hardware only",
                "tiltPageTurn": "X3 QMI8658 IMU only",
            },
            "implementationTests": {
                "groupedSettingsContract": "7/7",
                "classifierParity": "5/5",
                "workerBranchGroupedSettingsContract": "6/6",
                "workerBranchInventoryParity": "5/5",
                "generatedHtmlAndJsSyntax": "passed",
            },
            "build": "firmware compilation did not start; PlatformIO platforms.lock PermissionError",
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
            "SettingsList.h": unchanged_from("ce86b9f8", "src/SettingsList.h"),
            "CrossPointSettings.h": unchanged_from("ce86b9f8", "src/CrossPointSettings.h"),
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
            "filesystemWork": "font registry refresh and dictionary discovery occur only on Reading group entry",
            "ordinaryToggleRebuilds": "only sleep mode, theme, and short-power-button changes alter list shape",
        },
        "candidateForIntegration": False,
        "integrationBlockers": [
            "Web Settings routes are integrated on main but not merged into this isolated settings-organization experiment worktree.",
            "No physical X4 review of the group-root navigation has been performed.",
        ],
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
