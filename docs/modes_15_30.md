<!-- FALTUNG -->

### Bank 2 (red)

<!-- QUELL -->

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

<!-- PUTZ -->

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

<!-- POLISH -->
