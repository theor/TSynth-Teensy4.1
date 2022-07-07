#pragma once
#include <stdint.h>
#include <MIDI.h>

// Globals used for OLED Settings
extern byte midiChannel;
extern byte midiOutCh;
extern midi::Thru::Mode MIDIThru;
extern String patchName;
extern boolean encCW;
extern boolean vuMeter;

// Global patch modifiers
extern float lfoSyncFreq;
extern long midiClkTimeInterval;
extern float lfoTempoValue;
extern int pitchBendRange;
extern float modWheelDepth;
extern String oscLFOTimeDivStr;
extern int velocitySens;
// Exponential envelopes
extern int8_t envTypeAmp;
extern int8_t envTypeFilt;
//Glide shape
extern int8_t glideShape;

#define TOLERANCE 2 //Gives a window of when pick-up occurs, this is due to the speed of control changing and Mux reading

extern int dbgMode;
extern uint8_t dbgX;
extern uint8_t dbgY;
extern String dbgMsg;
//extern uint8_t buttonStates[16];
