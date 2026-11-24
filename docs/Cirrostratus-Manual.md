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
4. Computer volume about ¾, **IN LEVEL** about noon.
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
| big knob + V/OCT | **PITCH** | ±4 octaves; it gently snaps to semitones; noon = no change |
| IN LEVEL | input gain | analog, as before |
| WET + CV | **BLEND** | dry ↔ wet, equal power |
| STER + CV | **SPREAD** | mono ↔ as-is ↔ extra wide |
| FDB + CV | **FEEDBACK** | the mode's output back into itself |
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
  values of **WET / STER / FDB / RVB**. While frozen, the meter shows the
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

- **Hold FREEZE, tap DISP**: LEDs = WET · STER · FDB · RVB.
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
  talked down. Crank FDB and things *bloom* instead of shriek.
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

#### 15 · FALTUNG — the built-in space library
*Forecast: eight different rooms, all in the same cloud.*

Convolution with eight stereo spaces synthesized into the firmware (they
live in flash, so they cost no RAM): room, hall, plate, spring, cabinet,
wooden body, metal sheet, cathedral.

| Control | |
|---|---|
| POSITION | which space (8 zones, 30 ms crossfade) |
| SIZE | how much of the IR (shorter also saves CPU) |
| DENSITY | pre-delay 0 → 200 ms |
| TEXTURE | tilt on the wet sound, dark ↔ bright |
| PITCH | wet filter: high-cut ← noon (full range) → low-cut |
| FREEZE | input muted, the space loops back on itself forever |
| TRIG | next space |

### Bank 2 (red)

#### 16 · QUELL — resonance suppressor
*Forecast: harsh peaks clearing by afternoon.*

A spectral dynamic EQ: it finds narrow ringing peaks standing above their
neighbours and pushes them down, stereo-linked so the image doesn't move
(27 ms latency).

| Control | |
|---|---|
| POSITION | where it listens hardest, 100 Hz → 16 kHz |
| SIZE | selectivity: only sharp ringing → broad honky areas |
| DENSITY | depth, up to 24 dB of cut (bottom = transparent) |
| TEXTURE | speed: slow and smooth → fast |
| PITCH | output trim ±12 dB |
| FREEZE | hold the current cut curve as a static EQ |
| TRIG | delta: hear only what's being removed |

#### 17 · LEITNER — spatial path panner (after Leitner / Xenakis)
*Forecast: the sound takes flight and circles your head.*

The input becomes a source flying along a path around you, with ear-delay,
head shadow, distance, air, Doppler and four reflections from a virtual
room. Works on headphones and speakers.

| Control | |
|---|---|
| POSITION | where the path is centred: noon = in front, ¾ = left, ¼ = right, ends = behind |
| SIZE | radius / distance, 1 → 10 m |
| DENSITY | speed, 0.02 → 4 trips per second |
| TEXTURE | room: open field → big room with strong reflections |
| PITCH | Doppler: none ← physical (noon) → exaggerated ×3 |
| FREEZE | stop moving; the source stays where it is |
| TRIG | next path: circle → figure-8 → fly-by → spiral → random flight |

#### 18 · TANTRI — tabla + tanpura
*Forecast: monsoon season in Varanasi.*

A tanpura drone with a curved jawari bridge, and a tabla pair you play with
TRIG. Your input also sets the strings and drum heads ringing.

| Control | |
|---|---|
| POSITION | which stroke TRIG plays: Na · Tin · Tun · Te · Ge · Ke · Dha · Dhin |
| SIZE | bayan palm pressure: bends the bass drum up to +55 % (the "ghe" glide) |
| DENSITY | tanpura: silent → louder and faster plucking (Pa Sa Sa sa, 30 → 90 per minute) |
| TEXTURE | jawari: plain string → full buzzing bridge |
| PITCH | Sa, V/OCT (C3 at noon); dayan sits at upper Sa, bayan below |
| FREEZE | the drone sustains forever; plucking stops; the drums still play |
| TRIG | play the stroke |

#### 19 · HARMONIZER — scale harmonizer
*Forecast: a choir of you, always in key.*

Tracks your pitch and adds up to three voices exactly on scale degrees, even
if you're a little sharp.

| Control | |
|---|---|
| POSITION | chord set: 3rd up · 5th up · 3rd+5th · octave up+down · 3rd below+above · 4th+7th · 3rd+5th+octave · 5th+9th+octave down |
| PITCH | the key, V/OCT (C at noon; each semitone moves the key) |
| DENSITY | how many voices (1 → 3) and how loud |
| SIZE | humanise: a little detune and delay per voice |
| TEXTURE | stereo spread of the voices |
| FREEZE | latch: the voices hold their notes while you carry on |
| TRIG | next chord set |
| Options | 1 major · 2 natural minor · 3 dorian · 4 major pentatonic |

#### 20 · PUTZ — spectral cleaner
*Forecast: hiss lifting, hum dispersing.*

A denoiser plus a 50/60 Hz hum comb. It learns the noise when you hold
FREEZE; until then it tracks the noise floor by itself (32 ms latency).

| Control | |
|---|---|
| DENSITY | reduction, up to 30 dB |
| SIZE | smoothing (fewer "musical noise" twitters) |
| TEXTURE | transient protection: lets attacks through |
| POSITION | hum comb depth (8 harmonics) |
| PITCH | hum fundamental, ± one octave around 50/60 Hz |
| FREEZE (hold) | learn the noise: play only the noise while holding |
| TRIG | listen to what's removed |
| Options | 1 = 50 Hz mains · 2 = 60 Hz mains |

#### 21 · ENSEMBLE — string-machine ensemble
*Forecast: a 1970s string machine drifts in from the coast.*

Three modulated delay taps per side, a slow and a fast LFO at 120° steps:
the Solina shimmer.

| Control | |
|---|---|
| POSITION | chorus → ensemble → vibrato |
| SIZE | base delay 5 → 15 ms |
| DENSITY | modulation depth |
| TEXTURE | clean hi-fi → dark bucket-brigade |
| PITCH | LFO rate, V/OCT |
| FREEZE | stop the LFOs where they are |
| TRIG | reset the LFO phases |

#### 22 · BARBERPOLE — frequency-shift spirals
*Forecast: endlessly rising (or falling) and never arriving.*

A frequency shifter inside a feedback delay: every pass shifts again, so
echoes spiral up or down forever.

| Control | |
|---|---|
| POSITION | shift, bipolar: noon = none, up ↑ / down ↓, up to 2 kHz |
| SIZE | loop delay 1.5 ms → 0.5 s |
| DENSITY | feedback |
| TEXTURE | both sides the same way → left up, right down |
| PITCH | multiplies the shift, V/OCT |
| FREEZE | infinite spiral, input muted |
| TRIG | flip direction |

#### 23 · SPECDELAY — spectral delay
*Forecast: the lows arrive late, the highs early (or the other way round).*

Every frequency band has its own delay and feedback.

| Control | |
|---|---|
| POSITION | delay curve: lows last ← all together (noon) → highs last |
| SIZE | longest delay 5 ms → 0.5 s |
| DENSITY | feedback |
| TEXTURE | feedback tilt: dark repeats ← flat → thin bright repeats |
| PITCH | spectral shift per repeat (±24 st): spirals; noon = none |
| FREEZE | the spectral echoes hold forever |
| TRIG | clear |

#### 24 · CHOIR — formant choir
*Forecast: a choir on the horizon, singing whatever you hum.*

Four voices on a chord that follows V/OCT, each through vowel formants,
excited by your input and/or their own breath.

| Control | |
|---|---|
| POSITION | vowel a → e → i → o → u |
| DENSITY | chord: unison · fifths · major · minor · sus4 · dom7 |
| SIZE | voice spread: detune, width, vibrato |
| TEXTURE | the input pitched through the voices → their own glottal breath |
| PITCH | chord root, V/OCT (C3 at noon) |
| FREEZE | sustain the chord on their own breath, no input needed |
| TRIG | next inversion |

#### 25 · FOLDER — wavefolder + low-pass gate
*Forecast: west-coast weather, with a breeze off the Pacific.*

A sine wavefolder (oversampled, clean) into a vactrol low-pass gate. Your
input and an internal sine get folded together, and your input's level or
TRIG opens the gate.

| Control | |
|---|---|
| POSITION | fold amount 1× → 10× |
| SIZE | symmetry: noon = odd harmonics, either side adds even ones |
| DENSITY | gate type: low-pass → combo → VCA |
| TEXTURE | vactrol decay 30 ms → 3 s |
| PITCH | internal sine, V/OCT |
| FREEZE | gate held open: a drone |
| TRIG | ping the gate |
| Options | 1 input + sine · 2 input only (an effect) |

#### 26 · HALL — big reverb
*Forecast: cathedral conditions, visibility unlimited.*

A big eight-line reverb with diffusion inside every line: up to 30 seconds
of tail, separate from the output reverb.

| Control | |
|---|---|
| POSITION | pre-delay 0 → 250 ms |
| SIZE | room size |
| DENSITY | decay 0.4 → 30 s |
| TEXTURE | damping: dark → bright |
| PITCH | modulation: none ← gentle chorus (noon) → deep |
| FREEZE | hold forever |
| TRIG | clear the tail |

#### 27 · BLOOM — swells + reverse blooms
*Forecast: everything arrives backwards first, then gently.*

Each sound is heard reversed and swelling just before it arrives, then
arrives with its attack smoothed into a swell.

| Control | |
|---|---|
| POSITION | swell time: attacks untouched → ~2 s swell on every note |
| SIZE | reverse window 50 → 280 ms (the delay is twice this) |
| DENSITY | reverse-bloom level |
| TEXTURE | diffusion: clean reverse → hazy reverse reverb |
| PITCH | transposition of the reverse bloom (±12 st) |
| FREEZE | the last window keeps blooming, over and over |
| TRIG | restart the reverse heads now |

#### 28 · GAMELAN — metallophones
*Forecast: bronze rain, beating gently in pairs.*

Seven keys in pelog or slendro. Every key has a twin tuned a few hertz away,
so they beat (ombak).

| Control | |
|---|---|
| POSITION | which key TRIG strikes |
| SIZE | ombak (beating) 0 → 10 Hz |
| DENSITY | decay 0.3 → 20 s |
| TEXTURE | saron bar → bonang kettle → gong (two octaves down) |
| PITCH | root, V/OCT (C4 at noon) |
| FREEZE | ring forever |
| TRIG | strike |
| Options | 1 pelog · 2 slendro |

#### 29 · MIRROR — spectral warping
*Forecast: the sky reflected in a puddle, upside down.*

Every partial is moved to a new frequency: stretched apart (bells),
mirrored around an anchor note (highs become lows), or folded under it.

| Control | |
|---|---|
| POSITION | stretch → invert → fold |
| SIZE | amount: clean → full warp |
| PITCH | the anchor / mirror note, V/OCT (C4 at noon) |
| DENSITY | spectral blur |
| TEXTURE | tilt, dark ↔ bright |
| FREEZE | hold the spectrum (still warpable) |
| TRIG | grab a new spectrum while frozen |

#### 30 · POLISH — mastering chain
*Forecast: clear, bright and finished.*

Tilt → 3-band compressor → tape saturation → width with mono bass (the
limiter follows). Zero latency, flat when everything is off.

| Control | |
|---|---|
| DENSITY | compression amount (one macro for 3 bands, auto make-up) |
| TEXTURE | tape saturation: clean → warm and driven |
| POSITION | tone: dark ← flat (noon) → bright |
| SIZE | width: mono ← as is → 2× side; below 120 Hz always mono |
| PITCH | output trim ±12 dB |
| FREEZE | bypass, for A/B |
| TRIG | auto-gain: match output loudness to input |

### Bank 1 (green)

#### 1 · GRAINS — trajectory grains
*Forecast: scattered showers, falling in shapes.*

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
| Keeps | the buffer |

#### 2 · SPECTRAL — spectral freeze / resynthesis
*Forecast: a held moment, refracted into its colours.*

2048-point phase vocoder with peak-locked partials (64 ms latency).

| Control | |
|---|---|
| POSITION | partial thinning: all bins → only the strongest peaks. With a morph slot armed: blend from the current spectrum to the slot's |
| SIZE | harmonic snap: pulls partials onto the harmonic series of the PITCH root |
| DENSITY | blur (smears the spectrum over time) |
| TEXTURE | tilt, dark ↔ bright, flat at noon (±9 dB/oct) |
| PITCH | transposition without changing speed |
| FREEZE | hold the spectrum; it keeps resynthesising with a slight drift |
| TRIG | grab a new spectrum while frozen |
| Keeps / morph | the frozen spectrum / yes |

#### 3 · CONVOLVE — freeze captures the impulse response
*Forecast: the room you're standing in is whatever you just played.*

Your own audio becomes the space the sound plays in. The impulse response
(IR) can be up to 139 ms, and FEEDBACK makes it ring on much longer. The
mode starts with a small synthetic room so it sounds straight away.

| Control | |
|---|---|
| FREEZE (on) / TRIG | capture a new IR from the last 139 ms of input |
| POSITION | where in that capture the IR starts (bottom = newest) |
| SIZE | IR length, with fade-out |
| DENSITY | IR envelope: reverse swell ← as captured (noon) → faster decay |
| TEXTURE | IR brightness: low-pass below noon, treble boost above |
| PITCH | IR playback rate |
| Keeps | the capture and its settings |

#### 4 · RESONATOR — sympathetic strings with jawari
*Forecast: eight strings hum along, with a buzzing bridge.*

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
*Forecast: notes that only happen inside your head. Really.*

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
*Forecast: two weather fronts collide.*

Left input = modulator, right input = carrier (64 ms latency). With only IN L patched,
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
*Forecast: one second, smeared across an afternoon.*

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
| Keeps | the buffer |

#### 8 · SPREEK — voice
*Forecast: everything starts talking (and singing).*

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
*Forecast: catch a sound, play it like a synth.*

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
| Keeps / morph | the table / yes |

#### 10 · GENDY — dynamic stochastic synthesis (Xenakis)
*Forecast: Brownian drizzle, organised by Xenakis.*

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
| Keeps | the breakpoints |

#### 11 · SHIMMER — shimmer cascade
*Forecast: a reverb whose echoes keep climbing toward the sun.*

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
*Forecast: warm, wobbly, three heads at the ready.*

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
| Keeps | the tape |

#### 13 · PLATES — sympathetic plates (after the Ronin plates)
*Forecast: sheet metal (and glass, and wood) singing back.*

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
*Forecast: your Chloaelis, shrunk to fit the cloud.*

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
