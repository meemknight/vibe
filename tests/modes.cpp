#include "../fx.cpp"
#include <cstdio>
#include <cstring>
using opxy::i32;
using opxy::u32;
static i32 mem[131072], left[64], right[64], p[12];
static vibe::State state;
static i32 option(u32 i, u32 n) {
    return static_cast<i32>((static_cast<unsigned long long>(2 * i + 1) * 2147483647u) / (2 * n));
}
static void controls() {
    std::memset(p, 0, sizeof(p));
    p[4] = 2147483647; p[7] = 0;
    p[5] = option(1, 3); p[6] = option(1, 4);
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
    p[3] = option(speed,8);
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
    for (u32 mode = 0; mode < 3; ++mode) {
        std::memset(mem,0,sizeof(mem)); vibe::initialize(state); controls();
        p[6] = option(0,4); p[4] = 0; p[5] = option(mode,3);
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
int main() { return loop_modes() && wet_modes() ? 0 : 1; }
