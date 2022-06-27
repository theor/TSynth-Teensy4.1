// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include "TButton.h"
#include "Constants.h"
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();


//Teensy 4.1 - Mux Pins
#define MUX_0 36
#define MUX_1 35
#define MUX_2 33
#define MUX_3 38
//#define MUX_3 36
//#define MUX_2 35
//#define MUX_1 33
//#define MUX_0 34
#define MUX1_S 34
/*
 * recall on sig
 * back on s0
 * save s1
 * settings s3
 * section s2
 * */

//Teensy 4.1 Pins
//#define OSC_FX_SW 33
//#define FILTER_LFO_RETRIG_SW 39
//#define UNISON_SW 36
//#define TEMPO_SW 16

#define MUX1_RECALL_SW 4
#define MUX1_ENCODER1_SW 0
#define MUX1_ENCODER2_SW 1
#define MUX1_ENCODER3_SW 2
#define MUX1_ENCODER4_SW 3
#define MUX1_SAVE_SW 6
#define MUX1_SETTINGS_SW 5
#define MUX1_BACK_SW 7
#define MUX1_SECTION_1 8
#define MUX1_SECTION_2 9
#define MUX1_SECTION_3 10
#define MUX1_SECTION_4 11
#define MUX1_SECTION_5 12
#define MUX1_SECTION_6 13
#define MUX1_SECTION_7 14
#define MUX1_SECTION_8 15
#define VOLUME_POT A10

#define ENCODER_PINA 4
#define ENCODER_PINB 5
#define ENCODER4_PINB 17
#define ENCODER4_PINA 16
#define ENCODER3_PINB 25
#define ENCODER3_PINA 28
#define ENCODER2_PINB 29
#define ENCODER2_PINA 30
#define ENCODER1_PINB 31
#define ENCODER1_PINA 32

#define ENCODER1_LED 37
#define ENCODER2_LED 39
#define ENCODER3_LED 40
#define ENCODER4_LED 41

//#define RETRIG_LED 34
//#define TEMPO_LED 35
//#define UNISON_LED 37
//#define OSC_FX_LED 14

#define BACKLIGHT 6

#define MUXCHANNELS 16
#define QUANTISE_FACTOR 15// Sets a tolerance of noise on the ADC. 15 is 4 bits
#define QUANTISE_FACTOR_VOL 511// Sets a tolerance of noise on the ADC. 15 is 4 bits

#define DEBOUNCE 30

static byte muxInput = 0;
//static uint16_t mux1ValuesPrev[MUXCHANNELS] = {};
//static uint16_t mux2ValuesPrev[MUXCHANNELS] = {};

//static uint16_t mux1Read = 0;
//static uint16_t mux2Read = 0;
static int volumeRead = 0;
static int volumePrevious = 0;

TButton muxedButtons[16] = {
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION),
        TButton(MUX1_S, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION)
};
//#define SECTION_SW 37
//TButton sectionSwitch{SECTION_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};

//These are pushbuttons and require debouncing
//TButton oscFXSwitch{OSC_FX_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
//TButton filterLFORetrigSwitch{FILTER_LFO_RETRIG_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
//TButton unisonSwitch{UNISON_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
//TButton tempoSwitch{TEMPO_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
//TButton recallButton{RECALL_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION}; //On encoder
//
//TButton saveButton{SAVE_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
//TButton settingsButton{SETTINGS_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
//TButton backButton{BACK_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION};
TEncoder encoder(ENCODER_PINB, ENCODER_PINA);//This often needs the pins swapping depending on the encoder
TEncoder sectionEncoders[4] = {
        TEncoder(ENCODER1_PINB, ENCODER1_PINA),
        TEncoder(ENCODER2_PINB, ENCODER2_PINA),
        TEncoder(ENCODER3_PINB, ENCODER3_PINA),
        TEncoder(ENCODER4_PINB, ENCODER4_PINA),
};

FLASHMEM void setupHardware() {
  //Volume Pot is on ADC0
  adc->adc0->setAveraging(16); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(12); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed

  //MUXs on ADC1
//  adc->adc1->setAveraging(32); // set number of averages 0, 4, 8, 16 or 32.
//  adc->adc1->setResolution(12); // set bits of resolution  8, 10, 12 or 16 bits.
//  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
//  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed

  //Mux address pins
  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  //Mux ADC
  pinMode(MUX1_S, INPUT_PULLUP);
//  pinMode(MUX2_S, INPUT);

  //Volume ADC
  pinMode(VOLUME_POT, INPUT);

  //Switches
//  pinMode(OSC_FX_SW, INPUT_PULLUP);
//  pinMode(FILTER_LFO_RETRIG_SW, INPUT_PULLUP);
//  pinMode(UNISON_SW, INPUT_PULLUP);
//  pinMode(TEMPO_SW, INPUT_PULLUP);
//  pinMode(RECALL_SW, INPUT_PULLUP); //On encoder
//  pinMode(SAVE_SW, INPUT_PULLUP);
//  pinMode(SETTINGS_SW, INPUT_PULLUP);
//  pinMode(BACK_SW, INPUT_PULLUP);

  //LEDs
  pinMode(ENCODER1_LED, OUTPUT);
  pinMode(ENCODER2_LED, OUTPUT);
  pinMode(ENCODER3_LED, OUTPUT);
  pinMode(ENCODER4_LED, OUTPUT);

  //Display backlight - Can be use to turn off or dim using PWM
  //pinMode(BACKLIGHT, OUTPUT);
}
