// SPDX-License-Identifier: GPL-2.0-or-later
// Vibe adapts fixed-point algorithms from DigiTide by irpina and contributors.
// DigiTide: https://github.com/irpina/digitide, GPL-2.0-or-later.
// Reference revision: 8969fb9b7b9835101e89699ff32079e6741d753d.
// Its Digitone shell, global state, clock, and memory map are replaced here.
#include "core.h"

namespace vibe {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::Buffer;

u32 ring_index(u32 index) { return index >= loop_frames ? index - loop_frames : index; }
const u32 wet_mask = wet_frames - 1;
const u32 wet_q8_mask = (wet_frames << 8) - 1;
const u32 rev_mask = reverb_line - 1;
const i32 tape_rate[] = {-1024, -512, -256, -128, 128, 256, 512, 1024};
// The band boundaries and octave speeds follow DigiTide.
const u32 tape_bands[] = {5, 24, 43, 62, 85, 98, 120};
const u32 clock_bands[] = {5, 16, 29, 41, 54, 70, 84, 96, 110};
const u32 clock_rates[] = {2000,3000,4000,6000,8000,12000,16000,24000,32000,44100};
const u32 grab_ratios[] = {256,271,287,304,323,342,362,384,406,431,456,483,512};
const u32 rev_lengths[] = {691,827,941,1061,1181,1319,1481,1619};
const u32 ap_lengths[] = {142,379,149,367};

u32 control(i32 x) { return x < 0 ? 0u : static_cast<u32>(x) >> 16; }
u32 choice(i32 x, u32 count) { return (control(x) * count) >> 15; }
u32 band(u32 knob, const u32* bounds, u32 count) {
    const u32 value = (knob * 128u) >> 15;
    u32 i = 0;
    while (i < count && value >= bounds[i]) ++i;
    return i;
}
i32 clamp16(i32 x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
i32 q15(i32 x, i32 y) { return (x * y) >> 15; }
i32 mix12(i32 a, i32 b, i32 gain) {
    return a + (((b - a) * gain) >> 12);
}
i32 approach(i32 value, i32 target, i32 step) {
    if (value < target) return value + (target - value < step ? target - value : step);
    if (value > target) return value - (value - target < step ? value - target : step);
    return value;
}
i32 smooth_control(i32 value, i32 target) {
    const i32 difference = target - value;
    const i32 step = difference / 64;
    return value + (step ? step : (difference > 0) - (difference < 0));
}
i32 smooth_block(i32 value, i32 target) {
    const i32 difference = target - value;
    const i32 step = difference / 4;
    return value + (step ? step : (difference > 0) - (difference < 0));
}
u32 pack(i32 l, i32 r) {
    return (static_cast<u32>(clamp16(l)) & 65535u) |
           ((static_cast<u32>(clamp16(r)) & 65535u) << 16);
}
i32 unpack(u32 word, bool right) {
    const i32 x = static_cast<i32>(right ? word >> 16 : word & 65535u);
    return x >= 32768 ? x - 65536 : x;
}
i32 tape_get(Buffer b, u32 frame) {
    return unpack(static_cast<u32>(b.words[frame >> 1]), (frame & 1u) != 0);
}
void tape_put(Buffer b, u32 frame, i32 sample) {
    const u32 shift = (frame & 1u) * 16u;
    const u32 mask = 65535u << shift;
    const u32 old = static_cast<u32>(b.words[frame >> 1]);
    b.words[frame >> 1] = static_cast<i32>((old & ~mask) | ((static_cast<u32>(clamp16(sample)) & 65535u) << shift));
}
i32 wrap(i32 value, i32 length) {
    if (value < 0) value += length;
    if (value >= length) value -= length;
    return value;
}

// Scratch belongs to the stock send slot and is not zeroed by the SDK.
// Clear only the wet and reverb regions in bounded pieces; until then their
// output is held at zero. The loop region uses ring_valid instead.
bool clear_wet(State& s, Buffer b) {
    const u32 total = used_words - wet_base;
    const u32 remain = total - s.clear_pos;
    const u32 count = remain < 512 ? remain : 512;
    for (u32 i = 0; i < count; ++i) b.words[wet_base + s.clear_pos + i] = 0;
    s.clear_pos += count;
    return s.clear_pos == total;
}

u32 clock_step(u32 knob, bool smooth) {
    u32 hz;
    if (knob == 32767) return 65536;
    if (!smooth) {
        hz = clock_rates[band(knob, clock_bands, 9)];
    } else {
        const u32 scaled = knob * 9u;
        const u32 index = scaled >> 15;
        const u32 fraction = scaled & 32767u;
        hz = clock_rates[index] +
             (((clock_rates[index + 1] - clock_rates[index]) * fraction) >> 15);
    }
    return hz >= 44100 ? 65536u : (hz * 65536u) / 44100u;
}

// Linear interpolation on a captured region. Unlike an arbitrary ring read,
// both samples are within audio that has actually been captured.
void loop_read(const State& s, Buffer b, i32 pos, u32 length, i32& l, i32& r) {
    const u32 whole = static_cast<u32>(pos) >> 8;
    const u32 frac = static_cast<u32>(pos) & 255u;
    const u32 next = whole + 1 == length ? 0 : whole + 1;
    const i32 a = tape_get(b, ring_index(s.loop_start + whole));
    const i32 c = tape_get(b, ring_index(s.loop_start + next));
    l = a + (((c - a) * static_cast<i32>(frac)) >> 8);
    r = l;
}

// Crossfade the end into the beginning, making both directions safe at seams.
void loop_seam(const State& s, Buffer b, i32 pos, u32 length, i32& l, i32& r) {
    loop_read(s, b, pos, length, l, r);
    const u32 fade = length >= 512 ? 128 : 64;
    const u32 whole = static_cast<u32>(pos) >> 8;
    if (fade && whole >= length - fade) {
        const u32 offset = whole - (length - fade);
        i32 other_l, other_r;
        // Blend to the exact first frame, so the last sample meets the first
        // in forward and reverse playback even without pre-capture history.
        loop_read(s, b, 0, length, other_l, other_r);
        const i32 gain = static_cast<i32>(offset << (fade == 128 ? 5 : 6));
        l = mix12(l, other_l, gain);
        r = mix12(r, other_r, gain);
    }
}

void capture(State& s, u32 wanted) {
    s.loop_length = s.ring_valid < wanted ? s.ring_valid : wanted;
    s.loop_start = ring_index(s.ring_write + loop_frames - s.loop_length);
    s.loop_head = s.env_position = s.env_slice = s.env_envelope = s.env_mix = 0;
    s.grain_phase = s.grain_size = s.overdub_position = 0;
    s.overdub_envelope = s.overdub_mix = 0;
    s.overdub_active = 0;
    s.grain_start[0] = s.grain_start[1] = 0;
    s.captured = s.loop_length >= 256;
}

// ENV, TAPE and STRETCH follow DigiTide's slice hold, octave tape and
// overlapping triangular grains. Capacity and safety checks are specific to
// the OP-XY's smaller per-send buffer.
void looper(State& s, Buffer b, u32 mode, bool spread, u32 length_control,
            i32 tape_step,
            i32 input, bool tape_tick, i32& out_l, i32& out_r) {
    out_l = out_r = 0;
    const i32 target_gain = mode && s.captured ? 4096 : 0;
    s.loop_gain = approach(s.loop_gain, target_gain, 16);
    if (!s.captured || !s.loop_length || !s.loop_gain) return;
    u32 len = s.loop_length;
    // Length can shorten an already caught tape, as in DigiTide.
    if (length_control < len) len = length_control;
    if (len < 256) len = 256;
    const i32 len8 = static_cast<i32>(len << 8);
    if (s.loop_head >= len8 || s.loop_head < 0) s.loop_head = 0;
    {
        s.loop_head = wrap(s.loop_head + tape_step, len8);
        loop_seam(s, b, s.loop_head, len, out_l, out_r);
        if (spread) {
            i32 reverse_l, reverse_r;
            loop_seam(s, b, len8 - 1 - s.loop_head, len, reverse_l, reverse_r);
            out_l = reverse_l;
        }
    }
    out_l = (out_l * s.loop_gain) >> 12;
    out_r = (out_r * s.loop_gain) >> 12;
    if (mode == 3) {
        // Gate very quiet input with hysteresis, then fade the write level to
        // avoid clicks. The envelope retains short gaps between notes.
        const i32 peak = input < 0 ? -input : input;
        if (peak > s.overdub_envelope) s.overdub_envelope = peak;
        else if (s.overdub_envelope)
            s.overdub_envelope -= (s.overdub_envelope >> 9) ? (s.overdub_envelope >> 9) : 1;
        if (!s.overdub_active && s.overdub_envelope >= 128) s.overdub_active = 1;
        if (s.overdub_active && s.overdub_envelope < 64) s.overdub_active = 0;
        s.overdub_mix = approach(s.overdub_mix, s.overdub_active ? 16384 : 0, 32);
        if (s.overdub_position >= len) s.overdub_position = 0;
        if (tape_tick) {
            const u32 at = ring_index(s.loop_start + s.overdub_position);
            if (s.overdub_mix) {
                const i32 old = tape_get(b, at);
                tape_put(b, at, old + q15(input - old, s.overdub_mix));
            }
            if (++s.overdub_position >= len) s.overdub_position = 0;
        }
    }
}

void wet_tap(Buffer b, u32 position, i32& l, i32& r) {
    const u32 a = (position >> 8) & wet_mask;
    const u32 next = (a + 1) & wet_mask;
    const u32 frac = position & 255u;
    const u32 first = static_cast<u32>(b.words[wet_base + a]);
    const u32 second = static_cast<u32>(b.words[wet_base + next]);
    const i32 al = unpack(first, false), ar = unpack(first, true);
    l = al + (((unpack(second, false) - al) * static_cast<i32>(frac)) >> 8);
    r = ar + (((unpack(second, true) - ar) * static_cast<i32>(frac)) >> 8);
}

i32 allpass(State& s, Buffer b, u32 line, i32 x, i32 gain) {
    const u32 at = allpass_base + line * allpass_line + s.allpass_write[line];
    const i32 previous = b.words[at];
    const i32 next = clamp16(x - q15(previous, gain));
    b.words[at] = next;
    if (++s.allpass_write[line] == ap_lengths[line]) s.allpass_write[line] = 0;
    return clamp16(previous + q15(next, gain));
}

// DigiTide/DigiPrism's eight-line Householder feedback network and two
// input allpasses per side, with smaller lines to fit the OP-XY scratch.
void reverb(State& s, Buffer b, i32 in_l, i32 in_r, i32& l, i32& r) {
    const i32 diffusion = s.reverb_diffusion;
    const i32 feedback = s.reverb_feedback;
    const i32 xl = allpass(s, b, 1, allpass(s, b, 0, in_l / 2, diffusion), diffusion);
    const i32 xr = allpass(s, b, 3, allpass(s, b, 2, in_r / 2, diffusion), diffusion);
    i32 y[8], sum = 0;
    for (u32 k = 0; k < 8; ++k) {
        const u32 index = (s.reverb_write - s.reverb_lengths[k]) & rev_mask;
        const i32 x = b.words[reverb_base + k * reverb_line + index];
        s.reverb_lp[k] += ((x - s.reverb_lp[k]) * 20000) >> 15;
        y[k] = s.reverb_lp[k];
        sum += y[k];
    }
    sum >>= 2;
    for (u32 k = 0; k < 8; ++k) {
        // Halving before multiplication keeps the feedback product in i32.
        const i32 feedback_sample = (((y[k] - sum) >> 1) * feedback) >> 14;
        b.words[reverb_base + k * reverb_line + s.reverb_write] =
            clamp16(feedback_sample + ((k & 1) ? xr : xl));
    }
    s.reverb_write = (s.reverb_write + 1) & rev_mask;
    l = clamp16((y[0] + y[2] + y[4] + y[6]) / 2);
    r = clamp16((y[1] + y[3] + y[5] + y[7]) / 2);
}

void delay(State& s, Buffer b, u32 target, i32 feedback, bool spread,
           i32 in_l, i32 in_r, i32& l, i32& r) {
    if (!s.delay_current) s.delay_current = target;
    if (!s.delay_fade && (target > s.delay_current + 48 || target + 48 < s.delay_current)) {
        s.delay_old = s.delay_current;
        s.delay_current = target;
        s.delay_fade = 1024;
    } else if (!s.delay_fade) s.delay_current = target;
    wet_tap(b, ((s.wet_write - s.delay_current) & wet_mask) << 8, l, r);
    if (s.delay_fade) {
        i32 old_l, old_r;
        wet_tap(b, ((s.wet_write - s.delay_old) & wet_mask) << 8, old_l, old_r);
        l = mix12(l, old_l, static_cast<i32>(s.delay_fade * 4));
        r = mix12(r, old_r, static_cast<i32>(s.delay_fade * 4));
        --s.delay_fade;
    }
    // Damp high frequencies in the feedback path, as DigiTide does.
    s.delay_lp[0] += ((l - s.delay_lp[0]) * 2048) >> 12;
    s.delay_lp[1] += ((r - s.delay_lp[1]) * 2048) >> 12;
    const i32 write_l = in_l + q15(spread ? s.delay_lp[1] : s.delay_lp[0], feedback);
    const i32 write_r = in_r + q15(spread ? s.delay_lp[0] : s.delay_lp[1], feedback);
    b.words[wet_base + s.wet_write] = static_cast<i32>(pack(write_l, write_r));
    s.wet_write = (s.wet_write + 1) & wet_mask;
    if (s.wet_valid < wet_frames) ++s.wet_valid;
    l = s.delay_lp[0]; r = s.delay_lp[1];
}

// DigiTide's GRAB repeatedly jumps a variable-speed head behind the write
// head, crossfading the old head over the jump.
void grab(State& s, Buffer b, u32 period, i32 rate, i32 in_l, i32 in_r, i32& l, i32& r) {
    if (s.grab_counter >= period) s.grab_counter = 0;
    if (!s.grab_counter) {
        const u32 extra = rate > 256 ? (period * static_cast<u32>(rate - 256)) >> 8 : 0;
        const u32 lag = extra + 64 < wet_frames ? extra + 64 : wet_frames - 1;
        s.grab_old = s.grab_head;
        s.grab_head = ((s.wet_write - lag) & wet_mask) << 8;
        s.grab_fade = 256;
    }
    if (++s.grab_counter >= period) s.grab_counter = 0;
    wet_tap(b, s.grab_head, l, r);
    s.grab_head = (s.grab_head + static_cast<u32>(rate)) & wet_q8_mask;
    if (s.grab_fade) {
        i32 old_l, old_r;
        wet_tap(b, s.grab_old, old_l, old_r);
        l = mix12(l, old_l, static_cast<i32>(s.grab_fade * 16));
        r = mix12(r, old_r, static_cast<i32>(s.grab_fade * 16));
        s.grab_old = (s.grab_old + static_cast<u32>(rate)) & wet_q8_mask;
        --s.grab_fade;
    }
    b.words[wet_base + s.wet_write] = static_cast<i32>(pack(in_l, in_r));
    s.wet_write = (s.wet_write + 1) & wet_mask;
    if (s.wet_valid < wet_frames) ++s.wet_valid;
}
} // namespace

void initialize(State& state) { state = State(); }

void process(State& s, Buffer b, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters p) {
    if (!b.words || b.count < used_words) {
        for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
        return;
    }
    const bool wet_ready = clear_wet(s, b);
    const u32 wet_mode = choice(p[5], 3);
    const u32 mode = choice(p[6], 4);
    const u32 options = choice(p[1], 3);
    const bool smooth_clock = false, spread = options != 0;
    const u32 step = clock_step(32767, smooth_clock);
    const i32 targets[7] = {32767, static_cast<i32>(control(p[0])),
                            static_cast<i32>(control(p[2])), static_cast<i32>(control(p[3])),
                            static_cast<i32>(control(p[4])), static_cast<i32>(control(p[7])),
                            static_cast<i32>(step >> 1)};
    if (!s.controls_ready) {
        for (u32 k = 0; k < 7; ++k) s.smoothed[k] = targets[k];
        s.controls_ready = 1;
        s.previous_wet_mode = wet_mode;
    }
    for (u32 k = 0; k < 4; ++k) s.smoothed[k] = smooth_block(s.smoothed[k], targets[k]);
    s.smoothed[5] = smooth_block(s.smoothed[5], targets[5]);
    s.smoothed[6] = smooth_block(s.smoothed[6], targets[6]);
    // A slow triangle LFO runs once per block. Motion zero leaves the
    // original sound untouched; depth and speed grow together.
    const u32 motion = static_cast<u32>(s.smoothed[5]);
    s.motion_phase += 500000u + (motion * 88u);
    const u32 phase = s.motion_phase >> 16;
    const i32 triangle = static_cast<i32>(phase < 32768 ? phase * 2 : (65535 - phase) * 2) - 32768;
    const i32 movement = q15(triangle, static_cast<i32>(motion));
    i32 wet_moving = s.smoothed[1] + (movement * 2500) / 32768;
    if (wet_moving < 0) wet_moving = 0;
    if (wet_moving > 32767) wet_moving = 32767;
    // Coefficients follow the smoothed knobs once per block, not eight line
    // multiplications per sample.
    if (wet_mode == 0) {
        const u32 reverb_size = 18000u + ((static_cast<u32>(s.smoothed[0]) * 14000u) >> 15);
        for (u32 k = 0; k < 8; ++k) s.reverb_lengths[k] = (rev_lengths[k] * reverb_size) >> 15;
        s.reverb_feedback = 18000 + ((s.smoothed[0] * 13500) >> 15);
        s.reverb_diffusion = (wet_moving * 5) >> 3;
    }
    // Stop/restart is observable; the effect API cannot identify a double tap.
    if (!transport.playing && s.was_playing) {
        s.captured = s.ring_valid = s.listening = 0;
        s.loop_gain = 0;
    }
    s.was_playing = transport.playing ? 1u : 0u;
    if ((mode == 1 || mode == 3) && s.previous_mode != mode && !s.captured) {
        s.ring_valid = 0; s.listening = 0;
    }
    if (mode != 3 && s.previous_mode == 3) {
        s.overdub_envelope = s.overdub_mix = 0;
        s.overdub_active = 0;
    }
    if (wet_mode != s.previous_wet_mode) {
        s.transition = 256;
        s.previous_wet_mode = wet_mode;
        if (wet_mode == 2) s.grab_counter = 0;
    }
    s.previous_mode = mode;
    const u32 length_knob = static_cast<u32>(s.smoothed[2]);
    const u32 length = 1323 + length_knob * 4u + ((length_knob * 21205u) >> 15);
    const u32 wet_time = 882 + ((static_cast<u32>(s.smoothed[0]) * 31000u) >> 15);
    const u32 loop_modify = static_cast<u32>(s.smoothed[3]);
    const i32 tape_base = tape_rate[band(loop_modify, tape_bands, 7)];
    const i32 tape_step = (tape_base + (tape_base * movement) / 262144) / 2;
    const u32 wet_modify = static_cast<u32>(wet_moving);
    const i32 feedback = static_cast<i32>((wet_modify * 32112u) >> 15);
    const u32 k = (static_cast<u32>(s.smoothed[1]) * 128u) >> 15;
    const u32 semitone = k >= 64 ? ((k - 64) * 13) >> 6 : ((63 - k) * 13) >> 6;
    const i32 grab_base = k >= 64 ? static_cast<i32>(grab_ratios[semitone]) :
                                    -static_cast<i32>(grab_ratios[semitone]);
    const i32 grab_rate = grab_base + (grab_base * movement) / 262144;
    const u32 grab_period = 1102 + (wet_time >> 2);
    const u32 wet_spread_step = spread && wet_mode == 2 ? 2147483647u / grab_period : 0;
    const u32 clock = smooth_clock ? static_cast<u32>(s.smoothed[6]) * 2u : step;
    const i32 filter = clock >= 65534 ? 4096 : static_cast<i32>(clock >> 4);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        if (s.smoothed[4] != targets[4]) s.smoothed[4] = smooth_control(s.smoothed[4], targets[4]);
        const i32 in_l = left[i] >> 17, in_r = right[i] >> 17;
        // One-pole anti-alias filtering before clock reduction, as in DigiTide.
        if (clock >= 65534) {
            s.input_lp[0] = in_l;
            s.input_lp[1] = in_r;
        } else {
            s.input_lp[0] += ((in_l - s.input_lp[0]) * filter) >> 12;
            s.input_lp[1] += ((in_r - s.input_lp[1]) * filter) >> 12;
        }
        s.clock_phase += clock;
        if (s.clock_phase >= 65536) {
            s.clock_phase -= 65536;
            const i32 x_l = s.input_lp[0], x_r = s.input_lp[1];
            const i32 mono = (x_l + x_r) / 2;
            s.tape_input_lp[0] += (mono - s.tape_input_lp[0]) / 2;
            s.tape_input_lp[1] += (s.tape_input_lp[0] - s.tape_input_lp[1]) / 2;
            s.tape_phase ^= 1u;
            const bool tape_tick = s.tape_phase == 0;
            if (mode == 2 && !s.captured && s.ring_valid >= 256) capture(s, length);
            i32 loop_l = 0, loop_r = 0;
            looper(s, b, mode, spread, length, tape_step,
                   s.tape_input_lp[1], tape_tick, loop_l, loop_r);
            const i32 feed_l = x_l, feed_r = x_r;
            i32 wet_l = 0, wet_r = 0;
            if (wet_ready) {
                if (wet_mode == 0)
                    reverb(s, b, feed_l, feed_r, wet_l, wet_r);
                else if (wet_mode == 1)
                    delay(s, b, wet_time, feedback, spread, feed_l, feed_r, wet_l, wet_r);
                else
                    grab(s, b, grab_period, grab_rate, feed_l, feed_r, wet_l, wet_r);
            }
            if (s.transition) {
                wet_l = mix12(wet_l, s.previous_wet[0], static_cast<i32>(s.transition * 16));
                wet_r = mix12(wet_r, s.previous_wet[1], static_cast<i32>(s.transition * 16));
                --s.transition;
            } else {
                s.previous_wet[0] = wet_l;
                s.previous_wet[1] = wet_r;
            }
            if (spread && wet_mode == 0) {
                const i32 mid = (wet_l + wet_r) / 2, side = (wet_l - wet_r) / 2;
                wet_l = clamp16(mid + side + side / 2);
                wet_r = clamp16(mid - side - side / 2);
            }
            if (spread && wet_mode == 2) {
                s.wet_spread_phase += wet_spread_step;
                const u32 wave = s.wet_spread_phase >> 16;
                const i32 pan = static_cast<i32>(wave < 32768 ? wave : 65535 - wave);
                wet_l = q15(wet_l, 32767 - pan / 2);
                wet_r = q15(wet_r, 16384 + pan / 2);
            }
            if (options == 2) {
                // Emphasize the stereo sides of both returns, with saturation.
                const i32 wet_mid = (wet_l + wet_r) / 2, wet_side = (wet_l - wet_r) / 2;
                const i32 loop_mid = (loop_l + loop_r) / 2, loop_side = (loop_l - loop_r) / 2;
                wet_l = clamp16(wet_mid + wet_side + wet_side / 2);
                wet_r = clamp16(wet_mid - wet_side - wet_side / 2);
                loop_l = clamp16(loop_mid + loop_side + loop_side / 2);
                loop_r = clamp16(loop_mid - loop_side - loop_side / 2);
            }
            if (!mode && s.captured && !s.loop_gain) s.captured = 0;
            const i32 record_mono = (x_l + x_r) / 2 + (wet_l + wet_r) / 4;
            s.tape_record_lp[0] += (record_mono - s.tape_record_lp[0]) / 2;
            s.tape_record_lp[1] += (s.tape_record_lp[0] - s.tape_record_lp[1]) / 2;
            if (!s.captured && transport.playing) {
                const i32 level = s.tape_input_lp[1] < 0 ? -s.tape_input_lp[1] : s.tape_input_lp[1];
                if ((mode != 1 && mode != 3) || level > 128) s.listening = 1;
                if (s.listening && tape_tick) {
                    // Record the incoming signal and some of the wet sound,
                    // just as DigiTide listens to the pedal's output.
                    tape_put(b, s.ring_write, s.tape_record_lp[1]);
                    s.ring_write = ring_index(s.ring_write + 1);
                    if (s.ring_valid < loop_frames) ++s.ring_valid;
                    if ((mode == 1 || mode == 3) && s.ring_valid >= length) capture(s, length);
                }
            }
            s.wet_hold[0] = wet_l; s.wet_hold[1] = wet_r;
            s.loop_hold[0] = loop_l; s.loop_hold[1] = loop_r;
        }
        const i32 loop_gain = s.smoothed[4];
        const i32 wet_gain = 32767;
        const i32 lo_l = q15(s.loop_hold[0], loop_gain), lo_r = q15(s.loop_hold[1], loop_gain);
        // Linear 0 to 1.5x loop gain; wet gain is independent.
        const i32 out_l = clamp16(q15(s.wet_hold[0], wet_gain) + lo_l + lo_l / 2);
        const i32 out_r = clamp16(q15(s.wet_hold[1], wet_gain) + lo_r + lo_r / 2);
        if (clock < 65534) {
            s.output_lp[0] += ((out_l - s.output_lp[0]) * filter) >> 12;
            s.output_lp[1] += ((out_r - s.output_lp[1]) * filter) >> 12;
        } else {
            s.output_lp[0] = out_l; s.output_lp[1] = out_r;
        }
        left[i] = clamp16(s.output_lp[0]) * 65536;
        right[i] = clamp16(s.output_lp[1]) * 65536;
    }
}
} // namespace vibe
