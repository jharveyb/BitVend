# BitVend Pi setup

The Pi runs `bitvend`, which receives Lightning payments with the
[Lexe](https://docs.lexe.tech) wallet SDK. For each payment it sends quarters
to the Arduino, one GPIO pulse per quarter.

- **Pricing:** payments are converted at the current BTC/USD price from
  Coinbase, with Kraken as a fallback. The amount is rounded down to whole
  quarters, or up if it's within 2% of the next quarter (at least 2¢, at most
  24¢).
- **Small payments:** payments worth less than about 23¢ get nothing.
- **Bookkeeping:** each handled payment gets a private note in the wallet,
  e.g. `bitvend: vended 4 quarters @ $65432/BTC`. Nothing is vended twice.

Customers pay a **static** address (`₿name@lexe.app` / `name@lexe.app`), so
the QR code on the machine never changes.

## 1. Create the wallet (admin machine, not the Pi)

The wallet's seed phrase can spend all funds, so keep it off the Pi. Install
the Lexe CLI (`lexe`) on a trusted machine. For a test machine, add
`--network testnet3` to every command.

```sh
lexe init                                   # seed is saved to ~/.lexe/seedphrase.txt; back it up!
lexe get-human-bitcoin-address              # the default address, e.g. ₿enjoyable-razorbill@lexe.app
```

Optionally, claim a nicer address. This needs a balance of at least 10,000
sats. You can change it for 24 hours, and after that it's fixed for 90 days.

```sh
lexe update-human-bitcoin-address mybitvend
```

Print the address as a QR code for the machine, e.g. with
`qrencode -o qr.png 'mybitvend@lexe.app'`.

## 2. Create credentials for the Pi

These credentials can see payments, receive, and add notes to payments. They
**cannot spend**.

```sh
lexe create-client --label bitvend \
    --scope read_info --scope receive --scope read_payments \
    --permission update_personal_note --never-expires
```

Put the printed credential string in `/etc/bitvend.env` on the Pi (see
`bitvend.env.example`). If the Pi is lost or stolen, revoke it with
`lexe list-clients` and then `lexe revoke-client`.

**Payments made before the first start are never vended.** The first time
`bitvend` starts, it marks existing payments (such as the funding above) as
`bitvend: skipped, before install`.

## 3. Build and install on the Pi (64-bit Raspberry Pi OS)

Cross-compile on your dev machine with rustup and clang. The settings are
in `.cargo/config.toml`, and the result is a single static binary.

```sh
just cross                       # needs clang + llvm-ar; adds the rustup target if missing
just deploy dci@bitvend.local    # cross-builds, then copies the binary and service files to the Pi
```

To run the tests as aarch64 binaries under qemu (`apt install qemu-user`), use
`just cross-test`. Run `just` to list all recipes. You can also just run
`cargo build --release` on the Pi itself; it's slow, but it works.

On the Pi:

```sh
sudo install bitvend /usr/local/bin/
sudo install -m 600 bitvend.env.example /etc/bitvend.env   # then fill it in
sudo usermod -aG gpio dci
sudo cp bitvend.service /etc/systemd/system/
sudo systemctl enable --now bitvend
journalctl -u bitvend -f                    # logs the payment address, then each payment
```

## 4. Monitoring

If `bitvend` stops working, customers can still pay but get nothing, so set up
an alert:

1. Create a free check at https://healthchecks.io. Use period 5 minutes and
   grace 5 minutes, and add your email or phone.
2. Put its ping URL in `/etc/bitvend.env` as `BITVEND_HEALTHCHECK_URL`, then run
   `sudo systemctl restart bitvend`.

`bitvend` pings it about once a minute while healthy. When something fails
(e.g. Lexe or the price feed is unreachable) it pings `<url>/fail` with the
error, and healthchecks.io alerts you. It also alerts you if pings stop
entirely: the Pi is off, offline, or crash-looping.

Separately, systemd restarts `bitvend` if it crashes, or if it hangs and stops
checking in for 10 minutes (`WatchdogSec` in `bitvend.service`). Check with
`systemctl status bitvend` and `journalctl -u bitvend`.

## 5. Maintenance

- **Keep the wallet up to date.** When Lexe releases a new node version, run
  `lexe provision` on the admin machine. The Pi's credentials can't do this.
- **Check what was vended.** Run `lexe sync-payments && lexe list-payments`
  and look at the `bitvend:` notes.
- **Payments that weren't vended.** A payment is left alone if it arrived
  while the Pi was off for over 24 hours, or if `bitvend` crashed mid-vend.
  Handle it by hand.

## Development and tests

```sh
just test          # unit tests + offline end-to-end tests, no network needed
just run-test      # the real service on the test wallet (testnet3), logging pulses instead of using GPIO
just pay 1000      # in another terminal: the payer wallet pays the machine 1000 sats
```

### Live test (testnet3 or mainnet)

`tests/live.rs` has a "payer" wallet pay the machine's static offer for 600
sats. It then checks that exactly 3 quarters are sent, at a fixed test price.
It needs two **test** wallets, a machine and a payer. Never point it at the
production wallet: on first run it marks recent unhandled payments as skipped.

The test recipes default to testnet3. Add `net=mainnet` to use real bitcoin
instead; mainnet test wallets live in `~/.lexe-bitvend-test/mainnet/`.

```sh
just net=mainnet setup-test-wallets   # creates both wallets + machine credentials; prints the payer's address
# Fund the payer: pay at least 10,000 sats over Lightning to that address from any wallet.
# (Lexe's LSP opens a channel of at least 5,000 sats.)
just net=mainnet test-wallets         # check the payer now has a Lightning balance
just net=mainnet test-live            # the automated end-to-end test
just net=mainnet run-test             # or by hand: run the service with fake GPIO...
just net=mainnet pay 1000             # ...and pay it from another terminal
```

On testnet3, Lightning funding needs a testnet3 Lightning wallet. Otherwise,
fund the payer on-chain from a faucet (`get-next-unused-address`), then run
`open-channel --value-sats <sats>`.
