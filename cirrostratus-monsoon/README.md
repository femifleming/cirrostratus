# Cirrostratus ☁

**Alternative firmware for the [Monsoon](https://github.com/jakplugg/Monsoon)**
(Jak Plugg's 12HP Mutable Instruments Clouds variant): 30 modes in two
banks, a 48 kHz floating-point engine, and a clean output section.

**Version 1.5.3.** SMEAR is smoother: a 2048-point transform (four times finer, chords stay clear) and TEXTURE for its character, from clear steady tones to a soft diffuse wash at the same level.

**1.5.2:** mode 1 is back as **SPRING**, a dispersive spring reverb tank (falling chirps, dwell, TRIG kicks the tank), and SPECTRAL's SMEAR is phase-locked: smooth at long blurs, no more flutter or dropouts.

**1.5.1:** fixes glitchy pulses on top of the audio: a block that ran a little long used to skip the mode for the next block (a click); now only a real overrun does, and it ramps down and back in.

**1.5.0:** GRAINS is gone: mode 1 is retired, every other mode
keeps its number (bank 1 is 2–15), and settings, calibration and slots
carry over. Every mode and option was then checked on QEMU's STM32F405,
built with the firmware's own flags, against the same run on a computer
(`tools/bench/verify.sh`: ordinary settings, flat out, fully dry, all 16
panel corners). That found two old overloads, now fixed: CHLOE's PT2399
and BBD at short delays (up to 3× the module's time; the emulated clocks
now stop at 160 / 64 kHz and the line gets shorter, same delay time) and
TAPE at ×4 speed (now ×3 at most).

**1.4.0:** A master **FFT size** setting (hold DISP, tap FREEZE)
for the spectral modes (SPECTRAL and SMEAR, CROSS, STRETCH, PUTZ, QUELL,
SPECDELAY, MIRROR): 256, 512, 1024 or 2048 points, or each mode's own size
(the default, exactly 1.3). Their times (blur, followers, decays, delays)
are converted, so only the resolution changes; save slots keep their size.

**1.2.0:** A master **buffer quality** setting (hold WRITE, tap
DISP): store audio at 48, 24 or 16 kHz, 16- or 8-bit, for up to 6× longer
buffers in STRETCH, CAPTURE, TAPE, BARBERPOLE and BLOOM (TAPE mono goes
from 1.2 s to 7.2 s). The default is exactly 1.1.

**1.1.0:** BAL, FBK and RVB are now three extra controls in every
mode (90 new controls, all with CV; BAL at noon and FBK/RVB at zero give the
1.0 sound). The output is hard stereo, with the bass below 60 Hz kept mono
(LR4 crossover; hold FREEZE and tap WRITE to switch it off). Settings and
save slots from 1.0 load unchanged.

| Bank 1 (green) | | Bank 2 (red) | |
|---|---|---|---|
| 1 SPRING | spring reverb tank (new in 1.5.2) | 16 QUELL | resonance suppressor |
| 2 SPECTRAL | spectral freeze / resynthesis | 17 LEITNER | spatial path panner |
| 3 CONVOLVE | freeze captures the IR | 18 TANTRI | tabla + tanpura |
| 4 RESONATOR | sympathetic strings + jawari | 19 HARMONIZER | scale harmonizer |
| 5 DIFFERENCE | phantom difference tones | 20 PUTZ | spectral cleaner |
| 6 CROSS | L/R spectral morph | 21 ENSEMBLE | string-machine chorus |
| 7 STRETCH | extreme time-stretch | 22 BARBERPOLE | frequency-shift spirals |
| 8 SPREEK | voice / talkbox | 23 SPECDELAY | spectral delay |
| 9 CAPTURE | captured wavetable voice | 24 CHOIR | formant choir |
| 10 GENDY | Xenakis stochastic synthesis | 25 FOLDER | wavefolder + LPG |
| 11 SHIMMER | shimmer cascade | 26 HALL | big reverb |
| 12 TAPE | multi-head tape echo | 27 BLOOM | swells + reverse blooms |
| 13 PLATES | modal plates | 28 GAMELAN | pelog / slendro metallophones |
| 14 CHLOE | Chloaelis strip (filter > tube > delay) | 29 MIRROR | spectral warping |
| 15 FALTUNG | built-in IR library | 30 POLISH | mastering chain |

**Manual:** [docs/Cirrostratus-Manual.pdf](docs/Cirrostratus-Manual.pdf)
([Markdown](docs/Cirrostratus-Manual.md)), which covers installing, every mode
and every button.

## Install

Download `cirrostratus.wav` from the latest release, hold **FREEZE** while
powering on, and play the file into **IN L**. Details, and SWD flashing, are
in the manual, section 1.

## Build and test

Needs `arm-none-eabi-gcc` (8.x works), Python 3 with numpy, and clang for
the host tests.

```sh
make -f monsoon/makefile              # firmware -> build/cirrostratus/cirrostratus.bin
make -f monsoon/makefile wav          # audio update file
make -f monsoon/makefile flash        # SWD (ST-Link + OpenOCD)
make -C host test                     # all DSP tests (ASan + UBSan)
tools/arm_check.sh                    # every DSP file with the firmware's -Werror flags
tools/bench/run.sh                    # CPU per mode on QEMU's STM32F405 model
tools/bench/verify.sh                 # every mode/option on QEMU vs the Mac: starts,
                                      # no clipping, dry = input, levels agree, CPU
host/build/bootloader_sim build/cirrostratus/cirrostratus.wav build/cirrostratus/cirrostratus.bin
                                      # decode the update WAV with the real bootloader code
```

`host/build/render --mode hall --in music --secs 10 --out hall.wav` renders
any mode to a WAV for listening.

## Layout

| Path | |
|---|---|
| `monsoon/` | firmware: drivers (from the Monsoon port of Clouds), UI, settings, main |
| `monsoon/dsp/` | portable DSP: engine, output section, STFT, buffers |
| `monsoon/dsp/modes/` | the 30 modes |
| `host/` | test harness, renderer, bootloader simulator |
| `tools/` | QPSK encoder, table/IR generators, CPU bench, manual builder |
| `docs/` | design spec and manual |
| `stmlib/`, `stm_audio_bootloader/` | Mutable Instruments libraries (MIT) |

## Credits

Mutable Instruments / Émilie Gillet (Clouds, stmlib, audio bootloader), Tom
Burns (Monsoon firmware port), Jak Plugg (Monsoon hardware), ARM (CMSIS-DSP).
MIT licence; see LICENSE.
