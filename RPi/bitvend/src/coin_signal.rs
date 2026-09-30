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

/// A digital output pin. Implemented by the real Raspberry Pi GPIO pin, and by
/// stand-ins for testing or for running without hardware.
pub trait Pin {
    fn set(&mut self, high: bool);
}

/// How long to hold the line HIGH, then LOW, for each quarter.
#[derive(Clone, Copy, Debug)]
pub struct Timing {
    pub high: Duration,
    pub low: Duration,
}

impl Timing {
    /// The timing the Arduino firmware is designed for.
    pub const ARDUINO: Timing = Timing { high: Duration::from_millis(100), low: Duration::from_millis(450) };

    /// The longest replay (QUARTER4) takes ~85 ms. The Arduino doesn't watch the
    /// pin during a replay, so HIGH has to outlast the replay; otherwise the LOW
    /// could start and finish unseen. After that, it only needs to see the LOW once.
    pub const MIN_HIGH: Duration = Duration::from_millis(100);
    pub const MIN_LOW: Duration = Duration::from_millis(50);

    /// HIGH for [`Timing::MIN_HIGH`], LOW for the rest of `period`. Fails if
    /// `period` is too short for the firmware.
    pub fn with_period(period: Duration) -> anyhow::Result<Timing> {
        let min = Self::MIN_HIGH + Self::MIN_LOW;
        anyhow::ensure!(period >= min, "quarter period {period:?} is shorter than the Arduino's minimum {min:?}");
        Ok(Timing { high: Self::MIN_HIGH, low: period - Self::MIN_HIGH })
    }
}

/// Sends quarters to the Arduino.
pub struct CoinSignal<P: Pin> {
    pin: P,
    timing: Timing,
}

impl<P: Pin> CoinSignal<P> {
    pub fn new(mut pin: P, timing: Timing) -> Self {
        pin.set(false);
        Self { pin, timing }
    }

    /// Pulses the line once per quarter. Blocks until done (about 1 s each).
    pub fn send_quarters(&mut self, quarters: u32) {
        for _ in 0..quarters {
            self.pin.set(true);
            sleep(self.timing.high);
            self.pin.set(false);
            sleep(self.timing.low);
        }
    }

    /// The underlying pin, e.g. to inspect a [`RecordingPin`] in tests.
    pub fn pin(&self) -> &P {
        &self.pin
    }
}

/// The real Raspberry Pi GPIO pin.
impl Pin for rppal::gpio::OutputPin {
    fn set(&mut self, high: bool) {
        if high { self.set_high() } else { self.set_low() }
    }
}

/// Lets `main` pick a pin at runtime (real or [`LogPin`]).
impl Pin for Box<dyn Pin + Send> {
    fn set(&mut self, high: bool) {
        (**self).set(high)
    }
}

/// A pin that only logs, for running on a machine without GPIO.
pub struct LogPin;

impl Pin for LogPin {
    fn set(&mut self, high: bool) {
        tracing::info!("GPIO → {}", if high { "HIGH" } else { "LOW" });
    }
}

/// A pin that records every change and when it happened. Used by tests.
#[derive(Default)]
pub struct RecordingPin {
    pub changes: Vec<(bool, Instant)>,
}

impl Pin for RecordingPin {
    fn set(&mut self, high: bool) {
        self.changes.push((high, Instant::now()));
    }
}

impl RecordingPin {
    /// How many quarters the Arduino would have seen (LOW→HIGH edges).
    pub fn quarters_sent(&self) -> usize {
        let mut level = false;
        let mut edges = 0;
        for &(high, _) in &self.changes {
            if high && !level {
                edges += 1;
            }
            level = high;
        }
        edges
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Shorter than the real timing so tests run quickly.
    const FAST: Timing = Timing { high: Duration::from_millis(10), low: Duration::from_millis(30) };

    fn send(quarters: u32) -> Vec<(bool, Instant)> {
        let mut coins = CoinSignal::new(RecordingPin::default(), FAST);
        coins.send_quarters(quarters);
        assert_eq!(coins.pin().quarters_sent(), quarters as usize);
        coins.pin().changes.clone()
    }

    #[test]
    fn line_starts_low() {
        let coins = CoinSignal::new(RecordingPin::default(), FAST);
        assert_eq!(coins.pin().changes.len(), 1);
        assert!(!coins.pin().changes[0].0);
    }

    #[test]
    fn zero_quarters_sends_nothing() {
        let changes = send(0);
        assert_eq!(changes.len(), 1); // Only the initial LOW.
    }

    #[test]
    fn one_rising_edge_per_quarter() {
        for quarters in [1, 2, 5] {
            let changes = send(quarters);
            // Initial LOW, then a HIGH, LOW pair per quarter.
            assert_eq!(changes.len(), 1 + 2 * quarters as usize);
            let levels: Vec<bool> = changes.iter().map(|c| c.0).collect();
            for (i, level) in levels.iter().enumerate() {
                assert_eq!(*level, i % 2 == 1, "line must alternate LOW/HIGH");
            }
        }
    }

    #[test]
    fn line_ends_low() {
        let changes = send(3);
        assert!(!changes.last().unwrap().0);
    }

    #[test]
    fn pulses_are_long_enough_for_the_arduino() {
        let changes = send(3);
        // changes = [LOW, HIGH, LOW, HIGH, LOW, HIGH, LOW]
        for pair in changes[1..].windows(2) {
            let ((was_high, start), (_, end)) = (pair[0], pair[1]);
            let min = if was_high { FAST.high } else { FAST.low };
            assert!(end - start >= min, "level held for {:?}", end - start);
        }
        // The final LOW has no change after it, so check it by timing the call.
        let started = Instant::now();
        send(2);
        assert!(started.elapsed() >= 2 * (FAST.high + FAST.low));
    }

    /// Guards against someone "optimizing" the default timing below what the
    /// firmware can handle.
    #[test]
    fn arduino_timing_is_safe() {
        assert!(Timing::ARDUINO.high >= Timing::MIN_HIGH);
        assert!(Timing::ARDUINO.low >= Timing::MIN_LOW);
    }

    #[test]
    fn with_period_enforces_the_minimum() {
        assert!(Timing::with_period(Duration::from_millis(149)).is_err());
        let t = Timing::with_period(Duration::from_millis(150)).unwrap();
        assert_eq!((t.high, t.low), (Timing::MIN_HIGH, Timing::MIN_LOW));
        let t = Timing::with_period(Duration::from_millis(550)).unwrap();
        assert_eq!((t.high, t.low), (Timing::ARDUINO.high, Timing::ARDUINO.low));
    }
}
