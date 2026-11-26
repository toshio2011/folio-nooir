#pragma once
enum class StrId { STR_NONE_OPT, STR_USB_DRIVE, STR_USB_DRIVE_WAITING, STR_USB_DRIVE_EJECT_HINT,
  STR_USB_DRIVE_PREPARING, STR_USB_DRIVE_START_ERROR, STR_USB_DRIVE_CONNECTED, STR_USB_DRIVE_ERROR, STR_BACK };
constexpr StrId STR_USB_DRIVE=StrId::STR_USB_DRIVE;
constexpr StrId STR_BACK=StrId::STR_BACK;
inline const char* tr(const StrId) { return "text"; }
class TestI18n { public: const char* get(const StrId) const { return "text"; } };
extern TestI18n I18N;
namespace UIIcon { enum Icon { Library }; }
class TestGui {
 public:
  void drawHeader(GfxRenderer&,Rect,const char*) {}
  template<typename Title,typename Detail,typename Icon>
  void drawList(GfxRenderer&,Rect,int,int,Title,Detail,Icon) {}
  void drawButtonHints(GfxRenderer&,const char*,const char*,const char*,const char*) {}
};
extern TestGui GUI;
