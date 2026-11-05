# Vibe

Vibe is a send effect for the OP-XY SDK, with a mono wet path and stereo tape movement. It adapts DigiTide's
fixed-point wet and micro-loop algorithms to the OP-XY. Vibe is independent
of Chase Bliss and does not contain MOOD MKII source code.

## Controls

Press any encoder to switch between the two rows.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | Vibe Prm | Vibe |
| 2 | Loop Spd | Loop Vol |
| 3 | Fragment | Spread |
| 4 | Looper | Length |

- **Vibe:** Reverb (an eight-line feedback network), Delay, Grab (a retriggered variable-speed fragment), Phaser, Flanger or Chorus. Time is fixed at 100%. Vibe Prm sets diffusion, feedback or grab
  pitch/direction, phaser rate, flanger rate or chorus depth respectively. Phaser, Flanger and Chorus return the modulated signal; the OP-XY retains the dry signal and mixes it with the return.
- **Tape loop:** Loop Spd changes playback speed and direction in octave steps. Length sets capture duration and can shorten a captured loop.
The wet channel always processes the incoming send; the loop plays alongside it.

- **Loop Vol** adjusts only the loop, from silence to 1.5x; about 67% is unity. High levels can saturate the effect return.
- **Fragment:** records a rolling history of the incoming mono audio even at zero. While Looper is active, below 50%, it adds occasional randomly timed fragments from recent history to the loop return. Above 50%, the history plays continuously with crossfaded random selections, while the original tape is gradually reduced. Fragment size varies with Length (120 ms–1.2 seconds); playback waits for about one second of recorded history. Fragments follow Loop Spd with occasional octave and direction changes and random stereo placement. Loop Vol controls their level. The fragments bypass the Vibe effect.
- **Spread:** continuously increases stereo width and gradually adds slow left–right panning to the tape and overdub layer. The Vibe effect return is mono. High settings can saturate the return.
- **Looper:** Off clears the loop. Auto clears the previous loop, waits for audible input and begins looping after 120 ms to one second depending on Length; it continues recording until the full three-second buffer is ready, independently of Length. Hold keeps the current capture across effect reset. When empty, Hold records like Auto and then keeps that capture. Overdub records a separate layer up to 1.5 seconds long, regardless of Length and gradually refreshes it with new audible input. Switching to Hold keeps both layers.

Capture and loop playback are driven by incoming audio even when the sequencer is stopped. Stop does not clear or mute the loop; select Off to clear it. Hold's loop remains in the send slot's scratch memory across effect reset; selecting Off or Auto clears it. Scratch memory can be overwritten by another effect and is not saved to projects or guaranteed across power cycles. The effect cannot reliably observe the device play or Stop state.

## Memory and limits

Vibe requests 516,096 bytes of per-send scratch memory, 8 KiB below the SDK's
per-effect limit. The active loop ring holds 33,076 mono 16-bit frames at 11.025 kHz,
with a selectable capture from about 60 ms to 3 seconds. The overdub layer uses another 16,538 frames (33,076 bytes). The remaining tape storage holds about 6.09 seconds of rolling history for Fragment and a small amount of bookkeeping. A low-pass filter
precedes recording, and stereo playback options are applied to the mono loop. The shared
Delay/Grab/Flanger/Chorus history holds 32,768 mono Q31 frames. The wet effects process Q31 audio; tape storage remains 16-bit. The reverb uses eight feedback lines and input allpasses. The wet memory is cleared
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

Saved Options settings from earlier versions may need adjustment for the continuous Spread control.

Saved Motion settings from earlier versions may need adjustment for Fragment.

Parameter positions changed; saved settings from older development builds may need to be reselected.
