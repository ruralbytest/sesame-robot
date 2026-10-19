#include "Scene.h"
static constexpr float PI=3.14159265f;
static uint16_t C(int r,int g,int b){return Painter::rgb(r,g,b);}
const char* moodName(Mood m){static const char* names[]={"CURIOUS","HAPPY","LOVE","SLEEPY","PARTY","WOW","BRAVE","SAD","THINKING"};return names[(int)m];}
void Scene::prepare(){
  for(int y=0;y<466;y++)for(int x=0;x<466;x++){
    float dx=x-233,dy=y-233,r=sqrtf(dx*dx+dy*dy)/233;
    float glow=fmaxf(0,1-r);
    background[y*466+x]=r>1?0:C(2+glow*9,3+glow*7,9+glow*21);
  }
}
void Scene::buildBase(){
  uint16_t* frame=g.p;g.p=base;
  memcpy(base,background,466*466*2);
  float cx=233,cy=235,t=0,dance=0;
  auto ink=C(37,21,49),cream=C(255,237,195),white=C(252,252,255),pink=C(255,98,156),cyan=C(55,231,236);
  // The lion's layered sunset mane occupies the round AMOLED naturally.
  if(character==0){
    g.ellipse(cx,cy+8,181,178,C(101,35,80),C(40,20,58));
    for(int i=0;i<14;i++){
      float a=i*2*PI/14+.04f*sinf(t*1.6f),r=139+3*sinf(t*2+i);
      uint16_t hi=Painter::mix(C(255,160,56),C(248,65,125),(.5f+.5f*sinf(a+t*dance)));
      g.ellipse(cx+cosf(a)*r,cy+sin(a)*r,40,43,hi,C(142,41,83));
    }
    g.ellipse(cx,cy,144,149,C(255,181,68),C(232,112,43));
  }else if(character==1){
    // Floppy-eared puppy, with a caramel patch over one eye.
    g.ellipse(cx-117,cy-30,54,121,C(143,84,69),C(70,37,65));
    g.ellipse(cx+117,cy-30,54,121,C(143,84,69),C(70,37,65));
  }else{
    g.ellipse(cx,cy,158,153,C(100,64,202),C(32,27,81));
    g.line(cx,cy-149,cx,cy-174,7,cyan);g.oval(cx,cy-181,10,10,pink);
    g.ellipse(cx-156,cy,17,38,C(71,237,244),C(33,125,178));g.ellipse(cx+156,cy,17,38,C(71,237,244),C(33,125,178));
  }
  if(character!=2){
    if(character==0){
      g.ellipse(cx-105,cy-109,38,43,C(255,203,106),C(213,114,61));g.ellipse(cx+105,cy-109,38,43,C(255,203,106),C(213,114,61));
      g.oval(cx-105,cy-112,22,26,C(207,101,105));g.oval(cx+105,cy-112,22,26,C(207,101,105));
    }
    g.ellipse(cx,cy+1,126,132,character==0?C(255,212,116):C(255,230,187),character==0?C(246,163,69):C(222,167,119));
    g.ellipse(cx-18,cy-55,83,61,character==0?C(255,226,145):C(255,240,209),character==0?C(255,206,109):C(252,219,168));
    if(character==1)g.ellipse(cx-60,cy-26,48,57,C(181,114,74),C(151,83,66));
    else{
      g.curve(cx,cy-99,18,12,.2f,2.8f,6,C(213,125,49));
      g.curve(cx,cy-84,11,9,.2f,2.8f,5,C(226,139,53));
    }
  }else g.ellipse(cx,cy+1,134,127,C(23,37,67),C(12,19,40));
  g.p=frame;cachedCharacter=character;
}
void Scene::draw(float t){
  if(cachedCharacter!=character)buildBase();
  auto ink=C(37,21,49),cream=C(255,237,195),white=C(252,252,255),pink=C(255,98,156),cyan=C(55,231,236);
  float dance=mood==PARTY?1:0,sleep=mood==SLEEPY?1:0;
  int dx=(int)(sinf(t*3)*dance*9),dy=(int)(sinf(t*(dance?5:2))*(dance?8:3));
  float cx=233+dx,cy=235+dy;
  // Move the cached character a few pixels for breathing/bouncing. This avoids
  // redrawing its hundreds of thousands of static pixels through slow PSRAM.
  for(int y=0;y<466;y++){
    uint16_t* row=g.p+y*466;int sy=y-dy;
    if(sy<0||sy>=466){memcpy(row,background+y*466,466*2);continue;}
    int left=std::max(0,dx),right=std::min(466,466+dx);
    if(left)memcpy(row,background+y*466,left*2);
    memcpy(row+left,base+sy*466+left-dx,(right-left)*2);
    if(right<466)memcpy(row+right,background+y*466+right,(466-right)*2);
  }
  // Orbiting stardust and the audio-reactive outer halo.
  for(int i=0;i<36;i++){
    float a=i*2*PI/36+t*.10f,r=211+4*sinf(t*1.3f+i);
    float pulse=.5f+.5f*sinf(t*2+i*1.7f);
    uint16_t col=Painter::mix(C(29,29,65),i%2?cyan:pink,pulse*.55f);
    g.oval(233+cosf(a)*r,233+sinf(a)*r,1.2f+pulse,1.2f+pulse,col);
    if(energy>.05f&&i%2==0)g.line(233+cosf(a)*218,233+sinf(a)*218,233+cosf(a)*(219+energy*9),233+sinf(a)*(219+energy*9),3,cyan);
  }
  for(int i=0;i<9;i++){
    float x=65+fmodf(i*91.f,338),y=65+fmodf(i*57.f+t*(i%2?4:-3)+5000,336);
    float dustR=(x-233)*(x-233)+(y-233)*(y-233);
    if(dustR>195*195||dustR<180*180)continue;
    g.star(x,y,2+1.5f*sinf(t+i),C(71,64,112));
  }
  // Eye movement eases toward touch or toward a wandering curiosity target.
  float gx=gazeX,gy=gazeY;
  float cycle=fmodf(t,4.7f),blink=cycle<.16f?sinf(cycle/.16f*PI):0;
  float open=(1-blink)*(sleep?.14f:1);
  if(mood==HAPPY||mood==LOVE)open*=.84f;
  for(int side=-1;side<=1;side+=2){
    float ex=cx+side*55,ey=cy-24;
    if(mood==SAD)ey+=6;
    if(open<.18f){g.curve(ex,ey-4,27,11,.15f,PI-.15f,7,character==2?cyan:ink);}
    else if(mood==LOVE){g.heart(ex,ey-8,30,pink);g.oval(ex-8,ey-15,5,5,white);}
    else{
      float ry=(mood==SURPRISED?43:37)*open;
      g.oval(ex,ey+3,38,ry+5,character==2?C(55,88,120):C(166,100,55));
      g.ellipse(ex,ey,35,ry,white,C(230,227,249));
      g.ellipse(ex+gx,ey+gy,22,ry*.87f,character==2?pink:cyan,character==2?C(121,47,173):C(16,114,167));
      g.oval(ex+gx,ey+gy+1,12,ry*.76f,C(15,24,43));
      g.oval(ex+gx-7,ey+gy-12*open,8,8*open,white);g.oval(ex+gx+8,ey+gy+10*open,3,3*open,white);
    }
    float by=ey-52;
    if(mood==BRAVE)g.line(ex-side*25,by+10,ex+side*22,by-4,8,ink);
    else if(mood==SAD)g.line(ex-side*22,by-5,ex+side*22,by+10,7,ink);
    else g.curve(ex,by+8,24,12,PI+.25f,2*PI-.25f,6,character==2?cyan:C(160,89,54));
    g.ellipse(cx+side*92,cy+28,22,11,character==2?C(175,69,177):C(255,144,120),character==2?C(110,46,137):C(248,154,108));
  }
  if(character!=2){
    g.ellipse(cx,cy+64,73,48,cream,C(255,216,165));
    g.oval(cx-26,cy+42,36,28,cream);g.oval(cx+26,cy+42,36,28,cream);
    g.ellipse(cx,cy+29,22,15,C(91,47,69),ink);g.oval(cx-6,cy+24,7,3,C(165,105,115));
    g.line(cx,cy+41,cx,cy+53,4,ink);
  }
  float mouthY=cy+(character==2?54:64);
  if(mood==SURPRISED||energy>.4f){g.oval(cx,mouthY,character==2?23:18,16+energy*16,ink);g.oval(cx,mouthY+10,10,5,pink);}
  else if(mood==SAD){g.curve(cx,mouthY+12,25,13,PI+.2f,2*PI-.2f,4,ink);}
  else if(sleep){g.oval(cx,mouthY+1,9,5,ink);}
  else if(mood==PARTY||mood==HAPPY||character==1){
    g.oval(cx,mouthY,31,21,ink);g.ellipse(cx,mouthY+13,16,14,C(255,127,160),C(226,67,130));g.line(cx,mouthY+8,cx,mouthY+19,2,C(195,55,105));
  }else{g.curve(cx-15,mouthY-10,17,16,.1f,PI-.1f,4,ink);g.curve(cx+15,mouthY-10,17,16,.1f,PI-.1f,4,ink);}
  if(character==0)for(int s=-1;s<=1;s+=2)for(int i=0;i<3;i++)g.oval(cx+s*(35+i%2*10),cy+47+i*9,2,2,C(182,121,92));
  // Floating emotion particles stay inside the circular safe region.
  if(mood==LOVE)for(int i=0;i<4;i++){float a=t*.4f+i*1.7f;g.heart(233+cosf(a)*165,233+sinf(a)*156,9+3*sinf(t+i),pink);}
  if(mood==PARTY||mood==HAPPY)for(int i=0;i<7;i++){float a=i*2*PI/7+t*.45f;g.star(233+cosf(a)*178,233+sinf(a)*173,5+2*sinf(t*3+i),i%2?cyan:C(255,226,119));}
  if(sleep){g.oval(cx+113,cy-119,18,18,C(255,233,155));g.oval(cx+121,cy-125,16,16,C(30,19,48));}
  if(mood==THINKING)for(int i=0;i<3;i++)g.oval(cx+75+i*17,cy-125-i*9,4+i*2,4+i*2,cyan);
  // Small status badge: teal = UART recently heard, violet = standalone.
  g.oval(233,399,64,17,C(15,18,36));g.text(moodName(mood),393,C(212,212,242),1);
  g.oval(233,428,3,3,linked?C(59,242,179):C(150,99,230));
  g.text(character==0?"AURORA":character==1?"BISCUIT":"NOVA",35,C(190,180,221),2);
}
