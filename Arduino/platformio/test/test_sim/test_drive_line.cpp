// driveLine() on a simulated ATmega328P, checking the real pin registers:
// pio test -e sim --without-uploading
//
// Pins 5-7 are wired directly to the coinmech lines, so they must never be a
// normal output. Pins 5, 6 and 7 are bits 5, 6 and 7 of port D.
#include <Arduino.h>
#include <avr/sleep.h>
#include <unity.h>
#include <vend.h>

static const int PINS[] = { 5, 6, 7 };

void setUp() {}
void tearDown() {}

// Released: input (DDR bit 0) without pull-up (PORT bit 0).
void test_released_line_is_input_without_pullup() {
  for (int pin : PINS) {
    driveLine(pin, true);
    driveLine(pin, false);
    TEST_ASSERT_BITS_LOW(_BV(pin), DDRD);
    TEST_ASSERT_BITS_LOW(_BV(pin), PORTD);
  }
}

// Active: output (DDR bit 1) driving LOW (PORT bit 0), even when the pin had
// its pull-up on (PORT bit 1) before.
void test_active_line_is_output_low() {
  for (int pin : PINS) {
    pinMode(pin, INPUT_PULLUP);
    TEST_ASSERT_BITS_HIGH(_BV(pin), PORTD);
    driveLine(pin, true);
    TEST_ASSERT_BITS_HIGH(_BV(pin), DDRD);
    TEST_ASSERT_BITS_LOW(_BV(pin), PORTD);
  }
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_released_line_is_input_without_pullup);
  RUN_TEST(test_active_line_is_output_low);
  UNITY_END();
  Serial.flush();
  // simavr exits when the CPU sleeps with interrupts off.
  cli();
  sleep_enable();
  sleep_cpu();
}

void loop() {}
