//! Letting someone know when BitVend stops working.
//!
//! If BitVend is down, customers can still pay the QR code but get nothing, so
//! we report health in two ways:
//!
//! - **systemd watchdog:** after every loop we tell systemd "still alive". If
//!   that stops (e.g. the process hangs), systemd restarts the service. See
//!   `WatchdogSec` in bitvend.service.
//! - **Heartbeat URL** (optional, `BITVEND_HEALTHCHECK_URL`): we ping it at
//!   least once a minute while healthy, and ping `<url>/fail` with the error
//!   when something goes wrong. With a service like https://healthchecks.io,
//!   you get an email/text if the pings stop or report failure.

use std::time::{Duration, Instant};

use sd_notify::NotifyState;
use tracing::warn;

/// Don't ping the heartbeat URL more often than this (unless health changes).
const PING_EVERY: Duration = Duration::from_secs(60);

pub struct Health {
    http: reqwest::Client,
    url: Option<String>,
    /// Whether our last ping said healthy, and when we sent it.
    last_ping: Option<(bool, Instant)>,
}

impl Health {
    pub fn new(url: Option<String>) -> Self {
        let http =
            reqwest::Client::builder().timeout(Duration::from_secs(10)).build().expect("valid HTTP client config");
        Self { http, url, last_ping: None }
    }

    /// Tells systemd we've finished starting up.
    pub fn started(&self) {
        // Does nothing when not run by systemd.
        let _ = sd_notify::notify(&[NotifyState::Ready]);
    }

    /// Reports the outcome of one loop. Never fails: a broken heartbeat
    /// shouldn't stop us vending.
    pub async fn report(&mut self, result: &anyhow::Result<()>) {
        let _ = sd_notify::notify(&[NotifyState::Watchdog]);

        let Some(url) = &self.url else { return };
        let healthy = result.is_ok();
        if let Some((was_healthy, at)) = self.last_ping
            && was_healthy == healthy
            && at.elapsed() < PING_EVERY
        {
            return;
        }

        let request = match result {
            Ok(()) => self.http.get(url),
            Err(e) => self.http.post(format!("{url}/fail")).body(format!("{e:#}")),
        };
        match request.send().await.and_then(|response| response.error_for_status()) {
            Ok(_) => self.last_ping = Some((healthy, Instant::now())),
            Err(e) => warn!("Heartbeat ping failed: {e}"),
        }
    }
}

#[cfg(test)]
mod tests {
    use wiremock::{Mock, MockServer, ResponseTemplate, matchers::any};

    use super::*;

    /// Reports a sequence of results, and returns the requests the heartbeat
    /// server received as "METHOD /path body".
    async fn pings_for(results: &[anyhow::Result<()>]) -> Vec<String> {
        let server = MockServer::start().await;
        Mock::given(any()).respond_with(ResponseTemplate::new(200)).mount(&server).await;

        let mut health = Health::new(Some(format!("{}/check", server.uri())));
        for result in results {
            health.report(result).await;
        }

        let requests = server.received_requests().await.unwrap();
        requests.iter().map(|r| format!("{} {} {}", r.method, r.url.path(), String::from_utf8_lossy(&r.body))).collect()
    }

    #[tokio::test]
    async fn pings_when_health_changes_and_otherwise_once_a_minute() {
        let down = || Err(anyhow::anyhow!("Can't reach Lexe"));
        let pings = pings_for(&[
            Ok(()),
            Ok(()), // Too soon after the last ping: not sent.
            down(), // Health changed: sent.
            down(), // Too soon: not sent.
            Ok(()), // Recovered: sent.
        ])
        .await;
        assert_eq!(pings, ["GET /check ", "POST /check/fail Can't reach Lexe", "GET /check "]);
    }

    #[tokio::test]
    async fn no_url_means_no_pings() {
        // Just checks this doesn't panic or hang.
        Health::new(None).report(&Ok(())).await;
    }
}
