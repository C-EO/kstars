# Analyze Tests — Stub

This directory is a stub for tests of `kstars/ekos/analyze/`.

**No tests have been written yet.** See `Tests/README.md` for the full
coverage gap analysis.

---

## Source subsystem

`kstars/ekos/analyze/` contains the **Ekos Analyze** panel — a post-session
timeline viewer that parses KStars session log files (`.analyze`) and displays
a graphical timeline of all events (capture, focus, guide, scheduler, mount,
alignment) with statistics.

| Class | Source file | Responsibility |
|---|---|---|
| `Analyze` | `analyze.cpp` | Main panel: reads `.analyze` log, populates timeline |
| `YAxisTool` | `yaxistool.cpp` | Interactive Y-axis range editor for the timeline plots |

---

## What to test

### Log parsing (no display required)

The `.analyze` file format is a simple line-oriented text log.  The parser
extracts events such as:

```
2024-01-15T22:30:00 captureComplete exposure=120.0 filter=Luminance hfr=2.1 ...
2024-01-15T22:31:00 guideStats ra=0.12 dec=0.08 rms=0.14 ...
```

Tests should:

1. **Parse a synthetic `.analyze` file** — verify that each event line is
   converted to the correct segment type with the correct start time, end
   time, and metadata fields.
2. **Statistics** — median HFR over a session, peak guide RMS, total
   captured exposure time.
3. **Empty file** — no crash on an empty or minimal log.
4. **Corrupt lines** — malformed lines are skipped without aborting the parse.

### `YAxisTool` (no display required for math)

- Range clamping and tick spacing calculation for various data ranges.

---

## Adding tests

1. Create a small synthetic `.analyze` fixture file.
2. Create `test_analyze.cpp`.
3. Uncomment and fill in the `ADD_EXECUTABLE` / `ADD_TEST` block in
   `CMakeLists.txt`.
4. `Tests/ekos/CMakeLists.txt` already has `add_subdirectory(analyze)`.
5. Update this README with the test inventory.
