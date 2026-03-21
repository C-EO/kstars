# Tools Unit Tests

This document describes the unit tests in `Tests/tools/`.  These tests cover
astronomical math helpers in `kstars/tools/` that can be tested without a
running KStars window.

---

## Prerequisites

- No INDI, no CFITSIO, no display required — pure unit tests.
- CTest timeout is 600 s per test (set in CMakeLists).

---

## Running the tests

```bash
./build/Tests/tools/testartificialhorizon -v2
./build/Tests/tools/testgreatcircle -v2
./build/Tests/tools/testdeltara -v2

# Run a single function:
./build/Tests/tools/testartificialhorizon testFindBlocking -v2
```

---

## Test inventory

### `testartificialhorizon.cpp` — Artificial horizon constraints

Tests the `ArtificialHorizon` class and the constraint logic used by the Ekos
Scheduler and the sky map to determine whether a target is blocked by a
user-defined horizon polygon.

Key scenarios:

- **Point-in-polygon test** — given a closed horizon region (list of
  azimuth/altitude pairs), checks that `isVisible()` correctly returns
  `true`/`false` for points clearly inside, outside, and on the boundary.
- **Ceiling constraint** — a region with `ceiling = true` blocks targets that
  are *above* the region (used to exclude objects too close to zenith for a
  short-focus instrument).
- **Multiple non-overlapping regions** — a target is blocked if *any* region
  covers it.
- **Disabled enforcement** — `setEnabled(false)` makes all regions transparent.
- **Altitude clamp** — regions that wrap around azimuth 360°/0° are handled
  correctly.

---

### `testgreatcircle.cpp` — Great circle arc math

Tests `GreatCircle` utility functions used to compute angular distances and
bearings between two points on the celestial sphere.

Key scenarios:

- **`angularDistance()`** — great-circle distance between a set of reference
  pairs; compared with the Vincenty / Haversine formula to within 0.001°.
- **Pole-vicinity stability** — distance calculations near the celestial poles
  (Dec ± 89°) do not produce NaN or overflow.
- **Zero separation** — distance between a point and itself is exactly 0.

---

### `testdeltara.cpp` — RA delta / wrapping arithmetic

Tests the helper that computes the shortest signed difference between two
Right Ascension values, correctly handling the 0h/24h wraparound.

Key scenarios:

- Positive delta without wraparound (e.g. 10h − 8h = +2h)
- Negative delta without wraparound (e.g. 8h − 10h = −2h)
- Wraparound positive (e.g. 1h − 23h = +2h)
- Wraparound negative (e.g. 23h − 1h = −2h)
- Identical values return 0

---

## Known gaps in `kstars/tools/`

Most of the tools in `kstars/tools/` are GUI-centric and require a display,
but several have testable algorithmic cores:

| Tool | Source | Testable component |
|---|---|---|
| Conjunctions | `conjunctions.cpp` / `ksconjunct.cpp` | Angular separation minima over time |
| Eclipse tool | `eclipsetool.cpp` / `eclipsehandler.cpp` | Eclipse contact times vs. published data |
| Star hopper | `starhopper.cpp` | Route-finding algorithm |
| Altitude vs. time | `altvstime.cpp` | Rise/set/transit times vs. almanac |
| Imaging Planner | `imagingplanner.cpp` | Target visibility scoring |
