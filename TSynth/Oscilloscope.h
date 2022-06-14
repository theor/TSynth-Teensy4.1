#ifndef Oscilloscope_h_
#define Oscilloscope_h_
#include "AudioStream.h"
#include "ST7735_t3.h"
#include "Parameters.h"

uint8_t bufferBlock = 0;
uint8_t bufcount = 0;
uint8_t pixel_x = 0;
int16_t pixel_y = 0;
int16_t prev_pixel_y = 0;

class Oscilloscope : public AudioStream {
  public:
    Oscilloscope(void) : AudioStream(1, inputQueueArray) {
    }
    virtual void update(void);
    void ScreenSetup(ST7735_t3*);
    void Display(void);
    void AddtoBuffer(int16_t*);

  private:
    audio_block_t *inputQueueArray[1];
    ST7735_t3 *display;
    int16_t buffer[AUDIO_BLOCK_SAMPLES];

};
#endif

void Oscilloscope::ScreenSetup(ST7735_t3 *screen) {
  display = screen;
}

void Oscilloscope::Display() {
  pixel_x = 0;
  const uint8_t Ymed = 40;
  const uint8_t Ydelta = 20;
  const uint8_t minY = Ymed-Ydelta;
  const uint8_t maxY = Ymed + Ydelta;
  prev_pixel_y = map(buffer[0], 32767, -32768, -120, 120) + Ymed;
  if (prev_pixel_y < minY) prev_pixel_y = minY;
  if (prev_pixel_y > maxY)prev_pixel_y = maxY;

  for (uint8_t i = 0; i < AUDIO_BLOCK_SAMPLES - 1; i++) {
    pixel_y = map(buffer[i], 32767, -32768, -120, 120) + Ymed;
    if (pixel_y < minY) pixel_y = minY;
    if (pixel_y > maxY)pixel_y = maxY;

    // don't draw segments overlapping the red separator line
    if(prev_pixel_y != Ymed || pixel_y != Ymed)
        display->drawLine(pixel_x + 15, prev_pixel_y, pixel_x + 16, pixel_y, 0x07B0);
    prev_pixel_y = pixel_y;
    pixel_x++;
  }
}

void Oscilloscope::AddtoBuffer(int16_t *audio) {
  audio++;
  if (bufferBlock == 0) {
    if (*(audio - 1) > -16 && *(audio + 3) < 16) {
      bufferBlock = 1;
      bufcount = 0;
    }
  }
  else {
    for (uint16_t i = 0; i < 32; i++) {
      buffer[bufcount++] = *audio;
      audio += 4;
    }
    bufferBlock++;
    if (bufferBlock >= 5) {
      bufferBlock = 0;
    }
  }
}

void Oscilloscope::update(void) {
  if (!display) return;
  audio_block_t *block;
  block = receiveReadOnly(0);
  if (block) {
    AddtoBuffer(block->data);
    release(block);
    if (bufferBlock == 0) {
      Display();
    }
  }
}
