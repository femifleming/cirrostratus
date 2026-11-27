# Cirrostratus

### *a thin, high sheet of cloud for the Typhoon*

**Version 1.5.3 · Typhoon edition**

<img class="panel" src="img/typhoon/cover.png" width="300" align="right" alt="Typhoon panel: cover">


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

<img class="panel" src="img/typhoon/quick.png" width="300" align="right" alt="Typhoon panel: quick">


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

<img class="panel" src="img/typhoon/install.png" width="300" align="right" alt="Typhoon panel: install">


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

<img class="panel" src="img/typhoon/panel.png" width="300" align="right" alt="Typhoon panel">

| On the panel | We call it | |
|---|---|---|
| ① POSITION slider + CV | **POSITION** | also picks the mode in the mode menu (the slider only, not its CV) |
| ② DENSITY slider + CV | **DENSITY** | |
| ③ SIZE slider + CV | **SIZE** | |
| ④ TEXTURE slider + CV | **TEXTURE** | |
| ⑤ TUNE knob + V/OCT + FREQ | **PITCH** | TUNE: ±4 octaves, gently snapping to semitones, noon = no change. V/OCT is calibrated; FREQ adds ±1 octave over ±5 V |
| ⑥ WET knob + CV | **WET** | dry ↔ wet, equal power, on every mode |
| ⑦ STEREO knob + CV | **STEREO** | the mode's own control: bipolar, plain at noon |
| ⑧ FEEDBACK knob + CV | **FEEDBACK** | the mode's own control: plain at zero |
| ⑨ REVERB knob + CV | **REVERB** | the mode's own control: plain at zero |
| ⑩ FREEZE button + HOLD jack | **FREEZE** | a high gate on HOLD flips whatever the button says |
| ⑪ TRIG jack + TRIG button | **TRIG** | a different job in every mode |
| ⑫ TIME | **TIME** | display, mode menu |
| ⑬ BANK | **BANK** | load, save, bank flip |
| ⑭ IN MUTE · OUT MUTE | **mutes** | click-free input and output mutes (3.10) |
| ⑮ meters | **IN / OUT meters** | input level (top row), output level (second row) |
| ⑯ IN LEVEL + IN VCA | input level | analog, as on the stock firmware |
| ⑰ OUT LEVEL + OUT VCA | output level | analog; the OUT meter follows it |
| ⑱ AUX | **AUX** | the normal behind nine CV jacks: 0 V, or a slow random voltage (3.11) |
| ⑲ IN L · IN R | stereo in | IN R follows IN L when unpatched |
| ⑳ OUT L · OUT R | stereo out | hard stereo |

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

<img class="panel" src="img/typhoon/sunrise.png" width="300" align="right" alt="Typhoon panel: sunrise">

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

<img class="panel" src="img/typhoon/look.png" width="300" align="right" alt="Typhoon panel: look">

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

<img class="panel" src="img/typhoon/menu.png" width="300" align="right" alt="Typhoon panel: menu">

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

<img class="panel" src="img/typhoon/slots.png" width="300" align="right" alt="Typhoon panel: slots">

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

<img class="panel" src="img/typhoon/envelope.png" width="300" align="right" alt="Typhoon panel: envelope">

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

<img class="panel" src="img/typhoon/monobass.png" width="300" align="right" alt="Typhoon panel: monobass">

**Hold FREEZE, tap BANK.** For 1.5 s the LEDs tell you the new state:

- the **middle LEDs**, **2 and 4** and **B and C**: bass gathered in the
  middle (mono bass **on**, the default);
- the **outer LEDs**, **1 and 8** and **A and D**: bass out at the sides
  (mono bass **off**, full stereo all the way down).

The setting is remembered through power cycles. FREEZE doesn't toggle
freeze when you use it for this.

### 3.7 Buffer quality (longer buffers)

<img class="panel" src="img/typhoon/quality.png" width="300" align="right" alt="Typhoon panel: quality">

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

<img class="panel" src="img/typhoon/fft.png" width="300" align="right" alt="Typhoon panel: fft">

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

<img class="panel" src="img/typhoon/calibrate.png" width="300" align="right" alt="Typhoon panel: calibrate">

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

<img class="panel" src="img/typhoon/mutes.png" width="300" align="right" alt="Typhoon panel: mutes">

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

<img class="panel" src="img/typhoon/aux.png" width="300" align="right" alt="Typhoon panel: aux">

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

<img class="panel" src="img/typhoon/output.png" width="300" align="right" alt="Typhoon panel: output">


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

### Bank 1 (BANK LED A) · 1–15

#### 1 · SPRING — a spring reverb tank
*Forecast: thunder in a tin roof, a long way off.*

The tank from the back of a guitar amp. Every spring carries the low
frequencies slower than the high ones, so each echo arrives as a falling
chirp, and every trip along the spring spreads it further: the "boing" on a
snare, the "drip" on a picked note. Each spring is a delay loop with 32
dispersing allpass filters inside it. New in 1.5.2 (mode 1 was GRAINS until
1.5).

<img class="panel" src="img/typhoon/mode01.png" width="300" align="right" alt="Typhoon panel: mode 1 SPRING">

| Control | |
|---|---|
| ① POSITION | tone: the tank's band limit, dark 1.2 kHz → bright 8 kHz |
| ② SIZE | spring length: 25 → 80 ms per trip (the spacing of the echoes) |
| ③ DENSITY | decay: 0.4 → 8 s |
| ④ TEXTURE | dispersion: a short splash → long, liquid chirps (the drip) |
| ⑤ PITCH | spring tension: the trip follows V/OCT (5 to 150 ms), so the flutter between echoes is pitched |
| ⑥ FREEZE | the tank rings on: input off, no decay, tone wide open |
| ⑦ TRIG | kick the tank: a knock into every spring, the crash |
| ⑧ STEREO | stereo width: left = the springs summed (mono), noon = each spring on its side, right = wider still |
| ⑨ FEEDBACK | the module's feedback loop: the tank's output back into its input (dub) |
| ⑩ REVERB | dwell: how hard the tank is driven, clean at 0, up to ×6 into the driver's saturation (a hot spring) |
| ⑪ Options | 1 a pair (A left, B right) · 2 a tank of three (A left, B right, C in the middle) · 3 one spring |
| Keeps / morph | nothing / no |


#### 2 · SPECTRAL — spectral freeze / resynthesis
*Forecast: a held moment, refracted into its colours.*

2048-point phase vocoder with peak-locked partials (64 ms latency; 256–2048 with the FFT size setting, 3.8).

<img class="panel" src="img/typhoon/mode02.png" width="300" align="right" alt="Typhoon panel: mode 2 SPECTRAL">

| Control | |
|---|---|
| ① POSITION | partial thinning: all bins → only the strongest peaks. With a morph slot armed: blend from the current spectrum to the slot's |
| ② SIZE | harmonic snap: pulls partials onto the harmonic series of the PITCH root |
| ③ DENSITY | blur (smears the spectrum over time) |
| ④ TEXTURE | tilt, dark ↔ bright, flat at noon (±9 dB/oct) |
| ⑤ PITCH | transposition without changing speed |
| ⑥ FREEZE | hold the spectrum; it keeps resynthesising with a slight drift |
| ⑦ TRIG | grab a new spectrum while frozen |
| ⑧ STEREO | noon = as before. Turning it shifts every partial by the same number of Hz (not a ratio), so harmonic sounds go inharmonic. Left shifts down, right shifts up, exponential: a few Hz near noon, ±1 kHz at the ends |
| ⑨ FEEDBACK | regeneration: the last output spectrum comes back as a floor under the next frame (held 0.1 → 8 s). On its own it sustains; with STEREO or PITCH set every pass moves the partials again, so they spiral up or down |
| ⑩ REVERB | phase diffusion: random phases blend in, coherent → a whispered wash in the same colours |
| ⑪ Options | 1 as above · 2 SMEAR (below) |
| ⑫ Keeps / morph | the frozen spectrum / yes (option 1) |

**Option 2 · SMEAR** — *a photograph left out in the rain.* The spectrum
is blurred in time and a little across frequency, and its partials carry on.
The bins around each peak of the blurred spectrum move together as one
partial, so a long blur never flutters or cancels itself out (1.5.2). Since
1.5.3 the transform is four times finer, 2048 points (23 Hz apart instead of
94: chords stay chords instead of a grainy blend; 256–2048 with the FFT size
setting, 3.8), and **TEXTURE** sets the character: fully left the blurred
sound is held as steady, clear tones; turning right, every partial's phase
wanders more and more, until fully right it is a soft, diffuse wash, like a
long reverb of the same colours. 4× overlap, 53 ms latency. Mono: the mid
(L+R) is smeared and comes out of both sides.

<img class="panel" src="img/typhoon/mode02b.png" width="300" align="right" alt="Typhoon panel: mode 2 SPECTRAL">

| Control | |
|---|---|
| ① DENSITY | blur: none (the sound as it is) → about 5 s |
| ② TEXTURE | diffusion: clear, steady tones (left) → a smooth, diffuse wash (right); the level stays the same |
| ③ FREEZE | hold: the blur stops where it is |
| ④ REVERB | reverb (the module's FDN reverb: SMEAR's space) |
| Keeps / morph | nothing / no |

#### 3 · CONVOLVE — freeze captures the impulse response
*Forecast: the room you're standing in is whatever you just played.*

Your own audio becomes the space the sound plays in. The impulse response
(IR) can be up to 139 ms, and FEEDBACK makes it ring on much longer. The
mode starts with a small synthetic room so it sounds straight away.

<img class="panel" src="img/typhoon/mode03.png" width="300" align="right" alt="Typhoon panel: mode 3 CONVOLVE">

| Control | |
|---|---|
| ① FREEZE (on) / TRIG | capture a new IR from the last 139 ms of input |
| ② POSITION | where in that capture the IR starts (bottom = newest) |
| ③ SIZE | IR length, with fade-out |
| ④ DENSITY | IR envelope: reverse swell ← as captured (noon) → faster decay |
| ⑤ TEXTURE | IR brightness: low-pass below noon, treble boost above |
| ⑥ PITCH | IR playback rate |
| ⑦ STEREO | noon = as before. Left: only the attacks of the input reach the IR. Right: only the body (attacks ducked, so hits swell instead of crack). It shapes the input before everything, so captures hear it too |
| ⑧ FEEDBACK | feedback (the module's feedback loop, with its howl tamer): the IR rings on much longer |
| ⑨ REVERB | IR smear: the IR gets eight decaying echoes of itself (3 → 97 ms), and captures melt into a dense wash |
| Keeps | the capture and its settings |

#### 4 · RESONATOR — sympathetic strings with jawari
*Forecast: eight strings hum along, with a buzzing bridge.*

Eight tuned strings, excited by the input, tuned to the cent.

<img class="panel" src="img/typhoon/mode04.png" width="300" align="right" alt="Typhoon panel: mode 4 RESONATOR">

| Control | |
|---|---|
| ① PITCH | root (C2 at noon), V/OCT |
| ② SIZE | tuning: unison/octaves → fifths → sitar sympathetic set → harmonic series → inharmonic |
| ③ DENSITY | decay 0.2 → 20 s |
| ④ POSITION | brightness and pickup point |
| ⑤ TEXTURE | jawari: clean string → strong sitar-bridge buzz |
| ⑥ FREEZE | infinite sustain, input cut |
| ⑦ TRIG | pluck all strings |
| ⑧ STEREO | noon = as before. Left: detune, the strings spread up to ±25 cents (beating, a chorus of strings). Right: stretch, intervals widen up to +10 % (an octave ~+120 cents: piano-like, then bell-like) |
| ⑨ FEEDBACK | sympathetic coupling: energy passes from string to string. Strings tuned close together trade it back and forth (slow beating, blooming), and a note in one wakes the others |
| ⑩ REVERB | body: the strings sound through a resonant wooden box (~8.5 ms resonance, tail up to ~0.35 s) |

#### 5 · DIFFERENCE — difference tones (after the Oneirophone)
*Forecast: notes that only happen inside your head. Really.*

The input is frequency-shifted into high pairs of tones, f and f + Δ. Your
ear makes the tone at Δ, a note that isn't in the signal.

<img class="panel" src="img/typhoon/mode05.png" width="300" align="right" alt="Typhoon panel: mode 5 DIFFERENCE">

| Control | |
|---|---|
| ① PITCH | the heard tone Δ (C3 at noon), V/OCT |
| ② POSITION | register of the pairs, 2 → 8 kHz |
| ③ SIZE | input bandwidth: narrow (purest illusion) → whole input (rich) |
| ④ DENSITY | 1 → 6 pairs: Δ × 1, 3/2, 5/4, 2, 7/4, 9/8 (a chord of phantom notes) |
| ⑤ TEXTURE | purity: 0 = only in your ear; up = the difference tone becomes physically present (works through any speaker) |
| ⑥ FREEZE | drone of pure sine pairs at the held level |
| ⑦ STEREO | noon = as before. Left: glide, the heard note slides to each new pitch (20 ms → 2 s). Right: dichotic, f to the left ear and f + Δ to the right; on headphones the difference tone gives way to binaural beats |
| ⑧ FEEDBACK | spiral: the shifted sound recirculates (50 ms) and climbs by Δ on every pass. Echoes rise; held sounds build a ladder f, f+Δ, f+2Δ… that makes the phantom note stronger |
| ⑨ REVERB | reverb (the module's FDN reverb; both tones reverberate, so the phantom note survives) |

#### 6 · CROSS — left/right spectral morph
*Forecast: two weather fronts collide.*

Left input = modulator, right input = carrier (64 ms latency). With only IN L patched,
both sides are the same signal. FFT 2048 (256–2048 with the FFT size
setting, 3.8).

<img class="panel" src="img/typhoon/mode06.png" width="300" align="right" alt="Typhoon panel: mode 6 CROSS">

| Control | |
|---|---|
| ① POSITION | vocoder (L's envelope on R) → morph → L's magnitudes with R's phases |
| ② DENSITY | envelope resolution (coarse vocoder bands → fine detail) |
| ③ TEXTURE | envelope speed (attack/release) |
| ④ SIZE | formant shift of the modulator (×0.5 → ×2) |
| ⑤ PITCH | carrier transposition |
| ⑥ FREEZE | hold the modulator |
| ⑦ TRIG | toggle holding the carrier |
| ⑧ STEREO | noon = as before (carrier = right input). Left: the carrier fades to white noise (whispered; consonants come through). Right: it fades to an internal buzz played by PITCH (C3 at noon, V/OCT), a classic vocoder from one input; PITCH then stops transposing R |
| ⑨ FEEDBACK | formant emphasis: the modulator's envelope sharpens (peak and energy kept): more talkative, then resonant, wah-like peaks |
| ⑩ REVERB | reverb (the module's FDN reverb; CROSS is mono, this gives it a stereo room) |

#### 7 · STRETCH — extreme time-stretch
*Forecast: one second, smeared across an afternoon.*

A phase vocoder with phase locking reads a recorded buffer slowly: 0.30 s
stereo or 0.89 s mono, up to 1.76 s / 5.23 s at a lower buffer quality (3.7).
FFT 2048 (256–2048 with the FFT size setting, 3.8: smaller is more
transient, less smooth).

<img class="panel" src="img/typhoon/mode07.png" width="300" align="right" alt="Typhoon panel: mode 7 STRETCH">

| Control | |
|---|---|
| ① SIZE | stretch: 1× → ~1000× at 0.9 → standing still from about 0.98 |
| ② POSITION | where the read zone sits in the buffer |
| ③ DENSITY | smear |
| ④ TEXTURE | coherent (phase vocoder) → Paulstretch (random phases) |
| ⑤ PITCH | transposition without speed change |
| ⑥ FREEZE | stop recording |
| ⑦ TRIG | jump to the newest audio |
| ⑧ STEREO | noon = as before. Left: the read slows, stands still at 9 o'clock, and runs backwards at full left. Right: scatter, the read window jumps to random spots (up to half the buffer away, ~10 jumps/s) |
| ⑨ FEEDBACK | re-record: the output goes back into the recording, so stretches get stretched again and smear into drones (nothing is recorded while frozen) |
| ⑩ REVERB | octave halo: every partial also sounds an octave up, a shimmer over the stretch |
| ⑪ Options | 1 stereo · 2 mono (longer) |
| Keeps | the buffer |

#### 8 · SPREEK — voice
*Forecast: everything starts talking (and singing).*

LPC speech analysis, five-formant vowels and a singing carrier. STEREO, FEEDBACK
and REVERB shape the internal carrier, so they need DENSITY above zero.

<img class="panel" src="img/typhoon/mode08.png" width="300" align="right" alt="Typhoon panel: mode 8 SPREEK">

| Control | |
|---|---|
| ① POSITION | bottom: the input's own voice envelope (talkbox); then vowels a → e → i → o → u |
| ② SIZE | vocal-tract length (formants × 0.7 → 1.4) |
| ③ DENSITY | source: the input itself → an internal singing carrier at PITCH, following the input's level |
| ④ TEXTURE | smooth → breathy |
| ⑤ PITCH | carrier pitch, V/OCT |
| ⑥ FREEZE | hold the phoneme as a drone |
| ⑦ TRIG | consonant burst |
| ⑧ STEREO | noon = as before. Left: glide (PITCH slides; an octave takes up to ~0.6 s). Right: vibrato (5.3 Hz, up to ±1 semitone) |
| ⑨ FEEDBACK | growl: every other glottal period comes weaker and longer — from a rough edge to vocal fry with an undertone an octave down (pitch stays put) |
| ⑩ REVERB | choir: two more carriers join, up to ±22 cents apart and drifting, so one voice becomes a small chorus |
| ⑪ Options | 1 adult formants · 2 child formants |

#### 9 · CAPTURE — captured wavetable voice
*Forecast: catch a sound, play it like a synth.*

FREEZE turns the last half second of input (up to 3 s at a lower buffer
quality, 3.7) into a 16-frame wavetable. The
table is band-limited per octave, so it stays alias-free up high. Until the
first capture it plays a sine → saw table.

<img class="panel" src="img/typhoon/mode09.png" width="300" align="right" alt="Typhoon panel: mode 9 CAPTURE">

| Control | |
|---|---|
| ① FREEZE | capture (pitch-detected, one period per frame) |
| ② POSITION | scan through the frames (and on into the morph slot's frames) |
| ③ PITCH | V/OCT, C3 at noon |
| ④ DENSITY | unison 1 → 7 voices, detuned and spread |
| ⑤ TEXTURE | brightness |
| ⑥ SIZE | envelope: pluck → pad |
| ⑦ TRIG | gate. Drones until the first gate arrives |
| ⑧ STEREO | noon = as before. Left: bend — phase distortion, the first half of each cycle read up to 6× faster (CZ style). Right: sync formant — the table read up to 6× per cycle under a soft window, a resonant peak sweeping up. Stays alias-free |
| ⑨ FEEDBACK | self-FM: each voice bends its own phase (DX-style operator feedback) — warmer, then saw-like, then a rasp |
| ⑩ REVERB | reverb (the module's reverb, for a voice with no space of its own) |
| Keeps / morph | the table / yes |

#### 10 · GENDY — dynamic stochastic synthesis (Xenakis)
*Forecast: Brownian drizzle, organised by Xenakis.*

Two independent GENDYN voices (left/right). The breakpoints of a waveform
take bounded random walks.

<img class="panel" src="img/typhoon/mode10.png" width="300" align="right" alt="Typhoon panel: mode 10 GENDY">

| Control | |
|---|---|
| ① PITCH | centre frequency, V/OCT |
| ② DENSITY | 3 → 40 breakpoints |
| ③ TEXTURE | step size: static → restless |
| ④ SIZE | barriers: stable pitch → wild |
| ⑤ POSITION | random distribution: Cauchy → logistic → hyperbolic cosine → arcsine |
| ⑥ FREEZE | freeze the walks (the waveform repeats exactly) |
| ⑦ TRIG | reseed the breakpoints from the input's last period |
| ⑧ STEREO | noon = both voices around PITCH. The right voice moves in whole semitones: left down to an octave below, right up to an octave above (fifths, fourths, octaves) |
| ⑨ FEEDBACK | cross-coupling: each voice stretches or squeezes the other's segments (up to ±80 %), so the two walks start steering each other |
| ⑩ REVERB | reverb (the module's reverb) |
| Keeps | the breakpoints |

#### 11 · SHIMMER — shimmer cascade
*Forecast: a reverb whose echoes keep climbing toward the sun.*

A diffuse four-line delay network with pitch shifters inside, so every pass
climbs (or falls) again.

<img class="panel" src="img/typhoon/mode11.png" width="300" align="right" alt="Typhoon panel: mode 11 SHIMMER">

| Control | |
|---|---|
| ① POSITION | harmonic set: plain · octave · fifth · octave + fifth · Oneirophone ratios (7/4, 9/8, 3/2) · octave down |
| ② SIZE | room size |
| ③ DENSITY | decay, up to endless |
| ④ TEXTURE | damping |
| ⑤ PITCH | extra transposition of the shift |
| ⑥ FREEZE | hold forever, input muted |
| ⑦ TRIG | clear the tail |
| ⑧ STEREO | noon = as before. Left: less of each pass is shifted (fully left = a plain reverb in any set). Right: mirror — the fourth line falls by the inverse of the set's interval, so the cascade climbs and falls |
| ⑨ FEEDBACK | tank modulation: the four delay lines wobble on two slow LFOs; the ringing smears into a lush, chorused tail |
| ⑩ REVERB | pre-delay 0 → 250 ms before the network (glides when turned) |

#### 12 · TAPE — multi-head tape echo
*Forecast: warm, wobbly, three heads at the ready.*

Variable-speed tape with an anti-aliased record path: slower tape gives
longer, darker echoes. Heads at 1×, 2× and 3× spacing (0.2 / 0.4 / 0.6 s at
1× in stereo, about twice that in mono; up to 6× longer at a lower buffer
quality, 3.7).

<img class="panel" src="img/typhoon/mode12.png" width="300" align="right" alt="Typhoon panel: mode 12 TAPE">

| Control | |
|---|---|
| ① SIZE | tape speed ×0.25 → ×2 (×1 at noon) |
| ② PITCH | fine speed, V/OCT (together with SIZE: ×0.0625 → ×3; the tape stops speeding up at ×3) |
| ③ POSITION | heads: 1 · 2 · 3 · 1+2 · 2+3 · 1+3 · 1+2+3 · all into the repeats · swell |
| ④ DENSITY | feedback, up to gentle self-oscillation |
| ⑤ TEXTURE | tape character: wow, flutter, saturation, head bump |
| ⑥ FREEZE | loop hold: repeats sustain, no new input |
| ⑦ TRIG | tape stop (spins down); next TRIG spins back up |
| ⑧ STEREO | noon = as before. Left: darker repeats (loop low-pass 7 kHz → 500 Hz). Right: thinner repeats (loop high-pass up to 2 kHz), the dub telephone fade |
| ⑨ FEEDBACK | ping-pong: the repeats cross to the other side (fully right = every repeat swaps). Stereo tape only |
| ⑩ REVERB | diffusion in the loop: every pass smears a little more, so the echoes melt into a spring-like wash |
| ⑪ Options | 1 stereo · 2 mono (twice as long) |
| Keeps | the tape |

#### 13 · PLATES — sympathetic plates (after the Ronin plates)
*Forecast: sheet metal (and glass, and wood) singing back.*

24 plate modes from real plate physics, read by two pickups and excited by
the input.

<img class="panel" src="img/typhoon/mode13.png" width="300" align="right" alt="Typhoon panel: mode 13 PLATES">

| Control | |
|---|---|
| ① PITCH | root, V/OCT |
| ② SIZE | shape: square plate → long bar |
| ③ POSITION | strike/pickup position |
| ④ DENSITY | decay 0.3 → 30 s |
| ⑤ TEXTURE | material: metal → glass → wood |
| ⑥ FREEZE | ring forever, input cut |
| ⑦ TRIG | strike |
| ⑧ STEREO | noon = as before. Left: the modes are pulled onto whole harmonics (fully left = a tuned, pitched plate). Right: the series stretches — more clang, bell / tam-tam |
| ⑨ FEEDBACK | tension: hard hits ring sharp (up to ~3 semitones) and glide back down as they decay |
| ⑩ REVERB | reverb (the module's reverb, so a struck plate gets a room) |

#### 14 · CHLOE — the Chloaelis strip
*Forecast: your Chloaelis, shrunk to fit the cloud.*

Your Chloaelis channel strip, one per side, hard left and right: multimode
filter → 12AX7 tube → delay. The Lorenz attractor pushes the left and right
cutoffs apart and lets the filter wander between low-pass and band-pass.

<img class="panel" src="img/typhoon/mode14.png" width="300" align="right" alt="Typhoon panel: mode 14 CHLOE">

| Control | |
|---|---|
| ① POSITION | filter cutoff 30 Hz → 16 kHz |
| ② TEXTURE | tube drive (bottom = tube out) and filter resonance |
| ③ SIZE | delay time 20 ms → 1.6 s (tape: up to 0.45 s). String engine: interval of the right string (unison, 5th, octave, 12th, 2 octaves, 4th) |
| ④ DENSITY | regeneration; past about 85 % it runs away and sings |
| ⑤ PITCH | transposes the cutoff (play the resonant filter). String engine: the note |
| ⑥ FREEZE | delay freeze: PT2399 RAM held bit for bit, BBD buckets, tape loop, string |
| ⑦ TRIG | random freeze (0.2–1.5 s). String engine: pluck |
| ⑧ STEREO | the Lorenz attractor: noon = as before. Left: calmer and shallower until it stops (both cutoffs together, the filter halfway LP/BP, ¼ speed). Right: up to 8× faster and ±3 octaves, the filter wandering LP → BP → HP |
| ⑨ FEEDBACK | master feedback, crossed like the plug-in: left out → right in, right out → left in. With regen up it ping-pongs and can sing |
| ⑩ REVERB | diffusion in the regen loop: every repeat smears a little more and the echoes blur into a cloud (string engine: a haze on the strings) |
| ⑪ Options | 1 PT2399 · 2 BBD · 3 tape · 4 Karplus-Strong string, all filter → tube → delay; 5–8 the same as delay → tube → filter |

On the module: the tube runs 2× oversampled (4× in the plug-in), one
delay line per side (time changes glide), the tape is 0.45 s per side with
fixed wear, and there are no poly taps or flock. The emulated chips stop
raising their clock where the module would run out of time (PT2399 at
160 kHz, below 0.275 s; BBD at 64 kHz, below 64 ms) and use a shorter line
instead: the delay time is the same, short PT2399 echoes are a little
darker than the real chip's.

#### 15 · FALTUNG — the built-in space library
*Forecast: eight different rooms, all in the same cloud.*

Convolution with eight stereo spaces synthesized into the firmware (they
live in flash, so they cost no RAM): room, hall, plate, spring, cabinet,
wooden body, metal sheet, cathedral.

<img class="panel" src="img/typhoon/mode15.png" width="300" align="right" alt="Typhoon panel: mode 15 FALTUNG">

| Control | |
|---|---|
| ① POSITION | which space (8 zones, 30 ms crossfade) |
| ② SIZE | how much of the IR (shorter also saves CPU) |
| ③ DENSITY | pre-delay 0 → 200 ms |
| ④ TEXTURE | tilt on the wet sound, dark ↔ bright |
| ⑤ PITCH | wet filter: high-cut ← noon (full range) → low-cut |
| ⑥ FREEZE | input muted, the space loops back on itself forever |
| ⑦ TRIG | next space |
| ⑧ STEREO | early ↔ late: noon = the IR as it is. Left: the tail fades away (close, dry). Right: the early part fades and the tail swells (far away). Same loudness |
| ⑨ FEEDBACK | feedback through the IR: the space builds on itself, longer and denser; it never howls (FREEZE is the endless version) |
| ⑩ REVERB | a long smooth FDN tail (0.5 → 16 s) behind the space (the IRs are at most 170 ms) |

### Bank 2 (BANK LED B) · 16–30
#### 16 · QUELL — resonance suppressor
*Forecast: harsh peaks clearing by afternoon.*

A spectral dynamic EQ: it finds narrow ringing peaks standing above their
neighbours and pushes them down, stereo-linked so the image doesn't move
(27 ms latency). FFT 1024 (256–2048 with the FFT size setting, 3.8).

<img class="panel" src="img/typhoon/mode16.png" width="300" align="right" alt="Typhoon panel: mode 16 QUELL">

| Control | |
|---|---|
| ① POSITION | where it listens hardest, 100 Hz → 16 kHz |
| ② SIZE | selectivity: only sharp ringing → broad honky areas |
| ③ DENSITY | depth, up to 24 dB of cut (bottom = transparent) |
| ④ TEXTURE | speed: slow and smooth → fast |
| ⑤ PITCH | output trim ±12 dB |
| ⑥ FREEZE | hold the current cut curve as a static EQ |
| ⑦ TRIG | delta: hear only what's being removed |
| ⑧ STEREO | noon = stereo-linked. Left: mid only (the sides pass untouched). Right: sides only |
| ⑨ FEEDBACK | hard threshold: anything louder than a level falling 0 → −72 dBFS is pushed down too; all the way up it squashes the spectrum flat (up to DENSITY's depth) |
| ⑩ REVERB | halo: what's cut rings on as a soft spectral tail, 0.3 → 5 s (also heard in delta) |

#### 17 · LEITNER — spatial path panner (after Leitner / Xenakis)
*Forecast: the sound takes flight and circles your head.*

The input becomes a source flying along a path around you, with ear-delay,
head shadow, distance, air, Doppler and four reflections from a virtual
room. Works on headphones and speakers.

<img class="panel" src="img/typhoon/mode17.png" width="300" align="right" alt="Typhoon panel: mode 17 LEITNER">

| Control | |
|---|---|
| ① POSITION | where the path is centred: noon = in front, ¾ = left, ¼ = right, ends = behind |
| ② SIZE | radius / distance, 1 → 10 m |
| ③ DENSITY | speed, 0.02 → 4 trips per second |
| ④ TEXTURE | room: open field → big room with strong reflections |
| ⑤ PITCH | Doppler: none ← physical (noon) → exaggerated ×3 |
| ⑥ FREEZE | stop moving; the source stays where it is |
| ⑦ TRIG | next path: circle → figure-8 → fly-by → spiral → random flight |
| ⑧ STEREO | noon = a smooth glide. Left (Leitner): the path becomes 8 loudspeaker stations; the source rests at each and hops to the next. Right (Xenakis): every trip round the path takes a chance length |
| ⑨ FEEDBACK | trail: the sound is re-emitted every 150 ms from wherever the source has flown to, darker each time |
| ⑩ REVERB | the room's late tail (the FDN reverb) |

#### 18 · TANTRI — tabla + tanpura
*Forecast: monsoon season in Varanasi.*

A tanpura drone with a curved jawari bridge, and a tabla pair you play with
TRIG. Your input also sets the strings and drum heads ringing.

<img class="panel" src="img/typhoon/mode18.png" width="300" align="right" alt="Typhoon panel: mode 18 TANTRI">

| Control | |
|---|---|
| ① POSITION | which stroke TRIG plays: Na · Tin · Tun · Te · Ge · Ke · Dha · Dhin |
| ② SIZE | bayan palm pressure: bends the bass drum up to +55 % (the "ghe" glide) |
| ③ DENSITY | tanpura: silent → louder and faster plucking (Pa Sa Sa sa, 30 → 90 per minute) |
| ④ TEXTURE | jawari: plain string → full buzzing bridge |
| ⑤ PITCH | Sa, V/OCT (C3 at noon); dayan sits at upper Sa, bayan below |
| ⑥ FREEZE | the drone sustains forever; plucking stops; the drums still play |
| ⑦ TRIG | play the stroke |
| ⑧ STEREO | the tanpura's first string: left third Ma, noon Pa, right third Ni |
| ⑨ FEEDBACK | ring: the drum heads ring up to 5× longer, the closed strokes stop damping, and the drone sets the drums humming |
| ⑩ REVERB | a room round the drums and the drone (the FDN reverb) |

#### 19 · HARMONIZER — scale harmonizer
*Forecast: a choir of you, always in key.*

Tracks your pitch and adds up to three voices exactly on scale degrees, even
if you're a little sharp.

<img class="panel" src="img/typhoon/mode19.png" width="300" align="right" alt="Typhoon panel: mode 19 HARMONIZER">

| Control | |
|---|---|
| ① POSITION | chord set: 3rd up · 5th up · 3rd+5th · octave up+down · 3rd below+above · 4th+7th · 3rd+5th+octave · 5th+9th+octave down |
| ② PITCH | the key, V/OCT (C at noon; each semitone moves the key) |
| ③ DENSITY | how many voices (1 → 3) and how loud |
| ④ SIZE | humanise: a little detune and delay per voice |
| ⑤ TEXTURE | stereo spread of the voices |
| ⑥ FREEZE | latch: the voices hold their notes while you carry on |
| ⑦ TRIG | next chord set |
| ⑧ STEREO | register: left third every voice an octave down, right third an octave up (same chord shape) |
| ⑨ FEEDBACK | canon: the voices (not you) echo every 340 ms, darker each time |
| ⑩ REVERB | glide: the voices slide into each new note, from a snap up to ~0.8 s |
| ⑪ Options | 1 major · 2 natural minor · 3 dorian · 4 major pentatonic |

#### 20 · PUTZ — spectral cleaner
*Forecast: hiss lifting, hum dispersing.*

A denoiser plus a 50/60 Hz hum comb. It learns the noise when you hold
FREEZE; until then it tracks the noise floor by itself (32 ms latency).
FFT 1024 (256–1024 with the FFT size setting, 3.8).

<img class="panel" src="img/typhoon/mode20.png" width="300" align="right" alt="Typhoon panel: mode 20 PUTZ">

| Control | |
|---|---|
| ① DENSITY | reduction, up to 30 dB |
| ② SIZE | smoothing (fewer "musical noise" twitters) |
| ③ TEXTURE | transient protection: lets attacks through |
| ④ POSITION | hum comb depth (8 harmonics) |
| ⑤ PITCH | hum fundamental, ± one octave around 50/60 Hz |
| ⑥ FREEZE (hold) | learn the noise: play only the noise while holding |
| ⑦ TRIG | listen to what's removed |
| ⑧ STEREO | noon = as before. Left: the lows are cleaned harder (rumble, room) and the highs left alone. Right: hiss only, the bass untouched |
| ⑨ FEEDBACK | de-crackle: clicks and vinyl crackle on the input are flattened, steady sound passes |
| ⑩ REVERB | de-reverb: every tail dies up to 3× faster, attacks and held notes stay (works with DENSITY at 0 too) |
| ⑪ Options | 1 = 50 Hz mains · 2 = 60 Hz mains |

#### 21 · ENSEMBLE — string-machine ensemble
*Forecast: a 1970s string machine drifts in from the coast.*

Three modulated delay taps per side, a slow and a fast LFO at 120° steps:
the Solina shimmer.

<img class="panel" src="img/typhoon/mode21.png" width="300" align="right" alt="Typhoon panel: mode 21 ENSEMBLE">

| Control | |
|---|---|
| ① POSITION | chorus → ensemble → vibrato |
| ② SIZE | base delay 5 → 15 ms |
| ③ DENSITY | modulation depth |
| ④ TEXTURE | clean hi-fi → dark bucket-brigade |
| ⑤ PITCH | LFO rate, V/OCT |
| ⑥ FREEZE | stop the LFOs where they are |
| ⑦ TRIG | reset the LFO phases |
| ⑧ STEREO | noon = as before. Left: only the slow LFO, deeper (a pure swirl). Right: only the fast one, deeper (pure shimmer, even in chorus) |
| ⑨ FEEDBACK | regeneration: the taps go round again (with DENSITY low: flanger jets) |
| ⑩ REVERB | reverb: the string machine's hall (the module's FDN reverb) |

#### 22 · BARBERPOLE — frequency-shift spirals
*Forecast: endlessly rising (or falling) and never arriving.*

A frequency shifter inside a feedback delay: every pass shifts again, so
echoes spiral up or down forever.

<img class="panel" src="img/typhoon/mode22.png" width="300" align="right" alt="Typhoon panel: mode 22 BARBERPOLE">

| Control | |
|---|---|
| ① POSITION | shift, bipolar: noon = none, up ↑ / down ↓, up to 2 kHz |
| ② SIZE | loop delay 1.5 ms → 0.5 s (up to 3 s at a lower buffer quality, 3.7) |
| ③ DENSITY | feedback |
| ④ TEXTURE | both sides the same way → left up, right down |
| ⑤ PITCH | multiplies the shift, V/OCT |
| ⑥ FREEZE | infinite spiral, input muted |
| ⑦ TRIG | flip direction |
| ⑧ STEREO | noon = as before. Left: every pass darker (the loop closes to 1.2 kHz). Right: every pass thinner (opens up to 1.5 kHz) |
| ⑨ FEEDBACK | wow: the loop delay wobbles, so every pass is a little more out of tune, like tape |
| ⑩ REVERB | dispersion: every pass drags the lows behind the highs, and the repeats melt into falling chirps, like a spring |

#### 23 · SPECDELAY — spectral delay
*Forecast: the lows arrive late, the highs early (or the other way round).*

Every frequency band has its own delay and feedback. FFT 512 (256–1024 with
the FFT size setting, 3.8: bigger means finer bands and longer steps).

<img class="panel" src="img/typhoon/mode23.png" width="300" align="right" alt="Typhoon panel: mode 23 SPECDELAY">

| Control | |
|---|---|
| ① POSITION | delay curve: lows last ← all together (noon) → highs last |
| ② SIZE | longest delay 5 ms → 0.5 s |
| ③ DENSITY | feedback |
| ④ TEXTURE | feedback tilt: dark repeats ← flat → thin bright repeats |
| ⑤ PITCH | spectral shift per repeat (±24 st): spirals; noon = none |
| ⑥ FREEZE | the spectral echoes hold forever |
| ⑦ TRIG | clear |
| ⑧ STEREO | noon = as before. Left: stripes (neighbouring bands answer early and late). Right: scatter (every band gets its own random delay) |
| ⑨ FEEDBACK | blur: every repeat smears further across frequency, and tones widen into hiss |
| ⑩ REVERB | diffusion: every repeat smears further in time, and the echoes melt into a wash |

#### 24 · CHOIR — formant choir
*Forecast: a choir on the horizon, singing whatever you hum.*

Four voices on a chord that follows V/OCT, each through vowel formants,
excited by your input and/or their own breath.

<img class="panel" src="img/typhoon/mode24.png" width="300" align="right" alt="Typhoon panel: mode 24 CHOIR">

| Control | |
|---|---|
| ① POSITION | vowel a → e → i → o → u |
| ② DENSITY | chord: unison · fifths · major · minor · sus4 · dom7 |
| ③ SIZE | voice spread: detune, width, vibrato |
| ④ TEXTURE | the input pitched through the voices → their own glottal breath |
| ⑤ PITCH | chord root, V/OCT (C3 at noon) |
| ⑥ FREEZE | sustain the chord on their own breath, no input needed |
| ⑦ TRIG | next inversion |
| ⑧ STEREO | noon = as before (altos). Left: bigger, darker singers (formants down). Right: smaller, brighter ones (formants up). The notes stay put |
| ⑨ FEEDBACK | resonance: your input rings on and sings its chord for seconds (TEXTURE left of full) |
| ⑩ REVERB | reverb: a choir needs a church (the module's FDN reverb) |

#### 25 · FOLDER — wavefolder + low-pass gate
*Forecast: west-coast weather, with a breeze off the Pacific.*

A sine wavefolder (oversampled, clean) into a vactrol low-pass gate. Your
input and an internal sine get folded together, and your input's level or
TRIG opens the gate.

<img class="panel" src="img/typhoon/mode25.png" width="300" align="right" alt="Typhoon panel: mode 25 FOLDER">

| Control | |
|---|---|
| ① POSITION | fold amount 1× → 10× |
| ② SIZE | symmetry: noon = odd harmonics, either side adds even ones |
| ③ DENSITY | gate type: low-pass → combo → VCA |
| ④ TEXTURE | vactrol decay 30 ms → 3 s |
| ⑤ PITCH | internal sine, V/OCT |
| ⑥ FREEZE | gate held open: a drone |
| ⑦ TRIG | ping the gate |
| ⑧ STEREO | noon = as before. Left: your input bends the sine's phase (option 1). Right: the sine ring-modulates your input (in option 2 too: a ring mod at PITCH) |
| ⑨ FEEDBACK | the folder feeds itself: growl, then chaos |
| ⑩ REVERB | reverb (the module's FDN reverb) |
| ⑪ Options | 1 input + sine · 2 input only (an effect) |

#### 26 · HALL — big reverb
*Forecast: cathedral conditions, visibility unlimited.*

A big eight-line reverb with diffusion inside every line: up to 30 seconds
of tail, separate from the output reverb.

<img class="panel" src="img/typhoon/mode26.png" width="300" align="right" alt="Typhoon panel: mode 26 HALL">

| Control | |
|---|---|
| ① POSITION | pre-delay 0 → 250 ms |
| ② SIZE | room size |
| ③ DENSITY | decay 0.4 → 30 s |
| ④ TEXTURE | damping: dark → bright |
| ⑤ PITCH | modulation: none ← gentle chorus (noon) → deep |
| ⑥ FREEZE | hold forever |
| ⑦ TRIG | clear the tail |
| ⑧ STEREO | noon = as before. Left: the lows die sooner (bass decay down to ×0.25: a clear, airy hall). Right: the lows ring on (×3: warm and heavy) |
| ⑨ FEEDBACK | pre-delay echoes: the pre-delay repeats (time = POSITION), and every echo blooms into the hall. Nothing at POSITION 0 |
| ⑩ REVERB | diffusion backs off: the smooth wash turns grainy and its echoes come apart, like a stone stairwell |

#### 27 · BLOOM — swells + reverse blooms
*Forecast: everything arrives backwards first, then gently.*

Each sound is heard reversed and swelling just before it arrives, then
arrives with its attack smoothed into a swell.

<img class="panel" src="img/typhoon/mode27.png" width="300" align="right" alt="Typhoon panel: mode 27 BLOOM">

| Control | |
|---|---|
| ① POSITION | swell time: attacks untouched → ~2 s swell on every note |
| ② SIZE | reverse window 50 → 280 ms (the delay is twice this; up to 1.68 s at a lower buffer quality, 3.7) |
| ③ DENSITY | reverse-bloom level |
| ④ TEXTURE | diffusion: clean reverse → hazy reverse reverb |
| ⑤ PITCH | transposition of the reverse bloom (±12 st) |
| ⑥ FREEZE | the last window keeps blooming, over and over |
| ⑦ TRIG | restart the reverse heads now |
| ⑧ STEREO | noon = as before. Left: more and more blooms fade out instead of in (reverse echoes that strike, then trail away). Right: more and more play forwards, swelling in, one window late |
| ⑨ FEEDBACK | regeneration (the module's feedback loop): every bloom blooms again (reversed twice = forwards again …) |
| ⑩ REVERB | haze tail: the diffusion chain becomes a loop, so the blooms ring on after the note (reverse reverb → note → reverb) |

#### 28 · GAMELAN — metallophones
*Forecast: bronze rain, beating gently in pairs.*

Seven keys in pelog or slendro. Every key has a twin tuned a few hertz away,
so they beat (ombak).

<img class="panel" src="img/typhoon/mode28.png" width="300" align="right" alt="Typhoon panel: mode 28 GAMELAN">

| Control | |
|---|---|
| ① POSITION | which key TRIG strikes |
| ② SIZE | ombak (beating) 0 → 10 Hz |
| ③ DENSITY | decay 0.3 → 20 s |
| ④ TEXTURE | saron bar → bonang kettle → gong (two octaves down) |
| ⑤ PITCH | root, V/OCT (C4 at noon) |
| ⑥ FREEZE | ring forever |
| ⑦ TRIG | strike |
| ⑧ STEREO | mallet: noon = as before. Left: softer, padded (upper partials fade, round and dull). Right: harder (upper partials up to ×5). Shapes TRIG strikes |
| ⑨ FEEDBACK | tutup: on each strike the other hand chokes the key struck before (up to 60 dB in 60 ms). 0 = everything rings |
| ⑩ REVERB | the pendopo: a reverb around the bank (the module's FDN reverb) |
| ⑪ Options | 1 pelog · 2 slendro |

#### 29 · MIRROR — spectral warping
*Forecast: the sky reflected in a puddle, upside down.*

Every partial is moved to a new frequency: stretched apart (bells),
mirrored around an anchor note (highs become lows), or folded under it.
FFT 1024 (256–2048 with the FFT size setting, 3.8).

<img class="panel" src="img/typhoon/mode29.png" width="300" align="right" alt="Typhoon panel: mode 29 MIRROR">

| Control | |
|---|---|
| ① POSITION | stretch → invert → fold |
| ② SIZE | amount: clean → full warp |
| ③ PITCH | the anchor / mirror note, V/OCT (C4 at noon) |
| ④ DENSITY | spectral blur |
| ⑤ TEXTURE | tilt, dark ↔ bright |
| ⑥ FREEZE | hold the spectrum (still warpable) |
| ⑦ TRIG | grab a new spectrum while frozen |
| ⑧ STEREO | frequency shift: noon none, left down, right up, up to ±1 kHz. Every partial moves by the same Hz, so harmonic sounds turn inharmonic; below 0 Hz they reflect back up |
| ⑨ FEEDBACK | warp feedback: the warped spectrum is warped again and again. Partials spiral (stretch glides them away from the anchor, STEREO slides them, fold piles them under it); with no warp it's a spectral sustain |
| ⑩ REVERB | reverb (the module's FDN reverb; MIRROR's output is mono, the space comes from here) |

#### 30 · POLISH — mastering chain
*Forecast: clear, bright and finished.*

Tilt → 3-band compressor → tape saturation → width with mono bass (the
limiter follows). Zero latency, flat when everything is off.

<img class="panel" src="img/typhoon/mode30.png" width="300" align="right" alt="Typhoon panel: mode 30 POLISH">

| Control | |
|---|---|
| ① DENSITY | compression amount (one macro for 3 bands, auto make-up) |
| ② TEXTURE | tape saturation: clean → warm and driven |
| ③ POSITION | tone: dark ← flat (noon) → bright |
| ④ SIZE | width: mono ← as is → 2× side; below 120 Hz always mono |
| ⑤ PITCH | output trim ±12 dB |
| ⑥ FREEZE | bypass, for A/B |
| ⑦ TRIG | auto-gain: match output loudness to input |
| ⑧ STEREO | where the compressor leans: noon = as before. Left: the lows get squeezed harder (tight, dense bass). Right: the highs do (tames harshness) |
| ⑨ FEEDBACK | parallel ("New York") compression: a smashed copy under the mix. Quiet detail comes up, peaks stay |
| ⑩ REVERB | air: new harmonics above 6 kHz, made from the highs (sparkle, not EQ) |



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
