#pragma once
#include "Painter.h"
enum Mood { CURIOUS,HAPPY,LOVE,SLEEPY,PARTY,SURPRISED,BRAVE,SAD,THINKING };
struct Scene {
  Painter g;
  uint16_t* background=nullptr;
  uint16_t* base=nullptr;
  int cachedCharacter=-1;
  int character=0;
  Mood mood=CURIOUS;
  float energy=0,gazeX=0,gazeY=0;
  bool linked=false;
  void prepare();
  void buildBase();
  void draw(float seconds);
};
const char* moodName(Mood m);
