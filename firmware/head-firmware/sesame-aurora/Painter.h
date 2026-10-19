#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <algorithm>

// Small native-resolution RGB565 painter. Only shape edges are blended;
// solid spans avoid expensive per-pixel geometry. Buffer stays in PSRAM.
struct Painter {
  static constexpr int W=466, H=466;
  uint16_t* p;
  static uint16_t rgb(int r,int g,int b) { return ((r&248)<<8)|((g&252)<<3)|(b>>3); }
  static uint16_t mix(uint16_t a,uint16_t b,float t) {
    t=std::max(0.f,std::min(1.f,t));
    int ar=(a>>11)*255/31,ag=((a>>5)&63)*255/63,ab=(a&31)*255/31;
    int br=(b>>11)*255/31,bg=((b>>5)&63)*255/63,bb=(b&31)*255/31;
    return rgb(ar+(br-ar)*t,ag+(bg-ag)*t,ab+(bb-ab)*t);
  }
  void pixel(int x,int y,uint16_t c,float a=1) {
    if(x<0||y<0||x>=W||y>=H)return;
    p[y*W+x]=a>=1?c:mix(p[y*W+x],c,a);
  }
  void span(int x1,int x2,int y,uint16_t c) {
    if(y<0||y>=H)return;
    x1=std::max(0,x1); x2=std::min(W-1,x2);
    if(x1<=x2)std::fill(p+y*W+x1,p+y*W+x2+1,c);
  }
  void ellipse(float x,float y,float rx,float ry,uint16_t top,uint16_t bottom) {
    if(rx<=0||ry<=0)return;
    int ya=std::max(0,(int)floorf(y-ry)),yb=std::min(H-1,(int)ceilf(y+ry));
    for(int j=ya;j<=yb;j++) {
      float v=(j-y)/ry; if(fabsf(v)>=1)continue;
      float d=rx*sqrtf(1-v*v),left=x-d,right=x+d;
      uint16_t c=mix(top,bottom,(j-y+ry)/(2*ry));
      int l=(int)ceilf(left),r=(int)floorf(right);
      span(l,r,j,c);pixel(l-1,j,c,l-left);pixel(r+1,j,c,right-r);
    }
  }
  void oval(float x,float y,float rx,float ry,uint16_t c) { ellipse(x,y,rx,ry,c,c); }
  void line(float x1,float y1,float x2,float y2,float width,uint16_t c) {
    float dx=x2-x1,dy=y2-y1;int n=(int)(sqrtf(dx*dx+dy*dy)/2)+1;
    for(int i=0;i<=n;i++)oval(x1+dx*i/n,y1+dy*i/n,width/2,width/2,c);
  }
  void curve(float x,float y,float rx,float ry,float start,float end,float width,uint16_t c) {
    int n=(int)(fabsf(end-start)*std::max(rx,ry)/3)+1;
    float px=x+cosf(start)*rx,py=y+sinf(start)*ry;
    for(int i=1;i<=n;i++){float a=start+(end-start)*i/n;float nx=x+cosf(a)*rx,ny=y+sinf(a)*ry;line(px,py,nx,ny,width,c);px=nx;py=ny;}
  }
  void heart(float x,float y,float s,uint16_t c) {
    oval(x-s*.32f,y-s*.25f,s*.4f,s*.4f,c);oval(x+s*.32f,y-s*.25f,s*.4f,s*.4f,c);
    for(int j=0;j<(int)(s*.92f);j++){float half=s*.68f*(1-j/(s*.92f));span((int)(x-half),(int)(x+half),(int)(y+j),c);}
  }
  void star(float x,float y,float r,uint16_t c) { line(x-r,y,x+r,y,2,c);line(x,y-r,x,y+r,2,c);oval(x,y,2.5,2.5,c); }
  void text(const char* s,int y,uint16_t c,int scale=2) { textAt(s,(W-(int)strlen(s)*6*scale)/2,y,c,scale); }
  static int textWidth(const char* s,int scale) { return (int)strlen(s)*6*scale-scale; }
  void textAt(const char* s,int x,int y,uint16_t c,int scale) {
    // Compact uppercase 5x7 glyphs, plus digits and '%'. No font dependency.
    static const uint8_t az[][5]={
      {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{65,65,127,65,65},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}};
    static const uint8_t digits[][5]={
      {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
      {39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30}};
    static const uint8_t percent[5]={35,19,8,100,98};
    for(;*s;s++,x+=6*scale){
      const uint8_t* g=nullptr;
      if(*s>='A'&&*s<='Z')g=az[*s-'A'];else if(*s>='0'&&*s<='9')g=digits[*s-'0'];else if(*s=='%')g=percent;
      if(!g)continue;
      for(int col=0;col<5;col++)for(int row=0;row<7;row++)if(g[col]&(1<<row))for(int sy=0;sy<scale;sy++)span(x+col*scale,x+col*scale+scale-1,y+row*scale+sy,c);
    }
  }
};
