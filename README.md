# BitVend

Most recent code for each system is:

Arduino - Arduino/platformio/src/vend_hack.cpp
RPi - RPi/bitvend/ (Rust service; see RPi/bitvend/SETUP.md). The old Python 2 scripts are in RPi/legacy/.

Docs for the vending machine itself are all in docs/ - CONLUX for the billmech, tcr6xxx for the coinmech,
and the other two for the rest of the machine, including the control board.

## System Overview

The original vending machine components all seem to work. The Bitcoin acceptance
works by performing a 'replay attack' on the coin mech, the TRC6010. It communicates
with the vending machine 'motherboard' via a 12-pin connector
( `./manuals/Vending Machine Manual w Schematic.pdf, page 40 / last page, connector J7).

This 12-pin connection has 3 main wires for communication between the two components;
Send, Interrupt, and Data. These wires seem to be running at 5 V. Note that these pins
have two different numbers, depending on which connector you're referencing. In the trc6xxx manual,
they are pins 3, 4, and 5 ( `./manuals/trc6xxx_operational_service_guide.pdf, page 10).
But in the full Vending Machine manual, those pins are mapped to different pin numbers
on the J7 header. Effectively, the wire ordering is 'shuffled' on the TRC-6xxx connector vs.
the vending machine motherboard. I would recommend just using the pin numbers from the trc6xxx manual,
but be very mindful of the actual pin placement on that cable.

For the 'replay attack', we previously recorded the timing of voltage state transitions
on these wires when a quarter was inserted into the coin mech.
These recordings are stored at the top of `vend_hack.cpp`.

The responsibilities of each component are:

- Raspberry Pi: Receive Lightning payments to a static address (Lexe wallet), convert
        each payment to USD, round up to the next quarter, and pulse a GPIO pin once
        per quarter.
- Arduino: For each pulse from the Pi, grant one quarter of credit on the vending
        machine by replaying a coin signal recording.
- Vending Machine: Accurately list prices for items, and vend them. Can optionally
        provide change.

## Electrical

You probably want to power the Arduino from the coinmech -> motherboard connector / pins.
This is a simple way to make sure the Arduino is using a suitable ground reference.
For the coinmech pins:

- Pin 10 is ground and pin 12 is 24V (high).
- Pin 2 does not seem to actually be tied to pin 10.
- Pin 1 is indeed 5V high.
- Pins 3, 4, and 5 are 5V, and held high by default.
- An Arduino Uno (or similar) can run off of the 5V power available on this connector.

There is an ON/OFF switch behind the bill acceptor; that is for the whole vending machine.

## Accepting Funds

Customers pay over Lightning to a static Lightning Address / BIP353 address
(`₿name@lexe.app`), shown as a QR code on the machine. The Pi runs `RPi/bitvend`, a
small Rust service built on the [Lexe](https://docs.lexe.tech) wallet SDK, which:

1. Waits for incoming Lightning payments.
2. Converts each one to USD at the current BTC price (Coinbase, falling back to Kraken).
3. Rounds up to the next quarter. Payments under 25¢ are ignored.
4. Pulses Pi physical pin 12 (BCM 18) once per quarter; this pin is wired to Arduino pin 10.

The Pi holds only receive-only Lexe credentials; the wallet's seed phrase stays on an
admin machine. See `RPi/bitvend/SETUP.md` for setup, deployment and tests, and
`Arduino/platformio/README.md` for the pulse protocol.
