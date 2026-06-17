//  TM1637Display — Arduino library for TM1637 4-digit 7-segment display
//  Original author: Avishay Orpaz  (https://github.com/avishay/TM1637Display)
//  Bundled in this project because the upstream repository is no longer available.
//  API is identical to v1.2.0 of the original library.

#ifndef TM1637DISPLAY_H
#define TM1637DISPLAY_H

#include <inttypes.h>

#define SEG_A   0b00000001
#define SEG_B   0b00000010
#define SEG_C   0b00000100
#define SEG_D   0b00001000
#define SEG_E   0b00010000
#define SEG_F   0b00100000
#define SEG_G   0b01000000
#define SEG_DP  0b10000000

#define DEFAULT_BIT_DELAY  100

class TM1637Display {
public:
    TM1637Display(uint8_t pinClk, uint8_t pinDIO, unsigned int bitDelay = DEFAULT_BIT_DELAY);

    // brightness: 0 (off/dim) .. 7 (brightest), on: display power
    void setBrightness(uint8_t brightness, bool on = true);

    // Write raw segment data to length digits starting at pos
    void setSegments(const uint8_t segments[], uint8_t length = 4, uint8_t pos = 0);

    // Clear all digits
    void clear();

    // Show decimal number, optional leading zero, length digits from pos
    void showNumberDec(int num, bool leading_zero = false,
                       uint8_t length = 4, uint8_t pos = 0);

    // Same but with dot/colon control: bit 4=dot after digit 3,
    // bit 5=dot after digit 2, bit 6=dot after digit 1, bit 7=dot after digit 0
    void showNumberDecEx(int num, uint8_t dots = 0, bool leading_zero = false,
                         uint8_t length = 4, uint8_t pos = 0);

    void showNumberHexEx(uint16_t num, uint8_t dots = 0, bool leading_zero = false,
                         uint8_t length = 4, uint8_t pos = 0);

    // Encode a single decimal digit (0-9) into segment bits
    uint8_t encodeDigit(uint8_t digit);

private:
    uint8_t      m_pinClk;
    uint8_t      m_pinDIO;
    uint8_t      m_brightness;
    unsigned int m_bitDelay;

    void     bitDelay();
    void     start();
    void     stop();
    bool     writeByte(uint8_t b);
    void     showDots(uint8_t dots, uint8_t* digits);
    void     showNumberBaseEx(int8_t base, uint16_t num, uint8_t dots,
                              bool leading_zero, uint8_t length, uint8_t pos);
};

#endif
