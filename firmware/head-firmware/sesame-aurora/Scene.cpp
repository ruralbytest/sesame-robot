#include "Scene.h"
static constexpr float PI=3.14159265f;
static uint16_t C(int r,int g,int b){return Painter::rgb(r,g,b);}
const char* moodName(Mood m){static const char* names[]={"CURIOUS","HAPPY","LOVE","SLEEPY","PARTY","WOW","BRAVE","SAD","THINKING"};return names[(int)m];}
static const char* const moveCommands[MOVE_COUNT]={"forward","backward","left","right","rest","stand","wave","dance","swim","point","pushup","bow","cute","freaky","worm","shake","shrug","dead","crab"};
// Badge text: the painter's font is A-Z only.
static const char* const moveLabels[MOVE_COUNT]={"WALK","BACK","LEFT","RIGHT","REST","READY","HELLO","DANCE","SWIM","POINT","PUSHUP","BOW","CUTE","FREAKY","WORM","SHAKE","SHRUG","DEAD","CRAB"};
const char* moveLabel(Move m){return m>=0&&m<MOVE_COUNT?moveLabels[m]:"";}
Move moveFromName(const char* name){
  if(!strcmp(name,"walk"))return M_FORWARD; // body face shared by all four walking commands
  for(int i=0;i<MOVE_COUNT;i++)if(!strcmp(name,moveCommands[i]))return (Move)i;
  return NO_MOVE;
}
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

// ---- Expressions ----
// A Look is one complete expression: eyes, brows, mouth, eye shape and gaze,
// how the head moves, and which animated extra is drawn around it.
enum EyeStyle{EYE_OPEN,EYE_SPARKLE,EYE_HAPPY,EYE_SLEEP,EYE_HEART,EYE_X,EYE_SPIRAL,EYE_SQUEEZE};
enum BrowStyle{BROW_SOFT,BROW_ANGRY,BROW_SAD,BROW_RAISED,BROW_ASYM,BROW_NONE};
enum MouthStyle{MOUTH_W,MOUTH_OPEN,MOUTH_O,MOUTH_FLAT,MOUTH_SAD,MOUTH_GRIT,MOUTH_TONGUE,MOUTH_WAVY,MOUTH_SMALL};
enum Extra{X_NONE,X_STARS,X_HEARTS,X_MOON,X_DOTS,X_FOOTPRINTS,X_CHEVRONS,X_ARROW_L,X_ARROW_R,X_ZZZ,X_SPARKLES,X_PAW,
           X_NOTES,X_BUBBLES,X_POINTER,X_SWEAT,X_CUTE,X_SWIRLS,X_WORM,X_SHAKE_LINES,X_QUESTION,X_HALO,X_CLAWS};
struct Look{
  EyeStyle eye=EYE_OPEN;BrowStyle brow=BROW_SOFT;MouthStyle mouth=MOUTH_W;Extra extra=X_NONE;
  float open=1,tall=37,eyeDrop=0,gx=0,gy=0,cheek=1;int dx=0,dy=0;
};
static Look moodLook(Mood m,float t){
  Look k;k.dy=(int)(sinf(t*2)*3);
  switch(m){
    case HAPPY:k.open=.84f;k.mouth=MOUTH_OPEN;k.extra=X_STARS;break;
    case LOVE:k.eye=EYE_HEART;k.extra=X_HEARTS;break;
    case SLEEPY:k.eye=EYE_SLEEP;k.mouth=MOUTH_SMALL;k.extra=X_MOON;break;
    case PARTY:k.open=.84f;k.mouth=MOUTH_OPEN;k.extra=X_STARS;k.dx=(int)(sinf(t*3)*9);k.dy=(int)(sinf(t*5)*8);break;
    case SURPRISED:k.tall=43;k.mouth=MOUTH_O;break;
    case BRAVE:k.brow=BROW_ANGRY;break;
    case SAD:k.eyeDrop=6;k.brow=BROW_SAD;k.mouth=MOUTH_SAD;break;
    case THINKING:k.extra=X_DOTS;break;
    default:break;
  }
  return k;
}
static Look moveLook(Move m,float t){
  Look k;k.dy=(int)(sinf(t*2)*3);
  switch(m){
    case M_FORWARD:k.open=.9f;k.mouth=MOUTH_OPEN;k.extra=X_FOOTPRINTS;k.dy=(int)(3-fabsf(sinf(t*6))*8);break;
    case M_BACKWARD:k.tall=43;k.gy=-6;k.brow=BROW_RAISED;k.mouth=MOUTH_O;k.extra=X_CHEVRONS;k.dy=(int)(sinf(t*6)*3);break;
    case M_LEFT:k.open=.9f;k.gx=-13;k.extra=X_ARROW_L;k.dx=-8+(int)(sinf(t*3)*3);break;
    case M_RIGHT:k.open=.9f;k.gx=13;k.extra=X_ARROW_R;k.dx=8+(int)(sinf(t*3)*3);break;
    case M_REST:k.eye=EYE_SLEEP;k.mouth=MOUTH_SMALL;k.extra=X_ZZZ;k.dy=(int)(sinf(t*1.2f)*4);break;
    case M_STAND:k.tall=40;k.brow=BROW_RAISED;k.mouth=MOUTH_OPEN;k.extra=X_SPARKLES;k.dy=(int)(-fabsf(sinf(t*4))*6);break;
    case M_WAVE:k.eye=EYE_HAPPY;k.mouth=MOUTH_OPEN;k.extra=X_PAW;break;
    case M_DANCE:k.open=.84f;k.mouth=MOUTH_OPEN;k.extra=X_NOTES;k.dx=(int)(sinf(t*3)*9);k.dy=(int)(sinf(t*5)*8);break;
    case M_SWIM:k.open=.8f;k.mouth=MOUTH_O;k.extra=X_BUBBLES;k.dx=(int)(sinf(t*2)*10);k.dy=(int)(sinf(t*4)*4);break;
    case M_POINT:k.gx=15;k.gy=-2;k.brow=BROW_ASYM;k.extra=X_POINTER;break;
    case M_PUSHUP:k.open=.7f;k.brow=BROW_ANGRY;k.mouth=MOUTH_GRIT;k.extra=X_SWEAT;k.dy=(int)(sinf(t*4)*12);break;
    case M_BOW:k.eye=EYE_HAPPY;k.cheek=1.4f;k.extra=X_SPARKLES;k.dy=(int)(10+sinf(t*1.5f)*6);break;
    case M_CUTE:k.eye=EYE_SPARKLE;k.tall=42;k.cheek=1.5f;k.extra=X_CUTE;k.dx=(int)(sinf(t*2)*4);break;
    case M_FREAKY:k.eye=EYE_SPIRAL;k.brow=BROW_ASYM;k.mouth=MOUTH_WAVY;k.extra=X_SWIRLS;
      k.dx=(int)(sinf(t*23)*4+sinf(t*37)*3);k.dy=(int)(sinf(t*29)*3);break;
    case M_WORM:k.eye=EYE_HAPPY;k.mouth=MOUTH_WAVY;k.extra=X_WORM;k.dx=(int)(sinf(t*3)*12);k.dy=(int)(sinf(t*6)*4);break;
    case M_SHAKE:k.eye=EYE_SQUEEZE;k.extra=X_SHAKE_LINES;k.dx=(int)(sinf(t*30)*10);k.dy=0;break;
    case M_SHRUG:k.open=.85f;k.gy=-6;k.brow=BROW_ASYM;k.mouth=MOUTH_FLAT;k.extra=X_QUESTION;k.dy=(int)(-fabsf(sinf(t*2))*6);break;
    case M_DEAD:k.eye=EYE_X;k.brow=BROW_NONE;k.mouth=MOUTH_TONGUE;k.extra=X_HALO;k.dy=6;break;
    case M_CRAB:k.brow=BROW_ANGRY;k.mouth=MOUTH_OPEN;k.extra=X_CLAWS;k.dx=(int)(sinf(t*5)*14);k.gx=sinf(t*5)*8;break;
    default:break;
  }
  return k;
}
static void paw(Painter& g,float x,float y,float a,uint16_t c){
  g.oval(x,y,20,23,c);
  for(int i=-1;i<=1;i++){float fa=a-PI/2+i*.45f;g.oval(x+cosf(fa)*25,y+sinf(fa)*25,8,9,c);}
}
static void arrowHead(Painter& g,float x,float y,float dir,uint16_t c){
  g.line(x,y,x-cosf(dir-.6f)*18,y-sinf(dir-.6f)*18,6,c);g.line(x,y,x-cosf(dir+.6f)*18,y-sinf(dir+.6f)*18,6,c);
}
static uint16_t fade(uint16_t c,float ph){return Painter::mix(c,C(10,10,25),ph);}
static void drawExtra(Painter& g,Extra x,float t,float cx,float cy,int character){
  auto ink=C(37,21,49),white=C(252,252,255),pink=C(255,98,156),cyan=C(55,231,236),gold=C(255,226,119),soft=C(150,140,200),water=C(120,210,255);
  switch(x){
    case X_STARS:for(int i=0;i<7;i++){float a=i*2*PI/7+t*.45f;g.star(233+cosf(a)*178,233+sinf(a)*173,5+2*sinf(t*3+i),i%2?cyan:gold);}break;
    case X_HEARTS:for(int i=0;i<4;i++){float a=t*.4f+i*1.7f;g.heart(233+cosf(a)*165,233+sinf(a)*156,9+3*sinf(t+i),pink);}break;
    case X_MOON:g.oval(cx+113,cy-119,18,18,gold);g.oval(cx+121,cy-125,16,16,C(30,19,48));break;
    case X_DOTS:for(int i=0;i<3;i++)g.oval(cx+75+i*17,cy-125-i*9,4+i*2,4+i*2,cyan);break;
    case X_FOOTPRINTS: // paw prints marching up toward the face
      for(int k=0;k<4;k++){float ph=fmodf(t*1.6f+k/4.f,1.f),y=395-ph*90,px=233+(k%2?1:-1)*(112-ph*12);uint16_t c=fade(soft,ph);
        g.oval(px,y,9,11,c);for(int i=-1;i<=1;i++)g.oval(px+i*8,y-13,3.5f,3.5f,c);}
      break;
    case X_CHEVRONS: // chevrons sliding away underneath
      for(int s=-1;s<=1;s+=2)for(int k=0;k<3;k++){float ph=fmodf(t*1.2f+k/3.f,1.f),y=320+ph*55,px=233+s*120;uint16_t c=fade(cyan,ph);
        g.line(px-18,y,px,y+13,5,c);g.line(px+18,y,px,y+13,5,c);}
      break;
    case X_ARROW_L:case X_ARROW_R:{ // turning arrow arcing over the head
      bool l=x==X_ARROW_L;uint16_t c=Painter::mix(cyan,white,.5f+.5f*sinf(t*6));
      g.curve(233,95,72,28,PI+.5f,2*PI-.5f,6,c);
      float a=l?PI+.5f:2*PI-.5f;arrowHead(g,233+cosf(a)*72,95+sinf(a)*28,l?PI+.35f:-.35f,c);break;}
    case X_ZZZ:
      for(int k=0;k<3;k++){float ph=fmodf(t*.5f+k/3.f,1.f),z=8+k*3+ph*6,zx=cx+105+ph*40,zy=cy-110-ph*70;uint16_t c=fade(C(200,200,255),ph);
        g.line(zx-z,zy-z,zx+z,zy-z,3,c);g.line(zx+z,zy-z,zx-z,zy+z,3,c);g.line(zx-z,zy+z,zx+z,zy+z,3,c);}
      break;
    case X_SPARKLES:for(int i=0;i<4;i++){float a=-PI/2+(i-1.5f)*.55f;g.star(233+cosf(a)*180,233+sinf(a)*175,4+3*fabsf(sinf(t*3+i)),i%2?gold:white);}break;
    case X_PAW:{uint16_t c=character==2?C(71,237,244):character==1?C(255,230,187):C(255,203,106);
      float a=sinf(t*8)*.5f;paw(g,cx+150+sinf(a)*20,cy+70-cosf(a)*10,a,c);break;}
    case X_NOTES:
      for(int i=0;i<4;i++){float a=i*PI/2+t*.6f,nx=233+cosf(a)*172,ny=233+sinf(a)*168+sinf(t*5+i)*5;uint16_t c=i%2?cyan:pink;
        g.oval(nx,ny,8,6,c);g.line(nx+7,ny,nx+7,ny-22,3,c);g.line(nx+7,ny-22,nx+15,ny-16,3,c);}
      drawExtra(g,X_STARS,t,cx,cy,character);break;
    case X_BUBBLES:
      // Bubbles rise up both sides; waves ripple along the lower rim.
      for(int k=0;k<6;k++){float ph=fmodf(t*.45f+k*.17f,1.f),bx=233+(k%2?1:-1)*(150+sinf(t*3+k)*8),by=360-ph*240,r=5+k%3*3;
        g.curve(bx,by,r,r,0,2*PI,2,Painter::mix(cyan,white,.4f));}
      for(int i=0;i<20;i++){float a1=PI*(.2f+i*.03f),a2=a1+PI*.03f,r1=190+5*sinf(a1*14+t*5),r2=190+5*sinf(a2*14+t*5);
        g.line(233+cosf(a1)*r1,233+sinf(a1)*r1,233+cosf(a2)*r2,233+sinf(a2)*r2,4,C(40,120,220));}
      break;
    case X_POINTER:{float o=fabsf(sinf(t*4))*14;g.line(cx+125+o,cy-10,cx+165+o,cy-10,7,gold);arrowHead(g,cx+170+o,cy-10,0,gold);break;}
    case X_SWEAT:{float ph=fmodf(t*.8f,1.f),sx=cx+110,sy=cy-95+ph*40;uint16_t c=Painter::mix(water,C(10,10,25),ph*.8f);
      g.oval(sx,sy,8,10,c);g.line(sx,sy-16,sx,sy-6,4,c);break;}
    case X_CUTE:drawExtra(g,X_SPARKLES,t,cx,cy,character);
      for(int i=0;i<3;i++){float a=t*.5f+i*2.1f;g.heart(233+cosf(a)*170,233+sinf(a)*162,8+2*sinf(t*2+i),pink);}
      break;
    case X_SWIRLS:for(int i=0;i<6;i++){float a=i*2*PI/6-t*1.5f;g.curve(233+cosf(a)*178,233+sinf(a)*178,7,7,t*8,t*8+4,3,i%2?C(190,90,255):C(110,255,140));}break;
    case X_WORM: // a little worm inching around the lower rim
      for(int i=0;i<7;i++){float a=PI*(.62f-i*.035f)-fmodf(t*.15f,PI*.4f),r=184+6*sinf(i*1.1f-t*6),wx=233+cosf(a)*r,wy=233+sinf(a)*r;
        g.oval(wx,wy,8,8,i==6?C(255,120,150):C(255,160,180));if(i==6)g.oval(wx,wy-3,2,2,ink);}
      break;
    case X_SHAKE_LINES:
      for(int s=-1;s<=1;s+=2){
        for(int i=0;i<3;i++)g.curve(cx+s*(158+i*12),cy,10,30+i*8,s>0?-.9f:PI-.9f,s>0?.9f:PI+.9f,3,soft);
        for(int i=0;i<3;i++){float ph=fmodf(t*1.5f+i*.33f,1.f);g.oval(cx+s*(140+ph*60),cy-40+i*30-ph*20,4,5,fade(water,ph));}
      }
      break;
    case X_QUESTION:{float y=110+sinf(t*3)*5,qx=cx+115;g.curve(qx,y,14,14,PI,2.4f*PI,6,gold);g.line(qx+5,y+13,qx,y+24,6,gold);g.oval(qx,y+36,4,4,gold);break;}
    case X_HALO:{float y=cy-166+sinf(t*2)*5;g.curve(233,y,62,13,0,2*PI,6,Painter::mix(gold,white,.3f+.3f*sinf(t*3)));break;}
    case X_CLAWS: // snapping pincers on both sides
      for(int s=-1;s<=1;s+=2){float bx=cx+s*158,by=cy+75,o=.35f+.3f*fabsf(sinf(t*8));
        g.oval(bx,by,16,13,C(200,50,60));
        g.curve(bx,by-18,16,22,PI+o,2*PI-.1f,7,C(255,90,80));g.curve(bx,by-18,16,22,.1f,PI-o,7,C(255,90,80));}
      break;
    default:break;
  }
}
void Scene::draw(float t){
  if(cachedCharacter!=character)buildBase();
  auto ink=C(37,21,49),cream=C(255,237,195),white=C(252,252,255),pink=C(255,98,156),cyan=C(55,231,236);
  Look k=move!=NO_MOVE?moveLook(move,t):moodLook(mood,t);
  int dx=k.dx,dy=k.dy;
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
  float gx=gazeX+k.gx,gy=gazeY+k.gy;
  float cycle=fmodf(t,4.7f),blink=cycle<.16f?sinf(cycle/.16f*PI):0;
  float open=(1-blink)*k.open;
  uint16_t lineCol=character==2?cyan:ink,browCol=character==2?cyan:C(160,89,54);
  for(int side=-1;side<=1;side+=2){
    float ex=cx+side*55,ey=cy-24+k.eyeDrop;
    switch(k.eye){
      case EYE_SLEEP:g.curve(ex,ey-4,27,11,.15f,PI-.15f,7,lineCol);break;
      case EYE_HAPPY:g.curve(ex,ey+8,27,18,PI+.2f,2*PI-.2f,7,lineCol);break;
      case EYE_HEART:g.heart(ex,ey-8,30,pink);g.oval(ex-8,ey-15,5,5,white);break;
      case EYE_X:g.line(ex-20,ey-18,ex+20,ey+18,8,lineCol);g.line(ex+20,ey-18,ex-20,ey+18,8,lineCol);break;
      case EYE_SQUEEZE:{float tip=ex-side*14,back=ex+side*18;g.line(back,ey-15,tip,ey,7,lineCol);g.line(tip,ey,back,ey+15,7,lineCol);break;}
      case EYE_SPIRAL:{
        g.ellipse(ex,ey,35,35,white,C(230,227,249));
        float px=ex,py=ey;
        for(int i=1;i<=28;i++){float a=i*.45f+t*side*7,r=i*1.05f,nx=ex+cosf(a)*r,ny=ey+sinf(a)*r;g.line(px,py,nx,ny,4,C(120,40,140));px=nx;py=ny;}
        break;}
      default:{
        if(open<.18f){g.curve(ex,ey-4,27,11,.15f,PI-.15f,7,lineCol);break;}
        float ry=k.tall*open,iris=k.eye==EYE_SPARKLE?26:22;
        g.oval(ex,ey+3,38,ry+5,character==2?C(55,88,120):C(166,100,55));
        g.ellipse(ex,ey,35,ry,white,C(230,227,249));
        g.ellipse(ex+gx,ey+gy,iris,ry*.87f,character==2?pink:cyan,character==2?C(121,47,173):C(16,114,167));
        g.oval(ex+gx,ey+gy+1,12,ry*.76f,C(15,24,43));
        g.oval(ex+gx-7,ey+gy-12*open,8,8*open,white);g.oval(ex+gx+8,ey+gy+10*open,3,3*open,white);
        if(k.eye==EYE_SPARKLE){g.oval(ex+gx+9,ey+gy-6,4,4,white);g.star(ex+gx-10,ey+gy+8,3+sinf(t*5),white);}
      }
    }
    float by=ey-52;
    switch(k.brow){
      case BROW_ANGRY:g.line(ex-side*25,by+10,ex+side*22,by-4,8,ink);break;
      case BROW_SAD:g.line(ex-side*22,by-5,ex+side*22,by+10,7,ink);break;
      case BROW_RAISED:g.curve(ex,by-2,24,12,PI+.25f,2*PI-.25f,6,browCol);break;
      case BROW_ASYM:if(side<0)g.curve(ex,by-6,24,14,PI+.25f,2*PI-.25f,6,browCol);else g.line(ex-22,by+6,ex+22,by+10,7,ink);break;
      case BROW_NONE:break;
      default:g.curve(ex,by+8,24,12,PI+.25f,2*PI-.25f,6,browCol);
    }
    g.ellipse(cx+side*92,cy+28,22*k.cheek,11*k.cheek,character==2?C(175,69,177):C(255,144,120),character==2?C(110,46,137):C(248,154,108));
  }
  if(character!=2){
    g.ellipse(cx,cy+64,73,48,cream,C(255,216,165));
    g.oval(cx-26,cy+42,36,28,cream);g.oval(cx+26,cy+42,36,28,cream);
    g.ellipse(cx,cy+29,22,15,C(91,47,69),ink);g.oval(cx-6,cy+24,7,3,C(165,105,115));
    g.line(cx,cy+41,cx,cy+53,4,ink);
  }
  float mouthY=cy+(character==2?54:64);
  MouthStyle mouth=k.mouth;
  if(mouth==MOUTH_W&&character==1)mouth=MOUTH_OPEN; // the puppy always grins
  if(energy>.4f&&mouth!=MOUTH_TONGUE)mouth=MOUTH_O;  // mouth opens with loud sound
  switch(mouth){
    case MOUTH_O:g.oval(cx,mouthY,character==2?23:18,16+energy*16,ink);g.oval(cx,mouthY+10,10,5,pink);break;
    case MOUTH_SAD:g.curve(cx,mouthY+12,25,13,PI+.2f,2*PI-.2f,4,ink);break;
    case MOUTH_SMALL:g.oval(cx,mouthY+1,9,5,ink);break;
    case MOUTH_OPEN:g.oval(cx,mouthY,31,21,ink);g.ellipse(cx,mouthY+13,16,14,C(255,127,160),C(226,67,130));g.line(cx,mouthY+8,cx,mouthY+19,2,C(195,55,105));break;
    case MOUTH_FLAT:g.line(cx-22,mouthY+2,cx+22,mouthY-2,5,ink);break;
    case MOUTH_GRIT:
      g.oval(cx,mouthY,33,16,ink);g.oval(cx,mouthY,28,11,white);
      for(int i=-1;i<=1;i++)g.line(cx+i*9,mouthY-10,cx+i*9,mouthY+10,2,ink);
      g.line(cx-27,mouthY,cx+27,mouthY,2,ink);break;
    case MOUTH_TONGUE:g.line(cx-22,mouthY,cx+22,mouthY,5,ink);g.oval(cx+8,mouthY+11,10,12,pink);g.line(cx+8,mouthY+4,cx+8,mouthY+17,2,C(195,55,105));break;
    case MOUTH_WAVY:for(int i=0;i<8;i++){float x1=-28+i*7,x2=x1+7;g.line(cx+x1,mouthY+4*sinf(x1*.25f+t*6),cx+x2,mouthY+4*sinf(x2*.25f+t*6),4,ink);}break;
    default:g.curve(cx-15,mouthY-10,17,16,.1f,PI-.1f,4,ink);g.curve(cx+15,mouthY-10,17,16,.1f,PI-.1f,4,ink);
  }
  if(character==0)for(int s=-1;s<=1;s+=2)for(int i=0;i<3;i++)g.oval(cx+s*(35+i%2*10),cy+47+i*9,2,2,C(182,121,92));
  // Floating extras stay inside the circular safe region.
  drawExtra(g,k.extra,t,cx,cy,character);
  // Small status badge: teal = UART recently heard, violet = standalone.
  g.oval(233,399,64,17,C(15,18,36));g.text(move!=NO_MOVE?moveLabel(move):moodName(mood),393,C(212,212,242),1);
  g.oval(233,428,3,3,linked?C(59,242,179):C(150,99,230));
  g.text(character==0?"AURORA":character==1?"BISCUIT":"NOVA",35,C(190,180,221),2);
}
