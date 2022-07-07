#include "Detune.h"
#include "Arduino.h"

const float DETUNE[4][24] PROGMEM = {
  {0.0f, 0.04f, 0.09f, 0.13f, 0.17f, 0.22f, 0.26f, 0.30f, 0.35f, 0.39f, 0.43f, 0.49f, 0.52f, 0.57f, 0.61f, 0.65f, 0.70f, 0.74f, 0.78f, 0.83f, 0.87f, 0.91f, 0.96f, 1.0f},
  {0.0f, 0.09f, 0.18f, 0.27f, 0.36f, 0.45f, 0.55f, 0.63f, 0.73f, 0.82f, 0.91f, 1.0f, 0.0f, 0.09f, 0.18f, 0.27f, 0.36f, 0.45f, 0.55f, 0.63f, 0.73f, 0.82f, 0.91f, 1.0f},
  {0.0f, 0.14f, 0.29f, 0.43, 0.57f, 0.71f, 0.86f, 1.0f, 0.0f, 0.14f, 0.29f, 0.43, 0.57f, 0.71f, 0.86f, 1.0f, 0.0f, 0.14f, 0.29f, 0.43, 0.57f, 0.71f, 0.86f, 1.0f},
  {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f}
};

//        const char* CDT_STR[128] = {"Major", "Major", "Major", "Major", "Major", "Major", "Major", "Minor", "Minor", "Minor", "Minor", "Minor", "Minor", "Minor", "Diminished", "Diminished", "Diminished", "Diminished", "Diminished", "Diminished", "Diminished", "Augmented", "Augmented", "Augmented", "Augmented", "Augmented", "Augmented", "Augmented", "Sus 2nd", "Sus 2nd", "Sus 2nd", "Sus 2nd", "Sus 2nd", "Sus 2nd", "Sus 2nd", "Sus 4th", "Sus 4th", "Sus 4th", "Sus 4th", "Sus 4th", "Sus 4th", "Sus 4th", "7th Sus 2nd", "7th Sus 2nd", "7th Sus 2nd", "7th Sus 2nd", "7th Sus 2nd", "7th Sus 2nd", "7th Sus 2nd", "7th Sus 4th", "7th Sus 4th", "7th Sus 4th", "7th Sus 4th", "7th Sus 4th", "7th Sus 4th", "7th Sus 4th", "6th", "6th", "6th", "6th", "6th", "6th", "6th", "7th", "7th", "7th", "7th", "7th", "7th", "7th", "9th", "9th", "9th", "9th", "9th", "9th", "9th", "Major 7th", "Major 7th", "Major 7th", "Major 7th", "Major 7th", "Major 7th", "Major 7th", "Major 9th", "Major 9th", "Major 9th", "Major 9th", "Major 9th", "Major 9th", "Major 9th", "Major 11th", "Major 11th", "Major 11th", "Major 11th", "Major 11th", "Major 11th", "Major 11th", "Minor 6th", "Minor 6th", "Minor 6th", "Minor 6th", "Minor 6th", "Minor 6th", "Minor 6th", "Minor 7th", "Minor 7th", "Minor 7th", "Minor 7th", "Minor 7th", "Minor 7th", "Minor 7th", "Minor 9th", "Minor 9th", "Minor 9th", "Minor 9th", "Minor 9th", "Minor 9th", "Minor 9th", "Minor 11th", "Minor 11th", "Minor 11th", "Minor 11th", "Minor 11th", "Minor 11th", "Minor 11th", "All 12", "All 12"};
const char* CDT_STR[19] = {"Major",  "Minor",  "Diminished", "Augmented", "Sus 2nd", "Sus 4th", "7th Sus 2nd",  "7th Sus 4th", "6th", "7th", "9th",  "Major 7th", "Major 9th", "Major 11th", "Minor 6th", "Minor 7th", "Minor 9th", "Minor 11th",  "All 12"};

const uint8_t CHORD_DETUNE[12][19] PROGMEM = {
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        { 4, 3, 3, 4, 2, 5, 2, 5, 4, 4, 4, 4, 4, 4, 3, 3, 2, 3, 1},
        { 7, 7, 6, 8, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 2},
        { 12, 12, 0, 0, 0, 0, 10, 10, 9, 10, 10, 11, 11, 11, 9, 10, 7, 10, 3},
        { 0, 0, 3, 4, 2, 5, 0, 0, 0, 0, 14, 0, 14, 14, 0, 0, 10, 14, 4},
        { 4, 3, 6, 8, 5, 7, 2, 5, 4, 4, 0, 4, 0, 17, 3, 3, 0, 17, 5},
        { 7, 7, 0, 0, 0, 0, 7, 7, 7, 7, 4, 7, 4, 0, 7, 7, 2, 0, 6},
        { 12, 12, 3, 4, 2, 5, 10, 10, 9, 10, 7, 11, 7, 4, 9, 10, 3, 3, 7},
        { 0, 0, 6, 8, 5, 7, 0, 0, 0, 0, 10, 0, 11, 7, 0, 0, 7, 7, 8},
        { 4, 3, 0, 0, 0, 0, 2, 5, 4, 4, 14, 4, 14, 11, 3, 3, 10, 10, 9},
        { 7, 7, 3, 4, 2, 5, 7, 7, 7, 7, 0, 7, 0, 14, 7, 7, 0, 14, 10},
        { 12, 12, 6, 8, 5, 7, 10, 10, 9, 10, 0, 11, 0, 17, 9, 10, 0, 17, 11},
};
