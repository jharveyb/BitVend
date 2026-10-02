#include "vend.h"

// Four coinmech signals we recorded that represent a quarter.
static const uint16_t QUARTER1_S[] = { 1764,18004,3076,17104,0 };
static const uint16_t QUARTER1_I[] = { 0,45508,0 };
static const uint16_t QUARTER1_D[] = { 1856,6696,5020,3392,5972,6744,5012,3352,0 };
//--------------------
static const uint16_t QUARTER2_S[] = { 3848,18052,3076,17104,0 };
static const uint16_t QUARTER2_I[] = { 0,47640,0 };
static const uint16_t QUARTER2_D[] = { 3940,6696,5020,3392,6020,6744,5012,3352,0 };
//--------------------
static const uint16_t QUARTER3_S[] = { 3080,18096,3076,17100,0 };
static const uint16_t QUARTER3_I[] = { 0,46912,0 };
static const uint16_t QUARTER3_D[] = { 3124,6744,5020,3392,6064,6740,5020,3348,0 };
//--------------------
static const uint16_t QUARTER4_S[] = { 456,18096,3076,17100,0 };
static const uint16_t QUARTER4_I[] = { 0,44292,0 };
static const uint16_t QUARTER4_D[] = { 500,6744,5020,3392,6064,6736,5020,3352,33504,1720,3348,1676,5020,3348,0 };

const Recording RECORDINGS[4] = {
  {{ QUARTER1_S, QUARTER1_I, QUARTER1_D }},
  {{ QUARTER2_S, QUARTER2_I, QUARTER2_D }},
  {{ QUARTER3_S, QUARTER3_I, QUARTER3_D }},
  {{ QUARTER4_S, QUARTER4_I, QUARTER4_D }},
};

uint32_t replay(const Recording& r, Hal& hal) {
  int index[3] = {0, 0, 0};
  int state[3] = {HIGH, HIGH, HIGH};
  bool done[3] = {false, false, false};
  int remaining = 3;

  uint32_t start = hal.micros();

  // Times are kept relative to start: micros() wraps around every ~71 min, and
  // comparing elapsed times (unsigned subtraction) still works across the wrap.
  uint32_t next[3];
  for (int line = 0; line < 3; line++) {
    next[line] = r.lines[line][index[line]++];
  }
  uint32_t current = 0;

  // Replay the recorded coinmech signals, until the end of the longest signal.
  while (remaining > 0) {
    current = hal.micros() - start;
    for (int line = 0; line < 3; line++) {
      if (done[line] || next[line] > current) {
        continue;
      }
      const uint16_t* times = r.lines[line];
      if (times[index[line]] > 0) {
        // Toggle the recorded level; the line is active while the recorded
        // level is LOW. So the first change makes the line active.
        state[line] = !state[line];
        next[line] = next[line] + times[index[line]++];
        hal.setLine(line, !state[line]);
      } else {
        // Done replaying this line (a 0 ends the list), let go of it.
        done[line] = true;
        remaining--;
        hal.setLine(line, false);
      }
    }
  }
  return current;
}

void passThrough(int s, int i, int d, Hal& hal) {
  hal.setLine(LINE_SEND, s == LOW);
  hal.setLine(LINE_INTR, i == LOW);
  hal.setLine(LINE_DATA, d == LOW);
}

bool Debouncer::update(int raw, uint32_t now_ms) {
  if (raw != raw_level) {
    raw_level = raw;
    changed_ms = now_ms;
  }
  if (raw != stable && now_ms - changed_ms >= debounce_ms) {
    stable = raw;
    return stable == LOW;
  }
  return false;
}

Recorder::Result Recorder::sample(int s, int i, int d, uint32_t time) {
  int level[3] = {s, i, d};

  // If we aren't currently recording a signal, and the level on any coinmech
  // wire changed, start recording by setting an initial timestamp.
  bool changed = level[0] != last[0] || level[1] != last[1] || level[2] != last[2];
  if (!is_in_message && changed) {
    is_in_message = true;
    epoch_micros = time;
    for (int line = 0; line < 3; line++) {
      last_micros[line] = time;
    }
  }

  // For every level change on each wire, record the time since that wire's previous change.
  for (int line = 0; line < 3; line++) {
    if (level[line] != last[line]) {
      // Limit the number of level changes we'll record.
      if (idx[line] >= ARRAY_SIZE) {
        return OVERFLOW;
      }
      data[line][idx[line]++] = time - last_micros[line];
      last_micros[line] = time;
    }
    last[line] = level[line];
  }

  // Limit the duration of a signal recording.
  if (is_in_message && ((time - epoch_micros) / 1000) > MAX_MESSAGE_LENGTH_MILLIS) {
    is_in_message = false;
    return MESSAGE_DONE;
  }
  return NOTHING;
}

void Recorder::reset() {
  for (int line = 0; line < 3; line++) {
    idx[line] = 0;
    for (int k = 0; k < ARRAY_SIZE; k++) {
      data[line][k] = 0;
    }
  }
}
