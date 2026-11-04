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
const i32 stretch_rate[] = {-256, -171, -128, -64, 0, 64, 128, 171, 256};
// The band boundaries and octave speeds follow DigiTide.
const u32 tape_bands[] = {5, 24, 43, 62, 85, 98, 120};
const u32 stretch_bands[] = {16, 31, 47, 61, 81, 97, 112, 127};
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
    const u32 a = static_cast<u32>(b.words[ring_index(s.loop_start + whole)]);
    const u32 c = static_cast<u32>(b.words[ring_index(s.loop_start + next)]);
    const i32 al = unpack(a, false), ar = unpack(a, true);
    l = al + (((unpack(c, false) - al) * static_cast<i32>(frac)) >> 8);
    r = ar + (((unpack(c, true) - ar) * static_cast<i32>(frac)) >> 8);
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
    s.grain_start[0] = s.grain_start[1] = 0;
    s.captured = s.loop_length >= 256;
}

// ENV, TAPE and STRETCH follow DigiTide's slice hold, octave tape and
// overlapping triangular grains. Capacity and safety checks are specific to
// the OP-XY's smaller per-send buffer.
void looper(State& s, Buffer b, u32 mode, u32 loop_mode, bool spread, u32 length_control,
            i32 tape_step, i32 stretch_step, i32 threshold,
            i32 in_l, i32 in_r, i32& out_l, i32& out_r) {
    out_l = out_r = 0;
    const i32 target_gain = mode && s.captured ? 4096 : 0;
    s.loop_gain = approach(s.loop_gain, target_gain, 16);
    if (!s.captured || !s.loop_length || !s.loop_gain) return;
    u32 len = s.loop_length;
    // Length can shorten an already caught tape, as in DigiTide.
    if (loop_mode == 0 && length_control < len) len = length_control;
    if (len < 256) len = 256;
    const i32 len8 = static_cast<i32>(len << 8);
    if (s.loop_head >= len8 || s.loop_head < 0) s.loop_head = 0;
    if (loop_mode == 0) {
        s.loop_head = wrap(s.loop_head + tape_step, len8);
        loop_seam(s, b, s.loop_head, len, out_l, out_r);
        if (spread) {
            i32 reverse_l, reverse_r;
            loop_seam(s, b, len8 - 1 - s.loop_head, len, reverse_l, reverse_r);
            out_l = reverse_l;
        }
    } else if (loop_mode == 2) {
        const i32 step = stretch_step;
        const i32 direction = step < 0 ? -1 : 1;
        u32 grain = 256 + ((length_control - 256) >> 3);
        if (grain > 4096) grain = 4096;
        if (grain > len) grain = len;
        if (!s.grain_size || s.grain_size > len) {
            s.grain_size = grain;
            s.grain_weight = 4194304u / grain;
            s.grain_phase = 0;
            s.grain_start[0] = s.grain_start[1] = s.loop_head;
        }
        const u32 g = s.grain_size;
        s.loop_head = wrap(s.loop_head + step, len8);
        for (u32 k = 0; k < 2; ++k) {
            u32 phase = s.grain_phase + (k ? g / 2 : 0);
            if (phase >= g) phase -= g;
            if (!phase) s.grain_start[k] = s.loop_head;
            const i32 position = wrap(s.grain_start[k] + direction * static_cast<i32>(phase << 8), len8);
            i32 l, r;
            loop_seam(s, b, position, len, l, r);
            const u32 triangle = phase < g / 2 ? phase : g - phase;
            const i32 gain = static_cast<i32>((triangle * s.grain_weight) >> 9);
            out_l += (l * gain) >> 12;
            out_r += (r * gain) >> 12;
        }
        if (++s.grain_phase >= g) {
            s.grain_phase = 0;
            if (s.grain_size != grain) {
                s.grain_size = grain;
                s.grain_weight = 4194304u / grain;
            }
        }
    } else {
        // Loud input holds the current slice; silence resumes scanning.
        const i32 a = in_l < 0 ? -in_l : in_l, c = in_r < 0 ? -in_r : in_r;
        const i32 peak = a > c ? a : c;
        if (peak > s.env_envelope) s.env_envelope = peak;
        else if (s.env_envelope) {
            const i32 decay = s.env_envelope >> 9;
            s.env_envelope -= decay ? decay : 1;
        }
        const u32 slice = 128 + ((length_control - 256) >> 4);
        const u32 slice_len = slice < len ? slice : len;
        const i32 slice8 = static_cast<i32>(slice_len << 8);
        const bool hold = s.env_envelope > threshold;
        if (hold && !s.env_mix) {
            const u32 at = static_cast<u32>(s.loop_head) >> 8;
            s.env_slice = static_cast<i32>((at / slice_len) * slice_len);
            if (static_cast<u32>(s.env_slice) + slice_len > len)
                s.env_slice = static_cast<i32>(len - slice_len);
            s.env_position = s.loop_head - (s.env_slice << 8);
        }
        if (hold) {
            s.env_position += 256;
            if (s.env_position >= slice8) s.env_position -= slice8;
        } else {
            s.loop_head = wrap(s.loop_head + 256, len8);
        }
        s.env_mix = approach(s.env_mix, hold ? 4096 : 0, 32);
        i32 a_l, a_r, b_l, b_r;
        loop_seam(s, b, s.loop_head, len, a_l, a_r);
        const i32 held = wrap((s.env_slice << 8) + s.env_position, len8);
        loop_seam(s, b, held, len, b_l, b_r);
        out_l = mix12(a_l, b_l, s.env_mix);
        out_r = mix12(a_r, b_r, s.env_mix);
    }
    out_l = (out_l * s.loop_gain) >> 12;
    out_r = (out_r * s.loop_gain) >> 12;
    if (mode == 3) {
        // Replace a quarter per pass, but hold the loop when input falls silent.
        const i32 a = in_l < 0 ? -in_l : in_l, c = in_r < 0 ? -in_r : in_r;
        const i32 peak = a > c ? a : c;
        if (peak > s.overdub_envelope) s.overdub_envelope = peak;
        else if (s.overdub_envelope)
            s.overdub_envelope -= (s.overdub_envelope >> 5) ? (s.overdub_envelope >> 5) : 1;
        const u32 at = ring_index(s.loop_start + s.overdub_position);
        if (s.overdub_envelope > 16) {
            const u32 old = static_cast<u32>(b.words[at]);
            const i32 l = q15(unpack(old, false), 24576) + q15(in_l, 8192);
            const i32 r = q15(unpack(old, true), 24576) + q15(in_r, 8192);
            b.words[at] = static_cast<i32>(pack(l, r));
        }
        if (++s.overdub_position >= s.loop_length) s.overdub_position = 0;
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
    const u32 wet_mode = choice(p[5], 3), loop_mode = choice(p[6], 3);
    const u32 mode = choice(p[7], 4);
    const u32 options = choice(p[1], 3);
    const bool smooth_clock = false, spread = options != 0;
    const u32 step = clock_step(32767, smooth_clock);
    const i32 targets[7] = {32767, static_cast<i32>(control(p[0])),
                            static_cast<i32>(control(p[2])), static_cast<i32>(control(p[3])),
                            static_cast<i32>(control(p[4])), 32767,
                            static_cast<i32>(step >> 1)};
    if (!s.controls_ready) {
        for (u32 k = 0; k < 7; ++k) s.smoothed[k] = targets[k];
        s.controls_ready = 1;
        s.previous_wet_mode = wet_mode;
        s.previous_loop_mode = loop_mode;
    }
    for (u32 k = 0; k < 4; ++k) s.smoothed[k] = smooth_block(s.smoothed[k], targets[k]);
    s.smoothed[6] = smooth_block(s.smoothed[6], targets[6]);
    // Coefficients follow the smoothed knobs once per block, not eight line
    // multiplications per sample.
    if (wet_mode == 0) {
        const u32 reverb_size = 18000u + ((static_cast<u32>(s.smoothed[0]) * 14000u) >> 15);
        for (u32 k = 0; k < 8; ++k) s.reverb_lengths[k] = (rev_lengths[k] * reverb_size) >> 15;
        s.reverb_feedback = 18000 + ((s.smoothed[0] * 13500) >> 15);
        s.reverb_diffusion = (s.smoothed[1] * 5) >> 3;
    }
    // Stop/restart is observable; the effect API cannot identify a double tap.
    if (!transport.playing && s.was_playing) {
        s.captured = s.ring_valid = s.listening = 0;
        s.loop_gain = 0;
    }
    s.was_playing = transport.playing ? 1u : 0u;
    if (mode == 1 && s.previous_mode != 1 && !s.captured) {
        s.ring_valid = 0; s.listening = 0;
    }
    if (wet_mode != s.previous_wet_mode) {
        s.transition = 256;
        s.previous_wet_mode = wet_mode;
        if (wet_mode == 2) s.grab_counter = 0;
    }
    if (loop_mode != s.previous_loop_mode) {
        s.grain_size = s.grain_phase = 0;
        s.env_mix = 0;
        s.previous_loop_mode = loop_mode;
    }
    s.previous_mode = mode;
    const u32 length = 1764 + ((static_cast<u32>(s.smoothed[2]) * 76000u) >> 15);
    const u32 wet_time = 882 + ((static_cast<u32>(s.smoothed[0]) * 31000u) >> 15);
    const u32 loop_modify = static_cast<u32>(s.smoothed[3]);
    const i32 tape_step = tape_rate[band(loop_modify, tape_bands, 7)];
    const i32 stretch_step = stretch_rate[band(loop_modify, stretch_bands, 8)];
    const i32 threshold = loop_modify < 256 ? 65535 :
        16000 - static_cast<i32>((loop_modify * 15500u) >> 15);
    const u32 wet_modify = static_cast<u32>(s.smoothed[1]);
    const i32 feedback = static_cast<i32>((wet_modify * 32112u) >> 15);
    const u32 k = (wet_modify * 128u) >> 15;
    const u32 semitone = k >= 64 ? ((k - 64) * 13) >> 6 : ((63 - k) * 13) >> 6;
    const i32 grab_rate = k >= 64 ? static_cast<i32>(grab_ratios[semitone]) :
                                    -static_cast<i32>(grab_ratios[semitone]);
    const u32 grab_period = 1102 + (wet_time >> 2);
    const u32 wet_spread_step = spread && wet_mode == 2 ? 2147483647u / grab_period : 0;
    const u32 clock = smooth_clock ? static_cast<u32>(s.smoothed[6]) * 2u : step;
    const i32 filter = clock >= 65534 ? 4096 : static_cast<i32>(clock >> 4);
    for (u32 i = 0; i < opxy::block_frames; ++i) {
        if (s.smoothed[4] != targets[4]) s.smoothed[4] = smooth_control(s.smoothed[4], targets[4]);
        if (s.smoothed[5] != targets[5]) s.smoothed[5] = smooth_control(s.smoothed[5], targets[5]);
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
            if (mode >= 2 && !s.captured && s.ring_valid >= 256) capture(s, length);
            i32 loop_l = 0, loop_r = 0;
            looper(s, b, mode, loop_mode, spread, length, tape_step, stretch_step, threshold,
                   x_l, x_r, loop_l, loop_r);
            if (spread && loop_mode != 0) {
                s.spread_phase += 25000u;
                const u32 wave = s.spread_phase >> 16;
                const i32 pan = static_cast<i32>(wave < 32768 ? wave : 65535 - wave);
                loop_l = q15(loop_l, 32767 - pan / 2);
                loop_r = q15(loop_r, 16384 + pan / 2);
            }
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
            if (!s.captured && transport.playing) {
                const i32 a = x_l < 0 ? -x_l : x_l, c = x_r < 0 ? -x_r : x_r;
                if (mode != 1 || a > 16 || c > 16) s.listening = 1;
                if (s.listening) {
                    // Record the incoming signal and some of the wet sound,
                    // just as DigiTide listens to the pedal's output.
                    b.words[s.ring_write] = static_cast<i32>(pack(x_l + wet_l / 2, x_r + wet_r / 2));
                    s.ring_write = ring_index(s.ring_write + 1);
                    if (s.ring_valid < loop_frames) ++s.ring_valid;
                    if (mode == 1 && s.ring_valid >= length) capture(s, length);
                }
            }
            s.wet_hold[0] = wet_l; s.wet_hold[1] = wet_r;
            s.loop_hold[0] = loop_l; s.loop_hold[1] = loop_r;
        }
        const i32 balance = s.smoothed[4];
        const i32 wet_gain = balance < 16384 ? 32767 : (32767 - balance) * 2;
        const i32 loop_gain = balance >= 16384 ? 32767 : balance * 2;
        const i32 lo_l = q15(s.loop_hold[0], loop_gain), lo_r = q15(s.loop_hold[1], loop_gain);
        // Above centre, increase the loop from unity to 1.5x while the wet
        // return fades out. Saturate only after mixing both channels.
        const i32 boost = balance > 16384 ? balance - 16384 : 0;
        const i32 out_l = clamp16(q15(s.wet_hold[0], wet_gain) + lo_l + q15(lo_l, boost));
        const i32 out_r = clamp16(q15(s.wet_hold[1], wet_gain) + lo_r + q15(lo_r, boost));
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
