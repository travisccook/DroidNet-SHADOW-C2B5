# SHADOW Button Map: UI Design

> **Where the firmware ended up (2.0.0).** This design was written against an earlier draft of the
> protocol. The firmware that was built differs in these places, and
> [../COMMAND_MAPPING_DESIGN.md](../COMMAND_MAPPING_DESIGN.md) is the reference. The mockup
> (`mockup.html`) still shows the older assumptions.
>
> - **No edit lock.** The owner chose to allow edits any time, so there's no lock notice, no
>   `@SM LOCK` event and no `lock=` field.
> - **An empty slot sends nothing.** That's question 1's recommendation, and it's what the
>   firmware does.
> - **Use default is `@SM REVERT <slot>`,** not `SET <slot> *`. A bare ` *` would read as the
>   start of the line checksum.
> - **The map checksum is `@SM SUM`** (a request, about 0.2 s), not a `map=` field in READY.
> - **`SLOT` lines** are `@SM SLOT <slot> <key|-> <src> def=<key|->`, or
>   `@SM SLOT <slot> - reserved res=<reason>`.
> - **Reserved reasons are `psgate` and `overspeed` only.** PS alone, PS+PS, PS+CIRCLE and PS+CROSS
>   never reach the mapping, so they share `psgate`. The drive-stick toggles never produce a
>   press.
> - **`PRESS`** is `@SM PRESS <slot> <key|-> <src> [from=<F|D>] [res=<reason>]`. It's sent for
>   every combo that reaches the mapping, mapped or not.
> - **Controllers:** `READY ... pads=<FD|F|D|->`, plus `@SM CTL <F|D> <0|1>` events with EVENTS on.
> - **No learn mode yet.**
> - **Keys:** SHADOW accepts `[A-Za-z0-9_]{1,15}`. The WCB itself accepts anything but a comma, up
>   to 15 characters. DroidNet's WCB tab only offers letters and digits.
> - **Discovery:** SHADOW announces `@WDP1 {"type":"Shadow",...}` every 30 s. The Pi's WCB prints
>   `[WDP-DA] S<x>: Shadow fw ...` when it first hears it, and `?WDP,DA` lists it.


**Status:** design pass for the runtime mapping in `WIPDocs/COMMAND_MAPPING_DESIGN.md`. **Where it runs:** a new
feature-gated **SHADOW** tab in DroidNet Signal Booster (`web/index.html`).

**Mockup:** `mockup.html` (self-contained, offline). It uses DroidNet's real tokens and component CSS, and its fake data
comes from `src/config.cpp` and the `;W1;C<key>` strings in `src/main.cpp`. Open it in a browser. The purple **MOCKUP
STATES** pill (bottom left) switches link, lock, failure, drift, theme and simulated controller presses. URL presets for
screenshots include `?ctl=D&layer=PS&sel=UP`, `&lock=1`, `&link=no_reply`, `&view=grid`, `&sheet=1`, `&dark=1`,
`&drift=1`, `&press=F.L2.LEFT` and `&clean=1`, which hides the panel.

| Screenshot | Shows |
| --- | --- |
| `mockup-desktop.png` | 1280 px, Foot, Hold L1. A free slot was just assigned (Applied, Custom, Undo toast), with a live press ticker |
| `mockup-phone.png` | 390 px, Foot, Hold L2, locked, after a live press |
| `mockup-phone-editor.png` | 390 px editor bottom sheet, mid-assignment ("Sending") |
| `mockup-grid.png` | "All combos" overview, both controllers (also the print layout) |
| `mockup-dome-missing.png` | Dome, Hold PS: volume keys that don't exist on WCB 1 yet |

---

## 1. Users and their top tasks

| User | Context | Device |
| --- | --- | --- |
| **Builder/owner** | Garage or bench. Plans the map, creates stored commands, keeps the cheat sheet current | Laptop, desktop |
| **Operator at a show** | Outdoors in bright sun, a controller in one hand, often handing it to a helper | Phone |
| **Helper** | Borrows a controller and needs to know what each combo does | Printed sheet, or the owner's phone |

Top tasks, most frequent first:

1. **Look up** what a combo does, or where a command lives ("what's L2 + Left?", "where's Cantina?").
2. **Reassign or clear** a combo and be sure the droid now does it, without reflashing or losing the controllers.
3. **Find a free combo** for a new command, and create that stored command if it doesn't exist yet.
4. **Test** a command from the UI, or press a combo on the real controller and see which slot it hit.
5. **Keep a record:** print the cheat sheet, export/import, reset to defaults, and notice when SHADOW and the saved
   copy disagree.

What the owner already does: both reference PNGs draw the wand with callout boxes grouped by modifier ("L1 Plus",
"PS Plus", "○ Plus", each listing four arrow icons), and a separate "Stick Toggles" box for hardcoded combos.
`ControlMapping.pdf` is the same information as two lists grouped by modifier. So the owner thinks in **modifier
groups of about six buttons**, laid out **on the physical controller**, with the **built-ins kept apart**. The design
keeps all three habits.

---

## 2. Concepts considered

### A. "The owner's diagram, live": controller drawing with callouts, one modifier layer at a time

An upright Move Navigation drawing sits in the middle, with the stick, ✕, ○, the d-pad, PS, and L1/L2 on the top
edge. Six callout cards, one per primary (↑ → ↓ ← L3 PS), connect to their buttons by leader lines. Layer tabs across
the top ("No modifier", "Hold L1", …) switch which modifier is held, and the held button lights up on the drawing.

- **Pros:** Matches the owner's PNGs exactly. Only six labels at a time, so text stays large enough to read in the sun.
  The combo reads the way you perform it (hold this, press that). Cross-controller modifiers and reserved buttons can be
  explained right on the drawing. It's a good target for live press flashes.
- **Cons:** You only see a sixth of a controller at once. Callouts need about 700 px, so a phone has to fall back to a
  list. Getting geometry and leader lines right takes more work than a table.

### B. Layer matrix: a 6×6 grid per controller

Rows are modifiers and columns are primaries. Cells show the key and description, free cells are dashed, and reserved
cells are hatched.

- **Pros:** All 72 slots at a glance, so free and reserved patterns jump out. Dense, keyboard-friendly (arrow keys) and
  prints well. Simple to build.
- **Cons:** Abstract: nothing looks like the controller, and "which button is L3?" is the user's problem. Seven columns
  can't fit on a phone. Cross-controller modifiers are invisible.

### C. Both controllers side by side, with a "hold" rail between them

Foot and dome are drawn as they are held in two hands. Picking a modifier on one lights it on both, which shows
literally that ○/✕/PS can come from the other controller.

- **Pros:** The only concept that makes the cross-controller rule visible. It feels like holding the droid's remote.
- **Cons:** Halves the space for each controller, and twelve callouts don't fit. Most edits touch one controller, so
  the second drawing is mostly noise. On a phone it collapses to A anyway.

### D. Command-first reverse index

A searchable list of the WCB's stored commands, each showing which combos run it, with "assign to…" actions.

- **Pros:** Answers "where is Cantina?" fast, and search works well on phones.
- **Cons:** Free combos are invisible, and so are reserved ones. It isn't graphical, and it can't serve as the overview.

### E. Press-to-map ("learn") wizard

Pick a command, then press the combo on the real controller to assign it.

- **Pros:** No abstraction at all, and the fastest way to edit in the field.
- **Cons:** Depends on live press events and the lock. It can't plan with SHADOW off, shows no overview, and the press
  also fires the combo's current command unless SHADOW adds a learn mode.

### Choice: A as the main surface, B as the overview and cheat sheet, and E's press-to-select as an accelerator. D's strength goes into the picker.

- **A is the editor** because it matches how the owner already documents the droid, and it keeps text large on a
  phone. The one-layer limit is offset by **occupancy pips** in each layer tab: six small marks (filled, dashed, or
  struck through) summarize every layer of the controller without switching views.
- **B is the "All combos" view** because it is the only honest whole-map overview, and it is the print format the
  owner's PDF already uses. Selecting a cell jumps to it in A.
- **E becomes "Find by pressing"**: press any combo on either controller, and the UI jumps to that slot. This only
  selects; assigning is still an explicit click. Live presses also flash the matching button, which answers "what did
  I just trigger?"
- **D lives inside the picker:** a searchable list of stored commands in which each row says where else the command
  is used ("Also on Foot ◯ + Left").
- **C was dropped:** its one insight, the cross-controller rule, costs half the screen. A covers it with a caption on
  the drawing ("PS can be held on either controller") and a layer note.

---

## 3. The chosen design

### 3.1 Layout

The tab contains two cards, both using DroidNet's `.content-card` and `.content-card--full`.

**Card 1, "SHADOW link"** is the tab's signature element, like the CHiRP modeline:

```
[● DROIDNET c2-body] ──USB── [● WCB 1  USB · /dev/ttyUSB0 · 6.2.1] ──S3 · 9600── [● SHADOW  fw 2.0 · map v1 · gen 3]
Saved copy on this Booster: in sync   ·   Round trip 142 ms                       [Refresh] [Route…]
```

Each station has a lamp and a line of text. A failure turns its station and the link into it red and says what
failed ("No reply on S3 · last seen 14:02"). If the Booster's WCB isn't SHADOW's WCB, a middle hop is added:
`WCB 2 (USB) ~ESP-NOW~ WCB 1 ──S3── SHADOW`. While the map is read, a progress bar appears under the stations.

**Card 2, "Button map"**:

- **Header:** Foot / Dome toggle, showing mapped/usable counts ("Foot 24/31"). A Controller / All combos view toggle.
  **Find by pressing**. **More ▾**, which holds Print cheat sheet, Export, Import, Compare with saved copy, Route &
  command format, and Reset all to built-in defaults.
- **Notice row:** the lock state, find mode, or offline read-only notice (see 3.4).
- **Layer tabs:** six tabs. Each shows a name, occupancy pips in ↑ → ↓ ← L3 PS order, a mapped/usable count, a ⚠ if
  a slot needs attention, and a green dot when a live press just happened on a layer you aren't viewing.
- **Workspace (desktop, ≥ 900 px):** `grid-template-columns: minmax(0,1fr) 380px`.
  - Left is the **stage**: the drawing (232 × 435 px) centered, with three callouts each side (left: L3, ←, PS; right:
    ↑, →, ↓). The leader lines are an SVG overlay computed from the card positions. Under the stage are the live-press
    ticker, the layer note, and a collapsible **Built into SHADOW (not remappable)** list, the owner's "Stick Toggles"
    box.
  - Right is the **inspector** for the selected combo: current value, state, actions, description, and the stored-
    command picker.
- **900–1100 px:** the inspector narrows to 340 px. **Under 900 px:** the inspector moves below the stage.

**Phone (≤ 640 px), as in `mockup-phone.png`:**

- The shell's phone padding is 24 px on `main` plus 30 px on `.tab-content`, per side, which leaves about 250 px. This
  tab trims it to 8 + 12 px with a tab-scoped rule (`main:has(> #shadow-tab.active)`), giving about 350 px. Other tabs
  are unchanged. DroidNet already uses `:has()` in `dashboard.css`.
- The link card collapses to one line: "● SHADOW via WCB 1 · S3 · 142 ms", with an icon-only Refresh.
- The toggles go full width with 44 px targets. Layer tabs become a 3 × 2 grid, so no layer hides behind a scroll.
- The drawing is cropped to its head (200 × 262 px) and stays tappable. Callouts become six **rows** in the same DOM
  order: glyph chip, key (15 px mono), description, and state.
- Tapping a row or a drawn button opens the inspector as a **bottom sheet** (the same element restyled): sticky
  header with the combo and a 40 px close button, and a full-height picker with 52 px rows. Esc, the backdrop or ✕
  closes it.
- "All combos" becomes a list grouped by layer, like the owner's PDF.

### 3.2 The controller graphic

- **Drawing:** a stylized upright Move Navigation: stick/L3 at the top, ✕ upper left, ○ upper right, the d-pad, and
  PS below. L1 and L2 are tabs on the top edge. The placement comes from the owner's `SHADOW_MD_C2B5_*_Controller.png`,
  rotated upright. The same drawing is used for both controllers, and the eyebrow text and captions say which one.
- **Primaries (↑ → ↓ ← L3 PS)** are slot buttons. Their fill shows the slot state, and every state also has a
  different *shape or pattern*, so color is never the only signal:

  | State | Button on drawing | Callout / row |
  | --- | --- | --- |
  | Mapped | Solid blue tint | Key (mono) and description, with a **Default** or **Custom** badge |
  | Free | White, **dashed** outline | Dashed card, "Free · Tap to assign a command" |
  | Built in | **Hatched** fill | Hatched card, 🔒 icon, name ("Over-speed toggle") |
  | Command missing on WCB | Amber fill | Amber left bar, "⚠ Not on WCB 1" |
  | Sending | Selection ring stays | Blue left bar, spinner, "Sending", "Setting to cantina…" |
  | Failed | — | Red left bar, "⚠ Not applied" |
  | Differs from saved copy | — | "⇄ Differs" |
  | Selected | 3 px ring | 2 px blue border, tinted background, leader line drawn blue |
  | Live press | Green ring pulses twice | Green halo for 1.2 s |

- **Modifiers (L1, L2, ✕, ○)** are layer toggles, because they are never primaries. Clicking one holds it: it fills
  dark blue and a **HOLD** tag appears, and clicking it again returns to "No modifier". **PS** is a primary (it works
  with L1/L2), so clicking it selects the PS slot. On the "Hold PS" layer it renders as held, and its slot explains
  why it's built in.
- **Captions** under the drawing explain the layer: "Hold L1 on the foot controller", "PS can be held on either
  controller", or on the No-modifier layer, "Tap L1, L2, ✕ or ○ to see what they unlock".
- **Layer notes** come from `main.cpp`:
  - Foot L1/L2: "Holding L2 on the foot controller stops the feet while it's held."
  - ○/✕/PS: "✕ can be held on either controller. Dome ✕ plus a foot arrow runs the foot combo."
  - No modifier: the lookup order L1 › L2 › PS › ○ › ✕.
  - An empty layer: "Nothing on this layer yet: 5 free combos."
- **Accessibility:** the drawing is `aria-hidden`. The callouts are a `role="listbox"` with roving tabindex, and each
  option has a full spoken label ("Foot, hold L1, press Up: extremehappysou, Extreme Happy, default"). So the drawing
  is a pointer shortcut and never the only path.

Reserved slots, copied from the spec and `main.cpp`, with the text the UI shows:

| Slot | Title | Why (inspector text) |
| --- | --- | --- |
| PS, no modifier (both) | PS is a modifier | On its own PS only acts as a modifier. Hold L1 or L2 and press PS to use it as a button. |
| PS + PS (both) | Same button | PS is the button being held on this layer. |
| PS + ✕ (foot / dome) | Drive stick off / Dome stick off | PS + ✕ turns the drive stick off. It also unlocks button editing. |
| PS + ○ (foot / dome) | Drive stick on / Dome stick on | PS + ○ turns the drive stick back on. Editing locks again. |
| L1 + L3 (foot) | Over-speed toggle | L1 + L3 switches the feet between normal and extra speed. |

The "Built into SHADOW" list also shows the combos that fall outside the grid: L2 + ✕/○ (dome automation, either
controller), L2 + stick (turns the dome when there's no dome controller), and holding L1/L2 (the feet stop). Dome
L1 + L3 is editable, but until the over-speed fix ships it carries a note: "Firmware before the over-speed fix also
toggles foot over-speed when you press this."

### 3.3 Interaction flows

**Select a combo.** Any of these work, and all of them land in the inspector:
- click a callout or a drawn button;
- move with arrow keys through the callout listbox;
- click a cell in All combos;
- press the combo on a controller while **Find by pressing** is on.

**Edit (assign).** Changes apply immediately. There is no Save button.

1. The inspector shows the current key, its description, a Default/Custom badge, and the default key when the slot is
   custom.
2. "Change to" is a search box over the stored commands on SHADOW's target WCB (`GET /api/comlink/stored-commands`,
   the entry whose `wcb_number` matches the prefix's `;W<n>`). Each row shows the key, the Pi description, the WCB's
   own `***` label when it differs, and "Also on Foot ◯ + Left" when the command is used elsewhere. The current key
   has a ✓ and is scrolled into view.
3. Click a row, or press Enter on the highlighted row. The backend sends `@SM SET`, and the card and inspector show
   **Sending cantina to SHADOW…**.
4. On `@SM OK` they show **✓ Applied in 142 ms** for 4.5 s, and then the steady Custom badge. A toast says "Foot L2 +
   Left is now cantina. **[Undo]**" (7 s). Undo sends the previous value.

Why immediate: each SET is idempotent, takes 100–200 ms, and is CRC-protected per slot. The user's mental model is
the physical button: "press it and it does X now". A staged Apply would add a second, invisible "not on the droid
yet" state, which is exactly the confusion this UI exists to remove. Undo covers the cost of a mistake.

**Clear.** The Clear button (or Delete on a focused combo) sends `SET … -`. The slot becomes Free, with an Undo toast.
The button is disabled on free slots.

**Use default.** This only appears on custom slots. It sends `SET … *` (a protocol addition, 4.4), which makes SHADOW
fall back to its built-in default, so the slot really reads **Default** again rather than "custom but equal to the
default".

**Create a new stored command.** The entry points are:
- the picker's "+ New stored command on WCB 1…", prefilled with the search text when it is a valid key;
- the "Create volup on WCB 1…" button on a slot whose key is missing.

The modal (the `.modal` pattern the WCB backup dialogs use) has these fields:
- **Key:** up to 15 characters, validated as you type.
- **Description:** stored on the Pi.
- **Sequence:** the WCB tab's own composer, `DroidNet.builderUI.renderComposer(host, value, { onChange, onTest })`.

The buttons are **Save to WCB 1** and **Save and assign to Foot L1 + PS**. When the slot already names that key, only
Save appears. Saving uses `buildWCBStoreCommand()` (so `?SEQ,SAVE` on 6.1+ and `?CS` otherwise), respects
`wcbStoreCommandIgnoredAsHelp()`, and writes CRC-verified through `POST /api/wcb/command`, the same checks the WCB
tab's Update uses. Then the picker list reloads. If the target WCB isn't USB-attached to this Booster, the modal is
replaced by: "WCB 2 isn't plugged into this Signal Booster. Create the command on the Booster that has it, then
reload the list. [Open WCB tab]".

**Test.** This sends `prefix + key + suffix` (for example `;W1;Cscream`) from the Booster's WCB with
`send_wcb_command(expect_reply=False)`, so the droid does exactly what a press would. It is disabled on free slots,
missing keys, and a disconnected WCB. It stays **enabled while locked**, because it doesn't touch the map. The toast
says "Ran scream on WCB 1."

**Reset all to defaults.** Choose More → Reset all to built-in defaults…. The confirm is the **Maestro
EEPROM-overwrite modal**, reused unchanged (`showMaestroModal`, `.maestro-modal__warning`), so every "this overwrites
a board's EEPROM" moment in DroidNet looks the same. It lists:
- "This replaces the button map stored on SHADOW (EEPROM) with the firmware's built-in defaults";
- "N custom combos will go back to default: Foot ✕ + Left, …";
- "Your saved copy on this Booster is kept. You can re-apply it from Compare with saved copy".

The buttons are Cancel and **Reset to defaults** (danger). Confirming sends `@SM DEFAULTS`, which bumps the
generation, and re-reads the map with a progress bar.

**Edit lock.** SHADOW reports `lock=` in READY and pushes `@SM LOCK` on change (4.4).
- **Locked:** an info notice (blue left bar, 🔒, not amber or red) says "**Editing is locked while the droid can
  drive.** To change buttons, turn the drive stick off: on the foot controller hold [PS] and press [✕]." A second line
  adds "You can still browse, test commands and edit descriptions. This unlocks by itself when the stick goes off."
  The write controls are disabled, with a tooltip repeating the unlock step.
- **Unlocked:** a slim green-edged line says "**Editing on.** The drive stick is off. Press [PS] + [○] on the foot
  controller to drive again." The reminder is what makes the lock safe rather than scary: the user always knows the
  way back to driving.
- **Firmware without a lock:** the notice isn't shown at all.

**Find by pressing.** The toggle shows the notice "**Press any combo on a controller.** DroidNet jumps to it. The droid
runs its command as usual." The next `@SM PRESS` switches the controller, layer and slot, opens the sheet on a phone,
and ends find mode ("Found Dome ◯ + Up."). Esc or Cancel exits. With the optional LEARN command (4.4), the copy
changes to "…Nothing runs while DroidNet is listening."

**Live presses (always on).**
- Every PRESS updates the ticker: "Last press: **Foot L2 + Left** `cantina` · just now". A free combo shows "(free,
  nothing ran)" and a built-in shows "(built in: Drive stick off)".
- If the slot is on screen, its button pulses and its card gets a halo.
- Otherwise the layer tab or controller toggle shows a dot for 5 s.
- The ticker is an `aria-live="polite"` region.

**Print / Export / Import / Compare.**
- **Print cheat sheet:** the All-combos grid for both controllers plus the built-ins, on one landscape page (checked
  as a PDF). Descriptions lead and keys are small underneath, because helpers read names. Reserved cells say "Built
  in" as text, since print backgrounds are often off.
- **Export:** downloads `c2-body-shadow-map.json` (format in 4.3).
- **Import:** takes that file, does a dry run, and shows a diff modal. Choosing "Apply N changes" shows progress and
  then a summary that includes skipped reserved slots and keys missing on the WCB.
- **Compare with saved copy:** opened from the link card's "2 differ from saved copy · Review" button or from the
  menu. It is a table of combo / On SHADOW / Saved copy, with **Keep SHADOW's (update saved copy)** and **Apply saved
  copy to SHADOW**. Nothing is ever applied automatically on connect.

**Keyboard.**
- ←/→ (and Home/End) move within the Foot/Dome, view and layer tablists.
- ↑/↓ move through the combos.
- Enter or Space on a combo focuses the picker search.
- ↑/↓ in the search moves the highlighted command, and Enter assigns it.
- Esc closes the menu, sheet, modal or find mode.
- Delete clears the focused combo (not in the mockup).

### 3.4 Every state, and its copy

**Link and tab states**

| State | Where | Copy | Actions |
| --- | --- | --- | --- |
| Feature disabled | CHiRP-style overlay | **SHADOW Button Map Disabled.** This feature is currently disabled. Enable it in the Config tab under "Optional Features". | Go to Config |
| No route (first run) | Map card, `.chirp-empty` | **Tell DroidNet where SHADOW is.** SHADOW talks to DroidNet through a WCB. Pick the WCB this Booster is plugged into and the WCB port SHADOW's Serial1 is wired to. | Find SHADOW · Set route manually |
| Detecting | Same block | Listening for SHADOW's announce… (up to 30 s). When found: SHADOW announced itself on WCB 1, S3. | Use this · Cancel |
| Connecting | Link card, SHADOW lamp blinks | Saying hello… | — |
| Reading map | Progress bar under the stations; slots show "Checking" | Reading SHADOW's map: 18 of 72 slots. Showing your saved copy until it finishes. | — |
| Ready | Green lamps, badge **Live** | fw 2.0 · map v1 · gen 3 · Round trip 142 ms | Refresh · Route… |
| SHADOW silent | Station red, notice | **SHADOW isn't answering.** DroidNet reached WCB 1, but SHADOW didn't reply on S3. Check that SHADOW is powered, wired to S3, and running firmware with the button map. You're looking at the saved copy from 14:02. Changes are off until SHADOW is back. | Try again |
| WCB unplugged | Station red, notice | **This Booster's WCB isn't connected.** The always-on listener for /dev/ttyUSB0 isn't running. Plug the WCB in, or check the WCB tab. | Try again · Open WCB tab |
| Wizard holds the port | Notice | **Paused while the WCB Wizard has WCB 1.** Close the Wizard session to keep editing. | — |
| Incompatible SHADOW | `.chirp-empty` | **This SHADOW can't be remapped live from here.** Shown when READY reports a protocol version this DroidNet doesn't speak, or when SHADOW's `@WDP1` announce lacks `shadow.map`. Pre-mapping firmware ignores `@SM` lines, so it shows as "SHADOW silent" instead. Copy: "SHADOW speaks button map v2; this Signal Booster speaks v1. Update DroidNet." | Open Config → Updates |
| No saved copy yet, offline | Map card | **No saved copy yet.** Once SHADOW answers, DroidNet keeps a copy here so you can still see the map when SHADOW is off. | Try again |
| Locked / unlocked | Notice row | See 3.3 | — |
| Controller not connected (optional `CTL`) | Toggle label, and a layer note | Dome · not connected. The dome controller isn't connected. You can still edit its combos. | — |
| Differs from saved copy | Link card meta, slot badge "⇄ Differs" | 2 differ from saved copy · Review. Inspector: Your saved copy has magicpanelon here. SHADOW has cantina. | Review differences… |

**Slot and write states**

| State | Copy |
| --- | --- |
| Default | **Default** · Built into the firmware |
| Custom | **Custom** · Saved on SHADOW · default is `happysound` (or "default is free") |
| Free | Free · Nothing runs when you press this combo. (See open question 1.) |
| Sending | Sending `cantina` to SHADOW… |
| Applied | ✓ Applied in 142 ms (4.5 s), with a toast offering Undo |
| Timeout after 3 tries | **Couldn't set this to `disco`.** SHADOW didn't answer after 3 tries. It still has `imperialmarch` here. [Try again] |
| Unconfirmed | Couldn't confirm the change. Checking what SHADOW has… (the backend sends `GET`, then shows Applied or Failed) |
| `ERR locked` | Editing locked on SHADOW. Turn the drive stick off (hold PS, press ✕ on the foot controller), then try again. |
| `ERR busy` | Retried silently. After retries: SHADOW was busy saving another combo. [Try again] |
| `ERR badkey` | SHADOW rejected `x-y`: keys are 1–15 letters, digits or _. |
| `ERR reserved` | That combo is built into SHADOW and can't be changed. (Only possible if the UI and firmware disagree. The UI refreshes its reserved list.) |
| Command missing on WCB | **`volup` isn't a stored command on WCB 1.** Pressing this does nothing until it exists. [Create volup on WCB 1…] |
| Changed elsewhere | Toast: Foot L1 + Up changed to `scream` from another DroidNet window. |
| Known firmware issue | Firmware before the over-speed fix also toggles foot over-speed when you press this. |

**Picker states**

| State | Copy |
| --- | --- |
| Live list | ✓ Live list from WCB 1 · read 14:05 [↻] |
| Backup only | From WCB 1's backup, 14:02 · the live check timed out [↻] |
| Target WCB not on this Booster | WCB 2 isn't plugged into this Signal Booster, so DroidNet can't list its stored commands. Type a key. DroidNet can't check that it exists. [key field] [Assign] |
| No stored commands | WCB 1 has no stored commands yet. Create one here or in the WCB tab. |
| No match | No stored command on WCB 1 matches **cant**. [+ New stored command "cant"…] |
| Locked or offline | The list stays browsable. Rows are `aria-disabled`, and the label reads "Locked" or "Offline" instead of "Applies right away". |

### 3.5 Readability and accessibility

- Text: keys are mono and semibold, 14 px on desktop and 15 px on phones. Descriptions are 13–14 px in
  `--text-secondary`, with no light gray for data. The light theme is the outdoor theme; dark mode works through the
  existing `body.dark-mode` tokens (checked in the mockup).
- Every state pairs an icon with text, and the pips and cards also differ by shape (filled, dashed, hatched, left
  bar). Lamps always have a text line beside them.
- Button glyphs are drawn as SVG (chevrons, ✕, ○). Unicode arrows and ○ render too small to read at 11–12 px, which
  the first mockup iteration showed. In running text, combos are written in words ("Foot L2 + Left", "Dome ◯ + Up").
- Roles: the tablists use `role="tablist"`, the combos a `role="listbox"`, the picker a combobox with
  `aria-activedescendant`, and status changes go through `aria-live`. Focus is a 2 px `--border-focus` outline.
  Reduced motion turns off the pulses and spinners.
- Phone touch targets are at least 44 px (tabs 52 px, picker rows 52 px). No action is hover-only.

---

## 4. How it's built in DroidNet

### 4.1 Files

| File | Change |
| --- | --- |
| `web/index.html` | Add the nav button `<button class="nav-tab" id="shadow-tab-btn" onclick="showTab('shadow')" style="display:none;">SHADOW</button>`, the `#shadow-tab` markup (overlay, link card, map card; the SVG drawing inline, as in the mockup), `css/shadow.css` and `js/shadow.js`, and a toggle under Config → Optional Features |
| `web/js/tab-manager.js` | `TAB_LABELS.shadow = 'SHADOW'` (after `chirp`) and add `'shadow'` to `FEATURE_GATED` |
| `web/js/shadow.js` (new) | An IIFE like `chirp.js`. Pure helpers (slot model, reserved table, layer stats, combo wording, JSON import diff) go on `window.ShadowMap` for unit tests. Rendering, the SSE client and handlers live in the same file |
| `web/css/shadow.css` (new) | Section 5 of the mockup CSS, `shadow-` prefix, tokens only (as in `chirp.css`) |
| `web/js/app.js` | Add a feature loader that shows the tab when enabled (like CHiRP's). Extend `showStatusMessage(message, type, duration, action)` with an optional `{label, onClick}` for Undo; it is backward compatible |
| `web/handlers/shadow_handlers.py` (new) | HTTP handlers, following `chirp_handlers.py` (guarded imports, `{"success": …}` replies) |
| `scripts/shadow_mapping.py` (new) | Protocol codec and CRC32, the passive `@SM` matcher on WCB listener `data` events (like `victron_request_responder.py`), a one-in-flight op queue with timeouts and retries, the saved copy, and the event fan-out |
| `scripts/shadow_config.py` (new) | Reads and writes `/opt/droidnet/config/shadow-mapping.json` (route, prefix, suffix, labels, saved copy) |
| `web/server.py` | Route the endpoints below, and the SSE stream (like `/api/system/update-stream`) |
| `docs/API_REFERENCE.md`, `docs/SHADOW_MAPPING_SPEC.md` | Document the endpoints and the protocol |
| `tests/test_shadow_mapping.py`, `web/js/__tests__/shadow.test.js`, `tests/e2e/…` | Codec, CRC and matcher; UI helpers; one Playwright pass of assign → applied → undo against a fake SHADOW |
| SHADOW repo: `WIPDocs/scripts/update_control_mapping.py` | Read the exported JSON instead of parsing `config.cpp` |

### 4.2 Reused components and CSS

| Need | Reuse |
| --- | --- |
| Cards, header, icons | `.content-grid`, `.content-card--full`, `.content-card-header/-title/-actions/-body`, `.card-icon` (emoji entities, like the other tabs) |
| Foot/Dome and view toggles | `.btn-group` with `role="tablist"`, as in `.maestro-mode-toggle` |
| Layer tabs | Styled from `.maestro-anim-tab` (pill, active fill, count), made taller for the pips |
| Buttons, badges, forms | `.btn .btn-sm .btn-primary .btn-secondary`, `.badge .badge-sm .badge-primary`, `.form-control .form-group .form-hint` |
| Link stations, lamp, progress, spinner | `chirp.css`: `.chirp-lamp`, the transit-track modeline pattern, `.chirp-progress`, `.chirp-btn-spinner`, `.chirp-icon-btn` |
| Notices and empty states | `.chirp-apply` left-border notice, `.chirp-empty` / `.chirp-locked` |
| Toasts | `showStatusMessage` / `.status-message`, plus the Undo action above |
| Reset confirm | `showMaestroModal()` and `.maestro-modal__warning`, unchanged |
| Create, Diff, Route, Import dialogs | `.modal > .modal-backdrop + .modal-content` (the WCB backup modals) |
| Composer | `DroidNet.builderUI.renderComposer()`, `ensureWCBLibrary()`, `.wcb-builder` CSS |
| Store a new command | `buildWCBStoreCommand()`, `wcbStoreCommandIgnoredAsHelp()`, `POST /api/wcb/command` with CRC verify |
| Picker data | `GET /api/comlink/stored-commands` (`key`, `label`, `value`, `source`, `as_of`, `error`) |
| Escaping | `window.DroidNet.utils.escapeHtml` |

New CSS covers only the drawing, callouts and leader lines, pips, inspector and picker, the grid, the phone sheet, and
print. The mockup's section 5 is a working draft of that file.

### 4.3 Backend API

All endpoints are JSON, answer `{"success": true, …}` or `{"success": false, "error": "<code>", "message": "…"}`, and
use exact-path routing with JSON bodies (no path parameters), as `server.py` does today. Slot fields: `ctl` is `F`/`D`,
`mod` is `NONE L1 L2 PS CIRCLE CROSS`, `btn` is `UP RIGHT DOWN LEFT L3 PS`.

**`GET /api/shadow/config`** and **`POST /api/shadow/config`**

```json
{ "success": true,
  "config": { "enabled": true,
              "route": { "booster_wcb": { "device_id": "10c4:ea60:port:1-1.3", "port": "/dev/ttyUSB0", "wcb_number": 1 },
                         "shadow_wcb": 1, "shadow_port": 3 },
              "prefix": ";W1;C", "suffix": "" } }
```

The POST takes any subset. A changed `prefix` or `suffix` is also pushed to SHADOW (`@SM PREFIX <hex>`) and fails
with `locked` while the lock is on.

**`GET /api/shadow/status`**

```json
{ "success": true,
  "link": { "state": "ready",
            "booster_wcb": { "connected": true, "fw": "6.2.1", "paused_by_wizard": false },
            "shadow": { "proto": 1, "fw": "2.0", "gen": 3, "lock": 0, "map_crc": "1A2B3C4D",
                        "controllers": { "F": true, "D": true }, "last_seen": "2026-09-24T14:05:11Z" },
            "rtt_ms": 142, "error": null },
  "sync": { "state": "in_sync", "differences": 0, "saved_at": "2026-09-24T14:02:40Z" },
  "progress": null }
```

- `link.state` is one of `no_route | connecting | reading | ready | wcb_offline | wizard | no_reply | unsupported`.
- `lock` is `null` when the firmware has no lock.
- `progress` is `{ "done": 18, "total": 72 }` while reading.

**`GET /api/shadow/map`**

```json
{ "success": true, "source": "shadow", "as_of": "2026-09-24T14:05:11Z", "gen": 3,
  "slots": [ { "ctl": "F", "mod": "L1", "btn": "UP", "key": "extremehappysou", "src": "default",
               "default_key": "extremehappysou", "reserved": null, "saved_key": "extremehappysou",
               "state": "steady", "op_id": null, "error": null },
             { "ctl": "F", "mod": "L1", "btn": "L3", "key": null, "src": "reserved",
               "default_key": null, "reserved": { "code": "overspeed" }, "saved_key": null, "state": "steady" } ],
  "labels": { "extremehappysou": "Extreme Happy" } }
```

- Always 72 slots.
- `source` is `saved` when SHADOW isn't reachable.
- The UI owns the display copy for each `reserved.code` (`psalone`, `same`, `stickoff`, `stickon`, `overspeed`).

**`POST /api/shadow/slot`**, with body `{ "ctl": "F", "mod": "L2", "btn": "LEFT", "key": "cantina" }`:

- `key` is a key string, `null` to clear, or `"*"` for the built-in default.
- Reply: `{ "success": true, "op_id": "op_41", "state": "pending" }`.
- Fails immediately with `error` in `locked | reserved | badkey | offline`.
- The outcome arrives on the event stream. **`GET /api/shadow/op?id=op_41`** returns `{ "success": true, "op": { "id":
  "op_41", "state": "ok|failed|unconfirmed|pending", "error": null, "rtt_ms": 138 } }` for clients without SSE.
- The backend keeps one SET in flight, retries three times with a 700 ms timeout, then sends `GET` to resolve
  `unconfirmed`.

**`POST /api/shadow/apply`**, with body `{ "slots": [ { "ctl": …, "mod": …, "btn": …, "key": … } ], "source":
"import|saved" }`:

- Reply: `{ "success": true, "op_id": "op_42", "total": 7 }`, then `progress` events.
- Used by Import and "Apply saved copy".

**`POST /api/shadow/defaults`**, with body `{ "confirm": true }`, replies `{ "success": true, "op_id": "op_43" }`,
then the map is re-read.

**`POST /api/shadow/refresh`** re-sends HELLO. It sends DUMP only when `map_crc` differs from the saved copy.

**`POST /api/shadow/test`**, with body `{ "key": "scream" }`, replies `{ "success": true, "sent": ";W1;Cscream" }`.

**`POST /api/shadow/labels`**, with body `{ "key": "scream", "label": "Extreme Scared (Scream)" }`, is Pi-only and works
while locked.

**`GET /api/shadow/diff`** replies `{ "success": true, "diff": [ { "ctl": "F", "mod": "L2", "btn": "LEFT", "shadow_key":
"cantina", "saved_key": "magicpanelon" } ] }`.

**`POST /api/shadow/diff/resolve`**, with body `{ "keep": "shadow" | "saved" }`:
- `shadow` overwrites the saved copy.
- `saved` starts an apply op.

**`GET /api/shadow/export`** returns `application/json` with `Content-Disposition: attachment;
filename="<booster>-shadow-map.json"`:

```json
{ "format": "droidnet-shadow-map", "version": 1, "exported": "2026-09-24T14:06:00Z", "booster": "c2-body",
  "shadow": { "fw": "2.0", "gen": 3 }, "prefix": ";W1;C", "suffix": "",
  "slots": { "F": { "L1": { "UP": "extremehappysou", "PS": null } } },
  "labels": { "extremehappysou": "Extreme Happy" } }
```

**`POST /api/shadow/import`**, with body `{ "doc": { … }, "dry_run": true }`, replies:

```json
{ "success": true, "diff": [ { "ctl": "F", "mod": "L2", "btn": "LEFT", "shadow_key": "cantina", "import_key": "magicpanelon" } ],
  "unknown_keys": [ "volup" ], "reserved_skipped": [ "F.L1.L3" ] }
```

**`POST /api/shadow/detect`** listens up to 30 s for an `@WDP1 {"type":"Shadow",…}` announce and replies `{
"success": true, "found": [ { "wcb_number": 1, "port": 3 } ] }`.

**`POST /api/shadow/learn`**, with body `{ "on": true, "seconds": 10 }`, is optional (see 4.4).

**`GET /api/shadow/events`** is SSE. Every event's `data` is JSON:

| Event | Data |
| --- | --- |
| `link` | the `/status` object |
| `slot` | one slot object, plus `"by": "<client id>"` |
| `op` | `{ "id", "state", "error", "rtt_ms" }` |
| `progress` | `{ "op_id", "done", "total" }` (reads use `op_id: "read"`) |
| `lock` | `{ "lock": 0 \| 1 }` |
| `press` | `{ "ctl": "F", "mod": "L2", "btn": "LEFT", "key": "cantina", "from": "F", "reserved": null, "at": "…Z" }` |
| `ctl` | `{ "ctl": "D", "connected": false }` |
| `labels` | `{ "key", "label" }` |

Reused as they are: **`GET /api/comlink/stored-commands`** (picker), **`POST /api/wcb/command`** (store a new command,
test fallback), and **`GET /api/wcb/library`** (composer catalog).

Pi Zero 2 W notes:
- One SSE stream per open tab. It is idle except on events.
- The stored-commands list is fetched when the tab opens and when you press ↻, never on each keystroke; the search
  runs client-side.
- Rendering is plain DOM on small lists (6 callouts, at most about 50 picker rows).

### 4.4 Protocol additions needed from SHADOW

These build on the `@SM` protocol in `COMMAND_MAPPING_DESIGN.md`. They follow the same rules: CRC on every line, no
`^`, sent only after HELLO, sent only when `Serial1.availableForWrite()` has room, and never blocking the loop.

| # | Addition | Why the UI needs it |
| --- | --- | --- |
| 1 | **`@SM PRESS <ctl> <mod> <btn> <key\|-> [from=<F\|D>] [res=<code>]`**, sent for *every* combo that reaches the lookup, mapped or not. `mod` is the modifier that decided (after the L1 › L2 › PS › ○ › ✕ order). `from` is the controller the modifier came from. `res` marks built-ins (`stickoff`, `stickon`, `overspeed`). One line per press, dropped (not queued) if the TX buffer is full. | Live highlight, the last-press ticker, and Find by pressing. It must include free combos, or you can't find a free slot by pressing it. |
| 2 | **`@SM LOCK <0\|1>`**, unsolicited, when the lock changes | The lock notice updates the moment the operator presses PS + ✕, with no polling |
| 3 | **`SLOT` lines carry `def=<key\|->`**, and **reserved slots are listed** as `@SM SLOT F L1 L3 - src=reserved res=overspeed` | The "default is …" text and Use default, without the Pi hardcoding the firmware's defaults. The reserved list, including the dome L1 + L3 fix, then follows the firmware |
| 4 | **`@SM SET <ctl> <mod> <btn> *`**, meaning revert to the built-in default: invalidate that slot's CRC so it reads `src=default` | "Use default" really returns to Default, instead of "custom that equals the default" |
| 5 | **`READY … map=<CRC32 of the 72 names>`** | Skip DUMP when nothing changed. At 9600 baud a DUMP with `def=` is 72 lines of about 80 B, roughly 6 s |
| 6 | Optional: **`READY … ctl=F,D`** and **`@SM CTL <F\|D> <0\|1>`** | "Dome · not connected" labels. It also settles the lock rule when no foot controller is connected (question 2) |
| 7 | Optional: **`@SM LEARN <seconds>`**, after which the next combo press is reported as `PRESS … learn=1` *without sending its command* | Find by pressing at a show without firing a scream |

---

## 5. Open questions for the owner

1. **What does an empty slot do while a modifier is held?** The spec says "the first slot with a mapping wins", so
   holding ○ on the dome (whose ○ layer is completely empty) and pressing ↑ would fall through and run dome ↑
   `singlemuse`. Today it runs nothing: the `{UP, CIRCLE, 0, "UNUSED"}` rows in `config.cpp` match first and send
   nothing. **Recommendation:** the highest-priority held modifier decides, and an empty slot sends nothing; the
   no-modifier layer is used only when no modifier is held. The UI copy "Free · Nothing runs" assumes this. With
   fall-through it would read "Free · runs ↑ singlemuse (no-modifier)".
2. **Edit lock:** should it be required? If so, what does it do when no foot controller is connected, so PS + ✕ can't
   be pressed? The suggestion is unlocked, since nothing can drive. Should the lock also gate PREFIX/SUFFIX and
   DEFAULTS? The suggestion is yes. Is Test allowed while locked? The design says yes.
3. **Wiring:** is SHADOW on the WCB this Booster is plugged into, and on which S-port? This sets the route defaults. It
   also decides whether "Create stored command" can work inline, because the command has to live on the WCB the
   prefix targets and DroidNet can only list and write USB-attached WCBs.
4. **Volume and brightness keys:** the mockup uses `volup/voldown/volmid/volmax` and
   `brightup/brightdown/brightmid/brightmax` as placeholders. Volume up/down were stateful: ±10 with clamping, and a
   red rear holo at the limit. Can the HCR or WCB step relatively, or should these become fixed levels?
5. **Descriptions per key or per combo?** The design keys them by stored-command key: the same command reads the same
   everywhere, and the WCB's `***` label is the fallback. Say if a combo ever needs its own note.
6. **Protocol additions 1–5 in 4.4:** are they acceptable in SHADOW's flash and SRAM budget? Is LEARN (7) worth adding
   for shows?
7. **Key characters:** the SHADOW spec allows `[A-Za-z0-9_]{1,15}`, but DroidNet's WCB tab only accepts `[A-Za-z0-9]`
   (the `key-input` pattern and `updateCommand` validation). Which does WCB 6.2.1 accept? Both sides should match.
8. **Tab name and default:** "SHADOW", feature-gated and off by default like CHiRP?
9. **Phone chrome:** is trimming the console's phone padding on this tab alone acceptable, or should it become a
   global change? It is 54 px per side today.
