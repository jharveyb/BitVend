//! Connecting our payment logic to the Lexe Lightning wallet.
//!
//! The Pi only has *client credentials*: a revocable key that can receive and
//! read payments, but not spend. See SETUP.md for how to create them.

use std::{path::PathBuf, time::Duration, time::UNIX_EPOCH};

use anyhow::bail;
use lexe::{
    config::WalletEnvConfig,
    types::{
        auth::{ClientCredentials, CredentialsRef},
        command::{UpdatePersonalNoteRequest, WaitForNextPaymentRequest},
        payment::{Order, Payment, PaymentDirection, PaymentFilter, PaymentRail},
    },
    wallet::LexeWallet,
};

use crate::vend::{self, PaymentInfo, PaymentSource};

/// How many recent payments to check each time. More than enough, as each
/// payment is handled within seconds of arriving.
const RECENT_LIMIT: usize = 50;

/// "mainnet" for real money, "testnet3" for testing.
pub fn network(name: &str) -> anyhow::Result<WalletEnvConfig> {
    match name {
        "mainnet" => Ok(WalletEnvConfig::mainnet()),
        "testnet3" => Ok(WalletEnvConfig::testnet3()),
        other => bail!("Unknown network {other:?}, expected mainnet or testnet3"),
    }
}

/// Opens the wallet. Payment history is cached in `data_dir` (default `~/.lexe`).
pub fn open(
    network: WalletEnvConfig,
    client_credentials: &str,
    data_dir: Option<PathBuf>,
) -> anyhow::Result<LexeWallet> {
    let credentials = ClientCredentials::from_string(client_credentials)?;
    LexeWallet::load_or_fresh(network, CredentialsRef::from(&credentials), data_dir)
}

/// Syncs payment history from Lexe. If this is the first run (nothing cached
/// yet), existing payments are marked as handled so they're never vended.
pub async fn sync(wallet: &LexeWallet) -> anyhow::Result<()> {
    let first_run = wallet.list_payments(&PaymentFilter::All, None, Some(1), None)?.payments.is_empty();
    wallet.sync_payments().await?;
    if first_run {
        vend::skip_existing_payments(wallet).await?;
    }
    Ok(())
}

impl PaymentSource for LexeWallet {
    async fn wait_for_change(&self) {
        let req = WaitForNextPaymentRequest { start_index: None, timeout: Some(Duration::from_secs(60)) };
        // We don't need the payment itself: the caller re-checks all recent
        // payments. An error just means nothing happened before the timeout
        // (or the network is down), so pause briefly to avoid a busy loop.
        if self.wait_for_next_payment(req).await.is_err() {
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    }

    async fn recent_completed(&self) -> anyhow::Result<Vec<PaymentInfo>> {
        let recent = self.list_payments(&PaymentFilter::Completed, Some(Order::Desc), Some(RECENT_LIMIT), None)?;
        Ok(recent.payments.iter().map(to_payment_info).collect())
    }

    async fn mark(&self, id: &str, note: String) -> anyhow::Result<()> {
        let req = UpdatePersonalNoteRequest { index: id.parse()?, personal_note: Some(note) };
        self.update_personal_note(req).await
    }
}

fn to_payment_info(payment: &Payment) -> PaymentInfo {
    PaymentInfo {
        id: payment.index.to_string(),
        inbound: payment.direction == PaymentDirection::Inbound,
        lightning: matches!(payment.rail, PaymentRail::Invoice | PaymentRail::Offer | PaymentRail::Spontaneous),
        // Lexe deducts its fee (currently 0.5%) from what we receive. Credit
        // the customer for everything they sent, fee included, so e.g. paying
        // exactly 25¢ still gets a quarter.
        sats: payment.amount.map(|amount| amount.sats_u64()).unwrap_or(0) + payment.fees.sats_u64(),
        finalized_at: payment.finalized_at.map(|t| t.to_system_time()).unwrap_or(UNIX_EPOCH),
        note: payment.personal_note.clone(),
    }
}
