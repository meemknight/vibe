# Vibe

Vibe is a stereo send effect for the OP-XY SDK. It adapts DigiTide's
fixed-point wet and micro-loop algorithms to the OP-XY. Vibe is independent
of Chase Bliss and does not contain MOOD MKII source code.

## Controls

Press any encoder to switch between the two rows.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | Wet Mod | Loop Vol |
| 2 | Options | Wet Mode |
| 3 | Length | LoopMode |
| 4 | Loop Spd | Looper |

- **Wet mode:** Reverb (an eight-line feedback network), Delay or Grab
  (a retriggered variable-speed fragment). Time is fixed at 100%. Wet Mod sets diffusion, feedback or grab
  pitch/direction respectively.
- **Loop mode:** Tape changes speed and direction in octave steps; Env holds
  a slice while incoming sound exceeds its threshold; Stretch moves through
  overlapping grains, including reverse and stalled positions. Length sets
  capture duration and adjusts slice/grain length; Tape can shorten a capture.
  Loop Spd selects tape speed, Env sensitivity or Stretch progress.
The wet channel always processes the incoming send; the loop plays alongside it.

- **Loop Vol:** at centre both channels are at unity; turning left reduces
  the loop, and turning right fades the wet channel while boosting the loop
  up to 1.5x. High levels can saturate the effect return. The OP-XY send level governs how much signal enters Vibe;
  stock retains the source's dry signal.
- **Options:** Normal, Wide or X Wide (stronger stereo spread). Wide changes reverb width, makes Delay ping-pong and pans Grab; Tape plays the left loop backwards and Env/Stretch pan the loop.
- **Looper:** Off (the default) listens while transport plays but does not play a loop.
  Auto waits for audible input, records a Length-sized capture and loops it.
  Hold catches the most recent available audio and holds it. Overdub adds
  input at 25% per pass while fading existing material to prevent runaway gain.
  Select Off and then Hold to catch another passage.

Stop clears the capture; restart begins listening afresh. The SDK exposes
play state but not a double-Stop gesture to an effect, so Vibe cannot
distinguish the first Stop from a double press. Captured audio is temporary
and is not saved in projects.

## Memory and limits

Vibe requests 524,288 bytes of per-send scratch memory, matching the SDK's
per-effect limit. The loop ring holds 79,872 stereo 16-bit frames and its
longest selectable capture is about 1.76 seconds at 44.1 kHz. The shared
Delay/Grab history holds 32,768 stereo frames. The reverb's
eight lines and input allpasses occupy the rest. The wet memory is cleared
in bounded steps after reset, so its output begins after roughly 0.15 seconds.

These limits are shorter than DigiTide's original buffers. The sample rate,
control mappings, spread and gain staging are adapted;
Vibe is not a bit-exact MOOD MKII or DigiTide. It does not implement MIDI
notes/CC, MIDI synth mode, separate wet freeze, pedal ramping, or all DIP
and hidden settings. Hardware sound, CPU use and the UI remain unverified.

## Checks and build

With the SDK beside Vibe:

    python ../open-xy/sdk.py test --machine . --no-cpu

On Windows, pass `--host-cxx "C:/Program Files/LLVM/bin/clang++.exe"`
if a C++ compiler is not on PATH. Device CPU measurement remains necessary.

For a module-only build, use a fresh output directory. On the SDK revision
used for development, an effect-only compile hits an unused helper in the
SDK startup code; including a synth works around that without modifying the
SDK:

    python ../open-xy/sdk.py compile --machine . --machine <path-to-fm2op> --out <fresh-output>

The result is a non-bootable module, not firmware. Firmware requires the
user's own stock OS outside every checkout. See QA.md and the SDK's build and
profiling guides for hardware validation.

## Source and license

DigiTide by irpina and contributors:
https://github.com/irpina/digitide/tree/8969fb9b7b9835101e89699ff32079e6741d753d
(GPL-2.0-or-later). Its looper, grain, delay, Grab and Householder reverb
designs and rate bands inform Vibe's adapted DSP. Vibe is GPL-2.0-or-later;
see LICENSE.

Existing saved settings from earlier development builds may need to be reselected.
