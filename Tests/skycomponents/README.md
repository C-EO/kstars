# Sky Components Tests — Stub

This directory is a stub for tests of `kstars/skycomponents/`.

**No tests have been written yet.** See `Tests/README.md` for the full
coverage gap analysis.

---

## Source subsystem

`kstars/skycomponents/` contains all the components that make up the
`SkyMapComposite` — the tree of drawable sky objects shown in the KStars
sky map.  Key classes:

| Class | Responsibility |
|---|---|
| `SkyMapComposite` | Root of the component tree; drives `update()` and `draw()` |
| `StarComponent` | Loads HTMesh-indexed binary star catalogues |
| `ArtificialHorizonComponent` | User-defined horizon polygons (also tested in `Tests/tools/`) |
| `CatalogsComponent` | Deep-sky objects from OpenNGC / SAC / user catalogues |
| `SolarsystemComposite` | Sun, Moon, planets, asteroids, comets |
| `SatellitesComponent` | Earth satellites (TLE propagation) |
| `ConstellationLines` / `ConstellationArt` | Constellation line/art drawing |

---

## What to test

### High priority (no display required)

1. **`StarComponent` binary loading** — Load a small synthetic star binary
   file and verify the correct number of stars are indexed and retrievable by
   HTMesh trixel ID.

2. **`ArtificialHorizonComponent`** — The math is already partially covered by
   `Tests/tools/testartificialhorizon`.  Add component-level tests for loading
   a horizon from the user database and querying `isVisible()` at runtime.

3. **`CatalogsComponent`** — Verify that objects from `CatalogsDB` (tested
   separately in `Tests/datahandlers/`) are correctly wrapped into `SkyObject`
   instances with the right type, magnitude, and coordinate.

### Medium priority (partial display needed)

4. **`SatellitesComponent`** — TLE parsing and position propagation do not
   require a display; the drawing part does.

5. **`SolarsystemComposite`** — Planet and moon position calculations can be
   tested by comparing against published ephemeris (see also the gaps in
   `Tests/skyobjects/`).

---

## Adding tests

1. Create `test_skycomponents.cpp` (or multiple per-class files).
2. Uncomment and fill in the `ADD_EXECUTABLE` / `ADD_TEST` block in
   `CMakeLists.txt`.
3. Add the test to `Tests/CMakeLists.txt` (already has
   `add_subdirectory(skycomponents)`).
4. Update this README with the test inventory.
