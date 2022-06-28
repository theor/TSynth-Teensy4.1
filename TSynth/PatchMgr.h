/*
  TSynth patch saving and recall works like an analogue polysynth from the late 70s (Prophet 5).
  When you recall a patch, all the front panel controls will be different values from those saved in the patch. 
  Moving them will cause a jump to the current value.
*/
//Agileware CircularBuffer available in libraries manager
#include <CircularBuffer.h>
#include "Constants.h"


struct PatchMidiData {
    String name; // 0
    uint8_t oscLevelA; // 1
    uint8_t oscLevelB; // 2
    uint8_t noiseLevel; // 3
    uint8_t unison; // 4
    uint8_t oscFX; // 5
    uint8_t detune; // 6
    uint8_t lfoSyncFreq; // 7
    uint8_t midiClkTimeInterval; // 8
    uint8_t lfoTempoValue; // 9
    uint8_t keyTracking; // 10
    uint8_t glide; // 11
    uint8_t pitchA; // 12
    uint8_t pitchB; // 13
    uint8_t waveformA; // 14
    uint8_t waveformB; // 15
    uint8_t pWMSource; // 16
    uint8_t pWA; // 17
    uint8_t pWB; // 18
    uint8_t pWMRate; // 19
    uint8_t pwmAmtA; // 20
    uint8_t pwmAmtB; // 21
    uint8_t filterRes; // 22
    uint8_t filterFreq; // 23
    uint8_t filterMixer; // 24
    uint8_t filterEnv; // 25
    uint8_t pitchLFOAmt; // 26
    uint8_t pitchLFORate; // 27
    uint8_t pitchLFOWaveform; // 28
    uint8_t pitchLFORetrig; // 29
    uint8_t pitchLFOMidiClkSync; // 30
    uint8_t filterLfoRate; // 31
    uint8_t filterLFORetrig; // 32
    uint8_t filterLFOMidiClkSync; // 33
    uint8_t filterLfoAmt; // 34
    uint8_t filterLFOWaveform; // 35
    uint8_t filterAttack; // 36
    uint8_t filterDecay; // 37
    uint8_t filterSustain; // 38
    uint8_t filterRelease; // 39
    uint8_t attack; // 40
    uint8_t decay; // 41
    uint8_t sustain; // 42
    uint8_t release; // 43
    uint8_t effectAmt; // 44
    uint8_t effectMix; // 45
    uint8_t pitchEnv; // 46
    uint8_t velocitySens; // 47
    uint8_t chordDetune; // 48
    uint8_t monophonic; //49
    uint8_t spare1; // 50
    uint8_t spare2; // 51
};

#define TOTALCHARS 64

const static char CHARACTERS[TOTALCHARS] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', ' ', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
int charIndex = 0;
char currentCharacter = 0;
String renamedPatch = "";

struct PatchNoAndName{
  int patchNo;
  String patchName;
  bool newFormat;
};

CircularBuffer<PatchNoAndName, PATCHES_LIMIT> patches;
PatchMidiData patchMidiData = {};

FLASHMEM size_t readField(File *file, char *str, size_t size, const char *delim){
  char ch;
  size_t n = 0;
  while ((n + 1) < size && file->read(&ch, 1) == 1)
  {
    // Delete CR.
    if (ch == '\r')
    {
      continue;
    }
    str[n++] = ch;
    if (strchr(delim, ch))
    {
      break;
    }
  }
  str[n] = '\0';
  return n;
}
FLASHMEM void recallPatchMidiData(File patchFile, PatchMidiData* data){
    //Read patch data from file and set current patch parameters
    size_t n;     // Length of returned field with delimiter.
    char str[20]; // Must hold longest field with delimiter and zero byte.
    uint32_t i = 0;
    while (patchFile.available() && i < NO_OF_PARAMS)
    {
        n = readField(&patchFile, str, sizeof(str), ",\n");
        // done if Error or at EOF.
        if (n == 0)
            break;
        // Print the type of delimiter.
        if (str[n - 1] == ',' || str[n - 1] == '\n')
        {
            // Remove the delimiter.
            str[n - 1] = 0;
        }
        else
        {
            // At eof, too long, or read error.  Too long is error.
            Serial.print(patchFile.available() ? F("error: ") : F("eof:   "));
        }
        // Print the field.
        //    Serial.print(i);
        //    Serial.print(" - ");
        //    Serial.println(str);

        if(i == 0)
            data->name = String(str);
        else {
            uint8_t value = (uint8_t) (String(str).toInt());

            switch (i) {
                case 1: data-> oscLevelA = value; break;
                case 2: data-> oscLevelB = value; break;
                case 3: data-> noiseLevel = value; break;
                case 4: data-> unison = value; break;
                case 5: data-> oscFX = value; break;
                case 6: data-> detune = value; break;
                case 7: data-> lfoSyncFreq = value; break;
                case 8: data-> midiClkTimeInterval = value; break;
                case 9: data-> lfoTempoValue = value; break;
                case 10: data-> keyTracking = value; break;
                case 11: data-> glide = value; break;
                case 12: data-> pitchA = value; break;
                case 13: data-> pitchB = value; break;
                case 14: data-> waveformA = value; break;
                case 15: data-> waveformB = value; break;
                case 16: data-> pWMSource = value; break;
                case 17: data-> pWA = value; break;
                case 18: data-> pWB = value; break;
                case 19: data-> pWMRate = value; break;
                case 20: data-> pwmAmtA = value; break;
                case 21: data-> pwmAmtB = value; break;
                case 22: data-> filterRes = value; break;
                case 23: data-> filterFreq = value; break;
                case 24: data-> filterMixer = value; break;
                case 25: data-> filterEnv = value; break;
                case 26: data-> pitchLFOAmt = value; break;
                case 27: data-> pitchLFORate = value; break;
                case 28: data-> pitchLFOWaveform = value; break;
                case 29: data-> pitchLFORetrig = value; break;
                case 30: data-> pitchLFOMidiClkSync = value; break;
                case 31: data-> filterLfoRate = value; break;
                case 32: data-> filterLFORetrig = value; break;
                case 33: data-> filterLFOMidiClkSync = value; break;
                case 34: data-> filterLfoAmt = value; break;
                case 35: data-> filterLFOWaveform = value; break;
                case 36: data-> filterAttack = value; break;
                case 37: data-> filterDecay = value; break;
                case 38: data-> filterSustain = value; break;
                case 39: data-> filterRelease = value; break;
                case 40: data-> attack = value; break;
                case 41: data-> decay = value; break;
                case 42: data-> sustain = value; break;
                case 43: data-> release = value; break;
                case 44: data-> effectAmt = value; break;
                case 45: data-> effectMix = value; break;
                case 46: data-> pitchEnv = value; break;
                case 47: data-> velocitySens = value; break;
                case 48: data-> chordDetune = value; break;
                case 49: data-> monophonic = value; break;
                case 50: data-> spare1 = value; break;
                case 51: data-> spare2 = value; break;
            }
        }

        i++;
    }
}

FLASHMEM void recallPatchData(File patchFile, String data[]){
  //Read patch data from file and set current patch parameters
  size_t n;     // Length of returned field with delimiter.
  char str[20]; // Must hold longest field with delimiter and zero byte.
  uint32_t i = 0;
  while (patchFile.available() && i < NO_OF_PARAMS)
  {
    n = readField(&patchFile, str, sizeof(str), ",\n");
    // done if Error or at EOF.
    if (n == 0)
      break;
    // Print the type of delimiter.
    if (str[n - 1] == ',' || str[n - 1] == '\n')
    {
      // Remove the delimiter.
      str[n - 1] = 0;
    }
    else
    {
      // At eof, too long, or read error.  Too long is error.
      Serial.print(patchFile.available() ? F("error: ") : F("eof:   "));
    }
    // Print the field.
    //    Serial.print(i);
    //    Serial.print(" - ");
    //    Serial.println(str);
    data[i++] = String(str);
  }
}

FLASHMEM int compare(const void *a, const void *b) {
  return ((PatchNoAndName*)a)->patchNo - ((PatchNoAndName*)b)->patchNo;
}

FLASHMEM void sortPatches(){
  int arraySize = patches.size();
  //Sort patches buffer to be consecutive ascending patchNo order
  struct PatchNoAndName arrayToSort[arraySize];

  for (int i = 0; i < arraySize; ++i)
  {
    arrayToSort[i] = patches[i];
  }
  qsort(arrayToSort, arraySize, sizeof(PatchNoAndName), compare);
  patches.clear();

  for (int i = 0; i < arraySize; ++i)
  {
    patches.push(arrayToSort[i]);
  }
}

FLASHMEM void loadPatches(){
  File file = SD.open("/");
  patches.clear();
  while (true)
  {
    String data[NO_OF_PARAMS]; //Array of data read in
    File patchFile = file.openNextFile();
    if (!patchFile)
    {
      break;
    }
    if (patchFile.isDirectory())
    {
      Serial.println("Ignoring Dir");
    }
    else
    {
      recallPatchData(patchFile, data);
      patches.push(PatchNoAndName{atoi(patchFile.name()), data[0], data[50].toInt() != 0});
      Serial.println(String(patchFile.name()) + ":" + data[0]);
    }
    patchFile.close();
  }
  sortPatches();
}

FLASHMEM void savePatch(const char *patchNo, String patchData){
  // Serial.print("savePatch Patch No:");
  //  Serial.println(patchNo);
  //Overwrite existing patch by deleting
  if (SD.exists(patchNo))
  {
    SD.remove(patchNo);
  }
  File patchFile = SD.open(patchNo, FILE_WRITE);
  if (patchFile)
  {
    //    Serial.print("Writing Patch No:");
    //    Serial.println(patchNo);
    //Serial.println(patchData);
    patchFile.println(patchData);
    patchFile.close();
  }
  else
  {
    Serial.print("Error writing Patch file:");
    Serial.println(patchNo);
  }
}

FLASHMEM void savePatch(const char *patchNo, String patchData[]){
  String dataString = patchData[0];
  for (uint32_t i = 1; i < NO_OF_PARAMS; i++)
  {
    dataString = dataString + F(",") + patchData[i];
  }
  savePatch(patchNo, dataString);
}

FLASHMEM void deletePatch(const char *patchNo)
{
  if (SD.exists(patchNo)) SD.remove(patchNo);
}

FLASHMEM void renumberPatchesOnSD() {
  for (int i = 0; i < patches.size(); i++)
  {
    String data[NO_OF_PARAMS]; //Array of data read in
    File file = SD.open(String(patches[i].patchNo).c_str());
    if (file) {
      recallPatchData(file, data);
      file.close();
      savePatch(String(i + 1).c_str(), data);
    }
  }
  deletePatch(String(patches.size() + 1).c_str()); //Delete final patch which is duplicate of penultimate patch
}

FLASHMEM void setPatchesOrdering(int no) {
  if (patches.size() < 2)return;
  while (patches.first().patchNo != no) {
    patches.push(patches.shift());
  }
}

FLASHMEM void resetPatchesOrdering() {
  setPatchesOrdering(1);
}
