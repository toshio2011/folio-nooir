"""Add keyboard-driven synthetic IMU input to the X3 host HAL only.

The pinned simulator exposes X3 capability metadata but its host
``HalTiltSensor`` has no physical IMU and previously returned no tilt events.
This patch keeps the event in the simulator HAL: keyboard input supplies a
raw left/right gyro impulse, while the HAL applies the same orientation and
inversion mapping used by the device path before Nooir consumes the resulting
one-shot event.
"""

import re
from pathlib import Path


Import("env")  # noqa: F821 -- provided by PlatformIO


if env.get("PIOENV") == "simulator_x3":
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = project_dir / ".pio" / "libdeps" / "simulator_x3" / "simulator"
    tilt_path = simulator_dir / "src" / "HalTiltSensor.h"
    gpio_path = simulator_dir / "src" / "HalGPIO.cpp"

    if not tilt_path.is_file() or not gpio_path.is_file():
        print("Simulator X3 tilt compatibility: simulator dependency not installed yet")
    else:
        tilt = tilt_path.read_text(encoding="utf-8")
        gpio = gpio_path.read_text(encoding="utf-8")

        if "simulatorInjectTilt(float rawAxisDps)" not in tilt:
            if "_simulatorTiltAxisDps" not in tilt:
                tilt = tilt.replace(
                    "  bool _isAwake = false;\n",
                    "  bool _isAwake = false;\n"
                    "  bool _tiltForwardEvent = false;\n"
                    "  bool _tiltBackEvent = false;\n"
                    "  bool _hadActivity = false;\n"
                    "  float _simulatorTiltAxisDps = 0.0f;\n"
                    "  bool _simulatorTiltHeld = false;\n"
                    "  bool _simulatorTiltTriggered = false;\n"
                    "  bool _simulatorHasLastTilt = false;\n"
                    "  unsigned long _simulatorLastTiltMs = 0;\n",
                    1,
                )
            if "bool _tiltForwardEvent" not in tilt:
                event_anchor = "  float _simulatorTiltAxisDps = 0.0f;\n"
                if event_anchor not in tilt:
                    raise RuntimeError("Simulator HalTiltSensor.h changed: tilt state anchor missing")
                tilt = tilt.replace(
                    event_anchor,
                    "  bool _tiltForwardEvent = false;\n"
                    "  bool _tiltBackEvent = false;\n"
                    "  bool _hadActivity = false;\n"
                    + event_anchor,
                    1,
                )
            methods = (
                "  // Host-only synthetic IMU input. The keyboard never calls a\n"
                "  // reader/navigation method; it only supplies a raw gyro impulse to this HAL.\n"
                "  void simulatorInjectTilt(float rawAxisDps) {\n"
                "    if (!_available || _simulatorTiltHeld) return;\n"
                "    _simulatorTiltAxisDps = rawAxisDps;\n"
                "    _simulatorTiltHeld = true;\n"
                "    _simulatorTiltTriggered = false;\n"
                "  }\n"
                "  void simulatorReleaseTilt() {\n"
                "    _simulatorTiltAxisDps = 0.0f;\n"
                "    _simulatorTiltHeld = false;\n"
                "    _simulatorTiltTriggered = false;\n"
                "  }\n"
            )
            new_update = (
                "  void update(const uint8_t mode, const uint8_t orientation, const bool inReader) {\n"
                "    if (!_available) return;\n"
                "    if (mode == CrossPointTiltPageTurn::TILT_OFF || !inReader) {\n"
                "      simulatorReleaseTilt();\n"
                "      clearPendingEvents();\n"
                "      return;\n"
                "    }\n"
                "    if (!_simulatorTiltHeld || _simulatorTiltTriggered) return;\n"
                "\n"
                "    const unsigned long now = millis();\n"
                "    if (_simulatorHasLastTilt && (now - _simulatorLastTiltMs) < 600) return;\n"
                "\n"
                "    float tiltAxis = _simulatorTiltAxisDps;\n"
                "    switch (orientation) {\n"
                "      case CrossPointOrientation::PORTRAIT:\n"
                "        tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -tiltAxis : tiltAxis;\n"
                "        break;\n"
                "      case CrossPointOrientation::INVERTED:\n"
                "        tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? tiltAxis : -tiltAxis;\n"
                "        break;\n"
                "      case CrossPointOrientation::LANDSCAPE_CW:\n"
                "        tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? tiltAxis : -tiltAxis;\n"
                "        break;\n"
                "      case CrossPointOrientation::LANDSCAPE_CCW:\n"
                "        tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -tiltAxis : tiltAxis;\n"
                "        break;\n"
                "      default:\n"
                "        break;\n"
                "    }\n"
                "\n"
                "    if (tiltAxis > 270.0f) {\n"
                "      _tiltForwardEvent = true;\n"
                "    } else if (tiltAxis < -270.0f) {\n"
                "      _tiltBackEvent = true;\n"
                "    } else {\n"
                "      return;\n"
                "    }\n"
                "    _hadActivity = true;\n"
                "    _simulatorTiltTriggered = true;\n"
                "    _simulatorHasLastTilt = true;\n"
                "    _simulatorLastTiltMs = now;\n"
                "  }\n"
                "  void update(const uint8_t mode, const uint8_t /*direction*/, const uint8_t orientation, const bool inReader) {\n"
                "    update(mode, orientation, inReader);\n"
                "  }\n"
                "  bool wasTiltedForward() {\n"
                "    const bool value = _tiltForwardEvent;\n"
                "    _tiltForwardEvent = false;\n"
                "    return value;\n"
                "  }\n"
                "  bool wasTiltedBack() {\n"
                "    const bool value = _tiltBackEvent;\n"
                "    _tiltBackEvent = false;\n"
                "    return value;\n"
                "  }\n"
                "  bool hadActivity() {\n"
                "    const bool value = _hadActivity;\n"
                "    _hadActivity = false;\n"
                "    return value;\n"
                "  }\n"
                "  void clearPendingEvents() {\n"
                "    _tiltForwardEvent = false;\n"
                "    _tiltBackEvent = false;\n"
                "    _hadActivity = false;\n"
                "  }\n"
            )
            update_pattern = (
                r"  void update\(const uint8_t /\*mode\*/,.*?"
                r"  void clearPendingEvents\(\) \{\}\n"
            )
            if re.search(update_pattern, tilt, flags=re.DOTALL):
                tilt = re.sub(update_pattern, new_update, tilt, count=1, flags=re.DOTALL)
            elif "  void update(const uint8_t mode, const uint8_t orientation, const bool inReader) {\n" not in tilt:
                raise RuntimeError("Simulator HalTiltSensor.h changed: stub API missing")
            method_anchor = "  void update(const uint8_t mode, const uint8_t orientation, const bool inReader) {\n"
            if "simulatorInjectTilt(float rawAxisDps)" not in tilt:
                tilt = tilt.replace(method_anchor, methods + method_anchor, 1)
        if "bool _tiltForwardEvent" not in tilt:
            event_anchor = "  float _simulatorTiltAxisDps = 0.0f;\n"
            if event_anchor not in tilt:
                raise RuntimeError("Simulator HalTiltSensor.h changed: tilt state anchor missing")
            tilt = tilt.replace(
                event_anchor,
                "  bool _tiltForwardEvent = false;\n"
                "  bool _tiltBackEvent = false;\n"
                "  bool _hadActivity = false;\n"
                + event_anchor,
                1,
            )
        deep_sleep_anchor = (
            "    _isAwake = false;\n"
            "    return true;\n"
            "  }\n\n"
            "  bool isAvailable()"
        )
        if "    simulatorReleaseTilt();\n    return true;" not in tilt:
            if deep_sleep_anchor not in tilt:
                raise RuntimeError("Simulator HalTiltSensor.h changed: deep-sleep API missing")
            tilt = tilt.replace(
                deep_sleep_anchor,
                "    _isAwake = false;\n"
                "    simulatorReleaseTilt();\n"
                "    return true;\n"
                "  }\n\n"
                "  bool isAvailable()",
                1,
            )

        if '#include "HalTiltSensor.h"' not in gpio:
            anchor = '#include "SimulatorLifecycle.h"\n'
            if anchor not in gpio:
                raise RuntimeError("Simulator HalGPIO.cpp changed: include anchor missing")
            gpio = gpio.replace(
                anchor,
                anchor + '#if defined(SIMULATOR_DEVICE_X3)\n#include "HalTiltSensor.h"\n#endif\n',
                1,
            )

        if "SDL_SCANCODE_A" not in gpio:
            keydown_anchor = "    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {\n"
            if keydown_anchor not in gpio:
                raise RuntimeError("Simulator HalGPIO.cpp changed: keydown anchor missing")
            keydown = (
                keydown_anchor
                + "#if defined(SIMULATOR_DEVICE_X3)\n"
                + "      if (e.key.keysym.scancode == SDL_SCANCODE_A) {\n"
                + "        halTiltSensor.simulatorInjectTilt(-400.0f);\n"
                + "        continue;\n"
                + "      }\n"
                + "      if (e.key.keysym.scancode == SDL_SCANCODE_D) {\n"
                + "        halTiltSensor.simulatorInjectTilt(400.0f);\n"
                + "        continue;\n"
                + "      }\n"
                + "#endif\n"
            )
            gpio = gpio.replace(keydown_anchor, keydown, 1)

            keyup_anchor = "    } else if (e.type == SDL_KEYUP) {\n"
            if keyup_anchor not in gpio:
                raise RuntimeError("Simulator HalGPIO.cpp changed: keyup anchor missing")
            keyup = (
                keyup_anchor
                + "#if defined(SIMULATOR_DEVICE_X3)\n"
                + "      if (e.key.keysym.scancode == SDL_SCANCODE_A ||\n"
                + "          e.key.keysym.scancode == SDL_SCANCODE_D) {\n"
                + "        halTiltSensor.simulatorReleaseTilt();\n"
                + "        continue;\n"
                + "      }\n"
                + "#endif\n"
            )
            gpio = gpio.replace(keyup_anchor, keyup, 1)

        tilt_path.write_text(tilt, encoding="utf-8", newline="")
        gpio_path.write_text(gpio, encoding="utf-8", newline="")
        print("Patched simulator X3 tilt input: A=left, D=right")
