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
| BAL | early ↔ late: noon = the IR as it is. Left: the tail fades away (close, dry). Right: the early part fades and the tail swells (far away). Same loudness |
| FBK | feedback through the IR: the space builds on itself, longer and denser; it never howls (FREEZE is the endless version) |
| RVB | a long smooth FDN tail (0.5 → 16 s) behind the space (the IRs are at most 170 ms) |

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
| BAL | noon = stereo-linked. Left: mid only (the sides pass untouched). Right: sides only |
| FBK | hard threshold: anything louder than a level falling 0 → −72 dBFS is pushed down too; all the way up it squashes the spectrum flat (up to DENSITY's depth) |
| RVB | halo: what's cut rings on as a soft spectral tail, 0.3 → 5 s (also heard in delta) |

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
| BAL | noon = a smooth glide. Left (Leitner): the path becomes 8 loudspeaker stations; the source rests at each and hops to the next. Right (Xenakis): every trip round the path takes a chance length |
| FBK | trail: the sound is re-emitted every 150 ms from wherever the source has flown to, darker each time |
| RVB | the room's late tail (the FDN reverb) |

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
| BAL | the tanpura's first string: left third Ma, noon Pa, right third Ni |
| FBK | ring: the drum heads ring up to 5× longer, the closed strokes stop damping, and the drone sets the drums humming |
| RVB | a room round the drums and the drone (the FDN reverb) |

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
| BAL | register: left third every voice an octave down, right third an octave up (same chord shape) |
| FBK | canon: the voices (not you) echo every 340 ms, darker each time |
| RVB | glide: the voices slide into each new note, from a snap up to ~0.8 s |
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
| BAL | noon = as before. Left: the lows are cleaned harder (rumble, room) and the highs left alone. Right: hiss only, the bass untouched |
| FBK | de-crackle: clicks and vinyl crackle on the input are flattened, steady sound passes |
| RVB | de-reverb: every tail dies up to 3× faster, attacks and held notes stay (works with DENSITY at 0 too) |
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
| BAL | noon = as before. Left: only the slow LFO, deeper (a pure swirl). Right: only the fast one, deeper (pure shimmer, even in chorus) |
| FBK | regeneration: the taps go round again (with DENSITY low: flanger jets) |
| RVB | reverb: the string machine's hall (the module's FDN reverb) |

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
| BAL | noon = as before. Left: every pass darker (the loop closes to 1.2 kHz). Right: every pass thinner (opens up to 1.5 kHz) |
| FBK | wow: the loop delay wobbles, so every pass is a little more out of tune, like tape |
| RVB | dispersion: every pass drags the lows behind the highs, and the repeats melt into falling chirps, like a spring |

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
| BAL | noon = as before. Left: stripes (neighbouring bands answer early and late). Right: scatter (every band gets its own random delay) |
| FBK | blur: every repeat smears further across frequency, and tones widen into hiss |
| RVB | diffusion: every repeat smears further in time, and the echoes melt into a wash |

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
| BAL | noon = as before (altos). Left: bigger, darker singers (formants down). Right: smaller, brighter ones (formants up). The notes stay put |
| FBK | resonance: your input rings on and sings its chord for seconds (TEXTURE left of full) |
| RVB | reverb: a choir needs a church (the module's FDN reverb) |

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
| BAL | noon = as before. Left: your input bends the sine's phase (option 1). Right: the sine ring-modulates your input (in option 2 too: a ring mod at PITCH) |
| FBK | the folder feeds itself: growl, then chaos |
| RVB | reverb (the module's FDN reverb) |
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
| BAL | noon = as before. Left: the lows die sooner (bass decay down to ×0.25: a clear, airy hall). Right: the lows ring on (×3: warm and heavy) |
| FBK | pre-delay echoes: the pre-delay repeats (time = POSITION), and every echo blooms into the hall. Nothing at POSITION 0 |
| RVB | diffusion backs off: the smooth wash turns grainy and its echoes come apart, like a stone stairwell |

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
| BAL | noon = as before. Left: more and more blooms fade out instead of in (reverse echoes that strike, then trail away). Right: more and more play forwards, swelling in, one window late |
| FBK | regeneration (the module's feedback loop): every bloom blooms again (reversed twice = forwards again …) |
| RVB | haze tail: the diffusion chain becomes a loop, so the blooms ring on after the note (reverse reverb → note → reverb) |

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
| BAL | mallet: noon = as before. Left: softer, padded (upper partials fade, round and dull). Right: harder (upper partials up to ×5). Shapes TRIG strikes |
| FBK | tutup: on each strike the other hand chokes the key struck before (up to 60 dB in 60 ms). 0 = everything rings |
| RVB | the pendopo: a reverb around the bank (the module's FDN reverb) |
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
| BAL | frequency shift: noon none, left down, right up, up to ±1 kHz. Every partial moves by the same Hz, so harmonic sounds turn inharmonic; below 0 Hz they reflect back up |
| FBK | warp feedback: the warped spectrum is warped again and again. Partials spiral (stretch glides them away from the anchor, BAL slides them, fold piles them under it); with no warp it's a spectral sustain |
| RVB | reverb (the module's FDN reverb; MIRROR's output is mono, the space comes from here) |

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
| BAL | where the compressor leans: noon = as before. Left: the lows get squeezed harder (tight, dense bass). Right: the highs do (tames harshness) |
| FBK | parallel ("New York") compression: a smashed copy under the mix. Quiet detail comes up, peaks stay |
| RVB | air: new harmonics above 6 kHz, made from the highs (sparkle, not EQ) |
