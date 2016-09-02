/*
 Notes:
 */
// constants won't change. They're used here to
// set pin numbers:
const int PiPin = 2;     // the number of the pin with the Pi input
const int Relay1 =  8;   // the number of the pin for send, keep on during session
const int Relay2 = 9;    // pin for interrupt signal out
const int Relay3 = 10;   // pin for data out

const int sendpin =  5;  //pins for output
const int interruptpin =  6;
const int datapin =  7;

int PiState = 0;  // variable for reading the Pi status
int pin1 = 0;  //temp variables
int pin2 = 0;
int pin3 = 0;

void setup() {
  // initialize the relay pins as outputs:
  pinMode(Relay1, INPUT);
  pinMode(Relay2, INPUT);
  pinMode(Relay3, INPUT);
  
  pinMode(sendpin, OUTPUT);
  pinMode(interruptpin, OUTPUT);
  pinMode(datapin, OUTPUT);
  
  // initialize the Pi pin as an input:
  pinMode(PiPin, INPUT);
  Serial.begin(9600);
}

void loop() {
  // read the state of the Pi input:
  //PiState = digitalRead(PiPin);

  // check if the pushbutton is pressed.
  // if it is, the buttonState is HIGH:
  /*
  if (PiState == HIGH) {
    // start quarter sequence
    Serial.print("beginning to vend");
    digitalWrite(Relay1, HIGH);
    digitalWrite(Relay2, HIGH);
    digitalWrite(Relay3, HIGH);
    // Cutting Arduino in
    digitalWrite(Relay2, HIGH);  //cut out send line
    digitalWrite(Relay1, HIGH);  //begin interrupt signal
    delay(20);
    digitalWrite(Relay1, LOW);  //blip
    digitalWrite(Relay1, HIGH);
    delay(20);
    digitalWrite(Relay1, LOW);
    Serial.print("interrupt done");
    //end interrupt signal
    delay(20);  //wait for send signal
    digitalWrite(Relay3, HIGH);  //begin data sending
    delay(10);
    digitalWrite(Relay3, LOW);  //blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //second blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //third blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    Serial.print("data done");
    digitalWrite(Relay1, LOW);  //reset relays
    digitalWrite(Relay2, LOW);
    digitalWrite(Relay3, LOW);
    delay(20);  //delay until next transmission
    Serial.print("beginning to vend");
    digitalWrite(Relay2, HIGH);  //cut out send line
    digitalWrite(Relay1, HIGH);  //begin interrupt signal
    delay(20);
    digitalWrite(Relay1, LOW);  //blip
    digitalWrite(Relay1, HIGH);
    delay(20);
    digitalWrite(Relay1, LOW);
    Serial.print("interrupt done");
    //end interrupt signal
    delay(20);  //wait for send signal
    digitalWrite(Relay3, HIGH);  //begin data sending
    delay(10);
    digitalWrite(Relay3, LOW);  //blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //second blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //third blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    Serial.print("data done");
    digitalWrite(Relay1, LOW);  //reset relays
    digitalWrite(Relay2, LOW);
    digitalWrite(Relay3, LOW);
    delay(20);  //delay until next transmission
    Serial.print("beginning to vend");
    digitalWrite(Relay2, HIGH);  //cut out send line
    digitalWrite(Relay1, HIGH);  //begin interrupt signal
    delay(20);
    digitalWrite(Relay1, LOW);  //blip
    digitalWrite(Relay1, HIGH);
    delay(20);
    digitalWrite(Relay1, LOW);
    Serial.print("interrupt done");
    //end interrupt signal
    delay(20);  //wait for send signal
    digitalWrite(Relay3, HIGH);  //begin data sending
    delay(10);
    digitalWrite(Relay3, LOW);  //blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //second blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //third blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    Serial.print("data done");
    digitalWrite(Relay1, LOW);  //reset relays
    digitalWrite(Relay2, LOW);
    digitalWrite(Relay3, LOW);
    delay(20);  //delay until next transmission
    Serial.print("beginning to vend");
    digitalWrite(Relay2, HIGH);  //cut out send line
    digitalWrite(Relay1, HIGH);  //begin interrupt signal
    delay(20);
    digitalWrite(Relay1, LOW);  //blip
    digitalWrite(Relay1, HIGH);
    delay(20);
    digitalWrite(Relay1, LOW);
    Serial.print("interrupt done");
    //end interrupt signal
    delay(20);  //wait for send signal
    digitalWrite(Relay3, HIGH);  //begin data sending
    delay(10);
    digitalWrite(Relay3, LOW);  //blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //second blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    digitalWrite(Relay3, LOW);  //third blip
    digitalWrite(Relay3, HIGH);
    delay(5);
    Serial.print("data done");
    digitalWrite(Relay1, LOW);  //reset relays
    digitalWrite(Relay2, LOW);
    digitalWrite(Relay3, LOW);
    delay(20);  //delay until next transmission
  } else {
   */
    //Serial.print("not signalling");
  pin1=digitalRead(Relay1);
  pin2=digitalRead(Relay2);
  pin3=digitalRead(Relay3);
  digitalWrite(sendpin, pin1);
  digitalWrite(interruptpin, pin2);
  digitalWrite(datapin, pin3);
    /*
    digitalWrite(Relay1, LOW);
    digitalWrite(Relay2, LOW);
    digitalWrite(Relay3, LOW);
     */
  //}
}
