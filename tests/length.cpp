#include "../fx.cpp"
#include <cstdio>
using namespace vibe;
static opxy::i32 memory[used_words];
static bool check(bool spread, opxy::i32 step) {
    State s{}; Buffer b{memory, used_words};
    s.captured=1; s.loop_length=8000; s.tape_length=8000;
    s.loop_head=6000 << 10; s.loop_gain=4096;
    for (opxy::u32 i=0;i<8000;++i) tape_put(b,i,i<4000 ? -12000 : 12000);
    opxy::i32 l=0,r=0; looper(s,b,2,spread,8000,step,0,false,l,r);
    opxy::i32 prev_l=l,prev_r=r;
    for (int i=0;i<256;++i) {
        looper(s,b,2,spread,3000,step,0,false,l,r);
        if (l-prev_l > 1200 || l-prev_l < -1200 || r-prev_r > 1200 || r-prev_r < -1200) {
            std::puts("length change caused an abrupt sample jump"); return false;
        }
        prev_l=l; prev_r=r;
    }
    return s.tape_length==3000 && s.tape_fade==0;
}
int main() { return check(false,341) && check(false,-341) && check(true,341) && check(true,-341) ? 0 : 1; }
