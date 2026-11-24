# Cirrostratus ☁

**Alternative firmware for the [Monsoon](https://github.com/jakplugg/Monsoon)**
(Jak Plugg's 12HP Mutable Instruments Clouds variant): 30 modes in two
banks, a 48 kHz floating-point engine, and a clean output section.

**Version 1.2.0.** A master **buffer quality** setting (hold WRITE, tap
DISP): store audio at 48, 24 or 16 kHz, 16- or 8-bit, for up to 6× longer
buffers in GRAINS, STRETCH, CAPTURE, TAPE, BARBERPOLE and BLOOM (GRAINS mono
goes from 1.24 s to 7.3 s). The default is exactly 1.1.

**1.1.0:** BAL, FBK and RVB are now three extra controls in every
mode (90 new controls, all with CV; BAL at noon and FBK/RVB at zero give the
1.0 sound). The output is hard stereo, with the bass below 60 Hz kept mono
(LR4 crossover; hold FREEZE and tap WRITE to switch it off). Settings and
save slots from 1.0 load unchanged. Not yet run on a physical module.

| Bank 1 (green) | | Bank 2 (red) | |
|---|---|---|---|
| 1 GRAINS | trajectory granular | 16 QUELL | resonance suppressor |
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
