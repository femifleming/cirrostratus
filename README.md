# Cirrostratus ☁

Alternative firmware for three Eurorack modules, in one place.

| Folder | Module | Firmware | Version |
|---|---|---|---|
| [`cirrostratus-monsoon/`](cirrostratus-monsoon/) | [Monsoon](https://github.com/jakplugg/Monsoon) (Jak Plugg's 12HP Clouds variant) | **Cirrostratus**: 30 modes in two banks, a 48 kHz floating-point engine, a clean output section | 1.5.3 |
| [`cirrostratus-stages/`](cirrostratus-stages/) | Mutable Instruments **Stages** | **Cirrostratus Stages**: a mode in which every segment is its own instrument (randoms, chaos, envelopes, oscillators, CV processors, audio effects, cords, dream drift), built on BRIBESTAGES | 1.3.3 |
| [`cirrostratus-monsoon/typhoon/`](cirrostratus-monsoon/typhoon/) | Calsynth **Typhoon** (the Monsoon's bigger sibling, Grayscale Supercell layout) | **Cirrostratus for the Typhoon**: the same 30 modes and engine, with the Typhoon's panel: IN and OUT meters, IN/OUT MUTE, TIME and BANK, AUX random voltage. Not yet run on a Typhoon | 1.5.3 |

## Manuals

| | PDF manual |
|---|---|
| Monsoon | [`Cirrostratus-Manual.pdf`](cirrostratus-monsoon/docs/Cirrostratus-Manual.pdf) |
| Typhoon | [`Cirrostratus-Typhoon-Manual.pdf`](cirrostratus-monsoon/docs/Cirrostratus-Typhoon-Manual.pdf) |
| Stages | [`Cirrostratus-Stages-Manual.pdf`](cirrostratus-stages/docs/Cirrostratus-Stages-Manual.pdf) |

The manuals explain every mode, control, option and light in full; the lists below are just a map.

## Monsoon and Typhoon: the 30 modes

Two banks of 15 (pick one by holding DISP / TIME and moving POSITION). Every mode uses the four sliders plus the
BAL / STEREO, FBK / FEEDBACK and RVB / REVERB knobs as its own extra controls.

| | Bank 1 | | Bank 2 |
|---|---|---|---|
| 1 | **SPRING**: a spring reverb tank | 16 | **QUELL**: resonance suppressor |
| 2 | **SPECTRAL**: spectral freeze / resynthesis, with SMEAR | 17 | **LEITNER**: spatial path panner |
| 3 | **CONVOLVE**: freeze captures the impulse response | 18 | **TANTRI**: tabla and tanpura |
| 4 | **RESONATOR**: sympathetic strings with jawari | 19 | **HARMONIZER**: scale harmonizer |
| 5 | **DIFFERENCE**: difference tones | 20 | **PUTZ**: spectral cleaner |
| 6 | **CROSS**: left/right spectral morph | 21 | **ENSEMBLE**: string-machine ensemble |
| 7 | **STRETCH**: extreme time-stretch | 22 | **BARBERPOLE**: frequency-shift spirals |
| 8 | **SPREEK**: voice | 23 | **SPECDELAY**: spectral delay |
| 9 | **CAPTURE**: captured wavetable voice | 24 | **CHOIR**: formant choir |
| 10 | **GENDY**: dynamic stochastic synthesis | 25 | **FOLDER**: wavefolder + low-pass gate |
| 11 | **SHIMMER**: shimmer cascade | 26 | **HALL**: big reverb |
| 12 | **TAPE**: multi-head tape echo | 27 | **BLOOM**: swells and reverse blooms |
| 13 | **PLATES**: sympathetic plates | 28 | **GAMELAN**: metallophones |
| 14 | **CHLOE**: the Chloaelis strip | 29 | **MIRROR**: spectral warping |
| 15 | **FALTUNG**: eight rooms, a built-in space library | 30 | **POLISH**: mastering chain |

Also on every mode: freeze, four save slots with slot morphing, an envelope follower, buffer quality and FFT size
settings, and a clean output section (hard stereo, mono bass, limiter).

## Stages: the segment types

In Cirrostratus Stages every one of the six segments is its own instrument. Tap a segment's button to choose its
type; hold it and move the slider to choose the flavour.

| Type | Flavours |
|---|---|
| **Random** | uniform, gaussian, Xenakis walk, pink 1/f, quantised, kin, orbit, weather |
| **Chaos** | Turing loop, logistic map, Lorenz, cellular automaton, coin gates, dust, stair row |
| **Envelope** | attack and decay in one segment, with curve, loop and 1 to 4 ratchets |
| **Oscillator** | morph, 16-wave bank, additive, difference pair, fold, FM from the left, Gendy, wave-rider (built-in VCA option) |
| **Processor** | envelope follower, transient gate, slew, crush/fold, gesture looper, quantiser (38 scales), sample/track and hold, comparator/window |
| **Effect** | filter, VCA / low-pass gate, ring mod, comb, chorus/flanger, echo, drive/fold, lo-fi |

Plus cords (one segment feeding the next without a cable) and dream drift (the panel slowly wanders when left alone).

Each folder is a complete project with its own README, manual, tests and
build: open the folder and follow its README. The update files (`.wav`, played
into the module) and the manuals are on the
[releases page](https://github.com/femifleming/cirrostratus/releases).

Until November 2026 the Stages firmware was called fletchfoorth and lived in
its own repository; its whole history, back through BRIBESTAGES,
qiemem's firmware and Mutable Instruments' original code, is part of this
one. Both are MIT licensed (see each folder's LICENSE).
