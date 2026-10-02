// Unit tests for lib/vend, run on this computer: pio test -e native
#include <unity.h>
#include <vend.h>

#include <vector>

// Each fake micros() call advances the clock by this much.
static const uint32_t STEP = 4;

// A stand-in for the Arduino: a fake clock, and a log of every line change.
struct Change {
  uint32_t time;  // Since the FakeHal was created.
  int line;
  bool active;
};

class FakeHal : public Hal {
 public:
  explicit FakeHal(uint32_t start = 1000) : clock(start), start(start) {}
  uint32_t micros() override { return clock += STEP; }
  void setLine(int line, bool active) override { changes.push_back({clock - start, line, active}); }

  std::vector<Change> changes;
  std::vector<Change> changesOn(int line) const {
    std::vector<Change> out;
    for (const Change& c : changes) {
      if (c.line == line) out.push_back(c);
    }
    return out;
  }

 private:
  uint32_t clock;
  uint32_t start;
};

// How long a recording takes to replay, in microseconds.
static uint32_t replayTime(const Recording& r) {
  FakeHal hal;
  return replay(r, hal);
}

void setUp() {}
void tearDown() {}

// ---- Recordings ----

// A 0 ends each line's list, so a 0 anywhere but the first slot would cut the
// replay short.
void test_recordings_end_in_zero_and_have_no_gaps() {
  for (int q = 0; q < 4; q++) {
    for (int line = 0; line < 3; line++) {
      const uint16_t* times = RECORDINGS[q].lines[line];
      int k = 1;
      while (times[k] != 0) {
        k++;
        TEST_ASSERT_LESS_THAN_MESSAGE(64, k, "recording line has no 0 at the end");
      }
      TEST_ASSERT_GREATER_THAN_MESSAGE(1, k, "recording line has no level changes");
    }
  }
}

// ---- Replay ----

// Each line's changes happen at the recorded times: the first change makes the
// line active, then they alternate, and the line ends released.
void test_replay_matches_recorded_times() {
  for (int q = 0; q < 4; q++) {
    FakeHal hal;
    replay(RECORDINGS[q], hal);
    for (int line = 0; line < 3; line++) {
      const uint16_t* times = RECORDINGS[q].lines[line];
      std::vector<Change> changes = hal.changesOn(line);
      uint32_t expected = times[0];
      size_t n = 0;
      for (int k = 1;; k++, n++) {
        TEST_ASSERT_LESS_THAN(changes.size(), n);
        // Allow a few clock steps of delay; never early.
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expected, changes[n].time);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(expected + 6 * STEP, changes[n].time);
        if (times[k] == 0) {
          TEST_ASSERT_FALSE_MESSAGE(changes[n].active, "line must end released");
          break;
        }
        TEST_ASSERT_EQUAL(k % 2 == 1, changes[n].active);
        expected += times[k];
      }
      TEST_ASSERT_EQUAL_MESSAGE(n + 1, changes.size(), "unexpected extra line changes");
    }
  }
}

// micros() wraps around every ~71 min. A replay that straddles the wrap must
// have the same timing as any other.
void test_replay_across_micros_wraparound() {
  FakeHal normal;
  FakeHal wrapping(0xFFFFFFFFu - 10000);
  replay(RECORDINGS[0], normal);
  replay(RECORDINGS[0], wrapping);
  TEST_ASSERT_EQUAL(normal.changes.size(), wrapping.changes.size());
  for (size_t n = 0; n < normal.changes.size(); n++) {
    TEST_ASSERT_EQUAL_UINT32(normal.changes[n].time, wrapping.changes[n].time);
    TEST_ASSERT_EQUAL(normal.changes[n].line, wrapping.changes[n].line);
    TEST_ASSERT_EQUAL(normal.changes[n].active, wrapping.changes[n].active);
  }
}

// ---- Pi <-> Arduino timing contract ----

// The Arduino doesn't watch the Pi's pin during a replay, so every replayed
// recording must finish (with some margin) before the Pi's shortest HIGH ends.
// If this fails after re-enabling a recording (e.g. QUARTER4, ~85 ms), raise
// PI_MIN_HIGH_MS here and Timing::MIN_HIGH in RPi/bitvend/src/coin_signal.rs.
void test_replays_fit_in_the_pis_shortest_pulse() {
  for (int q = 0; q < REPLAYED_COUNT; q++) {
    uint32_t ms = replayTime(RECORDINGS[q]) / 1000;
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(PI_MIN_HIGH_MS, ms + 5, "replay too long for the Pi's PI_MIN_HIGH_MS");
  }
}

// Simulate the Pi sending quarters at its fastest timing, with the Arduino
// blind while it replays: every pulse must still be seen.
void test_no_quarters_lost_at_the_pis_fastest_timing() {
  uint32_t blind_ms = 0;
  for (int q = 0; q < REPLAYED_COUNT; q++) {
    uint32_t ms = replayTime(RECORDINGS[q]) / 1000 + 1;
    if (ms > blind_ms) blind_ms = ms;
  }
  const int pulses = 10;
  RisingEdge edge;
  int seen = 0;
  for (uint32_t t = 0; t < pulses * (PI_MIN_HIGH_MS + PI_MIN_LOW_MS); t++) {
    int level = t % (PI_MIN_HIGH_MS + PI_MIN_LOW_MS) < PI_MIN_HIGH_MS ? HIGH : LOW;
    if (edge.update(level)) {
      seen++;
      t += blind_ms;  // The replay blocks loop().
    }
  }
  TEST_ASSERT_EQUAL(pulses, seen);
}

// ---- Rising edge (Pi pin) ----

void test_rising_edge_once_per_pulse() {
  RisingEdge edge;
  int seen = 0;
  for (int k = 0; k < 100; k++) seen += edge.update(HIGH);  // Held HIGH: one edge.
  TEST_ASSERT_EQUAL(1, seen);
  seen += edge.update(LOW);
  seen += edge.update(HIGH);
  TEST_ASSERT_EQUAL(2, seen);
}

// ---- Debounce (debug pin) ----

// Feed `level` to the debouncer once per ms for `ms` ms; returns the presses seen.
static int hold(Debouncer& d, uint32_t& now, int level, uint32_t ms) {
  int presses = 0;
  for (uint32_t k = 0; k < ms; k++) presses += d.update(level, now++);
  return presses;
}

// A jumper wire flickering as it touches or leaves GND.
static int bounce(Debouncer& d, uint32_t& now) {
  int presses = 0;
  for (int k = 0; k < 10; k++) {
    presses += hold(d, now, LOW, 1);
    presses += hold(d, now, HIGH, 1);
  }
  return presses;
}

void test_debounce_clean_press() {
  Debouncer d(50);
  uint32_t now = 0;
  int presses = hold(d, now, HIGH, 100) + hold(d, now, LOW, 200) + hold(d, now, HIGH, 200);
  TEST_ASSERT_EQUAL(1, presses);
}

// The bug we had: a long hold, then the flicker on release, gave a second quarter.
void test_debounce_bouncy_press_long_hold_bouncy_release() {
  Debouncer d(50);
  uint32_t now = 0;
  int presses = hold(d, now, HIGH, 100);
  presses += bounce(d, now) + hold(d, now, LOW, 2000);
  presses += bounce(d, now) + hold(d, now, HIGH, 500);
  TEST_ASSERT_EQUAL(1, presses);
}

void test_debounce_two_separate_presses() {
  Debouncer d(50);
  uint32_t now = 0;
  int presses = hold(d, now, HIGH, 100);
  presses += hold(d, now, LOW, 100) + hold(d, now, HIGH, 100);
  presses += hold(d, now, LOW, 100) + hold(d, now, HIGH, 100);
  TEST_ASSERT_EQUAL(2, presses);
}

// Letting go for less than the debounce time doesn't start a new press.
void test_debounce_short_release_is_one_press() {
  Debouncer d(50);
  uint32_t now = 0;
  int presses = hold(d, now, HIGH, 100) + hold(d, now, LOW, 100);
  presses += hold(d, now, HIGH, 30) + hold(d, now, LOW, 100);
  TEST_ASSERT_EQUAL(1, presses);
}

// millis() wraps around every ~49 days.
void test_debounce_across_millis_wraparound() {
  Debouncer d(50);
  uint32_t now = 0xFFFFFFFFu - 20;
  int presses = hold(d, now, HIGH, 10) + hold(d, now, LOW, 100) + hold(d, now, HIGH, 100);
  TEST_ASSERT_EQUAL(1, presses);
}

// ---- Pass-through ----

// With pins 2-4 unconnected they read HIGH, and the lines must stay released,
// or the coinmech and control board can't talk.
void test_pass_through_idle_inputs_release_lines() {
  FakeHal hal;
  passThrough(HIGH, HIGH, HIGH, hal);
  TEST_ASSERT_EQUAL(3, hal.changes.size());
  for (const Change& c : hal.changes) TEST_ASSERT_FALSE(c.active);
}

void test_pass_through_active_input_activates_its_line() {
  FakeHal hal;
  passThrough(HIGH, LOW, HIGH, hal);
  TEST_ASSERT_FALSE(hal.changesOn(LINE_SEND)[0].active);
  TEST_ASSERT_TRUE(hal.changesOn(LINE_INTR)[0].active);
  TEST_ASSERT_FALSE(hal.changesOn(LINE_DATA)[0].active);
}

// ---- Recorder ----

// The bug we had: idle inputs at boot started a bogus recording.
void test_recorder_idle_inputs_record_nothing() {
  Recorder r;
  for (uint32_t t = 0; t < 2000000; t += 100) {
    TEST_ASSERT_EQUAL(Recorder::NOTHING, r.sample(HIGH, HIGH, HIGH, t));
  }
}

void test_recorder_records_time_between_changes() {
  Recorder r;
  r.sample(HIGH, HIGH, HIGH, 0);
  r.sample(LOW, HIGH, HIGH, 1000);   // Starts the recording.
  r.sample(HIGH, HIGH, HIGH, 3000);
  r.sample(LOW, HIGH, HIGH, 4500);
  TEST_ASSERT_EQUAL(Recorder::NOTHING, r.sample(LOW, HIGH, HIGH, 400000));
  TEST_ASSERT_EQUAL(Recorder::MESSAGE_DONE, r.sample(LOW, HIGH, HIGH, 502000));
  uint16_t expected[ARRAY_SIZE] = {0, 2000, 1500};
  TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, r.data[LINE_SEND], ARRAY_SIZE);
  r.reset();
  TEST_ASSERT_EQUAL_UINT16(0, r.data[LINE_SEND][1]);
}

void test_recorder_reports_overflow() {
  Recorder r;
  int level = HIGH;
  uint32_t t = 0;
  for (int k = 0; k < ARRAY_SIZE; k++) {
    level = !level;
    TEST_ASSERT_EQUAL(Recorder::NOTHING, r.sample(level, HIGH, HIGH, t += 100));
  }
  TEST_ASSERT_EQUAL(Recorder::OVERFLOW, r.sample(!level, HIGH, HIGH, t += 100));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_recordings_end_in_zero_and_have_no_gaps);
  RUN_TEST(test_replay_matches_recorded_times);
  RUN_TEST(test_replay_across_micros_wraparound);
  RUN_TEST(test_replays_fit_in_the_pis_shortest_pulse);
  RUN_TEST(test_no_quarters_lost_at_the_pis_fastest_timing);
  RUN_TEST(test_rising_edge_once_per_pulse);
  RUN_TEST(test_debounce_clean_press);
  RUN_TEST(test_debounce_bouncy_press_long_hold_bouncy_release);
  RUN_TEST(test_debounce_two_separate_presses);
  RUN_TEST(test_debounce_short_release_is_one_press);
  RUN_TEST(test_debounce_across_millis_wraparound);
  RUN_TEST(test_pass_through_idle_inputs_release_lines);
  RUN_TEST(test_pass_through_active_input_activates_its_line);
  RUN_TEST(test_recorder_idle_inputs_record_nothing);
  RUN_TEST(test_recorder_records_time_between_changes);
  RUN_TEST(test_recorder_reports_overflow);
  return UNITY_END();
}
