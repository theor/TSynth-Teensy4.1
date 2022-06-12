#ifndef TSYNTH_UTILS_H
#define TSYNTH_UTILS_H

#include <Arduino.h>

// Clamp a value to the given range.
template <typename NumericType>
NumericType inRangeOrDefault(NumericType value, NumericType defaultValue, NumericType min, NumericType max) {
  static_assert(std::is_arithmetic<NumericType>::value, "NumericType must be numeric");
  if (value < min || value > max) return defaultValue;
  return value;
}

String milliToString(float milli) {
    if (milli < 1000) return String(int(milli)) + " ms";
    return String(milli / 1000) + " s";
}


template<typename T, size_t  N>
T clampInto(const T(&array)[N] , T value) {
    for (size_t i = 0; i < N; ++i) {
        if(array[i] == value)
            return value;
    }
    return array[0];
}

template<typename T, size_t  N>
size_t indexOf(const T(&array)[N] , T value, bool next) {
    for (size_t i = 0; i < N; ++i) {
        if(value == array[i]) {
            if(next){
                while(value == array[i] && i < N) i++;
                return (i) % N;
            }else {
                return (i+N-1) % N;
            }
        }
    }
    return 0;
}

#endif