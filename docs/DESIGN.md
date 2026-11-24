# Monsoon HiFi — design spec

Source of truth for the modes. The manual (MANUAL.md) is written from this.

## Platform facts

- STM32F405RGT6, 168 MHz Cortex-M4F, WM8731 codec. Engine runs at **48 kHz**,
  blocks of 32 frames → ~3500 CPU cycles per stereo frame in total.
  Budget per mode (Process + Prepare averaged): **≤ 2000 cycles/frame**.
  The output section (reverb, spread, tamer, limiter) takes ~300–400.
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
| position | POSITION knob + CV | 0..1 |
| size | SIZE knob + CV | 0..1 |
| density | DENSITY knob + CV | 0..1 |
| texture | TEXTURE knob + CV | 0..1 |
| pitch | PITCH knob + V/OCT | semitones, −48..+48, 0 at noon |
| blend / spread / feedback / reverb | the 4 sliders + CV | 0..1 |
| freeze | FREEZE button toggle or FREEZE gate | bool |
| trigger | TRIG input rising edge this block | bool |
| gate | TRIG input level | bool |
| option | chosen in the mode menu | 0..num_options−1 |

Musical pitch reference: `pitch = 0` means C3 = 130.81 Hz unless a mode says
otherwise. `root_hz = 130.81 * 2^(pitch/12)`.

Options shown on the 4 LEDs: option i lights LED (i % 4); green for i < 4,
red for i ≥ 4. For buffer modes with a mono choice: options 0–3 stereo,
4–7 the same in mono (twice the length).

## Shared output section (engine, all modes)

1. Input + feedback → mode. Feedback = wet output → 25 Hz HP →
   ResonanceTamer (two adaptive notches; depth rises with how much of the
   loop's energy sits in narrow peaks, scaled by the FEEDBACK slider) →
   soft clip. Gain curve: smoothstep(feedback).
2. Wet → DC blocker → Spread: 0 = mono, 0.5 = as produced, 1 = wide
   (side gain up to 1.6 + mono-compatible allpass decorrelation + ≤0.35 ms
   Haas at the very top).
3. Blend: equal power dry/wet.
4. Reverb: 8-line FDN with 16-bit lines (≈ 15 k samples), modulated, T60
   0.5–16.5 s from the REVERB slider, which also sets the send level.
5. Limiter (−1 dBFS, transparent below −2 dBFS) → TPDF dither → codec.

Envelope follower: input level can push any of the 4 slider values up or
down (per-mode routing, set in the ENV page).

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
- SPREAD: each grain is panned from the trajectory's own pan (circle /
  figure-8 use it); spread scales that.
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
- Output mono (same on both sides); SPREAD widens it.

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
