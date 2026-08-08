#include "CrossPointSettings.h"

#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "fontIds.h"

namespace {

// Stack buffer for "<key>_obf" key construction — avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

void CrossPointSettings::toJson(JsonDocument& doc) const {
  const CrossPointSettings& s = *this;

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  doc["readerFrontButtonBack"] = readerFrontButtonBack;
  doc["readerFrontButtonConfirm"] = readerFrontButtonConfirm;
  doc["readerFrontButtonLeft"] = readerFrontButtonLeft;
  doc["readerFrontButtonRight"] = readerFrontButtonRight;
  doc["bluetoothEnabled"] = bluetoothEnabled;
  JsonArray bleMap = doc["bleKeyMap"].to<JsonArray>();
  for (const auto& entry : bleKeyMap) {
    if (entry.keyKind == 0xFF || entry.button == 0xFF) continue;
    JsonObject item = bleMap.add<JsonObject>();
    item["k"] = entry.keyKind;
    item["v"] = entry.keyValue;
    item["b"] = entry.button;
  }
  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  doc["fontFamily"] = fontFamily;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }
  doc["dictionaryFontFamily"] = dictionaryFontFamily;
  doc["dictionaryFontSize"] = dictionaryFontSize;

  // Language -- managed by LanguageSelectActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";
  // Sleep modes 8+ were simplified in Folio Nooir 1.5.2. Keep a small
  // layout marker so settings written by this firmware are not mistaken for
  // legacy values during the next boot.
  doc["sleepModeLayoutVersion"] = 1;
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;
  const bool currentSleepModeLayout = (doc["sleepModeLayoutVersion"] | (uint8_t)0) == 1;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        continue;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        const std::string decoded = obfuscation::deobfuscateFromBase64(doc[obfKey] | "", &ok);
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          copyToField(destPtr, raw, info.stringMaxLen);
        }
      }
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      // Older Folio Nooir builds exposed two cover/overlay variants. Collapse
      // both legacy values into the single current Cover + Overlay mode. The
      // marker keeps new values stable across subsequent boots.
      if (info.key && strcmp(info.key, "sleepScreen") == 0 && !currentSleepModeLayout) {
        const uint8_t legacyValue = v;
        if (legacyValue == 8 || legacyValue == 9) {
          v = COVER_OVERLAY;
        } else if (legacyValue == 10) {
          v = READING_STATS_SLEEP;
        } else if (legacyValue == 11) {
          v = MINIMAL_STATS;
        } else if (legacyValue == 12) {
          v = CLIPPING_COVER;
        }
        if (v != legacyValue) needsResave = true;
      }
      if (info.type == SettingType::ENUM) {
        const auto optionCount = info.enumStringValues.empty() ? info.enumValues.size() : info.enumStringValues.size();
        v = clamp(v, (uint8_t)optionCount, fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  if (!currentSleepModeLayout) needsResave = true;

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  readerFrontButtonBack =
      clamp(doc["readerFrontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  readerFrontButtonConfirm =
      clamp(doc["readerFrontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  readerFrontButtonLeft =
      clamp(doc["readerFrontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  readerFrontButtonRight =
      clamp(doc["readerFrontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  // Reuse the same duplicate protection for the reader mapping without
  // changing the user's global/home mapping.
  const uint8_t readerMapping[] = {readerFrontButtonBack, readerFrontButtonConfirm, readerFrontButtonLeft,
                                   readerFrontButtonRight};
  bool duplicateReaderMapping = false;
  for (size_t i = 0; i < 4 && !duplicateReaderMapping; ++i) {
    for (size_t j = i + 1; j < 4; ++j) {
      if (readerMapping[i] == readerMapping[j]) {
        duplicateReaderMapping = true;
        break;
      }
    }
  }
  if (duplicateReaderMapping) {
    readerFrontButtonBack = FRONT_HW_BACK;
    readerFrontButtonConfirm = FRONT_HW_CONFIRM;
    readerFrontButtonLeft = FRONT_HW_LEFT;
    readerFrontButtonRight = FRONT_HW_RIGHT;
    needsResave = true;
  }

  bluetoothEnabled = clamp(doc["bluetoothEnabled"] | (uint8_t)0, 2, 0);
  for (auto& entry : bleKeyMap) entry = BleKeyMapEntry{};
  JsonArrayConst storedBleMap = doc["bleKeyMap"];
  if (!storedBleMap.isNull()) {
    uint8_t slot = 0;
    for (JsonObjectConst item : storedBleMap) {
      if (slot >= BLE_MAP_CAPACITY) break;
      const uint8_t kind = item["k"] | (uint8_t)0xFF;
      const uint8_t button = item["b"] | (uint8_t)0xFF;
      if (kind > 1 || button >= MappedInputManager::kButtonCount) {
        needsResave = true;
        continue;
      }
      bleKeyMap[slot].keyKind = kind;
      bleKeyMap[slot].keyValue = item["v"] | (uint8_t)0;
      bleKeyMap[slot].button = button;
      slot++;
    }
  }

  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, 0);
  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = NOTOSERIF;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, load manually
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));
  dictionaryFontFamily = clamp(doc["dictionaryFontFamily"] | static_cast<uint8_t>(DICT_USE_READER),
                               DICT_FONT_FAMILY_COUNT, DICT_USE_READER);
  dictionaryFontSize = clamp(doc["dictionaryFontSize"] | static_cast<uint8_t>(MEDIUM), FONT_SIZE_COUNT, MEDIUM);

  // Line spacing was historically stored as the enum 0=Tight, 1=Normal,
  // 2=Wide. The public key remains "lineSpacing" for compatibility, while
  // the value is now a precise percentage.
  const uint16_t storedLineSpacing = doc["lineSpacing"] | static_cast<uint16_t>(LINE_SPACING_DEFAULT_PERCENT);
  if (storedLineSpacing <= WIDE) {
    static constexpr uint8_t LEGACY_LINE_SPACING[] = {95, 100, 110};
    lineSpacingPercent = LEGACY_LINE_SPACING[storedLineSpacing];
    needsResave = true;
  } else {
    lineSpacingPercent = static_cast<uint8_t>(
        std::clamp<uint16_t>(storedLineSpacing, LINE_SPACING_MIN_PERCENT, LINE_SPACING_MAX_PERCENT));
  }

  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.clockUtcOffsetQ = clockUtcOffsetQ;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  const float percent = static_cast<float>(std::clamp<uint8_t>(
      lineSpacingPercent, LINE_SPACING_MIN_PERCENT, LINE_SPACING_MAX_PERCENT));
  // Preserve the previous family-specific baseline at 100%, then apply the
  // precise percentage uniformly to built-in and SD-card fonts.
  const float familyBaseline = (sdFontFamilyName[0] != '\0' || fontFamily == NOTOSERIF) ? 1.0f : 0.95f;
  return familyBaseline * percent / 100.0f;
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  switch (fontFamily) {
    case NOTOSERIF:
    default:
      switch (fontSize) {
        case SMALL:
          return NOTOSERIF_12_FONT_ID;
        case MEDIUM:
        default:
          return NOTOSERIF_14_FONT_ID;
        case LARGE:
          return NOTOSERIF_16_FONT_ID;
        case EXTRA_LARGE:
          return NOTOSERIF_18_FONT_ID;
      }
    case NOTOSANS:
      switch (fontSize) {
        case SMALL:
          return NOTOSANS_12_FONT_ID;
        case MEDIUM:
        default:
          return NOTOSANS_14_FONT_ID;
        case LARGE:
          return NOTOSANS_16_FONT_ID;
        case EXTRA_LARGE:
          return NOTOSANS_18_FONT_ID;
      }
  }
}

int CrossPointSettings::getDictionaryFontId() const {
  const uint8_t size = dictionaryFontSize < FONT_SIZE_COUNT ? dictionaryFontSize : MEDIUM;
  // "Use Reader Font" follows the reader family (including an SD-card font),
  // while retaining the dictionary-specific point size. This keeps the two
  // controls independent instead of silently ignoring Dictionary Font Size.
  if (dictionaryFontFamily == DICT_USE_READER) {
    if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
      const int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, size);
      if (id != 0) return id;
    }
  }

  const bool useSans = dictionaryFontFamily == DICT_NOTOSANS ||
                       (dictionaryFontFamily == DICT_USE_READER && fontFamily == NOTOSANS);
  if (useSans) {
    switch (size) {
      case SMALL:
        return NOTOSANS_12_FONT_ID;
      case MEDIUM:
        return NOTOSANS_14_FONT_ID;
      case LARGE:
        return NOTOSANS_16_FONT_ID;
      case EXTRA_LARGE:
      default:
        return NOTOSANS_18_FONT_ID;
    }
  }

  switch (size) {
    case SMALL:
      return NOTOSERIF_12_FONT_ID;
    case MEDIUM:
      return NOTOSERIF_14_FONT_ID;
    case LARGE:
      return NOTOSERIF_16_FONT_ID;
    case EXTRA_LARGE:
    default:
      return NOTOSERIF_18_FONT_ID;
  }
}
