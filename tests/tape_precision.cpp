#include "../fx.cpp"
#include <cstdio>
#include <cstring>
using namespace opxy;
static i32 mem[vibe::used_words+2],l[64],r[64],p[8];static vibe::State state;
int main(){for(int mode=1;mode<=2;mode++){std::memset(mem,0,sizeof(mem));std::memset(p,0,sizeof(p));vibe::initialize(state);mem[0]=mem[vibe::used_words+1]=0x12345678;p[3]=static_cast<i32>((static_cast<unsigned long long>(2*mode+1)*2147483647u)/6);p[4]=536870911;p[5]=1431655765;for(int b=0;b<300;b++){for(int i=0;i<64;i++)l[i]=r[i]=((b*64+i)/100&1)?8192:-8192;vibe::process(state,{mem+1,vibe::used_words},{1200,0,false},l,r,p);}if(!state.captured)return 1;bool nonzero=false;for(u32 i=0;i<state.ring_valid;i++)nonzero|=vibe::tape_get({mem+1,vibe::used_words},i)!=0;if(!nonzero){std::puts("quiet input recorded as silence");return 1;}if(mem[0]!=0x12345678 || mem[vibe::used_words+1]!=0x12345678)return 1;}return 0;}
