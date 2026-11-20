"""Keep SdFat's next-allocation hint near the most recently freed cluster.

SdFat 2.3.1 only moves this hint backward when the freed cluster is below the
current hint. Atomic temporary/backup replacement can therefore leave a known
free backup chain behind while the next temporary file scans a large old FAT
region. The hint is advisory and RAM-only; this does not change FAT contents,
FSInfo, file data, or the existing promotion/recovery sequence.
"""

from pathlib import Path


PATCH_MARKER = "// NOOIR_RECENTLY_FREED_ALLOCATION_HINT"
OLD_BLOCK = """    if (cluster < m_allocSearchStart) {
      m_allocSearchStart = cluster - 1;
    }"""
NEW_BLOCK = """    // NOOIR_RECENTLY_FREED_ALLOCATION_HINT
    // This cluster is now known free; start the next global search just before it.
    m_allocSearchStart = cluster - 1;"""


def patch_fat_partition(source: str) -> str:
    """Patch one verified upstream freeChain hint update, idempotently."""
    if PATCH_MARKER in source:
        if source.count(PATCH_MARKER) != 1:
            raise RuntimeError("Duplicate Nooir SdFat allocation-hint patch marker")
        return source
    if source.count(OLD_BLOCK) != 1:
        raise RuntimeError("Expected one SdFat freeChain allocation-hint block")
    return source.replace(OLD_BLOCK, NEW_BLOCK, 1)


if "Import" in globals():  # SCons supplies Import and env when run by PlatformIO.
    Import("env")  # noqa: F821
    project_dir = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
    pio_env = env.subst("$PIOENV")  # noqa: F821
    source_path = project_dir / ".pio" / "libdeps" / pio_env / "SdFat" / "src" / "FatLib" / "FatPartition.cpp"
    if not source_path.is_file():
        raise RuntimeError("SdFat FAT allocator source not found: %s" % source_path)
    original = source_path.read_text(encoding="utf-8")
    patched = patch_fat_partition(original)
    if patched != original:
        source_path.write_text(patched, encoding="utf-8", newline="\n")
    # Export the verified patch state to firmware diagnostics. This define is
    # appended only after the expected source block or its unique marker passes.
    env.Append(CPPDEFINES=[("NOOIR_SDFAT_RECENT_FREE_HINT", 1)])  # noqa: F821
    print("Applied Nooir recent-free SdFat allocation hint for %s" % pio_env)
