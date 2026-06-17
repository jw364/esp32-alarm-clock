//  TM1637Display — Arduino library for TM1637 4-digit 7-segment display
//  Original author: Avishay Orpaz
//  Bundled in this project — API identical to v1.2.0

#include "TM1637Display.h"
#include <Arduino.h>

// Segment encoding table for digits 0-9 and A-F
static const uint8_t digitToSegment[] = {
    0b00111111, // 0
    0b00000110, // 1
    0b01011011, // 2
    0b01001111, // 3
    0b01100110, // 4
    0b01101101, // 5
    0b01111101, // 6
    0b00000111, // 7
    0b01111111, // 8
    0b01101111, // 9
    0b01110111, // A
    0b01111100, // b
    0b00111001, // C
    0b01011110, // d
    0b01111001, // E
    0b01110001  // F
};

TM1637Display::TM1637Display(uint8_t pinClk, uint8_t pinDIO, unsigned int bitDelay)
    : m_pinClk(pinClk), m_pinDIO(pinDIO), m_brightness(0xf), m_bitDelay(bitDelay)
{
    pinMode(m_pinClk, INPUT);
    pinMode(m_pinDIO, INPUT);
    digitalWrite(m_pinClk, LOW);
    digitalWrite(m_pinDIO, LOW);
}

void TM1637Display::setBrightness(uint8_t brightness, bool on)
{
    m_brightness = (brightness & 0x7) | (on ? 0x08 : 0x00);
}

void TM1637Display::setSegments(const uint8_t segments[], uint8_t length, uint8_t pos)
{
    // Write command 1: data command, fixed address
    start();
    writeByte(0x44);
    stop();

    // Write COMM2: set display address
    start();
    writeByte(0xc0 | (pos & 0x03));

    for (uint8_t k = 0; k < length; k++) {
        writeByte(segments[k]);
    }
    stop();

    // Write command 3: display control
    start();
    writeByte(0x80 | m_brightness);
    stop();
}

void TM1637Display::clear()
{
    uint8_t data[] = { 0, 0, 0, 0 };
    setSegments(data, 4, 0);
}

void TM1637Display::showNumberDec(int num, bool leading_zero, uint8_t length, uint8_t pos)
{
    showNumberDecEx(num, 0, leading_zero, length, pos);
}

void TM1637Display::showNumberDecEx(int num, uint8_t dots, bool leading_zero,
                                     uint8_t length, uint8_t pos)
{
    showNumberBaseEx(10, (uint16_t)num, dots, leading_zero, length, pos);
}

void TM1637Display::showNumberHexEx(uint16_t num, uint8_t dots, bool leading_zero,
                                     uint8_t length, uint8_t pos)
{
    showNumberBaseEx(-16, num, dots, leading_zero, length, pos);
}

void TM1637Display::showNumberBaseEx(int8_t base, uint16_t num, uint8_t dots,
                                      bool leading_zero, uint8_t length, uint8_t pos)
{
    bool negative = false;
    if (base < 0) {
        base = -base;
    } else {
        if ((int)num < 0) {
            negative = true;
            num = (uint16_t)(-(int)num);
        }
    }

    uint8_t digits[4];
    const uint8_t maxVal = (base == 10) ? 9999 : 0xffff;

    if (num > maxVal) {
        showDots(dots, nullptr);
        return;
    }

    for (int8_t k = length - 1; k >= 0; k--) {
        uint8_t digit = num % base;
        if (digit == 0 && num == 0 && k > 0) {
            digits[k] = leading_zero ? encodeDigit(0) : 0;
        } else {
            digits[k] = encodeDigit(digit);
        }
        num /= base;
    }

    if (negative) {
        digits[0] = SEG_G; // minus sign
    }

    showDots(dots, digits);
    setSegments(digits, length, pos);
}

void TM1637Display::showDots(uint8_t dots, uint8_t* digits)
{
    if (digits == nullptr) return;
    for (uint8_t k = 0; k < 4; k++) {
        digits[k] |= (dots & 0x80) ? SEG_DP : 0;
        dots <<= 1;
    }
}

uint8_t TM1637Display::encodeDigit(uint8_t digit)
{
    return digitToSegment[digit & 0x0f];
}

void TM1637Display::bitDelay()
{
    delayMicroseconds(m_bitDelay);
}

void TM1637Display::start()
{
    pinMode(m_pinDIO, OUTPUT);
    bitDelay();
}

void TM1637Display::stop()
{
    pinMode(m_pinDIO, OUTPUT);
    bitDelay();
    pinMode(m_pinClk, INPUT);
    bitDelay();
    pinMode(m_pinDIO, INPUT);
    bitDelay();
}

bool TM1637Display::writeByte(uint8_t b)
{
    for (uint8_t i = 0; i < 8; i++) {
        pinMode(m_pinClk, OUTPUT);
        bitDelay();
        if (b & 0x01) {
            pinMode(m_pinDIO, INPUT);
        } else {
            pinMode(m_pinDIO, OUTPUT);
        }
        bitDelay();
        pinMode(m_pinClk, INPUT);
        bitDelay();
        b >>= 1;
    }

    // ACK
    pinMode(m_pinClk, OUTPUT);
    bitDelay();
    pinMode(m_pinDIO, INPUT);
    bitDelay();
    pinMode(m_pinClk, INPUT);
    bitDelay();
    bool ack = (digitalRead(m_pinDIO) == LOW);
    pinMode(m_pinClk, OUTPUT);
    bitDelay();
    return ack;
}
