# bitvend

The Raspberry Pi half of BitVend. `bitvend` receives Lightning payments and
tells the Arduino to insert the matching number of quarters into the vending
machine.

- **Setup, deployment and operations:** see [SETUP.md](SETUP.md).
- **This file:** how the code works, for people changing it.

## The big picture

```text
customer's wallet ──Lightning──▶ Lexe (hosted wallet) ◀──polls── bitvend on the Pi
                                                                     │
                                                    1 pulse per quarter, GPIO
                                                                     ▼
                                  vending machine ◀──fake coin signals── Arduino
```

1. **Payment.** The customer scans a static QR code (`₿name@lexe.app`) and pays
   any amount. The wallet is hosted by [Lexe](https://docs.lexe.tech): the
   Lightning node runs in Lexe's secure enclaves, and we talk to it with their
   Rust SDK (the `lexe` crate).
2. **Price.** `bitvend` notices the payment and converts it to USD at the
   current BTC price.
3. **Rounding.** The value is rounded **up** to whole quarters. Payments under
   25¢ get nothing.
4. **Signal.** For each quarter, `bitvend` pulses a GPIO pin wired to the
   Arduino. On each pulse, the Arduino replays a recorded "a quarter was
   inserted" signal into the vending machine (see `Arduino/platformio/`).

## Code layout

Read the files in this order:

| File | What it does |
| --- | --- |
| `src/main.rs` | Entry point. Reads settings from environment variables, opens the wallet and GPIO pin, then loops forever: wait for a payment, then vend any new payments. |
| `src/vend.rs` | The core logic. Decides which payments still need vending, and records each one as handled. |
| `src/quarters.rs` | Sats + BTC price → number of quarters. A pure function, so it's the easiest place to start. |
| `src/price.rs` | Fetches the BTC/USD price from Coinbase, falling back to Kraken, and caches it. |
| `src/coin_signal.rs` | Pulses the GPIO pin. It also holds the pulse timing limits the Arduino firmware needs. |
| `src/wallet.rs` | The adapter between `vend.rs` and the Lexe SDK. This is the only file that knows Lexe's types. |
| `src/lib.rs` | Lists the modules, so the tests in `tests/` can use them. |
| `tests/vend_flow.rs` | End-to-end tests of the vending logic with a fake wallet, a fixed price and a recording pin. No network needed. |
| `tests/live.rs` | One real end-to-end test: a second wallet pays the machine on testnet3 or mainnet. Ignored by default; run it with `just test-live`. |
| `justfile` | Shortcuts for build, cross-build, tests and deploy. Run `just` to list them. |
| `.cargo/config.toml` | Cross-compiling for the Pi (`aarch64-unknown-linux-musl`). |
| `bitvend.service`, `bitvend.env.example` | systemd unit and configuration template for the Pi. |

## How a payment is handled

`vend::vend_new_payments` runs each time the wallet reports payment activity,
or at least once a minute:

1. List recent completed payments from Lexe's local cache. The SDK keeps the
   cache in sync: `wallet::wait_for_payment` polls Lexe every few seconds and
   syncs every new or updated payment before returning. Writing a note also
   re-syncs the cache. `wallet::sync` only does the first, full sync at
   startup.
2. Keep the ones that are `creditable`: inbound, over Lightning, no `bitvend:`
   note yet, and completed within the last 24 hours.
3. For each one, oldest first:
   1. Look up the price and compute the quarters.
   2. Write a private note on the payment, e.g.
      `bitvend: vended 4 quarters @ $84225/BTC`.
   3. Send the pulses.

### Design decisions worth knowing

- **No database or state files.** The note on each payment, stored by Lexe, is
  the record of what's been vended. Operators can see the notes with
  `lexe list-payments`. Anything without a `bitvend:` note hasn't been handled.
- **Note first, pulses second.** If the Pi crashes between the two, a customer
  can miss out, but a payment can never be vended twice. The operator can
  check the notes and refund by hand.
- **First start skips history.** On a fresh install (empty local cache),
  existing payments are marked `skipped, before install`, so funding the wallet
  never turns into quarters. See `wallet::sync`.
- **24-hour cutoff.** Payments that went unhandled for a day (e.g. the Pi was
  off) are left for the operator rather than vended unexpectedly.
- **Customers are credited what they sent.** Lexe keeps a 0.5% fee from each
  received payment, so `wallet.rs` credits `amount + fees`. Otherwise a payment
  of exactly 25¢ would arrive as 24.875¢ and get nothing.
- **Price outages delay payments rather than losing them.** If no price is
  available (the last good one is reused for up to an hour), the payment stays
  unmarked and is retried on the next loop.
- **Integer math for money.** `quarters_for` compares integers, so values on a
  quarter boundary don't round the wrong way.
- **The Pi can't spend.** It uses Lexe *client credentials* that can only
  receive, read payments and write notes. The seed phrase stays on an admin
  machine (see SETUP.md).

## Pi → Arduino signal

One LOW→HIGH edge on the wire means one quarter. Each quarter is 100 ms HIGH,
then LOW for the rest of the period (200 ms in total by default; set it with
`BITVEND_QUARTER_PERIOD_MS`). The firmware needs at least 60 ms HIGH and 40 ms
LOW; `Timing::MIN_HIGH`, `Timing::MIN_LOW` and their tests enforce this. The
reasoning is in the doc comments in `coin_signal.rs` and in
`Arduino/platformio/README.md`.

## Testing without hardware or money

The code has two seams for swapping in fakes:

- **`PaymentSource`** (in `vend.rs`) is implemented by `LexeWallet`, and by a
  `FakeWallet` in the tests.
- **`PriceSource`** (in `price.rs`) is implemented by `PriceFeed`, and by a
  fixed price in the tests.

The GPIO output is an enum, `coin_signal::Output`:

- `Gpio` is the real pin.
- `Log` only prints. Use it with `BITVEND_GPIO=fake` to run on a laptop.
- `Record` remembers every change, so tests can count the pulses.

```sh
just test          # all offline tests
just cross-test    # the same tests, built for the Pi and run under qemu
just run-test      # the real service on the testnet3 test wallet, logging pulses
just test-live     # real payment between two test wallets (add net=mainnet for mainnet)
```

## Configuration

These are environment variables, set in `/etc/bitvend.env` on the Pi. See
`bitvend.env.example` for the full list:

- `LEXE_CLIENT_CREDENTIALS`
- `BITVEND_NETWORK` (`mainnet` or `testnet3`)
- `LEXE_DATA_DIR`
- `BITVEND_GPIO=fake`
- `BITVEND_GPIO_PIN` (a BCM number; default 18, which is physical pin 12)
- `BITVEND_QUARTER_PERIOD_MS`

## Rust notes for newcomers

- **Errors:** `anyhow::Result` is "a result with any kind of error". `?` returns
  early with the error, and `.context(...)` adds a human-readable explanation.
- **`async` / `.await`:** network calls (Lexe, price APIs) are `async`, and
  `tokio` runs them. Sending pulses deliberately blocks with `sleep`, because
  nothing else needs to happen while quarters go out.
- **`impl Trait` arguments:** `payments: &impl PaymentSource` means "anything
  that implements `PaymentSource`". That is how the tests pass a fake wallet.
- **Formatting and linting:** `just check` runs `rustfmt` (configured in
  `rustfmt.toml`) and `clippy`.
