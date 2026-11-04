#include "../fx.cpp"
#include <cstdio>
using opxy::i32;
static i32 mem[3][vibe::used_words], l[3][64], r[3][64], p[3][12];
static vibe::State state[3];
static i32 option(unsigned i, unsigned n) { return static_cast<i32>((static_cast<unsigned long long>(2*i+1)*2147483647u)/(2*n)); }
int main() {
    for (unsigned n=0;n<3;++n) {
        vibe::initialize(state[n]);
        p[n][1]=option(5,8); p[n][5]=n==1 ? 0 : 1431655765;
        p[n][4]=option(1,6); p[n][3]=option(1,4); p[n][2]=n==2 ? 2147483647 : 0;
    }
    bool differs=false, wet=false;
    for (unsigned block=0;block<1000;++block) {
        for (unsigned n=0;n<3;++n) {
            for (unsigned i=0;i<64;++i) l[n][i]=r[n][i]=((block*64+i)&32) ? 67108864 : -67108864;
            vibe::process(state[n], {mem[n],vibe::used_words}, {1200,0,true}, l[n], r[n], p[n]);
        }
        if (block>=800) for (unsigned i=0;i<64;++i) {
            wet |= l[1][i]>100000 || l[1][i]< -100000;
            differs |= l[0][i]!=l[2][i];
        }
    }
    if (!wet) { std::puts("Wet return silent when loop volume is zero"); return 1; }
    if (!state[0].captured || !state[2].captured || !differs) {
        std::puts("Fragments did not change the output"); return 1;
    }
    return 0;
}

