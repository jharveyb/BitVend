//! BitVend service: receives Lightning payments and tells the Arduino to
//! insert the equivalent number of quarters into the vending machine.
//!
//! Configuration (environment variables, see bitvend.env.example):
//! - `LEXE_CLIENT_CREDENTIALS`: from `lexe create-client` (see SETUP.md).
//! - `BITVEND_NETWORK`: `mainnet` (default) or `testnet3`.
//! - `LEXE_DATA_DIR`: where to cache payment history (default `~/.lexe`).
//! - `BITVEND_GPIO=fake`: log pulses instead of using GPIO (for testing).
//! - `BITVEND_GPIO_PIN`: BCM GPIO number wired to the Arduino (default 18).
//! - `BITVEND_QUARTER_PERIOD_MS`: time per quarter pulse (default 200, min 100).

use std::{env, path::PathBuf, time::Duration};

use anyhow::Context;
use bitvend::{
    coin_signal::{CoinSignal, Output, Timing},
    price::PriceFeed,
    vend, wallet,
};
use lexe::wallet::LexeWallet;
use tracing::{info, warn};

/// Default GPIO wired to the Arduino: BCM 18, which is physical pin 12.
const DEFAULT_GPIO_PIN: u8 = 18;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    lexe::init_logger("info");

    let network = env::var("BITVEND_NETWORK").unwrap_or("mainnet".into());
    let credentials = env::var("LEXE_CLIENT_CREDENTIALS").context("LEXE_CLIENT_CREDENTIALS is not set")?;
    let data_dir = env::var_os("LEXE_DATA_DIR").map(PathBuf::from);
    let wallet = wallet::open(wallet::network(&network)?, &credentials, data_dir)?;
    log_address(&wallet).await;

    let timing = quarter_timing()?;
    info!("Sending one quarter every {:?}", timing.high + timing.low);
    let mut coins = CoinSignal::new(open_output()?, timing);
    let mut prices = PriceFeed::default();

    wallet::sync(&wallet).await?;
    info!("Ready for payments on {network}");
    loop {
        let result = async {
            wallet::wait_for_payment(&wallet).await?;
            vend::vend_new_payments(&wallet, &mut prices, &mut coins).await
        }
        .await;
        if let Err(e) = result {
            warn!("{e:#}");
            tokio::time::sleep(Duration::from_secs(5)).await;
        }
    }
}

/// Logs the static address customers pay to (print it as a QR code).
async fn log_address(wallet: &LexeWallet) {
    match wallet.get_human_bitcoin_address().await {
        Ok(addr) => info!("Pay to {} (Lightning Address {})", addr.human_bitcoin_address, addr.lightning_address),
        Err(e) => warn!("Couldn't look up payment address: {e:#}"),
    }
}

/// Pulse timing, from `BITVEND_QUARTER_PERIOD_MS` if set.
fn quarter_timing() -> anyhow::Result<Timing> {
    let Ok(ms) = env::var("BITVEND_QUARTER_PERIOD_MS") else {
        return Ok(Timing::ARDUINO);
    };
    let ms: u64 = ms.parse().with_context(|| format!("BITVEND_QUARTER_PERIOD_MS={ms:?} is not a number"))?;
    Timing::with_period(Duration::from_millis(ms))
}

fn open_output() -> anyhow::Result<Output> {
    if env::var("BITVEND_GPIO").as_deref() == Ok("fake") {
        return Ok(Output::Log);
    }
    let number = match env::var("BITVEND_GPIO_PIN") {
        Ok(n) => n.parse().with_context(|| format!("BITVEND_GPIO_PIN={n:?} is not a GPIO number"))?,
        Err(_) => DEFAULT_GPIO_PIN,
    };
    info!("Signalling the Arduino on BCM GPIO {number}");
    let pin = rppal::gpio::Gpio::new()?.get(number).with_context(|| format!("Can't open BCM GPIO {number}"))?;
    Ok(Output::Gpio(pin.into_output_low()))
}
