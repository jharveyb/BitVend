# BitVend

Most recent code for each system is:

Arduino - Arduino/platformio/src/vend_hack.cpp
RPi - vendnew.py
Misc - vendrun.sh (for cron job/ systemd service + collecting logs)

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

- Raspberry Pi: Use some API or other service to check a Bitcoin balance; if funds were
        received, communicate with the Arduino to grant a similar-enough credit on the
        vending machine.
- Arduino: Wait for messages from the Pi, and grant credit on the vending machine by
        replaying coin signal recordings.
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

TODO