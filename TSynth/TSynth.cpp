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

FLASHMEM void updateGlide(uint8_t midiValue)
{
    float glideSpeed = POWER[midiValue];
  groupvec[activeGroupIndex]->params().glideSpeed = glideSpeed;
  showCurrentParameterPage(F("Glide"), milliToString(glideSpeed * GLIDEFACTOR));
}

FLASHMEM void updateWaveformA(uint8_t waveform)
{
  groupvec[activeGroupIndex]->setWaveformA(WAVEFORMS_A[waveform]);
  showCurrentParameterPage(F("1. Waveform"), getWaveformStr(WAVEFORMS_A[waveform]));
}

FLASHMEM void updateWaveformB(uint8_t waveform)
{
  groupvec[activeGroupIndex]->setWaveformB(WAVEFORMS_B[waveform]);
  showCurrentParameterPage(F("2. Waveform"), getWaveformStr(waveform));
}

FLASHMEM void updatePitchA(uint8_t pitchMidi)
{
    int pitch = PITCH[pitchMidi];
  groupvec[activeGroupIndex]->params().oscPitchA = pitch;
  groupvec[activeGroupIndex]->updateVoices();
  showCurrentParameterPage("1. Semitones", (pitch > 0 ? "+" : "") + String(pitch));
}

FLASHMEM void updatePitchB(uint8_t pitchMidi)
{
    int pitch = PITCH[pitchMidi];
  groupvec[activeGroupIndex]->params().oscPitchB = pitch;
  groupvec[activeGroupIndex]->updateVoices();
  showCurrentParameterPage(F("2. Semitones"), (pitch > 0 ? "+" : "") + String(pitch));
}

FLASHMEM void updateDetune(uint8_t detuneMidi)
{
    float detune = 1.0f - (MAXDETUNE * POWER[detuneMidi]);
  groupvec[activeGroupIndex]->params().detune = detune;
  groupvec[activeGroupIndex]->params().chordDetune = detuneMidi;
  groupvec[activeGroupIndex]->updateVoices();

  if (groupvec[activeGroupIndex]->params().unisonMode == 2)
  {
    showCurrentParameterPage(F("Chord"), CDT_STR[detuneMidi % 19]);
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

FLASHMEM void updatePWMRate(uint8_t midiValue)
{
  float value = PWMRATE[midiValue];
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

FLASHMEM void updatePWA(uint8_t valuePwAMidi, uint8_t valuePwmAmtAMidi)
{
  float valuePwA = LINEARCENTREZERO[valuePwAMidi];
  float valuePwmAmtA = LINEAR[valuePwmAmtAMidi];
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

FLASHMEM void updatePWB(uint8_t valuePwBMidi, uint8_t valuePwmAmtBMidi)
{
    float valuePwB = LINEARCENTREZERO[valuePwBMidi];
    float valuePwmAmtB = LINEAR[valuePwmAmtBMidi];
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

FLASHMEM void updateOscLevelA(uint8_t midiValue)
{
    float value = LINEAR[OSCMIXA[midiValue]];

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

FLASHMEM void updateOscLevelB(uint8_t midiValue)
{
    float value = LINEAR[OSCMIXB[midiValue]];
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
FLASHMEM void updateOscMix(uint8_t midiValue) {
    updateOscLevelA(midiValue);
    updateOscLevelB(midiValue);
}
FLASHMEM void updateNoiseLevel(byte midiValue)
{
    float value = LINEARCENTREZERO[midiValue];
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

FLASHMEM void updateFilterFreq(uint8_t midiValue)
{
    float value = FILTERFREQS256[midiValue * 2];
  groupvec[activeGroupIndex]->setCutoff(value);
  showCurrentParameterPage(F("Cutoff"), String(int(value)) + F(" Hz"));
}

FLASHMEM void updateFilterRes(uint8_t midiValue)
{
    float value = FILTERRESONANCE[midiValue];
  groupvec[activeGroupIndex]->setResonance(value);
  showCurrentParameterPage(F("Resonance"), value);
}

FLASHMEM void updateFilterMixer(uint8_t midiValue)
{
    float value = LINEAR_FILTERMIXER[midiValue];
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

FLASHMEM void updateFilterEnv(uint8_t midiValue)
{
    float value = LINEARCENTREZERO[midiValue] * OSCMODMIXERMAX;
  groupvec[activeGroupIndex]->setFilterEnvelope(value);
  showCurrentParameterPage(F("Filter Env."), String(value));
}

FLASHMEM void updatePitchEnv(uint8_t midiValue)
{
    float value = LINEARCENTREZERO[midiValue] * OSCMODMIXERMAX;
    groupvec[activeGroupIndex]->setPitchEnvelope(value);
  showCurrentParameterPage(F("Pitch Env Amt"), String(value));
}

FLASHMEM void updateKeyTracking(float value)
{
  groupvec[activeGroupIndex]->setKeytracking(value);
  showCurrentParameterPage(F("Key Tracking"), String(value));
}

FLASHMEM void updatePitchLFOAmt(uint8_t midiValue)
{
    float value = POWER[midiValue];
  groupvec[activeGroupIndex]->setPitchLfoAmount(value);
  char buf[10];
  showCurrentParameterPage(F("LFO Amount"), dtostrf(value, 4, 3, buf));
}

FLASHMEM void updateModWheel(float value)
{
  groupvec[activeGroupIndex]->setModWhAmount(value);
}

FLASHMEM void updatePitchLFORate(uint8_t midiValue)
{
    float rate = 0.0;
    if (groupvec[activeGroupIndex]->getPitchLfoMidiClockSync())
    {
        // TODO: MIDI Tempo stuff remains global?
        lfoTempoValue = LFOTEMPO[midiValue];
        oscLFOTimeDivStr = LFOTEMPOSTR[midiValue];
        rate = lfoSyncFreq * LFOTEMPO[midiValue];
    }
    else
    {
        rate = LFOMAXRATE * POWER[midiValue];
    }
    groupvec[activeGroupIndex]->setPitchLfoRate(rate);
    showCurrentParameterPage(F("LFO Rate"), String(rate) + F(" Hz"));
}

FLASHMEM void updatePitchLFOWaveform(uint8_t value)
{
  uint32_t waveform = WAVEFORMS_LFO[value];
  groupvec[activeGroupIndex]->setPitchLfoWaveform(waveform);
  showCurrentParameterPage(F("Pitch LFO"), getWaveformStr(waveform));
}

// MIDI CC only
FLASHMEM void updatePitchLFOMidiClkSync(uint8_t midiValue)
{
    bool value = midiValue > 0;
  groupvec[activeGroupIndex]->setPitchLfoMidiClockSync(value);
  showCurrentParameterPage(F("P. LFO Sync"), value ? F("On") : F("Off"));
}

FLASHMEM void updateFilterLfoRate(uint8_t midiValue)
{
    float rate;
    String timeDivStr = "";
    if (groupvec[activeGroupIndex]->getFilterLfoMidiClockSync())
    {
        lfoTempoValue = LFOTEMPO[midiValue];
        rate = lfoSyncFreq * LFOTEMPO[midiValue];
        timeDivStr = LFOTEMPOSTR[midiValue];
        showCurrentParameterPage(F("LFO Time Div"), timeDivStr);
    }
    else
    {
        rate = LFOMAXRATE * POWER[midiValue];
        showCurrentParameterPage(F("F. LFO Rate"), String(rate) + F(" Hz"));
    }

  groupvec[activeGroupIndex]->setFilterLfoRate(rate);

}

FLASHMEM void updateFilterLfoAmt(uint8_t midiValue)
{
    float value = LINEAR[midiValue] * FILTERMODMIXERMAX;
  groupvec[activeGroupIndex]->setFilterLfoAmt(value);
  showCurrentParameterPage(F("F. LFO Amt"), String(value));
}

FLASHMEM void updateFilterLFOWaveform(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setFilterLfoWaveform(WAVEFORMS_LFO[waveform]);
  showCurrentParameterPage(F("Filter LFO"), getWaveformStr(WAVEFORMS_LFO[waveform]));
}

FLASHMEM void updatePitchLFORetrig(uint8_t midiValue)
{
    bool value = midiValue > 0;
  groupvec[activeGroupIndex]->setPitchLfoRetrig(value);
  showCurrentParameterPage(F("P. LFO Retrig"), value ? F("On") : F("Off"));
}

FLASHMEM void updateFilterLFORetrig(uint8_t midiValue)
{
    bool value = midiValue > 0;
  groupvec[activeGroupIndex]->setFilterLfoRetrig(value);
  showCurrentParameterPage(F("F. LFO Retrig"), groupvec[activeGroupIndex]->getFilterLfoRetrig() ? F("On") : F("Off"));
//  digitalWriteFast(RETRIG_LED, groupvec[activeGroupIndex]->getFilterLfoRetrig() ? HIGH : LOW); // LED
}

FLASHMEM void updateFilterLFOMidiClkSync(uint8_t value)
{
  groupvec[activeGroupIndex]->setFilterLfoMidiClockSync(value > 0);
  showCurrentParameterPage(F("Tempo Sync"), value > 0 ? F("On") : F("Off"));
//  digitalWriteFast(TEMPO_LED, value ? HIGH : LOW); // LED
}

FLASHMEM void updateFilterAttack(uint8_t midiValue)
{
    float value = ENVTIMES[midiValue];
  groupvec[activeGroupIndex]->setFilterAttack(value);
  showCurrentParameterPage(F("Filter Attack"), milliToString(value), FILTER_ENV);
}

FLASHMEM void updateFilterDecay(uint8_t midiValue)
{
    float value = ENVTIMES[midiValue];
  groupvec[activeGroupIndex]->setFilterDecay(value);
  showCurrentParameterPage("Filter Decay", milliToString(value), FILTER_ENV);
}

FLASHMEM void updateFilterSustain(uint8_t midiValue)
{
    float value = LINEAR[midiValue];
  groupvec[activeGroupIndex]->setFilterSustain(value);
  showCurrentParameterPage(F("Filter Sustain"), String(value), FILTER_ENV);
}

FLASHMEM void updateFilterRelease(uint8_t midiValue)
{
    float value = ENVTIMES[midiValue];
  groupvec[activeGroupIndex]->setFilterRelease(value);
  showCurrentParameterPage(F("Filter Release"), milliToString(value), FILTER_ENV);
}

FLASHMEM void updateAttack(uint8_t midiValue)
{
    float value = ENVTIMES[midiValue];
  groupvec[activeGroupIndex]->setAmpAttack(value);
  showCurrentParameterPage(F("Attack"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateDecay(uint8_t midiValue)
{
    float value = ENVTIMES[midiValue];
  groupvec[activeGroupIndex]->setAmpDecay(value);
  showCurrentParameterPage(F("Decay"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateSustain(uint8_t midiValue)
{
    float value = LINEAR[midiValue];
  groupvec[activeGroupIndex]->setAmpSustain(value);
  showCurrentParameterPage(F("Sustain"), String(value), AMP_ENV);
}

FLASHMEM void updateRelease(uint8_t midiValue)
{
    float value = ENVTIMES[midiValue];
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

FLASHMEM void updateEffectAmt(uint8_t midiValue)
{
    float value = ENSEMBLE_LFO[midiValue];
  groupvec[activeGroupIndex]->setEffectAmount(value);
  showCurrentParameterPage(F("Effect Amt"), String(value) + F(" Hz"));
}

FLASHMEM void updateEffectMix(uint8_t midiValue)
{
    float value = LINEAR[midiValue];
  groupvec[activeGroupIndex]->setEffectMix(value);
  showCurrentParameterPage(F("Effect Mix"), String(value));
}

FLASHMEM void updatePatch(String name, uint32_t index, int version = 0)
{
  groupvec[activeGroupIndex]->setPatchName(name);
  groupvec[activeGroupIndex]->setPatchIndex(index);
  showPatchPage(String(index), name, version);
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
    updateUnison(value);
    break;

  case CCglide:
    updateGlide(value);
    break;

  case CCpitchenv:
    updatePitchEnv(value);
    break;

  case CCoscwaveformA:
    updateWaveformA(value);
    break;

  case CCoscwaveformB:
    updateWaveformB((uint32_t)clampInto(WAVEFORMS_B, value));
    break;

  case CCpitchA:
    updatePitchA(value);
    break;

  case CCpitchB:
    updatePitchB(PITCH[value]);
    break;

  case CCdetune:
    updateDetune(value);
    break;

  case CCpwmSource:
    updatePWMSource(value > 0 ? PWMSOURCEFENV : PWMSOURCELFO);
    break;

  case CCpwmRate:
    // Uses combination of PWMRate, PWa and PWb
    updatePWMRate(value);
    break;

  case CCpwmAmt:
    // NO FRONT PANEL CONTROL - MIDI CC ONLY
    // Total PWM amount for both oscillators
    updatePWMAmount(LINEAR[value]);
    break;

  case CCpwA:
    updatePWA(value, value);
    break;

  case CCpwB:
    updatePWB(value, value);
    break;

  case CCoscLevelA:
    updateOscLevelA(value);
    break;

  case CCoscLevelB:
    updateOscLevelB(value);
    break;

  case CCoscMix:
    updateOscMix(value);
    break;

  case CCnoiseLevel:
    updateNoiseLevel(value);
    break;

  case CCfilterfreq:
    // MIDI is 7 bit, 128 values and needs to choose alternate filterfreqs(8 bit) by multiplying by 2
    updateFilterFreq(value);
    break;

  case CCfilterres:
    // If <1.1 there is noise at high cutoff freq
    updateFilterRes(value);
    break;

  case CCfiltermixer:
    updateFilterMixer(value);
    break;

  case CCfilterenv:
    updateFilterEnv(value);
    break;

  case CCkeytracking:
    updateKeyTracking(KEYTRACKINGAMT[value]);
    break;

  case CCmodwheel:
    // Variable LFO amount from mod wheel - Settings Option
    updateModWheel(POWER[value] * modWheelDepth);
    break;

  case CCosclfoamt:
    updatePitchLFOAmt(value);
    break;

  case CCoscLfoRate:
    updatePitchLFORate(value);
    break;

  case CCoscLfoWaveform:
    updatePitchLFOWaveform(value);
    break;

  case CCosclforetrig:
    updatePitchLFORetrig(value);
    break;

  case CCfilterLFOMidiClkSync:
    updateFilterLFOMidiClkSync(value);
    break;

  case CCfilterlforate:
    updateFilterLfoRate(value);
    break;

  case CCfilterlfoamt:
    updateFilterLfoAmt(value);
    break;

  case CCfilterlfowaveform:
    updateFilterLFOWaveform(value);
    break;

  case CCfilterlforetrig:
    updateFilterLFORetrig(value);
    break;

  // MIDI Only
  case CCoscLFOMidiClkSync:
    updatePitchLFOMidiClkSync(value);
    break;

  case CCfilterattack:
    updateFilterAttack(value);
    break;

  case CCfilterdecay:
    updateFilterDecay(value);
    break;

  case CCfiltersustain:
    updateFilterSustain(value);
    break;

  case CCfilterrelease:
    updateFilterRelease(value);
    break;

  case CCampattack:
    updateAttack(value);
    break;

  case CCampdecay:
    updateDecay(value);
    break;

  case CCampsustain:
    updateSustain(value);
    break;

  case CCamprelease:
    updateRelease(value);
    break;

  case CCoscfx:
    updateOscFX(inRangeOrDefault<int>(value, 2, 0, 2));
    break;

  case CCfxamt:

    updateEffectAmt(value);
    break;

  case CCfxmix:

    updateEffectMix(value);
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
FLASHMEM String getPatchData(PatchMidiData data)
{
    return patchName + F(",") +
    String(data.oscMix) + F(",") +
    /*String(data.oscLevelB) +*/ F(",") +
    String(data.noiseLevel) + F(",") +
    String(data.unison) + F(",") +
    String(data.oscFX) + F(",") +
    String(data.detune, 5) + F(",") +
    String(lfoSyncFreq) + F(",") +
    String(midiClkTimeInterval) + F(",") +
    String(lfoTempoValue) + F(",") +
    String(data.keyTracking) + F(",") +
    String(data.glide, 5) + F(",") +
    String(data.pitchA) + F(",") +
    String(data.pitchB) + F(",") +
    String(data.waveformA) + F(",") +
    String(data.waveformB) + F(",") +

    String(data.pWMSource) + F(",") +
    /*String(data.pwmAmtA) +*/ F(",") +
    String(data.pwmAmtB) + F(",") +
    String(data.pWMRate) + F(",") +
    String(data.pWA) + F(",") +
    String(data.pWB) + F(",") +
    String(data.filterRes) + F(",") +
    String(data.filterFreq) + F(",") +
    String(data.filterMixer) + F(",") +
    String(data.filterEnv) + F(",") +
    String(data.pitchLFOAmt, 5) + F(",") +
    String(data.pitchLFORate, 5) + F(",") +
    String(data.pitchLFOWaveform) + F(",") +
    String(int(data.pitchLFORetrig)) + F(",") +
    String(int(data.pitchLFOMidiClkSync)) + F(",") +
    String(data.filterLfoRate, 5) + F(",") +
    data.filterLFORetrig + F(",") +
    data.filterLFOMidiClkSync + F(",") +
    data.filterLfoAmt + F(",") +
    data.filterLFOWaveform + F(",") +
    data.filterAttack + F(",") +
    data.filterDecay + F(",") +
    data.filterSustain + F(",") +
    data.filterRelease + F(",") +
    data.attack + F(",") +
    data.decay + F(",") +
    data.sustain + F(",") +
    data.release + F(",") +

    String(data.effectAmt) + F(",") +
    String(data.effectMix) + F(",") +
    String(data.pitchEnv) + F(",") +
    String(velocitySens) + F(",") +
    /*String(data.chordDetune) +*/ F(",") +
    String(data.monophonic) + F(",") +
    String(0.0f) + F(",") +
    String(0.0f);
}

FLASHMEM void loadPatchMidiData(PatchMidiData data)
{
    updatePatch(data.name, patchNo);
    updateOscMix(data.oscMix);
//    updateOscLevelB(data.oscLevelB);
    updateNoiseLevel(data.noiseLevel);
    updateUnison(data.unison);
    updateOscFX(data.oscFX);
    updateDetune(data.detune);
    lfoSyncFreq =data.lfoSyncFreq;
    midiClkTimeInterval =data.midiClkTimeInterval;
    lfoTempoValue =data.lfoTempoValue;
    updateKeyTracking(data.keyTracking);
    updateGlide(data.glide);
    updatePitchA(data.pitchA);
    updatePitchB(data.pitchB);
    updateWaveformA(data.waveformA);
    updateWaveformB(data.waveformB);
    updatePWMSource(data.pWMSource);
    updatePWA(data.pWA, data.pWA);
    updatePWB(data.pWB, data.pWB);
    updatePWMRate(data.pWMRate);
    updateFilterRes(data.filterRes);
    updateFilterFreq(data.filterFreq);
    updateFilterMixer(data.filterMixer);
    updateFilterEnv(data.filterEnv);
    updatePitchLFOAmt(data.pitchLFOAmt);
    updatePitchLFORate(data.pitchLFORate);
    updatePitchLFOWaveform(data.pitchLFOWaveform);
    updatePitchLFORetrig(data.pitchLFORetrig);
    updatePitchLFOMidiClkSync(data.pitchLFOMidiClkSync); // MIDI CC Only
    updateFilterLfoRate(data.filterLfoRate);
    updateFilterLFORetrig(data.filterLFORetrig);
    updateFilterLFOMidiClkSync(data.filterLFOMidiClkSync);
    updateFilterLfoAmt(data.filterLfoAmt);
    updateFilterLFOWaveform(data.filterLFOWaveform);
    updateFilterAttack(data.filterAttack);
    updateFilterDecay(data.filterDecay);
    updateFilterSustain(data.filterSustain);
    updateFilterRelease(data.filterRelease);
    updateAttack(data.attack);
    updateDecay(data.decay);
    updateSustain(data.sustain);
    updateRelease(data.release);
    updateEffectAmt(data.effectAmt);
    updateEffectMix(data.effectMix);
    updatePitchEnv(data.pitchEnv);
    velocitySens =data.velocitySens;
    groupvec[activeGroupIndex]->setMonophonic(data.monophonic);
    //  SPARE1 = data[50].toFloat();
    //  SPARE2 = data[51].toFloat();

    Serial.print(F("Set Patch: "));
    Serial.println(data.name);
}

uint8_t fromMix(float mixA, float mixB)
{
    size_t closestIndex = 0;
    float minError = 50;
    for (size_t i = 0; i < 128; i++) {
        float la = LINEAR[OSCMIXA[i]] - mixA;
        float lb = LINEAR[OSCMIXB[i]] - mixB;
        float error = la*la + lb*lb;

        if (error < minError) {
            closestIndex = i;
            minError = error;
        }
    }
    return closestIndex;
}

FLASHMEM void setCurrentPatchData(String data[])
{
    //    dbgMsg = String(data[6].toFloat()) + String(' ')+ patchMidiData.detune+ String(' ') + data[48].toFloat();
    updatePatch(data[0], patchNo, data[50].toInt());
    updateOscMix(patchMidiData.oscMix = fromMix(data[1].toFloat(), data[2].toFloat()));

    updateNoiseLevel(closest(LINEARCENTREZERO,  data[3].toFloat(), &patchMidiData.noiseLevel));
    updateUnison(data[4].toInt());
    updateOscFX(patchMidiData.oscFX = (uint8_t)data[5].toInt());
    // 1.0f - (MAXDETUNE * POWER[value]), value
    float detunePower = (float)((1.0 - data[6].toFloat()) / MAXDETUNE);
    // TODO if unison mode == 2, use patchMidiData.chordDetune
    updateDetune(closest(POWER, detunePower, &patchMidiData.detune));
    // Why is this MIDI Clock stuff part of the patch??
    lfoSyncFreq = data[7].toInt();
    midiClkTimeInterval = data[8].toInt();
    lfoTempoValue = data[9].toFloat();
    updateKeyTracking(data[10].toFloat());
    updateGlide(closest(POWER, data[11].toFloat(), &patchMidiData.glide));

    updatePitchA(closest(PITCH, (int8_t)data[12].toInt(), &patchMidiData.pitchA));
    updatePitchB(closest(PITCH, (int8_t)data[13].toInt(), &patchMidiData.pitchB));
    updateWaveformA(closest(WAVEFORMS_A, (uint8_t)data[14].toInt(), &patchMidiData.waveformA));
    updateWaveformB(closest(WAVEFORMS_B,(uint8_t)data[15].toInt(), &patchMidiData.waveformB));
    updatePWMSource(data[16].toInt());
    updatePWMRate(closest(PWMRATE, data[19].toFloat(), &patchMidiData.pWMRate));
    auto pwA = patchMidiData.pWMRate == PWMRATE_PW_MODE
               ? closest(LINEARCENTREZERO,  data[20].toFloat(), &patchMidiData.pWA)
               : closest(LINEAR, data[17].toFloat(), &patchMidiData.pWA);
    updatePWA(pwA, pwA);
    updatePWB(closest(LINEARCENTREZERO,  data[21].toFloat(), &patchMidiData.pWB),
              closest(LINEAR, data[18].toFloat(), &patchMidiData.pwmAmtB));
    updateFilterRes(closest(FILTERRESONANCE, data[22].toFloat(), &patchMidiData.filterRes));
    updateFilterFreq(patchMidiData.filterFreq = closest(FILTERFREQS256, (uint16_t)data[23].toFloat(), nullptr) / 2);
    updateFilterMixer(closest(LINEAR_FILTERMIXER, data[24].toFloat(),&patchMidiData.filterMixer));
    updateFilterEnv(closest(LINEARCENTREZERO, data[25].toFloat()/ OSCMODMIXERMAX, &patchMidiData.filterEnv));
    updatePitchLFOAmt(closest(POWER, data[26].toFloat(), &patchMidiData.pitchLFOAmt));


    updatePitchLFOMidiClkSync(patchMidiData.pitchLFOMidiClkSync = data[30].toInt()); // MIDI CC Only
    float rate = data[27].toFloat();
    if (patchMidiData.pitchLFOMidiClkSync)
        updatePitchLFORate(closest(LFOTEMPO, rate / lfoSyncFreq,&patchMidiData.pitchLFORate));
    else
        updatePitchLFORate(closest(POWER, rate / LFOMAXRATE, &patchMidiData.pitchLFORate));

    updatePitchLFOWaveform(closest(WAVEFORMS_LFO, (uint8_t)data[28].toInt(), &patchMidiData.pitchLFOWaveform));
    dbgMsg = String(data[28].toInt()) + String(' ') + patchMidiData.pitchLFOWaveform;
    updatePitchLFORetrig(data[29].toInt());

    updateFilterLFOMidiClkSync(patchMidiData.filterLFOMidiClkSync = data[33].toInt());
    rate = data[31].toFloat();
    if (patchMidiData.filterLFOMidiClkSync)
        updateFilterLfoRate(closest(LFOTEMPO, rate / lfoSyncFreq,&patchMidiData.filterLfoRate));
    else
        updateFilterLfoRate(closest(POWER, rate / LFOMAXRATE, &patchMidiData.filterLfoRate));
    updateFilterLFORetrig(data[32].toInt());
    updateFilterLfoAmt(closest(LINEAR, data[34].toFloat(), &patchMidiData.filterLfoAmt));
    updateFilterLFOWaveform(closest(WAVEFORMS_LFO, (uint8_t)data[35].toFloat(), &patchMidiData.filterLFOWaveform));

   updateFilterAttack(closest(ENVTIMES,(uint16_t)data[36].toFloat(), &patchMidiData.filterAttack));
    updateFilterDecay(closest(ENVTIMES,(uint16_t)data[37].toFloat(), &patchMidiData.filterDecay));
    updateFilterSustain(closest(LINEAR,data[38].toFloat(), &patchMidiData.filterSustain));
    updateFilterRelease(closest(ENVTIMES,(uint16_t)data[39].toFloat(), &patchMidiData.filterRelease));

    updateAttack(closest(ENVTIMES,(uint16_t)data[40].toFloat(), &patchMidiData.attack));
    updateDecay(closest(ENVTIMES,(uint16_t)data[41].toFloat(), &patchMidiData.decay));
    updateSustain(closest(LINEAR,data[42].toFloat(), &patchMidiData.sustain));
    updateRelease(closest(ENVTIMES,(uint16_t)data[43].toFloat(), &patchMidiData.release));

    updateEffectAmt(closest(ENSEMBLE_LFO, data[44].toFloat(), &patchMidiData.effectAmt));
    updateEffectMix(closest(LINEAR, data[45].toFloat(), &patchMidiData.effectMix));
    updatePitchEnv(closest(LINEARCENTREZERO, data[46].toFloat() / OSCMODMIXERMAX, &patchMidiData.pitchEnv));
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

void sendSysex(String ss) {
    usbMIDI.sendSysEx(ss.length(), reinterpret_cast<const uint8_t *>(ss.c_str()), false);
}

byte cycleMidiIn(byte cc, byte& b, int delta, size_t N, bool clamp = false) {
    int v = (int)b + delta;
    if(clamp) {
        v = v < 0 ? 0 : v >= (long)N ? (long)(N-1) : v;
    } else {
        while (v < 0) v += N;
        while (v >= (long) N) v -= N;
    }
    b = (byte)v;

    midiCCOut(cc, b);
    myControlChange(midiChannel, cc, b);
    return b;
}

template<typename T, size_t  N>
byte cycleMidiIn(byte cc, byte& b, int delta, const T(&array)[N], bool clamp = false) {
    return cycleMidiIn(cc, b, delta, N, clamp);
}

void updateSection(byte encIndex, bool moveUp) {
    int sign = moveUp ? 1 : -1;
    int delta = sign * (sectionSwitch.pressed() ? (dbgMode != 0 ? 0 : 10) : 1);
    switch(section) {
        case Section::Osc1:
            switch(encIndex) {
                case 0: cycleMidiIn(CCpitchA, patchMidiData.pitchA, delta, PITCH); return;
                case 1: cycleMidiIn(CCoscwaveformA, patchMidiData.waveformA, delta, WAVEFORMS_A); return;
                case 2: cycleMidiIn(CCpwA, patchMidiData.pWA, delta, LINEARCENTREZERO); return;
                case 3: /*OSC MIX*/ cycleMidiIn(CCoscMix, patchMidiData.oscMix, delta, 128, true); return;
            }
            break;
        case Section::Osc2:
            switch(encIndex) {
                case 0: cycleMidiIn(CCpitchB, patchMidiData.pitchB, delta, PITCH); return;
                case 1:cycleMidiIn(CCoscwaveformB, patchMidiData.waveformB, delta, WAVEFORMS_B); return;
                case 2: cycleMidiIn(CCpwB, patchMidiData.pWB, delta, LINEARCENTREZERO); return;
                case 3: /*detune*/ cycleMidiIn(CCdetune, patchMidiData.detune, delta, POWER); return;
            }
            break;
        case Section::Noise:
            // "Noise", "Env", "PWM Rate", "Osc FX"
            switch(encIndex) {
                case 0:cycleMidiIn(CCnoiseLevel, patchMidiData.noiseLevel, delta, 128, true);return;
                case 1:cycleMidiIn(CCpitchenv, patchMidiData.pitchEnv, delta, 128, true);return;
                case 2: cycleMidiIn(CCpwmRate, patchMidiData.pWMRate, delta, PWMRATE, true);return;
                case 3: cycleMidiIn(CCoscfx, patchMidiData.oscFX, sign, 3);return;
            }
            break;
        case Section::LFO:
            // "Level", "Waveform", "Rate", "Unison"
            switch(encIndex) {
                case 0:cycleMidiIn(CCosclfoamt, patchMidiData.pitchLFOAmt, delta, POWER, true);return;
                case 1:cycleMidiIn(CCoscLfoWaveform, patchMidiData.pitchLFOWaveform, sign, WAVEFORMS_LFO, true);return;
                case 2: cycleMidiIn(CCoscLfoRate, patchMidiData.pitchLFORate, delta, 128, true);return;
                case 3:
                    if(moveUp) // tempo
                        cycleMidiIn(CCunison, patchMidiData.unison, sign, 3);
                    else // retrig
                        cycleMidiIn(CCosclforetrig, patchMidiData.pitchLFORetrig, 1, 2);
                    return;
            }
            break;
        case Section::FilterEnvelope:
            // "ATK", "DECAY", "SUSTN", "REL"
            switch(encIndex) {
                case 0:cycleMidiIn(CCfilterattack, patchMidiData.filterAttack, delta, ENVTIMES, true);return;
                case 1:cycleMidiIn(CCfilterdecay, patchMidiData.filterDecay, delta, ENVTIMES, true);return;
                case 2: cycleMidiIn(CCfiltersustain, patchMidiData.filterSustain, delta, LINEAR, true);return;
                case 3: cycleMidiIn(CCfilterrelease, patchMidiData.filterRelease, delta, ENVTIMES, true);return;
            }
            break;
        case Section::Filter:
            // "Cutoff", "Resonance", "Type", "Env"
            switch(encIndex) {
                case 0:cycleMidiIn(CCfilterfreq, patchMidiData.filterFreq, delta, 128);return;
                case 1:cycleMidiIn(CCfilterres, patchMidiData.filterRes, delta, FILTERRESONANCE);return;
                case 2: cycleMidiIn(CCfiltermixer, patchMidiData.filterMixer, delta, LINEAR_FILTERMIXER);return;
                case 3: cycleMidiIn(CCfilterenv, patchMidiData.filterEnv, delta, 128, true);return;
            }
            break;
        case Section::FilterLFO:
            // "Level", "Waveform", "Rate", "Retrig/Tempo"
            switch(encIndex) {
                case 0:cycleMidiIn(CCfilterlfoamt, patchMidiData.filterLfoAmt, delta, LINEAR, true);return;
                case 1:cycleMidiIn(CCfilterlfowaveform, patchMidiData.filterLFOWaveform, sign, WAVEFORMS_LFO, true);return;
                case 2: cycleMidiIn(CCfilterlforate, patchMidiData.filterLfoRate, delta, 128, true);return;
                case 3: {
                    if(moveUp) // tempo
                        cycleMidiIn(CCfilterLFOMidiClkSync, patchMidiData.filterLFOMidiClkSync, 1, 2);
                     else // retrig
                        cycleMidiIn(CCfilterlforetrig, patchMidiData.filterLFORetrig, 1, 2);
                    return;
                }
            }
            break;
        case Section::Amp:
            // "ATK", "DECAY", "SUSTN", "REL"
            switch(encIndex) {
                case 0: cycleMidiIn(CCampattack, patchMidiData.attack, delta, ENVTIMES, true);return;
                case 1: cycleMidiIn(CCampdecay, patchMidiData.decay, delta, ENVTIMES, true);return;
                case 2:  cycleMidiIn(CCampsustain, patchMidiData.sustain, delta, LINEAR, true);return;
                case 3:  cycleMidiIn(CCamprelease, patchMidiData.release, delta, ENVTIMES, true);return;
            }
            break;
        case Section::FX:
            // "Glide", "FX Amt", "FX Mix", "4"
            switch(encIndex) {
                case 0:cycleMidiIn(CCglide, patchMidiData.glide, delta, POWER, true);return;
                case 1:cycleMidiIn(CCfxamt, patchMidiData.effectAmt, delta, ENSEMBLE_LFO, true);return;
                case 2: cycleMidiIn(CCfxmix, patchMidiData.effectMix, delta, LINEAR, true);return;
                case 3:  /*unused*/ return;
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
