# Monsoon HiFi

**Alternative firmware for the Monsoon** (Jak Plugg's 12HP Clouds variant).
It replaces the four Clouds playback modes with **30 modes in two banks**,
running in a new 48 kHz floating-point engine with a new output section.

---

## 1. Installing

The firmware comes as two files:

| File | Use it for |
|---|---|
| `monsoon_hifi.wav` | Updating through the module's audio input (no tools needed) |
| `monsoon.bin` / `monsoon_bootloader_combo.bin` | Flashing over SWD with an ST-Link |

### 1a. Audio update (recommended)

This works if your Monsoon already runs a Clouds/Monsoon firmware with the
standard audio bootloader. The "build_4" Monsoon hex files include it.

1. Power the rack **off**.
2. Patch your computer's headphone/line output into the Monsoon's **IN L**.
   Use a cable, not Bluetooth. Close every other app that might make a sound.
3. Hold **FREEZE** and power the rack on. The FREEZE LED blinks: the
   bootloader is listening.
4. Set the computer's volume to about 3/4 and **IN LEVEL** to about noon.
5. Play `monsoon_hifi.wav` from the start (it is about 50 s).
   - While it receives, the LEDs show the input level.
   - All four LEDs turn green while each block is written.
   - When the file ends, the module restarts into Monsoon HiFi. A short
     "rain" animation runs across the LEDs.
6. If the four LEDs **blink red**, reception failed (level too low/high, a
   system sound, a gap). Press **FREEZE** to reset the receiver, adjust the
   level, and play the file again from the start.

Nothing is lost by a failed update. The bootloader stays intact and you can
retry as often as you like.

### 1b. SWD (ST-Link)

Use this for a module with no bootloader, or if you prefer a programmer.

1. Install OpenOCD: `brew install open-ocd`.
2. Connect an ST-Link V2 to the 10-pin SWD/JTAG header on the Monsoon's
   board (SWDIO, SWCLK, GND, 3V3 sense). The module must be powered.
3. From the firmware folder:
   - Application only (keeps the bootloader, settings and save slots):
     `make -f monsoon/makefile flash`
   - Blank chip (bootloader + application, resets settings):
     `make -f monsoon/bootloader/makefile && make -f monsoon/makefile flash_all`

### After installing

- **V/OCT calibration is carried over** from the stock firmware on first
  boot. If your module was never calibrated, or the import fails, run the
  calibration (section 3.6).
- Save slots made with the stock firmware are not compatible. They are
  ignored, never misread. Re-save your sounds.
- **Going back to stock:** flash the stock Monsoon firmware the same way
  (its WAV through the bootloader, or its hex over SWD).

### Building it yourself

`make -f monsoon/makefile` builds the firmware, `make -f monsoon/makefile wav`
builds the update file, and `make -C host test` runs the test suite on a
computer (it needs `arm-none-eabi-gcc`).

---

## 2. Panel

| Control | Name in this manual | Notes |
|---|---|---|
| Slider + CV | **POSITION** | also picks the mode in the mode menu |
| Slider + CV | **DENSITY** | |
| Slider + CV | **SIZE** | |
| Slider + CV | **TEXTURE** | |
| Big knob + V/OCT | **TUNE / PITCH** | ±4 octaves, gentle semitone "magnets", C3 at noon |
| IN LEVEL | input gain | analog, as on stock firmware |
| WET + CV | **BLEND** | equal-power dry/wet |
| STER + CV | **SPREAD** | stereo width of the wet signal |
| FDB + CV | **FEEDBACK** | wet signal back into the mode |
| RVB + CV | **REVERB** | amount and length of the output reverb |
| FREEZE button + gate | **FREEZE** | the gate inverts the button state |
| TRIG input | **TRIG** | each mode gives it its own job |
| DISP button | **DISP** | display / mode menu |
| Pencil button | **WRITE** | load / save |

The four slider LEDs show incoming CV (hardware, as before). The four
**status LEDs** in the top row are driven by the firmware.

---

## 3. Buttons and pages

### 3.1 Normal display

- Tap **DISP** to switch the LEDs between the **input level meter** and the
  **WET / STER / FDB / RVB values**. The output level is shown while frozen.
- The FREEZE LED shows the freeze state.

### 3.2 Choosing a mode

1. **Hold DISP** for 1 s. The LEDs show the current mode as a binary number,
   **green for bank 1** (modes 1–15) and **red for bank 2** (16–30).
2. **Move POSITION** to step through the bank. The sound keeps its old
   POSITION value while you do this. The menu only starts following the
   slider once you actually move it.
3. **Tap WRITE** to switch to the other bank.
4. **Tap FREEZE** to step through the mode's **options** (if it has any).
   The option shows for a moment as one LED: LED 1–4 green = options 1–4,
   red = options 5–8.
5. **Release DISP** to switch. The modes crossfade (about 20 ms).

Binary on the LEDs, left to right = 8, 4, 2, 1:

| LEDs | # | LEDs | # | LEDs | # |
|---|---|---|---|---|---|
| ○○○● | 1 | ○●○● | 5 | ●○○● | 9 |
| ○○●○ | 2 | ○●●○ | 6 | ●○●○ | 10 |
| ○○●● | 3 | ○●●● | 7 | ●○●● | 11 |
| ○●○○ | 4 | ●○○○ | 8 | ●●○○ | 12 |
| ●●○● | 13 | ●●●○ | 14 | ●●●● | 15 |

Every mode remembers its own option and envelope routing.

### 3.3 Save slots (four, kept when powered off)

- **Hold WRITE** (1 s): save page, slot LED blinks red. **DISP** picks the
  slot, **WRITE** saves. Saving takes about a second; the module is silent
  meanwhile.
- **Tap WRITE**: load page, slot LED blinks green. **DISP** picks, **WRITE**
  loads. A slot remembers its mode and option, so loading switches to it.
- Each mode decides what a slot holds: its audio buffer, its frozen
  spectrum, its wavetable, its captured impulse response, and so on (listed
  with each mode). Modes with nothing worth saving ignore the save.

**Slot morph** (SPECTRAL and CAPTURE): on the load page, tap **FREEZE**
instead of WRITE. That slot becomes the *morph source* (its LED stays red)
and the mode can blend toward it with POSITION, reading it straight from
flash. Tap FREEZE on it again to clear it.

### 3.4 Envelope page

The input level can push any of the four output knobs up or down: ducking
the reverb under playing, opening WET with loud notes, and so on.

- **Hold FREEZE and tap DISP** to open it. The four LEDs are WET, STER, FDB, RVB.
- **DISP** moves the cursor (blinking LED).
- **FREEZE** cycles the cursor's amount: off → +½ (dim green) → +1 (green)
  → −½ (dim red) → −1 (red) → off.
- **WRITE** saves and leaves (it also leaves by itself after 6 s).

### 3.5 Freeze and TRIG

FREEZE is the button toggle, and the FREEZE gate inverts it: with the button
off, a high gate freezes; with the button on, a high gate unfreezes. FREEZE
toggles on *release*, because the button is also a modifier for the
envelope page.

### 3.6 V/OCT calibration

1. **Hold DISP** (the mode menu opens), then also **hold WRITE** for 1 s.
   Two LEDs blink.
2. Send **1 V** to V/OCT and tap **WRITE**. Four LEDs blink.
3. Send **3 V** and tap **WRITE**. Done. Red LEDs mean the readings were off;
   check the voltages and repeat.

---

## 4. The output section (all modes)

```
IN ─┬───────────────────────────────────────────────┐
    │                                               │ dry
    └─► (+) ─► MODE ─► DC block ─► SPREAD ─► BLEND ─┴─► REVERB ─► LIMITER ─► OUT
         ▲                │
         └── FEEDBACK ◄───┘  (25 Hz high-pass, resonance tamer, soft clip)
```

- **FEEDBACK** sends the mode's output back to its input. A **resonance
  tamer** sits in the loop: two adaptive notches that find the strongest
  narrow peaks. They cut harder the more the loop's energy collects in
  those peaks. Broad sounds pass untouched, and a whistling loop gets reined
  in, so high feedback swells instead of screaming.
- **SPREAD**: 0 = mono, noon = as the mode made it, full = wide. The
  widening is mid/side, plus an allpass decorrelation that leaves the mono
  sum unchanged, plus a hint of Haas delay at the very top.
- **BLEND**: equal-power dry/wet. Fully dry is bit-transparent.
- **REVERB**: an 8-line modulated feedback-delay network. The knob sets the
  send and the decay together (0.5 s → about 16 s).
- **Limiter**: −1 dBFS peak limiter, transparent below −2 dBFS, then TPDF
  dither to the codec.

---

## 5. The engine, in brief

- 48 kHz (Clouds: 32 kHz), 32-bit float processing throughout.
- Audio buffers store 16-bit samples with a shared exponent per 32 samples.
  A sound at −60 dBFS keeps 93 dB SNR, where plain 16-bit would keep about 38 dB.
- Hermite interpolation for every variable-rate read.
- FFT modes use ARM's CMSIS-DSP FFT; their heavy work runs in the background
  between audio interrupts, as in Clouds' spectral mode.
- RAM is about 130 KB per mode. Only the running mode holds memory, which is
  why buffers are short at full quality and every buffer mode offers a mono
  option with twice the length.

---

## 6. Modes

Each mode lists what the controls do. **PITCH** means TUNE + V/OCT. The
four output knobs always work as in section 4.

<!-- MODES -->
