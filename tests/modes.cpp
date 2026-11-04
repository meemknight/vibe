#include "../fx.cpp"
#include <cstdio>
#include <cstring>
#include <cmath>
using opxy::i32;
using opxy::u32;
static i32 mem[131072], left[64], right[64], p[12];
static vibe::State state;
static i32 option(u32 i, u32 n) {
    return static_cast<i32>((static_cast<unsigned long long>(2 * i + 1) * 2147483647u) / (2 * n));
}
static void controls() {
    std::memset(p, 0, sizeof(p));
    p[5] = 2147483647; p[2] = 0;
    p[4] = option(1, 6); p[3] = option(1, 4);
}
static i32 signal(u32 i) {
    const i32 phase = static_cast<i32>(i & 63u);
    const i32 triangle = phase < 32 ? phase : 63 - phase;
    return (triangle - 16) * 50000000;
}
static void run(bool source, u32& phase) {
    for (u32 i = 0; i < 64; ++i) {
        left[i] = source ? signal(phase++) : 0;
        right[i] = left[i];
    }
    vibe::process(state, {mem,vibe::used_words}, {1200,0,true}, left,right,p);
}
static bool failure(const char* text) { std::printf("FAIL: %s\n", text); return false; }
static bool capture() {
    std::memset(mem, 0, sizeof(mem)); vibe::initialize(state); controls();
    u32 phase = 0;
    for (u32 i = 0; i < 70; ++i) run(true, phase);
    return state.captured != 0;
}
static int crossings(u32 speed) {
    p[1] = option(speed,8);
    u32 phase = 0;
    for (u32 i = 0; i < 100; ++i) run(false,phase); // settle the control
    int crossings = 0;
    i32 last = 0;
    for (u32 i = 0; i < 200; ++i) {
        run(false,phase);
        for (u32 j = 0; j < 64; ++j) {
            if (last <= 0 && left[j] > 0) ++crossings;
            last = left[j];
        }
    }
    return crossings;
}
static bool loop_modes() {
    if (!capture()) return failure("capture");
    const int one = crossings(5), two = crossings(6);
    if (one < 80 || two < one * 3 / 2 || two > one * 5 / 2)
        return failure("tape speed does not change pitch by an octave");
    return true;
}
static bool wet_modes() {
    for (u32 mode = 0; mode < 6; ++mode) {
        std::memset(mem,0,sizeof(mem)); vibe::initialize(state); controls();
        p[3] = option(0,4); p[5] = 0; p[4] = option(mode,6);
        p[0] = 1073741824;
        u32 phase = 0;
        for (u32 i = 0; i < 130; ++i) run(false,phase); // clear the wet memory
        bool heard = false;
        for (u32 i = 0; i < 700; ++i) {
            run(true,phase);
            for (u32 j = 0; j < 64; ++j)
                heard |= left[j] > 1000000 || left[j] < -1000000;
        }
        if (!heard) return failure(mode == 0 ? "reverb silent" : mode == 1 ? "delay silent" : "grab silent");
    }
    return true;
}
static bool modulation_controls() {
    for (u32 mode = 3; mode < 6; ++mode) {
        long long signatures[2] = {};
        for (u32 setting = 0; setting < 2; ++setting) {
            std::memset(mem, 0, sizeof(mem)); vibe::initialize(state); controls();
            p[3] = option(0,4); p[5] = 0; p[4] = option(mode,6);
            p[0] = setting ? 2147483647 : 0;
            u32 phase = 0;
            for (u32 i = 0; i < 1100; ++i) {
                run(true,phase);
                if (i >= 200) for (u32 j = 0; j < 64; ++j)
                    signatures[setting] += static_cast<long long>(left[j] >> 16) * ((j & 1) ? 1 : -1);
            }
        }
        if (signatures[0] == signatures[1]) return failure("modulation parameter has no effect");
    }
    return true;
}
static bool modulation_return_is_wet() {
    for (u32 mode = 3; mode < 6; ++mode) {
        std::memset(mem, 0, sizeof(mem)); vibe::initialize(state);
        i32 l = 0, r = 0;
        vibe::modulation(state, {mem,vibe::used_words}, mode, 16384, false, 12000, 12000, l, r);
        if (mode >= 4 && (l || r)) return failure("modulated delay returned dry input immediately");
        if (mode == 3 && (l == 12000 || r == 12000)) return failure("phaser returned unprocessed input");
    }
    return true;
}
static bool chorus_has_stereo_motion() {
    std::memset(mem, 0, sizeof(mem)); vibe::initialize(state); controls();
    p[3] = option(0,4); p[5] = 0; p[4] = option(5,6); p[0] = 1073741824;
    u32 phase = 0; bool differs = false;
    for (u32 i = 0; i < 800; ++i) {
        run(true,phase);
        if (i > 300) for (u32 j = 0; j < 64; ++j) {
            const i32 difference = (left[j] >> 16) - (right[j] >> 16);
            if (difference > 500 || difference < -500) differs = true;
        }
    }
    return differs || failure("chorus has no audible stereo modulation");
}
static bool chorus_depth_changes_smoothly() {
    std::memset(mem, 0, sizeof(mem)); vibe::initialize(state);
    const opxy::Buffer buffer{mem, vibe::used_words};
    i32 l, r;
    for (unsigned i = 0; i < 300; ++i) vibe::modulation(state, buffer, 5, 0, false, 8000, 8000, l, r);
    for (unsigned i = 0; i < 500; ++i) {
        const i32 old_l = state.chorus_lag[0], old_r = state.chorus_lag[1];
        vibe::modulation(state, buffer, 5, 32767, false, 8000, 8000, l, r);
        if (state.chorus_lag[0] - old_l > 32 || state.chorus_lag[0] - old_l < -32 ||
            state.chorus_lag[1] - old_r > 32 || state.chorus_lag[1] - old_r < -32)
            return failure("chorus delay jumped while changing depth");
    }
    return true;
}
static bool phaser_sweep_audible(double frequency) {
    std::memset(mem, 0, sizeof(mem)); vibe::initialize(state); controls();
    p[3] = option(0,4); p[5] = 0; p[4] = option(3,6); p[0] = 2147483647;
    long long low = 0x7fffffffffffffffLL, high = 0;
    unsigned sample = 0;
    for (unsigned window = 0; window < 35; ++window) {
        long long energy = 0;
        for (unsigned block = 0; block < 32; ++block) {
            i32 dry[64];
            for (unsigned j = 0; j < 64; ++j) {
                dry[j] = static_cast<i32>(std::sin(6.283185307179586 * frequency * sample++ / 44100.0) * 268435456.0);
                left[j] = right[j] = dry[j];
            }
            vibe::process(state, {mem,vibe::used_words}, {1200,0,true}, left,right,p);
            for (unsigned j = 0; j < 64; ++j) {
                const i32 mix = (dry[j] >> 16) + (left[j] >> 16) * 7 / 10;
                energy += mix < 0 ? -mix : mix;
            }
        }
        if (window > 4) { if (energy < low) low = energy; if (energy > high) high = energy; }
    }
    return high > low * 3 / 2 || failure("phaser sweep is too weak in a dry-plus-send mix");
}
int main() { return loop_modes() && wet_modes() && modulation_controls() && modulation_return_is_wet() && chorus_has_stereo_motion() && chorus_depth_changes_smoothly() && phaser_sweep_audible(220.0) && phaser_sweep_audible(1200.0) ? 0 : 1; }
