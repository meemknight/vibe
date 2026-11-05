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
    controls[1] = option(5, 8);
    controls[2] = 0;
    controls[5] = max_word;
    controls[3] = max_word;
    controls[4] = option(1, 6); // delay
    controls[3] = option(0, 4);
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
    controls[5] = 0; // all wet
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
    left[0] = 1073741824; right[0] = 0;
    for (unsigned i = 1; i < 64; ++i) left[i] = right[i] = 0;
    vibe::process(state_a, {memory_a + 1, vibe::used_words}, {1200,0,true}, left, right, controls);
    bool echo = false;
    for (unsigned k = 0; k < 520; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i)
            if (left[i] > 1000000 && right[i] > 1000000) echo = true;
    }
    return check(echo, "delay does not return mono impulse in both channels") &&
           check(memory_a[0] == 0x5a5a5a5a && memory_a[vibe::used_words + 1] == 0x5a5a5a5a,
                 "buffer boundary overwritten");
}
static bool auto_capture_and_stop() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[3] = option(1, 4);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 0);
    if (!check(!state_a.captured && !state_a.ring_valid, "auto captured silence")) return false;
    for (unsigned k = 0; k < 90; ++k) block(state_a, memory_a, true, 536870912);
    if (!check(state_a.captured && state_a.loop_length >= 1323 && state_a.tape_length == 662, "auto did not start with the requested playback length")) return false;
    bool heard = false;
    for (unsigned k = 0; k < 10; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i) heard |= left[i] > 1000000;
    }
    if (!check(heard, "captured loop is silent")) return false;
    block(state_a, memory_a, false, 0);
    if (!check(state_a.captured && state_a.ring_valid, "capture was cleared by unavailable transport")) return false;
    controls[3] = option(0, 4); block(state_a, memory_a, false, 0);
    return check(!state_a.captured && !state_a.ring_valid, "Off did not clear capture");
}
static bool auto_starts_before_full_capture() {
    defaults();
    std::memset(memory_a, 0x5a, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[7] = max_word; // maximum Length
    controls[3] = option(1, 4); // Auto
    for (unsigned k = 0; k < 700; ++k) block(state_a, memory_a, true, 536870912);
    if (!check(state_a.captured && state_a.loop_length >= 11025 && state_a.loop_length < 11625,
               "Auto did not start after one third of the selected length")) return false;
    bool audible = false;
    for (unsigned k = 0; k < 20; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i) audible |= left[i] > 1000000;
    }
    if (!check(audible, "Auto initial loop was silent")) return false;
    for (unsigned k = 0; k < 2050; ++k) block(state_a, memory_a, true, 268435456);
    return check(state_a.captured && state_a.loop_length == vibe::loop_frames,
                 "Auto did not finish recording the requested length");
}
static bool hold_overdub_and_independence() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    std::memset(memory_b, 0, sizeof(memory_b));
    vibe::initialize(state_a); vibe::initialize(state_b);
    controls[3] = option(2, 4);
    for (unsigned k = 0; k < 90; ++k) block(state_a, memory_a, true, 268435456);
    block(state_a, memory_a, true, 0);
    if (!check(state_a.captured && state_a.loop_length >= 1323, "hold did not record audio")) return false;
    const u32 index = vibe::ring_index(state_a.loop_start + 1);
    const i32 before = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    controls[3] = option(3, 4);
    for (unsigned k = 0; k < 1100; ++k) block(state_a, memory_a, true, 1073741824);
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, index) == before, "overdub altered original capture")) return false;
    if (!check(state_a.layer_valid == state_a.layer_length, "overdub did not record its layer")) return false;
    const u32 layer_index = vibe::layer_base + 1;
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, layer_index) > 1000,
               "overdub layer is too quiet")) return false;
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 0);
    const i32 after_silence = vibe::tape_get({memory_a + 1, vibe::used_words}, layer_index);
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 0);
    if (!check(vibe::tape_get({memory_a + 1, vibe::used_words}, layer_index) == after_silence, "overdub erased a loop during silence")) return false;
    const i32 after = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    for (unsigned k = 0; k < 4; ++k) block(state_b, memory_b, false, 0);
    return check(state_a.captured && !state_b.captured && !state_b.ring_valid &&
                 vibe::tape_get({memory_a + 1, vibe::used_words}, index) == after, "send states or buffers shared");
}
static bool hold_survives_reset_and_off_clears() {
    defaults(); std::memset(memory_a, 0x5a, sizeof(memory_a)); vibe::initialize(state_a);
    controls[3] = option(2, 4);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(state_a.captured, "empty Hold did not record")) return false;
    block(state_a, memory_a, false, 0);
    const u32 saved_start = state_a.loop_start, saved_length = state_a.loop_length;
    const i32 saved_sample = vibe::tape_get({memory_a + 1, vibe::used_words}, saved_start);
    vibe::initialize(state_a);
    block(state_a, memory_a, false, 0);
    if (!check(state_a.captured && state_a.loop_start == saved_start && state_a.loop_length == saved_length,
               "Hold did not restore across reset")) return false;
    bool heard = false;
    for (unsigned k = 0; k < 10; ++k) {
        block(state_a, memory_a, true, 0);
        for (unsigned i = 0; i < 64; ++i) heard |= left[i] > 1000000;
    }
    if (!check(heard && vibe::tape_get({memory_a + 1, vibe::used_words}, saved_start) == saved_sample,
               "restored Hold did not play the saved audio")) return false;
    controls[3] = option(1, 4); block(state_a, memory_a, true, 0);
    vibe::initialize(state_a); controls[3] = option(2, 4);
    block(state_a, memory_a, true, 0);
    if (!check(!state_a.captured, "Auto failed to clear saved Hold")) return false;
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(state_a.captured, "Hold could not record after Auto cleared it")) return false;
    controls[3] = option(0, 4); block(state_a, memory_a, true, 0);
    vibe::initialize(state_a); controls[3] = option(2, 4);
    for (unsigned k = 0; k < 10; ++k) block(state_a, memory_a, true, 0);
    return check(!state_a.captured && !state_a.ring_valid, "Off failed to clear saved Hold");
}
static bool long_capture_wraps() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[7] = max_word;
    controls[3] = option(1, 4);
    for (u32 i = (vibe::loop_frames * 3 + 3) / 4; i < (vibe::history_base * 3) / 4; ++i) memory_a[i + 1] = 0x12345678;
    for (unsigned k = 0; k < 2200; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(state_a.ring_valid == vibe::loop_frames && state_a.ring_write < vibe::loop_frames,
               "long recording ring did not wrap safely")) return false;
    controls[3] = option(2, 4);
    block(state_a, memory_a, true, 0);
    for (u32 i = (vibe::loop_frames * 3 + 3) / 4; i < (vibe::history_base * 3) / 4; ++i)
        if (!check(memory_a[i + 1] == 0x12345678, "reserved tape memory was overwritten")) return false;
    return check(state_a.captured && state_a.loop_length == vibe::loop_frames,
                 "long capture did not retain the requested history");
}
static bool overdub_waits_for_input() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[3] = option(3, 4);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 32);
    if (!check(!state_a.captured && !state_a.ring_valid, "overdub captured quiet input")) return false;
    for (unsigned k = 0; k < 20; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(!state_a.captured, "overdub captured before full length")) return false;
    for (unsigned k = 0; k < 70; ++k) block(state_a, memory_a, true, 268435456);
    if (!check(state_a.captured && state_a.loop_length >= 1323, "overdub did not start recording")) return false;
    for (unsigned k = 0; k < 50; ++k) block(state_a, memory_a, true, 0);
    const u32 index = vibe::ring_index(state_a.loop_start + 1);
    const i32 before = vibe::tape_get({memory_a + 1, vibe::used_words}, index);
    for (unsigned k = 0; k < 100; ++k) block(state_a, memory_a, true, 32);
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
    if (!check(state_a.tape_input_lp[1] == 0,
               "stereo input did not fold down to mono")) return false;
    // Nyquist-rate alternating input should be attenuated before decimation.
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    for (unsigned k = 0; k < 20; ++k) {
        for (unsigned i = 0; i < 64; ++i) left[i] = right[i] = (i & 1) ? 536870912 : -536870912;
        vibe::process(state_a, {memory_a + 1, vibe::used_words}, {1200,0,true}, left, right, controls);
    }
    const i32 filtered = state_a.tape_input_lp[1];
    return check(filtered > -1000 && filtered < 1000, "downsampling filter did not suppress high frequencies");
}
static bool layer_playback() {
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    const opxy::Buffer b{memory_a + 1, vibe::used_words};
    state_a.captured = 1; state_a.loop_length = 882; state_a.layer_length = 882;
    state_a.layer_valid = 882; state_a.loop_gain = 4096;
    for (u32 i = 0; i < 882; ++i) vibe::tape_put(b, vibe::layer_base + i, 8000);
    i32 l, r;
    for (unsigned i = 0; i < 100; ++i) vibe::looper(state_a, b, 2, 32767, 882, -341, 0, false, l, r);
    if (!check(l > 3000 && r > 3000, "overdub layer was not audible in Hold")) return false;
    vibe::initialize(state_a);
    return check(!state_a.layer_valid, "reset retained an old overdub layer");
}
static bool fragments_history_and_stop() {
    defaults(); std::memset(memory_a, 0, sizeof(memory_a)); vibe::initialize(state_a);
    for (unsigned k = 0; k < 150; ++k) block(state_a, memory_a, true, 268435456);
    const u32 base = vibe::wet_base - 16 + 1;
    u32* state = reinterpret_cast<u32*>(memory_a + base);
    if (!check(state[1] > 2048 && state[4] == 0, "history or zero-density behavior wrong")) return false;
    controls[2] = max_word;
    controls[3] = option(1, 4);
    for (unsigned k = 0; k < 450; ++k) {
        block(state_a, memory_a, true, 268435456);
        if (!check(state[4] == 0, "fragment started before enough history")) return false;
    }
    bool triggered = false, audible = false;
    for (unsigned k = 0; k < 250; ++k) {
        block(state_a, memory_a, true, 268435456);
        triggered |= state[4] != 0;
        if (state[4] && !check(state[3] >= 1323 && state[3] <= 13230, "fragment length outside safe range")) return false;
        for (unsigned i = 0; i < 64; ++i) audible |= left[i] > 1000000;
    }
    if (!check(triggered && audible, "fragment did not replay recorded history")) return false;
    for (unsigned k = 0; k < 1000; ++k) {
        block(state_a, memory_a, true, 268435456);
        if (!check(state[4] != 0, "continuous history tape stopped between fragments")) return false;
    }
    controls[3] = option(0, 4);
    block(state_a, memory_a, true, 0);
    if (!check(state[4] == 0, "Off retained fragment playback")) return false;
    block(state_a, memory_a, false, 0);
    return check(state[1] > 0 && state[4] == 0, "history or Off state lost when transport changed");
}
static bool modes_and_extremes() {
    defaults();
    std::memset(memory_a, 0, sizeof(memory_a));
    vibe::initialize(state_a);
    controls[3] = option(1, 4);
    for (unsigned k = 0; k < 90; ++k) block(state_a, memory_a, true, 536870912);
    if (!check(state_a.captured, "mode test could not capture")) return false;
    u32 random = 7;
    for (unsigned wet = 0; wet < 6; ++wet)
        for (unsigned setting = 0; setting < 4; ++setting) {
            controls[4] = option(wet, 6);
            controls[6] = option(setting % 3, 3);
            controls[0] = setting & 2 ? max_word : 0;
            controls[1] = setting & 1 ? max_word : 0;
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
    if (!startup_and_delay() || !auto_capture_and_stop() || !auto_starts_before_full_capture() ||
        !hold_overdub_and_independence() || !hold_survives_reset_and_off_clears() || !long_capture_wraps() ||
        !overdub_waits_for_input() || !mono_and_filtering() || !layer_playback() ||
        !fragments_history_and_stop() || !modes_and_extremes()) return 1;
    std::puts("Vibe sound checks passed");
    return 0;
}
