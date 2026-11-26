#pragma once
class MappedInputManager {
 public:
  enum class Button { Back, Power };
  struct Labels { const char* btn1; const char* btn2; const char* btn3; const char* btn4; };
  bool back=false, power=false, home=false;
  bool wasPressed(const Button button) const { return (button==Button::Back && back)||(button==Button::Power && power); }
  bool wasHomeGesture() const { return home; }
  Labels mapLabels(const char*,const char*,const char*,const char*) const { return {"Back","","",""}; }
};
