# DroidNet Integration Plan: SHADOW Button Map

**Status:** plan only. Nothing in DroidNetSignalBooster has changed yet.

**Related docs:**
- The firmware side and the protocol reference: [COMMAND_MAPPING_DESIGN.md](COMMAND_MAPPING_DESIGN.md)
- The UI design and clickable mockup: [droidnet-ui/](droidnet-ui/), with a note at the top on
  where the firmware differs from what the design assumed
- A working reference client for everything below: `tools/shadowmap/shadowmap.py`, tested
  against the firmware's own protocol code

DroidNet file references are to `travisccook/DroidNetSignalBooster` at `66e2448` (v2.3.5).

## The short version

- **Transport:** DroidNet talks to SHADOW through the WCB it's already plugged into, using the
  always-on WCB listener it already runs.
- **Requests** go out as `;S<x>@SM ... *<crc>`. **Replies** come back as `@SM ...` lines on the
  same USB stream.
- **SHADOW's S-port is found automatically** from SHADOW's `@WDP1` announce.
- **Map reads:** a `SUM` check (about 0.2 s) decides whether the full `DUMP` (about 4 s) is
  needed.
- **Edits are single `SET` / `REVERT` requests,** about 0.15 s each. The controllers stay
  connected.
- **Never open the Mega's USB port for this.** Opening it resets SHADOW.

## What DroidNet already has to build on

| Need | Existing piece |
| --- | --- |
| Send a line to the WCB | `send_wcb_command(port, cmd, expect_reply=False)` (`web/handlers/wcb_handlers.py:912`); `PortHandler.write()` appends `\n`, which the WCB accepts |
| Receive lines from the WCB | Passive matchers on the always-on listener's `"data"` events: `_subscribe_passive_matchers` (`web/server.py:1024`), the same way `victron_request_responder.on_data` (`scripts/victron_request_responder.py:114`) does it. Events carry `line`, `port`, `direction` (skip `"sent"`). |
| Listener lifecycle | `wcb_alwayson.json` plus `resolve_alwayson_targets` (`web/handlers/wcb_handlers.py:350`) re-keys by `device_id` across `/dev` renames |
| Stored-command picker | `GET /api/comlink/stored-commands` (keys from the WCB backup plus live `?SEQ,NAMES`) |
| Creating a stored command | WCB tab's `buildWCBStoreCommand` (`web/js/app.js:7128`) → `?SEQ,SAVE,key,value` |
| Feature-tab template | CHiRP: `web/handlers/chirp_handlers.py`, `scripts/chirp_*.py`, `web/js/chirp.js`, `TAB_LABELS` / `FEATURE_GATED` in `web/js/tab-manager.js` |
| Config files | `utils/config_helpers.py:63` `save_json_config` (atomic), under `/opt/droidnet/config` |
| Confirm dialog for destructive writes | Maestro EEPROM-overwrite confirm (`web/js/maestro.js` ~1918-2030) |
| Flashing SHADOW | Firmware tab uses `avrdude -D`, which keeps the EEPROM mapping |

## Components to add

```
scripts/shadow_link.py          ShadowLink: protocol client on the WCB listener (port of
                                tools/shadowmap/shadowmap.py's Client, made asyncio-safe)
web/handlers/shadow_handlers.py HTTP + SSE endpoints (UI_DESIGN.md 4.3, minus the lock)
web/js/shadow.js                the tab (UI_DESIGN.md section 3)
web/css/shadow.css              the controller graphic, callouts, grid
web/index.html, tab-manager.js  the tab, FEATURE_GATED like CHiRP
/opt/droidnet/config/shadow-map.json
                                route, labels, cached map (72 slots + SUM), last seen
tests/unit/test_shadow_link.py  against shadow_sim through the WCB emulator (see Testing)
```

### ShadowLink (`scripts/shadow_link.py`)

- **Construction.** Built once in `server.py main()` and injected like the other singletons:
  `shadow_handlers.shadow_link = ...`. Add `"shadow_link"` to the attribute list in
  `_subscribe_passive_matchers` / `_unsubscribe_passive_matchers` so it sees the Pi's WCB stream.
- **`on_data(event)`,** on the event loop. It must be cheap and must never raise:
  - Drop `direction == "sent"`.
  - Keep only lines that *start with* `@SM ` (with WCB debug on, `Sent to USB: @SM ...` lines also
    appear) or `[WDP-DA]`.
  - Check the CRC with `shadowmap.unframe()`.
  - A reply resolves the pending request's future.
  - `PRESS` / `CTL` lines go to the SSE fan-out.
  - `[WDP-DA] S<x>: Shadow fw <ver>` updates the discovered route.
- **Requests: one in flight, FIFO.** Each has a timeout (1 s; `DUMP` gets about 8 s) and up to 3
  resends on silence, `ERR crc` or `ERR busy`. Everything is idempotent.
  - Sends use `send_wcb_command(..., expect_reply=False)` from a worker, not the loop thread: it's
    the synchronous wrapper.
  - Never call anything that runs `lsusb` on the loop thread.
- **Background ops for slow reads.** `DUMP` runs as an op with progress events (`done/72`), so no
  HTTP thread waits 4 s. The Pi Zero 2 W has 6 HTTP workers.
- **States:** `no_route | connecting | reading | ready | wcb_offline | wizard | no_reply | unsupported`.
  - `wizard`: the Wizard's exclusive mode pauses the listener, and `send_wcb_command` fails fast
    with `reason: "wizard_active"`. Wait it out.
  - `unsupported`: READY says `v` > 1.
- **Keeping the session alive.** SHADOW keeps the reply route and the EVENTS subscription in RAM,
  and a reboot clears both.
  - While a tab is open, send `HELLO` every 20 s and `EVENTS 1` after each `READY`. Both are
    cheap and idempotent.
  - After a request goes unanswered through all its retries, send `HELLO` (and `EVENTS 1`)
    before giving up. The reference client does this.
  - With no tab open, send `EVENTS 0` once and stop polling.

### Route and discovery

- **The owner's droid:** SHADOW and the Pi share one WCB.
  - The route is `;S<x>` and HELLO's argument is `0`. The WCB number isn't needed.
  - `WcbRoute(shadow_port=x)` in the reference client.
- **Finding `x`:**
  1. **Passive:** the WCB prints `[WDP-DA] S<x>: Shadow fw <ver>` on USB the first time it hears
     SHADOW's announce, 5 s after SHADOW boots and every 30 s after. ShadowLink sees it on the
     listener stream.
  2. **Active ("Detect" button):** send `?WDP,DA`. The WCB lists `  S<x>  Shadow  fw <ver>  <n>s ago`
     and `caps: shadow.map`.
  3. **Manual override** in the tab's Route dialog.
- **Remote WCB (not needed today):** SHADOW on WCB `n`, the Pi on WCB `m`. Requests become
  `;W<n>;S<x>...` (`;W<nn>,;S<x>...` for 10 and above), and HELLO carries `m`. It's modelled in the
  reference client and its tests, but not tested on hardware.
- **Optional tidy-up:** offer `?BCAST,OUT,S<x>,OFF` on SHADOW's port, with a confirm. SHADOW
  ignores broadcasts anyway, so this only cuts noise.

### Reading and caching the map

1. `HELLO` → `READY v1 fw=... gen=... fmt=... pads=...`
2. `SUM`. Compare it with `shadowmap.map_sum(cached_slots)`.
3. If they differ, or there's no cache: `DUMP`, then cache the 72 slot lines together with the
   SUM.
4. `CFG` → prefix and suffix, shown in the Route dialog and used for "this sends `;W1;Cscream`".

The cache is also what the tab shows (marked "saved copy") when SHADOW is off or unreachable.

### Edits

| UI action | Request | Then |
| --- | --- | --- |
| Pick a stored command | `SET <slot> <key>` | `OK SET ...`; update the cache, recompute the cached SUM locally, send an SSE `slot` event |
| Clear | `SET <slot> -` | as above; `src=eeprom`, key empty |
| Use default | `REVERT <slot>` | `OK REVERT ...`; `src=default` |
| Reset all | `DEFAULTS` (with the Maestro-style confirm) | re-read the map (`SUM` differs, so `DUMP`) |
| Import / apply saved copy | `SET` / `REVERT` per changed slot, sequentially, with progress | `SUM` check at the end |

- **Validate keys client-side:** `[A-Za-z0-9_]{1,15}`.
  - SHADOW rejects anything else with `ERR key`.
  - The WCB would accept more (anything but a comma).
  - DroidNet's WCB tab only creates letters and digits.
- **Test-fire:** send the stored command the way SHADOW would, `;W1;C<key>`, through
  `/api/wcb/command`. Don't go via SHADOW.

### Events, for the live controller view

- With `EVENTS 1` on, a press arrives as `@SM PRESS F CROSS UP leia default from=D`. That covers
  the "Last press" line and "Find by pressing".
  - It is sent for every combo that reaches the mapping, mapped or not, so free slots can be
    found by pressing them.
  - The command still fires; there's no learn mode.
- `@SM CTL D 0` / `@SM CTL D 1` mark a controller dropping or connecting.
- Forward both as SSE `press` / `ctl` events (the UI doc's table).

## Where stored commands live

SHADOW sends `;W1;C<key>`, so the stored commands have to exist on **WCB 1**.

- **If the Pi's WCB is WCB 1,** the picker and "create new stored command" work directly.
- **If it isn't,** DroidNet can only list and write WCBs plugged into it. The picker would then
  need a remote `?SEQ` path, or the prefix would need to point at the Pi's WCB. (Question 1 below.)

## Gotchas collected from both codebases

- **Mega USB = reset.**
  - Don't make the Mega's USB port an always-on handler.
  - Don't point Comlink at it.
  - Don't open the serial monitor on it during a show: `PortHandler` toggles DTR on connect
    (`web/port_handler.py:351-362`), and Linux raises DTR on any open.
- **Line framing.**
  - `PortHandler` strips and normalises lines, and replaces bad UTF-8. That's fine, because the
    protocol is ASCII.
  - Never append `^?CHK` to SHADOW-bound lines. It's harmless (the WCB would verify and strip it)
    but pointless.
- **Busy WCB USB.** With WCB debug on, the USB stream gets busy. Filter strictly on `^@SM ` and
  `^\[WDP-DA\]`.
- **Retries after a reboot.** SHADOW replies `READY ... fmt=0` on a blank EEPROM. The first `SET`
  takes about 0.25 s (it formats). Other `SET`s take about 0.15 s.
- **Flashing.** A firmware upload through DroidNet's Firmware tab keeps the mapping (`-D`, no chip
  erase). If a future firmware changes the EEPROM layout, SHADOW reads all defaults and DroidNet's
  "saved copy ≠ SHADOW" drift notice offers to re-apply the saved copy.

## Testing the DroidNet side

- **Protocol logic:** port or vendor `tools/shadowmap/shadowmap.py`. Its tests (`test_shadowmap.py`)
  already cover:
  - framing, CRC and SUM
  - retries on a corrupted or dropped line
  - power cuts
  - remote routing
- **ShadowLink:**
  - Build `tools/shadowmap/shadow_sim` (one `g++` command, see its header) in the test setup.
  - Run ShadowLink against it through the `WcbEmulator` from `test_shadowmap.py`, adapted to feed
    ShadowLink's `on_data` and capture its writes.
  - This tests the real firmware logic, not a mock.
- **Frontend:** jest tests beside the existing ones in `web/js/__tests__`, covering the slot
  state machine (steady → sending → applied/failed) and the SSE handling.

## Rollout

1. Flash firmware 2.0.0 (DroidNet Firmware tab, or `pio run -t upload`).
2. On DroidNet's WCB tab, watch for `[WDP-DA] S<x>: Shadow fw 2.0.0` within about 35 s. That's
   SHADOW's port.
3. From the Pi, check the round trip with the reference client. Stop DroidNet's always-on
   listener for the WCB port first, because two programs can't share a serial port:

   ```
   python3 shadowmap.py --wcb /dev/ttyUSB0 --shadow-port <x> dump
   ```

4. Create the volume and brightness stored commands on WCB 1, then
   `shadowmap.py ... set D PS UP <key>` and so on, and try them on the controller.
5. Build ShadowLink and the endpoints, test them against `shadow_sim`, then build the tab from the
   mockup.

## Open questions for the owner

1. **Stored commands:** is the Pi's WCB also WCB 1, where SHADOW's `;W1;C` commands run? If not,
   the picker and "create new stored command" need a remote path.
2. **Labels:** per stored-command key (shown wherever that key is used; the UI doc's
   recommendation) or per combo?
3. **Tab:** name "SHADOW", hidden until enabled like CHiRP?
4. **Learn mode:** press to find a combo without firing it. It would need a small firmware
   addition with an automatic timeout so the droid can't be left muted. Worth it for shows?
5. **Phone layout:** the UI design trims DroidNet's 54 px side padding on this tab. Should that be
   this tab only, or everywhere?
