//! Converting a payment (in sats) into a number of quarters.

/// Number of satoshis ("sats") in one bitcoin.
const SATS_PER_BTC: u128 = 100_000_000;

/// How many quarters to credit for a payment of `sats`, at a price of
/// `usd_per_btc` dollars per bitcoin.
///
/// - Payments worth less than 25¢ credit nothing, so nobody can get a quarter
///   for 1 sat.
/// - Anything else is rounded *up* to the next quarter, e.g. 26¢ → 2 quarters.
pub fn quarters_for(sats: u64, usd_per_btc: f64) -> u32 {
    // Integer math avoids floating-point rounding surprises at the boundaries.
    // Payment value in cents = sats * cents_per_btc / SATS_PER_BTC, so we
    // compare `sats * cents_per_btc` against multiples of 25 * SATS_PER_BTC.
    let cents_per_btc = (usd_per_btc * 100.0).round() as u128;
    let value = sats as u128 * cents_per_btc;
    let one_quarter = 25 * SATS_PER_BTC;

    if value < one_quarter {
        return 0;
    }
    value.div_ceil(one_quarter) as u32
}

#[cfg(test)]
mod tests {
    use super::*;

    /// At $100,000/BTC, 1 sat = 0.1¢, so 250 sats is exactly one quarter.
    const PRICE: f64 = 100_000.0;

    #[test]
    fn less_than_a_quarter_credits_nothing() {
        assert_eq!(quarters_for(0, PRICE), 0);
        assert_eq!(quarters_for(1, PRICE), 0);
        assert_eq!(quarters_for(249, PRICE), 0);
    }

    #[test]
    fn exact_quarters_are_not_rounded() {
        assert_eq!(quarters_for(250, PRICE), 1);
        assert_eq!(quarters_for(500, PRICE), 2);
        assert_eq!(quarters_for(1_000, PRICE), 4); // $1.00
    }

    #[test]
    fn partial_quarters_round_up() {
        assert_eq!(quarters_for(251, PRICE), 2); // 25.1¢
        assert_eq!(quarters_for(260, PRICE), 2); // 26¢
        assert_eq!(quarters_for(999, PRICE), 4); // 99.9¢
        assert_eq!(quarters_for(1_001, PRICE), 5); // $1.001
    }

    #[test]
    fn large_payments() {
        // $100 → 400 quarters.
        assert_eq!(quarters_for(100_000, PRICE), 400);
        // One whole bitcoin at $100k → 400,000 quarters (no overflow).
        assert_eq!(quarters_for(100_000_000, PRICE), 400_000);
    }

    #[test]
    fn fractional_prices() {
        // At $65,432.10/BTC a $1.00 payment is 1528.29.. sats.
        assert_eq!(quarters_for(1_528, 65_432.10), 4); // 99.98¢ → rounds up
        assert_eq!(quarters_for(1_529, 65_432.10), 5); // $1.0005
    }
}
