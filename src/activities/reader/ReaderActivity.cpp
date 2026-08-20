#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <optional>

#include "CrossPointSettings.h"
#include "Cbz.h"
#include "Epub.h"
#include "CbzReaderActivity.h"
#include "EpubReaderActivity.h"
#include "SdCardFontSystem.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "util/CbzDiagnostics.h"

namespace {

std::string openingBookLabel(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  std::string label = path.substr(start);
  constexpr size_t MAX_LABEL_CHARS = 28;
  if (label.size() > MAX_LABEL_CHARS) {
    label.resize(MAX_LABEL_CHARS - 3);
    label += "...";
  }
  return label;
}

void showOpeningBookFeedback(GfxRenderer& renderer, const std::string& path) {
  const std::string label = openingBookLabel(path);
  const std::string message = std::string(tr(STR_LOADING_POPUP)) + "\n" + label;
  // drawPopup() performs the single acknowledgement refresh.  ReaderActivity
  // then loads the format synchronously; no second activity or persistent
  // buffer is needed, and the existing reader render replaces the popup.
  GUI.drawPopup(renderer, message.c_str());
}

}  // namespace

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isCbzFile(const std::string& path) { return FsHelpers::hasCbzExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);  // Treat .md as txt files (until we have a markdown reader)
}

bool ReaderActivity::isBmpFile(const std::string& path) { return FsHelpers::hasBmpExtension(path); }

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("READER", "Failed to allocate EPUB object");
    return nullptr;
  }
  // First open: building the spine/TOC index (book.bin) takes a couple of seconds. Show the
  // indexing popup so it isn't a silent wait on the home screen. The cachePath/hash is known at
  // construction, so this check is valid before load(); a cached open loads in a blink -> no popup.
  const bool uncached = !Storage.exists((epub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }
  bool loaded;
  {
    // Lend the framebuffer's 48 KB to the container parse (expat + spine/TOC
    // build). The popup just displayed stays on the panel; whichever reader
    // activity follows redraws the full screen anyway.
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = epub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (loaded) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
  if (!xtc) {
    LOG_ERR("READER", "Failed to allocate XTC object");
    return nullptr;
  }
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto txt = makeUniqueNoThrow<Txt>(path, "/.crosspoint");
  if (!txt) {
    LOG_ERR("READER", "Failed to allocate TXT object");
    return nullptr;
  }
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

std::unique_ptr<Cbz> ReaderActivity::loadCbz(const std::string& path) {
  logCbzPath("reader-load-cbz", path);
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto cbz = makeUniqueNoThrow<Cbz>(path, "/.crosspoint");
  if (!cbz) {
    LOG_ERR("READER", "Failed to allocate CBZ object");
    return nullptr;
  }
  if (cbz->load()) return cbz;

  if (cbz->pageIndexTooLarge()) {
    LOG_ERR("READER", "CBZ archive too large for this device: %s", path.c_str());
  }

  LOG_ERR("READER", "Failed to load CBZ");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : FsHelpers::extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  activityManager.replaceActivity(
      std::make_unique<EpubReaderActivity>(renderer, mappedInput, std::move(epub), std::move(initialBookmark)));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::onGoToCbzReader(std::unique_ptr<Cbz> cbz) {
  const auto cbzPath = cbz->getPath();
  logCbzPath("reader-cbz-routing", cbzPath);
  currentBookPath = cbzPath;
  activityManager.replaceActivity(
      std::make_unique<CbzReaderActivity>(renderer, mappedInput, std::move(cbz)));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  // A format may need to parse an archive, build a spine, or extract an
  // index before its reader can render. Acknowledge the open immediately so a
  // second button press cannot be mistaken for a dropped input.
  showOpeningBookFeedback(renderer, initialBookPath);
  sdFontSystem.ensureLoaded(renderer);

  currentBookPath = initialBookPath;
  logCbzPath("reader-on-enter", initialBookPath);
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isCbzFile(initialBookPath)) {
    auto cbz = loadCbz(initialBookPath);
    if (!cbz) {
      onGoBack();
      return;
    }
    onGoToCbzReader(std::move(cbz));
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      onGoBack();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() { finish(); }
