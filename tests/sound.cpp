#include "../fx.cpp"
#include <cstdio>
#include <cstring>

using opxy::i32;
using opxy::u32;
const i32 max_word = 2147483647;
static i32 memory_a[131074], memory_b[131074];
static i32 left[64], right[64], controls[12];
static vibe::State state_a, state_b;

static bool check(bool condition, const char* name) {
    if (!condition) std::printf("FAIL: %s\n", name);
    return condition;
}
static i32 option(unsigned value, unsigned count) {
    return static_cast<i32>((static_cast<unsigned long long>(2 * value + 1) * 2147483647u) / (2 * count));
}
static void defaults() {
    std::memset(controls, 0, sizeof(controls));
    controls[3] = option(5, 8);
    controls[7] = 0;
    controls[4] = max_word;
    controls[6] = max_word;
    controls[5] = option(1, 3); // delay
    controls[6] = option(0, 4);
}
static void block(vibe::State& state, i32* memory, bool playing, i32 sample) {
    for (unsigned i = 0; i < 64; ++i) {
        left[i] = sample;
        right[i] = sample;
    }
    vibe::process(state, {memory + 1, vibe::used_words}, {1200, 0, playing}, left, right, controls);
}
static bool startup_and_delay() {
    defaults();
    controls[4] = 0; // all wet
    std::memset(memory_a, 0x5a, sizeof(memory_a));
    vibe::initialize(state_a);
    for (unsigned i = 0; i < 64; ++i) left[i] = right[i] = 100;
    vibe::process(state_a, {memory_a + 1, vibe::used_words - 1}, {1200,0,true}, left, right, controls);
    if (!check(left[0] == 0 && right[0] == 0 && memory_a[1] == 0x5a5a5a5a,
               "short buffer was used")) return false;
    for (unsigned k = 0; k < 130; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i)
            if (!check(left[i] == 0 && right[i] == 0, "stale memory audible")) return false;
    }
    left[0] = 1073741824; right[0] = -1073741824;
    for (unsigned i = 1; i < 64; ++i) left[i] = right[i] = 0;
    vibe::process(state_a, {memory_a + 1, vibe::used_words}, {1200,0,true}, left, right, controls);
    bool echo = false;
    for (unsigned k = 0; k < 520; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i)
            if (left[i] > 1000000 && right[i] < -1000000) echo = true;
    }
    return check(echo, "delay does not return stereo impulse") &&
           check(memory_a[0] == 0x5a5a5a5a && memory_a[vibe::used_words + 1] == 0x5a5a5a5a,
                 "buffer boundary overwritten");
}
static bool auto_capture_and_stop() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[6] = option(1, 4);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 0);
    if (!check(!state_a.captured && !state_a.ring_valid, "auto captured silence")) return false;
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 536870912);
    if (!check(state_a.captured && state_a.loop_length == 1323, "auto did not capture full loop")) return false;
    bool heard = false;
    for (unsigned k = 0; k < 10; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i) heard |= left[i] > 1000000;
    }
    if (!check(heard, "captured loop is silent")) return false;
    block(state_a, memory_a, false, 0);
    if (!check(!state_a.captured && !state_a.ring_valid, "transport stop did not clear capture")) return false;
    block(state_a, memory_a, true, 0);
    return check(!state_a.captured && !state_a.ring_valid, "restart captured stale audio");
}
static bool hold_overdub_and_independence() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    std::memset(memory_b, 0, sizeof(memory_b));
    vibe::initialize(state_a); vibe::initialize(state_b);
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 268435456);
    controls[6] = option(2, 4);
    block(state_a, memory_a, true, 0);
    if (!check(state_a.captured && state_a.loop_length == 1323, "hold did not catch recent audio")) return false;
    const u32 index = vibe::ring_index(state_a.loop_start + 1);
    const i32 before = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    controls[6] = option(3, 4);
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 1073741824);
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, index) != before, "overdub did not update recorded audio")) return false;
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, index) -
               before > 1000,
               "overdub is too quiet after one pass")) return false;
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 0);
    const i32 after_silence = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 0);
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, index) == after_silence, "overdub erased a loop during silence")) return false;
    const i32 after = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    for (unsigned k = 0; k < 4; ++k) block(state_b, memory_b, false, 0);
    return check(state_a.captured && !state_b.captured && !state_b.ring_valid &&
                 vibe::tape_get({memory_a + 1, vibe::used_words}, index) == after, "send states or buffers shared");
}
static bool long_capture_wraps() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[2] = max_word;
    for (unsigned k = 0; k < 4900; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(state_a.ring_valid == vibe::loop_frames && state_a.ring_write < vibe::loop_frames,
               "long recording ring did not wrap safely")) return false;
    controls[6] = option(2, 4);
    block(state_a, memory_a, true, 0);
    return check(state_a.captured && state_a.loop_length > 153000,
                 "long capture did not retain the requested history");
}
static bool overdub_waits_for_input() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[6] = option(3, 4);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 64 * 131072);
    if (!check(!state_a.captured && !state_a.ring_valid, "overdub captured quiet input")) return false;
    for (unsigned k = 0; k < 20; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(!state_a.captured, "overdub captured before full length")) return false;
    for (unsigned k = 0; k < 30; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(state_a.captured && state_a.loop_length == 1323, "overdub initial capture differs from auto")) return false;
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 0);
    const u32 index = vibe::ring_index(state_a.loop_start + 1);
    const i32 before = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 64 * 131072);
    return check(vibe::tape_get({memory_a + 1, vibe::used_words}, index) == before, "quiet input modified the loop");
}
static bool mono_and_filtering() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    // Opposite-polarity stereo cancels in the mono tape path.
    for (unsigned k = 0; k < 20; ++k) {
        for (unsigned i = 0; i < 64; ++i) { left[i] = 536870912; right[i] = -536870912; }
        vibe::process(state_a, {memory_a + 1, vibe::used_words}, {1200,0,true}, left, right, controls);
    }
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, 300) == 0,
               "stereo input did not fold down to mono")) return false;
    // Nyquist-rate alternating input should be attenuated before decimation.
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    for (unsigned k = 0; k < 20; ++k) {
        for (unsigned i = 0; i < 64; ++i) left[i] = right[i] = (i & 1) ? 536870912 : -536870912;
        vibe::process(state_a, {memory_a + 1, vibe::used_words}, {1200,0,true}, left, right, controls);
    }
    const i32 filtered = vibe::tape_get({memory_a + 1, vibe::used_words}, 300);
    return check(filtered > -1000 && filtered < 1000, "downsampling filter did not suppress high frequencies");
}
static bool modes_and_extremes() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[6] = option(1, 4);
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 536870912);
    if (!check(state_a.captured, "mode test could not capture")) return false;
    u32 random = 7;
    for (unsigned wet = 0; wet < 3; ++wet)
        for (unsigned setting = 0; setting < 4; ++setting) {
            controls[5] = option(wet, 3);
            controls[1] = option(setting % 3, 3);
            controls[0] = setting & 2 ? max_word : 0;
            controls[3] = setting & 1 ? max_word : 0;
            for (unsigned k = 0; k < 8; ++k) {
                for (unsigned i = 0; i < 64; ++i) {
                    random = random * 1664525u + 1013904223u;
                    left[i] = static_cast<i32>(random);
                    random = random * 1664525u + 1013904223u;
                    right[i] = static_cast<i32>(random);
                }
                vibe::process(state_a, {memory_a + 1, vibe::used_words}, {1200, 0, true}, left, right, controls);
            }
        }
    return check(memory_a[0] == 0 && memory_a[vibe::used_words + 1] == 0, "mode sweep wrote outside buffer");
}
int main() {
    if (!startup_and_delay() || !auto_capture_and_stop() ||
        !hold_overdub_and_independence() || !long_capture_wraps() ||
        !overdub_waits_for_input() || !mono_and_filtering() || !modes_and_extremes()) return 1;
    std::puts("Vibe sound checks passed");
    return 0;
}
