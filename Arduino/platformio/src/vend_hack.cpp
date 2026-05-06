#include <Arduino.h>

// Pins used to record coinmech signals.
#define IN_SEND_PIN 2
#define IN_INTR_PIN 3
#define IN_DATA_PIN 4

// Pins used to replay coinmech signals.
#define OUT_SEND_PIN 5
#define OUT_INTR_PIN 6
#define OUT_DATA_PIN 7

// Pin used by the Raspberry Pi to trigger coin replay.
#define IN_RASPI 10

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

void eraseArrays();
void printArrays();
void fakeQuarter(unsigned int s[], unsigned int i[], unsigned int d[]);
void dollar();

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

  pinMode(OUT_SEND_PIN, OUTPUT);
  pinMode(OUT_INTR_PIN, OUTPUT);
  pinMode(OUT_DATA_PIN, OUTPUT);

  pinMode(IN_RASPI, INPUT);

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

  unsigned long next_s = start+s[index_s++];
  unsigned long next_i = start+i[index_i++];
  unsigned long next_d = start+d[index_d++];
  unsigned long current;

  // Replay the recorded coinmech signals, until the end of the longest signal.
  while (!s_done || !i_done || !d_done) {
    current = micros();
      if (next_s<=current) {
        // >0 check implies that the 0 values in the recorded signals are special?
        if (s[index_s]>0) {
          // Write out the current wire state, and store the inverted state for the
          // next transition. So the first write will be a rising edge.
          s_state=!s_state;
          next_s = next_s+s[index_s++];
          digitalWrite(OUT_SEND_PIN,!s_state);
        }
        else {
          // Done replaying a signal, set the output low.
          s_done = true;
          digitalWrite(OUT_SEND_PIN,LOW);
        }
      }

      // For interrupt, are we holding the line low, and then sending a single pulse?
      // Seems surprising, but the first value in the recorded signal is 0.
      // Maybe this is handled by the other logic in loop().
    if (next_i<=current) {
      if (i[index_i]>0) {
        i_state=!i_state;
        next_i = next_i+i[index_i++];
        digitalWrite(OUT_INTR_PIN,!i_state);
      }
      else {
        i_done = true;
        digitalWrite(OUT_INTR_PIN,LOW);
      }
    }

    if (next_d<=current) {
      if (d[index_d]>0) {
        d_state=!d_state;
        next_d = next_d+d[index_d++];
        digitalWrite(OUT_DATA_PIN,!d_state);
      }
      else {
        d_done = true;
        digitalWrite(OUT_DATA_PIN,LOW);
      }
    }

  }
  Serial.print("Took (micros):");
  Serial.println(current-start);
  Serial.println("Fake Done!");
}

// Replay four coinmech signals to 'send' a dollar to the machine.
// TODO: Why is there such a large delay? Is it important to use different quarter signals?
void dollar() {
    fakeQuarter(QUARTER1_S,QUARTER1_I,QUARTER1_D);
    delay(500);
    fakeQuarter(QUARTER2_S,QUARTER2_I,QUARTER2_D);
    delay(500);
    fakeQuarter(QUARTER3_S,QUARTER3_I,QUARTER3_D);
    delay(500);
    fakeQuarter(QUARTER4_S,QUARTER4_I,QUARTER4_D);
}

void loop() {
  unsigned long time = micros();

  // Invert the inputs from the coinmech.
  // TODO: Why?
  int s=digitalRead(IN_SEND_PIN);
  int i=digitalRead(IN_INTR_PIN);
  int d=digitalRead(IN_DATA_PIN);

  digitalWrite(OUT_SEND_PIN, !s);
  digitalWrite(OUT_INTR_PIN, !i);
  digitalWrite(OUT_DATA_PIN, !d);

  // If we aren't currently recording a signal, and the level on any coinmech
  // wire changed, start recording by setting an initial timestamp.
  if (!is_in_message && (s != last_s || i!= last_i || d != last_d)) {
    is_in_message = true;
    epoch_micros = time;
    last_s_micros = time;
    last_i_micros = time;
    last_d_micros = time;
  }

  // For every level change on each wire, record the time since the start of recording.
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

  // Main functionality; replay coin signals based on a signal from the Raspberry Pi.
  if (digitalRead(IN_RASPI)==HIGH) {
    dollar();
  }
}
