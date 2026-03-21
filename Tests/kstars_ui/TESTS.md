# KStars UI Tests — `test_ekos_scheduler_ops`

This document describes every test function in
`Tests/kstars_ui/test_ekos_scheduler_ops.cpp` and explains what each one
verifies, the scenario it simulates, and the expected outcome.  The descriptions
here agree with the inline comments in the source file.

---

## Prerequisites

All tests in this file require:

- KStars fully initialised with solar-system data (checked in `initTestCase()`
  via `findByName("Sun")`).
- An active D-Bus session bus (mock Ekos modules are registered on it).

If solar-system data is absent, `initTestCase()` calls `QSKIP` and the entire
test class is skipped.

## Known issues / workarounds

### `QDateTime::setTimeSpec` warning flood (Qt ≥ 6.8)

Qt 6.8 deprecated `QDateTime::setTimeSpec(Qt::TimeZone)` and emits a runtime
warning every time it is called.  KStars uses this API internally via
`KStarsDateTime`, which is called on every simulated clock tick.  The result
is hundreds of identical `QWARN` lines that drown out the real test output.

**Fix:** `initTestCase()` installs a custom Qt message handler
(`suppressKnownWarnings`) that silently drops any `QtWarningMsg` containing
`"QDateTime::setTimeSpec: Pass a QTimeZone"`.  All other messages pass through
normally.  The underlying KStars source should eventually be updated to use
`QTimeZone` directly.

### Shutdown stall: `SHUTDOWN_PRE_QUEUE_RUNNING` never completes (Qt task queue)

The scheduler's shutdown state machine checks for a pre-shutdown task queue
file (`preShutdownScriptURL`).  In a real installation this file is populated
from `taskqueue/collections/observatory_shutdown.json`.  When the system
templates are present in the test home directory, the scheduler loads the queue
and enters `SHUTDOWN_PRE_QUEUE_RUNNING`, then tries to execute it — but the
queue executor requires a live Ekos profile, which does not exist in unit
tests.  The state machine stalls indefinitely and "Wait for Shutdown" times
out after 50 iterations.

**Fix:** `init()` explicitly clears the pre-shutdown script URL after creating
the `Scheduler` instance:
```cpp
scheduler->process()->moduleState()->setPreShutdownScriptURL(QUrl());
```
This ensures the shutdown code takes the fast path ("No pre-shutdown tasks,
proceeding to stop Ekos/INDI") for all tests that exercise the full shutdown
sequence (`testSimpleJob`, `testTimeZone`, `testArtificialHorizonConstraints`).

---

## Running a single test

Qt Test accepts the test function name as a command-line argument, so you can
run any individual test without waiting for the whole suite:

```bash
./build/Tests/kstars_ui/test_ekos_scheduler_ops testBasics -v2
./build/Tests/kstars_ui/test_ekos_scheduler_ops testSimpleJob -v2
```

All per-test launch configurations are also available in
`.vscode/launch.json` under names like
`Debug test_ekos_scheduler_ops::testBasics`.

---

## Test inventory

### `testBasics`

**What it tests**

1. **D-Bus interface discovery** — Before any mock Ekos modules are registered
   on the session bus, all five scheduler D-Bus interfaces (Focus, Mount,
   Capture, Align, Guide) must be `null`.  After `addModule()` and a brief Qt
   event-loop spin, every interface must become non-null, proving the scheduler
   correctly discovers each module via D-Bus asynchronous notifications.

2. **D-Bus method call round-trip** — `Focus::resetFrame` is called via the
   scheduler's proxy.  The mock object's in-process flag `focuser->isReset` must
   flip to `true`, confirming the call completes end-to-end.

3. **Scheduler idle-shutdown state machine** — With no jobs loaded the scheduler
   must boot and stop cleanly:

   ```
   RUN_WAKEUP → [iter 1] → RUN_SCHEDULER → [iter 2, no jobs] → RUN_NOTHING
   ```

   The sleep interval returned from the second iteration must be **1000 ms**
   (not 0), confirming the scheduler does not busy-spin on an empty job list.

   > **Implementation note:** `stop()` now calls `setupNextIteration(RUN_NOTHING)`
   > directly when no devices are connected, skipping the intermediate
   > `RUN_SHUTDOWN` step.  The test handles both paths: if `RUN_SHUTDOWN` is
   > reached it is consumed before asserting `RUN_NOTHING`.

---

### `testSimpleJob`

**Location:** Silicon Valley, CA (UTC−8)  
**Target:** Altair  
**Start time:** 2021-06-13 22:00 UTC (3 pm local)

A full end-to-end simulated observation:

1. Scheduler starts, Altair is well below the 30° altitude constraint.
2. Scheduler sleeps until Altair clears 30° (~11:35 pm local → 06:35 UTC 6/14).
3. Ekos and INDI start; mount slews, plate-solves, focuses, starts guiding.
4. Capture completes → guider aborts → full shutdown → `RUN_NOTHING`.

**Checks:** correct wakeup time; correct slew coordinates for Altair.

---

### `testTimeZone`

Same scenario as `testSimpleJob` but run from **New York City (UTC−5)**.

Altair crosses 30° around the same local time, but ~3 hours earlier UTC
(~03:26 UTC on 6/14).  This test ensures the scheduler's time-zone handling is
correct and results do not accidentally depend on the Silicon Valley timezone.

---

### `testDawnShutdown`

**Location:** Silicon Valley, CA  
**Target:** Altair  
**Start time:** 2021-06-14 10:10 UTC (3:10 am local)

Astronomical dawn for SF on this date is ~3:52 am local (10:52 UTC).

This test exercises the **park-and-sleep** (non-preemptive) shutdown path.
When the next observable window is far away but preemptive shutdown is **off**,
the scheduler should only park the mount and set a wakeup timer — it must *not*
stop Ekos or disconnect INDI.

1. Scheduler starts at 3:10 am; job begins ~3:12 am.
2. At astronomical dawn the scheduler interrupts the job, parks the mount, and
   enters sleep (`RUN_WAKEUP`).  Ekos and INDI **remain running**.
3. The next evening (~06:31 UTC on 6/15) the scheduler wakes up, unparks the
   mount, and restarts the job.

**Checks:** job start time; interrupt time near dawn; mount parked and scheduler
in `RUN_WAKEUP` state; **Ekos state == `EKOS_READY` and INDI state ==
`INDI_READY`** (confirming Ekos/INDI were not stopped during the sleep period);
correct restart time the following evening; mount unparked on restart.

---

### `testPreemptiveShutdown`

**Location:** Silicon Valley, CA  
**Target:** Altair (FINISH_LOOP — never self-completes)  
**Start time:** 2021-06-14 10:10 UTC (3:10 am local)

Verifies the **preemptive shutdown + automatic wakeup** cycle:

1. Job runs until astronomical dawn interrupts it (~3:53 am local).
2. The next observable Altair window is ~19 hours away (next evening).
3. With `Options::preemptiveShutdown(true)` and a 2-hour threshold,
   `shouldSchedulerSleep()` must:
   - call `enablePreemptiveShutdown(nextStartupTime)`,
   - trigger a full Ekos/INDI shutdown,
   - leave the scheduler in `RUN_WAKEUP` (not `RUN_NOTHING`).
4. When the stored wakeup time arrives the scheduler automatically calls
   `wakeUpScheduler()`, logs *"Scheduler is awake."*, clears the
   `preemptiveShutdown` flag, and restarts Ekos/INDI.

**Checks:** `RUN_WAKEUP` state entered with flag set; stored wakeup time is
valid and in the future; flag cleared after wakeup; simulated clock near
expected restart window; Ekos/INDI successfully restarted.

---

### `testPreemptiveShutdownTimerSwitchOnQueueComplete`

**Regression test** for a bug where Ekos was **never stopped** during a
preemptive shutdown when a pre-shutdown task queue was configured
(`StopEkosAfterShutdown` enabled).

#### Root cause

When `shouldSchedulerSleep()` decides to do a preemptive shutdown it calls
`enablePreemptiveShutdown()` which calls `setupNextIteration(RUN_WAKEUP, …)` —
so the iteration timer is set to **`RUN_WAKEUP`**.  On the next tick the
pre-shutdown task queue starts executing (`SHUTDOWN_PRE_QUEUE_RUNNING`).

When the queue completes, `queueExecutionCompleted()` correctly sets
`shutdownState = SHUTDOWN_STOPPING_EKOS` — **but it never changed the iteration
timer**.  Because `runSchedulerIteration()` only dispatches to
`checkShutdownState()` when the timer is `RUN_SHUTDOWN`, the
`SHUTDOWN_STOPPING_EKOS` state was never processed:  the scheduler kept calling
`wakeUpScheduler()` on every tick, Ekos and INDI remained running, and the user
saw all devices still connected throughout the 15-hour sleep period.

This is exactly what the user's log showed:

```
[05:28:36] Pre-shutdown queue completed successfully.
[20:49:48] Scheduler is awake.
[20:49:50] Starting startup process...  ← Ekos was never stopped
```

#### Fix

`queueExecutionCompleted()` now calls `setupNextIteration(RUN_SHUTDOWN)`
unconditionally after setting `SHUTDOWN_STOPPING_EKOS`, guaranteeing that
`checkShutdownState()` is reached on the very next scheduler tick regardless of
what the timer was set to before.

#### Test strategy

The test manipulates `SchedulerModuleState` directly — no full simulation cycle
is needed (no jobs, no mount, no capture).  The `init()` fixture already creates
a fully-wired `Scheduler` + `SchedulerProcess` + `SchedulerModuleState`.

**Pre-conditions set:**
- `schedulerState = SCHEDULER_RUNNING`
- `preemptiveShutdown` flag active (wakeup 1 hour from now)
- `shutdownState = SHUTDOWN_PRE_QUEUE_RUNNING`
- `timerState = RUN_WAKEUP`  ← the exact buggy state
- `weatherGracePeriodActive = false`

`queueExecutionCompleted()` is then invoked via
`QMetaObject::invokeMethod(…, Qt::DirectConnection)`.  Qt's meta-object system
allows invoking `private slots` from outside the class; they are registered in
the meta-object table just like public slots and this is the same mechanism the
`QueueExecutor::completed` signal uses in production.

**Assertions — Part 1 (state machine transition):**
1. `timerState() == RUN_SHUTDOWN` — without this switch
   `checkShutdownState()` is never called and Ekos never stops.
2. `shutdownState() == SHUTDOWN_STOPPING_EKOS` — confirms the state machine
   advanced to the correct next state so the very next
   `checkShutdownState()` call will disconnect INDI and stop Ekos.

**Assertions — Part 2 (full dispatch chain to `stop()`):**

One call to `runSchedulerIteration()` is made after Part 1.  Because INDI and
Ekos are both `IDLE` in this test (never started), `completeShutdown()` has
nothing to disconnect and advances directly to `SHUTDOWN_COMPLETE`, then calls
`stop()`.  The following is verified:

3. `schedulerState() == SCHEDULER_IDLE` — `stop()` was actually reached via
   `checkShutdownState → completeShutdown → stop()`.  Without the fix,
   `wakeUpScheduler()` would be called instead and the scheduler would remain
   `SCHEDULER_RUNNING` for the entire sleep period.

> **Note on `timerState` after Part 2:** because the `preemptiveShutdown` flag
> is still set when `stop()` runs, `stop()` calls
> `setupNextIteration(RUN_WAKEUP, 10)` to re-enter sleep mode (waiting for the
> next scheduled window) rather than `RUN_NOTHING`.  This is correct
> preemptive-shutdown behaviour: Ekos/INDI are now stopped and the scheduler
> will wake up automatically at the configured time.  The test does **not**
> assert `timerState() == RUN_NOTHING` — that would be wrong for this path.

Together, Parts 1 and 2 prove the entire chain:
```
queueExecutionCompleted()   →  shutdownState = SHUTDOWN_STOPPING_EKOS
                               timerState    = RUN_SHUTDOWN
runSchedulerIteration()     →  checkShutdownState() dispatched
completeShutdown()          →  stop() called
stop()                      →  schedulerState = SCHEDULER_IDLE
                               timerState    = RUN_WAKEUP  (preemptive re-sleep)
```

#### Clarification: three scheduler sleep paths

```
shouldSchedulerSleep() decision tree
├── PreemptiveShutdown=ON  AND  gap > PreemptiveShutdownTime (default 2 h)
│     → enablePreemptiveShutdown()  →  checkShutdownState()
│         Runs full observatory shutdown state machine:
│           pre-shutdown queue (park mount/dome via observatory_shutdown.json)
│           + IF StopEkosAfterShutdown=ON: disconnect INDI, stop Ekos
│           + post-shutdown queue
│         stop() is called as the last step (the "soft shutdown" path):
│           schedulerState = SCHEDULER_IDLE
│           timerState     = RUN_WAKEUP  (automatic wakeup at next window)
│         [testPreemptiveShutdown, regression test exercise this path]
│
├── gap > leadTime (5 min)  AND  can park  (preemptive OFF or gap < threshold)
│     → park mount directly, Ekos/INDI remain running
│       timerState = RUN_WAKEUP  (automatic wakeup, no stop() call)
│     [testDawnShutdown exercises this path]
│
└── gap > leadTime, no parking
      → scheduler sleeps, nothing parked, Ekos/INDI running
        timerState = RUN_WAKEUP
```

Key point: `StopEkosAfterShutdown` is orthogonal to the choice of path — it is only
consulted **inside** the preemptive-shutdown state machine.  The simple park-and-sleep
path never reaches `completeShutdown()` and therefore never consults `StopEkosAfterShutdown`.

This test will **fail** on the unfixed code and **pass** once the fix is applied.

---

### `testTwilightStartup`

**Data-driven** (`testTwilightStartup_data`): two rows — San Francisco /
Rasalhague and Melbourne / Arcturus.

Verifies that the scheduler correctly waits for **twilight** before starting a
job when the twilight constraint is active (`START_ASAP` + enforce twilight).

**Checks:** the job starts within ±5 minutes of the expected post-twilight time
for each city/target pair.

---

### `testArtificialHorizonConstraints`

Runs four sub-scenarios:

1. **Raised wakeup time** — An artificial horizon region at 40° altitude over
   azimuths 100–120° (where Altair travels) pushes the wakeup time from 06:35
   UTC to ~07:27 UTC.
2. **Disabled enforcement** — Setting `enforceArtificialHorizon = false` restores
   the original wakeup time even though the horizon region is still present.
3. **Empty horizon** — Removing all horizon constraints also restores the
   original wakeup time.
4. **Job interruption** — A horizon constraint at azimuth 175–200° / 70° altitude
   interrupts the running job at ~3:19 am local; the mount parks, the scheduler
   sleeps, and wakes up the next evening.

---

### `testGreedySchedulerRun`

**Full simulation** with two targets (Altair and Deneb) and an artificial
horizon.  The greedy scheduler interleaves them across a night according to
altitude constraints:

| Slot | Target | ~Local time |
|------|--------|-------------|
| 1 | Deneb | 10:48 pm → 11:34 pm |
| 2 | Altair | 11:34 pm → 3:20 am |
| 3 | Deneb | 3:20 am → dawn |
| 4 | Deneb | next evening |

**Checks:** each job starts within ±10 minutes of the predicted time; park/sleep
cycle after dawn; correct wakeup the following evening.

---

### `testRememberJobProgress`

**Data-driven** (`testRememberJobProgress_data`): six rows covering various
combinations of planned captures and pre-existing FITS files.

When `Options::rememberJobProgress = true` the scheduler scans the output
directory and skips frames that already exist.  Tests verify that:

- A partially-completed job is marked **SCHEDULED** (still has work to do).
- A fully-completed job is marked **COMPLETE** (no more frames needed).

---

### `testGreedy`

Pure schedule-prediction unit test (no simulated clock).  Calls
`evaluateJobs()` and inspects `getSchedule()` for multiple configurations:

- FINISH_SEQUENCE vs FINISH_REPEAT vs FINISH_LOOP vs FINISH_AT completion
  conditions.
- START_ASAP vs START_AT startup conditions.
- Greedy scheduling enabled vs disabled (non-greedy never preempts a
  lower-priority running job).

Each configuration is checked against a hard-coded expected schedule with a
±10-minute tolerance.

---

### `testMaxMoonAltitude`

Verifies the **maximum moon altitude** constraint.  Target Dubhe is observable
but only scheduled during the window when the Moon is below 40°.

**Checks:** a single ~15-minute slot is predicted at ~11:40 pm local on 2021-04-20.

---

### `testGreedyStartAt`

Tests **START_AT** conditions in a full simulated run with two jobs (Altair
at 1 am, Deneb at 3 am).  Both are driven through the full mount/focus/align/
guide/capture sequence and verified to start within ±10 minutes of their
configured times.

---

### `testGroups`

Verifies **group-based round-robin scheduling**.  Three jobs (J1-finish,
J2-loop, J3-repeat2) share `group1`.  The greedy scheduler interleaves them
so each job runs once before any job runs again:

```
J1 → J2 → J3 → J2 → J3 → J2 → ...
```

Without group assignment the scheduler runs J1 once then J2 forever.

---

### `testGreedyAborts`

Verifies that **aborted jobs** are delayed by the configured `errorDelay`
(1 hour) before being rescheduled.  A four-job schedule is evaluated, then M 104
is marked ABORTED one minute before the evaluation time; the updated schedule
must place NGC 3628 first and M 104 only after the delay has elapsed.

Also verifies `checkJob()` live preemption: NGC 3628 keeps running until
M 104's delay expires, then M 104 preempts it.

---

### `testArtificialCeiling`

An artificial horizon with a **floor** and a **ceiling** band is applied.  The
target (theta Bootis / HD 126660) is only observable when it is *above* the
floor and *below* the ceiling.  The predicted schedule must contain the correct
alternating observable windows across 3 simulated days.

---

### `testSettingAltitudeBug`

Regression test for a bug where the (now-removed) `settingAltitudeCutoff`
option was applied to a **running** job, causing it to be preempted mid-session.
The fix ensures `checkJob()` does not abort a job that is already running near
its altitude cutoff.

**Checks:** `checkJob()` returns `true` (keep running) when evaluated at the
time the bug originally triggered.

---

### `testEstimateTimeBug`

Regression test for incorrect job-time estimation when `rememberJobProgress`
is active.  Pre-existing FITS files are planted for NGC 2359 (mostly done) and
the predicted schedule must show a ~45-minute first slot for NGC 2359 (only the
remaining frames) rather than a full session.

**Checks:** schedule matches expected times to within ±5 minutes.

---

### `testGreedyMessier`

Large-scale stress test with the **Messier 1–40** catalogue.  Three variants are
checked:

1. No artificial horizon, min altitude = 0° — 39-entry schedule across 2 nights.
2. No artificial horizon, min altitude = 30° — 15-entry schedule (many objects
   excluded).
3. Realistic large artificial horizon, min altitude = 30° — slightly different
   15-entry schedule due to horizon constraints.

This test is **skipped** if the `.esl` / `.esq` fixture files are not found.

---

## Shared helpers

| Helper | Purpose |
|--------|---------|
| `startup()` | Load jobs, start Ekos/INDI, slew, focus, align, guide |
| `slewAndRun()` | Verify job starts at the expected time; optionally verify it is interrupted at a given time |
| `parkAndSleep()` | Assert mount parks, scheduler enters `RUN_WAKEUP`, and Ekos/INDI remain running (`EKOS_READY` / `INDI_READY`) |
| `wakeupAndRestart()` | Assert scheduler wakes at the expected time and mount unparks |
| `startModules()` | Simulate mount tracking, focus completion, plate-solve, and guiding |
| `iterateScheduler()` | Advance the simulated clock by `sleepMs` per tick and run one scheduler iteration; repeat until a predicate is true |
| `loadGreedySchedule()` | Write `.esl`/`.esq` files and load them into the scheduler |
| `checkLastSlew()` | Verify the most recent mount slew matched the expected target coordinates |
