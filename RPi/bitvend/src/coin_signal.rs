//! Telling the Arduino to insert quarters, over a single GPIO wire.
//!
//! Protocol: every LOW→HIGH edge on the wire means "insert one quarter". For
//! each quarter we hold the line HIGH for 100 ms, then LOW for the rest of the
//! period (900 ms by default). The Arduino (`Arduino/platformio/src/vend_hack.cpp`)
//! replays one recorded coin mech signal per edge, which takes up to ~85 ms.
//! The line always idles LOW.
//!
//! The firmware's limits are [`Timing::MIN_HIGH`] and [`Timing::MIN_LOW`]. How
//! fast the vending machine itself accepts coins is found by testing on the
//! machine, via `BITVEND_QUARTER_PERIOD_MS`.
//!
//! Wiring: Pi BCM GPIO 18 (physical pin 12) → Arduino pin 10. The Pi pin can
//! be changed with `BITVEND_GPIO_PIN` (see main.rs).

use std::{
    thread::sleep,
    time::{Duration, Instant},
};

use rppal::gpio::Level;

/// Where the pulses go.
pub enum Output {
    /// The real Raspberry Pi GPIO pin.
    Gpio(rppal::gpio::OutputPin),
    /// Only log each change, for running without hardware (`BITVEND_GPIO=fake`).
    Log,
    /// Record each change and when it happened, for tests.
    Record(Vec<(bool, Instant)>),
}

impl Output {
    fn set(&mut self, high: bool) {
        match self {
            Output::Gpio(pin) => pin.write(if high { Level::High } else { Level::Low }),
            Output::Log => tracing::info!("GPIO → {}", if high { "HIGH" } else { "LOW" }),
            Output::Record(changes) => changes.push((high, Instant::now())),
        }
    }
}

/// How long to hold the line HIGH, then LOW, for each quarter.
#[derive(Clone, Copy, Debug)]
pub struct Timing {
    pub high: Duration,
    pub low: Duration,
}

impl Timing {
    /// The timing the Arduino firmware is designed for. Conservative old defaults.
    pub const ARDUINO: Timing = Timing { high: Duration::from_millis(100), low: Duration::from_millis(100) };

    /// The replays used take ~50 ms. The Arduino doesn't watch the
    /// pin during a replay, so HIGH has to outlast the replay; otherwise the LOW
    /// could start and finish unseen. After that, it only needs to see the LOW once.
    pub const MIN_HIGH: Duration = Duration::from_millis(60);
    pub const MIN_LOW: Duration = Duration::from_millis(40);

    /// HIGH for [`Timing::MIN_HIGH`], LOW for the rest of `period`. Fails if
    /// `period` is too short for the firmware.
    pub fn with_period(period: Duration) -> anyhow::Result<Timing> {
        let min = Self::MIN_HIGH + Self::MIN_LOW;
        anyhow::ensure!(period >= min, "quarter period {period:?} is shorter than the Arduino's minimum {min:?}");
        Ok(Timing { high: Self::MIN_HIGH, low: period - Self::MIN_HIGH })
    }
}

/// Sends quarters to the Arduino.
pub struct CoinSignal {
    output: Output,
    timing: Timing,
}

impl CoinSignal {
    pub fn new(mut output: Output, timing: Timing) -> Self {
        output.set(false);
        Self { output, timing }
    }

    /// Pulses the line once per quarter. Blocks until done.
    pub fn send_quarters(&mut self, quarters: u32) {
        for _ in 0..quarters {
            self.output.set(true);
            sleep(self.timing.high);
            self.output.set(false);
            sleep(self.timing.low);
        }
    }

    /// The changes recorded by [`Output::Record`] (empty for other outputs).
    pub fn recorded(&self) -> &[(bool, Instant)] {
        match &self.output {
            Output::Record(changes) => changes,
            _ => &[],
        }
    }

    /// How many quarters the Arduino would have seen (LOW→HIGH edges), with
    /// [`Output::Record`].
    pub fn quarters_sent(&self) -> usize {
        self.recorded().windows(2).filter(|pair| !pair[0].0 && pair[1].0).count()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Shorter than the real timing so tests run quickly.
    const FAST: Timing = Timing { high: Duration::from_millis(10), low: Duration::from_millis(30) };

    fn send(quarters: u32) -> CoinSignal {
        let mut coins = CoinSignal::new(Output::Record(Vec::new()), FAST);
        coins.send_quarters(quarters);
        coins
    }

    #[test]
    fn one_pulse_per_quarter_starting_and_ending_low() {
        for quarters in [0, 1, 3] {
            let coins = send(quarters);
            assert_eq!(coins.quarters_sent(), quarters as usize);
            // The initial LOW, then a HIGH, LOW pair per quarter.
            let levels: Vec<bool> = coins.recorded().iter().map(|(high, _)| *high).collect();
            let expected: Vec<bool> = (0..=2 * quarters).map(|i| i % 2 == 1).collect();
            assert_eq!(levels, expected);
        }
    }

    #[test]
    fn pulses_are_long_enough_for_the_arduino() {
        let started = Instant::now();
        let coins = send(3);
        for pair in coins.recorded()[1..].windows(2) {
            let ((was_high, start), (_, end)) = (pair[0], pair[1]);
            let min = if was_high { FAST.high } else { FAST.low };
            assert!(end - start >= min, "level held for {:?}", end - start);
        }
        // The final LOW has no change after it, so check it by timing the call.
        assert!(started.elapsed() >= 3 * (FAST.high + FAST.low));
    }

    /// Guards against someone "optimizing" the default timing below what the
    /// firmware can handle.
    #[test]
    fn arduino_timing_is_safe() {
        assert!(Timing::ARDUINO.high >= Timing::MIN_HIGH);
        assert!(Timing::ARDUINO.low >= Timing::MIN_LOW);
    }

    /// The firmware keeps its own copy of these limits (PI_MIN_HIGH_MS and
    /// PI_MIN_LOW_MS in vend.h), and its tests check the replays fit inside
    /// them. Both copies must agree.
    #[test]
    fn firmware_agrees_on_timing_limits() {
        let header = include_str!("../../../Arduino/platformio/lib/vend/vend.h");
        let define = |name: &str| -> Duration {
            let ms = header
                .lines()
                .find_map(|line| line.strip_prefix("#define ")?.strip_prefix(name)?.trim().parse().ok())
                .unwrap_or_else(|| panic!("no `#define {name} <ms>` in vend.h"));
            Duration::from_millis(ms)
        };
        assert_eq!(define("PI_MIN_HIGH_MS"), Timing::MIN_HIGH);
        assert_eq!(define("PI_MIN_LOW_MS"), Timing::MIN_LOW);
    }

    #[test]
    fn with_period_enforces_the_minimum() {
        // Timing constraints are MIN_HIGH and MIN_LOW above.
        assert!(Timing::with_period(Duration::from_millis(79)).is_err());
        let t = Timing::with_period(Duration::from_millis(100)).unwrap();
        assert_eq!((t.high, t.low), (Timing::MIN_HIGH, Timing::MIN_LOW));
        let custom_timing = Duration::from_millis(200);
        let t = Timing::with_period(custom_timing).unwrap();
        assert_eq!((t.high, t.low), (Timing::MIN_HIGH, custom_timing - Timing::MIN_HIGH));
    }
}
