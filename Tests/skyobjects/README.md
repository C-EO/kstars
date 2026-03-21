# Sky Objects Unit Tests

This document describes the unit tests in `Tests/skyobjects/`.  These tests
cover core sky object classes in `kstars/skyobjects/` — coordinate
transformations for `SkyPoint` and magnitude/spectral-type handling for
`StarObject`.  No KStars window or INDI connection is required.

---

## Prerequisites

- `test_skypoint` requires `NOVA_FOUND` (libnova for high-precision coordinate
  transforms); it is skipped if libnova is not present.
- `test_starobject` has no additional prerequisites beyond the base KStars
  library.

---

## Running the tests

```bash
./build/Tests/skyobjects/test_skypoint -v2    # requires libnova
./build/Tests/skyobjects/test_starobject -v2
```

---

## Test inventory

### `test_skypoint.cpp` — SkyPoint coordinate transforms

Tests `SkyPoint` — the fundamental class representing a direction on the
celestial sphere.

Key scenarios:

- **Epoch conversion** — J2000 → apparent place and back, verified against
  libnova reference values.
- **Precession** — `precess()` between arbitrary epochs; compared with
  standard star catalogue positions.
- **Aberration** — annual aberration correction; compared with published
  values for a set of reference stars.
- **Nutation** — nutation in longitude and obliquity; compared with USNO
  tabulated values.
- **Refraction** — `altRefracted()` atmospheric refraction at various
  altitudes; verified against standard refraction tables.
- **Horizontal ↔ equatorial transform** — `EquatorialToHorizontal()` and
  inverse for a known observer location and sidereal time.

---

### `test_starobject.cpp` — StarObject properties

Tests `StarObject`, the sky object class for stars in the KStars catalogue.

Key scenarios:

- **Magnitude formatting** — `StarObject::sptype()` returns the correct
  spectral type string for a range of spectral codes.
- **Colour index** — `StarObject::getBVIndex()` returns correct B−V value.
- **Name resolution** — `StarObject::name()` and `StarObject::name2()` return
  the expected common and catalogue names for well-known stars.
- **Proper motion** — `StarObject::pmRA()` / `pmDec()` correctly stored and
  retrieved.

---

## Known gaps

The following solar system and moving-object classes have **no tests**:

| Class | Source file | What to test |
|---|---|---|
| `KSSun` | `kstars/skyobjects/kssun.cpp` | RA/DEC at known dates vs. published ephemeris |
| `KSMoon` | `kstars/skyobjects/ksmoon.cpp` | Phase, illumination fraction, rise/set times |
| `KSPlanet` | `kstars/skyobjects/ksplanet.cpp` | Planet positions vs. DE430 ephemeris |
| `KSAsteroid` | `kstars/skyobjects/ksasteroid.cpp` | Orbital element propagation |
| `KSComet` | `kstars/skyobjects/kscomet.cpp` | Comet position and tail direction |

These are high-value targets for new unit tests.
