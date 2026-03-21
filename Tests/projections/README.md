# Projections Tests — Stub

This directory is a stub for tests of `kstars/projections/`.

**No tests have been written yet.** See `Tests/README.md` for the full
coverage gap analysis.

---

## Source subsystem

`kstars/projections/` contains six concrete `Projector` subclasses that map
celestial coordinates (RA/DEC or Alt/Az) to screen pixels and back:

| Class | Source file | Description |
|---|---|---|
| `GnomonicProjector` | `gnomonicprojector.cpp` | Tangent-plane (default KStars projection) |
| `LambertProjector` | `lambertprojector.cpp` | Lambert equal-area azimuthal |
| `AzimuthalEquidistantProjector` | `azimuthalequidistantprojector.cpp` | Azimuthal equidistant |
| `OrthographicProjector` | `orthographicprojector.cpp` | Orthographic |
| `StereographicProjector` | `stereographicprojector.cpp` | Stereographic |
| `EquirectangularProjector` | `equirectangularprojector.cpp` | Plate carrée / equirectangular |

The base class `Projector` provides `toScreen()` (sky → pixel) and
`fromScreen()` (pixel → sky) interfaces.

---

## What to test

For each projector:

1. **Round-trip accuracy** — `fromScreen(toScreen(point))` returns the
   original `SkyPoint` within tolerance (1 arcsecond).
2. **Known-value** — A star at the zenith with a known sidereal time maps to
   the centre pixel; a star 30° off-axis maps to the expected pixel radius.
3. **Scale invariance** — Doubling the zoom factor halves all pixel distances
   from the centre.
4. **Horizon/boundary** — Points beyond the valid projection range (e.g.,
   behind the observer in orthographic) are correctly classified as
   off-screen.
5. **Flip/mirror flags** — `mirrored()` and `flipped()` correctly invert the
   appropriate axis.

These tests do **not** require a display — the projector math operates
entirely on floating-point coordinates.

---

## Adding tests

1. Create `test_projections.cpp`.
2. Uncomment and fill in the `ADD_EXECUTABLE` / `ADD_TEST` block in
   `CMakeLists.txt`.
3. `Tests/CMakeLists.txt` already has `add_subdirectory(projections)`.
4. Update this README with the test inventory.
