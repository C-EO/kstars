# Mount Unit Tests

This document describes the unit tests in `Tests/mount/`.  These tests cover
the **meridian flip state machine** in `kstars/ekos/mount/` —
`MeridianFlipState` and its interactions with the mount, capture, and guider
subsystems.  No running KStars window and no physical mount are required.

> **Note:** This directory tests the *state machine logic* in isolation.
> End-to-end meridian flip tests that run a full simulated observation
> (with mount slewing, guiding, and capture) live in
> [`Tests/kstars_ui/`](../kstars_ui/README.md) —
> see `test_ekos_meridianflip.cpp`, `test_ekos_meridianflip_specials.cpp`,
> and `test_ekos_meridianflipstate.cpp`.

---

## Prerequisites

- `INDI_FOUND` (parent CMakeLists guard)
- CTest label: `unstable` (the test is currently marked unstable)

---

## Running the tests

```bash
./build/Tests/mount/test_meridianflipstate -v2
```

---

## Test inventory

### `test_meridianflipstate.cpp` — MeridianFlipState

Tests the `MeridianFlipState` class which coordinates the decision to flip,
the flip execution, and the post-flip recovery sequence.

Key scenarios:

- **Flip decision** — `checkMeridianFlip()` returns the correct action
  (no action / prepare / execute) based on current HA, configured flip offset,
  and whether the mount is already past the meridian.
- **State transitions** — stepping through `MERIDIAN_FLIP_NONE →
  MERIDIAN_FLIP_PLANNED → MERIDIAN_FLIP_WAITING → MERIDIAN_FLIP_FLIPPING →
  MERIDIAN_FLIP_COMPLETED`.
- **Capture and guide interaction** — the state machine correctly signals
  capture to suspend and guiding to stop before the flip, and signals them
  to resume once the flip is confirmed.
- **Abort** — the state machine resets cleanly if the flip is aborted
  mid-sequence.
- **Re-alignment after flip** — when plate-solving is required after a flip,
  the state machine waits for the align result before allowing capture to
  resume.
