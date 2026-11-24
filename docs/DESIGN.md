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
| position | POSITION slider + CV | 0..1 |
| density | DENSITY slider + CV | 0..1 |
| size | SIZE slider + CV | 0..1 |
| texture | TEXTURE slider + CV | 0..1 |
| pitch | TUNE knob + V/OCT | semitones, −48..+48, 0 at noon |
| blend / spread / feedback / reverb | WET / STER / FDB / RVB knobs + CV | 0..1 |
| freeze | FREEZE button toggle or FREEZE gate | bool |

Panel buttons: DISP (top left), WRITE (pencil, top right), FREEZE (big).
In the text below "PITCH" means the TUNE knob + V/OCT input, and the
output-section names refer to the four small knobs.
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
   loop's energy sits in narrow peaks, scaled by the FDB knob) →
   soft clip. Gain curve: smoothstep(feedback).
2. Wet → DC blocker → Spread: 0 = mono, 0.5 = as produced, 1 = wide
   (side gain up to 1.6 + mono-compatible allpass decorrelation + ≤0.35 ms
   Haas at the very top).
3. Blend: equal power dry/wet.
4. Reverb: 8-line FDN with 16-bit lines (≈ 15 k samples), modulated, T60
   0.5–16.5 s from the RVB knob, which also sets the send level.
5. Limiter (−1 dBFS, transparent below −2 dBFS) → TPDF dither → codec.

Envelope follower: input level can push any of the 4 output knobs up or
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
