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

### Bank 1 (green)

#### 1 · GRAINS — trajectory grains
Grains are read from a recorded buffer along a moving path, not at random
spots. Buffer: 0.62 s stereo or 1.24 s mono.

| Control | |
|---|---|
| POSITION | centre of the path (bottom = newest audio) |
| SIZE | path size and speed: bottom = a fixed point; up = wider and faster (0.05 → 2 Hz) |
| DENSITY | grain rate 1 → 150 grains/s; grain length follows (long and sparse → short and dense) |
| TEXTURE | window: percussive → smooth (Hann) → flat; top quarter adds pitch scatter |
| PITCH | grain transposition, quantised by the option |
| SPREAD | how far each grain follows the path's left/right movement |
| FREEZE | stop recording |
| TRIG | next path shape: circle → line → figure-8 → spiral → random walk |
| Options | 1 chromatic · 2 minor pentatonic · 3 octaves + fifths · 4 free; options 5–8 = the same in mono (double length) |
| Save | the buffer |

#### 2 · SPECTRAL — spectral freeze / resynthesis
2048-point phase vocoder with peak-locked partials.

| Control | |
|---|---|
| POSITION | partial thinning: all bins → only the strongest peaks. With a morph slot armed: blend from the current spectrum to the slot's |
| SIZE | harmonic snap: pulls partials onto the harmonic series of the PITCH root |
| DENSITY | blur (smears the spectrum over time) |
| TEXTURE | tilt, dark ↔ bright, flat at noon (±9 dB/oct) |
| PITCH | transposition without changing speed |
| FREEZE | hold the spectrum; it keeps resynthesising with a slight drift |
| TRIG | grab a new spectrum while frozen |
| Save / morph | the frozen spectrum / yes |

#### 3 · CONVOLVE — freeze captures the impulse response
Your own audio becomes the space the sound plays in. The impulse response
(IR) can be up to 165 ms, and FEEDBACK makes it ring on much longer. The
mode starts with a small synthetic room so it sounds straight away.

| Control | |
|---|---|
| FREEZE (on) / TRIG | capture a new IR from the last 165 ms of input |
| POSITION | where in that capture the IR starts (bottom = newest) |
| SIZE | IR length, with fade-out |
| DENSITY | IR envelope: reverse swell ← as captured (noon) → faster decay |
| TEXTURE | IR brightness: low-pass below noon, treble boost above |
| PITCH | IR playback rate |
| Save | the capture and its settings |

#### 4 · RESONATOR — sympathetic strings with jawari
Eight tuned strings, excited by the input, tuned to the cent.

| Control | |
|---|---|
| PITCH | root (C2 at noon), V/OCT |
| SIZE | tuning: unison/octaves → fifths → sitar sympathetic set → harmonic series → inharmonic |
| DENSITY | decay 0.2 → 20 s |
| POSITION | brightness and pickup point |
| TEXTURE | jawari: clean string → strong sitar-bridge buzz |
| FREEZE | infinite sustain, input cut |
| TRIG | pluck all strings |

#### 5 · DIFFERENCE — difference tones (after the Oneirophone)
The input is frequency-shifted into high pairs of tones, f and f + Δ. Your
ear makes the tone at Δ, a note that isn't in the signal.

| Control | |
|---|---|
| PITCH | the heard tone Δ (C3 at noon), V/OCT |
| POSITION | register of the pairs, 2 → 8 kHz |
| SIZE | input bandwidth: narrow (purest illusion) → whole input (rich) |
| DENSITY | 1 → 6 pairs: Δ × 1, 3/2, 5/4, 2, 7/4, 9/8 (a chord of phantom notes) |
| TEXTURE | purity: 0 = only in your ear; up = the difference tone becomes physically present (works through any speaker) |
| FREEZE | drone of pure sine pairs at the held level |

#### 6 · CROSS — left/right spectral morph
Left input = modulator, right input = carrier. With only IN L patched,
both sides are the same signal.

| Control | |
|---|---|
| POSITION | vocoder (L's envelope on R) → morph → L's magnitudes with R's phases |
| DENSITY | envelope resolution (coarse vocoder bands → fine detail) |
| TEXTURE | envelope speed (attack/release) |
| SIZE | formant shift of the modulator (×0.5 → ×2) |
| PITCH | carrier transposition |
| FREEZE | hold the modulator |
| TRIG | toggle holding the carrier |

#### 7 · STRETCH — extreme time-stretch
A phase vocoder with phase locking reads a recorded buffer slowly: 0.30 s
stereo or 0.89 s mono.

| Control | |
|---|---|
| SIZE | stretch: 1× → ~1000× at 0.9 → standing still from about 0.98 |
| POSITION | where the read zone sits in the buffer |
| DENSITY | smear |
| TEXTURE | coherent (phase vocoder) → Paulstretch (random phases) |
| PITCH | transposition without speed change |
| FREEZE | stop recording |
| TRIG | jump to the newest audio |
| Options | 1 stereo · 2 mono (longer) |
| Save | the buffer |

#### 8 · SPREEK — voice
LPC speech analysis, five-formant vowels and a singing carrier.

| Control | |
|---|---|
| POSITION | bottom: the input's own voice envelope (talkbox); then vowels a → e → i → o → u |
| SIZE | vocal-tract length (formants × 0.7 → 1.4) |
| DENSITY | source: the input itself → an internal singing carrier at PITCH, following the input's level |
| TEXTURE | smooth → breathy |
| PITCH | carrier pitch, V/OCT |
| FREEZE | hold the phoneme as a drone |
| TRIG | consonant burst |
| Options | 1 adult formants · 2 child formants |

#### 9 · CAPTURE — captured wavetable voice
FREEZE turns the last half second of input into a 16-frame wavetable. The
table is band-limited per octave, so it stays alias-free up high. Until the
first capture it plays a sine → saw table.

| Control | |
|---|---|
| FREEZE | capture (pitch-detected, one period per frame) |
| POSITION | scan through the frames (and on into the morph slot's frames) |
| PITCH | V/OCT, C3 at noon |
| DENSITY | unison 1 → 7 voices, detuned and spread |
| TEXTURE | brightness |
| SIZE | envelope: pluck → pad |
| TRIG | gate. Drones until the first gate arrives |
| Save / morph | the table / yes |

#### 10 · GENDY — dynamic stochastic synthesis (Xenakis)
Two independent GENDYN voices (left/right). The breakpoints of a waveform
take bounded random walks.

| Control | |
|---|---|
| PITCH | centre frequency, V/OCT |
| DENSITY | 3 → 40 breakpoints |
| TEXTURE | step size: static → restless |
| SIZE | barriers: stable pitch → wild |
| POSITION | random distribution: Cauchy → logistic → hyperbolic cosine → arcsine |
| FREEZE | freeze the walks (the waveform repeats exactly) |
| TRIG | reseed the breakpoints from the input's last period |
| Save | the breakpoints |

#### 11 · SHIMMER — shimmer cascade
A diffuse four-line delay network with pitch shifters inside, so every pass
climbs (or falls) again.

| Control | |
|---|---|
| POSITION | harmonic set: plain · octave · fifth · octave + fifth · Oneirophone ratios (7/4, 9/8, 3/2) · octave down |
| SIZE | room size |
| DENSITY | decay, up to endless |
| TEXTURE | damping |
| PITCH | extra transposition of the shift |
| FREEZE | hold forever, input muted |
| TRIG | clear the tail |

#### 12 · TAPE — multi-head tape echo
Variable-speed tape with an anti-aliased record path: slower tape gives
longer, darker echoes. Heads at 1×, 2× and 3× spacing (0.2 / 0.4 / 0.6 s at
1× in stereo, about twice that in mono).

| Control | |
|---|---|
| SIZE | tape speed ×0.25 → ×2 (×1 at noon) |
| PITCH | fine speed, V/OCT |
| POSITION | heads: 1 · 2 · 3 · 1+2 · 2+3 · 1+3 · 1+2+3 · all into the repeats · swell |
| DENSITY | feedback, up to gentle self-oscillation |
| TEXTURE | tape character: wow, flutter, saturation, head bump |
| FREEZE | loop hold: repeats sustain, no new input |
| TRIG | tape stop (spins down); next TRIG spins back up |
| Options | 1 stereo · 2 mono (twice as long) |
| Save | the tape |

#### 13 · PLATES — sympathetic plates (after the Ronin plates)
24 plate modes from real plate physics, read by two pickups and excited by
the input.

| Control | |
|---|---|
| PITCH | root, V/OCT |
| SIZE | shape: square plate → long bar |
| POSITION | strike/pickup position |
| DENSITY | decay 0.3 → 30 s |
| TEXTURE | material: metal → glass → wood |
| FREEZE | ring forever, input cut |
| TRIG | strike |

#### 14 · CHLOE — the Chloaelis strip
Your Chloaelis channel strip, one per side, hard left and right: multimode
filter → 12AX7 tube → delay. The Lorenz attractor pushes the left and right
cutoffs apart and lets the filter wander between low-pass and band-pass.

| Control | |
|---|---|
| POSITION | filter cutoff 30 Hz → 16 kHz |
| TEXTURE | tube drive (bottom = tube out) and filter resonance |
| SIZE | delay time 20 ms → 1.6 s (tape: up to 0.45 s). String engine: interval of the right string (unison, 5th, octave, 12th, 2 octaves, 4th) |
| DENSITY | regeneration; past about 85 % it runs away and sings |
| PITCH | transposes the cutoff (play the resonant filter). String engine: the note |
| FREEZE | delay freeze: PT2399 RAM held bit for bit, BBD buckets, tape loop, string |
| TRIG | random freeze (0.2–1.5 s). String engine: pluck |
| Options | 1 PT2399 · 2 BBD · 3 tape · 4 Karplus-Strong string, all filter → tube → delay; 5–8 the same as delay → tube → filter |

On the module: the tube runs 2× oversampled (4× in the plug-in), one
delay line per side (time changes glide), the tape is 0.45 s per side with
fixed wear, and there are no poly taps or flock.


