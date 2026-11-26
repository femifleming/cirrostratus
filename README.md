# fletchfoorth

**Every segment its own instrument, for Mutable Instruments Stages.**

fletchfoorth is a new mode for Stages in which every segment is its own instrument. Tap a button and the segment becomes the next type:

| Light | Type | Flavours |
|---|---|---|
| green | **Random** | uniform, gaussian, Xenakis walk, pink 1/f, quantised, kin, orbit, weather |
| yellow | **Chaos** | Turing loop, logistic map, Lorenz, cellular automaton, coin gates, dust, stair row |
| red | **Envelope** | attack *and* decay in one segment, with curve, loop and 1 to 4 ratchets |
| green/red | **Oscillator** | morph, 16-wave bank, additive, difference pair, fold, FM from the left, Gendy (rows share one waveform), wave-rider; built-in VCA voice option |
| yellow/red | **Processor** | CV: envelope follower, transient gate, slew, crush/fold, gesture looper, quantiser (Sleepcousonique's 38 scales), sample/track and hold, comparator/window |
| green/yellow | **Effect** | audio, the left neighbour at the full rate: filter, VCA/low-pass gate, ring mod, comb, chorus/flanger, echo, drive/fold, lo-fi |

Hold a button and move the slider to pick the flavour, turn the knob to set its hidden setting, hold half a second for its extra option (bipolar, loop, voice, invert). Hold a button and tap the one on its left for a **cord** from that segment into this one's CV input, Sleepcousonique's kinds: hall, fog, water, door, range, maybe. Hold buttons 1 and 2 for 2 s for **dream drift**: left alone for a minute, the panel slowly wanders on a Lorenz attractor until you touch it. Neighbours can team up: Orbit and Lorenz rows read one shared point or attractor, cellular automata share one ring of cells, FM and the wave-rider listen to the segment on their left.

It is built on [BRIBESTAGES](https://github.com/thisisbribery/bribestages) (qiemem's bipolar multi-mode firmware plus segment linking, envelope followers and sync LFOs). All of that still works in its own modes; fletchfoorth is one more mode, on **button 6** (hold 5 s). Ouroboros alternate moved to a second hold of button 5.

**Manual:** [`docs/fletchfoorth-manual.pdf`](docs/fletchfoorth-manual.pdf), with a drawing of the panel for every section (also readable as [`docs/fletchfoorth-manual.md`](docs/fletchfoorth-manual.md)).

> Tested on the host (713 checks on the real engine code) and on an emulated Cortex-M4 for CPU, and the update file is checked to decode to exactly the firmware. **Not yet run on a real Stages.**

## Download

[`firmware/fletchfoorth-v1.3.wav`](firmware/fletchfoorth-v1.3.wav) for a module mounted normally, [`firmware/fletchfoorth-v1.3-flipped.wav`](firmware/fletchfoorth-v1.3-flipped.wav) upside down (SHA-256 in `firmware/SHA256SUMS.txt`). Also on the [releases page](../../releases).

## Install

The module updates from an audio file; the updater can't be damaged by a failed attempt.

1. Power the rack off. Unplug every cable from the Stages.
2. Patch a headphone out (wired; not Bluetooth) into the **CV input of segment 6** (top row, rightmost).
3. Set **slider 6 to the middle** (input gain).
4. Hold **button 6** and power on. Slider 6's light blinks green.
5. Volume high; sound effects, EQ and notifications off.
6. Play the .wav from the start. The button lights are a level meter: **3 or 4 lit is right**.
7. Green slider lights run left to right (about 80 s); the module restarts on the new firmware.

All six button lights blinking red: press button 6, raise slider 6, play again from the start. Upside-down module: use the `-flipped` file, mirrored (leftmost segment). Then **hold button 6 for 5 seconds** to enter fletchfoorth.

Your BRIBESTAGES / qiemem settings are kept; fletchfoorth stores its own settings separately, in the last 4 KB of flash. (Coming from fletchfoorth 1.0: its settings start fresh once, they grew. 1.1 → 1.3 keeps them.)

## Build

Needs the Arm GNU toolchain and Python 3.

```
touch stages/resources.cc stages/resources.h
make -f stages/makefile TOOLCHAIN_PATH=<toolchain>/
make -f stages/makefile TOOLCHAIN_PATH=<toolchain>/ build/stages/stages.bin
python3 tools/qpsk.py encode build/stages/stages.bin fletchfoorth.wav
python3 tools/qpsk.py decode fletchfoorth.wav check.bin    # expect bad=0
```
Add `FLIPPED=1` for the upside-down build.

**Tests** (no hardware): `make -f stages/test/dobbel.mk test` runs the engine on the computer with sanitizers (713 checks): pitch and 1 V/oct, envelope times, ratchets, loop, random rates, ranges and clocked chance, orbit, kin, Turing, logistic, Lorenz, coin, dust, cellular, follower, transient, slew, looper, crush, FM, rider, the wave bank and the oscillators' alias levels; the scales to the cent, hold, comparator, filter, VCA, ring, comb pitch, echo time, every cord, the stair, the weather, Gendy rows, dream drift, and a storm of every type, flavour and option with cords on and the controls thrown.

**CPU:** `tools/bench/run.sh` runs the engine on QEMU's Cortex-M4F and prints instructions per sample for six segments of every flavour (worst 1156, default layout 651; Stages has 2304 cycles per sample, and from flash it needs more than one per instruction: if the module ever misses a block, its button lights flicker red).

**Wave bank:** `python3 tools/make_bank.py` writes `stages/dobbel_bank.cc` (16 waves, band-limited per octave, 8 to 16 points per harmonic, in flash).

**Manual:** `python3 tools/manual/make_manual.py` (Pillow, `markdown`, Chrome) draws the panel figures and builds the PDF from `docs/MANUAL.md`.

## What changed, against BRIBESTAGES v1.1

| File | What |
|---|---|
| `stages/dobbel.h/.cc` | the fletchfoorth engine: all types and flavours, hardware-free |
| `stages/stages.cc` | the mode's process function; the engine lives in the idle segment generators' memory and they are re-initialised when you leave |
| `stages/settings.h/.cc` | fletchfoorth's settings in their own flash pages (the State chunk keeps its size, so nobody's settings reset) |
| `stages/ui.cc/.h` | the mode on button 6, Ouroboros alternate on a second hold of button 5, fletchfoorth's buttons and lights |
| `stages/modes.h` | `MULTI_MODE_DOBBEL` |
| `stages/test/dobbel_test.cc`, `tools/bench/`, `tools/manual/` | tests, CPU benchmark, manual |

## BRIBESTAGES features (unchanged, in their own modes)

### Segment linking

Linking works in the three segment generator modes (Stages, Advanced, Slow LFO) with a single module. With two Stages chained, or in the 6EG and Ouroboros modes, everything behaves as stock qiemem. Your links stay stored and come back when you return to a segment generator mode.

| Gesture | Result |
|---|---|
| Hold A, tap B (not linked yet) | Links every segment from A to B. Triple green flash. Works right to left too. |
| Hold A, tap B (already linked) | Sets a loop from A to B, same as stock. |
| Hold A, hold B for 2 s | Unlinks: every segment from A to B stands alone. Triple red flash. Unlinking the middle of a group splits it in two. |
| Short press | Changes segment type. The link stays. |
| Hold 0.5 s, release | Toggles loop (qiemem timing). |
| Hold 5 s, one button | Mode change, as in qiemem. With two buttons held, mode change is blocked, so a long link gesture can't switch modes by accident. |
| Hold + slider or pot | qiemem's property changes, unchanged. |

**Display:** linked segments do a quick triple yellow flash every 4 seconds, over their normal colours.

**Gates and outputs:**
- A group comes out of its leftmost segment's output.
- The leftmost segment's gate fires the whole group.
- Gates patched into the other segments of a group are ignored. Patching or unpatching them doesn't split the group or restart it.
- A segment that isn't linked follows its own gate.

**No gate in the first segment:** a looped group runs by itself, like a multi-stage LFO. A group without a loop waits for a gate.

### Follow segments

Patch audio into a segment's CV jack and the segment's output becomes the envelope of that audio.

**Making one:** tap the segment's button past the last type.
- Stages and Slow LFO modes: ramp, step, hold, **follow**, sync LFO, back to ramp.
- Advanced mode: ramp, step, hold, random, **follow**, sync LFO, back to ramp.

Follow works where linking works. In other modes, a stored Follow segment runs as a ramp.

| Control | Does |
|---|---|
| CV jack | Audio in. DC is blocked, so an offset doesn't read as level. |
| Slider | Release, 5 ms at the bottom to 5 s at the top. |
| Pot | Attack, instant fully left to 250 ms fully right. |
| Hold button + move slider | Input gain in four zones, bottom to top: x1, x2, x4, x8. At x1, a 5 V audio peak gives full output. Use x4 or x8 for line-level sources. |
| Hold button + turn pot | Invert: the envelope goes negative, 0 V down to -8 V. Same gesture as qiemem's bipolar toggle. |
| Gate jack | High freezes the envelope where it is. Low lets it follow again. |
| Output | 0 to 8 V, or 0 to -8 V when inverted. |

**Lights:**
- The button light is a level meter: green, then yellow, then red as the input gets louder. With no sound it breathes dim yellow, or dim red when inverted.
- While you set the gain it shows the zone: green x1, yellow x2, red x4, red and green alternating x8.
- The slider light follows the output.

**With links:** a Follow segment always stands alone. Links into or out of it are ignored while it's a follower, but they stay stored and come back when you change its type. Hold + tap across a follower does nothing. Holding a follower's button for 0.5 s does nothing, because a follower has no loop.

**Limits:**
- The jack is read 3906 times a second, so pure sine tones at exact multiples of 1953 Hz read low. Real audio almost never sits there.
- With a very short release, low notes ripple: a 50 Hz bass makes the envelope wobble at 100 Hz. Raise the release to smooth it.

### Sync LFO segments

One clock in, a bank of LFOs out. Make a few segments Sync LFOs and link them: each segment then runs its own LFO from its own output, and all of them are locked to the clock patched into the first segment's gate. Set one to the clock, one to x2, one to /4 and they stay in step for as long as the clock runs.

**Making one:** tap the segment's button past Follow.
- Stages and Slow LFO modes: ramp, step, hold, follow, **sync LFO**, back to ramp.
- Advanced mode: ramp, step, hold, random, follow, **sync LFO**, back to ramp.

Then link them as usual: hold the first, tap the last. Link first and change the types after if you like: the stored links come back once both neighbours are Sync LFOs.

**Example:** clock into segment 1's gate. Make segments 1 to 4 Sync LFOs, hold button 1 and tap button 4. Four LFOs come out of outputs 1 to 4, all on that clock.

| Control | Does |
|---|---|
| Gate jack, first segment of the group | Clock in. Each rising edge is one clock period. |
| Gate jack, other segments | Ignored. |
| Slider (plus CV) | Rate against the clock: /32, /16, /8, /6, /4, /3, /2 in the lower half, x1 in the middle, x2, x3, x4, x6, x8, x16, x32 in the upper half. |
| Pot | Shape, morphing smoothly from sine to triangle to saw (falling) to ramp (rising) to square to random. Random picks a new level every cycle and holds it. |
| Hold button + turn pot | Bipolar: -5 V to +5 V instead of 0 to 8 V. Same gesture as qiemem's bipolar toggle. |
| Hold button + move slider | Phase offset in four zones, bottom to top: 0, 90, 180, 270 degrees. Two LFOs at the same rate, one at 90, give you a quadrature pair. |
| Output | That segment's LFO: 0 to 8 V, or -5 V to +5 V bipolar. |

**Clock:**
- With no clock, or when the clock stops, the LFOs keep going at the last tempo. Before any clock has arrived it's 120 BPM (2 Hz).
- When the clock starts again after stopping for 4 beats or more, every LFO restarts from the top, so divisions line up with the first clock of the new run.
- A tempo change is followed from the next clock.
- A segment that isn't linked is a group of one: its own gate is its clock.

**Lights:**
- The button flashes at the start of every cycle of that LFO: green when it multiplies the clock, yellow at x1, red when it divides. Between flashes it's dark, or a dim glow when bipolar.
- While you set the phase it shows the zone: green 0, yellow 90, red 180, red and green alternating 270.
- The slider light follows the LFO.
- Linked groups do the usual triple yellow flash.

**With links:** a Sync LFO only links to other Sync LFOs. Hold + tap from a Sync LFO to any other type does nothing, and a stored link between a Sync LFO and another type is ignored but kept. Holding a Sync LFO's button for 0.5 s does nothing: it has no loop.

**Limits:** Sync LFOs work where linking works (Stages, Advanced and Slow LFO modes, one module). In other modes, or with two Stages chained, a stored Sync LFO runs as a ramp.


## Credits

- **Mutable Instruments / Émilie Gillet**: Stages, stmlib, the audio bootloader (MIT).
- **qiemem** (and joeSeggiola): the bipolar multi-mode firmware (MIT).
- **BRIBESTAGES** by Bribery: linking, Follow and Sync LFO segments (MIT).
- **SDS Digital SDS VCO**: inspiration for the wave bank, the voice option and the wave-rider.

MIT-licensed, see [LICENSE](LICENSE).
