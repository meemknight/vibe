#include "../fx.cpp"
#include <cstdio>
static opxy::i32 mem[vibe::used_words], left[64], right[64], p[12];
static vibe::State state;
static opxy::i32 opt(unsigned i,unsigned n) {
    return static_cast<opxy::i32>((static_cast<unsigned long long>(2*i+1)*2147483647u)/(2*n));
}
static unsigned long long measure(unsigned setting) {
    vibe::initialize(state);
    for (unsigned i=0;i<12;++i) p[i]=0;
    p[2]=0; p[6]=opt(setting,3); p[4]=opt(1,6); p[3]=opt(0,4);
    unsigned long long sides=0;
    for (unsigned block=0;block<800;++block) {
        for (unsigned i=0;i<64;++i) {
            left[i]=block>=110 ? ((i&16)?268435456:-268435456) : 0;
            right[i]=0;
        }
        vibe::process(state,{mem,vibe::used_words},{1200,0,true},left,right,p);
        if (block>=700) for (unsigned i=0;i<64;++i) {
            const opxy::i32 difference=(left[i]>>16)-(right[i]>>16);
            sides+=difference<0 ? -difference : difference;
        }
    }
    return sides;
}
static bool moving_loop_pan() {
    for (unsigned i=0;i<vibe::used_words;++i) mem[i]=0;
    vibe::initialize(state);
    for (unsigned i=0;i<12;++i) p[i]=0;
    p[1]=opt(5,8); p[3]=opt(2,4); p[4]=opt(1,6);
    p[5]=1431655765; p[6]=2147483647;
    state.captured=1; state.loop_length=1764;
    for (unsigned i=0;i<1764;++i) vibe::tape_put({mem,vibe::used_words},i,4000);
    bool left_louder=false, right_louder=false;
    for (unsigned block=0;block<1700;++block) {
        for (unsigned i=0;i<64;++i) left[i]=right[i]=0;
        vibe::process(state,{mem,vibe::used_words},{1200,0,true},left,right,p);
        if (block>10) {
            left_louder |= (left[0] >> 16) > (right[0] >> 16) + 100;
            right_louder |= (right[0] >> 16) > (left[0] >> 16) + 100;
        }
    }
    return left_louder && right_louder;
}
int main() {
    const auto wide=measure(1), extra=measure(2);
    if (!wide || extra <= wide) { std::puts("Extra Wide did not increase stereo separation"); return 1; }
    if (!moving_loop_pan()) { std::puts("Tape did not pan across stereo"); return 1; }
    return 0;
}
