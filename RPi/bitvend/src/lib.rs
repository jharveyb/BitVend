//! BitVend: turns Lightning payments into quarters for a vending machine.
//!
//! How the pieces fit together:
//!
//! - [`wallet`]: our Lexe Lightning wallet, which receives payments.
//! - [`price`]: looks up the BTC/USD price.
//! - [`quarters`]: converts a payment in sats into a number of quarters.
//! - [`coin_signal`]: tells the Arduino to insert quarters, via one GPIO wire.
//! - [`vend`]: decides which payments to vend, and how many quarters each.
//! - [`health`]: tells systemd and an optional monitoring URL that we're working.
//!
//! `main.rs` wires them together: wait for a payment, then vend any new ones.

// We only ever `.await` our traits' async methods directly (never spawn them
// onto other threads), so the usual `Send` caveat of this lint doesn't apply.
#![allow(async_fn_in_trait)]

pub mod coin_signal;
pub mod health;
pub mod price;
pub mod quarters;
pub mod vend;
pub mod wallet;
