# Cirrostratus

### *a thin, high sheet of cloud for the Typhoon*

**Version 1.5.3 · Typhoon edition**

<!-- PANEL cover -->

> **cir·ro·stra·tus** /ˌsɪroʊˈstreɪtəs/ *n.* — a pale veil of ice crystals
> spread so high and thin across the sky that the sun shines straight
> through it, with a ring of light around it: a **halo**.

Cirrostratus is alternative firmware for the **Typhoon**, Calsynth's
Clouds on the Grayscale Supercell layout: the Monsoon's bigger sibling,
with the same chip and codec, more controls, two meters and two mute
buttons. Clouds made granular weather. This firmware goes one layer
higher, thinner and brighter:

- **30 modes** in two banks (Clouds had four kinds of sky)
- **seven controls of their own in every mode**: the four sliders plus the
  three knobs **STEREO**, **FEEDBACK** and **REVERB**, which mean something
  different (and specific) in each mode. That's 87 extra controls, all with
  CV
- **48 kHz**, 32-bit float inside, 16-bit block-float buffers that keep quiet
  sounds clean (a −60 dB whisper is stored with 93 dB SNR), a **buffer
  quality** setting that trades fidelity for up to **6× longer buffers** in
  every mode that records, and an **FFT size** setting (256 to 2048 points)
  for every spectral mode
- a clean **output section**: equal-power blend, hard stereo (whatever the
  mode makes), **bass kept mono below 60 Hz**, a transparent limiter and
  dithered output
- four **save slots**, **slot morphing**, per-mode **options** and
  **envelope-follower routing**
- on the Typhoon: an **IN and an OUT meter**, click-free **IN MUTE** and
  **OUT MUTE**, and a slow **random voltage on AUX** that can drift every
  unpatched CV jack

Underneath it is exactly the Monsoon's Cirrostratus 1.5.3: the same modes,
the same engine, the same save slots. Only the panel code is the Typhoon's
own, and only the weather changes.

> **Not yet run on a Typhoon.** This edition builds and has been checked in
> software, but nobody has run it on a real module yet. Section 7 lists
> what still needs checking on hardware. If something on your module
> doesn't match this manual, that list is the first place to look.

**If you know the Monsoon version:** DISP is **TIME** here, WRITE is
**BANK**, the knobs BAL, FBK and RVB are **STEREO**, **FEEDBACK** and
**REVERB**, and GAIN is **IN LEVEL**. Every gesture is the same. The
Monsoon's coloured LEDs become the Typhoon's 16 single-colour ones
(section 3.13).

---

## 0. Forecast in thirty seconds

<!-- PANEL quick -->

1. Hold **TIME**. The **1 2 4 8** LEDs above it show a number, and **A**
   or **B** lights for the bank.
2. Push **POSITION** up or down to pick a mode. **Tap BANK** to flip
   between bank A and bank B.
3. Let go of **TIME**.
4. Play something in, and wiggle all four sliders, then the knobs
   **STEREO**, **FEEDBACK** and **REVERB**. They're the mode's own extra
   controls: STEREO at noon and FEEDBACK/REVERB at zero is the mode plain.
   **FREEZE** and **TRIG** do something different (and good) in every
   mode too.

The rest of this manual is the long-range forecast.

---

## 1. Installing (getting the cloud up there)

You need these files from the Typhoon build of Cirrostratus:

| File | What for |
|---|---|
| `cirrostratus-typhoon.wav` | update through the audio input, no tools |
| `cirrostratus_typhoon.bin` | the application, for SWD |
| `cirrostratus_typhoon_bootloader_combo.bin` | bootloader + application, for a blank chip |

### 1a. By ear (recommended)

<!-- PANEL install -->

The Typhoon's stock firmware (SuperParasites) comes with the Mutable
Instruments audio bootloader, and Cirrostratus uses it as it is.

1. Rack **off**.
2. Cable from your computer's headphone/line out into **IN L**. IN R is
   normalled to IN L, so one cable is enough. No Bluetooth, no system
   sounds, close the chat apps.
3. **Hold FREEZE** (the big white button) and switch the rack on. The
   FREEZE LED blinks: the module is listening for weather data.
4. Computer volume about ¾, **IN LEVEL** about noon.
5. Play `cirrostratus-typhoon.wav` from the beginning (6 min 26 s; put the
   kettle on). It sounds like a modem singing. That's correct.
    - While it receives, **FREEZE blinks fast** and the **meters** show the
      level.
    - **All the TIME and BANK LEDs light** each time a chunk is written to
      flash.
    - When the file ends, the module restarts into Cirrostratus and greets
      you with a sunrise (3.0).
6. **The TIME and BANK LEDs blinking** means reception failed (too quiet,
   too loud, a notification ping…). Press **FREEZE**, fix the level, and
   play the file again from the start. Nothing can break: the bootloader is
   never overwritten.

### 1b. With a programmer (SWD)

1. `brew install open-ocd`
2. Connect an ST-Link to the module's SWD header, with the module powered.
3. In the firmware folder:
    - `make -f typhoon/makefile flash` writes the application and keeps the
     bootloader, settings and slots.
    - `make -f typhoon/bootloader/makefile && make -f typhoon/makefile combo`,
     then `make -f typhoon/makefile flash_all`, writes bootloader +
     application to a blank chip (settings reset).

### After landing

- Your **V/OCT calibration comes along** from SuperParasites
  automatically. Its 0 V readings for the CV jacks come along too, but only
  if every one of them looks like a real 0 V (they don't if its AUX noise
  was on while you calibrated). Never calibrated, or the CVs feel off? See
  3.9.
- SuperParasites' **save slots aren't loaded**: Cirrostratus recognises
  its own slots only and ignores anything else, so it never plays garbage.
- **Back to stock:** play the stock firmware's update file the same way.

---

## 2. The panel

| On the panel | We call it | |
|---|---|---|
| POSITION slider + CV | **POSITION** | also picks the mode in the mode menu (the slider only, not its CV) |
| DENSITY slider + CV | **DENSITY** | |
| SIZE slider + CV | **SIZE** | |
| TEXTURE slider + CV | **TEXTURE** | |
| TUNE knob + V/OCT + FREQ | **PITCH** | TUNE: ±4 octaves, gently snapping to semitones, noon = no change. V/OCT is calibrated; FREQ adds ±1 octave over ±5 V |
| WET knob + CV | **WET** | dry ↔ wet, equal power, on every mode |
| STEREO knob + CV | **STEREO** | the mode's own control: bipolar, plain at noon |
| FEEDBACK knob + CV | **FEEDBACK** | the mode's own control: plain at zero |
| REVERB knob + CV | **REVERB** | the mode's own control: plain at zero |
| FREEZE button + HOLD jack | **FREEZE** | a high gate on HOLD flips whatever the button says |
| TRIG jack + TRIG button | **TRIG** | a different job in every mode |
| TIME | **TIME** | display, mode menu |
| BANK | **BANK** | load, save, bank flip |
| IN MUTE · OUT MUTE | **mutes** | click-free input and output mutes (3.10) |
| meters | **IN / OUT meters** | input level (top row), output level (second row) |
| IN LEVEL + IN VCA | input level | analog, as on the stock firmware |
| OUT LEVEL + OUT VCA | output level | analog; the OUT meter follows it |
| AUX | **AUX** | the normal behind nine CV jacks: 0 V, or a slow random voltage (3.11) |
| IN L · IN R | stereo in | IN R follows IN L when unpatched |
| OUT L · OUT R | stereo out | hard stereo |

**Every slider and knob adds its CV jack** (POSITION, DENSITY, SIZE,
TEXTURE, WET, STEREO, FEEDBACK, REVERB): the control's value is the
slider or knob plus the CV, so ±5 V sweeps the whole range and a positive
voltage turns the control up. The LEDs in the sliders show the incoming
CV; that's done in hardware.

**IN LEVEL, OUT LEVEL and the two VCA jacks** are analog, outside the
firmware: they work exactly as on the stock firmware. Cirrostratus only
reads the OUT VCA's voltage to scale the OUT meter.

**The 16 LEDs belong to Cirrostratus:** the two meter rows along the top
(green, green, yellow, red), the **A B C D** column above BANK and the
**1 2 4 8** column above TIME. The FREEZE button's own light is the freeze
state.

**STEREO, FEEDBACK and REVERB are per-mode controls.** Each mode's table
in section 5 says what they do. The house rule: **STEREO is bipolar**
(noon = the mode plain, left and right of noon do two related things),
**FEEDBACK and REVERB start at zero** (fully left = the mode plain).
The names are a loose theme: FEEDBACK is usually something that
recirculates or intensifies, REVERB something about space, tail or wash,
STEREO a balance between two things (often, but not always, left and
right).

---

## 3. Buttons, pages and little rituals

### 3.0 Sunrise

<!-- PANEL sunrise -->
Every time you power up, the module greets you with three seconds of
weather, on all four groups of LEDs at once (the two meter rows, A–D and
1 2 4 8):

1. **Ice crystals**: dim sparkles twinkle across all 16 LEDs.
2. **Halo**: two rings pulse outward from the middle LEDs of each group to
   its ends, and the FREEZE LED glows with each pulse.
3. **The sun breaks through**: every group sweeps from half to full
   brightness.
4. **The forecast**: your current mode on TIME and BANK (3.2), fading out.

Any button skips it (that press does nothing else).

### 3.1 Looking at the sky

<!-- PANEL look -->
- **The meters are always on.** The top row shows the input level (after
  IN MUTE), the second row the output level (after OUT MUTE, scaled by OUT
  LEVEL and the OUT VCA, as on the stock firmware). Each row runs green,
  green, yellow, red, from about −48 dB to 0 dB.
- **The meters page** (the normal view): the TIME and BANK LEDs show the
  running mode dimly, its number on 1 2 4 8 and its bank on A or B.
- **Tap TIME** for the **knobs page**: A, B, C and D glow as brightly as
  **WET, STEREO, FEEDBACK** and **REVERB** are turned up (the mode number
  stays dim on 1 2 4 8). Tap TIME again to go back.
- The FREEZE LED is the freeze state, always.

### 3.2 Choosing a mode

<!-- PANEL menu -->
1. **Hold TIME** for a second. The **1 2 4 8** LEDs show the mode number
   within its bank in binary, at full brightness, and **A = bank 1**
   (modes 1–15) or **B = bank 2** (16–30) lights.
2. **Move POSITION** to browse. Only the slider counts here; its CV is
   ignored. The menu only starts following POSITION once you actually move
   it, so peeking never changes anything. The sound keeps its old POSITION
   while you browse.
3. **Tap BANK** to flip banks.
4. **Tap FREEZE** to step through the mode's **options**. For 1.5 s the
   option number (1–8) blinks in binary on 1 2 4 8; the bank LED stays lit.
5. **Release TIME**. The old mode fades out and the new one fades in
   (about 20 ms, no clicks).

Reading the binary: add up the lit LEDs, 1 at the top, 8 at the bottom.

| 1 2 4 8 (top → bottom) | # | | # | | # |
|---|---|---|---|---|---|
| ●○○○ | 1 | ●○●○ | 5 | ●○○● | 9 |
| ○●○○ | 2 | ○●●○ | 6 | ○●○● | 10 |
| ●●○○ | 3 | ●●●○ | 7 | ●●○● | 11 |
| ○○●○ | 4 | ○○○● | 8 | ○○●● | 12 |
| ●○●● | 13 | ○●●● | 14 | ●●●● | 15 |

Bank B: add 15 (B with ●○○○ is mode 16, B with ●●●● is mode 30).

Every mode remembers its own option and envelope routing, even through
power cycles.

### 3.3 Four jars of sky (save slots)

<!-- PANEL slots -->
The four slots are **A, B, C and D**, on the LEDs above BANK.

- **Hold BANK**: the save page. The slot's LED blinks and the other three
  glow dimly. **TIME** picks a slot, **BANK** saves (its LED stays lit
  while it saves). About a second of silence while flash is written.
- **Tap BANK**: the load page. The slot's LED blinks. **TIME** picks,
  **BANK** loads. The slot remembers its mode and option, so loading takes
  you there.
- What goes in the jar depends on the mode: a recorded buffer, a frozen
  spectrum, a wavetable, a captured impulse response, a tape loop… Each
  mode below says what it keeps. A jar also keeps the buffer quality (3.7)
  and FFT size (3.8) it was made with, and plays back at them.

**Slot morph** (SPECTRAL and CAPTURE): on the load page, tap **FREEZE**
instead of BANK. That slot becomes the *morph source*: its LED stays lit,
it's read straight from flash, and POSITION blends toward it. When the slot
you're on is the morph source, its LED blinks between full and dim. Tap
FREEZE on it again to let it go.

### 3.4 The envelope page (weather that follows you)

<!-- PANEL envelope -->
Your input's loudness can push WET and the mode's three knobs up or down:
the wet that opens on loud notes, a STEREO that leans further the harder
you play, a FEEDBACK that swells in the gaps, a REVERB that ducks while
you play. Whatever STEREO, FEEDBACK and REVERB mean in the current mode,
that's what moves.

- **Hold FREEZE, tap TIME**. Each row of the two LED columns is one knob:

  | Row | Knob |
  |---|---|
  | 1 and A | WET |
  | 2 and B | STEREO |
  | 4 and C | FEEDBACK |
  | 8 and D | REVERB |

- The **BANK-side LED** (A–D) lit means the knob is pushed **up**, the
  **TIME-side LED** (1 2 4 8) lit means **down**. **Dim** = ½, **full** = 1.
- **TIME** moves the cursor: its row blinks fast (a faint glow on both
  sides if that knob is off).
- **FREEZE** cycles its amount: off → +½ → +1 → −½ → −1 → off.
- **BANK** saves and leaves (it also leaves by itself after 6 s).
- Each mode keeps its own routing.

### 3.5 FREEZE and TRIG

- FREEZE toggles on **release**, because the button is also a modifier.
- The **HOLD** jack is FREEZE's gate, and it inverts the button: button
  off + gate high = frozen; button on + gate high = thawed.
- **TRIG** is a jack and a button. The button acts like the jack: it
  fires one trigger per press and holds the TRIG input high while you hold
  it, so modes that use TRIG as a gate (CAPTURE) see a gate.

### 3.6 Mono bass on/off

<!-- PANEL monobass -->
**Hold FREEZE, tap BANK.** For 1.5 s the LEDs tell you the new state:

- the **middle LEDs**, **2 and 4** and **B and C**: bass gathered in the
  middle (mono bass **on**, the default);
- the **outer LEDs**, **1 and 8** and **A and D**: bass out at the sides
  (mono bass **off**, full stereo all the way down).

The setting is remembered through power cycles. FREEZE doesn't toggle
freeze when you use it for this.

### 3.7 Buffer quality (longer buffers)

<!-- PANEL quality -->
One master setting for every mode that records into a buffer: **STRETCH,
CAPTURE, TAPE, BARBERPOLE** and **BLOOM**. Store the audio at a lower
sample rate and/or 8-bit and the same memory holds up to six times as
much sound.

**Hold BANK, tap TIME** (it doesn't matter how long you've held BANK).
A light runs down the 1 2 4 8 column: you're on the page, and **8** stays
lit while you are. **FREEZE** steps through the six settings. LEDs 1, 2
and 4 show the rate as a bar, **A** or **B** the bit depth:

| 1 2 4 | BANK | Setting | Length | Sound |
|---|---|---|---|---|
| ●○○ | A | 48 kHz 16-bit | 1× | full quality (the default) |
| ●●○ | A | 24 kHz 16-bit | 2× | top end stops at ~9.6 kHz |
| ●●● | A | 16 kHz 16-bit | 3× | top end stops at ~6.4 kHz: warm, dusty |
| ●○○ | B | 48 kHz 8-bit | 2× | full range, a fine grain of noise under loud parts |
| ●●○ | B | 24 kHz 8-bit | 4× | both |
| ●●● | B | 16 kHz 8-bit | 6× | both: lo-fi, longest |

**8** blinks while the choice differs from the current one. **BANK**
applies it: the whole 1 2 4 8 column and the A or B LED flash, then the
page closes. **TIME** (or 6 s) leaves without changing anything.
Applying restarts the running mode's buffer empty, so do it before you
record. Modes without a buffer don't notice.

How long that makes each buffer (seconds):

| Mode | 48k 16 | 24k 16 | 16k 16 | 48k 8 | 24k 8 | 16k 8 |
|---|---|---|---|---|---|---|
| STRETCH stereo | 0.30 | 0.60 | 0.90 | 0.59 | 1.18 | 1.76 |
| STRETCH mono | 0.89 | 1.77 | 2.66 | 1.75 | 3.49 | 5.23 |
| CAPTURE (capture length) | 0.50 | 1.01 | 1.51 | 0.99 | 1.99 | 2.98 |
| TAPE stereo (head 3 at 1× speed) | 0.60 | 1.19 | 1.79 | 1.19 | 2.37 | 3.56 |
| TAPE mono (head 3 at 1× speed) | 1.20 | 2.41 | 3.61 | 2.38 | 4.76 | 7.15 |
| BARBERPOLE (longest loop) | 0.50 | 1.00 | 1.50 | 1.00 | 2.00 | 3.00 |
| BLOOM (longest window; delay is 2×) | 0.28 | 0.56 | 0.84 | 0.56 | 1.12 | 1.68 |

The lower rates use a steep anti-alias filter (8th-order elliptic, 80 dB
down), so nothing folds back. TAPE's rate setting simply runs the tape 2× or
3× slower, which is what a real one would do. A **save slot remembers the
quality it was recorded with** and plays back at it, whatever the setting;
the next time the mode restarts it follows the setting again.

### 3.8 FFT size (spectral resolution)

<!-- PANEL fft -->
One master setting for the modes that work in the frequency domain:
**SPECTRAL** (and its SMEAR option), **CROSS, STRETCH, PUTZ, QUELL,
SPECDELAY** and **MIRROR**. Each has its own FFT size, the one it has
always had; this page lets you run them all at one size instead.

**Hold TIME, tap FREEZE**: a light runs up the 1 2 4 8 column, from 8 to
1, and the page is open. **FREEZE** steps through the settings. The 1 2 4
8 LEDs show the size as a bar, **A** alone means each mode's own size,
**A and B** together one size for all:

| 1 2 4 8 | BANK | Setting | Window | Sound |
|---|---|---|---|---|
| the running mode's size | A | each mode's own | | as always (the default) |
| ●○○○ | A B | 256 | 5 ms | sharp in time, coarse in pitch: transients stay crisp, partials blur into bands, smears turn grainy and robotic |
| ●●○○ | A B | 512 | 11 ms | |
| ●●●○ | A B | 1024 | 21 ms | |
| ●●●● | A B | 2048 | 43 ms | fine in pitch, soft in time: single partials, smooth glassy freezes, attacks wash out |

If the running mode has no FFT, the whole column and A glow dimly. The bar
blinks while the choice differs from the current one. **BANK** applies it
(the column and A, or A and B, flash), **TIME** (or 6 s) leaves without
changing anything. Applying restarts the running spectral mode (a frozen
spectrum is cleared); other modes don't notice. Every time constant (blur,
envelope followers, decays, delay times) is converted, so a knob does the
same thing at every size, only the resolution changes.

Own sizes: SPECTRAL, CROSS, STRETCH 2048; PUTZ, QUELL, MIRROR 1024;
SPECDELAY and SMEAR 512. PUTZ and SPECDELAY go up to 1024 (at 2048 they
don't fit in memory next to the rest of the mode) and stay there if you
pick 2048. At 256 the lowest bin is 187 Hz wide, so the very bottom
(below ~90 Hz) thins out in the resynthesising modes.

A **save slot remembers the FFT size** it was saved with (a frozen
spectrum only fits its own size) and plays back at it; the next time the
mode restarts it follows the setting again. The CPU cost is about the same
at every size (measured: within 5 % of each mode's own).

### 3.9 Teaching it volts (V/OCT calibration)

<!-- PANEL calibrate -->
Your calibration comes along from the stock firmware (1, After landing),
so you may never need this. There are two ways in:

- **The stock way:** hold the **TRIG** button while you power on.
- **The Monsoon way:** **hold TIME** (the menu opens), then also **hold
  BANK** for a second.

Then:

1. The **IN meter row blinks**. Leave every CV jack except V/OCT
   **unpatched**.
2. Send **1 V** into V/OCT, tap **TRIG** (or BANK). The **IN and OUT rows
   blink**. This step also learns the 0 V reading of every CV jack; the
   AUX normal is held at 0 V while you calibrate.
3. Send **3 V**, tap **TRIG** (or BANK). Done.

If **all 16 LEDs light** for 1.5 s, the readings looked wrong and nothing
was stored: check the voltages and try again.

### 3.10 IN MUTE and OUT MUTE

<!-- PANEL mutes -->
- **Tap IN MUTE** to mute the input; tap again to unmute. A 5 ms fade
  means it never clicks. It mutes the input before the engine (dry
  and wet alike), so tails, frozen buffers and voices keep sounding: mute
  the input and let the reverb ring out.
- **Tap OUT MUTE** to mute the module's output: after the engine, the
  mono bass and the limiter. Same 5 ms fade.
- **While a side is muted**, the red LED at the end of its meter row
  blinks. The rest of the row shows what passes the mute, so it stays dark.
- **Not saved:** both mutes are off every time you power up.
- **Which button is which** follows the stock firmware and is one of the
  things not yet checked on a real module (7). If IN MUTE turns out to mute
  the output and OUT MUTE the input, that's the reason, and it is a
  one-line change in the firmware.

### 3.11 AUX (drifting air)

<!-- PANEL aux -->
The AUX jack's normal feeds the normal of **nine CV jacks**: every one
except V/OCT (the four slider CVs, the four knob CVs and FREQ). Cirrostratus
can't tell which jacks are patched, so AUX affects every unpatched one.

- **Off** (the default): AUX is 0 V, so unpatched jacks do nothing, as on
  the stock firmware with its noise off.
- **On**: a slow, smooth random voltage of 0 to about 1.2 V. It moves to a
  new random value every half second, gliding there in about 0.2 s, and
  pushes every unpatched CV up by up to about a quarter of its range. The
  whole patch breathes a little.
- **Switch it:** hold **IN MUTE and OUT MUTE together for 1 s** (from the
  meters or knobs page). For 1.5 s the LEDs show the new state: **all of
  1 2 4 8 and A–D** lit = on; **8 and D** alone = off. The setting is
  saved. Holding the mutes this way doesn't mute anything.
- **A cable in AUX** replaces it with your own CV, for every unpatched jack
  at once.
- While you calibrate (3.9), AUX is held at 0 V.

### 3.12 Gestures at a glance

| Gesture | Does |
|---|---|
| tap TIME | meters page ↔ knobs page (3.1) |
| hold TIME | mode menu: POSITION picks the mode, tap BANK flips the bank, tap FREEZE steps the option, release TIME applies (3.2) |
| hold TIME, then hold BANK 1 s | V/OCT calibration (3.9) |
| hold TIME, tap FREEZE | FFT size page (3.8) |
| tap BANK | load page: TIME picks the slot, BANK loads, FREEZE arms or clears the morph source (3.3) |
| hold BANK | save page: TIME picks the slot, BANK saves (3.3) |
| hold BANK, tap TIME | buffer quality page: FREEZE steps, BANK applies, TIME or 6 s cancels (3.7) |
| hold FREEZE, tap TIME | envelope page: TIME moves, FREEZE steps the amount, BANK saves (3.4) |
| hold FREEZE, tap BANK | mono bass on/off (3.6) |
| tap FREEZE | freeze on/off |
| tap IN MUTE · OUT MUTE | input · output mute on/off (3.10) |
| hold IN MUTE + OUT MUTE 1 s | AUX random voltage on/off (3.11) |
| hold TRIG at power-up | V/OCT calibration (3.9) |
| any button during the sunrise | skips it, and does nothing else |

### 3.13 Lights at a glance

| Page / state | What the LEDs show |
|---|---|
| meters page | IN row = input level, OUT row = output level, always; the mode dimly on 1 2 4 8 + A/B |
| knobs page | A / B / C / D brightness = WET / STEREO / FEEDBACK / REVERB; the mode number dimly on 1 2 4 8 |
| mode menu | the mode number (1–15) at full brightness on 1 2 4 8; A = bank 1, B = bank 2 |
| option (1.5 s) | the option number (1–8) blinks on 1 2 4 8; the bank LED stays lit |
| load page | the slot's LED blinks; the morph source's LED stays lit (full ↔ dim if it's the slot you're on) |
| save page · saving | the slot's LED blinks, the other three glow dimly · the slot's LED lit |
| envelope page | row 1/A WET, 2/B STEREO, 4/C FEEDBACK, 8/D REVERB: A–D = up, 1 2 4 8 = down, dim = ½, full = 1; the cursor row blinks fast |
| mono bass (1.5 s) | on: 2, 4, B, C · off: 1, 8, A, D |
| AUX (1.5 s) | on: all of 1 2 4 8 and A–D · off: 8 and D |
| buffer page | a light runs down 1 → 8; then 1 2 4 = 48 / 24 / 16 kHz bar, A = 16-bit, B = 8-bit, 8 = on the page (blinking: a change is pending); applied: the column + A/B flash |
| FFT page | a light runs up 8 → 1; then the size as a bar on 1 2 4 8, A = each mode's own, A + B = one size for all; all dim = no FFT in this mode; applied: the column + A(/B) flash |
| muted | that meter row's red LED blinks |
| calibration | step 1: the IN row blinks · step 2: the IN and OUT rows blink |
| failed calibration · engine failure | all 16 LEDs lit (1.5 s for a failed calibration; stays lit for an engine failure) |
| sunrise | sparkles, halos from each group's middle, half → full sweep, then the mode, fading (3.0) |
| FREEZE LED | the freeze state |
| updating (bootloader) | FREEZE blinks fast, meters = level · all of TIME + BANK lit = writing · TIME + BANK blinking = error (1a) |

---

## 4. The output section, on every mode

```
IN ─► IN MUTE ─┬──────────────────────────────────────────┐ dry
               │                                          │
               └─► MODE ─► DC block ─► WET ─┬─(+ reverb)─(+)─► MONO BASS ─►
                    ▲                       │   if the mode    < 60 Hz
                    └── feedback loop ◄─────┘   uses it
                        if the mode uses it
    ─► LIMITER ─► OUT MUTE ─► OUT ─► OUT LEVEL · OUT VCA (analog)
```

<!-- PANEL output -->

- **WET**: equal power, dry ↔ wet. With mono bass switched off, fully dry
  is bit-transparent.
- **Hard stereo.** Left is left, right is right, exactly what the mode
  makes. Modes that are naturally wide (LEITNER, ENSEMBLE, HALL…) are wide
  on their own.
- **MONO BASS** (on by default): below 60 Hz the whole main out (dry and
  wet) is summed to mono, L+R halved to both sides; above it nothing
  changes. It's a 4th-order Linkwitz-Riley crossover, so the two halves add
  back up to a flat response. Your sub stays in the middle of the club,
  your vinyl cut stays cuttable. **Hold FREEZE, tap BANK** to switch it
  (3.6).
- **LIMITER**: −1 dBFS peak limiter, transparent below −2 dBFS, then TPDF
  dither into the codec.
- **OUT MUTE** comes last (3.10), then the analog OUT LEVEL and OUT VCA.

**Feedback and reverb as engine services.** A mode can borrow the
module's feedback loop or its reverb for one of its knobs when that's
truly the best control for it. CONVOLVE, BLOOM and SPRING borrow the
feedback loop for FEEDBACK (the classic Clouds feedback, with its built-in
howl tamer: two adaptive notches that talk down a whistling loop). The dry
voices and mono modes that have no space of their own borrow the reverb
(the 8-line FDN, 0.5 → 16 s) for REVERB: DIFFERENCE, CROSS, CAPTURE,
GENDY, PLATES, FALTUNG, LEITNER, TANTRI, ENSEMBLE, CHOIR, FOLDER, GAMELAN,
MIRROR and SPECTRAL's SMEAR option. Everything else is the mode's own, and a mode pays no CPU for a
reverb it doesn't use.

---

## 5. Modes

**PITCH** = TUNE knob + V/OCT + FREQ. WET always blends dry and wet.
**STEREO**, **FEEDBACK** and **REVERB** are each mode's own: STEREO at
noon and FEEDBACK/REVERB at zero give the mode plain. "Keeps" = what a
save slot holds for that mode. The modes are the Monsoon's, unchanged;
where a chapter mentions the module, it means the Typhoon just the same.

<!-- MODES -->

---

## 6. Under the cloud (specs)

| | |
|---|---|
| Sample rate | 48 kHz (Clouds: 32 kHz) |
| Processing | 32-bit float; buffers 16-bit (or 8-bit) mantissa + exponent per 32 samples, stored at 48, 24 or 16 kHz (buffer quality, 3.7) |
| Interpolation | Hermite everywhere it reads between samples |
| FFT | ARM CMSIS-DSP real FFT, heavy work between interrupts; 256–2048 points (FFT size, 3.8) |
| RAM per mode | about 130 KB (only the running mode holds memory) |
| CPU | the Monsoon's engine, unchanged; measured on an emulated STM32F405 at ≤ ~2200 instructions per stereo frame including the output section. The Typhoon runs the same core at the same clock |
| Knobs and CV | every slider and knob + its CV (±5 V = the whole range); FREQ ±5 V = ±12 semitones; LEDs with 8-step brightness |
| Output | hard stereo, mono bass below 60 Hz (LR4, switchable), limiter, TPDF dither, click-free IN/OUT mutes (5 ms) |
| Save slots | 4 × 128 KB of flash (sectors 8–11, where the stock firmware keeps its own) |
| Settings | calibration, mode, display page, per-mode option + envelope routing, mono bass, buffer quality, FFT size, AUX on/off |

The source lives in the repository's `cirrostratus-monsoon/` folder: the
shared engine in `monsoon/dsp`, the Typhoon's panel code in `typhoon/`
and its notes in `docs/TYPHOON.md`.

---

## 7. Not yet verified on hardware

This edition has been built and checked in software: the update WAV
decodes back to the exact firmware, and the firmware starts up on an
emulated chip. It has **not yet run on a real Typhoon**. These things come
from the Typhoon's schematics, parts list and stock firmware and still need
checking on a module:

1. **The mute buttons:** which one is IN MUTE and which is OUT MUTE.
2. **The LEDs:** that they light the right way round, and how bright the
   FREEZE LED is.
3. **The bootloader:** that your module carries the stock audio
   bootloader (playing the WAV is the test).
4. **Directions:** that every slider and knob turns up the way you'd
   expect.
5. **CV scaling:** the range and direction of the CV, V/OCT and FREQ jacks.
6. **The OUT meter:** its curve is copied from the stock firmware, not
   measured.
7. **AUX:** the real range of the random voltage and how far it moves each
   CV.
8. **Knob reading:** that every knob reads cleanly and without lag.
9. **CPU:** the same as the Monsoon's on paper (same core and clock); not
   measured on the Typhoon's chip.
10. **The codec:** that audio starts up at 48 kHz.

If you try it: a short note of what works and what doesn't helps
everybody. You can always go back to the stock firmware the same way you
came (1a).

---

## 8. Thanks and licences

- **Mutable Instruments / Émilie Gillet**: Clouds, stmlib, the audio
  bootloader and its QPSK encoder (MIT).
- **Tom Burns**: the Monsoon port of the Clouds firmware (MIT).
- **Jak Plugg** and **Calsynth**: the Monsoon and Typhoon hardware.
- **SuperParasites**: the Typhoon's stock firmware, whose Supercell pin
  map and scaling this port follows (MIT).
- **ARM**: CMSIS-DSP FFT (BSD-style licence, see `stmlib/third_party`).
- The Chloaelis, Tantri, Oneirophone, Quell, Leitner and ConvoSolution
  ideas come from their own projects; the DSP here is written for the M4.

Cirrostratus is MIT-licensed. Look up. ☁
