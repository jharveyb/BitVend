//! Deciding what to vend: find unhandled payments, work out their value, send quarters.
//!
//! We don't keep any state files of our own. Instead, after handling a payment
//! we attach a private note to it in the Lexe wallet (e.g. "bitvend: vended 4
//! quarters @ $65432/BTC"). Any payment with such a note has been handled, and
//! the operator can see the notes with `lexe list-payments`.

use std::time::{Duration, SystemTime};

use tracing::{info, warn};

use crate::{coin_signal::CoinSignal, price::PriceSource, quarters::quarters_for};

/// Every note we write starts with this, so we can recognize handled payments.
pub const NOTE_PREFIX: &str = "bitvend:";

/// Payments older than this are never vended automatically, e.g. if the Pi was
/// off for a day. This avoids surprise vending of long-forgotten payments.
pub const MAX_AGE: Duration = Duration::from_secs(24 * 60 * 60);

/// The facts about a payment that matter for vending.
#[derive(Clone, Debug)]
pub struct PaymentInfo {
    /// Lexe's unique ID for this payment.
    pub id: String,
    /// Was it sent *to* us?
    pub inbound: bool,
    /// Was it a Lightning payment? (We ignore slow on-chain payments.)
    pub lightning: bool,
    /// What the customer paid, including Lexe's fee.
    pub sats: u64,
    /// When the payment completed.
    pub finalized_at: SystemTime,
    /// Our private note on the payment, if any.
    pub note: Option<String>,
}

/// Where payments come from: the Lexe wallet (see `wallet.rs`), or a fake in
/// tests.
pub trait PaymentSource {
    /// Recently completed payments, newest first.
    async fn recent_completed(&self) -> anyhow::Result<Vec<PaymentInfo>>;
    /// Attaches our note to a payment.
    async fn mark(&self, id: &str, note: String) -> anyhow::Result<()>;
}

/// Should we give quarters for this payment?
pub fn creditable(payment: &PaymentInfo, now: SystemTime) -> bool {
    let handled = payment.note.as_deref().is_some_and(|n| n.starts_with(NOTE_PREFIX));
    let age = now.duration_since(payment.finalized_at).unwrap_or_default();
    payment.inbound && payment.lightning && !handled && age < MAX_AGE
}

/// Vends every payment we haven't handled yet, oldest first. `main` calls this
/// each time the wallet sees payment activity.
pub async fn vend_new_payments(
    payments: &impl PaymentSource,
    prices: &mut impl PriceSource,
    coins: &mut CoinSignal,
) -> anyhow::Result<()> {
    let now = SystemTime::now();
    let mut new_payments: Vec<PaymentInfo> =
        payments.recent_completed().await?.into_iter().filter(|p| creditable(p, now)).collect();
    new_payments.reverse();

    for payment in new_payments {
        // If the price is unavailable we stop here; the payment stays unmarked
        // and will be retried on the next loop.
        let price = prices.usd_per_btc().await?;
        let quarters = quarters_for(payment.sats, price);
        let note = if quarters == 0 {
            format!("{NOTE_PREFIX} skipped, worth under a quarter @ ${price:.0}/BTC")
        } else {
            format!("{NOTE_PREFIX} vended {quarters} quarters @ ${price:.0}/BTC")
        };

        // Mark the payment *before* sending quarters. If we crash in between,
        // the customer misses out (and can ask the operator), but we can never
        // vend the same payment twice.
        payments.mark(&payment.id, note.clone()).await?;
        info!("Payment of {} sats: {note}", payment.sats);
        coins.send_quarters(quarters);
    }
    Ok(())
}

/// Marks all current payments as handled without vending anything. Used the
/// first time the service runs, so that payments made before BitVend was
/// installed (like funding the wallet) are never turned into quarters.
pub async fn skip_existing_payments(payments: &impl PaymentSource) -> anyhow::Result<()> {
    let now = SystemTime::now();
    for payment in payments.recent_completed().await? {
        if creditable(&payment, now) {
            warn!("Skipping {} sat payment from before install", payment.sats);
            payments.mark(&payment.id, format!("{NOTE_PREFIX} skipped, before install")).await?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn payment() -> PaymentInfo {
        PaymentInfo {
            id: "p1".into(),
            inbound: true,
            lightning: true,
            sats: 1_000,
            finalized_at: SystemTime::now(),
            note: None,
        }
    }

    #[test]
    fn new_inbound_lightning_payment_is_creditable() {
        assert!(creditable(&payment(), SystemTime::now()));
    }

    #[test]
    fn other_payments_are_not_creditable() {
        let now = SystemTime::now();
        let outbound = PaymentInfo { inbound: false, ..payment() };
        let onchain = PaymentInfo { lightning: false, ..payment() };
        let handled = PaymentInfo { note: Some("bitvend: vended 4 quarters".into()), ..payment() };
        let old = PaymentInfo { finalized_at: now - MAX_AGE - Duration::from_secs(1), ..payment() };
        for p in [outbound, onchain, handled, old] {
            assert!(!creditable(&p, now), "{p:?}");
        }
    }

    #[test]
    fn unrelated_note_is_still_creditable() {
        let p = PaymentInfo { note: Some("coffee".into()), ..payment() };
        assert!(creditable(&p, SystemTime::now()));
    }
}
