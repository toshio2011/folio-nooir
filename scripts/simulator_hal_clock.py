"""Add the small HalClock API extension required by Folio Nooir.

The simulator replaces the firmware HAL, so Nooir-only date/statistics methods
must be added to the downloaded host HAL. The compatibility layer has had two
public clock shapes: the older fork exposed ``hasValidTime()/setUtcTime()``;
the current CrossPoint simulator exposes ``getDateTime()`` and uses the host
clock directly. Keep one project-side patch compatible with both shapes so a
dependency refresh does not silently break the native build.
"""

from pathlib import Path
import re


Import("env")  # noqa: F821  -- provided by PlatformIO


if str(env.get("PIOENV") or "") in ("simulator_x4", "simulator_x3"):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = project_dir / ".pio" / "libdeps" / str(env.get("PIOENV")) / "simulator"
    header_path = simulator_dir / "src" / "HalClock.h"
    source_path = simulator_dir / "src" / "HalClock.cpp"

    if not header_path.is_file() or not source_path.is_file():
        # Dependency installation can happen after pre-scripts on a fresh
        # checkout. Do not alter device source; the next build will retry.
        print("Simulator HalClock compatibility: simulator dependency not installed yet")
    else:
        header = header_path.read_text(encoding="utf-8")
        source = source_path.read_text(encoding="utf-8")

        declaration = """  // Folio Nooir compatibility: host date/statistics helpers.
  // Date keys use the same YYYYMMDD representation as the device HAL.
  bool hasUsableTime() const;
  bool restoreFromEpoch(int64_t epoch);
  uint32_t getDateKey() const;
"""

        if "bool hasUsableTime() const;" not in header:
            # Match both the older and current simulator formatting without
            # depending on a particular spacing style.
            match = re.search(r"(?m)^\s*bool\s+getTime\s*\([^;]+;\s*$", header)
            if not match:
                raise RuntimeError("Simulator HalClock.h changed: getTime API missing")
            line_end = header.find("\n", match.start())
            line_end = len(header) if line_end < 0 else line_end + 1
            header = header[:line_end] + declaration + header[line_end:]

        marker = "// Folio Nooir compatibility helpers"
        if marker not in source:
            if "bool HalClock::hasValidTime" in source:
                # Older simulator fork: retain its own trusted-time helpers.
                definitions = r'''// Folio Nooir compatibility helpers
bool HalClock::hasUsableTime() const {
  // The simulator clock is backed by the host clock. Do not require a
  // physical RTC: X4 intentionally has no RTC in the simulator profile.
  return hasValidTime();
}

uint32_t HalClock::getDateKey() const {
  const std::time_t now = nowUtc();
  if (now < MIN_TRUSTED_EPOCH)
    return 0;

  std::tm utcTime{};
  if (!toUtc(now, utcTime))
    return 0;

  return static_cast<uint32_t>(utcTime.tm_year + 1900) * 10000UL +
         static_cast<uint32_t>(utcTime.tm_mon + 1) * 100UL +
         static_cast<uint32_t>(utcTime.tm_mday);
}

bool HalClock::restoreFromEpoch(const int64_t epoch) {
  if (epoch < static_cast<int64_t>(MIN_TRUSTED_EPOCH))
    return false;
  return setUtcTime(static_cast<std::time_t>(epoch));
}

'''
                anchor = "bool HalClock::setUtcTime(const std::time_t epoch) {\n"
            elif re.search(r"(?m)^\s*bool\s+getDateTime\s*\([^;]+;\s*$", header) and "#include <ctime>" in source:
                # Current simulator: use the host clock directly. The host
                # process already owns its wall clock, so restore is a valid
                # no-op and does not need to mutate process-global time.
                definitions = r'''// Folio Nooir compatibility helpers
bool HalClock::hasUsableTime() const {
  return std::time(nullptr) > static_cast<std::time_t>(100000);
}

uint32_t HalClock::getDateKey() const {
  const std::time_t now = std::time(nullptr);
  if (now <= static_cast<std::time_t>(100000))
    return 0;

  std::tm utcTime{};
#if defined(_WIN32)
  gmtime_s(&utcTime, &now);
#else
  gmtime_r(&now, &utcTime);
#endif
  return static_cast<uint32_t>(utcTime.tm_year + 1900) * 10000UL +
         static_cast<uint32_t>(utcTime.tm_mon + 1) * 100UL +
         static_cast<uint32_t>(utcTime.tm_mday);
}

bool HalClock::restoreFromEpoch(const int64_t epoch) {
  return epoch > static_cast<int64_t>(100000);
}

'''
                anchor = "bool HalClock::formatTime"
            else:
                raise RuntimeError("Simulator HalClock changed: unsupported clock API")

            if anchor == "bool HalClock::formatTime":
                match = re.search(r"bool HalClock::formatTime\s*\(", source)
                if not match:
                    raise RuntimeError("Simulator HalClock changed: formatTime API missing")
                source = source[:match.start()] + definitions + source[match.start():]
            else:
                if anchor not in source:
                    raise RuntimeError("Simulator HalClock changed: setUtcTime API missing")
                source = source.replace(anchor, definitions + anchor, 1)

        header_path.write_text(header, encoding="utf-8", newline="")
        source_path.write_text(source, encoding="utf-8", newline="")
        print("Patched simulator HalClock with Nooir date/time helpers")
