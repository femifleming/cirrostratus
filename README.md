# BRIBESTAGES

Formerly Qiemem Bribery Edition. Custom firmware for the Mutable Instruments Stages module. It's [qiemem's bipolar multi-mode firmware](https://github.com/qiemem/eurorack/tree/bipolar/stages) v1.3.0 with three additions:

- **Segment linking:** you choose which segments form an envelope with the buttons, and patching cables no longer changes your groups.
- **Follow segments:** an envelope follower. Patch audio into a segment and its output follows the loudness.
- **Sync LFO segments:** link several and each one becomes its own LFO, all locked to one clock in the first segment. Every segment picks its own division or multiplication, shape and phase.

Everything qiemem's firmware does still works.

**Web page:** [thisisbribery.com/bribestages](https://www.thisisbribery.com/bribestages/), where you can also play the update audio straight from the page.

**Support:** find it useful? [Buy me a coffee on Ko-fi](https://ko-fi.com/thisisbribery).

## Download

Get the firmware from the [v1.0 release](https://github.com/thisisbribery/bribestages/releases/tag/v1.0) or [firmware/](firmware/), or right-click a link and choose Save Link As:
- [qiemem-bribery-edition-v1.0.wav](https://github.com/thisisbribery/bribestages/raw/main/firmware/qiemem-bribery-edition-v1.0.wav), for a module mounted normally.
- [qiemem-bribery-edition-v1.0-flipped.wav](https://github.com/thisisbribery/bribestages/raw/main/firmware/qiemem-bribery-edition-v1.0-flipped.wav), for a module mounted upside down.

## Why

On stock Stages, the gate jacks decide how segments group together: a segment with a cable in its gate jack starts a new group. That's fine in the studio. Playing live, I kept losing track of which gate jacks I'd patched, and a cable going in or out in the middle of a set would split or merge my envelopes without warning. Linking lets you set the groups with the buttons, so the patch can change without the envelopes changing.

I also wanted an envelope follower in the case without giving up a module for one. Stages segments can already read audio through their CV jacks, so any segment can now be one.

## Install

The module updates from an audio file. Its updater is built in and can't be damaged by a failed attempt, so you can always try again.

**You need:** a computer or phone with a headphone jack (a wired USB headphone adapter works, Bluetooth doesn't) and a 3.5 mm patch cable.

1. Power the rack off. Unplug every cable from the Stages.
2. Patch the headphone out into the **CV input of the rightmost segment** (top row of jacks).
3. Set the **rightmost slider to the middle**. It sets the input gain.
4. Hold the **rightmost button** and power the rack on. The rightmost slider light blinks green: the module is waiting for audio.
5. Set the volume high. Turn off sound effects, EQ and notifications. Unplug speakers and headphones from the computer: the update sound is harsh.
6. Play the .wav from the start. The six button lights are a level meter: **3 or 4 lit is right**. Move the rightmost slider to get there.
7. Green slider lights run left to right while it loads. It takes about 80 seconds.
8. When the file ends, the module restarts on the new firmware.

**If all six button lights blink red:** the level was too low or the audio cut out. Press the rightmost button, raise the gain with the rightmost slider, and play the file again from the start. If the module doesn't restart when the file ends, power off and start again from step 4.

**Upside-down module:** use the `-flipped` file. Everything is mirrored as you look at it: patch into the leftmost segment's CV input, hold the leftmost button, set the level with the leftmost slider.

**Your settings:** coming from qiemem v1.3.0, your segment types and mode are kept. Coming from stock Mutable firmware, settings reset, as with any qiemem install. To go back, flash [qiemem's own v1.3.0 release](https://github.com/qiemem/eurorack/releases) the same way.

## Segment linking

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

## Follow segments

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

## Sync LFO segments

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

## Everything else

All of qiemem's features are unchanged: bipolar segments, slow and audio-rate LFOs, re-trigger control, 6EG mode, Ouroboros and the rest. Their docs are in [stages/README.md](stages/README.md), with a cheat sheet in [stages/resources/](stages/resources/).

## What's in this repo

| Path | What |
|---|---|
| `firmware/` | The .wav files to flash. |
| `stages/` | Stages firmware source: qiemem v1.3.0 plus these changes. |
| `stmlib/`, `tides2/ramp/`, `plaits/` | The Mutable library code Stages needs to build. |
| `tools/qpsk.py` | Turns a firmware .bin into an update .wav, and checks one. |

The changed files, against qiemem v1.3.0:
- `stages/chain_state.cc/.h`: link and unlink gestures, groups set by links instead of gate jacks, free-running groups, Follow and Sync LFO in the type cycle, followers stand alone, Sync LFO groups.
- `stages/segment_generator.cc/.h`: the envelope follower, and the Sync LFO clock and shapes.
- `stages/settings.h`: links and Follow are stored in two spare bits of each segment's settings, so qiemem settings carry over. A Sync LFO is the Follow bit with a different type.
- `stages/ui.cc/.h`: link flashes, the two-button mode-change guard, Follow lights and gain, Sync LFO lights and phase.
- `stages/stages.cc`: free-run gate, follower input and lights, Sync LFO lights.
- `stages/io_buffer.h`, `stages/cv_reader.cc`: an unsmoothed CV reading for the follower.
- `stages/test/links/`: host tests.

This version also fixes two bugs that were in the first linking build on stock firmware. A loop change could be missed when it arrived a moment late, and a free-running group could stop after you changed one of its segments.

## Build

Needs the [Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (tested with 14.2.rel1) and Python 3 with numpy.

```
touch stages/resources.cc stages/resources.h
make -f stages/makefile TOOLCHAIN_PATH=<toolchain>/
make -f stages/makefile TOOLCHAIN_PATH=<toolchain>/ build/stages/stages.bin
make -f stages/makefile TOOLCHAIN_PATH=<toolchain>/ FLIPPED=1
make -f stages/makefile TOOLCHAIN_PATH=<toolchain>/ FLIPPED=1 build/stages-flipped/stages-flipped.bin
python3 tools/qpsk.py encode build/stages/stages.bin out.wav
python3 tools/qpsk.py decode out.wav check.bin    # expect bad=0
```
The `touch` skips regenerating the lookup tables, which needs Python 2. The first `make` without a target generates the dependency files.

Host tests, no hardware needed:
```
sh stages/test/links/run.sh
```
They run the real firmware code with simulated buttons, gates and audio.

## Credits and licence

- Stages and its code: Emilie Gillet, Mutable Instruments.
- Multi-mode firmware: joeSeggiola. Bipolar firmware: qiemem.
- Linking and Follow: Bribery.

Code is MIT licensed, see [LICENSE](LICENSE). This is an unofficial firmware, not made or supported by Mutable Instruments.
