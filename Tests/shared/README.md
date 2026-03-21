# KStars Shared Test Infrastructure

This directory contains test utilities that are **shared across all KStars test
subdirectories**.  Everything here is header-only — no compilation step and no
library link is required.

---

## Table of Contents

1. [Quick start](#1-quick-start)
2. [`kstars_test_macros.h` — Generic Qt Test macros](#2-kstars_test_macrosh)
3. [`testhelpers.h` — Environment isolation](#3-testhelpershy)
4. [Ekos integration helpers (in `kstars_ui/`)](#4-ekos-integration-helpers)
5. [Migration guide](#5-migration-guide)

---

## 1. Quick start

To use the shared macros in any new test:

```cmake
# In your test's CMakeLists.txt — usually not needed because
# Tests/CMakeLists.txt already adds the shared/ include dir globally:
# include_directories(${kstars_SOURCE_DIR}/Tests/shared)

ADD_EXECUTABLE( mytest mytest.cpp )
TARGET_LINK_LIBRARIES( mytest ${TEST_LIBRARIES} )
ADD_TEST( NAME MyTest COMMAND mytest )
```

```cpp
// In your test header / source:
#include "kstars_test_macros.h"   // generic macros
#include "testhelpers.h"          // environment isolation (already at Tests/)
```

---

## 2. `kstars_test_macros.h`

**File:** `Tests/shared/kstars_test_macros.h`

**Dependencies:** Qt5/Qt6 only (`QtTest`, `QCheckBox`, `QComboBox`, …)  
**Available to:** every test directory (no INDI/CFITSIO required)

### 2.1 Core verification macros

These are "subroutine" versions of the standard `QVERIFY` / `QTRY_*` macros.
They allow test *helper functions* that return `bool` to report failures back
to the top-level test function without calling the QtTest internal long-jump:

| Macro | Equivalent to | Use in |
|---|---|---|
| `KVERIFY_SUB(stmt)` | `QVERIFY(stmt)` | `bool` helper functions |
| `KVERIFY2_SUB(stmt, desc)` | `QVERIFY2(stmt, desc)` | `bool` helper functions |
| `KWRAP_SUB(stmt)` | — | `bool` helper that calls `void` QtTest macros |
| `KTRY_VERIFY_WITH_TIMEOUT_SUB(expr, ms)` | `QTRY_VERIFY_WITH_TIMEOUT(expr, ms)` | `bool` helper functions |

#### Pattern — composing bool helper functions

```cpp
bool helperA() {
    KVERIFY_SUB(someCondition());   // returns false if condition is false
    KVERIFY2_SUB(x == 42, "x must be 42");
    return true;
}

bool helperB() {
    KWRAP_SUB(KTRY_CLICK(myWidget, okButton));   // KTRY_CLICK calls QVERIFY internally
    KVERIFY_SUB(helperA());                      // chain helpers
    return true;
}

void MyTest::testSomething() {
    QVERIFY(helperB());   // top-level: use normal QVERIFY
}
```

### 2.2 Timeout / retry internals

| Macro | Purpose |
|---|---|
| `KTRY_IMPL_SUB(expr, ms)` | Poll `expr` every 50 ms (or ms/7 for short timeouts) for up to `ms` ms |
| `KTRY_TIMEOUT_DEBUG_IMPL_SUB(expr, ms, step)` | Emit a diagnostic if `expr` needed longer than expected |

These are used internally by `KTRY_VERIFY_WITH_TIMEOUT_SUB` and are rarely
called directly.

### 2.3 Widget-interaction macros

All widget macros look up a named child of `module` using `findChild<klass*>`.

#### Finding widgets

```cpp
// void test context — fails test if not found within 10 s:
KTRY_GADGET(module, QPushButton, okButton);   // declares QPushButton* okButton

// bool helper context:
KTRY_GADGET_SUB(module, QComboBox, filterCombo);

// Find a top-level QWindow by window title:
KTRY_FIND_WINDOW(win, "Plate Solve");
```

#### Clicking

```cpp
KTRY_CLICK(module, okButton);       // void context
KTRY_CLICK_SUB(module, okButton);   // bool helper context
```

#### Setting values

| Macro | Widget type | Notes |
|---|---|---|
| `KTRY_SET_CHECKBOX(m, name, v)` | `QCheckBox` | Verifies `isChecked() == v` |
| `KTRY_SET_RADIOBUTTON(m, name, v)` | `QRadioButton` | Verifies `isChecked() == v` |
| `KTRY_SET_SPINBOX(m, name, x)` | `QSpinBox` | Verifies `value() == x` |
| `KTRY_SET_DOUBLESPINBOX(m, name, x)` | `QDoubleSpinBox` | Tolerance 0.001 |
| `KTRY_SET_COMBO(m, name, txt)` | `QComboBox` | Sets by text; waits 1 s |
| `KTRY_SET_COMBO_INDEX(m, name, i)` | `QComboBox` | Sets by index |
| `KTRY_SET_LINEEDIT(m, name, v)` | `QLineEdit` | Verifies `text() == v` |

All setters have `_SUB` variants for use inside `bool` helper functions.

### 2.4 State-queue helpers

KStars integration tests often verify a sequence of state transitions by
enqueueing expected states and checking that they are dequeued in order:

```cpp
QQueue<Ekos::CaptureState> expected;
expected.enqueue(Ekos::CAPTURE_CAPTURING);
expected.enqueue(Ekos::CAPTURE_COMPLETE);

// … trigger capture …

// Wait up to 30 s for both states to be dequeued:
KVERIFY_EMPTY_QUEUE_WITH_TIMEOUT(expected, 30000);   // void context
// or:
KVERIFY_EMPTY_QUEUE_WITH_TIMEOUT_SUB(expected, 30000);  // bool helper context
```

### 2.5 Text-field helper

```cpp
// Verify that a QLineEdit starts with a given prefix within a timeout:
KTRY_VERIFY_TEXTFIELD_STARTS_WITH_TIMEOUT_SUB(myLineEdit, "NGC", 5000);
```

---

## 3. `testhelpers.h`

**File:** `Tests/testhelpers.h`  *(one level up from this directory)*

**Dependencies:** Qt5/Qt6, KStars headers (`kstars.h`, `kspaths.h`)  
**Available to:** every test directory (auto-included via the `EarlyEnvironment`
static initialiser at file scope)

This header provides **test isolation** — every test binary automatically gets:
- A private `HOME` / `XDG_DATA_HOME` / `XDG_CONFIG_HOME` / `XDG_CACHE_HOME`
  under `/tmp/kstars-tests-<pid>/`
- Optional network blocking (set `KSTARS_TEST_NO_NETWORK=1`)
- Symlinks to `kstars/data/` so `KStarsData` can initialise

### API summary

| Symbol | Type | Purpose |
|---|---|---|
| `KTest::setupTestEnvironment()` | function | Redirect env vars to tmp dirs; optionally block network |
| `KTest::installTestData()` | function | Symlink/copy `kstars/data/` into the test XDG home |
| `KTest::rootDir()` | function → `QString` | `/tmp/kstars-tests-<pid>/` |
| `KTest::homePath()` | function → `QString` | `rootDir()/home/` |
| `KTest::tempPath()` | function → `QString` | `rootDir()/tmp/` |
| `KTest::tempDirPattern(prefix)` | function → `QString` | Pattern for `QTemporaryDir` |
| `KTest::devShareDir()` | function → `QString` | `rootDir()/devshare/` |
| `KTest::kstarsDataDir()` | function → `QString` | Path to `kstars/data/` (from compile def or env) |
| `KTest::copyRecursively(src, dst)` | function → `bool` | Recursive directory copy |
| `KTEST_BEGIN()` | macro | App name + test mode + clean dirs + install data |
| `KTEST_END()` | macro | Clean up XDG dirs after test run |
| `KTEST_CLEAN_TEST(recreate)` | macro | Remove and optionally recreate XDG dirs |
| `KTEST_CLEAN_RCFILE()` | macro | Remove `kstarsrc` from the test config dir |

### Environment variables

| Variable | Effect |
|---|---|
| `KSTARS_TEST_SOURCEDIR` | Set by CMake; locates the repo root for data files |
| `KSTARS_TEST_DATADIR` | Manual override of the data directory path |
| `KSTARS_TEST_NO_NETWORK=1` | Block outbound connections (loopback always allowed) |
| `KSTARS_TEST_ENABLE_NETWORK=1` | Allow connections even if `NO_NETWORK` would otherwise apply |

### Typical usage

```cpp
// In main() or initTestCase():
KTEST_BEGIN();

// … run tests …

// In cleanupTestCase():
KTEST_END();
```

---

## 4. Ekos integration helpers (in `kstars_ui/`)

These helpers live in `Tests/kstars_ui/` but are documented here because
they are consumed by multiple test files in that directory and are candidates
for future migration to `Tests/shared/`.

### `mockmodules.h` / `mockmodules.cpp`

**What it provides:** D-Bus mock implementations of all five Ekos modules:

| Class | D-Bus interface |
|---|---|
| `Ekos::MockEkos` | `org.kde.mockkstars.MockEkos` |
| `Ekos::MockMount` | `org.kde.mockkstars.MockEkos.MockMount` |
| `Ekos::MockCapture` | `org.kde.mockkstars.MockEkos.MockCapture` |
| `Ekos::MockFocus` | `org.kde.mockkstars.MockEkos.MockFocus` |
| `Ekos::MockAlign` | `org.kde.mockkstars.MockEkos.MockAlign` |
| `Ekos::MockGuide` | `org.kde.mockkstars.MockEkos.MockGuide` |

**Usage:** Create a `Scheduler` using the mock D-Bus path:

```cpp
// In test setup:
auto mockEkos  = new Ekos::MockEkos();
auto mockMount = new Ekos::MockMount();
// … register on D-Bus …

Scheduler scheduler("/MockKStars/MockEkos", "org.kde.mockkstars.MockEkos");
```

**Limitations:** Only the methods actually called by the scheduler are
implemented.  If you add a new scheduler call that goes through a mock, add
the corresponding `Q_SCRIPTABLE` method to the appropriate mock class.

**Migration note:** The D-Bus adaptor generator (`qt*_add_dbus_adaptor`) references
the XML files and header by relative path, so physically moving these files
requires updating `Tests/kstars_ui/CMakeLists.txt`.  The move is tracked as a
future refactoring task.

---

### `test_ekos_helper.h` / `test_ekos_helper.cpp`

**What it provides:**

1. **KStars/INDI-specific macros** (not in `kstars_test_macros.h`):

   | Macro | Purpose |
   |---|---|
   | `KTRY_INDI_PROPERTY(dev, grp, prop, var)` | Look up an INDI property via GUIManager |
   | `KTRY_SWITCH_TO_MODULE_WITH_TIMEOUT(mod, ms)` | Switch the Ekos toolbar to a module tab |
   | `SET_INDI_VALUE_DOUBLE(dev, grp, prop, val)` | Set a numeric INDI property via `indi_setprop` |
   | `SET_INDI_VALUE_SWITCH(dev, grp, prop, val)` | Set a switch INDI property |
   | `CLOSE_MODAL_DIALOG(button_nr)` | Click button N of an active modal dialog |

2. **`TestEkosHelper` class** — sets up a full INDI simulator environment:
   - `startEkosProfile()` / `shutdownEkosProfile()` — profile lifecycle
   - `prepareMountModule()`, `prepareCaptureModule()`, `prepareFocusModule()`, `prepareGuidingModule()` — module configuration
   - `slewTo()`, `startGuiding()`, `stopGuiding()`, `startAligning()`, `executeFocusing()` — operation helpers
   - Expected-state queues for all modules (`expectedCaptureStates`, `expectedGuidingStates`, …)
   - `connectModules()` — wire all module state signals to the queues

---

### `test_ekos_capture_helper.h` / `test_ekos_capture_helper.cpp`

Subclass of `TestEkosHelper` with capture-specific helpers: building and
loading `.esq` files, running a capture sequence, and verifying image counts.

---

### `test_ekos_scheduler_helper.h` / `test_ekos_scheduler_helper.cpp`

Subclass of `TestEkosHelper` with scheduler-specific helpers:
`getSchedulerFile()`, `getEsqContent()`, `writeSimpleSequenceFiles()`.

---

## 5. Migration guide

### Adding a new unit test (no INDI, no display)

```cmake
# CMakeLists.txt — include_directories already set globally
ADD_EXECUTABLE( test_mything test_mything.cpp )
TARGET_LINK_LIBRARIES( test_mything ${TEST_LIBRARIES} )
ADD_TEST( NAME TestMyThing COMMAND test_mything )
SET_TESTS_PROPERTIES( TestMyThing PROPERTIES LABELS "stable" )
```

```cpp
// test_mything.cpp
#include "kstars_test_macros.h"   // generic macros
#include "testhelpers.h"          // environment isolation
#include <QTest>

class TestMyThing : public QObject {
    Q_OBJECT
    private slots:
        void initTestCase() { KTEST_BEGIN(); }
        void cleanupTestCase() { KTEST_END(); }
        void testWidget() {
            auto *w = new QSpinBox();
            KTRY_SET_SPINBOX(w, value, 42);   // Note: only works if QSpinBox has a named child "value"
            // For direct widget access: w->setValue(42); QVERIFY(w->value() == 42);
        }
};

QTEST_GUILESS_MAIN(TestMyThing)
#include "test_mything.moc"
```

### Adding a new Ekos integration test (with mock modules)

```cpp
// Include the shared macros plus the kstars_ui helpers
#include "kstars_test_macros.h"          // generic Qt Test macros
#include "../kstars_ui/mockmodules.h"    // mock D-Bus modules
#include "../kstars_ui/test_ekos_helper.h"  // Ekos test class + INDI macros
```

### Refactoring an existing test to use shared macros

If you see a test that defines its own `KVERIFY_SUB` or similar macros, replace
the local definitions with `#include "kstars_test_macros.h"`.

`test_ekos_helper.h` still defines all these macros locally for backward
compatibility.  A future refactoring should update it to include
`kstars_test_macros.h` and remove the duplicate definitions.
