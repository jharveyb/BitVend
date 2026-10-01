#include <Arduino.h>

// Pins used to record coinmech signals.
#define IN_SEND_PIN 2
#define IN_INTR_PIN 3
#define IN_DATA_PIN 4

// Pins used to replay coinmech signals. These are wired directly in parallel
// with the coinmech -> control board lines, which idle at 5 V (pulled up by the
// machine) and are active at 0 V. So we never drive them HIGH: driveLine()
// either pulls a line to 0 V or lets go of it (see below).
#define OUT_SEND_PIN 5
#define OUT_INTR_PIN 6
#define OUT_DATA_PIN 7

// Pin used by the Raspberry Pi to trigger coin replay.
// Protocol: each LOW->HIGH edge on this pin means "insert one quarter".
// By default the Pi holds the line HIGH for 100 ms, then LOW for 100 ms, per
// quarter; the minimum is 60 ms HIGH / 40 ms LOW (see Timing in
// RPi/bitvend/src/coin_signal.rs). The replayed recordings (Q1-Q3) take up to
// ~48 ms, so fakeQuarter() finishes while the line is still HIGH.
#define IN_RASPI 10

// Pull to GND by hand (jumper wire) to replay one quarter, for bench testing.
#define IN_DEBUG 9
// A new level on IN_DEBUG must hold this long before we believe it. A jumper
// wire flickers for a few ms when it touches or leaves GND (contact bounce).
#define DEBUG_DEBOUNCE_MS 50

#define MAX_MESSAGE_LENGTH_MILLIS 500

// Four coinmech signals we recorded that represent a quarter.
unsigned int QUARTER1_S[] = { 1764,18004,3076,17104,0 };
unsigned int QUARTER1_I[] = { 0,45508,0 };
unsigned int QUARTER1_D[] = { 1856,6696,5020,3392,5972,6744,5012,3352,0 };
//--------------------
unsigned int QUARTER2_S[] = { 3848,18052,3076,17104,0 };
unsigned int QUARTER2_I[] = { 0,47640,0 };
unsigned int QUARTER2_D[] = { 3940,6696,5020,3392,6020,6744,5012,3352,0 };
//--------------------
unsigned int QUARTER3_S[] = { 3080,18096,3076,17100,0 };
unsigned int QUARTER3_I[] = { 0,46912,0 };
unsigned int QUARTER3_D[] = { 3124,6744,5020,3392,6064,6740,5020,3348,0 };
//--------------------
unsigned int QUARTER4_S[] = { 456,18096,3076,17100,0 };
unsigned int QUARTER4_I[] = { 0,44292,0 };
unsigned int QUARTER4_D[] = { 500,6744,5020,3392,6064,6736,5020,3352,33504,1720,3348,1676,5020,3348,0 };

// The recordings above as a table, so we can cycle through them one quarter at a time.
unsigned int* QUARTERS[4][3] = {
  { QUARTER1_S, QUARTER1_I, QUARTER1_D },
  { QUARTER2_S, QUARTER2_I, QUARTER2_D },
  { QUARTER3_S, QUARTER3_I, QUARTER3_D },
  { QUARTER4_S, QUARTER4_I, QUARTER4_D },
};
// Which recording to replay next.
int quarter_idx = 0;

// Last level seen on IN_RASPI, used to detect rising edges.
int last_raspi = LOW;

// IN_DEBUG debouncing: the level we believe, the last raw reading, and when
// the raw reading last changed.
int debug_stable = HIGH;
int debug_raw = HIGH;
unsigned long debug_changed_ms = 0;

void eraseArrays();
void printArrays();
void fakeQuarter(unsigned int s[], unsigned int i[], unsigned int d[]);
void quarter();

// Acts like an open-collector transistor on a coinmech line. The original
// hardware had one per line (Arduino HIGH = pull the line to 0 V), which is why
// the rest of this code passes "active" as the inverse of the recorded level.
// - active:   OUTPUT LOW, pulls the line to 0 V.
// - inactive: INPUT without pull-up, lets the machine hold the line at 5 V.
void driveLine(int pin, bool active) {
  if (active) {
    digitalWrite(pin, LOW);  // Set LOW first, so the pin never outputs HIGH.
    pinMode(pin, OUTPUT);
  } else {
    pinMode(pin, INPUT);
  }
}

void(* resetFunc) (void) = 0;

// Timestamp we started recording coinmech signals.
unsigned long epoch_micros;

// Flag for ongoing signal recording.
bool is_in_message = false;

// Timestamps for level transitions on each coinmech wire.
unsigned long last_s_micros;
unsigned long last_i_micros;
unsigned long last_d_micros;

// List of timestamps that represent a captured coinmech message.
#define ARRAY_SIZE 20
unsigned int data_s[ARRAY_SIZE];
unsigned int data_i[ARRAY_SIZE];
unsigned int data_d[ARRAY_SIZE];

// Starting state we compare each coinmech wire to; so we're watching for a falling edge (?).
int last_s, last_i, last_d = HIGH;
int idx_s, idx_i, idx_d = 0;

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
  Serial.println("Welcome!");
}

void eraseArrays() {
  for (int i=0; i<ARRAY_SIZE; i++) {
    data_s[i]=0;
    data_i[i]=0;
    data_d[i]=0;
  }
}

void printArrays() {
  Serial.print("unsigned int s[] = { ");
  for (int i=0; i<ARRAY_SIZE; i++) {
    Serial.print(data_s[i]);
    if (i<ARRAY_SIZE-1) { Serial.print(","); }
  }
  Serial.println(" }");

  Serial.print("unsigned int i[] = { ");
  for (int i=0; i<ARRAY_SIZE; i++) {
    Serial.print(data_i[i]);
    if (i<ARRAY_SIZE-1) { Serial.print(","); }
  }
  Serial.println(" }");

  Serial.print("unsigned int d[] = { ");
  for (int i=0; i<ARRAY_SIZE; i++) {
    Serial.print(data_d[i]);
    if (i<ARRAY_SIZE-1) { Serial.print(","); }
  }
  Serial.println(" }");
  Serial.println("--------------------");
}

// Replay the recording of coinmech signals that represent a quarter.
void fakeQuarter(unsigned int s[], unsigned int i[], unsigned int d[]) {
  int index_s = 0;
  int index_i = 0;
  int index_d = 0;

  int s_state = HIGH;
  int i_state = HIGH;
  int d_state = HIGH;

  bool s_done = false;
  bool i_done = false;
  bool d_done = false;

  unsigned long start = micros();

  // Times are kept relative to start: micros() wraps around every ~71 min, and
  // comparing elapsed times (unsigned subtraction) still works across the wrap.
  unsigned long next_s = s[index_s++];
  unsigned long next_i = i[index_i++];
  unsigned long next_d = d[index_d++];
  unsigned long current;

  // Replay the recorded coinmech signals, until the end of the longest signal.
  while (!s_done || !i_done || !d_done) {
    current = micros() - start;
      if (next_s<=current) {
        // >0 check implies that the 0 values in the recorded signals are special?
        if (s[index_s]>0) {
          // Write out the current wire state, and store the inverted state for the
          // next transition. So the first write will be a rising edge.
          s_state=!s_state;
          next_s = next_s+s[index_s++];
          driveLine(OUT_SEND_PIN,!s_state);
        }
        else {
          // Done replaying a signal, let go of the line.
          s_done = true;
          driveLine(OUT_SEND_PIN,false);
        }
      }

      // For interrupt, are we holding the line low, and then sending a single pulse?
      // Seems surprising, but the first value in the recorded signal is 0.
      // Maybe this is handled by the other logic in loop().
    if (next_i<=current) {
      if (i[index_i]>0) {
        i_state=!i_state;
        next_i = next_i+i[index_i++];
        driveLine(OUT_INTR_PIN,!i_state);
      }
      else {
        i_done = true;
        driveLine(OUT_INTR_PIN,false);
      }
    }

    if (next_d<=current) {
      if (d[index_d]>0) {
        d_state=!d_state;
        next_d = next_d+d[index_d++];
        driveLine(OUT_DATA_PIN,!d_state);
      }
      else {
        d_done = true;
        driveLine(OUT_DATA_PIN,false);
      }
    }

  }
  Serial.print("Took (micros):");
  Serial.println(current);
  Serial.println("Fake Done!");
}

// Replay one recorded quarter, cycling through the four recordings.
void quarter() {
  fakeQuarter(QUARTERS[quarter_idx][0], QUARTERS[quarter_idx][1], QUARTERS[quarter_idx][2]);
  // Actually, ignore the fourth quarter since its longer than the others.
  quarter_idx = (quarter_idx + 1) % 3;
}

void loop() {
  unsigned long time = micros();

  // Copy the coinmech inputs to the outputs: a line reading 0 V (active) is
  // pulled to 0 V on the output side too. With pins 2-4 unconnected they read
  // HIGH (pull-up), so this just keeps the outputs let go.
  int s=digitalRead(IN_SEND_PIN);
  int i=digitalRead(IN_INTR_PIN);
  int d=digitalRead(IN_DATA_PIN);

  driveLine(OUT_SEND_PIN, !s);
  driveLine(OUT_INTR_PIN, !i);
  driveLine(OUT_DATA_PIN, !d);

  // If we aren't currently recording a signal, and the level on any coinmech
  // wire changed, start recording by setting an initial timestamp.
  if (!is_in_message && (s != last_s || i!= last_i || d != last_d)) {
    is_in_message = true;
    epoch_micros = time;
    last_s_micros = time;
    last_i_micros = time;
    last_d_micros = time;
  }

  // For every level change on each wire, record the time since that wire's previous change.
  if (s != last_s) {
    data_s[idx_s++] = (time-last_s_micros);
    last_s_micros = time;
  }

  if (i != last_i) {
    data_i[idx_i++] = (time-last_i_micros);
    last_i_micros = time;
  }

  if (d != last_d) {
    data_d[idx_d++] = (time-last_d_micros);
    last_d_micros = time;
  }

  // Limit the number of level changes we'll record.
  if (idx_s > ARRAY_SIZE || idx_i > ARRAY_SIZE || idx_d > ARRAY_SIZE) {
    Serial.println("UNEXPECTED!");
    resetFunc();
  }

  last_s = s;
  last_i = i;
  last_d = d;

  // Limit the duration of a signal recording. Print whatever we've recorded and
  // reset the recording state.
  if (is_in_message && (((time - epoch_micros)/1000) > MAX_MESSAGE_LENGTH_MILLIS)) {
    is_in_message = false;
    printArrays();
    idx_s = 0;
    idx_i = 0;
    idx_d = 0;
    eraseArrays();
  }

  // Main functionality; replay one quarter for each rising edge from the Raspberry Pi.
  int raspi = digitalRead(IN_RASPI);
  if (raspi == HIGH && last_raspi == LOW) {
    quarter();
  }
  last_raspi = raspi;

  // Bench testing; replay one quarter each time IN_DEBUG is pulled to GND.
  int raw = digitalRead(IN_DEBUG);
  if (raw != debug_raw) {
    debug_raw = raw;
    debug_changed_ms = millis();
  }
  if (raw != debug_stable && millis() - debug_changed_ms >= DEBUG_DEBOUNCE_MS) {
    debug_stable = raw;
    if (debug_stable == LOW) {
      Serial.println("Debug: quarter");
      quarter();
    }
  }
}
