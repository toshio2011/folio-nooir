#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/NooirLogo360.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(NooirLogo360, (pageWidth - NOOIR_LOGO_WIDTH) / 2,
                     (pageHeight - NOOIR_LOGO_HEIGHT) / 2 - 28, NOOIR_LOGO_WIDTH, NOOIR_LOGO_HEIGHT);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 55, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
