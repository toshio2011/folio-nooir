#include "QuickActions.h"

#include <I18n.h>

namespace QuickActions {

const char* label(const ActionId action) {
  switch (action) {
    case ActionId::RefreshScreen:
      return I18N.get(StrId::STR_FORCE_REFRESH);
    case ActionId::ToggleDarkMode:
      return I18N.get(StrId::STR_DARK);
    case ActionId::Lookup:
      return I18N.get(StrId::STR_LOOKUP);
    case ActionId::ReadingStats:
      return I18N.get(StrId::STR_READING_STATS);
    case ActionId::ToggleBookmark:
      return I18N.get(StrId::STR_TOGGLE_BOOKMARK);
    case ActionId::ReaderOptions:
      return I18N.get(StrId::STR_READER_OPTIONS);
    case ActionId::Orientation:
      return I18N.get(StrId::STR_ORIENTATION);
    case ActionId::Sync:
      return I18N.get(StrId::STR_SYNC_PROGRESS);
    case ActionId::Screenshot:
      return I18N.get(StrId::STR_SCREENSHOT_BUTTON);
    case ActionId::Home:
      return I18N.get(StrId::STR_GO_HOME_BUTTON);
    case ActionId::Sleep:
      return I18N.get(StrId::STR_SLEEP);
    case ActionId::ToggleBluetooth:
      return I18N.get(StrId::STR_TOGGLE_BLUETOOTH);
    case ActionId::None:
      return "";
  }
  return "";
}

}  // namespace QuickActions
