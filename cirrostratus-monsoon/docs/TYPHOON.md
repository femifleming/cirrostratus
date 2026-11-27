# Cirrostratus on the Typhoon

The Typhoon (Calsynth; designed by Jak Plugg) is a Clouds variant built on the
Grayscale Supercell/Microcell layout: an STM32F427 and the same WM8731
codec as the Monsoon. Its stock firmware is SuperParasites.

`typhoon/` is a port of Cirrostratus to it:

- **Shared:** the DSP is the Monsoon build's, unchanged (`monsoon/dsp`). That
  means the same 30 modes, the same engine at 48 kHz in 32-frame blocks, the
  same RAM budget and the same save-slot format.
- **New:** only the panel code is new (`typhoon/`): drivers, CV scaling,
  settings, UI, main and bootloader.

The Monsoon manual (docs/Cirrostratus-Manual.md) applies to the modes.
This page covers what is different on the Typhoon.

**Status:** it builds and is checked in software (below). It has **not yet
been run on a Typhoon**.

## Build

```sh
make -f typhoon/makefile                     # build/cirrostratus_typhoon/cirrostratus_typhoon.bin
make -f typhoon/makefile wav                 # build/cirrostratus_typhoon/cirrostratus-typhoon.wav
make -f typhoon/makefile flash               # SWD: application only (keeps bootloader, settings, slots)
make -f typhoon/bootloader/makefile && make -f typhoon/makefile combo
                                             # bootloader + application for a blank chip
make -f typhoon/makefile flash_all           # SWD: write that combo image (wipes settings)
host/build/bootloader_sim build/cirrostratus_typhoon/cirrostratus-typhoon.wav \
    build/cirrostratus_typhoon/cirrostratus_typhoon.bin   # decode the WAV with the bootloader code
```

### Memory layout

The application runs from 0x08008000 and must end by 0x0807FFFF. That is
480 KB in flash bank 1, the same region as the Monsoon build. Above it:

- **Sectors 8-11** hold the four save slots. The stock firmware keeps its
  slots there too.
- **Sector 1** holds the settings. Again, this is where the stock firmware
  keeps its own.

The image must never cross 0x08100000, because the stock audio bootloader
only erases sectors 0-11.

### Chip settings

- **Device define:** the build uses the F405 device define
  (`STM32F40_41xxx`), as the stock code does. It covers every peripheral
  used here.
- **Clock:** 168 MHz.
- **RAM:** the linker script gives 192 KB of SRAM and 64 KB of CCM. The
  engine still gets the Monsoon's 96 KB + 64 KB.

## Install

1. With the rack off, connect your computer's line out to **IN L**. IN R is
   normalled to IN L.
2. Hold **FREEZE** (the white button) and power on. The FREEZE LED blinks.
3. Play `cirrostratus-typhoon.wav` from the start. It lasts 6 min 26 s.
   Use about ¾ volume and IN LEVEL at about noon.

While it plays, the LEDs show the stock bootloader's states:

| What you see | Meaning |
|---|---|
| FREEZE blinks fast, the meters show the level | receiving |
| TIME and BANK LEDs all lit | a block is being written |
| TIME and BANK LEDs blinking | error. Press FREEZE, fix the level, and play the file again from the start |

This uses the bootloader already on the module: the stock SuperParasites
one, which is the same Mutable QPSK bootloader at the same address. Our own
`typhoon/bootloader` behaves the same way and is only needed for a blank
chip (the `combo` image).

**What carries over from SuperParasites:**

- **Calibration:** its V/OCT calibration (`pitch_offset` / `pitch_scale`)
  is imported automatically. Its 0 V CV offsets are imported too, but only
  if every one of them reads as a plausible 0 V. They are garbage if its
  AUX noise was on during calibration.
- **Save slots:** its slots are not loaded. The engine checks its own slot
  header and ignores anything else.

**Going back:** flash the stock firmware the same way.

## Controls

The slider LEDs show the CVs; that is done in hardware, as on the Monsoon.
The following are purely analog and not seen by the firmware:

- IN LEVEL and the IN VCA
- OUT LEVEL and the OUT VCA. The firmware does read the OUT VCA's control
  voltage, but only to scale the OUT meter.

Every slider or knob is summed with its CV jack in software:
value = pot + 2 × CV, where the CV is the jack's voltage relative to its
calibrated 0 V reading. So ±5 V covers the whole range, and positive
voltages turn the control up. This is the stock Microcell scaling.

| Typhoon | Cirrostratus | Notes |
|---|---|---|
| POSITION slider + CV | POSITION | In the mode menu, only the slider picks the mode; the CV is ignored there |
| DENSITY slider + CV | DENSITY | |
| SIZE slider + CV | SIZE | |
| TEXTURE slider + CV | TEXTURE | |
| TUNE knob | PITCH | ±48 semitones with soft semitone snapping, as on the Monsoon |
| V/OCT | PITCH | calibrated |
| FREQ CV | PITCH | ±5 V = ±12 semitones (as stock) |
| WET knob + CV | WET | |
| STEREO knob + CV | BAL | the mode's own control, plain at noon |
| FEEDBACK knob + CV | FBK | the mode's own control, plain at zero |
| REVERB knob + CV | RVB | the mode's own control, plain at zero |
| FREEZE button | FREEZE | toggles on release, as on the Monsoon |
| HOLD jack | FREEZE gate | the gate inverts the button state, as on the Monsoon |
| TRIG jack | TRIG | |
| TRIG button | TRIG | acts as the jack while held (TRIG input level) and fires one trigger per press |
| TIME | DISP | every DISP gesture |
| BANK | WRITE | every WRITE gesture |
| IN MUTE | input mute | new on the Typhoon (below) |
| OUT MUTE | output mute | new on the Typhoon (below) |
| AUX | AUX normal | analog (below) |

### Gestures

These are the Monsoon's gestures, with DISP = TIME and WRITE = BANK:

| Gesture | Does |
|---|---|
| tap TIME | IN/OUT meters page ↔ knobs page |
| hold TIME | mode menu. POSITION picks the mode, tap BANK flips the bank, tap FREEZE steps the option, release TIME applies |
| hold TIME + hold BANK 1 s (in the menu) | V/OCT calibration |
| hold TIME, tap FREEZE | FFT size page |
| tap BANK | load page. TIME picks the slot, BANK loads, FREEZE arms or clears the slot as the morph source |
| hold BANK | save page. TIME picks the slot, BANK saves |
| hold BANK, tap TIME | buffer quality page. FREEZE steps, BANK applies, TIME or 6 s cancels |
| hold FREEZE, tap TIME | envelope page. TIME moves the cursor, FREEZE steps the amount, BANK saves and leaves |
| hold FREEZE, tap BANK | mono bass on/off |
| tap FREEZE | freeze on/off |
| tap IN MUTE | input mute on/off |
| tap OUT MUTE | output mute on/off |
| hold IN MUTE + OUT MUTE 1 s | AUX random voltage on/off. Works from the meters or knobs page; saved |
| TRIG held at power-up | V/OCT calibration (the stock way) |
| any button during the start-up animation | skips it, and does nothing else |

### Mutes

- **What they do:** a tap toggles a click-free digital mute with a 5 ms
  linear fade.
  - IN MUTE fades the input before the engine, so tails, frozen buffers and
    voices keep sounding.
  - OUT MUTE fades the module's final output after the engine, the mono bass
    and the limiter.
- **Which button is which:** we follow the stock firmware: PE8 is IN MUTE
  and PE7 is OUT MUTE. The schematic's net names say the opposite. If they
  turn out swapped on the module, build with `-DTYPHOON_SWAP_MUTE_BUTTONS=1`
  (one `#define` in `typhoon/drivers/switches.h`).
- **Not saved:** both mutes are off after power-up.
- **Display:** while a side is muted, that meter row's top (red) LED blinks.
  The row itself shows what passes the mute, so a muted row is dark apart
  from that LED.

### AUX

The DAC (PA4) drives the AUX jack's normal, which in turn feeds the normal
of all nine CV jacks (every one except V/OCT). The firmware cannot tell
which jacks are patched.

- **Off (the default):** PA4 is driven low from the first lines of `Init()`.
  The normal is 0 V, as on the stock firmware with its noise off.
- **On** (hold both mute buttons for 1 s): a slow, smooth random voltage
  of 0 to about 1.2 V. It moves to a new random target every 500 ms with a
  200 ms glide, and pushes every unpatched CV jack up by up to about a
  quarter of its range. A cable in AUX replaces it with your own CV.
- **During calibration:** it is forced to 0 V.

The setting is saved.

### LEDs

Panel layout:

- **TIME column, "1 2 4 8" (right, above TIME, top to bottom):** the mode number in
  binary (1 + 2 + 4 + 8).
- **BANK column, "A B C D" (left, above BANK):** bank, slots and knobs.
- **Top two rows:** the IN meter (top) and the OUT meter (second row), each
  green, green, yellow, red.
- **FREEZE button LED:** the freeze state, as on the Monsoon.

Brightness uses 8-step software PWM from the 1 kHz tick, as on the Monsoon.

**Colour conventions.** Where the Monsoon uses colour to mean something:

- green → BANK A
- red → BANK B
- amber → BANK A + B

Bars that the Monsoon draws on its four LEDs go on the TIME column.

| Page / state | Monsoon (4 bicolour LEDs) | Typhoon |
|---|---|---|
| Meters page | input level bar (the output while frozen) | IN row = input level, OUT row = output level (scaled by OUT LEVEL/VCA, as stock), always. TIME + BANK show the current mode dimly (mode in binary + A/B) |
| Knobs page | WET / BAL / FBK / RVB as a green→red ramp | BANK A / B / C / D brightness = WET / BAL / FBK / RVB. TIME shows the mode number dimly |
| Mode menu | mode 1-15 in binary, green = bank 1, red = bank 2 | TIME = 1-15 in binary at full brightness. BANK A = bank 1, B = bank 2 |
| Mode menu, option flash (1.5 s) | LED (option % 4) blinks, green for 1-4, red for 5-8 | the option number 1-8 blinks in binary on TIME. The bank LED stays lit |
| Load page | slot LED blinks green; the morph source LED stays red | the slot's BANK LED blinks; the morph source's BANK LED stays lit. If the slot is the morph source, it blinks between full and dim |
| Save page | slot LED blinks red | the slot's BANK LED blinks; the other three BANK LEDs glow dimly |
| Saving | slot LED red | the slot's BANK LED lit |
| Envelope page | LED i = WET/BAL/FBK/RVB. Green = up, red = down, dim = ½, full = 1. The cursor blinks (faint amber when off) | row i (1/A = WET, 2/B = BAL, 4/C = FBK, 8/D = RVB). BANK LED = up, TIME LED = down, dim = ½, full = 1. The cursor row blinks fast; if off, both LEDs glow faintly |
| Mono bass (1.5 s) | on: middle two green. Off: outer two amber | on: TIME 2, 4 and BANK B, C. Off: TIME 1, 8 and BANK A, D |
| Buffer page | amber sweep 1→4, then LEDs 1-3 = 48/24/16 kHz bar. Green = 16-bit, red = 8-bit. LED 4 amber, blinking while a change is pending. Applied: all four flash its colour | sweep down TIME 1→8, then TIME 1-3 = 48/24/16 kHz bar. BANK A = 16-bit, B = 8-bit. TIME 8 = page marker, blinking while a change is pending. Applied: TIME + the A/B LED flash |
| FFT page | green sweep 4→1, then a bar 1-4 (256-2048). Green = each mode's own, amber = one size for all. All dim green = no FFT in this mode. Blinks while pending. Applied: all four flash | sweep up TIME 8→1, then the bar on TIME. BANK A = each mode's own, A + B = one size for all. TIME dim + A dim = no FFT in this mode. Blinks while pending. Applied: TIME + A(/B) flash |
| Calibration step 1 (send 1 V) | LEDs 1-2 blink amber | IN row blinks (as stock) |
| Calibration step 2 (send 3 V) | all four blink amber | IN and OUT rows blink (as stock) |
| Calibration failed / panic | all four red | all 16 LEDs lit. Gone after 1.5 s for a failed calibration; stays lit for an engine failure |
| Start-up (3.3 s) | sparkles; amber halo pulses from the middle out with the FREEZE LED; amber→green sweep; mode in bank colour, fading | the same on all four groups: sparkles; pulses from each group's middle LEDs out, with the FREEZE LED; a half→full brightness sweep; then the mode on TIME + A/B, fading |
| AUX on/off (1.5 s), new | — | on: all of TIME + BANK. Off: TIME 8 and BANK D |
| Input / output muted, new | — | the IN / OUT row's top (red) LED blinks |
| FREEZE LED | freeze state | freeze state |

## Calibration (V/OCT)

There are two ways in.

**The stock way:**

1. Hold **TRIG** while powering on. The IN row blinks.
2. Send **1 V** to V/OCT and tap **TRIG**. The IN and OUT rows blink.
3. Send **3 V** and tap **TRIG**. Done.

**The Monsoon way:**

1. Hold **TIME** (the menu opens), then also hold **BANK** for 1 s. The IN
   row blinks.
2. Do the same two steps as above.

On either calibration page, **TRIG** and **BANK** both confirm.

**What step 2 records:** the 0 V reading of every CV jack, so leave them
unpatched. The AUX normal is forced to 0 V while you calibrate.

**If it fails:** all LEDs light for 1.5 s and nothing is stored.

## Not verified on hardware

These come from the netlist, the BOM and the stock source. Check them
against a real module:

1. **Mute buttons:** which one is on PE7 and which on PE8. If they are
   swapped, see `TYPHOON_SWAP_MUTE_BUTTONS`.
2. **LEDs:** that they are active high, and whether the FREEZE LED (PD10)
   has a series resistor.
3. **Bootloader:** that the module's sector 0 holds the stock audio
   bootloader. To check, read it over SWD or just play the WAV.
4. **Pot direction:** the direction of the eight mux pots and the SIZE
   slider. None are flipped, as on stock.
5. **CV scaling:** CV, V/OCT and FREQ scaling and sign. These are inferred
   from the BOM (inverting stages, ±5 V full range) and the stock code.
6. **OUT meter scaling:** the stock curve on the PC4 sense voltage
   (level⁴ × 2.1, nothing below 0.15) is copied, not measured.
7. **AUX voltage:** the AUX random voltage's real range (DAC × 1.5) and how
   strongly it moves each CV.
8. **Pot mux timing:** the 4051 settling time with the alternating read
   (one pot every two audio blocks, as stock).
9. **CPU load:** assumed identical to the Monsoon's (same core, clock and
   flash wait states), so the Monsoon QEMU bench numbers should apply.
   F427-specific flash timing has not been measured.
10. **Codec start-up:** that the codec runs as I2S master at 48 kHz from its
    own crystal, with PC6 driven as an LED and never as I2S MCK.

## Checked in software

- **Build:** the firmware and bootloader build with the repo's flags
  (`-Wall -Werror`), with no warnings beyond the Monsoon build's own make
  notices.
- **Update WAV:** `host/build/bootloader_sim` decodes it back to the exact
  `.bin`.
- **Combo image:** it holds the bootloader at 0x08000000 and the
  application at 0x08008000, byte for byte.
- **QEMU:** the image boots to its main loop on QEMU's STM32F405 model.
  This used a relinked copy with RAM cut to 128 KB, entered through
  `typhoon/bootloader`. There is no codec in QEMU, so it shows the panic
  page for 1.5 s, then the meters page (mode 1 dim on TIME, A dim). No
  hard fault occurs. The codec, I2S, ADC and GPIO are not modelled there,
  so the audio path and buttons cannot be exercised.
- **Monsoon build:** `make -f monsoon/makefile` is untouched and still
  produces the same binary.
