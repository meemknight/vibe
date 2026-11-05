#include "../fx.cpp"
#include <cstdio>
#include <cstring>
using namespace opxy;
static i32 mem[vibe::used_words],l[64],r[64],p[8]; static vibe::State state;
int main(){for(unsigned mode=1;mode<=2;mode++){std::memset(mem,0,sizeof(mem));std::memset(p,0,sizeof(p));vibe::initialize(state);p[3]=static_cast<i32>((static_cast<unsigned long long>(2*mode+1)*2147483647u)/6);p[4]=536870911;p[5]=1431655765;for(int b=0;b<100;b++){for(int i=0;i<64;i++)l[i]=r[i]=268435456;vibe::process(state,{mem,vibe::used_words},{1200,0,false},l,r,p);}if(!state.captured){std::printf("mode %u did not capture while stopped\n",mode);return 1;}bool heard=false;for(int b=0;b<40;b++){for(int i=0;i<64;i++)l[i]=r[i]=0;vibe::process(state,{mem,vibe::used_words},{1200,0,false},l,r,p);for(int i=0;i<64;i++)if(l[i]>100000 || l[i]<-100000)heard=true;}if(!heard)return 1;}return 0;}
