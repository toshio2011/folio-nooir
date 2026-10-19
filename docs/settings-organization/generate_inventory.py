#!/usr/bin/env python3
"""Generate a source-derived inventory for the 1.6.6 settings organization experiment.

Run from the repository root. The output is documentary; it does not compile or
modify firmware sources.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "settings-organization"
# Keep the BEFORE device inventory pinned to the exact frozen experiment
# source even while SettingsActivity is being prototyped in this worktree.
DEVICE_SOURCE_REVISION = "ce86b9f8"
GROUPS = [
    {"id": "home_appearance", "label": "Home & Appearance", "labelId": "STR_CAT_DISPLAY"},
    {"id": "sleep", "label": "Sleep", "labelId": "STR_SLEEP_SCREEN"},
    {"id": "reading", "label": "Reading", "labelId": "STR_CAT_READER"},
    {"id": "controls", "label": "Controls", "labelId": "STR_CAT_CONTROLS"},
    {"id": "connectivity", "label": "Connectivity", "labelId": "STR_WIFI_NETWORKS"},
    {"id": "library", "label": "Library", "labelId": "STR_LIBRARY"},
    {"id": "system", "label": "System", "labelId": "STR_CAT_SYSTEM"},
]


def git(*args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8").strip()


def read(path: str, revision: str | None = None) -> str:
    if revision:
        return git("show", f"{revision}:{path}")
    return (ROOT / path).read_text(encoding="utf-8")


def matching_close(text: str, open_at: int, opening: str, closing: str) -> int:
    depth = 0
    i = open_at
    quote = None
    line_comment = False
    block_comment = False
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if ch == "\n":
                line_comment = False
        elif block_comment:
            if ch == "*" and nxt == "/":
                block_comment = False
                i += 1
        elif quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif ch == "/" and nxt == "/":
            line_comment = True
            i += 1
        elif ch == "/" and nxt == "*":
            block_comment = True
            i += 1
        elif ch in ('"', "'"):
            quote = ch
        elif ch == opening:
            depth += 1
        elif ch == closing:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError(f"unclosed {opening} at {open_at}")


def split_args(text: str) -> list[str]:
    args: list[str] = []
    start = 0
    depths = {"(": 0, "{": 0, "[": 0}
    quote = None
    line_comment = False
    block_comment = False
    i = 0
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if ch == "\n":
                line_comment = False
        elif block_comment:
            if ch == "*" and nxt == "/":
                block_comment = False
                i += 1
        elif quote:
            if ch == "\\":
                i += 1
            elif ch == quote:
                quote = None
        elif ch == "/" and nxt == "/":
            line_comment = True
            i += 1
        elif ch == "/" and nxt == "*":
            block_comment = True
            i += 1
        elif ch in ('"', "'"):
            quote = ch
        elif ch in depths:
            depths[ch] += 1
        elif ch == ")":
            depths["("] -= 1
        elif ch == "}":
            depths["{"] -= 1
        elif ch == "]":
            depths["["] -= 1
        elif ch == "," and not any(depths.values()):
            args.append(text[start:i].strip())
            start = i + 1
        i += 1
    tail = text[start:].strip()
    if tail:
        args.append(tail)
    return args


def calls(source: str, start: int = 0, end: int | None = None):
    end = len(source) if end is None else end
    pos = start
    while True:
        pos = source.find("SettingInfo::", pos, end)
        if pos < 0:
            return
        method_match = re.match(r"SettingInfo::(\w+)\s*\(", source[pos:end])
        if not method_match:
            pos += len("SettingInfo::")
            continue
        method = method_match.group(1)
        open_at = pos + method_match.end() - 1
        close_at = matching_close(source, open_at, "(", ")")
        yield method, source[pos:close_at + 1], split_args(source[open_at + 1:close_at]), pos
        pos = close_at + 1


def sym(expr: str | None) -> str | None:
    if expr is None:
        return None
    m = re.search(r"StrId::(\w+)", expr)
    return m.group(1) if m else None


def quoted(expr: str | None) -> str | None:
    if expr is None:
        return None
    m = re.search(r'"([^"\\]*(?:\\.[^"\\]*)*)"', expr)
    return m.group(1) if m else None


def field_ref(expr: str | None) -> str | None:
    if expr is None:
        return None
    m = re.search(r"&CrossPointSettings::(\w+)", expr)
    if m:
        return m.group(1)
    m = re.search(r"&SETTINGS\.(\w+)", expr)
    return m.group(1) if m else None


def enum_ids(expr: str | None) -> list[str]:
    if not expr:
        return []
    return re.findall(r"StrId::(\w+)", expr)


def source_line(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def collect_enum_values(cross: str, sleep_mode: str) -> dict[str, int]:
    values: dict[str, int] = {}
    enum_re = re.compile(r"enum(?:\s+\w+)?\s*\{(.*?)\};", re.S)
    for match in enum_re.finditer(cross):
        current = -1
        for part in match.group(1).split(","):
            part = re.sub(r"//.*", "", part).strip()
            m = re.match(r"(\w+)\s*(?:=\s*([^,]+))?", part)
            if not m:
                continue
            name, expr = m.groups()
            if not name:
                continue
            if expr:
                expr = expr.strip()
                if expr.isdigit():
                    current = int(expr)
                elif expr in values:
                    current = values[expr]
                else:
                    current = current + 1
            else:
                current += 1
            values[name] = current
    for name, raw in re.findall(r"\b([A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*,", sleep_mode):
        values[name] = int(raw)
    # Appended persisted sleep mode values from SleepScreenMode.h.
    for name, raw in re.findall(r"\b([A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*,", sleep_mode):
        values[f"SleepScreenModeValue::{name}"] = int(raw)
    return values


def field_defaults(cross: str) -> dict[str, str]:
    defaults: dict[str, str] = {}
    pattern = re.compile(r"\b(?:uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|bool|char)\s+(\w+)(?:\[[^]]+\])?\s*=\s*([^;]+);", re.M)
    for m in pattern.finditer(cross):
        defaults[m.group(1)] = m.group(2).strip()
    for name, value in re.findall(r"static constexpr (?:uint8_t|uint16_t|uint32_t|int)\s+(\w+)\s*=\s*([^;]+);", cross):
        defaults.setdefault(name, value.strip())
    return defaults


def resolve_default(field: str | None, defaults: dict[str, str], values: dict[str, int]) -> dict:
    if not field:
        return {"expression": None, "resolved": None}
    expr = defaults.get(field)
    if expr is None:
        return {"expression": "external/dynamic or manually persisted", "resolved": None}
    simple = expr.strip()
    if simple.startswith('"') and simple.endswith('"'):
        return {"expression": simple, "resolved": simple[1:-1]}
    if simple in values:
        return {"expression": simple, "resolved": values[simple]}
    if simple in defaults and defaults[simple] != expr:
        nested = defaults[simple].strip()
        if nested.isdigit():
            return {"expression": f"{simple} ({nested})", "resolved": int(nested)}
        if nested in values:
            return {"expression": f"{simple} ({nested})", "resolved": values[nested]}
    cast_value = re.search(r"SleepComposerElement::(\w+)", simple)
    composer_values = {"None": 0, "Dark": 1, "Light": 2, "Blank": 3, "NooirDefault": 4,
                       "CustomImage": 5, "CurrentBookCover": 6, "CurrentPage": 7, "TransparentPng": 8,
                       "BookTitleAuthor": 9, "BookProgress": 10, "BookStats": 11, "ReadingCalendar": 12,
                       "ReadingSummary": 13, "ToDo": 14, "ClippingQuote": 15, "ClockDate": 16, "Random": 17}
    if cast_value and cast_value.group(1) in composer_values:
        return {"expression": simple, "resolved": composer_values[cast_value.group(1)]}
    if simple.isdigit():
        return {"expression": simple, "resolved": int(simple)}
    if re.fullmatch(r"\d+U?", simple):
        return {"expression": simple, "resolved": int(simple.rstrip("U"))}
    return {"expression": simple, "resolved": None}


def category(sym_id: str | None) -> str:
    if sym_id == "STR_CAT_DISPLAY": return "Display"
    if sym_id == "STR_CAT_READER": return "Reader"
    if sym_id == "STR_CAT_CONTROLS": return "Controls"
    if sym_id == "STR_CAT_SYSTEM": return "System"
    if sym_id == "STR_KOREADER_SYNC": return "KOReader Sync (web-only)"
    if sym_id == "STR_CUSTOMISE_STATUS_BAR": return "Customize Status Bar (web-only)"
    return "Uncategorized"


def proposed_group(record: dict) -> str:
    sid = record.get("stableId", "")
    key = record.get("key") or sid
    action = record.get("action") or ""
    category_id = record.get("categoryId") or ""
    if sid in {"sleepScreen.option.flexible_composite", "web:clippings_notes"}:
        return "sleep" if sid.startswith("sleepScreen") else "library"
    if key in {"sleepScreen", "randomSleepOrder", "action:RandomSleepPool", "sleepScreenCoverMode",
               "sleepScreenCoverFilter", "todoSleepMode", "quickResumeSleepScreen", "sleepTimeoutMinutes",
               "sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement"}:
        return "sleep"
    if action in {"RandomSleepPool", "SleepScreenPreview", "ClearFavoriteSleepImage"}:
        return "sleep"
    if action == "VisualPreview" or category_id == "STR_CAT_DISPLAY":
        return "home_appearance"
    if action == "QuickActions":
        return "controls"
    if category_id in {"STR_CAT_READER", "STR_CUSTOMISE_STATUS_BAR"} or sid in {"fontFamily", "fontSize", "dictionaryFontFamily", "dictionaryFontSize", "dictionaryName"}:
        return "reading"
    if category_id == "STR_CAT_CONTROLS" or sid.startswith("quickActionSlot") or sid.startswith("frontButton") or sid.startswith("readerFrontButton") or action in {"RemapFrontButtons", "RemapReaderFrontButtons", "QuickActions"}:
        return "controls"
    if category_id == "STR_KOREADER_SYNC" or key in {"bluetoothEnabled", "bluetoothNoDeviceTimeoutSeconds", "bleControllerPreset", "bleKeyMap"} or action in {"Network", "Bluetooth", "KOReaderSync", "OPDSBrowser"}:
        return "connectivity"
    if key in {"opdsDownloadFolder", "opdsFilenameFormat", "showHiddenFiles", "removeReadBooksFromRecents", "moveFinishedToReadFolder"} or action == "ToDoList":
        return "library"
    if category_id == "STR_CAT_SYSTEM" or category_id == "STR_NONE_OPT" or action in {"ClockWeatherSync", "SettingsProfiles", "ClearCache", "CheckForUpdates", "SdFirmwareUpdate", "Language"} or sid == "language":
        return "system"
    return "system"


def enrich_labels_and_groups(records: list[dict]) -> None:
    english = read("lib/I18n/translations/english.yaml", DEVICE_SOURCE_REVISION)
    labels = {key: value.replace('\\"', '"') for key, value in re.findall(r'^([A-Z0-9_]+):\s*"((?:[^"\\]|\\.)*)"', english, re.M)}
    for record in records:
        label_id = record.get("label")
        if label_id:
            record["englishLabel"] = labels.get(label_id, label_id)
        for option in record.get("options", []):
            option_label = option.get("label")
            if isinstance(option_label, str) and option_label.startswith("STR_"):
                option["englishLabel"] = labels.get(option_label, option_label)
            elif isinstance(option_label, str):
                option["englishLabel"] = option_label
        record["proposedGroupId"] = proposed_group(record)


def stable_id(key: str | None, name_id: str | None, action: str | None) -> str:
    if key:
        return key
    if action:
        return f"action:{action}"
    if name_id == "STR_INTERFACE_FONT":
        return "manual:uiFontFamilyName"
    return f"label:{name_id or 'UNKNOWN'}"


def setting_record(method, args, line, cross, defaults, values, source_file, setting_source):
    name = sym(args[0]) if args else None
    setting_type = method.lower().replace("dynamic", "").replace("enum", "enum")
    field = None
    key = None
    cat = None
    options: list[dict] = []
    value_range = None
    action = None
    external = None

    if method in ("Enum", "Toggle", "Value", "String"):
        field = field_ref(args[1] if len(args) > 1 else None)
        if method == "String":
            key = quoted(args[3] if len(args) > 3 else None)
            cat = sym(args[4]) if len(args) > 4 else None
        elif method == "Value":
            key = quoted(args[3] if len(args) > 3 else None)
            cat = sym(args[4]) if len(args) > 4 else None
            rm = re.search(r"\{([^{}]+)\}", args[2] if len(args) > 2 else "")
            if rm:
                value_range = [p.strip() for p in rm.group(1).split(",")]
        elif method == "Enum":
            key = quoted(args[3] if len(args) > 3 else None)
            cat = sym(args[4]) if len(args) > 4 else None
            options = [{"label": item, "optionIndex": i, "persistedValue": i} for i, item in enumerate(enum_ids(args[2] if len(args) > 2 else None))]
        else:
            key = quoted(args[2] if len(args) > 2 else None)
            cat = sym(args[3]) if len(args) > 3 else None
            options = [{"label": "Off", "optionIndex": 0, "persistedValue": 0},
                       {"label": "On", "optionIndex": 1, "persistedValue": 1}]
    elif method == "Action":
        action = re.search(r"SettingAction::(\w+)", args[1] if len(args) > 1 else "")
        action = action.group(1) if action else None
        cat = sym(args[2]) if len(args) > 2 else None
        setting_type = "action"
    elif method == "DynamicEnum":
        key = quoted(args[4] if len(args) > 4 else None)
        cat = sym(args[5]) if len(args) > 5 else None
        options = [{"label": item, "optionIndex": i, "persistedValue": i} for i, item in enumerate(enum_ids(args[1] if len(args) > 1 else None))]
        external = "dynamic getter/setter; inspect owner for stored field"
    elif method == "DynamicString":
        key = quoted(args[3] if len(args) > 3 else None)
        cat = sym(args[4]) if len(args) > 4 else None
        external = "KOReaderCredentialStore dynamic getter/setter"
        setting_type = "string"
    else:
        external = "unparsed constructor"

    # Dynamic replacements used by getSettingsList() have more exact storage/options.
    default_field = field
    if name == "STR_FONT_FAMILY":
        field = "fontFamily + sdFontFamilyName"
        external = "Noto Serif plus installed SD families accepted by registry; legacy Noto Sans stored value is hidden"
        options = [{"label": "STR_NOTO_SERIF", "optionIndex": 0, "persistedValue": 0}]
        options.append({"label": "<installed compatible reader family>", "optionIndex": "1..N", "persistedValue": "stable family name in sdFontFamilyName"})
    elif name == "STR_INTERFACE_FONT":
        field = "uiFontFamilyName"
        external = "Default plus complete compatible 8/10/12pt installed families; stored by name in uiFontFamilyName; Worker E defers custom font application in 1.6.6"
        options = [{"label": "STR_DEFAULT_VALUE", "optionIndex": 0, "persistedValue": "empty string"},
                   {"label": "<installed compatible UI family>", "optionIndex": "1..N", "persistedValue": "stable family name"}]
    elif name == "STR_DICTIONARY_FONT":
        field = "dictionaryFontFamily"
        external = "legacy Noto Sans is hidden; getter/setter exposes Use Reader and Noto Serif only"
        options = [{"label": "STR_USE_READER_FONT", "optionIndex": 0, "persistedValue": 0},
                   {"label": "STR_NOTO_SERIF", "optionIndex": 1, "persistedValue": 1}]
    elif name == "STR_DICTIONARY":
        field = "dictionaryName"
        external = "None plus discovered /dictionaries folders; manually persisted by CrossPointSettings"
        options = [{"label": "STR_NONE_OPT", "optionIndex": 0, "persistedValue": "empty string"},
                   {"label": "<discovered dictionary folder>", "optionIndex": "1..N", "persistedValue": "folder name"}]
    elif name == "STR_SLEEP_SCREEN":
        field = "sleepScreen"
        external = "configureSleepSetting remaps compact option indexes to stable persisted SleepScreenModeValue; Flexible=17 is intentionally not selectable"
        sleep_values = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
        sleep_labels = ["STR_DARK", "STR_LIGHT", "STR_CUSTOM", "STR_COVER", "STR_NONE_OPT", "STR_QUICK_RESUME",
                        "STR_PAGE_OVERLAY", "Cover + Overlay", "Reading Stats", "Minimal Stats", "Clipping + Cover",
                        "STR_TODO_LIST", "STR_READING_CALENDAR_SLEEP", "STR_READING_SUMMARY_SLEEP",
                        "STR_SLEEP_COVER_PROGRESS", "STR_SLEEP_RANDOM"]
        options = [{"label": label, "optionIndex": i, "persistedValue": value}
                   for i, (label, value) in enumerate(zip(sleep_labels, sleep_values))]
    if method == "Value" and value_range:
        pass
    default = resolve_default(default_field or field, defaults, values)
    if name == "STR_FONT_FAMILY":
        default = {"expression": "NOTOSERIF; empty sdFontFamilyName", "resolved": {"fontFamily": 0, "sdFontFamilyName": ""}}
    elif name == "STR_INTERFACE_FONT":
        default = {"expression": "empty uiFontFamilyName", "resolved": "Default (built-in)"}
    elif name == "STR_DICTIONARY":
        default = {"expression": "empty dictionaryName", "resolved": "None"}
    elif (name and name.startswith("STR_KOREADER_")) or name in {"STR_SYNC_SERVER_URL", "STR_SYNC_DEVICE_NAME", "STR_DOCUMENT_MATCHING", "STR_SEND_METADATA", "STR_SYNC_BEHAVIOR"}:
        defaults_by_id = {
            "STR_KOREADER_USERNAME": ("empty username", ""),
            "STR_KOREADER_PASSWORD": ("empty password", ""),
            "STR_SYNC_SERVER_URL": ("empty serverUrl", ""),
            "STR_SYNC_DEVICE_NAME": ('deviceName = "Folio Nooir X4"', "Folio Nooir X4"),
            "STR_DOCUMENT_MATCHING": ("DocumentMatchMethod::FILENAME", 0),
            "STR_SEND_METADATA": ("sendMetadata = false", 0),
            "STR_SYNC_BEHAVIOR": ("KOReaderSyncBehavior::SMART", 1),
        }
        if name in defaults_by_id:
            default = {"expression": defaults_by_id[name][0], "resolved": defaults_by_id[name][1]}
    return {
        "stableId": stable_id(key, name, action),
        "key": key,
        "label": name,
        "category": category(cat),
        "categoryId": cat,
        "type": setting_type,
        "backingField": field or external,
        "options": options,
        "rangeExpressions": value_range,
        "default": default,
        "source": {"file": source_file, "line": line, "constructor": method},
        "sourceScope": setting_source,
    }


def annotate(record: dict) -> None:
    sid = record["stableId"]
    label = record.get("label") or ""
    cat = record.get("category")
    key = record.get("key")
    device = "device"
    hidden = False
    reason = None
    if sid in {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement"}:
        device, hidden, reason = "intentionally hidden from normal Settings; deferred Flexible Sleep composer", True, "composer settings withheld for 1.6.6"
    elif sid == "manual:uiFontFamilyName":
        device, hidden, reason = "source row currently visible; approved Worker E 1.6.6 device-UI deferral pending row-hide change", True, "selection preserved; built-in UI fallback active"
    elif sid == "dictionaryName":
        device, hidden, reason = "conditional: Text Settings > Dictionary selector when dictionary folders are discovered", False, None
    elif sid in {"carouselLayout"}:
        device, hidden, reason = "conditional: shown only when Carousel theme is selected", True, "theme-specific setting"
    elif sid in {"recentBookLayout", "finishedBookLayout"}:
        device, hidden, reason = "conditional: shown only when Folio Nooir theme is selected", True, "theme-specific setting"
    elif sid in {"randomSleepOrder", "action:RandomSleepPool"}:
        device, hidden, reason = "conditional: shown only when Sleep Screen is Random", True, "random-only control"
    elif sid in {"coverProgressStyle", "featuredShowCover", "featuredShowFileType", "featuredShowAuthor", "featuredShowSynopsis", "featuredShowStatus", "featuredShowProgress", "featuredShowStartedDate", "featuredShowFinishedDate", "featuredShowReadingTime", "featuredShowSessions", "spineDecorationStyle", "spineAppearanceMode"}:
        device, hidden, reason = "Visual Preview sub-screen", True, "omitted from flat settings list; edited through Visual Preview"
    elif not key and record["type"] != "action" and sid != "manual:uiFontFamilyName":
        device, hidden, reason = "not exposed", True, "no device setting row"
    elif record["type"] == "action":
        device = "conditional/action row"
        if record.get("action") == "ClearFavoriteSleepImage":
            device, hidden, reason = "conditional: configured favorite sleep image exists", True, "state-dependent action"
    elif record.get("sourceScope") == "text_settings":
        device, hidden, reason = "Text Settings sub-screen", False, None
    elif record.get("categoryId") == "STR_KOREADER_SYNC":
        device, hidden, reason = "KOReader Sync sub-screen opened by the device-only KOReader Sync action", False, None
    elif record.get("categoryId") == "STR_CUSTOMISE_STATUS_BAR":
        device, hidden, reason = "Status Bar sub-screen opened by the device-only Customize Status Bar action", False, None
    elif cat == "Uncategorized":
        device, hidden, reason = "not exposed in device Settings; edited from OPDS UI or Web Settings", True, "category-less descriptor"
    elif sid == "touchReaderControls":
        device = "conditional: BoardConfig::hasTouch()"
    elif sid in {"frontButtonFollowOrientation", "fadingFix"}:
        device = "conditional: hidden on touch devices"
    elif sid == "tiltPageTurn":
        device = "conditional: inserted only when QMI8658 tilt sensor is available (X3)"

    web_before = "yes: GET/POST /api/settings" if key and record["type"] != "action" else "no"
    if sid in {"sleepBackgroundElement", "sleepOverlayAElement", "sleepOverlayBElement"}:
        web_before = "no: skipped by old Web GET; POST loop skipped these composer settings"
    if sid == "manual:uiFontFamilyName":
        web_before = "no: no API key; old GET/POST skipped Interface Font"
    if sid == "touchReaderControls":
        web_before = "conditional: only when BoardConfig::hasTouch()"
    if sid in {"frontButtonFollowOrientation", "fadingFix"}:
        web_before = "conditional: omitted on touch devices"
    record["deviceExposureBefore"] = device
    old_group_by_category = {
        "Display": "home_appearance",
        "Reader": "reading",
        "Controls": "controls",
        "System": "system",
    }
    record["deviceGroupBefore"] = old_group_by_category.get(cat)
    record["webExposureBeforeDisabledDecision"] = web_before
    record["currentWebSettingsRoute"] = f"disabled/not registered in source snapshot {DEVICE_SOURCE_REVISION[:8]}" if record["type"] != "action" else "n/a"
    record["intentionallyHidden"] = hidden
    record["hiddenReason"] = reason


def make_inventory(web_revision: str | None, label: str) -> dict:
    inventory_name = label
    # Device descriptors/defaults/action exposure come from the frozen source,
    # not this worktree's in-progress SettingsActivity experiment. Only the
    # Web routing source is read historically for the pre-deferral baseline.
    settings = read("src/SettingsList.h", DEVICE_SOURCE_REVISION)
    cross = read("src/CrossPointSettings.h", DEVICE_SOURCE_REVISION)
    sleep_path_exists = subprocess.run(
        ["git", "cat-file", "-e", f"{DEVICE_SOURCE_REVISION}:src/util/SleepComposer.h"],
        cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
    ).returncode == 0
    sleep = read("src/util/SleepComposer.h", DEVICE_SOURCE_REVISION) if sleep_path_exists else ""
    activity = read("src/activities/settings/SettingsActivity.cpp", DEVICE_SOURCE_REVISION)
    web = read("src/network/CrossPointWebServer.cpp", web_revision)
    start = settings.index("std::vector<SettingInfo> v = {")
    list_end = settings.index("    return v;", start)
    defaults = field_defaults(cross)
    values = collect_enum_values(cross, sleep)

    records: list[dict] = []
    text_ranges = [(m.start(), m.end()) for m in re.finditer(r"\.withTextSettings\(\)", settings)]
    for method, call, args, pos in calls(settings, start, list_end):
        if method not in {"Enum", "Toggle", "Value", "String", "Action", "DynamicEnum", "DynamicString"}:
            continue
        source_line_no = source_line(settings, pos)
        # In base list, `inTextSettings` is a chained modifier after the call.
        scope = "text_settings" if any(pos <= a <= pos + len(call) + 80 for a, _ in text_ranges) else "base_list"
        rec = setting_record(method, args, source_line_no, cross, defaults, values, "src/SettingsList.h", scope)
        if rec["stableId"] == "manual:uiFontFamilyName":
            rec["key"] = None
        rec["action"] = re.search(r"SettingAction::(\w+)", call).group(1) if "SettingAction::" in call else None
        # Category-less OPDS settings remain Web-exposed, not a device row.
        annotate(rec)
        # Numeric enum choices preserve API option indexes unless overridden by the sleep special case.
        if rec["type"] == "value" and rec.get("rangeExpressions"):
            range_ids = {m.group(1) for m in re.finditer(r"CrossPointSettings::(\w+)", " ".join(rec["rangeExpressions"]))}
            resolved = []
            for expression in rec["rangeExpressions"]:
                expr = expression.strip()
                m = re.match(r"(?:CrossPointSettings::)?(\w+)$", expr)
                if m and m.group(1) in values:
                    resolved.append(values[m.group(1)])
                elif m and re.search(rf"static constexpr [^;]*\b{m.group(1)}\s*=\s*(\d+)", cross):
                    resolved.append(int(re.search(rf"static constexpr [^;]*\b{m.group(1)}\s*=\s*(\d+)", cross).group(1)))
                elif expr.isdigit():
                    resolved.append(int(expr))
                else:
                    m2 = re.search(rf"\b{re.escape(expr.split('::')[-1])}\s*=\s*(\d+)", cross)
                    resolved.append(int(m2.group(1)) if m2 else None)
            rec["range"] = {"min": resolved[0], "max": resolved[1], "step": resolved[2], "sourceExpressions": rec["rangeExpressions"]}
            del rec["rangeExpressions"]
        records.append(rec)

    # Device-only actions assembled after base descriptor filtering.
    action_start = activity.index("// Device-only display action")
    action_end = activity.index("// Keep the four tabs", action_start)
    action_slice = activity[action_start:action_end]
    inferred_cat = {
        "displaySettings": "STR_CAT_DISPLAY", "readerSettings": "STR_CAT_READER",
        "controlsSettings": "STR_CAT_CONTROLS", "systemSettings": "STR_CAT_SYSTEM",
    }
    action_pattern = re.compile(r"SettingInfo::Action\(StrId::(\w+)\s*,\s*SettingAction::(\w+)(?:\s*,\s*StrId::(\w+))?", re.S)
    seen_action_ids = {r["stableId"] for r in records if r["type"] == "action"}
    for match in action_pattern.finditer(action_slice):
        name, action_name, category_id = match.groups()
        sid = f"action:{action_name}"
        if sid in seen_action_ids:
            continue
        prefix = action_slice[max(0, match.start() - 160):match.start()]
        target = next((name for name in inferred_cat if name in prefix[-120:]), None)
        cat = category_id or inferred_cat.get(target or "", "STR_NONE_OPT")
        rec = {
            "stableId": sid, "key": None, "label": name, "category": category(cat), "categoryId": cat,
            "type": "action", "backingField": None, "options": [], "range": None,
            "default": {"expression": None, "resolved": None},
            "action": action_name, "source": {"file": "src/activities/settings/SettingsActivity.cpp",
            "line": source_line(activity, activity.index(match.group(0), action_start)), "constructor": "Action"},
            "sourceScope": "device_action",
        }
        annotate(rec)
        records.append(rec)
        seen_action_ids.add(sid)

    # Settings managed by device sub-activities or manual JSON persistence are
    # not represented as scalar rows in SettingsList.h, but are reachable from
    # actions above and must remain in a complete device inventory.
    manual_rows = [
        ("language", "STR_LANGUAGE", "enum", "language", "System", "LanguageSelectActivity; language index/code", [],
         {"expression": "0 (English)", "resolved": 0}, "action:Language"),
        ("frontButtonBack", "STR_BACK", "enum", "frontButtonBack", "Controls", "ButtonRemapActivity global mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_BACK", "resolved": 0}, "action:RemapFrontButtons"),
        ("frontButtonConfirm", "STR_CONFIRM", "enum", "frontButtonConfirm", "Controls", "ButtonRemapActivity global mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_CONFIRM", "resolved": 1}, "action:RemapFrontButtons"),
        ("frontButtonLeft", "STR_DIR_LEFT", "enum", "frontButtonLeft", "Controls", "ButtonRemapActivity global mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_LEFT", "resolved": 2}, "action:RemapFrontButtons"),
        ("frontButtonRight", "STR_DIR_RIGHT", "enum", "frontButtonRight", "Controls", "ButtonRemapActivity global mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_RIGHT", "resolved": 3}, "action:RemapFrontButtons"),
        ("readerFrontButtonBack", "STR_BACK", "enum", "readerFrontButtonBack", "Controls", "ButtonRemapActivity reader mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_BACK", "resolved": 0}, "action:RemapReaderFrontButtons"),
        ("readerFrontButtonConfirm", "STR_CONFIRM", "enum", "readerFrontButtonConfirm", "Controls", "ButtonRemapActivity reader mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_CONFIRM", "resolved": 1}, "action:RemapReaderFrontButtons"),
        ("readerFrontButtonLeft", "STR_DIR_LEFT", "enum", "readerFrontButtonLeft", "Controls", "ButtonRemapActivity reader mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_LEFT", "resolved": 2}, "action:RemapReaderFrontButtons"),
        ("readerFrontButtonRight", "STR_DIR_RIGHT", "enum", "readerFrontButtonRight", "Controls", "ButtonRemapActivity reader mapping", ["FRONT_HW_BACK", "FRONT_HW_CONFIRM", "FRONT_HW_LEFT", "FRONT_HW_RIGHT"], {"expression": "FRONT_HW_RIGHT", "resolved": 3}, "action:RemapReaderFrontButtons"),
        ("bluetoothEnabled", "STR_BLUETOOTH", "toggle", "bluetoothEnabled", "System", "BluetoothSettingsActivity menu", ["Off", "On"], {"expression": "0", "resolved": 0}, "action:Bluetooth"),
        ("bluetoothNoDeviceTimeoutSeconds", "STR_BT_NO_DEVICE_TIMEOUT", "enum", "bluetoothNoDeviceTimeoutSeconds", "System", "BluetoothSettingsActivity menu", ["0=Never", "30=30 seconds", "60=60 seconds", "90=90 seconds"], {"expression": "BLE_NO_DEVICE_TIMEOUT_DEFAULT_SECONDS", "resolved": 60}, "action:Bluetooth"),
        ("bleControllerPreset", "STR_BT_PRESET_YISER", "enum", "bleControllerPreset", "System", "Bluetooth preset actions; generic or Yiser J6 ring", ["BLE_CONTROLLER_PRESET_GENERIC=0", "BLE_CONTROLLER_PRESET_YISER_J6_RING=1"], {"expression": "BLE_CONTROLLER_PRESET_GENERIC", "resolved": 0}, "action:Bluetooth"),
        ("bleKeyMap", "STR_BT_MAP_BUTTONS", "fixed_map", "bleKeyMap[10]", "System", "BluetoothSettingsActivity/BleButtonMapActivity; 10 fixed-capacity signature-to-button slots", ["empty/unassigned slot", "decoded key signature (up to 8 events)", "logical target Back/Confirm/Left/Right/Up/Down/PageBack/PageForward"], {"expression": "all entries empty/unassigned", "resolved": "empty"}, "action:Bluetooth"),
        ("quickActionSlot1", "STR_QUICK_ACTIONS", "enum", "quickActionSlot1", "Controls", "Quick Actions picker; 12 actions plus None", ["RefreshScreen=0", "ToggleDarkMode=1", "Lookup=2", "ReadingStats=3", "ToggleBookmark=4", "ReaderOptions=5", "Orientation=6", "Sync=7", "Screenshot=8", "Home=9", "Sleep=10", "ToggleBluetooth=11", "None=255"], {"expression": "ActionId::ToggleBookmark", "resolved": 4}, "action:QuickActions"),
        ("quickActionSlot2", "STR_QUICK_ACTIONS", "enum", "quickActionSlot2", "Controls", "Quick Actions picker; 12 actions plus None", ["RefreshScreen=0", "ToggleDarkMode=1", "Lookup=2", "ReadingStats=3", "ToggleBookmark=4", "ReaderOptions=5", "Orientation=6", "Sync=7", "Screenshot=8", "Home=9", "Sleep=10", "ToggleBluetooth=11", "None=255"], {"expression": "ActionId::Lookup", "resolved": 2}, "action:QuickActions"),
        ("quickActionSlot3", "STR_QUICK_ACTIONS", "enum", "quickActionSlot3", "Controls", "Quick Actions picker; 12 actions plus None", ["RefreshScreen=0", "ToggleDarkMode=1", "Lookup=2", "ReadingStats=3", "ToggleBookmark=4", "ReaderOptions=5", "Orientation=6", "Sync=7", "Screenshot=8", "Home=9", "Sleep=10", "ToggleBluetooth=11", "None=255"], {"expression": "ActionId::ToggleDarkMode", "resolved": 1}, "action:QuickActions"),
        ("quickActionSlot4", "STR_QUICK_ACTIONS", "enum", "quickActionSlot4", "Controls", "Quick Actions picker; 12 actions plus None", ["RefreshScreen=0", "ToggleDarkMode=1", "Lookup=2", "ReadingStats=3", "ToggleBookmark=4", "ReaderOptions=5", "Orientation=6", "Sync=7", "Screenshot=8", "Home=9", "Sleep=10", "ToggleBluetooth=11", "None=255"], {"expression": "ActionId::RefreshScreen", "resolved": 0}, "action:QuickActions"),
    ]
    for key, label, typ, backing, cat, exposure, option_labels, default, parent_action in manual_rows:
        options = []
        if typ == "enum":
            for idx, option in enumerate(option_labels):
                if key.startswith("quickActionSlot") and option.endswith("=255"):
                    persisted = 255
                elif re.match(r"^\d+=", option):
                    persisted = int(option.split("=", 1)[0])
                elif re.search(r"=\d+$", option):
                    persisted = int(option.rsplit("=", 1)[1])
                else:
                    persisted = idx
                options.append({"label": option, "optionIndex": idx, "persistedValue": persisted})
        elif typ == "toggle":
            options = [{"label": "Off", "optionIndex": 0, "persistedValue": 0},
                       {"label": "On", "optionIndex": 1, "persistedValue": 1}]
        elif typ == "fixed_map":
            options = [{"label": option} for option in option_labels]
        rec = {
            "stableId": key, "key": key, "label": label, "category": cat, "categoryId": None,
            "type": typ, "backingField": backing, "options": options, "range": None, "default": default,
            "action": parent_action, "source": {"file": "src/CrossPointSettings.h / device sub-activity", "line": None, "constructor": "manual user setting"},
            "sourceScope": "manual_device_setting", "deviceExposureBefore": exposure,
            "webExposureBeforeDisabledDecision": "no: not included by old generic /api/settings GET/POST",
            "currentWebSettingsRoute": f"n/a or disabled/not registered in source snapshot {DEVICE_SOURCE_REVISION[:8]}",
            "intentionallyHidden": False, "hiddenReason": None,
        }
        records.append(rec)

    # Dynamically discovered dictionaries only exist as a selector when at least one folder is present.
    if not any(r["stableId"] == "dictionaryName" for r in records):
        records.append({
            "stableId": "dictionaryName", "key": None, "label": "STR_DICTIONARY", "category": "Reader",
            "categoryId": "STR_CAT_READER", "type": "enum", "backingField": "dictionaryName",
            "options": [{"label": "STR_NONE_OPT", "optionIndex": 0, "persistedValue": "empty string"},
                        {"label": "<discovered dictionary folder>", "optionIndex": "1..N", "persistedValue": "folder name"}],
            "range": None, "default": {"expression": "empty dictionaryName", "resolved": "None"},
            "action": "action:DictionarySettings", "source": {"file": "src/SettingsList.h", "line": 161, "constructor": "buildDictionarySetting"},
            "sourceScope": "dynamic_device_setting", "deviceExposureBefore": "conditional: Text Settings > Dictionary selector when dictionary folders are discovered",
            "webExposureBeforeDisabledDecision": "no: no API key in SettingInfo",
            "currentWebSettingsRoute": f"disabled/not registered in source snapshot {DEVICE_SOURCE_REVISION[:8]}",
            "intentionallyHidden": False, "hiddenReason": None,
        })

    # Add the hidden persisted Flexible enum value as an explicit non-row item.
    if "FLEXIBLE_COMPOSITE" in sleep:
        records.append({
            "stableId": "sleepScreen.option.flexible_composite", "key": None, "label": "STR_FLEXIBLE_SLEEP",
            "category": "Display", "categoryId": "STR_CAT_DISPLAY", "type": "enum_option",
            "backingField": "sleepScreen", "options": [], "range": None,
            "enumValue": 17, "default": {"expression": None, "resolved": None},
            "source": {"file": "src/util/SleepComposer.h", "line": source_line(sleep, sleep.index("FLEXIBLE_COMPOSITE")), "constructor": "SleepScreenModeValue"},
            "deviceExposureBefore": "not selectable in normal Settings; composer preview route omitted while Flexible mode active",
            "webExposureBeforeDisabledDecision": "not selectable by Web Settings API (old API maps indices 0..15 only)",
            "currentWebSettingsRoute": f"disabled/not registered in source snapshot {DEVICE_SOURCE_REVISION[:8]}",
            "intentionallyHidden": True,
            "hiddenReason": "deferred 1.6.6 Flexible Sleep implementation preserved; stable persisted enum value retained",
        })
    # Include the Web Clippings/Notes route as an explicit deferred non-setting capability.
    records.append({
        "stableId": "web:clippings_notes", "key": None, "label": "Clippings / Highlights / Notes",
        "category": "Web (deferred)", "categoryId": None, "type": "web_feature",
        "backingField": "existing per-book ClipFile JSON; no second database", "options": [], "range": None,
        "default": {"expression": None, "resolved": None},
        "webExposureBeforeDisabledDecision": "registered before 4eb9b811; disabled in this RC",
        "currentWebSettingsRoute": f"disabled/unregistered in source snapshot {DEVICE_SOURCE_REVISION[:8]}",
        "intentionallyHidden": True,
        "hiddenReason": "Clippings/Notes routes remain explicitly deferred for physical X4 safety; /My Clippings.txt remains unchanged",
    })

    # Remove duplicated call rows only if parser sees an identical base descriptor; keep source-location distinctness for actions.
    dedup: dict[str, dict] = {}
    duplicates: list[str] = []
    for rec in records:
        sid = rec["stableId"]
        if sid in dedup:
            duplicates.append(sid)
            # Prefer the base descriptor with its key over a redundant synthetic action row.
            if dedup[sid].get("key") is None and rec.get("key") is not None:
                dedup[sid] = rec
        else:
            dedup[sid] = rec
    records = list(dedup.values())
    records.sort(key=lambda r: (r.get("category", ""), r.get("source", {}).get("line") or 0, r["stableId"]))
    enrich_labels_and_groups(records)

    # Old Web Settings exposure is reconstructed from the source revision before route deferral.
    old_routes = bool(re.search(r'server->on\("/settings",\s*HTTP_GET', web) and
                               re.search(r'server->on\("/api/settings",\s*HTTP_GET', web) and
                               re.search(r'server->on\("/api/settings",\s*HTTP_POST', web))
    old_keys = sorted(r["key"] for r in records if r.get("key") and r["type"] != "action" and
                      (str(r.get("webExposureBeforeDisabledDecision", "")).startswith("yes:") or
                       str(r.get("webExposureBeforeDisabledDecision", "")).startswith("conditional:")) and
                      not r["stableId"].startswith("sleepBackground") and
                      r["stableId"] not in {"sleepOverlayAElement", "sleepOverlayBElement"})
    old_x4_keys = [key for key in old_keys if key not in {"touchReaderControls", "tiltPageTurn"}]
    return {
        "schemaVersion": 1,
        "inventoryName": inventory_name,
        "source": {
            "integrationSnapshot": git("rev-parse", DEVICE_SOURCE_REVISION),
            "branchAtInventory": git("branch", "--show-current"),
            "worktreeHead": git("rev-parse", "HEAD"),
            "deviceSettingsSource": f"src/SettingsList.h + CrossPointSettings.h + SettingsActivity.cpp at {DEVICE_SOURCE_REVISION}",
            "priorWebSettingsSourceRevision": web_revision,
            "priorWebRouteExposureVerified": old_routes,
            "priorWebRouteExposureNote": "The pre-4eb source registers /settings GET and /api/settings GET+POST; GET can page with offset/limit but no query returns the full catalog.",
            "currentWebRouteExposure": "disabled: /settings and /api/settings are not registered after 4eb9b811; Clippings handler not registered",
            "webSettingsDeferralCommit": "4eb9b811af823344a36950725614f9ab3e341544",
            "clippingsDeferralCommit": "4eb9b811af823344a36950725614f9ab3e341544",
            "interfaceFontDecision": (
                "Custom Interface Font is reachable in the 1.6.5 baseline; the 1.6.6 RC hides its user-facing row, "
                "keeps the saved value, and uses the built-in UI font."
                if DEVICE_SOURCE_REVISION.startswith("3a66") else
                "Worker E commit ce86b9f8 disables custom UI font application and preserves selection; row is still present in this exact source and row-hide remains pending"
            ),
            "homeWorkerOverlap": (
                "This frozen inventory is sourced directly from the 1.6.5 baseline branch."
                if DEVICE_SOURCE_REVISION.startswith("3a66") else
                "Home worker A is integrated separately at 33ed7e30; it changed presentation/layout visibility, preserving stable theme value 5 and stored layout fields. This inventory remains frozen to ce86b9f8 and does not include or merge A; SettingsActivity overlap must be resolved during integration."
            ),
        },
        "summary": {
            "baseRecords": len([r for r in records if r.get("sourceScope") in {"base_list", "text_settings"}]),
            "manualDeviceSettings": len([r for r in records if r.get("sourceScope") == "manual_device_setting"]),
            "dynamicDeviceSettings": len([r for r in records if r.get("sourceScope") == "dynamic_device_setting"]),
            "deviceActions": len([r for r in records if r["type"] == "action"]),
            "proposedGroupCounts": {group["id"]: len([r for r in records if r.get("proposedGroupId") == group["id"]]) for group in GROUPS},
            "oldWebStableKeys": len(old_keys),
            "oldWebStableKeysList": old_keys,
            "oldWebX4StableKeys": len(old_x4_keys),
            "oldWebX4StableKeysList": old_x4_keys,
            "conditionalOldWebKeys": [
                {"key": "touchReaderControls", "condition": "BoardConfig::hasTouch()"},
                {"key": "tiltPageTurn", "condition": "QMI8658 tilt sensor available (X3)"},
            ],
            "intentionallyHiddenRecords": len([r for r in records if r.get("intentionallyHidden")]),
            "duplicateStableIdsInSourceExtract": duplicates,
        },
        "proposedGroups": GROUPS,
        "records": records,
    }


def main() -> None:
    global DEVICE_SOURCE_REVISION
    parser = argparse.ArgumentParser(description="Generate a source-derived device settings inventory.")
    parser.add_argument("--device-revision", default=DEVICE_SOURCE_REVISION)
    parser.add_argument("--web-revision", default="d4a97653")
    parser.add_argument("--output-stem", default="BEFORE")
    parser.add_argument(
        "--label",
        default="BEFORE settings organization (device source at ce86; Web exposure reconstructed at d4a97653)",
    )
    args = parser.parse_args()
    DEVICE_SOURCE_REVISION = args.device_revision
    OUT.mkdir(parents=True, exist_ok=True)
    before = make_inventory(args.web_revision, args.label)
    output_json = OUT / f"{args.output_stem}_inventory.json"
    output_markdown = OUT / f"{args.output_stem}_inventory.md"
    output_json.write_text(json.dumps(before, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    is_165_baseline = args.device_revision.startswith("3a66")
    title = "1.6.5 Device Settings Baseline" if is_165_baseline else "1.6.6 Settings Organization — Frozen Inventory"
    font_note = (
        "- The Interface Font row was available in this 1.6.5 baseline; the 1.6.6 RC hides the row, retains the saved value, and uses built-in UI fonts."
        if is_165_baseline else
        "- Custom Interface Font application is deferred for the 1.6.6 RC; this frozen descriptor inventory retains the row and persisted field for parity comparison."
    )
    source_note = (
        "- This file records the protected 1.6.5 device source; no baseline files were changed."
        if is_165_baseline else
        "- This frozen descriptor inventory is compared with the protected 1.6.5 baseline and current Settings tabs/sections."
    )
    md = [
        f"# {title}",
        "",
        f"- Exact device/settings source snapshot: `{before['source']['integrationSnapshot']}` (`{args.device_revision[:8]}`).",
        f"- Historical Web exposure source: `{before['source']['priorWebSettingsSourceRevision']}` (`d4a97653`), where `/settings` GET and `/api/settings` GET/POST are registered.",
        "- Current Web routes remain disabled by Worker C commit `4eb9b811`; Clippings routes remain disabled. No route was re-enabled to make this inventory.",
        font_note,
        source_note,
        "",
        "## Counts",
        "",
        f"- Base descriptor records: {before['summary']['baseRecords']}",
        f"- Additional manually managed device settings: {before['summary']['manualDeviceSettings']}",
        f"- Dynamic dictionary selector descriptors: {before['summary']['dynamicDeviceSettings']}",
        f"- Device-only action records: {before['summary']['deviceActions']}",
        f"- Stable Web setting keys from the pre-deferral API: {before['summary']['oldWebStableKeys']}",
        f"- Intentionally hidden/deferred records: {before['summary']['intentionallyHiddenRecords']}",
        "- Dynamic options (installed fonts, dictionaries) are recorded as source-defined runtime sets, not guessed from an absent device SD card.",
        "- Proposed group IDs are included in JSON on each record; group counts include action and deferred records.",
        "",
        "## Web Settings stable IDs before the safety deferral",
        "",
        f"- Source-possible keys (including conditional touch/tilt entries): {before['summary']['oldWebStableKeys']}.",
        f"- X4 key set after the existing touch/tilt filters: {before['summary']['oldWebX4StableKeys']}.",
        ", ".join(f"`{key}`" for key in before["summary"]["oldWebX4StableKeysList"]),
        "",
        "## Special decisions",
        "",
        ("- Flexible Sleep is absent from the 1.6.5 device baseline; its 1.6.6 persisted enum and composer settings are separately inventoried as hidden/deferred."
         if is_165_baseline else
         "- Flexible Sleep enum value 17 and its three persisted composer settings are preserved in the inventory as intentionally hidden/deferred."),
        "- Web Clippings/Notes is included as a deferred capability backed by the existing per-book ClipFile JSON. `/My Clippings.txt` remains unchanged.",
        "- The current Web settings route is not a parity baseline: the user-directed comparison baseline is the pre-Worker-C exposed key set above.",
        f"- Full machine-readable option/range/default/source metadata is in `{output_json.name}`.",
        "",
    ]
    output_markdown.write_text("\n".join(md), encoding="utf-8")
    print(json.dumps(before["summary"], indent=2))


if __name__ == "__main__":
    main()
