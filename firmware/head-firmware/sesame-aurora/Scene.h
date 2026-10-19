#pragma once
#include "Painter.h"
enum Mood { CURIOUS,HAPPY,LOVE,SLEEPY,PARTY,SURPRISED,BRAVE,SAD,THINKING,MOOD_COUNT };
// The body's 19 movement commands. Each has its own expression and sound.
enum Move { NO_MOVE=-1,M_FORWARD,M_BACKWARD,M_LEFT,M_RIGHT,M_REST,M_STAND,M_WAVE,M_DANCE,M_SWIM,M_POINT,
            M_PUSHUP,M_BOW,M_CUTE,M_FREAKY,M_WORM,M_SHAKE,M_SHRUG,M_DEAD,M_CRAB,MOVE_COUNT };
struct Scene {
  Painter g;
  uint16_t* background=nullptr;
  uint16_t* base=nullptr;
  int cachedCharacter=-1;
  int character=0;
  Mood mood=CURIOUS;
  Move move=NO_MOVE; // when set, the move's expression replaces the mood's
  float energy=0,gazeX=0,gazeY=0;
  bool linked=false;
  void prepare();
  void buildBase();
  void draw(float seconds);
};
const char* moodName(Mood m);
const char* moveLabel(Move m);
// Body command ("forward", "crab", ...) or body face name ("walk") to a move.
Move moveFromName(const char* name);
