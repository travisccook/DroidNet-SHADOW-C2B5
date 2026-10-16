# Controller Addresses at Runtime

Design, 2026-10-16. Approved by the owner in conversation; this is the written spec for review.

## Why

SHADOW accepts only the PS3 Navigation controllers whose Bluetooth addresses it knows. Today those
addresses are compiled in from `include/controller_macs.h`, so a firmware file can't be shared: a
build without your addresses accepts no controller, and finding an address needs a `SHADOW_DEBUG`
build. The owner wants to publish a ready-to-flash `.hex` that anyone can flash with DroidNet's
Firmware tab, then add their controllers without building anything.

The controllers don't have to work straight after flashing. The owner adds them by hand.

## What the owner does

1. Download the `.hex` from the GitHub Release.
2. Plug SHADOW's USB into the Signal Booster. In DroidNet's **Firmware** tab, pick the Mega, choose
   the file, set Board Type to **Arduino Mega** (not Auto-detect), and flash. DroidNet's flasher runs
   `avrdude -c wiring -b 115200 -D`, so the EEPROM (button map and controller list) is kept.
3. Pair each controller with the dongle over USB, as today (USB Host Shield 2.0's procedure).
4. Open DroidNet's **Serial** tab on SHADOW's USB port at 115200 and press PS on the controller.
   SHADOW refuses it, as today, and prints `@SM REFUSED F 00:1A:2B:3C:4D:05 *<crc>`.
5. Type `@SM MAC F 00:1A:2B:3C:4D:05` and press Enter. SHADOW replies
   `@SM OK MAC F 00:1A:2B:3C:4D:05 *<crc>`. Press PS again: the controller connects.

The same requests work through the WCB (with the CRC), so DroidNet can offer this in its SHADOW tab
later. Nothing in DroidNet has to change for the steps above.

## Protocol additions (still v4)

DroidNet accepts `READY v1` to `v4` only, so a version bump would cut today's DroidNet off. These
are additions: no existing request, reply or event changes, and `HELLO` is unchanged. A client finds
out whether SHADOW has them by sending `MAC`: firmware without them answers `ERR verb`.

Slots: `F` (foot), `D` (dome), `F2` (backup foot), `D2` (backup dome). An address is
`HH:HH:HH:HH:HH:HH`, hex in either case. SHADOW sends upper case, two digits a byte, most
significant byte first: the form `formatBtAddress()` prints.

| Request             | Reply                                                                                                                                                                                                                                                                              |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MAC`               | Four lines, one per slot in the order F, D, F2, D2: `MAC <S> <address\|-> <src>`, sent one a pass like DUMP. `src` is `eeprom` (set over `@SM`) or `default` (from the build, `-` if the build has none). Refused with `ERR busy` while another operation runs, like DUMP and SUM. |
| `MAC <S>`           | `MAC <S> <address\|-> <src>`. Always answered, like GET.                                                                                                                                                                                                                           |
| `MAC <S> <address>` | `OK MAC <S> <address>` once written and verified.                                                                                                                                                                                                                                  |
| `MAC <S> -`         | `OK MAC <S> -`. The slot has no controller, even if the build has an address for it (`src=eeprom`).                                                                                                                                                                                |
| `MAC <S> default`   | `OK MAC <S> default`. The slot is back on the build's address, if any (`src=default`).                                                                                                                                                                                             |

The three forms that change a slot are write-type operations: one at a time with SET, REVERT,
DEFAULTS, PREFIX, SUFFIX, DUMP, SUM and the `MAC` list, and refused with `ERR busy` otherwise.

New event:

| Event                      | When                                                                                                                                                                                                             |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `REFUSED <F\|D> <address>` | A paired controller whose address isn't allowed for that role tried to connect. `F` or `D` is the role SHADOW checked it for: which of the two PS3BT instances (`PS3NavFoot`, `PS3NavDome`) took the connection. |

`REFUSED` goes to the USB port every time, without `EVENTS 1`, because that is where the owner looks
for it; if the USB port has no room for the line, it waits there for a later pass. On the WCB port it is an ordinary event: sent only to the port that sent `EVENTS 1`, and like `CTL`
it waits for room (never while a press waits); a newer refusal replaces it. DroidNet's `parse_event()`
returns `None` for event words it doesn't know, so today's DroidNet ignores it. If two refusals come
before the first is sent, the newest wins.

New error code: `addr`, not a valid address. `00:00:00:00:00:00` and `FF:FF:FF:FF:FF:FF` are
refused with it (the USB Host library's `disc_bdaddr` is all zero before any connection).

Lengths: the longest request, `@SM MAC F2 00:1A:2B:3C:4D:05 *XXXXXXXX`, is 38 characters; the longest
reply, `@SM MAC F2 00:1A:2B:3C:4D:05 default *XXXXXXXX`, is 46. Both are far inside the limits (72, 96).

## Storage

Four records after the button map, at `0xF70` (`MapStore::kFootprint`, 3952), 9 bytes each, in the
order F, D, F2, D2, ending at `0xF94`:

```
0xF70  F   state (1 B), address (6 B, most significant byte first), CRC16 (2 B)
0xF79  D
0xF82  F2
0xF8B  D2
0xF94  free
```

- `state`: `0x01` = this address, `0x02` = no controller. The CRC16 (the store's existing CRC) covers
  the slot index, the state and the address.
- An erased record (all `0xFF`), a record that fails its CRC, an unknown state, or an all-zero or
  all-`FF` address reads as "not set": the slot uses the build's address.
- `MAC <S> default` erases the record to `0xFF`.
- The records are independent of the button map. They carry no generation, so `DEFAULTS` doesn't
  touch them, and a new map layout wouldn't either.
- They're written by `MapStore`'s existing job engine (a new job kind): one byte a pass when the
  EEPROM is idle, bytes that already match skipped, read back and verified, one retry, then
  `ERR eeprom`. `MapStore::busy()` covers them, so only one write-type operation runs at a time.
- They're read once, in `begin()`, with the rest of the store.
- Older firmware never reads past `0xF70`, so flashing back is safe, and the records survive it.
- CLAUDE.md's EEPROM note changes from 3952 bytes to 3988 (`0xF94`).

## The connect check

- `MapStore` (in `lib/ShadowMap`, no Arduino dependency, so it tests on the host) holds, through
  `setBuiltInMac()`, `macFor()` and `macAllows()`, the effective address of each slot: the EEPROM
  record when it's set, else the build's address. `macAllows(role, bdaddr)` compares raw bytes with the address the controller's `PS3BT` recorded
  when it claimed the connection (`getBdaddr()`, from the Connection Complete event; least significant
  byte first). Not `Btd.disc_bdaddr`: every incoming connection request overwrites that, and `onInit`
  runs about 2 s after the claim, so two controllers powering on together could swap addresses:
  the foot role accepts F or F2, the dome role D or D2, as today. An empty slot matches nothing.
- The build's addresses come from `include/controller_macs.h`, parsed once at boot by
  `sm::parseMac()` (`lib/ShadowMap/src/sm_mac.h`; the protocol uses it too, and the library can't
  include the project's `include/`). The template's
  `XX:XX:XX:XX:XX:XX` doesn't parse, so it means no address.
- `controller_macs.h` becomes optional: `config.cpp` includes it when it exists
  (`__has_include`), and otherwise uses no built-in addresses. A build without it accepts no
  controller until one is added over `@SM`. C2-B5's build, which has the file, behaves as today.
- A refused controller is handled exactly as today (motors stopped, controller dropped), and its
  address and role go to the protocol for the `REFUSED` event. The `SHADOW_DEBUG` print stays.
- **A change that revokes a connected controller drops it at once.** `main.cpp` keeps the address of
  each connected controller (read when it connects). When the list changes (a change counter), each
  connected controller is checked again; one that's no longer allowed goes through the same path as
  a refused connection: motors stopped, controller dropped. A mistyped `MAC F -` stops the droid,
  which is the safe way to fail.

## Timing

Every mapping pass stays under 1 ms on the emulated Mega, and the timing suites enforce it:

- A `MAC` request is handled in a request pass like GET or SET. Its writes happen one byte a pass in
  the write phase, its reply in the background unit.
- `REFUSED` is built and sent in the background unit, like `CTL`, only when the port has room.
- The connect check compares 12 bytes at most per controller, once per connection or list change.
- The timing suites gain a `MAC` set and a `REFUSED` event.

## Release

- `scripts/build_release.sh` builds the release `.hex` from a clean checkout of a commit (a temporary
  `git worktree`), where `include/controller_macs.h` doesn't exist. It then decodes the `.hex` and
  fails if any address from the local `include/controller_macs.h` appears in it, as text or as
  bytes in either order. The result is `build/release/SHADOWC2B5_<commit>.hex`.
- The owner tags the commit and creates the GitHub Release with that file attached
  (`gh release create`). SHADOW's announce and `READY` report the commit as `fw`.

## Docs

- README: a "Flash with DroidNet" section (download, Firmware tab, Board Type Arduino Mega, motor
  power off and wheels up) and "Allow your controllers" (Serial tab, `REFUSED`, `MAC`), with the
  source build kept for people who want to compile addresses in.
- `WIPDocs/COMMAND_MAPPING_DESIGN.md`: the `MAC` requests, the `REFUSED` event, the `addr` error and
  the EEPROM records.
- `include/controller_macs.example.h`: say the file is optional.
- CLAUDE.md: the EEPROM footprint and the optional `controller_macs.h`.
- Tell the DroidNet session about `MAC` and `REFUSED`, for a Controllers panel later.

## Testing

Test-first, on the host (`native`) and the emulated Mega (`simavr`):

- `sm::parseMac()`: valid forms, either case, wrong lengths and separators, bad digits, all-zero
  and all-`FF`.
- Records: write, read back, CRC failure, erased, unknown state, power cut mid-write (a torn record
  reads as not set), `default` erasing, `DEFAULTS` leaving them alone, survives `begin()`.
- `MapStore::macFor()` / `macAllows()`: precedence (EEPROM over build, `-` blocking a built-in address), role matching
  (F/F2 for foot, D/D2 for dome), byte order against the library's (least significant byte first), empty slots matching nothing.
- Protocol: each `MAC` form and its reply, `ERR args`, `ERR addr`, `ERR busy`, CRC rules per port,
  `REFUSED` on USB without `EVENTS`, on WCB only with `EVENTS 1`, newest refusal wins.
- Revocation: a change that removes a connected controller's address drops it; one that doesn't,
  doesn't.
- Timing suites: a `MAC` set and a `REFUSED` event, every pass under 1 ms.
- Transcripts: the existing goldens don't change.
- `scripts/build_release.sh`: fails when a local address is in the `.hex`, passes for the clean build.
- Bench test on C2-B5 with the wheels up, owner present: flash a release build through DroidNet,
  see `REFUSED`, add the four addresses with `MAC`, connect, revoke one while connected (the droid
  stops), add it back, drive check.

## Not in this change

- A Controllers panel in DroidNet's SHADOW tab (DroidNet's repo, later).
- A `mac` command in the reference client `tools/shadowmap` (add it when DroidNet ports the panel).
- Learning a controller's address automatically when it's paired.
