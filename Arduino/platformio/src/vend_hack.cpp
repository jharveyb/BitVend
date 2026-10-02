#include <Arduino.h>
#include <vend.h>

// The coin replay logic lives in lib/vend/ (vend.h, vend.cpp), so it can be
// unit tested on a PC. This file connects it to the pins and Serial.

// Pins used to record coinmech signals.
#define IN_SEND_PIN 2
#define IN_INTR_PIN 3
#define IN_DATA_PIN 4

// Pins used to replay coinmech signals. These are wired directly in parallel
// with the coinmech -> control board lines, which idle at 5 V (pulled up by the
// machine) and are active at 0 V. So we never drive them HIGH: driveLine()
// (in vend.h) either pulls a line to 0 V or lets go of it.
#define OUT_SEND_PIN 5
#define OUT_INTR_PIN 6
#define OUT_DATA_PIN 7

// Pin used by the Raspberry Pi to trigger coin replay.
// Protocol: each LOW->HIGH edge on this pin means "insert one quarter".
// By default the Pi holds the line HIGH for 100 ms, then LOW for 100 ms, per
// quarter; the minimum is PI_MIN_HIGH_MS / PI_MIN_LOW_MS (see vend.h). The
// replayed recordings (Q1-Q3) take up to ~48 ms, so a replay finishes while
// the line is still HIGH.
#define IN_RASPI 10

// Pull to GND by hand (jumper wire) to replay one quarter, for bench testing.
#define IN_DEBUG 9
// A new level on IN_DEBUG must hold this long before we believe it. A jumper
// wire flickers for a few ms when it touches or leaves GND (contact bounce).
#define DEBUG_DEBOUNCE_MS 50

// Output pin for each coinmech line, indexed by LINE_SEND/INTR/DATA.
const int OUT_PINS[3] = { OUT_SEND_PIN, OUT_INTR_PIN, OUT_DATA_PIN };

// The real hardware for the logic in vend.h.
class ArduinoHal : public Hal {
 public:
  uint32_t micros() override { return ::micros(); }
  void setLine(int line, bool active) override { driveLine(OUT_PINS[line], active); }
};

ArduinoHal hal;
RisingEdge raspi;
Debouncer debug(DEBUG_DEBOUNCE_MS);

// Which recording to replay next.
int quarter_idx = 0;

// Recording coinmech signals (to make new recordings) is off by default. Build
// with -D RECORD_SIGNALS to turn it on: pio run -e ATmega328P_record
#ifdef RECORD_SIGNALS
Recorder recorder;

void(* resetFunc) (void) = 0;
#endif

void setup() {
  pinMode(IN_SEND_PIN, INPUT_PULLUP);
  pinMode(IN_INTR_PIN, INPUT_PULLUP);
  pinMode(IN_DATA_PIN, INPUT_PULLUP);

  driveLine(OUT_SEND_PIN, false);
  driveLine(OUT_INTR_PIN, false);
  driveLine(OUT_DATA_PIN, false);

  pinMode(IN_RASPI, INPUT);
  pinMode(IN_DEBUG, INPUT_PULLUP);

  Serial.begin(9600);
#ifdef RECORD_SIGNALS
  Serial.println("Welcome! (recording signals)");
#else
  Serial.println("Welcome!");
#endif
}

#ifdef RECORD_SIGNALS
// Print one line of the recording as a C array, ready to paste into vend.cpp.
void printArray(const char* name, const uint16_t* data) {
  Serial.print("uint16_t ");
  Serial.print(name);
  Serial.print("[] = { ");
  for (int k=0; k<ARRAY_SIZE; k++) {
    Serial.print(data[k]);
    if (k<ARRAY_SIZE-1) { Serial.print(","); }
  }
  Serial.println(" }");
}

void printArrays() {
  printArray("s", recorder.data[LINE_SEND]);
  printArray("i", recorder.data[LINE_INTR]);
  printArray("d", recorder.data[LINE_DATA]);
  Serial.println("--------------------");
}

// Record the coinmech signals read in loop().
void recordSignals(int s, int i, int d, unsigned long time) {
  Recorder::Result result = recorder.sample(s, i, d, time);
  if (result == Recorder::OVERFLOW) {
    Serial.println("UNEXPECTED!");
    resetFunc();
  }
  if (result == Recorder::MESSAGE_DONE) {
    printArrays();
    recorder.reset();
  }
}
#endif // RECORD_SIGNALS

// Replay one recorded quarter, cycling through the replayed recordings.
void quarter() {
  uint32_t took = replay(RECORDINGS[quarter_idx], hal);
  Serial.print("Took (micros):");
  Serial.println(took);
  Serial.println("Fake Done!");
  quarter_idx = (quarter_idx + 1) % REPLAYED_COUNT;
}

void loop() {
  // Copy the coinmech inputs to the outputs. With pins 2-4 unconnected they
  // read HIGH (pull-up), so this just keeps the outputs let go.
  int s=digitalRead(IN_SEND_PIN);
  int i=digitalRead(IN_INTR_PIN);
  int d=digitalRead(IN_DATA_PIN);
  passThrough(s, i, d, hal);

#ifdef RECORD_SIGNALS
  recordSignals(s, i, d, micros());
#endif

  // Main functionality; replay one quarter for each rising edge from the Raspberry Pi.
  if (raspi.update(digitalRead(IN_RASPI))) {
    quarter();
  }

  // Bench testing; replay one quarter each time IN_DEBUG is pulled to GND.
  if (debug.update(digitalRead(IN_DEBUG), millis())) {
    Serial.println("Debug: quarter");
    quarter();
  }
}
