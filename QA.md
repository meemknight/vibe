# Testing Vibe on an OP-XY

Checks to run on a device with a firmware that includes Vibe, from top to bottom, in one project:
each section leaves the project ready for the next. To run only some sections, set up what their first
line says. Host tests (`python ../open-xy/sdk.py test --machine .`) don't replace these: they can't hear
the sound or see the screen.

Report a failure with the section, the line and what you saw. A photo of the screen helps.

Record the machine revision, SDK revision and firmware image for each full pass.

## Setup

- Build a firmware with the machine ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine))
  and install it.
- Create a new project, **plain**, without Vibe: give track 1 a stock sound of its own and save it.
  Later steps load it as a project that never had the machine.
- Create another new project, **qa**, for the pass: don't use a project you care about.
- Track 1: a stock synth, with one note on step 1 lasting a whole bar, so it sounds all the time while
  the sequencer plays. Track 15 (a send): Vibe (Shift + track 15's key, then choose it). Turn track 1's
  send to track 15 up.
- Press play, and leave it playing unless a step says otherwise.

## Firmware basics

- [ ] The OP-XY boots, and stock engines, effects, the sequencer and project save/load work
- [ ] Vibe is in the effect list of tracks 15 and 16 (Shift + their track keys), under its name, and in
      no other track's list

## Its page and controls

- [ ] Vibe Prm, Spread, Length, Loop Spd, Loop Vol and Fragment through their whole ranges, both directions: the sound follows, with no clicks, jumps or
      silence at the ends; the value shown reads in its unit (Hz, ms, %, a name)
- [ ] Choice controls move one choice per detent, both directions, and wrap or stop as the page says
- [ ] Fast turns: the value keeps up, no flicker or slowdown
- [ ] Controls with an alternate (an encoder press, a second page): pressing switches, the page shows
      which is active, and each keeps its own value
- [ ] The page while playing and while stopped, and after leaving it and coming back: nothing stale,
      missing or drawn twice
- [ ] With track 1's send at 0 the effect is silent; raising it brings the effect in smoothly
- [ ] Stop the sequencer while a note rings: the effect's tail ends naturally, with no click
- [ ] The same effect on track 16, at the same time as on track 15: each keeps its own values

## Sound and capture

- [ ] Sweep Vibe Prm rapidly in Chorus and listen for crackles and level jumps.

- [ ] Sweep Length during loop playback, in forward/reverse and high Spread; listen for clicks or harsh jumps.

- [ ] Check mono tape recording with stereo input, maximum Length, overdub, and high Spread playback.

- [ ] The second page selects Vibe Prme and Looper;
      each shows its own name and setting.
- [ ] Reverb, Delay, Grab, Phaser, Flanger and Chorus sound distinct with the dry source audible; Vibe Prm does what the README says; Time remains fixed at maximum.
- [ ] Sweep Spread smoothly from minimum to maximum; check stereo behavior on each Vibe mode and the tape loop, including smooth left–right movement.
- [ ] From Off, select Hold and play a phrase: it waits for input, records and loops it. Turn Loop Spd through forward and reverse speeds.
- [ ] Select Off and then Hold again: silence does not restore the old loop; a new phrase is captured.
- [ ] Auto waits for input, starts after 120 ms to one second depending on Length and transitions to the selected Length when recorded;
      silence at the start must not produce a silent locked loop.
- [ ] Overdub captures the original, then records and plays a separate layer up to 1.5 seconds. Verify the original remains, the layer refreshes on audible input, quiet input leaves it intact, and switching to Hold retains both.
- [ ] Check Loop Vol with both channels sounding. Test Fragment below and above 50%: occasional playback becomes continuous with smooth crossfades, and the original tape recedes. Test both loop directions and short Length, and confirm Stop clears history. Check the stereo result on headphones.
- [ ] Stop and restart in Hold: the saved loop resumes. In Auto, Stop clears it and capture restarts.
      Wet effect tails may continue; check that they do not click.
- [ ] Put Vibe on both sends, use different modes/captures, then change one:
      the other send's audio and settings stay independent.
- [ ] Switch another effect into the slot and back; no old buffer audio leaks.

## Parameter locks

- [ ] Lock each control on a step (hold the step, turn the knob): it plays on that step, and the page
      shows the lock while the step is held
- [ ] Several locks on one step, and locks on neighbouring steps: each plays as written
- [ ] Clear the locks: the control plays its own value again

## Patterns, scenes and songs

- [ ] Give the track a second pattern with different values: each pattern keeps its own
- [ ] Copy a pattern: the copy holds the same values, and changing one leaves the other alone
- [ ] Switch between the patterns while playing and while stopped: the sound and the page follow at once
- [ ] Make scenes 1 and 2 with the two patterns, and a song of scene 1 then scene 2, looping: each scene
      plays its own values from its first bar, on every pass

Then play scene 1.

## Mute

- [ ] Mute the track (hold aux and press the track's key): it's silent, with no click or tail left behind;
      unmute: it plays as before
- [ ] The same from the mixer
- [ ] Muted in scene 2 only: switching between scenes 1 and 2 mutes and unmutes it

Unmute it everywhere.

## Saving and loading

- [ ] Save **qa**, power off and on, load it: the machine is there with every value as you left it, on
      each pattern
- [ ] Change a value, wait a few seconds and power off without saving: the OP-XY starts normally, and
      **qa** loads as last saved
- [ ] Load **plain**: track 1 has its own sound, nothing of **qa** shows. Load **qa** again: all back
- [ ] While playing, load **plain** and then **qa** again: nothing freezes, no stray values
- [ ] Create a new project: put Vibe on a track; it starts at its defaults
- [ ] On a firmware without Vibe, open **qa**: it loads, and the rest of the project plays

## CPU

- [ ] Follow [hardware profiling](https://github.com/etcetc000/open-xy/blob/staging/docs/profiling.md):
      a profiling build from the committed source, a run collected at the machine's heaviest setting, and
      the readout kept with the commit it measured

## In the builder

Once the machine's catalog entry is merged and released (the beta site from `staging`, open-xy.com from
`main`; see [submitting a machine](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)):

- [ ] The machine list shows Vibe as Community, with its name, maintainer and description
- [ ] Filtering by its kind (effects) lists it, and filtering by the other kinds doesn't
- [ ] Searching its name, and each of its keywords, finds it; its details panel lists its keywords, and
      clicking one lists it among that keyword's machines
- [ ] Choosing it builds a firmware that includes it, and that firmware passes
      [Firmware basics](#firmware-basics)

## Known limits

- The loop is at most about 3 seconds.
- Any transition to stopped clears the loop; double Stop cannot be detected.
- A new effect takes about 0.15 seconds to initialize its wet scratch.
- MIDI note control, separate freeze, ramping and most hidden pedal settings
  are not provided. Hardware CPU and sound have not yet been measured.

