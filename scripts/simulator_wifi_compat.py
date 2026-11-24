"""Align the simulator Wi-Fi mode stub with the Arduino ESP32 return type."""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


SIMULATOR_ENVS = {
    "simulator_x4",
    "simulator_x3",
    "simulator_x4pro",
    "simulator_x4classic",
    "simulator_nooir167_x4pro",
    "simulator_nooir167_x4classic",
}

if str(env.get("PIOENV") or "") in SIMULATOR_ENVS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_header = (
        project_dir
        / ".pio"
        / "libdeps"
        / str(env.get("PIOENV"))
        / "simulator"
        / "src"
        / "WiFi.h"
    )
    if not simulator_header.is_file():
        print("Simulator Wi-Fi compatibility: simulator dependency not installed yet")
    else:
        header = simulator_header.read_text(encoding="utf-8")
        if (
            "ARDUINO_EVENT_WIFI_STA_GOT_IP" not in header
            or "ARDUINO_EVENT_WIFI_STA_DISCONNECTED" not in header
        ):
            anchor = "inline constexpr arduino_event_id_t ARDUINO_EVENT_WIFI_STA_START = 0;\n"
            if anchor not in header:
                raise RuntimeError("Simulator WiFi.h changed: STA_START event missing")
            header = header.replace(
                anchor,
                anchor
                + "inline constexpr arduino_event_id_t ARDUINO_EVENT_WIFI_STA_GOT_IP = 2;\n"
                + "inline constexpr arduino_event_id_t ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 3;\n",
                1,
            )

        old_method = """  void mode(int mode) {
    currentMode = static_cast<wifi_mode_t>(mode);
    if (mode == WIFI_OFF)
      currentStatus = WL_DISCONNECTED;
  }
"""
        new_method = """  bool mode(int mode) {
    currentMode = static_cast<wifi_mode_t>(mode);
    if (mode == WIFI_OFF)
      currentStatus = WL_DISCONNECTED;
    return true;
  }
"""
        if "bool mode(int mode)" not in header:
            if old_method not in header:
                raise RuntimeError("Simulator WiFi.h changed: mode() stub not found")
            header = header.replace(old_method, new_method, 1)
            simulator_header.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator Wi-Fi event IDs and mode return type")
        else:
            if (
                "ARDUINO_EVENT_WIFI_STA_GOT_IP" in header
                and "ARDUINO_EVENT_WIFI_STA_DISCONNECTED" in header
            ):
                simulator_header.write_text(header, encoding="utf-8", newline="")
            print("Simulator Wi-Fi mode compatibility: return type already matches")
