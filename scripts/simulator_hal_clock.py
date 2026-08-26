"""Add the small HalClock API extension required by Folio Nooir.

The CrossPoint simulator supplies the host implementation of ``HalClock`` but
its public API predates the two methods used by Nooir's date/statistics stores.
Patch the downloaded simulator copy only for the native simulator environment;
the device HAL and all production source files remain untouched.
"""

from pathlib import Path


Import("env")  # noqa: F821  -- provided by PlatformIO


simulator_env = str(env.get("PIOENV") or "")

if simulator_env in ("simulator_x4", "simulator_x3"):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = project_dir / ".pio" / "libdeps" / simulator_env / "simulator"
    header_path = simulator_dir / "src" / "HalClock.h"
    source_path = simulator_dir / "src" / "HalClock.cpp"

    if not header_path.is_file() or not source_path.is_file():
        # Dependency installation can happen after pre-scripts on a fresh
        # checkout.  Do not alter any device source; the next build will retry.
        print("Simulator HalClock compatibility: simulator dependency not installed yet")
    else:
        header = header_path.read_text(encoding="utf-8")
        source = source_path.read_text(encoding="utf-8")

        declaration = """  // Folio Nooir compatibility: host date/statistics helpers.
  // Date keys use the same YYYYMMDD representation as the device HAL.
  bool hasUsableTime() const;
  uint32_t getDateKey() const;
  bool restoreFromEpoch(int64_t epoch);
"""
        if "bool hasUsableTime() const;" not in header:
            anchor = "  bool hasValidTime() const;\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalClock.h changed: cannot add Nooir API")
            header = header.replace(anchor, anchor + declaration, 1)
        elif "bool restoreFromEpoch(int64_t epoch);" not in header:
            anchor = "  uint32_t getDateKey() const;\n"
            if anchor not in header:
                raise RuntimeError("Simulator HalClock.h changed: date-key API missing")
            header = header.replace(anchor, anchor + "  bool restoreFromEpoch(int64_t epoch);\n", 1)

        definition_marker = "// Folio Nooir compatibility helpers"
        definitions = r'''// Folio Nooir compatibility helpers
bool HalClock::hasUsableTime() const {
  // The simulator clock is backed by the host clock.  Do not require a
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
        if definition_marker not in source:
            anchor = "bool HalClock::setUtcTime(const std::time_t epoch) {\n"
            if anchor not in source:
                raise RuntimeError("Simulator HalClock changed: cannot add Nooir API")
            source = source.replace(anchor, definitions + anchor, 1)
        elif "bool HalClock::restoreFromEpoch" not in source:
            anchor = "bool HalClock::setUtcTime(const std::time_t epoch) {\n"
            if anchor not in source:
                raise RuntimeError("Simulator HalClock changed: setUtcTime missing")
            restore_definition = """bool HalClock::restoreFromEpoch(const int64_t epoch) {
  if (epoch < static_cast<int64_t>(MIN_TRUSTED_EPOCH))
    return false;
  return setUtcTime(static_cast<std::time_t>(epoch));
}

"""
            source = source.replace(anchor, restore_definition + anchor, 1)

        header_path.write_text(header, encoding="utf-8", newline="")
        source_path.write_text(source, encoding="utf-8", newline="")
        print("Patched simulator HalClock with Nooir date/time helpers")
