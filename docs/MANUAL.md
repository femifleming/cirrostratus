# fletchfoorth

### *every segment its own instrument, for Stages*

**Version 1.0**

<!-- FIG overview -->

**fletchfoorth** is a new mode for Mutable Instruments **Stages**, built on
[BRIBESTAGES](https://github.com/thisisbribery/bribestages) (qiemem's bipolar
firmware plus segment linking, envelope followers and sync LFOs).

In fletchfoorth every one of the six segments is its own instrument. Tap a
segment's button and it becomes the next type:

| Light | Type | What it is |
|---|---|---|
| green | **Random** | slewed random voltages: 7 kinds |
| yellow | **Chaos** | patterned randomness: Turing loop, logistic map, Lorenz, cellular automaton, coin gates, dust |
| red | **Envelope** | attack *and* decay in one segment, with curve, loop and ratchets |
| green/red | **Oscillator** | 8 audio oscillators, including a 16-wave bank, additive, Gendy and a wave-rider |
| yellow/red | **Processor** | envelope follower, transient gate, slew, crush/fold, gesture looper |

Every type has several **flavours**, and every segment remembers its own
flavour, settings and extra options for each type.

Everything BRIBESTAGES does still works in its own modes. fletchfoorth is one
more mode next to them.

---

## 1. Installing

The module updates from an audio file. The updater is built in and can't be
damaged by a failed attempt, so you can always try again.

**You need:** a computer or phone with a headphone jack (a wired USB headphone
adapter works, Bluetooth doesn't), a 3.5 mm patch cable, and
`fletchfoorth-v1.0.wav` (or `fletchfoorth-v1.0-flipped.wav` for a module mounted
upside down).

<!-- FIG install -->

1. Power the rack off. Unplug every cable from the Stages.
2. Patch the headphone out into the **CV input of segment 6** (top row of jacks,
   rightmost).
3. Set **slider 6 to the middle**. It sets the input gain.
4. Hold **button 6** and power the rack on. Slider 6's light blinks green: the
   module is waiting for audio.
5. Set the volume high. Turn off sound effects, EQ and notifications.
6. Play the .wav from the start. The six button lights are a level meter:
   **3 or 4 lit is right**. Move slider 6 to get there.
7. Green slider lights run left to right while it loads (about 80 seconds).
8. When the file ends, the module restarts on the new firmware.

**All six button lights blink red:** the level was too low or the audio cut
out. Press button 6, raise slider 6, and play the file again from the start.

**Your settings:** BRIBESTAGES and qiemem settings (segment types, links,
mode) are kept. fletchfoorth keeps its own settings in a separate corner of
the memory, so switching modes never disturbs either. To go back, flash
BRIBESTAGES or qiemem the same way.

---

## 2. Getting into fletchfoorth

<!-- FIG enter -->

**Hold button 6 for 5 seconds.** The lights show the mode, and the module
starts in fletchfoorth. It remembers the mode through power cycles.

The first time, the segments are what you probably want anyway: **1–4 Random,
5 Envelope, 6 Oscillator**.

**To leave:** hold another button for 5 seconds, as in every Stages mode:

| Hold for 5 s | Mode |
|---|---|
| button 1 | Stages (segment generator) |
| button 2 | Advanced |
| button 3 | Slow LFO |
| button 4 | 6EG |
| button 5 | Ouroboros. **Hold button 5 again** while in Ouroboros: Ouroboros alternate (and back) |
| button 6 | **fletchfoorth** |

Ouroboros alternate used to have button 6; it is now behind a second hold of
button 5, so no mode was lost.

When you leave fletchfoorth, the other modes start fresh (their settings are
kept; a running loop or LFO simply restarts).

---

## 3. One segment, every control

<!-- FIG controls -->

Each segment works the same way, whatever its type:

| Control | Does |
|---|---|
| **Slider** (+ the CV jack) | the main control: rate, pitch, attack… (each type below) |
| **Knob** | the second control: slew, timbre, decay… |
| **Gate jack** | clock, trigger, sync, freeze or record, depending on the type |
| **Out jack** | the segment's output |
| **Button light** | the type (its colour), a bright flash on every tick or hit, breathing when the extra option is on |
| **Slider light** | the output level |

### The button

| Gesture | Does |
|---|---|
| **Tap** | next type: Random → Chaos → Envelope → Oscillator → Processor → Random |
| **Hold + move the slider** | choose the **flavour** (the slider is cut into as many zones as the type has flavours; bottom = 1) |
| **Hold + turn the knob** | set the type's **hidden setting** (amplitude, curve, fine tune, gain…). The slider light shows its value while you turn |
| **Hold ½ to 5 s, release** | toggle the type's **extra option** (bipolar, loop, voice, invert…) |
| **Hold 5 s** | mode change (section 2) |

While you hold a button, its slider and knob are frozen: moving them sets the
flavour and the hidden setting without changing the sound. When you let go,
they glide back to where they really are over up to a second.

Settings are saved when you let go of all buttons.

### Reading the flavour

<!-- FIG flavours -->

While you hold a button, its light shows the segment's flavour:

| Flavour | Light |
|---|---|
| 1 | green |
| 2 | yellow |
| 3 | red |
| 4 | green, blinking |
| 5 | yellow, blinking |
| 6 | red, blinking |
| 7 | green and red, alternating |
| 8 | yellow and red, alternating |

---

## 4. Random (green)

<!-- FIG random -->

Slewed random voltages, one per segment, each with its own kind of
randomness.

| Control | Does |
|---|---|
| Slider + CV | **rate**: 0.05 Hz at the bottom, about 2 Hz at noon, 100 Hz at the top |
| Gate jack | **clock**: with a cable in, a new value comes on every rising gate, and the slider becomes the **chance** of a new value (top = every clock, bottom = never: the value is locked) |
| Knob | **slew**: fully left = clean steps; turning right, each new value glides in over more of the time until the next one |
| Hold + knob | **amplitude** (output level), full by default |
| Extra option (hold ½ s) | **bipolar**: −5 to +5 V instead of 0 to 8 V |

| # | Flavour | What it does |
|---|---|---|
| 1 | **Uniform** | any value, all equally likely |
| 2 | **Gaussian** | mostly near the middle, sometimes far out |
| 3 | **Walk** | a random walk between two walls (Xenakis): it wanders and bounces instead of jumping |
| 4 | **Pink** | 1/f drift: slow wandering with occasional leaps, the way natural things move |
| 5 | **Quantised** | whole semitones, 0 to 5 V at 1 V/oct. Hold + knob sets the range: 1 to 5 octaves. Bipolar shifts it down 2.5 V |
| 6 | **Kin** | stays close to the segment on its left; the knob is how loosely (the slew is fixed and gentle) |
| 7 | **Orbit** | a point moving along a path. The knob morphs the path: circle → figure 8 → spiral → wandering. The slider is its speed. Side by side, Orbit segments read the same point: the first gives **X**, the next **Y**, the third the **distance** from the centre |

Four Random segments with different flavours make four voltages that move in
four different ways from one gesture of your hand.

---

## 5. Chaos (yellow)

<!-- FIG chaos -->

Randomness with a shape you can lock, push and listen to.

| Control | Does |
|---|---|
| Slider + CV | **rate** (0.05 to 100 Hz), or the gate jack as a **clock** |
| Knob | depends on the flavour (below) |
| Hold + knob | **amplitude** |
| Extra option | **bipolar** (not for the gate flavours) |

| # | Flavour | Knob | What it does |
|---|---|---|---|
| 1 | **Turing loop** | left = locked, middle = random, right = locked and flipping every pass | an 8-step loop of bits that you can freeze, mutate or break. Fully right it plays its 8 steps, then their opposite: a loop twice as long |
| 2 | **Logistic map** | r from 3.4 to 4.0 | one knob from order to chaos: fully left it settles into a 2-step pattern, a little to the right it doubles to 4, 8, 16 steps, and from about a quarter of the way it never repeats |
| 3 | **Lorenz** | ρ from 20 to 50 | the butterfly. Slider = speed. Fully left it slowly settles; from about a sixth of the way it never does. Side by side, Lorenz segments read one attractor: **X**, **Y**, **Z** |
| 4 | **Cellular** | the rule: 30, 90, 110, 150, 184, 45, 73, 54 | a cellular automaton, 8 cells per segment. Side by side, the segments share one ring of cells; each segment outputs its 8 cells as a stepped voltage. Clocked by the first segment of the row |
| 5 | **Coin gates** | chance of heads | a coin toss per clock: heads opens the gate for half a clock. Outputs 0 or 8 V (times the amplitude) |
| 6 | **Dust** | how random the heights are | triggers (5 ms) at random moments, no clock. The slider sets how many: from 0.1 to 50 a second (at the top, triggers closer than 5 ms merge, so about 40 land) |

---

## 6. Envelope (red)

<!-- FIG envelope -->

Attack and decay in one segment: no more patching two segments together.

| Control | Does |
|---|---|
| Slider | **attack**, 1 ms to 10 s |
| Knob | **decay**, 1 ms to 10 s |
| CV jack | **time**: +1 V makes both twice as fast, −1 V twice as slow |
| Gate jack | a **trigger** plays attack then decay; a **gate held** past the attack stays at the top until you let go, then decays |
| Hold + knob | **curve**. Noon: straight lines. Left: snappy, moving fast at the start and easing in. Right: slow to start, fast at the end |
| Extra option | **loop**: with no gate cable it cycles by itself; with a gate it loops while the gate is high |

Output: 0 to 8 V. Every new trigger starts the attack from wherever the
envelope is, so retriggering never clicks.

| # | Flavour | What it does |
|---|---|---|
| 1 | **Once** | one hit per trigger |
| 2 | **Twice** | a ratchet: two hits per trigger |
| 3 | **Three times** | three hits |
| 4 | **Four times** | four hits |

Ratchets play their hits back to back at the attack and decay you set; they
don't hold for a long gate.

---

## 7. Oscillator (green/red)

<!-- FIG oscillator -->

Audio oscillators, ±5 V. Several are inspired by the SDS Digital SDS VCO
(a bank of selectable waveforms, a built-in envelope and VCA, recorded
waveforms).

| Control | Does |
|---|---|
| Slider | **pitch**: C1 (33 Hz) at the bottom to C7 (2093 Hz) at the top |
| CV jack | **1 V/oct** pitch |
| Knob | **timbre** (per flavour, below) |
| Gate jack | **hard sync** (restarts the wave), or with the extra option the voice's gate |
| Hold + knob | **fine tune**, ±1 semitone (noon = in tune) |
| Extra option | **voice**: a built-in envelope and VCA. The gate opens it (2 ms attack), letting go fades it (0.3 s). With no gate cable the voice stays open |

| # | Flavour | Knob | Sound |
|---|---|---|---|
| 1 | **Morph** | sine → triangle → saw → square → narrow pulse | the classic shapes, smoothly in between, alias-reduced |
| 2 | **Wave bank** | scans 16 waveforms, crossfading | sine, triangle, saw, square, pulse 25 %, pulse 10 %, half sine, full-wave sine, pinched sine, 4 steps, 8 steps, formant, sync saw, frozen noise, sine + octave, organ |
| 3 | **Additive** | 1 to 16 harmonics | from a pure sine to a bright, saw-like stack. Harmonics above 14 kHz are left out, so high notes stay clean |
| 4 | **Pair** | second sine from unison to a fifth above | two sines; between them your ear hears a third, lower tone (difference tones, as in the Oneirophone) |
| 5 | **Fold** | fold depth | a sine through a wavefolder: from round to glassy |
| 6 | **FM** | modulation depth | the segment on the **left** is the modulator: another oscillator gives FM; a random or envelope bends the phase slowly. Segment 1 has no left neighbour: a plain sine |
| 7 | **Gendy** | how far the waveform wanders | Xenakis' dynamic stochastic synthesis: 12 points whose heights and spacing take a random walk every cycle. Left: a steady tone; right: a living, unstable one |
| 8 | **Rider** | how long one rewrite takes (0.05 to 10 s) | the wave-rider: it records the segment on its **left** into its own waveform as it plays (segment 1 records its CV jack). The gate held high freezes the waveform |

---

## 8. Processor (yellow/red)

<!-- FIG processor -->

The CV jack becomes an input. Stages reads it about 3,900 times a second, so
audio comes in lo-fi: that's part of the sound. Here the slider works alone
(the CV jack is the input, not a modulation).

| # | Flavour | Slider | Knob | Hold + knob | Gate | Extra option |
|---|---|---|---|---|---|---|
| 1 | **Follow** | release, 5 ms to 5 s | attack, 0 to 250 ms | input gain ×1 to ×8 | high = freeze | invert (0 to −8 V) |
| 2 | **Transient** | threshold (bottom = most sensitive) | trigger length, 2 to 100 ms | input gain ×1 to ×8 | – | invert (5 V, dropping to 0 on hits) |
| 3 | **Slew** | rise time, 0 to 5 s | fall time, 0 to 5 s | – | high = hold | invert |
| 4 | **Crush/fold** | drive ×1 to ×8 into a folder | rate: each reading held 1 to 32 times (3.9 kHz down to 122 Hz) | output level | high = freeze | invert |
| 5 | **Looper** | speed ¼× to 4× (noon = 1×) | smoothing | – | high = **record**, low = play | backwards |

- **Follow** is the BRIBESTAGES envelope follower: 0 to 8 V; at ×1 a 5 V audio
  peak gives full output.
- **Transient** fires a 5 V trigger when the input jumps (a drum hit, a
  pluck), at most one every 48 ms. Patch a drum loop in and trigger
  envelopes from it.
- **Looper** records the CV jack 64 times a second, up to 12 seconds, while
  the gate is high, then loops it when the gate goes low. Before anything is
  recorded, and while recording, the input passes straight through. A new
  gate records over it.

---

## 9. Neighbours

<!-- FIG neighbours -->

Some flavours listen to the segment on their **left**, or work as a team
when neighbours share a type **and** a flavour:

| Flavour | With its neighbours |
|---|---|
| Random 6 **Kin** | follows the segment on its left, loosely |
| Random 7 **Orbit** | side by side: X, Y, distance from the centre of one moving point |
| Chaos 3 **Lorenz** | side by side: X, Y, Z of one attractor |
| Chaos 4 **Cellular** | side by side: one ring of cells, 8 per segment |
| Oscillator 6 **FM** | the left segment is its modulator |
| Oscillator 8 **Rider** | records the left segment as its waveform |

A row starts at its leftmost segment: that one's slider, knob and gate run
the row.

---

## 10. Recipes

**Your usual set.** Segments 1–4 Random (tap until green), 5 Envelope, 6
Oscillator. For a bipolar random, hold its button ½ s. For four different
motions, give each a different flavour: Uniform, Walk, Pink, Orbit.

**A whole voice from two segments.** Segment 5 Random, flavour 5 (Quantised)
→ patch its out into **CV 6**. Segment 6 Oscillator with the voice option on,
a clock into **gate 5 and gate 6**. Every clock: a new note, played through
the built-in VCA.

**Drums triggering envelopes.** A drum loop into **CV 4**, segment 4
Processor, flavour 2 (Transient). Its out into **gate 5**, segment 5 an
Envelope with a short decay. The envelope now follows the kick.

**The butterfly.** Segments 1–3 Chaos, flavour 3 (Lorenz) each. Outputs 1–3 are
X, Y and Z of one attractor: three related, never-repeating voltages.

**A wave drawn by chance.** Segment 5 Random (Walk, slew up), segment 6
Oscillator flavour 8 (Rider). The random's motion becomes the oscillator's
waveform, slowly changing as it rides.

**FM bells.** Segment 5 Oscillator (Morph, a sine), segment 6 Oscillator
flavour 6 (FM), both tuned with their sliders. Knob 6 is the FM depth.

**Play it back.** A hand-played CV (a joystick, a ribbon) into **CV 4**,
segment 4 Processor flavour 5 (Looper). Hold a gate high while you play, let
go: your gesture loops.

---

## 11. Under the hood

| | |
|---|---|
| Output | 1.0 = 8 V; randoms 0 to 8 V or ±5 V; oscillators ±5 V; gates 8 V (times the amplitude) |
| Processing | 31,250 samples a second per segment; CV and knobs read 3,906 times a second |
| CPU | measured on an emulated Cortex-M4: 588 instructions per sample for the default set, 1,125 for the heaviest (six additive oscillators); the chip has 2,304 cycles per sample |
| Memory | fletchfoorth uses the memory the normal Stages engines use; they rest while it runs |
| Settings | stored in their own flash pages (the last 4 KB), untouched by firmware updates and by the other modes |
| Chaining | each module runs its own six segments; fletchfoorth doesn't chain |
| Tests | 111 host tests on the real engine code (pitch, V/oct, envelope times, rates, ranges, every flavour) and a check that the update file decodes to exactly the firmware |

fletchfoorth has so far been tested on the computer and an emulated
processor, not yet on a real Stages.

---

## 12. Thanks and licences

- **Mutable Instruments / Émilie Gillet**: Stages, stmlib and the audio
  bootloader (MIT).
- **qiemem**: the bipolar multi-mode firmware this is built on (MIT).
- **BRIBESTAGES**: segment linking, envelope followers and sync LFOs (MIT).
- **SDS Digital SDS VCO**: the inspiration for the wave bank, the voice option
  and the wave-rider.
- **Iannis Xenakis**: the random walk and Gendy.

fletchfoorth is MIT-licensed.
