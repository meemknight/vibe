#pragma once
#include <opxy/effect.hpp>

namespace vibe {
// The scratch buffer belongs to the send slot, not the effect. Positions count
// words written since initialize; unwritten ring data is never read.
const opxy::u32 loop_frames = 44100; // three seconds of mono at 14.7 kHz
const opxy::u32 layer_base = loop_frames, layer_frames = 22050;
const opxy::u32 history_base = layer_base + layer_frames, history_frames = 89454;
const opxy::u32 wet_frames = 32768;
const opxy::u32 reverb_line = 2048;
const opxy::u32 allpass_line = 512;
// Keep the rest of the prior tape allocation reserved for future use.
const opxy::u32 wet_base = 77824;
const opxy::u32 reverb_base = wet_base + wet_frames;
const opxy::u32 allpass_base = reverb_base + 8 * reverb_line;
const opxy::u32 used_words = allpass_base + 4 * allpass_line;

struct State {
    opxy::u32 clear_pos, ring_write, ring_valid, loop_start, loop_length;
    opxy::u32 captured, previous_mode, was_playing, listening;
    opxy::i32 loop_head, loop_gain, overdub_envelope, overdub_mix;
    opxy::u32 overdub_active;
    opxy::u32 tape_length, tape_old_length, tape_fade;
    opxy::i32 tape_old_head;
    opxy::u32 overdub_position, layer_length, layer_valid;
    opxy::i32 layer_head, layer_last, layer_fade_from;
    opxy::u32 layer_fade, layer_play_length;
    opxy::u32 tape_phase, loop_pan_phase;
    opxy::i32 tape_input_lp[2], tape_record_lp[2];
    opxy::i32 smoothed[7];
    opxy::u32 controls_ready;
    opxy::u32 wet_write, wet_valid, delay_current, delay_old, delay_fade;
    opxy::i32 delay_lp[2];
    opxy::u32 grab_counter, grab_head, grab_old, grab_fade, wet_spread_phase;
    opxy::u32 reverb_write, allpass_write[4];
    opxy::u32 reverb_lengths[8];
    opxy::i32 reverb_feedback, reverb_diffusion;
    opxy::i32 reverb_lp[8];
    opxy::u32 previous_wet_mode, transition;
    opxy::i32 previous_wet[2];
    opxy::u32 modulation_phase;
    opxy::i32 chorus_lag[2];
    opxy::i32 phaser_x[2][6], phaser_y[2][6], phaser_feedback[2];
};
void initialize(State& state);
void process(State& state, opxy::Buffer buffer, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
}
