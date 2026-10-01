# vend_hack — PlatformIO

Firmware for the BitVend Arduino Uno (ATmega328P). The Uno watches digital
pin 10 (`IN_RASPI`) for pulses from the Raspberry Pi. On each rising edge it
replays one pre-recorded coin-acceptor pulse train, which fakes one quarter
being inserted into the vending machine. It cycles through the four
recordings.

Communication with the Pi is **GPIO-to-GPIO**, not serial. `Serial` (9600
baud) is used only for debug output and capture-mode array dumps.

## Pi → Arduino protocol

**One LOW→HIGH edge = one quarter.** For each quarter, the Pi holds the line
HIGH for 100 ms, then LOW for the rest of the quarter period (900 ms by default).
The line idles LOW. A replay takes up to ~85 ms, and pin 10 isn't watched during
a replay, so the HIGH must outlast it. The firmware's minimum period is 150 ms
(100 ms HIGH + 50 ms LOW). The Pi side is `RPi/bitvend/src/coin_signal.rs`, and
its tests enforce these limits.

The Pi drives 3.3 V into a 5 V input. That is above the ATmega's 3.0 V HIGH
threshold, but only just, so a level shifter is advisable.

## Pin map

| Pin | Direction | Name           | Wired to                                   |
|-----|-----------|----------------|--------------------------------------------|
| 2   | IN        | `IN_SEND_PIN`  | Coin acceptor SEND line                    |
| 3   | IN        | `IN_INTR_PIN`  | Coin acceptor INTERRUPT line               |
| 4   | IN        | `IN_DATA_PIN`  | Coin acceptor DATA line                    |
| 5   | OUT (OC)  | `OUT_SEND_PIN` | SEND line, in parallel with coin mech      |
| 6   | OUT (OC)  | `OUT_INTR_PIN` | INTERRUPT line, in parallel with coin mech |
| 7   | OUT (OC)  | `OUT_DATA_PIN` | DATA line, in parallel with coin mech      |
| 9   | IN        | `IN_DEBUG`     | Nothing; touch to GND to test (see below)  |
| 10  | IN        | `IN_RASPI`     | Pi physical pin 12 (BCM 18), configurable  |

Pins 2, 3, & 4 were used for recording the pulses that represent different coins,
so they are not needed for a 'production' deployment where we are only replaying
coins.

"OC" = open collector. Pins 5–7 connect directly to the coin mech → control
board lines, which idle at 5 V and are active at 0 V. The firmware never drives
them HIGH. It either pulls a line to 0 V (OUTPUT LOW) or lets go of it (INPUT),
just like the coin mech does (`driveLine()` in `vend_hack.cpp`). The original
hardware had a transistor per line doing this; driving these pins as normal
outputs holds the lines active and stops real coins working too.

## Wiring the Pi

- **Pi GND must connect to Arduino GND.** The Arduino reads pin 10 relative to its
  own ground (the vending machine's), so without a shared ground the Pi's signal
  has no reference. Pi physical pin 14 is a GND pin, two pins from pin 12.
- Fit a 10 kΩ resistor from pin 10 to GND, so pin 10 doesn't float (and trigger
  quarters) when the Pi is off or unplugged.

## Bench testing

Pin 9 (`IN_DEBUG`) has the internal pull-up enabled. Touch a jumper wire from
pin 9 to GND to replay one quarter, using the same code path as a Pi pulse. The
serial monitor shows `Debug: quarter` followed by one `Took (micros):` line.
The pin must hold a new level for 50 ms before it counts, so the flicker as the
wire touches or leaves GND doesn't add extra quarters; one touch-and-release is
one quarter.
Consecutive presses cycle through the four recordings.

## Tuning speed

The Pi sets how fast quarters are sent. Set `BITVEND_QUARTER_PERIOD_MS` in
`/etc/bitvend.env` on the Pi (default 1000, minimum 150) and restart the
service; no reflash is needed. The vending machine's own limit is unknown, so
step the value down, pay $1.00 at each step, and keep the lowest value where the
credit display always reaches $1.00.

## Build / upload / monitor

Install PlatformIO Core (one-time):

```sh
pipx install platformio   # or: pip install platformio

# Check that a plugged-in board is detected
pio device list
```

From this directory (`Arduino/platformio/`):

```sh
pio run                   # compile
pio run -t upload         # compile + flash the Uno (auto-detects /dev/ttyUSB* or /dev/ttyACM*)
pio run -e ATmega328P --target upload # compile for a specific target
pio device monitor        # serial monitor at 9600 baud
```

To target a specific port: `pio run -t upload --upload-port /dev/ttyUSB0`.

## Source

- [src/vend_hack.cpp](src/vend_hack.cpp) — full firmware. Ported from
  [../vend_hack.ino](../vend_hack.ino) with `boolean` → `bool` and explicit
  forward declarations. Lives as a `.cpp` (not `.ino`) so VSCode's C/C++
  extension treats it as a first-class translation unit.
