# SHADOW WDP Device Announce Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every 25–30 s, SHADOW sends `@WDP1 {"type":"Shadow","fw":"<rev>"}\r` to WCB 1 on Serial1, without
ever blocking the loop.

**Architecture:**

- `lib/WdpAnnounce/WdpAnnounce.h` is a header-only timer with no Arduino dependencies. It decides when an announce
  is due and whether Serial1 has room for it.
- `src/main.cpp` adds about 20 lines of glue: the PROGMEM line, the `static_assert`s, and the `wdpAnnounceTick()`
  called from `loop()`.
- `scripts/git_rev.py` injects the git revision as `GIT_REV` through a PlatformIO dynamic build flag.
- A new `[env:native]` runs Unity tests of the timer on the Mac.

**Tech Stack:** PlatformIO Core 6.2.0, `atmelavr@5.1.0` (Arduino AVR core 1.8.6, avr-gcc 7.3, gnu++11),
`platformio/native` with Unity, Python 3 (stdlib only).

**Spec:** `WIPDocs/specs/2026-09-23-shadow-wdp-device-announce-design.md`. Protocol facts and the flashing procedure
are in `WIPDocs/HANDOFF_2026-09-23_shadow_wdp_device_announce.md`.

**Worktree:** `~/Repos/SHADOW-C2B5-wdp-announce`, branch `feature/wdp-device-announce`. The real
`include/controller_macs.h` has already been copied in. It is gitignored: never `git add` it.

**Tool path:** `pio` below means `~/.platformio/penv/bin/pio`.

## Global Constraints

- The main loop must never block. No `delay()`, no busy waits, and never write to Serial1 unless
  `availableForWrite()` already has room for the whole line.
- Announce line format, exactly: `@WDP1 {"type":"Shadow","fw":"<rev>"}` followed by `\r`. `type` is exactly
  `Shadow`. No `caps`. Nothing precedes the `@` in the same write.
- The whole line, including `\r`, is at most 63 bytes (`wdp::kMaxLineLen`).
- `fw` (`GIT_REV`) is at most 14 characters (`wdp::kMaxFwLen`) and contains only `[0-9a-z-]`.
- First announce 500–3000 ms after `setup()`. After that, 25000–30000 ms, drawn fresh after each send.
- SHADOW never reads Serial1. Serial1 stays at 9600 baud.
- `default_envs` stays `SHADOWC2B5`. `pio run` with no `-e` must build only the firmware.
- Every commit ends with these trailers:
  ```
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01YEjbiB6wzzgdcCiwuUbCe5
  ```
- Never edit `Reference/`. Generated `.md` files go in `WIPDocs/`.

## Verified tool facts (checked 2026-09-23, do not re-derive)

- The PlatformIO docs give the dynamic flag form as `build_flags = !<command>`, with the script printing
  `'-DGIT_REV="<value>"'`.
- Tested in this repo:
  - `!python3 <script>` works on its own line alongside the existing `-D`/`-I` lines, and runs with cwd = project dir.
  - `!$PYTHONEXE ...` does **not** expand: exit 127.
  - `!python ...` fails: there is no `python` on PATH.
- `pio check -e SHADOWC2B5` sees `GIT_REV` from the dynamic flag. Baseline: `Total 0 69 0` (High, Medium, Low).
- Baseline `pio run -e SHADOWC2B5` gives RAM 3855 B (47.1%), flash 39572 B (15.6%), and 4 warnings: one
  `-Wswitch` at `src/main.cpp:224` and three `-Wsign-compare` from `Arduino.h:92`.
- The native platform uses the system compiler (`/usr/bin/clang` here). Unity test files use `setUp`/`tearDown`, and
  `main()` with `UNITY_BEGIN()`/`RUN_TEST()`/`UNITY_END()`.
- `test_ignore = *` on an env stops `pio test` from running any suite on it.

## Review Focus

Failure modes the spec implies that the headline tests don't obviously cover. Each now has a test in the named task.

1. **A controller fault lasting minutes, then clearing.** The timer must be ready immediately, with no second full
   wait. Test: `test_ready_long_after_due` (Task 1).
2. **`loop()` running several times in the same millisecond as a send.** It must not send twice. Test:
   `test_not_ready_same_ms_after_send` (Task 1).
3. **Serial1 busy for a stretch (button spam) and then free.** It sends as soon as there's room, and the gap never
   costs a reschedule. Test: `test_waits_for_room_without_rescheduling` (Task 1).
4. **Built from a dirty tree, or without git** (a tarball, or git missing). `fw` becomes `<sha>-dirty` or
   `unknown`, still ≤ 14 characters in the safe character set. Checks: Task 2, Steps 2–4.
5. **A tag added to the repo later.** `fw` must stay a short SHA, never `v1.0-3-g…`. `git_rev.py` uses
   `git rev-parse --short=7 HEAD`, not `git describe`. Check: Task 2, Step 3.

---

### Task 1: `WdpAnnounce` timer library with native Unity tests

**Files:**

- Create: `lib/WdpAnnounce/WdpAnnounce.h`
- Create: `test/test_wdp_announce/test_main.cpp`
- Modify: `platformio.ini` (add `test_ignore` to `[env:SHADOWC2B5]`, add `+<lib/WdpAnnounce/>` to
  `check_src_filters`, add `[env:native]`)

**Interfaces:**

- Consumes: nothing.
- Produces (used by Task 3):
  - `wdp::kFirstDelayMinMs` (500), `wdp::kFirstDelayMaxMs` (3000), `wdp::kIntervalMinMs` (25000) and
    `wdp::kIntervalMaxMs` (30000). All are `constexpr uint32_t`.
  - `wdp::kMaxLineLen` (63) and `wdp::kMaxFwLen` (14). Both are `constexpr size_t`.
  - `class wdp::AnnounceTimer`:
    - `void start(uint32_t now, uint32_t firstDelayMs)`
    - `bool ready(uint32_t now, int freeTx, size_t lineLen) const`
    - `void markSent(uint32_t now, uint32_t nextDelayMs)`

- [ ] **Step 1: Add the native env and keep tests off the firmware env**

In `platformio.ini`, add these lines to `[env:SHADOWC2B5]`, after the `build_src_filter` block and before
`build_flags =`:

```ini
; Unit tests run only in [env:native]; never build or upload a test image to the Mega.
test_ignore = *
```

Change `check_src_filters` to:

```ini
check_src_filters =
    +<src/>
    +<include/>
    +<lib/WdpAnnounce/>
```

Append at the end of the file:

```ini

[env:native]
; Host unit tests for hardware-independent logic: pio test -e native
; Uses the system compiler (Xcode Command Line Tools on macOS).
platform = platformio/native
test_framework = unity
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_wdp_announce/test_main.cpp`:

```cpp
// Host tests for wdp::AnnounceTimer. Run: pio test -e native
#include <unity.h>

#include "../../lib/WdpAnnounce/WdpAnnounce.h"

namespace {
constexpr size_t kLen = 39;        // "@WDP1 {\"type\":\"Shadow\",\"fw\":\"a51a459\"}\r"
constexpr int kRoomy = 63;         // empty Serial1 TX ring
wdp::AnnounceTimer timer;
}  // namespace

void setUp(void) {
    timer = wdp::AnnounceTimer();
}

void tearDown(void) {}

void test_not_ready_before_start(void) {
    TEST_ASSERT_FALSE(timer.ready(0, kRoomy, kLen));
    TEST_ASSERT_FALSE(timer.ready(100000, kRoomy, kLen));
}

void test_first_delay(void) {
    timer.start(1000, 2000);
    TEST_ASSERT_FALSE(timer.ready(1000, kRoomy, kLen));
    TEST_ASSERT_FALSE(timer.ready(2999, kRoomy, kLen));
    TEST_ASSERT_TRUE(timer.ready(3000, kRoomy, kLen));
    TEST_ASSERT_TRUE(timer.ready(3001, kRoomy, kLen));
}

void test_waits_for_room_without_rescheduling(void) {
    timer.start(0, 500);
    TEST_ASSERT_FALSE(timer.ready(600, 10, kLen));   // due, but Serial1 is busy
    TEST_ASSERT_FALSE(timer.ready(700, 38, kLen));   // still one byte short
    TEST_ASSERT_TRUE(timer.ready(800, 39, kLen));    // room now, still due
}

void test_room_boundary(void) {
    timer.start(0, 500);
    TEST_ASSERT_TRUE(timer.ready(500, static_cast<int>(kLen), kLen));
    TEST_ASSERT_FALSE(timer.ready(500, static_cast<int>(kLen) - 1, kLen));
    TEST_ASSERT_FALSE(timer.ready(500, 0, kLen));
    TEST_ASSERT_FALSE(timer.ready(500, -1, kLen));
}

void test_interval_after_send(void) {
    timer.start(0, 500);
    timer.markSent(500, 27000);
    TEST_ASSERT_FALSE(timer.ready(501, kRoomy, kLen));
    TEST_ASSERT_FALSE(timer.ready(27499, kRoomy, kLen));
    TEST_ASSERT_TRUE(timer.ready(27500, kRoomy, kLen));
}

void test_not_ready_same_ms_after_send(void) {
    timer.start(0, 500);
    TEST_ASSERT_TRUE(timer.ready(500, kRoomy, kLen));
    timer.markSent(500, 25000);
    TEST_ASSERT_FALSE(timer.ready(500, kRoomy, kLen));
}

void test_ready_long_after_due(void) {
    timer.start(0, 500);
    timer.markSent(500, 30000);
    TEST_ASSERT_TRUE(timer.ready(500 + 10UL * 60UL * 1000UL, kRoomy, kLen));  // fault cleared after 10 min
}

void test_millis_rollover(void) {
    const uint32_t start = 0xFFFFF000UL;  // 4096 ms before millis() wraps
    timer.start(start, 30000);
    TEST_ASSERT_FALSE(timer.ready(0x00000010U, kRoomy, kLen));  // just after the wrap: 4112 ms elapsed
    TEST_ASSERT_FALSE(timer.ready(static_cast<uint32_t>(start + 29999U), kRoomy, kLen));  // wraps to 0x6D2F
    TEST_ASSERT_TRUE(timer.ready(static_cast<uint32_t>(start + 30000U), kRoomy, kLen));
}

void test_constants(void) {
    TEST_ASSERT_EQUAL_UINT32(500, wdp::kFirstDelayMinMs);
    TEST_ASSERT_EQUAL_UINT32(3000, wdp::kFirstDelayMaxMs);
    TEST_ASSERT_EQUAL_UINT32(25000, wdp::kIntervalMinMs);
    TEST_ASSERT_EQUAL_UINT32(30000, wdp::kIntervalMaxMs);
    TEST_ASSERT_EQUAL_UINT32(63, wdp::kMaxLineLen);
    TEST_ASSERT_EQUAL_UINT32(14, wdp::kMaxFwLen);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_not_ready_before_start);
    RUN_TEST(test_first_delay);
    RUN_TEST(test_waits_for_room_without_rescheduling);
    RUN_TEST(test_room_boundary);
    RUN_TEST(test_interval_after_send);
    RUN_TEST(test_not_ready_same_ms_after_send);
    RUN_TEST(test_ready_long_after_due);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_constants);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the tests and confirm they fail for the right reason**

Run: `pio test -e native`
Expected: a compile error saying `../../lib/WdpAnnounce/WdpAnnounce.h` was not found. The first run may also
download the `native` platform and Unity, which is fine. Any other error, such as a bad `platformio.ini` or Unity
not found, must be fixed before continuing.

- [ ] **Step 4: Write the library**

Create `lib/WdpAnnounce/WdpAnnounce.h`:

```cpp
// =======================================================================================
//                     WDP Device Announce (WDP-DA) timing
// =======================================================================================
// Decides when SHADOW sends its "@WDP1 {...}" announce line to WCB 1, and whether
// Serial1 has room to take the whole line without blocking. Pure logic with no Arduino
// dependencies: the caller passes in millis(), availableForWrite() and the random
// interval, so the same code runs on the Mega and in the native unit tests.
// Protocol: Wireless_Communication_Board-WCB docs/WDP_DEVICE_ANNOUNCE.md

#ifndef WDP_ANNOUNCE_H
#define WDP_ANNOUNCE_H

#include <stddef.h>
#include <stdint.h>

namespace wdp {

constexpr uint32_t kFirstDelayMinMs = 500;  // first announce after boot
constexpr uint32_t kFirstDelayMaxMs = 3000;
constexpr uint32_t kIntervalMinMs = 25000;  // WCB forgets a device after 90 s of silence
constexpr uint32_t kIntervalMaxMs = 30000;
constexpr size_t kMaxLineLen = 63;          // Serial1 TX ring is 64 bytes; availableForWrite() tops out at 63
constexpr size_t kMaxFwLen = 14;            // ?WDP,DA prints fw in a 14-character column

class AnnounceTimer {
public:
    // Arm the timer: the first announce is due firstDelayMs after now.
    void start(uint32_t now, uint32_t firstDelayMs) {
        last_ = now;
        delay_ = firstDelayMs;
        started_ = true;
    }

    // True when an announce is due and freeTx bytes of TX buffer can hold the whole
    // line. Unsigned subtraction keeps this correct across the millis() rollover.
    // No side effects: when it returns false the caller simply asks again next loop.
    bool ready(uint32_t now, int freeTx, size_t lineLen) const {
        if (!started_ || freeTx < 0) {
            return false;
        }
        if (static_cast<uint32_t>(now - last_) < delay_) {
            return false;
        }
        return static_cast<size_t>(freeTx) >= lineLen;
    }

    // Record a send at now; the next announce is due nextDelayMs later.
    void markSent(uint32_t now, uint32_t nextDelayMs) {
        last_ = now;
        delay_ = nextDelayMs;
    }

private:
    uint32_t last_ = 0;
    uint32_t delay_ = 0;
    bool started_ = false;
};

}  // namespace wdp

#endif  // WDP_ANNOUNCE_H
```

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `pio test -e native`
Expected: `9 Tests 0 Failures 0 Ignored`, with `test_wdp_announce` shown as `PASSED`.

- [ ] **Step 6: Confirm the firmware env is unaffected**

Run: `pio run -e SHADOWC2B5 2>&1 | grep -E "RAM:|Flash:|warning|SUCCESS|FAILED"`
Expected: `SUCCESS`, RAM 3855 B and flash 39572 B (unchanged, since nothing includes the header yet), and the
same 4 warnings.

Run: `pio test -e SHADOWC2B5`
Expected: no test suite runs for that env (everything is ignored), and **nothing is uploaded**. Nothing should be
plugged in, but if a board is attached, abort this step if PlatformIO starts uploading.

- [ ] **Step 7: Commit**

```bash
git add lib/WdpAnnounce/WdpAnnounce.h test/test_wdp_announce/test_main.cpp platformio.ini
git commit -F - <<'EOF'
Add a WDP Device Announce timer with native unit tests

AnnounceTimer decides when SHADOW's @WDP1 line is due and whether Serial1
has room for all of it, so the send never blocks. It has no Arduino
dependencies, so [env:native] tests it on the host with Unity. The firmware
env ignores all test suites so pio test never uploads to the Mega.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01YEjbiB6wzzgdcCiwuUbCe5
EOF
```

---

### Task 2: Inject the git revision as `GIT_REV`

**Files:**

- Create: `scripts/git_rev.py`
- Modify: `platformio.ini` (`[env:SHADOWC2B5]` `build_flags`)

**Interfaces:**

- Consumes: nothing.
- Produces (used by Task 3): the preprocessor macro `GIT_REV`, a string literal such as `"a51a459"`,
  `"a51a459-dirty"` or `"unknown"`. It is at most 14 characters, all in `[0-9a-z-]`, and defined for every
  `SHADOWC2B5` build and `pio check`.

- [ ] **Step 1: Write the script**

Create `scripts/git_rev.py`:

```python
"""Print the build flag that defines GIT_REV for the firmware.

Run by PlatformIO through `build_flags = !python3 scripts/git_rev.py` (a dynamic
build flag). The value is the short commit SHA, plus "-dirty" when tracked files
have uncommitted changes, or "unknown" if git is unavailable. SHADOW sends it as
the "fw" field of its WDP Device Announce, so it is limited to 14 characters
(the width of WCB's ?WDP,DA column) and to [0-9a-z-] (WCB does not escape JSON).
"""

import os
import re
import subprocess

MAX_LEN = 14
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args):
    return subprocess.check_output(
        ["git", *args], cwd=PROJECT_DIR, stderr=subprocess.DEVNULL, text=True
    ).strip()


def revision():
    try:
        rev = git("rev-parse", "--short=7", "HEAD")
        if git("status", "--porcelain", "--untracked-files=no"):
            rev += "-dirty"
    except (OSError, subprocess.CalledProcessError):
        rev = "unknown"
    rev = re.sub(r"[^0-9a-z-]", "", rev.lower())[:MAX_LEN]
    return rev or "unknown"


if __name__ == "__main__":
    print("'-DGIT_REV=\"%s\"'" % revision())
```

- [ ] **Step 2: Check the script on a clean tree**

Run: `git status --porcelain --untracked-files=no; python3 scripts/git_rev.py`
Expected: nothing from `git status` (only the untracked new script), then exactly
`'-DGIT_REV="<the 7-char SHA of HEAD>"'`. Compare it with `git rev-parse --short=7 HEAD`.

- [ ] **Step 3: Check the dirty and tagged cases**

Run:

```bash
echo "# probe" >> CLAUDE.md && python3 scripts/git_rev.py; git checkout -- CLAUDE.md
git tag probe-tag && python3 scripts/git_rev.py; git tag -d probe-tag
```

Expected:

- The first line is `'-DGIT_REV="<sha>-dirty"'`, 13 characters inside the quotes.
- The second is `'-DGIT_REV="<sha>"'`, with no tag name.
- `git status --short` afterwards shows only the untracked `scripts/git_rev.py`.

- [ ] **Step 4: Check the no-git fallback**

Run: `PY="$(command -v python3)"; PATH=/usr/sbin:/sbin "$PY" scripts/git_rev.py`
Expected: `'-DGIT_REV="unknown"'`. With `/usr/bin` off the PATH there is no `git`, so `check_output` raises
`FileNotFoundError`, which is an `OSError`.

- [ ] **Step 5: Add the dynamic flag**

In `platformio.ini`, `[env:SHADOWC2B5]`, change `build_flags` to:

```ini
build_flags =
    -D BOARD_SHADOWC2B5
    -D ENABLE_UHS_DEBUGGING=1
    -I include
    -I lib/Sabertooth
    -I lib/USB_Host_Shield
    ; GIT_REV = short commit SHA, sent as "fw" in the WDP Device Announce.
    ; python3, not python: macOS has no python on PATH, and $PYTHONEXE is not expanded here.
    !python3 scripts/git_rev.py
```

- [ ] **Step 6: Check the define reaches the build**

Run: `pio run -e SHADOWC2B5 -t envdump 2>&1 | grep GIT_REV`
Expected: a line like `('GIT_REV', '\\"<sha>-dirty\\"'),`. It says dirty because `platformio.ini` is
uncommitted, which is expected here.

Run: `pio run -e SHADOWC2B5 2>&1 | grep -E "RAM:|Flash:|SUCCESS|FAILED"`
Expected: `SUCCESS`, and RAM and flash unchanged from baseline (nothing uses `GIT_REV` yet).

- [ ] **Step 7: Commit**

```bash
git add scripts/git_rev.py platformio.ini
git commit -F - <<'EOF'
Inject the git revision into the firmware build as GIT_REV

scripts/git_rev.py runs as a PlatformIO dynamic build flag and defines
GIT_REV as the short commit SHA, "-dirty" when tracked files are modified,
or "unknown" without git. It is capped at 14 characters of [0-9a-z-]
because it becomes the fw field of the WDP Device Announce.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01YEjbiB6wzzgdcCiwuUbCe5
EOF
```

---

### Task 3: Send the announce from `main.cpp`

**Files:**

- Modify: `src/main.cpp`:
  - the include block (lines 86–91)
  - a new section just before the `setup()` section banner (line ~400)
  - the end of `setup()` (`randomSeed` at line ~447)
  - `loop()` (`toggleSettings();` at line ~1756)

**Interfaces:**

- Consumes:
  - `wdp::AnnounceTimer`, `wdp::kFirstDelayMinMs`, `wdp::kFirstDelayMaxMs`, `wdp::kIntervalMinMs`,
    `wdp::kIntervalMaxMs`, `wdp::kMaxLineLen` and `wdp::kMaxFwLen` (Task 1).
  - The `GIT_REV` macro (Task 2).
- Produces: `void wdpAnnounceTick()` and the globals `kWdpAnnounceLine` and `wdpTimer`. Nothing else depends on
  them.

This task has no unit test of its own. The logic is covered by Task 1, and the glue is checked by the
`static_assert`s, the build, `pio check`, and the on-droid verification after flashing.

- [ ] **Step 1: Include the library**

In `src/main.cpp`, change the include block to:

```cpp
#include <Arduino.h>

#include <PS3BT.h>
#include <usbhub.h>
#include "../lib/Sabertooth/Sabertooth.h"
#include "../lib/WdpAnnounce/WdpAnnounce.h"
#include "../include/config.h"
```

- [ ] **Step 2: Add the announce section**

At lines ~398–406, right after the closing `}` of `simpleMarcDuinoDome()`, the file reads:

```cpp
}

// =======================================================================================
//                          Main Program
// =======================================================================================

// =======================================================================================
//                          Initialize - Setup Function
// =======================================================================================
void setup() {
```

Insert this block immediately **before** the `Main Program` banner, after the blank line that follows `}`:

```cpp
// =======================================================================================
//                     WDP Device Announce to WCB 1
// =======================================================================================
// Every 25-30 s SHADOW tells WCB 1 what it is, so the WCB mesh labels this port "Shadow"
// and ?WDP,DA on WCB 1 shows the firmware revision. Announce only: nothing is read back.
// The line is sent only when Serial1's TX buffer can take all of it, so it never blocks.

#ifndef GIT_REV
#error "GIT_REV is not defined: platformio.ini must run scripts/git_rev.py as a build flag"
#endif

static const char kWdpAnnounceLine[] PROGMEM = "@WDP1 {\"type\":\"Shadow\",\"fw\":\"" GIT_REV "\"}\r";
static_assert(sizeof(kWdpAnnounceLine) - 1 <= wdp::kMaxLineLen,
              "WDP announce line must fit in Serial1's TX buffer or it is never sent");
static_assert(sizeof(GIT_REV) - 1 <= wdp::kMaxFwLen, "GIT_REV must be 14 characters or fewer");

wdp::AnnounceTimer wdpTimer;

void wdpAnnounceTick() {
    const size_t lineLen = sizeof(kWdpAnnounceLine) - 1;
    if (!wdpTimer.ready(millis(), Serial1.availableForWrite(), lineLen)) {
        return;
    }
    Serial1.print(reinterpret_cast<const __FlashStringHelper *>(kWdpAnnounceLine));
    wdpTimer.markSent(millis(), random(wdp::kIntervalMinMs, wdp::kIntervalMaxMs + 1));
}

```

- [ ] **Step 3: Arm the timer at the end of `setup()`**

In `setup()`, change:

```cpp
    randomSeed(analogRead(0));  // random number seed for dome automation

    Serial1.print("P1\r");  // Set Magic Panel to Always On
}
```

to:

```cpp
    randomSeed(analogRead(0));  // random number seed for dome automation

    Serial1.print("P1\r");  // Set Magic Panel to Always On

    wdpTimer.start(millis(), random(wdp::kFirstDelayMinMs, wdp::kFirstDelayMaxMs + 1));
}
```

- [ ] **Step 4: Call the tick from `loop()`**

In `loop()`, change:

```cpp
    toggleSettings();
    printOutput();
```

to:

```cpp
    toggleSettings();
    wdpAnnounceTick();  // after the readUSB() fault return: no announce while a controller fault is active
    printOutput();
```

Check that this is the only `toggleSettings();` call inside `loop()`: `grep -n "toggleSettings();" src/main.cpp`.

- [ ] **Step 5: Build the firmware**

Run: `pio run -e SHADOWC2B5 2>&1 | grep -E "RAM:|Flash:|warning|error|SUCCESS|FAILED"`
Expected:

- `SUCCESS`, with the `static_assert`s passing.
- RAM near 3855 B, up by at most about 20 B (the line itself is in flash).
- Flash up by at most about 400 B from 39572 B.
- Exactly the same 4 baseline warnings, none new.

If there is a `random()` ambiguity or a narrowing error, stop and report it rather than casting blindly.

- [ ] **Step 6: Confirm the line that is actually compiled in**

Run: `strings -a .pio/build/SHADOWC2B5/firmware.elf | grep "@WDP1"`
Expected: `@WDP1 {"type":"Shadow","fw":"<sha>-dirty"}`. It is dirty because `main.cpp` is uncommitted.

- [ ] **Step 7: Run clang-tidy**

Run: `pio check -e SHADOWC2B5 2>&1 | grep -E "WdpAnnounce|wdpAnnounce|kWdpAnnounce|wdpTimer|Total"`
Expected:

- `Total 0 <m> 0`: no High defects.
- No defect lines mentioning `WdpAnnounce.h`, `wdpAnnounceTick`, `kWdpAnnounceLine` or `wdpTimer`.

If a medium defect points at the new code, fix it when the fix is straightforward. If the only defect is the
`reinterpret_cast` to `__FlashStringHelper` (the Arduino `F()` idiom), leave it and note it in the task report.

- [ ] **Step 8: Re-run the host tests**

Run: `pio test -e native`
Expected: `9 Tests 0 Failures 0 Ignored`.

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -F - <<'EOF'
Send a WDP Device Announce to WCB 1 every 25-30 seconds

SHADOW now sends @WDP1 {"type":"Shadow","fw":"<rev>"} on Serial1, first
0.5-3 s after boot and then at a fresh random 25-30 s interval. The line is
written only when Serial1's TX buffer can take all of it, so loop() never
blocks, and static_asserts keep it within that buffer. No announce is sent
while a controller fault holds loop() at the readUSB() return.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01YEjbiB6wzzgdcCiwuUbCe5
EOF
```

---

### Task 4: Document the announce and the test env in CLAUDE.md

**Files:**

- Modify: `CLAUDE.md` (the commands block at lines ~17–25, line 27, and Serial Interfaces at line ~50)

**Interfaces:** none.

- [ ] **Step 1: Add the test command**

In the `bash` commands block, after the `pio check` line, add:

```bash
pio test -e native                       # Host unit tests (lib/WdpAnnounce)
```

- [ ] **Step 2: Correct the environment sentence**

Replace:

```
The only environment is `SHADOWC2B5`. Each build also copies the firmware to `build/` via `scripts/post_build.py`.
```

with:

```
The firmware environment is `SHADOWC2B5`, the default. Each build also copies the firmware to `build/` via
`scripts/post_build.py`, and `scripts/git_rev.py` defines `GIT_REV` (the short commit SHA, `-dirty` if modified).
`native` runs host unit tests only; the firmware env ignores all test suites, so `pio test` never uploads to the Mega.
```

- [ ] **Step 3: Describe the announce under Serial Interfaces**

Replace the Serial1 bullet:

```
- **Serial1 (9600)**: commands to the WCB (Wireless Communication Board), which routes them to the HCR Vocalizer, holoprojectors, and other boards
```

with:

```
- **Serial1 (9600)**: commands to the WCB (Wireless Communication Board), which routes them to the HCR Vocalizer, holoprojectors, and other boards. Every 25-30 s it also carries a WDP Device Announce, `@WDP1 {"type":"Shadow","fw":"<GIT_REV>"}\r` (`lib/WdpAnnounce`, `wdpAnnounceTick()`), sent only when the TX buffer can take the whole line. WCB 1 labels the port "Shadow" in the mesh; `?WDP,DA` on WCB 1 shows `fw`.
```

The WCB 1 port number is added after flashing, once the first announce reveals it. That is not part of this task.

- [ ] **Step 4: Check and commit**

Run: `git diff --stat` and confirm that only `CLAUDE.md` changed.

```bash
git add CLAUDE.md
git commit -F - <<'EOF'
Document the WDP Device Announce and the native test env

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01YEjbiB6wzzgdcCiwuUbCe5
EOF
```

---

### Task 5: Final verification on the committed tree

**Files:** none. This task only verifies.

- [ ] **Step 1: Clean tree, clean build**

Run:

```bash
git status --porcelain --untracked-files=no
pio run -e SHADOWC2B5 -t clean >/dev/null
pio run -e SHADOWC2B5 2>&1 | grep -E "RAM:|Flash:|warning|SUCCESS|FAILED"
strings .pio/build/SHADOWC2B5/firmware.elf | grep "@WDP1"
git rev-parse --short=7 HEAD
```

Expected:

- `git status` prints nothing.
- `SUCCESS`, with only the 4 baseline warnings.
- The compiled line shows `fw` equal to the SHA printed by `git rev-parse`, with **no** `-dirty`.
- The line length (count it, including the trailing `\r`) is 39 bytes for a 7-character SHA.

- [ ] **Step 2: Tests and lint**

Run: `pio test -e native` and `pio check -e SHADOWC2B5 2>&1 | grep Total`
Expected: `9 Tests 0 Failures`, and `Total 0 <m> 0` with `m` within the Task 3 result.

- [ ] **Step 3: Record the build**

Note the new `build/SHADOWC2B5_<timestamp>.hex` that `scripts/post_build.py` produced, its commit SHA, and the
RAM and flash figures. These go in the report to the owner for the flash step. Do not commit anything in `build/`
unless `git status` shows it as tracked and modified; check first with `git check-ignore build/`.

---

## After the plan: flashing (owner-run, not part of execution)

When the branch is finished and merged, or before merging if the owner prefers, the owner flashes the Mega ADK
following the spec's "Flashing and verification" section and the handoff's commands. Steps that need the owner:

1. Coordinate with session `droidnetsignalbooster-1d`.
2. Motor power off and wheels up.
3. VBUS reading.
4. Dump the current flash.
5. Flash as **Arduino Mega**, never Auto-detect.
6. Check `[WDP-DA] S<n>: Shadow fw <sha>` in `/api/serial/history`.
7. Add `?LABEL,S<n>,Shadow` on WCB 1.
8. Press buttons across several announce windows.
9. Then record the port in CLAUDE.md (Serial Interfaces), as a follow-up commit.
