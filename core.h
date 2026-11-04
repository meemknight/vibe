#pragma once
#include <opxy/effect.hpp>

namespace vibe {
// The scratch buffer belongs to the send slot, not the effect. Positions count
// words written since initialize; unwritten ring data is never read.
const opxy::u32 loop_frames = 155648; // mono 16-bit frames, two per word
const opxy::u32 wet_frames = 32768;
const opxy::u32 reverb_line = 2048;
const opxy::u32 allpass_line = 512;
const opxy::u32 wet_base = loop_frames / 2;
const opxy::u32 reverb_base = wet_base + wet_frames;
const opxy::u32 allpass_base = reverb_base + 8 * reverb_line;
const opxy::u32 used_words = allpass_base + 4 * allpass_line;

struct State {
    opxy::u32 clear_pos, ring_write, ring_valid, loop_start, loop_length;
    opxy::u32 captured, previous_mode, previous_loop_mode, was_playing, listening;
    opxy::i32 loop_head, grain_start[2], env_position, env_slice, env_envelope;
    opxy::i32 env_mix, loop_gain, overdub_envelope, overdub_mix;
    opxy::u32 overdub_active;
    opxy::u32 grain_phase, grain_size, grain_weight, overdub_position, spread_phase;
    opxy::u32 clock_phase, motion_phase, tape_phase;
    opxy::i32 tape_input_lp[2], tape_record_lp[2];
    opxy::i32 input_lp[2], wet_hold[2], loop_hold[2], output_lp[2];
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
};
void initialize(State& state);
void process(State& state, opxy::Buffer buffer, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters parameters);
}
