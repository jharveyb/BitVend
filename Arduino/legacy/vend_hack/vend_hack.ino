#define IN_SEND_PIN 2
#define IN_INTR_PIN 3
#define IN_DATA_PIN 4

#define OUT_SEND_PIN 5
#define OUT_INTR_PIN 6
#define OUT_DATA_PIN 7

#define MAX_MESSAGE_LENGTH_MILLIS 500

void setup() {
  pinMode(IN_SEND_PIN, INPUT_PULLUP);
  pinMode(IN_INTR_PIN, INPUT_PULLUP);
  pinMode(IN_DATA_PIN, INPUT_PULLUP);

  pinMode(OUT_SEND_PIN, OUTPUT);
  pinMode(OUT_INTR_PIN, OUTPUT);
  pinMode(OUT_DATA_PIN, OUTPUT);

  Serial.begin(9600);
  Serial.println("Welcome!");
}

unsigned long epoch_micros;
boolean is_in_message = false;

unsigned long last_s_micros;
unsigned long last_i_micros;
unsigned long last_d_micros;



#define ARRAY_SIZE 20
unsigned int data_s[ARRAY_SIZE];
unsigned int data_i[ARRAY_SIZE];
unsigned int data_d[ARRAY_SIZE];


int last_s, last_i, last_d = HIGH;
int idx_s, idx_i, idx_d = 0;


void eraseArrays() {
  for (int i=0; i<ARRAY_SIZE; i++) {
    data_s[i]=0;
    data_i[i]=0;
    data_d[i]=0;
  }
}


void printArrays() {
  for (int i=0; i<ARRAY_SIZE; i++) {
    Serial.print(data_s[i]);
    Serial.print(",");
  }
  Serial.println();

  for (int i=0; i<ARRAY_SIZE; i++) {
    Serial.print(data_i[i]);
    Serial.print(",");
  }
  Serial.println();

  for (int i=0; i<ARRAY_SIZE; i++) {
    Serial.print(data_d[i]);
    Serial.print(",");
  }
  Serial.println();
  Serial.println("--------------------");

}

void fakeQuarter() {
  unsigned int s[] = {
    384,18048,3044,17116,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  unsigned int i[] = {
    0,44172,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  unsigned int d[] = {

  480,1688,1640,1692,6696,3380,5996,1692,1644,1732,6704,3324,1,0,0,0,0,0,0  };



  int index_s = 0;
  int index_i = 0;
  int index_d = 0;

  int s_state = HIGH;
  int i_state = HIGH;
  int d_state = HIGH;

  boolean s_done = false;
  boolean i_done = false;
  boolean d_done = false;

  unsigned long start = micros();

  unsigned long next_s = start+s[index_s++];
  unsigned long next_i = start+i[index_i++];
  unsigned long next_d = start+d[index_d++];
  unsigned long current;
 
  while (!s_done || !i_done || !d_done) {   
    current = micros();
      if (next_s<=current) {
        if (s[index_s]>0) {
          s_state=!s_state;
          next_s = next_s+s[index_s++];
          digitalWrite(OUT_SEND_PIN,!s_state);
        } 
        else {
          s_done = true;
        }  
      }

    if (next_i<=current) {
      if (i[index_i]>0) {
        i_state=!i_state;
        next_i = next_i+i[index_i++];
        digitalWrite(OUT_INTR_PIN,!i_state);
      } 
      else {
        i_done = true;
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
      }  
    }

  }
  Serial.print("Took (micros):");
  Serial.println(current-start);
  Serial.println("Fake Done!");
}


boolean callFake = true;

void loop() {
  unsigned long time = micros();

  int s=digitalRead(IN_SEND_PIN);
  int i=digitalRead(IN_INTR_PIN);
  int d=digitalRead(IN_DATA_PIN);

  digitalWrite(OUT_SEND_PIN, !s);
  digitalWrite(OUT_INTR_PIN, !i);
  digitalWrite(OUT_DATA_PIN, !d);

  if (!is_in_message && (s != last_s || i!= last_i || d != last_d)) {
    is_in_message = true;
    epoch_micros = time;
    last_s_micros = time;
    last_i_micros = time;
    last_d_micros = time;
  }

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

  if (idx_s > ARRAY_SIZE || idx_i > ARRAY_SIZE || idx_d > ARRAY_SIZE) {
    Serial.println("HALTING!");
    while (true) {
      delay(500);
      Serial.println("HALTED.");
    }
  }

  last_s = s;
  last_i = i;
  last_d = d;

  if (is_in_message && (((time - epoch_micros)/1000) > MAX_MESSAGE_LENGTH_MILLIS)) {
    is_in_message = false;
    printArrays();
    idx_s = 0;
    idx_i = 0;
    idx_d = 0;
    eraseArrays();
  }
  
  if (callFake && time>3300000) {
    Serial.println("Calling Fake");
    fakeQuarter();
    callFake=false;
  }

}

