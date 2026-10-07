#pragma once
#include <Arduino.h>
// Original synth API; declarations needed when included as C++.
void precalculate_ftw_table();
void synthESP32_begin();
void initADSR();
void synthESP32_setIni(unsigned char voice, int ini);
void synthESP32_setEnd(unsigned char voice, int end);
void synthESP32_setMVol(unsigned char vol);
void synthESP32_setMFilter(unsigned char freq);
void synthESP32_setFilter(unsigned char voice, unsigned char freq);
void synthESP32_setVol(unsigned char voice,unsigned char vol);
void synthESP32_setPan(unsigned char voice,signed char pan);
void synthESP32_updateVolPan(unsigned char voice);
void synthESP32_TRIGGER(int nkey);
void synthESP32_TRIGGER_P(int nkey, int ppitch);
void setSoundALL();
void setSound(byte f);
void setRandomVoice2(byte f);
void setRandomVoice(byte f);
void setRandomPattern(byte f);
bool find_scale(uint8_t note);
void setRandomPitch(uint8_t ssound);
void setRandomNotes(byte f);
void synthESP32_setWave(unsigned char voice, unsigned char wave);
void synthESP32_setEnvelope(unsigned char voice, unsigned char env);
void synthESP32_setLength(unsigned char voice,unsigned char length);
void synthESP32_setPitch(unsigned char voice,unsigned char MIDInote);
void synthESP32_setMod(unsigned char voice,unsigned char mod);
void move_pattern(int ssound, int dir);

void synthESP32_setTrackTone(unsigned char voice, unsigned char cutoff, unsigned char resonance);
