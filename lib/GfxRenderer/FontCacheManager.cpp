#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

#include "../../src/util/EpubDiagnostics.h"

FontCacheManager::FontCacheManager(const std::map<int, EpdFontFamily>& fontMap,
                                   const std::map<int, SdCardFont*>& sdCardFonts)
    : fontMap_(fontMap), sdCardFonts_(sdCardFonts) {}

void FontCacheManager::setFontDecompressor(FontDecompressor* d) { fontDecompressor_ = d; }

void FontCacheManager::clearCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    font->clearCache();
  }
}

void FontCacheManager::releaseSdFontCaches() {
  EpubDiagnostics::record("sd_font_release_start");
  // The built-in decompressor has no persistent layout metadata, so its page
  // and hot-group buffers can be released together with the disposable SD
  // glyph arenas. SdCardFont::releaseDisposableCaches() deliberately
  // preserves its advance tables and full coverage/kerning/ligature metadata.
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    if (font) font->releaseDisposableCaches();
  }
  EpubDiagnostics::record("sd_font_release_end");
}

void FontCacheManager::prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask) {
  EpubDiagnostics::record("font_prepare_start", -1, -1, 0, utf8Text ? strlen(utf8Text) : 0, styleMask);
  // SD card font prewarm path: prewarm all requested styles in one call
  auto it = sdCardFonts_.find(fontId);
  if (it != sdCardFonts_.end()) {
    if (!it->second) {
      LOG_ERR("FCM", "prewarmCache(SD): null font for fontId=%d; using normal fallback", fontId);
      return;
    }
    int missed = it->second->prewarm(utf8Text, styleMask);
    EpubDiagnostics::record("font_prepare_end", -1, -1, 0, utf8Text ? strlen(utf8Text) : 0, styleMask, 0,
                            missed < 0 ? 0 : 1);
    if (missed < 0) {
      // SdCardFont keeps its coverage/miss callback usable when preparation
      // cannot run. The real render therefore falls back to on-demand glyph
      // loading instead of treating a failed prewarm as a page failure.
      LOG_ERR("FCM", "prewarmCache(SD): preparation failed for fontId=%d; using on-demand fallback", fontId);
    } else if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache(SD): %d glyph(s) not found (styleMask=0x%02X)", missed, styleMask);
    }
    return;
  }

  // Standard compressed font prewarm path: loop over all requested styles
  if (!fontDecompressor_ || fontMap_.count(fontId) == 0) return;

  for (uint8_t i = 0; i < 4; i++) {
    if (!(styleMask & (1 << i))) continue;
    auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = fontMap_.at(fontId).getData(style);
    if (!data || !data->groups) continue;
    int missed = fontDecompressor_->prewarmCache(data, utf8Text);
    EpubDiagnostics::record("font_prepare_end", -1, -1, 0, utf8Text ? strlen(utf8Text) : 0, i, 0,
                            missed < 0 ? 0 : 1);
    if (missed < 0) {
      // FontDecompressor's hot-group path is the bounded recovery path for a
      // page-prewarm allocation/slot failure; keep the page render alive.
      LOG_ERR("FCM", "prewarmCache: preparation failed for style %d; using hot-group fallback", i);
    } else if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
    }
  }
}

void FontCacheManager::logStats(const char* label) {
  if (fontDecompressor_) fontDecompressor_->logStats(label);
  for (auto& [id, font] : sdCardFonts_) {
    font->logStats(label);
  }
}

void FontCacheManager::resetStats() {
  if (fontDecompressor_) fontDecompressor_->resetStats();
  for (auto& [id, font] : sdCardFonts_) {
    font->resetStats();
  }
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

FontCacheManager::ScanEntry* FontCacheManager::findOrCreateScanEntry(const int fontId) {
  for (uint8_t i = 0; i < scanEntryCount_; ++i) {
    if (scanEntries_[i].fontId == fontId) return &scanEntries_[i];
  }
  if (scanEntryCount_ >= MAX_SCAN_FONTS) {
    LOG_DBG("FCM", "Prewarm scan font cap reached; skipping font %d", fontId);
    return nullptr;
  }
  auto& entry = scanEntries_[scanEntryCount_++];
  entry.fontId = fontId;
  entry.text.clear();
  entry.text.reserve(2048);
  memset(entry.styleCounts, 0, sizeof(entry.styleCounts));
  return &entry;
}

void FontCacheManager::recordText(const char* text, const int fontId, const EpdFontFamily::Style style) {
  if (!text) return;
  auto* entry = findOrCreateScanEntry(fontId);
  if (!entry) return;
  const size_t remaining = entry->text.size() < MAX_SCAN_TEXT_BYTES ? MAX_SCAN_TEXT_BYTES - entry->text.size() : 0;
  if (remaining > 0) entry->text.append(text, std::min(strlen(text), remaining));
  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
  uint32_t cpCount = 0;
  while (*p) {
    if ((*p & 0xC0) != 0x80) cpCount++;
    p++;
  }
  entry->styleCounts[baseStyle] += cpCount;
}

void FontCacheManager::recordCodepoint(const uint32_t cp, const int fontId, const EpdFontFamily::Style style) {
  auto* entry = findOrCreateScanEntry(fontId);
  if (!entry || entry->text.size() >= MAX_SCAN_TEXT_BYTES) return;
  utf8AppendCodepoint(cp, entry->text);
  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;
  entry->styleCounts[baseStyle]++;
}

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager) : manager_(&manager) {
  EpubDiagnostics::record("font_scan_scope_start");
  manager_->scanMode_ = ScanMode::Scanning;
  manager_->clearCache();
  manager_->resetStats();
  manager_->scanEntryCount_ = 0;
  for (auto& entry : manager_->scanEntries_) {
    entry.text.clear();
    memset(entry.styleCounts, 0, sizeof(entry.styleCounts));
    entry.fontId = -1;
  }
}

void FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  unsigned long totalBytes = 0;
  for (uint8_t i = 0; i < manager_->scanEntryCount_; ++i) {
    totalBytes += manager_->scanEntries_[i].text.size();
  }
  EpubDiagnostics::record("font_scan_scope_end", -1, -1, 0, totalBytes, manager_->scanEntryCount_, totalBytes);
  manager_->scanMode_ = ScanMode::None;
  for (uint8_t entryIndex = 0; entryIndex < manager_->scanEntryCount_; ++entryIndex) {
    auto& entry = manager_->scanEntries_[entryIndex];
    if (entry.text.empty()) continue;
    uint8_t styleMask = 0;
    for (uint8_t i = 0; i < 4; i++) {
      if (entry.styleCounts[i] > 0) styleMask |= (1 << i);
    }
    if (styleMask == 0) styleMask = 1;
    manager_->prewarmCache(entry.fontId, entry.text.c_str(), styleMask);
    entry.text.clear();
    entry.text.shrink_to_fit();
  }
  manager_->scanEntryCount_ = 0;
}

FontCacheManager::PrewarmScope::~PrewarmScope() {
  if (active_) {
    endScanAndPrewarm();  // no-op if already called (scan entries are empty)
    manager_->clearCache();
  }
}

FontCacheManager::PrewarmScope::PrewarmScope(PrewarmScope&& other) noexcept
    : manager_(other.manager_), active_(other.active_) {
  other.active_ = false;
}

FontCacheManager::PrewarmScope FontCacheManager::createPrewarmScope() { return PrewarmScope(*this); }
