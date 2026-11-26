#pragma once
struct Rect { Rect(const int,const int,const int,const int) {} };
class GfxRenderer { public:
  void clearScreen() {}
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  void displayBuffer() {}
};
