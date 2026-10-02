//! Converting a payment (in sats) into a number of quarters.
//!
//! We round *down* to whole quarters, but first add a little slack, so a
//! payment that's just short of the next quarter still reaches it. The slack
//! covers the customer's wallet using a slightly different (or slightly older)
//! BTC price than us:
//!
//! - **2% of the payment**, because price differences grow with the amount,
//! - but **at least 2¢**, since wallets round small amounts to the cent,
//! - and **at most 24¢**, so an exact amount (like $40.00) is never rounded up
//!   to the next quarter. 2% reaches this cap at $12.
//!
//! So 98¢ gets $1.00, but 26¢ gets one quarter, not two. (Always rounding up
//! would let someone pay 25.1¢ again and again to get 50¢ each time.)

const SATS_PER_BTC: f64 = 100_000_000.0;

/// All money below is in US dollars.
const QUARTER: f64 = 0.25;
const SLACK_PERCENT: f64 = 2.0;
const MIN_SLACK: f64 = 0.02;
const MAX_SLACK: f64 = 0.24;

/// How many quarters to credit for a payment of `sats`, at a price of
/// `usd_per_btc` dollars per bitcoin.
pub fn quarters_for(sats: u64, usd_per_btc: f64) -> u32 {
    let dollars = (sats as f64 / SATS_PER_BTC) * usd_per_btc;
    let slack = slack(dollars);
    ((dollars + slack) / QUARTER).floor() as u32
}

/// How far short of the next quarter a payment can be, and still get it.
fn slack(dollars: f64) -> f64 {
    (dollars * (SLACK_PERCENT / 100.0)).clamp(MIN_SLACK, MAX_SLACK)
}

#[cfg(test)]
mod tests {
    use proptest::prelude::*;

    use super::*;

    /// At $100,000/BTC, 1 sat = 0.1¢, so 250 sats is exactly one quarter.
    const PRICE: f64 = 100_000.0;

    #[test]
    fn examples() {
        #[rustfmt::skip]
        let cases = [
            // (sats, quarters, why)
            (0,      0,   "nothing"),
            (1,      0,   "nothing"),
            (229,    0,   "22.9¢ is 2.1¢ short of a quarter: too far"),
            (231,    1,   "23.1¢ is 1.9¢ short: rounds up"),
            (250,    1,   "exactly one quarter"),
            (260,    1,   "26¢ is not rounded up to 50¢"),
            (479,    1,   "47.9¢ is 2.1¢ short of 50¢"),
            (481,    2,   "48.1¢ rounds up to 50¢"),
            (979,    3,   "97.9¢ is 2.1¢ short of $1 (2% would only be ~1.96¢)"),
            (981,    4,   "98.1¢ rounds up to $1"),
            (9_800,  39,  "$9.80 is 20¢ short of $10, more than 2% (19.6¢)"),
            (9_802,  39,  "$9.805 is 19.95¢ short of $10, not within 2% (19.61¢)"),
            (9_810,  40,  "$9.81 is 19¢ short of $10, within 2% (19.62¢)"),
            (39_700, 159, "$39.70 is 5¢ short of $39.75: rounds up"),
            (39_770, 160, "$39.77 is 23¢ short of $40: within the 24¢ cap"),
            (40_000, 160, "exactly $40.00 is never rounded up to $40.25"),
            (100_000_000, 400_000, "one whole bitcoin, no overflow"),
        ];
        for (sats, quarters, why) in cases {
            assert_eq!(quarters_for(sats, PRICE), quarters, "{sats} sats: {why}");
        }
    }

    #[test]
    fn fractional_prices() {
        // At $65,432.10/BTC, $1.00 is 1528.29.. sats.
        assert_eq!(quarters_for(1_528, 65_432.10), 4); // 99.98¢ rounds up
        assert_eq!(quarters_for(1_529, 65_432.10), 4); // $1.0005, not rounded up to $1.25
    }

    // Property-based tests: check rules that must hold for *any* payment and
    // price, using randomly generated inputs. When one fails, proptest shrinks
    // it to a minimal example.

    /// Allows for floating-point error, far below a cent.
    const EPSILON: f64 = 1e-9;

    /// Payments (at `PRICE`) below this many sats get the minimum slack, since
    /// the percentage slack is smaller.
    const SMALL_SATS: u64 = (((MIN_SLACK / (SLACK_PERCENT / 100.0)) / PRICE) * SATS_PER_BTC) as u64;

    /// Payment sizes: half small (where rounding rules matter most), half
    /// anything up to 100 BTC.
    fn any_sats() -> impl Strategy<Value = u64> {
        prop_oneof![0..10_000u64, 0..10_000_000_000u64]
    }

    fn any_price() -> impl Strategy<Value = f64> {
        1_000.0..1_000_000.0f64
    }

    proptest! {
        /// We never give away more than the slack, and never short the
        /// customer by a quarter or more.
        #[test]
        fn credit_is_close_to_value(sats in any_sats(), price in any_price()) {
            let dollars = (sats as f64 / SATS_PER_BTC) * price;
            let credit = (quarters_for(sats, price) as f64) * QUARTER;
            prop_assert!(credit <= dollars + slack(dollars) + EPSILON, "gave away too much");
            prop_assert!(dollars - credit < QUARTER, "shorted a whole quarter");
        }

        /// Paying more never gets fewer quarters.
        #[test]
        fn more_sats_never_get_fewer_quarters(sats in any_sats(), extra in 0..1_000_000u64, price in any_price()) {
            prop_assert!(quarters_for(sats + extra, price) >= quarters_for(sats, price));
        }

        /// A payment worth an exact number of quarters is never rounded.
        #[test]
        fn exact_quarters_are_exact(quarters in 0..100_000u32) {
            // At $100,000/BTC each quarter is exactly 250 sats.
            prop_assert_eq!(quarters_for((quarters as u64) * 250, PRICE), quarters);
        }

        /// Repeated tiny payments can't be farmed: on small payments, the
        /// most anyone gains by rounding is the 2¢ minimum slack.
        #[test]
        fn small_payments_gain_at_most_2_cents(sats in 0..SMALL_SATS) {
            let dollars = (sats as f64 / SATS_PER_BTC) * PRICE;
            let credit = (quarters_for(sats, PRICE) as f64) * QUARTER;
            prop_assert!((credit - dollars) <= (MIN_SLACK + EPSILON));
        }
    }
}
