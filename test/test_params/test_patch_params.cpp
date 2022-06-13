//
// Created by theor on 2022-06-13.
//
#include <unity.h>
#include <iostream>

const uint8_t OSCMIXA[128] = {127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 126, 125, 123, 120, 118, 116, 114, 112, 110, 108, 106, 104, 102, 100, 98, 96, 94, 92, 90, 88, 86, 84, 82, 80, 78, 76, 74, 72, 70, 68, 66, 64, 62, 60, 58, 56, 54, 52, 50, 48, 46, 44, 42, 40, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0};
const uint8_t OSCMIXB[128] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 123, 125, 126, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127};

const float LINEAR[128] = {0, 0.008, 0.016, 0.024, 0.031, 0.039, 0.047, 0.055, 0.063, 0.071, 0.079, 0.087, 0.094, 0.102, 0.11, 0.118, 0.126, 0.134, 0.142, 0.15, 0.157, 0.165, 0.173, 0.181, 0.189, 0.197, 0.205, 0.213, 0.22, 0.228, 0.236, 0.244, 0.252, 0.26, 0.268, 0.276, 0.283, 0.291, 0.299, 0.307, 0.315, 0.323, 0.331, 0.339, 0.346, 0.354, 0.362, 0.37, 0.378, 0.386, 0.394, 0.402, 0.409, 0.417, 0.425, 0.433, 0.441, 0.449, 0.457, 0.465, 0.472, 0.48, 0.488, 0.496, 0.504, 0.512, 0.52, 0.528, 0.535, 0.543, 0.551, 0.559, 0.567, 0.575, 0.583, 0.591, 0.598, 0.606, 0.614, 0.622, 0.63, 0.638, 0.646, 0.654, 0.661, 0.669, 0.677, 0.685, 0.693, 0.701, 0.709, 0.717, 0.724, 0.732, 0.74, 0.748, 0.756, 0.764, 0.772, 0.78, 0.787, 0.795, 0.803, 0.811, 0.819, 0.827, 0.835, 0.843, 0.85, 0.858, 0.866, 0.874, 0.882, 0.89, 0.898, 0.906, 0.913, 0.921, 0.929, 0.937, 0.945, 0.953, 0.961, 0.969, 0.976, 0.984, 0.992, 1.00};

void setUp() {}
void tearDown() {}

void toMix(uint8_t midi)
{
    std::cout << LINEAR[OSCMIXA[midi]] << " " << LINEAR[OSCMIXB[midi]] << std::endl;
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

void test_oscmix()
{
    toMix(64);
    // 77 -> oscmixa 100 -> 0.787
    // TEST_ASSERT_EQUAL_FLOAT(0.787, LINEAR[OSCMIXA[77]]);
    uint8_t r = fromMix(0.787, 1);
    std::cout << (int)r << std::endl;
    TEST_ASSERT_EQUAL_INT8(77, r);

    for (size_t i = 0; i < 128; i++)
    {
        float linA = LINEAR[OSCMIXA[i]];
        float linB = LINEAR[OSCMIXB[i]];
        uint8_t r = fromMix(linA, linB);
        TEST_ASSERT_EQUAL_INT8(i, r);

        /* code */
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_oscmix);
    UNITY_END();
}