#pragma once
#include <Arduino.h>
#include <driver/i2c_master.h>
#include <atomic>
extern i2c_master_bus_handle_t sharedBus;
extern std::atomic<float> micLevel;
extern std::atomic<uint32_t> audioBlocks, audioErrors, soundUntil;
extern bool micReady, speakerReady;
extern std::atomic<bool> soundEnabled;
bool initPeripherals();
void chirp(int kind);
bool readTouch(int &x,int &y);
