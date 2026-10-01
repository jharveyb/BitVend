//! End-to-end tests of the vending loop, with a fake wallet, a fixed price,
//! and a pin that records what the Arduino would see. No network needed.

use std::{
    cell::RefCell,
    time::{Duration, SystemTime},
};

use bitvend::{
    coin_signal::{CoinSignal, Output, Timing},
    price::PriceSource,
    vend::{self, PaymentInfo, PaymentSource},
};

/// At $100,000/BTC, 250 sats is exactly one quarter.
const PRICE: f64 = 100_000.0;

const FAST: Timing = Timing { high: Duration::from_millis(1), low: Duration::from_millis(1) };

/// A wallet that holds payments in memory.
#[derive(Default)]
struct FakeWallet {
    /// Newest first, like the real wallet.
    payments: RefCell<Vec<PaymentInfo>>,
}

impl FakeWallet {
    fn receive(&self, id: &str, sats: u64) {
        self.add(PaymentInfo {
            id: id.into(),
            inbound: true,
            lightning: true,
            sats,
            finalized_at: SystemTime::now(),
            note: None,
        });
    }

    fn add(&self, payment: PaymentInfo) {
        self.payments.borrow_mut().insert(0, payment);
    }

    fn note(&self, id: &str) -> Option<String> {
        self.payments.borrow().iter().find(|p| p.id == id).unwrap().note.clone()
    }
}

impl PaymentSource for FakeWallet {
    async fn recent_completed(&self) -> anyhow::Result<Vec<PaymentInfo>> {
        Ok(self.payments.borrow().clone())
    }

    async fn mark(&self, id: &str, note: String) -> anyhow::Result<()> {
        let mut payments = self.payments.borrow_mut();
        payments.iter_mut().find(|p| p.id == id).unwrap().note = Some(note);
        Ok(())
    }
}

/// A price feed that returns a fixed price, or fails if `None`.
struct FixedPrice(Option<f64>);

impl PriceSource for FixedPrice {
    async fn usd_per_btc(&mut self) -> anyhow::Result<f64> {
        self.0.ok_or_else(|| anyhow::anyhow!("price feed down"))
    }
}

struct Machine {
    wallet: FakeWallet,
    prices: FixedPrice,
    coins: CoinSignal,
}

impl Machine {
    fn new() -> Self {
        Self {
            wallet: FakeWallet::default(),
            prices: FixedPrice(Some(PRICE)),
            coins: CoinSignal::new(Output::Record(Vec::new()), FAST),
        }
    }

    async fn run(&mut self) -> anyhow::Result<()> {
        vend::vend_new_payments(&self.wallet, &mut self.prices, &mut self.coins).await
    }

    fn quarters_sent(&self) -> usize {
        self.coins.quarters_sent()
    }
}

#[tokio::test]
async fn payment_becomes_quarters() {
    let mut machine = Machine::new();
    machine.wallet.receive("a", 1_000); // $1.00
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 4);
    assert_eq!(machine.wallet.note("a").unwrap(), "bitvend: vended 4 quarters @ $100000/BTC");
}

#[tokio::test]
async fn partial_quarter_rounds_up() {
    let mut machine = Machine::new();
    machine.wallet.receive("a", 260); // 26¢
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 2);
}

#[tokio::test]
async fn several_payments_are_all_vended() {
    let mut machine = Machine::new();
    machine.wallet.receive("a", 250);
    machine.wallet.receive("b", 500);
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 3);
}

#[tokio::test]
async fn payment_is_vended_only_once() {
    let mut machine = Machine::new();
    machine.wallet.receive("a", 1_000);
    machine.run().await.unwrap();
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 4);

    // A later payment is still vended.
    machine.wallet.receive("b", 250);
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 5);
}

#[tokio::test]
async fn dust_is_marked_but_not_vended() {
    let mut machine = Machine::new();
    machine.wallet.receive("a", 1); // 0.1¢
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 0);
    assert!(machine.wallet.note("a").unwrap().starts_with("bitvend: skipped"));
}

#[tokio::test]
async fn outbound_and_onchain_payments_are_ignored() {
    let mut machine = Machine::new();
    let payment = PaymentInfo {
        id: String::new(),
        inbound: true,
        lightning: true,
        sats: 1_000,
        finalized_at: SystemTime::now(),
        note: None,
    };
    machine.wallet.add(PaymentInfo { id: "out".into(), inbound: false, ..payment.clone() });
    machine.wallet.add(PaymentInfo { id: "chain".into(), lightning: false, ..payment });
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 0);
    assert_eq!(machine.wallet.note("out"), None);
    assert_eq!(machine.wallet.note("chain"), None);
}

#[tokio::test]
async fn payment_waits_for_price_feed() {
    let mut machine = Machine::new();
    machine.prices = FixedPrice(None);
    machine.wallet.receive("a", 1_000);
    assert!(machine.run().await.is_err());
    assert_eq!(machine.quarters_sent(), 0);
    assert_eq!(machine.wallet.note("a"), None);

    // Once the price is back, the payment is vended.
    machine.prices = FixedPrice(Some(PRICE));
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 4);
}

#[tokio::test]
async fn payments_before_install_are_skipped() {
    let mut machine = Machine::new();
    machine.wallet.receive("funding", 10_000);
    vend::skip_existing_payments(&machine.wallet).await.unwrap();
    assert_eq!(machine.wallet.note("funding").unwrap(), "bitvend: skipped, before install");

    machine.wallet.receive("customer", 250);
    machine.run().await.unwrap();
    assert_eq!(machine.quarters_sent(), 1);
}
