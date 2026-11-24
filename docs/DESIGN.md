# Monsoon HiFi — design spec

Source of truth for the modes. The manual (MANUAL.md) is written from this.

## Platform facts

- STM32F405RGT6, 168 MHz Cortex-M4F, WM8731 codec. Engine runs at **48 kHz**,
  blocks of 32 frames → ~3500 CPU cycles per stereo frame in total.
  Budget per mode (Process + Prepare averaged): **≤ 2000 cycles/frame**.
  The output section (DC block, blend, mono bass, limiter) takes ~120;
  the feedback-loop and FDN services add ~110 / ~435 when a mode uses them.
- RAM for a mode: `Arena` with two regions, ~96 KB main SRAM + ~33 KB CCM
  (≈ 129 KB total). One allocation must fit inside one region; allocate the
  biggest things first. `SampleBuffer` can span both regions.
- Save slot: 128 KB of flash, minus a 12-byte engine header. A mode's
  persistent data must be ≤ 120 KB.
- No libm per sample (expf/powf/tanf only at block rate). Use `FastSin`,
  `FastCos`, `FastAtan2Cycles`, `sqrtf` (single VSQRT instruction).
- Code builds with arm-none-eabi-g++ 8.5, `-std=gnu++14 -Wall -Werror`.
  Check with `tools/arm_check.sh`. Host tests: `make -C host test`.

## Controls (`monsoon::Controls`)

| Field | Panel | Range |
|---|---|---|
| position | POSITION slider + CV | 0..1 |
| density | DENSITY slider + CV | 0..1 |
| size | SIZE slider + CV | 0..1 |
| texture | TEXTURE slider + CV | 0..1 |
| pitch | TUNE knob + V/OCT | semitones, −48..+48, 0 at noon |
| blend | WET knob + CV (engine dry/wet) | 0..1 |
| bal / fbk / rvb | BAL / FBK / RVB knobs + CV: **per-mode controls** | 0..1 |
| freeze | FREEZE button toggle or FREEZE gate | bool |

Panel buttons: DISP (top left), WRITE (pencil, top right), FREEZE (big).
In the text below "PITCH" means the TUNE knob + V/OCT input.

Since 1.1, BAL, FBK and RVB are three extra controls of every mode
(documented in each mode's header and in docs/modes_*.md). Conventions:
BAL is bipolar (`KnobBipolar()`, small dead zone) and gives the v1.0 sound
at noon; FBK and RVB give the v1.0 sound at 0. CV can change them every
block: everything is smoothed and every internal feedback stays below
unity (or saturates) except where FREEZE defines an infinite hold.
| trigger | TRIG input rising edge this block | bool |
| gate | TRIG input level | bool |
| option | chosen in the mode menu | 0..num_options−1 |

Musical pitch reference: `pitch = 0` means C3 = 130.81 Hz unless a mode says
otherwise. `root_hz = 130.81 * 2^(pitch/12)`.

Options shown on the 4 LEDs: option i lights LED (i % 4); green for i < 4,
red for i ≥ 4. For buffer modes with a mono choice: options 0–3 stereo,
4–7 the same in mono (twice the length).

## Shared output section (engine, all modes)

1. Input (+ the feedback service's signal, if the mode uses it) → mode.
2. Wet → DC blocker → equal-power blend. The dry part goes to the output
   bus; the wet part goes to a wet bus.
3. FDN reverb service (if the mode maps a knob to it): hears dry + wet, its
   tail joins the wet bus.
4. Dry + wet summed, then mono bass on the whole main out: LR4 crossover at 60 Hz done in mid/side
   (mid → LR4 allpass, side → LR4 high-pass), i.e. HP4(L) + LP4((L+R)/2).
   On by default; FREEZE hold + WRITE toggles it (settings byte
   `mono_bass_off`), 20 ms crossfade, free when off (then fully dry is
   bit-transparent).
5. → limiter (−1 dBFS, transparent below −2 dBFS) → TPDF dither →
   codec. The output is hard stereo: no widener.

Engine services a mode may map a knob to (`Mode::feedback_amount(c)`,
`Mode::reverb_amount(c)`, pure functions of the controls; 0 = unused and
skipped):
- Feedback loop: wet → 25 Hz HP → ResonanceTamer (two adaptive notches;
  depth rises with how much of the loop's energy sits in narrow peaks) →
  soft clip → mode input. Gain curve: smoothstep(amount). FBK in GRAINS,
  CONVOLVE and BLOOM.
- FDN: 8 lines, 16-bit (≈ 15 k samples, 32 KB of CCM), modulated, T60
  0.5–16.5 s, amount sets send and decay. RVB in GRAINS, DIFFERENCE, CROSS,
  CAPTURE, GENDY, PLATES, FALTUNG, LEITNER, TANTRI, ENSEMBLE, CHOIR,
  FOLDER, GAMELAN, MIRROR (dry voices and mono modes with no space of their
  own; several were chosen over a mode-specific idea to fit the flash).

Envelope follower: input level can push WET, BAL, FBK or RVB up or down
(per-mode routing, set in the ENV page).

## Per-mode knobs (1.1)

Full wording in docs/modes_*.md and each mode's header. BAL noon / FBK 0 /
RVB 0 = v1.0 exactly (checked against v1.0 fingerprints in host/tests_v11.cc).

| Mode | BAL (left / right of noon) | FBK | RVB |
|---|---|---|---|
| 1 GRAINS | noon = plain. Left: spray (grains scatter around the path, up to half the buffer away). Right: more and more grains play backwards, all of them at full | feedback: the cloud back into itself, Clouds style, with the howl tamer on duty | the 8-line FDN reverb: send and length together, 0.5 → 16 s |
| 2 SPECTRAL | Turning it shifts every partial by the same number of Hz (not a ratio), so harmonic sounds go inharmonic. Left shifts down, right shifts up, exponential: a few Hz near noon, ±1 kHz at the ends | regeneration | phase diffusion: random phases blend in, coherent → a whispered wash in the same colours |
| 3 CONVOLVE | Left: only the attacks of the input reach the IR. Right: only the body (attacks ducked, so hits swell instead of crack). It shapes the input before everything, so captures hear it too | feedback (the module's feedback loop, with its howl tamer): the IR rings on much longer | IR smear |
| 4 RESONATOR | Left: detune, the strings spread up to ±25 cents (beating, a chorus of strings). Right: stretch, intervals widen up to +10 % (an octave ~+120 cents: piano-like, then bell-like) | sympathetic coupling | body |
| 5 DIFFERENCE | Left: glide, the heard note slides to each new pitch (20 ms → 2 s). Right: dichotic, f to the left ear and f + Δ to the right; on headphones the difference tone gives way to binaural beats | spiral | reverb (the module's FDN reverb; both tones reverberate, so the phantom note survives) |
| 6 CROSS | noon = as before (carrier = right input). Left: the carrier fades to white noise (whispered; consonants come through). Right: it fades to an internal buzz played by PITCH (C3 at noon, V/OCT), a classic vocoder from one input; PITCH then stops transposing R | formant emphasis | reverb (the module's FDN reverb; CROSS is mono, this gives it a stereo room) |
| 7 STRETCH | Left: the read slows, stands still at 9 o'clock, and runs backwards at full left. Right: scatter, the read window jumps to random spots (up to half the buffer away, ~10 jumps/s) | re-record | octave halo: every partial also sounds an octave up, a shimmer over the stretch |
| 8 SPREEK | Left: glide (PITCH slides; an octave takes up to ~0.6 s). Right: vibrato (5.3 Hz, up to ±1 semitone) | growl | choir |
| 9 CAPTURE | Left: bend — phase distortion, the first half of each cycle read up to 6× faster (CZ style). Right: sync formant — the table read up to 6× per cycle under a soft window, a resonant peak sweeping up. Stays alias-free | self-FM | reverb (the module's reverb, for a voice with no space of its own) |
| 10 GENDY | noon = both voices around PITCH. The right voice moves in whole semitones: left down to an octave below, right up to an octave above (fifths, fourths, octaves) | cross-coupling | reverb (the module's reverb) |
| 11 SHIMMER | Left: less of each pass is shifted (fully left = a plain reverb in any set). Right: mirror — the fourth line falls by the inverse of the set's interval, so the cascade climbs and falls | tank modulation | pre-delay 0 → 250 ms before the network (glides when turned) |
| 12 TAPE | Left: darker repeats (loop low-pass 7 kHz → 500 Hz). Right: thinner repeats (loop high-pass up to 2 kHz), the dub telephone fade | ping-pong | diffusion in the loop |
| 13 PLATES | Left: the modes are pulled onto whole harmonics (fully left = a tuned, pitched plate). Right: the series stretches — more clang, bell / tam-tam | tension: hard hits ring sharp (up to ~3 semitones) and glide back down as they decay | reverb (the module's reverb, so a struck plate gets a room) |
| 14 CHLOE | the Lorenz attractor: noon = as before. Left: calmer and shallower until it stops (both cutoffs together, the filter halfway LP/BP, ¼ speed). Right: up to 8× faster and ±3 octaves, the filter wandering LP → BP → HP | master feedback, crossed like the plug-in: left out → right in, right out → left in | diffusion in the regen loop |
| 15 FALTUNG | early ↔ late: noon = the IR as it is. Left: the tail fades away (close, dry). Right: the early part fades and the tail swells (far away). Same loudness | feedback through the IR | a long smooth FDN tail (0.5 → 16 s) behind the space (the IRs are at most 170 ms) |
| 16 QUELL | noon = stereo-linked. Left: mid only (the sides pass untouched). Right: sides only | hard threshold | halo: what's cut rings on as a soft spectral tail, 0.3 → 5 s (also heard in delta) |
| 17 LEITNER | noon = a smooth glide. Left (Leitner): the path becomes 8 loudspeaker stations; the source rests at each and hops to the next. Right (Xenakis): every trip round the path takes a chance length | trail | the room's late tail (the FDN reverb) |
| 18 TANTRI | the tanpura's first string: left third Ma, noon Pa, right third Ni | ring | a room round the drums and the drone (the FDN reverb) |
| 19 HARMONIZER | register: left third every voice an octave down, right third an octave up (same chord shape) | canon: the voices (not you) echo every 340 ms, darker each time | glide: the voices slide into each new note, from a snap up to ~0.8 s |
| 20 PUTZ | Left: the lows are cleaned harder (rumble, room) and the highs left alone. Right: hiss only, the bass untouched | de-crackle: clicks and vinyl crackle on the input are flattened, steady sound passes | de-reverb |
| 21 ENSEMBLE | Left: only the slow LFO, deeper (a pure swirl). Right: only the fast one, deeper (pure shimmer, even in chorus) | regeneration: the taps go round again (with DENSITY low: flanger jets) | reverb: the string machine's hall (the module's FDN reverb) |
| 22 BARBERPOLE | Left: every pass darker (the loop closes to 1.2 kHz). Right: every pass thinner (opens up to 1.5 kHz) | wow: the loop delay wobbles, so every pass is a little more out of tune, like tape | dispersion |
| 23 SPECDELAY | Left: stripes (neighbouring bands answer early and late). Right: scatter (every band gets its own random delay) | blur: every repeat smears further across frequency, and tones widen into hiss | diffusion: every repeat smears further in time, and the echoes melt into a wash |
| 24 CHOIR | noon = as before (altos). Left: bigger, darker singers (formants down). Right: smaller, brighter ones (formants up). The notes stay put | resonance: your input rings on and sings its chord for seconds (TEXTURE left of full) | reverb: a choir needs a church (the module's FDN reverb) |
| 25 FOLDER | Left: your input bends the sine's phase (option 1). Right: the sine ring-modulates your input (in option 2 too: a ring mod at PITCH) | the folder feeds itself: growl, then chaos | reverb (the module's FDN reverb) |
| 26 HALL | Left: the lows die sooner (bass decay down to ×0.25: a clear, airy hall). Right: the lows ring on (×3: warm and heavy) | pre-delay echoes | diffusion backs off |
| 27 BLOOM | Left: more and more blooms fade out instead of in (reverse echoes that strike, then trail away). Right: more and more play forwards, swelling in, one window late | regeneration (the module's feedback loop): every bloom blooms again (reversed twice = forwards again …) | haze tail |
| 28 GAMELAN | mallet: noon = as before. Left: softer, padded (upper partials fade, round and dull). Right: harder (upper partials up to ×5). Shapes TRIG strikes | tutup | the pendopo: a reverb around the bank (the module's FDN reverb) |
| 29 MIRROR | frequency shift: noon none, left down, right up, up to ±1 kHz. Every partial moves by the same Hz, so harmonic sounds turn inharmonic; below 0 Hz they reflect back up | warp feedback | reverb (the module's FDN reverb; MIRROR's output is mono, the space comes from here) |
| 30 POLISH | where the compressor leans: noon = as before. Left: the lows get squeezed harder (tight, dense bass). Right: the highs do (tames harshness) | parallel ("New York") compression | air: new harmonics above 6 kHz, made from the highs (sparkle, not EQ) |

## Modes

### 1 GRAINS — trajectory grains
Buffer: SampleBuffer, stereo (options 0–3) or mono (4–7), ≤ 118 KB,
records continuously unless frozen.
- POSITION: centre of the trajectory (0 = newest audio, 1 = oldest).
- SIZE: trajectory extent and speed. < 0.08 = static point; above that the
  path widens to half the buffer and its speed rises 0.05 → 2 Hz.
- DENSITY: grain rate 1 → 150 grains/s (exponential). Grain length follows
  automatically (≈ 4 overlapping grains, clamped 25–600 ms).
- TEXTURE: window shape: 0 percussive (fast attack, exp decay) → 0.5 Hann
  → 1 flat-top (Tukey). Above 0.75 adds random pitch scatter (up to ±1 st).
- PITCH: grain transposition, quantised by option: 0 chromatic,
  1 minor pentatonic, 2 octaves+fifths, 3 free (unquantised).
- Pan: each grain is panned from the trajectory's own pan (circle /
  figure-8 use it).
- BAL: left = spray (random offset up to ±½ buffer), right = probability
  of reversed grains. FBK: feedback service. RVB: FDN service.
- FREEZE: stop recording. TRIG: next trajectory shape:
  circle → line (back-and-forth scan) → figure-8 → spiral → random walk.
- Up to 32 voices, Hermite reads. Save: the buffer.

### 2 SPECTRAL — spectral freeze / resynthesis
STFT 2048 / hop 512 stereo, phase vocoder resynthesis.
- POSITION: partial thinning: 0 = all bins, 1 = only the strongest peaks
  (≈ 12). With a morph slot armed: morph from the current (frozen) spectrum
  to the slot's spectrum instead.
- SIZE: harmonic snap amount: bins are pulled onto the harmonic series of
  root = 65.41 Hz × transposition ratio.
- DENSITY: blur (temporal magnitude smoothing, + frequency smear at top).
- TEXTURE: spectral tilt −9 … +9 dB/oct, flat at noon.
- PITCH: transposition (bin shifting with frequency scaling).
- FREEZE: hold magnitudes + instantaneous frequencies; resynthesised with
  running phases and a slight random drift. TRIG: grab a new spectrum.
- Save: frozen magnitudes and frequencies. Morph: supported.

### 3 CONVOLVE — convolution, freeze captures the impulse response
Mono capture buffer (~0.35 s). Uniform partitioned convolution, block 256,
IR up to 9216 samples (0.19 s), int16 spectra with per-partition scale.
Stereo in → stereo out through the same IR. Starts with a synthetic small
room IR so it sounds immediately.
- FREEZE rising edge or TRIG: capture a new IR from the buffer.
- POSITION: where in the capture the IR starts (0 = newest).
- SIZE: IR length (5 ms → max) with fade-out.
- DENSITY: IR envelope: < 0.5 reverse swell, 0.5 as captured, > 0.5
  increasingly fast exponential decay.
- TEXTURE: IR brightness (tilt on the IR spectrum).
- PITCH: IR playback rate (resampled at capture/rebuild).
- Knob changes rebuild the IR spectra in the background (debounced).
- Save: the IR (time domain) + capture params; load rebuilds spectra.

### 4 RESONATOR — sympathetic strings + jawari
8 Karplus-Strong strings (fractional delay, loop filter), excited by input.
- PITCH: root, V/oct, root = 65.41 Hz × 2^(pitch/12).
- SIZE: tuning: unison/octaves → fifths → sitar sympathetic set
  (Sa Re Ga ma Pa Dha Ni Sa') → harmonic series → inharmonic.
- DENSITY: decay 0.2 → 20 s.
- POSITION: brightness (loop low-pass) and excitation point.
- TEXTURE: jawari: 0 clean string → 1 heavy bridge buzz.
- FREEZE: infinite sustain, input cut. TRIG: pluck all strings.
- Strings alternate left/right.

### 5 DIFFERENCE — difference tones (after the Oneirophone)
Input → Hilbert transform → frequency-shifted copies. Pair k = input
shifted by f_a and by f_a + Δ_k; every input partial p becomes p+f_a and
p+f_a+Δ_k, so the ear hears Δ_k for all of them.
- PITCH: Δ (the heard tone), V/oct, Δ = 130.81 Hz × 2^(pitch/12).
- POSITION: register f_a, 2 → 8 kHz (log).
- SIZE: input bandwidth fed to the pairs: narrow (low-passed at 200 Hz,
  purest illusion) → wide (whole input, rich).
- DENSITY: number of pairs 1 → 6, Δ_k = Δ × {1, 3/2, 5/4, 2, 7/4, 9/8}.
- TEXTURE: purity: 0 pure (difference only in the ear) → 1 quadratic
  distortion makes the difference tone physically present.
- FREEZE: drone of pure sine pairs at the level held from the input.

### 6 CROSS — L/R spectral morph
STFT 2048/512. Left = modulator, right = carrier.
- POSITION: 0 vocoder (L's smoothed envelope on whitened R) → 0.5 morph
  (geometric magnitude interpolation, R's phases) → 1 L's magnitudes with
  R's phases.
- DENSITY: envelope resolution (smoothing width in bins).
- TEXTURE: time smoothing (attack/release of the envelopes).
- SIZE: formant shift of the modulator envelope (×0.5 → ×2).
- PITCH: carrier transposition.
- FREEZE: hold the modulator spectrum. TRIG: toggle hold of the carrier.
- Output mono (same on both sides); RVB (FDN) gives it a stereo room.

### 7 STRETCH — extreme time stretch
SampleBuffer stereo (option 0) or mono (option 1), ≤ 118 KB; STFT
2048/512 reading frames from the buffer.
- SIZE: stretch: 0 = 1× → 0.9 ≈ 1000× → 1 = infinite (read head stops).
- POSITION: offset of the read zone in the buffer.
- DENSITY: smear (magnitude averaging over frames + bin blur).
- TEXTURE: 0 phase-vocoder (coherent) → 1 Paulstretch (random phases).
- PITCH: transposition without speed change.
- FREEZE: stop recording. TRIG: jump the read head to the newest audio.
- Save: the buffer.

### 8 SPREEK — voice
LPC analysis (order 14) of the input every 256 samples + 5-formant vowel
filters + glottal-pulse carrier.
- POSITION: 0–0.15 the input's own spectral envelope (talkbox), then
  vowels a → e → i → o → u (interpolated formants).
- SIZE: vocal-tract length (formants × 0.7 → 1.4).
- DENSITY: source: 0 the input itself through the filters → 1 internal
  glottal carrier at PITCH, enveloped by the input.
- TEXTURE: smooth → breathy (aspiration noise, wider formants).
- PITCH: carrier pitch, V/oct.
- FREEZE: hold the current phoneme as a drone. TRIG: consonant burst.
- Options: 0 adult formants, 1 child formants.

### 9 CAPTURE — captured wavetable voice
Mono capture 0.5 s. FREEZE builds a wavetable: 16 frames × band-limited
mip levels (built by FFT, so they are periodic and alias-free).
Starts with a sine → saw table.
- POSITION: scan through the frames (and into the morph slot's frames).
- PITCH: V/oct, C3 at noon.
- DENSITY: unison 1 → 7 voices, detune, stereo spread.
- TEXTURE: brightness (mip-level bias = alias-free low-pass).
- SIZE: envelope: pluck → pad.
- TRIG gate plays the voice; until a gate arrives it drones.
- Save: the table. Morph: supported (reads the slot's frames from flash).

### 10 GENDY — dynamic stochastic synthesis (Xenakis)
Two independent GENDYN voices (L/R), 2× oversampled.
- PITCH: centre frequency, V/oct (65.41 Hz × 2^(pitch/12)).
- DENSITY: breakpoints 3 → 40.
- TEXTURE: random-walk step size (0 = static waveform).
- SIZE: barriers: stable → wild.
- POSITION: distribution: Cauchy → logistic → hyperbolic cosine → arcsine.
- FREEZE: freeze the walks. TRIG: reseed the waveform from the input.
- Save: breakpoints.

### 11 SHIMMER — shimmer cascade
4-line diffuse delay network with a pitch shifter in the loop.
- POSITION: harmonic set: plain → octave → fifth → octave+fifth
  (alternating per pass) → Oneirophone ratios (7/4, 9/8, 3/2 cycling)
  → octave down.
- SIZE: room size. DENSITY: decay (→ infinite). TEXTURE: damping.
- PITCH: extra transposition of the shift.
- FREEZE: infinite hold, input muted. TRIG: clear the tail.

### 12 TAPE — multi-head tape echo
Tape: SampleBuffer-like int16 loop, stereo (option 0) or mono (option 1).
Tape speed resamples the write (anti-aliased) so slower tape = longer,
darker echoes. Three heads at 1, 2, 3 × spacing.
- SIZE: tape speed ×0.25 → ×2 (1× at noon). PITCH: fine speed / V/oct.
- POSITION: head combination (7 combinations + all + swell).
- DENSITY: feedback (to slight self-oscillation, saturating).
- TEXTURE: tape character: wow, flutter, saturation, head bump.
- FREEZE: loop hold (no new input, repeats sustain).
- TRIG: tape stop (spin-down), next TRIG spins up.
- Save: the tape.

### 13 PLATES — sympathetic plates (modal, Ronin plates)
24 modes per side (two pickups), plate eigenfrequencies f(m,n) ∝
(m/a)² + (n·a)²-style series, excited by the input.
- PITCH: root, V/oct. SIZE: aspect ratio (square → long bar).
- POSITION: strike/pickup position (mode amplitudes).
- DENSITY: decay. TEXTURE: material (metal → glass → wood damping).
- FREEZE: infinite ring, input cut. TRIG: strike.

---

## Bank 2 and the rest of bank 1 (modes 15–30)

Menu: hold DISP; POSITION picks one of 15 modes in the current bank; tap
WRITE to switch bank (LEDs green = bank 1, red = bank 2, binary 1–15).

General rules for these modes (same as before): every control does
something useful; document the final control mapping in a comment table at
the top of the mode's header (the manual is written from it); total CPU
(engine ~940 + mode, measured with tools/bench/run.sh) ≤ 2400 instructions
per frame at both bench settings; RAM from the arena only; object ≤ 1.5 KB.

### 15 FALTUNG — convolution with a built-in IR library
8 impulse responses stored in flash (no RAM for the IR spectra), generated
by a build-time script (tools/gen_irs.py → monsoon/dsp/modes/faltung_irs.cc):
small room, large hall, plate, spring, cabinet, wooden body, metal sheet,
cathedral-ish dense tail. All synthesized (no third-party IRs), ~0.17 s each,
stereo (L/R IR), stored as int16 partition spectra ready for the MAC loop.
Mono (L+R) input → stereo out. Reuse CONVOLVE's partitioned engine ideas.
- POSITION: which IR (8 zones, 30 ms crossfade on change).
- SIZE: IR length (shorter also costs less CPU).
- DENSITY: pre-delay 0–200 ms.
- TEXTURE: tilt EQ on the wet signal.
- PITCH: input pre-shift is too costly; use it for wet low-cut (20–800 Hz).
- FREEZE: input muted, wet fed back at unity through the IR (endless).
- TRIG: next IR.

### 16 QUELL — resonance suppressor (spectral dynamic EQ)
STFT (choose size/overlap within budget). Find narrow peaks standing above
a smoothed spectral envelope and attenuate them dynamically, linked stereo.
- POSITION: frequency focus (weights detection toward lows ↔ highs).
- SIZE: selectivity (only sharp peaks ↔ broad areas).
- DENSITY: depth (0–24 dB max cut).
- TEXTURE: speed (slow, smooth ↔ fast).
- PITCH: output trim −12 … +12 dB.
- FREEZE: hold the current cut curve (static EQ).
- TRIG: toggle "delta" (hear only what is removed).

### 17 LEITNER — spatial path panner (after Leitner/Xenakis)
Mono source moves along a path around the listener: ITD (≤0.7 ms) + ILD +
head-shadow low-pass + distance gain + air absorption + Doppler (variable
delay ≤ 30 ms) + 4 early reflections from a virtual room.
- POSITION: path centre azimuth. SIZE: radius / distance.
- DENSITY: speed 0.02–4 Hz. TEXTURE: room (reflections amount/size).
- PITCH: Doppler strength (noon = physical, CCW none, CW exaggerated).
- FREEZE: stop moving. TRIG: next path (circle, figure-8, line, spiral,
  random flight).

### 18 TANTRI — tabla + tanpura (from ~/tantri, M4-lite)
Read ~/tantri for the models. Dayan/bayan modal drums + a 4-string tanpura
(Pa Sa Sa Sa) with jawari.
- TRIG: strike (stroke chosen by POSITION: Na, Tin, Tun, Ge, Ka …).
- PITCH: dayan / Sa pitch, V/oct. SIZE: bayan palm glide (pitch bend).
- DENSITY: tanpura drone level + auto-pluck rate.
- TEXTURE: jawari amount. Input excites strings and drums.
- FREEZE: drone sustains forever.

### 19 HARMONIZER — scale-quantized harmonizer
Pitch-track the input (NSDF/YIN), up to 3 pitch-shifted voices (pitch-
synchronous overlap-add or two-tap shifter) on scale degrees.
- POSITION: chord / interval set (3rd, 5th, 3rd+5th, octaves, 4th+7th …).
- PITCH: key root, V/oct. Option: scale (major, minor, dorian, pentatonic).
- DENSITY: number of voices 1–3 and their level. SIZE: humanize (detune,
  small delays). TEXTURE: stereo spread of voices.
- FREEZE: latch the current notes. TRIG: next chord in the set.

### 20 PUTZ — spectral cleaner
Noise profile learned while FREEZE is held (or minimum-statistics tracking
when never learned), Wiener-style gain with smoothing, plus a hum comb.
- DENSITY: reduction amount. SIZE: smoothing (less musical noise).
- TEXTURE: transient protection. POSITION: hum comb depth.
- PITCH: hum fundamental (noon = 50 Hz; option 1 = 60 Hz base).
- FREEZE (hold): learn noise. TRIG: toggle "listen to what is removed".

### 21 ENSEMBLE — string-machine ensemble
Three modulated delays (5–15 ms) driven by two LFOs (slow ~0.6 Hz + fast
~6 Hz) at 120° phases, stereo; optional BBD darkness.
- POSITION: chorus ↔ ensemble ↔ vibrato. DENSITY: depth.
- SIZE: base delay. TEXTURE: clean hi-fi ↔ dark BBD.
- PITCH: LFO rate (V/oct). FREEZE: stop the LFOs. TRIG: reset phases.

### 22 BARBERPOLE — frequency-shift spirals
SSB frequency shifter (reuse DIFFERENCE's Hilbert pair) inside a delay
feedback loop: each pass shifts further → endless rising/falling spirals.
- POSITION: shift amount, bipolar (CCW down, CW up, 0.1–2000 Hz exp).
- PITCH: multiplies the shift (V/oct). DENSITY: feedback.
- SIZE: loop delay 1–500 ms. TEXTURE: up/down mix (L up, R down at max).
- FREEZE: infinite spiral, input muted. TRIG: flip direction.

### 23 SPECDELAY — spectral delay
STFT; each bin has its own delay (frames) and feedback, stored compactly
(e.g. int16 complex, N=512/hop 128, ~32 frames).
- POSITION: delay curve (lows later ↔ highs later). SIZE: max delay.
- DENSITY: feedback. TEXTURE: feedback tilt / smear.
- PITCH: bin shift per pass (spectral spirals). FREEZE: infinite hold.

### 24 CHOIR — formant choir
4 voices on a chord following V/oct; each voice = pitched resonator/comb at
its note + vowel formant filter. Excitation = input and/or an internal
glottal source.
- POSITION: vowel a-e-i-o-u. DENSITY: chord type (unison, 5ths, maj, min,
  sus, 7th). SIZE: voice spread (detune, stereo). TEXTURE: input ↔ glottal
  excitation (breath). PITCH: root V/oct.
- FREEZE: sustain chord on the internal source. TRIG: next inversion.

### 25 FOLDER — west-coast folder + low-pass gate
Input (+ internal sine at PITCH) → wavefolder (series folds, ADAA or
oversampled) → vactrol LPG (envelope follower and TRIG pings).
- POSITION: fold amount. SIZE: symmetry / bias. DENSITY: LPG mode
  (low-pass ↔ combo ↔ VCA). TEXTURE: vactrol decay. PITCH: sine V/oct.
- FREEZE: LPG held open (drone). TRIG: ping the LPG.

### 26 HALL — big algorithmic reverb
Large modulated tank (most of the arena, int16 lines), decay up to ~30 s,
lush and dense; separate from the output-section reverb.
- POSITION: pre-delay 0–250 ms. SIZE: size. DENSITY: decay.
- TEXTURE: damping. PITCH: modulation depth/rate.
- FREEZE: infinite. TRIG: clear.

### 27 BLOOM — swells + reverse bloom
Removes attacks (envelope-driven slow-attack gain) and adds reverse
pre-swells: buffer the input, play reversed windows with rising envelopes
ahead of the (delayed) dry.
- POSITION: swell time. SIZE: reverse window (= latency) 0.1–1 s.
- DENSITY: reverse-bloom amount. TEXTURE: diffusion/smear.
- PITCH: transposition of the reverse bloom. FREEZE: hold the bloom.

### 28 GAMELAN — metallophones
7 keys tuned pelog (option 0) or slendro (option 1), each key a small modal
set (saron bar ratios ↔ bonang kettle ↔ gong), paired-key detune → ombak
beating. Input excites the bank; TRIG strikes the key chosen by POSITION.
- PITCH: root V/oct. SIZE: ombak rate 0–10 Hz. DENSITY: decay.
- TEXTURE: instrument (saron ↔ bonang ↔ gong). FREEZE: infinite ring.

### 29 MIRROR — spectral warping
STFT; output bin k reads input at f(k): stretch (k^a, inharmonic),
inversion around a mirror frequency, folding.
- POSITION: warp type morph (stretch → invert → fold). SIZE: amount.
- PITCH: mirror / anchor frequency (V/oct). DENSITY: spectral blur.
- TEXTURE: dry-spectrum blend + tilt. FREEZE: hold the spectrum.

### 30 POLISH — mastering chain
3-band compressor (LR crossovers ~200 Hz / 3 kHz) → tape saturation
(oversampled) → M/S width with mono bass. (The engine limiter follows.)
- DENSITY: compression amount (macro). TEXTURE: saturation.
- POSITION: tone tilt. SIZE: width. PITCH: output trim ±12 dB.
- FREEZE: bypass (A/B). TRIG: auto-gain re-match.


## Implementation notes (as built)

- SPECTRAL / CROSS: own lean STFT (modes/lean_stft.h), FFT 2048 with hop
  1024 and sine windows (2× overlap) to fit the CPU budget; 3072-sample
  latency. CROSS synthesises mono.
- STRETCH: hop 683 (3× overlap), complex-rotation resynthesis.
- CONVOLVE: block 512, packed int16 SIMD multiply-accumulate (SMUSD/SMUADX),
  IR up to 6656 samples (139 ms), rebuilds debounced to one per 150 ms.
- All FFTs: CMSIS-DSP arm_rfft_fast_f32 (monsoon/dsp/cmsis).
