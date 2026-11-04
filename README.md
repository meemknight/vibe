# Vibe

Vibe is a stereo send effect for the OP-XY SDK. It adapts DigiTide's
fixed-point wet and micro-loop algorithms to the OP-XY. Vibe is independent
of Chase Bliss and does not contain MOOD MKII source code.

## Controls

Press any encoder to switch between the two rows.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | Vibe Prm | Loop Vol |
| 2 | Options | Vibe |
| 3 | Length | Looper |
| 4 | Loop Spd | Motion |

- **Vibe:** Reverb (an eight-line feedback network), Delay or Grab
  (a retriggered variable-speed fragment). Time is fixed at 100%. Vibe Prm sets diffusion, feedback or grab
  pitch/direction respectively.
- **Tape loop:** Loop Spd changes playback speed and direction in octave steps. Length sets capture duration and can shorten a captured loop.
The wet channel always processes the incoming send; the loop plays alongside it.

- **Loop Vol** adjusts only the loop, from silence to 1.5x; about 67% is unity. High levels can saturate the effect return.
- **Motion** adds a slow, repeating drift to tape playback speed and the selected Vibe effect. Higher settings make the movement deeper and faster. At zero, the sound is steady. The wet return is fixed at unity; the OP-XY send controls the input level and stock retains the dry signal. The loop captures input plus some wet signal.
- **Options:** Normal, Wide or X Wide (stronger stereo spread). Wide changes reverb width, makes Delay ping-pong and pans Grab; Tape plays the left loop backwards.
- **Looper:** Off (the default) listens while transport plays but does not play a loop.
  Auto waits for audible input, records a Length-sized capture and loops it.
  Hold catches the most recent available audio and holds it. Overdub starts like Auto, then gradually replaces the captured audio with new input. It ignores very quiet input and smoothly fades recording in and out; short gaps can still be recorded.
  Select Off and then Hold to catch another passage.

Stop clears the capture; restart begins listening afresh. The SDK exposes
play state but not a double-Stop gesture to an effect, so Vibe cannot
distinguish the first Stop from a double press. Captured audio is temporary
and is not saved in projects.

## Memory and limits

Vibe requests 516,096 bytes of per-send scratch memory, 8 KiB below the SDK's
per-effect limit. The loop ring holds 155,648 mono 16-bit frames at 22.05 kHz,
with a selectable capture from about 60 ms to 6.97 seconds. A low-pass filter
precedes recording, and stereo playback options are applied to the mono loop. The shared
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
