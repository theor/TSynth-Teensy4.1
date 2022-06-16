#ifndef TSYNTH_UTILS_H
#define TSYNTH_UTILS_H

#include <Arduino.h>
#include <algorithm>

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
size_t indexOf(const T(&array)[N] , T value) {
    for (size_t i = 0; i < N; ++i) {
        if (value == array[i]) { return i; }
    }
    return N;
}
template<typename T, size_t  N>
size_t indexOfSorted(const T(&array)[N] , T value) {
    for (size_t i = 0; i < N; ++i) {
        if (value == array[i] || value < array[i])
            return i;
    }
    return N;
}
template<typename T, size_t  N>
size_t lastIndexOf(const T(&array)[N] , T value) {
    for (size_t i = N - 1; i >= 0; --i) {
        if (value == array[i]) { return i; }
    }
    return N;
}
template<typename T, size_t  N>
size_t lastIndexOfSorted(const T(&array)[N] , T value) {
    for (size_t i = N - 1; i >= 0; --i) {
        if (value == array[i] || value > array[i]) { return i; }
    }
    return N;
}
template<typename T, size_t  N>
size_t indexOfClosest(const T(&array)[N] , T value, bool biggerIfNotFound) {
    return biggerIfNotFound ? lastIndexOfSorted(array, value) : indexOfSorted(array, value);

}
size_t cycleByte(uint8_t value, bool next, bool loop = true) {
    if(next) {
        if(value > 0 || loop)
            value--;
    } else {
        if(value < 255 || loop)
            value++;
    }
    return value;
}

template<typename T, size_t  N>
size_t cycleIndexOf(const T(&array)[N] , T value, bool next, bool loop = true) {
    for (size_t i = 0; i < N; ++i) {
        if(value == array[i]) {
            if(next){
                while(value == array[i] && i < N) i++;
                if(i >= N && !loop)
                    return N - 1;
                return (i) % N;
            }else {
                if(i == 0 && !loop)
                    return 0;
                return (i+N-1) % N;
            }
        }
    }
    return 0;
}
template<typename T, size_t  N>
size_t cycleIndexOfSorted(const T(&array)[N] , T value, bool next, bool loop = true) {
    for (size_t i = 0; i < N; ++i) {
        if(value < array[i]) { return next ? i : (i -1); }
        if(value == array[i]) {
            if(next){
//                while(value == array[i] && i < N) i++;
                if(i >= N && !loop)
                    return N - 1;
                return (i) % N;
            }else {
                if(i == 0 && !loop)
                    return 0;
                return (i+N-1) % N;
            }
        }
    }
    return 0;
}

#endif