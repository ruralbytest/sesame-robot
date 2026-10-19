#pragma once
#include <Arduino.h>
#include <driver/i2c_master.h>
#include <atomic>
#include "Scene.h"
extern i2c_master_bus_handle_t sharedBus;
extern std::atomic<float> micLevel;
extern std::atomic<uint32_t> audioBlocks, audioErrors, soundUntil;
extern bool micReady, speakerReady;
extern std::atomic<bool> soundEnabled;
// Sound ids: the three original pet calls, then one per mood, then one per body move.
enum SoundId { SND_CHIRP, SND_AFFECTION, SND_PURR, SND_MOOD_FIRST,
               SND_MOVE_FIRST=SND_MOOD_FIRST+MOOD_COUNT, SND_COUNT=SND_MOVE_FIRST+MOVE_COUNT };
bool initPeripherals();
void playSound(int id);
inline void chirp(int kind){playSound(kind-1);}   // 1 = chirp, 2 = affection, 3 = purr
inline void moodSound(Mood m){playSound(SND_MOOD_FIRST+m);}
inline void moveSound(Move m){if(m!=NO_MOVE)playSound(SND_MOVE_FIRST+m);}
bool readTouch(int &x,int &y);
