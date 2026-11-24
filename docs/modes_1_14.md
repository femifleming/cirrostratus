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
