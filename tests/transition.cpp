#include "../fx.cpp"
#include <cstdio>
using namespace opxy;
static i32 mem[vibe::used_words],l[64],r[64],p[8];
int main(){vibe::State s{};s.controls_ready=1;s.previous_wet_mode=0;s.previous_wet[0]=s.previous_wet[1]=200000000;s.half_previous[0]=s.half_previous[1]=200000000;p[4]=536870911;i32 last=200000000;for(int block=0;block<20;block++){for(int i=0;i<64;i++)l[i]=r[i]=0;if(block==3)p[4]=894784853;vibe::process(s,{mem,vibe::used_words},{1200,0,false},l,r,p);for(int i=0;i<64;i++){i32 diff=l[i]-last;if(diff>3000000 || diff< -3000000){std::puts("mode switch caused abrupt wet jump");return 1;}last=l[i];}}if(last>100000||last< -100000)return 1;return 0;}
