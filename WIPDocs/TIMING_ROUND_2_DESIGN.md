# Timing round 2: every mapping pass under 1 ms, motors first

**Status:** implemented on branch timing/round-2 (to be merged to main and tagged `stage/timing-round-2`), 2026-10-02 / 2026-10-03; not flashed.
**Base:** `main` at `52df1b9` (timing Part A, tagged `stage/timing-part-a`). The droid runs `0a8b73f`
(tagged `droid/c2-body-2026-09-29`).
**Delivers:** tag `stage/timing-round-2`. Raw commands (@SM v4) are rebased on top afterwards, under their own
plan (DroidNetSignalBooster `docs/superpowers/plans/2026-10-02-shadow-raw-commands.md`).
**Flashing:** none during this work. The owner flashes the final stage (after v4) and walks back by stage tag if
something is wrong.

## 1. Goal and the owner's rules

1. **Motor control is never compromised.** This outranks everything below. The foot and dome drive must run every
   pass, before any mapping work, and nothing the mapping does may delay a pass enough to matter or touch the motor
   path.
2. **Every mapping pass under 1 ms** on the emulated ATmega2560 at 16 MHz, in every case the timing suites measure,
   and the suites fail if one isn't.
3. **Behaviour as `0a8b73f`, except work deferred by one pass.** Strictly identical behaviour and rule 2 can't both
   hold: presses can't wait (their timing rules are fixed), and four presses alone take 990 us, so a request in the
   same pass can't fit. The owner chose (2026-10-02): **when work has to give way, it waits one pass**. Every line's
   bytes are unchanged, each port's lines keep their order, and nothing waits more than one pass except as stated
   in section 3.
4. **No button command is ever dropped silently** (two existing bugs, section 4).

## 2. Where Part A leaves it

`test_avr_timing_*` (126 figures, report only): 5 at or over 1 ms, all a press firing in the pass a request is
handled.

| Pass | Part A |
| --- | --- |
| A SET in flight and a long press | 1115 us |
| The worst GET, a long press and the drain | 1092.5 us |
| A press with its PRESS line, a SET mid-write | 1073 us |
| A press and a SET | 1072 us |
| A press and the worst GET | 1014 us |
| Four presses (no request) | 990.5 us |
| Two requests in the RX buffer, no press | 977.5 us |

The pieces: a press is about 290 us, and its PRESS event line (EVENTS on) adds about 280 us. `handleLine` is
180-590 us (GET the most). The background unit (protocol service, announce, battery report) is at most 610 us with
the drain.

## 3. The pass rule

`loop()` becomes (fault path in brackets):

```
readUSB()                         [fault: skip to "requests"]
footMotorDrive(); domeDrive()     motors first, as now
fired = mappedButtons()
toggleSettings()
requests or background            (the rule below)
batteries; EEPROM step; drain     every pass, faults included
printOutput()
```

Today `pump()` for both ports runs at the top of `loop()`, before `readUSB()` and the motors. It moves after them,
so a request no longer delays the motors' pass at all.

**The rule.** One pass does at most one heavy mapping unit besides its presses:

- **A pass where a press fired** (including a deferred press) reads no request bytes and runs no background unit.
  Bytes wait in the UART's 64-byte RX buffer (9600 baud fills about 1 byte a millisecond).
- **The first pass without a press after one that handled a request** runs the background unit and reads no
  request bytes (a press pass in between doesn't cancel the background unit it is owed).
- **Any other pass** pumps the ports (WCB first, then USB) until one request is handled or the bytes run out. If it
  handled a request, the pass ends there; otherwise it runs the background unit.

A request counts as handled when `handleLine` ran on it, answered or not (a GET of a slot the busy EEPROM holds
parses the whole line before it returns false). A request that can't be answered yet keeps `0a8b73f`'s rule: the
bytes behind it are read and dropped so the RX buffer can't overflow.

The rule lives in `sm::Runtime` (one call after the buttons, taking whether a press fired and whether the pass is
faulted), so it builds and is tested on the host; `loop()` only calls it.

**The background unit** is `0a8b73f`'s service work, unchanged inside: `Protocol::service()` (a DUMP line, a SUM step,
a write's reply, a CTL event), then the announce, then the battery report. Each still waits for room in the TX queue
as before. The EEPROM writer's step and the drain into Serial1 are not part of it: they run every pass.

**Why nothing starves.** A background-only pass happens only right after a request pass, so requests get at least
every other pass and the background unit always gets the pass after a request. Press passes can't come back to back
for long: presses fire on transitions and timers (the shared repeat is once a second).

**What changes on the wire** (each documented by `tools/shadowmap/test_transcripts.py` against `0a8b73f`):

- A request arriving in a press pass is handled one pass later (2-3 ms), so the press's command and PRESS event go
  out before its reply, and a SET in that pass takes effect after that press.
- A second request already in the RX buffer waits one pass (two in one pass measured 977.5 us).
- A background line (DUMP line, battery report) that would have gone out in a press pass or after a request goes
  out one pass later.
- READY in the first pass after a boot names that pass's controllers, not `pads=-` (`setPads` now runs before
  any request).
- When `@SM EVENTS 1` arrives in the pass a controller connects, that controller's `@SM CTL` event is no longer
  sent. The cause is the same as READY's `pads=`: `setPads` now runs before the request.
- A request queued right behind one that can't be answered yet is read and dropped, under the existing rule. The
  Pi sends one request at a time and resends on silence.
- A second request already in the RX buffer waits one pass, so a background line (for example a DUMP line) can go
  out before its reply.
- Button commands, and PRESS events of presses that waited, that `0a8b73f` dropped for want of TX room are now
  sent later, in order.

## 4. No dropped button commands

Two bugs, both in `0a8b73f`:

1. **The TX queue keeps room for one command.** `kCommandHeadroom` (34 bytes) is held back from replies and events,
   and `pushCommand` uses it. A second command in the same pass, or any command while a DUMP keeps the queue full,
   can find no room. `Runtime::fire()` then logs "[WCB queue full, dropped]" and the command is lost.
   **Fix:** a command that doesn't fit is deferred, exactly as a press is deferred while the EEPROM is mid-write.
   While a command waits for room, the WCB port takes no replies, events or announce (`canSend` is false), so the
   drain frees room for it. At 9600 baud 34 bytes drain in about 36 ms, well inside `kDeferMaxMs` (250 ms). A
   request arriving meanwhile meets the existing "can't be answered yet" rule (it waits; bytes behind it are
   dropped; the Pi resends on silence), as it does today whenever the queue is full.
2. **One deferred slot.** `fireOrDefer` stores a single deferred press, so a second deferral (two presses in one
   pass while the EEPROM is busy, or now a full queue) overwrites the first.
   **Fix:** a FIFO of 8 deferred presses (48 bytes). Each entry keeps the existing rule: dropped if its controller
   has gone or it is over 250 ms old. They fire in order at the start of `buttons()`, stopping at the first that
   still can't go, and a new press queues behind any that wait. A press that finds the FIFO full is counted
   (`Runtime::droppedPresses()`); nothing a person can do with two controllers fills it.
3. **At most three presses fire in one pass.** One pass can fire one press per controller plus one on the shared
   repeat timer, plus any that waited; releasing a queue of waiting presses could fire several at once. A press
   past the third waits one pass in the FIFO, in order. Three presses with their PRESS lines measure 867-891 us
   (Part A), four 990.5 us.

A deferred press's PRESS event goes out when it fires, as today.

**Decisions taken during implementation.** The hold is released at the start of `buttons()` and turned on as soon
as a press is kept waiting or newly deferred. It covers presses waiting on the EEPROM or on the three-a-pass
limit, not only presses waiting for room. A deferred press's PRESS event goes out when it fires.

## 5. The EEPROM writer's step

`MapStore::step()` in its write phase reads bytes until it finds one to write, so a step over bytes that already
match reads up to 32 of them (about 5 us each). **Fix:** at most 4 reads per step; the next step carries on. A
write takes a few more passes when many bytes already match; what is written, verified and replied is unchanged.

## 6. If presses alone are over 1 ms

With at most three presses a pass (section 4), a press-only pass measured 891 us at worst under Part A. The timing
task measured it again with this design. If any press-only pass reaches 1 ms, PRESS event lines move out of the press pass: the pass records the event (slot, key,
source, `from=`) and the background unit sends recorded events first, one per background pass, in order. The
button commands themselves always go out in the pass they fire. If no press-only pass reaches 1 ms, this section
is not built.

**Outcome:** Task 6a was not needed. The worst press-only pass is 892.0 us (three presses + a write's last step).

## 7. Motor safety

What protects the motors, and how each is checked:

| Rule | How it holds | Checked by |
| --- | --- | --- |
| Motors run every pass before any mapping work | `readUSB()` and the drive calls stay first; `pump()` moves after them | review of `loop()` (the order is in `main.cpp`); `test_scheduler` checks the rule in `Runtime` |
| A pass's mapping work is under 1 ms | section 3, enforced by the timing suites (`SM_TIMING_REPORT_ONLY` removed) | `pio test -e simavr` |
| The mapping never waits | no busy-waits outside `setup()`; EEPROM, UART and queue checks all return | existing tests, review |
| The mapping never touches Serial2 or motor state | `lib/ShadowMap` has no Arduino dependency; `main.cpp` glue only | review |
| Interrupts are off only briefly | `sm::Uart1`'s atomic blocks are a few instructions each; nothing new adds any | review |
| No debug output stalls the loop | `SHADOW_VERBOSE` off (below) | the build; `config.h` |
| RAM stays safe | static SRAM grows by at most 64 bytes over Part A (2895) | the build's RAM line, recorded in the design doc |

**Found while checking: blocking debug output. Owner's decision (2026-10-02): turn `SHADOW_VERBOSE` off.** With it
on (`include/config.h`), every press, ramp step and battery change adds a line to `output`, and `printOutput()` hands
it all to `Serial.println`, which blocks until everything but the last 64 bytes has gone out at 115200 (about 11.5
bytes a millisecond): several milliseconds per pass while driving, far more than any mapping pass. `0a8b73f` does
the same. Every `SHADOW_VERBOSE` block in `main.cpp` is logging only (plus `setup()`'s count of valid EEPROM slots),
so turning it off changes nothing else. Nothing reads the verbose text: DroidNet never opens SHADOW's USB port
(opening it resets the Mega) and talks to it only through the WCB; no DroidNet or SHADOW code or test matches any
verbose string.

With it off, the only thing that still writes to `output` in `loop()` is a USB `@SM` reply (bench only, 128-byte
rule, at most about 5.5 ms while someone has the USB port open). That stays as it is. For the bench check in
`WIPDocs/HANDOFF_BUTTON_MAP.md` that read `ACTIVATED:` lines, use `shadowmap.py --usb` with `EVENTS 1` (the PRESS
events carry the same slot, key and source), or turn `SHADOW_VERBOSE` back on for that session.

## 8. Testing

- Timing suites ("five presses in one pass" is superseded by the three-a-pass limit; the four-due figure, three
  then one, covers it): `SM_TIMING_REPORT_ONLY` removed, so any figure at or over 1 ms fails. New figures: five presses
  in one pass (with and without EVENTS), a press with each request kind arriving, two requests, the background unit
  in the pass after a request, presses while commands wait for room.
- `test_scheduler` (new): the rule's three cases; a press pass reads nothing; alternation under sustained requests
  (requests and background both progress); a DUMP and a stream of GETs both finish; the battery report and announce
  still go out during a DUMP; the fault path follows the rule.
- TX and deferral: several presses into a queue held full by a DUMP all send their commands, in order, none
  dropped; two presses deferred in one pass both fire; a deferred entry past 250 ms or whose controller left is
  dropped; while a command waits, no reply is queued.
- EEPROM step: a write over fully matching bytes reads at most 4 per step and ends with the same EEPROM image,
  verify and reply.
- `SHADOW_VERBOSE` off: the firmware builds without warnings, and the HANDOFF_BUTTON_MAP bench step is updated.
- Transcripts: every stream identical to `0a8b73f` except the differences listed in section 3, each named in
  `test_transcripts.py`.
- All of: `pio run`, `pio test -e native`, `pio test -e simavr --without-uploading`,
  `python3 -m unittest tools/shadowmap/test_shadowmap.py tools/shadowmap/test_transcripts.py`.
  One `pio test` at a time per checkout.

## 9. Out of scope

- Raw commands (@SM v4): rebased on this afterwards. Its longer lines (`kMaxLine` 96) fit: the reply room rule
  doesn't change, and a raw command (at most 16 bytes with its `\r`) fits the command headroom.
- The T1/T2 targets (750/900 us) stay as reported figures, not failures; the owner's rule is 1 ms.
- On-droid checks (real presses, driving, power cycle): with the final flash.
