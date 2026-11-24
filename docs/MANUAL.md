# Cirrostratus

### *a thin, high sheet of cloud for the Monsoon*

> **cir·ro·stra·tus** /ˌsɪroʊˈstreɪtəs/ *n.* — a pale veil of ice crystals
> spread so high and thin across the sky that the sun shines straight
> through it, with a ring of light around it: a **halo**.

Cirrostratus is alternative firmware for **Monsoon**, Jak Plugg's 12HP
module built on Mutable Instruments' Clouds. Clouds made granular weather.
This firmware goes one layer higher, thinner and brighter:

- **30 modes** in two banks (Clouds had four kinds of sky)
- **48 kHz**, 32-bit float inside, 16-bit block-float buffers that keep quiet
  sounds clean (a −60 dB whisper is stored with 93 dB SNR)
- a new **output section** on every mode: equal-power blend, a widener that
  stays mono-compatible, feedback that **tames its own howl**, a lush 8-line
  reverb, a transparent limiter and dithered output
- four **save slots**, **slot morphing**, per-mode **options** and
  **envelope-follower routing**

It is still your Monsoon: same panel, same jacks, same bootloader. Only the
weather changes.

---

## 0. Forecast in thirty seconds

1. Hold **DISP**. The four top LEDs show a number.
2. Push **POSITION** up or down to pick a mode. **Tap WRITE** to flip
   between the green bank and the red bank.
3. Let go of **DISP**.
4. Play something in, and wiggle all four sliders. **FREEZE** and **TRIG** do
   something different (and good) in every mode.

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

Your Monsoon already has the Clouds/Monsoon audio bootloader if it runs any
of the "build_4" Monsoon firmwares.

1. Rack **off**.
2. Cable from your computer's headphone/line out into **IN L**. No Bluetooth,
   no system sounds, close the chat apps.
3. **Hold FREEZE** and switch the rack on. The FREEZE LED blinks: the module
   is listening for weather data.
4. Computer volume about ¾, **GAIN** about noon.
5. Play `cirrostratus.wav` from the beginning (about 53 s). It sounds like a
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
  automatically. Never calibrated? See 3.6.
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
| WET + CV | **BLEND** | dry ↔ wet, equal power |
| BAL + CV | **SPREAD** | mono ↔ as-is ↔ extra wide |
| FBK + CV | **FEEDBACK** | the mode's output back into itself |
| RVB + CV | **REVERB** | how much and how long |
| FREEZE + gate | **FREEZE** | the gate flips whatever the button says |
| TRIG in | **TRIG** | a different job in every mode |
| DISP | **DISP** | display, mode menu |
| ✎ | **WRITE** | load, save, bank flip |

The slider LEDs still show incoming CV (that's hardware). The four LEDs
along the top belong to Cirrostratus.

---

## 3. Buttons, pages and little rituals

### 3.0 Sunrise
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
- **Tap DISP**: the LEDs flip between the **input level** meter and the
  values of **WET / BAL / FBK / RVB**. While frozen, the meter shows the
  output.
- The FREEZE LED is the freeze state, always.

### 3.2 Choosing a mode
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
Your input's loudness can push any of the four output knobs up or down: the
reverb that ducks while you play, the wet that opens on loud notes, the
feedback that swells in the gaps.

- **Hold FREEZE, tap DISP**: LEDs = WET · BAL · FBK · RVB.
- **DISP** moves the cursor (blinking).
- **FREEZE** cycles its amount: off → +½ (dim green) → +1 (green) →
  −½ (dim red) → −1 (red) → off.
- **WRITE** saves and leaves (it also leaves by itself after 6 s).

### 3.5 FREEZE and TRIG
- FREEZE toggles on **release**, because the button is also a modifier.
- The FREEZE **gate** inverts the button: button off + gate high = frozen;
  button on + gate high = thawed.

### 3.6 Teaching it volts (V/OCT calibration)
1. **Hold DISP** (the menu opens), then also **hold WRITE** for a second.
   Two LEDs blink.
2. Send **1 V** into V/OCT, tap **WRITE**. Four LEDs blink.
3. Send **3 V**, tap **WRITE**. Done. Red means the readings looked wrong:
   check the voltages and try again.

---

## 4. The output section, on every mode

```
IN ─┬───────────────────────────────────────────────┐
    │                                               │ dry
    └─► (+) ─► MODE ─► DC block ─► SPREAD ─► BLEND ─┴─► REVERB ─► LIMITER ─► OUT
         ▲                │
         └── FEEDBACK ◄───┘   25 Hz high-pass · resonance tamer · soft clip
```

- **FEEDBACK with manners.** Inside the loop sits a little Quell: two
  adaptive notches hunt for the loudest narrow peaks and cut harder the more
  of the loop's energy piles into them. Broad sound passes; a whistle gets
  talked down. Crank FBK and things *bloom* instead of shriek.
- **SPREAD**: 0 = mono, noon = untouched, full = wide. The widener is
  mid/side plus an allpass decorrelator that cancels in mono, so your mix
  survives a mono club PA, plus a pinch of Haas at the very top.
- **BLEND**: equal power. Fully dry is bit-transparent.
- **REVERB**: an 8-line modulated feedback delay network. One knob for send
  and length together (0.5 s → about 16 s). It's skipped entirely at zero,
  which saves CPU for the modes.
- **LIMITER**: −1 dBFS peak limiter, transparent below −2 dBFS, then TPDF
  dither into the codec.

---

## 5. Modes

**PITCH** = TUNE knob + V/OCT. The four output knobs always do what
section 4 says. "Keeps" = what a save slot holds for that mode.

<!-- MODES -->

---

## 6. Under the cloud (specs)

| | |
|---|---|
| Sample rate | 48 kHz (Clouds: 32 kHz) |
| Processing | 32-bit float; buffers 16-bit mantissa + exponent per 32 samples |
| Interpolation | Hermite everywhere it reads between samples |
| FFT | ARM CMSIS-DSP real FFT, heavy work between interrupts |
| RAM per mode | about 130 KB (only the running mode holds memory) |
| CPU | every mode measured on an emulated STM32F405 at ≤ ~2400 instructions per stereo frame including the output section |
| Save slots | 4 × 128 KB of flash |
| Settings | calibration, mode, per-mode option + envelope routing |

The source, the 180-odd tests and the CPU benchmark live in the repository:
`make -C host test`, `tools/bench/run.sh`.

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
