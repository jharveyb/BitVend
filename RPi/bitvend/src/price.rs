//! Looking up the BTC/USD price.

use std::time::{Duration, Instant};

use anyhow::{Context, ensure};
use serde_json::Value;
use tracing::warn;

const COINBASE_URL: &str = "https://api.coinbase.com/v2/prices/BTC-USD/spot";
const KRAKEN_URL: &str = "https://api.kraken.com/0/public/Ticker?pair=XBTUSD";

/// Reuse a price for this long before fetching a new one. Kept short so our
/// price stays close to what the customer's wallet showed them.
const REFRESH_AFTER: Duration = Duration::from_secs(120);
/// If every price source is down, keep using the last price for this long.
const GIVE_UP_AFTER: Duration = Duration::from_secs(60 * 60);

/// Something that knows the price of bitcoin. Tests use a fixed price.
pub trait PriceSource {
    async fn usd_per_btc(&mut self) -> anyhow::Result<f64>;
}

/// Fetches the price from Coinbase, falling back to Kraken, with caching.
pub struct PriceFeed {
    http: reqwest::Client,
    /// The last price we fetched, and when.
    last: Option<(f64, Instant)>,
}

impl Default for PriceFeed {
    fn default() -> Self {
        let http =
            reqwest::Client::builder().timeout(Duration::from_secs(10)).build().expect("valid HTTP client config");
        Self { http, last: None }
    }
}

impl PriceFeed {
    async fn fetch(&self, url: &str, parse: fn(&Value) -> anyhow::Result<f64>) -> anyhow::Result<f64> {
        let json: Value = self.http.get(url).send().await?.error_for_status()?.json().await?;
        let price = parse(&json).with_context(|| format!("Unexpected response from {url}: {json}"))?;
        ensure!(price.is_finite() && price > 0.0, "Nonsense price {price} from {url}");
        Ok(price)
    }
}

impl PriceSource for PriceFeed {
    async fn usd_per_btc(&mut self) -> anyhow::Result<f64> {
        if let Some((price, fetched_at)) = self.last
            && fetched_at.elapsed() < REFRESH_AFTER
        {
            return Ok(price);
        }

        let fetched = match self.fetch(COINBASE_URL, parse_coinbase).await {
            Ok(price) => Ok(price),
            Err(e) => {
                warn!("Coinbase price failed, trying Kraken: {e:#}");
                self.fetch(KRAKEN_URL, parse_kraken).await
            }
        };

        match (fetched, self.last) {
            (Ok(price), _) => {
                self.last = Some((price, Instant::now()));
                Ok(price)
            }
            (Err(e), Some((price, fetched_at))) if fetched_at.elapsed() < GIVE_UP_AFTER => {
                warn!("All price sources failed, reusing last price ${price}: {e:#}");
                Ok(price)
            }
            (Err(e), _) => Err(e.context("No BTC price available")),
        }
    }
}

/// `{"data":{"amount":"65432.10","base":"BTC","currency":"USD"}}`
fn parse_coinbase(json: &Value) -> anyhow::Result<f64> {
    let amount = json["data"]["amount"].as_str().context("missing data.amount")?;
    Ok(amount.parse()?)
}

/// `{"error":[],"result":{"XXBTZUSD":{"c":["65432.10000","0.001"], ...}}}`
/// where `c` is [last trade price, volume].
fn parse_kraken(json: &Value) -> anyhow::Result<f64> {
    let last_trade = json["result"]["XXBTZUSD"]["c"][0].as_str().context("missing last trade price")?;
    Ok(last_trade.parse()?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn parses_coinbase() {
        let json = json!({"data": {"amount": "65432.10", "base": "BTC", "currency": "USD"}});
        assert_eq!(parse_coinbase(&json).unwrap(), 65432.10);
        assert!(parse_coinbase(&json!({"errors": []})).is_err());
    }

    #[test]
    fn parses_kraken() {
        let json = json!({"error": [], "result": {"XXBTZUSD": {"a": ["65433.0", "1", "1.0"], "c": ["65432.10000", "0.00100000"]}}});
        assert_eq!(parse_kraken(&json).unwrap(), 65432.10);
        assert!(parse_kraken(&json!({"error": ["EQuery:Unknown asset pair"]})).is_err());
    }
}
