"""Add Arduino String::equalsIgnoreCase to the simulator String shim.

This is a host-only compatibility extension.  It uses ASCII case folding,
which matches the Arduino String API's practical semantics for HTTP/status
tokens and does not change the device String implementation.
"""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


if env.get("PIOENV") == "simulator_x4":
    project_dir = Path(env.subst("$PROJECT_DIR"))
    string_path = (
        project_dir
        / ".pio"
        / "libdeps"
        / "simulator_x4"
        / "simulator"
        / "src"
        / "WString.h"
    )

    if not string_path.is_file():
        print("Simulator String compatibility: simulator dependency not installed yet")
    else:
        text = string_path.read_text(encoding="utf-8")
        if "equalsIgnoreCase" not in text:
            text = text.replace(
                "#include <cstdint>\n#include <cstring>\n",
                "#include <cctype>\n#include <cstdint>\n#include <cstring>\n",
                1,
            )
            anchor = "  bool equals(const char *other) const { return s == (other ? other : \"\"); }\n"
            if anchor not in text:
                raise RuntimeError("Simulator WString.h changed: equals API missing")
            methods = """  bool equalsIgnoreCase(const String &other) const {
    return equalsIgnoreCase(other.c_str());
  }
  bool equalsIgnoreCase(const char *other) const {
    if (!other || s.size() != std::strlen(other))
      return false;
    for (size_t i = 0; i < s.size(); ++i) {
      const unsigned char lhs = static_cast<unsigned char>(s[i]);
      const unsigned char rhs = static_cast<unsigned char>(other[i]);
      if (std::tolower(lhs) != std::tolower(rhs))
        return false;
    }
    return true;
  }
"""
            text = text.replace(anchor, anchor + methods, 1)
            string_path.write_text(text, encoding="utf-8", newline="")
            print("Patched simulator String with equalsIgnoreCase")
        else:
            print("Simulator String already provides equalsIgnoreCase")
