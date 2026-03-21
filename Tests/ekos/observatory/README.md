# Observatory Tests — Stub

This directory is a stub for tests of `kstars/ekos/observatory/`.

**No tests have been written yet.** See `Tests/README.md` for the full
coverage gap analysis.

---

## Source subsystem

`kstars/ekos/observatory/` contains three model classes that drive the
KStars Observatory panel:

| Class | Source file | Responsibility |
|---|---|---|
| `ObservatoryModel` | `observatorymodel.cpp` | Top-level state machine: auto-park on weather alert, auto-unpark on weather OK, park/unpark sequencing |
| `ObservatoryDomeModel` | `observatorydomemodel.cpp` | Dome parking state, shutter open/close, azimuth slaving to mount |
| `ObservatoryWeatherModel` | `observatoryweathermodel.cpp` | Weather sensor thresholds (warning / alert), grace period, status transitions |

---

## What to test

### `ObservatoryWeatherModel`

- **Status transitions** — `OK → WARNING → ALERT` when sensor values cross
  thresholds; `ALERT → OK` after grace period expires.
- **Grace period** — Sensor briefly crosses back to OK during the alert grace
  period; status must remain ALERT until the full grace period has elapsed.
- **Threshold updates** — Changing warning/alert thresholds at runtime
  correctly re-evaluates the current sensor value.

### `ObservatoryDomeModel`

- **Parking state machine** — `canPark()` / `park()` / `unpark()` transitions.
- **Shutter** — `openShutter()` and `closeShutter()` fire the correct INDI
  property signals.
- **Slaving** — When slaving is enabled, `syncDomeWithTelescope()` sets the
  dome azimuth to match the mount azimuth.

### `ObservatoryModel`

- **Auto-park on alert** — When the weather model emits ALERT, the
  observatory model parks the dome and mount within the configured timeout.
- **Auto-unpark on recovery** — When weather returns to OK, the observatory
  model unparks and resumes the scheduler.
- **Manual override** — A manual park command takes priority over auto-unpark.

---

## Adding tests

1. Create `test_observatorymodel.cpp` (mock INDI signals using
   `QSignalSpy` or a lightweight mock device).
2. Uncomment and fill in the `ADD_EXECUTABLE` / `ADD_TEST` block in
   `CMakeLists.txt`.
3. `Tests/ekos/CMakeLists.txt` already has `add_subdirectory(observatory)`.
4. Update this README with the test inventory.
