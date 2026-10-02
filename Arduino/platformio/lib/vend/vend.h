// Coin replay logic, kept free of Arduino.h (except driveLine() at the bottom)
// so it can be unit tested on a PC: `pio test -e native`. src/vend_hack.cpp
// connects it to the real pins.
#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <stdint.h>

#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif

// The Pi's pulse timing limits, in ms. These must match Timing::MIN_HIGH and
// Timing::MIN_LOW in RPi/bitvend/src/coin_signal.rs; a Rust test reads them
// from this file. The Arduino doesn't watch the Pi's pin during a replay, so
// every replayed recording must finish within PI_MIN_HIGH_MS.
#define PI_MIN_HIGH_MS 60
#define PI_MIN_LOW_MS 40

// The three coinmech lines, as indexes into Recording::lines.
#define LINE_SEND 0
#define LINE_INTR 1
#define LINE_DATA 2

// The hardware the logic needs: a microsecond clock, and a way to make a
// coinmech line active (pulled to 0 V) or released. Tests use a fake one.
class Hal {
 public:
  virtual uint32_t micros() = 0;
  virtual void setLine(int line, bool active) = 0;
};

// A recorded coinmech signal for one quarter. Each line is a list of times in
// microseconds between level changes, ending in 0. The first value is the
// delay before the line's first change, and may be 0.
struct Recording {
  const uint16_t* lines[3];
};

// The four quarters we recorded.
extern const Recording RECORDINGS[4];

// How many of RECORDINGS we cycle through. The fourth recording is skipped,
// since its signal is unusually long (~85 ms, longer than PI_MIN_HIGH_MS).
#define REPLAYED_COUNT 3

// Replay a recording on the three lines. Blocks until done, and returns how
// long it took in microseconds.
uint32_t replay(const Recording& r, Hal& hal);

// Copy the coinmech input levels to the lines: an input reading LOW (0 V,
// active) makes its line active. Inputs reading HIGH leave the lines released.
void passThrough(int s, int i, int d, Hal& hal);

// Debounces a pin pulled to GND by hand. A new level must hold for
// debounce_ms before we believe it, because a jumper wire flickers for a few
// ms when it touches or leaves GND.
class Debouncer {
 public:
  explicit Debouncer(uint32_t debounce_ms) : debounce_ms(debounce_ms) {}
  // Feed the raw pin level; returns true once per press (steady HIGH -> steady LOW).
  bool update(int raw, uint32_t now_ms);

 private:
  uint32_t debounce_ms;
  int stable = HIGH;  // The level we believe.
  int raw_level = HIGH;  // The last raw reading.
  uint32_t changed_ms = 0;  // When the raw reading last changed.
};

// Detects LOW -> HIGH changes, e.g. one per quarter from the Pi.
class RisingEdge {
 public:
  // Feed the pin level; returns true when it has just gone from LOW to HIGH.
  bool update(int level) {
    bool rose = level == HIGH && last == LOW;
    last = level;
    return rose;
  }

 private:
  int last = LOW;
};

// Records the coinmech signals, so we can make new recordings.
#define ARRAY_SIZE 20
#define MAX_MESSAGE_LENGTH_MILLIS 500

class Recorder {
 public:
  enum Result { NOTHING, MESSAGE_DONE, OVERFLOW };

  // Feed one reading of the three coinmech inputs. Returns MESSAGE_DONE when
  // a recording is complete: data then holds it, until reset(). Returns
  // OVERFLOW if a line changed more than ARRAY_SIZE times.
  Result sample(int s, int i, int d, uint32_t time);
  // Clear the recording, ready for the next one.
  void reset();

  // For each line, the time since that line's previous change, in microseconds.
  uint16_t data[3][ARRAY_SIZE] = {};

 private:
  // Flag for ongoing signal recording.
  bool is_in_message = false;
  // Timestamp we started recording coinmech signals.
  uint32_t epoch_micros = 0;
  // Idle level of the coinmech lines (pulled up). All three must start HIGH,
  // or the first sample sees a "change" and records a bogus message at boot.
  int last[3] = {HIGH, HIGH, HIGH};
  // Timestamps for level transitions on each coinmech wire.
  uint32_t last_micros[3] = {};
  int idx[3] = {};
};

#ifdef ARDUINO
// Acts like an open-collector transistor on a coinmech line. The original
// hardware had one per line (Arduino HIGH = pull the line to 0 V), which is why
// the recordings are replayed with "active" as the inverse of the recorded level.
// - active:   OUTPUT LOW, pulls the line to 0 V.
// - inactive: INPUT without pull-up, lets the machine hold the line at 5 V.
inline void driveLine(int pin, bool active) {
  if (active) {
    digitalWrite(pin, LOW);  // Set LOW first, so the pin never outputs HIGH.
    pinMode(pin, OUTPUT);
  } else {
    pinMode(pin, INPUT);
  }
}
#endif
