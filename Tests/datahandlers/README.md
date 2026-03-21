# Data Handlers Unit Tests

This document describes the unit tests in `Tests/datahandlers/`.  These tests
cover the KStars catalogue database in `kstars/catalogsdb/`.

---

## Prerequisites

- Always built (no additional CMake guards)
- Requires SQLite (linked transitively through `KStarsLib`)
- A test SQLite database is provided at `Tests/datahandlers/data/test.sqlite`
  and is copied to the build directory via `file(COPY data ...)` in CMakeLists

---

## Running the tests

```bash
./build/Tests/datahandlers/test_catalogsdb -v2
```

---

## Test inventory

### `test_catalogsdb.h` — CatalogsDB

Tests `CatalogsDB`, the SQLite-backed catalogue database that stores deep-sky
objects imported from catalogues such as OpenNGC, SAC, and user-defined sources.

Key scenarios:

- **Database creation and schema** — a fresh database is created in a temporary
  directory; the schema is verified to contain the expected tables and indices.
- **Catalogue import** — a test catalogue is imported from `data/test.sqlite`;
  the imported object count and sample object fields are verified.
- **Object lookup** — objects are retrieved by ID, name, and coordinate
  proximity (`objectsAround()`); results match expected values.
- **Search** — `findObjects()` with a name prefix returns the correct subset.
- **User catalogue** — objects are added to a user-defined catalogue, retrieved,
  and then removed; verifies that the user catalogue is independent of the
  built-in catalogues.
- **Duplicate handling** — importing the same catalogue twice does not create
  duplicate rows.

---

## Known gaps

The `datahandlers/ksparser.cpp` binary data file parser (used to read star
catalogue binary files) has **no unit tests**.  This parser is responsible for
reading the HTMesh-indexed binary star data files at KStars startup.  A test
should verify:

- Reading a small synthetic binary file with known content
- Correct handling of endian conversion on big-endian platforms
- Error handling for truncated or corrupt files
