// SPDX-License-Identifier: GPL-2.0-or-later
// Vibe adapts fixed-point algorithms from DigiTide by irpina and contributors.
// DigiTide: https://github.com/irpina/digitide, GPL-2.0-or-later.
// Reference revision: 8969fb9b7b9835101e89699ff32079e6741d753d.
// Its Digitone shell, global state, clock, and memory map are replaced here.
#include "core.h"
#include <opxy/q31.hpp>

namespace vibe {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::Buffer;

u32 ring_index(u32 index) { return index >= loop_frames ? index - loop_frames : index; }
const u32 wet_mask = wet_frames - 1;
const u32 wet_q8_mask = (wet_frames << 8) - 1;
const u32 rev_mask = reverb_line - 1;
static_assert((history_base + history_frames) * 3 <= wet_base * 4 - 88, "24-bit tape overlaps Hold metadata");
const i32 tape_rate[] = {-1365, -682, -341, -170, 170, 341, 682, 1365};
// The band boundaries and octave speeds follow DigiTide.
const u32 tape_bands[] = {5, 24, 43, 62, 85, 98, 120};
const u32 grab_ratios[] = {256,271,287,304,323,342,362,384,406,431,456,483,512};
const u32 rev_lengths[] = {691,827,941,1061,1181,1319,1481,1619};
const u32 ap_lengths[] = {36,95,38,92};

u32 control(i32 x) { return x < 0 ? 0u : static_cast<u32>(x) >> 16; }
u32 choice(i32 x, u32 count) { return (control(x) * count) >> 15; }
u32 band(u32 knob, const u32* bounds, u32 count) {
    const u32 value = (knob * 128u) >> 15;
    u32 i = 0;
    while (i < count && value >= bounds[i]) ++i;
    return i;
}
i32 clamp24(i32 x) { return x > 8388607 ? 8388607 : x < -8388608 ? -8388608 : x; }
i32 q15(i32 x, i32 y) { return static_cast<i32>(opxy::q31::wide(x, y) >> 15); }
i32 mix12(i32 a, i32 b, i32 gain) {
    return a + static_cast<i32>(opxy::q31::wide(b - a, gain) >> 12);
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
// Packed signed 24-bit little-endian samples, entirely before saved metadata.
i32 tape_get(Buffer b, u32 frame) {
    const opxy::u8* bytes = reinterpret_cast<const opxy::u8*>(b.words) + frame * 3;
    const u32 bits = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) | (static_cast<u32>(bytes[2]) << 16);
    return bits & 0x800000u ? static_cast<i32>(bits) - 0x1000000 : static_cast<i32>(bits);
}
void tape_put(Buffer b, u32 frame, i32 sample) {
    opxy::u8* bytes = reinterpret_cast<opxy::u8*>(b.words) + frame * 3;
    const u32 bits = static_cast<u32>(clamp24(sample));
    bytes[0] = static_cast<opxy::u8>(bits); bytes[1] = static_cast<opxy::u8>(bits >> 8); bytes[2] = static_cast<opxy::u8>(bits >> 16);
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

// Linear interpolation on a captured region. Unlike an arbitrary ring read,
// both samples are within audio that has actually been captured.
void loop_read(const State& s, Buffer b, i32 pos, u32 length, i32& l, i32& r) {
    const u32 whole = static_cast<u32>(pos) >> 10;
    const u32 frac = static_cast<u32>(pos) & 1023u;
    const u32 next = whole + 1 == length ? 0 : whole + 1;
    const i32 a = tape_get(b, ring_index(s.loop_start + whole));
    const i32 c = tape_get(b, ring_index(s.loop_start + next));
    l = a + (static_cast<i32>(opxy::q31::wide(c - a, static_cast<i32>(frac)) >> 10));
    r = l;
}

// Crossfade the end into the beginning, making both directions safe at seams.
void loop_seam(const State& s, Buffer b, i32 pos, u32 length, i32& l, i32& r) {
    loop_read(s, b, pos, length, l, r);
    const u32 fade = length >= 512 ? 128 : 64;
    const u32 whole = static_cast<u32>(pos) >> 10;
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
    s.loop_head = 0;
    s.overdub_position = 0;
    s.layer_length = layer_frames;
    s.layer_valid = 0; s.layer_head = 0; s.layer_play_length = s.layer_fade = 0; s.layer_last = 0;
    s.overdub_envelope = s.overdub_mix = 0;
    s.overdub_active = 0;
    s.tape_length = s.tape_old_length = s.tape_fade = 0;
    s.captured = s.loop_length >= 256;
}

i32 layer_sample(Buffer b, i32 pos, u32 length) {
    const u32 whole = static_cast<u32>(pos) >> 10;
    const u32 frac = static_cast<u32>(pos) & 1023u;
    const u32 next = whole + 1 == length ? 0 : whole + 1;
    const i32 a = tape_get(b, layer_base + whole);
    const i32 c = tape_get(b, layer_base + next);
    i32 sample = a + (static_cast<i32>(opxy::q31::wide(c - a, static_cast<i32>(frac)) >> 10));
    if (whole >= length - 128) {
        const i32 first = tape_get(b, layer_base);
        sample = mix12(sample, first, static_cast<i32>((whole - (length - 128)) * 32));
    }
    return sample;
}

void looper(State& s, Buffer b, u32 mode, u32 spread, u32 length_control,
            i32 tape_step,
            i32 input, bool tape_tick, i32& out_l, i32& out_r) {
    (void)spread;
    out_l = out_r = 0;
    const i32 target_gain = mode && s.captured ? 4096 : 0;
    s.loop_gain = approach(s.loop_gain, target_gain, 16);
    if (!s.captured || !s.loop_length || !s.loop_gain) return;
    u32 len = s.loop_length;
    // Length can shorten an already caught tape, as in DigiTide.
    if (length_control < len) len = length_control;
    if (len < 256) len = 256;
    if (!s.tape_length) s.tape_length = len;
    if (!s.tape_fade && len != s.tape_length) {
        s.tape_old_length = s.tape_length;
        s.tape_old_head = s.loop_head;
        s.tape_length = len;
        s.tape_fade = 256;
    }
    len = s.tape_length;
    const i32 len10 = static_cast<i32>(len << 10);
    if (s.loop_head >= len10 || s.loop_head < 0) s.loop_head = 0;
    {
        s.loop_head = wrap(s.loop_head + tape_step, len10);
        loop_seam(s, b, s.loop_head, len, out_l, out_r);
        if (s.tape_fade) {
            const i32 old_len = static_cast<i32>(s.tape_old_length << 10);
            s.tape_old_head = wrap(s.tape_old_head + tape_step, old_len);
            i32 old_l, old_r;
            loop_seam(s, b, s.tape_old_head, s.tape_old_length, old_l, old_r);
            const i32 old_gain = static_cast<i32>(s.tape_fade * 16);
            out_l = mix12(out_l, old_l, old_gain);
            out_r = mix12(out_r, old_r, old_gain);
            --s.tape_fade;
        }
    }
    u32 layer_play = s.layer_valid < len ? s.layer_valid : len;
    if (layer_play >= 256) {
        const i32 layer_span = static_cast<i32>(layer_play << 10);
        if (s.layer_play_length && layer_play < s.layer_play_length) {
            s.layer_fade_from = s.layer_last;
            s.layer_fade = 256;
        }
        s.layer_play_length = layer_play;
        if (s.layer_head >= layer_span || s.layer_head < 0) s.layer_head = 0;
        s.layer_head = wrap(s.layer_head + tape_step, layer_span);
        i32 layer_r = layer_sample(b, s.layer_head, layer_play);
        if (s.layer_fade) {
            layer_r = mix12(layer_r, s.layer_fade_from, static_cast<i32>(s.layer_fade * 16));
            --s.layer_fade;
        }
        s.layer_last = layer_r;
        out_l = clamp24(out_l + layer_r / 2);
        out_r = clamp24(out_r + layer_r / 2);
    }
    out_l = static_cast<i32>(opxy::q31::wide(out_l, s.loop_gain) >> 12);
    out_r = static_cast<i32>(opxy::q31::wide(out_r, s.loop_gain) >> 12);
    if (mode == 3) {
        // Gate very quiet input with hysteresis, then fade the write level to
        // avoid clicks. The envelope retains short gaps between notes.
        const i32 peak = input < 0 ? -input : input;
        if (peak > s.overdub_envelope) s.overdub_envelope = peak;
        else if (s.overdub_envelope)
            s.overdub_envelope -= (s.overdub_envelope >> 9) ? (s.overdub_envelope >> 9) : 1;
        if (!s.overdub_active && s.overdub_envelope >= 2) s.overdub_active = 1;
        if (s.overdub_active && s.overdub_envelope < 1) s.overdub_active = 0;
        s.overdub_mix = approach(s.overdub_mix, s.overdub_active ? 16384 : 0, 32);
        if (tape_tick) {
            const u32 at = layer_base + s.overdub_position;
            if (s.layer_valid < s.layer_length && (s.overdub_active || s.layer_valid)) {
                tape_put(b, at, input);
                if (++s.layer_valid == s.layer_length) { s.overdub_position = 0; s.layer_head = 0; }
                else ++s.overdub_position;
            } else if (s.layer_valid == s.layer_length && s.overdub_mix) {
                const i32 old = tape_get(b, at);
                tape_put(b, at, old + q15(input - old, s.overdub_mix));
                if (++s.overdub_position >= s.layer_length) s.overdub_position = 0;
            } else if (s.layer_valid == s.layer_length) {
                if (++s.overdub_position >= s.layer_length) s.overdub_position = 0;
            }
        }
    }
}

// Full Q31 wet path. These helpers use saturating SDK operations.
i32 wet_add(i32 a, i32 b) { return opxy::q31::add(a, b); }
i32 wet_sub(i32 a, i32 b) { return opxy::q31::add(opxy::q31::half(a), -opxy::q31::half(b)); }
i32 wet_gain(i32 x, i32 gain) { return opxy::q31::multiply(x, gain * 65536); }
i32 wet_mix(i32 a, i32 b, i32 gain) {
    return wet_add(opxy::q31::multiply(a, (4096-gain) * 524287), opxy::q31::multiply(b, gain * 524287));
}
i32 wet_tap_mono(Buffer b, u32 position) {
    const u32 a = (position >> 8) & wet_mask, frac = position & 255u;
    const i32 first = b.words[wet_base+a], second = b.words[wet_base+((a+1)&wet_mask)];
    return wet_mix(first, second, static_cast<i32>(frac*16));
}
i32 allpass(State& s, Buffer b, u32 line, i32 x, i32 gain) {
    const u32 at=allpass_base+line*allpass_line+s.allpass_write[line];
    const i32 previous=b.words[at];
    const i32 next=wet_sub(x, wet_gain(previous,gain));
    b.words[at]=wet_add(next,next);
    if (++s.allpass_write[line]==ap_lengths[line]) s.allpass_write[line]=0;
    return wet_add(previous,wet_gain(b.words[at],gain));
}
void reverb(State& s, Buffer b, i32 in_l, i32 in_r, i32& l, i32& r) {
    (void)in_r;
    const i32 x=allpass(s,b,1,allpass(s,b,0,opxy::q31::half(in_l),s.reverb_diffusion),s.reverb_diffusion);
    i32 y[8], sum=0;
    for(u32 k=0;k<8;++k){
        const i32 sample=b.words[reverb_base+k*reverb_line+((s.reverb_write-s.reverb_lengths[k])&rev_mask)];
        s.reverb_lp[k]=wet_add(wet_gain(s.reverb_lp[k],12768),wet_gain(sample,20000));
        y[k]=s.reverb_lp[k]; sum=wet_add(sum,opxy::q31::half(opxy::q31::half(y[k])));
    }
    for(u32 k=0;k<8;++k){
        const i32 delta=wet_sub(y[k],sum);
        b.words[reverb_base+k*reverb_line+s.reverb_write]=wet_add(wet_add(wet_gain(delta,s.reverb_feedback),wet_gain(delta,s.reverb_feedback)),x);
    }
    s.reverb_write=(s.reverb_write+1)&rev_mask;
    i32 out=0; for(u32 k=0;k<8;++k) out=wet_add(out,opxy::q31::half(opxy::q31::half(y[k])));
    l=r=out;
}
void delay(State& s, Buffer b, u32 target, i32 feedback, u32 spread, i32 in_l, i32 in_r, i32& l, i32& r) {
    (void)spread; (void)in_r;
    if(!s.delay_current) s.delay_current=target;
    if(!s.delay_fade && (target>s.delay_current+48 || target+48<s.delay_current)){s.delay_old=s.delay_current;s.delay_current=target;s.delay_fade=1024;}
    else if(!s.delay_fade) s.delay_current=target;
    i32 sample=b.words[wet_base+((s.wet_write-s.delay_current)&wet_mask)];
    if(s.delay_fade){const i32 old=b.words[wet_base+((s.wet_write-s.delay_old)&wet_mask)];sample=wet_mix(sample,old,static_cast<i32>(s.delay_fade*4));--s.delay_fade;}
    s.delay_lp[0]=wet_add(opxy::q31::half(s.delay_lp[0]),opxy::q31::half(sample));
    b.words[wet_base+s.wet_write]=wet_add(in_l,wet_gain(s.delay_lp[0],feedback));
    s.wet_write=(s.wet_write+1)&wet_mask; if(s.wet_valid<wet_frames)++s.wet_valid;
    l=r=s.delay_lp[0];
}
void grab(State& s, Buffer b, u32 period, i32 rate, i32 in_l, i32 in_r, i32& l, i32& r) {
    (void)in_r;
    if(s.grab_counter>=period)s.grab_counter=0;
    if(!s.grab_counter){const u32 extra=rate>256?(period*static_cast<u32>(rate-256))>>8:0;const u32 lag=extra+64<wet_frames?extra+64:wet_frames-1;s.grab_old=s.grab_head;s.grab_head=((s.wet_write-lag)&wet_mask)<<8;s.grab_fade=256;}
    if(++s.grab_counter>=period)s.grab_counter=0;
    i32 sample=wet_tap_mono(b,s.grab_head);s.grab_head=(s.grab_head+static_cast<u32>(rate))&wet_q8_mask;
    if(s.grab_fade){sample=wet_mix(sample,wet_tap_mono(b,s.grab_old),static_cast<i32>(s.grab_fade*16));s.grab_old=(s.grab_old+static_cast<u32>(rate))&wet_q8_mask;--s.grab_fade;}
    b.words[wet_base+s.wet_write]=in_l;s.wet_write=(s.wet_write+1)&wet_mask;if(s.wet_valid<wet_frames)++s.wet_valid;l=r=sample;
}
u32 modulation_wave(u32 phase) {
    const u32 x = phase >> 16;
    return x < 32768 ? x : 65535 - x;
}
void pick_fragment(u32 length, u32 history_valid, u32 history_write, i32 tape_step,
                   u32& random, u32& start, u32& size, i32& head, i32& step) {
    random = random * 1664525u + 1013904223u;
    size = length / 4 + ((random >> 16) * (length / 4) >> 16);
    if (size > 13230) size = 13230;
    const u32 safe_max = (history_valid - 768) / 2;
    if (size > safe_max) size = safe_max;
    if (size < 1323) size = 1323;
    const u32 age = size + 384 + random % (history_valid - 2 * size - 768 + 1);
    start = history_write + history_frames - age;
    if (start >= history_frames) start -= history_frames;
    step = tape_step;
    const u32 choice = (random >> 20) & 3u;
    if (choice == 0) step /= 2;
    if (choice == 1) step *= 2;
    if (random & 0x40000000u) step = -step;
    head = step < 0 ? static_cast<i32>((size << 10) - 1) : 0;
}
i32 fragment_sample(Buffer b, u32 start, u32 size, i32 head) {
    const u32 whole = static_cast<u32>(head) >> 10;
    u32 at = start + whole;
    if (at >= history_frames) at -= history_frames;
    const i32 a = tape_get(b, history_base + at);
    const u32 next = whole + 1 == size ? 0 : whole + 1;
    at = start + next;
    if (at >= history_frames) at -= history_frames;
    const i32 c = tape_get(b, history_base + at);
    i32 result = a + (static_cast<i32>(opxy::q31::wide(c - a, head & 1023) >> 10));
    if (whole >= size - 128) {
        const i32 first = tape_get(b, history_base + start);
        result = mix12(result, first, static_cast<i32>((whole - (size - 128)) * 32));
    }
    return result;
}
void modulation(State& s, Buffer b, u32 mode, u32 knob, u32 spread, i32 in_l, i32 in_r, i32& l, i32& r) {
    (void)spread; (void)in_r;
    s.modulation_phase += mode==5 ? 328000u : 160000u+knob*40u;
    const u32 wave=modulation_wave(s.modulation_phase);
    if(mode==3){
        i32 value=wet_add(in_l,wet_gain(s.phaser_feedback[0],20000));
        const i32 coefficient=-32300+static_cast<i32>((wave*26000u)>>15);
        for(u32 k=0;k<6;++k){const i32 x=value;const i32 difference=wet_sub(x,s.phaser_y[0][k]);const i32 y=wet_add(s.phaser_x[0][k],wet_add(wet_gain(difference,coefficient),wet_gain(difference,coefficient)));s.phaser_x[0][k]=x;s.phaser_y[0][k]=y;value=y;}
        s.phaser_feedback[0]=value;
        const i32 half_diff=wet_sub(value,opxy::q31::half(in_l));l=r=wet_add(half_diff,half_diff);
    }else{
        const u32 depth=mode==4?90u:125u+((knob*325u)>>15),base=mode==4?3u:110u;
        u32 lag=(base<<8)+((wave*depth)>>7);
        if(mode==5){if(!s.chorus_lag[0])s.chorus_lag[0]=static_cast<i32>(lag);s.chorus_lag[0]=approach(s.chorus_lag[0],static_cast<i32>(lag),16);lag=static_cast<u32>(s.chorus_lag[0]);}
        const i32 sample=wet_tap_mono(b,((s.wet_write<<8)-lag)&wet_q8_mask);
        b.words[wet_base+s.wet_write]=wet_add(in_l,wet_gain(sample,mode==4?25000:0));s.wet_write=(s.wet_write+1)&wet_mask;
        l=r=mode==5?wet_gain(sample,24576):wet_add(sample,opxy::q31::half(sample));
    }
}
} // namespace

void initialize(State& state) { state = State(); }

void process(State& s, Buffer b, const opxy::Transport& transport,
             opxy::Channel left, opxy::Channel right, opxy::Parameters p) {
    if (!b.words || b.count < used_words) {
        for (u32 i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = 0;
        return;
    }
    const u32 mode = choice(p[3], 4);
    // Hold metadata stays in the send scratch across effect resets. Audio is
    // only restored when both markers and all recorded bounds are valid.
    u32* const saved = reinterpret_cast<u32*>(b.words + wet_base - 22);
    if (!s.controls_ready && mode == 2 && saved[0] == 0x56494247u && saved[1] == 0x484f4c44u &&
        saved[2] < loop_frames && saved[3] >= 256 && saved[3] <= loop_frames &&
        saved[4] <= layer_frames && saved[5] <= saved[4]) {
        s.loop_start = saved[2]; s.loop_length = saved[3];
        s.ring_valid = saved[3]; s.ring_write = ring_index(saved[2] + saved[3]);
        s.layer_length = saved[4]; s.layer_valid = saved[5]; s.captured = 1;
        s.overdub_position = saved[5] < saved[4] ? saved[5] : 0;
    }
    // Fragment bookkeeping occupies sixteen words at the end of scratch, outside
    // the rolling audio ring; initialize before reading any prior contents.
    u32* const fragment_state = reinterpret_cast<u32*>(b.words + wet_base - 16);
    u32 &history_write = fragment_state[0], &history_valid = fragment_state[1];
    u32 &fragment_start = fragment_state[2], &fragment_length = fragment_state[3];
    u32 &fragment_remaining = fragment_state[4], &fragment_total = fragment_state[5];
    u32 &fragment_wait = fragment_state[6], &random_state = fragment_state[7];
    i32 &fragment_head = b.words[wet_base - 8], &fragment_step = b.words[wet_base - 7];
    u32 &next_start = fragment_state[10], &next_length = fragment_state[11];
    i32 &next_head = b.words[wet_base - 4], &next_step = b.words[wet_base - 3];
    u32 &current_pan = fragment_state[14];
    u32 &auto_filling = fragment_state[15];
    if (!s.controls_ready) for (u32 i = 0; i < 16; ++i) fragment_state[i] = 0;
    const bool wet_ready = clear_wet(s, b);
    const u32 wet_mode = choice(p[4], 6);
    const i32 targets[7] = {32767, static_cast<i32>(control(p[0])),
                            static_cast<i32>(control(p[7])), static_cast<i32>(control(p[1])),
                            static_cast<i32>(control(p[5])), static_cast<i32>(control(p[2])),
                            static_cast<i32>(control(p[6]))};
    if (!s.controls_ready) {
        for (u32 k = 0; k < 7; ++k) s.smoothed[k] = targets[k];
        s.controls_ready = 1;
        s.previous_wet_mode = wet_mode;
    }
    for (u32 k = 0; k < 4; ++k) s.smoothed[k] = smooth_block(s.smoothed[k], targets[k]);
    s.smoothed[5] = smooth_block(s.smoothed[5], targets[5]);
    s.smoothed[6] = smooth_block(s.smoothed[6], targets[6]);
    const u32 spread = static_cast<u32>(s.smoothed[6]);
    s.loop_pan_phase += 3000000u;
    const u32 loop_wave = s.loop_pan_phase >> 16;
    const i32 pan_wave = static_cast<i32>(loop_wave < 32768 ? loop_wave : 65535 - loop_wave) - 16384;
    const i32 loop_pan = q15(pan_wave / 2, static_cast<i32>(spread));
    const u32 density = static_cast<u32>(s.smoothed[5]);
    const i32 wet_moving = s.smoothed[1];
    // Coefficients follow the smoothed knobs once per block, not eight line
    // multiplications per sample.
    if (wet_mode == 0) {
        const u32 reverb_size = 18000u + ((static_cast<u32>(s.smoothed[0]) * 14000u) >> 15);
        for (u32 k = 0; k < 8; ++k) s.reverb_lengths[k] = (rev_lengths[k] * reverb_size) >> 17;
        s.reverb_feedback = 18000 + ((s.smoothed[0] * 13500) >> 15);
        s.reverb_diffusion = (wet_moving * 5) >> 3;
    }
    (void)transport; // Capture is audio-driven; the play flag is unavailable on device.
    if ((mode == 0 || mode == 1) && s.previous_mode != mode) {
        saved[0] = saved[1] = 0;
        s.captured = s.ring_valid = s.listening = 0; s.loop_gain = 0;
    }
    if (mode == 0) saved[0] = saved[1] = 0;
    if ((mode == 1 || mode == 2 || mode == 3) && s.previous_mode != mode && !s.captured) {
        s.ring_valid = 0; s.listening = 0;
    }
    if (mode == 0) {
        auto_filling = 0;
        fragment_remaining = next_length = fragment_wait = 0;
    }
    if (mode == 2 && s.previous_mode != mode && s.captured) auto_filling = 0;
    if (mode != 3 && s.previous_mode == 3) {
        s.overdub_envelope = s.overdub_mix = 0;
        s.overdub_active = 0;
    }
    if (wet_mode != s.previous_wet_mode) {
        s.transition = 256;
        s.previous_wet_mode = wet_mode;
        if (wet_mode == 2) s.grab_counter = 0;
        if (wet_mode == 5) s.chorus_lag[0] = s.chorus_lag[1] = 0;
    }
    s.previous_mode = mode;
    const u32 length_knob = static_cast<u32>(s.smoothed[2]);
    const u32 length = 662 + ((length_knob * 32414u) >> 15);
    // A longer requested loop gets a larger safe initial capture, while even
    // the shortest loop waits for at least 120 ms of incoming audio.
    const u32 initial_capture = mode != 0 && !s.captured && length > 3969 ? length / 3 : 1323;
    const u32 wet_time = (882 + ((static_cast<u32>(s.smoothed[0]) * 31000u) >> 15)) / 4;
    const u32 loop_modify = static_cast<u32>(s.smoothed[3]);
    const i32 tape_base = tape_rate[band(loop_modify, tape_bands, 7)];
    const i32 tape_step = tape_base * 3;
    const u32 wet_modify = static_cast<u32>(wet_moving);
    const i32 feedback = static_cast<i32>((wet_modify * 32112u) >> 15);
    const u32 k = (static_cast<u32>(s.smoothed[1]) * 128u) >> 15;
    const u32 semitone = k >= 64 ? ((k - 64) * 13) >> 6 : ((63 - k) * 13) >> 6;
    const i32 grab_base = k >= 64 ? static_cast<i32>(grab_ratios[semitone]) :
                                    -static_cast<i32>(grab_ratios[semitone]);
    const i32 grab_rate = grab_base;
    // Trigger at block boundaries; the fragment itself is played sample by
    // sample. The selected region is safely behind the live write head.
    // Wait for enough history to select at least 120 ms without approaching
    // the live write head, even at the shortest Length setting.
    if (mode && density && !fragment_remaining && history_valid >= 10584) {
        if (fragment_wait && density <= 16384) --fragment_wait;
        else {
            pick_fragment(length, history_valid, history_write, tape_step, random_state,
                          fragment_start, fragment_length, fragment_head, fragment_step);
            current_pan = (random_state >> 8) & 16383u;
            next_length = 0;
            fragment_total = fragment_remaining = fragment_length;
            const u32 spacing = 4 + (((32767u - density) * 180u) >> 15);
            fragment_wait = spacing + ((random_state >> 8) % (spacing + 1));
        }
    }
    const u32 grab_period = 276 + (wet_time >> 2);
    const i32 history_tape_gain = density > 16384 ? 32767 - static_cast<i32>((density - 16384) * 13 / 10) : 32767;
    const u32 active_looper = mode;
    for (u32 i = 0; i < opxy::block_frames / 4; ++i) {
        if (s.smoothed[4] != targets[4]) s.smoothed[4] = smooth_control(s.smoothed[4], targets[4]);
        // Four-tap binomial anti-alias filter before quarter-rate processing.
        const i32 x_l = ((left[4*i] >> 9) + 3*(left[4*i+1] >> 9) + 3*(left[4*i+2] >> 9) + (left[4*i+3] >> 9)) / 8;
        const i32 x_r = ((right[4*i] >> 9) + 3*(right[4*i+1] >> 9) + 3*(right[4*i+2] >> 9) + (right[4*i+3] >> 9)) / 8;
        const i32 mono = (x_l + x_r) / 2;
        s.tape_input_lp[0] += ((mono - s.tape_input_lp[0]) * 3) / 8;
        s.tape_input_lp[1] += ((s.tape_input_lp[0] - s.tape_input_lp[1]) * 3) / 8;
        const bool tape_tick = true;
        if (tape_tick) {
            tape_put(b, history_base + history_write, s.tape_input_lp[1]);
            if (++history_write == history_frames) history_write = 0;
            if (history_valid < history_frames) ++history_valid;
        }
        i32 loop_l = 0, loop_r = 0;
        if (s.captured || s.loop_gain)
            looper(s, b, active_looper, spread, length, tape_step,
                   s.tape_input_lp[1], tape_tick, loop_l, loop_r);
        if (spread && s.captured) {
            loop_l = q15(loop_l, 32767 - loop_pan);
            loop_r = q15(loop_r, 32767 + loop_pan);
        }
        const i32 feed_l = wet_add(wet_add((left[4*i] >> 4), 3*(left[4*i+1] >> 4)),wet_add(3*(left[4*i+2] >> 4),left[4*i+3] >> 4)) +
                           wet_add(wet_add((right[4*i] >> 4), 3*(right[4*i+1] >> 4)),wet_add(3*(right[4*i+2] >> 4),right[4*i+3] >> 4));
        const i32 feed_r = feed_l;
        i32 wet_l = 0, wet_r = 0;
        if (wet_ready) {
            if (wet_mode == 0)
                reverb(s, b, feed_l, feed_r, wet_l, wet_r);
            else if (wet_mode == 1)
                delay(s, b, wet_time, feedback, spread, feed_l, feed_r, wet_l, wet_r);
            else if (wet_mode == 2)
                grab(s, b, grab_period, grab_rate, feed_l, feed_r, wet_l, wet_r);
            else
                modulation(s, b, wet_mode, static_cast<u32>(wet_moving), spread, feed_l, feed_r, wet_l, wet_r);
        }
        if (s.transition) {
            wet_l = wet_mix(wet_l, s.previous_wet[0], static_cast<i32>(s.transition * 16));
            wet_r = wet_mix(wet_r, s.previous_wet[1], static_cast<i32>(s.transition * 16));
            --s.transition;
        } else {
            s.previous_wet[0] = wet_l;
            s.previous_wet[1] = wet_r;
        }
        if (spread && s.captured) {
            const i32 loop_mid = (loop_l + loop_r) / 2, loop_side = (loop_l - loop_r) / 2;
            const i32 loop_extra = q15(loop_side, static_cast<i32>(spread)) / 2;
            loop_l = clamp24(loop_mid + loop_side + loop_extra);
            loop_r = clamp24(loop_mid - loop_side - loop_extra);
        }
        if (mode && fragment_remaining) {
            const bool continuous = density > 16384;
            if (!continuous) next_length = 0;
            if (continuous && fragment_remaining <= 110 && !next_length)
                pick_fragment(length, history_valid, history_write, tape_step, random_state,
                              next_start, next_length, next_head, next_step);
            const i32 grain = fragment_sample(b, fragment_start, fragment_length, fragment_head);
            const u32 elapsed = fragment_total - fragment_remaining;
            u32 fade = elapsed < 110 ? elapsed : 110;
            if (!continuous && fragment_remaining < fade) fade = fragment_remaining;
            const i32 level = q15(static_cast<i32>(fade * 252u), static_cast<i32>(density));
            i32 added = q15(grain, level);
            i32 fragment_l = q15(added, 16384 + static_cast<i32>(current_pan));
            i32 fragment_r = q15(added, 32767 - static_cast<i32>(current_pan));
            if (continuous && next_length) {
                const i32 next_grain = fragment_sample(b, next_start, next_length, next_head);
                const i32 next_added = q15(next_grain, level);
                const i32 pan = static_cast<i32>((random_state >> 8) & 16383u);
                const i32 fade_next = static_cast<i32>(((110u - fragment_remaining) * 4096u) / 110u);
                fragment_l = mix12(fragment_l, q15(next_added, 16384 + pan), fade_next);
                fragment_r = mix12(fragment_r, q15(next_added, 32767 - pan), fade_next);
                next_head = wrap(next_head + next_step, static_cast<i32>(next_length << 10));
            }
            if (continuous) {
                loop_l = q15(loop_l, history_tape_gain); loop_r = q15(loop_r, history_tape_gain);
            }
            loop_l = clamp24(loop_l + fragment_l); loop_r = clamp24(loop_r + fragment_r);
            fragment_head = wrap(fragment_head + fragment_step, static_cast<i32>(fragment_length << 10));
            --fragment_remaining;
            if (!fragment_remaining && continuous && next_length) {
                fragment_start = next_start; fragment_length = next_length;
                fragment_head = next_head; fragment_step = next_step;
                fragment_total = fragment_length;
                fragment_remaining = fragment_total - 110;
                current_pan = (random_state >> 8) & 16383u;
                next_length = 0;
            }
        }
        if (!mode && s.captured && !s.loop_gain) s.captured = 0;
        const i32 record_mono = (x_l + x_r) / 2 + (wet_l >> 9) / 2;
        s.tape_record_lp[0] += ((record_mono - s.tape_record_lp[0]) * 3) / 8;
        s.tape_record_lp[1] += ((s.tape_record_lp[0] - s.tape_record_lp[1]) * 3) / 8;
        if (mode != 0 && (!s.captured || auto_filling)) {
            if (feed_l > 512 || feed_l < -512) s.listening = 1;
            if (s.listening && tape_tick) {
                // Record the incoming signal and some of the wet sound,
                // just as DigiTide listens to the pedal's output.
                tape_put(b, s.ring_write, s.tape_record_lp[1]);
                s.ring_write = ring_index(s.ring_write + 1);
                if (s.ring_valid < loop_frames) ++s.ring_valid;
                if (!s.captured && s.ring_valid >= initial_capture) {
                    // Start from a valid portion, then continue writing
                    // until the chosen length is available.
                    capture(s, s.ring_valid);
                    auto_filling = s.ring_valid < loop_frames;
                } else if (auto_filling) {
                    s.loop_length = s.ring_valid;
                    if (s.ring_valid >= loop_frames) auto_filling = 0;
                }
            }
        }
        const i32 loop_gain = s.smoothed[4];
        const i32 lo_l = q15(loop_l, loop_gain), lo_r = q15(loop_r, loop_gain);
        // Linear 0 to 1.5x loop gain; wet gain is independent.
        const i32 out_l = wet_add(wet_l, clamp24(lo_l + lo_l / 2) * 256);
        const i32 out_r = wet_add(wet_r, clamp24(lo_r + lo_r / 2) * 256);
        for (u32 j = 0; j < 4; ++j) {
            left[4*i+j] = wet_mix(s.half_previous[0], out_l, static_cast<i32>((j+1)*1024));
            right[4*i+j] = wet_mix(s.half_previous[1], out_r, static_cast<i32>((j+1)*1024));
        }
        s.half_previous[0] = out_l; s.half_previous[1] = out_r;
    }
    if (mode == 2 && s.captured) {
        saved[2] = s.loop_start; saved[3] = s.loop_length;
        saved[4] = s.layer_length; saved[5] = s.layer_valid;
        saved[0] = 0x56494247u; saved[1] = 0x484f4c44u;
    }
}
} // namespace vibe
