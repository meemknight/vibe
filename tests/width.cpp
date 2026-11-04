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
    p[7]=0; p[6]=opt(setting,3); p[4]=opt(1,6); p[3]=opt(0,4);
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
int main() {
    const auto wide=measure(1), extra=measure(2);
    if (!wide || extra <= wide) { std::puts("Extra Wide did not increase stereo separation"); return 1; }
    return 0;
}
