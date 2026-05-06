# vend_hack — PlatformIO

Firmware for the BitVend Arduino Uno (ATmega328P). The Uno listens for a
HIGH pulse from the Raspberry Pi on digital pin 10 (`IN_RASPI`) and replays
four pre-recorded coin-acceptor pulse trains to fake quarters being
inserted into the vending machine.

Communication with the Pi is **GPIO-to-GPIO**, not serial. `Serial` (9600
baud) is used only for debug output and capture-mode array dumps.

## Pin map

| Pin | Direction | Name           | Wired to                                   |
|-----|-----------|----------------|--------------------------------------------|
| 2   | IN        | `IN_SEND_PIN`  | Coin acceptor SEND line                    |
| 3   | IN        | `IN_INTR_PIN`  | Coin acceptor INTERRUPT line               |
| 4   | IN        | `IN_DATA_PIN`  | Coin acceptor DATA line                    |
| 5   | OUT       | `OUT_SEND_PIN` | Vending machine SEND input (inverted)      |
| 6   | OUT       | `OUT_INTR_PIN` | Vending machine INTERRUPT input (inverted) |
| 7   | OUT       | `OUT_DATA_PIN` | Vending machine DATA input (inverted)      |
| 10  | IN        | `IN_RASPI`     | Raspberry Pi GPIO pin 12 (BOARD numbering) |

Pins 2, 3, & 4 were used for recording the pulses that represent different coins,
so they are not needed for a 'production' deployment where we are only replaying
coins.

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
