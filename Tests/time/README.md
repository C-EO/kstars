# Time Tests — Stub

This directory is a stub for tests of `kstars/time/`.

**No tests have been written yet.** See `Tests/README.md` for the full
coverage gap analysis.

---

## Source subsystem

`kstars/time/` contains the KStars time and clock subsystem:

| Class | Responsibility |
|---|---|
| `KStarsDateTime` | Extends `QDateTime` with Julian Day (JD) support and conversion to/from UT/LST |
| `SimClock` | Simulated clock that emits `timeAdvanced()` on every tick; supports pause, resume, and time-warp factor |
| `TimeZoneRule` | Encodes DST transition rules (start/end date, offset) for a geographic timezone |

---

## What to test

### `KStarsDateTime`

- **JD ↔ calendar round-trip** — Convert a known date to JD and back;
  compare with published Astronomical Almanac values.
- **`djd()` precision** — Verify sub-second precision of JD storage.
- **UTC offset handling** — Construct a `KStarsDateTime` with a UTC offset
  and verify `toUTC()` round-trips correctly.
- **Modified Julian Date** — `mjd()` = `djd()` − 2400000.5.
- **DST transitions** — Test dates straddling DST change (ambiguous hour).

### `SimClock`

- **Tick emission** — Start the clock at rate 1×; after N Qt event-loop
  iterations the `timeAdvanced()` signal must have fired N times.
- **Pause / resume** — Verify that time does not advance while paused.
- **Time warp** — At rate 60× the clock advances 60 seconds per real second.
- **`setUTC()`** — Setting a new time fires `timeChanged()`.

### `TimeZoneRule`

- **Transition calculation** — For a given year, `nextTransition()` returns
  the correct DST switch date for a representative set of time zones.
- **UTC offset** — `utcOffset()` returns the correct offset before and after
  the transition.
- **No-DST zones** — A rule with equal start and end dates (e.g. UTC, most
  equatorial zones) returns the same offset year-round.

---

## Adding tests

1. Create `test_kstarsdatetime.cpp`, `test_simclock.cpp`,
   `test_timezonerule.cpp`.
2. Uncomment and fill in the `ADD_EXECUTABLE` / `ADD_TEST` blocks in
   `CMakeLists.txt`.
3. `Tests/CMakeLists.txt` already has `add_subdirectory(time)`.
4. Update this README with the test inventory.
