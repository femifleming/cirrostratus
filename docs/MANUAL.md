# Cirrostratus

### *a thin, high sheet of cloud for the Monsoon*

**Version 1.3.0**

<!-- PANEL cover -->

> **cir·ro·stra·tus** /ˌsɪroʊˈstreɪtəs/ *n.* — a pale veil of ice crystals
> spread so high and thin across the sky that the sun shines straight
> through it, with a ring of light around it: a **halo**.

Cirrostratus is alternative firmware for **Monsoon**, Jak Plugg's 12HP
module built on Mutable Instruments' Clouds. Clouds made granular weather.
This firmware goes one layer higher, thinner and brighter:

- **30 modes** in two banks (Clouds had four kinds of sky)
- **seven controls of their own in every mode**: the four sliders plus the
  three small knobs **BAL**, **FBK** and **RVB**, which mean something
  different (and specific) in each mode. That's 90 extra controls, all with
  CV
- **48 kHz**, 32-bit float inside, 16-bit block-float buffers that keep quiet
  sounds clean (a −60 dB whisper is stored with 93 dB SNR), and a
  **buffer quality** setting that trades fidelity for up to **6× longer
  buffers** in every mode that records
- a clean **output section**: equal-power blend, hard stereo (whatever the
  mode makes), **bass kept mono below 60 Hz**, a transparent limiter and
  dithered output
- four **save slots**, **slot morphing**, per-mode **options** and
  **envelope-follower routing**

It is still your Monsoon: same panel, same jacks, same bootloader. Only the
weather changes.

**New in 1.3.0:** SPECTRAL has a second option, **SMEAR**, the spectral
smear from Sleepcousonique's Wingie mode (section 5, mode 2). RESONATOR,
PLATES and GAMELAN no longer retune on the tiny wobble every knob has, which
made RESONATOR glitch and stop answering the mode menu; and if a mode ever
runs out of time, it drops out for a moment instead of locking the panel.

---

## 0. Forecast in thirty seconds

<!-- PANEL quick -->

1. Hold **DISP**. The four top LEDs show a number.
2. Push **POSITION** up or down to pick a mode. **Tap WRITE** to flip
   between the green bank and the red bank.
3. Let go of **DISP**.
4. Play something in, and wiggle all four sliders, then the three small
   knobs **BAL**, **FBK** and **RVB**. They're the mode's own extra controls:
   BAL at noon and FBK/RVB at zero is the mode plain. **FREEZE** and **TRIG**
   do something different (and good) in every mode too.

The rest of this manual is the long-range forecast.

---

## 1. Installing (getting the cloud up there)

You get:

| File | What for |
|---|---|
| `cirrostratus.wav` | update through the audio input, no tools |
| `cirrostratus.bin` | the application, for SWD |
| `cirrostratus_bootloader_combo.bin` | bootloader + application, for a blank chip |

### 1a. By ear (recommended)

<!-- PANEL install -->

Your Monsoon already has the Clouds/Monsoon audio bootloader if it runs any
of the "build_4" Monsoon firmwares.

1. Rack **off**.
2. Cable from your computer's headphone/line out into **IN L**. No Bluetooth,
   no system sounds, close the chat apps.
3. **Hold FREEZE** and switch the rack on. The FREEZE LED blinks: the module
   is listening for weather data.
4. Computer volume about ¾, **GAIN** about noon.
5. Play `cirrostratus.wav` from the beginning (about 6¼ minutes; put the kettle on). It sounds like a
   modem singing. That's correct.
    - While it receives, the LEDs dance with the level.
    - **All four light green** each time a chunk is written to flash.
    - When the file ends, the module reboots into Cirrostratus and greets you
     with a little **rain** running across the LEDs.
6. **Four red blinking LEDs** mean reception failed (too quiet, too loud, a
   notification ping…). Press **FREEZE**, fix the level, and play the file
   again from the start. Nothing can break: the bootloader is never
   overwritten.

### 1b. With a programmer (SWD)

1. `brew install open-ocd`
2. Connect an ST-Link to the 10-pin SWD header on the Monsoon board, with
   the module powered.
3. In the firmware folder:
    - `make -f monsoon/makefile flash` writes the application and keeps the
     bootloader, settings and slots.
    - `make -f monsoon/bootloader/makefile && make -f monsoon/makefile flash_all`
     writes bootloader + application to a blank chip (settings reset).

### After landing

- Your **V/OCT calibration comes along** from the stock firmware
  automatically. Never calibrated? See 3.8.
- **Coming from Cirrostratus 1.0 or 1.1?** Settings, per-mode options,
  envelope routings and all four save slots carry over untouched. Mono bass
  starts on; buffer quality starts at 48 kHz 16-bit (exactly as before).
- Old Clouds save slots aren't compatible. Cirrostratus recognises and
  ignores them; it never plays garbage.
- **Back to stock:** flash the stock Monsoon firmware the same way. Clouds
  never mind.

---

## 2. The panel

| On the panel | We call it | |
|---|---|---|
| slider + CV | **POSITION** | also picks the mode in the mode menu |
| slider + CV | **DENSITY** | |
| slider + CV | **SIZE** | |
| slider + CV | **TEXTURE** | |
| TUNE knob + V/OCT | **PITCH** | ±4 octaves; it gently snaps to semitones; noon = no change |
| GAIN | input gain | analog, as before |
| WET + CV | **WET** | dry ↔ wet, equal power, on every mode |
| BAL + CV | **BAL** | the mode's own control: bipolar, plain at noon |
| FBK + CV | **FBK** | the mode's own control: plain at zero |
| RVB + CV | **RVB** | the mode's own control: plain at zero |
| FREEZE + gate | **FREEZE** | the gate flips whatever the button says |
| TRIG in | **TRIG** | a different job in every mode |
| DISP | **DISP** | display, mode menu |
| ✎ | **WRITE** | load, save, bank flip |

The slider LEDs still show incoming CV (that's hardware). The four LEDs
along the top belong to Cirrostratus.

**BAL, FBK and RVB are per-mode controls.** Each mode's table in section 5
says what they do. The house rule: **BAL is bipolar** (noon = the mode
plain, left and right of noon do two related things), **FBK and RVB start
at zero** (fully left = the mode plain). Their CV jacks work like the
sliders' (the jack adds to the knob). The names are a loose theme: FBK is
usually something that recirculates or intensifies, RVB something about
space, tail or wash, BAL a balance between two things.

---

## 3. Buttons, pages and little rituals

### 3.0 Sunrise

<!-- PANEL sunrise -->
Every time you power up, the module greets you with three seconds of
weather:

1. **Ice crystals**: dim sparkles twinkle across the four LEDs.
2. **Halo**: two amber rings pulse outward from the middle LEDs to the
   edges, and the FREEZE LED glows with each pulse.
3. **The sun breaks through**: amber sweeps to green, left to right.
4. **The forecast**: your current mode in binary (green or red bank),
   fading out.

Any button skips it (that press does nothing else).

### 3.1 Looking at the sky

<!-- PANEL look -->
- **Tap DISP**: the LEDs flip between the **input level** meter and the
  values of **WET / BAL / FBK / RVB**. While frozen, the meter shows the
  output.
- The FREEZE LED is the freeze state, always.

### 3.2 Choosing a mode

<!-- PANEL menu -->
1. **Hold DISP** for a second. The LEDs show the current mode in binary:
   **green = bank 1** (modes 1–15), **red = bank 2** (16–30).
2. **Move POSITION** to browse. The menu only starts following POSITION
   once you actually move it, so peeking never changes anything. The sound
   keeps its old POSITION while you browse.
3. **Tap WRITE** to flip banks.
4. **Tap FREEZE** to step through the mode's **options**. Each shows briefly
   as a single LED: green 1–4 for options 1–4, red 1–4 for options 5–8.
5. **Release DISP**. The old mode fades out and the new one fades in
   (about 20 ms, no clicks).

Reading the binary, left to right is 8 · 4 · 2 · 1:

| ●=on | # | | # | | # |
|---|---|---|---|---|---|
| ○○○● | 1 | ○●○● | 5 | ●○○● | 9 |
| ○○●○ | 2 | ○●●○ | 6 | ●○●○ | 10 |
| ○○●● | 3 | ○●●● | 7 | ●○●● | 11 |
| ○●○○ | 4 | ●○○○ | 8 | ●●○○ | 12 |
| ●●○● | 13 | ●●●○ | 14 | ●●●● | 15 |

Red bank: add 15 (red ○○○● is mode 16, red ●●●● is mode 30).

Every mode remembers its own option and envelope routing, even through
power cycles.

### 3.3 Four jars of sky (save slots)

<!-- PANEL slots -->
- **Hold WRITE**: the save page (slot LED blinks red). **DISP** picks a
  slot, **WRITE** saves. About a second of silence while flash is written.
- **Tap WRITE**: the load page (slot LED blinks green). **DISP** picks,
  **WRITE** loads. The slot remembers its mode and option, so loading takes
  you there.
- What goes in the jar depends on the mode: a recorded buffer, a frozen
  spectrum, a wavetable, a captured impulse response, a tape loop… Each
  mode below says what it keeps.

**Slot morph** (SPECTRAL and CAPTURE): on the load page, tap **FREEZE**
instead of WRITE. That slot becomes the *morph source*: its LED stays red,
it's read straight from flash, and POSITION blends toward it. Tap FREEZE on
it again to let it go.

### 3.4 The envelope page (weather that follows you)

<!-- PANEL envelope -->
Your input's loudness can push WET and the mode's three knobs up or down:
the wet that opens on loud notes, a BAL that leans further the harder you
play, an FBK that swells in the gaps, an RVB that ducks while you play.
Whatever BAL, FBK and RVB mean in the current mode, that's what moves.

- **Hold FREEZE, tap DISP**: LEDs = WET · BAL · FBK · RVB.
- **DISP** moves the cursor (blinking).
- **FREEZE** cycles its amount: off → +½ (dim green) → +1 (green) →
  −½ (dim red) → −1 (red) → off.
- **WRITE** saves and leaves (it also leaves by itself after 6 s).
- Each mode keeps its own routing. Routings made with 1.0 carry over to the
  same knobs.

### 3.5 FREEZE and TRIG
- FREEZE toggles on **release**, because the button is also a modifier.
- The FREEZE **gate** inverts the button: button off + gate high = frozen;
  button on + gate high = thawed.

### 3.6 Mono bass on/off

<!-- PANEL monobass -->
**Hold FREEZE, tap WRITE.** For 1.5 s the LEDs tell you the new state:

- the **two middle LEDs green**: bass gathered in the middle (mono bass
  **on**, the default);
- the **two outer LEDs amber**: bass out at the sides (mono bass **off**,
  full stereo all the way down).

The setting is remembered through power cycles. FREEZE doesn't toggle
freeze when you use it for this.

### 3.7 Buffer quality (longer buffers)

<!-- PANEL quality -->
One master setting for every mode that records into a buffer: **GRAINS,
STRETCH, CAPTURE, TAPE, BARBERPOLE** and **BLOOM**. Store the audio at a
lower sample rate and/or 8-bit and the same memory holds up to six times
as much sound.

**Hold WRITE, tap DISP** (within a second, before the save page opens).
**FREEZE** steps through the six settings, the LEDs show the one you're on:

| LEDs | Setting | Length | Sound |
|---|---|---|---|
| ● green | 48 kHz 16-bit | 1× | full quality (the default, exactly 1.0/1.1) |
| ●● green | 24 kHz 16-bit | 2× | top end stops at ~9.6 kHz |
| ●●● green | 16 kHz 16-bit | 3× | top end stops at ~6.4 kHz: warm, dusty |
| ● red | 48 kHz 8-bit | 2× | full range, a fine grain of noise under loud parts |
| ●● red | 24 kHz 8-bit | 4× | both |
| ●●● red | 16 kHz 8-bit | 6× | both: lo-fi, longest |

LED 4 blinks amber while the choice differs from the current one. **WRITE**
applies it and leaves; **DISP** (or 6 s) leaves without changing anything.
Applying restarts the running mode's buffer empty, so do it before you
record. Modes without a buffer don't notice.

How long that makes each buffer (seconds):

| Mode | 48k 16 | 24k 16 | 16k 16 | 48k 8 | 24k 8 | 16k 8 |
|---|---|---|---|---|---|---|
| GRAINS stereo | 0.62 | 1.24 | 1.86 | 1.22 | 2.44 | 3.66 |
| GRAINS mono | 1.24 | 2.48 | 3.72 | 2.44 | 4.88 | 7.32 |
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

### 3.8 Teaching it volts (V/OCT calibration)

<!-- PANEL calibrate -->
1. **Hold DISP** (the menu opens), then also **hold WRITE** for a second.
   Two LEDs blink.
2. Send **1 V** into V/OCT, tap **WRITE**. Four LEDs blink.
3. Send **3 V**, tap **WRITE**. Done. Red means the readings looked wrong:
   check the voltages and try again.

---

## 4. The output section, on every mode

<!-- PANEL output -->

```
IN ─┬──────────────────────────────────────────┐ dry
    │                                          │
    └─► MODE ─► DC block ─► WET ─┬─(+ reverb)─(+)─► MONO BASS ─► LIMITER ─► OUT
         ▲                       │   if the mode     < 60 Hz
         └── feedback loop ◄─────┘   uses it
             if the mode uses it
```

- **WET**: equal power, dry ↔ wet. With mono bass switched off, fully dry
  is bit-transparent.
- **Hard stereo.** No widener any more: left is left, right is right,
  exactly what the mode makes. Modes that are naturally wide (LEITNER,
  ENSEMBLE, HALL…) are wide on their own.
- **MONO BASS** (on by default): below 60 Hz the whole main out (dry and wet) is
  summed to mono, L+R halved to both sides; above it nothing changes. It's
  a 4th-order Linkwitz-Riley crossover, so the two halves add back up to a
  flat response. Your sub stays in the middle of the club, your vinyl cut
  stays cuttable. **Hold FREEZE, tap WRITE** to switch it (3.6).
- **LIMITER**: −1 dBFS peak limiter, transparent below −2 dBFS, then TPDF
  dither into the codec.

**What happened to FEEDBACK and REVERB?** They became engine services that
a mode can borrow for one of its knobs when that's truly the best control
for it. GRAINS borrows both (FBK = the classic Clouds feedback, still with
its built-in howl tamer: two adaptive notches that talk down a whistling
loop; RVB = the 8-line FDN reverb, 0.5 → 16 s). CONVOLVE and BLOOM borrow
the feedback loop for FBK. The dry voices and mono modes that have no space
of their own borrow the reverb for RVB: DIFFERENCE, CROSS, CAPTURE, GENDY,
PLATES, FALTUNG, LEITNER, TANTRI, ENSEMBLE, CHOIR, FOLDER, GAMELAN and
MIRROR. Everything else (73 of the 90 knob jobs) is the mode's own, and a
mode pays no CPU for a reverb it doesn't use.

---

## 5. Modes

**PITCH** = TUNE knob + V/OCT. WET always blends dry and wet. **BAL**,
**FBK** and **RVB** are each mode's own: BAL at noon and FBK/RVB at zero
give the mode plain, exactly as in 1.0. "Keeps" = what a save slot holds
for that mode.

<!-- MODES -->

---

## 6. Under the cloud (specs)

| | |
|---|---|
| Sample rate | 48 kHz (Clouds: 32 kHz) |
| Processing | 32-bit float; buffers 16-bit (or 8-bit) mantissa + exponent per 32 samples, stored at 48, 24 or 16 kHz (buffer quality, 3.7) |
| Interpolation | Hermite everywhere it reads between samples |
| FFT | ARM CMSIS-DSP real FFT, heavy work between interrupts |
| RAM per mode | about 130 KB (only the running mode holds memory) |
| CPU | every mode measured on an emulated STM32F405 at ≤ ~2400 instructions per stereo frame including the output section |
| Output | hard stereo, mono bass below 60 Hz (LR4, switchable), limiter, TPDF dither |
| Save slots | 4 × 128 KB of flash |
| Settings | calibration, mode, per-mode option + envelope routing, mono bass, buffer quality |

The source, the 224 tests and the CPU benchmark live in the repository:
`make -C host test`, `tools/bench/run.sh`. Cirrostratus has so far been
tested on the host and an emulated STM32F405, not yet on a physical
module.

---

## 7. Thanks and licences

- **Mutable Instruments / Émilie Gillet**: Clouds, stmlib, the audio
  bootloader and its QPSK encoder (MIT).
- **Tom Burns**: the Monsoon port of the Clouds firmware, whose pin mapping
  this firmware stands on (MIT).
- **Jak Plugg**: the Monsoon hardware.
- **ARM**: CMSIS-DSP FFT (BSD-style licence, see `stmlib/third_party`).
- The Chloaelis, Tantri, Oneirophone, Quell, Leitner and ConvoSolution
  ideas come from their own projects; the DSP here is written for the M4.

Cirrostratus is MIT-licensed. Look up. ☁
