#include "../fx.cpp"
#include <cstdio>
#include <cstring>
using opxy::i32; using opxy::u32;
static i32 mem[vibe::used_words], left[64], right[64], p[8];
static vibe::State s;
static i32 opt(unsigned i,unsigned n){return static_cast<i32>((static_cast<unsigned long long>(2*i+1)*2147483647u)/(2*n));}
static void block(){for(unsigned i=0;i<64;++i) left[i]=right[i]=268435456; vibe::process(s,{mem,vibe::used_words},{1200,0,true},left,right,p);}
int main(){for(u32 mode=1;mode<=3;++mode){std::memset(mem,0,sizeof(mem));std::memset(p,0,sizeof(p));vibe::initialize(s);p[1]=opt(5,8);p[3]=opt(mode,4);p[4]=opt(1,6);p[5]=1431655765;
for(unsigned i=0;i<2200;++i)block();
if(!s.captured||s.ring_valid!=vibe::loop_frames||s.loop_length!=vibe::loop_frames||s.tape_length!=882){std::printf("mode %u did not retain full buffer while playing short loop\n",mode);return 1;}
if(mode==3 && (s.layer_length!=vibe::layer_frames||s.layer_valid!=vibe::layer_frames)){std::puts("overdub layer did not fill");return 1;}
p[7]=2147483647;for(unsigned i=0;i<250;++i)block();
if(s.tape_length<43000){std::printf("mode %u did not expand playback into recorded audio\n",mode);return 1;}
}return 0;}
