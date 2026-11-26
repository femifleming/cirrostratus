# Requests

What was asked for, in order (2026-11-23 → 24).

1. A new hi-fi firmware for the Monsoon (Clouds port), based on the things I like.
2. Drop the clocked repeat idea; replace it (→ DIFFERENCE, after the Oneirophone).
3. More modes (→ CROSS, STRETCH, SPREEK, CAPTURE, GENDY, SHIMMER, TAPE, PLATES, slot morph, per-mode trigger, envelope follower, per-mode memory, calibration).
4. Do all of it, test it, commit locally.
5. A manual, and how to flash it.
6. A version of Chloaelis on it (→ CHLOE).
7. Even more ideas; fill every mode slot (→ 30 modes in two banks: FALTUNG, QUELL, LEITNER, TANTRI, HARMONIZER, PUTZ, ENSEMBLE, BARBERPOLE, SPECDELAY, CHOIR, FOLDER, HALL, BLOOM, GAMELAN, MIRROR, POLISH).
8. Test everything and make sure it works.
9. Call it Cirrostratus, make a fun manual, push to GitHub as Cirrostratus.
10. A video manual in the faltung style, dark blue, with Bliss as the music.
11. A cool light show on start-up (→ "sunrise").

## 2026-11-24 — video manual v2
> the video needs to have the same voice that aeria os used, and needs to have a visual of the panel layout of monsoon so i know what knobs and sliders do what. you can make the module look faltung style as long as everything is located in the same place. delete the old video and make a new one.

## 2026-11-24 — 1.1: per-mode BAL / FBK / RVB, hard stereo, mono bass
> on the monsoon, bal feedback and reverb can have unique controls in each mode, i rarely need stereo width as an option, it can be hard stereo, and by default keep bass mono on the main out, below 60hz. implement those knobs as more controls for the modes, make the new firmware and update the manual and video manual.

## 2026-11-24 — 90-second version
> make a poetic condendes but in depth 1 minute 30 second version of the manual for cirrostratus for me to post.

## 2026-11-24 — 1.2: buffer quality (longer buffers)
> why is the grain freeze for grains on monsoon only max 1 second thats so short
> add an option for me to choose 48kz, 24khz or 16hz, and then all of those plus 8bit, in a setting menu that lets me free up space for longer grains. i want to have the option.
> have this setting effect all buffer effects, so i can choose it in a master setting and get loinger buffers on the effects that use buffers if i want.
> give a list of the new timings per buffer for each resoultion setting as well.
> make a new video manual when your done, and make the msuic sliglty quaiter in this new one.
> the manual needs to be visual and show me the module like the video
> when the slider is all the way down the lights show binary 2 instead of 1, i think its skipping the first one on both sets of effects.

> also in the monsoon firmware, some audio modes like the resonator sound glitchy and prevent me from changing modes, it gets stuck in that mode, can you run more tests for the modes? also can you make the spectral smear sound as good as the one for wingie does? its not that high qual right now on the monsoon.

1.3.0. RESONATOR, PLATES and GAMELAN retuned on every twitch of the knobs (ADC noise), which the bench never saw because it holds the knobs still: up to a third more CPU, enough to glitch and starve the main loop, where mode changes happen. They now retune only past the knobs' noise; RESONATOR spreads its retune over four blocks. Any block that runs late now makes the next skip the mode, so mode changes always go through. New tests (host/tests_hardware.cc): knob noise must not change those modes' sound, every mode switches away while blocks are being shed, a minute of every mode at heavy settings with noisy knobs; the bench has BENCH_JITTER. SPECTRAL option 2 = SMEAR, the Wingie's spectral smear ported (same algorithm; spectrum within 1 dB of the Wingie's at every blur), mono, 1644 insns/frame.

## 2026-11-26 — 1.3.1: GRAINS distortion, buffer page
> on monsoon, even when i change the different rsoultions it sounds like grains is still 8 bit, its super distoreted and i cant change it
> and sometimes when i hold to tap the differenresoultuons it just doesnt show me visually that its happeneing
> when when dry its distoriting my inout signal in grains mode

1.3.1. GRAINS needed more CPU than the module has (worst block ~2465 insns/frame against ~2150), so every block ran late and the whole output broke up, dry included, at any buffer quality. Each grain now decodes its stretch of the buffer once per block and interpolates from it (same values, same Hermite) and the grain window is exact every 4 samples (exact over the percussive attack): ~1770 typical, worst block ~2040, within -105 dB of 1.3.0. Buffer page: holding WRITE past a second opened the save page and DISP then picked a slot; DISP while WRITE is still held now opens the buffer page either way. The page opens with an amber sweep, LED 4 stays amber on it, and WRITE flashes all four LEDs in the applied colour. The bench's GRAINS figures were inflated by its own micro-benchmarks overwriting the engine's memory; fixed.
