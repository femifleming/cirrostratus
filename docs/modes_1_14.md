### Bank 1 (green)

#### 1 · GRAINS — trajectory grains
*Forecast: scattered showers, falling in shapes.*

Grains are read from a recorded buffer along a moving path, not at random
spots. Buffer: 0.62 s stereo or 1.24 s mono, up to 3.66 s / 7.32 s at a
lower buffer quality (3.7).

| Control | |
|---|---|
| POSITION | centre of the path (bottom = newest audio) |
| SIZE | path size and speed: bottom = a fixed point; up = wider and faster (0.05 → 2 Hz) |
| DENSITY | grain rate 1 → 150 grains/s; grain length follows (long and sparse → short and dense) |
| TEXTURE | window: percussive → smooth (Hann) → flat; top quarter adds pitch scatter |
| PITCH | grain transposition, quantised by the option |
| BAL | noon = plain. Left: spray (grains scatter around the path, up to half the buffer away). Right: more and more grains play backwards, all of them at full |
| FBK | feedback: the cloud back into itself, Clouds style, with the howl tamer on duty |
| RVB | the 8-line FDN reverb: send and length together, 0.5 → 16 s |
| FREEZE | stop recording |
| TRIG | next path shape: circle → line → figure-8 → spiral → random walk |
| Options | 1 chromatic · 2 minor pentatonic · 3 octaves + fifths · 4 free; options 5–8 = the same in mono (double length) |
| Keeps | the buffer |

#### 2 · SPECTRAL — spectral freeze / resynthesis
*Forecast: a held moment, refracted into its colours.*

2048-point phase vocoder with peak-locked partials (64 ms latency; 256–2048 with the FFT size setting, 3.8).

| Control | |
|---|---|
| POSITION | partial thinning: all bins → only the strongest peaks. With a morph slot armed: blend from the current spectrum to the slot's |
| SIZE | harmonic snap: pulls partials onto the harmonic series of the PITCH root |
| DENSITY | blur (smears the spectrum over time) |
| TEXTURE | tilt, dark ↔ bright, flat at noon (±9 dB/oct) |
| PITCH | transposition without changing speed |
| FREEZE | hold the spectrum; it keeps resynthesising with a slight drift |
| TRIG | grab a new spectrum while frozen |
| BAL | noon = as before. Turning it shifts every partial by the same number of Hz (not a ratio), so harmonic sounds go inharmonic. Left shifts down, right shifts up, exponential: a few Hz near noon, ±1 kHz at the ends |
| FBK | regeneration: the last output spectrum comes back as a floor under the next frame (held 0.1 → 8 s). On its own it sustains; with BAL or PITCH set every pass moves the partials again, so they spiral up or down |
| RVB | phase diffusion: random phases blend in, coherent → a whispered wash in the same colours |
| Options | 1 as above · 2 SMEAR (below) |
| Keeps / morph | the frozen spectrum / yes (option 1) |

**Option 2 · SMEAR** — *a photograph left out in the rain.* The spectral
smear from Sleepcousonique's Wingie mode, the same sound: the spectrum is
blurred in time and a little across frequency, and its partials carry on.
512-point transform, 4× overlap, 16 ms latency. Mono, like the Wingie's: the
mid (L+R) is smeared and comes out of both sides.

| Control | |
|---|---|
| DENSITY | blur: none (the sound as it is) → seconds; long blurs beat and shimmer |
| FREEZE | hold: the blur stops where it is |
| RVB | reverb (the module's FDN reverb: SMEAR's space) |
| Keeps / morph | nothing / no |

#### 3 · CONVOLVE — freeze captures the impulse response
*Forecast: the room you're standing in is whatever you just played.*

Your own audio becomes the space the sound plays in. The impulse response
(IR) can be up to 139 ms, and FBK makes it ring on much longer. The
mode starts with a small synthetic room so it sounds straight away.

| Control | |
|---|---|
| FREEZE (on) / TRIG | capture a new IR from the last 139 ms of input |
| POSITION | where in that capture the IR starts (bottom = newest) |
| SIZE | IR length, with fade-out |
| DENSITY | IR envelope: reverse swell ← as captured (noon) → faster decay |
| TEXTURE | IR brightness: low-pass below noon, treble boost above |
| PITCH | IR playback rate |
| BAL | noon = as before. Left: only the attacks of the input reach the IR. Right: only the body (attacks ducked, so hits swell instead of crack). It shapes the input before everything, so captures hear it too |
| FBK | feedback (the module's feedback loop, with its howl tamer): the IR rings on much longer |
| RVB | IR smear: the IR gets eight decaying echoes of itself (3 → 97 ms), and captures melt into a dense wash |
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
| BAL | noon = as before. Left: detune, the strings spread up to ±25 cents (beating, a chorus of strings). Right: stretch, intervals widen up to +10 % (an octave ~+120 cents: piano-like, then bell-like) |
| FBK | sympathetic coupling: energy passes from string to string. Strings tuned close together trade it back and forth (slow beating, blooming), and a note in one wakes the others |
| RVB | body: the strings sound through a resonant wooden box (~8.5 ms resonance, tail up to ~0.35 s) |

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
| BAL | noon = as before. Left: glide, the heard note slides to each new pitch (20 ms → 2 s). Right: dichotic, f to the left ear and f + Δ to the right; on headphones the difference tone gives way to binaural beats |
| FBK | spiral: the shifted sound recirculates (50 ms) and climbs by Δ on every pass. Echoes rise; held sounds build a ladder f, f+Δ, f+2Δ… that makes the phantom note stronger |
| RVB | reverb (the module's FDN reverb; both tones reverberate, so the phantom note survives) |

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
| BAL | noon = as before (carrier = right input). Left: the carrier fades to white noise (whispered; consonants come through). Right: it fades to an internal buzz played by PITCH (C3 at noon, V/OCT), a classic vocoder from one input; PITCH then stops transposing R |
| FBK | formant emphasis: the modulator's envelope sharpens (peak and energy kept): more talkative, then resonant, wah-like peaks |
| RVB | reverb (the module's FDN reverb; CROSS is mono, this gives it a stereo room) |

#### 7 · STRETCH — extreme time-stretch
*Forecast: one second, smeared across an afternoon.*

A phase vocoder with phase locking reads a recorded buffer slowly: 0.30 s
stereo or 0.89 s mono, up to 1.76 s / 5.23 s at a lower buffer quality (3.7).

| Control | |
|---|---|
| SIZE | stretch: 1× → ~1000× at 0.9 → standing still from about 0.98 |
| POSITION | where the read zone sits in the buffer |
| DENSITY | smear |
| TEXTURE | coherent (phase vocoder) → Paulstretch (random phases) |
| PITCH | transposition without speed change |
| FREEZE | stop recording |
| TRIG | jump to the newest audio |
| BAL | noon = as before. Left: the read slows, stands still at 9 o'clock, and runs backwards at full left. Right: scatter, the read window jumps to random spots (up to half the buffer away, ~10 jumps/s) |
| FBK | re-record: the output goes back into the recording, so stretches get stretched again and smear into drones (nothing is recorded while frozen) |
| RVB | octave halo: every partial also sounds an octave up, a shimmer over the stretch |
| Options | 1 stereo · 2 mono (longer) |
| Keeps | the buffer |

#### 8 · SPREEK — voice
*Forecast: everything starts talking (and singing).*

LPC speech analysis, five-formant vowels and a singing carrier. BAL, FBK
and RVB shape the internal carrier, so they need DENSITY above zero.

| Control | |
|---|---|
| POSITION | bottom: the input's own voice envelope (talkbox); then vowels a → e → i → o → u |
| SIZE | vocal-tract length (formants × 0.7 → 1.4) |
| DENSITY | source: the input itself → an internal singing carrier at PITCH, following the input's level |
| TEXTURE | smooth → breathy |
| PITCH | carrier pitch, V/OCT |
| FREEZE | hold the phoneme as a drone |
| TRIG | consonant burst |
| BAL | noon = as before. Left: glide (PITCH slides; an octave takes up to ~0.6 s). Right: vibrato (5.3 Hz, up to ±1 semitone) |
| FBK | growl: every other glottal period comes weaker and longer — from a rough edge to vocal fry with an undertone an octave down (pitch stays put) |
| RVB | choir: two more carriers join, up to ±22 cents apart and drifting, so one voice becomes a small chorus |
| Options | 1 adult formants · 2 child formants |

#### 9 · CAPTURE — captured wavetable voice
*Forecast: catch a sound, play it like a synth.*

FREEZE turns the last half second of input (up to 3 s at a lower buffer
quality, 3.7) into a 16-frame wavetable. The
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
| BAL | noon = as before. Left: bend — phase distortion, the first half of each cycle read up to 6× faster (CZ style). Right: sync formant — the table read up to 6× per cycle under a soft window, a resonant peak sweeping up. Stays alias-free |
| FBK | self-FM: each voice bends its own phase (DX-style operator feedback) — warmer, then saw-like, then a rasp |
| RVB | reverb (the module's reverb, for a voice with no space of its own) |
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
| BAL | noon = both voices around PITCH. The right voice moves in whole semitones: left down to an octave below, right up to an octave above (fifths, fourths, octaves) |
| FBK | cross-coupling: each voice stretches or squeezes the other's segments (up to ±80 %), so the two walks start steering each other |
| RVB | reverb (the module's reverb) |
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
| BAL | noon = as before. Left: less of each pass is shifted (fully left = a plain reverb in any set). Right: mirror — the fourth line falls by the inverse of the set's interval, so the cascade climbs and falls |
| FBK | tank modulation: the four delay lines wobble on two slow LFOs; the ringing smears into a lush, chorused tail |
| RVB | pre-delay 0 → 250 ms before the network (glides when turned) |

#### 12 · TAPE — multi-head tape echo
*Forecast: warm, wobbly, three heads at the ready.*

Variable-speed tape with an anti-aliased record path: slower tape gives
longer, darker echoes. Heads at 1×, 2× and 3× spacing (0.2 / 0.4 / 0.6 s at
1× in stereo, about twice that in mono; up to 6× longer at a lower buffer
quality, 3.7).

| Control | |
|---|---|
| SIZE | tape speed ×0.25 → ×2 (×1 at noon) |
| PITCH | fine speed, V/OCT |
| POSITION | heads: 1 · 2 · 3 · 1+2 · 2+3 · 1+3 · 1+2+3 · all into the repeats · swell |
| DENSITY | feedback, up to gentle self-oscillation |
| TEXTURE | tape character: wow, flutter, saturation, head bump |
| FREEZE | loop hold: repeats sustain, no new input |
| TRIG | tape stop (spins down); next TRIG spins back up |
| BAL | noon = as before. Left: darker repeats (loop low-pass 7 kHz → 500 Hz). Right: thinner repeats (loop high-pass up to 2 kHz), the dub telephone fade |
| FBK | ping-pong: the repeats cross to the other side (fully right = every repeat swaps). Stereo tape only |
| RVB | diffusion in the loop: every pass smears a little more, so the echoes melt into a spring-like wash |
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
| BAL | noon = as before. Left: the modes are pulled onto whole harmonics (fully left = a tuned, pitched plate). Right: the series stretches — more clang, bell / tam-tam |
| FBK | tension: hard hits ring sharp (up to ~3 semitones) and glide back down as they decay |
| RVB | reverb (the module's reverb, so a struck plate gets a room) |

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
| BAL | the Lorenz attractor: noon = as before. Left: calmer and shallower until it stops (both cutoffs together, the filter halfway LP/BP, ¼ speed). Right: up to 8× faster and ±3 octaves, the filter wandering LP → BP → HP |
| FBK | master feedback, crossed like the plug-in: left out → right in, right out → left in. With regen up it ping-pongs and can sing |
| RVB | diffusion in the regen loop: every repeat smears a little more and the echoes blur into a cloud (string engine: a haze on the strings) |
| Options | 1 PT2399 · 2 BBD · 3 tape · 4 Karplus-Strong string, all filter → tube → delay; 5–8 the same as delay → tube → filter |

On the module: the tube runs 2× oversampled (4× in the plug-in), one
delay line per side (time changes glide), the tape is 0.45 s per side with
fixed wear, and there are no poly taps or flock.
