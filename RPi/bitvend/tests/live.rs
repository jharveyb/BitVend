//! Live end-to-end test: a "payer" wallet pays the vending machine's static
//! offer, and we check the right quarters are sent.
//!
//! Needs two wallets, a machine and a funded payer, set up as described in
//! SETUP.md. Run it with `just test-live`, or:
//!
//! ```sh
//! BITVEND_TEST_NETWORK=testnet3 \
//! BITVEND_TEST_CLIENT_CREDENTIALS=... \
//! BITVEND_TEST_PAYER_DIR=~/.lexe-bitvend-test/payer \
//!     cargo test --test live -- --ignored --nocapture
//! ```

use std::{
    env,
    path::Path,
    time::{Duration, Instant},
};

use bitvend::{
    coin_signal::{CoinSignal, Output, Timing},
    price::PriceSource,
    vend::{self, PaymentSource},
    wallet,
};
use lexe::{
    types::{
        auth::{CredentialsRef, RootSeed},
        bitcoin::Amount,
        command::PayOfferRequest,
    },
    wallet::LexeWallet,
};

/// At $100,000/BTC, 740 sats is 74¢, which is within 2¢ of 75¢, so it rounds
/// up to 3 quarters.
struct FixedPrice;
impl PriceSource for FixedPrice {
    async fn usd_per_btc(&mut self) -> anyhow::Result<f64> {
        Ok(100_000.0)
    }
}
const SATS: u32 = 740;
const EXPECTED_QUARTERS: usize = 3;

fn env_var(name: &str) -> String {
    env::var(name).unwrap_or_else(|_| panic!("{name} must be set, see SETUP.md"))
}

#[tokio::test]
#[ignore = "needs funded wallets, see SETUP.md"]
async fn live_payment_becomes_quarters() {
    // `testnet3`, or `mainnet` with real (tiny) amounts.
    let network = wallet::network(&env::var("BITVEND_TEST_NETWORK").unwrap_or("testnet3".into())).unwrap();

    // The vending machine, with a fresh payment cache, as on first install.
    let machine_dir = tempfile::tempdir().unwrap();
    let machine =
        wallet::open(network.clone(), &env_var("BITVEND_TEST_CLIENT_CREDENTIALS"), Some(machine_dir.path().into()))
            .unwrap();
    // Marks payments from earlier test runs as handled.
    wallet::sync(&machine).await.unwrap();

    // The customer pays the machine's static offer (what its QR code shows).
    let payer_seed_path = network.seedphrase_path(Path::new(&env_var("BITVEND_TEST_PAYER_DIR")));
    let payer_seed = RootSeed::read_from_path(&payer_seed_path).unwrap().expect("payer seed file exists");
    let payer_dir = tempfile::tempdir().unwrap();
    let payer =
        LexeWallet::load_or_fresh(network, CredentialsRef::from(&payer_seed), Some(payer_dir.path().into())).unwrap();
    let offer = machine.get_human_bitcoin_address().await.unwrap().offer;
    payer
        .pay_offer(PayOfferRequest {
            offer,
            amount: Amount::from_sats_u32(SATS),
            client_payment_id: None,
            message: Some("bitvend integration test".into()),
            personal_note: None,
        })
        .await
        .unwrap();

    // Run the vending loop until the quarters come out.
    let mut coins = CoinSignal::new(Output::Record(Vec::new()), Timing::ARDUINO);
    let deadline = Instant::now() + Duration::from_secs(180);
    while coins.quarters_sent() == 0 {
        assert!(Instant::now() < deadline, "payment never arrived");
        wallet::wait_for_payment(&machine).await.unwrap();
        vend::vend_new_payments(&machine, &mut FixedPrice, &mut coins).await.unwrap();
    }
    assert_eq!(coins.quarters_sent(), EXPECTED_QUARTERS);

    // The payment is marked in the wallet, and running again sends nothing more.
    let latest = &machine.recent_completed().await.unwrap()[0];
    assert_eq!(latest.sats, SATS as u64);
    assert_eq!(latest.note.as_deref(), Some("bitvend: vended 3 quarters @ $100000/BTC"));
    vend::vend_new_payments(&machine, &mut FixedPrice, &mut coins).await.unwrap();
    assert_eq!(coins.quarters_sent(), EXPECTED_QUARTERS);
}
