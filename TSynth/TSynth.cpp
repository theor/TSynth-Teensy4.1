/*
   MIT License

  Copyright (c) 2020-21 ElectroTechnique

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ElectroTechnique TSynth - Firmware Rev 2.33
  TEENSY 4.1 - 12 VOICES

  Arduino IDE Tools Settings:
    Board: "Teensy4.1"
    USB Type: "Serial + MIDI + Audio"
    CPU Speed: "600MHz"
    Optimize: "Faster"

  Performance Tests   Max CPU  Mem
  600MHz Faster        80+     59

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html
    Alexander Davis / Vince R. Pearson - Stereo ensemble chorus effect https://github.com/quarterturn/teensy3-ensemble-chorus
    Will Winder - Major refactoring and monophonic mode
    Vince R. Pearson - Exponential envelopes & glide
    Github members fab672000 & CDW2000 - General improvements to code
    Mark Tillotson - Special thanks for band-limiting the waveforms in the Audio Library

  Additional libraries:
    Agileware CircularBuffer, Adafruit_GFX (available in Arduino libraries manager)
*/
#include <vector>
#include "Audio.h" //Using local version to override Teensyduino version
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include "MidiCC.h"
#include "SettingsService.h"
#include "AudioPatching.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"
#include "Detune.h"
#include "utils.h"
#include "Voice.h"
#include "VoiceGroup.h"

#define PARAMETER 0     // The main page for displaying the current patch and control (parameter) changes
#define RECALL 1        // Patches list
#define SAVE 2          // Save patch page
#define REINITIALISE 3  // Reinitialise message
#define PATCH 4         // Show current patch bypassing PARAMETER
#define PATCHNAMING 5   // Patch naming page
#define DELETE 6        // Delete patch page
#define DELETEMSG 7     // Delete patch message page
#define SETTINGS 8      // Settings page
#define SETTINGSVALUE 9 // Settings page


uint32_t state = PARAMETER;

// Initialize the audio configuration.
Global global{VOICEMIXERLEVEL};
// VoiceGroup voices1{global.SharedAudio[0]};
std::vector<VoiceGroup *> groupvec;
uint8_t activeGroupIndex = 0;

#include "ST7735Display.h"

// USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);
//MIDIDevice_BigBuffer midi1(myusb); // Try this if your MIDI Compliant controller has problems

// MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

void changeMIDIThruMode()
{
  MIDI.turnThruOn(MIDIThru);
}

#include "Settings.h"

boolean cardStatus = false;
boolean firstPatchLoaded = false;

float previousMillis = millis(); // For MIDI Clk Sync

uint8_t count = 0;           // For MIDI Clk Sync
uint16_t patchNo = 1;         // Current patch no
long earliestTime = millis(); // For voice allocation - initialise to now


void myNoteOn(byte channel, byte note, byte velocity)
{
  // Check for out of range notes
  if (note + groupvec[activeGroupIndex]->params().oscPitchA < 0 || note + groupvec[activeGroupIndex]->params().oscPitchA > 127 || note + groupvec[activeGroupIndex]->params().oscPitchB < 0 || note + groupvec[activeGroupIndex]->params().oscPitchB > 127)
    return;

  groupvec[activeGroupIndex]->noteOn(note, velocity);
}

void myNoteOff(byte channel, byte note, byte velocity)
{
  groupvec[activeGroupIndex]->noteOff(note);
}

void midiCCOut(byte cc, byte value)
{
    if (midiOutCh > 0)
    {
        usbMIDI.sendControlChange(cc, value, midiOutCh);
        midi1.sendControlChange(cc, value, midiOutCh);
        if (MIDIThru == midi::Thru::Off)
            MIDI.sendControlChange(cc, value, midiOutCh); // MIDI DIN is set to Out
    }
}

int getLFOWaveform(int value)
{
  if (value >= 0 && value < 8)
  {
    return WAVEFORM_SINE;
  }
  else if (value >= 8 && value < 30)
  {
    return WAVEFORM_TRIANGLE;
  }
  else if (value >= 30 && value < 63)
  {
    return WAVEFORM_SAWTOOTH_REVERSE;
  }
  else if (value >= 63 && value < 92)
  {
    return WAVEFORM_SAWTOOTH;
  }
  else if (value >= 92 && value < 111)
  {
    return WAVEFORM_SQUARE;
  }
  else
  {
    return WAVEFORM_SAMPLE_HOLD;
  }
}

FLASHMEM String getWaveformStr(uint32_t value)
{
  switch (value)
  {
  case WAVEFORM_SILENT:
    return F("Off");
  case WAVEFORM_SAMPLE_HOLD:
    return F("Sample & Hold");
  case WAVEFORM_SINE:
    return F("Sine");
  case WAVEFORM_BANDLIMIT_SQUARE:
  case WAVEFORM_SQUARE:
    return F("Square");
  case WAVEFORM_TRIANGLE:
    return F("Triangle");
  case WAVEFORM_BANDLIMIT_SAWTOOTH:
  case WAVEFORM_SAWTOOTH:
    return F("Sawtooth");
  case WAVEFORM_SAWTOOTH_REVERSE:
    return F("Ramp");
  case WAVEFORM_BANDLIMIT_PULSE:
    return F("Var. Pulse");
  case WAVEFORM_TRIANGLE_VARIABLE:
    return F("Var. Triangle");
  case WAVEFORM_PARABOLIC:
    return F("Parabolic");
  case WAVEFORM_HARMONIC:
    return F("Harmonic");
  default:
    return F("ERR_WAVE");
  }
}

FLASHMEM void updateUnison(uint8_t unison)
{
  groupvec[activeGroupIndex]->setUnisonMode(unison);

  if (unison == 0)
  {
    showCurrentParameterPage(F("Unison"), F("Off"));
//    pinMode(UNISON_LED, OUTPUT);
//    digitalWriteFast(UNISON_LED, LOW); // LED off
  }
  else if (unison == 1)
  {
    showCurrentParameterPage(F("Dyn. Unison"), F("On"));
//    pinMode(UNISON_LED, OUTPUT);
//    digitalWriteFast(UNISON_LED, HIGH); // LED on
  }
  else
  {
    showCurrentParameterPage(F("Chd. Unison"), F("On"));
//    analogWriteFrequency(UNISON_LED, 1); // This is to make the LED flash using PWM rather than some thread
//    analogWrite(UNISON_LED, 127);
  }
}

FLASHMEM void updateVolume(float vol)
{
  global.sgtl5000_1.volume(vol * SGTL_MAXVOLUME);
  showCurrentParameterPage(F("Volume"), vol);
}

FLASHMEM void updateGlide(float glideSpeed)
{
  groupvec[activeGroupIndex]->params().glideSpeed = glideSpeed;
  showCurrentParameterPage(F("Glide"), milliToString(glideSpeed * GLIDEFACTOR));
}

FLASHMEM void updateWaveformA(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setWaveformA(waveform);
  showCurrentParameterPage(F("1. Waveform"), getWaveformStr(waveform));
}

FLASHMEM void updateWaveformB(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setWaveformB(waveform);
  showCurrentParameterPage(F("2. Waveform"), getWaveformStr(waveform));
}

FLASHMEM void updatePitchA(int pitch)
{
  groupvec[activeGroupIndex]->params().oscPitchA = pitch;
  groupvec[activeGroupIndex]->updateVoices();
  showCurrentParameterPage("1. Semitones", (pitch > 0 ? "+" : "") + String(pitch));
}

FLASHMEM void updatePitchB(int pitch)
{
  groupvec[activeGroupIndex]->params().oscPitchB = pitch;
  groupvec[activeGroupIndex]->updateVoices();
  showCurrentParameterPage(F("2. Semitones"), (pitch > 0 ? "+" : "") + String(pitch));
}

FLASHMEM void updateDetune(float detune, uint32_t chordDetune)
{
  groupvec[activeGroupIndex]->params().detune = detune;
  groupvec[activeGroupIndex]->params().chordDetune = chordDetune;
  groupvec[activeGroupIndex]->updateVoices();

  if (groupvec[activeGroupIndex]->params().unisonMode == 2)
  {
    showCurrentParameterPage(F("Chord"), CDT_STR[chordDetune]);
  }
  else
  {
    showCurrentParameterPage(F("Detune"), String((1 - detune) * 100) + F(" %"));
  }
}

FLASHMEM void updatePWMSource(uint8_t source)
{
  groupvec[activeGroupIndex]->setPWMSource(source);

  if (source == PWMSOURCELFO)
  {
    showCurrentParameterPage(F("PWM Source"), F("LFO")); // Only shown when updated via MIDI
  }
  else
  {
    showCurrentParameterPage(F("PWM Source"), F("Filter Env"));
  }
}

FLASHMEM void updatePWMRate(float value)
{
  groupvec[activeGroupIndex]->setPwmRate(value);

  if (value == PWMRATE_PW_MODE)
  {
    // Set to fixed PW mode
    showCurrentParameterPage(F("PW Mode"), F("On"));
  }
  else if (value == PWMRATE_SOURCE_FILTER_ENV)
  {
    // Set to Filter Env Mod source
    showCurrentParameterPage(F("PWM Source"), F("Filter Env"));
  }
  else
  {
    showCurrentParameterPage(F("PWM Rate"), String(2 * value) + F(" Hz")); // PWM goes through mid to maximum, sounding effectively twice as fast
  }
}

FLASHMEM void updatePWMAmount(float value)
{
  // MIDI only - sets both osc PWM
  groupvec[activeGroupIndex]->overridePwmAmount(value);
  showCurrentParameterPage(F("PWM Amt"), String(value) + F(" : ") + String(value));
}

FLASHMEM void updatePWA(float valuePwA, float valuePwmAmtA)
{
  groupvec[activeGroupIndex]->setPWA(valuePwA, valuePwmAmtA);

  if (groupvec[activeGroupIndex]->getPwmRate() == PWMRATE_PW_MODE)
  {
    if (groupvec[activeGroupIndex]->getWaveformA() == WAVEFORM_TRIANGLE_VARIABLE)
    {
      showCurrentParameterPage(F("1. PW Amt"), VAR_TRI, groupvec[activeGroupIndex]->getPwA());
    }
    else
    {
      showCurrentParameterPage(F("1. PW Amt"), PULSE, groupvec[activeGroupIndex]->getPwA());
    }
  }
  else
  {
    if (groupvec[activeGroupIndex]->getPwmSource() == PWMSOURCELFO)
    {
      // PW alters PWM LFO amount for waveform A
      showCurrentParameterPage(F("1. PWM Amt"), F("LFO ") + String(groupvec[activeGroupIndex]->getPwmAmtA()));
    }
    else
    {
      // PW alters PWM Filter Env amount for waveform A
      showCurrentParameterPage(F("1. PWM Amt"), F("F. Env ") + String(groupvec[activeGroupIndex]->getPwmAmtA()));
    }
  }
}

FLASHMEM void updatePWB(float valuePwB, float valuePwmAmtB)
{
  groupvec[activeGroupIndex]->setPWB(valuePwB, valuePwmAmtB);

  if (groupvec[activeGroupIndex]->getPwmRate() == PWMRATE_PW_MODE)
  {
    if (groupvec[activeGroupIndex]->getWaveformB() == WAVEFORM_TRIANGLE_VARIABLE)
    {
      showCurrentParameterPage(F("2. PW Amt"), VAR_TRI, groupvec[activeGroupIndex]->getPwB());
    }
    else
    {
      showCurrentParameterPage(F("2. PW Amt"), PULSE, groupvec[activeGroupIndex]->getPwB());
    }
  }
  else
  {
    if (groupvec[activeGroupIndex]->getPwmSource() == PWMSOURCELFO)
    {
      // PW alters PWM LFO amount for waveform B
      showCurrentParameterPage(F("2. PWM Amt"), F("LFO ") + String(groupvec[activeGroupIndex]->getPwmAmtB()));
    }
    else
    {
      // PW alters PWM Filter Env amount for waveform B
      showCurrentParameterPage(F("2. PWM Amt"), F("F. Env ") + String(groupvec[activeGroupIndex]->getPwmAmtB()));
    }
  }
}

FLASHMEM void updateOscLevelA(float value)
{
  groupvec[activeGroupIndex]->setOscLevelA(value);

  switch (groupvec[activeGroupIndex]->getOscFX())
  {
  case 1: // XOR
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  case 2: // XMod
    // osc A sounds with increasing osc B mod
    if (groupvec[activeGroupIndex]->getOscLevelA() == 1.0f && groupvec[activeGroupIndex]->getOscLevelB() <= 1.0f)
    {
      showCurrentParameterPage(F("XMod Osc 1"), F("Osc 2: ") + String(1 - groupvec[activeGroupIndex]->getOscLevelB()));
    }
    break;
  case 0: // None
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  }
}

FLASHMEM void updateOscLevelB(float value)
{
  groupvec[activeGroupIndex]->setOscLevelB(value);

  switch (groupvec[activeGroupIndex]->getOscFX())
  {
  case 1: // XOR
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  case 2: // XMod
    // osc B sounds with increasing osc A mod
    if (groupvec[activeGroupIndex]->getOscLevelB() == 1.0f && groupvec[activeGroupIndex]->getOscLevelA() < 1.0f)
    {
      showCurrentParameterPage(F("XMod Osc 2"), F("Osc 1: ") + String(1 - groupvec[activeGroupIndex]->getOscLevelA()));
    }
    break;
  case 0: // None
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  }
}

FLASHMEM void updateNoiseLevel(float value)
{
  float pink = 0.0;
  float white = 0.0;
  if (value > 0)
  {
    pink = value;
  }
  else if (value < 0)
  {
    white = - value;
  }

  groupvec[activeGroupIndex]->setPinkNoiseLevel(pink);
  groupvec[activeGroupIndex]->setWhiteNoiseLevel(white);

  if (value > 0)
  {
    showCurrentParameterPage(F("Noise Level"), F("Pink ") + String(pink));
  }
  else if (value < 0)
  {
    showCurrentParameterPage(F("Noise Level"), F("White ") + String(white));
  }
  else
  {
    showCurrentParameterPage(F("Noise Level"), F("Off"));
  }
}

FLASHMEM void updateFilterFreq(float value)
{
  groupvec[activeGroupIndex]->setCutoff(value);
  showCurrentParameterPage(F("Cutoff"), String(int(value)) + F(" Hz"));
}

FLASHMEM void updateFilterRes(float value)
{
  groupvec[activeGroupIndex]->setResonance(value);
  showCurrentParameterPage(F("Resonance"), value);
}

FLASHMEM void updateFilterMixer(float value)
{
  groupvec[activeGroupIndex]->setFilterMixer(value);

  String filterStr;
  if (value == BANDPASS)
  {
    filterStr = F("Band Pass");
  }
  else
  {
    // LP-HP mix mode - a notch filter
    if (value == LOWPASS)
    {
      filterStr = F("Low Pass");
    }
    else if (value == HIGHPASS)
    {
      filterStr = F("High Pass");
    }
    else
    {
      filterStr = F("LP ") + String(100 - int(100 * value)) + F(" - ") + String(int(100 * value)) + F(" HP");
    }
  }

  showCurrentParameterPage(F("Filter Type"), filterStr);
}

FLASHMEM void updateFilterEnv(float value)
{
  groupvec[activeGroupIndex]->setFilterEnvelope(value);
  showCurrentParameterPage(F("Filter Env."), String(value));
}

FLASHMEM void updatePitchEnv(float value)
{
  groupvec[activeGroupIndex]->setPitchEnvelope(value);
  showCurrentParameterPage(F("Pitch Env Amt"), String(value));
}

FLASHMEM void updateKeyTracking(float value)
{
  groupvec[activeGroupIndex]->setKeytracking(value);
  showCurrentParameterPage(F("Key Tracking"), String(value));
}

FLASHMEM void updatePitchLFOAmt(float value)
{
  groupvec[activeGroupIndex]->setPitchLfoAmount(value);
  char buf[10];
  showCurrentParameterPage(F("LFO Amount"), dtostrf(value, 4, 3, buf));
}

FLASHMEM void updateModWheel(float value)
{
  groupvec[activeGroupIndex]->setModWhAmount(value);
}

FLASHMEM void updatePitchLFORate(float value)
{
  groupvec[activeGroupIndex]->setPitchLfoRate(value);
  showCurrentParameterPage(F("LFO Rate"), String(value) + F(" Hz"));
}

FLASHMEM void updatePitchLFOWaveform(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setPitchLfoWaveform(waveform);
  showCurrentParameterPage(F("Pitch LFO"), getWaveformStr(waveform));
}

// MIDI CC only
FLASHMEM void updatePitchLFOMidiClkSync(bool value)
{
  groupvec[activeGroupIndex]->setPitchLfoMidiClockSync(value);
  showCurrentParameterPage(F("P. LFO Sync"), value ? F("On") : F("Off"));
}

FLASHMEM void updateFilterLfoRate(float value, String timeDivStr)
{
  groupvec[activeGroupIndex]->setFilterLfoRate(value);

  if (timeDivStr.length() > 0)
  {
    showCurrentParameterPage(F("LFO Time Div"), timeDivStr);
  }
  else
  {
    showCurrentParameterPage(F("F. LFO Rate"), String(value) + F(" Hz"));
  }
}

FLASHMEM void updateFilterLfoAmt(float value)
{
  groupvec[activeGroupIndex]->setFilterLfoAmt(value);
  showCurrentParameterPage(F("F. LFO Amt"), String(value));
}

FLASHMEM void updateFilterLFOWaveform(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setFilterLfoWaveform(waveform);
  showCurrentParameterPage(F("Filter LFO"), getWaveformStr(waveform));
}

FLASHMEM void updatePitchLFORetrig(bool value)
{
  groupvec[activeGroupIndex]->setPitchLfoRetrig(value);
  showCurrentParameterPage(F("P. LFO Retrig"), value ? F("On") : F("Off"));
}

FLASHMEM void updateFilterLFORetrig(bool value)
{
  groupvec[activeGroupIndex]->setFilterLfoRetrig(value);
  showCurrentParameterPage(F("F. LFO Retrig"), groupvec[activeGroupIndex]->getFilterLfoRetrig() ? F("On") : F("Off"));
//  digitalWriteFast(RETRIG_LED, groupvec[activeGroupIndex]->getFilterLfoRetrig() ? HIGH : LOW); // LED
}

FLASHMEM void updateFilterLFOMidiClkSync(bool value)
{
  groupvec[activeGroupIndex]->setFilterLfoMidiClockSync(value);
  showCurrentParameterPage(F("Tempo Sync"), value ? F("On") : F("Off"));
//  digitalWriteFast(TEMPO_LED, value ? HIGH : LOW); // LED
}

FLASHMEM void updateFilterAttack(float value)
{
  groupvec[activeGroupIndex]->setFilterAttack(value);
  showCurrentParameterPage(F("Filter Attack"), milliToString(value), FILTER_ENV);
}

FLASHMEM void updateFilterDecay(float value)
{
  groupvec[activeGroupIndex]->setFilterDecay(value);
  showCurrentParameterPage("Filter Decay", milliToString(value), FILTER_ENV);
}

FLASHMEM void updateFilterSustain(float value)
{
  groupvec[activeGroupIndex]->setFilterSustain(value);
  showCurrentParameterPage(F("Filter Sustain"), String(value), FILTER_ENV);
}

FLASHMEM void updateFilterRelease(float value)
{
  groupvec[activeGroupIndex]->setFilterRelease(value);
  showCurrentParameterPage(F("Filter Release"), milliToString(value), FILTER_ENV);
}

FLASHMEM void updateAttack(float value)
{
  groupvec[activeGroupIndex]->setAmpAttack(value);
  showCurrentParameterPage(F("Attack"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateDecay(float value)
{
  groupvec[activeGroupIndex]->setAmpDecay(value);
  showCurrentParameterPage(F("Decay"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateSustain(float value)
{
  groupvec[activeGroupIndex]->setAmpSustain(value);
  showCurrentParameterPage(F("Sustain"), String(value), AMP_ENV);
}

FLASHMEM void updateRelease(float value)
{
  groupvec[activeGroupIndex]->setAmpRelease(value);
  showCurrentParameterPage(F("Release"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateOscFX(uint8_t value)
{
  groupvec[activeGroupIndex]->setOscFX(value);
  if (value == 2)
  {
    showCurrentParameterPage(F("Osc FX"), F("On - X Mod"));
//    analogWriteFrequency(OSC_FX_LED, 1); // This is to make the LED flash using PWM rather than some thread
//    analogWrite(OSC_FX_LED, 127);
  }
  else if (value == 1)
  {
    showCurrentParameterPage(F("Osc FX"), F("On - XOR"));
//    pinMode(OSC_FX_LED, OUTPUT);
//    digitalWriteFast(OSC_FX_LED, HIGH); // LED on
  }
  else
  {
    showCurrentParameterPage(F("Osc FX"), F("Off"));
//    pinMode(OSC_FX_LED, OUTPUT);
//    digitalWriteFast(OSC_FX_LED, LOW); // LED off
  }
}

FLASHMEM void updateEffectAmt(float value)
{
  groupvec[activeGroupIndex]->setEffectAmount(value);
  showCurrentParameterPage(F("Effect Amt"), String(value) + F(" Hz"));
}

FLASHMEM void updateEffectMix(float value)
{
  groupvec[activeGroupIndex]->setEffectMix(value);
  showCurrentParameterPage(F("Effect Mix"), String(value));
}

FLASHMEM void updatePatch(String name, uint32_t index)
{
  groupvec[activeGroupIndex]->setPatchName(name);
  groupvec[activeGroupIndex]->setPatchIndex(index);
  showPatchPage(String(index), name);
}

void myPitchBend(byte channel, int bend)
{
  // 0.5 to give 1oct max - spread of mod is 2oct
  groupvec[activeGroupIndex]->pitchBend(bend * 0.5f * pitchBendRange * DIV12 * DIV8192);
}

void myControlChange(byte channel, byte control, byte value)
{
  switch (control)
  {
  case CCvolume:
    updateVolume(LINEAR[value]);
    break;
  case CCunison:
    updateUnison(inRangeOrDefault<int>(value, 2, 0, 2));
    break;

  case CCglide:
    updateGlide(POWER[value]);
    break;

  case CCpitchenv:
    updatePitchEnv(LINEARCENTREZERO[value] * OSCMODMIXERMAX);
    break;

  case CCoscwaveformA:
    updateWaveformA((uint32_t)clampInto(WAVEFORMS_A, value));
    break;

  case CCoscwaveformB:
    updateWaveformB((uint32_t)clampInto(WAVEFORMS_B, value));
    break;

  case CCpitchA:
    updatePitchA(PITCH[value]);
    break;

  case CCpitchB:
    updatePitchB(PITCH[value]);
    break;

  case CCdetune:
    updateDetune(1.0f - (MAXDETUNE * POWER[value]), value);
    break;

  case CCpwmSource:
    updatePWMSource(value > 0 ? PWMSOURCEFENV : PWMSOURCELFO);
    break;

  case CCpwmRate:
    // Uses combination of PWMRate, PWa and PWb
    updatePWMRate(PWMRATE[value]);
    break;

  case CCpwmAmt:
    // NO FRONT PANEL CONTROL - MIDI CC ONLY
    // Total PWM amount for both oscillators
    updatePWMAmount(LINEAR[value]);
    break;

  case CCpwA:
    updatePWA(LINEARCENTREZERO[value], LINEAR[value]);
    break;

  case CCpwB:
    updatePWB(LINEARCENTREZERO[value], LINEAR[value]);
    break;

  case CCoscLevelA:
    updateOscLevelA(LINEAR[value]);
    break;

  case CCoscLevelB:
    updateOscLevelB(LINEAR[value]);
    break;

  case CCnoiseLevel:
    updateNoiseLevel(LINEARCENTREZERO[value]);
    break;

  case CCfilterfreq:
    // Pick up
    if (!pickUpActive && pickUp && (filterfreqPrevValue < FILTERFREQS256[(value - TOLERANCE) * 2] || filterfreqPrevValue > FILTERFREQS256[(value - TOLERANCE) * 2]))
      return; // PICK-UP

    // MIDI is 7 bit, 128 values and needs to choose alternate filterfreqs(8 bit) by multiplying by 2
    updateFilterFreq(FILTERFREQS256[value * 2]);
    filterfreqPrevValue = FILTERFREQS256[value * 2]; // PICK-UP
    break;

  case CCfilterres:
    // Pick up
    if (!pickUpActive && pickUp && (resonancePrevValue < ((14.29f * POWER[value - TOLERANCE]) + 0.71f) || resonancePrevValue > ((14.29f * POWER[value + TOLERANCE]) + 0.71f)))
      return; // PICK-UP

    // If <1.1 there is noise at high cutoff freq
    updateFilterRes(FILTERRESONANCE[value]);
    resonancePrevValue = FILTERRESONANCE[value]; // PICK-UP
    break;

  case CCfiltermixer:
    // Pick up
    if (!pickUpActive && pickUp && (filterMixPrevValue < LINEAR_FILTERMIXER[value - TOLERANCE] || filterMixPrevValue > LINEAR_FILTERMIXER[value + TOLERANCE]))
      return; // PICK-UP

    updateFilterMixer(LINEAR_FILTERMIXER[value]);
    filterMixPrevValue = LINEAR_FILTERMIXER[value]; // PICK-UP
    break;

  case CCfilterenv:
    updateFilterEnv(LINEARCENTREZERO[value] * FILTERMODMIXERMAX);
    break;

  case CCkeytracking:
    updateKeyTracking(KEYTRACKINGAMT[value]);
    break;

  case CCmodwheel:
    // Variable LFO amount from mod wheel - Settings Option
    updateModWheel(POWER[value] * modWheelDepth);
    break;

  case CCosclfoamt:
    // Pick up
    if (!pickUpActive && pickUp && (oscLfoAmtPrevValue < POWER[value - TOLERANCE] || oscLfoAmtPrevValue > POWER[value + TOLERANCE]))
      return; // PICK-UP

    updatePitchLFOAmt(POWER[value]);
    oscLfoAmtPrevValue = POWER[value]; // PICK-UP
    break;

  case CCoscLfoRate:
  {
    // Pick up
    if (!pickUpActive && pickUp && (oscLfoRatePrevValue < LFOMAXRATE * POWER[value - TOLERANCE] || oscLfoRatePrevValue > LFOMAXRATE * POWER[value + TOLERANCE]))
      return; // PICK-UP

    float rate = 0.0;
    if (groupvec[activeGroupIndex]->getPitchLfoMidiClockSync())
    {
      // TODO: MIDI Tempo stuff remains global?
      lfoTempoValue = LFOTEMPO[value];
      oscLFOTimeDivStr = LFOTEMPOSTR[value];
      rate = lfoSyncFreq * LFOTEMPO[value];
    }
    else
    {
      rate = LFOMAXRATE * POWER[value];
    }
    updatePitchLFORate(rate);
    oscLfoRatePrevValue = rate; // PICK-UP
    break;
  }

  case CCoscLfoWaveform:
    updatePitchLFOWaveform(getLFOWaveform(value));
    break;

  case CCosclforetrig:
    updatePitchLFORetrig(value > 0);
    break;

  case CCfilterLFOMidiClkSync:
    updateFilterLFOMidiClkSync(value > 0);
    break;

  case CCfilterlforate:
  {
    // Pick up
    if (!pickUpActive && pickUp && (filterLfoRatePrevValue < LFOMAXRATE * POWER[value - TOLERANCE] || filterLfoRatePrevValue > LFOMAXRATE * POWER[value + TOLERANCE]))
      return; // PICK-UP

    float rate;
    String timeDivStr = "";
    if (groupvec[activeGroupIndex]->getFilterLfoMidiClockSync())
    {
      lfoTempoValue = LFOTEMPO[value];
      rate = lfoSyncFreq * LFOTEMPO[value];
      timeDivStr = LFOTEMPOSTR[value];
    }
    else
    {
      rate = LFOMAXRATE * POWER[value];
    }

    updateFilterLfoRate(rate, timeDivStr);
    filterLfoRatePrevValue = rate; // PICK-UP
    break;
  }

  case CCfilterlfoamt:
    // Pick up
    if (!pickUpActive && pickUp && (filterLfoAmtPrevValue < LINEAR[value - TOLERANCE] * FILTERMODMIXERMAX || filterLfoAmtPrevValue > LINEAR[value + TOLERANCE] * FILTERMODMIXERMAX))
      return; // PICK-UP

    updateFilterLfoAmt(LINEAR[value] * FILTERMODMIXERMAX);
    filterLfoAmtPrevValue = LINEAR[value] * FILTERMODMIXERMAX; // PICK-UP
    break;

  case CCfilterlfowaveform:
    updateFilterLFOWaveform(getLFOWaveform(value));
    break;

  case CCfilterlforetrig:
    updateFilterLFORetrig(value > 0);
    break;

  // MIDI Only
  case CCoscLFOMidiClkSync:
    updatePitchLFOMidiClkSync(value > 0);
    break;

  case CCfilterattack:
    updateFilterAttack(ENVTIMES[value]);
    break;

  case CCfilterdecay:
    updateFilterDecay(ENVTIMES[value]);
    break;

  case CCfiltersustain:
    updateFilterSustain(LINEAR[value]);
    break;

  case CCfilterrelease:
    updateFilterRelease(ENVTIMES[value]);
    break;

  case CCampattack:
    updateAttack(ENVTIMES[value]);
    break;

  case CCampdecay:
    updateDecay(ENVTIMES[value]);
    break;

  case CCampsustain:
    updateSustain(LINEAR[value]);
    break;

  case CCamprelease:
    updateRelease(ENVTIMES[value]);
    break;

  case CCoscfx:
    updateOscFX(inRangeOrDefault<int>(value, 2, 0, 2));
    break;

  case CCfxamt:
    // Pick up
    if (!pickUpActive && pickUp && (fxAmtPrevValue < ENSEMBLE_LFO[value - TOLERANCE] || fxAmtPrevValue > ENSEMBLE_LFO[value + TOLERANCE]))
      return; // PICK-UP
    updateEffectAmt(ENSEMBLE_LFO[value]);
    fxAmtPrevValue = ENSEMBLE_LFO[value]; // PICK-UP
    break;

  case CCfxmix:
    // Pick up
    if (!pickUpActive && pickUp && (fxMixPrevValue < LINEAR[value - TOLERANCE] || fxMixPrevValue > LINEAR[value + TOLERANCE]))
      return; // PICK-UP
    updateEffectMix(LINEAR[value]);
    fxMixPrevValue = LINEAR[value]; // PICK-UP
    break;

  case CCallnotesoff:
    groupvec[activeGroupIndex]->allNotesOff();
    break;
  }
}

FLASHMEM void myMIDIClockStart()
{
  setMIDIClkSignal(true);
  // Resync LFOs when MIDI Clock starts.
  // When there's a jump to a different
  // part of a track, such as in a DAW, the DAW must have same
  // rhythmic quantisation as Tempo Div.

  // TODO: Apply to all groupvec[activeGroupIndex]-> Maybe check channel?
  groupvec[activeGroupIndex]->midiClockStart();
}

FLASHMEM void myMIDIClockStop()
{
  setMIDIClkSignal(false);
}

FLASHMEM void myMIDIClock()
{
  // This recalculates the LFO frequencies if the tempo changes (MIDI cLock is 24ppq)
  if (count > 23)
  {
    // TODO: Most of this needs to move into the VoiceGroup

    setMIDIClkSignal(!getMIDIClkSignal());
    long timeNow = millis();
    midiClkTimeInterval = (timeNow - previousMillis);
    lfoSyncFreq = 1000.0f / midiClkTimeInterval;
    previousMillis = timeNow;
    groupvec[activeGroupIndex]->midiClock(lfoSyncFreq * lfoTempoValue);
    count = 0;
  }

  count++;
}

void checkVolumePot()
{
  volumeRead = adc->adc0->analogRead(VOLUME_POT);
  if (volumeRead > (volumePrevious + QUANTISE_FACTOR_VOL) || volumeRead < (volumePrevious - QUANTISE_FACTOR_VOL))
  {
    volumePrevious = volumeRead;
    volumeRead = (volumeRead >> 5); // Change range to 0-127
    myControlChange(midiChannel, CCvolume, volumeRead);
  }
}

void showSettingsPage()
{
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

FLASHMEM String getCurrentPatchData()
{
    auto p = groupvec[activeGroupIndex]->params();
    return patchName + F(",") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(",") + String(groupvec[activeGroupIndex]->getOscLevelB()) + F(",") + String(groupvec[activeGroupIndex]->getPinkNoiseLevel() - groupvec[activeGroupIndex]->getWhiteNoiseLevel()) + F(",") + String(p.unisonMode) + F(",") + String(groupvec[activeGroupIndex]->getOscFX()) + F(",") + String(p.detune, 5) + F(",") + String(lfoSyncFreq) + F(",") + String(midiClkTimeInterval) + F(",") + String(lfoTempoValue) + F(",") + String(groupvec[activeGroupIndex]->getKeytrackingAmount()) + F(",") + String(p.glideSpeed, 5) + F(",") + String(p.oscPitchA) + F(",") + String(p.oscPitchB) + F(",") + String(groupvec[activeGroupIndex]->getWaveformA()) + F(",") + String(groupvec[activeGroupIndex]->getWaveformB()) + F(",") +
           String(groupvec[activeGroupIndex]->getPwmSource()) + F(",") + String(groupvec[activeGroupIndex]->getPwmAmtA()) + F(",") + String(groupvec[activeGroupIndex]->getPwmAmtB()) + F(",") + String(groupvec[activeGroupIndex]->getPwmRate()) + F(",") + String(groupvec[activeGroupIndex]->getPwA()) + F(",") + String(groupvec[activeGroupIndex]->getPwB()) + F(",") + String(groupvec[activeGroupIndex]->getResonance()) + F(",") + String(groupvec[activeGroupIndex]->getCutoff()) + F(",") + String(groupvec[activeGroupIndex]->getFilterMixer()) + F(",") + String(groupvec[activeGroupIndex]->getFilterEnvelope()) + F(",") + String(groupvec[activeGroupIndex]->getPitchLfoAmount(), 5) + F(",") + String(groupvec[activeGroupIndex]->getPitchLfoRate(), 5) + F(",") + String(groupvec[activeGroupIndex]->getPitchLfoWaveform()) + F(",") + String(int(groupvec[activeGroupIndex]->getPitchLfoRetrig())) + F(",") + String(int(groupvec[activeGroupIndex]->getPitchLfoMidiClockSync())) + F(",") + String(groupvec[activeGroupIndex]->getFilterLfoRate(), 5) + F(",") +
           groupvec[activeGroupIndex]->getFilterLfoRetrig() + F(",") + groupvec[activeGroupIndex]->getFilterLfoMidiClockSync() + F(",") + groupvec[activeGroupIndex]->getFilterLfoAmt() + F(",") + groupvec[activeGroupIndex]->getFilterLfoWaveform() + F(",") + groupvec[activeGroupIndex]->getFilterAttack() + F(",") + groupvec[activeGroupIndex]->getFilterDecay() + F(",") + groupvec[activeGroupIndex]->getFilterSustain() + F(",") + groupvec[activeGroupIndex]->getFilterRelease() + F(",") + groupvec[activeGroupIndex]->getAmpAttack() + F(",") + groupvec[activeGroupIndex]->getAmpDecay() + F(",") + groupvec[activeGroupIndex]->getAmpSustain() + F(",") + groupvec[activeGroupIndex]->getAmpRelease() + F(",") +
           String(groupvec[activeGroupIndex]->getEffectAmount()) + F(",") + String(groupvec[activeGroupIndex]->getEffectMix()) + F(",") + String(groupvec[activeGroupIndex]->getPitchEnvelope()) + F(",") + String(velocitySens) + F(",") + String(p.chordDetune) + F(",") + String(groupvec[activeGroupIndex]->getMonophonicMode()) + F(",") + String(0.0f) + F(",") + String(0.0f);
}

FLASHMEM void reinitialiseToPanel()
{
    // This sets the current patch to be the same as the current hardware panel state - all the pots
    // The four button controls stay the same state
    // This reinialises the previous hardware values to force a re-read
//  muxInput = 0;
//  for (int i = 0; i < MUXCHANNELS; i++)
//  {
//    mux1ValuesPrev[i] = RE_READ;
//    mux2ValuesPrev[i] = RE_READ;
//  }
    volumePrevious = RE_READ;
    patchName = INITPATCHNAME;
}
FLASHMEM void setCurrentPatchData(String data[])
{
    updatePatch(data[0], patchNo);
    updateOscLevelA(data[1].toFloat());
    updateOscLevelB(data[2].toFloat());
    updateNoiseLevel(data[3].toFloat());
    updateUnison(data[4].toInt());
    updateOscFX(data[5].toInt());
    updateDetune(data[6].toFloat(), data[48].toInt());
    // Why is this MIDI Clock stuff part of the patch??
    lfoSyncFreq = data[7].toInt();
    midiClkTimeInterval = data[8].toInt();
    lfoTempoValue = data[9].toFloat();
    updateKeyTracking(data[10].toFloat());
    updateGlide(data[11].toFloat());
    updatePitchA(data[12].toFloat());
    updatePitchB(data[13].toFloat());
    updateWaveformA(data[14].toInt());
    updateWaveformB(data[15].toInt());
    updatePWMSource(data[16].toInt());
    updatePWA(data[20].toFloat(), data[17].toFloat());
    updatePWB(data[21].toFloat(), data[18].toFloat());
    updatePWMRate(data[19].toFloat());
    updateFilterRes(data[22].toFloat());
    resonancePrevValue = data[22].toFloat(); // Pick-up
    updateFilterFreq(data[23].toFloat());
    filterfreqPrevValue = data[23].toInt(); // Pick-up
    updateFilterMixer(data[24].toFloat());
    filterMixPrevValue = data[24].toFloat(); // Pick-up
    updateFilterEnv(data[25].toFloat());
    updatePitchLFOAmt(data[26].toFloat());
    oscLfoAmtPrevValue = data[26].toFloat(); // PICK-UP
    updatePitchLFORate(data[27].toFloat());
    oscLfoRatePrevValue = data[27].toFloat(); // PICK-UP
    updatePitchLFOWaveform(data[28].toInt());
    updatePitchLFORetrig(data[29].toInt() > 0);
    updatePitchLFOMidiClkSync(data[30].toInt() > 0); // MIDI CC Only
    updateFilterLfoRate(data[31].toFloat(), "");
    filterLfoRatePrevValue = data[31].toFloat(); // PICK-UP
    updateFilterLFORetrig(data[32].toInt() > 0);
    updateFilterLFOMidiClkSync(data[33].toInt() > 0);
    updateFilterLfoAmt(data[34].toFloat());
    filterLfoAmtPrevValue = data[34].toFloat(); // PICK-UP
    updateFilterLFOWaveform(data[35].toFloat());
    updateFilterAttack(data[36].toFloat());
    updateFilterDecay(data[37].toFloat());
    updateFilterSustain(data[38].toFloat());
    updateFilterRelease(data[39].toFloat());
    updateAttack(data[40].toFloat());
    updateDecay(data[41].toFloat());
    updateSustain(data[42].toFloat());
    updateRelease(data[43].toFloat());
    updateEffectAmt(data[44].toFloat());
    fxAmtPrevValue = data[44].toFloat(); // PICK-UP
    updateEffectMix(data[45].toFloat());
    fxMixPrevValue = data[45].toFloat(); // PICK-UP
    updatePitchEnv(data[46].toFloat());
    velocitySens = data[47].toFloat();
    groupvec[activeGroupIndex]->setMonophonic(data[49].toInt());
    //  SPARE1 = data[50].toFloat();
    //  SPARE2 = data[51].toFloat();

    Serial.print(F("Set Patch: "));
    Serial.println(data[0]);
}


FLASHMEM void recallPatch(int patchNo)
{
    groupvec[activeGroupIndex]->allNotesOff();
    groupvec[activeGroupIndex]->closeEnvelopes();
    File patchFile = SD.open(String(patchNo).c_str());
    if (!patchFile)
    {
        Serial.println(F("File not found"));
    }
    else
    {
        String data[NO_OF_PARAMS]; // Array of data read in
        recallPatchData(patchFile, data);
        setCurrentPatchData(data);
        patchFile.close();
    }
}

void checkSwitches()
{
    sectionSwitch.update();
  saveButton.update();
  if (saveButton.held())
  {
    switch (state)
    {
    case PARAMETER:
    case PATCH:
      state = DELETE;
      break;
    }
  }
  else if (saveButton.numClicks() == 1)
  {
    switch (state)
    {
    case PARAMETER:
      if (patches.size() < PATCHES_LIMIT)
      {
        resetPatchesOrdering(); // Reset order of patches from first patch
        patches.push({patches.size() + 1, INITPATCHNAME});
        state = SAVE;
      }
      break;
    case SAVE:
      // Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
      patchName = patches.last().patchName;
      state = PATCH;
      savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
      showPatchPage(patches.last().patchNo, patches.last().patchName);
      patchNo = patches.last().patchNo;
      loadPatches(); // Get rid of pushed patch if it wasn't saved
      setPatchesOrdering(patchNo);
      renamedPatch = "";
      state = PARAMETER;
      break;
    case PATCHNAMING:
      if (renamedPatch.length() > 0)
        patchName = renamedPatch; // Prevent empty strings
      state = PATCH;
      savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
      showPatchPage(patches.last().patchNo, patchName);
      patchNo = patches.last().patchNo;
      loadPatches(); // Get rid of pushed patch if it wasn't saved
      setPatchesOrdering(patchNo);
      renamedPatch = "";
      state = PARAMETER;
      break;
    }
  }

  settingsButton.update();
  if (settingsButton.held())
  {
    // If recall held, set current patch to match current hardware state
    // Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
  }
  else if (settingsButton.numClicks() == 1)
  {
    switch (state)
    {
    case PARAMETER:
      state = SETTINGS;
      showSettingsPage();
      break;
    case SETTINGS:
      showSettingsPage();
    case SETTINGSVALUE:
      settings::save_current_value();
      state = SETTINGS;
      showSettingsPage();
      break;
    }
  }

  backButton.update();
  if (backButton.held())
  {
    // If Back button held, Panic - all notes off
    groupvec[activeGroupIndex]->allNotesOff();
    groupvec[activeGroupIndex]->closeEnvelopes();
  }
  else if (backButton.numClicks() == 1)
  {
    switch (state)
    {
    case RECALL:
      setPatchesOrdering(patchNo);
      state = PARAMETER;
      break;
    case SAVE:
      renamedPatch = "";
      state = PARAMETER;
      loadPatches(); // Remove patch that was to be saved
      setPatchesOrdering(patchNo);
      break;
    case PATCHNAMING:
      charIndex = 0;
      renamedPatch = "";
      state = SAVE;
      break;
    case DELETE:
      setPatchesOrdering(patchNo);
      state = PARAMETER;
      break;
    case SETTINGS:
      state = PARAMETER;
      break;
    case SETTINGSVALUE:
      state = SETTINGS;
      showSettingsPage();
      break;
    }
  }

  // Encoder switch
  recallButton.update();
  if (recallButton.held())
  {
    // If Recall button held, return to current patch setting
    // which clears any changes made
    state = PATCH;
    // Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
  }
  else if (recallButton.numClicks() == 1)
  {
    switch (state)
    {
    case PARAMETER:
      state = RECALL; // show patch list
      break;
    case RECALL:
      state = PATCH;
      // Recall the current patch
      patchNo = patches.first().patchNo;
      recallPatch(patchNo);
      state = PARAMETER;
      break;
    case SAVE:
      showRenamingPage(patches.last().patchName);
      patchName = patches.last().patchName;
      state = PATCHNAMING;
      break;
    case PATCHNAMING:
      if (renamedPatch.length() < 12) // actually 12 chars
      {
        renamedPatch.concat(String(currentCharacter));
        charIndex = 0;
        currentCharacter = CHARACTERS[charIndex];
        showRenamingPage(renamedPatch);
      }
      break;
    case DELETE:
      // Don't delete final patch
      if (patches.size() > 1)
      {
        state = DELETEMSG;
        patchNo = patches.first().patchNo;    // PatchNo to delete from SD card
        patches.shift();                      // Remove patch from circular buffer
        deletePatch(String(patchNo).c_str()); // Delete from SD card
        loadPatches();                        // Repopulate circular buffer to start from lowest Patch No
        renumberPatchesOnSD();
        loadPatches();                     // Repopulate circular buffer again after delete
        patchNo = patches.first().patchNo; // Go back to 1
        recallPatch(patchNo);              // Load first patch
      }
      state = PARAMETER;
      break;
    case SETTINGS:
      state = SETTINGSVALUE;
      showSettingsPage();
      break;
    case SETTINGSVALUE:
      settings::save_current_value();
      state = SETTINGS;
      showSettingsPage();
      break;
    }
  }
}

uint8_t fromMix(float mixA, float mixB)
{
    for (size_t i = 0; i < 128; i++)
    {
        if (LINEAR[OSCMIXA[i]] == mixA && LINEAR[OSCMIXB[i]] == mixB)
        {
            return i;
        }
    }
    return 0;
}

void sendSysex(String ss) {
    usbMIDI.sendSysEx(ss.length(), reinterpret_cast<const uint8_t *>(ss.c_str()), false);
}

void updateSection(byte encIndex, bool moveUp) {
    switch(section) {
        case Section::Osc1:
            switch(encIndex) {
                case 0:{
                    auto newVal = cycleIndexOf(PITCH, (int8_t) groupvec[activeGroupIndex]->params().oscPitchA, moveUp);
                    midiCCOut(CCpitchA, newVal);
                    myControlChange(midiChannel, CCpitchA, newVal);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOf(WAVEFORMS_A, (uint8_t) groupvec[activeGroupIndex]->getWaveformA(), moveUp);
                    midiCCOut(CCoscwaveformA, WAVEFORMS_A[newVal]);
                    updateWaveformA(WAVEFORMS_A[newVal]);
                    return;
                }
                case 2: {
                    auto idx = cycleIndexOf(LINEARCENTREZERO, groupvec[activeGroupIndex]->getPwA(), moveUp);
                    midiCCOut(CCpwA, LINEARCENTREZERO[idx]);
                    updatePWA(LINEARCENTREZERO[idx], LINEAR[idx]);
                    return;
                }
                case 3: /*OSC MIX*/ {
                    // 77 -> oscmixa 100 -> 0.787
                    // 0.787 -> indexof linear = 100 -> indexof oscmixa 77
                    // LINEAR[OSCMIXA[midibyte]]

                    auto midiValue = fromMix(groupvec[activeGroupIndex]->getOscLevelA(), groupvec[activeGroupIndex]->getOscLevelB());
                    const uint8_t DELTA = 8;
                    if(moveUp) {
                        midiValue += min(DELTA, 128 - midiValue);
                    } else {
                        midiValue -= min(DELTA, midiValue);
                    }
                    midiCCOut(CCoscLevelA, midiValue);
                    midiCCOut(CCoscLevelB, midiValue);
                    updateOscLevelA(LINEAR[OSCMIXA[midiValue]]);
                    updateOscLevelB(LINEAR[OSCMIXB[midiValue]]);
                    return;
                }
            }
            break;
        case Section::Osc2:
            switch(encIndex) {
                case 0:{
                    auto newVal = cycleIndexOf(PITCH, (int8_t) groupvec[activeGroupIndex]->params().oscPitchB, moveUp);
                    midiCCOut(CCpitchB, newVal);
                    myControlChange(midiChannel, CCpitchB, newVal);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOf(WAVEFORMS_B, (uint8_t) groupvec[activeGroupIndex]->getWaveformB(), moveUp);
                    midiCCOut(CCoscwaveformB, WAVEFORMS_B[newVal]);
                    updateWaveformB(WAVEFORMS_B[newVal]);
                    return;
                }
                case 2: {
                    auto idx = cycleIndexOf(LINEARCENTREZERO, groupvec[activeGroupIndex]->getPwB(), moveUp);
                    midiCCOut(CCpwB, LINEARCENTREZERO[idx]);
                    updatePWB(LINEARCENTREZERO[idx], LINEAR[idx]);
                    return;
                }
                // todo check. chord detune is an enum (unison 2 ?), detune (unison 1?) is a percent. pick a unit for each.
                case 3: /*detune*/ {
                    byte mux1Read = cycleByte((uint8_t)groupvec[activeGroupIndex]->params().chordDetune, moveUp, false);
                    midiCCOut(CCdetune, mux1Read);
                    myControlChange(midiChannel, CCdetune, mux1Read);
                    return;
                }
            }
            break;
        case Section::Noise:
            // "Noise", "Env", "PWM Rate", "Osc FX"
            switch(encIndex) {
                case 0:{
                    float value = groupvec[activeGroupIndex]->getPinkNoiseLevel() > 0
                                  ? groupvec[activeGroupIndex]->getPinkNoiseLevel()
                                  : (- groupvec[activeGroupIndex]->getWhiteNoiseLevel());
                    byte mux1Read = cycleIndexOfSorted(LINEARCENTREZERO,
                                                       value, moveUp, false);

                    midiCCOut(CCnoiseLevel, mux1Read);
                    myControlChange(midiChannel, CCnoiseLevel, mux1Read);
                    return;
                }
                case 1:{
                    float value = groupvec[activeGroupIndex]->getPitchEnvelope() / OSCMODMIXERMAX;

                    byte mux1Read = cycleIndexOfSorted(LINEARCENTREZERO,
                                                       value, moveUp, false);
                    midiCCOut(CCpitchenv, mux1Read);
                    myControlChange(midiChannel, CCpitchenv, mux1Read);

                     return;
                }
                case 2: {
                    float value = groupvec[activeGroupIndex]->getPwmRate();
                    byte mux1Read = cycleIndexOfSorted(PWMRATE,
                                                       value, moveUp, false);
                    midiCCOut(CCpwmRate, mux1Read);
                    myControlChange(midiChannel, CCpwmRate, mux1Read);
                    return;
                }
                case 3: {
                    byte value = (groupvec[activeGroupIndex]->getOscFX() + 3 + (moveUp ? 1 : -1)) % 3;
                    midiCCOut(CCoscfx, value);
                    myControlChange(midiChannel, CCoscfx, value);
                    return;
                }
            }
            break;
        case Section::LFO:
            // "Level", "Waveform", "Rate", "Unison"
            switch(encIndex) {
                case 0:{
                    float value = groupvec[activeGroupIndex]->getPitchLfoAmount();
                    byte mux1Read = cycleIndexOfSorted(POWER,
                                                       value, moveUp, false);
                    midiCCOut(CCosclfoamt, mux1Read);
                    myControlChange(midiChannel, CCosclfoamt, mux1Read);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOf(WAVEFORMS_LFO, (uint8_t) groupvec[activeGroupIndex]->getPitchLfoWaveform(), moveUp);
                    midiCCOut(CCoscLfoWaveform, WAVEFORMS_LFO_MIDI[newVal]);
                    myControlChange(midiChannel, CCoscLfoWaveform, WAVEFORMS_LFO_MIDI[newVal]);
                     return;
                }
                case 2: {

                    byte newVal = 0;
                    if (groupvec[activeGroupIndex]->getPitchLfoMidiClockSync())
                    {
                        newVal = cycleIndexOfSorted(LFOTEMPO,
                                                    groupvec[activeGroupIndex]->getPitchLfoRate() / lfoSyncFreq, moveUp, false);
                    }
                    else
                    {
                        newVal = cycleIndexOfSorted(POWER,
                                                    groupvec[activeGroupIndex]->getPitchLfoRate() / LFOMAXRATE, moveUp, false);
                    }
                    midiCCOut(CCoscLfoRate, newVal);
                    myControlChange(midiChannel, CCoscLfoRate, newVal);
                    return;
                }
                case 3: {
                    uint8_t newVal = (groupvec[activeGroupIndex]->params().unisonMode + (moveUp ? 1 : 2)) % 3;
                    midiCCOut(CCunison, newVal);
                    myControlChange(midiChannel, CCunison, newVal);
                    return;
                }
            }
            break;
        case Section::FilterEnvelope:
            // "ATK", "DECAY", "SUSTN", "REL"
            switch(encIndex) {
                case 0:{
                    auto newVal = cycleIndexOfSorted(ENVTIMES, (uint16_t) groupvec[activeGroupIndex]->getFilterAttack(), moveUp, false);
                    midiCCOut(CCfilterattack, newVal);
                    myControlChange(midiChannel, CCfilterattack, newVal);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOfSorted(ENVTIMES, (uint16_t) groupvec[activeGroupIndex]->getFilterDecay(), moveUp, false);
                    midiCCOut(CCfilterdecay, newVal);
                    myControlChange(midiChannel, CCfilterdecay, newVal);
                    return;
                }
                case 2: {
                    auto newVal = cycleIndexOfSorted(LINEAR, groupvec[activeGroupIndex]->getFilterSustain(), moveUp, false);
                    midiCCOut(CCfiltersustain, newVal);
                    myControlChange(midiChannel, CCfiltersustain, newVal);
                    return;
                }
                case 3: {
                    auto newVal = cycleIndexOfSorted(ENVTIMES, (uint16_t) groupvec[activeGroupIndex]->getFilterRelease(), moveUp, false);
                    midiCCOut(CCfilterrelease, newVal);
                    myControlChange(midiChannel, CCfilterrelease, newVal);
                    return;
                }
            }
            break;
        case Section::Filter:
            // "Cutoff", "Resonance", "Type", "Env"
            switch(encIndex) {
                case 0:{
                    auto newVal = cycleIndexOfSorted(FILTERFREQS256, (uint16_t) (groupvec[activeGroupIndex]->getCutoff()), moveUp, false);
                    // array of 256 -> byte, have to move twice to divide later
                    if(moveUp){
                        if(newVal < 255)
                            newVal++;
                    }else {
                        if(newVal > 0)
                            newVal--;
                    }
                    newVal /= 2;
                    midiCCOut(CCfilterfreq, newVal);
                    myControlChange(midiChannel, CCfilterfreq, newVal);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOfSorted(FILTERRESONANCE, groupvec[activeGroupIndex]->getResonance(), moveUp, false);
                    midiCCOut(CCfilterres, newVal);
                    myControlChange(midiChannel, CCfilterres, newVal);
                    return;
                }
                case 2: {
                    auto newVal = cycleIndexOfSorted(LINEAR_FILTERMIXER, groupvec[activeGroupIndex]->getFilterMixer(), moveUp, false);
                    midiCCOut(CCfiltermixer, newVal);
                    myControlChange(midiChannel, CCfiltermixer, newVal);
                    return;
                }
                case 3: {
                    auto newVal = cycleIndexOfSorted(LINEARCENTREZERO, groupvec[activeGroupIndex]->getFilterEnvelope(), moveUp, false);
                    midiCCOut(CCfilterenv, newVal);
                    myControlChange(midiChannel, CCfilterenv, newVal);
                    return;
                }
            }
            break;
        case Section::FilterLFO:
            // "Level", "Waveform", "Rate", "Retrig/Tempo"
            switch(encIndex) {
                case 0:{
                    float value = groupvec[activeGroupIndex]->getFilterLfoAmt();
                    byte mux1Read = cycleIndexOfSorted(LINEAR,
                                                       value, moveUp, false);
                    midiCCOut(CCfilterlfoamt, mux1Read);
                    myControlChange(midiChannel, CCfilterlfoamt, mux1Read);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOf(WAVEFORMS_LFO, (uint8_t) groupvec[activeGroupIndex]->getFilterLfoWaveform(), moveUp);
                    midiCCOut(CCfilterlfowaveform, WAVEFORMS_LFO_MIDI[newVal]);
                    myControlChange(midiChannel, CCfilterlfowaveform, WAVEFORMS_LFO_MIDI[newVal]);
                    return;
                }
                case 2: {
                    byte newVal = 0;
                    if (groupvec[activeGroupIndex]->getFilterLfoMidiClockSync())
                    {
                        newVal = cycleIndexOfSorted(LFOTEMPO,
                                                    groupvec[activeGroupIndex]->getFilterLfoRate() / lfoSyncFreq, moveUp, false);
                    }
                    else
                    {
                        newVal = cycleIndexOfSorted(POWER,
                                                    groupvec[activeGroupIndex]->getFilterLfoRate() / LFOMAXRATE, moveUp, false);
                    }
                    midiCCOut(CCfilterlforate, newVal);
                    myControlChange(midiChannel, CCfilterlforate, newVal);
                    return;
                }
                case 3: {
                    if(moveUp) // tempo
                    {
                        bool newVal = !groupvec[activeGroupIndex]->getFilterLfoMidiClockSync();
                        midiCCOut(CCfilterLFOMidiClkSync, newVal);
                        myControlChange(midiChannel, CCfilterLFOMidiClkSync, newVal);

                    } else // retrig
                    {
                        bool newVal = !groupvec[activeGroupIndex]->getFilterLfoRetrig();
                        midiCCOut(CCfilterlforetrig, newVal);
                        myControlChange(midiChannel, CCfilterlforetrig, newVal);
                    }
                    return;
                }
            }
            break;
        case Section::Amp:
            // "ATK", "DECAY", "SUSTN", "REL"
            switch(encIndex) {
                case 0:{
                    auto newVal = cycleIndexOfSorted(ENVTIMES, (uint16_t) groupvec[activeGroupIndex]->getAmpAttack(), moveUp, false);
                    midiCCOut(CCampattack, newVal);
                    myControlChange(midiChannel, CCampattack, newVal);
                    return;
                }
                case 1:{
                    auto newVal = cycleIndexOfSorted(ENVTIMES, (uint16_t) groupvec[activeGroupIndex]->getAmpDecay(), moveUp, false);
                    midiCCOut(CCampdecay, newVal);
                    myControlChange(midiChannel, CCampdecay, newVal);
                    return;
                }
                case 2: {
                    auto newVal = cycleIndexOfSorted(LINEAR, groupvec[activeGroupIndex]->getAmpSustain(), moveUp, false);
                    midiCCOut(CCampsustain, newVal);
                    myControlChange(midiChannel, CCampsustain, newVal);
                    return;
                }
                case 3: {
                    auto newVal = cycleIndexOfSorted(ENVTIMES, (uint16_t) groupvec[activeGroupIndex]->getAmpRelease(), moveUp, false);
                    midiCCOut(CCamprelease, newVal);
                    myControlChange(midiChannel, CCamprelease, newVal);
                    return;
                }
            }
            break;
        case Section::FX:
            // "Glide", "FX Amt", "FX Mix", "4"
            switch(encIndex) {
                case 0:{
                    auto newVal = cycleIndexOfSorted(POWER, groupvec[activeGroupIndex]->params().glideSpeed, moveUp, false);
                    midiCCOut(CCglide, newVal);
                    myControlChange(midiChannel, CCglide, newVal);
                     return;
                }
                case 1:{
                    auto newVal = cycleIndexOfSorted(ENSEMBLE_LFO, groupvec[activeGroupIndex]->getEffectAmount(), moveUp, false);
                    midiCCOut(CCfxamt, newVal);
                    myControlChange(midiChannel, CCfxamt, newVal);
                     return;
                }
                case 2: {
                    auto newVal = cycleIndexOfSorted(LINEAR, groupvec[activeGroupIndex]->getEffectMix(), moveUp, false);
                    midiCCOut(CCfxmix, newVal);
                    myControlChange(midiChannel, CCfxmix, newVal);
                    return;
                }
                case 3: {
                    // unused
                    return;
                }
            }
            break;
    }
    showPatchPage(String(F("ERROR")) + String(encIndex), String((int)section));
}

FLASHMEM void myProgramChange(byte channel, byte program)
{
    state = PATCH;
    patchNo = program + 1;
    recallPatch(patchNo);
    Serial.print(F("MIDI Pgm Change:"));
    Serial.println(patchNo);
    state = PARAMETER;
}


void checkMux()
{
    muxInput++;
    if (muxInput >= MUXCHANNELS)
    {
        muxInput = 0;
        checkVolumePot(); // Check here
        if (!firstPatchLoaded)
        {
            recallPatch(patchNo); // Load first patch after all controls read
            firstPatchLoaded = true;
            global.sgtl5000_1.unmuteHeadphone();
            global.sgtl5000_1.unmuteLineout();
        }
    }
}

void checkEncoder()
{
  // Encoder works with relative inc and dec values
  // Detent encoder goes up in 4 steps, hence +/-3
  encoder.update();

//    if(section != Section::None) {
        byte encIndex = 0;
        for (auto &sectionEncoder: sectionEncoders) {
            sectionEncoder.update();
            int8_t sectionDelta = sectionEncoder.getDelta();
            if (sectionDelta != 0)
                updateSection(encIndex, sectionDelta > 0);
            encIndex++;
        }
//    }
  int8_t delta = encoder.getDelta();

    if(sectionSwitch.numClicks() == 3) {
        if(dbgMode == 0)
            dbgMode = 1;
        else
            dbgMode = 0;
    }
    if(dbgMode != 0) {
        if(sectionSwitch.numClicks() == 1)
            dbgMode = dbgMode == 1 ? 2 : 1;
        if(dbgMode == 1)
            dbgX += delta;
        else
            dbgY += delta;
        return;
    }
  if (delta > 0)
  {
    if(sectionSwitch.pressed())
    {
        nextSection();
        return;
    }
    switch (state)
    {
    case PARAMETER:
      state = PATCH;
      patches.push(patches.shift());
      patchNo = patches.first().patchNo;
      recallPatch(patchNo);
      state = PARAMETER;
      // Make sure the current setting value is refreshed.
      settings::increment_setting();
      settings::decrement_setting();
      break;
    case RECALL:
      patches.push(patches.shift());
      break;
    case SAVE:
      patches.push(patches.shift());
      break;
    case PATCHNAMING:
      if (charIndex == TOTALCHARS)
        charIndex = 0; // Wrap around
      currentCharacter = CHARACTERS[charIndex++];
      showRenamingPage(renamedPatch + currentCharacter);
      break;
    case DELETE:
      patches.push(patches.shift());
      break;
    case SETTINGS:
      settings::increment_setting();
      showSettingsPage();
      break;
    case SETTINGSVALUE:
      settings::increment_setting_value();
      showSettingsPage();
      break;
    }
  }
  else if (delta < 0)
  {
      if(sectionSwitch.pressed())
      {
          prevSection();
          return;
      }
    switch (state)
    {
    case PARAMETER:
      state = PATCH;
      patches.unshift(patches.pop());
      patchNo = patches.first().patchNo;
      recallPatch(patchNo);
      state = PARAMETER;
      // Make sure the current setting value is refreshed.
      settings::increment_setting();
      settings::decrement_setting();
      break;
    case RECALL:
      patches.unshift(patches.pop());
      break;
    case SAVE:
      patches.unshift(patches.pop());
      break;
    case PATCHNAMING:
      if (charIndex == -1)
        charIndex = TOTALCHARS - 1;
      currentCharacter = CHARACTERS[charIndex--];
      showRenamingPage(renamedPatch + currentCharacter);
      break;
    case DELETE:
      patches.unshift(patches.pop());
      break;
    case SETTINGS:
      settings::decrement_setting();
      showSettingsPage();
      break;
    case SETTINGSVALUE:
      settings::decrement_setting_value();
      showSettingsPage();
      break;
    }
  }
}

void CPUMonitor()
{
  Serial.print(F(" CPU:"));
  Serial.print(AudioProcessorUsage());
  Serial.print(F(" ("));
  Serial.print(AudioProcessorUsageMax());
  Serial.print(F(")"));
  Serial.print(F("  MEM:"));
  Serial.println(AudioMemoryUsageMax());
  delayMicroseconds(500);
}


FLASHMEM void setup()
{
    // Initialize the voice groups.
    uint8_t total = 0;
    while (total < global.maxVoices())
    {
        VoiceGroup *currentGroup = new VoiceGroup{global.SharedAudio[groupvec.size()]};

        for (uint8_t i = 0; total < global.maxVoices() && i < global.maxVoicesPerGroup(); i++)
        {
            Voice *v = new Voice(global.Oscillators[i], i);
            currentGroup->add(v);
            total++;
        }

        groupvec.push_back(currentGroup);
    }

    setupDisplay();
    setUpSettings();
    setupHardware();

    AudioMemory(60);
    global.sgtl5000_1.enable();
    global.sgtl5000_1.volume(0.5 * SGTL_MAXVOLUME);
    global.sgtl5000_1.dacVolumeRamp();
    global.sgtl5000_1.muteHeadphone();
    global.sgtl5000_1.muteLineout();
    global.sgtl5000_1.audioPostProcessorEnable();
    global.sgtl5000_1.enhanceBass(0.85, 0.87, 0, 4); // Normal level, bass level, HPF bypass (1 - on), bass cutoff freq
    global.sgtl5000_1.enhanceBassDisable();          // Turned on from EEPROM
    global.sgtl5000_1.adcHighPassFilterDisable();

//    Serial.begin(9600);
//    while(!Serial);

    cardStatus = SD.begin(BUILTIN_SDCARD);
    if (cardStatus)
    {
        Serial.println(F("SD card is connected"));
        // Get patch numbers and names from SD card
        loadPatches();
        if (patches.size() == 0)
        {
            // save an initialised patch to SD card
            savePatch("1", INITPATCH);
            loadPatches();
        }
    }
    else
    {
        Serial.println(F("SD card is not connected or unusable"));
        reinitialiseToPanel();
        showPatchPage(F("No SD"), F("conn'd / usable"));
    }

    // Read MIDI Channel from EEPROM
    midiChannel = getMIDIChannel();
    Serial.println(F("MIDI In Ch:") + String(midiChannel) + F(" (0 is Omni On)"));

    // USB HOST MIDI Class Compliant
    delay(200); // Wait to turn on USB Host
    myusb.begin();
    midi1.setHandleControlChange(myControlChange);
    midi1.setHandleNoteOff(myNoteOff);
    midi1.setHandleNoteOn(myNoteOn);
    midi1.setHandlePitchChange(myPitchBend);
    midi1.setHandleProgramChange(myProgramChange);
    midi1.setHandleClock(myMIDIClock);
    midi1.setHandleStart(myMIDIClockStart);
    midi1.setHandleStop(myMIDIClockStop);
    Serial.println(F("USB HOST MIDI Class Compliant Listening"));

    // USB Client MIDI
    usbMIDI.setHandleControlChange(myControlChange);
    usbMIDI.setHandleNoteOff(myNoteOff);
    usbMIDI.setHandleNoteOn(myNoteOn);
    usbMIDI.setHandlePitchChange(myPitchBend);
    usbMIDI.setHandleProgramChange(myProgramChange);
    usbMIDI.setHandleClock(myMIDIClock);
    usbMIDI.setHandleStart(myMIDIClockStart);
    usbMIDI.setHandleStop(myMIDIClockStop);
    Serial.println(F("USB Client MIDI Listening"));

    // MIDI 5 Pin DIN
    MIDI.begin();
    MIDI.setHandleNoteOn(myNoteOn);
    MIDI.setHandleNoteOff(myNoteOff);
    MIDI.setHandlePitchBend(myPitchBend);
    MIDI.setHandleControlChange(myControlChange);
    MIDI.setHandleProgramChange(myProgramChange);
    MIDI.setHandleClock(myMIDIClock);
    MIDI.setHandleStart(myMIDIClockStart);
    MIDI.setHandleStop(myMIDIClockStop);
    Serial.println(F("MIDI In DIN Listening"));

    volumePrevious = RE_READ; // Force volume control to be read and set to current

    // Read Pitch Bend Range from EEPROM
    pitchBendRange = getPitchBendRange();
    // Read Mod Wheel Depth from EEPROM
    modWheelDepth = getModWheelDepth();
    // Read MIDI Out Channel from EEPROM
    midiOutCh = getMIDIOutCh();
    // Read MIDI Thru mode from EEPROM
    MIDIThru = getMidiThru();
    changeMIDIThruMode();
    // Read Encoder Direction from EEPROM
    encCW = getEncoderDir();
    // Read Pick-up enable from EEPROM - experimental feature
    pickUp = getPickupEnable();
    // Read bass enhance enable from EEPROM
    if (getBassEnhanceEnable())
        global.sgtl5000_1.enhanceBassEnable();
    // Read oscilloscope enable from EEPROM
    enableScope(getScopeEnable());
    // Read VU enable from EEPROM
    vuMeter = getVUEnable();
    // Read Filter and Amp Envelope shapes
    reloadFiltEnv();
    reloadAmpEnv();
    reloadGlideShape();
}

void loop()
{
  // USB HOST MIDI Class Compliant
  myusb.Task();
  midi1.read();
  // USB Client MIDI
  usbMIDI.read();
  // MIDI 5 Pin DIN
  MIDI.read();
   checkMux();

    checkVolumePot(); // Check here
//    if (!firstPatchLoaded)
//    {
//      recallPatch(patchNo); // Load first patch after all controls read
//      firstPatchLoaded = true;
//      global.sgtl5000_1.unmuteHeadphone();
//      global.sgtl5000_1.unmuteLineout();
//    }
   checkSwitches();
  checkEncoder();
  // CPUMonitor();
}
