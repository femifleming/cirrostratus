# Dobbeltrap: handoff (paused 2026-11-25, before any code was written)

Working name: **Dobbeltrap** ("dice staircase"). New PUBLIC repo on the user's GitHub
(femifleming), Dutch name, built from BRIBESTAGES (thisisbribery/bribestages, MIT; credit
BRIBESTAGES, qiemem, Mutable Instruments). Local: ~/bribestages, branch `dobbeltrap`
(origin is thisisbribery: never push there; create the new repo when publishing).

## User's request (verbatim intent)
A new mode for Stages built around how they use it: per-segment random sources
(uni/bipolar), envelope with attack AND decay in one segment + shape, audio-rate
oscillator, audio processor (lofi CV inputs are fine). Keep the envelope follower.
"Do it all", MORE random types, more complex oscillator types inspired by the SDS
Digital SDS VCO (16 CV-scannable waveforms, built-in envelope + VCA, custom/recorded
waveforms, wave-rider). Build, test, manual with a VISUAL of the module (panel photo:
~/Desktop/mutableinstruments_stages_01.webp), push to a new public repo, Dutch name.
Work in checkpoints; user resumes later.

## Facts found
- Build: `make -f stages/makefile -j8 TOOLCHAIN_PATH=/opt/homebrew/` (works, gcc 8.5).
  Stock BRIBESTAGES: text 95 KB of 224 KB flash; RAM data+bss 30.5 KB of 32 KB (+1 KB
  stack) -> ~1 KB free RAM.
- Big RAM user: `SegmentGenerator segment_generator[6]` = 23.5 KB (37 segments each for
  chaining). Plan: the new mode's engine lives in that memory (placement new, static_assert
  size); main loop re-runs `segment_generator[i].Init(mode, &note_quantizer[i])` when
  leaving the mode, before Process() (chain reinit then calls Configure). ui/ISR never
  touches segment_generator (only chain_state) - verified by reading ui.cc.
- Process at 31250 Hz, blocks of 8; CV read once per block (3906 Hz). block fields:
  cv, cv_raw (unfiltered), slider, cv_slider (slider+cv, -1..2), pot, input_patched,
  input[ch][8] gate flags. Output: settings.dac_code(ch, v), v=1.0 -> 8 V
  (bipolar convention ±5 V = ±0.625). V/oct: 1.0 = 96 semitones.
- Modes: stages.cc main() switches on settings.state().multimode; 6EG
  (ProcessSixEg) is the template for a whole-module mode. ui.cc multimodes_[6] maps
  5 s hold on button i -> mode. PLAN: button 6 -> new MULTI_MODE_DOBBEL = 6; long-hold
  button 5 while in Ouroboros toggles Ouroboros <-> Ouroboros alternate (so nothing is
  lost); fix show_mode() for alt.
- Settings State struct size MUST NOT change (ChunkStorage checks size; a mismatch
  formats and triggers the factory test!). Store the new mode's config in its own
  ChunkStorage in the LAST 4 KB of flash 0x0803F000-0x08040000 (bootloader only erases
  pages it writes; check app end < 0x0803F000 in tests).
- Ui::Poll runs in SysTick; existing lock/unlock of slider/pot on button hold
  (cv_reader_->Lock) and press_time_ logic can be reused for the new mode.

## Design (agreed in chat, refine while building)
Every segment independent. Tap = next type: Random -> Chaos -> Envelope -> Oscillator ->
Processor. Hold+slider = flavour (zones; LED: G,Y,R solid, then blinking G,Y,R, G/R alt,
Y/R alt). Hold+pot = per-type continuous "aux". Long press 0.5 s = per-type "alt" toggle.
5 s = mode change (unchanged). Button LED = type colour (Random green, Chaos yellow,
Envelope red, Osc green/red alternating, Processor yellow/red alternating), breathing when
alt is on, flash on events; slider LED = output level.
- RANDOM (slider+CV = rate 0.05-100 Hz, or with a gate patched: chance of change per
  clock; pot = slew relative to period; aux = amplitude; alt = bipolar): Uniform,
  Gaussian, Xenakis walk (reflecting), Pink 1/f (Voss), Quantised semitones (aux = range
  1-5 oct), Kin (follows left neighbour, pot = looseness), Orbit (run of Orbit segments:
  X, Y, radius; pot = path circle/figure-8/spiral/wander).
- CHAOS (same rate/clock; aux amplitude; alt bipolar): Turing loop (8 bits, pot locked ->
  random -> inverted loop), Logistic map (pot = r 3.4-4.0), Lorenz (run = x,y,z; pot =
  rho 20-50), Cellular automaton (run, 8 cells per segment, pot picks rule 30/90/110/...),
  Coin gates (pot = probability), Dust (slider = density, pot = height randomness).
- ENVELOPE: slider = attack, pot = decay (1 ms-10 s), CV jack scales both times
  (1 V = half), aux = curve (rational x/(x+k(1-x))), alt = loop; trigger -> AD, gate held
  -> sustain at top; flavours = ratchet 1/2/3/4 repeats.
- OSCILLATOR (slider 0-1 = C1-C7 + CV V/oct; aux = fine ±1 semitone; alt = VOICE: gate
  drives built-in AR VCA (SDS idea), otherwise gate = hard sync; ±5 V): Morph
  (sine-tri-saw-square-pulse, polyBLEP), Wavebank (16 SDS-style tables, 256 int16 each,
  built in RAM on mode entry, pot scans with crossfade), Additive (1-16 harmonics,
  Chebyshev recurrence, skip > 14 kHz), Pair (difference tones, ratio 1-1.5), Fold,
  FM-from-left (left segment = modulator), Gendy (Xenakis stochastic waveform, 12
  breakpoints), Rider (records the left neighbour's output as its waveform; gate = freeze).
- PROCESSOR (input = cv_raw at block rate; slider raw, not cv_slider): Follow (as
  BRIBESTAGES: slider release, pot attack, aux gain, alt invert, gate freeze), Transient
  gate (trigger on jumps), Slew (rise/fall, gate = hold), Crush/Fold (lofi, pot = rate
  reduction), Gesture looper (gate high records CV, 768 int16 @ 64 Hz = 12 s).
- Memory pool (~22 KB of the 23.5 KB): looper 6x768 int16, rider 6x256 int16, wavebank
  16x256 int16, per-segment state.

## Next steps
1. stages/dobbel.h/.cc: host-testable engine (no hardware calls), defaults per type.
2. stages.cc: ProcessDobbel + enter/leave (re-Init generators); settings: storage +
   in_dobbel_mode(); ui.cc: Poll branch + LEDs + mode button changes.
3. Host tests (render WAV/CSV per flavour; pitch, timing, ranges; RAM/flash/app-end
   checks); bootloader WAV via stages' tools (check python version) + decode check.
4. Manual: panel drawing from the photo (faltung-style visual like Cirrostratus),
   per-type pages with highlighted controls. README, REQUESTS.md, LICENSE credits.
5. New public repo femifleming/<dutch name>, push, release with WAV + manual.
