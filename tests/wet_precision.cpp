#include "../fx.cpp"
#include <cstdio>
#include <cstring>
using namespace opxy;
static i32 memory[vibe::used_words],l[64],r[64],p[8]; static vibe::State state;
int main(){for(unsigned mode=0;mode<6;mode++){std::memset(memory,0,sizeof(memory));std::memset(p,0,sizeof(p));p[4]=static_cast<i32>((static_cast<unsigned long long>(2*mode+1)*2147483647u)/12);p[0]=751619276;vibe::initialize(state);bool heard=false;for(unsigned b=0;b<900;b++){for(unsigned i=0;i<64;i++)l[i]=r[i]=(((b*64+i)/100)&1)?8192:-8192;vibe::process(state,{memory,vibe::used_words},{1200,0,true},l,r,p);if(b>500)for(unsigned i=0;i<64;i++)if(l[i]>256 || l[i]<-256)heard=true;}if(!heard){std::printf("mode %u lost low-level wet input\n",mode);return 1;}}return 0;}
